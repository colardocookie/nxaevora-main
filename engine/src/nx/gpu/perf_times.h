/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* SNX_ARCEUS_PERF_TIMES=1 (research 10:38, log only): where a frame's time goes on native. The frame job's cycle
   (posted, started, ended), the GPU thread's waits on an empty queue and inside the job by site, and the game's
   nvnSyncWait and nvnWindowAcquireTexture. One line every PERF_TIMES_WINDOW presents. With it off, the sites test
   g_perf_times and read no clock. Any thread may add; the line is written on the GPU thread. */
#ifndef SNX_GPU_PERF_TIMES_H
#define SNX_GPU_PERF_TIMES_H
#include <stddef.h>
#include <stdint.h>

#define PERF_TIMES_WINDOW 600u
#define PERF_TIMES_NAMED 8u /* the GPU thread's other waits, by what gpu_wait_note names them */

enum {
    PT_POST_START, /* posted on the game's thread to frame_run entered */
    PT_START_END,  /* frame_run entered to its syncpoint increment queued (frame_end) */
    PT_END_POST,   /* that increment to the next present's post: the game's part that does not overlap the replay */
    PT_PERIOD,     /* post to post */
    PT_IDLE,       /* the GPU thread on an empty queue */
    PT_SHOW,       /* frame_show, the present */
    PT_TL_RECYCLE, /* timeline_wait for a frame slot's previous use (frame_recycle) */
    PT_TL_FINISH,  /* timeline_wait in vk_finish */
    PT_TL_SUBMIT,  /* timeline_wait for a submission's results (vk_wait_submit: releases and landings) */
    PT_REC_SYNC,   /* vk_rec_sync: the recorder catching up */
    PT_REC_FULL,   /* rec_push on a full ring */
    PT_SYNC_WAIT,  /* the game's nvnSyncWait, the whole call */
    PT_ACQUIRE,    /* the game's nvnWindowAcquireTexture, the whole call */
    PT_RELEASE_HOLD, /* the release thread holding vk_lock in release_through (the landings and releases) */
    PT_SITES
};
/* counts a frame, not times */
enum {
    PC_WB_OUTPUTS, /* graphics storage outputs landed (buffer.c, as g_wb_outputs counts them) */
    PC_WB_BYTES,   /* their bytes */
    PC_WC_BYTES,   /* bytes vk_write_changed wrote to guest memory, the ones the shader changed */
    PC_COUNTS
};

typedef struct {
    uint64_t sum, max, count;
} PerfTime;
typedef struct {
    PerfTime site[PT_SITES];
    struct {
        const char* what; /* 0 while free: claimed with a compare-exchange */
        PerfTime t;
    } named[PERF_TIMES_NAMED];
    uint64_t count[PC_COUNTS];
    uint64_t overlaps; /* presents posted before the previous frame job ended */
    uint64_t named_lost;
} PerfTimes;

extern int g_perf_times;
extern PerfTimes g_perf;

void perf_time_add(PerfTimes* p, int site, uint64_t ns);
void perf_time_named(PerfTimes* p, const char* what, uint64_t ns);
void perf_count(PerfTimes* p, int counter, uint64_t n);
/* Writes the window's line into out and starts the next window. frames: the presents it covers. */
size_t perf_times_line(PerfTimes* p, uint32_t frames, char* out, size_t size);

#endif
