/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The portable part of the Windows profiler (runtime/platform/profile_calc.c): a sample's kind, the per-thread
   counts and their top functions. */
#include "nx/runtime/platform/profile_calc.h"
#include <stdio.h>

static int failures;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

static void test_kind(void) {
    const ProfRange image = {0x1000, 0x2000};
    const ProfRange waits[2] = {{0x9000, 0x9020}, {0x1800, 0x1820}};
    CHECK(prof_kind(0x1500, 1, image, waits, 2) == PROF_GAME); /* a game function wins */
    CHECK(prof_kind(0x1000, 0, image, waits, 2) == PROF_RUNTIME);
    CHECK(prof_kind(0x1fff, 0, image, waits, 2) == PROF_RUNTIME);
    CHECK(prof_kind(0x2000, 0, image, waits, 2) == PROF_HOST);
    CHECK(prof_kind(0xfff, 0, image, waits, 2) == PROF_HOST);
    CHECK(prof_kind(0x9000, 0, image, waits, 2) == PROF_WAIT);
    CHECK(prof_kind(0x901f, 0, image, waits, 2) == PROF_WAIT);
    CHECK(prof_kind(0x9020, 0, image, waits, 2) == PROF_HOST);
    CHECK(prof_kind(0x1810, 0, image, waits, 2) == PROF_WAIT); /* a wait range inside the image */
    CHECK(prof_kind(0x1810, 0, image, waits, 1) == PROF_RUNTIME);
}

static void test_counts_and_top(void) {
    static ProfTable t;
    static ProfBucket top[PROF_TOP + 1];
    prof_clear(&t);
    for (unsigned i = 0; i < 5; i++) prof_count(&t, 1, 1, 0x500);
    for (unsigned i = 0; i < 3; i++) prof_count(&t, 1, 1, 0x600);
    prof_count(&t, 1, 1, 0x700);
    for (unsigned i = 0; i < 9; i++) prof_count(&t, 2, 1, 0x500); /* another thread, the same function */
    for (unsigned i = 0; i < 4; i++) prof_count(&t, 1, 2, 0x500); /* the slot's next thread */
    prof_count(&t, 3, 1, 0); /* key 0 is kept apart from a free bucket */
    CHECK(prof_top(&t, 1, 1, top, 2) == 2);
    CHECK(top[0].key == 0x500 && top[0].count == 5);
    CHECK(top[1].key == 0x600 && top[1].count == 3);
    CHECK(prof_top(&t, 1, 1, top, PROF_TOP) == 3);
    CHECK(top[2].key == 0x700 && top[2].count == 1);
    CHECK(prof_top(&t, 2, 1, top, PROF_TOP) == 1 && top[0].count == 9);
    CHECK(prof_top(&t, 1, 2, top, PROF_TOP) == 1 && top[0].count == 4);
    CHECK(prof_top(&t, 3, 1, top, PROF_TOP) == 1 && top[0].key == 16 && top[0].count == 1);
    CHECK(prof_top(&t, 4, 1, top, PROF_TOP) == 0);
    CHECK(prof_top(&t, 1, 1, top, 0) == 0);
    CHECK(t.unplaced == 0);
    prof_clear(&t);
    CHECK(prof_top(&t, 1, 1, top, PROF_TOP) == 0);
}

static void test_top_order(void) {
    static ProfTable t;
    static ProfBucket top[PROF_TOP];
    prof_clear(&t);
    /* 30 functions with counts 1..30 in a mixed order: the top 20 are 30 down to 11 */
    for (unsigned k = 0; k < 30; k++) {
        const unsigned f = (k * 7) % 30 + 1;
        for (unsigned i = 0; i < f; i++) prof_count(&t, 0, 0, 0x1000 + 16 * f);
    }
    CHECK(prof_top(&t, 0, 0, top, PROF_TOP) == PROF_TOP);
    for (unsigned i = 0; i < PROF_TOP; i++) CHECK(top[i].count == 30 - i && top[i].key == 0x1000 + 16 * (30 - i));
}

static void test_full(void) {
    static ProfTable t;
    prof_clear(&t);
    /* more functions than buckets: each one is either in a bucket of its own or counted as not placed */
    const unsigned total = PROF_BUCKETS + 100;
    for (unsigned i = 0; i < total; i++) prof_count(&t, 0, 0, 16ull * (i + 1));
    unsigned used = 0;
    for (unsigned i = 0; i < PROF_BUCKETS; i++) used += t.buckets[i].key != 0 && t.buckets[i].count == 1;
    CHECK(t.unplaced >= 100);
    CHECK(used + t.unplaced == total);
}

static void test_function_at(void) {
    /* RUNTIME_FUNCTION-like entries of 12 bytes: begin, end, unwind */
    static const uint32_t pdata[] = {0x1000, 0x1040, 0, 0x1040, 0x1100, 0, 0x2000, 0x2010, 0};
    uint32_t begin = 7;
    CHECK(!prof_function_at(pdata, 3, 12, 0xfff, &begin) && begin == 7);
    CHECK(prof_function_at(pdata, 3, 12, 0x1000, &begin) && begin == 0x1000);
    CHECK(prof_function_at(pdata, 3, 12, 0x103f, &begin) && begin == 0x1000);
    CHECK(prof_function_at(pdata, 3, 12, 0x1040, &begin) && begin == 0x1040);
    CHECK(prof_function_at(pdata, 3, 12, 0x10ff, &begin) && begin == 0x1040);
    begin = 7;
    CHECK(!prof_function_at(pdata, 3, 12, 0x1100, &begin) && begin == 7); /* a gap between functions */
    CHECK(!prof_function_at(pdata, 3, 12, 0x1fff, &begin));
    CHECK(prof_function_at(pdata, 3, 12, 0x200f, &begin) && begin == 0x2000);
    CHECK(!prof_function_at(pdata, 3, 12, 0x2010, &begin));
    CHECK(!prof_function_at(pdata, 0, 12, 0x1000, &begin));
    CHECK(!prof_function_at(NULL, 3, 12, 0x1000, &begin));
    CHECK(!prof_function_at(pdata, 3, 4, 0x1000, &begin));
    static const uint32_t pairs[] = {0x10, 0x20, 0x30, 0x40}; /* an 8-byte stride */
    CHECK(prof_function_at(pairs, 2, 8, 0x35, &begin) && begin == 0x30);
    CHECK(!prof_function_at(pairs, 2, 8, 0x25, &begin));
}

int main(void) {
    test_kind();
    test_counts_and_top();
    test_top_order();
    test_full();
    test_function_at();
    if (failures) return 1;
    printf("profile_calc: ok\n");
    return 0;
}
