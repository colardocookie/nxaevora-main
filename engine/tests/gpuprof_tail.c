/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The GPU profile's frame tail (gpu/vk/gpuprof_tail.h, PERF-15): passes and gaps clipped to the last stretch of a
   frame, gaps by kind, those between two submissions, and the largest one. */
#include "nx/gpu/vk/gpuprof_tail.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

int main(void) {
    GpTail t;
    memset(&t, 0, sizeof t);
    CHECK(gp_clip(0, 100, 200) == 0 && gp_clip(150, 300, 200) == 100 && gp_clip(250, 300, 200) == 50);
    CHECK(gp_clip(300, 250, 0) == 0 && gp_clip(200, 200, 0) == 0); /* empty or reversed */
    /* a frame ending at 1000, its tail from 600: passes [100, 500) [550, 700) [800, 1000) */
    CHECK(gp_tail_pass(&t, 100, 500, 600) == 0);
    CHECK(gp_tail_pass(&t, 550, 700, 600) == 100);
    CHECK(gp_tail_pass(&t, 800, 1000, 600) == 200);
    CHECK(t.pass_ns == 300);
    gp_tail_gap(&t, 500, 550, 600, GP_GAP_COPY, 1);     /* before the tail: not counted */
    gp_tail_gap(&t, 700, 800, 600, GP_GAP_BARRIER, 1);  /* in it, across a submission */
    gp_tail_gap(&t, 590, 620, 600, GP_GAP_COPY, 0);     /* cut by the tail's start */
    gp_tail_gap(&t, 900, 950, 600, GP_GAPS, 1);         /* no such kind: ignored */
    CHECK(t.gaps[GP_GAP_COPY] == 1 && t.gap_ns[GP_GAP_COPY] == 20);
    CHECK(t.gaps[GP_GAP_BARRIER] == 1 && t.gap_ns[GP_GAP_BARRIER] == 100);
    CHECK(t.cross == 1 && t.cross_ns == 100 && t.largest == 100);
    CHECK(!t.gaps[GP_GAP_DISPATCH] && !t.gaps[GP_GAP_NOTHING]);
    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("gpuprof_tail: ok\n");
    return 0;
}
