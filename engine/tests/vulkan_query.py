# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
"""SNX_ARCEUS_PERF_QUERY (PERF-19) through query.c's own code: the ring, vk_query_draw, vk_query_report,
vk_query_room, vk_query_flush, the reset and enable paths, vk_query_value and vk_query_pred_flush are copied out of
the source as they are, and run against a model of the GPU's occlusion queries. The model checks Vulkan's rules as
the commands are recorded: a query slot is reset before it begins, copies and resets are outside a rendering pass, and
a query ends in the pass it began in.

Cases (research 05:05 and its change after astra's objection): the same sequence of draws, resets, enables, reports
and submissions gives the same report values at 0 and 1, with fewer pass ends at 1. A report at next=2047 with
nothing copied, then resets and draws past 4096, stays counted, while the same trace without the room check before
draws (the first guard, at the report alone) makes the epoch's reports made up. Segment changes with no reset or
report (alternating sample counts) are covered by the check before each draw. The predicate batch copies the counts
after its pass end and before its barrier and dispatch.

Run: python3 tests/vulkan_query.py [compiler and flags], for example
    python3 tests/vulkan_query.py clang -std=gnu11 -O2 -fsanitize=address,undefined
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
    end = text.index(last, start)
    return text[start:text.index("\n", end) + 1]


HEAD = r'''
#include "nx/gpu/perf_times.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint64_t u64;
typedef int64_t i64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
typedef int VkQueryPool, VkBuffer, VkCommandBuffer, VkDescriptorSet, VkDescriptorSetLayout, VkPipelineLayout,
    VkPipeline;
typedef void* VmaAllocation;
typedef u64 VkDeviceSize;
typedef struct {
    int sType;
    u64 srcStageMask, srcAccessMask, dstStageMask, dstAccessMask;
} VkMemoryBarrier2;
typedef struct {
    int sType;
    u32 memoryBarrierCount;
    const VkMemoryBarrier2* pMemoryBarriers;
} VkDependencyInfo;
typedef struct {
    VkBuffer buffer;
    u64 offset, range;
} VkDescriptorBufferInfo;
typedef struct {
    int sType;
    VkDescriptorSet dstSet;
    u32 dstBinding, descriptorCount;
    int descriptorType;
    const VkDescriptorBufferInfo* pBufferInfo;
} VkWriteDescriptorSet;
enum { VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 = 1, VK_STRUCTURE_TYPE_DEPENDENCY_INFO, VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
       VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_PIPELINE_BIND_POINT_COMPUTE, VK_SHADER_STAGE_COMPUTE_BIT };
#define VK_PIPELINE_STAGE_2_COPY_BIT 1
#define VK_PIPELINE_STAGE_2_HOST_BIT 2
#define VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT 4
#define VK_PIPELINE_STAGE_2_CONDITIONAL_RENDERING_BIT_EXT 8
#define VK_ACCESS_2_TRANSFER_WRITE_BIT 1
#define VK_ACCESS_2_HOST_READ_BIT 2
#define VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT 4
#define VK_ACCESS_2_SHADER_STORAGE_READ_BIT 8
#define VK_ACCESS_2_CONDITIONAL_RENDERING_READ_BIT_EXT 16
#define VK_QUERY_RESULT_64_BIT 1
#define VK_QUERY_RESULT_WAIT_BIT 2
#define VK_QUERY_CONTROL_PRECISE_BIT 1
#define VK_WHOLE_SIZE (~0ull)
#define VK_QUERY_COUNTERS 8
#define VK_NULL_HANDLE 0
#define LOG(...) ((void)0)
#define LOG_LIMIT(...) ((void)0)
#define WARN_LIMIT(...) ((void)0)
static struct {
    void* vma;
    void* dev;
    struct {
        int occlusionQueryPrecise;
    } feat;
    int has_cond_render;
} g_vk = {0, 0, {1}, 1};
static float g_vk_scale = 1.0f;
static u64 snx_le64(u64 v) { return v; }
static void snx_le32_array(u32* w, size_t n) { (void)w, (void)n; }
static void vmaInvalidateAllocation(void* a, VmaAllocation m, u64 o, u64 n) { (void)a, (void)m, (void)o, (void)n; }
/* the GPU's model: counts by query slot, whether a slot was reset since its last query, the open pass */
#define MODEL_QN 4096
static u64 g_count[MODEL_QN], g_res_model[MODEL_QN];
static int g_ready[MODEL_QN], g_active = -1, g_in_pass, g_pass_id, g_begin_pass, g_pass_ends;
static u64 g_gval;
static char g_log[64];
static unsigned g_nlog;
static void note(char c) { g_log[g_nlog++ % sizeof g_log] = c; }
static void vk_query_pass_end(VkCommandBuffer cb);
static void vk_query_flush(VkCommandBuffer cb);
static VkCommandBuffer vk_cmd(void) { return 1; }
static u64 vk_submit_number(void) { return g_gval + 1; }
static void vk_submit_progress(u64* submitted, u64* done) { *submitted = *done = g_gval; }
static void rec_CmdBeginQuery(VkCommandBuffer cb, VkQueryPool p, u32 at, int flags) {
    (void)cb, (void)p, (void)flags;
    assert(g_in_pass && g_active < 0 && g_ready[at]); /* in a pass, one at a time, reset before */
    g_ready[at] = 0;
    g_count[at] = 0;
    g_active = (int)at;
    g_begin_pass = g_pass_id;
}
static void rec_CmdEndQuery(VkCommandBuffer cb, VkQueryPool p, u32 at) {
    (void)cb, (void)p;
    assert(g_active == (int)at && g_in_pass && g_begin_pass == g_pass_id); /* in the pass it began in */
    g_active = -1;
}
static void rec_CmdCopyQueryPoolResults(VkCommandBuffer cb, VkQueryPool p, u32 at, u32 n, VkBuffer b, u64 off,
                                        u64 stride, int flags) {
    (void)cb, (void)p, (void)b, (void)off, (void)stride, (void)flags;
    assert(!g_in_pass);
    for (u32 i = 0; i < n; i++) g_res_model[at + i] = g_count[at + i];
    note('C');
}
static void rec_CmdResetQueryPool(VkCommandBuffer cb, VkQueryPool p, u32 at, u32 n) {
    (void)cb, (void)p;
    assert(!g_in_pass);
    for (u32 i = 0; i < n; i++) g_ready[at + i] = 1;
}
static void rec_CmdPipelineBarrier2(VkCommandBuffer cb, const VkDependencyInfo* d) { (void)cb, (void)d, note('B'); }
static void vk_render_end(void) { /* as render.c's: the open query ends in the pass, then the pass */
    perf_pass_why(PE_OTHER);
    if (!g_in_pass) return;
    vk_query_pass_end(1);
    g_in_pass = 0;
    g_pass_ends++;
    note('E');
}
static void pass_begin(void) {
    if (!g_in_pass) g_in_pass = 1, g_pass_id++;
}
/* the reports queued: performed in order once their submission completed */
typedef struct {
    u32 counter;
    u64 first, end, gate;
} Rep;
static Rep g_rep[8192];
static u32 g_nrep, g_done_rep;
static u64 g_values[8192];
static void vk_release_samples(u64 gva, u32 len, u32 counter, u64 first, u64 end, u64 ticks) {
    (void)gva, (void)len, (void)ticks;
    g_rep[g_nrep++] = (Rep){counter, first, end, vk_submit_number()};
}
static void vk_perf_extra(int which) { perf_copy_extra(&g_perf_cp, which, 1); }
'''

PRED = r'''
static u32 g_words[1 << 16];
static void* vk_stage(u64 size, u64 align, VkBuffer* b, u64* off) { return (void)size, (void)align, *b = 5, *off = 0, g_words; }
static u64 vk_stage_align(void) { return 4; }
static VkDescriptorSet vk_desc_alloc(VkDescriptorSetLayout l) { return (void)l, 1; }
static void vkUpdateDescriptorSets(void* d, u32 n, const VkWriteDescriptorSet* w, u32 c, void* p) {
    (void)d, (void)n, (void)w, (void)c, (void)p;
}
static void rec_CmdBindPipeline(VkCommandBuffer cb, int bp, VkPipeline p) { (void)cb, (void)bp, (void)p; }
static void rec_CmdBindDescriptorSets(VkCommandBuffer cb, int bp, VkPipelineLayout l, u32 a, u32 n,
                                      const VkDescriptorSet* s, u32 d, const u32* o) {
    (void)cb, (void)bp, (void)l, (void)a, (void)n, (void)s, (void)d, (void)o;
}
static void rec_CmdPushConstants(VkCommandBuffer cb, VkPipelineLayout l, int st, u32 o, u32 n, const void* v) {
    (void)cb, (void)l, (void)st, (void)o, (void)n, (void)v;
}
static void rec_CmdDispatch(VkCommandBuffer cb, u32 x, u32 y, u32 z) {
    (void)cb, (void)x, (void)y, (void)z;
    assert(!g_in_pass);
    note('X');
}
'''

BODY = r'''
/* the submission's end as cmd.c's submit: the pass ends, then the counts are copied; then it completes, and the
   reports gated on it are performed in order */
static void submit(void) {
    vk_render_end();
    vk_query_flush(1);
    g_gval++;
    for (; g_done_rep < g_nrep && g_rep[g_done_rep].gate <= g_gval; g_done_rep++) {
        const Rep* r = &g_rep[g_done_rep];
        assert(g_q.copied >= r->end); /* its segments' copies were recorded in the submission it waited for */
        g_values[g_done_rep] = vk_query_value(r->counter, r->first, r->end);
    }
}
static void reset(int query) {
    memset(&g_q, 0, sizeof g_q);
    memset(g_qc, 0, sizeof g_qc);
    memset(g_qmark, 0, sizeof g_qmark);
    memset(g_qown, 0, sizeof g_qown);
    g_qmark_at = 0;
    for (u32 i = 0; i < MODEL_QN; i++) g_ready[i] = 1, g_count[i] = 0;
    g_qres = g_res_model;
    g_qpool = 1;
    g_active = -1;
    g_in_pass = g_pass_id = g_pass_ends = 0;
    g_gval = 0;
    g_nrep = g_done_rep = 0;
    g_nlog = 0;
    g_perf_query = query;
    g_perf_times = 1; /* the counts of reports kept and copies at half the ring */
    perf_copies_reset(&g_perf_cp);
}
/* a draw of samples samples counted by channel chan at sample mode msaa, with the room check before it (render.c) */
static int g_room_before_draw = 1;
static void draw(u32 chan, u32 msaa, u64 samples) {
    if (g_room_before_draw) vk_query_room();
    pass_begin();
    vk_query_draw(1, chan, msaa);
    if (g_active >= 0) g_count[g_active] += samples;
}
/* a deterministic run of the game's pattern: per frame two channels reset, draw, report, with submissions between */
static u64 g_out0[8192], g_out1[8192];
static u32 run(int query, u64* out) {
    reset(query);
    unsigned seed = 12345;
    for (int frame = 0; frame < 40; frame++) {
        vk_query_reset(0);
        vk_query_reset(1);
        for (int test = 0; test < 60; test++) {
            seed = seed * 1103515245u + 12345u;
            u32 chan = (seed >> 8) & 1, n = 1 + ((seed >> 12) & 3);
            for (u32 k = 0; k < n; k++) draw(chan, (seed >> (16 + k)) & 1, (seed >> 20) & 7);
            if ((seed >> 24) % 7 == 0) vk_query_enable(chan, 0), draw(chan, 0, 5), vk_query_enable(chan, 1);
            assert(vk_query_report(chan, 0x1000, 16, 0) == 1);
            if ((seed >> 27) % 11 == 0) submit();
        }
        submit();
    }
    submit();
    assert(g_done_rep == g_nrep);
    memcpy(out, g_values, g_nrep * sizeof *out);
    for (int i = 0; i < QCH; i++) assert(!g_qc[i].bad);
    return g_nrep;
}
static void test_same_values(void) {
    const u32 n0 = run(0, g_out0);
    const int ends0 = g_pass_ends;
    const u32 n1 = run(1, g_out1);
    const int ends1 = g_pass_ends;
    assert(n0 == n1 && n0 == 2400 && !memcmp(g_out0, g_out1, n0 * sizeof *g_out0));
    assert(ends1 < ends0 / 4); /* reports no longer end the pass */
    assert(g_perf_cp.extra[PX_QUERY_KEPT] == n1 && g_perf_cp.extra[PX_QUERY_GUARD] == 0);
    u64 sum = 0;
    for (u32 i = 0; i < n0; i++) sum += g_out0[i];
    assert(sum > 0);
}
/* research's trace: the report's epoch starts at 2000 and it comes at next 2047 with nothing copied, then draws past
   4096 with no submission (resets every 500 when asked). A queued report holds its epoch's first segment, so only
   the counts not copied can fill the ring before 2000 + 4096 */
static u64 trace(int query, int room_before_draw, int resets, int* bad) {
    reset(query);
    g_room_before_draw = room_before_draw;
    vk_query_reset(0);
    for (u32 i = 0; g_q.next < 2000; i++) draw(0, i & 1, 1); /* each draw a segment of its own */
    vk_query_reset(0);
    for (u32 i = 0; g_q.next < 2047; i++) draw(0, i & 1, 1);
    assert(g_q.next == 2047 && (!query || g_q.copied == 0));
    assert(vk_query_report(0, 0x1000, 16, 0) == 1);
    for (u32 i = 0; i < 2200 && !g_qc[0].bad; i++) {
        if (resets && i % 500 == 250) vk_query_reset(0);
        draw(0, i & 1, 1);
    }
    *bad = g_qc[0].bad;
    const int second = vk_query_report(0, 0x1000, 16, 0);
    submit();
    g_room_before_draw = 1;
    assert(g_done_rep == g_nrep);
    return second ? g_values[1] : ~0ull;
}
static void test_trace(void) {
    int bad0, bad1, bad_first;
    for (int resets = 0; resets < 2; resets++) {
        const u64 v0 = trace(0, 1, resets, &bad0);
        const u64 v1 = trace(1, 1, resets, &bad1);
        assert(!bad0 && !bad1 && v0 == v1 && v0 > 0 && v0 != ~0ull && g_q.next > 4096);
        assert(g_perf_cp.extra[PX_QUERY_GUARD] >= 1 && g_values[0] == 24 + 2 * 23); /* 47 draws, odd ones at 2x */
    }
    trace(1, 0, 0, &bad_first); /* the first guard, at reports alone: the draws outgrow the ring */
    assert(bad_first);
}
/* segments that change with no reset or report (alternating sample modes), 9000 draws with a submission every 3000:
   at 1 the check before each draw copies at half the ring, and the count is the same as at 0 */
static u64 no_report(int query) {
    reset(query);
    vk_query_reset(0);
    for (u32 i = 0; i < 9000; i++) {
        draw(0, i & 1, 2);
        assert(!g_qc[0].bad && g_q.next - g_q.copied <= (query ? QN / 2 : QN - 1));
        if (i % 3000 == 2999) submit();
    }
    assert(vk_query_report(0, 0x1000, 16, 0) == 1);
    submit();
    return g_values[0];
}
static void test_no_report(void) {
    const u64 v0 = no_report(0), v1 = no_report(1);
    assert(g_perf_cp.extra[PX_QUERY_GUARD] >= 3);
    assert(v0 == v1 && v0 == 2 * (4500 + 2 * 4500ull)); /* 2 samples a draw, the odd draws at 2x */
}
/* each place a segment may begin copies at half the ring itself, not only the next draw's check: a reset, an
   enable or a report with exactly QN / 2 segments not copied */
static void half_ring(int which) {
    reset(1);
    vk_query_reset(0);
    for (u32 i = 0; g_q.next - g_q.copied < QN / 2; i++) draw(0, i & 1, 1);
    assert(g_q.next - g_q.copied == QN / 2 && g_in_pass);
    if (which == 0) vk_query_reset(1);
    if (which == 1) vk_query_enable(0, 0);
    if (which == 2) assert(vk_query_report(0, 0x1000, 16, 0) == 1);
    assert(g_q.copied == g_q.next && !g_in_pass && g_perf_cp.extra[PX_QUERY_GUARD] == 1);
    assert(g_perf_cp.extra[PX_QUERY_KEPT] == 0); /* a report that copied is not kept */
}
static void test_half_ring(void) {
    for (int which = 0; which < 3; which++) half_ring(which);
}
/* the predicate batch reads the counts on the GPU: they are copied after its pass end, before its barrier */
static void test_pred(void) {
    static u32 words[QCH][4]; /* the batches' words: query.c's memcpy of an empty batch from a NULL one is UB */
    for (int i = 0; i < QCH; i++)
        if (!g_pb[i].w) g_pb[i].w = words[i];
    reset(1);
    vk_query_reset(0);
    draw(0, 0, 3);
    assert(vk_query_report(0, 0x1000, 16, 0) == 1);
    assert(g_q.copied < g_q.next && g_in_pass); /* the report kept the pass and left the copy */
    g_pngroups = 1;
    g_nlog = 0;
    assert(vk_query_pred_flush() == 1);
    /* the pass ends, the copy and its barrier, then the batch's barriers around its dispatch */
    assert(g_nlog == 6 && !memcmp(g_log, "ECBBXB", 6) && g_q.copied == g_q.next);
    reset(0); /* unset: the report copied already, the batch copies nothing, not even a segment begun since */
    vk_query_reset(0);
    draw(0, 0, 3);
    assert(vk_query_report(0, 0x1000, 16, 0) == 1 && g_q.copied == g_q.next && !g_in_pass);
    draw(0, 1, 3);
    assert(g_q.copied + 1 == g_q.next);
    g_pngroups = 1;
    g_nlog = 0;
    assert(vk_query_pred_flush() == 1);
    assert(g_nlog == 4 && !memcmp(g_log, "EBXB", 4));
}
int main(void) {
    test_same_values();
    test_trace();
    test_no_report();
    test_half_ring();
    test_pred();
    puts("vulkan_query: ok");
    return 0;
}
'''


def main():
    q = (SRC / "nx/gpu/vk/query.c").read_text()
    code = HEAD
    code += between(q, "#define QN ", "static u32 g_qmark_at;")
    code += "static int pool_make(void) { return 1; }\n"
    code += between(q, "#define PN ", "static u32 g_pbatch, g_pslot_batch[PN];")
    code += PRED
    for sig in ["static QCounter* counter(", "static u64 live_from(", "static u64 live_oldest(",
                "static u32 msaa_samples(", "static void fold_done(", "void vk_query_draw(", "void vk_query_pass_end(", "void vk_query_flush(",
                "int vk_query_room(", "static void end_own(", "void vk_query_reset(", "void vk_query_enable(",
                "int vk_query_report(", "u64 vk_query_value(", "void vk_query_pred_drop(",
                "static void pred_batch_reset(int drop) {", "int vk_query_pred_flush("]:
        text = function(q, sig)
        for name in ["void vk_query_draw(", "void vk_query_pass_end(", "void vk_query_flush(", "int vk_query_room(",
                     "void vk_query_reset(", "void vk_query_enable(", "int vk_query_report(", "u64 vk_query_value(",
                     "void vk_query_pred_drop(", "int vk_query_pred_flush("]:
            if text.startswith(name):
                text = "static " + text
        code += text
    code += BODY
    cc = sys.argv[1:] or ["cc", "-std=gnu11", "-O1"]
    with tempfile.TemporaryDirectory() as tmp:
        c = pathlib.Path(tmp) / "vulkan_query.c"
        c.write_text(code)
        exe = pathlib.Path(tmp) / "vulkan_query"
        subprocess.run(cc + ["-Wall", "-Wno-unused-function", "-Wno-unused-variable", "-I", str(SRC), str(c), str(SRC / "nx/gpu/perf_times.c"),
                             "-o", str(exe)], check=True)
        return subprocess.run([str(exe)]).returncode


if __name__ == "__main__":
    sys.exit(main())
