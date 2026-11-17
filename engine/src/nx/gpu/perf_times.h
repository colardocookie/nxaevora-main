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
    PT_PASS_END,     /* vk_render_end ending an open pass (PERF-11, research 20:41) */
    PT_PASS_BEGIN,   /* a game pass begun: from the end of the pass before to its vkCmdBeginRendering recorded */
    /* a present job's tail, each from its syncpoint increment queued at frame_end (PERF-10, research 19:46) */
    PT_TAIL_SUBMIT, /* to the recorder thread handing the submission the increment waits for to the driver */
    PT_TAIL_DONE,   /* to the release thread's wait for that submission returning */
    PT_TAIL_INCR,   /* to the increment performed, after the landings before it */
    PT_TAIL_FREE,   /* to the end of the release_through that performed it: vk_lock released */
    PT_TAIL_WAKE,   /* to the game's next nvnSyncWait returning after the increment */
    PT_SITES
};
/* counts a frame, not times */
enum {
    PC_WB_OUTPUTS, /* graphics storage outputs landed (buffer.c, as g_wb_outputs counts them) */
    PC_WB_BYTES,   /* their bytes */
    PC_WC_BYTES,   /* bytes vk_write_changed wrote to guest memory, the ones the shader changed */
    PC_TAIL_SUBMITS, /* graphics submissions from one present's increment to the next, the increment's own included */
    PC_TAIL_AFTER,   /* of those, the ones not complete when the increment was queued */
    PC_PASS_BEGINS,  /* game render passes begun */
    PC_END_OTHER,    /* open passes ended, by the reason the caller gave: none named */
    PC_END_GPU_SRC,  /* gfx_gpu_src's barrier for a draw that reads an earlier result */
    PC_END_COPY,     /* a buffer copy (overlay, owned input, patch, staging) */
    PC_END_IMAGE,    /* an image upload, copy, blit or readback */
    PC_END_TARGET,   /* the next draw's targets or their layouts differ */
    PC_SRC_BARRIERS, /* gfx_gpu_src's barriers */
    PC_SRC_CLEAN,    /* of those, ones whose result no draw bound for writing since the barrier before */
    PC_SRC_UNKNOWN,  /* of those, ones whose result's last writer was not kept (the table's slot taken) */
    /* from here on printed as totals, not a frame's mean */
    PC_TAIL_UNGATED, /* increments that waited for no submission: no submit or done time */
    PC_TAIL_PARTIAL, /* tails closed with a time missing (performed by another path, or no wait after it) */
    PC_TAIL_LOST,    /* tails whose slot was needed again before they closed */
    PC_COUNTS
};

typedef struct {
    uint64_t sum, max, count;
} PerfTime;
/* A tail's times, each written once by the thread that reaches that point, and closed when both its vk_lock release
   and the game's wake are in */
#define PERF_TAILS 4u
typedef struct {
    uint64_t armed, gate, seq, t0, t_submit, t_done, t_incr, t_free, t_wake;
    int closes;
} PerfTail;
typedef struct {
    PerfTime site[PT_SITES];
    PerfTail tail[PERF_TAILS];
    uint64_t tail_next, tail_handed, tail_mark; /* the next tail's number, the last value handed, the submit count */
    int tail_arm;                               /* the GPU thread's: frame_end armed the next increment */
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
/* SNX_ARCEUS_PERF_SUBMIT_US, bench only (research 20:51): the GPU thread's recording time a submission holds at most
   (vk/cmd.c vk_frame_check), upstream's 1 ms unless set to 250 to 8000 us. Read once at start */
#define PERF_SUBMIT_EVERY_NS 1000000ull
extern uint64_t g_perf_submit_every_ns;

/* why the next vk_render_end ends the pass, a PC_END_* count less PC_END_OTHER: set just before the call */
enum { PE_OTHER = 0, PE_GPU_SRC, PE_COPY, PE_IMAGE, PE_TARGET };
extern int g_perf_pass_why; /* the GPU thread's */
static inline void perf_pass_why(int why) {
    if (g_perf_times) g_perf_pass_why = why;
}

void perf_time_add(PerfTimes* p, int site, uint64_t ns);
void perf_time_named(PerfTimes* p, const char* what, uint64_t ns);
void perf_count(PerfTimes* p, int counter, uint64_t n);
/* The tail's points. arm and take on the GPU thread: frame_end arms, the next increment it queues takes */
void perf_tail_arm(PerfTimes* p);
int perf_tail_take(PerfTimes* p);
/* the armed increment queued as release seq, waiting for submission gate (0: none), with the graphics submissions
   recorded and done so far */
void perf_tail_queued(PerfTimes* p, uint64_t seq, uint64_t gate, uint64_t submitted, uint64_t done, uint64_t now);
void perf_tail_handed(PerfTimes* p, uint64_t value, uint64_t now);  /* the recorder handed submission value over */
void perf_tail_done(PerfTimes* p, uint64_t value, uint64_t now);    /* the release thread's wait for value returned */
void perf_tail_performed(PerfTimes* p, uint64_t seq, uint64_t now); /* release seq, an increment, performed */
void perf_tail_freed(PerfTimes* p, uint64_t now);                   /* a release_through ended, vk_lock released */
void perf_tail_woke(PerfTimes* p, uint64_t now);                    /* the game's nvnSyncWait returned */
/* Writes the window's line into out and starts the next window. frames: the presents it covers. */
size_t perf_times_line(PerfTimes* p, uint32_t frames, char* out, size_t size);

#endif
