/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The pure part of the overlap check (diag/overlap_check.c): the guest ranges a frame's ops name, walked as the
   replay walks them, and the ranges the replay itself writes, so a change there is not taken for the game's. No
   runtime header, so the host test links it with nvn_cmdlist.c and perf_reuse_calc.c alone. */
#ifndef SNX_GPU_NVN_OVERLAP_CHECK_CALC_H
#define SNX_GPU_NVN_OVERLAP_CHECK_CALC_H
#include "nx/gpu/nvn/diag/perf_reuse_calc.h"
#include "nx/gpu/nvn/nvn_cmdlist.h"

#define OVERLAP_WRITES 4096u /* the replay's own writes a frame kept, the rest counted */

typedef struct {
    uint64_t gva, len;
} OverlapWrite;
typedef struct {
    PerfReuseSet set; /* what the ops read: uniform, storage and vertex binds, index ranges */
    OverlapWrite w[OVERLAP_WRITES];
    uint32_t nw;
    uint64_t w_overflow;
} OverlapRanges;

typedef const NvnOpList* (*OverlapFind)(void* ctx, uint64_t handle);
void overlap_clear(OverlapRanges* r);
/* Adds what list's ops read and write, its CALLs followed to NVN_CALL_DEPTH_MAX as the replay does */
void overlap_collect(OverlapRanges* r, const NvnOpList* list, OverlapFind find, void* ctx, uint32_t depth);
/* whether [gva, gva + len) meets a range the replay writes: a storage bind, a uniform update, a buffer copy or
   clear, a report. 1 too when writes overflowed, since one of them may be it */
int overlap_self_written(const OverlapRanges* r, uint64_t gva, uint64_t len);

#endif
