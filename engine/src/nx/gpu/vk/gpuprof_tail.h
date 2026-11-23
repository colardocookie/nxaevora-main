/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The pure part of the GPU profile's frame tail (gpuprof.c, PERF-15): the time of passes and of the gaps between them
   inside the last stretch of a frame on the GPU's clock, gaps by kind and those between two submissions. No runtime
   header, so the host test includes it alone. */
#ifndef SNX_GPU_VK_GPUPROF_TAIL_H
#define SNX_GPU_VK_GPUPROF_TAIL_H
#include <stdint.h>

enum { GP_GAP_DISPATCH = 0, GP_GAP_COPY, GP_GAP_BARRIER, GP_GAP_NOTHING, GP_GAPS };
typedef struct {
    uint64_t pass_ns, gap_ns[GP_GAPS], gaps[GP_GAPS], cross, cross_ns, largest;
} GpTail;

/* the part of [a, b) at or after from, 0 when none */
static inline double gp_clip(double a, double b, double from) {
    if (b <= from || b <= a) return 0;
    return b - (a > from ? a : from);
}
/* a pass [a, b): its part in the tail, added, and returned for its target's sum */
static inline double gp_tail_pass(GpTail* t, double a, double b, double from) {
    const double in = gp_clip(a, b, from);
    t->pass_ns += (uint64_t)in;
    return in;
}
/* a gap [a, b) of kind (GP_GAP_*), cross when its two passes are in different submissions */
static inline void gp_tail_gap(GpTail* t, double a, double b, double from, int kind, int cross) {
    const double in = gp_clip(a, b, from);
    if (in <= 0 || kind < 0 || kind >= GP_GAPS) return;
    t->gap_ns[kind] += (uint64_t)in;
    t->gaps[kind]++;
    if (cross) {
        t->cross++;
        t->cross_ns += (uint64_t)in;
    }
    if ((uint64_t)in > t->largest) t->largest = (uint64_t)in;
}

#endif
