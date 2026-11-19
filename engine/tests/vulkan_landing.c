/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* PERF-1 (gpu/vk/landing.c) against the byte loops it replaces: out_changed_runs' word test (buffer.c) and
   vk_write_changed's walk (writeback.c), copied here as they are with SNX_ARCEUS_PERF_LANDING unset. The same runs,
   and the same bytes in guest memory, on random and edge inputs. Level 3's landing_masked: the same bytes and the
   same count, run only where the CPU and OS have AVX-512BW. And the same, landed run by run (PERF-28). */
#include "nx/gpu/vk/landing.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

static int failures;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

#define MAX 6000u
typedef struct {
    uint32_t from, n;
} Run;
typedef struct {
    Run runs[MAX];
    uint32_t count;
    uint8_t* dst; /* the guest memory the runs land in */
    const uint8_t* res;
} Out;

/* writeback.c's walk with the switch unset, putting its runs into o */
static void old_walk(const uint8_t* res, const uint8_t* org, const uint8_t* cur, uint32_t size, Out* o) {
    for (uint32_t i = 0; i < size;) {
        uint32_t n = size - i < 1024 ? size - i : 1024, end = i + n;
        if (!memcmp(res + i, org + i, n)) {
            i = end;
            continue;
        }
        do {
            if (end - i >= 8 && !memcmp(res + i, org + i, 8)) {
                i += 8;
                continue;
            }
            if (res[i] == org[i] || cur[i] != org[i]) {
                i++;
                continue;
            }
            uint32_t start = i;
            while (i < size && res[i] != org[i] && cur[i] == org[i]) i++;
            o->runs[o->count++] = (Run){start, i - start};
            memcpy(o->dst + start, res + start, i - start);
        } while (i < end);
    }
}
static void put(void* ctx, uint32_t from, uint32_t n) {
    Out* o = ctx;
    o->runs[o->count++] = (Run){from, n};
    memcpy(o->dst + from, o->res + from, n);
}
/* buffer.c's run finding, with memcmp or landing_differs as the word test */
static uint32_t changed_runs(const uint8_t* res, const uint8_t* org, uint32_t size, int words, Run* runs) {
    uint32_t k = 0, n = 0;
    while (k < size) {
        uint32_t w = size - k < 8 ? size - k : 8;
        if (words ? landing_differs(res + k, org + k, w) : memcmp(res + k, org + k, w) != 0) {
            if (n && runs[n - 1].from + runs[n - 1].n == k)
                runs[n - 1].n += w;
            else
                runs[n++] = (Run){k, w};
        }
        k += w;
    }
    return n;
}

static uint32_t rnd_state = 12345;
static uint32_t rnd(void) {
    rnd_state = rnd_state * 1103515245u + 12345u;
    return rnd_state >> 8;
}

static uint8_t org[MAX], res[MAX], guest[MAX], old_dst[MAX], new_dst[MAX];
static Out old_out, new_out;

/* one case: the old and the new walk from the same guest bytes, cur a snapshot or the guest memory itself */
static void compare(uint32_t size, int alias) {
    memcpy(old_dst, guest, size);
    memcpy(new_dst, guest, size);
    old_out.count = new_out.count = 0;
    old_out.dst = old_dst;
    new_out.dst = new_dst;
    old_out.res = new_out.res = res;
    old_walk(res, org, alias ? old_dst : guest, size, &old_out);
    const uint64_t total = landing_runs(res, org, alias ? new_dst : guest, size, put, &new_out);
    uint64_t old_total = 0;
    for (uint32_t i = 0; i < old_out.count; i++) old_total += old_out.runs[i].n;
    CHECK(total == old_total);
    CHECK(new_out.count == old_out.count);
    CHECK(!memcmp(new_out.runs, old_out.runs, old_out.count * sizeof(Run)));
    CHECK(!memcmp(new_dst, old_dst, size));
    static Run a[MAX], b[MAX];
    const uint32_t na = changed_runs(res, org, size, 0, a), nb = changed_runs(res, org, size, 1, b);
    CHECK(na == nb && !memcmp(a, b, na * sizeof(Run)));
}

static void fill(uint32_t size, int density, int guest_moves) {
    for (uint32_t i = 0; i < size; i++) org[i] = (uint8_t)rnd();
    memcpy(res, org, size);
    memcpy(guest, org, size);
    for (uint32_t i = 0; i < size; i++) {
        if ((int)(rnd() % 1000) < density) res[i] = (uint8_t)(org[i] + 1 + rnd() % 255);
        if (guest_moves && rnd() % 7 == 0) guest[i] = (uint8_t)(guest[i] ^ (1 + rnd() % 255));
    }
}

static void test_random(void) {
    static const int densities[] = {0, 1, 20, 300, 900, 1000};
    for (unsigned round = 0; round < 3000; round++) {
        const uint32_t size = rnd() % 5001;
        fill(size, densities[round % 6], (int)(round / 6 % 2));
        compare(size, (int)(round % 2));
    }
}

/* runs at both ends and across the 1 KiB and 8-byte boundaries */
static void test_edges(void) {
    static const uint32_t sizes[] = {0, 1, 7, 8, 9, 15, 16, 1023, 1024, 1025, 1031, 1032, 2048, 2055, 5000};
    static const uint32_t spots[] = {0, 1, 6, 7, 8, 9, 1016, 1020, 1023, 1024, 1025, 1031, 2047, 2048, 4999};
    for (unsigned s = 0; s < sizeof sizes / sizeof sizes[0]; s++) {
        const uint32_t size = sizes[s];
        for (unsigned p = 0; p < sizeof spots / sizeof spots[0]; p++) {
            for (uint32_t len = 1; len <= 17; len += 4) {
                for (int g = 0; g < 3; g++) {
                    for (uint32_t i = 0; i < size; i++) org[i] = res[i] = guest[i] = (uint8_t)(i * 31);
                    for (uint32_t i = spots[p]; i < spots[p] + len && i < size; i++) res[i] ^= 0x5a;
                    if (g == 1 && spots[p] + 2 < size) guest[spots[p] + 2] ^= 1; /* the guest wrote inside the run */
                    if (g == 2 && size) guest[size - 1] ^= 1;
                    compare(size, g == 2);
                }
            }
        }
        /* every byte changed, and every other byte: the most runs */
        for (uint32_t i = 0; i < size; i++) org[i] = guest[i] = (uint8_t)i, res[i] = (uint8_t)(i + 1);
        compare(size, 0);
        for (uint32_t i = 0; i < size; i++) res[i] = (uint8_t)(i & 1 ? i + 1 : i);
        compare(size, 1);
        CHECK(size < 400 || old_out.count > 64); /* past out_changed_runs' first 64 runs */
    }
}

static void test_words(void) {
    uint8_t a[8] = {1, 2, 3, 4, 5, 6, 7, 8}, b[8];
    memcpy(b, a, 8);
    CHECK(!landing_differs(a, b, 8));
    for (int i = 0; i < 8; i++) {
        b[i] ^= 0x80;
        CHECK(landing_differs(a, b, 8));
        CHECK(landing_differs(a, b, (uint32_t)i + 1) == 1);
        CHECK(!i || !landing_differs(a, b, (uint32_t)i));
        b[i] ^= 0x80;
    }
}

/* every selection of one word (res differs where rm says, the guest holds org where cm says) at each of the eight
   alignments, and tails up to 17 bytes, in exact allocations so that ASan sees a read past the end (reviewer 11:13) */
typedef struct {
    uint8_t* dst;
    const uint8_t* res;
    uint32_t size, seen;
    int bad;
} Masks;
static void mask_put(void* ctx, uint32_t from, uint32_t n) {
    Masks* m = ctx;
    if (!n || from + n > m->size) {
        m->bad = 1;
        return;
    }
    for (uint32_t i = from; i < from + n; i++) {
        if (m->seen >> i & 1) m->bad = 1; /* a byte put twice */
        m->seen |= 1u << i;
    }
    memcpy(m->dst + from, m->res + from, n);
}
static void test_masks(void) {
    for (unsigned off = 0; off < 8; off++)
        for (unsigned rm = 0; rm < 256; rm++)
            for (unsigned cm = 0; cm < 256; cm++) {
                uint8_t *a = malloc(off + 8), *b = malloc(off + 8), *d = malloc(off + 8);
                uint8_t *o = a + off, *r = b + off, *cur = d + off;
                for (unsigned i = 0; i < 8; i++) {
                    o[i] = 0x55;
                    r[i] = (uint8_t)(o[i] ^ (rm >> i & 1 ? (i & 1 ? 0x80 : 1) : 0));
                    cur[i] = (uint8_t)(o[i] ^ (cm >> i & 1 ? 0 : 0x40));
                }
                Masks m = {cur, r, 8, 0, 0};
                const uint64_t n = landing_runs(r, o, cur, 8, mask_put, &m);
                const unsigned want = rm & cm;
                CHECK(!m.bad && m.seen == want && n == (uint64_t)__builtin_popcount(want));
                for (unsigned i = 0; i < 8; i++)
                    CHECK(cur[i] == (want >> i & 1 ? r[i] : (uint8_t)(o[i] ^ (cm >> i & 1 ? 0 : 0x40))));
                free(a);
                free(b);
                free(d);
            }
    for (unsigned off = 0; off < 8; off++)
        for (unsigned size = 0; size < 18; size++) {
            const unsigned bytes = off + size ? off + size : 1;
            uint8_t *a = malloc(bytes), *b = malloc(bytes), *d = malloc(bytes);
            unsigned want = 0;
            for (unsigned i = 0; i < size; i++) {
                a[off + i] = 0x55;
                b[off + i] = 0xd5;
                d[off + i] = i % 3 ? 0x55 : 0x15;
                if (i % 3) want |= 1u << i;
            }
            Masks m = {d + off, b + off, size, 0, 0};
            const uint64_t n = landing_runs(b + off, a + off, d + off, size, mask_put, &m);
            CHECK(!m.bad && m.seen == want && n == (uint64_t)__builtin_popcount(want));
            free(a);
            free(b);
            free(d);
        }
}

/* the tail's puts of landing_masked (below): where they land, each byte once */
typedef struct {
    uint8_t* dst;
    const uint8_t* res;
    uint32_t size;
    uint64_t total;
    int bad;
    uint8_t seen[MAX];
    uint8_t noted[MAX]; /* PERF-23: in a span the note heard */
    int bad_note;
} Blocks;
static Blocks blocks;
#define FILL_BLOCK 1024u
/* 1 KB blocks of each kind in random order: res unchanged, changed with the guest unchanged, changed with a guest write
   to a byte the shader left alone or to one it changed, the guest's write alone, and the guest holding res already.
   Changes are 2 or 3 bytes of a float, as in a skinned vertex */
static void fill_blocks(uint32_t size) {
    for (uint32_t i = 0; i < size; i++) org[i] = res[i] = guest[i] = (uint8_t)rnd();
    for (uint32_t i = 0, n; i < size; i += n) {
        n = size - i < FILL_BLOCK ? size - i : FILL_BLOCK;
        const unsigned kind = rnd() % 6;
        if (kind == 1 || kind == 2 || kind == 4 || kind == 5)
            for (uint32_t f = i; f + 4 <= i + n; f += 4)
                if (rnd() % 2)
                    for (uint32_t b = 0; b < 2 + rnd() % 2; b++) res[f + b] ^= (uint8_t)(1 + rnd() % 255);
        if (kind == 1 || kind == 2 || kind == 4 || kind == 5) res[i + n - 1] ^= 0x10; /* a changed last byte */
        if (kind == 2 || kind == 3) { /* a short last block may have no unchanged byte */
            uint32_t at = i + rnd() % n;
            for (uint32_t tries = 0; kind == 2 && res[at] != org[at] && tries < 64; tries++) at = i + rnd() % n;
            guest[at] ^= 0x22;
        }
        if (kind == 4) guest[i + n - 1] ^= 0x22;
        if (kind == 5) memcpy(guest + i, res + i, n);
    }
}
/* landing_masked, or landing_runs where it can't run, as writeback.c picks them */
static int masked;
static uint64_t land3(const uint8_t* r, const uint8_t* o, uint8_t* d, uint32_t size, LandingPut p, void* ctx) {
    return masked ? landing_masked(r, o, d, size, p, NULL, ctx) : landing_runs(r, o, d, size, p, ctx);
}
/* a masked step's span for the record as written (PERF-23): within one whole step, before the tail */
static void note_span(void* ctx, uint32_t from, uint32_t n) {
    Blocks* b = ctx;
    if (!n || n > 64 || from / 64 != (from + n - 1) / 64 || from + n > b->size / 64 * 64) b->bad_note = 1;
    for (uint32_t i = from; !b->bad_note && i < from + n; i++) b->noted[i] = 1;
}
/* the step of landing_masked in scalar code, so that the rule is tested where AVX-512BW is missing too, with its
   note (noted, when not NULL, gets each step's span) */
static uint64_t model_masked(const uint8_t* r, const uint8_t* o, uint8_t* d, uint32_t size, Blocks* noted) {
    uint64_t total = 0;
    uint32_t k = 0;
    for (; size - k >= 64; k += 64) {
        uint64_t m = 0;
        for (unsigned j = 0; j < 64; j++) m |= (uint64_t)(r[k + j] != o[k + j] && d[k + j] == o[k + j]) << j;
        if (noted) landing_note_mask(m, k, note_span, noted);
        for (unsigned j = 0; j < 64; j++)
            if (m >> j & 1) d[k + j] = r[k + j];
        total += (uint64_t)__builtin_popcountll(m);
    }
    for (; k < size; k++)
        if (r[k] != o[k] && d[k] == o[k]) d[k] = r[k], total++;
    return total;
}
/* the tail's puts: after the last whole step, within the result, each byte once */
static void tail_put(void* ctx, uint32_t from, uint32_t n) {
    Blocks* b = ctx;
    if (!n || from + n > b->size || from < b->size / 64 * 64) b->bad = 1;
    for (uint32_t i = from; !b->bad && i < from + n; i++) {
        if (b->seen[i]) b->bad = 1;
        b->seen[i] = 1;
    }
    if (!b->bad) memcpy(b->dst + from, b->res + from, n);
    b->total += n;
}
/* every byte old_walk landed lies in a span heard, and each span begins and ends on one (blocks.noted, whole steps) */
static int every_landed_noted(uint32_t size) {
    static uint8_t landed[MAX];
    memset(landed, 0, size);
    for (uint32_t i = 0; i < old_out.count; i++)
        for (uint32_t j = old_out.runs[i].from; j < old_out.runs[i].from + old_out.runs[i].n; j++) landed[j] = 1;
    for (uint32_t i = 0; i < size / 64 * 64; i++) {
        if (landed[i] && !blocks.noted[i]) return 0;
        if (blocks.noted[i] && (i % 64 == 0 || !blocks.noted[i - 1]) && !landed[i]) return 0; /* a span's first */
        if (blocks.noted[i] && (i % 64 == 63 || !blocks.noted[i + 1]) && !landed[i]) return 0; /* and last */
    }
    return 1;
}
/* every byte old_walk landed was heard, a step's span or a tail put (blocks.seen) */
static int every_landed_seen(uint32_t size) {
    for (uint32_t i = 0; i < old_out.count; i++)
        for (uint32_t j = old_out.runs[i].from; j < old_out.runs[i].from + old_out.runs[i].n; j++)
            if (j < size && !blocks.seen[j]) return 0;
    return 1;
}
static void compare_masked(uint32_t size) {
    memcpy(old_dst, guest, size);
    old_out.count = 0;
    old_out.dst = old_dst;
    old_out.res = res;
    old_walk(res, org, old_dst, size, &old_out); /* cur is the guest memory written, as at level 3 */
    uint64_t want = 0;
    for (uint32_t i = 0; i < old_out.count; i++) want += old_out.runs[i].n;
    memcpy(new_dst, guest, size);
    memset(&blocks, 0, sizeof blocks);
    blocks.size = size;
    CHECK(model_masked(res, org, new_dst, size, &blocks) == want && !memcmp(new_dst, old_dst, size));
    CHECK(!blocks.bad_note && every_landed_noted(size));
    if (!masked) return;
    memcpy(new_dst, guest, size);
    memset(&blocks, 0, sizeof blocks);
    blocks.dst = new_dst;
    blocks.res = res;
    blocks.size = size;
    const uint64_t total = landing_masked(res, org, new_dst, size, tail_put, note_span, &blocks);
    CHECK(!blocks.bad && !blocks.bad_note && total == want && !memcmp(new_dst, old_dst, size));
    for (uint32_t i = 0; i < size / 64 * 64; i++) blocks.seen[i] = blocks.noted[i]; /* the steps, then the tail */
    CHECK(every_landed_seen(size));
}
typedef struct {
    uint32_t calls, from, n;
} Heard;
static void heard(void* ctx, uint32_t from, uint32_t n) {
    Heard* h = ctx;
    h->calls++, h->from = from, h->n = n;
}
static void test_masked(void) {
    /* PERF-23: a step's span from its mask, lowest set bit to highest (reviewer: a sparse byte 7 was missed) */
    static const struct {
        uint64_t m;
        uint32_t calls, from, n;
    } spans[] = {{0, 0, 0, 0},           {1ull << 7, 1, 64 + 7, 1}, {~0ull, 1, 64, 64},
                 {1ull << 63, 1, 127, 1}, {1ull | 1ull << 63, 1, 64, 64}, {0x8ull | 1ull << 60, 1, 67, 58}};
    for (unsigned i = 0; i < sizeof spans / sizeof spans[0]; i++) {
        Heard h = {0, 0, 0};
        landing_note_mask(spans[i].m, 64, heard, &h);
        CHECK(h.calls == spans[i].calls && (!h.calls || (h.from == spans[i].from && h.n == spans[i].n)));
    }
    for (uint32_t size = 0; size < 300; size++) {
        fill_blocks(size);
        compare_masked(size);
    }
    for (unsigned round = 0; round < 2000; round++) {
        const uint32_t size = rnd() % (MAX + 1);
        fill_blocks(size);
        compare_masked(size);
    }
    for (unsigned round = 0; round < 600; round++) {
        static const int densities[] = {0, 1, 20, 300, 900, 1000};
        const uint32_t size = rnd() % 5001;
        fill(size, densities[round % 6], (int)(round / 6 % 2));
        compare_masked(size);
    }
}

/* A result and its guest bytes that each end where an unmapped page starts: no step reads or writes past the end */
static void test_canary(void) {
    const size_t page = 4096; /* res in page 0, guest memory in page 2, pages 1 and 3 unmapped */
#if defined(_WIN32)
    uint8_t* map = VirtualAlloc(NULL, 4 * page, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    DWORD was;
    CHECK(map && VirtualProtect(map + page, page, PAGE_NOACCESS, &was) &&
          VirtualProtect(map + 3 * page, page, PAGE_NOACCESS, &was));
#else
    uint8_t* map = mmap(NULL, 4 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(map != MAP_FAILED && !mprotect(map + page, page, PROT_NONE) && !mprotect(map + 3 * page, page, PROT_NONE));
#endif
    static uint8_t o[4096];
    for (uint32_t size = 1; size <= page; size = size < 200 ? size + 1 : size * 2 + 7) {
        uint8_t *r = map + page - size, *d = map + 3 * page - size;
        for (uint32_t i = 0; i < size; i++) o[i] = (uint8_t)i, r[i] = (uint8_t)(i % 3 ? i : i + 1), d[i] = o[i];
        Out out = {.dst = d, .res = r};
        CHECK(land3(r, o, d, size, put, &out) == (size + 2) / 3 && !memcmp(d, r, size));
    }
#if defined(_WIN32)
    VirtualFree(map, 0, MEM_RELEASE);
#else
    munmap(map, 4 * page);
#endif
}

/* A thread keeps writing the guest bytes the shader left alone and reading them back while the landing runs: none of
   its writes may be undone (the bytes of a clear mask bit are not stored). A whole-block copy fails it */
#define GUARD 4133u
static uint8_t g_res[GUARD], g_org[GUARD], g_dst[GUARD];
static int g_stop;
static unsigned g_lost;
static void* guard_writer(void* arg) {
    (void)arg;
    for (unsigned v = 0; !__atomic_load_n(&g_stop, __ATOMIC_ACQUIRE); v++) {
        for (uint32_t i = 0; i < GUARD; i++)
            if (i % 4) __atomic_store_n(&g_dst[i], (uint8_t)(v & 1), __ATOMIC_RELAXED);
        for (uint32_t i = 0; i < GUARD; i++)
            if (i % 4 && __atomic_load_n(&g_dst[i], __ATOMIC_RELAXED) != (uint8_t)(v & 1)) g_lost++;
    }
    return NULL;
}
static void guard_put(void* ctx, uint32_t from, uint32_t n) {
    (void)ctx;
    for (uint32_t i = from; i < from + n; i++) __atomic_store_n(&g_dst[i], g_res[i], __ATOMIC_RELAXED);
}
static void test_guard(void) {
    for (uint32_t i = 0; i < GUARD; i++) g_org[i] = 0, g_res[i] = i % 4 ? 0 : (uint8_t)(0x80 | i);
    pthread_t t;
    CHECK(!pthread_create(&t, NULL, guard_writer, NULL));
    uint64_t landed = 0;
    for (unsigned round = 0; round < 20000; round++) {
        for (uint32_t i = 0; i < GUARD; i += 4) __atomic_store_n(&g_dst[i], 0, __ATOMIC_RELAXED);
        landed += land3(g_res, g_org, g_dst, GUARD, guard_put, NULL);
    }
    __atomic_store_n(&g_stop, 1, __ATOMIC_RELEASE);
    pthread_join(t, NULL);
    CHECK(!g_lost);
    CHECK(landed == 20000ull * ((GUARD + 3) / 4));
}

/* SNX_ARCEUS_PERF_COMPARE (PERF-28, reviewer N1): writeback.c lands a result run by run, each run of changed words
   (changed_runs) a call of its own from the run's first byte. The same bytes and count as old_walk over the whole
   result, through landing_masked where it runs, through the scalar model of its step, and through landing_runs */
typedef struct {
    uint8_t* dst;
    const uint8_t* res;
} At;
static void at_put(void* ctx, uint32_t from, uint32_t n) {
    const At* a = ctx;
    memcpy(a->dst + from, a->res + from, n);
}
static void test_runs(void) {
    static const int densities[] = {1, 20, 300, 1000};
    static Run runs[MAX];
    for (unsigned round = 0; round < 3000; round++) {
        const uint32_t size = 1 + rnd() % 5000;
        fill(size, densities[round % 4], (int)(round / 4 % 2));
        memcpy(old_dst, guest, size);
        old_out.count = 0;
        old_out.dst = old_dst;
        old_out.res = res;
        old_walk(res, org, old_dst, size, &old_out);
        uint64_t want = 0;
        for (uint32_t i = 0; i < old_out.count; i++) want += old_out.runs[i].n;
        const uint32_t n = changed_runs(res, org, size, 1, runs);
        for (int how = 0; how < 3; how++) {
            if (how == 0 && !masked) continue;
            uint64_t total = 0;
            memcpy(new_dst, guest, size);
            for (uint32_t i = 0; i < n; i++) {
                const uint32_t at = runs[i].from, len = runs[i].n;
                At a = {new_dst + at, res + at};
                total += how == 0   ? landing_masked(res + at, org + at, new_dst + at, len, at_put, NULL, &a)
                         : how == 1 ? model_masked(res + at, org + at, new_dst + at, len, NULL)
                                    : landing_runs(res + at, org + at, new_dst + at, len, at_put, &a);
            }
            CHECK(total == want && !memcmp(new_dst, old_dst, size));
        }
    }
}
int main(void) {
    masked = landing_masked_supported();
    printf("vulkan_landing: level 3's masked path %s\n", masked ? "RUNS (AVX-512BW)" : "skipped: no AVX-512BW here");
    test_masked();
    test_canary();
    test_guard();
    test_words();
    test_edges();
    test_random();
    test_masks();
    test_runs();
    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("vulkan_landing: ok\n");
    return 0;
}
