/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* SNX_ARCEUS_PERF_SUBMIT_PROBE=1, log only (PERF-7, research 18:28): when a frame's lists are submitted against its
   present's post, and whether the game changes what a submitted list reads before that post. 2: the times alone,
   since the hashes delay the game (reviewer 18:52). One line every 600 presents of the queue that presents first.
   Its own lock is never taken under objs_lock, lists_lock or vk_lock. Unset, the hooks test g_submit_probe alone. */
#ifndef SNX_GPU_NVN_SUBMIT_PROBE_H
#define SNX_GPU_NVN_SUBMIT_PROBE_H
#include "nx/gpu/nvn/diag/submit_probe_calc.h"
#include "nx/runtime/platform/plat.h"

extern int g_submit_probe;
/* when the game's nvnSyncWait returns */
void submit_probe_wait_return(void);
/* the flushes posted so far, read at a submit's entry and given to submit_probe_submit */
uint64_t submit_probe_flushes(void);
/* in queue_post_frame for a flush job, caller holds objs_lock: atomics only */
void submit_probe_flush_posted(uint64_t queue);
/* After nvnQueueSubmitCommands has added handles, outside every lock. entry_ns and flushes from its entry. Takes
   lists_lock to read the lists, then hashes the ranges new in the frame */
void submit_probe_submit(uint64_t queue, const uint64_t* handles, uint32_t count, uint64_t entry_ns,
                         uint64_t flushes, OverlapFind find, void* ctx, PMutex* lists_lock);
/* At the entry of nvnQueuePresentTexture, before objs_lock: hashes the frame's ranges again, closes the frame */
void submit_probe_present(uint64_t queue);

#endif
