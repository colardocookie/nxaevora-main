/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* --script's presses against a clock and a present count the test sets: one at a time in ms is held press_len ms from
   then, one at present N ("Nf") from the first ask at or after that present, however late it comes, and both mix */
#define plat_ns test_ns
#include "nx/runtime/platform/unattended.c"
#include "common/utilities/common.h"
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static u64 g_now;
static jmp_buf g_exit_jump;
u64 test_ns(void) { return g_now; }
int g_frames;
void snx_save_before_exit(void) {}
void ini_poll(int final) { (void) final; }
void plat_exit_now(int code) { longjmp(g_exit_jump, code + 1); }
void logf_(int lvl, const char* fmt, ...) {
    va_list ap;
    (void)lvl;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

static unsigned checks, failures;
#define CHECK(c, ...)                                                                                                  \
    do {                                                                                                               \
        checks++;                                                                                                      \
        if (!(c)) {                                                                                                    \
            failures++;                                                                                                \
            printf("FAIL line %d: ", __LINE__);                                                                        \
            printf(__VA_ARGS__);                                                                                       \
            printf("\n");                                                                                              \
        }                                                                                                              \
    } while (0)

/* the buttons at ms after the start, with frames presented */
static u64 at(u64 ms, int frames) {
    g_now = 1000000000ull + ms * 1000000ull;
    g_frames = frames;
    return unattended_buttons();
}

/* a file's whole text, read from its start in text mode as stderr wrote it; a missing file gives an empty text */
static void read_text_file(const char* path, char* text, size_t size) {
    FILE* f = fopen(path, "r");
    size_t got = f ? fread(text, 1, size - 1, f) : 0;
    if (f) fclose(f);
    text[got] = 0;
}

/* unattended_exit with w and h: the stream's run done line and stderr's form */
static void check_run_exit(int w, int h, const char* stderr_wanted, const char* err_path) {
    FILE* stream = tmpfile();
    char text[512];
    int code;
    if (!stream || !freopen(err_path, "w", stderr)) {
        CHECK(0, "the run done test's files were not made");
    } else {
        snx_progress_test_attach(stream, SNX_PROGRESS_JSONL);
        snx_progress_stage(SNX_PROGRESS_STAGE_RUN);
        code = setjmp(g_exit_jump);
        if (!code) unattended_exit(0x0123456789abcdefull, w, h);
        CHECK(code == 1, "unattended_exit did not exit with 0");
        fflush(stream);
        if (fseek(stream, 0, SEEK_SET)) {
            CHECK(0, "the progress stream cannot be read");
        } else {
            size_t got = fread(text, 1, sizeof text - 1, stream);
            text[got] = 0;
            CHECK(!strcmp(text, "{\"v\":1,\"stage\":\"run\",\"event\":\"start\"}\n"
                               "{\"v\":1,\"stage\":\"run\",\"event\":\"done\"}\n"),
                  "the run done line is wrong: %s", text);
        }
        fflush(stderr);
        read_text_file(err_path, text, sizeof text);
        CHECK(!strcmp(text, stderr_wanted), "stderr's run end is wrong: %s", text);
    }
    snx_progress_test_attach(NULL, SNX_PROGRESS_OFF);
    if (stream) fclose(stream);
}

int main(void) {
    g_now = 1000000000ull;
    unattended_start();
    unattended_ini("script", "1000:1,5f:2,20f:400");
    CHECK(at(0, 0) == 0, "nothing at the start");
    CHECK(at(999, 4) == 0, "nothing before its time or present");
    CHECK(at(1000, 4) == 1, "A at 1000 ms: %llx", (unsigned long long)at(1000, 4));
    /* present 5 comes at 2000 ms: B is held from then, not from when the script was read */
    CHECK(at(2000, 5) == 2, "B from present 5: %llx", (unsigned long long)at(2000, 5));
    unattended_ini("script", "1000:1,5f:2,20f:400");
    CHECK(at(2299, 9) == 2, "B held 300 ms: %llx", (unsigned long long)at(2299, 9));
    CHECK(at(2300, 9) == 0, "B released after 300 ms: %llx", (unsigned long long)at(2300, 9));
    /* a present skipped over (frames go by two at a time) still presses */
    CHECK(at(5000, 21) == 0x400, "Plus once past present 20: %llx", (unsigned long long)at(5000, 21));
    CHECK(at(9000, 400) == 0, "each press once: %llx", (unsigned long long)at(9000, 400));
    CHECK(unattended_ms() == 9000, "the clock --script counts from: %llu", (unsigned long long)unattended_ms());
    unattended_ini("script", "1000:1,5f:2,20f:400");
    CHECK(at(9001, 401) == 0, "reloading unchanged script does not replay completed presses");
    unattended_ini("script", "402f:1");
    CHECK(at(9010, 402) == 1, "changed script starts its new press");
    CHECK(at(9310, 403) == 0, "new press ends normally");

    /* the help's example of a present is one the parser takes as a present */
    {
        const IniOpt* o = ini_find(g_unattended_ini, "script");
        CHECK(o && o->help && strstr(o->help, "(300f)"), "script's help gives no 300f: %s", o && o->help ? o->help : "");
        unattended_ini("script", "300f:1");
        CHECK(at(9020, 299) == 0 && at(9030, 300) == 1, "300f is no present's number");
    }
    /* a malformed item ends the script there, the items before it kept */
    unattended_ini("script", "500f:8,oops,600f:10");
    CHECK(at(9500, 500) == 8, "the item before a malformed one still presses");
    CHECK(at(9900, 600) == 0, "an item after a malformed one pressed");
    CHECK(unattended_known("shot_every") && !unattended_known("touch"), "unattended_known");

    /* the limits, and numbered shots by bucket even when frames go by two */
    CHECK(!unattended_active(), "active without seconds or frames");
    unattended_ini("frames", "700");
    unattended_ini("shot", "out/run");
    unattended_ini("shot_every", "10");
    CHECK(unattended_active() && !unattended_done(), "frames=700 not active, or done too early");
    {
        char path[64];
        g_frames = 605;
        CHECK(unattended_periodic_shot(path, sizeof path) && !strcmp(path, "out/run_0000"), "first numbered shot");
        g_frames = 609;
        CHECK(!unattended_periodic_shot(path, sizeof path), "the same bucket shot twice");
        g_frames = 611; /* 610 went by between two looks */
        CHECK(unattended_periodic_shot(path, sizeof path) && !strcmp(path, "out/run_0001"), "a bucket skipped over");
        g_frames = 621;
        CHECK(unattended_periodic_shot(path, sizeof path) && !strcmp(path, "out/run_0002"), "third numbered shot");
    }
    g_frames = 700;
    CHECK(unattended_done(), "frames=700 not done at 700");
    unattended_ini("shot", "");
    CHECK(!unattended_shot(), "an empty shot does not clear it");

    /* the stick keys, and the clamp into the circle */
    {
        i32 st[4], x = 32767, y = 32767;
        input_key_sticks(1ull << 40 | 1ull << 41 | 1ull << 46 | 1ull << 44, st);
        CHECK(st[0] == -32767 && st[1] == 32767 && st[2] == 0 && st[3] == 0, "stick keys %d %d %d %d", st[0], st[1],
              st[2], st[3]);
        input_key_sticks(1ull << 47, st);
        CHECK(st[2] == 0 && st[3] == -32767, "right stick down %d %d", st[2], st[3]);
        input_stick_clamp(&x, &y);
        CHECK(x == y && x > 23000 && x < 23171 && (double)x * x + (double)y * y <= 32767.0 * 32767.0,
              "diagonal clamp %d %d", x, y);
        x = -40000;
        y = 0;
        input_stick_clamp(&x, &y);
        CHECK(x == -32767 && y == 0, "axis clamp %d %d", x, y);
    }

    /* last, as it starts the clock over: a press the backend looked at before the game started (during main.c's
       loading screen) is pressed again once unattended_start starts the game's clock, not used up before it */
    g_now = 1000000000ull + 20000ull * 1000000ull;
    unattended_start();
    unattended_ini("script", "703f:4");
    CHECK(at(20000, 703) == 4 && at(20400, 703) == 0, "703f pressed and ended before the start");
    g_now = 1000000000ull + 30000ull * 1000000ull;
    unattended_start();
    CHECK(at(30000, 703) == 4, "703f pressed again after unattended_start: %llx", (unsigned long long)at(30000, 703));

    /* unattended_exit: the stream's run done line and stderr's two forms */
    {
        char* dir = snx_temp_dir(NULL);
        char* err = dir ? snx_path_join(dir, "unattended-exit.stderr") : NULL;
        CHECK(dir && err, "the run done test's folder was not made");
        if (dir && err) {
            g_frames = 700;
            check_run_exit(800, 600, "\nframes=700 hash=0123456789abcdef 800x600\n", err);
            check_run_exit(0, 0, "\nframes=700 hash=0123456789abcdef\n", err);
        }
#ifdef _WIN32
        {
            FILE* sink = freopen("NUL", "w", stderr);
            (void)sink;
        }
#else
        {
            FILE* sink = freopen("/dev/null", "w", stderr);
            (void)sink;
        }
#endif
        if (dir) snx_remove_tree(dir);
        free(err);
        free(dir);
    }
    printf("%u checks, %u failed\n", checks, failures);
    return failures != 0;
}
