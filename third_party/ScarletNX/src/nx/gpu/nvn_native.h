/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_NVN_NATIVE_H
#define SNX_NVN_NATIVE_H

#include "common/cpu/aarch64/cpu.h"
#include "nx/gpu/gpu.h"
#include "nx/gpu/nvn_replay.h"

/* nvn_native=1: the host answers the NVN functions that the game resolves, and the game's NVN library does not run
   (docs/nvn-design.md). Init runs once the game's symbols and dispatch table are loaded, before nvn_trace_init and
   before any guest thread starts. */
void nvn_native_init(void);
/* at a clean exit: the functions that the game called and the host does not have */
void nvn_native_report(void);
/* 1 once nvn_native_init found nvn_native=1 and hooked the loader */
int nvn_native_active(void);
/* the host handler of an NVN function by name, or NULL when the host has none (the selftest compares it with the
   library). It does not need nvn_native=1. */
GuestFn nvn_native_handler(const char* name);

/* The state behind the callbacks of a replay (docs/nvn-design.md, 3c decision 6). The frame job owns it for one replay. */
typedef struct {
    GpuChannel* channel; /* the private channel that the methods go to, used on the GPU thread only */
    uint32_t clip_limit; /* the device word at +0x18: 0 until its source is known, then SetRenderTargets is refused */
} NvnNativeReplayHost;
/* Fills env for the replay of host lists. The callbacks read the records of the host under its lock and write
   methods on host->channel. window_origin_flip is 1 when the device window origin mode is 1. */
void nvn_native_replay_env(NvnNativeReplayHost* host, int window_origin_flip, NvnReplayEnv* env);

#endif
