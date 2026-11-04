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

/* The bind state of one buffer slot. bound follows the library: a size 0 uniform or vertex bind is stored but not active. */
typedef struct { uint64_t addr, size; uint8_t bound; } NvnBindEntry;
/* The state of one state-object bind, stored by the replay from the bind state op. */
typedef struct { uint32_t w[NVN_STATE_WORDS_MAX]; uint8_t bound; } NvnBoundState;
/* The state behind the callbacks of a replay (docs/nvn-design.md, 3c decision 6). The frame job owns it for one replay. */
typedef struct {
    GpuChannel* channel; /* the private channel that the methods go to, used on the GPU thread only */
    uint32_t clip_limit; /* the device word at +0x18: 0 until its source is known, then SetRenderTargets is refused */
    /* The texture binds of one frame job: written by the replay, read by the shader and pipeline stage later. The host
       struct starts zeroed at each frame job, and the table is not cleared between the command lists of one job. */
    uint64_t texture_handle[NVN_BIND_STAGE_MAX + 1u][NVN_BIND_SLOT_MAX];
    uint8_t texture_bound[NVN_BIND_STAGE_MAX + 1u][NVN_BIND_SLOT_MAX];
    /* The buffer binds of one frame job, written by the replay and read by the shader and pipeline stage later. */
    NvnBindEntry uniform[NVN_BIND_STAGE_MAX + 1u][NVN_UNIFORM_SLOT_MAX];
    NvnBindEntry storage[NVN_BIND_STAGE_MAX + 1u][NVN_STORAGE_SLOT_MAX];
    NvnBindEntry vertex[NVN_VERTEX_SLOT_MAX];
    NvnBindEntry xfb[NVN_XFB_SLOT_MAX];
    /* The bound state objects of one frame job, written by the replay and read by the pipeline stage later. */
    NvnBoundState state[NVN_STATE_COUNT];
    /* The draws, dispatches and program binds of one frame job. The replay thread writes them without a lock. */
    uint64_t draws[NVN_DRAW_KIND_COUNT];
    NvnOpDraw last_draw;
    uint64_t dispatches;
    uint32_t last_dispatch[3];
    uint64_t program[NVN_PROGRAM_STAGE_COUNT]; /* 0: no program of that stage */
    /* The bound vertex state of one frame job. The replay thread writes it without a lock, and it is not cleared
       between the command lists of one job. */
    struct { uint32_t w; uint8_t enabled; } vertex_attrib[NVN_VERTEX_ATTRIB_MAX];
    struct { uint32_t stride, divisor; uint8_t enabled; } vertex_stream[NVN_VERTEX_STREAM_MAX];
    uint32_t vattrib_binds, vstream_binds;
    /* The clears, copies, reports and image binds of one frame job. The replay thread writes them without a lock. */
    uint64_t gpu_ops[NVN_OP_COUNT];
    NvnOpReportCounter last_report;
    NvnOpBindImage last_bind_image;
} NvnNativeReplayHost;
/* Fills env for the replay of host lists. The callbacks read the records of the host under its lock and write
   methods on host->channel. window_origin_flip is 1 when the device window origin mode is 1. */
void nvn_native_replay_env(NvnNativeReplayHost* host, int window_origin_flip, NvnReplayEnv* env);

#endif
