/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* SNX_ARCEUS_PERF_TIMES's counting (gpu/perf_times.c): sums, largest values, the named waits, the counts and the
   line, which starts the next window empty. */
#include "nx/gpu/perf_times.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

static void test_line(void) {
    static PerfTimes p;
    static const char lock[] = "vk_lock, held", sub[] = "a submission";
    char line[2048];
    memset(&p, 0, sizeof p);
    perf_time_add(&p, PT_START_END, 3000000);
    perf_time_add(&p, PT_START_END, 5000000);
    perf_time_add(&p, PT_IDLE, 1000000);
    perf_time_add(&p, PT_SITES, 7); /* out of range: ignored */
    perf_time_add(&p, -1, 7);
    perf_time_named(&p, lock, 2000000);
    perf_time_named(&p, sub, 500000);
    perf_time_named(&p, lock, 4000000);
    perf_time_named(&p, NULL, 9); /* no name: counted as lost */
    perf_count(&p, PC_WB_OUTPUTS, 3);
    perf_count(&p, PC_WB_BYTES, 1000);
    p.overlaps = 1;
    perf_count(&p, PC_COUNTS, 5); /* out of range: overlaps, next in the struct, stays 1 */
    perf_times_line(&p, 2, line, sizeof line);
    CHECK(strstr(line, "perf-times: frames=2 "));
    CHECK(strstr(line, " start_end=4.000/5.000 ")); /* 8 ms over 2 frames, the largest 5 */
    CHECK(strstr(line, " idle=0.500/1.000 "));
    CHECK(strstr(line, " post_start=0.000/0.000 "));
    CHECK(strstr(line, " [vk_lock, held]=3.000/4.000"));
    CHECK(strstr(line, " [a submission]=0.250/0.500"));
    CHECK(strstr(line, " wb_outputs=1.5 wb_bytes=500.0 wc_bytes=0.0"));
    CHECK(strstr(line, " overlaps=1 named_lost=1"));
    perf_times_line(&p, 2, line, sizeof line); /* the next window starts empty */
    CHECK(strstr(line, " start_end=0.000/0.000 "));
    CHECK(!strstr(line, "[vk_lock")); /* a name with no wait this window is left out */
    CHECK(strstr(line, " wb_outputs=0.0 "));
    CHECK(strstr(line, " overlaps=0 named_lost=0"));
}

static void test_named_full(void) {
    static PerfTimes p;
    static const char names[PERF_TIMES_NAMED + 1][4] = {"a", "b", "c", "d", "e", "f", "g", "h", "i"};
    memset(&p, 0, sizeof p);
    for (unsigned i = 0; i <= PERF_TIMES_NAMED; i++) perf_time_named(&p, names[i], 1);
    CHECK(p.named_lost == 1);
    CHECK(p.named[PERF_TIMES_NAMED - 1].what == names[PERF_TIMES_NAMED - 1]);
    perf_time_named(&p, names[0], 1); /* a name already there still counts */
    CHECK(p.named[0].t.count == 2 && p.named_lost == 1);
}

static void test_small_buffers(void) {
    static PerfTimes p;
    char tiny[24];
    memset(&p, 0, sizeof p);
    perf_time_add(&p, PT_SHOW, 1);
    const size_t n = perf_times_line(&p, 0, tiny, sizeof tiny); /* 0 frames reads as 1 */
    CHECK(n < sizeof tiny && strlen(tiny) == n);
    CHECK(!strncmp(tiny, "perf-times: frames=1", 20));
    CHECK(perf_times_line(&p, 1, tiny, 0) == 0);
}

/* a present's tail: each point once, the times from the increment's queueing, closed by the later of the vk_lock
   release and the game's wake, ungated, partial and lost tails counted */
static void test_tail(void) {
    static PerfTimes p;
    memset(&p, 0, sizeof p);
    CHECK(!perf_tail_take(&p));
    perf_tail_arm(&p);
    CHECK(perf_tail_take(&p) && !perf_tail_take(&p));
    perf_tail_queued(&p, 10, 5, 4, 3, 100);
    perf_tail_handed(&p, 4, 120); /* an earlier submission */
    CHECK(!p.tail[0].t_submit);
    perf_tail_handed(&p, 5, 150);
    perf_tail_handed(&p, 6, 160); /* once only */
    perf_tail_done(&p, 4, 200);
    CHECK(!p.tail[0].t_done);
    perf_tail_done(&p, 5, 300);
    perf_tail_woke(&p, 310); /* before its increment: not this tail's */
    perf_tail_freed(&p, 312); /* a release_through before it: not this tail's either */
    CHECK(!p.tail[0].t_wake && !p.tail[0].t_free);
    perf_tail_performed(&p, 9, 315);
    CHECK(!p.tail[0].t_incr);
    perf_tail_performed(&p, 10, 320);
    perf_tail_woke(&p, 400); /* the wake before the release: the release closes it */
    CHECK(p.tail[0].armed && p.tail[0].closes == 1);
    perf_tail_freed(&p, 330);
    CHECK(!p.tail[0].armed);
    CHECK(p.site[PT_TAIL_SUBMIT].sum == 50 && p.site[PT_TAIL_DONE].sum == 200 && p.site[PT_TAIL_INCR].sum == 220);
    CHECK(p.site[PT_TAIL_FREE].sum == 230 && p.site[PT_TAIL_WAKE].sum == 300 && p.site[PT_TAIL_WAKE].count == 1);
    CHECK(p.count[PC_TAIL_AFTER] == 2 && p.count[PC_TAIL_SUBMITS] == 0 && !p.count[PC_TAIL_PARTIAL]);
    /* handed over before it was queued: no wait for the recorder. 3 submissions since the last tail */
    perf_tail_handed(&p, 8, 900);
    perf_tail_queued(&p, 20, 8, 8, 8, 1000);
    CHECK(p.tail[1].t_submit == 1000 && p.count[PC_TAIL_SUBMITS] == 3);
    perf_tail_performed(&p, 20, 1100);
    perf_tail_freed(&p, 1110);
    perf_tail_woke(&p, 1120); /* closed without a done time: partial */
    CHECK(!p.tail[1].armed && p.count[PC_TAIL_PARTIAL] == 1 && p.site[PT_TAIL_DONE].count == 1);
    /* no gate: no submit or done time, not partial */
    perf_tail_queued(&p, 30, 0, 8, 8, 2000);
    perf_tail_performed(&p, 30, 2010);
    perf_tail_woke(&p, 2020);
    perf_tail_freed(&p, 2030);
    CHECK(p.count[PC_TAIL_UNGATED] == 1 && p.count[PC_TAIL_PARTIAL] == 1 && p.site[PT_TAIL_SUBMIT].count == 2);
    /* the oldest performed tail takes the wake */
    perf_tail_queued(&p, 40, 0, 8, 8, 3000);
    perf_tail_queued(&p, 41, 0, 8, 8, 3001);
    perf_tail_performed(&p, 41, 3010);
    perf_tail_performed(&p, 40, 3011);
    perf_tail_woke(&p, 3020);
    CHECK(p.tail[3].t_wake == 3020 && !p.tail[0].t_wake);
    /* a slot needed again before it closed */
    for (int i = 0; i < 4; i++) perf_tail_queued(&p, 50 + (uint64_t)i, 0, 8, 8, 4000); /* 40 and 41 still open */
    CHECK(p.count[PC_TAIL_LOST] == 2);
    char line[2048];
    perf_times_line(&p, 1, line, sizeof line);
    CHECK(strstr(line, " tail_lost=2 ") && strstr(line, " tail_partial=1 ") && strstr(line, " tail_ungated=7 "));
    CHECK(strstr(line, " tail_incr=0.000/0.000/3 ")); /* three tails closed, their times under a microsecond */
}
/* a tail closed in the line: its mean over the tails, not over the frames, and how many */
static void test_tail_line(void) {
    static PerfTimes p;
    char line[2048];
    memset(&p, 0, sizeof p);
    perf_tail_queued(&p, 1, 0, 0, 0, 1000000);
    perf_tail_performed(&p, 1, 3000000);
    perf_tail_freed(&p, 3500000);
    perf_tail_woke(&p, 4000000);
    perf_count(&p, PC_END_OTHER + PE_GPU_SRC, 1200); /* PERF-11's pass counts: a frame's mean, by reason */
    perf_count(&p, PC_SRC_CLEAN, 300);
    perf_time_add(&p, PT_PASS_END, 600000);
    perf_times_line(&p, 600, line, sizeof line);
    CHECK(strstr(line, " tail_incr=2.000/2.000/1 ") && strstr(line, " tail_wake=3.000/3.000/1 "));
    CHECK(strstr(line, " end_gpu_src=2.0 ") && strstr(line, " src_clean=0.5 ") && strstr(line, " end_other=0.0 "));
    CHECK(strstr(line, " pass_end=0.001/0.600 ") && strstr(line, " passes=0.0 "));
}
/* reviewer 20:08 F1: a slot reused by the GPU thread while another thread closes it (run under TSan by hand) */
static PerfTimes threaded;
static int threaded_done;
static void* tail_writer(void* arg) {
    (void)arg;
    for (uint64_t i = 1; i <= 50000; i++) {
        perf_tail_queued(&threaded, i, 0, 0, 0, 100 * i);
        perf_tail_performed(&threaded, i, 100 * i + 10);
        perf_tail_freed(&threaded, 100 * i + 20);
    }
    __atomic_store_n(&threaded_done, 1, __ATOMIC_RELEASE);
    return NULL;
}
static void* tail_waker(void* arg) {
    (void)arg;
    for (uint64_t i = 1; !__atomic_load_n(&threaded_done, __ATOMIC_ACQUIRE); i++)
        perf_tail_woke(&threaded, 100 * i + 30);
    return NULL;
}
static void test_tail_threads(void) {
    pthread_t w, k;
    if (pthread_create(&k, NULL, tail_waker, NULL)) {
        CHECK(!"the waker starts");
        return;
    }
    if (pthread_create(&w, NULL, tail_writer, NULL)) {
        CHECK(!"the writer starts");
        __atomic_store_n(&threaded_done, 1, __ATOMIC_RELEASE);
    } else {
        CHECK(!pthread_join(w, NULL));
    }
    CHECK(!pthread_join(k, NULL));
    /* a late close can land in the slot's next tail (reviewer note 2), so closed and lost are not a partition */
    CHECK(threaded.site[PT_TAIL_INCR].count > 0 && threaded.site[PT_TAIL_INCR].count <= 50000);
}

/* PERF-12: submissions by site, those in the last 1/2/4 ms before frame_end, each one's time from handing to its
   completion seen, and the last five a frame queued, whether seen before or after its frame_end */
static void test_subs(void) {
    static PerfTimes p;
    memset(&p, 0, sizeof p);
    const uint64_t ms = 1000000;
    perf_sub_frame_end(&p, 1 * ms); /* the frame before */
    for (uint64_t v = 1; v <= 7; v++) { /* queued at 2..8 ms, handed 0.1 ms later */
        perf_sub_queued(&p, v, v == 7 ? SW_RELEASE : SW_SLICE, (1 + v) * ms);
        perf_sub_handed(&p, v, (1 + v) * ms + 100000);
    }
    perf_sub_queued(&p, 8, SW_SITES + 3, 9 * ms); /* an unknown site: other, never handed */
    perf_sub_seen(&p, 2, 9 * ms); /* 1 and 2 seen, 6.9 and 5.9 ms after handing */
    perf_sub_seen(&p, 1, 9 * ms); /* again, older: nothing */
    CHECK(p.site[PT_SUB_DONE].count == 2 && p.site[PT_SUB_DONE].sum == 6900000 + 5900000);
    perf_sub_frame_end(&p, 9 * ms + 500000); /* 8, 7 within 1 ms; 7 and 6 within 4; 5..8 the ones within 4 ms */
    CHECK(p.sub_site[SW_SLICE][0] == 6 && p.sub_site[SW_RELEASE][0] == 1 && p.sub_site[SW_OTHER][0] == 1);
    CHECK(p.sub_site[SW_OTHER][1] == 1 && p.sub_site[SW_RELEASE][1] == 0 && p.sub_site[SW_RELEASE][2] == 1);
    CHECK(p.sub_site[SW_SLICE][1] == 0 && p.sub_site[SW_SLICE][2] == 0 && p.sub_site[SW_SLICE][3] == 2);
    CHECK(p.site[PT_SUB_LAST5].count == 0); /* the last five are 4..8: none seen yet */
    perf_sub_seen(&p, 8, 10 * ms); /* 3..8 seen at 10 ms: 8 was never handed */
    CHECK(p.site[PT_SUB_DONE].count == 7 && p.site[PT_SUB_LAST5].count == 4);
    CHECK(p.site[PT_SUB_LAST5].sum == (10 * ms - 5100000) + (10 * ms - 6100000) + (10 * ms - 7100000) +
                                          (10 * ms - 8100000));
    /* a frame whose last five were seen before its frame_end: the time kept at the seeing counts */
    perf_sub_queued(&p, 9, SW_FRAME, 11 * ms);
    perf_sub_handed(&p, 9 + PERF_SUBS, 10 * ms); /* another submission of the slot: not this one's */
    CHECK(!p.sub[9 % PERF_SUBS].handed);
    perf_sub_handed(&p, 9, 11 * ms);
    perf_sub_seen(&p, 9, 12 * ms);
    perf_sub_frame_end(&p, 13 * ms); /* queued exactly 2 ms before: in the 2 and 4 ms windows, not the 1 ms one */
    CHECK(p.sub_site[SW_FRAME][1] == 0 && p.sub_site[SW_FRAME][2] == 1 && p.sub_site[SW_FRAME][3] == 1);
    CHECK(p.site[PT_SUB_LAST5].count == 5 && p.site[PT_SUB_LAST5].max >= 1 * ms);
    /* a slot needed again before its completion was seen */
    for (uint64_t v = 10; v < 10 + PERF_SUBS + 1; v++) perf_sub_queued(&p, v, SW_SLICE, 14 * ms);
    CHECK(p.sub_lost == 1);
    char line[3072];
    perf_times_line(&p, 1, line, sizeof line);
    CHECK(strstr(line, " sub_slice=") && strstr(line, " sub_release=1.0/0.0/1.0/1.0 ") && strstr(line, " sub_lost=1"));
    CHECK(!strstr(line, " sub_compute=")); /* a site with none is left out */
    CHECK(p.sub_site[SW_SLICE][0] == 0 && p.sub_lost == 0);
}
/* two threads seeing completions at once: each submission's time is added once */
static PerfTimes subs_threaded;
static void* subs_seer(void* arg) {
    const uint64_t step = (uint64_t)(uintptr_t)arg;
    for (uint64_t v = step; v <= 200; v += 2) perf_sub_seen(&subs_threaded, v, 1000 + v);
    return NULL;
}
static void test_subs_threads(void) {
    for (uint64_t v = 1; v <= 200; v++) {
        perf_sub_queued(&subs_threaded, v, SW_SLICE, v);
        perf_sub_handed(&subs_threaded, v, v);
    }
    pthread_t a, b;
    if (pthread_create(&a, NULL, subs_seer, (void*)(uintptr_t)1)) {
        CHECK(!"the first seer starts");
        return;
    }
    if (!pthread_create(&b, NULL, subs_seer, (void*)(uintptr_t)2))
        CHECK(!pthread_join(b, NULL));
    else
        CHECK(!"the second seer starts");
    CHECK(!pthread_join(a, NULL));
    CHECK(subs_threaded.site[PT_SUB_DONE].count == 200 && subs_threaded.sub_seen == 200);
}

/* reviewer 21:58 F1: submissions from a thread that holds vk_lock (here a mutex) while the GPU thread runs frame_end
   and the line without it (run under TSan by hand) */
static PerfTimes subs_producer;
static pthread_mutex_t subs_lock = PTHREAD_MUTEX_INITIALIZER;
static int subs_done;
static void* subs_queuer(void* arg) {
    (void)arg;
    for (uint64_t v = 1; v <= 50000; v++) {
        pthread_mutex_lock(&subs_lock);
        perf_sub_queued(&subs_producer, v, (int)(v % SW_SITES), 10 * v);
        perf_sub_handed(&subs_producer, v, 10 * v + 1);
        perf_sub_seen(&subs_producer, v, 10 * v + 5);
        pthread_mutex_unlock(&subs_lock);
    }
    __atomic_store_n(&subs_done, 1, __ATOMIC_RELEASE);
    return NULL;
}
static void test_subs_producer(void) {
    pthread_t q;
    if (pthread_create(&q, NULL, subs_queuer, NULL)) {
        CHECK(!"the producer starts");
        return;
    }
    char line[3072];
    uint64_t total = 0;
    for (uint64_t t = 1; !__atomic_load_n(&subs_done, __ATOMIC_ACQUIRE); t++) {
        perf_sub_frame_end(&subs_producer, 100 * t);
        perf_times_line(&subs_producer, 1, line, sizeof line);
        total++;
    }
    CHECK(!pthread_join(q, NULL) && total > 0);
}

/* PERF-14: a tail's window, from its increment queued to tail_done, split by the release thread's intervals in it,
   clipped at both ends, the rest as other, and the outputs landed under the holds in it */
static void test_rel_window(void) {
    static PerfTimes p;
    memset(&p, 0, sizeof p);
    perf_rel_interval(&p, REL_HOLD, 10, 50, 7); /* before the window: not counted */
    perf_rel_interval(&p, REL_GPU, 100, 300, 0);
    perf_tail_queued(&p, 1, 3, 2, 1, 150);
    perf_rel_interval(&p, REL_LOCK, 300, 320, 9); /* outputs count under a hold only */
    perf_rel_interval(&p, REL_HOLD, 320, 400, 5);
    perf_rel_interval(&p, REL_GPU, 410, 500, 0);
    perf_rel_interval(&p, REL_STATES, 400, 410, 0); /* no such state: ignored */
    perf_rel_interval(&p, REL_GPU, 420, 410, 0);    /* ends before it starts: ignored */
    perf_tail_done(&p, 2, 480);                       /* not its gate yet */
    CHECK(!p.site[PT_WIN_GPU].count);
    perf_tail_done(&p, 3, 500);
    CHECK(p.site[PT_WIN_GPU].sum == 150 + 90 && p.site[PT_WIN_LOCK].sum == 20 && p.site[PT_WIN_HOLD].sum == 80);
    CHECK(p.site[PT_WIN_OTHER].sum == 10 && p.count[PC_WIN_OUTPUTS] == 5 && p.site[PT_WIN_GPU].count == 1);
    perf_tail_done(&p, 4, 600); /* stamped once: the window is not split again */
    CHECK(p.site[PT_WIN_GPU].count == 1);
    /* the window's end cuts an interval that runs past it */
    perf_tail_queued(&p, 2, 9, 2, 1, 1000);
    perf_rel_interval(&p, REL_HOLD, 900, 1100, 3);
    perf_tail_done(&p, 9, 1050);
    CHECK(p.site[PT_WIN_HOLD].sum == 80 + 50 && p.site[PT_WIN_OTHER].sum == 10 + 0 && p.count[PC_WIN_OUTPUTS] == 8);
    perf_count(&p, PC_WB_OUTPUTS, 4);
    CHECK(p.wb_total == 4); /* kept apart from the window's counts, never reset */
    char line[3072];
    perf_times_line(&p, 1, line, sizeof line);
    CHECK(strstr(line, " win_hold=0.000/0.000/2 ") && strstr(line, " win_outputs=8.0 ") && p.wb_total == 4);
}

/* PERF-16's table: a copy's site, the pass it ended by target with that copy's bytes, the draws' reads */
static void test_copies(void) {
    static PerfCopies c;
    const PerfTarget a = {0x440e90000ull, 1920, 1080, 97}, b = {0x500000000ull, 960, 540, 97};
    const PerfTarget a_half = {0x440e90000ull, 960, 540, 97}, none = {0, 0, 0, 0};
    perf_copies_reset(&c);
    CHECK(c.site == -1 && c.slot == -1 && c.draw == -1);
    perf_copy_pass_begin(&c, &a);
    perf_copy_begin(&c, PK_COMPOSE);
    perf_copy_pass_end(&c, &a, PE_COPY);
    perf_copy_bytes(&c, 100);
    perf_copy_bytes(&c, 28);
    CHECK(c.ops[PK_COMPOSE] == 1 && c.ends[PK_COMPOSE] == 1 && c.bytes[PK_COMPOSE] == 128);
    CHECK(c.tg[0].passes == 1 && c.tg[0].why[PE_COPY] == 1 && c.tg[0].ends[PK_COMPOSE] == 1);
    CHECK(c.tg[0].bytes[PK_COMPOSE] == 128);
    perf_copy_begin(&c, PK_PATCH); /* no pass open: its bytes are the site's, no target's */
    perf_copy_bytes(&c, 64);
    CHECK(c.ops[PK_PATCH] == 1 && c.ends[PK_PATCH] == 0 && c.bytes[PK_PATCH] == 64);
    CHECK(c.tg[0].bytes[PK_PATCH] == 0 && c.tg[0].bytes[PK_COMPOSE] == 128);
    perf_copy_pass_end(&c, &b, PE_GPU_SRC); /* another reason: the target's, and no copy's end */
    CHECK(c.tg[1].why[PE_GPU_SRC] == 1 && c.ends[PK_PATCH] == 0 && c.slot == -1);
    perf_copy_pass_end(&c, &b, 99); /* out of range: other */
    perf_copy_pass_end(&c, &b, -1);
    CHECK(c.tg[1].why[PE_OTHER] == 2);
    perf_copy_begin(&c, PK_SITES); /* out of range: nothing until the next begin */
    perf_copy_pass_end(&c, &b, PE_COPY);
    perf_copy_bytes(&c, 5);
    CHECK(c.site == -1 && c.tg[1].why[PE_COPY] == 1 && c.bytes[PK_PATCH] == 64);
    uint64_t ends = 0, ops = 0;
    for (int k = 0; k < PK_SITES; k++) ends += c.ends[k], ops += c.ops[k];
    CHECK(ends == 1 && ops == 2);
    perf_copy_begin(&c, PK_DMA); /* a pass with no target: counted for the site alone */
    perf_copy_pass_end(&c, &none, PE_COPY);
    perf_copy_bytes(&c, 7);
    CHECK(c.ends[PK_DMA] == 1 && c.bytes[PK_DMA] == 7 && c.slot == -1);
    perf_copy_pass_end(&c, &a_half, PE_TARGET); /* the same address at another size is another target */
    CHECK(c.tg[2].t.w == 960 && c.tg[2].why[PE_TARGET] == 1 && c.tg[0].why[PE_TARGET] == 0);
    perf_copy_pass_begin(&c, NULL);
    CHECK(c.lost == 0);

    perf_copy_draw(&c, &b);
    perf_copy_src(&c, PB_INPUT, PS_REUSE);
    perf_copy_src(&c, PB_WRITE, PS_SUBMIT);
    perf_copy_draw(&c, &a);
    perf_copy_src(&c, PB_READ, PS_SEVERAL);
    perf_copy_draw(&c, NULL);
    perf_copy_src(&c, PB_READ, PS_CHANGED); /* no draw: a dispatch's */
    perf_copy_src(&c, PB_KINDS, PS_REUSE);  /* out of range: ignored */
    perf_copy_src(&c, PB_READ, PS_N);
    perf_copy_src(&c, -1, PS_REUSE);
    perf_copy_src(&c, PB_READ, -1);
    CHECK(c.tg[1].draws == 1 && c.tg[0].draws == 1);
    CHECK(c.tg[1].src[PB_INPUT][PS_REUSE] == 1 && c.tg[1].src[PB_WRITE][PS_SUBMIT] == 1);
    CHECK(c.tg[0].src[PB_READ][PS_SEVERAL] == 1 && c.tg[0].src[PB_READ][PS_CHANGED] == 0);
    CHECK(c.src[PB_READ][PS_CHANGED] == 1 && c.nodraw == 1 && c.src[PB_INPUT][PS_REUSE] == 1);
    CHECK(c.src[PB_READ][PS_SEVERAL] == 1 && c.src[PB_WRITE][PS_SUBMIT] == 1);
}

static void test_copies_full(void) {
    static PerfCopies c;
    perf_copies_reset(&c);
    for (unsigned i = 0; i <= PERF_COPY_TARGETS; i++) {
        const PerfTarget t = {0x1000ull * (i + 1), 64, 64, 1};
        perf_copy_pass_begin(&c, &t);
    }
    CHECK(c.lost == 1);
    CHECK(c.tg[PERF_COPY_TARGETS - 1].t.gva == 0x1000ull * PERF_COPY_TARGETS);
    const PerfTarget first = {0x1000, 64, 64, 1}, extra = {0x1000ull * (PERF_COPY_TARGETS + 1), 64, 64, 1};
    perf_copy_pass_begin(&c, &first); /* a target already there still counts */
    CHECK(c.tg[0].passes == 2 && c.lost == 1);
    perf_copy_begin(&c, PK_OWN); /* a copy ending a pass on a target with no slot: the site's, not a slot's */
    perf_copy_pass_end(&c, &extra, PE_COPY);
    perf_copy_bytes(&c, 9);
    CHECK(c.lost == 2 && c.ends[PK_OWN] == 1 && c.bytes[PK_OWN] == 9 && c.slot == -1);
    perf_copy_draw(&c, &extra);
    perf_copy_src(&c, PB_INPUT, PS_PART);
    CHECK(c.lost == 3 && c.nodraw == 1 && c.src[PB_INPUT][PS_PART] == 1);
}

static void test_copies_line(void) {
    static PerfCopies c;
    char line[2048];
    perf_copies_reset(&c);
    /* six targets with scores 1..6 (copy ends, and reads not in place), one with draws only */
    for (unsigned i = 0; i < 6; i++) {
        const PerfTarget t = {0x10000ull * (i + 1), 100 + i, 50, 37};
        perf_copy_draw(&c, &t);
        for (unsigned k = 0; k <= i; k++) {
            perf_copy_begin(&c, PK_OWNED_READ);
            perf_copy_pass_end(&c, &t, PE_COPY);
            perf_copy_bytes(&c, 2048);
        }
        perf_copy_src(&c, PB_INPUT, PS_REUSE); /* in place: not scored */
    }
    const PerfTarget quiet = {0x90000, 8, 8, 1};
    perf_copy_draw(&c, &quiet);
    perf_copy_src(&c, PB_READ, PS_REUSE);
    perf_copy_draw(&c, NULL);
    const PerfTarget top = {0x60000, 105, 50, 37};
    perf_copy_draw(&c, &top);
    perf_copy_src(&c, PB_READ, PS_ALIGN); /* the top one gains a read not in place: score 7 */
    __atomic_fetch_add(&c.elsewhere, 3, __ATOMIC_RELAXED);
    perf_copies_line(&c, 2, line, sizeof line);
    CHECK(strstr(line, "perf-copies: frames=2 "));
    CHECK(strstr(line, " lost=0 nodraw=0.0 elsewhere=3 "));
    CHECK(strstr(line, " owned_read=10.5/10.5/21.0")); /* 21 copies of 2 KB over 2 frames */
    CHECK(!strstr(line, " compose="));                  /* a site with no copy is left out */
    CHECK(strstr(line, " in=3.0/0.0/0.0/0.0/0.0/0.0/0.0 rd=0.5/0.0/0.0/0.5/0.0/0.0/0.0"));
    CHECK(!strstr(line, " wr="));
    const char* t6 = strstr(line, " [60000 105x50 f37] draws=1.0 passes=0.0 ends=0.0/0.0/3.0/0.0/0.0");
    const char* t5 = strstr(line, " [50000 104x50 f37]");
    const char* t4 = strstr(line, " [40000 103x50 f37]");
    const char* t3 = strstr(line, " [30000 102x50 f37]");
    CHECK(t6 && t5 && t4 && t3 && t6 < t5 && t5 < t4 && t4 < t3); /* highest first, four of them */
    CHECK(!strstr(line, "[20000 ") && !strstr(line, "[10000 ") && !strstr(line, "[90000 "));
    CHECK(t6 && strstr(t6, " end_owned_read=3.0/6.0 in=0.5/0.0/0.0/0.0/0.0/0.0/0.0 rd=0.0/0.0/0.0/0.5/"));
    CHECK(c.site == -1 && c.draw == -1 && c.last == -1 && c.tg[0].t.gva == 0 && c.ops[PK_OWNED_READ] == 0);
    perf_copies_line(&c, 2, line, sizeof line); /* the next window starts empty */
    CHECK(strstr(line, " lost=0 nodraw=0.0 elsewhere=0"));
    CHECK(!strstr(line, "owned_read") && !strstr(line, " in=") && !strstr(line, "["));
    /* two tied targets in slot order, one with reads in place only and one with nothing: not printed */
    const PerfTarget p1 = {0xa0000, 4, 4, 1}, p2 = {0xb0000, 4, 4, 1}, p3 = {0xc0000, 4, 4, 1}, p4 = {0xd000, 4, 4, 1};
    perf_copy_draw(&c, &p1);
    perf_copy_src(&c, PB_WRITE, PS_OLD);
    perf_copy_draw(&c, &p2);
    perf_copy_src(&c, PB_WRITE, PS_PART);
    perf_copy_draw(&c, &p3);
    perf_copy_src(&c, PB_WRITE, PS_REUSE);
    perf_copy_pass_begin(&c, &p4);
    perf_copies_line(&c, 0, line, sizeof line); /* no frames: counted as one */
    CHECK(strstr(line, "frames=1 "));
    CHECK(strstr(line, " wr=1.0/0.0/1.0/0.0/0.0/1.0/0.0"));
    const char* q1 = strstr(line, " [a0000 "), *q2 = strstr(line, " [b0000 ");
    CHECK(q1 && q2 && q1 < q2 && !strstr(line, "[c0000 ") && !strstr(line, "[d000 "));
    char small[24];
    memset(small, 'x', sizeof small);
    const size_t n = perf_copies_line(&c, 2, small, sizeof small); /* cut, and terminated */
    CHECK(n == sizeof small - 1 && small[n] == 0);
    CHECK(perf_copies_line(&c, 2, small, 0) == 0);
}

/* other threads add to elsewhere while the GPU thread writes the line (run under -fsanitize=thread) */
static void* copies_elsewhere(void* arg) {
    PerfCopies* c = arg;
    for (unsigned i = 0; i < 100000; i++) __atomic_fetch_add(&c->elsewhere, 1, __ATOMIC_RELAXED);
    return NULL;
}
static void test_copies_threads(void) {
    static PerfCopies c;
    char line[2048];
    unsigned long long total = 0;
    perf_copies_reset(&c);
    pthread_t t;
    if (pthread_create(&t, NULL, copies_elsewhere, &c)) {
        CHECK(0);
        return;
    }
    const PerfTarget a = {0x440e90000ull, 1920, 1080, 97};
    for (unsigned i = 0; i < 2000; i++) {
        perf_copy_begin(&c, PK_COMPOSE);
        perf_copy_pass_end(&c, &a, PE_COPY);
        perf_copy_bytes(&c, 1);
        if (i % 100 == 99) {
            perf_copies_line(&c, 1, line, sizeof line);
            const char* e = strstr(line, " elsewhere=");
            if (e) total += strtoull(e + 11, NULL, 10);
        }
    }
    CHECK(!pthread_join(t, NULL));
    perf_copies_line(&c, 1, line, sizeof line);
    const char* e = strstr(line, " elsewhere=");
    if (e) total += strtoull(e + 11, NULL, 10);
    CHECK(total == 100000); /* none lost to a window's reset */
}

int main(void) {
    test_rel_window();
    test_subs();
    test_subs_producer();
    test_subs_threads();
    CHECK(g_perf_submit_every_ns == 1000000); /* upstream's interval while SNX_ARCEUS_PERF_SUBMIT_US is unset */
    CHECK(g_perf_compute_submit == 1); /* deferred dispatches submit at once while the switch is unset */
    test_tail();
    test_tail_line();
    test_tail_threads();
    test_line();
    test_named_full();
    test_small_buffers();
    test_copies();
    test_copies_full();
    test_copies_line();
    test_copies_threads();
    CHECK(g_perf_cp.site == -1 && g_perf_cp.slot == -1 && g_perf_cp.draw == -1 && g_perf_cp.last == -1);
    if (failures) return 1;
    printf("perf_times: ok\n");
    return 0;
}
