/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The pure part of the submit probe (diag/submit_probe.c): a frame's submit calls with their time and op counts, and
   at the post how many of those ops were submitted at least 1, 2 and 3 ms before it. No runtime header, so the host
   test links it with nvn_cmdlist.c alone. */
#ifndef SNX_GPU_NVN_SUBMIT_PROBE_CALC_H
#define SNX_GPU_NVN_SUBMIT_PROBE_CALC_H
#include "nx/gpu/nvn/diag/overlap_check_calc.h"

#define SUBMIT_PROBE_CALLS 512u /* the submit calls a frame keeps, the rest counted as dropped */
#define SUBMIT_PROBE_LEADS 3u   /* the ops submitted at least 1, 2 and 3 ms before the post */

typedef struct {
    uint64_t ns;
    uint32_t lists, ops;
} SubmitCall;
typedef struct {
    SubmitCall call[SUBMIT_PROBE_CALLS];
    uint32_t n;
    uint64_t dropped;
} SubmitFrame;
typedef struct {
    uint64_t posts, calls, lists, ops, dropped;
    uint64_t ops_lead[SUBMIT_PROBE_LEADS];
    uint64_t lead_ns_sum, lead_ns_max; /* from each submit call to the post */
} SubmitTotals;

/* The ops of list and of the lists its CALLs reach, to NVN_CALL_DEPTH_MAX as the replay follows them */
uint64_t submit_list_ops(const NvnOpList* list, OverlapFind find, void* ctx, uint32_t depth);
/* Keeps one submit call. 0 when the frame is full, which counts it as dropped */
int submit_frame_add(SubmitFrame* f, uint64_t ns, uint32_t lists, uint32_t ops);
/* The frame's calls into t against a post at post_ns, and the frame emptied. A call after post_ns leads by 0 */
void submit_frame_close(SubmitFrame* f, uint64_t post_ns, SubmitTotals* t);

#endif
