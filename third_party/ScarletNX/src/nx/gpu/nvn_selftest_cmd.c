/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The command buffer cases of the NVN selftest (docs/nvn-design.md, "The selftest") for step 4-u3-3: the same calls go
   to the library's command buffer and to the host's, the library recording is decoded into push buffer words, and both
   sides run on a private channel of their own in one GPU call. The records compare kinds S (the registers at the clear)
   and Y (the order of the sync operations). */
#include "nx/gpu/nvn_selftest_int.h"
#include "nx/gpu/nvn_selftest_dec.h"
#include "nx/gpu/nvn_selftest_rec.h"
#include "nx/gpu/nvn_selftest_regdiff.h"
#include "nx/gpu/nvn_native.h"
#include "nx/gpu/gpu.h"
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
#define ST_STATE_CASES 29u
#define ST_CASE_COUNT (1u + ST_BARRIERS + ST_STATE_CASES)

/* The work calls of one side: the registers at the clear. A later call means that the case cannot be compared. */
#define ST_MAX_WORKS 2u

/* The device word that SetRenderTargets compares with 0x08c8b771 (nvn_selftest.c's DEVICE_DEPTH_MODE_OFFSET). */
#define ST_DEVICE_DEPTH_MODE_OFFSET 0x1a04u

/* ClearColor is macro 1: it writes CLEAR_SURFACE once per layer of the target, at 0x206 + 16 per target. A real
   render target has one layer; the cases do not call SetRenderTargets, so the fixture sets that count. */
#define ST_CLEAR_LAYER_COUNT_REG 0x206u
#define ST_CLEAR_LAYER_COUNT 1u

/* The argument block of the cases, in the arena: the same values for the library, a changed copy for a mutation. Each
   region is 16-byte aligned. */
#define ST_ARG_VP_LIB 0x000u
#define ST_ARG_VP_HOST 0x020u
#define ST_ARG_SC_LIB 0x040u
#define ST_ARG_SC_HOST 0x060u
#define ST_ARG_COLOR 0x080u
#define ST_ARG_ARRAY_LIB 0x090u /* the floats of an array argument: a blend color or a depth range pair */
#define ST_ARG_ARRAY_HOST 0x0A0u
#define ST_ARG_STATE_LIB 0x0B0u /* one state object for each side of the Bind cases */
#define ST_ARG_STATE_HOST 0x2B0u
#define ST_ARG_BYTES 0x4B0u

#define ST_CLEAR_INDEX 0u
#define ST_MUTATE_VIEWPORT_WORD 0u /* the x of viewport 0 */
#define ST_MUTATE_VIEWPORT_ADD 1.0f

static const float st_viewports[8] = {0.0f, 0.0f, 1280.0f, 720.0f, 16.0f, 32.0f, 640.0f, 360.0f};
static const int32_t st_scissors[8] = {0, 0, 1280, 720, 8, 8, 100, 50};
static const float st_clear[4] = {0.25f, 0.5f, 0.75f, 1.0f};

typedef struct {
    NvnStProc initialize, finalize, add_command_memory, add_control_memory, begin, end;
    NvnStProc set_viewports, set_scissors, clear_color, barrier;
    NvnStProc free_bytes; /* optional: without it the command memory is refilled before each case */
} StCmdProcs;

/* One GPU job: both channels, the decoded ranges of the library recording, one record of each side, and the register
   copies of each work call. The copies are large, so they live in this heap object (the job is calloc'd). */
typedef struct {
    GpuChannel* lib;
    GpuChannel* host;
    const char* name;
    NvnStRange ranges[ST_MAX_RANGES];
    unsigned n_ranges;
    uint64_t host_handle;
    int window_origin_flip;
    const u32* init_words; /* the queue init stream: the channels run it once, before the first case */
    GpuChannel* base;       /* the channel state after the init stream, which each case starts from */
    unsigned init_count;
    int channels_ready;
    int init_failed; /* the init stream ended inside a header: no case runs on these channels */
    u32 lib_regs[ST_MAX_WORKS][MAXWELL3D_NUM_REGS];
    u32 host_regs[ST_MAX_WORKS][MAXWELL3D_NUM_REGS];
    unsigned lib_works, host_works;
    int work_overflow; /* a side had a third work call */
    NvnStRec* ref;
    NvnStRec* got;
    NvnStRec lib_rec, host_rec;
    int status; /* 0, or -1 when a side could not run */
    int done;
    PMutex lock;
    PCond cv;
} StCmdJob;

/* The recorder calls the snapshot on the GPU thread, and one job runs at a time on that thread (one guest thread runs
   the selftest). */
static StCmdJob* st_snapshot_job;

/* The snapshot of one work call: copy the 3D registers (not the macro area) of the side, for the diff after both
   sides ran. It adds no field, because a register that both sides left as the base is not a difference. */
static void st_snapshot_regs(GpuChannel* ch, NvnStRec* rec) {
    (void)rec;
    StCmdJob* job = st_snapshot_job;
    if (!job) return;
    const int lib = ch == job->lib;
    if (!lib && ch != job->host) return;
    unsigned* count = lib ? &job->lib_works : &job->host_works;
    if (*count >= ST_MAX_WORKS) {
        if (!job->work_overflow)
            LOG("selftest: %s: more than %u work calls on the %s side\n", job->name, ST_MAX_WORKS,
                lib ? "library" : "host");
        job->work_overflow = 1;
        return;
    }
    u32* const dst = lib ? job->lib_regs[*count] : job->host_regs[*count];
    memcpy(dst, ch->r3d, MAXWELL3D_NUM_REGS * sizeof dst[0]);
    (*count)++;
}

/* Adds one S field for each register that a side left different from the base, in register order. The fields go to both
   records, with the value of each side, so a mutation shows as a difference and an equal write does not show. */
static void st_add_regdiff(StCmdJob* job) {
    if (!job->base) return;
    if (job->work_overflow) {
        /* The copies are incomplete, so the record cannot be compared: a full table is a fail as well. */
        job->lib_rec.overflow |= NVN_ST_S;
        job->host_rec.overflow |= NVN_ST_S;
        return;
    }
    if (job->lib_works != job->host_works) {
        LOG("selftest: %s: %u library work calls, %u host work calls\n", job->name, job->lib_works,
            job->host_works);
        return;
    }
    u32 regs[NVN_ST_MAX_FIELDS];
    for (unsigned w = 0; w < job->lib_works; w++) {
        int more = 0;
        const unsigned n = nvn_st_regdiff(job->base->r3d, job->lib_regs[w], job->host_regs[w], MAXWELL3D_NUM_REGS,
                                          regs, NVN_ST_MAX_FIELDS, &more);
        for (unsigned i = 0; i < n; i++) {
            const unsigned r = regs[i];
            const char* const name = nvn_st_reg_name(r);
            nvn_st_add_state(&job->lib_rec, name, job->lib_regs[w][r]);
            nvn_st_add_state(&job->host_rec, name, job->host_regs[w][r]);
        }
        if (more) LOG("selftest: %s: more than %u changed registers\n", job->name, NVN_ST_MAX_FIELDS);
    }
}

/* The id and the heap buffers belong to the channel: the base shares neither. */
static void st_channel_reset(GpuChannel* ch, const GpuChannel* base) {
    const u32 id = ch->id;
    u32* const inline_indices = ch->inline_indices;
    const u32 inline_cap = ch->inline_cap;
    u8* const i2m_buffer = ch->i2m.inner_buffer;
    const u32 i2m_capacity = ch->i2m.capacity;
    *ch = *base;
    ch->id = id;
    ch->inline_indices = inline_indices;
    ch->inline_cap = inline_cap;
    ch->inline_n = 0;
    ch->i2m.inner_buffer = i2m_buffer;
    ch->i2m.capacity = i2m_capacity;
    ch->i2m.copy_size = 0;
    ch->i2m.write_offset = 0;
}

typedef struct {
    const NvnStArena* arena;
} StReadCtx;

#define ST_CALL(c, p, side, ...) \
    nvn_st_call((c), (side) ? &(p)->host : &(p)->lib, &(NvnStCall){.x = {__VA_ARGS__}})

static uint32_t st_f2b(float v) {
    uint32_t b;
    memcpy(&b, &v, sizeof b);
    return b;
}

static float st_b2f(uint32_t b) {
    float v;
    memcpy(&v, &b, sizeof v);
    return v;
}

/* A call with integer and float arguments: ST_CALL passes x only, and the float values are bit patterns in s. */
static void st_call_proc(CPU* c, const NvnStProc* p, int host, const NvnStCall* a) {
    nvn_st_call(c, host ? &p->host : &p->lib, a);
}

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
        if (!failed) {
            job->base = malloc(sizeof *job->base);
            if (!job->base) {
                LOG("selftest: out of memory for the channel base copy\n");
                job->init_failed = 1;
            } else {
                memcpy(job->base, job->lib, sizeof *job->base);
            }
        }
    }
    if (!job->init_failed) {
        st_channel_reset(job->lib, job->base);
        st_channel_reset(job->host, job->base);
    }
    if (job->init_failed) job->status = -1;
    job->lib_works = 0;
    job->host_works = 0;
    job->work_overflow = 0;
    st_snapshot_job = job;
    nvn_st_renderer_begin(job->ref, st_snapshot_regs);
    for (unsigned i = 0; i < job->n_ranges && job->status == 0; i++) {
        const u32* words = (const u32*)GP(job->ranges[i].cpu_start);
        if (gpu_channel_words(job->lib, words, job->ranges[i].words)) {
            LOG("selftest: %s: library word range %u ends inside a header\n", job->name, i);
            job->status = -1;
        }
    }
    nvn_st_renderer_end();
    nvn_st_renderer_begin(job->got, st_snapshot_regs);
    NvnReplayStats stats = {0};
    if (nvn_native_selftest_replay(job->host, job->host_handle, job->window_origin_flip, &stats)) {
        LOG("selftest: %s: the host replay failed\n", job->name);
        job->status = -1;
    }
    nvn_st_renderer_end();
    st_snapshot_job = NULL;
    st_add_regdiff(job);
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

/* The work point of every case: the clear that ends the calls of the script. */
static void st_clear_work(CPU* c, const StCmdProcs* p, int host, uint64_t cb, uint64_t args) {
    ST_CALL(c, &p->clear_color, host, cb, ST_CLEAR_INDEX, args + ST_ARG_COLOR, ST_CLEAR_MASK);
}

/* The mutation of one case. The rule is the one that the case's table row names. */
enum {
    ST_MUT_NONE,   /* the argument has no value to change (a NULL pointer) */
    ST_MUT_FLOAT,  /* the first float argument + 1.0f */
    ST_MUT_INT,    /* the first integer value argument XOR 1 */
    ST_MUT_ARRAY,  /* the first float of an array argument + 1.0f */
    ST_MUT_SETTER, /* the first setter value of an object argument XOR 1 */
};

/* Runs the calls of one side, then the calls of the other side, one recording each, and compares the two records.
   A script returns 1, or 0 when a function of its case was not resolved: the case is then skipped, not failed. */
typedef int (*StScript)(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb, uint64_t args,
                        uint64_t param, unsigned rule);

static void st_case(CPU* c, NvnStRun* run, StCmdProcs* p, StCmdJob* job, uint64_t lib_cb, uint64_t host_cb,
                    uint64_t lib_pool, uint64_t host_pool, uint64_t args, const char* name, const char* text,
                    StScript script, const NvnStProc* fn, uint64_t param, unsigned rule, int* stop) {
    st_refill(c, p, lib_cb, host_cb, lib_pool, host_pool);
    ST_CALL(c, &p->begin, 0, lib_cb);
    const int lib_script = script(c, p, fn, 0, lib_cb, args, param, rule);
    const uint64_t lib_handle = ST_CALL(c, &p->end, 0, lib_cb);
    ST_CALL(c, &p->begin, 1, host_cb);
    const int host_script = script(c, p, fn, 1, host_cb, args, param, rule);
    const uint64_t host_handle = ST_CALL(c, &p->end, 1, host_cb);
    if (!lib_script || !host_script) {
        run->skipped++;
        LOG("selftest: %s skipped, not resolved\n", name);
        return;
    }

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

static int st_script_viewports(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                               uint64_t args, uint64_t param, unsigned rule) {
    (void)fn;
    (void)param;
    (void)rule;
    ST_CALL(c, &p->set_viewports, host, cb, 0, ST_VIEWPORTS, args + (host ? ST_ARG_VP_HOST : ST_ARG_VP_LIB));
    ST_CALL(c, &p->set_scissors, host, cb, 0, ST_SCISSORS, args + (host ? ST_ARG_SC_HOST : ST_ARG_SC_LIB));
    st_clear_work(c, p, host, cb, args);
    return 1;
}

/* The library bits are in the low word of param, the host's (a mutation's) in the high word. */
static int st_script_barrier(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                             uint64_t args, uint64_t param, unsigned rule) {
    (void)fn;
    (void)rule;
    const uint32_t bits = host ? (uint32_t)(param >> 32) : (uint32_t)param;
    ST_CALL(c, &p->barrier, host, cb, bits);
    st_clear_work(c, p, host, cb, args);
    return 1;
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

/* One float argument. param holds its bit pattern. */
static int st_script_float_arg(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                               uint64_t args, uint64_t param, unsigned rule) {
    uint32_t bits = (uint32_t)param;
    if (host && rule == ST_MUT_FLOAT) bits = st_f2b(st_b2f(bits) + 1.0f);
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb}, .s = {bits}, .n_s = 1});
    st_clear_work(c, p, host, cb, args);
    return 1;
}

/* One integer argument. param holds its value. */
static int st_script_int_arg(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb, uint64_t args,
                             uint64_t param, unsigned rule) {
    uint32_t value = (uint32_t)param;
    if (host && rule == ST_MUT_INT) value ^= 1u;
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, value}});
    st_clear_work(c, p, host, cb, args);
    return 1;
}

/* SetStencilMask, SetStencilRef and SetStencilValueMask: face in the low word of param, value in the high word. */
static int st_script_stencil(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb, uint64_t args,
                             uint64_t param, unsigned rule) {
    uint32_t face = (uint32_t)param;
    const uint32_t value = (uint32_t)(param >> 32);
    if (host && rule == ST_MUT_INT) face ^= 1u;
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, face, value}});
    st_clear_work(c, p, host, cb, args);
    return 1;
}

static int st_script_blend_color(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                 uint64_t args, uint64_t param, unsigned rule) {
    (void)param;
    float v[4] = {0.1f, 0.2f, 0.3f, 0.4f};
    if (host && rule == ST_MUT_ARRAY) v[0] += 1.0f;
    const uint64_t addr = args + (host ? ST_ARG_ARRAY_HOST : ST_ARG_ARRAY_LIB);
    memcpy(GP(addr), v, sizeof v);
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, addr}});
    st_clear_work(c, p, host, cb, args);
    return 1;
}

/* SetDepthRange: the near value in the low word of param, the far value in the high word (bit patterns). */
static int st_script_depth_range(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                 uint64_t args, uint64_t param, unsigned rule) {
    uint32_t near_bits = (uint32_t)param;
    const uint32_t far_bits = (uint32_t)(param >> 32);
    if (host && rule == ST_MUT_FLOAT) near_bits = st_f2b(st_b2f(near_bits) + 1.0f);
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb}, .s = {near_bits, far_bits}, .n_s = 2});
    st_clear_work(c, p, host, cb, args);
    return 1;
}

static int st_script_depth_bounds(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                  uint64_t args, uint64_t param, unsigned rule) {
    (void)param;
    NvnStCall a = {.x = {cb, 1}, .s = {st_f2b(0.25f), st_f2b(0.75f)}, .n_s = 2};
    if (host && rule == ST_MUT_FLOAT) a.s[0] = st_f2b(0.25f + 1.0f);
    st_call_proc(c, fn, host, &a);
    st_clear_work(c, p, host, cb, args);
    return 1;
}

/* SetDepthRanges: first 0, count 2, and two pairs of floats in the array region. */
static int st_script_depth_ranges(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                  uint64_t args, uint64_t param, unsigned rule) {
    (void)param;
    float v[4] = {0.1f, 0.9f, 0.2f, 0.8f};
    if (host && rule == ST_MUT_ARRAY) v[0] += 1.0f;
    const uint64_t addr = args + (host ? ST_ARG_ARRAY_HOST : ST_ARG_ARRAY_LIB);
    memcpy(GP(addr), v, sizeof v);
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, 0, 2, addr}});
    st_clear_work(c, p, host, cb, args);
    return 1;
}

static int st_script_polygon_offset(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                    uint64_t args, uint64_t param, unsigned rule) {
    (void)param;
    NvnStCall a = {.x = {cb}, .s = {st_f2b(1.0f), st_f2b(2.0f), st_f2b(0.5f)}, .n_s = 3};
    if (host && rule == ST_MUT_FLOAT) a.s[0] = st_f2b(1.0f + 1.0f);
    st_call_proc(c, fn, host, &a);
    st_clear_work(c, p, host, cb, args);
    return 1;
}

static int st_script_viewport(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb, uint64_t args,
                              uint64_t param, unsigned rule) {
    (void)param;
    uint32_t x = 16u;
    if (host && rule == ST_MUT_INT) x ^= 1u;
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, x, 32u, 640u, 360u}});
    st_clear_work(c, p, host, cb, args);
    return 1;
}

static int st_script_scissor(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb, uint64_t args,
                             uint64_t param, unsigned rule) {
    (void)param;
    uint32_t x = 8u;
    if (host && rule == ST_MUT_INT) x ^= 1u;
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, x, 8u, 100u, 50u}});
    st_clear_work(c, p, host, cb, args);
    return 1;
}

/* A Bind case: each side builds its own state object, then binds it. The host object layout may differ from the
   library's, so the two sides never share an object. param is the NVN_ST_STATE_* kind. */
static int st_script_bind_state(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                uint64_t args, uint64_t param, unsigned rule) {
    const uint64_t obj = args + (host ? ST_ARG_STATE_HOST : ST_ARG_STATE_LIB);
    if (!nvn_st_state_build(c, (unsigned)param, host, obj, host && rule == ST_MUT_SETTER ? 1u : 0u)) return 0;
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, obj}});
    st_clear_work(c, p, host, cb, args);
    return 1;
}

/* SetStencilCullCriteria: func 8, ref 0, mask 0xFF. */
static int st_script_stencil_cull(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                  uint64_t args, uint64_t param, unsigned rule) {
    (void)param;
    uint32_t func = 8u;
    if (host && rule == ST_MUT_INT) func ^= 1u;
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, func, 0u, 0xFFu}});
    st_clear_work(c, p, host, cb, args);
    return 1;
}

/* SetColorReductionThresholds: format class 0, then the values 1 and 2. */
static int st_script_thresholds(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                uint64_t args, uint64_t param, unsigned rule) {
    (void)param;
    uint32_t cls = 0u;
    if (host && rule == ST_MUT_INT) cls ^= 1u;
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, cls, 1u, 2u}});
    st_clear_work(c, p, host, cb, args);
    return 1;
}

/* Two integer arguments: the first in the low word of param, the second in the high word. */
static int st_script_two_ints(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb, uint64_t args,
                              uint64_t param, unsigned rule) {
    uint32_t first = (uint32_t)param;
    const uint32_t second = (uint32_t)(param >> 32);
    if (host && rule == ST_MUT_INT) first ^= 1u;
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, first, second}});
    st_clear_work(c, p, host, cb, args);
    return 1;
}

/* BindCoverageModulationTable: the NULL (disable) form. A table is a refusal of the host and out of scope, so a
   mutation leaves the argument NULL. */
static int st_script_coverage_table(CPU* c, const StCmdProcs* p, const NvnStProc* fn, int host, uint64_t cb,
                                    uint64_t args, uint64_t param, unsigned rule) {
    (void)param;
    (void)rule;
    st_call_proc(c, fn, host, &(NvnStCall){.x = {cb, 0}});
    st_clear_work(c, p, host, cb, args);
    return 1;
}

/* The 29 cases of decision 3. `rule` is the mutation rule of the row. The no-op rows (COMMAND_NO_OP_HANDLER:
   SetSampleMask, SetStencilCullCriteria, SetColorReductionEnable, SetColorReductionThresholds,
   SetSubpixelPrecisionBias, SetTiledCacheAction, SetTiledCacheTileSize) may fail on purpose. A mutation changes
   nothing on the host side for them, because the library writes registers that the host does not. */
static const struct {
    const char* name;
    const char* text;
    StScript script;
    uint64_t param;
    unsigned rule;
} st_state_cases[] = {
    {"nvnCommandBufferSetAlphaRef", "value=0.5", st_script_float_arg, 0x3F000000u, ST_MUT_FLOAT},
    {"nvnCommandBufferSetBlendColor", "color=0.1,0.2,0.3,0.4", st_script_blend_color, 0, ST_MUT_ARRAY},
    {"nvnCommandBufferSetDepthBounds", "enable=1 near=0.25 far=0.75", st_script_depth_bounds, 0, ST_MUT_FLOAT},
    {"nvnCommandBufferSetDepthClamp", "enable=1", st_script_int_arg, 1, ST_MUT_INT},
    {"nvnCommandBufferSetDepthRange", "near=0.1 far=0.9", st_script_depth_range,
     (uint64_t)0x3DCCCCCDu | ((uint64_t)0x3F666666u << 32), ST_MUT_FLOAT},
    {"nvnCommandBufferSetDepthRanges", "first=0 count=2 ranges=0.1,0.9,0.2,0.8", st_script_depth_ranges, 0,
     ST_MUT_ARRAY},
    {"nvnCommandBufferSetLineWidth", "width=2.0", st_script_float_arg, 0x40000000u, ST_MUT_FLOAT},
    {"nvnCommandBufferSetPolygonOffsetClamp", "factor=1.0 units=2.0 clamp=0.5", st_script_polygon_offset, 0,
     ST_MUT_FLOAT},
    {"nvnCommandBufferSetRasterizerDiscard", "discard=1", st_script_int_arg, 1, ST_MUT_INT},
    {"nvnCommandBufferSetStencilMask", "face=3 value=0x5a", st_script_stencil,
     (uint64_t)3u | ((uint64_t)0x5Au << 32), ST_MUT_INT},
    {"nvnCommandBufferSetStencilRef", "face=3 value=0x5a", st_script_stencil,
     (uint64_t)3u | ((uint64_t)0x5Au << 32), ST_MUT_INT},
    {"nvnCommandBufferSetStencilValueMask", "face=3 value=0x5a", st_script_stencil,
     (uint64_t)3u | ((uint64_t)0x5Au << 32), ST_MUT_INT},
    {"nvnCommandBufferSetViewport", "x=16 y=32 w=640 h=360", st_script_viewport, 0, ST_MUT_INT},
    {"nvnCommandBufferSetScissor", "x=8 y=8 w=100 h=50", st_script_scissor, 0, ST_MUT_INT},
    {"nvnCommandBufferBindBlendState", "state=blend func=8,4,10,6", st_script_bind_state, NVN_ST_STATE_BLEND,
     ST_MUT_SETTER},
    {"nvnCommandBufferBindChannelMaskState", "state=channel mask target=3 0,1,0,1", st_script_bind_state,
     NVN_ST_STATE_CHANNEL_MASK, ST_MUT_SETTER},
    {"nvnCommandBufferBindColorState", "state=color logicOp=3", st_script_bind_state, NVN_ST_STATE_COLOR,
     ST_MUT_SETTER},
    {"nvnCommandBufferBindDepthStencilState", "state=depth stencil depthFunc=2", st_script_bind_state,
     NVN_ST_STATE_DEPTH_STENCIL, ST_MUT_SETTER},
    {"nvnCommandBufferBindMultisampleState", "state=multisample samples=4", st_script_bind_state,
     NVN_ST_STATE_MULTISAMPLE, ST_MUT_SETTER},
    {"nvnCommandBufferBindPolygonState", "state=polygon cullFace=2", st_script_bind_state, NVN_ST_STATE_POLYGON,
     ST_MUT_SETTER},
    {"nvnCommandBufferSetSampleMask", "mask=0x1", st_script_int_arg, 1, ST_MUT_INT},
    {"nvnCommandBufferSetStencilCullCriteria", "func=8 ref=0 mask=0xff", st_script_stencil_cull, 0, ST_MUT_INT},
    {"nvnCommandBufferSetColorReductionEnable", "enable=1", st_script_int_arg, 1, ST_MUT_INT},
    {"nvnCommandBufferSetColorReductionThresholds", "class=0 t0=1 t1=2", st_script_thresholds, 0, ST_MUT_INT},
    {"nvnCommandBufferSetSubpixelPrecisionBias", "x=2 y=3", st_script_two_ints,
     (uint64_t)2u | ((uint64_t)3u << 32), ST_MUT_INT},
    {"nvnCommandBufferSetTiledCacheAction", "action=1", st_script_int_arg, 1, ST_MUT_INT},
    {"nvnCommandBufferSetTiledCacheTileSize", "width=64 height=64", st_script_two_ints,
     (uint64_t)64u | ((uint64_t)64u << 32), ST_MUT_INT},
    {"nvnCommandBufferSetConservativeRasterEnable", "enable=0", st_script_int_arg, 0, ST_MUT_INT},
    {"nvnCommandBufferBindCoverageModulationTable", "table=null", st_script_coverage_table, 0, ST_MUT_NONE},
};

_Static_assert(sizeof st_state_cases / sizeof st_state_cases[0] == ST_STATE_CASES, "the state case count");

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
            "viewports=0,2 scissors=0,2 clear=0", st_script_viewports, NULL, 0, ST_MUT_NONE, &stop);
    for (unsigned k = 0; k < ST_BARRIERS && !stop; k++) {
        const uint32_t bits = 1u << k;
        const uint32_t host_bits = mutate[0] && !strcmp(mutate, "nvnCommandBufferBarrier") ? bits ^ 1u : bits;
        char text[32];
        snprintf(text, sizeof text, "bits=0x%x", bits);
        st_case(c, run, &procs, job, lib_cb, host_cb, lib_pool.pool, host_pool.pool, args, "nvnCommandBufferBarrier",
                text, st_script_barrier, NULL, (uint64_t)bits | ((uint64_t)host_bits << 32), ST_MUT_NONE, &stop);
    }
    for (unsigned i = 0; i < ST_STATE_CASES && !stop; i++) {
        const char* const name = st_state_cases[i].name;
        NvnStProc fn = {0};
        if (!nvn_st_proc_find(&fn, name)) {
            run->skipped++;
            LOG("selftest: %s skipped, not resolved\n", name);
            continue;
        }
        const unsigned rule =
            mutate[0] && !strcmp(mutate, name) ? st_state_cases[i].rule : (unsigned)ST_MUT_NONE;
        st_case(c, run, &procs, job, lib_cb, host_cb, lib_pool.pool, host_pool.pool, args, name,
                st_state_cases[i].text, st_state_cases[i].script, &fn, st_state_cases[i].param, rule, &stop);
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
    free(job->base);
    pm_destroy(&job->lock);
    free(job);
}
