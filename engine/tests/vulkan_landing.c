/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* PERF-1 (gpu/vk/landing.c) against the byte loops it replaces: out_changed_runs' word test (buffer.c) and
   vk_write_changed's walk (writeback.c), copied here as they are with SNX_ARCEUS_PERF_LANDING unset. The same runs,
   and the same bytes in guest memory, on random and edge inputs. */
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

int main(void) {
    test_words();
    test_edges();
    test_random();
    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("vulkan_landing: ok\n");
    return 0;
}
