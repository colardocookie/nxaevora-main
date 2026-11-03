/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/gpu/nvn_selftest_rec.h"
#include "nx/gpu/maxwell/kepler_compute_regs.h"

static NvnStRec* rec;
static NvnStSnapshotFn snapshot_fn;
static unsigned calls;

static int start(void) { return 1; }
static void mem_may_have_changed(void) {}
static void stream_run(int on) { (void)on; }

/* a call that changes the picture (kind S): the snapshot first, then the arguments */
static void work(GpuChannel* ch, const char* op) {
    if (!rec) return;
    calls++;
    if (snapshot_fn) snapshot_fn(ch, rec);
    nvn_st_add_state(rec, op, 0);
}

static void clear(GpuChannel* ch, u32 v) {
    work(ch, "clear");
    if (rec) nvn_st_add_state(rec, "clear.value", v);
}

static void draw(GpuChannel* ch, int indexed, u32 topology, u32 first, u32 count, u32 base_vertex, u32 base_instance,
                 u32 instances) {
    work(ch, "draw");
    if (!rec) return;
    nvn_st_add_state(rec, "draw.indexed", (uint64_t)indexed);
    nvn_st_add_state(rec, "draw.topology", topology);
    nvn_st_add_state(rec, "draw.first", first);
    nvn_st_add_state(rec, "draw.count", count);
    nvn_st_add_state(rec, "draw.base_vertex", base_vertex);
    nvn_st_add_state(rec, "draw.base_instance", base_instance);
    nvn_st_add_state(rec, "draw.instances", instances);
}

static void draw_texture(GpuChannel* ch) { work(ch, "draw_texture"); }

static void dispatch(GpuChannel* ch, const u32* q, u64 code) {
    work(ch, "dispatch");
    if (!rec) return;
    nvn_st_add_state(rec, "dispatch.code", code);
    if (!q) return;
    nvn_st_add_state(rec, "dispatch.grid_x", qmd_grid_dim_x(q));
    nvn_st_add_state(rec, "dispatch.grid_y", qmd_grid_dim_y(q));
    nvn_st_add_state(rec, "dispatch.grid_z", qmd_grid_dim_z(q));
    nvn_st_add_state(rec, "dispatch.block_x", qmd_block_dim_x(q));
    nvn_st_add_state(rec, "dispatch.block_y", qmd_block_dim_y(q));
    nvn_st_add_state(rec, "dispatch.block_z", qmd_block_dim_z(q));
    nvn_st_add_state(rec, "dispatch.shared", qmd_shared_alloc(q));
    nvn_st_add_state(rec, "dispatch.program_start", qmd_program_start(q));
    nvn_st_add_state(rec, "dispatch.cb_mask", q[QMD_CONST_BUFFER_ENABLE] & 0xFFu);
    for (u32 i = 0; i < QMD_NUM_CONST_BUFFERS; i++) {
        if (!qmd_const_buffer_enabled(q, i)) continue;
        nvn_st_add_state(rec, "dispatch.cb_address", qmd_const_buffer_address(q, i));
        nvn_st_add_state(rec, "dispatch.cb_size", qmd_const_buffer_size(q, i));
    }
}

/* kind Y: the order of the sync operations, not their timing */
static void method_barrier(int kind) {
    if (!rec) return;
    calls++;
    nvn_st_add_sync(rec, "barrier", (uint64_t)kind);
}
static void before_syncwait(void) {
    if (!rec) return;
    calls++;
    nvn_st_add_sync(rec, "wait", 0);
}
static void stream_incr(u32 id) {
    if (!rec) return;
    calls++;
    nvn_st_add_sync(rec, "fence", id);
}
static void release_write(u64 gva, const void* data, u32 len) {
    if (!rec) return;
    calls++;
    /* the payload (a counter or a timestamp) is a value of the run, so only the length is part of the order */
    (void)data;
    nvn_st_add_sync(rec, "semaphore", gva);
    nvn_st_add_sync(rec, "semaphore.len", len);
}
static void stream_end(void) {}

const Renderer nvn_st_renderer = {
    .name = "nvn-selftest-recorder",
    .start = start,
    .mem_may_have_changed = mem_may_have_changed,
    .stream_run = stream_run,
    .clear = clear,
    .draw = draw,
    .method_barrier = method_barrier,
    .draw_texture = draw_texture,
    .dispatch = dispatch,
    .before_syncwait = before_syncwait,
    .stream_incr = stream_incr,
    .release_write = release_write,
    .stream_end = stream_end,
};

void nvn_st_renderer_begin(NvnStRec* r, NvnStSnapshotFn snapshot) {
    rec = r;
    snapshot_fn = snapshot;
    calls = 0;
}

unsigned nvn_st_renderer_end(void) {
    unsigned n = calls;
    rec = NULL;
    snapshot_fn = NULL;
    return n;
}
