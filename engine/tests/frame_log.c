/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* DAILY-LT's frame log and latency probe (gpu/frame_log.c): the intervals and their lines, the buffer handed over and
   the one dropped meanwhile, the press, the presents that get a line around it, the posted presents in order and the
   picture difference. */
#include "nx/gpu/frame_log.h"
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

static void test_frame_log(void) {
    static FrameLog f;
    char line[2048];
    memset(&f, 0, sizeof f);
    uint64_t t = 5000000000ull, no = 1;
    frame_log_present(&f, no++, t); /* the first present has no interval */
    for (unsigned i = 0; i < FRAME_LOG_WINDOW - 1; i++) frame_log_present(&f, no++, t += 8333999);
    CHECK(frame_log_ready(&f) == 0);
    frame_log_present(&f, no++, t += 25000000);
    CHECK(frame_log_ready(&f) == 1);
    frame_log_line(&f, 0, 0, line, sizeof line);
    CHECK(!strncmp(line, "arceus-frame-log: presents 2-151 us: 8333 8333 ", 47));
    frame_log_line(&f, 0, 3, line, sizeof line);
    CHECK(!strncmp(line, "arceus-frame-log: presents 452-601 us: 8333 ", 44));
    CHECK(strlen(line) > 6 && !strcmp(line + strlen(line) - 6, " 25000")); /* the last interval, no lost= */
    unsigned words = 0;
    for (const char* s = line; (s = strchr(s, ' ')); s++) words++;
    CHECK(words == 3 + FRAME_LOG_PART); /* the three of the head, then one before each interval */
    /* the next buffer fills while this one is out: it is dropped, and the one after takes its place */
    for (unsigned i = 0; i < FRAME_LOG_WINDOW; i++) frame_log_present(&f, no++, t += 16666000);
    CHECK(frame_log_ready(&f) == 1 && f.lost == 1 && f.cur == 1);
    frame_log_done(&f);
    for (unsigned i = 0; i < FRAME_LOG_WINDOW; i++) frame_log_present(&f, no++, t += 1000);
    CHECK(frame_log_ready(&f) == 2);
    frame_log_line(&f, 1, 0, line, sizeof line);
    CHECK(!strncmp(line, "arceus-frame-log: presents 1202-1351 us: 1 1 ", 45) && !strstr(line, "lost="));
    frame_log_line(&f, 1, 3, line, sizeof line);
    CHECK(strstr(line, " 1 lost=1") && line[strlen(line) - 1] == '1');
    frame_log_done(&f);
    /* a pause past the 32-bit microseconds keeps the largest value */
    for (unsigned i = 0; i < FRAME_LOG_WINDOW; i++) frame_log_present(&f, no++, t += i ? 1000 : 5000000000000ull);
    CHECK(frame_log_ready(&f) == 1);
    frame_log_line(&f, 0, 0, line, sizeof line);
    CHECK(strstr(line, "us: 4294967295 1 1 "));
    CHECK(frame_log_line(&f, 0, 0, line, 10) == 9 && strlen(line) == 9); /* a small buffer stays terminated */
}

static void test_input(void) {
    static Latency l;
    memset(&l, 0, sizeof l);
    CHECK(!latency_input(&l, 0, 100) && l.t0 == 0);
    CHECK(latency_input(&l, 1ull << 46, 200) && l.t0 == 200);
    CHECK(!latency_input(&l, 1ull << 46, 300) && l.t0 == 200); /* held */
    CHECK(latency_input(&l, 1ull << 46 | 1, 400) && l.t0 == 400); /* another one with it */
    CHECK(!latency_input(&l, 0, 500) && l.t0 == 400);             /* let go */
    CHECK(latency_input(&l, 1ull << 46, 600) && l.t0 == 600);
}

static char lines[512][192];
static int nlines;
static void put(const char* line) {
    if (nlines < 512) snprintf(lines[nlines], sizeof lines[0], "%s", line);
    nlines++;
}
static unsigned long long line_no(int i) { return strtoull(lines[i] + strlen("arceus-latency: present "), NULL, 10); }
static LatPresent at(uint64_t no) {
    return (LatPresent){.no = no, .post_ns = no * 8000000, .acquire_ns = 2500, .jobs = 1, .shown = 1, .diff = 1.5};
}

static void test_result(void) {
    static Latency l;
    memset(&l, 0, sizeof l);
    nlines = 0;
    uint64_t no = 1;
    for (; no <= 100; no++) {
        const LatPresent p = at(no);
        latency_result(&l, &p, put);
    }
    CHECK(nlines == 0); /* no press yet */
    l.t0 = 100 * 8000000 + 4000000; /* between presents 100 and 101 */
    for (; no <= 200; no++) {
        const LatPresent p = at(no);
        latency_result(&l, &p, put);
    }
    CHECK(nlines == (int)(LAT_BEFORE + LAT_AFTER));
    CHECK(line_no(0) == 71 && line_no(LAT_BEFORE - 1) == 100 && line_no(LAT_BEFORE) == 101);
    CHECK(line_no(nlines - 1) == 190);
    CHECK(!strcmp(lines[0], "arceus-latency: present 71 at_us=568000 diff=1.50 jobs=1 acquire_us=2 shown=1"));
    /* a press inside the window: its own LAT_AFTER from it on, no present twice */
    memset(&l, 0, sizeof l);
    nlines = 0;
    for (no = 1; no <= 300; no++) {
        if (no == 101) l.t0 = 100 * 8000000 + 1;
        if (no == 151) l.t0 = 150 * 8000000 + 1;
        LatPresent p = at(no);
        if (no == 1) p.diff = -1;
        latency_result(&l, &p, put);
    }
    CHECK(nlines == (int)(LAT_BEFORE + 50 + LAT_AFTER));
    int ordered = 1;
    for (int i = 1; i < nlines && i < 512; i++) ordered &= line_no(i) == line_no(i - 1) + 1;
    CHECK(ordered && line_no(0) == 71 && line_no(nlines - 1) == 240);
    /* presents processed before the press was seen, posted after it, count among those from it on */
    memset(&l, 0, sizeof l);
    nlines = 0;
    for (no = 1; no <= 300; no++) {
        if (no == 103) l.t0 = 100 * 8000000 + 1;
        const LatPresent p = at(no);
        latency_result(&l, &p, put);
    }
    CHECK(nlines == (int)(LAT_BEFORE - 2 + LAT_AFTER));
    CHECK(line_no(0) == 73 && line_no(LAT_BEFORE - 3) == 100 && line_no(nlines - 1) == 190);
    /* no picture difference prints as - */
    memset(&l, 0, sizeof l);
    nlines = 0;
    l.t0 = 1;
    LatPresent p = at(5);
    p.diff = -1;
    latency_result(&l, &p, put);
    CHECK(nlines == 1 && strstr(lines[0], " diff=- "));
}

static void test_posted(void) {
    static Latency l;
    memset(&l, 0, sizeof l);
    LatPresent out;
    CHECK(!latency_take(&l, &out));
    for (uint64_t no = 1; no <= LAT_POSTED + 2; no++) {
        const LatPresent p = at(no);
        latency_posted(&l, &p);
    }
    CHECK(l.post_lost == 2);
    CHECK(latency_take(&l, &out) && out.no == 1);
    const LatPresent p = at(99);
    latency_posted(&l, &p);
    uint64_t want = 2;
    int ordered = 1;
    while (latency_take(&l, &out)) ordered &= out.no == (want == LAT_POSTED + 1 ? 99 : want), want++;
    CHECK(ordered && want == LAT_POSTED + 2);
}

static void test_diff(void) {
    uint8_t a[8] = {10, 20, 30, 0, 0, 0, 0, 0}, b[8] = {10, 20, 30, 255, 255, 0, 0, 7};
    CHECK(latency_picture_diff(a, a, 2) == 0);
    CHECK(latency_picture_diff(a, b, 2) == 255.0 / 6); /* alpha left out */
    CHECK(latency_picture_diff(b, a, 2) == 255.0 / 6);
    CHECK(latency_picture_diff(a, b, 0) == 0);
}

int main(void) {
    test_frame_log();
    test_input();
    test_result();
    test_posted();
    test_diff();
    if (failures) return 1;
    printf("frame_log: all checks passed\n");
    return 0;
}
