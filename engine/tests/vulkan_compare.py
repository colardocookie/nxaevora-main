# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
"""SNX_ARCEUS_PERF_COMPARE=2 (PERF-28) through buffer.c's and writeback.c's own code: the landing (vk_buf_frame_done
with out_changed_runs, the rebase walk and vk_write_changed), the page index and gfx_gpu_src's candidate list are
copied out of the source as they are, and run against small stand-ins for the rest (no Vulkan, guest memory one
array, GPU addresses mapped to it one to one).

Random batches of results over shared pages, some composed (rebased by earlier ones), some with a GPU-owned
baseline, land in order at level 0 and at level 2 from the same start, with SNX_ARCEUS_PERF_LANDING at 0 and at 3,
and with guest memory reached through a pointer and without one. landing.c is built portable: the runs from the
write's own masked pass and the walk through the page index give the same bytes and the same rebases, also with a
landing nested in a write and without memory. vk_write_changed_found's answer by path, and the index emptied by
landings and by vk_buf_writable_abort. And gfx_gpu_src's walks: the results its two tests take, in order, through
the index and through g_out, with unmapped pages and results not in g_out.

Run: python3 tests/vulkan_compare.py [compiler and flags], for example
    python3 tests/vulkan_compare.py clang -std=gnu11 -O2 -fsanitize=address,undefined
"""
import pathlib
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
SRC = HERE.parent / "src"


def function(text, signature):
    """The definition starting at signature, through its closing brace"""
    start = text.index(signature)
    at = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[at] == "{") - (text[at] == "}")
        at += 1
    return text[start:at] + "\n"


def between(text, first, last):
    start = text.index(first)
    return text[start:text.index(last, start)]


HEAD = r'''
#include "nx/gpu/perf_times.h"
#include "nx/gpu/vk/landing.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
typedef uint64_t u64;
typedef uint32_t u32;
typedef uint8_t u8;
typedef int VkBuffer;
typedef void* VmaAllocation;
typedef struct {
    u32 from, to;
} ChgRun;
#define VK_NULL_HANDLE NULL
#define VK_WHOLE_SIZE 0
#define VK_OK(x) ((x) == 0)
typedef pthread_mutex_t PMutex;
#define PMUTEX_INIT PTHREAD_MUTEX_INITIALIZER
static void pm_lock(PMutex* m) { pthread_mutex_lock(m); }
static void pm_unlock(PMutex* m) { pthread_mutex_unlock(m); }
static u64 plat_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64)ts.tv_sec * 1000000000ull + (u64)ts.tv_nsec;
}
static unsigned said; /* LOG_LIMIT's lines, counted */
#define LOG_LIMIT(n, ...) ((void)(n), said++)
static void fatal(const char* why) {
    fprintf(stderr, "fatal: %s\n", why);
    abort();
}
static __thread int t_depth; /* reentrant on the thread that holds it, as device.c's */
static pthread_mutex_t g_vkl = PTHREAD_MUTEX_INITIALIZER;
static void vk_lock(void) {
    if (!t_depth++) pthread_mutex_lock(&g_vkl);
}
static void vk_unlock(void) {
    if (!--t_depth) pthread_mutex_unlock(&g_vkl);
}

#define GUEST (1u << 18)
static u8 guest[GUEST];
static u8* const g_mem = guest;
#define AS_SIZE GUEST
static int direct = 1; /* guest memory through gmem_ptr, else gmem_read and gmem_write */
static u8* gmem_ptr(u64 a, u64 n) { return direct && a + n <= GUEST ? guest + a : NULL; }
static void gmem_read(u64 a, void* p, u64 n) { memcpy(p, guest + a, n); }
static void gmem_write(u64 a, const void* p, u64 n) { memcpy(guest + a, p, n); }
/* the write record and render-target calls, in order */
static u64 calls[1 << 16][3];
static u32 ncalls;
static void note(u64 kind, u64 a, u64 n) {
    assert(ncalls < sizeof calls / sizeof *calls);
    calls[ncalls][0] = kind, calls[ncalls][1] = a, calls[ncalls][2] = n, ncalls++;
}
/* a landing nested in a write's render target flush, as out_changed_runs for another result (level 2's guard) */
static void (*nested)(void);
static void vk_rt_before_write(u64 a, u64 n) {
    note(1, a, n);
    if (nested) nested();
}
static void gpu_mem_written_shader(u64 a, u64 n) { note(2, a, n); }
static void gpu_mem_landed(u64 a, u64 n) { (void)a, (void)n; }
/* the longest compare the copied code makes, to see a landing that compares a whole result again (reviewer N1) */
static size_t memcmp_max;
static int counted_memcmp(const void* a, const void* b, size_t n) {
    if (n > memcmp_max) memcmp_max = n;
    return memcmp(a, b, n);
}
#define memcmp counted_memcmp
/* allocations that fail on request after the first n, for the paths without memory */
static int alloc_fails;
static void* test_malloc(size_t n) { return alloc_fails ? NULL : malloc(n); }
static void* test_realloc(void* p, size_t n) { return alloc_fails ? NULL : realloc(p, n); }
static unsigned result_depth;
static void vk_result_begin(void) { result_depth++; }
static void vk_result_end(void) { result_depth--; }
static u32 invalidations;
static int vmaInvalidateAllocation(void* vma, VmaAllocation m, u64 at, u64 n) {
    (void)vma, (void)m, (void)at, (void)n;
    invalidations++;
    return 0;
}
static struct {
    void* vma;
} g_vk;
static int copy_stats_on(void) { return 0; }
static u64 g_wb_outputs, g_wb_bytes;
static int g_ww;
static int written_since_gva(u64 gva, u64 len, u32 since) {
    (void)gva, (void)len, (void)since;
    return 0;
}
static u64 vk_frame_id(void) { return 1; }
static u64 vk_submit_number(void) { return 1; }
static u64 gpu_mem_generation(void) { return 1; }
static u32 vk_ww_now(void) { return 1; }
static void out_pages(void* o, int d) { (void)o, (void)d; }
/* GPU addresses are guest addresses, but for the pages check_src unmaps */
static u8 unmapped[GUEST >> 12];
static u64 gmmu_translate(u64 a) { return a < GUEST && !unmapped[a >> 12] ? a : 0; }
static int g_out_landing, g_gfx_any;
'''

TESTS = r'''
/* a batch: results over a few shared pages, in frames 1 to FRAMES, later ones composed over earlier ones */
#define FRAMES 4
static u32 rnd(u32* s) {
    *s = *s * 1103515245u + 12345u;
    return *s >> 8;
}
static void* owned[1 << 12]; /* the stand-ins' host copies, which the ring holds in the runtime */
static u32 nowned;
static void* own(void* p) {
    assert(nowned < sizeof owned / sizeof *owned);
    return owned[nowned++] = p;
}
static void free_owned(void) {
    while (nowned) free(owned[--nowned]);
}
static int crowd; /* every result inside one guest page (the index's bound) */
static GfxOut* make(u32* s, u64 frame) {
    u32 size = crowd ? 1 + rnd(s) % 900 : 1 + rnd(s) % (rnd(s) % 4 ? 300 : 9000);
    u64 addr = crowd ? 0x5000 + rnd(s) % (4096 - size) : 0x1000 + rnd(s) % (8 * 4096);
    if (rnd(s) % 3 == 0) addr &= ~7ull;
    u32 npages = (u32)(((addr + size - 1) >> 12) - (addr >> 12) + 1);
    GfxOut* o = calloc(1, sizeof *o + 2 * (size_t)npages * 8 + size);
    o->pages = (u64*)(o + 1);
    o->vmap = o->pages + npages;
    o->npages = npages;
    o->org = (u8*)(o->vmap + npages);
    for (u32 i = 0; i < npages; i++) o->pages[i] = o->vmap[i] = (addr >> 12) + i;
    o->addr = addr;
    o->size = size;
    o->frame = frame;
    o->mem = (VmaAllocation)(uintptr_t)(1 + frame);
    o->ptr = own(malloc(size));
    memcpy(o->org, guest + addr, size);
    if (rnd(s) % 4 == 0) /* the guest changed some bytes since it was bound */
        for (u32 k = rnd(s) % 6; k--;) guest[addr + rnd(s) % size] ^= 1 + rnd(s) % 255;
    memcpy(o->ptr, o->org, size);
    const u32 mode = rnd(s) % 5; /* nothing changed, a few bytes, dense runs, all, a run at the end */
    if (mode == 1)
        for (u32 k = 1 + rnd(s) % 8; k--;) o->ptr[rnd(s) % size] ^= 1 + rnd(s) % 255;
    else if (mode == 2)
        for (u32 k = 0; k < size; k++) o->ptr[k] ^= (k / 37) % 3 == 0 ? 1 + rnd(s) % 255 : 0;
    else if (mode == 3)
        for (u32 k = 0; k < size; k++) o->ptr[k] ^= 1 + rnd(s) % 255;
    else if (mode == 4)
        o->ptr[size - 1] ^= 0x5a;
    o->composed = frame > 1 && rnd(s) % 2;
    if (rnd(s) % 9 == 0) { /* a GPU-owned baseline, copied into org at its landing */
        o->initial = own(malloc(size));
        for (u32 k = 0; k < size; k++) o->initial[k] = o->org[k] ^ (rnd(s) % 4 == 0);
        o->initial_mem = o->mem;
    }
    return o;
}
static void build(u32 seed, int n) {
    u32 s = seed;
    for (u32 i = 0; i < GUEST; i++) guest[i] = (u8)rnd(&s);
    g_out = g_out_tail = NULL;
    for (int i = 0; i < n; i++) {
        GfxOut* o = make(&s, 1 + (u64)i * FRAMES / n);
        walk_add(o); /* level 2's index, as at the append to g_prep */
        o->in_out = 1; /* and its commit */
        if (g_out_tail)
            g_out_tail->next = o;
        else
            g_out = o;
        g_out_tail = o;
    }
    g_gfx_any = 1;
}
static u64 hash(const u8* p, u64 n) {
    u64 h = 1469598103934665603ull;
    while (n--) h = (h ^ *p++) * 1099511628211ull;
    return h;
}
/* every pending result's original bytes, in order, after a landing */
static u64 orgs[1 << 14];
static u32 norgs;
static void note_orgs(void) {
    for (GfxOut* o = g_out; o; o = o->next) {
        assert(norgs < sizeof orgs / sizeof *orgs);
        orgs[norgs++] = hash(o->org, o->size);
    }
}
/* lands frames 1 to FRAMES */
static void land_all(u32 seed) {
    (void)seed;
    for (u64 f = 1; f <= FRAMES; f++) {
        vk_buf_frame_done(f);
        note_orgs();
    }
    assert(!g_out);
    free_owned();
}
static u8 guest0[GUEST];
static u64 calls0[1 << 16][3], orgs0[1 << 14];
static u32 ncalls0, norgs0;
/* SNX_ARCEUS_PERF_COMPARE=2 (PERF-28 1b and 3, research 22:33): the runs from the write's masked pass and the walk
   through the page index, against level 0 on the same batches: the same guest bytes, calls and original bytes after
   every landing. landing.c is built portable here (no AVX-512BW on the host), so the masked pass is its scalar
   twin, which tells the same words (vulkan_landing tests the real one against it). With nest, every write has a
   landing nested in it that fills g_chg with another result's runs, and the write's own runs must be dropped */
static uint64_t take64(uint64_t* v) { return __atomic_exchange_n(v, 0, __ATOMIC_RELAXED); }
static GfxOut* nest_other;
static void nest_runs(void) { (void)out_changed_runs(nest_other); }
/* or a whole later frame landing inside the write, once: a level 2 landing nested in one */
static int nesting;
static void nest_frame(void) {
    if (nesting || !g_out) return;
    nesting = 1;
    vk_buf_frame_done(g_out->frame);
    nesting = 0;
}
static u64 take_met(void) {
    u64 m = 0;
    for (int k = 0; k < 2; k++) m += take64(&g_perf_caps.walk[k][PCW_MET]);
    return m;
}
static int index_empty(void) {
    for (u32 i = 0; i < WALK_BUCKETS; i++)
        if (g_walk[i]) return 0;
    return 1;
}
static void check_level2(void) {
    u32 trials = 0;
    u64 found = 0, runs = 0, met = 0;
    u32 t = 99;
    nest_other = make(&t, 1);
    nest_other->size = 4096, nest_other->ptr = malloc(4096); /* not owned: it outlives every batch */
    nest_other = realloc(nest_other, sizeof *nest_other + 2 * 8 * nest_other->npages + 4096);
    nest_other->pages = (u64*)(nest_other + 1), nest_other->vmap = nest_other->pages + nest_other->npages;
    nest_other->org = (u8*)(nest_other->vmap + nest_other->npages);
    for (u32 k = 0; k < 4096; k++) nest_other->org[k] = 0, nest_other->ptr[k] = (k / 8) % 2; /* a run a word apart */
    for (int k = 0; k < 2; k++)
        for (int i = 0; i < PCW_N; i++) (void)take64(&g_perf_caps.walk[k][i]);
    for (int nest = 0; nest < 3; nest++)
        for (int landing = 0; landing < 2; landing++)
            for (direct = 1; direct >= 0; direct--)
                for (u32 seed = 1; seed <= 150; seed++) {
                    g_perf_landing = landing ? 3 : 0;
                    g_landing_masked = landing; /* the portable pass in place of AVX-512BW */
                    const int n = 2 + (int)(seed % 40);
                    nested = nest == 2 ? nest_frame : NULL; /* the same nested landing in both */
                    g_perf_compare = 0;
                    g_perf_times = 1;
                    (void)take_met();
                    build(seed, n);
                    ncalls = norgs = 0;
                    land_all(seed);
                    const u64 met0 = take_met(); /* the whole walk's rebases */
                    memcpy(guest0, guest, GUEST);
                    memcpy(calls0, calls, sizeof calls[0] * ncalls);
                    memcpy(orgs0, orgs, sizeof orgs[0] * norgs);
                    ncalls0 = ncalls, norgs0 = norgs;
                    g_perf_compare = 2;
                    g_perf_times = 1;
                    nested = nest == 1 ? nest_runs : nest == 2 ? nest_frame : NULL;
                    build(seed, n);
                    ncalls = norgs = 0;
                    land_all(seed);
                    nested = NULL;
                    g_perf_times = 0;
                    for (int k = 0; k < 2; k++)
                        found += take64(&g_perf_caps.walk[k][PCW_FOUND]),
                            runs += take64(&g_perf_caps.walk[k][PCW_RUNS]);
                    const u64 met2 = take_met();
                    assert(met2 == met0); /* the index rebases exactly the results the whole walk meets */
                    met += met2;
                    assert(!memcmp(guest, guest0, GUEST));
                    assert(ncalls == ncalls0 && !memcmp(calls, calls0, sizeof calls[0] * ncalls));
                    assert(norgs == norgs0 && !memcmp(orgs, orgs0, sizeof orgs[0] * norgs));
                    assert(!t_depth && !result_depth && !g_chg_for && index_empty());
                    trials++;
                }
    g_perf_compare = 0, g_perf_landing = 0, g_landing_masked = 0, direct = 1;
    free(nest_other->ptr);
    free(nest_other);
    free_owned();
    printf("vulkan_compare: level 2, %u batches the same, %llu runs found by the write, %llu changed runs, %llu "
           "rebased through the index\n", trials, (unsigned long long)found, (unsigned long long)runs,
           (unsigned long long)met);
    assert(found > 5000 && runs > 20000 && met > 1000 && !g_walk_lost);
}
/* vk_write_changed_found's answer (condition iii and ii): 1 with no run for a result that changed nothing, 1 with
   out_changed_runs' runs through the whole-size masked pass, 0 through the byte loop or without a host pointer */
static ChgRun told[1 << 12];
static u32 ntold;
static void tell(void* ctx, u32 from, u32 n) {
    (void)ctx;
    assert(ntold < sizeof told / sizeof *told);
    told[ntold++] = (ChgRun){from, from + n};
}
static void check_found(void) {
    u32 s = 17, cases[4] = {0};
    build(13, 0);
    for (int round = 0; round < 400; round++) {
        GfxOut* o = make(&s, 1);
        const int unchanged = !memcmp(o->ptr, o->org, o->size), how = round % 4; /* 3: level 3 without AVX-512BW */
        g_perf_landing = how ? 3 : 0, g_landing_masked = how == 1 || how == 2, direct = how != 2;
        ntold = 0;
        const int r = vk_write_changed_found(o->addr, o->ptr, o->org, o->size, tell, NULL);
        const u32 want = out_changed_runs(o); /* into g_chg, with nothing put there for o */
        if (unchanged)
            assert(r == 1 && !ntold && !want), cases[0]++;
        else if (how == 1)
            assert(r == 1 && ntold == want && !memcmp(told, g_chg, want * sizeof *told)), cases[1]++;
        else
            assert(r == 0), cases[how == 3 ? 3 : 2]++;
        free(o);
    }
    assert(cases[0] && cases[1] > 50 && cases[2] > 50 && cases[3] > 50);
    g_perf_landing = 0, g_landing_masked = 0, direct = 1;
    free_owned();
}
/* without memory for the runs, the index or the walk's list: every byte a candidate, the whole walk, and the bytes
   of level 0 without memory either */
static void check_lost(void) {
    const unsigned said0 = said;
    if (g_chg != g_chg_first) free(g_chg); /* back to its first 64 runs, so that growing it fails */
    g_chg = g_chg_first, g_capchg = 64;
    for (u32 seed = 1; seed <= 150; seed++) {
        g_perf_landing = 3, g_landing_masked = 1;
        g_perf_compare = 0;
        build(seed, 30);
        ncalls = norgs = 0;
        alloc_fails = 1;
        land_all(seed);
        alloc_fails = 0;
        memcpy(guest0, guest, GUEST);
        memcpy(orgs0, orgs, sizeof orgs[0] * norgs);
        norgs0 = norgs;
        g_perf_compare = 2;
        build(seed, 30);
        ncalls = norgs = 0;
        alloc_fails = 1;
        land_all(seed);
        alloc_fails = 0;
        assert(!memcmp(guest, guest0, GUEST) && norgs == norgs0 && !memcmp(orgs, orgs0, sizeof orgs[0] * norgs));
        assert(index_empty());
        g_walk_lost = 0;
    }
    g_perf_compare = 0, g_perf_landing = 0, g_landing_masked = 0;
    assert(said > said0); /* a lost run list is said (reviewer N2) */
}
/* two pages in one bucket of the index: a result on the other page is not visited */
static void check_collide(void) {
    g_perf_compare = 2;
    build(21, 0);
    u32 s = 3;
    GfxOut *e = make(&s, 1), *o = make(&s, 2);
    u64 q = e->pages[0] + 1;
    while (walk_bucket(q) != walk_bucket(e->pages[0])) q++;
    o->composed = 1, o->npages = 1, o->pages[0] = q;
    walk_add(o);
    e->npages = 1;
    u64 walk[PCW_N] = {0};
    assert(walk_index(e, walk) && walk[PCW_MET] == 0);
    o->in_out = 1; /* and gfx_gpu_src's list for a range on e's page leaves it out too */
    RangePages rq = {.n = 1};
    rq.page[0] = e->pages[0];
    src_index(&rq);
    assert(g_src_indexed && !g_nscand);
    walk_remove(o);
    assert(index_empty());
    free(e), free(o);
    free_owned();
    g_perf_compare = 0;
}
/* gfx_gpu_src's walks (research 00:47): for random lists with unmapped pages and ranges at, inside, across and away
   from results, the results out_meets_range and out_meets take, in order, through the index and through g_out */
static void check_src(void) {
    u32 compared = 0, taken = 0, indexed = 0;
    for (u32 seed = 1; seed <= 300; seed++) {
        g_perf_compare = 0;
        build(seed, 2 + (int)(seed % 60));
        u32 s = seed * 7 + 1;
        memset(unmapped, 0, sizeof unmapped);
        if (seed % 3) unmapped[1 + rnd(&s) % 9] = 1;
        if (seed % 5 == 0) unmapped[1 + rnd(&s) % 9] = 1;
        g_perf_compare = 2;
        for (GfxOut* o = g_out; o; o = o->next) { /* its pages as the GMMU maps them, sorted with unmapped ones last */
            for (u32 i = 0; i < o->npages; i++) {
                const u64 gp = (o->addr >> 12) + i;
                o->vmap[i] = unmapped[gp] ? ~0ull : gp;
                o->pages[i] = o->vmap[i];
            }
            qsort(o->pages, o->npages, sizeof *o->pages, page_cmp);
            o->in_out = 1;
            walk_add(o);
        }
        GfxOut* prep[8]; /* in the index but not in g_out, as in g_prep: never taken */
        for (int i = 0; i < 8; i++) {
            prep[i] = make(&s, FRAMES);
            walk_add(prep[i]);
        }
        for (u32 k = 0; k < 200; k++) {
            GfxOut* pick = g_out;
            for (u32 j = rnd(&s) % 40; j-- && pick->next;) pick = pick->next;
            const u32 how = rnd(&s) % 4;
            u64 gva = how == 0 ? pick->addr : how == 1 ? pick->addr + rnd(&s) % pick->size : 0x1000 + rnd(&s) % 0x9000;
            u64 len = how == 3 ? 1 + rnd(&s) % (20 * 4096) : 1 + rnd(&s) % 9000;
            if (gva + len > GUEST) len = GUEST - gva;
            RangePages q;
            range_pages(gva, len, &q);
            const int starve = k % 50 == 7; /* no memory for the list: the walks over g_out */
            if (starve) free(g_scand), g_scand = NULL, g_capscand = 0, alloc_fails = 1;
            src_index(&q);
            alloc_fails = 0;
            assert(!starve || !g_src_indexed || !g_nscand);
            indexed += g_src_indexed;
            for (int pred = 0; pred < 2; pred++) {
                GfxOut* want[1 << 7];
                u32 nwant = 0, ngot = 0, at;
                u64 local[16], *rmap = NULL;
                for (GfxOut* o = g_out; o; o = o->next)
                    if (pred ? out_meets(o, gva, len, &q, &rmap, local) : out_meets_range(o, gva, len, &q)) {
                        assert(nwant < 1 << 7);
                        want[nwant++] = o;
                    }
                if (rmap && rmap != local) free(rmap);
                rmap = NULL;
                for (GfxOut* o = src_first(&at); o; o = src_next(o, &at))
                    if (pred ? out_meets(o, gva, len, &q, &rmap, local) : out_meets_range(o, gva, len, &q)) {
                        assert(ngot < nwant && want[ngot] == o);
                        ngot++;
                    }
                if (rmap && rmap != local) free(rmap);
                assert(ngot == nwant);
                compared++, taken += nwant;
            }
        }
        while (g_out) {
            GfxOut* o = g_out;
            g_out = o->next;
            walk_remove(o);
            free(o);
        }
        for (int i = 0; i < 8; i++) walk_remove(prep[i]), free(prep[i]);
        g_out_tail = NULL;
        assert(index_empty());
        free_owned();
    }
    memset(unmapped, 0, sizeof unmapped);
    g_perf_compare = 0;
    printf("vulkan_compare: src walks, %u sequences the same, %u results taken, %u ranges through the index\n",
           compared, taken, indexed);
    assert(taken > 20000 && indexed > 40000 && indexed < compared / 2); /* some ranges are over 16 pages */
}
/* the pool (research 01:36): after every batch each entry is back on the free list, which holds whole blocks. Out of
   memory for a block, also part way through a result: the index is lost, and the entries made return */
static u32 free_count(void) {
    u32 n = 0;
    for (WalkRef* r = g_walk_free; r; r = r->own) n++;
    return n;
}
static void check_pool(void) {
    for (u32 seed = 1; seed <= 60; seed++) {
        g_perf_compare = 2;
        build(seed, 2 + (int)(seed % 50));
        land_all(seed);
        assert(index_empty() && free_count() % WALK_BLOCK == 0 && free_count() >= WALK_BLOCK);
    }
    const u32 all = free_count();
    u32 s = 41;
    build(9, 0);
    GfxOut* o = make(&s, 1);
    while (o->npages < 2) free(o), o = make(&s, 1);
    WalkRef* kept = g_walk_free;
    WalkRef one = {0};
    g_walk_free = &one, one.own = NULL; /* room for one entry, and none more */
    const unsigned said0 = said;
    alloc_fails = 1;
    walk_add(o);
    alloc_fails = 0;
    assert(g_walk_lost && o->wrefs == &one && !one.own && !g_walk_free && said == said0 + 1); /* said (N2) */
    walk_remove(o);
    assert(!o->wrefs && g_walk_free == &one && index_empty());
    g_walk_free = kept, g_walk_lost = 0;
    assert(free_count() == all);
    free(o);
    free_owned();
    g_perf_compare = 0;
}
/* past the bound (research 02:32): batches of up to 200 results on one page, the same as level 0, with the landings'
   lookups giving up; and a binding's lookup on that page giving up, its walks over g_out, unsorted */
static void check_bound(void) {
    crowd = 1;
    u64 fell = 0, most = 0;
    for (u32 seed = 1; seed <= 40; seed++) {
        const int n = 130 + (int)(seed % 70);
        g_perf_compare = 0;
        g_perf_times = 1, g_perf_probes = 1;
        (void)take_met();
        build(seed, n);
        ncalls = norgs = 0;
        land_all(seed);
        const u64 met0 = take_met();
        memcpy(guest0, guest, GUEST);
        memcpy(orgs0, orgs, sizeof orgs[0] * norgs);
        norgs0 = norgs;
        g_perf_compare = 2;
        build(seed, n);
        ncalls = norgs = 0;
        land_all(seed);
        assert(!memcmp(guest, guest0, GUEST) && norgs == norgs0 && !memcmp(orgs, orgs0, sizeof orgs[0] * norgs));
        assert(take_met() == met0 && index_empty());
        fell += take64(&g_perf_p29.idx_fell[1]);
        const u64 m = take64(&g_perf_p29.idx_max[1]);
        if (m > most) most = m;
    }
    assert(fell > 100 && most == WALK_BOUND + 1);
    build(77, 120); /* level 2, every result in the index and in g_out */
    RangePages q;
    range_pages(0x5000, 64, &q);
    (void)take64(&g_perf_p29.idx_fell[0]);
    src_index(&q);
    assert(!g_src_indexed && take64(&g_perf_p29.idx_fell[0]) == 1 && take64(&g_perf_p29.idx_max[0]) == WALK_BOUND + 1);
    u32 at;
    assert(src_first(&at) == g_out); /* the walk over g_out, in its order */
    land_all(77);
    assert(index_empty());
    g_perf_times = 0, g_perf_probes = 0, g_perf_compare = 0, crowd = 0;
    printf("vulkan_compare: past the bound, 40 crowded batches the same, %llu landing lookups gave up\n",
           (unsigned long long)fell);
}
/* a result dropped before it is committed leaves the index too */
static void check_abort(void) {
    g_perf_compare = 2;
    build(3, 0);
    u32 s = 5;
    for (int i = 0; i < 20; i++) {
        GfxOut* o = make(&s, 2);
        o->composed = 1;
        walk_add(o);
        o->next = g_prep, g_prep = o;
    }
    assert(!index_empty());
    vk_buf_writable_abort();
    assert(!g_prep && index_empty());
    free_owned();
    g_perf_compare = 0;
}
int main(void) {
    alarm(120); /* a mark never cleared hangs a landing: fail instead (reviewer N3) */
    check_level2();
    check_found();
    check_lost();
    check_collide();
    check_src();
    check_pool();
    check_bound();
    check_abort();
    printf("vulkan_compare: all checks passed\n");
    return 0;
}
'''


def main():
    buffer = (SRC / "nx/gpu/vk/buffer.c").read_text()
    writeback = (SRC / "nx/gpu/vk/writeback.c").read_text()
    body = HEAD + "#define malloc test_malloc\n#define realloc test_realloc\n"
    body += between(buffer, "typedef struct GfxOut {", "} GfxOut;") + "} GfxOut;\n"
    body += "static GfxOut *g_prep, *g_prep_tail, *g_out, *g_out_tail;\n"
    body += function(buffer, "static u64 out_block(")
    body += between(buffer, "/* SNX_ARCEUS_PERF_COMPARE=2 (PERF-28 3): the results not landed yet",
                    "/* e has landed")
    body += function(buffer, "void vk_buf_writable_abort(void) {")
    body += between(buffer, "static ChgRun g_chg_first[64];", "/* whether e's w bytes")
    body += between(buffer, "typedef void (*SpanFn)", "/* the run of R's page")
    body += function(buffer, "static int page_cmp(const void* a, const void* b) {")
    body += function(writeback, "static int in_guest(")
    body += between(writeback, "typedef struct {\n    u64 addr;", "static void landing_put(")
    for sig in ("static void landing_put(", "static void landing_note(", "static int write_changed(",
                "void vk_write_changed(", "int vk_write_changed_found("):
        body += function(writeback, sig)
    body += between(buffer, "typedef struct {\n    const GfxOut* e;", "/* the changed bytes of e's")
    for sig in ("static void out_span_page(", "static void out_spans(", "static int pages_meet(",
                "static int word_changed("):
        body += function(buffer, sig)
    body += between(buffer, "/* SNX_ARCEUS_PERF_COMPARE=2 (PERF-28 1b): the result whose", "/* e's changed runs into")
    for sig in ("static u32 out_changed_runs(", "static void rebase_bytes(", "static void rebase_span(",
                "static void out_rebase(const GfxOut* e, GfxOut* o) {"):
        body += function(buffer, sig)
    body += between(buffer, "static GfxOut** g_wcand;", "/* the composed results recorded after e")
    body += function(buffer, "static u64* range_map(")
    body += between(buffer, "typedef struct {\n    u64 page[16];", "static void range_pages(")
    for sig in ("static void range_pages(", "static int backing_overlap(", "static void any_span(",
                "static int out_meets(const GfxOut* o,", "static int out_meets_range("):
        body += function(buffer, sig)
    body += between(buffer, "static GfxOut** g_scand;", "/* kind: the caller")
    for sig in ("static void out_rebase_later(", "static void cap_step("):
        body += function(buffer, sig)
    body += "static void land_count(GfxOut* d) { (void)d; }\nstatic void land_copy(void) {}\n"  # PERF-35's, apart
    body += "static void land_list(GfxOut* done);\nstatic u64 g_frames_done;\n"
    body += function(buffer, "void vk_buf_frame_done(")
    body += function(buffer, "static void land_list(GfxOut* done) {")
    body += "#undef malloc\n#undef realloc\n" + TESTS
    cc = sys.argv[1:] or ["cc", "-std=gnu11", "-O2"]
    with tempfile.TemporaryDirectory() as d:
        c = pathlib.Path(d) / "compare.c"
        c.write_text(body)
        exe = pathlib.Path(d) / "compare"
        subprocess.run([*cc, "-I", str(SRC), "-DSNX_LANDING_PORTABLE", "-Wall", "-Wno-unused-function", "-o",
                        str(exe), str(c),
                        str(SRC / "nx/gpu/vk/landing.c"), str(SRC / "nx/gpu/perf_times.c"), "-lpthread"], check=True)
        return subprocess.run([str(exe)]).returncode


if __name__ == "__main__":
    sys.exit(main())
