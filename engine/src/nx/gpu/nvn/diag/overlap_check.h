/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* SNX_ARCEUS_PERF_OVERLAP_CHECK=1, log only (research 11:34): which guest ranges a present job's ops read change
   between the job's post and the end of its replay. Hashed at the post on the game's thread, outside objs_lock, and
   at frame_end on the GPU thread. Unset, the hooks test g_overlap_check and nothing else runs. */
#ifndef SNX_GPU_NVN_OVERLAP_CHECK_H
#define SNX_GPU_NVN_OVERLAP_CHECK_H
#include "nx/gpu/nvn/diag/overlap_check_calc.h"

extern int g_overlap_check;
/* At a present's post, caller holds objs_lock and lists_lock: the frame's ranges, read from its lists. A record
   number for overlap_check_post_hash, or -1 */
int overlap_check_collect(const uint64_t* handles, uint32_t count, OverlapFind find, void* ctx);
/* After the post, outside objs_lock: hashes the record's ranges */
void overlap_check_post_hash(int record);
/* On the GPU thread when a present job's replay has ended: hashes the oldest record again and counts it */
void overlap_check_end(void);

#endif
