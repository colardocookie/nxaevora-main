/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The NVN selftest (docs/nvn-design.md, "The selftest"). The first cases are kinds R and M of the object layer:
   nvnDeviceGetInteger (the word the call writes, kind M), and the texture builder storage queries and getters (kind R).
   The calls run on the guest thread that triggers the run, with the CPU context saved and restored around each call.
   Nothing here writes an object of the game: the builders are in the arena of the selftest. */
#include "nx/gpu/nvn_selftest.h"
#include "nx/gpu/nvn_native.h"
#include "nx/gpu/nvn_selftest_cmp.h"
#include "nx/gpu/nvn_selftest_mem.h"
#include "nx/gpu/nvn_trace.h"
#include "nx/memory/mem.h"
#include "nx/runtime/platform/log.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define ARENA_BYTES 0x10000u
#define BUILDER_BYTES 0x200u /* more than the library's builder (a reserved block) and the host's record */
#define NAME_MAX_LEN 64
#define OUT_GUARD_BYTES 16u /* untouched bytes before and after the output word: an extra write shows as kind M */
#define OUT_WORD_BYTES 4u
#define OUT_FILL 0xA5
#define DEVICE_TEXTURE_WORD_OFFSET 0x1b18u     /* the buffer kind word of the device (nvn_params.h) */
#define DEVICE_COMPRESSION_BYTE_OFFSET 0x1b1cu /* the compression switch of the device, source unknown */

#define DEVICE_CLIP_LIMIT_OFFSET 0x18u      /* the limit that SetRenderTargets applies to SURFACE_CLIP (d856414), source unknown */
#define DEVICE_WINDOW_ORIGIN_OFFSET 0x19d8u /* read by SetViewports (d84acb0) */
#define DEVICE_DEPTH_MODE_OFFSET 0x1a04u    /* compared with 0x08c8b771 by SetRenderTargets (d856218) */

static uint64_t g_device;
static int g_ran;

void nvn_selftest_note_device(uint64_t device) { g_device = device; }

/* One side of a comparison: the function, its address, and the name for the report. */
typedef struct {
    GuestFn fn;
    u64 entry;
} Callee;

typedef struct {
    Callee lib, host;
    const char* name;
} Proc;

static int proc_find(Proc* p, const char* name) {
    p->name = name;
    if (!nvn_trace_library(name, &p->lib.fn, &p->lib.entry)) return 0;
    p->host.fn = nvn_native_handler(name);
    p->host.entry = p->lib.entry;
    return p->host.fn != NULL;
}

/* x0..x4 are the arguments. Returns x0. The whole context is restored, so the caller's thread goes on as before. */
static uint64_t call(CPU* c, const Callee* f, uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4) {
    CPU saved = *c;
    c->x[0] = a0;
    c->x[1] = a1;
    c->x[2] = a2;
    c->x[3] = a3;
    c->x[4] = a4;
    f->fn(c, f->entry);
    uint64_t r = c->x[0];
    *c = saved;
    return r;
}

typedef struct {
    unsigned cases, failed, skipped;
    NvnStArena arena;
} Run;

/* The report of one comparison: the line, and the first difference of each failed kind with both values. */
static void report(Run* run, const char* name, const char* args, const NvnStRec* ref, const NvnStDiff* d) {
    char line[256];
    nvn_st_format_line(line, sizeof line, name, args, ref, d);
    run->cases++;
    if (d->failed) run->failed++;
    LOG("%s\n", line);
    for (int k = 0; k < NVN_ST_KINDS; k++)
        if (d->failed & (1u << k)) LOG("selftest:   first difference (%c): %s\n", "RMSY"[k], d->first[k]);
}

/* nvnDeviceGetInteger(device, what, int* out): the call writes one word into memory that the game can see (kind M).
   The values are the ones that the run of 2026-11-02 asked for; an unobserved value is a later step. */
static const uint32_t device_what[] = {4,  15, 16, 17, 18, 19, 20, 40, 41, 42, 50, 59, 66, 67,
                                       68, 69, 71, 72, 73, 74, 75, 76, 82, 85, 87, 90, 92};

static void case_device_get_integer(CPU* c, Run* run) {
    Proc p;
    if (!proc_find(&p, "nvnDeviceGetInteger")) {
        run->skipped++;
        LOG("selftest: nvnDeviceGetInteger skipped (the game did not resolve it, or the host has no handler)\n");
        return;
    }
    enum { REGION = OUT_GUARD_BYTES + OUT_WORD_BYTES + OUT_GUARD_BYTES };
    uint64_t region = nvn_st_arena_alloc(&run->arena, REGION, 16);
    uint64_t out = region + OUT_GUARD_BYTES;
    for (size_t i = 0; region && i < sizeof device_what / sizeof device_what[0]; i++) {
        uint8_t lib_bytes[REGION], host_bytes[REGION];
        memset(GP(region), OUT_FILL, REGION); /* the word must be overwritten, the guards must not */
        call(c, &p.lib, g_device, device_what[i], out, 0, 0);
        memcpy(lib_bytes, GP(region), REGION);
        memset(GP(region), OUT_FILL, REGION);
        call(c, &p.host, g_device, device_what[i], out, 0, 0);
        memcpy(host_bytes, GP(region), REGION);
        NvnStRec ref = {0}, got = {0};
        NvnStDiff d;
        nvn_st_add_blob(&ref, "out", lib_bytes, NULL, REGION);
        nvn_st_add_blob(&got, "out", host_bytes, NULL, REGION);
        nvn_st_compare(&ref, &got, &d);
        char args[32];
        snprintf(args, sizeof args, "what=%u", device_what[i]);
        report(run, p.name, args, &ref, &d);
    }
}

/* The texture builder: the setters fill the builder (in the arena for the library, in a record for the host), then the
   storage queries and the getters answer. Sets are formats, sizes, levels and flags that the game used (flags 9, formats
   0x25 and 0x26), the target 1 (2D). */
typedef struct {
    Proc set_device, set_defaults, set_flags, set_target, set_size, set_levels, set_format;
    Proc get_size, get_align, get_width, get_height;
} TexProcs;

static int tex_procs_find(TexProcs* t) {
    return proc_find(&t->set_device, "nvnTextureBuilderSetDevice") &&
           proc_find(&t->set_defaults, "nvnTextureBuilderSetDefaults") &&
           proc_find(&t->set_flags, "nvnTextureBuilderSetFlags") &&
           proc_find(&t->set_target, "nvnTextureBuilderSetTarget") &&
           proc_find(&t->set_size, "nvnTextureBuilderSetSize2D") &&
           proc_find(&t->set_levels, "nvnTextureBuilderSetLevels") &&
           proc_find(&t->set_format, "nvnTextureBuilderSetFormat") &&
           proc_find(&t->get_size, "nvnTextureBuilderGetStorageSize") &&
           proc_find(&t->get_align, "nvnTextureBuilderGetStorageAlignment") &&
           proc_find(&t->get_width, "nvnTextureBuilderGetWidth") &&
           proc_find(&t->get_height, "nvnTextureBuilderGetHeight");
}

typedef struct {
    uint32_t flags, format, width, height, levels;
} TexCase;

static void tex_set(CPU* c, const TexProcs* t, int host, uint64_t b, const TexCase* k) {
#define SIDE(p) (host ? &(p).host : &(p).lib)
    call(c, SIDE(t->set_defaults), b, 0, 0, 0, 0);
    call(c, SIDE(t->set_device), b, g_device, 0, 0, 0);
    call(c, SIDE(t->set_flags), b, k->flags, 0, 0, 0);
    call(c, SIDE(t->set_target), b, 1, 0, 0, 0);
    call(c, SIDE(t->set_size), b, k->width, k->height, 0, 0);
    call(c, SIDE(t->set_levels), b, k->levels, 0, 0, 0);
    call(c, SIDE(t->set_format), b, k->format, 0, 0, 0);
}

static void tex_query(CPU* c, const TexProcs* t, int host, uint64_t b, NvnStRec* rec) {
    nvn_st_add_ret(rec, "size", call(c, SIDE(t->get_size), b, 0, 0, 0, 0));
    nvn_st_add_ret(rec, "alignment", call(c, SIDE(t->get_align), b, 0, 0, 0, 0));
    nvn_st_add_ret(rec, "width", (uint32_t)call(c, SIDE(t->get_width), b, 0, 0, 0, 0));
    nvn_st_add_ret(rec, "height", (uint32_t)call(c, SIDE(t->get_height), b, 0, 0, 0, 0));
}
#undef SIDE

static void case_texture_builder(CPU* c, Run* run) {
    TexProcs t;
    if (!tex_procs_find(&t)) {
        run->skipped++;
        LOG("selftest: nvnTextureBuilder skipped (a function was not resolved, or the host has no handler)\n");
        return;
    }
    uint64_t b = nvn_st_arena_alloc(&run->arena, BUILDER_BYTES, 16);
    if (!b) return;
    static const uint32_t flags[] = {0, 9}, formats[] = {0x25, 0x26}, levels[] = {1, 4};
    static const uint32_t sizes[][2] = {{64, 64}, {1280, 720}, {1920, 1080}};
    for (size_t f = 0; f < 2; f++)
        for (size_t m = 0; m < 2; m++)
            for (size_t s = 0; s < 3; s++)
                for (size_t l = 0; l < 2; l++) {
                    TexCase k = {flags[f], formats[m], sizes[s][0], sizes[s][1], levels[l]};
                    NvnStRec ref = {0}, got = {0};
                    NvnStDiff d;
                    memset(GP(b), 0, BUILDER_BYTES);
                    tex_set(c, &t, 0, b, &k);
                    tex_query(c, &t, 0, b, &ref);
                    tex_set(c, &t, 1, b, &k);
                    tex_query(c, &t, 1, b, &got);
                    nvn_st_compare(&ref, &got, &d);
                    char args[96];
                    snprintf(args, sizeof args, "flags=%u fmt=0x%x %ux%u levels=%u", k.flags, k.format, k.width, k.height,
                             k.levels);
                    report(run, "nvnTextureBuilderStorage", args, &ref, &d);
                }
}

void nvn_selftest_run(CPU* c) {
    if (__atomic_exchange_n(&g_ran, 1, __ATOMIC_ACQ_REL)) return;
    if (nvn_native_active()) {
        LOG("selftest: not run, nvn_native=1 (the library must run as the reference)\n");
        return;
    }
    if (!g_device) {
        LOG("selftest: not run, the game made no device before nvnQueueInitialize\n");
        return;
    }
    Run run = {0};
    u64 base = kmem_alloc(ARENA_BYTES, MS_STATIC, PERM_RW);
    nvn_st_arena_init(&run.arena, base, ARENA_BYTES);
    LOG("selftest: start, device=%" PRIx64 ", arena=%" PRIx64 "+%x\n", g_device, base, ARENA_BYTES);
    LOG("selftest: device texture words +%x=%08x +%x=%08x\n", DEVICE_TEXTURE_WORD_OFFSET,
        gr32(g_device + DEVICE_TEXTURE_WORD_OFFSET), DEVICE_COMPRESSION_BYTE_OFFSET,
        (u32)*(const uint8_t*)GP(g_device + DEVICE_COMPRESSION_BYTE_OFFSET));
    LOG("selftest: device render target words +%x=%08x +%x=%08x +%x=%08x\n", DEVICE_CLIP_LIMIT_OFFSET,
        gr32(g_device + DEVICE_CLIP_LIMIT_OFFSET), DEVICE_WINDOW_ORIGIN_OFFSET,
        gr32(g_device + DEVICE_WINDOW_ORIGIN_OFFSET), DEVICE_DEPTH_MODE_OFFSET,
        gr32(g_device + DEVICE_DEPTH_MODE_OFFSET));
    case_device_get_integer(c, &run);
    case_texture_builder(c, &run);
    LOG("selftest: %u cases, %u failed, %u skipped, arena used %" PRIu64 " of %u bytes\n", run.cases, run.failed,
        run.skipped, run.arena.high_water, ARENA_BYTES);
}
