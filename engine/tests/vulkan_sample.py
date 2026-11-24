# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
"""SNX_ARCEUS_PERF_SAMPLE (PERF-24) and SNX_ARCEUS_PATCH_HASH through buffer.c's own buf_get, Vb and patches
(patch_own, patch_apply, patch_dirty, written_runs), copied out of the source as they are, run against small
stand-ins for the rest: guest memory, write tracking and GPU-side writes by page, the staging ring and an own buffer
whose bytes are what a draw reads (no Vulkan).

With the switch at 0 the sample is taken at every full check once an entry has been the same 63 times, and the
check without write tracking reads it. With the switch and write tracking no sample is taken. Tracking lost after a
skip, through buf_get or another caller of vk_written_since, sends the next check to the full hash, which samples
again. Contents A, B, A with tracking lost after B take the full check and draw A. At 256 KB or more, B patched in
place (patch_own, or patch_dirty after a GPU-side write) leaves e->hash as A's, so A afterwards is drawn as B: the
defect astra found (23:15). With PATCH_HASH the next full check takes a patched entry as changed, and the three
checks without write tracking pass it by.

Run: python3 tests/vulkan_sample.py [buffer.c] [compiler and flags], for example
    python3 tests/vulkan_sample.py src/nx/gpu/vk/buffer.c clang -std=gnu11 -O2 -fsanitize=address,undefined
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


def between(text, first, last):
    start = text.index(first)
    return text[start:text.index(last, start) + len(last)] + "\n"


HEAD = r'''
#include "nx/gpu/perf_times.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint64_t u64;
typedef uint32_t u32;
typedef int32_t i32;
typedef uint8_t u8;
typedef int VkBuffer;
typedef void* VmaAllocation;
typedef struct { u64 srcOffset, dstOffset, size; } VkBufferCopy;
#define VK_NULL_HANDLE 0
#define VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT 1
#define VK_PIPELINE_STAGE_2_COPY_BIT 2
#define VK_ACCESS_2_TRANSFER_WRITE_BIT 1
#define VK_ACCESS_2_MEMORY_READ_BIT 2
#define ST_NEW 1
#define ST_NOBUF 2
#define ST_SUBMIT 3
#define ST_DIRTY 4
#define ST_WRITTEN 5
#define ST_EPOCH 6
#define PB_INPUT 0
#define LEN_MAX (256u << 10)
int g_perf_times, g_perf_sample, g_patch_hash;
static int g_ww = 1; /* as buffer.c's: -1 not tried, 0 not available, 1 on */
static u32 g_vk_epoch = 100, g_wclock = 1, g_lose;
static u32 g_pagew[LEN_MAX >> 12];      /* the write clock each page was last written at */
static u64 g_gpu_gen, g_pageg[LEN_MAX >> 12]; /* GPU-side writes: their generation, and each page's */
static u64 g_submit = 7, g_uploads, g_upload_bytes;
static unsigned g_stage_fail; /* the next this many vk_stage_upload calls fail */
static u8 g_guest[LEN_MAX], g_stage[LEN_MAX], g_own[LEN_MAX];
static u8* g_mem = g_guest;
static unsigned g_samples, g_hashes, g_patches, g_full;
#define GVA 0x10000ull
'''

STUBS = r'''
static Vb g_vb[2];
static struct { u32 idx; } g_vbi_one[1], *g_vbi = g_vbi_one;
static u64 plat_ns(void) { return 0; }
static int own_why(int why) { return why; }
static u32 vb_slot(u64 gva, u32 len) { return (void)gva, (void)len, 0; }
static u32 vb_new(u64 start, u32 len) {
    memset(&g_vb[1], 0, sizeof g_vb[1]);
    g_vb[1].start = start, g_vb[1].len = len;
    g_vbi[0].idx = 1;
    return 1;
}
static u64 vk_compute_adds(void) { return 0; }
static u64 vk_buf_adds(void) { return 0; }
static int gfx_gpu_src(u64 a, u64 n, VkBuffer* b, u64* o, int w, u64 al, int k) {
    return (void)a, (void)n, (void)b, (void)o, (void)w, (void)al, (void)k, 0;
}
static int gfx_composed(u64 a, u64 n, VkBuffer* b, u64* o) { return (void)a, (void)n, (void)b, (void)o, assert(0), 0; }
static int vk_compute_gpu_src(u64 a, u64 n, VkBuffer* b, u64* o) { return (void)a, (void)n, (void)b, (void)o, 0; }
static void vk_compute_mem_need(u64 a, u64 n) { (void)a, (void)n; }
static int g_cs_any;
static u64 vk_submit_number(void) { return g_submit; }
static int gpu_mem_dirty_since(u64 gw, u64 a, u64 n) {
    for (u64 pg = (a - GVA) >> 12; pg <= (a - GVA + n - 1) >> 12; pg++)
        if (g_pageg[pg] > gw) return 1;
    return 0;
}
static u64 gpu_mem_generation(void) { return g_gpu_gen; }
static const u8* entry_host(Vb* e, u64 gva, u64 len) { return (void)e, (void)len, g_guest + (gva - GVA); }
/* write tracking: a write the test made after since, or -1 once tracking is lost, as vk_written_since gives it */
static int vk_written_since(const u8* host, u64 len, u32 since) {
    if (!g_ww) return -1;
    if (g_lose) {
        g_ww = 0; /* as its failure paths do */
        return -1;
    }
    for (u64 pg = (u64)(host - g_mem) >> 12; pg <= (u64)(host - g_mem + len - 1) >> 12; pg++)
        if (g_pagew[pg] > since) return 1;
    return 0;
}
static int page_written(u64 pg, u32 since) { return g_pagew[pg] > since; }
static int written_since_gva(u64 gva, u64 len, u32 since) { return (void)gva, (void)len, (void)since, assert(0), -1; }
static u32 vk_ww_now(void) { return g_wclock; }
/* a patch's copy from staging into the own buffer */
static int vk_cmd(void) { return 1; }
static void rec_CmdCopyBuffer(int cb, VkBuffer src, VkBuffer dst, u32 n, const VkBufferCopy* r) {
    (void)cb, (void)src;
    assert(dst == 77);
    for (u32 k = 0; k < n; k++) memcpy(g_own + r[k].dstOffset, g_stage + r[k].srcOffset, r[k].size);
    g_patches++;
}
static void copy_barrier(int a, int b, int c, int d) { (void)a, (void)b, (void)c, (void)d; }
static void vk_render_end(void) {}
static void vk_perf_copy(int k) { (void)k; }
static void vk_perf_copy_bytes(u64 n) { (void)n; }
static u64 g_copy_owned;
static void own_note(int what, int why, u64 gva, u64 len, const u64* ns) {
    (void)what, (void)why, (void)gva, (void)len, (void)ns;
}
static void vk_perf_steps(int what, const u64* st, const u8* ran) { (void)what, (void)st, (void)ran; }
static void vk_perf_quiet(u64 ns) { (void)ns; }
static void vk_perf_sample_skip(void) {}
PerfCopies g_perf_cp;
static void landed_note(int made, u64 gva, u64 len, u64 ns, u64 ends, int slot) {
    (void)made, (void)gva, (void)len, (void)ns, (void)ends, (void)slot;
}
static void vk_perf_gsame(int real, int q, unsigned k) { (void)real, (void)q, (void)k; }
static void vk_perf_cleared(unsigned c) { (void)c; }
static void vk_perf_csame(int a, int b, u64 c, u64 d) { (void)a, (void)b, (void)c, (void)d; }
static void vk_perf_probe(u64 ns) { (void)ns; }
static int gpu_mem_written_ask(u64 s, int r, u64 a, u64 n, int* o, unsigned* k) {
    return (void)s, (void)r, (void)a, (void)n, *o = 0, *k = 0, 0;
}
static u64 written_pages_note(u64 a, u64 n, u32 s) { return (void)a, (void)n, (void)s, 0; }
static u64 perf_step(u64* st, u8* ran, int k, u64 since) { return (void)st, (void)ran, (void)k, since; }
static int copy_stats_on(void) { return 0; }
static u64 g_copy_stage;
static void stage_note(int why, int changed, u64 gva, u64 len) { (void)why, (void)changed, (void)gva, (void)len; }
static int range_owned(u64 a, u64 n) { return (void)a, (void)n, 0; }
static int buf_get_owned(u32 i, Vb* e, u64 a, u64 n, int s, VkBuffer* b, u64* o) {
    return (void)i, (void)e, (void)a, (void)n, (void)s, (void)b, (void)o, assert(0), 0;
}
static const u8* guest_bytes(u64 gva, u64 len) { return (void)len, g_guest + (gva - GVA); }
static u64 vk_stage_align(void) { return 4; }
static u8* vk_stage_upload(u64 len, u64 al, VkBuffer* sb, u64* so) {
    (void)al;
    if (g_stage_fail) return g_stage_fail--, NULL;
    g_uploads++, g_upload_bytes += len;
    return *sb = 50, *so = 0, g_stage;
}
static u64 fnv(const u8* p, u64 n) {
    u64 h = 1469598103934665603ull;
    for (u64 k = 0; k < n; k++) h = (h ^ p[k]) * 1099511628211ull;
    return h;
}
static u64 vk_hash(const void* p, u64 n) { return g_hashes++, fnv(p, n); }
static u64 vk_hash_copy(void* d, const void* s, u64 n) { return memcpy(d, s, n), vk_hash(s, n); }
/* 64 pieces of the range, as cmd.c samples it */
static u64 vk_sample_hash(u64 gva, u64 len) {
    const u8* p = g_guest + (gva - GVA);
    u64 h = fnv(p, 64) ^ fnv(p + len - 64, 64), step = (len - 32) / 62;
    for (int k = 0; k < 62; k++) h = h * 31 + fnv(p + k * step, 32);
    return g_samples++, h;
}
static void drop_own(u32 i) { g_vb[i].own = 0, g_vb[i].buf = 0; }
/* the staged bytes become the entry's own buffer */
static int make_own(u32 i, VkBuffer sb, u64 so) {
    (void)sb, (void)so;
    memcpy(g_own, g_stage, g_vb[i].len);
    g_vb[i].own = 1, g_vb[i].buf = 77, g_vb[i].off = 0;
    return 1;
}
static void use(u32 i, VkBuffer* buf, u64* off) { *buf = g_vb[i].buf, *off = g_vb[i].off; }
'''

TESTS = r'''
/* one submit later, one read of the range; the bytes the draw reads */
static const u8* draw(u64 len, u32 gap) {
    VkBuffer b = 0;
    u64 off = 0;
    g_vk_epoch += gap;
    g_submit++;
    const unsigned hashes = g_hashes;
    assert(buf_get(GVA, len, &b, &off, 0, 4));
    g_full += g_hashes != hashes;
    assert(b == 77 || b == 50);
    return b == 77 ? g_own : g_stage;
}
static void touch(u64 at, u64 len) {
    ++g_wclock;
    for (u64 pg = at >> 12; pg <= (at + len - 1) >> 12; pg++) g_pagew[pg] = g_wclock;
}
static void fill(u64 len, u8 v) {
    memset(g_guest, v, len);
    g_guest[len / 3] = (u8)(v + 1); /* not all one byte */
    touch(0, len);
}
static void reset(int sample, int ww) {
    memset(g_vb, 0, sizeof g_vb);
    g_vbi[0].idx = 0;
    g_perf_sample = sample, g_ww = ww, g_lose = 0, g_stage_fail = 0;
    g_samples = g_hashes = g_patches = g_full = 0;
}
/* an entry the same for n full checks: each draw after a rewrite of the same bytes (a written check finding them) */
static void settle(u64 len, u8 v, unsigned n) {
    fill(len, v);
    for (unsigned k = 0; k < n; k++) {
        touch(0, len);
        assert(!memcmp(draw(len, 1), g_guest, len));
    }
}
int main(void) {
    const Vb* e = &g_vb[1];
    /* at 0, write tracking on: the first check makes the entry, 62 more bring it to 63, and from there every full
       check takes the sample (today's sequence) */
    reset(0, 1);
    settle(16u << 10, 3, 100);
    assert(g_full == 100 && e->stable == 99 && g_samples == 100 - 63 && !e->sskip);
    /* at 0, no write tracking: the same sequence of samples, then the check without tracking reads it at 64 and on */
    reset(0, 0);
    fill(16u << 10, 3);
    for (unsigned k = 0; k < 65; k++) (void)draw(16u << 10, 9);
    assert(e->stable == 64 && g_samples == 2 && !e->sskip && g_full == 65);
    const unsigned full = g_full, samples = g_samples;
    for (unsigned k = 0; k < 8; k++) assert(!memcmp(draw(16u << 10, 5), g_guest, 16u << 10)); /* 40 < 64 epochs */
    assert(g_full == full && g_samples == samples + 8); /* each read the sample, none hashed the range */
    /* the switch and write tracking: no sample at any full check, the flag set */
    reset(1, 1);
    settle(16u << 10, 3, 100);
    assert(g_full == 100 && e->stable == 99 && g_samples == 0 && e->sskip);
    /* the switch with tracking not tried yet (g_ww -1, kept so by this stand-in): the sample is taken, as without it */
    reset(1, -1);
    settle(16u << 10, 3, 100);
    assert(g_samples == 100 - 63 && !e->sskip);
    reset(1, 1);
    settle(16u << 10, 3, 100);
    /* tracking lost after a skip, through another caller of vk_written_since: the next check is a full one (the flag
       keeps it from the sample shortcut), samples again and clears the flag; the one after reads that sample */
    g_lose = 1;
    (void)vk_written_since(g_guest, 64, 0);
    assert(!g_ww);
    unsigned f = g_full;
    assert(!memcmp(draw(16u << 10, 9), g_guest, 16u << 10));
    assert(g_full == f + 1 && g_samples == 1 && !e->sskip);
    assert(!memcmp(draw(16u << 10, 9), g_guest, 16u << 10) && g_full == f + 1 && g_samples == 2);
    /* the same, tracking lost inside buf_get's own written check */
    reset(1, 1);
    settle(16u << 10, 3, 100);
    g_lose = 1;
    f = g_full;
    assert(!memcmp(draw(16u << 10, 9), g_guest, 16u << 10) && g_full == f + 1 && !e->sskip && !g_ww);
    /* A, B, A below 256 KB: A sampled (the switch off then), B with tracking on and the switch set (samples skipped),
       tracking lost, A again: the old sample of A matches, so only the flag sends it to the full check. A is drawn */
    reset(0, 1);
    settle(64u << 10, 0xa0, 70);
    g_perf_sample = 1;
    settle(64u << 10, 0xb0, 70);
    assert(e->sskip && e->stable >= 64);
    g_lose = 1;
    (void)vk_written_since(g_guest, 64, 0);
    fill(64u << 10, 0xa0);
    f = g_full;
    const u8* drawn = draw(64u << 10, 9);
    assert(g_full == f + 1 && !memcmp(drawn, g_guest, 64u << 10));
    /* A, B, A at 256 KB: B lands by patch_own, which leaves e->hash as A's. Tracking lost, A again: the full check
       finds A's hash, keeps the own buffer and draws B. The defect astra found, today and with PERF-24: recorded as it
       is, until PATCH-HASH */
    for (int sample = 0; sample < 2; sample++) {
        reset(sample, 1);
        settle(256u << 10, 0xa0, 70);
        fill(256u << 10, 0xb0);
        const unsigned patches = g_patches;
        assert(!memcmp(draw(256u << 10, 1), g_guest, 256u << 10) && g_patches == patches + 1);
        g_lose = 1;
        (void)vk_written_since(g_guest, 64, 0);
        fill(256u << 10, 0xa0);
        f = g_full;
        drawn = draw(256u << 10, 9);
        assert(g_full == f + 1 && drawn[0] == 0xb0); /* KNOWN DEFECT: the guest holds A */
    }
    /* PATCH-HASH (research 22:48, astra 23:35): each case unset as today (B drawn), then set (A drawn) */
    const u64 big = 256u << 10;
    for (int set = 0; set < 2; set++) {
        /* (a) the full check: B patched in by patch_own, tracking lost, A again */
        reset(0, 1);
        g_patch_hash = set;
        settle(big, 0xa0, 70);
        assert(e->stable < 16); /* every check of it was a patch: no shortcut without tracking can take it */
        fill(big, 0xb0);
        assert(!memcmp(draw(big, 1), g_guest, big) && e->patched == set);
        g_lose = 1;
        (void)vk_written_since(g_guest, 64, 0);
        fill(big, 0xa0);
        f = g_full;
        drawn = draw(big, 9);
        assert(g_full == f + 1 && drawn[0] == (set ? 0xa0 : 0xb0) && !e->patched);
        /* (b) the checks without tracking: an entry the same 70 full checks (each patch failing on staging, so the
           check runs) with A sampled, B patched in, tracking lost, A again. At 2 submits after the full check the
           first shortcut, at 10 the sampled one (PERF_SAMPLE 0), and with PERF_SAMPLE 1 the full check */
        for (int sample = 0; sample < 2; sample++)
            for (int gap = 0; gap < 2; gap++) {
                reset(sample, 1);
                g_patch_hash = set;
                fill(big, 0xa0);
                (void)draw(big, 1);
                for (unsigned k = 0; k < 70; k++) {
                    touch(0, 4096);
                    g_stage_fail = 1;
                    (void)draw(big, 1);
                }
                assert(e->stable >= 64 && e->own && !e->patched && e->sskip == sample);
                g_guest[100] = 0xb0;
                touch(100, 1);
                const unsigned patches = g_patches;
                assert(draw(big, 1)[100] == 0xb0 && g_patches == patches + 1 && e->patched == set);
                g_lose = 1;
                (void)vk_written_since(g_guest, 64, 0);
                g_guest[100] = 0xa0;
                f = g_full;
                drawn = draw(big, gap ? 8 : 1);
                assert(drawn[100] == (set ? 0xa0 : 0xb0));
                assert(g_full == f + (set || (sample == 1 && gap))); /* unset: a shortcut, or the full check */
            }
        /* (c) patch_dirty: a GPU-side write of B patched in, then A written back by the GPU with the next patch's
           staging failing: the full check after it */
        reset(0, 1);
        g_patch_hash = set;
        settle(big, 0xa0, 3);
        g_guest[5000] = 0xb0;
        g_pageg[1] = ++g_gpu_gen;
        const unsigned patches = g_patches;
        assert(draw(big, 1)[5000] == 0xb0 && g_patches == patches + 1 && e->patched == set);
        g_guest[5000] = 0xa0;
        g_pageg[1] = ++g_gpu_gen;
        g_stage_fail = 1;
        f = g_full;
        const u64 uploads = g_uploads, bytes = g_upload_bytes;
        drawn = draw(big, 1);
        assert(g_full == f + 1 && drawn[5000] == (set ? 0xa0 : 0xb0) && !e->patched);
        /* (e) the recopy is one upload of the range, and the next check under tracking takes the early return */
        assert(g_uploads == uploads + set && g_upload_bytes == bytes + (set ? big : 0));
        f = g_full;
        assert(!memcmp(draw(big, 1), set ? g_guest : g_own, big) && g_full == f);
    }
    /* (d) straight on the patches: one that fails on staging does not mark, one with no written page copies nothing and
       does not mark either (it returns 1), one that copies a run marks */
    reset(0, 1);
    g_patch_hash = 1;
    settle(big, 0xa0, 3); /* its patches marked it */
    g_vb[1].patched = 0;
    touch(8192, 16);
    g_stage_fail = 1;
    assert(!patch_own(1, g_guest, e->wseen) && !e->patched);
    const u32 now = g_wclock;
    assert(patch_own(1, g_guest, now) && !e->patched);
    assert(patch_own(1, g_guest, e->wseen) && e->patched);
    g_vb[1].patched = 0;
    assert(patch_dirty(1, GVA, g_guest, gpu_mem_generation(), now) && !e->patched); /* nothing GPU- or CPU-written */
    /* the switch unset never writes the mark */
    g_patch_hash = 0;
    assert(patch_own(1, g_guest, e->wseen) && !e->patched);
    printf("vulkan_sample: all checks passed\n");
    return 0;
}
'''


def main():
    args = sys.argv[1:]
    path = pathlib.Path(args.pop(0)) if args and args[0].endswith(".c") else SRC / "nx/gpu/vk/buffer.c"
    cc = args or ["cc", "-std=gnu11", "-O1"]
    text = path.read_text()
    vb = between(text, "typedef struct {\n    u64 start, len, hash, shash;", "} Vb;")
    patches = text[text.index("#define PATCH_MIN"):text.index("/* An own buffer is device memory")]
    runs = function(text, "static void written_runs(")
    body = HEAD + vb + STUBS + runs + patches + function(text, "static int buf_get(u64 gva, u64 len") + TESTS
    with tempfile.TemporaryDirectory() as d:
        c = pathlib.Path(d) / "sample.c"
        c.write_text(body)
        exe = pathlib.Path(d) / "sample"
        subprocess.run(cc + ["-Wall", "-Wno-unused-function", "-I", str(SRC), str(c), "-o", str(exe)], check=True)
        sys.exit(subprocess.run([str(exe)]).returncode)


if __name__ == "__main__":
    main()
