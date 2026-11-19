# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
"""SNX_ARCEUS_PERF_COMPARE (PERF-28) through buffer.c's and writeback.c's own code: the landing (vk_buf_frame_done
with out_changed_runs, the rebase walk and vk_write_changed), the release thread's pick (vk_buf_compare_ahead) and
the landing's wait for its mark are copied out of the source as they are, and run against small stand-ins for the
rest (no Vulkan, guest memory one array, GPU addresses mapped to it one to one).

Random batches of results over shared pages, some composed (rebased by earlier ones), some with a GPU-owned
baseline, land in order twice from the same start: with the switch unset, and with picks ahead of the landings at
several gates. Both must leave the same guest bytes, the same original bytes in every pending result after each
landing, and the same vk_rt_before_write and gpu_mem_written_shader calls. Stale runs must occur and be found again.
The pick's gates: none with vk_lock already held or the mark set, nothing picked twice. A landing waits for the mark
on another thread until it is cleared. Each with SNX_ARCEUS_PERF_LANDING unset and at 3 (masked where the CPU has
AVX-512BW), and with guest memory reached through a pointer and without one.

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
typedef pthread_cond_t PCond;
#define PMUTEX_INIT PTHREAD_MUTEX_INITIALIZER
static void pm_lock(PMutex* m) { pthread_mutex_lock(m); }
static void pm_unlock(PMutex* m) { pthread_mutex_unlock(m); }
static void pc_init(PCond* c) { pthread_cond_init(c, NULL); }
static void pc_broadcast(PCond* c) { pthread_cond_broadcast(c); }
static int pc_wait(PCond* c, PMutex* m, long t) {
    (void)t;
    return pthread_cond_wait(c, m);
}
static u64 plat_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64)ts.tv_sec * 1000000000ull + (u64)ts.tv_nsec;
}
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
static int vk_lock_depth(void) { return t_depth; }

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
static void vk_rt_before_write(u64 a, u64 n) { note(1, a, n); }
static void gpu_mem_written_shader(u64 a, u64 n) { note(2, a, n); }
static void gpu_mem_landed(u64 a, u64 n) { (void)a, (void)n; }
/* the longest compare the copied code makes, to see a landing that compares a whole result again (reviewer N1) */
static size_t memcmp_max;
static int counted_memcmp(const void* a, const void* b, size_t n) {
    if (n > memcmp_max) memcmp_max = n;
    return memcmp(a, b, n);
}
#define memcmp counted_memcmp
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
static GfxOut* make(u32* s, u64 frame) {
    u32 size = 1 + rnd(s) % (rnd(s) % 4 ? 300 : 9000);
    u64 addr = 0x1000 + rnd(s) % (8 * 4096);
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
static u32 stale_seen, picked_seen;
/* lands frames 1 to FRAMES, with picks before some landings when pick is set: their gates from the seed */
static void land_all(u32 seed, int pick) {
    u32 s = seed ^ 0x9e3779b9u;
    for (u64 f = 1; f <= FRAMES; f++) {
        if (pick && rnd(&s) % 3) {
            u64 gate = f + rnd(&s) % (FRAMES - f + 1);
            vk_buf_compare_ahead(gate);
            for (GfxOut* o = g_out; o; o = o->next) { /* those up to gate, and earlier picks kept */
                assert(o->initial ? !o->runs_ok : o->frame > gate || o->runs_ok);
                picked_seen += o->runs_ok;
            }
        }
        for (GfxOut* o = g_out; o; o = o->next) stale_seen += o->frame == f && o->runs_ok && o->stale;
        vk_buf_frame_done(f);
        note_orgs();
    }
    assert(!g_out);
    free_owned();
}
static u8 guest0[GUEST];
static u64 calls0[1 << 16][3], orgs0[1 << 14];
static u32 ncalls0, norgs0;
static void check_same(void) {
    u32 trials = 0;
    for (int landing = 0; landing < 2; landing++)
        for (direct = 1; direct >= 0; direct--)
            for (u32 seed = 1; seed <= 300; seed++) {
                g_perf_landing = landing ? 3 : 0;
                g_landing_masked = landing && landing_masked_supported();
                const int n = 2 + (int)(seed % 40);
                g_perf_compare = 0;
                build(seed, n);
                ncalls = norgs = 0;
                land_all(seed, 0);
                memcpy(guest0, guest, GUEST);
                memcpy(calls0, calls, sizeof calls[0] * ncalls);
                memcpy(orgs0, orgs, sizeof orgs[0] * norgs);
                ncalls0 = ncalls, norgs0 = norgs;
                g_perf_compare = 1;
                build(seed, n);
                ncalls = norgs = 0;
                land_all(seed, 1);
                assert(!memcmp(guest, guest0, GUEST));
                assert(ncalls == ncalls0 && !memcmp(calls, calls0, sizeof calls[0] * ncalls));
                assert(norgs == norgs0 && !memcmp(orgs, orgs0, sizeof orgs[0] * norgs));
                assert(!t_depth && !result_depth && !g_picked);
                trials++;
            }
    g_perf_compare = 0, g_perf_landing = 0, g_landing_masked = 0, direct = 1;
    printf("vulkan_compare: %u batches the same, %u results picked, %u stale at their landing\n", trials,
           picked_seen, stale_seen);
    assert(picked_seen > 10000 && stale_seen > 100);
}
/* the gates (reviewer O1): vk_lock held on entry, or the mark set, picks nothing */
static void check_gates(void) {
    g_perf_compare = 1;
    build(7, 20);
    vk_lock();
    vk_buf_compare_ahead(FRAMES);
    vk_unlock();
    for (GfxOut* o = g_out; o; o = o->next) assert(!o->runs_ok);
    __atomic_store_n(&g_picked, 1, __ATOMIC_RELEASE);
    vk_buf_compare_ahead(FRAMES);
    for (GfxOut* o = g_out; o; o = o->next) assert(!o->runs_ok);
    __atomic_store_n(&g_picked, 0, __ATOMIC_RELEASE);
    vk_buf_compare_ahead(1);
    u32 first = 0;
    for (GfxOut* o = g_out; o; o = o->next) first += o->runs_ok, assert(o->runs_ok == (o->frame <= 1 && !o->initial));
    ChgRun* kept = g_out->runs;
    vk_buf_compare_ahead(FRAMES); /* the ones picked before are not picked again */
    assert(g_out->runs == kept && first);
    for (u64 f = 1; f <= FRAMES; f++) vk_buf_frame_done(f);
    assert(!g_out && !g_picked);
    free_owned();
    g_perf_compare = 0;
}
/* a landing on another thread waits while the mark is set, and goes once it is cleared */
static int landed_done;
static void* lander(void* p) {
    (void)p;
    vk_lock();
    vk_buf_frame_done(1);
    vk_unlock();
    __atomic_store_n(&landed_done, 1, __ATOMIC_RELEASE);
    return NULL;
}
static void check_wait(void) {
    g_perf_compare = 1;
    g_perf_times = 1;
    build(11, 10);
    vk_buf_compare_ahead(1); /* makes the condition, as the first pick does */
    __atomic_store_n(&g_picked, 1, __ATOMIC_RELEASE);
    pthread_t t;
    pthread_create(&t, NULL, lander, NULL);
    usleep(50000);
    assert(!__atomic_load_n(&landed_done, __ATOMIC_ACQUIRE));
    pm_lock(&g_pick_lock); /* the clear at the end of vk_buf_compare_ahead */
    __atomic_store_n(&g_picked, 0, __ATOMIC_RELEASE);
    pc_broadcast(&g_pick_cv);
    pm_unlock(&g_pick_lock);
    pthread_join(t, NULL);
    assert(landed_done && g_perf_caps.compare[PCC_WAITS] == 1 && g_perf_caps.compare[PCC_WAIT_NS] >= 40000000);
    for (u64 f = 2; f <= FRAMES; f++) vk_buf_frame_done(f);
    assert(!g_out);
    free_owned();
    g_perf_compare = 0;
    g_perf_times = 0;
}
/* the release thread's pick against a landing of every frame on another thread, as they meet: the same bytes as the
   landing alone, whichever goes first */
static void* release_thread(void* p) {
    vk_buf_compare_ahead((u64)(uintptr_t)p);
    return NULL;
}
static void check_race(void) {
    for (u32 seed = 1; seed <= 200; seed++) {
        g_perf_compare = 0;
        build(seed, 30);
        ncalls = norgs = 0;
        land_all(seed, 0);
        memcpy(guest0, guest, GUEST);
        ncalls0 = ncalls, norgs0 = norgs;
        memcpy(calls0, calls, sizeof calls[0] * ncalls);
        memcpy(orgs0, orgs, sizeof orgs[0] * norgs);
        g_perf_compare = 1;
        build(seed, 30);
        ncalls = norgs = 0;
        pthread_t t;
        pthread_create(&t, NULL, release_thread, (void*)(uintptr_t)(1 + seed % FRAMES));
        for (u64 f = 1; f <= FRAMES; f++) {
            vk_lock();
            vk_buf_frame_done(f);
            note_orgs();
            vk_unlock();
        }
        pthread_join(t, NULL);
        assert(!g_out && !memcmp(guest, guest0, GUEST));
        assert(ncalls == ncalls0 && !memcmp(calls, calls0, sizeof calls[0] * ncalls));
        assert(norgs == norgs0 && !memcmp(orgs, orgs0, sizeof orgs[0] * norgs));
        free_owned();
    }
    g_perf_compare = 0;
}
/* reviewer N3: a rebase that changes no byte of a picked result leaves its runs, one that changes a byte makes
   them stale */
static void check_stale(void) {
    for (int changes = 0; changes < 2; changes++) {
        g_perf_compare = 1;
        build(5, 0);
        u32 s = 77;
        GfxOut* e = make(&s, 1);
        GfxOut* o = make(&s, 2);
        free(o);
        o = make(&s, 2); /* a second one of the same shape, at e's address */
        o->addr = e->addr, o->size = e->size < 64 ? e->size : 64, o->npages = 1, o->composed = 1, o->initial = NULL;
        o->pages[0] = o->vmap[0] = e->addr >> 12;
        memcpy(e->ptr, e->org, e->size);
        e->ptr[0] ^= 0x11;               /* e changes its first byte */
        memcpy(o->org, e->ptr, o->size); /* o read it as e wrote it */
        if (changes) o->org[0] ^= 0x22;  /* or o read the byte before e wrote it */
        e->initial = NULL;
        g_out = e, e->next = o, o->next = NULL, g_out_tail = o;
        vk_buf_compare_ahead(2);
        assert(e->runs_ok && o->runs_ok && !o->stale);
        vk_buf_frame_done(1);
        assert(g_out == o && o->stale == changes);
        vk_buf_frame_done(2);
        assert(!g_out);
        free_owned();
    }
    /* a picked result that changed nothing lands with no compare of its bytes at all (reviewer N1) */
    build(5, 0);
    u32 s = 91;
    GfxOut* q = make(&s, 1);
    while (q->size < 64) free(q), q = make(&s, 1); /* long enough that a whole compare shows */
    memcpy(q->ptr, q->org, q->size);
    q->initial = NULL;
    g_out = g_out_tail = q, q->next = NULL;
    vk_buf_compare_ahead(1);
    assert(q->runs_ok && !q->nruns && !q->runs);
    memcmp_max = 0;
    const u32 calls_before = ncalls;
    vk_buf_frame_done(1);
    assert(!g_out && memcmp_max < 8 && ncalls == calls_before);
    free_owned();
    g_perf_compare = 0;
}
int main(void) {
    alarm(120); /* a mark never cleared hangs a landing: fail instead (reviewer N3) */
    check_same();
    check_stale();
    check_race();
    check_gates();
    check_wait();
    printf("vulkan_compare: all checks passed\n");
    return 0;
}
'''


def main():
    buffer = (SRC / "nx/gpu/vk/buffer.c").read_text()
    writeback = (SRC / "nx/gpu/vk/writeback.c").read_text()
    body = HEAD
    body += between(buffer, "typedef struct GfxOut {", "} GfxOut;") + "} GfxOut;\n"
    body += "static GfxOut *g_prep, *g_out, *g_out_tail;\n"
    body += between(buffer, "static ChgRun g_chg_first[64];", "/* whether e's w bytes")
    body += between(buffer, "typedef void (*SpanFn)", "/* the run of R's page")
    body += function(buffer, "static int page_cmp(const void* a, const void* b) {")
    body += function(writeback, "static int in_guest(")
    body += between(writeback, "typedef struct {\n    u64 addr;", "static void landing_put(")
    for sig in ("static void landing_put(", "static void landing_note(", "static void write_changed(",
                "void vk_write_changed(", "void vk_write_changed_runs("):
        body += function(writeback, sig)
    body += between(buffer, "typedef struct {\n    const GfxOut* e;", "/* the changed bytes of e's")
    for sig in ("static void out_span_page(", "static void out_spans(", "static int pages_meet(",
                "static int word_changed(", "static u32 out_changed_runs(", "static void rebase_bytes(",
                "static void rebase_span(", "static void out_rebase(const GfxOut* e, GfxOut* o) {",
                "static void out_rebase_later(", "static void cap_step("):
        body += function(buffer, sig)
    body += between(buffer, "static GfxOut** g_pick;", "/* o's changed runs")
    for sig in ("static void pick_runs(", "void vk_buf_compare_ahead(", "static void pick_wait(",
                "void vk_buf_frame_done("):
        body += function(buffer, sig)
    body += TESTS
    cc = sys.argv[1:] or ["cc", "-std=gnu11", "-O2"]
    with tempfile.TemporaryDirectory() as d:
        c = pathlib.Path(d) / "compare.c"
        c.write_text(body)
        exe = pathlib.Path(d) / "compare"
        subprocess.run([*cc, "-I", str(SRC), "-Wall", "-Wno-unused-function", "-o", str(exe), str(c),
                        str(SRC / "nx/gpu/vk/landing.c"), str(SRC / "nx/gpu/perf_times.c"), "-lpthread"], check=True)
        return subprocess.run([str(exe)]).returncode


if __name__ == "__main__":
    sys.exit(main())
