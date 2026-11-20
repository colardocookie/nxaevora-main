/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_GPU_PERF_SAMPLE_H
#define SNX_GPU_PERF_SAMPLE_H
/* SNX_ARCEUS_PERF_PROBES (PERF-31): a named thread stores its own CPU time for perf_mem_line, at most every 50 ms,
   called where it passes regularly. Only with the switch set */
#include "nx/gpu/perf_times.h"
#include "nx/runtime/platform/plat.h"

static inline void perf_thread_sample(int who) {
    static _Thread_local u64 last;
    if (!g_perf_probes) return;
    const u64 now = plat_ns();
    if (last && now - last < 50000000ull) return;
    last = now;
    u64 kernel, user;
    plat_thread_cpu(&kernel, &user);
    perf_mem_thread(&g_perf_mem, who, (uintptr_t)&last, kernel, user);
}

#endif
