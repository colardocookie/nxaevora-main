/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* SNX_ARCEUS_PERF_REUSE_PROBE=1, log only: whether the game rewrites, while it builds the next frame, the guest
   ranges that a native frame's replay read as constants, vertices and indices. The GPU thread calls all of it. */
#ifndef SNX_GPU_NVN_PERF_REUSE_H
#define SNX_GPU_NVN_PERF_REUSE_H
#include "nx/gpu/nvn/diag/perf_reuse_calc.h"

/* set while a native frame job replays with the probe on: the draws then note what they read */
extern int g_perf_reuse_rec;
void perf_reuse_note(uint64_t gva, uint64_t len, uint32_t kind);
#define PERF_REUSE_NOTE(gva, len, kind)                                                                                \
    do {                                                                                                               \
        if (g_perf_reuse_rec) perf_reuse_note(gva, len, kind);                                                         \
    } while (0)
/* a frame job starts its replay: the last frame's ranges are hashed again and compared */
void perf_reuse_job_begin(void);
/* a frame job ends, before its syncpoint lets the game go on: a frame's ranges are hashed (frame), a flush's are
   kept for the frame it is part of */
void perf_reuse_job_end(int frame);
/* the hash of [gva, gva + len) as the probe takes it: GPU-owned runs by their owners (*owned set), so nothing lands.
   Caller holds vk_lock */
uint64_t perf_reuse_hash(uint64_t gva, uint64_t len, uint8_t* owned);

#endif
