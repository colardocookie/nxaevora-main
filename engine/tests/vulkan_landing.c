/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* PERF-1 (gpu/vk/landing.c) against the byte loops it replaces: out_changed_runs' word test (buffer.c) and
   vk_write_changed's walk (writeback.c), copied here as they are with SNX_ARCEUS_PERF_LANDING unset. The same runs,
   and the same bytes in guest memory, on random and edge inputs. Level 2's landing_blocks: the same guest bytes. */
#include "nx/gpu/vk/landing.h"
#include <stdio.h>
#include <stdlib.h>

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

/* landing_blocks against the level 0 walk: the same guest bytes, and per block 0 bytes put where res is org, the
   whole block where the guest still holds org, else the level 0 bytes. No put crosses a block or repeats a byte */
typedef struct {
    uint8_t* dst;
    const uint8_t* res;
    uint32_t size;
    uint64_t total;
    int bad;
    uint8_t seen[MAX];
} Blocks;
static Blocks blocks;
static void block_put(void* ctx, uint32_t from, uint32_t n) {
    Blocks* b = ctx;
    if (!n || from + n > b->size || from / LANDING_BLOCK != (from + n - 1) / LANDING_BLOCK) {
        b->bad = 1;
        return;
    }
    for (uint32_t i = from; i < from + n; i++) {
        if (b->seen[i]) b->bad = 1;
        b->seen[i] = 1;
    }
    memcpy(b->dst + from, b->res + from, n);
    b->total += n;
}
static void compare_blocks(uint32_t size, int alias) {
    uint64_t want = 0;
    for (uint32_t i = 0, n; i < size; i += n) {
        n = size - i < LANDING_BLOCK ? size - i : LANDING_BLOCK;
        if (!memcmp(res + i, org + i, n)) continue;
        if (!memcmp(guest + i, org + i, n)) {
            want += n;
            continue;
        }
        for (uint32_t j = i; j < i + n; j++) want += res[j] != org[j] && guest[j] == org[j];
    }
    memcpy(old_dst, guest, size);
    memcpy(new_dst, guest, size);
    old_out.count = 0;
    old_out.dst = old_dst;
    old_out.res = res;
    old_walk(res, org, alias ? old_dst : guest, size, &old_out);
    memset(&blocks, 0, sizeof blocks);
    blocks.dst = new_dst;
    blocks.res = res;
    blocks.size = size;
    const uint64_t total = landing_blocks(res, org, alias ? new_dst : guest, size, block_put, &blocks);
    CHECK(!blocks.bad && total == blocks.total && total == want);
    CHECK(!memcmp(new_dst, old_dst, size));
}
/* a block of each kind in random order: res unchanged, changed with the guest unchanged, changed with a guest write
   to a byte the shader left alone or to one it changed, the guest's write alone, and the guest holding res already.
   Changes are 2 or 3 bytes of a float, as in a skinned vertex */
static void fill_blocks(uint32_t size) {
    for (uint32_t i = 0; i < size; i++) org[i] = res[i] = guest[i] = (uint8_t)rnd();
    for (uint32_t i = 0, n; i < size; i += n) {
        n = size - i < LANDING_BLOCK ? size - i : LANDING_BLOCK;
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
static void test_blocks(void) {
    static const uint32_t sizes[] = {0, 1, 3, 1023, 1024, 1025, 2047, 2048, 2049, 3000, 4096, 5000, MAX};
    for (unsigned s = 0; s < sizeof sizes / sizeof sizes[0]; s++)
        for (unsigned round = 0; round < 40; round++) {
            fill_blocks(sizes[s]);
            compare_blocks(sizes[s], (int)(round % 2));
        }
    for (unsigned round = 0; round < 2000; round++) {
        const uint32_t size = rnd() % (MAX + 1);
        fill_blocks(size);
        compare_blocks(size, (int)(round % 2));
    }
    for (unsigned round = 0; round < 600; round++) { /* the random fills of the level 1 test as well */
        static const int densities[] = {0, 1, 20, 300, 900, 1000};
        const uint32_t size = rnd() % 5001;
        fill(size, densities[round % 6], (int)(round / 6 % 2));
        compare_blocks(size, (int)(round % 2));
    }
    /* only the last byte of a block or of the whole result changed, the guest untouched */
    for (uint32_t size = 1020; size < 2060; size += 13) {
        for (uint32_t i = 0; i < size; i++) org[i] = res[i] = guest[i] = (uint8_t)(i * 7);
        res[size - 1] ^= 1;
        if (size > LANDING_BLOCK) res[LANDING_BLOCK - 1] ^= 1;
        compare_blocks(size, (int)(size & 1));
    }
}

int main(void) {
    test_blocks();
    test_words();
    test_edges();
    test_random();
    test_masks();
    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("vulkan_landing: ok\n");
    return 0;
}
