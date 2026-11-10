/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The command buffer cases of the NVN selftest (docs/nvn-design.md, "The selftest") for step 4-u3-3: the same calls go
   to the library's command buffer and to the host's, the library recording is decoded into push buffer words, and both
   sides run on a private channel of their own in one GPU call. The records compare kinds S (the state at the clear) and Y
   (the order of the sync operations). */
#include "nx/gpu/nvn_selftest_int.h"
#include "nx/gpu/nvn_selftest_dec.h"
#include "nx/gpu/nvn_selftest_rec.h"
#include "nx/gpu/nvn_native.h"
#include "nx/gpu/gpu.h"
#include "nx/gpu/maxwell/maxwell_3d_regs.h"
#include "nx/gpu/renderer.h"
#include "nx/hle/services/nvdrv.h"
#include "nx/memory/mem.h"
#include "nx/runtime/options.h"
#include "nx/runtime/platform/log.h"
#include "nx/runtime/platform/plat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A GPU call that does not finish in this time is a hang, not a slow case. */
#define ST_GPU_WAIT_MS 5000u
#define ST_CMD_BYTES 0x1000u
#define ST_CONTROL_BYTES 0x800u
#define ST_CMD_REFILL_BYTES 0x400u
#define ST_CB_OBJ_BYTES 0x200u /* the NVN part of a game command buffer is at most 0xE0 (docs/nvn-design.md) */
#define ST_MAX_RANGES 64u
#define ST_VIEWPORTS 2u
#define ST_SCISSORS 2u
#define ST_CLEAR_MASK 0xFu
#define ST_BARRIERS 9u
#define ST_CASE_COUNT (1u + ST_BARRIERS)

/* Eden's Regs::scissor_test, clear_color: not in maxwell_3d_regs.h */
#define MAXWELL3D_SCISSOR_TEST 0x380u
#define MAXWELL3D_CLEAR_COLOR 0x360u

/* The device word that SetRenderTargets compares with 0x08c8b771 (nvn_selftest.c's DEVICE_DEPTH_MODE_OFFSET). */
#define ST_DEVICE_DEPTH_MODE_OFFSET 0x1a04u

/* ClearColor is macro 1: it writes CLEAR_SURFACE once per layer of the target, at 0x206 + 16 per target. A real
   render target has one layer; the cases do not call SetRenderTargets, so the fixture sets that count. */
#define ST_CLEAR_LAYER_COUNT_REG 0x206u
#define ST_CLEAR_LAYER_COUNT 1u

/* The argument block of the S cases, in the arena: the same values for the library, a changed copy for a mutation. */
#define ST_ARG_VP_LIB 0x00u
#define ST_ARG_VP_HOST 0x20u
#define ST_ARG_SC_LIB 0x40u
#define ST_ARG_SC_HOST 0x60u
#define ST_ARG_COLOR 0x80u
#define ST_ARG_BYTES 0x90u

#define ST_CLEAR_INDEX 0u
#define ST_MUTATE_VIEWPORT_WORD 0u /* the x of viewport 0 */
#define ST_MUTATE_VIEWPORT_ADD 1.0f

static const float st_viewports[8] = {0.0f, 0.0f, 1280.0f, 720.0f, 16.0f, 32.0f, 640.0f, 360.0f};
static const int32_t st_scissors[8] = {0, 0, 1280, 720, 8, 8, 100, 50};
static const float st_clear[4] = {0.25f, 0.5f, 0.75f, 1.0f};

/* The state that the 3c functions set, at the clear. 2 * (6 viewport transform words + 4 viewport words + 3 scissor
   words) + 4 clear words = 30 fields, and the recorder adds 2 for the clear itself. */
static const char* const st_vp_names[2][6] = {
    {"vp0.scale_x", "vp0.scale_y", "vp0.scale_z", "vp0.translate_x", "vp0.translate_y", "vp0.translate_z"},
    {"vp1.scale_x", "vp1.scale_y", "vp1.scale_z", "vp1.translate_x", "vp1.translate_y", "vp1.translate_z"},
};
static const char* const st_viewport_names[2][4] = {
    {"vp0.x", "vp0.y", "vp0.width", "vp0.height"},
    {"vp1.x", "vp1.y", "vp1.width", "vp1.height"},
};
static const char* const st_scissor_names[2][3] = {
    {"sc0.enable", "sc0.horizontal", "sc0.vertical"},
    {"sc1.enable", "sc1.horizontal", "sc1.vertical"},
};
static const char* const st_clear_names[4] = {"clear.r", "clear.g", "clear.b", "clear.a"};

static void st_command_snapshot(GpuChannel* ch, NvnStRec* rec) {
    for (unsigned i = 0; i < 2; i++) {
        const u32* vp = &ch->r3d[MAXWELL3D_VIEWPORT_TRANSFORM + MAXWELL3D_VIEWPORT_TRANSFORM_STRIDE * i];
        for (unsigned k = 0; k < 6; k++) nvn_st_add_state(rec, st_vp_names[i][k], vp[k]);
        const u32* view = &ch->r3d[MAXWELL3D_VIEWPORTS + MAXWELL3D_VIEWPORT_STRIDE * i];
        for (unsigned k = 0; k < 4; k++) nvn_st_add_state(rec, st_viewport_names[i][k], view[k]);
        const u32* sc = &ch->r3d[MAXWELL3D_SCISSOR_TEST + 4u * i];
        for (unsigned k = 0; k < 3; k++) nvn_st_add_state(rec, st_scissor_names[i][k], sc[k]);
    }
    for (unsigned k = 0; k < 4; k++) nvn_st_add_state(rec, st_clear_names[k], ch->r3d[MAXWELL3D_CLEAR_COLOR + k]);
}

typedef struct {
    NvnStProc initialize, finalize, add_command_memory, add_control_memory, begin, end;
    NvnStProc set_viewports, set_scissors, clear_color, barrier;
    NvnStProc free_bytes; /* optional: without it the command memory is refilled before each case */
} StCmdProcs;

/* One GPU job: both channels, the decoded ranges of the library recording and one record of each side. */
typedef struct {
    GpuChannel* lib;
    GpuChannel* host;
    const char* name;
    NvnStRange ranges[ST_MAX_RANGES];
    unsigned n_ranges;
    uint64_t host_handle;
    int window_origin_flip;
    const u32* init_words; /* the queue init stream: the channels run it once, before the first case */
    unsigned init_count;
    int channels_ready;
    int init_failed; /* the init stream ended inside a header: no case runs on these channels */
    NvnStRec* ref;
    NvnStRec* got;
    NvnStRec lib_rec, host_rec;
    int status; /* 0, or -1 when a side could not run */
    int done;
    PMutex lock;
    PCond cv;
} StCmdJob;

typedef struct {
    const NvnStArena* arena;
} StReadCtx;

#define ST_CALL(c, p, side, ...) \
    nvn_st_call((c), (side) ? &(p)->host : &(p)->lib, &(NvnStCall){.x = {__VA_ARGS__}})

static const void* st_guest_read(void* ctx, uint64_t addr, size_t size) {
    const StReadCtx* r = ctx;
    if (!nvn_st_arena_contains(r->arena, addr, size)) return NULL;
    return GP(addr);
}

static int st_procs_find(StCmdProcs* p) {
    if (!nvn_st_proc_find(&p->initialize, "nvnCommandBufferInitialize") ||
        !nvn_st_proc_find(&p->finalize, "nvnCommandBufferFinalize") ||
        !nvn_st_proc_find(&p->add_command_memory, "nvnCommandBufferAddCommandMemory") ||
        !nvn_st_proc_find(&p->add_control_memory, "nvnCommandBufferAddControlMemory") ||
        !nvn_st_proc_find(&p->begin, "nvnCommandBufferBeginRecording") ||
        !nvn_st_proc_find(&p->end, "nvnCommandBufferEndRecording") ||
        !nvn_st_proc_find(&p->set_viewports, "nvnCommandBufferSetViewports") ||
        !nvn_st_proc_find(&p->set_scissors, "nvnCommandBufferSetScissors") ||
        !nvn_st_proc_find(&p->clear_color, "nvnCommandBufferClearColor") ||
        !nvn_st_proc_find(&p->barrier, "nvnCommandBufferBarrier"))
        return 0;
    nvn_st_proc_find(&p->free_bytes, "nvnCommandBufferGetCommandMemoryFree");
    return 1;
}

static void st_gpu_fn(void* arg) {
    StCmdJob* job = arg;
    const Renderer* prev = g_renderer;
    g_renderer = &nvn_st_renderer;
    if (!job->channels_ready) {
        /* No recording is open yet, so the init stream's work calls reach the recorder as a no-op. */
        const int failed = gpu_channel_words(job->lib, job->init_words, job->init_count) |
                           gpu_channel_words(job->host, job->init_words, job->init_count);
        job->lib->r3d[ST_CLEAR_LAYER_COUNT_REG] = ST_CLEAR_LAYER_COUNT;
        job->host->r3d[ST_CLEAR_LAYER_COUNT_REG] = ST_CLEAR_LAYER_COUNT;
        job->channels_ready = 1;
        job->init_failed = failed != 0;
        if (failed) LOG("selftest: the queue init stream ends inside a header\n");
    }
    if (job->init_failed) job->status = -1;
    nvn_st_renderer_begin(job->ref, st_command_snapshot);
    for (unsigned i = 0; i < job->n_ranges && job->status == 0; i++) {
        const u32* words = (const u32*)GP(job->ranges[i].cpu_start);
        if (gpu_channel_words(job->lib, words, job->ranges[i].words)) {
            LOG("selftest: %s: library word range %u ends inside a header\n", job->name, i);
            job->status = -1;
        }
    }
    nvn_st_renderer_end();
    nvn_st_renderer_begin(job->got, st_command_snapshot);
    NvnReplayStats stats = {0};
    if (nvn_native_selftest_replay(job->host, job->host_handle, job->window_origin_flip, &stats)) {
        LOG("selftest: %s: the host replay failed\n", job->name);
        job->status = -1;
    }
    nvn_st_renderer_end();
    g_renderer = prev;
    pm_lock(&job->lock);
    job->done = 1;
    pc_broadcast(&job->cv);
    pm_unlock(&job->lock);
}

/* 0 when both sides ran, -1 when one side failed, 1 when the GPU thread did not finish in time. */
static int st_run_gpu(StCmdJob* job) {
    gpu_call(st_gpu_fn, job, NULL, 0);
    const u64 end = plat_ns() + (u64)ST_GPU_WAIT_MS * 1000000u;
    int timed_out = 0;
    pm_lock(&job->lock);
    while (!job->done) {
        const i64 left = (i64)(end - plat_ns());
        if (left <= 0) {
            timed_out = 1;
            break;
        }
        pc_wait(&job->cv, &job->lock, left);
    }
    const int status = job->status;
    pm_unlock(&job->lock);
    if (timed_out) return 1;
    return status ? -1 : 0;
}

static void st_skip(NvnStRun* run, const char* name, const char* args, int status, const NvnStDecodeFault* fault) {
    run->skipped++;
    switch (status) {
        case NVN_ST_DEC_REFUSED_OP:
            LOG("selftest: %s %s skipped, record op %u at %llx\n", name, args, fault->op,
                (unsigned long long)fault->addr);
            break;
        case NVN_ST_DEC_TOO_MANY_RANGES:
            LOG("selftest: %s %s skipped, more than %u ranges\n", name, args, ST_MAX_RANGES);
            break;
        case NVN_ST_DEC_TOO_MANY_JUMPS:
            LOG("selftest: %s %s skipped, a jump cycle at %llx\n", name, args, (unsigned long long)fault->addr);
            break;
        case NVN_ST_DEC_BAD_ENTRY:
            LOG("selftest: %s %s skipped, a bad record entry at %llx\n", name, args, (unsigned long long)fault->addr);
            break;
        default:
            LOG("selftest: %s %s skipped, the recording at %llx is unreadable\n", name, args,
                (unsigned long long)fault->addr);
            break;
    }
}

static void st_skip_all(NvnStRun* run, const char* why) {
    run->skipped += ST_CASE_COUNT;
    LOG("selftest: the command buffer cases are skipped, %s\n", why);
}

/* A full 0x1000 byte command chunk holds more than one case. Without the free word the chunk is replaced each case. */
static void st_refill(CPU* c, StCmdProcs* p, uint64_t lib_cb, uint64_t host_cb, uint64_t lib_pool, uint64_t host_pool) {
    uint64_t free_bytes = 0;
    if (p->free_bytes.lib.fn) {
        free_bytes = ST_CALL(c, &p->free_bytes, 0, lib_cb);
        if (free_bytes >= ST_CMD_REFILL_BYTES) return;
        LOG("selftest: command memory refilled, %llu bytes were free\n", (unsigned long long)free_bytes);
    }
    ST_CALL(c, &p->add_command_memory, 0, lib_cb, lib_pool, 0, ST_CMD_BYTES);
    ST_CALL(c, &p->add_command_memory, 1, host_cb, host_pool, 0, ST_CMD_BYTES);
}

static int st_setup_lists(CPU* c, StCmdProcs* p, NvnStResCache* cache, NvnStRun* run, int host, uint64_t cb,
                          uint64_t control, NvnStResPool* pool) {
    if (!nvn_st_res_pool_create(c, run, cache, host, pool, NVN_ST_RES_POOL_CPU_UNCACHED_GPU_CACHED, ST_CMD_BYTES))
        return 0;
    memset(GP(cb), 0, ST_CB_OBJ_BYTES);
    memset(GP(control), 0, ST_CONTROL_BYTES);
    if (!ST_CALL(c, &p->initialize, host, cb, nvn_st_device())) {
        LOG("selftest: a command buffer was not initialized (host=%d)\n", host);
        return 0;
    }
    ST_CALL(c, &p->add_command_memory, host, cb, pool->pool, 0, ST_CMD_BYTES);
    ST_CALL(c, &p->add_control_memory, host, cb, control, ST_CONTROL_BYTES);
    return 1;
}

/* Runs the calls of one side, then the calls of the other side, one recording each, and compares the two records. */
typedef void (*StScript)(CPU* c, const StCmdProcs* p, int host, uint64_t cb, uint64_t args, uint64_t param);

static void st_case(CPU* c, NvnStRun* run, StCmdProcs* p, StCmdJob* job, uint64_t lib_cb, uint64_t host_cb,
                    uint64_t lib_pool, uint64_t host_pool, uint64_t args, const char* name, const char* text,
                    StScript script, uint64_t param, int* stop) {
    st_refill(c, p, lib_cb, host_cb, lib_pool, host_pool);
    ST_CALL(c, &p->begin, 0, lib_cb);
    script(c, p, 0, lib_cb, args, param);
    const uint64_t lib_handle = ST_CALL(c, &p->end, 0, lib_cb);
    ST_CALL(c, &p->begin, 1, host_cb);
    script(c, p, 1, host_cb, args, param);
    const uint64_t host_handle = ST_CALL(c, &p->end, 1, host_cb);

    unsigned n = 0;
    NvnStDecodeFault fault;
    StReadCtx reader = {&run->arena};
    const int status =
        nvn_st_decode_recording(lib_handle, st_guest_read, (void*)&reader, job->ranges, ST_MAX_RANGES, &n, &fault);
    if (status != NVN_ST_DEC_OK) {
        st_skip(run, name, text, status, &fault);
        return;
    }
    for (unsigned i = 0; i < n; i++) {
        const uint64_t bytes = (uint64_t)job->ranges[i].words * 4u;
        if (nvn_st_arena_contains(&run->arena, job->ranges[i].cpu_start, bytes)) continue;
        run->skipped++;
        LOG("selftest: %s %s skipped, record range %u at %llx is outside the arena\n", name, text, i,
            (unsigned long long)job->ranges[i].cpu_start);
        return;
    }

    job->name = name;
    job->n_ranges = n;
    job->host_handle = host_handle;
    job->window_origin_flip = gr32(nvn_st_device() + NVN_ST_DEVICE_WINDOW_ORIGIN_OFFSET) == 1;
    job->ref = &job->lib_rec;
    job->got = &job->host_rec;
    memset(&job->lib_rec, 0, sizeof job->lib_rec);
    memset(&job->host_rec, 0, sizeof job->host_rec);
    job->status = 0;
    job->done = 0;
    const int gpu = st_run_gpu(job);
    if (gpu > 0) {
        /* The GPU thread may still read the job, the ranges and the records: leak them and stop the S/Y cases. */
        run->cases++;
        run->failed++;
        *stop = 1;
        LOG("selftest: %s %s failed, the GPU call did not finish within %u ms\n", name, text, ST_GPU_WAIT_MS);
        return;
    }
    if (gpu < 0) {
        run->skipped++;
        LOG("selftest: %s %s skipped, a side could not run the stream\n", name, text);
        return;
    }
    NvnStDiff diff;
    nvn_st_compare(&job->lib_rec, &job->host_rec, &diff);
    nvn_st_report(run, name, text, &job->lib_rec, &diff);
}

static void st_script_viewports(CPU* c, const StCmdProcs* p, int host, uint64_t cb, uint64_t args, uint64_t param) {
    (void)param;
    ST_CALL(c, &p->set_viewports, host, cb, 0, ST_VIEWPORTS, args + (host ? ST_ARG_VP_HOST : ST_ARG_VP_LIB));
    ST_CALL(c, &p->set_scissors, host, cb, 0, ST_SCISSORS, args + (host ? ST_ARG_SC_HOST : ST_ARG_SC_LIB));
    ST_CALL(c, &p->clear_color, host, cb, ST_CLEAR_INDEX, args + ST_ARG_COLOR, ST_CLEAR_MASK);
}

/* The library bits are in the low word of param, the host's (a mutation's) in the high word. */
static void st_script_barrier(CPU* c, const StCmdProcs* p, int host, uint64_t cb, uint64_t args, uint64_t param) {
    const uint32_t bits = host ? (uint32_t)(param >> 32) : (uint32_t)param;
    ST_CALL(c, &p->barrier, host, cb, bits);
    ST_CALL(c, &p->clear_color, host, cb, ST_CLEAR_INDEX, args + ST_ARG_COLOR, ST_CLEAR_MASK);
}

static void st_fill_s_args(uint64_t args, int mutate) {
    memcpy(GP(args + ST_ARG_VP_LIB), st_viewports, sizeof st_viewports);
    memcpy(GP(args + ST_ARG_VP_HOST), st_viewports, sizeof st_viewports);
    memcpy(GP(args + ST_ARG_SC_LIB), st_scissors, sizeof st_scissors);
    memcpy(GP(args + ST_ARG_SC_HOST), st_scissors, sizeof st_scissors);
    if (mutate) {
        float x;
        memcpy(&x, GP(args + ST_ARG_VP_HOST + ST_MUTATE_VIEWPORT_WORD * sizeof(float)), sizeof x);
        x += ST_MUTATE_VIEWPORT_ADD;
        memcpy(GP(args + ST_ARG_VP_HOST + ST_MUTATE_VIEWPORT_WORD * sizeof(float)), &x, sizeof x);
    }
}

void nvn_selftest_command_cases(CPU* c, NvnStRun* run) {
    StCmdProcs procs = {0};
    if (!g_renderer_up) {
        st_skip_all(run, "the renderer is not up, so no clear reaches the recorder");
        return;
    }
    if (!st_procs_find(&procs)) {
        st_skip_all(run, "a command buffer function was not resolved, or the host has no handler");
        return;
    }
    const char* mutate = g_settings.nvn_selftest_mutation;
    if (mutate[0]) LOG("selftest: mutation %s, the host side gets a changed argument for it\n", mutate);
    if (!procs.free_bytes.lib.fn)
        LOG("selftest: no nvnCommandBufferGetCommandMemoryFree, the command memory is refilled before each case\n");

    StCmdJob* job = calloc(1, sizeof *job);
    if (!job) {
        st_skip_all(run, "out of memory for the GPU job");
        return;
    }
    pm_init(&job->lock);
    pc_init(&job->cv);
    NvnStResCache cache = {0};

    const uint64_t lib_cb = nvn_st_arena_alloc(&run->arena, ST_CB_OBJ_BYTES, 16);
    const uint64_t host_cb = nvn_st_arena_alloc(&run->arena, ST_CB_OBJ_BYTES, 16);
    const uint64_t lib_control = nvn_st_arena_alloc(&run->arena, ST_CONTROL_BYTES, 0x1000);
    const uint64_t host_control = nvn_st_arena_alloc(&run->arena, ST_CONTROL_BYTES, 0x1000);
    const uint64_t args = nvn_st_arena_alloc(&run->arena, ST_ARG_BYTES, 16);
    if (!lib_cb || !host_cb || !lib_control || !host_control || !args) {
        st_skip_all(run, "the arena is full");
        pm_destroy(&job->lock);
        free(job);
        return;
    }
    NvnStResPool lib_pool = {0}, host_pool = {0};
    const int lib_ok = st_setup_lists(c, &procs, &cache, run, 0, lib_cb, lib_control, &lib_pool);
    const int host_ok = st_setup_lists(c, &procs, &cache, run, 1, host_cb, host_control, &host_pool);
    if (!lib_ok || !host_ok) {
        st_skip_all(run, "a command memory pool or a command buffer was not made");
        if (lib_pool.pool) nvn_st_res_pool_destroy(c, &cache, 0, &lib_pool);
        if (host_pool.pool) nvn_st_res_pool_destroy(c, &cache, 1, &host_pool);
        pm_destroy(&job->lock);
        free(job);
        return;
    }

    uint32_t ids[2];
    ids[0] = syncpt_alloc();
    ids[1] = syncpt_alloc();
    job->lib = gpu_channel_open(ids[0]);
    job->host = gpu_channel_open(ids[1]);
    if (!job->lib || !job->host) {
        if (job->lib) gpu_channel_close(ids[0]);
        if (job->host) gpu_channel_close(ids[1]);
        syncpt_free(ids[0]);
        syncpt_free(ids[1]);
        st_skip_all(run, "a private channel was not made");
        nvn_st_res_pool_destroy(c, &cache, 0, &lib_pool);
        nvn_st_res_pool_destroy(c, &cache, 1, &host_pool);
        pm_destroy(&job->lock);
        free(job);
        return;
    }

    uint32_t* init_words = NULL;
    uint32_t init_count = 0;
    CPU saved = *c;
    const uint64_t device = nvn_st_device();
    const int init_ok = nvn_native_selftest_init_words(
        c, gr32(device + NVN_ST_DEVICE_WINDOW_ORIGIN_OFFSET), gr32(device + ST_DEVICE_DEPTH_MODE_OFFSET), &init_words,
        &init_count);
    *c = saved;
    if (!init_ok) {
        st_skip_all(run, "the queue init stream was not built, so the clear macro has no code");
        ST_CALL(c, &procs.finalize, 0, lib_cb);
        ST_CALL(c, &procs.finalize, 1, host_cb);
        nvn_st_res_pool_destroy(c, &cache, 0, &lib_pool);
        nvn_st_res_pool_destroy(c, &cache, 1, &host_pool);
        gpu_channel_close(ids[0]);
        gpu_channel_close(ids[1]);
        syncpt_free(ids[0]);
        syncpt_free(ids[1]);
        pm_destroy(&job->lock);
        free(job);
        return;
    }
    job->init_words = init_words;
    job->init_count = init_count;

    memcpy(GP(args + ST_ARG_COLOR), st_clear, sizeof st_clear);
    st_fill_s_args(args, mutate[0] && !strcmp(mutate, "nvnCommandBufferSetViewports"));
    int stop = 0;
    st_case(c, run, &procs, job, lib_cb, host_cb, lib_pool.pool, host_pool.pool, args, "nvnCommandBufferSetViewports",
            "viewports=0,2 scissors=0,2 clear=0", st_script_viewports, 0, &stop);
    for (unsigned k = 0; k < ST_BARRIERS && !stop; k++) {
        const uint32_t bits = 1u << k;
        const uint32_t host_bits = mutate[0] && !strcmp(mutate, "nvnCommandBufferBarrier") ? bits ^ 1u : bits;
        char text[32];
        snprintf(text, sizeof text, "bits=0x%x", bits);
        st_case(c, run, &procs, job, lib_cb, host_cb, lib_pool.pool, host_pool.pool, args, "nvnCommandBufferBarrier",
                text, st_script_barrier, (uint64_t)bits | ((uint64_t)host_bits << 32), &stop);
    }

    if (stop) {
        LOG("selftest: the command cases stopped, the channels and the job stay (the GPU thread may still use them)\n");
        return;
    }
    ST_CALL(c, &procs.finalize, 0, lib_cb);
    ST_CALL(c, &procs.finalize, 1, host_cb);
    nvn_st_res_pool_destroy(c, &cache, 0, &lib_pool);
    nvn_st_res_pool_destroy(c, &cache, 1, &host_pool);
    gpu_channel_close(ids[0]);
    gpu_channel_close(ids[1]);
    syncpt_free(ids[0]);
    syncpt_free(ids[1]);
    free(init_words);
    pm_destroy(&job->lock);
    free(job);
}
