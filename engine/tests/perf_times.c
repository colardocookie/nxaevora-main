/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* SNX_ARCEUS_PERF_TIMES's counting (gpu/perf_times.c): sums, largest values, the named waits, the counts and the
   line, which starts the next window empty. */
#include "nx/gpu/perf_times.h"
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

int main(void) {
    test_line();
    test_named_full();
    test_small_buffers();
    if (failures) return 1;
    printf("perf_times: ok\n");
    return 0;
}
