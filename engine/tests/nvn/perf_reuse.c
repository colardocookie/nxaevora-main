/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The pure part of the reuse probe (gpu/nvn/diag/perf_reuse_calc.c): ranges kept once with their kinds, the cap and
   its overflow, a cleared set, and a window's counts with its largest changed ranges. */
#include "nx/gpu/nvn/diag/perf_reuse_calc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

static PerfReuseSet set;

static void test_add(void) {
    perf_reuse_clear(&set);
    CHECK(perf_reuse_add(&set, 0x1000, 256, PERF_REUSE_CB));
    CHECK(perf_reuse_add(&set, 0x1000, 256, PERF_REUSE_VERTEX)); /* the same range: its kinds, not a new entry */
    CHECK(perf_reuse_add(&set, 0x1000, 512, PERF_REUSE_CB));     /* another length: another range */
    CHECK(set.n == 2 && set.r[0].kinds == (PERF_REUSE_CB | PERF_REUSE_VERTEX) && set.r[1].len == 512);
    CHECK(!perf_reuse_add(&set, 0x2000, 0, PERF_REUSE_CB));
    CHECK(!perf_reuse_add(&set, 0x2000, 0x100000000ull, PERF_REUSE_CB));
    CHECK(set.n == 2 && !set.overflow);
    perf_reuse_clear(&set);
    CHECK(!set.n && perf_reuse_add(&set, 0x1000, 256, PERF_REUSE_INDEX) && set.r[0].kinds == PERF_REUSE_INDEX);
}

static void test_cap(void) {
    perf_reuse_clear(&set);
    for (uint32_t i = 0; i < PERF_REUSE_CAP; i++) CHECK(perf_reuse_add(&set, 0x10000 + (uint64_t)i * 256, 256, 1));
    CHECK(set.n == PERF_REUSE_CAP && !set.overflow);
    CHECK(perf_reuse_add(&set, 0x10000, 256, PERF_REUSE_INDEX)); /* a kept range still takes a kind */
    CHECK(!perf_reuse_add(&set, 0x8000, 256, 1));
    CHECK(!perf_reuse_add(&set, 0x8100, 256, 1));
    CHECK(set.n == PERF_REUSE_CAP && set.overflow == 2 && set.r[0].kinds == (1 | PERF_REUSE_INDEX));
    /* every kept range is found again, none twice */
    for (uint32_t i = 0; i < PERF_REUSE_CAP; i++) CHECK(perf_reuse_add(&set, 0x10000 + (uint64_t)i * 256, 256, 2));
    CHECK(set.n == PERF_REUSE_CAP && set.overflow == 2);
}

static void test_window(void) {
    PerfReuseWindow w;
    memset(&w, 0, sizeof w);
    PerfReuseRange a = {0x1000, 1, 256, PERF_REUSE_CB, 0}, b = {0x2000, 1, 4096, PERF_REUSE_VERTEX, 1},
                   c = {0x3000, 1, 64, PERF_REUSE_CB | PERF_REUSE_INDEX, 0};
    perf_reuse_count(&w, &a, 0);
    perf_reuse_count(&w, &b, 1);
    perf_reuse_count(&w, &c, 1);
    perf_reuse_count(&w, &b, 1); /* the same range changed again: counted, listed once */
    CHECK(w.kept == 4 && w.changed == 3 && w.bytes_kept == 256 + 4096 * 2 + 64 && w.bytes_changed == 4096 * 2 + 64);
    CHECK(w.owned == 2);
    CHECK(w.kept_kind[0] == 2 && w.changed_kind[0] == 1);
    CHECK(w.kept_kind[1] == 2 && w.changed_kind[1] == 2);
    CHECK(w.kept_kind[2] == 1 && w.changed_kind[2] == 1);
    CHECK(w.ntop == 2 && w.top[0].gva == 0x2000 && w.top[1].gva == 0x3000);
    /* the list keeps the largest, largest first */
    for (uint32_t i = 0; i < 2 * PERF_REUSE_TOP; i++) {
        PerfReuseRange r = {0x100000 + (uint64_t)i * 0x10000, 1, 128 + i * 16, PERF_REUSE_CB, 0};
        perf_reuse_count(&w, &r, 1);
    }
    CHECK(w.ntop == PERF_REUSE_TOP && w.top[0].gva == 0x2000);
    for (uint32_t i = 1; i < PERF_REUSE_TOP; i++) CHECK(w.top[i - 1].len >= w.top[i].len);
    CHECK(w.top[PERF_REUSE_TOP - 1].len == 128 + (2 * PERF_REUSE_TOP - (PERF_REUSE_TOP - 1)) * 16);
    PerfReuseRange small = {0x900000, 1, 8, PERF_REUSE_CB, 0};
    perf_reuse_count(&w, &small, 1);
    CHECK(w.ntop == PERF_REUSE_TOP && w.top[PERF_REUSE_TOP - 1].gva != 0x900000);
}

int main(void) {
    test_add();
    test_cap();
    test_window();
    if (failures) {
        printf("perf_reuse: %d failures\n", failures);
        return 1;
    }
    puts("perf_reuse: all checks passed");
    return 0;
}
