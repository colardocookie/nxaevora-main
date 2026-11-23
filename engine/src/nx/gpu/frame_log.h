/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* DAILY-LT (research 04:29, log only): the daily frame-time and latency test. SNX_ARCEUS_FRAME_LOG=1 keeps the time
   between the game's present posts, where perf-times takes its period, and writes every interval. With
   SNX_ARCEUS_LATENCY_PROBE=1 a scripted press is stamped where HID hands it to the guest, and the presents around it
   get a line each with a picture difference. Off, the sites test the switch only. */
#ifndef SNX_GPU_FRAME_LOG_H
#define SNX_GPU_FRAME_LOG_H
#include <stddef.h>
#include <stdint.h>

extern int g_frame_log;       /* SNX_ARCEUS_FRAME_LOG=1 */
extern int g_latency_probe;   /* SNX_ARCEUS_LATENCY_PROBE=1 */

#define FRAME_LOG_WINDOW 600u /* intervals a buffer */
#define FRAME_LOG_PART 150u   /* intervals a line: a log line holds 2048 bytes */
#define LAT_BEFORE 30u        /* presents before a press that get a line */
#define LAT_AFTER 90u         /* presents from a press on that get a line */
#define LAT_W 64u             /* the presented picture reduced to this size */
#define LAT_H 36u
#define LAT_POSTED 16u        /* presents posted and not yet replayed that the probe follows */

/* Two buffers of intervals in microseconds. The game's thread fills one under objs_lock and hands it over full; the
   GPU thread writes it out and gives it back. A buffer filled while the other is still out is dropped and counted */
typedef struct {
    uint32_t us[2][FRAME_LOG_WINDOW];
    uint64_t first[2]; /* the present number of a buffer's first interval */
    uint64_t last_ns;
    uint32_t n, cur;
    uint32_t ready;    /* atomic: 1 + the buffer handed over, or 0 */
    uint32_t lost;     /* atomic: buffers dropped */
} FrameLog;
extern FrameLog g_frame_log_buf;
/* Present number no posted at now_ns. The first present has no interval */
void frame_log_present(FrameLog* f, uint64_t no, uint64_t now_ns);
/* The buffer handed over, 1 + its index, or 0. Then its lines, part 0 to FRAME_LOG_WINDOW / FRAME_LOG_PART - 1, and
   frame_log_done gives it back */
int frame_log_ready(FrameLog* f);
size_t frame_log_line(const FrameLog* f, int buffer, unsigned part, char* out, size_t size);
void frame_log_done(FrameLog* f);

/* One present as the probe saw it: its number and post time, the guest jobs in flight and its acquire's wait at the
   post, and the picture difference to the present before (-1 without one) */
typedef struct {
    uint64_t no, post_ns, acquire_ns;
    uint32_t jobs, shown;
    double diff;
} LatPresent;
typedef struct {
    uint64_t t0;      /* atomic: the newest press, written on HID's thread */
    uint64_t mask;    /* HID's thread: the scripted buttons of its last update */
    uint64_t seen_t0; /* the GPU thread's from here on: the press the lines follow */
    uint32_t after, n, head;
    LatPresent ring[LAT_BEFORE]; /* the newest presents without a line */
    LatPresent posted[LAT_POSTED];  /* posted on the game's thread, taken in order on the GPU thread */
    uint32_t post_head, post_tail;  /* atomic */
    uint32_t post_lost;             /* atomic: posts that found it full */
} Latency;
extern Latency g_latency;
extern LatPresent g_lat_next; /* the GPU thread's: the present the renderer shows now, no 0 for none */
/* The game's thread, one at a time: a present posted. The GPU thread: the oldest one not taken, 1, or 0 for none */
void latency_posted(Latency* l, const LatPresent* p);
int latency_take(Latency* l, LatPresent* out);
/* HID's thread, at each update with the scripted buttons in it: 1 when one is pressed that the last update had not,
   then l->t0 is now_ns */
int latency_input(Latency* l, uint64_t mask, uint64_t now_ns);
/* The GPU thread, each present in order: put gets the lines due, the presents within LAT_BEFORE before a press and
   LAT_AFTER from it on, each once */
void latency_result(Latency* l, const LatPresent* p, void (*put)(const char* line));
/* The mean absolute difference of two RGBA8 pictures over red, green and blue, 0 to 255 */
double latency_picture_diff(const uint8_t* a, const uint8_t* b, size_t pixels);

#endif
