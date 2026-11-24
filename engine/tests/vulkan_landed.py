# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
"""PERF-25's landed_note through buffer.c's own code, copied out of the source as it is, against perf_times.c's table
and counts and small stand-ins for the rest: write tracking and GPU-side writes by page, the record as stamped by
byte, and the frames whose staging was reused. Each case is one lookup and the line's counts after it.

Run: python3 tests/vulkan_landed.py [buffer.c] [compiler and flags], for example
    python3 tests/vulkan_landed.py src/nx/gpu/vk/buffer.c clang -std=gnu11 -O2 -fsanitize=address,undefined
"""
import pathlib
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
SRC = HERE.parent / "src"


def function(text, signature):
    """The definition starting at signature, through its closing brace"""
    start = text.index(signature)
    at = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[at] == "{") - (text[at] == "}")
        at += 1
    return text[start:at] + "\n"


HEAD = r'''
#include "nx/gpu/perf_times.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef uint64_t u64;
typedef uint32_t u32;
static u64 g_frame = 10, g_submit = 100, g_gen = 1, g_freed_frame = ~0ull, g_freed_at;
static u32 g_clock = 1;
static int g_tracking = 1;
static u32 g_pagew[64];  /* the write clock each page was written at */
static u64 g_pageg[64];  /* the GPU-side write generation of each page */
static u64 g_byte_a, g_byte_n, g_byte_gen; /* the one GPU-side write kept by byte */
#define BASE 0x40000ull
static u64 vk_frame_id(void) { return g_frame; }
static u64 vk_submit_number(void) { return g_submit; }
static int vk_frame_freed(u64 frame, u64* at) { return frame == g_freed_frame ? (*at = g_freed_at, 1) : 0; }
static int gpu_mem_dirty_since(u64 gw, u64 a, u64 n) {
    for (u64 p = (a - BASE) >> 12; p <= (a - BASE + n - 1) >> 12; p++)
        if (g_pageg[p] > gw) return 1;
    return 0;
}
static int gpu_mem_written_ask(u64 since, int real, u64 a, u64 n, int* outside, unsigned* kinds) {
    (void)real, *outside = 0, *kinds = 0;
    return g_byte_gen > since && g_byte_a < a + n && a < g_byte_a + g_byte_n ? PSA_IN : PSA_NONE;
}
static int written_since_gva(u64 a, u64 n, u32 since) {
    if (!g_tracking) return -1;
    for (u64 p = (a - BASE) >> 12; p <= (a - BASE + n - 1) >> 12; p++)
        if (g_pagew[p] > since) return 1;
    return 0;
}
static void vk_perf_landed(int made, int kind, int age, int freed, int clear, unsigned wrote, u64 ns, u64 ends,
                           int slot) {
    perf_copy_landed(&g_perf_cp, made, kind, age, freed, clear, wrote, ns, ends, slot);
}
static void vk_perf_landed_kept(u64 bytes) { perf_copy_landed_kept(&g_perf_cp, bytes); }
'''

TESTS = r'''
static void land(u64 a, u64 n) { /* a result lands now: its stamp, the tracking's clock after */
    g_gen++;
    for (u64 p = (a - BASE) >> 12; p <= (a - BASE + n - 1) >> 12; p++) g_pageg[p] = g_gen;
    perf_landed_add(g_perf_landed, a, n, g_frame, g_frame, g_submit, g_gen, g_clock);
}
static char line[PERF_COPIES_LINE];
static const char* look(int made, u64 a, u64 n) {
    perf_copies_reset(&g_perf_cp);
    landed_note(made, a, n, 1000000, made, -1);
    perf_landed_line(&g_perf_cp, 1, line, sizeof line);
    return line;
}
static void reset(void) {
    memset(g_perf_landed, 0, sizeof g_perf_landed);
    memset(g_pagew, 0, sizeof g_pagew);
    memset(g_pageg, 0, sizeof g_pageg);
    g_byte_gen = 0, g_freed_frame = ~0ull, g_tracking = 1;
}
int main(void) {
    /* nothing since: cleared by page tests, the buffer alive, the age in submissions */
    reset();
    land(BASE, 0x2000);
    g_submit += 2;
    CHECK_LINE(look(0, BASE, 0x2000), " staged=1.0/0.0/0.0 age=0.0/0.0/1.0/0.0 buffer=1.0/0.0/0.0/0.0/0.0 "
               "cleared=1.0/0.0/0.0 wrote=0.0/0.0/0.0/0.0 ms=1.000 ends=0.0 kept_KB=0.0");
    /* a GPU-side write beside it on its page: cleared only with the byte record */
    g_pageg[1] = ++g_gen, g_byte_a = BASE + 0x1f00, g_byte_n = 0x100, g_byte_gen = g_gen;
    CHECK_LINE(look(1, BASE, 0x1f00), " own_made=0.0/1.0/0.0 age=0.0/0.0/1.0/0.0 buffer=1.0/0.0/0.0/0.0/0.0 "
               "cleared=0.0/1.0/0.0 wrote=0.0/1.0/0.0/0.0 ms=1.000 ends=1.0");
    /* the same write over a byte of the range: not cleared */
    CHECK_LINE(look(1, BASE, 0x2000), " cleared=0.0/0.0/1.0 wrote=0.0/1.0/1.0/0.0 ");
    /* a CPU write on its page after the landing: not cleared, and so without tracking */
    reset();
    land(BASE, 0x2000);
    g_pagew[0] = ++g_clock;
    CHECK_LINE(look(0, BASE + 0x100, 0x100), " staged=0.0/1.0/0.0 age=1.0/0.0/0.0/0.0 ");
    CHECK_LINE(line, " cleared=0.0/0.0/1.0 wrote=0.0/0.0/0.0/1.0 ");
    g_pagew[0] = 0, g_tracking = 0;
    CHECK_LINE(look(0, BASE + 0x100, 0x100), " cleared=0.0/0.0/1.0 wrote=0.0/0.0/0.0/1.0 ");
    /* a CPU write before the landing (its clock) is not one after it */
    reset();
    g_pagew[0] = g_clock;
    land(BASE, 0x2000);
    CHECK_LINE(look(0, BASE, 0x100), " cleared=1.0/0.0/0.0 ");
    /* a later result over a byte: written, also by page and byte */
    reset();
    land(BASE, 0x2000);
    g_submit++;
    land(BASE + 0x1000, 0x100);
    g_byte_a = BASE + 0x1000, g_byte_n = 0x100, g_byte_gen = g_gen;
    CHECK_LINE(look(0, BASE + 0xf00, 0x200), " staged=0.0/1.0/0.0 age=0.0/1.0/0.0/0.0 "); /* a part of the first */
    CHECK_LINE(line, " cleared=0.0/0.0/1.0 wrote=1.0/1.0/1.0/0.0 ");
    g_byte_gen = 0; /* the later result missing from the record by byte (it lost it): still not cleared */
    CHECK_LINE(look(0, BASE + 0xf00, 0x200), " cleared=0.0/0.0/1.0 wrote=1.0/1.0/0.0/0.0 ");
    /* its buffer freed 2 submissions before the lookup: the bucket, and its bytes kept alive once */
    reset();
    land(BASE, 0x2000);
    g_freed_frame = g_frame, g_freed_at = g_submit + 1, g_submit += 3;
    CHECK_LINE(look(1, BASE, 0x2000), " buffer=0.0/0.0/0.0/1.0/0.0 "); /* alive, freed 0, 1, 2-3: here */
    CHECK_LINE(line, " kept_KB=8.0");
    CHECK_LINE(look(1, BASE, 0x2000), " kept_KB=0.0");
    /* two results: each part from its own landing, the worse of the two */
    reset();
    land(BASE, 0x1000);
    g_submit += 4;
    land(BASE + 0x1000, 0x1000);
    g_pagew[0] = ++g_clock; /* only the first one's page written after it */
    CHECK_LINE(look(0, BASE + 0x800, 0x1000), " staged=0.0/0.0/1.0 age=0.0/0.0/0.0/1.0 ");
    CHECK_LINE(line, " cleared=0.0/0.0/1.0 wrote=0.0/0.0/0.0/1.0 ");
    g_pagew[0] = 0, g_pagew[1] = ++g_clock; /* only the second one's page written after it */
    CHECK_LINE(look(0, BASE + 0x800, 0x1000), " cleared=0.0/0.0/1.0 wrote=0.0/0.0/0.0/1.0 ");
    g_pagew[1] = 0;
    CHECK_LINE(look(0, BASE + 0x800, 0x1000), " cleared=1.0/0.0/0.0 ");
    /* outside every result, or landed two frames before: not counted */
    CHECK_LINE(look(0, BASE + 0x3000, 0x100), " kept_KB=0.0");
    assert(!strstr(line, " staged="));
    g_frame += 2;
    CHECK_LINE(look(0, BASE, 0x100), " kept_KB=0.0");
    assert(!strstr(line, " staged="));
    printf("vulkan_landed: all checks passed\n");
    return 0;
}
'''


def main():
    args = sys.argv[1:]
    path = pathlib.Path(args.pop(0)) if args and args[0].endswith(".c") else SRC / "nx/gpu/vk/buffer.c"
    cc = args or ["cc", "-std=gnu11", "-O1"]
    text = path.read_text()
    check = r'''
#define CHECK_LINE(l, want) do { const char* l_ = (l); if (!strstr(l_, want)) { \
    fprintf(stderr, "%s:%d: missing \"%s\" in %s\n", __FILE__, __LINE__, want, l_); return 1; } } while (0)
'''
    body = HEAD + check + function(text, "static void landed_note(int made, u64 gva") + TESTS
    with tempfile.TemporaryDirectory() as d:
        c = pathlib.Path(d) / "landed.c"
        c.write_text(body)
        exe = pathlib.Path(d) / "landed"
        subprocess.run(cc + ["-Wall", "-Wno-unused-function", "-I", str(SRC), str(c), str(SRC / "nx/gpu/perf_times.c"),
                             "-o", str(exe)], check=True)
        sys.exit(subprocess.run([str(exe)]).returncode)


if __name__ == "__main__":
    main()
