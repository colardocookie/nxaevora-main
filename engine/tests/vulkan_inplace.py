# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
"""SNX_ARCEUS_PERF_INPLACE (PERF-17) through buffer.c's own code: gfx_gpu_src, vk_buf_copy_writable,
vk_buf_draw_prepare, draw_output_page, vk_buf_writable_commit and the write stamp are copied out of the source as
they are, and run against small stand-ins for the rest (no Vulkan, no guest memory).

Level 3 (research 02:48, astra 02:46): a reuse barrier, then an alias-only writer through the atlas, then a reader
must record a barrier, two readers with no writer between record one, a new submission records again, and a read
then a rewrite in place in one draw keeps the barrier before that draw. Level 1: newer page-only neighbours, a newer
partial writer, edited bytes, the frame before, the alignment refusal, the allocation failure and a miss.

Run: python3 tests/vulkan_inplace.py [compiler and flags], for example
    python3 tests/vulkan_inplace.py clang -std=gnu11 -O2 -fsanitize=address,undefined
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
    return text[start:text.index(last, start)]


HEAD = r'''
#include "nx/gpu/perf_times.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint64_t u64;
typedef uint32_t u32;
typedef uint8_t u8;
typedef int VkBuffer;
typedef void* VmaAllocation;
typedef int RangePages;
typedef struct GfxOut {
    u64 addr, size, off, frame, frame_id;
    VkBuffer buf;
    int ok, alias, alias_bytes, composed;
    u8 *org, *ptr;
    VmaAllocation mem;
    u64 pages[1], vmap[1];
    struct GfxOut* next;
} GfxOut;
typedef struct {
    u64 addr;
    u32 size;
    int writable, aliased, bare;
    VkBuffer buf;
    u64 off, range;
} VkDrawGbuf;
typedef struct {
    VkDrawGbuf b[2][16];
    int n[2];
    VkBuffer map_buf[2];
    u64 map_off[2], map_size[2];
} VkDrawStorage;
typedef struct {
    u64 key, gva;
    int writable, bind;
} DrawPage;
static struct {
    struct {
        struct {
            u64 maxStorageBufferRange, nonCoherentAtomSize;
        } limits;
    } props;
} g_vk = {{{1u << 27, 64}}};
#define WARN_LIMIT(...) ((void)0)
#define VK_NULL_HANDLE 0
#define WB_DRAW 1
#define VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT 1
#define VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT 2
#define VK_ACCESS_2_MEMORY_READ_BIT 1
#define VK_ACCESS_2_MEMORY_WRITE_BIT 2
static GfxOut *g_out, *g_out_tail, *g_prep, *g_prep_tail;
static int g_gfx_any = 1, g_out_landing, g_cs_any, g_out_pages, g_map_fail;
static u64 g_wb_draw_miss[4], g_wb_reuses, g_vk_drawn, g_vk_dispatches, g_alias_draws, g_alias_pages;
static u64 g_submit = 10, g_frame = 10;
static struct {
    GfxOut* o;
    u64 work;
} g_wmark[1];
static int lands, holds, composes, classes;
static char g_log[512]; /* B<work> a reuse barrier, D<work> a draw recorded, in the order recorded */
static void note(char what) {
    size_t n = strlen(g_log);
    snprintf(g_log + n, sizeof g_log - n, "%c%llu ", what, (unsigned long long)(g_vk_drawn + g_vk_dispatches));
}
static u64 vk_frame_id(void) { return g_frame; }
static u64 vk_submit_number(void) { return g_submit; }
static u64 vk_stage_align(void) { return 4; }
static void vk_compute_mem_need(u64 a, u64 n) { (void)a, (void)n; }
static int pend_hit(int* p, u64 a, u64 n) { return (void)p, (void)a, (void)n, 1; }
static void range_pages(u64 a, u64 n, RangePages* q) { (void)a, (void)n, *q = 1; }
/* a page in common at this address, or through another one (alias) */
static int out_meets_range(const GfxOut* o, u64 a, u64 n, const RangePages* q) {
    (void)q;
    return ((a & ~4095ull) < o->addr + o->size && (o->addr & ~4095ull) < a + n) || o->alias;
}
/* a byte in common: at this address, or through another (alias_bytes). No page map: any page counts */
static int out_meets(const GfxOut* o, u64 a, u64 n, const RangePages* q, u64** rmap, u64 local[16]) {
    (void)rmap, (void)local;
    if (a < o->addr + o->size && o->addr < a + n) return 1;
    if (!out_meets_range(o, a, n, q)) return 0;
    return g_map_fail ? 1 : o->alias_bytes;
}
static int out_still_copyable(const GfxOut* o, u64 a, u64 n, u64** rmap, u64 local[16]) {
    (void)a, (void)n, *rmap = local;
    return o->ok;
}
static int input_class(u64 a, u64 n, u64 exact) { return (void)a, (void)n, (void)exact, classes++, PI_NONE; }
static int copy_stats_on(void) { return 0; }
static void land_reason(int why) { assert(why == WB_DRAW), lands++; }
static void vk_frame_hold(u64 frame) { (void)frame, holds++; }
static u32 wmark_slot(const GfxOut* o) { return (void)o, 0; }
static void wmark_set(GfxOut* o) { g_wmark[0].o = o, g_wmark[0].work = g_vk_drawn + g_vk_dispatches; }
static int out_stages(void) { return 1; }
static int out_access(void) { return 2; }
static void vk_render_end(void) {}
static void copy_barrier(int a, int b, int c, int d) { (void)a, (void)b, (void)c, (void)d, note('B'); }
static void out_pages(GfxOut* o, int d) { (void)o, (void)d; }
static int vk_buf_copy(u64 a, u64 n, VkBuffer* b, u64* off) { return (void)a, (void)n, *b = 99, *off = 0, 1; }
static void gmem_need(u64 a, u64 n) { (void)a, (void)n; }
static int range_owned(u64 a, u64 n) { return (void)a, (void)n, 0; }
static int written_since_gva(u64 a, u64 n, u32 s) { return (void)a, (void)n, (void)s, 0; }
static GfxOut* gfx_out_new(u32 pages, u32 len) { /* one allocation, as buffer.c's: free(o) frees it all */
    (void)pages;
    GfxOut* o = calloc(1, sizeof *o + len);
    if (o) o->org = (u8*)(o + 1), o->ok = 1;
    return o;
}
static u8 g_guest[1 << 16], g_stage[1 << 16];
static u32 g_map[256];
static void read_untracked(u64 a, u8* dst, u64 n) { (void)a, memset(dst, 0, n); }
static const u8* guest_bytes(u64 a, u64 n) { return (void)a, (void)n, g_guest; }
static const u8* gmem_ptr_untracked(u64 a, u64 n) { return (void)a, (void)n, g_guest; }
static void out_map(GfxOut* o, u64 a) { (void)o, (void)a; }
static u8* vk_stage_output(u64 n, u64 align, VkBuffer* b, u64* off, VmaAllocation* m) {
    (void)n, (void)align, *b = 77, *off = 0, *m = NULL;
    return g_stage;
}
static void* vk_stage(u64 n, u64 align, VkBuffer* b, u64* off) { return (void)n, (void)align, *b = 78, *off = 0, g_map; }
static int writable_owned(GfxOut* o, u64 a, u32 n, VkBuffer b, u64 off) {
    return (void)o, (void)a, (void)n, (void)b, (void)off, 1;
}
static int gfx_overlay(u64 a, u64 n, VkBuffer b, u64 off, int site) {
    return (void)a, (void)n, (void)b, (void)off, (void)site, composes++, 1;
}
static u64 g_clock;
static u64 plat_ns(void) { return ++g_clock; }
static void copy_note(u64 a, u64 n) { (void)a, (void)n; }
static void snx_le32_array(u32* w, size_t n) { (void)w, (void)n; }
/* the atlas helpers: one writable aliased binding, one page */
static int draw_has_writable(const VkDrawStorage* d) { return d->b[0][0].writable; }
static int draw_may_alias(const VkDrawStorage* d) { return (void)d, 1; }
static DrawPage* draw_pages(const VkDrawStorage* d, size_t* n) {
    DrawPage* p = calloc(1, sizeof *p);
    if (p) *p = (DrawPage){d->b[0][0].addr >> 12, d->b[0][0].addr & ~4095ull, 1, 0}, *n = 1;
    return p;
}
static int draw_alias_bindings(const DrawPage* p, size_t n, u8 active[32]) { return (void)p, (void)n, active[0] = 1, 1; }
static u64 draw_page_key(u64 g) { return g >> 12; }
static u32 draw_page_index(const DrawPage* p, size_t n, u64 key) { return (void)p, (void)n, (void)key, 0; }
static int atlas_fill(const DrawPage* p, size_t n, u8* ptr, VkBuffer b, u64 off) {
    return (void)p, (void)n, (void)ptr, (void)b, (void)off, 0;
}
/* the PERF_TIMES hooks, counted straight into the table */
static void vk_perf_src(int kind, int outcome) { perf_copy_src(&g_perf_cp, kind, outcome); }
static void vk_perf_class(int cls) { perf_copy_class(&g_perf_cp, cls); }
static void vk_perf_extra(int which) { perf_copy_extra(&g_perf_cp, which, 1); }
static void vk_perf_made(int producer) { perf_copy_made(&g_perf_cp, producer); }
static void vk_perf_writable(int path, const u64* ns, u64 bytes) { perf_copy_writable(&g_perf_cp, path, ns, bytes); }
'''

BODY = r'''
static GfxOut* result(u64 addr, u64 size, VkBuffer buf) {
    GfxOut* o = gfx_out_new(1, (u32)size);
    o->addr = addr, o->size = size, o->buf = buf, o->frame = g_submit, o->frame_id = g_frame;
    if (g_out_tail) g_out_tail->next = o; else g_out = o;
    g_out_tail = o;
    return o;
}
static void reset(int level) {
    while (g_out) { GfxOut* o = g_out; g_out = o->next; free(o); }
    g_out_tail = NULL;
    vk_buf_writable_abort();
    g_perf_inplace = level;
    g_out_write_work = 0;
    g_vk_drawn = g_vk_dispatches = 0;
    g_submit = g_frame = 10;
    g_log[0] = 0;
    g_map_fail = lands = holds = composes = classes = 0;
    perf_copies_reset(&g_perf_cp);
}
/* a draw that reads o in place */
static void reads(const GfxOut* o) {
    VkBuffer b = 0;
    u64 off = ~0ull;
    assert(gfx_gpu_src(o->addr, o->size, &b, &off, 0, 4, PB_INPUT) == 1 && b == o->buf && off == o->off);
}
static void drawn(void) { note('D'); g_vk_drawn++; }
/* a draw whose only writable binding is aliased: its result comes from the atlas, not vk_buf_copy_writable */
static GfxOut* alias_writer(u64 gva) {
    VkDrawStorage d;
    memset(&d, 0, sizeof d);
    d.n[0] = 1;
    d.b[0][0] = (VkDrawGbuf){gva, 256, 1, 0, 0, 0, 0, 0};
    assert(vk_buf_draw_prepare(&d) == 1 && d.b[0][0].aliased && g_prep);
    if (g_perf_inplace >= 3) assert(g_out_write_work == g_vk_drawn + g_vk_dispatches); /* marked before commit */
    vk_buf_writable_commit();
    drawn();
    return g_out_tail;
}
static void test_level3(void) {
    reset(3);
    g_perf_times = 1;
    GfxOut* r = result(0x20000, 1024, 5);
    reads(r); drawn();                   /* 0: the barrier */
    GfxOut* x = alias_writer(0x30000);   /* 1: an atlas page, after the barrier */
    assert(x && x->addr == 0x30000 && g_perf_cp.made[PR_ATLAS] == 1);
    reads(x); drawn();                   /* 2: must record one (astra 02:46) */
    reads(r); drawn();                   /* 3: no writer since: skipped */
    reads(x); drawn();                   /* 4: skipped */
    g_submit++;                          /* a new submission */
    r->frame = g_submit;                 /* r rewritten in this one, below, so in place */
    reads(r); drawn();                   /* 5: records */
    reads(r);                            /* 6: covered by 5's */
    VkBuffer b;
    u64 off;
    assert(vk_buf_copy_writable(r->addr, (u32)r->size, &b, &off) == 1 && b == r->buf); /* then rewritten in place */
    drawn();
    reads(r); drawn();                   /* 7: after a writer: records */
    assert(!strcmp(g_log, "B0 D0 D1 B2 D2 D3 D4 B5 D5 B6 D6 B7 D7 "));
    assert(g_perf_cp.extra[PX_SKIPPED] == 3 && g_perf_cp.extra[PX_BARRIER] == 5);
    assert(g_perf_cp.made[PR_INPLACE] == 1);
    assert(g_perf.count[PC_SRC_WALKS] >= 8 && g_perf.site[PT_SRC_WALK].count == g_perf.count[PC_SRC_WALKS]);
    /* a dispatch's result and a transform feedback capture, each through vk_buf_copy_writable, then a reader */
    reads(r); drawn();                   /* 8: covered by 7's */
    g_perf_bind = PBK_DISPATCH;
    assert(vk_buf_copy_writable(0x40000, 512, &b, &off) == 1);
    g_perf_bind = PBK_OTHER;
    vk_buf_writable_commit();
    g_vk_dispatches++;
    reads(r); drawn();                   /* 10: records */
    g_perf_bind = PBK_XFB;
    assert(vk_buf_copy_writable(0x50000, 512, &b, &off) == 1);
    assert(vk_buf_copy_writable(0x50000, 512, &b, &off) == 1); /* the same range again in that draw */
    g_perf_bind = PBK_OTHER;
    vk_buf_writable_commit();
    drawn();
    reads(r); drawn();                   /* 12: records */
    assert(!strcmp(g_log, "B0 D0 D1 B2 D2 D3 D4 B5 D5 B6 D6 B7 D7 D8 B10 D10 D11 B12 D12 "));
    assert(g_perf_cp.made[PR_DISPATCH] == 1 && g_perf_cp.made[PR_XFB] == 1 && g_perf_cp.made[PR_REPEAT] == 1);
    /* PERF-20: the two new results took the plain path, each step timed once (the repeated binding is not one) */
    assert(g_perf_cp.wr_calls[PW_PLAIN] == 2 && g_perf_cp.wr_bytes[PW_PLAIN] == 1024);
    for (int k = 0; k < PWS_N; k++) assert(g_perf_cp.wr_ns[PW_PLAIN][k] == 2);
    /* a producer that reaches the prepared list some other way is caught at its publication */
    reads(r); drawn();                   /* 13: covered */
    GfxOut* other = gfx_out_new(1, 64);
    other->addr = 0x60000, other->size = 64, other->frame = g_submit, other->frame_id = g_frame;
    g_prep = g_prep_tail = other;
    vk_buf_writable_commit();
    drawn();
    reads(r); drawn();                   /* 15: records */
    assert(!strcmp(g_log + strlen("B0 D0 D1 B2 D2 D3 D4 B5 D5 B6 D6 B7 D7 D8 B10 D10 D11 B12 D12 "), "D13 D14 B15 D15 "));
    g_perf_times = 0;
}
/* the same sequence unset: a barrier for every reading draw, as before the switch */
static void test_level0(void) {
    reset(0);
    const u64 clock = g_clock; /* PERF_TIMES off: no clock read (PERF-18's timing) */
    GfxOut* r = result(0x20000, 1024, 5);
    reads(r); drawn();
    GfxOut* x = alias_writer(0x30000);
    reads(x); drawn();
    reads(r); drawn();
    reads(x); drawn();
    assert(!strcmp(g_log, "B0 D0 D1 B2 D2 B3 D3 B4 D4 "));
    assert(g_out_write_work == 0); /* not kept below level 3 */
    assert(g_clock == clock);
}
/* level 1: what gfx_gpu_src returns for a read of [0x10000, 0x10100) over r, with others on its pages */
static int read_at(int level, u64 len, VkBuffer* b) {
    u64 off;
    g_perf_inplace = level;
    return gfx_gpu_src(0x10000, len, b, &off, 0, 4, PB_INPUT);
}
static void test_level1(void) {
    VkBuffer b;
    reset(1);
    GfxOut* r = result(0x10000, 0x400, 5);
    GfxOut* n = result(0x10400, 0x400, 6); /* newer, the same page, no byte in common */
    assert(read_at(0, 0x100, &b) == 2);    /* unset: several, composed */
    assert(read_at(1, 0x100, &b) == 1 && b == 5);
    g_map_fail = 1;                        /* no page map: every page neighbour meets, so the old path runs */
    assert(read_at(1, 0x100, &b) == 2);
    g_map_fail = 0;
    n->alias = n->alias_bytes = 1;         /* newer, sharing bytes through another address: the old path */
    assert(read_at(1, 0x100, &b) == 2);
    n->alias = n->alias_bytes = 0;
    r->ok = 0;                             /* the guest edited r's bytes: landed, as unset */
    lands = 0;
    assert(read_at(1, 0x100, &b) == 0 && lands == 1);
    lands = 0;
    assert(read_at(0, 0x100, &b) == 0 && lands == 1);
    r->ok = 1;
    r->frame_id = 9;                       /* the frame before: held, bound */
    holds = 0;
    assert(read_at(1, 0x100, &b) == 1 && holds == 1);
    r->frame_id = 10;
    r->off = 2;                            /* an offset the binding refuses: composed */
    assert(read_at(1, 0x100, &b) == 2);
    r->off = 0;
    assert(read_at(1, 0x800, &b) == 2);    /* r does not cover the range: the old path */
    GfxOut* p = result(0x10080, 0x100, 7); /* newer, a part of the range: the old path */
    assert(read_at(1, 0x100, &b) == 2);
    (void)p;
    u64 off;                               /* a writable binding is never changed */
    reset(1);
    result(0x10000, 0x400, 5);
    result(0x10400, 0x400, 6);
    assert(gfx_gpu_src(0x10000, 0x100, &b, &off, 1, 4, PB_WRITE) == 2);
    g_vk_drawn = 4;
    assert(vk_buf_copy_writable(0x10000, 0x100, &b, &off) == 1 && g_out_write_work == 0); /* kept at level 3 only */
    reset(1);
    GfxOut* q = result(0x10200, 0x100, 5); /* no byte of the range in any result: composed as before */
    (void)q;
    assert(read_at(1, 0x100, &b) == 2);
}
int main(void) {
    test_level3();
    test_level0();
    test_level1();
    reset(0);
    puts("vulkan_inplace: ok");
    return 0;
}
'''


def main():
    buffer = (SRC / "nx/gpu/vk/buffer.c").read_text()
    code = HEAD
    code += between(buffer, "static u64 g_out_write_work;", "void vk_buf_writable_commit(void) {")
    for sig in ["void vk_buf_writable_abort(void) {", "void vk_buf_writable_commit(void) {", "static void src_walked(", "static void wr_step(",
                "static int gfx_gpu_src(u64 gva", "int vk_buf_copy_writable(u64 gva",
                "static int draw_output_page(", "int vk_buf_draw_prepare(VkDrawStorage* draw) {"]:
        code += function(buffer, sig)
    code += BODY
    cc = sys.argv[1:] or ["cc", "-std=gnu11", "-O1"]
    with tempfile.TemporaryDirectory() as tmp:
        c = pathlib.Path(tmp) / "vulkan_inplace.c"
        c.write_text(code)
        exe = pathlib.Path(tmp) / "vulkan_inplace"
        subprocess.run(cc + ["-Wall", "-Wno-unused-function", "-I", str(SRC), str(c), str(SRC / "nx/gpu/perf_times.c"),
                             "-o", str(exe)], check=True)
        return subprocess.run([str(exe)]).returncode


if __name__ == "__main__":
    sys.exit(main())
