/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* Guest memory ranges as buffers the GPU reads: vertices, indices and storage buffers. A range is checked once per
   guest submit (and again after the GPU front end wrote it). Data that changed is copied into the frame's staging
   ring, which draws read directly; data that stayed the same over a few checks gets a buffer of its own, which a
   change replaces, so nothing waits for the GPU. A big range's own buffer instead takes the pages written since it
   last matched memory, copied on the GPU in order with the draws (patch_own). Big ranges that have been stable for
   long are checked less often. */
#include <stdlib.h>
#include <string.h>
#include "nx/gpu/gpu.h"
#include "nx/gpu/perf_times.h"
#include "nx/gpu/vk/landing.h"
#include "nx/gpu/renderer.h"
#include "nx/hle/services/nvdrv.h"
#include "nx/memory/mem.h"
#include "nx/memory/own.h"
#include "nx/runtime/options.h"
#include "nx/runtime/platform/log.h"
#include "nx/runtime/platform/plat.h"
#include "nx/runtime/platform/tracy.h"
#include "nx/gpu/vk/vk.h"
#include "nx/gpu/vk/pending_pages.h"
#include "nx/runtime/platform/tracy.h"

typedef struct {
    u64 start, len, hash, shash;
    u32 epoch, strict_epoch, chk_epoch, schk, stable, used;
    u64 gw;    /* GPU-side write generation at the last check */
    u64 cs_adds, gfx_adds; /* vk_compute_adds and vk_buf_adds when pending results were last checked for */
    u64 frame; /* the staging frame buf/off point into, when not own */
    int own;   /* buf is this entry's own buffer */
    u32 wseen; /* write-tracking clock when it last matched memory */
    u32 host_gen; /* g_vb_map_gen when host_cpu was found: 0 never */
    u8 sskip;     /* SNX_ARCEUS_PERF_SAMPLE: the last full check took no sample, shash is older (never set unset) */
    u8 patched;   /* SNX_ARCEUS_PATCH_HASH: a patch copied bytes since the hash, so hash is older (never set unset) */
    u64 host_cpu; /* the guest memory the range shows, ~0 for none (entry_host) */
    VkBuffer buf;
    VmaAllocation mem;
    u64 off;
    u32 own_prev, own_next; /* own buffers, from the least recently used to the most (vk_buf_reclaim); a free entry's
                               next free one in own_next */
} Vb;
/* sskip and patched sit in the padding after host_gen, so the cache's array does not grow (PERF-24, PATCH-HASH) */
_Static_assert(sizeof(void*) != 8 || sizeof(Vb) == 144, "Vb's flags must fill the hole after host_gen");
/* The cache of guest ranges as buffers: its entries in one array by index (0 is none), and an index of them by range,
   open addressed. Entries live twenty seconds after their last use, and draws make new ranges all the time (constant
   buffers streamed through a ring, vertices at new offsets): chained buckets of separately allocated entries grew
   long, and walking them, a cache miss an entry, was a twentieth of the GPU thread in one game. A lookup reads one
   slot of the index, rarely two */
static Vb* g_vb;
static u32 g_nvb, g_capvb, g_vbfree, g_vblive;
typedef struct {
    u64 start;
    u32 len, idx; /* idx 0: an empty slot */
} VbSlot;
static VbSlot* g_vbi;
static u32 g_vbimask; /* the index's size less one, a power of two; at most half its slots are used */
static u64 g_vb_lookups, g_vb_probes;
static int copy_stats_on(void);
static u32 vb_home(u64 start, u32 len) {
    /* every bit of the range, mixed by multiplying: constant buffers are 256-byte aligned and buffers often 4K */
    return (u32)((((start ^ ((u64)len << 40)) * 0x9E3779B97F4A7C15ull) >> 32) & g_vbimask);
}
/* the slot holding the range, or the empty one where it would go */
static u32 vb_slot(u64 start, u32 len) {
    u32 i = vb_home(start, len);
    if (copy_stats_on()) g_vb_lookups++;
    while (g_vbi[i].idx && (g_vbi[i].start != start || g_vbi[i].len != len)) {
        i = (i + 1) & g_vbimask;
        if (copy_stats_on()) g_vb_probes++;
    }
    return i;
}
/* room in the index for one more range: 0 when none could be made */
static int vb_index_room(void) {
    u32 cap = g_vbi ? g_vbimask + 1 : 0, ncap = cap ? cap * 2 : 1u << 16;
    VbSlot *old = g_vbi, *n;
    if (cap && (g_vblive + 1) * 2 <= cap) return 1;
    n = calloc(ncap, sizeof *n);
    if (!n) return cap && g_vblive + 1 < cap; /* fuller than meant, but still a free slot */
    g_vbi = n;
    g_vbimask = ncap - 1;
    for (u32 i = 0; i < cap; i++)
        if (old[i].idx) g_vbi[vb_slot(old[i].start, old[i].len)] = old[i];
    free(old);
    return 1;
}
/* the range's slot emptied, the entries after it in its run moved back over it (no tombstones) */
static void vb_index_remove(u64 start, u32 len) {
    u32 i = vb_slot(start, len), j = i;
    if (!g_vbi[i].idx) return;
    for (;;) {
        g_vbi[i].idx = 0;
        for (;;) {
            j = (j + 1) & g_vbimask;
            if (!g_vbi[j].idx) return;
            u32 h = vb_home(g_vbi[j].start, g_vbi[j].len);
            /* j's entry can move to i when its home isn't in (i, j], cyclically */
            if (i <= j ? (h <= i || h > j) : (h <= i && h > j)) break;
        }
        g_vbi[i] = g_vbi[j];
        i = j;
    }
}
/* a new entry for the range, indexed: its index, 0 out of memory */
static u32 vb_new(u64 start, u32 len) {
    u32 idx;
    if (!vb_index_room()) return 0;
    if (g_vbfree) {
        idx = g_vbfree;
        g_vbfree = g_vb[idx].own_next;
    } else {
        if (g_nvb + 1 >= g_capvb) {
            u32 cap = g_capvb ? g_capvb * 2 : 4096;
            Vb* n = realloc(g_vb, (size_t)cap * sizeof *n);
            if (!n) return 0;
            g_vb = n;
            g_capvb = cap;
            if (!g_nvb) g_nvb = 1; /* entry 0 is none */
        }
        idx = g_nvb++;
    }
    memset(&g_vb[idx], 0, sizeof g_vb[idx]);
    g_vb[idx].start = start;
    g_vb[idx].len = len;
    g_vbi[vb_slot(start, len)] = (VbSlot){start, len, idx};
    g_vblive++;
    return idx;
}
static void vb_free(u32 idx) {
    vb_index_remove(g_vb[idx].start, (u32)g_vb[idx].len);
    g_vb[idx].len = 0; /* free: the sweep skips it */
    g_vb[idx].own_next = g_vbfree;
    g_vbfree = idx;
    g_vblive--;
}
static struct {
    u32 head, tail;
    u32 n;
    u64 bytes;
} g_own;
static void own_link(u32 i) {
    Vb* e = &g_vb[i];
    e->own_next = 0;
    e->own_prev = g_own.tail;
    if (e->own_prev)
        g_vb[e->own_prev].own_next = i;
    else
        g_own.head = i;
    g_own.tail = i;
}
static void own_unlink(u32 i) {
    Vb* e = &g_vb[i];
    if (e->own_prev)
        g_vb[e->own_prev].own_next = e->own_next;
    else
        g_own.head = e->own_next;
    if (e->own_next)
        g_vb[e->own_next].own_prev = e->own_prev;
    else
        g_own.tail = e->own_prev;
}
/* Optional copy telemetry. Counters are updated under vk_lock. */
static u64 g_copy_stage, g_copy_owned, g_copy_uncached, g_copy_zero, g_copy_calls, g_copy_peak, g_read_bound;
static u64 g_wb_outputs, g_wb_bytes, g_wb_waits, g_wb_wait_ns, g_wb_reuses;
static u64 g_alias_draws, g_alias_pages;
enum { WB_DRAW, WB_READ, WB_WRITE, WB_SIGNAL, WB_SYNCWAIT, WB_MAP, WB_REASON_N };
static u64 g_wb_reason[WB_REASON_N], g_wb_reason_ns[WB_REASON_N], g_wb_draw_miss[4];
static int copy_stats_on(void) { return g_settings.buffer_copy_stats; }
/* Why buf_get copied a range into staging: no entry yet, its copy gone, a newer submit than its copy's, a GPU write
   over it, a newer guest submit with nothing written, or (with a buffer of its own) a write the tracking saw. Each by
   whether the bytes had changed, with the copies' sizes and the ranges that copied most */
enum { ST_NEW, ST_NOBUF, ST_SUBMIT, ST_DIRTY, ST_EPOCH, ST_WRITTEN, ST_N };
static u64 g_st_bytes[ST_N][2], g_st_n[ST_N][2], g_st_size[4], g_st_submit0;
static struct {
    u64 gva, bytes, n;
    u32 len;
} g_st_top[64];
static void stage_note(int why, int changed, u64 gva, u64 len) {
    g_st_bytes[why][changed] += len;
    g_st_n[why][changed]++;
    g_st_size[len < 4096 ? 0 : len < (64u << 10) ? 1 : len < (1u << 20) ? 2 : 3] += len;
    u32 h = (u32)((gva * 0x9E3779B97F4A7C15ull) >> 58);
    for (u32 k = 0; k < 64; k++) {
        u32 s = (h + k) & 63;
        if (g_st_top[s].gva && g_st_top[s].gva != gva) continue;
        g_st_top[s].gva = gva;
        g_st_top[s].bytes += len;
        g_st_top[s].n++;
        if (len > g_st_top[s].len) g_st_top[s].len = (u32)len;
        return;
    }
}
static void stage_report(void) {
    static const char* const names[ST_N] = {"new", "no_copy", "submit", "gpu_write", "epoch", "written"};
    char line[1024];
    int at = 0;
    for (int w = 0; w < ST_N; w++)
        at += snprintf(line + at, sizeof line - (size_t)at, " %s=%llu/%lluMB same %llu/%lluMB changed", names[w],
                       (unsigned long long)g_st_n[w][0], (unsigned long long)(g_st_bytes[w][0] >> 20),
                       (unsigned long long)g_st_n[w][1], (unsigned long long)(g_st_bytes[w][1] >> 20));
    LOG("vulkan: staged by reason:%s; by size <4K %lluMB <64K %lluMB <1M %lluMB >=1M %lluMB; %llu submits\n", line,
        (unsigned long long)(g_st_size[0] >> 20), (unsigned long long)(g_st_size[1] >> 20),
        (unsigned long long)(g_st_size[2] >> 20), (unsigned long long)(g_st_size[3] >> 20),
        (unsigned long long)(vk_submit_number() - g_st_submit0));
    at = 0;
    for (int t = 0; t < 6; t++) {
        int best = -1;
        for (int s = 0; s < 64; s++)
            if (g_st_top[s].bytes && (best < 0 || g_st_top[s].bytes > g_st_top[best].bytes)) best = s;
        if (best < 0) break;
        at += snprintf(line + at, sizeof line - (size_t)at, " %llx+%x:%llux/%lluMB", (unsigned long long)g_st_top[best].gva,
                       g_st_top[best].len, (unsigned long long)g_st_top[best].n,
                       (unsigned long long)(g_st_top[best].bytes >> 20));
        g_st_top[best].bytes = 0;
    }
    LOG("vulkan: staged most (address+largest length:copies/bytes):%s\n", line);
    memset(g_st_bytes, 0, sizeof g_st_bytes);
    memset(g_st_n, 0, sizeof g_st_n);
    memset(g_st_size, 0, sizeof g_st_size);
    memset(g_st_top, 0, sizeof g_st_top);
    g_st_submit0 = vk_submit_number();
}
static void copy_note(u64 gva, u64 len) {
    if (!copy_stats_on()) return;
    g_copy_uncached += len;
    g_copy_calls++;
    if (len > g_copy_peak) g_copy_peak = len;
    if (!gva) g_copy_zero += len;
}

typedef struct GfxOut {
    u64 addr, frame, frame_id; /* the submit that recorded it, and its frame (whose staging holds buf) */
    u32 size;
    VkBuffer buf;
    u64 off;
    VmaAllocation mem;
    u8* ptr;
    u8* org;
    /* GPU-owned input snapshotted before shader writes, read only at the frame fence. */
    u8* initial;
    VmaAllocation initial_mem;
    u64* pages; /* sorted guest CPU pages behind this GPU virtual range */
    u64* vmap;  /* the same pages in the range's order, ~0 for an unmapped one */
    u32 npages;
    /* earlier pending results were copied into buf on the GPU when it was made (gfx_overlay, atlas_fill): org holds
       guest memory without them until each lands (out_rebase) */
    int composed;
    u32 wseen; /* the write tracking clock before org was read (vk_ww_now): copyable_span skips comparing pages since */
    /* SNX_ARCEUS_PERF_COMPARE=2 (PERF-28 3): a composed result's places in the walk's index (walk_add), its order
       among the results and the stamp of the last landing that found it there */
    struct WalkRef* wrefs; /* its first, the rest through each one's own */
    u32 wdepth; /* the depth of the vk_buf_frame_done whose list holds it, 0 in g_out or g_prep */
    u64 seq, wstamp;
    u64 gw0; /* PERF-35 (PERF_PROBES): the write generation before org was read */
    u8 in_out; /* in g_out (from its commit until a landing takes it out): what gfx_gpu_src's walks go over */
    struct GfxOut* next;
} GfxOut;
static GfxOut *g_prep, *g_prep_tail, *g_out, *g_out_tail;
/* SNX_ARCEUS_PERF_TIMES (PERF-11): the draw that last bound a result for writing, by result in a fixed table, to
   count gfx_gpu_src's barriers that no write since the barrier before needed. A slot another result took: unknown */
#define WMARK_N 1024u
static struct {
    const GfxOut* o;
    u64 work;
} g_wmark[WMARK_N];
static u32 wmark_slot(const GfxOut* o) {
    return (u32)((u64)(uintptr_t)o * 0x9E3779B97F4A7C15ull >> 54) & (WMARK_N - 1);
}
static void wmark_set(const GfxOut* o) {
    if (!g_perf_times) return;
    const u32 i = wmark_slot(o);
    g_wmark[i].o = o;
    g_wmark[i].work = g_vk_drawn + g_vk_dispatches; /* the draw or dispatch being prepared */
}
/* the guest pages the results in g_out cover, so a read or write that meets none (almost all) skips the walk over
   them: each adds its own translated pages when it is committed and removes them when it lands. A range that meets a
   result only at the same GPU address through an unmapped page skips it too: nothing is read or written there. A GMMU
   change (WB_MAP) still checks every result, so one over a page about to be mapped lands first */
static PendPages g_out_pages = {.also = &g_pend_any};
static void out_pages(const GfxOut* o, int delta) {
    for (u32 i = 0, j; i < o->npages && o->pages[i] != ~0ull; i = j) { /* in runs of consecutive pages, sorted */
        for (j = i + 1; j < o->npages && o->pages[j] == o->pages[j - 1] + 1; j++) continue;
        pend_add_pages(&g_out_pages, o->pages[i], o->pages[j - 1], delta);
    }
}
/* where and how pending results were written: shader stores, copies, and transform feedback's captures when the device
   has it (its flags are invalid without the extension) */
static VkPipelineStageFlags2 out_stages(void) {
    return VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
           VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_COPY_BIT |
           (g_vk.has_xfb ? VK_PIPELINE_STAGE_2_TRANSFORM_FEEDBACK_BIT_EXT : 0);
}
static VkAccessFlags2 out_access(void) {
    return VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT |
           (g_vk.has_xfb ? VK_ACCESS_2_TRANSFORM_FEEDBACK_WRITE_BIT_EXT : 0);
}
/* an output with its page lists and its original bytes after it, in one block */
/* a result's block: the result, its two page lists and its original bytes (PERF-31 counts it at its free too) */
static u64 out_block(u32 npages, u32 len) { return sizeof(GfxOut) + 2 * (u64)npages * sizeof(u64) + len; }
/* SNX_ARCEUS_PERF_SMALL bit 2 (PERF-34): the block is not zeroed past the result itself. Both callers write its page
   lists whole (out_map, or the 4 KB path's one page) and its original bytes (owned ranges zero them) before any read */
/* PERF-36G: a result call made without vk_lock, counted by entry (log only, with the probes) */
static void unlocked_note(int k) {
    if (g_perf_probes && !vk_lock_depth()) __atomic_fetch_add(&g_perf_vkx.unlocked[k], 1, __ATOMIC_RELAXED);
}
static GfxOut* gfx_out_new(u32 npages, u32 len) {
    unlocked_note(VKU_OUT_NEW);
    const u64 a0 = g_perf_probes ? plat_ns() : 0;
    GfxOut* o = g_perf_small & PSM_NOZERO ? malloc((size_t)out_block(npages, len))
                                          : calloc(1, (size_t)out_block(npages, len));
    if (!o) return NULL;
    if (g_perf_small & PSM_NOZERO) memset(o, 0, sizeof *o);
    if (a0) perf_wait_note(PW_ALLOC, NULL, plat_ns() - a0); /* PERF-34: the block made, zeroed or not */
    if (g_perf_probes) perf_mem_alloc(&g_perf_mem, out_block(npages, len) >= 4096 ? PMA_RESULT : PMA_RESULT_SMALL,
                                      out_block(npages, len));
    if (g_perf_probes) o->gw0 = gpu_mem_generation(); /* PERF-35: before its caller reads org */
    o->pages = (u64*)(o + 1);
    o->vmap = o->pages + npages;
    o->npages = npages;
    o->org = (u8*)(o->vmap + npages);
    o->wseen = vk_ww_now(); /* before its caller reads org: a write in between only makes copyable_span compare */
    return o;
}
static int page_cmp(const void* a, const void* b);
/* o's page lists for its range at gva, as the GMMU maps it now */
static void out_map(GfxOut* o, u64 gva) {
    for (u32 i = 0; i < o->npages; i++) {
        u64 cpu = gmmu_translate((gva & ~0xFFFull) + (u64)i * 0x1000);
        o->vmap[i] = o->pages[i] = cpu ? cpu >> 12 : ~0ull;
    }
    qsort(o->pages, o->npages, sizeof *o->pages, page_cmp);
}
/* The bytes of [gva, gva + len) that are bytes of o: the same guest memory, through the same GPU address or another
   one mapping the same pages (an unmapped page only through the same address). rmap is the range's pages
   (range_map). fn(user, at, from, n) gets each run: n bytes at gva + at that are o's bytes at o->addr + from */
typedef void (*SpanFn)(void* user, u64 at, u64 from, u64 n);
/* the run of R's page gr ([ra, rb) of it) that o's page j holds, if any */
static void out_span_page(const GfxOut* o, u64 j, u64 gva, u64 gr, u64 ra, u64 rb, SpanFn fn, void* user) {
    u64 go = (o->addr & ~4095ull) + j * 4096, oend = o->addr + o->size;
    u64 oa = go > o->addr ? 0 : o->addr - go, ob = go + 4096 < oend ? 4096 : oend - go;
    u64 a = ra > oa ? ra : oa, b = rb < ob ? rb : ob;
    if (a < b) fn(user, gr + a - gva, go + a - o->addr, b - a);
}
static void out_spans(const GfxOut* o, u64 gva, u64 len, const u64* rmap, SpanFn fn, void* user) {
    u64 rbase = gva & ~4095ull, rend = gva + len, obase = o->addr & ~4095ull, n = o->npages;
    for (u64 i = 0, gr = rbase; gr < rend; i++, gr += 4096) {
        u64 ra = gr > gva ? 0 : gva - gr, rb = gr + 4096 < rend ? 4096 : rend - gr, page = rmap[i];
        u64 same = gr >= obase && gr - obase < n * 4096 ? (gr - obase) >> 12 : n; /* o's page at this address */
        const u64* hit;
        if (page == ~0ull) { /* unmapped: only o's page at the same address, if that is unmapped too */
            if (same < n && o->vmap[same] == ~0ull) out_span_page(o, same, gva, gr, ra, rb, fn, user);
            continue;
        }
        hit = bsearch(&page, o->pages, n, sizeof page, page_cmp);
        if (!hit) continue;
        if ((hit == o->pages || hit[-1] != page) && (hit + 1 == o->pages + n || hit[1] != page) && same < n &&
            o->vmap[same] == page) {
            out_span_page(o, same, gva, gr, ra, rb, fn, user); /* o holds the page once, at this address */
            continue;
        }
        for (u64 j = 0; j < n; j++) /* elsewhere in o, or more than once */
            if (o->vmap[j] == page) out_span_page(o, j, gva, gr, ra, rb, fn, user);
    }
}
/* the guest CPU page behind each GPU page of [gva, gva + len), ~0 for an unmapped one: in local when 16 do, malloc'd
   otherwise (NULL out of memory) */
static u64* range_map(u64 gva, u64 len, u64 local[16]) {
    u64 base = gva & ~4095ull, n = (gva + len - base + 4095) >> 12;
    u64* map = n <= 16 ? local : malloc(n * sizeof *map);
    if (!map) return NULL;
    if (g_perf_probes && n * sizeof *map >= 4096) perf_mem_alloc(&g_perf_mem, PMA_RANGE_MAP, n * sizeof *map);
    for (u64 i = 0; i < n; i++) {
        u64 cpu = gmmu_translate(base + i * 4096);
        map[i] = cpu ? cpu >> 12 : ~0ull;
    }
    return map;
}

/* Suppress recursive lookups by the landing thread. Other readers still take vk_lock and wait for landing. */
static _Thread_local int g_out_landing;

static void walk_remove(GfxOut* o);
void vk_buf_writable_abort(void) {
    while (g_prep) {
        GfxOut* o = g_prep;
        g_prep = o->next;
        walk_remove(o);
        if (g_perf_probes) perf_mem_free(&g_perf_mem, g_perf_release_thread, out_block(o->npages, o->size));
        free(o);
    }
    g_prep_tail = NULL;
}
/* SNX_ARCEUS_PERF_INPLACE=3: the work index (g_vk_drawn + g_vk_dispatches) of the last draw or dispatch handed a
   writable result, new or rewritten in place, through any path (astra 02:46: the atlas's outputs too). A read skips
   the reuse barrier only when that index is below the last barrier's */
static u64 g_out_write_work;
static void out_wrote(void) {
    const u64 work = g_vk_drawn + g_vk_dispatches;
    if (g_perf_inplace >= 3 && work > g_out_write_work) g_out_write_work = work;
}
void vk_buf_writable_commit(void) {
    unlocked_note(VKU_COMMIT);
    if (!g_prep) return;
    out_wrote(); /* every output a draw or dispatch prepared: copies of writable ranges, the atlas's pages */
    for (GfxOut* o = g_prep; o; o = o->next) {
        out_pages(o, 1);
        if (g_perf_compare == 2) o->in_out = 1; /* gfx_gpu_src's walks through the index */
        if (g_perf_probes) vk_release_result(o->frame, PCY_OUT); /* the release thread lands it (PERF-33) */
    }
    if (g_out_tail)
        g_out_tail->next = g_prep;
    else
        g_out = g_prep;
    g_out_tail = g_prep_tail;
    g_prep = g_prep_tail = NULL;
    g_gfx_any = 1;
}
/* whether two sorted page lists share a page */
static int pages_meet(const GfxOut* a, const GfxOut* b) {
    u32 i = 0, j = 0;
    while (i < a->npages && j < b->npages) {
        if (a->pages[i] == ~0ull || b->pages[j] == ~0ull) return 0; /* unmapped pages sort last */
        if (a->pages[i] < b->pages[j])
            i++;
        else if (a->pages[i] > b->pages[j])
            j++;
        else
            return 1;
    }
    return 0;
}
/* SNX_ARCEUS_PERF_COMPARE=2 (PERF-28 3): the results not landed yet, by their mapped pages and once under ~0 when
   one has an unmapped page, so that a landing (composed ones) and gfx_gpu_src (those in g_out) visit the ones on
   their own pages, not every pending result. A result is in it from its append to g_prep until it leaves the lists,
   landed or dropped. g_walk_lost: no memory to add one, and the whole walks from then on */
typedef struct WalkRef {
    GfxOut* o;
    u64 page;
    struct WalkRef *next, **pprev; /* its bucket's */
    struct WalkRef* own;           /* the result's next one, or the free list's */
} WalkRef;
#define WALK_BUCKETS 4096u
static WalkRef* g_walk[WALK_BUCKETS];
/* the entries not in use, from blocks of WALK_BLOCK kept for the session: no allocation a result. Every add and
   removal is under vk_lock (all recording is, renderer.c, and so are landings), so the list needs no lock */
#define WALK_BLOCK 1024u
static WalkRef* g_walk_free;
static WalkRef* walk_node(void) {
    if (!g_walk_free) {
        WalkRef* b = malloc(WALK_BLOCK * sizeof *b);
        if (!b) return NULL;
        for (u32 i = 0; i < WALK_BLOCK; i++) b[i].own = g_walk_free, g_walk_free = &b[i];
    }
    WalkRef* r = g_walk_free;
    g_walk_free = r->own;
    return r;
}
static int g_walk_lost;
static u32 g_walk_depth; /* vk_buf_frame_done calls in progress, nested in a write's flush, under vk_lock */
static u64 g_walk_seq, g_walk_stamp;
static WalkRef** walk_bucket(u64 page) {
    return &g_walk[(u32)(page * 0x9E3779B97F4A7C15ull >> 52) & (WALK_BUCKETS - 1)];
}
/* o under page, one entry more in its own chain: 0 without memory */
static int walk_ref(GfxOut* o, u64 page) {
    WalkRef *r = walk_node(), **b = walk_bucket(page);
    if (!r) return 0;
    r->o = o, r->page = page, r->next = *b, r->pprev = b, r->own = o->wrefs;
    if (*b) (*b)->pprev = &r->next;
    *b = r;
    o->wrefs = r;
    return 1;
}
/* no memory for an entry: the walks go over every pending result for the rest of the session, said once (reviewer
   N2 on ce3c2804) */
static void walk_lost(void) {
    g_walk_lost = 1;
    LOG_LIMIT(1, "perf-compare: no memory for the page index, the walks go over every pending result from now on\n");
}
/* at o's append to g_prep, when its pages are final (sorted, unmapped ones last): its order, and it into the index */
static void walk_add(GfxOut* o) {
    if (g_perf_compare != 2) return;
    o->seq = ++g_walk_seq;
    if (g_walk_lost) return;
    u32 i = 0;
    for (; i < o->npages && o->pages[i] != ~0ull; i++) /* the entries made stay o's until it leaves the lists */
        if ((!i || o->pages[i] != o->pages[i - 1]) && !walk_ref(o, o->pages[i])) {
            walk_lost();
            return;
        }
    if (i < o->npages && !walk_ref(o, ~0ull)) walk_lost(); /* an unmapped page */
}
/* o out of the index, as it leaves the lists */
static void walk_remove(GfxOut* o) {
    for (WalkRef *r = o->wrefs, *own; r; r = own) {
        own = r->own;
        *r->pprev = r->next;
        if (r->next) r->next->pprev = r->pprev;
        r->own = g_walk_free;
        g_walk_free = r;
    }
    o->wrefs = NULL;
}
/* e has landed: a later composed result o read e's result where they share guest memory, not the bytes org holds
   there, so the bytes e changed become o's original bytes. Then o lands only what its own draw changed, and doesn't
   take e's landing for a guest write and drop its own writes over it (vk_write_changed). Either GPU range may map
   the shared pages at other addresses: they meet through the GMMU, whose mappings stay put while results are pending
   (a mapping change lands them first) */
/* The runs of the landing result's bytes its draw changed, its 8-byte words that differ from its original ones,
   merged: found once per landing (out_changed_runs). Rebasing every later composed result compared all of the landed
   result's bytes again for each of them. One game landed 20,000 results in 10 s that changed 4 MB of their 590, and
   the rebases read 42 GB, most of the release thread, under vk_lock, which the GPU thread waits for */
static ChgRun g_chg_first[64]; /* where the runs start, so even a failed allocation has room for one */
static ChgRun* g_chg = g_chg_first;
static u32 g_nchg, g_capchg = 64;
/* whether e's w bytes at k differ from its original ones */
static int word_changed(const GfxOut* e, u32 k, u32 w) {
    /* SNX_ARCEUS_PERF_LANDING: a whole word in one compare, not a library call */
    return g_perf_landing ? landing_differs(e->ptr + k, e->org + k, w) : memcmp(e->ptr + k, e->org + k, w) != 0;
}
/* SNX_ARCEUS_PERF_COMPARE=2 (PERF-28 1b): the result whose changed runs its own write put in g_chg (chg_put), until
   out_changed_runs takes them. g_chg_gen counts every filling of g_chg, so that a landing nested in the write (a
   render target it flushes) shows. g_chg_lost: no memory for a run, so every byte is a candidate */
static const GfxOut* g_chg_for;
static u32 g_chg_gen;
static int g_chg_lost;
static void chg_put(void* ctx, u32 from, u32 n) {
    (void)ctx;
    if (g_chg_lost) return;
    if (g_nchg == g_capchg) {
        const u32 cap = g_capchg * 2;
        ChgRun* b = g_chg == g_chg_first ? malloc((size_t)cap * sizeof *b) : realloc(g_chg, (size_t)cap * sizeof *b);
        if (!b) {
            g_chg_lost = 1; /* every byte a candidate for this result, said once (reviewer N2 on ce3c2804) */
            LOG_LIMIT(1, "perf-compare: no memory for a result's changed runs, its rebase compares all its bytes\n");
            return;
        }
        if (g_chg == g_chg_first) memcpy(b, g_chg_first, sizeof g_chg_first);
        g_chg = b;
        g_capchg = cap;
    }
    g_chg[g_nchg++] = (ChgRun){from, from + n};
}
/* e's changed runs into g_chg: 0 when none, or with no memory for them (every byte is a candidate then, one run) */
static u32 out_changed_runs(const GfxOut* e) {
    u32 k = 0;
    if (g_chg_for == e) { /* found by e's write, from the same bytes: nothing changed e's since (PERF-28 1b) */
        g_chg_for = NULL;
        if (g_chg_lost) g_chg[0] = (ChgRun){0, e->size}, g_nchg = 1;
        return g_nchg;
    }
    g_chg_for = NULL;
    g_chg_gen++;
    g_nchg = 0;
    while (k < e->size) {
        u32 w = e->size - k < 8 ? e->size - k : 8;
        if (word_changed(e, k, w)) {
            if (g_nchg && g_chg[g_nchg - 1].to == k)
                g_chg[g_nchg - 1].to = k + w;
            else {
                if (g_nchg == g_capchg) {
                    u32 cap = g_capchg * 2;
                    ChgRun* b = g_chg == g_chg_first ? malloc((size_t)cap * sizeof *b)
                                                     : realloc(g_chg, (size_t)cap * sizeof *b);
                    if (!b) {
                        g_chg[0] = (ChgRun){0, e->size};
                        return g_nchg = 1;
                    }
                    if (g_chg == g_chg_first) memcpy(b, g_chg_first, sizeof g_chg_first);
                    g_chg = b;
                    g_capchg = cap;
                }
                g_chg[g_nchg++] = (ChgRun){k, k + w};
            }
        }
        k += w;
    }
    return g_nchg;
}
typedef struct {
    const GfxOut* e;
    GfxOut* o;
} Rebase;
/* the changed bytes of e's [from, from + n) into o's original bytes at at. Eight bytes at a time: a result is mostly
   the bytes it read, and a byte loop here was 84% of the release thread's time in one game */
static void rebase_bytes(const Rebase* r, u64 at, u64 from, u64 n) {
    const u8 *res = r->e->ptr + from, *org = r->e->org + from;
    u8* dst = r->o->org + at;
    u64 k = 0;
    for (; k + 8 <= n; k += 8) {
        u64 a, b, d, m;
        memcpy(&a, res + k, 8);
        memcpy(&b, org + k, 8);
        if (a == b) continue;
        memcpy(&d, dst + k, 8);
        m = a ^ b; /* the low bit of each byte becomes the OR of its byte's bits, then the byte becomes 0 or 0xFF */
        m |= m >> 4;
        m |= m >> 2;
        m |= m >> 1;
        m = (m & 0x0101010101010101ull) * 0xFF;
        d = (d & ~m) | (a & m);
        memcpy(dst + k, &d, 8);
    }
    for (; k < n; k++)
        if (res[k] != org[k]) dst[k] = res[k];
}
/* a span of e in o: only where it meets e's changed runs (g_chg, sorted), the first found by binary search */
static void rebase_span(void* user, u64 at, u64 from, u64 n) {
    u32 lo = 0, hi = g_nchg;
    while (lo < hi) {
        u32 mid = lo + (hi - lo) / 2;
        if (g_chg[mid].to <= from)
            lo = mid + 1;
        else
            hi = mid;
    }
    for (u32 i = lo; i < g_nchg && g_chg[i].from < from + n; i++) {
        u64 a = g_chg[i].from > from ? g_chg[i].from : from, b = g_chg[i].to < from + n ? g_chg[i].to : from + n;
        rebase_bytes(user, at + (a - from), a, b - a);
    }
}
static void out_rebase(const GfxOut* e, GfxOut* o) {
    Rebase r = {e, o};
    out_spans(e, o->addr, o->size, o->vmap, rebase_span, &r);
}
static GfxOut** g_wcand;
static u32 g_capwcand;
/* the candidates one lookup of the index collects at most (research 02:32): a key with more (results with unmapped
   pages under ~0, or many on one page) would cost each walk a list and a sort longer than the walk the index saves,
   so past it that call walks as without the index. Noted with SNX_ARCEUS_PERF_TIMES (PERF-29's line) */
#define WALK_BOUND 64u
static void walk_noted(int which, int fell_back, u32 n) {
    if (g_perf_probes) perf_index_note(&g_perf_p29, which, fell_back, n);
}
static int seq_cmp(const void* a, const void* b) {
    const u64 x = (*(GfxOut* const*)a)->seq, y = (*(GfxOut* const*)b)->seq;
    return (x > y) - (x < y);
}
/* SNX_ARCEUS_PERF_COMPARE=2 (PERF-28 3): out_rebase_later's walk through the index: each composed result on e's
   mapped pages once, in the order they were made. The ones the whole walk meets: e left the lists and the index
   together, and a result in an outer landing's list (wdepth below this one's) is in none of the walk's lists. 0,
   with nothing rebased, without memory for the list */
static int walk_index(const GfxOut* e, u64* walk) {
    const u64 stamp = ++g_walk_stamp;
    u32 n = 0;
    for (u32 i = 0; i < e->npages && e->pages[i] != ~0ull; i++)
        for (WalkRef* r = *walk_bucket(e->pages[i]); r; r = r->next) {
            if (r->page != e->pages[i] || r->o->wstamp == stamp || !r->o->composed) continue;
            if (r->o->wdepth && r->o->wdepth != g_walk_depth) continue;
            if (n == WALK_BOUND) {
                walk_noted(1, 1, n + 1);
                return 0;
            }
            if (n == g_capwcand) {
                const u32 cap = g_capwcand ? g_capwcand * 2 : 64;
                GfxOut** b = realloc(g_wcand, (size_t)cap * sizeof *b);
                if (!b) return 0;
                g_wcand = b;
                g_capwcand = cap;
            }
            r->o->wstamp = stamp;
            g_wcand[n++] = r->o;
        }
    walk_noted(1, 0, n);
    if (n > 1) qsort(g_wcand, n, sizeof *g_wcand, seq_cmp);
    for (u32 i = 0; i < n; i++) {
        if (walk) walk[PCW_VISITED]++, walk[PCW_COMPOSED]++, walk[PCW_MET]++;
        out_rebase(e, g_wcand[i]);
    }
    return 1;
}
/* the composed results recorded after e, in this frame's landed list (rest), later frames' and the draw being set up */
/* SNX_ARCEUS_PERF_TIMES (PERF-27): with walk set, *runs_ns gets the clock after the changed runs and walk the counts
   (PCW_*) */
static void out_rebase_later(const GfxOut* e, GfxOut* rest, u64* runs_ns, u64* walk) {
    GfxOut* lists[3] = {rest, g_out, g_prep};
    const int found = g_chg_for == e;
    const u32 runs = out_changed_runs(e);
    if (walk) *runs_ns = plat_ns(), walk[PCW_RUNS] += runs, walk[PCW_FOUND] += found;
    if (!runs) return; /* it changed nothing: memory has what every later one took as original */
    if (g_perf_compare == 2 && !g_walk_lost && walk_index(e, walk)) return;
    for (int l = 0; l < 3; l++)
        for (GfxOut* o = lists[l]; o; o = o->next) {
            if (walk) walk[PCW_VISITED]++, walk[PCW_COMPOSED] += o->composed;
            if (o->composed && pages_meet(e, o)) {
                if (walk) walk[PCW_MET]++;
                out_rebase(e, o);
            }
        }
}

/* SNX_ARCEUS_PERF_TIMES (PERF-27): the time since *t into ns[step], and *t moved on */
static void cap_step(u64* t, u64* ns, int step) {
    const u64 now = plat_ns();
    ns[step] += now - *t;
    *t = now;
}
/* The frame fence has signaled. Its stage allocations still exist here; cmd.c frees them only after this call. */
static int g_ww;
static int written_since_gva(u64 gva, u64 len, u32 since);
/* PERF-35 (research 13:57, PERF_PROBES): before a landing's writes, each result's bytes counted by page against the
   three tests a plain copy would need: no CPU write on the page since org was read, no other GPU-side write over
   them since, no composed result on the page (itself included). Then the loop gets one bracket, and after it a plain
   copy of the same bytes into a scratch block gets one. The counts walk pages, never inside the bracket */
static const u8** g_land_ptr;
static u32* g_land_len;
static u32 g_land_n, g_land_cap;
static u8* g_land_scratch;
static u64 g_land_scratch_cap;
static int land_composed_on(const GfxOut* o, u64 page) {
    for (const WalkRef* r = *walk_bucket(page); r; r = r->next)
        if (r->page == page && r->o != o && r->o->composed) return 1;
    return 0;
}
static void land_count(GfxOut* done) {
    g_land_n = 0;
    for (GfxOut* o = done; o; o = o->next) {
        __atomic_fetch_add(&g_perf_handoff.land_results, 1, __ATOMIC_RELAXED);
        const u64 base = o->addr & ~4095ull, end = o->addr + o->size;
        for (u32 i = 0; i < o->npages; i++) {
            const u64 pa = base + (u64)i * 4096 > o->addr ? base + (u64)i * 4096 : o->addr;
            const u64 pb = base + (u64)(i + 1) * 4096 < end ? base + (u64)(i + 1) * 4096 : end;
            if (pa >= pb) continue;
            const u64 n = pb - pa, cpu = o->vmap[i];
            __atomic_fetch_add(&g_perf_handoff.land_bytes[0], n, __ATOMIC_RELAXED);
            const int ww = cpu == ~0ull ? 0 : written_since_gva(pa, n, o->wseen);
            const int told = ww >= 0 && cpu != ~0ull && g_perf_compare == 2 && !g_walk_lost;
            if (!told) {
                __atomic_fetch_add(&g_perf_handoff.land_unknown, n, __ATOMIC_RELAXED);
                continue;
            }
            const int a = !ww, b = !gpu_mem_dirty_since(o->gw0, pa, n), c = !o->composed && !land_composed_on(o, cpu);
            if (a) __atomic_fetch_add(&g_perf_handoff.land_bytes[1], n, __ATOMIC_RELAXED);
            if (b) __atomic_fetch_add(&g_perf_handoff.land_bytes[2], n, __ATOMIC_RELAXED);
            if (c) __atomic_fetch_add(&g_perf_handoff.land_bytes[3], n, __ATOMIC_RELAXED);
            if (a && b && c) __atomic_fetch_add(&g_perf_handoff.land_bytes[4], n, __ATOMIC_RELAXED);
        }
        if (g_land_n == g_land_cap) {
            const u32 cap = g_land_cap ? g_land_cap * 2 : 1024;
            const u8** p = realloc(g_land_ptr, cap * sizeof *p);
            u32* l = realloc(g_land_len, cap * sizeof *l);
            if (p) g_land_ptr = p;
            if (l) g_land_len = l;
            if (!p || !l) continue;
            g_land_cap = cap;
        }
        g_land_ptr[g_land_n] = o->ptr, g_land_len[g_land_n] = o->size, g_land_n++;
    }
}
/* the plain copy, after the loop: the same bytes, now warm, so a lower bound on a cold copy */
static void land_copy(void) {
    const u64 t0 = plat_ns();
    for (u32 i = 0; i < g_land_n; i++) {
        if (g_land_len[i] > g_land_scratch_cap) {
            u8* s = realloc(g_land_scratch, g_land_len[i]);
            if (!s) continue;
            g_land_scratch = s, g_land_scratch_cap = g_land_len[i];
        }
        memcpy(g_land_scratch, g_land_ptr[i], g_land_len[i]);
    }
    __atomic_fetch_add(&g_perf_handoff.land_copy_ns, plat_ns() - t0, __ATOMIC_RELAXED);
    g_land_n = 0;
}
void vk_buf_frame_done(u64 frame) {
    GfxOut *done = NULL, *done_tail = NULL;
    for (GfxOut** pp = &g_out; *pp;) {
        GfxOut* o = *pp;
        if (o->frame != frame) {
            pp = &o->next;
            continue;
        }
        *pp = o->next;
        o->next = NULL;
        if (g_perf_compare == 2) o->wdepth = g_walk_depth + 1, o->in_out = 0; /* in this call's list, out of g_out */
        if (done_tail)
            done_tail->next = o;
        else
            done = o;
        done_tail = o;
    }
    g_out_tail = NULL;
    for (GfxOut* o = g_out; o; o = o->next) g_out_tail = o;
    g_gfx_any = g_out != NULL || done != NULL;
    VmaAllocation invalidated = VK_NULL_HANDLE;
    g_out_landing++;
    if (g_perf_compare == 2) g_walk_depth++;
    const int land_probe = g_perf_probes && g_out_landing == 1 && done; /* PERF-35, the outermost landing only */
    if (land_probe) land_count(done);
    const u64 loop0 = land_probe ? plat_ns() : 0;
    while (done) {
        GfxOut* o = done;
        done = o->next;
        walk_remove(o); /* it leaves the lists: no walk meets it now, a nested landing's neither */
        u64 t = g_perf_times ? plat_ns() : 0, ns[PCL_STEPS] = {0}, walk[PCW_N] = {0}, runs_t = 0; /* PERF-27 */
        if (o->mem != invalidated) {
            if (!VK_OK(vmaInvalidateAllocation(g_vk.vma, o->mem, 0, VK_WHOLE_SIZE)))
                fatal("Could not invalidate a graphics storage buffer after its frame completed");
            invalidated = o->mem;
        }
        if (o->initial) {
            if (!VK_OK(vmaInvalidateAllocation(g_vk.vma, o->initial_mem, 0, VK_WHOLE_SIZE)))
                fatal("Could not invalidate a graphics storage buffer baseline");
            memcpy(o->org, o->initial, o->size);
            o->initial = NULL;
        }
        if (t) cap_step(&t, ns, PCL_INVALIDATE);
        if (g_perf_compare == 2) { /* 1b: the write's own pass finds the changed runs, unless a landing nested */
            const u32 gen = ++g_chg_gen; /* every filling of g_chg counts, a nested one of this kind too */
            g_nchg = 0;
            g_chg_lost = 0;
            if (vk_write_changed_found(o->addr, o->ptr, o->org, o->size, chg_put, NULL) && g_chg_gen == gen)
                g_chg_for = o;
        } else
            vk_write_changed(o->addr, o->ptr, o->org, o->size);
        if (t) cap_step(&t, ns, PCL_WRITE);
        out_pages(o, -1);
        if (t) cap_step(&t, ns, PCL_PAGES);
        out_rebase_later(o, done, &runs_t, t ? walk : NULL);
        if (t) {
            ns[PCL_RUNS] += runs_t - t;
            t = runs_t;
            cap_step(&t, ns, PCL_WALK);
        }
        if (copy_stats_on()) {
            g_wb_outputs++;
            g_wb_bytes += o->size;
        }
        if (g_perf_times) {
            perf_count(&g_perf, PC_WB_OUTPUTS, 1);
            perf_count(&g_perf, PC_WB_BYTES, o->size);
            /* PERF-25: its own guest writes taken into write tracking first, so that only a later one shows */
            if (g_ww > 0) (void)written_since_gva(o->addr, o->size, vk_ww_now());
            perf_landed_add(g_perf_landed, o->addr, o->size, vk_frame_id(), o->frame_id, vk_submit_number(),
                            gpu_mem_generation(), vk_ww_now()); /* PERF-22, under vk_lock */
            cap_step(&t, ns, PCL_OTHER);
            perf_caps_landed(&g_perf_caps, g_perf_release_thread, ns, walk);
        }
        if (g_perf_probes) {
            perf_mem_free(&g_perf_mem, g_perf_release_thread, out_block(o->npages, o->size));
            perf_sub_landed(&g_perf, o->frame, plat_ns()); /* PERF-33: a result of that submission */
        }
        free(o);
    }
    if (land_probe) {
        __atomic_fetch_add(&g_perf_handoff.land_loop_ns, plat_ns() - loop0, __ATOMIC_RELAXED);
        land_copy();
        for (int k = 0; k < LMS_N; k++) { /* the masked pass's counts, nested landings' too, under vk_lock */
            __atomic_fetch_add(&g_perf_handoff.land_steps[k], g_landing_mask[k], __ATOMIC_RELAXED);
            g_landing_mask[k] = 0;
        }
    }
    g_out_landing--;
    if (g_perf_compare == 2) g_walk_depth--;
    g_gfx_any = g_out != NULL;
}
/* At the frame's submit: its storage results are copied into host-cached memory (vk_stage_back), where landing them
   compares bytes quickly. A result whose copy can't be allocated is read where the shader wrote it. */
void vk_buf_host_barrier(VkCommandBuffer cb, u64 frame) {
    unlocked_note(VKU_BARRIER);
    GfxOut* first = g_out;
    while (first && first->frame != frame) first = first->next;
    if (!first) return;
    VkMemoryBarrier2 b = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                          .srcStageMask = out_stages(),
                          .srcAccessMask = out_access(),
                          .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
                          .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT};
    VkDependencyInfo d = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .memoryBarrierCount = 1, .pMemoryBarriers = &b};
    rec_CmdPipelineBarrier2(cb, &d);
    /* consecutive results usually share both buffers: one copy command each run */
    VkBufferCopy regions[64];
    u32 n = 0;
    VkBuffer src = VK_NULL_HANDLE, dst = VK_NULL_HANDLE;
    for (GfxOut* o = first; o; o = o->next) {
        if (o->frame != frame) continue;
        VkBuffer back;
        u64 at;
        VmaAllocation mem;
        u8* p = vk_stage_back(o->size, &back, &at, &mem);
        if (!p) break; /* out of memory: this result and the rest stay where the shader wrote them */
        if (n && (n == sizeof regions / sizeof *regions || o->buf != src || back != dst)) {
            rec_CmdCopyBuffer(cb, src, dst, n, regions);
            n = 0;
        }
        src = o->buf;
        dst = back;
        regions[n++] = (VkBufferCopy){.srcOffset = o->off, .dstOffset = at, .size = o->size};
        o->ptr = p;
        o->mem = mem;
    }
    if (n) rec_CmdCopyBuffer(cb, src, dst, n, regions);
    b.srcStageMask |= VK_PIPELINE_STAGE_2_COPY_BIT;
    b.srcAccessMask |= VK_ACCESS_2_TRANSFER_WRITE_BIT;
    b.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
    b.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    rec_CmdPipelineBarrier2(cb, &d);
}
static void wait_finish(int reason) {
    u64 t = copy_stats_on() ? plat_ns() : 0;
    vk_finish();
    if (t) {
        u64 ns = plat_ns() - t;
        g_wb_waits++;
        g_wb_wait_ns += ns;
        g_wb_reason[reason]++;
        g_wb_reason_ns[reason] += ns;
    }
}
static void land_reason(int reason) {
    if (!g_gfx_any || g_out_landing) return;
    vk_lock();
    if (g_gfx_any && !g_out_landing) wait_finish(reason);
    vk_unlock();
}
void vk_buf_land(void) { land_reason(WB_SIGNAL); }
void vk_buf_land_syncwait(void) { land_reason(WB_SYNCWAIT); }
void vk_buf_land_map(void) { land_reason(WB_MAP); }
/* Two GPU virtual ranges can name the same guest pages. Mapping changes land pending output while holding vk_lock,
   so the translations remain stable until this check and any following landing have completed. */
static int page_cmp(const void* a, const void* b) {
    u64 x = *(const u64*)a, y = *(const u64*)b;
    return (x > y) - (x < y);
}
/* A range's guest pages, translated once for a walk of the pending results (translating them again for every result
   was the GPU thread's top cost once results stayed pending through a frame). Up to 16 pages are kept; a longer
   range compares with each result's pages through bounds alone, then page by page */
typedef struct {
    u64 page[16];
    u32 n;
    u64 lo, hi; /* the lowest and highest guest page, ~0 and 0 when none is mapped */
    u64 a, an;
} RangePages;
static void range_pages(u64 a, u64 an, RangePages* q) {
    q->n = 0;
    q->lo = ~0ull;
    q->hi = 0;
    q->a = a;
    q->an = an;
    for (u64 ap = a >> 12, ae = (a + an - 1) >> 12; ap <= ae; ap++) {
        u64 ac = gmmu_translate(ap << 12), page;
        if (!ac) continue;
        page = ac >> 12;
        if (q->n < 16) q->page[q->n] = page;
        q->n++;
        if (page < q->lo) q->lo = page;
        if (page > q->hi) q->hi = page;
    }
}
static int backing_overlap(const RangePages* q, const GfxOut* o) {
    u64 olo, ohi;
    u32 i;
    if (!q->n || !o->npages) return 0;
    olo = o->pages[0];
    ohi = o->pages[o->npages - 1];
    if (ohi == ~0ull) /* unmapped pages sort last */
        for (i = o->npages; i-- && o->pages[i] == ~0ull;) ohi = i ? o->pages[i - 1] : 0;
    if (q->hi < olo || ohi < q->lo) return 0;
    if (q->n <= 16) {
        for (i = 0; i < q->n; i++)
            if (bsearch(&q->page[i], o->pages, o->npages, sizeof q->page[i], page_cmp)) return 1;
        return 0;
    }
    for (u64 ap = q->a >> 12, ae = (q->a + q->an - 1) >> 12; ap <= ae; ap++) {
        u64 ac = gmmu_translate(ap << 12);
        if (!ac) continue;
        u64 page = ac >> 12;
        if (bsearch(&page, o->pages, o->npages, sizeof page, page_cmp)) return 1;
    }
    return 0;
}
static void any_span(void* user, u64 at, u64 from, u64 n) {
    (void)at;
    (void)from;
    (void)n;
    *(int*)user = 1;
}
/* [gva, gva + len) and o share bytes, through the same GPU address or pages in common (q): a result lands its own
   bytes alone, so the rest of a page it shares is free to read and write (Pokemon Legends Z-A writes a word next to
   its transform feedback captures before each, through a macro's inline upload). *rmap is the range's pages, made
   once; without memory for them, any page in common counts */
static int out_meets(const GfxOut* o, u64 gva, u64 len, const RangePages* q, u64** rmap, u64 local[16]) {
    int hit = 0;
    if (gva < o->addr + o->size && o->addr < gva + len) return 1;
    if (!backing_overlap(q, o)) return 0;
    if (!*rmap && !(*rmap = range_map(gva, len, local))) return 1;
    out_spans(o, gva, len, *rmap, any_span, &hit);
    return hit;
}
static void mem_need_reason(u64 gva, u64 len, int reason) {
    unlocked_note(VKU_MEM_NEED);
    RangePages q;
    u64 local[16], *rmap = NULL;
    if (!g_gfx_any || g_out_landing || !len) return;
    vk_lock();
    if (reason != WB_MAP && !pend_hit(&g_out_pages, gva, len)) {
        vk_unlock();
        return;
    }
    range_pages(gva, len, &q);
    /* the results over the range land: those of submits already made by waiting for the newest of them, the GPU
       going on with what came after, and only one in the commands being recorded by finishing them all */
    u64 newest = 0;
    int recording = 0;
    for (GfxOut* o = g_out; o; o = o->next)
        if (reason == WB_MAP ? (gva < o->addr + o->size && o->addr < gva + len) || backing_overlap(&q, o)
                             : out_meets(o, gva, len, &q, &rmap, local)) {
            if (o->frame > vk_last_submit()) recording = 1;
            if (o->frame > newest) newest = o->frame;
        }
    if (recording)
        wait_finish(reason);
    else if (newest) {
        vk_wait_submit(newest);
        vk_land_through(newest);
    }
    if (rmap && rmap != local) free(rmap);
    vk_unlock();
}
void vk_buf_mem_need(u64 gva, u64 len) { mem_need_reason(gva, len, WB_READ); }
int vk_buf_pending_any(void) { return g_gfx_any; }
u64 vk_buf_adds(void) { return g_out_pages.adds; }
int vk_buf_pending_unsubmitted(void) {
    unlocked_note(VKU_UNSUBMITTED);
    for (GfxOut* o = g_out; o; o = o->next)
        if (o->frame == vk_submit_number()) return 1;
    return 0;
}
int vk_buf_pending(u64 gva, u64 len) {
    unlocked_note(VKU_PENDING);
    RangePages q;
    if (!g_gfx_any || !len || !pend_hit(&g_out_pages, gva, len)) return 0;
    range_pages(gva, len, &q);
    for (GfxOut* o = g_out; o; o = o->next)
        if ((gva < o->addr + o->size && o->addr < gva + len) || backing_overlap(&q, o)) return 1;
    return 0;
}
void vk_buf_mem_need_map(u64 gva, u64 len) { mem_need_reason(gva, len, WB_MAP); }
void vk_buf_before_write(u64 gva, u64 len) {
    if (len)
        mem_need_reason(gva, len, WB_WRITE);
    else
        land_reason(WB_SIGNAL);
}
/* GPU copies of pending results into staging, in the order the results were recorded, after the shaders and copies
   that wrote them: copies over bytes an earlier copy wrote wait for it behind a barrier (one copy command mustn't
   overlap itself, and two unordered ones would land in either order), so the later result ends up on top */
typedef struct {
    VkBufferCopy regions[64];
    u64 written[128][2]; /* destination bytes copied since the last barrier */
    u32 n, nwritten;
    VkBuffer src, dst;
    int started;
    int site; /* SNX_ARCEUS_PERF_TIMES: the PK_* the batch is counted under */
} CopyBatch;
static void copy_barrier(VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access, VkPipelineStageFlags2 dst_stage,
                         VkAccessFlags2 dst_access) {
    VkMemoryBarrier2 b = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                          .srcStageMask = src_stage,
                          .srcAccessMask = src_access,
                          .dstStageMask = dst_stage,
                          .dstAccessMask = dst_access};
    VkDependencyInfo d = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .memoryBarrierCount = 1, .pMemoryBarriers = &b};
    rec_CmdPipelineBarrier2(vk_cmd(), &d);
}
static void copy_flush(CopyBatch* c) {
    if (c->n) rec_CmdCopyBuffer(vk_cmd(), c->src, c->dst, c->n, c->regions);
    c->n = 0;
}
static void copy_add(CopyBatch* c, VkBuffer src, u64 src_off, u64 dst_off, u64 size) {
    u32 k;
    int overlap = c->nwritten == sizeof c->written / sizeof *c->written;
    if (!c->started) {
        vk_perf_copy(c->site);
        vk_render_end();
        copy_barrier(out_stages(), out_access(), VK_PIPELINE_STAGE_2_COPY_BIT,
                     VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT);
        c->started = 1;
    }
    for (k = 0; k < c->nwritten && !overlap; k++)
        overlap = dst_off < c->written[k][1] && c->written[k][0] < dst_off + size;
    if (overlap) {
        copy_flush(c);
        copy_barrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
                     VK_ACCESS_2_TRANSFER_WRITE_BIT);
        c->nwritten = 0;
    }
    if (c->n && (c->n == sizeof c->regions / sizeof *c->regions || src != c->src)) copy_flush(c);
    c->src = src;
    c->regions[c->n++] = (VkBufferCopy){.srcOffset = src_off, .dstOffset = dst_off, .size = size};
    c->written[c->nwritten][0] = dst_off;
    c->written[c->nwritten][1] = dst_off + size;
    c->nwritten++;
    if (g_perf_times) vk_perf_copy_bytes(size);
}
static void copy_end(CopyBatch* c) {
    copy_flush(c);
    if (c->started) /* before draws and dispatches read (or write) what was copied */
        copy_barrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                     VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
}

/* guest bytes without landing pending results (they are copied over them on the GPU); unmapped pages read as zero */
static void read_untracked(u64 gva, u8* dst, u64 len) {
    const u8* contiguous;

    if (!len) return;
    contiguous = gmem_ptr_untracked(gva, len);
    if (contiguous) {
        memcpy(dst, contiguous, len);
        return;
    }
    while (len) {
        u64 n = 4096 - (gva & 4095);
        const u8* p;
        if (n > len) n = len;
        p = gmem_ptr_untracked(gva, n);
        if (p)
            memcpy(dst, p, n);
        else
            memset(dst, 0, n);
        gva += n;
        dst += n;
        len -= n;
    }
}
/* a pending result can be copied over guest memory on the GPU: this frame's staging or the last one's still holds
   it, and the guest hasn't written the bytes it covers in [lo, hi) since it was snapshotted */
static int out_copyable(const GfxOut* o, u64 lo, u64 hi) {
    const u8* host = gmem_host(lo, hi - lo);
    if (o->frame_id + 1 < vk_frame_id() || !host) return 0;
    if (vk_written_since(host, hi - lo, o->wseen) == 0) return 1;
    if (o->initial) return 0; /* the baseline is still being produced by the GPU */
    host = gmem_ptr_untracked(lo, hi - lo);
    return host && !memcmp(host, o->org + (lo - o->addr), hi - lo);
}
/* the same for each run of o's bytes in [gva, gva + len), through whichever GPU address maps them (out_spans) */
typedef struct {
    const GfxOut* o;
    u64 gva;
    int ok;
} Copyable;
/* Every draw that binds memory under pending results asks this of each of them, so comparing their bytes made the
   walk quadratic in a frame's results: a frame of 200 composed bindings spent a fifth of the GPU thread in memcmp. Pages
   write tracking saw unwritten since the result read its original bytes still hold them. It sees a write from the
   next write epoch on, which is when the stream can see it too, and a host without tracking compares */
static void copyable_span(void* user, u64 at, u64 from, u64 n) {
    Copyable* c = user;
    const u8* host = c->ok ? gmem_host(c->gva + at, n) : NULL;
    if (!host) {
        c->ok = 0;
        return;
    }
    if (vk_written_since(host, n, c->o->wseen) == 0) return;
    if (c->o->initial) {
        c->ok = 0;
        return;
    }
    host = gmem_ptr_untracked(c->gva + at, n);
    if (!host || memcmp(host, c->o->org + from, n)) c->ok = 0;
}
/* A composition takes each byte from the newest pending result over it: that result was made over the ones before it
   (composed, or over guest memory they hadn't touched) and its draw ran after theirs, so its buffer holds the byte as
   every result before it left it, and copying the older ones too only to overwrite them made composing quadratic. One
   game composed 200 bindings a frame over a few hundred results each and its GPU copied 955 MB a frame, 147 ms. The
   results are walked from the newest, and a byte of the destination one of them gave is skipped by the older ones:
   Covered holds those bytes as sorted, disjoint runs of destination offsets */
typedef struct {
    u64 a, b;
} CoverRun;
typedef struct {
    CoverRun* v;
    u32 n, cap;
    u64 len;              /* the destination's size: all of it covered ends the walk */
    CoverRun first[32];   /* where the runs start */
} Covered;
static void cover_init(Covered* c, u64 len) {
    c->v = c->first;
    c->n = 0;
    c->cap = sizeof c->first / sizeof *c->first;
    c->len = len;
}
static void cover_free(Covered* c) {
    if (c->v != c->first) free(c->v);
}
static int cover_full(const Covered* c) { return c->n == 1 && c->v[0].a == 0 && c->v[0].b >= c->len; }
/* [a, b) of the destination, given by a newer result than the ones still to come: fn(user, a', b') for each of its
   parts no newer result gave, then all of it covered. 0 when the runs can't grow (nothing covered then: the older
   results copy over it too, as before, which is only slower) */
static int cover_take(Covered* c, u64 a, u64 b, void (*fn)(void* user, u64 a, u64 b), void* user) {
    u32 i = 0, j;
    while (i < c->n && c->v[i].b < a) i++;
    u64 at = a;
    for (j = i; j < c->n && c->v[j].a <= b; j++) {
        if (c->v[j].a > at) fn(user, at, c->v[j].a);
        if (c->v[j].b > at) at = c->v[j].b;
    }
    if (at < b) fn(user, at, b);
    /* the runs i..j-1 meet [a, b]: one run in their place */
    u64 na = a, nb = b;
    if (i < j) {
        if (c->v[i].a < na) na = c->v[i].a;
        if (c->v[j - 1].b > nb) nb = c->v[j - 1].b;
    }
    if (i == j) { /* a new run at i */
        if (c->n == c->cap) {
            u32 cap = c->cap * 2;
            CoverRun* v = c->v == c->first ? malloc((size_t)cap * sizeof *v) : realloc(c->v, (size_t)cap * sizeof *v);
            if (!v) return 0;
            if (g_perf_probes && cap * sizeof *v >= 4096) perf_mem_alloc(&g_perf_mem, PMA_LIST, cap * sizeof *v);
            if (c->v == c->first) memcpy(v, c->first, sizeof c->first);
            c->v = v;
            c->cap = cap;
        }
        memmove(&c->v[i + 1], &c->v[i], (size_t)(c->n - i) * sizeof *c->v);
        c->n++;
    } else {
        memmove(&c->v[i + 1], &c->v[j], (size_t)(c->n - j) * sizeof *c->v);
        c->n -= j - i - 1;
    }
    c->v[i] = (CoverRun){na, nb};
    return 1;
}
/* the pending results, newest first: their count, into *list (out when it is local and too small: allocated) */
static GfxOut** out_newest_first(GfxOut** local, u32 nlocal, u32* count) {
    u32 n = 0, i = 0;
    for (GfxOut* o = g_out; o; o = o->next) n++;
    GfxOut** v = n <= nlocal ? local : malloc((size_t)n * sizeof *v);
    if (!v) return NULL;
    for (GfxOut* o = g_out; o; o = o->next) v[n - 1 - i++] = o;
    *count = n;
    return v;
}
typedef struct {
    CopyBatch* c;
    const GfxOut* o;
    u64 dst_off, at, from; /* the span being taken: its destination offset and its offset in o */
    Covered* cov;
} Overlay;
static void overlay_part(void* user, u64 a, u64 b) {
    const Overlay* v = user;
    copy_add(v->c, v->o->buf, v->o->off + v->from + (a - v->at), v->dst_off + a, b - a);
}
static void overlay_span(void* user, u64 at, u64 from, u64 n) {
    Overlay* v = user;
    v->at = at;
    v->from = from;
    if (!cover_take(v->cov, at, at + n, overlay_part, v)) overlay_part(v, at, at + n);
}
/* copies the pending results' bytes in [gva, gva + len), through any GPU address, into dst at dst_off, which holds
   guest memory's bytes (after gfx_gpu_src said they can be): each byte from the newest result over it. 1 when a byte
   was copied */
static int gfx_overlay(u64 gva, u64 len, VkBuffer dst, u64 dst_off, int site) {
    CopyBatch c;
    Covered cov;
    GfxOut *nlocal[64], **newest;
    u32 n = 0;
    u64 local[16], *rmap = range_map(gva, len, local);
    if (!rmap || !(newest = out_newest_first(nlocal, 64, &n))) fatal("Out of memory composing a draw's storage buffer");
    memset(&c, 0, sizeof c);
    c.dst = dst;
    c.site = site;
    cover_init(&cov, len);
    for (u32 i = 0; i < n && !cover_full(&cov); i++) {
        Overlay v = {&c, newest[i], dst_off, 0, 0, &cov};
        out_spans(newest[i], gva, len, rmap, overlay_span, &v);
    }
    copy_end(&c);
    if (g_perf_times && !c.started) vk_perf_empty(site, len); /* the range staged, no byte copied over it */
    cover_free(&cov);
    if (newest != nlocal) free(newest);
    if (rmap != local) free(rmap);
    return c.started; /* a byte was copied */
}

/* A draw or dispatch can read an earlier one's storage result without a round trip through guest RAM. Returns 1 with
   that result's buffer itself in *buf, *off when the newest result over the range covers all of it, unchanged since,
   at an offset bindings accept, from this submit when the reader writes it (its copy back for landing was recorded at
   its submit's end) or this frame otherwise, at an offset that is a multiple of align. Returns 2 when pending results cover the range and can all be copied over
   guest memory on the GPU (gfx_overlay), 0 when none do, or after landing them because one can't: it maps the range
   through another virtual address, or the guest wrote its original bytes since. A result of the frame before is read
   where it is too (transform feedback captures the same buffers every frame, over the last frame's capture), its
   frame's rings held until the reader is done (vk_frame_hold) */
/* o has bytes of [gva, gva + len): at the same GPU address, or on the same guest pages (q) */
static int out_meets_range(const GfxOut* o, u64 gva, u64 len, const RangePages* q) {
    u64 lo = gva > o->addr ? gva : o->addr, hi = gva + len < o->addr + o->size ? gva + len : o->addr + o->size;
    return lo < hi || backing_overlap(q, o);
}
/* whether the guest still holds o's original bytes in [gva, gva + len) (rmap: made on first use) */
static int out_still_copyable(const GfxOut* o, u64 gva, u64 len, u64** rmap, u64 local[16]) {
    Copyable c = {o, gva, o->frame_id + 1 >= vk_frame_id()};
    if (c.ok && !*rmap && !(*rmap = range_map(gva, len, local))) c.ok = 0;
    if (c.ok) out_spans(o, gva, len, *rmap, copyable_span, &c);
    return c.ok;
}
/* SNX_ARCEUS_PERF_TIMES (PERF-17): whether o shares a byte with [gva, gva + len), as out_meets, without allocating:
   with no page map (a range of more than 16 pages) a page in common counts. *same: it shares them at its address */
static int out_meets_noalloc(const GfxOut* o, u64 gva, u64 len, const RangePages* q, const u64* map, int* same) {
    int hit = 0;
    *same = gva < o->addr + o->size && o->addr < gva + len;
    if (*same) return 1;
    if (!backing_overlap(q, o)) return 0;
    if (!map) return 1;
    out_spans(o, gva, len, map, any_span, &hit);
    return hit;
}
/* the range's page map in local when it has 16 pages at most, else NULL */
static u64* range_map_local(u64 gva, u64 len, u64 local[16]) {
    return ((gva + len - (gva & ~4095ull) + 4095) >> 12) <= 16 ? range_map(gva, len, local) : NULL;
}
/* the class (PI_*) of a composed input range by the pending results sharing a byte with it, exact 0 or the vertex
   stream's exact length. Reads only: no landing, hold, barrier or allocation */
static int input_class(u64 gva, u64 len, u64 exact) {
    PerfResult r[PERF_RESULTS];
    u32 n = 0;
    RangePages q, qe;
    u64 local[16], elocal[16];
    if (!exact || exact >= len) exact = 0;
    range_pages(gva, len, &q);
    const u64* map = range_map_local(gva, len, local);
    const u64* emap = NULL;
    if (!map) vk_perf_extra(PX_WIDE);
    if (exact) {
        range_pages(gva, exact, &qe);
        emap = range_map_local(gva, exact, elocal);
    }
    for (GfxOut* o = g_out; o; o = o->next) {
        int same, esame;
        if (!out_meets_noalloc(o, gva, len, &q, map, &same)) continue;
        if (n < PERF_RESULTS)
            r[n] = (PerfResult){o->addr, o->size, (u64)(uintptr_t)o->buf, o->off, same,
                                exact && out_meets_noalloc(o, gva, exact, &qe, emap, &esame)};
        n++;
    }
    return perf_inplace_class(r, n, gva, len, exact);
}
/* SNX_ARCEUS_PERF_TIMES (PERF-18): gfx_gpu_src's walks over n results took the time since *t0, once (*t0 cleared) */
static void src_walked(u64* t0, u64 n) {
    if (!*t0) return;
    perf_time_add(&g_perf, PT_SRC_WALK, plat_ns() - *t0);
    perf_count(&g_perf, PC_SRC_WALKS, 1);
    perf_count(&g_perf, PC_SRC_RESULTS, n);
    *t0 = 0;
}
/* SNX_ARCEUS_PERF_COMPARE=2 (PERF-28, research 00:47): gfx_gpu_src's walks go over the results of g_out on the
   range's mapped pages or with an unmapped page, from the index, in g_out's order (seq), in place of all of g_out:
   every result out_meets_range or out_meets can take is among them. g_src_indexed 0: the walks go over g_out, as
   without the switch, for a range of more than 16 mapped pages, a lost index or no memory for the list */
static GfxOut** g_scand;
static u32 g_nscand, g_capscand;
static int g_src_indexed;
static int src_add(GfxOut* o) {
    if (g_nscand == g_capscand) {
        const u32 cap = g_capscand ? g_capscand * 2 : 64;
        GfxOut** b = realloc(g_scand, (size_t)cap * sizeof *b);
        if (!b) return 0;
        g_scand = b;
        g_capscand = cap;
    }
    g_scand[g_nscand++] = o;
    return 1;
}
static void src_index(const RangePages* q) {
    g_src_indexed = 0;
    if (g_perf_compare != 2 || g_walk_lost || q->n > 16) return;
    const u64 stamp = ++g_walk_stamp;
    g_nscand = 0;
    for (u32 i = 0; i <= q->n; i++) { /* the range's pages, then ~0 */
        const u64 page = i < q->n ? q->page[i] : ~0ull;
        for (WalkRef* r = *walk_bucket(page); r; r = r->next) {
            if (r->page != page || !r->o->in_out || r->o->wstamp == stamp) continue;
            r->o->wstamp = stamp;
            if (g_nscand == WALK_BOUND) { /* past the bound: the walks go over g_out, unsorted, as unset */
                walk_noted(0, 1, g_nscand + 1);
                return;
            }
            if (!src_add(r->o)) return;
        }
    }
    walk_noted(0, 0, g_nscand);
    if (g_nscand > 1) qsort(g_scand, g_nscand, sizeof *g_scand, seq_cmp);
    g_src_indexed = 1;
}
static GfxOut* src_first(u32* at) {
    *at = 0;
    return !g_src_indexed ? g_out : g_nscand ? g_scand[0] : NULL;
}
static GfxOut* src_next(const GfxOut* o, u32* at) {
    return !g_src_indexed ? o->next : ++*at < g_nscand ? g_scand[*at] : NULL;
}
/* kind: the caller, a PB_* for SNX_ARCEUS_PERF_TIMES */
static int gfx_gpu_src(u64 gva, u64 len, VkBuffer* buf, u64* off, int writable, u64 align, int kind) {
    GfxOut *match = NULL, *newest = NULL;
    int count = 0, before = 0;
    RangePages q;
    u64 local[16], *rmap = NULL;
    if (!g_gfx_any || g_out_landing || !gva || !len) return 0;
    if (g_cs_any) vk_compute_mem_need(gva, len);
    if (!pend_hit(&g_out_pages, gva, len)) return 0;
    u64 walk_t0 = g_perf_times ? plat_ns() : 0, walked = 0;
    u32 at;
    range_pages(gva, len, &q);
    src_index(&q);
    for (GfxOut* o = src_first(&at); o; o = src_next(o, &at), walked++)
        if (out_meets_range(o, gva, len, &q)) newest = o;
    if (!newest) {
        src_walked(&walk_t0, walked);
        return 0;
    }
    /* SNX_ARCEUS_PERF_INPLACE >= 1, a read (research 02:42): the newest result sharing a BYTE with the range, when it
       covers it at this address. The newer ones on its pages hold none of its bytes, and the checks below run on it
       as on the newest one. Otherwise the code below runs on the newest by page, as without the switch */
    if (g_perf_inplace >= 1 && !writable && !(gva >= newest->addr && gva + len <= newest->addr + newest->size)) {
        GfxOut* nb = NULL;
        for (GfxOut* o = src_first(&at); o; o = src_next(o, &at))
            if (out_meets(o, gva, len, &q, &rmap, local)) nb = o; /* conservative by page without a map */
        if (nb && gva >= nb->addr && gva + len <= nb->addr + nb->size) newest = nb;
    }
    /* The newest result over the range was composed over the older ones when it was made (gfx_overlay, through any
       address), and its draw ran after theirs: when it covers the whole range at this address, its buffer holds every
       byte as all of them left it, and the older ones need no check. Requiring a single result made every binding
       after a second one compose again: in one game each storage range was copied, written back and landed about 16
       times a frame (114,000 compositions in 600 frames, against 3,600 for an offset or a submit) */
    if (gva >= newest->addr && gva + len <= newest->addr + newest->size) {
        if (!out_still_copyable(newest, gva, len, &rmap, local)) {
            src_walked(&walk_t0, walked);
            if (copy_stats_on()) g_wb_draw_miss[newest->frame_id != vk_frame_id() ? 0 : 2]++;
            if (g_perf_times) vk_perf_src(kind, newest->frame_id != vk_frame_id() ? PS_OLD : PS_CHANGED);
            if (rmap && rmap != local) free(rmap);
            land_reason(WB_DRAW);
            return 0;
        }
        match = newest;
        count = 1;
        before = newest->frame_id != vk_frame_id();
    } else
        for (GfxOut* o = src_first(&at); o; o = src_next(o, &at)) {
            if (!out_meets_range(o, gva, len, &q)) continue;
            if (!out_still_copyable(o, gva, len, &rmap, local)) {
                src_walked(&walk_t0, walked);
                if (copy_stats_on()) g_wb_draw_miss[o->frame_id != vk_frame_id() ? 0 : 2]++;
                if (g_perf_times) vk_perf_src(kind, o->frame_id != vk_frame_id() ? PS_OLD : PS_CHANGED);
                if (rmap && rmap != local) free(rmap);
                land_reason(WB_DRAW);
                return 0;
            }
            match = o;
            count++;
            before |= o->frame_id != vk_frame_id();
        }
    if (rmap && rmap != local) free(rmap);
    src_walked(&walk_t0, walked);
    if (!count) return 0;
    if (before) vk_frame_hold(vk_frame_id() - 1);
    if (count == 1 && gva >= match->addr && gva + len <= match->addr + match->size &&
        !((match->off + (gva - match->addr)) & (align - 1)) &&
        (!writable || match->frame == vk_submit_number())) {
        /* one barrier per draw or dispatch that reads an earlier one's result: either may have written it, and
           either may read it next (a dispatch on the graphics queue binds like a draw) */
        static u64 barrier_frame = ~0ull, barrier_work = ~0ull;
        if (writable) {
            wmark_set(match); /* this draw writes it again */
            out_wrote();
            if (g_perf_times) vk_perf_made(PR_INPLACE);
        }
        /* SNX_ARCEUS_PERF_INPLACE=3, a read: the barrier recorded before in this submission orders every write of
           a result recorded before it, all buffers, and no draw or dispatch handed a writable result since */
        const int covered = g_perf_inplace >= 3 && !writable && barrier_frame == vk_submit_number() &&
                            g_out_write_work < barrier_work;
        if (covered && barrier_work != g_vk_drawn + g_vk_dispatches && g_perf_times) vk_perf_extra(PX_SKIPPED);
        if (!covered && (barrier_frame != vk_submit_number() || barrier_work != g_vk_drawn + g_vk_dispatches)) {
            if (g_perf_times) vk_perf_extra(PX_BARRIER);
            if (g_perf_times) { /* no write of match since the barrier before (a barrier orders all earlier work) */
                const u32 i = wmark_slot(match);
                perf_count(&g_perf, PC_SRC_BARRIERS, 1);
                if (g_wmark[i].o != match)
                    perf_count(&g_perf, PC_SRC_UNKNOWN, 1);
                else if (barrier_work != ~0ull && g_wmark[i].work < barrier_work)
                    perf_count(&g_perf, PC_SRC_CLEAN, 1);
            }
            perf_pass_why(PE_GPU_SRC);
            vk_render_end();
            copy_barrier(out_stages(), out_access(),
                         VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                         VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
            barrier_frame = vk_submit_number();
            barrier_work = g_vk_drawn + g_vk_dispatches;
        }
        *buf = match->buf;
        *off = match->off + (gva - match->addr);
        if (copy_stats_on()) g_wb_reuses++;
        if (g_perf_times) vk_perf_src(kind, PS_REUSE);
        return 1;
    }
    if (copy_stats_on()) g_wb_draw_miss[count > 1 ? 1 : 3]++; /* composed: several results, or an offset or submit */
    if (g_perf_times) { /* the first of the reuse conditions above that failed */
        int why = PS_SUBMIT;
        if (count > 1)
            why = PS_SEVERAL;
        else if (gva < match->addr || gva + len > match->addr + match->size)
            why = PS_PART;
        else if ((match->off + (gva - match->addr)) & (align - 1))
            why = PS_ALIGN;
        vk_perf_src(kind, why);
        if (kind == PB_INPUT) {
            const u64 t0 = plat_ns();
            vk_perf_class(input_class(gva, len, g_perf_exact));
            perf_time_add(&g_perf, PT_CLASS_WALK, plat_ns() - t0);
        }
    }
    return 2;
}
/* the range for reading on the GPU, with its pending results composed over guest memory in staging */
static int gfx_composed(u64 gva, u64 len, VkBuffer* buf, u64* off) {
    const u64 t0 = g_perf_times ? plat_ns() : 0; /* PERF-18 */
    u8* p = vk_stage(len, vk_stage_align(), buf, off);
    if (!p) return 0;
    read_untracked(gva, p, len);
    const int copied = gfx_overlay(gva, len, *buf, *off, PK_COMPOSE);
    if (t0) perf_time_add(&g_perf, copied ? PT_COMPOSE_COPY : PT_COMPOSE_EMPTY, plat_ns() - t0);
    return 1;
}

/* written-page tracking (plat_ww_scan): a generation per 4K page of guest memory, bumped when a scan finds it
   written. A cache entry remembers the clock when it last matched memory, and is stale only if a page in its range
   has a newer generation. Scanning re-protects what it reports, and the shared table keeps that from hiding a change
   from another entry over the same memory. The table has a leaf of 1024 pages per 4 MB that has been written, with
   the newest generation in the leaf beside it so whole leaves are skipped */
#define WW_LEAF_SHIFT 22
static u32* g_wleaf[AS_MAX >> WW_LEAF_SHIFT];
static u32 g_wleaf_max[AS_MAX >> WW_LEAF_SHIFT];
/* The CPU writes a GPU stream may see come before its submission or the end of one of its waits: each of those starts
   a write epoch (vk_ww_new_epoch). A leaf scanned whole in an epoch isn't scanned again in it, and a scan covers whole
   leaves, so the textures and buffers checked in one epoch share a few scans: one game made 5,600 of them a frame.
   g_wleaf_seen holds the epoch + 1 whose scans of the leaf its table has taken in (checks only, under vk_lock) */
static u32 g_wleaf_seen[AS_MAX >> WW_LEAF_SHIFT];
static u32 g_ww_epoch; /* atomic */
/* A job on the GPU thread checks against the newest epoch when it started (vk_ww_stream), not one a later submission
   starts while it runs: what the CPU wrote for that submission is the later job's to see. That leaves the scanner
   thread (ww_scanner) the time until the job starts to scan what it will check */
static _Thread_local u32 t_ww_stream;
static _Thread_local int t_ww_streaming;
static u32 g_wclock = 1;
static int g_ww = -1; /* -1 not tried yet, 0 not available, 1 on */
static void ww_mark(u8* p, u64 n) {
    u64 o = (u64)(p - g_mem);
    if (o >= AS_SIZE || !n) return;
    for (u64 pg = o >> 12, e = (o + n - 1) >> 12; pg <= e && pg < (AS_SIZE >> 12); pg++) {
        u64 l = pg >> (WW_LEAF_SHIFT - 12);
        if (!g_wleaf[l]) {
            /* a leaf without a table stands for all its pages at its newest generation (ww_mark_range, or a table
               that couldn't be allocated): a new table starts them there */
            u32* t = calloc(1u << (WW_LEAF_SHIFT - 12), 4);
            if (t && g_wleaf_max[l])
                for (u32 i = 0; i < 1u << (WW_LEAF_SHIFT - 12); i++) t[i] = g_wleaf_max[l];
            g_wleaf[l] = t;
        }
        if (g_wleaf[l]) g_wleaf[l][pg & ((1u << (WW_LEAF_SHIFT - 12)) - 1)] = g_wclock;
        g_wleaf_max[l] = g_wclock;
    }
}
/* the pages of guest memory [o, e) as written now. A leaf the range covers whole, and that has no table, only gets
   its newest generation: kernel/mem.c can unmap gigabytes at once */
static void ww_mark_range(u64 o, u64 e) {
    const u64 lp = 1ull << (WW_LEAF_SHIFT - 12);
    for (u64 pg = o >> 12, pe = (e + 4095) >> 12; pg < pe;) {
        u64 l = pg >> (WW_LEAF_SHIFT - 12), lend = (l + 1) * lp, end = pe < lend ? pe : lend;
        if (!g_wleaf[l] && pg == l * lp && end == lend)
            g_wleaf_max[l] = g_wclock;
        else
            ww_mark(g_mem + (pg << 12), (end - pg) << 12);
        pg = end;
    }
}

/* ranges of guest memory changed without a write the tracking sees (vk_mem_changed: decommitted pages, which read
   as zero now). Noted on the game's threads, outside vk_lock, and marked written by the next vk_written_since, as if
   its scan had found them. When the list is full its last range grows to cover the new one, marking more than
   changed: a cache over the rest only checks its contents once */
typedef struct {
    u64 a, e;
} WwRange;
#define WW_CHG_N 32
static WwRange g_wchg[WW_CHG_N];
static int g_wchg_n; /* atomic: read without the lock to skip it */
static PMutex g_wchg_lock = PMUTEX_INIT;
void vk_mem_changed(u64 cpu, u64 len) {
    if (cpu >= AS_SIZE || !len) return;
    u64 e = len > AS_SIZE - cpu ? AS_SIZE : cpu + len;
    pm_lock(&g_wchg_lock);
    int n = __atomic_load_n(&g_wchg_n, __ATOMIC_RELAXED);
    if (n < WW_CHG_N) {
        g_wchg[n] = (WwRange){cpu, e};
        __atomic_store_n(&g_wchg_n, n + 1, __ATOMIC_RELEASE);
    } else {
        WwRange* r = &g_wchg[WW_CHG_N - 1];
        if (cpu < r->a) r->a = cpu;
        if (e > r->e) r->e = e;
    }
    pm_unlock(&g_wchg_lock);
}
static void ww_take_changed(void) {
    if (!__atomic_load_n(&g_wchg_n, __ATOMIC_ACQUIRE)) return;
    WwRange c[WW_CHG_N];
    pm_lock(&g_wchg_lock);
    int n = __atomic_load_n(&g_wchg_n, __ATOMIC_RELAXED);
    memcpy(c, g_wchg, (size_t)n * sizeof *c);
    __atomic_store_n(&g_wchg_n, 0, __ATOMIC_RELAXED);
    pm_unlock(&g_wchg_lock);
    if (n) g_wclock++; /* what it marks is newer than every baseline taken before (vk_written_since) */
    for (int i = 0; i < n; i++) ww_mark_range(c[i].a, c[i].e);
}

/* Scans, on the scanner thread as soon as an epoch starts and on the checks' thread for what it hasn't scanned yet.
   A scan of a 4 MB leaf takes about 9 us in one game, 2.4 k cycles of each draw's setup on the GPU thread when the
   checks did them all. A scan claims its leaves first (g_wleaf_busy), so one leaf is never scanned twice at once, and
   the pages it finds written are in g_wres before the leaves' g_wleaf_done say they were scanned and their claims
   end: a page a scan re-protects can't be missed by a check that scans the same leaf. The next check marks them
   (ww_take_scanned). g_wleaf_done holds the epoch + 1 of the leaf's newest scan, read before it started, and
   g_wleaf_used the epoch + 1 a check last needed the leaf in. The scanner only tries claims, and a check waits only
   for a leaf the scanner is scanning right then: a lock over all scans had checks waiting out whole passes, 0.5 ms
   each */
#define WW_RES_N       4096
#define WW_RUN_MAX     8 /* leaves the scanner scans in one call at most */
#define WW_USED_EPOCHS 4 /* the scanner scans the leaves the checks of the last epochs needed */
static u32 g_wleaf_done[AS_MAX >> WW_LEAF_SHIFT]; /* atomic, stored by the leaf's claimer */
static u32 g_wleaf_used[AS_MAX >> WW_LEAF_SHIFT]; /* atomic: only a hint for the scanner */
static u8 g_wleaf_busy[AS_MAX >> WW_LEAF_SHIFT];  /* atomic: 1 while a scan has claimed the leaf */
static u32 g_ww_used[WW_USED_EPOCHS];             /* the last epochs + 1 checks needed leaves in, under vk_lock */
static u32 g_ww_used_i;
static u32 g_ww_used_from; /* atomic: the oldest of them */
/* a scan's written pages before they go to g_wres: the scanner's, and the checks' (under vk_lock) */
typedef struct {
    WwRange r[WW_RES_N];
    int n;
} WwStage;
static WwStage g_wstage_scanner, g_wstage_check;
static _Thread_local WwStage* t_wstage; /* the running scan's, for ww_note */
static WwRange g_wres[WW_RES_N]; /* what scans found written, for the next check to mark */
static int g_wres_n;             /* atomic: read without the lock to skip it */
static PMutex g_wres_lk = PMUTEX_INIT; /* after a leaf's claim */
static int g_wscan_failed; /* atomic */
static int g_wscan_on;     /* atomic: the scanner runs */
static PMutex g_wscan_wlk = PMUTEX_INIT;
static PCond g_wscan_cv;
/* plat_ww_scan's callback: a run of written pages. When the stage is full its last range grows over the new one
   (scans go up through memory), marking more than was written: a cache over the rest only checks its contents once */
static void ww_note(u8* p, u64 n) {
    WwStage* st = t_wstage;
    u64 a = (u64)(p - g_mem);
    if (a >= AS_SIZE || !n) return;
    if (st->n && st->r[st->n - 1].e == a)
        st->r[st->n - 1].e = a + n;
    else if (st->n < WW_RES_N)
        st->r[st->n++] = (WwRange){a, a + n};
    else
        st->r[WW_RES_N - 1].e = a + n;
}
static int ww_claim(u64 l) {
    u8 free = 0;
    return __atomic_compare_exchange_n(&g_wleaf_busy[l], &free, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}
/* scans leaves [lo, hi], claimed by the caller, in the epoch seen - 1, and ends the claims: 0, or -1 when tracking
   failed */
static int ww_scan_leaves(u64 lo, u64 hi, u32 seen, WwStage* st) {
    t_wstage = st;
    const u64 s0 = g_perf_probes ? plat_ns() : 0; /* PERF-32 */
    int r = plat_ww_scan(g_mem + (lo << WW_LEAF_SHIFT), (hi - lo + 1) << WW_LEAF_SHIFT, ww_note);
    if (s0) perf_wait_note(PW_SCAN, __builtin_return_address(0), plat_ns() - s0);
    pm_lock(&g_wres_lk);
    int n = g_wres_n;
    for (int i = 0; i < st->n; i++)
        if (n < WW_RES_N)
            g_wres[n++] = st->r[i];
        else { /* full: the last range covers this one too */
            WwRange* w = &g_wres[WW_RES_N - 1];
            if (st->r[i].a < w->a) w->a = st->r[i].a;
            if (st->r[i].e > w->e) w->e = st->r[i].e;
        }
    __atomic_store_n(&g_wres_n, n, __ATOMIC_RELEASE);
    pm_unlock(&g_wres_lk);
    st->n = 0;
    if (r < 0) __atomic_store_n(&g_wscan_failed, 1, __ATOMIC_RELEASE);
    for (u64 l = lo; l <= hi; l++) {
        if (r == 0) __atomic_store_n(&g_wleaf_done[l], seen, __ATOMIC_RELEASE);
        __atomic_store_n(&g_wleaf_busy[l], 0, __ATOMIC_RELEASE);
    }
    return r < 0 ? -1 : 0;
}
/* Pages a landing wrote (own.c, docs/GPU_OWNERSHIP.md), with their bytes as it left them. The ownership lets go of
   them after, which watches them for writes again, and a page just watched reads as written: the next scan reports
   all of them, whatever happened to them. The landing is the GPU's write, counted when its result was published
   (vk_own_publish's gpu_mem_written_cpu) and older than anything drawn over those bytes since. Taken for a CPU write,
   newer than every baseline, it had targets drawn there after the publish give their pixels up for memory's older
   bytes: one game's reflection array, drawn whole over the memory of the previous one, kept seven layers of it, NaN
   included. So a report of such a page counts only when its bytes are not the landing's any more. They are read
   through the pool's mirror, which no trap covers, and the entry goes with the report: the scan protected the page */
typedef struct {
    u64 page, hash;  /* the 4K page's number, and its bytes' vk_hash after the landing */
    const u8* bytes; /* its place in the mirror */
    u32 seq;         /* the newest of a page's entries wins */
} WwLanded;
#define WW_LANDED_MAX (1u << 18) /* pages (1 GB): past it the reports count as they are */
static WwLanded* g_wland;
static u32 g_wland_n, g_wland_cap, g_wland_seq;
static int g_wland_sorted = 1;
static PMutex g_wland_lk = PMUTEX_INIT; /* after g_wres_lk */
void vk_ww_landed(u64 cpu, u64 size, const u8* bytes) {
    if (!bytes || cpu >= AS_SIZE || size > AS_SIZE - cpu) return;
    pm_lock(&g_wland_lk);
    for (u64 o = 0; o + 4096 <= size; o += 4096) {
        if (g_wland_n == g_wland_cap) {
            u32 cap = g_wland_cap ? g_wland_cap * 2 : 1024;
            WwLanded* n = cap <= WW_LANDED_MAX ? (WwLanded*)realloc(g_wland, (size_t)cap * sizeof *n) : NULL;
            if (!n) break; /* those pages' reports count */
            g_wland = n;
            g_wland_cap = cap;
        }
        g_wland[g_wland_n++] = (WwLanded){(cpu + o) >> 12, vk_hash(bytes + o, 4096), bytes + o, g_wland_seq++};
        g_wland_sorted = 0;
    }
    pm_unlock(&g_wland_lk);
}
static int landed_order(const void* a, const void* b) {
    const WwLanded *x = (const WwLanded*)a, *y = (const WwLanded*)b;
    if (x->page != y->page) return x->page < y->page ? -1 : 1;
    return x->seq < y->seq ? -1 : x->seq > y->seq;
}
/* g_wland by page, a page's newest entry alone */
static void landed_sort(void) {
    u32 n = 0;
    if (g_wland_sorted) return;
    qsort(g_wland, g_wland_n, sizeof *g_wland, landed_order);
    for (u32 i = 0; i < g_wland_n; i++) {
        if (n && g_wland[n - 1].page == g_wland[i].page) n--;
        g_wland[n++] = g_wland[i];
    }
    g_wland_n = n;
    g_wland_sorted = 1;
}
/* what a scan found written, [o, e), marked but for the landed pages on it that still hold the landing's bytes. Every
   entry on it goes (no bytes until ww_take_scanned drops them) */
static void ww_mark_scanned(u64 o, u64 e) {
    u32 lo = 0, hi = g_wland_n;
    u64 at = o;
    while (lo < hi) {
        u32 mid = lo + (hi - lo) / 2;
        if (g_wland[mid].page < o >> 12)
            lo = mid + 1;
        else
            hi = mid;
    }
    for (u32 i = lo; i < g_wland_n && g_wland[i].page << 12 < e; i++) {
        u64 a = g_wland[i].page << 12;
        if (!g_wland[i].bytes) continue;
        if (a + 4096 <= e && vk_hash(g_wland[i].bytes, 4096) == g_wland[i].hash) {
            if (a > at) ww_mark_range(at, a);
            at = a + 4096;
        }
        g_wland[i].bytes = NULL;
    }
    if (at < e) ww_mark_range(at, e);
}
/* marks what scans found written, newer than every baseline taken before: those were taken before the scans
   re-protected the pages, and their caches may have read memory before the writes */
static void ww_take_scanned(void) {
    if (!__atomic_load_n(&g_wres_n, __ATOMIC_ACQUIRE)) return;
    pm_lock(&g_wres_lk);
    if (g_wres_n) g_wclock++;
    pm_lock(&g_wland_lk);
    if (g_wland_n) {
        u32 n = 0;
        landed_sort();
        for (int i = 0; i < g_wres_n; i++) ww_mark_scanned(g_wres[i].a, g_wres[i].e);
        for (u32 i = 0; i < g_wland_n; i++)
            if (g_wland[i].bytes) g_wland[n++] = g_wland[i];
        g_wland_n = n;
    } else
        for (int i = 0; i < g_wres_n; i++) ww_mark_range(g_wres[i].a, g_wres[i].e);
    pm_unlock(&g_wland_lk);
    __atomic_store_n(&g_wres_n, 0, __ATOMIC_RELAXED);
    pm_unlock(&g_wres_lk);
}
/* whether the scanner scans leaf l for the epoch seen - 1: a check of the last epochs needed it, and it wasn't scanned
   in this epoch or a later one */
static int ww_wanted(u64 l, u32 seen, u32 from) {
    u32 u = __atomic_load_n(&g_wleaf_used[l], __ATOMIC_RELAXED);
    return u && (i32)(u - from) >= 0 && (i32)(__atomic_load_n(&g_wleaf_done[l], __ATOMIC_ACQUIRE) - seen) < 0;
}
/* Scans the leaves the checks of the last epochs needed each time an epoch starts, up through memory, and starts over
   for a newer epoch. A check that comes before the scanner got to its leaf scans it itself */
static void ww_scanner(void* arg) {
    u32 last = 0;
    (void)arg;
    SNX_THREAD_NAME("Vulkan write scans");
    if (g_perf_probes) perf_who_set(PWHO_SCANNER); /* PERF-32 */
    for (;;) {
        u32 e, seen, from;
        pm_lock(&g_wscan_wlk);
        while ((e = __atomic_load_n(&g_ww_epoch, __ATOMIC_ACQUIRE)) == last) pc_wait(&g_wscan_cv, &g_wscan_wlk, -1);
        pm_unlock(&g_wscan_wlk);
        last = e;
        seen = e + 1;
        from = __atomic_load_n(&g_ww_used_from, __ATOMIC_RELAXED);
        for (u64 l = 0, n = AS_SIZE >> WW_LEAF_SHIFT; l < n;) {
            if (!ww_wanted(l, seen, from)) {
                l++;
                continue;
            }
            u64 lo = l;
            while (l < n && l - lo < WW_RUN_MAX && ww_wanted(l, seen, from) && ww_claim(l)) l++;
            if (l == lo) { /* a check is scanning it */
                l++;
                continue;
            }
            if (ww_scan_leaves(lo, l - 1, seen, &g_wstage_scanner) < 0) return;
            if (__atomic_load_n(&g_ww_epoch, __ATOMIC_RELAXED) != e) break; /* a newer one: start over for it */
        }
    }
}
void vk_ww_new_epoch(void) {
    u32 e = __atomic_add_fetch(&g_ww_epoch, 1, __ATOMIC_RELEASE);
    if (t_ww_streaming) t_ww_stream = e; /* a wait of the running job ended: what the CPU wrote before is its to see */
    if (__atomic_load_n(&g_wscan_on, __ATOMIC_ACQUIRE)) {
        pm_lock(&g_wscan_wlk);
        pc_signal(&g_wscan_cv);
        pm_unlock(&g_wscan_wlk);
    }
}
u32 vk_ww_epoch(void) { return t_ww_streaming ? t_ww_stream : __atomic_load_n(&g_ww_epoch, __ATOMIC_ACQUIRE); }
void vk_ww_stream(int on) {
    t_ww_streaming = on;
    t_ww_stream = __atomic_load_n(&g_ww_epoch, __ATOMIC_ACQUIRE);
}
/* a check in the epoch seen - 1 needs leaves: the scanner's next passes scan what the last epochs needed */
static void ww_used_epoch(u32 seen) {
    u32 cur = g_ww_used[g_ww_used_i % WW_USED_EPOCHS];
    if (cur && (i32)(seen - cur) <= 0) return; /* this one, or an older one's check (another thread's) */
    g_ww_used[++g_ww_used_i % WW_USED_EPOCHS] = seen;
    u32 oldest = g_ww_used[(g_ww_used_i + 1) % WW_USED_EPOCHS];
    __atomic_store_n(&g_ww_used_from, oldest ? oldest : seen, __ATOMIC_RELAXED);
}

int vk_written_since(const u8* host, u64 len, u32 since) {
    if (!g_ww || !host || !len) return -1;
    if (g_ww < 0) {
        g_ww = plat_ww_scan(g_mem, 4096, ww_mark) == 0;
        LOG("vulkan: %s\n",
            g_ww ? "write tracking on (caches skip unchanged memory)" : "no write tracking (caches check contents)");
        if (!g_ww) return -1;
        pc_init(&g_wscan_cv);
        if (!plat_thread(ww_scanner, NULL, (size_t)1 << 20)) __atomic_store_n(&g_wscan_on, 1, __ATOMIC_RELEASE);
    }
    if (__atomic_load_n(&g_wscan_failed, __ATOMIC_ACQUIRE)) {
        g_ww = 0;
        LOG("vulkan: write tracking failed, caches check contents\n");
        return -1;
    }
    /* Existing cache baselines cannot be ordered against a wrapped clock. Fall back
       to content checks permanently before consuming any dirty pages at wrap. */
    if (g_wclock == UINT32_MAX) {
        g_ww = 0;
        return -1;
    }
    u64 o = (u64)(host - g_mem), lo = o >> WW_LEAF_SHIFT, hi = (o + len - 1) >> WW_LEAF_SHIFT, l;
    u32 seen = vk_ww_epoch() + 1;
    for (l = lo; l <= hi && (i32)(g_wleaf_seen[l] - seen) >= 0; l++) {}
    /* the clock moves only when this call marks pages, in ww_take_changed and ww_take_scanned when they take ranges,
       so what it marks is newer than every baseline taken before: a query that marks nothing, most of them, leaves
       it, and it doesn't reach its wrap within a session at thousands of queries a frame */
    ww_take_changed();
    if (l <= hi) {
        /* the leaves not taken in for this epoch: scanned in it by the scanner already, or here */
        u64 first = hi + 1, last = 0, a;
        ww_used_epoch(seen);
        for (a = l; a <= hi; a++) {
            if ((i32)(g_wleaf_seen[a] - seen) >= 0) continue;
            __atomic_store_n(&g_wleaf_used[a], seen, __ATOMIC_RELAXED);
            if ((i32)(__atomic_load_n(&g_wleaf_done[a], __ATOMIC_ACQUIRE) - seen) < 0) {
                if (first > hi) first = a;
                last = a;
            }
        }
        if (first <= hi) {
            int r = 0;
            /* the scanner may be scanning some of them: wait for those, which it may have scanned in this epoch */
            for (a = first; a <= last; a++)
                while (!ww_claim(a)) plat_yield();
            for (; first <= last && (i32)(__atomic_load_n(&g_wleaf_done[first], __ATOMIC_RELAXED) - seen) >= 0; first++)
                __atomic_store_n(&g_wleaf_busy[first], 0, __ATOMIC_RELEASE);
            for (; last > first && (i32)(__atomic_load_n(&g_wleaf_done[last], __ATOMIC_RELAXED) - seen) >= 0; last--)
                __atomic_store_n(&g_wleaf_busy[last], 0, __ATOMIC_RELEASE);
            if (first <= last) r = ww_scan_leaves(first, last, seen, &g_wstage_check);
            if (r < 0) {
                g_ww = 0;
                LOG("vulkan: write tracking failed, caches check contents\n");
                return -1;
            }
        }
        ww_take_scanned();
        for (a = l; a <= hi; a++)
            if ((i32)(g_wleaf_seen[a] - seen) < 0) g_wleaf_seen[a] = seen;
    }
    for (u64 pg = o >> 12, e = (o + len - 1) >> 12; pg <= e;) {
        u64 l = pg >> (WW_LEAF_SHIFT - 12), lend = (l + 1) << (WW_LEAF_SHIFT - 12);
        if (g_wleaf_max[l] > since) {
            if (!g_wleaf[l]) return 1; /* the whole leaf was marked */
            for (u64 q = pg; q <= e && q < lend; q++)
                if (g_wleaf[l][q & ((1u << (WW_LEAF_SHIFT - 12)) - 1)] > since) return 1;
        }
        pg = lend;
    }
    return 0;
}
u32 vk_ww_now(void) { return g_wclock; }
/* fn(user, at, n) for each run of [host, host + len) on pages written after since, at the range's byte offset at: for
   a range vk_written_since(host, len, since) has just found written, so its pages are marked */
static int page_written(u64 pg, u32 since) {
    u64 l = pg >> (WW_LEAF_SHIFT - 12);
    return g_wleaf_max[l] > since && (!g_wleaf[l] || g_wleaf[l][pg & ((1u << (WW_LEAF_SHIFT - 12)) - 1)] > since);
}
static void written_runs(const u8* host, u64 len, u32 since, void (*fn)(void*, u64, u64), void* user) {
    u64 o = (u64)(host - g_mem), first = o >> 12, last = (o + len - 1) >> 12, run = 0, pg;
    int in = 0;
    for (pg = first; pg <= last + 1; pg++) {
        int w = pg <= last && page_written(pg, since);
        if (w && !in) run = pg;
        if (!w && in) {
            u64 a = run << 12 > o ? (run << 12) - o : 0, b = pg << 12 < o + len ? (pg << 12) - o : len;
            fn(user, a, b - a);
        }
        in = w;
    }
}

static u8* g_tmp;
static u64 g_tmp_cap;
/* the guest bytes, without a copy when they are contiguous in host memory */
static const u8* guest_bytes(u64 gva, u64 len) {
    u8* p = gmem_ptr(gva, len);
    if (p) return p;
    if (g_tmp_cap < len) {
        u8* n = realloc(g_tmp, len);
        if (!n) return NULL;
        g_tmp = n;
        g_tmp_cap = len;
    }
    gmem_read(gva, g_tmp, len);
    return g_tmp;
}

int vk_buf_copy(u64 gva, u64 len, VkBuffer* buf, u64* off) {
    int src = gva ? gfx_gpu_src(gva, len, buf, off, 0, vk_stage_align(), PB_READ) : 0;
    if (src == 1) return 1;
    if (src == 2) return gfx_composed(gva, len, buf, off);
    u8* p = vk_stage(len ? len : 16, vk_stage_align(), buf, off);
    if (!p) return 0;
    if (!gva)
        memset(p, 0, len);
    else {
        const u8* src = guest_bytes(gva, len);
        if (!src) return 0;
        memcpy(p, src, len);
    }
    copy_note(gva, len);
    return 1;
}

static int range_owned(u64 gva, u64 len);
static int written_since_gva(u64 gva, u64 len, u32 since);
/* Keep an owned input on the GPU. Only the baseline needed to merge later CPU edits
   is copied to host-cached staging, asynchronously with the draw. */
static int writable_owned(GfxOut* o, u64 gva, u32 len, VkBuffer buf, u64 off) {
    VkBuffer back;
    u64 at;
    o->initial = vk_stage_back(len, &back, &at, &o->initial_mem);
    if (!o->initial) return 0;
    vk_perf_copy(PK_OWNED_BASE);
    vk_render_end();
    if (g_perf_times) vk_perf_copy_bytes(len);
    vk_guest_read(gva, len, o->ptr, buf, off);
    copy_barrier(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
                 VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    VkBufferCopy copy = {off, at, len};
    rec_CmdCopyBuffer(vk_cmd(), buf, back, 1, &copy);
    copy_barrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT,
                 VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                 VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
    return 1;
}

/* SNX_ARCEUS_PERF_INPLACE=4 (PERF-21, research 06:40): whether a pending result shares a byte with [gva, gva + len),
   as out_meets tests it (a page in common counts without a page map). gfx_overlay copies only such bytes, so with
   none it records nothing and its call can be skipped */
static int out_any_meets(u64 gva, u64 len) {
    RangePages q;
    u64 local[16], *rmap = NULL;
    int any = 0;
    range_pages(gva, len, &q);
    for (GfxOut* o = g_out; o && !any; o = o->next) any = out_meets(o, gva, len, &q, &rmap, local);
    if (rmap && rmap != local) free(rmap);
    return any;
}
/* PERF-36G: a composed capture, and whether it meets a result of a frame already submitted (PERF-36 could have that
   one in flight, and the capture's read of guest bytes would wait for it) */
static void composed_note(u64 gva, u64 len) {
    RangePages q;
    u64 local[16], *rmap = NULL;
    const u64 submitted = vk_last_submit();
    int any = 0;
    range_pages(gva, len, &q);
    for (GfxOut* o = g_out; o && !any; o = o->next)
        any = o->frame <= submitted && out_meets(o, gva, len, &q, &rmap, local);
    if (rmap && rmap != local) free(rmap);
    __atomic_fetch_add(&g_perf_vkx.composed, 1, __ATOMIC_RELAXED);
    if (any) __atomic_fetch_add(&g_perf_vkx.composed_submitted, 1, __ATOMIC_RELAXED);
}
/* SNX_ARCEUS_PERF_TIMES (PERF-20): the time since *t added to step k of st, the clock read again (t 0: off) */
static void wr_step(u64* t, u64* st, int k) {
    if (!*t) return;
    const u64 now = plat_ns();
    st[k] += now - *t;
    *t = now;
}
/* Each writable binding keeps its original bytes until its draw's frame completes. The guest can keep writing RAM:
   vk_write_changed resolves those writes against the GPU result at the fence. */
/* SNX_ARCEUS_PERF_TIMES (PERF-27): a capture made, with whether another GPU-side write or a publication stamped its
   pages since the frame before this one began (the record as stamped). The GPU thread's */
static void cap_note(u64 gva, u32 len) {
    static u64 gen[2]; /* the write generation at this frame's start, and at the one before's */
    const u64 frame = vk_frame_id();
    if (frame != g_perf_caps.frame) gen[1] = gen[0], gen[0] = gpu_mem_generation();
    int outside;
    unsigned kinds = 0;
    (void)gpu_mem_written_ask(gen[1], 0, gva, len, &outside, &kinds);
    perf_caps_add(&g_perf_caps, frame, gva, len, kinds & (1u << PSK_GPU | 1u << PSK_PUBLISH));
}
int vk_buf_copy_writable(u64 gva, u32 len, VkBuffer* buf, u64* off) {
    GfxOut* o;
    unlocked_note(VKU_COPY_WRITABLE);
    out_wrote(); /* every way out that hands a buffer is a write (SNX_ARCEUS_PERF_INPLACE=3), the zero one too */
    if (!gva) return vk_buf_copy(0, len, buf, off);
    /* Repeated bindings of one writable range in the same draw must name the same storage. */
    for (GfxOut* p = g_prep; p; p = p->next)
        if (p->addr == gva && p->size == len) {
            *buf = p->buf;
            *off = p->off;
            if (g_perf_times) vk_perf_made(PR_REPEAT);
            return 1;
        }
    int pending = gfx_gpu_src(gva, len, buf, off, 1, vk_stage_align(), PB_WRITE), copied = 0;
    if (pending == 1) return 1;
    if (pending == 2 && g_perf_probes) composed_note(gva, len);
    const u64 compose_t0 = g_perf_times && pending == 2 ? plat_ns() : 0; /* PERF-18 */
    u64 wr_t = g_perf_times ? plat_ns() : 0, st[PWS_N] = {0}; /* PERF-20: the steps' times */
    if (!pending) gmem_need(gva, len);
    int owned = !pending && range_owned(gva, len);
    if (owned) (void)written_since_gva(gva, len, 0); /* establish the CPU-write baseline before the GPU copy */
    wr_step(&wr_t, st, PWS_SETUP);
    o = gfx_out_new((u32)(((gva & 0xFFF) + len + 0xFFF) >> 12), len);
    if (!o) return 0;
    wr_step(&wr_t, st, PWS_RECORD);
    if (pending == 2) { /* guest memory's bytes, with the earlier results copied over them below (out_rebase) */
        read_untracked(gva, o->org, len);
        o->composed = 1;
    } else if (owned) {
        if (g_perf_small & PSM_NOZERO) memset(o->org, 0, len); /* as calloc left them (PERF-34 bit 2) */
    } else {
        const u8* src = guest_bytes(gva, len);
        if (!src) {
            free(o);
            return 0;
        }
        memcpy(o->org, src, len);
    }
    wr_step(&wr_t, st, PWS_ORIGINAL);
    out_map(o, gva);
    wr_step(&wr_t, st, PWS_MAP);
    u64 align = vk_stage_align();
    if (align < g_vk.props.limits.nonCoherentAtomSize) align = g_vk.props.limits.nonCoherentAtomSize;
    o->ptr = vk_stage_output(len, align, buf, off, &o->mem);
    if (!o->ptr) {
        free(o);
        return 0;
    }
    wr_step(&wr_t, st, PWS_STAGE);
    if (owned) {
        if (!writable_owned(o, gva, len, *buf, *off)) {
            free(o);
            return 0;
        }
    } else
        memcpy(o->ptr, o->org, len);
    wr_step(&wr_t, st, PWS_COPY);
    if (pending == 2 && (g_perf_inplace < 4 || out_any_meets(gva, len))) {
        if (g_perf_times) vk_perf_extra(PX_OVERLAY_ENTERED);
        copied = gfx_overlay(gva, len, *buf, *off, PK_COMPOSE_W);
    } else if (pending == 2 && g_perf_times) { /* counted as gfx_overlay would have: a composition with no byte */
        vk_perf_empty(PK_COMPOSE_W, len);
        vk_perf_extra(PX_OVERLAY_SKIPPED);
    }
    wr_step(&wr_t, st, PWS_OVERLAY);
    o->addr = gva;
    o->size = len;
    o->buf = *buf;
    o->off = *off;
    o->frame = vk_submit_number();
    o->frame_id = vk_frame_id();
    wmark_set(o); /* the draw being prepared writes it */
    if (g_prep_tail)
        g_prep_tail->next = o;
    else
        g_prep = o;
    g_prep_tail = o;
    walk_add(o);
    copy_note(gva, len);
    if (g_perf_times && g_perf_bind == PBK_XFB) cap_note(gva, len);
    if (g_perf_times)
        vk_perf_made(g_perf_bind == PBK_XFB        ? PR_XFB
                     : g_perf_bind == PBK_DISPATCH ? PR_DISPATCH
                     : pending == 2                ? PR_COMPOSED
                     : owned                       ? PR_OWNED
                                                   : PR_ORDINARY);
    if (compose_t0) perf_time_add(&g_perf, copied ? PT_COMPOSE_W_COPY : PT_COMPOSE_W_EMPTY, plat_ns() - compose_t0);
    if (wr_t) {
        wr_step(&wr_t, st, PWS_TAIL);
        vk_perf_writable(pending == 2 ? (copied ? PW_COMPOSED_BYTES : PW_COMPOSED_EMPTY) : owned ? PW_OWNED : PW_PLAIN,
                         st, len);
    }
    return 1;
}

typedef struct {
    u64 key, gva;
    int writable, bind;
} DrawPage;
static int draw_page_cmp(const void* a, const void* b) {
    u64 x = ((const DrawPage*)a)->key, y = ((const DrawPage*)b)->key;
    return (x > y) - (x < y);
}
static u64 draw_page_key(u64 gva) {
    u64 cpu = gmmu_translate(gva);
    /* Unmapped virtual pages are distinct and have no guest destination for writeback. */
    return cpu ? cpu >> 12 : (1ull << 63) | (gva >> 12);
}
static DrawPage* draw_pages(const VkDrawStorage* draw, size_t* count) {
    size_t n = 0;
    for (int s = 0; s < 2; s++)
        for (int i = 0; i < draw->n[s]; i++) {
            const VkDrawGbuf* b = &draw->b[s][i];
            if (b->addr) n += ((b->addr & 4095) + b->size + 4095) >> 12;
        }
    DrawPage* p = malloc((n ? n : 1) * sizeof *p);
    if (!p) return NULL;
    size_t k = 0;
    for (int s = 0; s < 2; s++)
        for (int i = 0; i < draw->n[s]; i++) {
            const VkDrawGbuf* b = &draw->b[s][i];
            if (!b->addr) continue;
            for (u64 g = b->addr & ~4095ull, e = b->addr + b->size; g < e; g += 4096)
                p[k++] = (DrawPage){draw_page_key(g), g, b->writable, s * 16 + i};
        }
    qsort(p, k, sizeof *p, draw_page_cmp);
    *count = k;
    return p;
}
static int draw_alias_bindings(const DrawPage* p, size_t n, u8 active[32]) {
    memset(active, 0, 32);
    int alias = 0;
    for (size_t i = 0; i < n;) {
        size_t j = i + 1;
        int writable = p[i].writable;
        while (j < n && p[j].key == p[i].key) writable |= p[j++].writable;
        if (j - i > 1 && writable) {
            for (size_t k = i; k < j; k++) active[p[k].bind] = 1;
            alias = 1;
        }
        i = j;
    }
    return alias;
}
static int draw_has_writable(const VkDrawStorage* draw) {
    for (int s = 0; s < 2; s++)
        for (int i = 0; i < draw->n[s]; i++)
            if (draw->b[s][i].addr && draw->b[s][i].writable) return 1;
    return 0;
}
/* Whether the draw's bindings may share a guest page with a writable one: 0 when each binding is one run of guest
   memory and their page ranges, compared pairwise, meet no writable one's, 1 otherwise (the page list decides). Every
   draw with storage asked the page list, allocated and sorted, twice: a twentieth of the GPU thread in one game */
static int draw_may_alias(const VkDrawStorage* draw) {
    u64 lo[32], hi[32];
    int w[32], k = 0;
    for (int s = 0; s < 2; s++)
        for (int i = 0; i < draw->n[s]; i++) {
            const VkDrawGbuf* b = &draw->b[s][i];
            u64 cpu = 0;
            if (!b->addr) continue;
            if (!b->size || gmmu_run(b->addr, b->size, &cpu) != b->size) return 1;
            lo[k] = cpu >> 12;
            hi[k] = (cpu + b->size - 1) >> 12;
            w[k] = b->writable;
            for (int j = 0; j < k; j++)
                if ((w[j] || w[k]) && lo[j] <= hi[k] && lo[k] <= hi[j]) return 1;
            k++;
        }
    return 0;
}
int vk_buf_draw_has_alias(const VkDrawStorage* draw) {
    if (!draw_has_writable(draw) || !draw_may_alias(draw)) return 0;
    size_t n;
    DrawPage* p = draw_pages(draw, &n);
    if (!p) return -1;
    u8 active[32];
    int alias = draw_alias_bindings(p, n, active);
    free(p);
    return alias;
}
static u32 draw_page_index(const DrawPage* p, size_t n, u64 key) {
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (p[mid].key < key)
            lo = mid + 1;
        else
            hi = mid;
    }
    return (u32)lo;
}
static int draw_output_page(const DrawPage* p, u8* ptr, VkBuffer buf, u64 off, VmaAllocation mem, int composed) {
    GfxOut* o;
    const u8* page;
    if (p->key & (1ull << 63)) return 1;
    o = gfx_out_new(1, 4096);
    if (!o) return 0;
    /* the original bytes from guest memory, which the atlas page was filled from: staging can be device-local
       memory behind the BAR, where a CPU read crosses PCIe uncached (this copy was most of the GPU thread's memcpy
       time in one game) */
    page = gmem_ptr_untracked(p->gva, 4096);
    if (page)
        memcpy(o->org, page, 4096);
    else
        memset(o->org, 0, 4096);
    o->pages[0] = o->vmap[0] = p->key;
    o->composed = composed;
    o->addr = p->gva;
    o->size = 4096;
    o->frame = vk_submit_number();
    o->frame_id = vk_frame_id();
    o->buf = buf;
    o->off = off;
    o->mem = mem;
    o->ptr = ptr;
    if (g_prep_tail)
        g_prep_tail->next = o;
    else
        g_prep = o;
    g_prep_tail = o;
    walk_add(o);
    out_wrote(); /* a result the draw writes (SNX_ARCEUS_PERF_INPLACE=3) */
    if (g_perf_times) vk_perf_made(PR_ATLAS);
    return 1;
}
/* An atlas page starts from guest memory, and the pending results over it are copied in on the GPU in the order they
   were recorded, after the shaders that wrote them: an aliased draw doesn't wait for earlier results to land in guest
   memory (one game drew about four a frame, each landing everything with a vk_finish). Its outputs keep guest memory as
   their original bytes until those results land (out_rebase). Compute results that haven't landed land for the
   atlas's pages alone. A result whose original bytes the guest has written since can't be copied whole: it lands
   first, as in gfx_gpu_src, which submits, and the draw starts its setup over. 1 when a result was copied in */
static int atlas_fill(const DrawPage* p, size_t unique, u8* ptr, VkBuffer atlas, u64 atlas_off) {
    CopyBatch c;
    size_t i;
    int landed = 0, composed = 0;
    if (g_cs_any)
        for (i = 0; i < unique; i++)
            if (!(p[i].key & (1ull << 63))) vk_compute_mem_need(p[i].gva, 4096);
    /* first, so a landing comes before memory is read; it frees the list, and leaves nothing to copy (nor o's next
       to read) */
    for (GfxOut* o = g_out; o && !landed && !g_out_landing; o = landed ? NULL : o->next)
        for (u64 g = o->addr & ~4095ull, e = o->addr + o->size; g < e && !landed; g += 4096) {
            u64 key = draw_page_key(g), lo = g > o->addr ? g : o->addr, hi = g + 4096 < e ? g + 4096 : e;
            u32 at = draw_page_index(p, unique, key);
            if (at < unique && p[at].key == key && !out_copyable(o, lo, hi)) {
                land_reason(WB_DRAW);
                landed = 1;
            }
        }
    for (i = 0; i < unique; i++) {
        const u8* page = p[i].key & (1ull << 63) ? NULL : gmem_ptr_untracked(p[i].gva, 4096);
        if (page)
            memcpy(ptr + i * 4096, page, 4096);
        else
            memset(ptr + i * 4096, 0, 4096);
    }
    memset(&c, 0, sizeof c);
    c.dst = atlas;
    c.site = PK_ATLAS;
    /* each byte from the newest result over it, as gfx_overlay takes them */
    Covered cov;
    GfxOut *nlocal[64], **newest = NULL;
    u32 n = 0;
    if (!g_out_landing && !(newest = out_newest_first(nlocal, 64, &n))) fatal("Out of memory composing a draw's atlas");
    cover_init(&cov, (u64)unique * 4096);
    for (u32 k = 0; k < n && !cover_full(&cov); k++) {
        const GfxOut* o = newest[k];
        for (u64 g = o->addr & ~4095ull, e = o->addr + o->size; g < e; g += 4096) {
            u64 key = draw_page_key(g), lo = g > o->addr ? g : o->addr, hi = g + 4096 < e ? g + 4096 : e;
            u32 at = draw_page_index(p, unique, key);
            if (at < unique && p[at].key == key) {
                Overlay v = {&c, o, atlas_off, at * 4096ull + (lo - g), lo - o->addr, &cov};
                u64 a = v.at, b = a + (hi - lo);
                if (!cover_take(&cov, a, b, overlay_part, &v)) overlay_part(&v, a, b);
                if (o->frame_id != vk_frame_id()) vk_frame_hold(o->frame_id);
                composed = 1;
            }
        }
    }
    copy_end(&c);
    cover_free(&cov);
    if (newest && newest != nlocal) free(newest);
    return composed;
}

/* All bindings of an aliasing draw index one page atlas. This also covers fragmented GPU mappings: the shader's
   word index is translated through its binding's page list, so a write through either VA reaches the same word. */
int vk_buf_draw_prepare(VkDrawStorage* draw) {
    size_t n = 0;
    int writable = draw_has_writable(draw) && draw_may_alias(draw);
    if (writable) out_wrote(); /* the atlas is bound writable (SNX_ARCEUS_PERF_INPLACE=3), its outputs at commit too */
    DrawPage* p = writable ? draw_pages(draw, &n) : NULL;
    if (writable && !p) return 0;
    u8 active[32] = {0};
    int alias = p ? draw_alias_bindings(p, n, active) : 0;
    size_t unique = 0;
    if (alias)
        for (size_t i = 0; i < n; i++)
            if (active[p[i].bind] && (!unique || p[i].key != p[unique - 1].key)) p[unique++] = p[i];
    VkBuffer atlas = VK_NULL_HANDLE;
    u64 atlas_off = 0, atlas_size = unique * 4096ull;
    VmaAllocation mem = VK_NULL_HANDLE;
    u8* ptr = NULL;
    int composed = 0;
    if (alias) {
        if (atlas_size > g_vk.props.limits.maxStorageBufferRange) {
            WARN_LIMIT(1, "vulkan: aliased draw needs %llu storage bytes, beyond the device's descriptor range\n",
                       (unsigned long long)atlas_size);
            free(p);
            return 0;
        }
        u64 align = vk_stage_align();
        if (align < g_vk.props.limits.nonCoherentAtomSize) align = g_vk.props.limits.nonCoherentAtomSize;
        ptr = vk_stage_output(atlas_size, align, &atlas, &atlas_off, &mem);
        if (!ptr) {
            free(p);
            return 0;
        }
        composed = atlas_fill(p, unique, ptr, atlas, atlas_off);
        copy_note(1, atlas_size);
    }
    for (int s = 0; s < 2; s++) {
        if (!draw->n[s]) continue;
        size_t words = 48;
        if (alias)
            for (int i = 0; i < draw->n[s]; i++) {
                VkDrawGbuf* b = &draw->b[s][i];
                if (b->addr && active[s * 16 + i]) words += ((b->addr & 4095) + b->size + 4095) >> 12;
            }
        u32* map = (u32*)vk_stage(words * 4, vk_stage_align(), &draw->map_buf[s], &draw->map_off[s]);
        if (!map) {
            free(p);
            return 0;
        }
        draw->map_size[s] = words * 4;
        for (int i = 0; i < 16; i++) map[i] = ~0u;
        for (int i = 16; i < 32; i++) map[i] = 0;
        for (int i = 32; i < 48; i++) map[i] = 0;
        size_t at = 48;
        if (alias)
            for (int i = 0; i < draw->n[s]; i++) {
                VkDrawGbuf* b = &draw->b[s][i];
                if (!b->addr || !active[s * 16 + i]) continue;
                map[i] = (u32)at;
                map[16 + i] = (u32)((b->addr & 4095) >> 2);
                map[32 + i] = b->size >> 2;
                for (u64 g = b->addr & ~4095ull, e = b->addr + b->size; g < e; g += 4096)
                    map[at++] = draw_page_index(p, unique, draw_page_key(g));
                b->aliased = 1;
                b->buf = atlas;
                b->off = atlas_off;
                b->range = atlas_size;
            }
        snx_le32_array(map, words); /* the shader reads the table little-endian */
    }
    if (alias)
        for (size_t i = 0; i < unique; i++)
            if (!draw_output_page(&p[i], ptr + i * 4096, atlas, atlas_off + i * 4096, mem, composed)) {
                vk_buf_writable_abort();
                free(p);
                return 0;
            }
    if (alias && copy_stats_on()) {
        g_alias_draws++;
        g_alias_pages += unique;
    }
    free(p);
    return 1;
}

/* the entry's data where draws read it */
/* SNX_ARCEUS_PERF_TIMES (PERF-22): the draws on each own buffer since it was made, by entry, allocated only then */
static u32 *g_own_uses, g_own_uses_cap;
static u32* own_uses(u32 i) {
    if (i >= g_own_uses_cap) {
        u32 cap = g_capvb > i ? g_capvb : i + 1;
        u32* n = realloc(g_own_uses, (size_t)cap * sizeof *n);
        if (!n) return NULL;
        memset(n + g_own_uses_cap, 0, (size_t)(cap - g_own_uses_cap) * sizeof *n);
        g_own_uses = n;
        g_own_uses_cap = cap;
    }
    return &g_own_uses[i];
}
static void use(u32 i, VkBuffer* buf, u64* off) {
    Vb* e = &g_vb[i];
    if (g_perf_times && e->own) {
        u32* u = own_uses(i);
        if (u) ++*u;
    }
    if (e->used != g_vk_present) {
        e->used = g_vk_present;
        if (e->own) { /* to the recent end */
            own_unlink(i);
            own_link(i);
        }
    }
    if (copy_stats_on()) g_read_bound += e->len;
    *buf = e->buf;
    *off = e->off;
}
static void drop_own(u32 i) {
    Vb* e = &g_vb[i];
    if (e->own && g_perf_times) {
        u32* u = own_uses(i);
        vk_perf_own_drop(u ? *u : 0);
    }
    if (e->own) {
        vk_defer_buffer(e->buf, e->mem);
        own_unlink(i);
        g_own.n--;
        g_own.bytes -= e->len;
    }
    e->own = 0;
    e->buf = VK_NULL_HANDLE;
}
/* A big own buffer takes the pages of its range written since it last matched memory (written_runs), through this
   frame's staging ring, with a copy on the GPU between the draws before and after: a write anywhere in a 64 MiB window
   (vk_buf_get_window) made it hash and copy all of it, again at each submit until it held still for two checks. In
   one game's underground, where terrain streams in as the player moves, that was most of each hitch */
#define PATCH_MIN (256u << 10)
#define PATCH_RUNS 64
typedef struct {
    const u8* src;
    VkBufferCopy r[PATCH_RUNS];
    u32 n;
    u64 bytes;
} Patch;
static void patch_note(void* user, u64 at, u64 n) {
    Patch* p = user;
    if (p->n < PATCH_RUNS) p->r[p->n] = (VkBufferCopy){0, at, n};
    p->n++;
    p->bytes += n;
}
static int patch_apply(Vb* e, Patch* pt);
static int patch_own(u32 i, const u8* host, u32 since) {
    Vb* e = &g_vb[i];
    Patch pt = {.src = host};
    written_runs(host, e->len, since, patch_note, &pt);
    if (pt.n > PATCH_RUNS) { /* scattered: from the first written byte to the end of the last run, in one copy */
        u64 o = (u64)(host - g_mem), a = pt.r[0].dstOffset, b = e->len;
        for (u64 pg = (o + e->len - 1) >> 12; pg > o >> 12 && !page_written(pg, since); pg--) b = (pg << 12) - o;
        pt.r[0] = (VkBufferCopy){0, a, b - a};
        pt.n = 1;
        pt.bytes = b - a;
    }
    return patch_apply(e, &pt);
}
/* pt's runs of the entry's bytes (pt->src) copied over its own buffer: staged, then copied on the GPU */
static int patch_apply(Vb* e, Patch* pt0) {
    Patch pt = *pt0;
    VkBuffer sb;
    u64 so, at = 0;
    u8* p;
    if (!pt.n) return 1;
    if (!(p = vk_stage_upload(pt.bytes, vk_stage_align(), &sb, &so))) return 0;
    for (u32 k = 0; k < pt.n; k++) {
        memcpy(p + at, pt.src + pt.r[k].dstOffset, pt.r[k].size);
        pt.r[k].srcOffset = so + at;
        at += pt.r[k].size;
    }
    if (copy_stats_on()) g_copy_owned += pt.bytes;
    vk_perf_copy(PK_PATCH);
    vk_render_end(); /* copies happen outside rendering passes */
    if (g_perf_times) vk_perf_copy_bytes(pt.bytes);
    /* the draws recorded and submitted before read the old bytes, and an earlier patch wrote them: both are done
       before the copy writes */
    copy_barrier(VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
                 VK_ACCESS_2_TRANSFER_WRITE_BIT);
    rec_CmdCopyBuffer(vk_cmd(), sb, e->buf, pt.n, pt.r);
    copy_barrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                 VK_ACCESS_2_MEMORY_READ_BIT);
    /* PATCH-HASH: the own buffer now holds bytes e->hash was not taken from. Bytes back as they were hashed would
       match it at the next full check, which instead takes the entry as changed */
    if (g_patch_hash) e->patched = 1;
    return 1;
}
/* An own buffer a GPU-side write touched (an inline upload, an engine's result landed in memory): the pages that write
   stamped (gpu_mem_dirty_since) and those the CPU wrote since (the kernel's dirty bits, which vk_written_since reads)
   copied again, instead of the whole range hashed again to learn whether anything changed. A write to one page of
   a big buffer hashed all of it: most of one game's GPU thread. 0 when there is no write tracking */
static int patch_dirty(u32 i, u64 gva, const u8* host, u64 gw, u32 ws) {
    Vb* e = &g_vb[i];
    Patch pt = {.src = host};
    u64 o = (u64)(host - g_mem), first = o >> 12, last = (o + e->len - 1) >> 12, run = 0;
    int in = 0;
    if (vk_written_since(host, e->len, ws) < 0) return 0; /* page_written is current from here */
    for (u64 pg = first; pg <= last + 1; pg++) {
        int d = 0;
        if (pg <= last) {
            u64 a = pg == first ? 0 : (pg << 12) - o, b = pg == last ? e->len : ((pg + 1) << 12) - o;
            d = page_written(pg, ws) || gpu_mem_dirty_since(gw, gva + a, b - a);
        }
        if (d && !in) run = pg;
        if (!d && in) {
            u64 a = run == first ? 0 : (run << 12) - o, b = pg > last ? e->len : (pg << 12) - o;
            patch_note(&pt, a, b - a);
        }
        in = d;
    }
    if (pt.n > PATCH_RUNS) { /* scattered: from the first dirty byte to the end of the range, in one copy */
        pt.r[0] = (VkBufferCopy){0, pt.r[0].dstOffset, e->len - pt.r[0].dstOffset};
        pt.n = 1;
        pt.bytes = pt.r[0].size;
    }
    return patch_apply(e, &pt);
}

/* An own buffer is device memory the GPU reads at full speed, filled by a copy from this frame's staging ring. Mapped
   memory for it is the BAR's window on most GPUs (256 MB, which VMA prefers for host writes), and thousands of own
   buffers there crowded out the staging rings: one game's rings, made again in the window as they grew after loading,
   took writes at a twentieth of their speed for minutes (18 to 25 fps against 43). sb at so is the entry's bytes, in
   the staging ring already */
static int make_own_(u32 i, VkBuffer sb, u64 so);
/* a buffer of its own being made, as a profiler zone (SNX_TRACY builds) */
static int make_own(u32 i, VkBuffer sb, u64 so) {
    SNX_ZONE_V(z, "own buffer", g_vb[i].len);
    int r = make_own_(i, sb, so);
    SNX_ZONE_END(z);
    return r;
}
static int make_own_(u32 i, VkBuffer sb, u64 so) {
    Vb* e = &g_vb[i];
    u64 len = e->len;
    VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                              .size = len,
                              .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                       VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                       VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT |
                                       VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                              .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    VmaAllocationCreateInfo aci = {.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};
    VkBuffer b;
    VmaAllocation m;
    if (vmaCreateBuffer(g_vk.vma, &bci, &aci, &b, &m, NULL) != VK_SUCCESS) return 0;
    if (g_perf_probes && len >= 4096) perf_mem_alloc(&g_perf_mem, PMA_OWN, len);
    vk_mem_track(m);
    if (copy_stats_on()) g_copy_owned += len;
    vk_perf_copy(PK_OWN);
    vk_render_end(); /* copies happen outside rendering passes */
    if (g_perf_times) vk_perf_copy_bytes(len);
    VkBufferCopy r = {so, 0, len};
    rec_CmdCopyBuffer(vk_cmd(), sb, b, 1, &r);
    copy_barrier(VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                 VK_ACCESS_2_MEMORY_READ_BIT);
    drop_own(i);
    if (g_perf_times) {
        u32* u = own_uses(i);
        if (u) *u = 0;
    }
    e->own = 1;
    e->buf = b;
    e->mem = m;
    e->off = 0;
    e->used = g_vk_present;
    own_link(i);
    g_own.n++;
    g_own.bytes += len;
    return 1;
}

static u32 g_vb_map_gen = 1;
void vk_buf_mapping_changed(void) {
    if (!++g_vb_map_gen) g_vb_map_gen = 1;
}
/* the host bytes of an entry's range, for the write tracking's question about them: kept with the generation of the GPU
   mappings, which a change of either kind moves (vk_buf_mapping_changed). An own buffer asked it each time it was
   checked, translating the range, 1.6% of one game's GPU thread. The question needs no more than the address: the
   pending results were checked already, and the bytes aren't read */
static const u8* entry_host(Vb* e, u64 gva, u64 len) {
    if (e->host_gen != g_vb_map_gen) {
        const u8* p = gmem_host(gva, len);
        e->host_cpu = p ? (u64)(p - g_mem) : ~0ull;
        e->host_gen = g_vb_map_gen;
    }
    return e->host_cpu == ~0ull ? NULL : GP(e->host_cpu);
}
/* vk_written_since by GPU address, run by run of the host memory behind it: a range the GPU maps from several pieces
   of guest memory (a buffer of several memory handles) has no one host pointer, and had no answer, so it was hashed
   whole at every check. In one game's matches that was 140 GB every 600 frames, nearly all of its GPU thread */
static int written_since_gva(u64 gva, u64 len, u32 since) {
    int any = 0;
    for (u64 o = 0; o < len;) {
        u64 cpu = 0, n = gmmu_run(gva + o, len - o, &cpu);
        if (!n) { /* an unmapped page: nothing writes it (a mapping change moves g_vb_map_gen) */
            n = 0x1000 - ((gva + o) & 0xFFF);
            if (n > len - o) n = len - o;
        } else {
            int w = vk_written_since(GP(cpu), n, since);
            if (w < 0) return -1;
            any |= w;
        }
        o += n;
    }
    return any;
}
/* whether GPU-owned bytes are on [gva, gva + len)'s pages (memory/own.c): reading them on the CPU lands them, a wait
   for the GPU (vk_lock held, as own_hash needs) */
static int range_owned(u64 gva, u64 len) {
    if (!__atomic_load_n(&g_own_any, __ATOMIC_ACQUIRE)) return 0;
    for (u64 o = 0; o < len;) {
        u64 cpu = 0, h = 0, n = gmmu_run(gva + o, len - o, &cpu);
        if (!n) {
            n = 0x1000 - ((gva + o) & 0xFFF);
            if (n > len - o) n = len - o;
        } else if (own_hash(cpu, n, &h))
            return 1;
        o += n;
    }
    return 0;
}
/* A range over bytes the GPU holds: copied into this frame's staging ring, the CPU's bytes by the CPU and the owned
   ones from their owners by the GPU (vk_guest_read), with nothing landed. It isn't hashed (the CPU's copy of the
   owned bytes is stale) or given a buffer of its own: it is copied again when the GPU writes those bytes again
   (gpu_mem_dirty_since) or a later submit reads it. Draws over the results of the GPU's engines landed them, a wait
   for the GPU each */
static int buf_get_owned(u32 i, Vb* e, u64 gva, u64 len, int strict, VkBuffer* buf, u64* off) {
    VkBuffer sb;
    u64 so;
    if (!e) {
        if (!(i = vb_new(gva, (u32)len))) return 0;
        e = &g_vb[i];
        e->cs_adds = vk_compute_adds(); /* checked for pending results above */
        e->gfx_adds = vk_buf_adds();
    }
    drop_own(i);
    e->hash = 0;
    e->stable = 0;
    vk_perf_copy(PK_OWNED_READ);
    vk_render_end(); /* the GPU's copies happen outside rendering passes */
    u8* p = vk_stage_upload(len, vk_stage_align(), &sb, &so);
    if (!p) return 0;
    if (g_perf_times) vk_perf_copy_bytes(len);
    vk_guest_read(gva, len, p, sb, so);
    if (copy_stats_on()) g_copy_stage += len;
    e->buf = sb;
    e->off = so;
    e->frame = vk_submit_number();
    e->epoch = e->chk_epoch = g_vk_epoch;
    if (strict) e->strict_epoch = g_vk_epoch;
    e->gw = gpu_mem_generation();
    e->wseen = vk_ww_now();
    use(i, buf, off);
    return 1;
}
/* SNX_ARCEUS_PERF_TIMES (PERF-22): buf_get's reason (ST_*) as perf_times' (POW_*), and a slow path counted */
static int own_why(int st) {
    static const int map[ST_N] = {POW_NEW, POW_NOBUF, POW_SUBMIT, POW_DIRTY, POW_EPOCH, POW_WRITTEN};
    return st >= 0 && st < ST_N ? map[st] : POW_NEW;
}
static void own_note(int what, int why, u64 gva, u64 len, const u64* ns) {
    vk_perf_own(what, why, len, perf_landed_inside(g_perf_landed, gva, len, vk_frame_id()), ns);
}
/* SNX_ARCEUS_PERF_TIMES (PERF-25, research 23:56): a lookup that ended staged or own_made (made) inside a landed
   result, with its ns at the site and the passes it ended. What wrote its range since the landing: a later result
   (the table), the front end by page (gpu_mem_dirty_since) and by byte (the record as stamped), the CPU by page.
   A write tracking query here, and one at the landing, are this probe's own */
static void landed_note(int made, u64 gva, u64 len, u64 ns, u64 ends, int slot) {
    const u64 frame = vk_frame_id(), now = vk_submit_number();
    PerfLanded r[2];
    const int kind = perf_landed_find(g_perf_landed, gva, len, frame, r);
    if (kind == PLK_NONE) return;
    int age = 0, freed = 0;
    unsigned wrote = 0;
    for (int k = 0; k < (kind == PLK_TWO ? 2 : 1); k++) { /* two results: each part from its own landing, the worst */
        const u64 a = gva > r[k].start ? gva : r[k].start, b = gva + len < r[k].end ? gva + len : r[k].end;
        const u64 since = now - r[k].submit;
        const int g = since == 0 ? 0 : since == 1 ? 1 : since < 4 ? 2 : 3;
        u64 at;
        if (g > age) age = g;
        if (vk_frame_freed(r[k].made, &at)) {
            const u64 gone = now - at;
            const int fr = gone == 0 ? 1 : gone == 1 ? 2 : gone < 4 ? 3 : 4;
            if (fr > freed) freed = fr;
            vk_perf_landed_kept(perf_landed_keep(g_perf_landed, &r[k]));
        }
        int outside;
        unsigned kinds;
        if (perf_landed_later(g_perf_landed, r[k].seq, a, b - a, frame)) wrote |= 1u << PLW_LATER;
        if (gpu_mem_dirty_since(r[k].gen, a, b - a)) wrote |= 1u << PLW_FRONT_PAGE;
        if (gpu_mem_written_ask(r[k].gen, 0, a, b - a, &outside, &kinds) != PSA_NONE) wrote |= 1u << PLW_FRONT_BYTE;
        if (written_since_gva(a, b - a, r[k].wclock) != 0) wrote |= 1u << PLW_CPU; /* -1, no tracking: not cleared */
    }
    const unsigned page = 1u << PLW_LATER | 1u << PLW_FRONT_PAGE | 1u << PLW_CPU;
    const unsigned byte = 1u << PLW_LATER | 1u << PLW_FRONT_BYTE | 1u << PLW_CPU;
    vk_perf_landed(made, kind, age, freed, !(wrote & page) ? 0 : !(wrote & byte) ? 1 : 2, wrote, ns, ends, slot);
}
/* PERF-23: step k (POS_*) ran from since to now, which is returned */
static u64 perf_step(u64* st, u8* ran, int k, u64 since) {
    const u64 now = plat_ns();
    st[k] += now - since;
    ran[k] = 1;
    return now;
}
/* SNX_ARCEUS_PERF_TIMES (PERF-23): the host pages of [gva, gva + len) the CPU wrote after since, each to the window's
   set, and the range's bytes on them. For a range vk_written_since has just asked about, so its pages are marked */
static u64 written_pages_note(u64 gva, u64 len, u32 since) {
    u64 bytes = 0;
    for (u64 o = 0; o < len;) {
        u64 cpu = 0, n = gmmu_run(gva + o, len - o, &cpu);
        if (!n) {
            n = 0x1000 - ((gva + o) & 0xFFF);
            if (n > len - o) n = len - o;
        } else {
            const u64 h = (u64)(GP(cpu) - g_mem);
            for (u64 pg = h >> 12, last = (h + n - 1) >> 12; pg <= last; pg++)
                if (page_written(pg, since)) {
                    const u64 lo = pg << 12 > h ? pg << 12 : h, hi = (pg + 1) << 12 < h + n ? (pg + 1) << 12 : h + n;
                    bytes += hi - lo;
                    vk_perf_wpage(pg);
                }
        }
        o += n;
    }
    return bytes;
}
static int buf_get(u64 gva, u64 len, VkBuffer* buf, u64* off, int strict, u64 align) {
    int pending;
    u32 i = 0;
    Vb* e = NULL;
    u64 pre_written = 0, pre_patch = 0; /* PERF-23: the written check, and a patch that failed, before the slow path */
    int seen_w = 2, pre_ran = 0;        /* the written check's answer (2: not asked); bit 0 a patch tried, 1 asked */
    if (!len || len > 0xFFFFFFFFull) return 0; /* no buffer the GPU reads is 4 GB */
    if (g_vbi) {
        u32 sl = vb_slot(gva, (u32)len);
        if ((i = g_vbi[sl].idx)) e = &g_vb[i];
    }
    /* Pending results over the range, unless none was added since they were last checked for here (only the sweep in
       vk_buf_frame frees entries, and only this function adds them, so e stays good meanwhile) */
    if (!e || e->cs_adds != vk_compute_adds() || e->gfx_adds != vk_buf_adds()) {
        pending = gfx_gpu_src(gva, len, buf, off, 0, align, PB_INPUT);
        if (pending == 1) return 1;
        if (pending == 2) return gfx_composed(gva, len, buf, off); /* newer than any cached copy */
        if (vk_compute_gpu_src(gva, len, buf, off)) return 1;      /* a compute result that hasn't landed yet */
        /* one that covers the range only in part lands first (its write shows in gpu_mem_dirty_since below): the
           cached copy would otherwise be used without it */
        if (g_cs_any) vk_compute_mem_need(gva, len);
        if (e) {
            e->cs_adds = vk_compute_adds();
            e->gfx_adds = vk_buf_adds();
        }
    }
    /* valid: the data is where draws can read it this frame */
    int valid = e && e->buf && (e->own || e->frame == vk_submit_number());
    if (valid && !gpu_mem_dirty_since(e->gw, gva, len)) {
        if (e->epoch == g_vk_epoch && (!strict || e->strict_epoch == g_vk_epoch)) {
            use(i, buf, off);
            return 1;
        }
        /* with write tracking, an own buffer is checked only when its memory was written */
        const u64 wt0 = g_perf_times && e->own ? plat_ns() : 0;
        const u8* host = e->own ? entry_host(e, gva, len) : NULL;
        int w = !e->own ? -1 : host ? vk_written_since(host, len, e->wseen) : written_since_gva(gva, len, e->wseen);
        if (wt0) {
            pre_written = plat_ns() - wt0;
            pre_ran |= 2;
        }
        seen_w = w;
        if (w == 1 && host && len >= PATCH_MIN) { /* the written pages, not the whole range again */
            u32 wnow = vk_ww_now();
            const u64 pt0 = g_perf_times ? plat_ns() : 0;
            if (patch_own(i, host, e->wseen)) {
                if (pt0) {
                    const u64 ns = plat_ns() - pt0;
                    own_note(PO_PATCH, POW_WRITTEN, gva, len, (u64[POT_N]){0, 0, ns});
                    vk_perf_steps(PO_PATCH, (u64[POS_N]){[POS_PATCH] = ns, [POS_WRITTEN] = pre_written},
                                  (u8[POS_N]){[POS_PATCH] = 1, [POS_WRITTEN] = 1});
                }
                e->wseen = wnow;
                w = 0;
            } else if (pt0) {
                pre_patch = plat_ns() - pt0;
                pre_ran |= 1;
            }
        }
        if (w == 0) {
            if (wt0 && seen_w == 0) vk_perf_quiet(pre_written);
            e->epoch = g_vk_epoch;
            if (strict) e->strict_epoch = g_vk_epoch;
            use(i, buf, off);
            return 1;
        }
        /* without it: own buffers that stayed the same for long are checked every few submits, big ones sampled in
           between. A CPU write in between shows that many submits late; writes of the GPU front end always show at
           once (gpu_mem_dirty_since above) */
        /* PATCH-HASH: none of the three for an entry patched since its hash, whose hash and sample are older */
        if (!strict && w < 0 && e->own && !e->patched && e->stable >= 16 && len >= (64u << 10) &&
            g_vk_epoch - e->chk_epoch < 8) {
            e->epoch = g_vk_epoch;
            use(i, buf, off);
            return 1;
        }
        if (!strict && w < 0 && e->own && !e->patched && e->stable >= 32 && len < (64u << 10) &&
            g_vk_epoch - e->chk_epoch < 4) {
            e->epoch = g_vk_epoch;
            use(i, buf, off);
            return 1;
        }
        if (!strict && w < 0 && e->own && !e->patched && e->stable >= 64 && len >= 4096 && !e->sskip &&
            g_vk_epoch - e->chk_epoch < (e->stable >= 256 ? 240u : 64u)) {
            if (g_vk_epoch - e->schk < 4 || vk_sample_hash(gva, len) == e->shash) {
                if (g_vk_epoch - e->schk >= 4) e->schk = g_vk_epoch;
                e->epoch = g_vk_epoch;
                use(i, buf, off);
                return 1;
            }
        }
    }
    else if (valid && e->own && len >= PATCH_MIN && !range_owned(gva, len)) {
        /* a GPU-side write on its pages: those pages copied again, not the range hashed again. Not a small range: one
           is hashed in microseconds, and GPU-side writes beside it stamp its pages (in one game 760 ranges a frame,
           78% of them unchanged), each copy ending the rendering pass */
        const u8* host = entry_host(e, gva, len);
        u32 wnow = vk_ww_now();
        u64 gnow = gpu_mem_generation(); /* both before the bytes are read */
        const u64 pt0 = g_perf_times ? plat_ns() : 0;
        if (host && patch_dirty(i, gva, host, e->gw, e->wseen)) {
            if (pt0) {
                const u64 ns = plat_ns() - pt0;
                own_note(PO_PATCH, POW_DIRTY, gva, len, (u64[POT_N]){0, 0, ns});
                vk_perf_steps(PO_PATCH, (u64[POS_N]){[POS_PATCH] = ns}, (u8[POS_N]){[POS_PATCH] = 1});
            }
            e->gw = gnow;
            e->wseen = wnow;
            e->epoch = g_vk_epoch;
            if (strict) e->strict_epoch = g_vk_epoch;
            use(i, buf, off);
            return 1;
        }
        if (pt0 && host) { /* tried and failed: without a host pointer it isn't tried */
            pre_patch = plat_ns() - pt0;
            pre_ran |= 1;
        }
    }
    const u64 why0 = g_perf_times ? plat_ns() : 0, gw0 = e ? e->gw : 0; /* PERF-23: as before this check */
    const u32 ws0 = e ? e->wseen : 0;
    int why = 0;
    if (copy_stats_on() || g_perf_times)
        why = !e ? ST_NEW
            : !e->buf ? ST_NOBUF
            : !valid ? ST_SUBMIT
            : gpu_mem_dirty_since(e->gw, gva, len) ? ST_DIRTY
            : e->own ? ST_WRITTEN
                     : ST_EPOCH;
    u64 ot = g_perf_times ? plat_ns() : 0, ons[POT_N] = {0}; /* PERF-22: the slow path's time, by part */
    const u64 ot0 = ot, pe0 = ot0 ? g_perf_cp.pass_end_n : 0; /* PERF-25: the passes ended before it */
    u64 st[POS_N] = {0}, mk = ot0; /* PERF-23: by step, mk the last step's end */
    u8 ran[POS_N] = {0};
    if (ot0) {
        st[POS_PATCH] = pre_patch, ran[POS_PATCH] = pre_ran & 1;
        st[POS_WRITTEN] = pre_written, ran[POS_WRITTEN] = pre_ran >> 1 & 1;
        st[POS_WHY] = ot0 - why0, ran[POS_WHY] = 1;
    }
    const int owned = range_owned(gva, len);
    if (ot0) mk = perf_step(st, ran, POS_OWNED, mk);
    if (owned) {
        const int r = buf_get_owned(i, e, gva, len, strict, buf, off);
        if (ot0 && r) {
            own_note(PO_OWNED_READ, own_why(why), gva, len, (u64[POT_N]){0, 0, plat_ns() - ot0});
            perf_step(st, ran, POS_OTHER, mk);
            vk_perf_steps(PO_OWNED_READ, st, ran);
        }
        return r;
    }
    u32 wnow = vk_ww_now();
    const u8* src = guest_bytes(gva, len);
    if (!src) return 0;
    if (ot0) mk = perf_step(st, ran, POS_BYTES, mk);
    /* Data without a buffer of its own goes into this frame's staging ring whatever its hash says, so it is copied as
       it is hashed and read once: reading it twice, for the hash and then the copy, was half of one game's GPU thread */
    VkBuffer sb = VK_NULL_HANDLE;
    u64 so = 0, h;
    u8* staged = NULL;
    if (!e || !e->own) {
        if (e) e->buf = VK_NULL_HANDLE; /* a failed allocation must not leave the old snapshot reusable */
        if (!(staged = vk_stage_upload(len, vk_stage_align(), &sb, &so))) return 0;
        h = vk_hash_copy(staged, src, len);
    } else
        h = vk_hash(src, len);
    if (ot) { /* the guest bytes, their hash and copy */
        const u64 now = plat_ns();
        ons[POT_HASH] += now - ot;
        ot = now;
        st[POS_HASH] += now - mk, ran[POS_HASH] = 1;
    }
    if (!e) {
        if (!(i = vb_new(gva, (u32)len))) return 0;
        e = &g_vb[i];
        e->hash = h ^ 1;
        e->cs_adds = vk_compute_adds(); /* checked for pending results above */
        e->gfx_adds = vk_buf_adds();
    }
    /* PATCH-HASH: after a patch a matching hash says nothing of the own buffer: the entry is taken as changed */
    const int patched = e->patched;
    if (patched) e->patched = 0;
    int changed = e->hash != h || patched, made = 0, own_before = e->own;
    if (!changed && e->stable < 100000)
        e->stable++;
    else if (changed) {
        e->hash = h;
        e->stable = 0;
        drop_own(i);
    }
    e->epoch = e->chk_epoch = g_vk_epoch;
    if (strict) e->strict_epoch = g_vk_epoch;
    e->gw = gpu_mem_generation();
    e->wseen = wnow;
    if (e->stable >= 63 && len >= 4096) {
        if (g_perf_sample && g_ww > 0) { /* PERF-24: only the shortcut without write tracking reads the sample */
            e->sskip = 1;
            if (ot0) vk_perf_sample_skip();
        } else {
            const u64 s0 = ot0 ? plat_ns() : 0;
            e->shash = vk_sample_hash(gva, len);
            e->schk = g_vk_epoch;
            e->sskip = 0;
            if (s0) perf_step(st, ran, POS_SAMPLE, s0);
        }
    }
    if (!e->own) {
        const u64 c0 = ot0 ? plat_ns() : 0;
        if (!staged) { /* its own buffer was dropped above: the bytes changed */
            if (!(staged = vk_stage_upload(len, vk_stage_align(), &sb, &so))) return 0;
            memcpy(staged, src, len);
        }
        if (ot) {
            const u64 now = plat_ns();
            ons[POT_HASH] += now - ot;
            ot = now;
            st[POS_HASH] += now - c0;
        }
        /* a big range takes its own buffer at once when writes are tracked: changes then patch it (patch_own) */
        const int try_own = e->stable >= 2 || (len >= PATCH_MIN && g_ww > 0);
        made = try_own && make_own(i, sb, so);
        if (ot && try_own) {
            const u64 now = plat_ns();
            ons[POT_CREATE] += now - ot;
            ot = now;
        }
        if (!made) {
            /* changing (or new) data: read from this frame's staging ring */
            e->buf = sb;
            e->off = so;
            if (copy_stats_on()) {
                g_copy_stage += len;
                stage_note(why, changed, gva, len);
            }
            e->frame = vk_submit_number();
        }
    }
    const u64 u0 = ot0 ? plat_ns() : 0;
    use(i, buf, off);
    if (ot0) {
        const u64 end = perf_step(st, ran, POS_USE, u0);
        ons[POT_REST] = end - ot0 - ons[POT_HASH] - ons[POT_CREATE];
        const int what = made ? PO_OWN_MADE : own_before && e->own ? PO_OWN_SAME : PO_STAGED;
        own_note(what, own_why(why), gva, len, ons);
        u64 in = ons[POT_CREATE];
        for (int k = POS_OWNED; k < POS_OTHER; k++) in += st[k];
        st[POS_OTHER] = end - ot0 - in, ran[POS_OTHER] = 1;
        vk_perf_steps(what, st, ran);
        if (what == PO_STAGED || what == PO_OWN_MADE) /* PERF-25 */
            landed_note(what == PO_OWN_MADE, gva, len, end - ot0, g_perf_cp.pass_end_n - pe0, g_perf_cp.end_slot);
        if (what == PO_OWN_SAME && why == ST_DIRTY) { /* the GPU-side write to the byte, as stamped and as written */
            unsigned cleared = 0; /* PCL_* */
            for (int real = 0; real < 1 + (g_perf_times > 1); real++) { /* as written only at PERF_TIMES=2 */
                int outside;
                unsigned kinds;
                const int q = gpu_mem_written_ask(gw0, real, gva, len, &outside, &kinds);
                if (q == PSA_NONE) cleared |= real ? PCL_REAL : PCL_BYTES | (outside > 0 ? PCL_SPAN : 0);
                vk_perf_gsame(real,
                              q == PSA_IN        ? PSQ_IN
                              : q == PSA_UNKNOWN ? PSQ_UNKNOWN
                              : outside > 0      ? PSQ_OUT
                              : outside == 0     ? PSQ_SPAN
                                                 : PSQ_SPAN_UNKNOWN,
                              kinds);
            }
            vk_perf_cleared(cleared);
        }
        if (what == PO_OWN_SAME && why == ST_WRITTEN)
            vk_perf_csame(seen_w == 1, len >= PATCH_MIN, len, seen_w == 1 ? written_pages_note(gva, len, ws0) : 0);
        vk_perf_probe(plat_ns() - end); /* these asks and walks: outside every step above */
    }
    return 1;
}

int vk_buf_get(u64 gva, u64 len, VkBuffer* buf, u64* off) { return buf_get(gva, len, buf, off, 0, vk_stage_align()); }
/* Vertex and index buffers bind at any offset that keeps their elements aligned, not only at the uniform and storage
   bindings' alignment: a draw reading an earlier draw's storage result at another offset reads its buffer in place
   instead of composing it over guest memory. In one game 216,000 vertex buffer bindings in 600 frames were composed
   that way, each a copy of its range into staging and a GPU copy of the result over it */
int vk_buf_get_input(u64 gva, u64 len, u64 align, VkBuffer* buf, u64* off) {
    return buf_get(gva, len, buf, off, 0, align);
}

/* Windows: guest ranges that storage buffers with no size of their own (a bare address in a program's constant buffer,
   gbuf_tag_any) are bound inside, so draws whose ranges overlap read one copy of guest memory, each at its own offset
   into it. Each such buffer takes the 16 MiB after its address (vk_gbuf_desc): one game's terrain tiles, each with its
   own address about 1 MB after the last, made thirty overlapping copies of 16 MiB, 430 MB, and a write under them made
   up to sixteen of them copy again. A range in no window gets one of WIN_SIZE from the MiB it starts in, as far as
   guest memory runs on and short of a window above, which the ranges after it fall in until the next window. Sized up
   front rather than grown into the union of the ranges that meet it: those come one draw after another, often within a
   frame, and each growth would copy the whole window again. Windows go when the GPU mappings change (their run of guest
   memory may have) and when unused for a while, before the cache's entries for them age out */
#define WIN_N    32
#define WIN_SIZE (64ull << 20)
#define WIN_IDLE 1200 /* presents */
typedef struct {
    u64 start, end; /* end 0: none */
    u32 used, gen;  /* the present it was last bound at, g_vb_map_gen when made */
} Win;
static Win g_win[WIN_N];
static int win_live(const Win* w) {
    return w->end && w->gen == g_vb_map_gen && (u32)(g_vk_present - w->used) < WIN_IDLE;
}
int vk_buf_get_window(u64 gva, u64 len, VkBuffer* buf, u64* off) {
    u64 align = g_vk.props.limits.minStorageBufferOffsetAlignment, at, cpu = 0;
    int slot = -1;
    if (!len || gva + len < gva) return 0;
    for (int i = 0; i < WIN_N && slot < 0; i++)
        if (win_live(&g_win[i]) && g_win[i].start <= gva && gva + len <= g_win[i].end) slot = i;
    if (slot < 0) {
        u64 start = gva & ~((1ull << 20) - 1), run;
        /* the MiB's start may lie before the run of guest memory the range is in: the range's own start then */
        if (gmmu_run(start, gva + len - start, &cpu) < gva + len - start) start = gva;
        run = gmmu_run(start, WIN_SIZE, &cpu) & ~3ull;
        if (run < gva + len - start) /* not one run: the range alone */
            return buf_get(gva, len, buf, off, 0, vk_stage_align());
        /* a window above, which ranges from its start on fall in: this one needs to hold no more than the ranges
           starting before it, so the two overlap by a range's length at most (ranges come in any order) */
        for (int i = 0; i < WIN_N; i++)
            if (win_live(&g_win[i]) && g_win[i].start > gva && g_win[i].start - start < run) {
                u64 need = g_win[i].start + len - start;
                if (need < run) run = need;
            }
        for (int i = 0; i < WIN_N && slot < 0; i++)
            if (!win_live(&g_win[i])) slot = i;
        if (slot < 0) /* all live: the one bound longest ago */
            for (int i = slot = 0; i < WIN_N; i++)
                if ((i32)(g_win[i].used - g_win[slot].used) < 0) slot = i;
        g_win[slot] = (Win){start, start + run, 0, g_vb_map_gen};
    }
    g_win[slot].used = g_vk_present;
    if (!buf_get(g_win[slot].start, g_win[slot].end - g_win[slot].start, buf, &at, 0, vk_stage_align())) return 0;
    /* a binding's offset must be a multiple of the device's alignment: a range that would land off it is copied
       alone */
    if ((at + (gva - g_win[slot].start)) % (align ? align : 1)) return buf_get(gva, len, buf, off, 0, vk_stage_align());
    *off = at + (gva - g_win[slot].start);
    return 1;
}
/* A draw looks its buffers up one after another, and each lookup waited on two cache misses in a row, its index slot
   and then the entry the slot names: 88% of buf_get's own time in one game. A caller that knows its ranges first
   fetches every slot (step 0), then every entry they name (step 1), so the misses overlap */
void vk_buf_prefetch(u64 gva, u64 len, int step) {
    u32 sl;
    if (!g_vbi || !len || len > 0xFFFFFFFFull) return;
    sl = vb_home(gva, (u32)len);
    if (!step) {
        __builtin_prefetch(&g_vbi[sl]);
        return;
    }
    if (g_vbi[sl].idx && g_vbi[sl].start == gva && g_vbi[sl].len == (u32)len)
        for (size_t o = 0; o < sizeof(Vb); o += 64) __builtin_prefetch((const char*)&g_vb[g_vbi[sl].idx] + o);
}
/* Constant buffers cannot accept the delayed checks used for stable vertex data: a changed word affects this draw. */
int vk_buf_get_strict(u64 gva, u64 len, VkBuffer* buf, u64* off) {
    return buf_get(gva, len, buf, off, 1, vk_stage_align());
}

u64 vk_buf_reclaim(u32 before, u64 want) {
    u64 freed = 0;
    while (g_own.head && freed < want && (i32)(g_vb[g_own.head].used - before) < 0) {
        u32 i = g_own.head;
        freed += g_vb[i].len;
        drop_own(i); /* the entry stays: its range is copied into a buffer again when next read */
        vk_defer_bytes(g_vb[i].len);
    }
    return freed;
}
void vk_buf_stats(u32* count, u64* bytes) {
    *count = g_own.n;
    *bytes = g_own.bytes;
}

/* buffers not read for twenty seconds, and in 120 presents, go */
void vk_buf_frame(void) {
    if (g_vk_present % 4) return;
    if (copy_stats_on() && g_vk_present % 600 == 0) {
        LOG("vulkan: buffer bytes over 600 presents: uncached=%llu zero=%llu calls=%llu peak=%llu staged=%llu owned=%llu bound=%llu writeback=%llu/%llu waits=%llu/%llums reused=%llu\n",
            (unsigned long long)g_copy_uncached, (unsigned long long)g_copy_zero, (unsigned long long)g_copy_calls,
            (unsigned long long)g_copy_peak, (unsigned long long)g_copy_stage, (unsigned long long)g_copy_owned,
            (unsigned long long)g_read_bound, (unsigned long long)g_wb_outputs, (unsigned long long)g_wb_bytes,
            (unsigned long long)g_wb_waits, (unsigned long long)(g_wb_wait_ns / 1000000),
            (unsigned long long)g_wb_reuses);
        stage_report();
        LOG("vulkan: buffer waits: draw=%llu/%llums read=%llu/%llums write=%llu/%llums signal=%llu/%llums syncwait=%llu/%llums map=%llu/%llums draw_landed=old_frame:%llu changed:%llu composed=%llu+%llu\n",
            (unsigned long long)g_wb_reason[WB_DRAW], (unsigned long long)(g_wb_reason_ns[WB_DRAW] / 1000000),
            (unsigned long long)g_wb_reason[WB_READ], (unsigned long long)(g_wb_reason_ns[WB_READ] / 1000000),
            (unsigned long long)g_wb_reason[WB_WRITE], (unsigned long long)(g_wb_reason_ns[WB_WRITE] / 1000000),
            (unsigned long long)g_wb_reason[WB_SIGNAL], (unsigned long long)(g_wb_reason_ns[WB_SIGNAL] / 1000000),
            (unsigned long long)g_wb_reason[WB_SYNCWAIT], (unsigned long long)(g_wb_reason_ns[WB_SYNCWAIT] / 1000000),
            (unsigned long long)g_wb_reason[WB_MAP], (unsigned long long)(g_wb_reason_ns[WB_MAP] / 1000000),
            (unsigned long long)g_wb_draw_miss[0], (unsigned long long)g_wb_draw_miss[2],
            (unsigned long long)g_wb_draw_miss[1], (unsigned long long)g_wb_draw_miss[3]);
        LOG("vulkan: aliased storage draws=%llu pages=%llu; buffer cache entries=%u, lookups=%llu probing %llu more "
            "slots\n",
            (unsigned long long)g_alias_draws, (unsigned long long)g_alias_pages, g_vblive,
            (unsigned long long)g_vb_lookups, (unsigned long long)g_vb_probes);
        g_vb_lookups = g_vb_probes = 0;
        g_copy_uncached = g_copy_stage = g_copy_owned = g_copy_zero = g_copy_calls = g_copy_peak = g_read_bound = 0;
        g_wb_outputs = g_wb_bytes = g_wb_waits = g_wb_wait_ns = g_wb_reuses = 0;
        g_alias_draws = g_alias_pages = 0;
        memset(g_wb_reason, 0, sizeof g_wb_reason);
        memset(g_wb_reason_ns, 0, sizeof g_wb_reason_ns);
        memset(g_wb_draw_miss, 0, sizeof g_wb_draw_miss);
    }
    /* a sixteenth of the entries every fourth present: an entry goes at most 64 presents after its time. One with a
       buffer of its own goes after twenty seconds unused. One without holds only what its next check compares with:
       ranges read once (constants streamed through a ring, vertices at new offsets) made most of the entries, and the
       lookups of the ones in use missed the cache over them, so those go after two seconds */
    static u32 sweep;
    u32 before = vk_present_before(20000000000ull, 120), before_copy = vk_present_before(2000000000ull, 12);
    if ((!before && !before_copy) || !g_nvb) return;
    u32 chunk = (g_nvb + 15) / 16, from = (sweep++ & 15) * chunk;
    for (u32 i = from ? from : 1; i < from + chunk && i < g_nvb; i++) {
        u32 by = g_vb[i].own ? before : before_copy;
        if (!g_vb[i].len || !by || (i32)(g_vb[i].used - by) >= 0) continue; /* free, or used since */
        drop_own(i);
        vb_free(i);
    }
}

/* a save state's load: every buffer kept of guest memory goes, and their host addresses with it */
void vk_buf_forget_all(void) {
    u32 i;
    for (i = 0; i < g_nvb; i++)
        if (g_vb[i].len) {
            drop_own(i);
            vb_free(i);
        }
    vk_buf_mapping_changed();
}
