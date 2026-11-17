/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* SNX_ARCEUS_PERF_TIMES's counting (gpu/perf_times.c): sums, largest values, the named waits, the counts and the
   line, which starts the next window empty. */
#include "nx/gpu/perf_times.h"
#include <pthread.h>
#include <stdio.h>
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

int main(void) {
    test_tail();
    test_tail_line();
    test_tail_threads();
    test_line();
    test_named_full();
    test_small_buffers();
    if (failures) return 1;
    printf("perf_times: ok\n");
    return 0;
}
