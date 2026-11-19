/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* Reconcile a shader's mapped output with guest memory. The host may have written the guest range while the GPU was
   running: shader-changed bytes land only where the guest still holds the value the shader originally read. */
#include <stdlib.h>
#include <string.h>
#include "nx/gpu/gpu.h"
#include "nx/memory/mem.h"
#include "nx/runtime/platform/log.h"
#include "nx/gpu/vk/vk.h"
#include "nx/gpu/perf_times.h"
#include "nx/gpu/vk/landing.h"

/* Nested because a result can flush an overlapping render target while landing. */
static _Thread_local unsigned t_result_depth;
void vk_result_begin(void) { t_result_depth++; }
void vk_result_end(void) { t_result_depth--; }
int vk_result_landing(void) { return t_result_depth != 0; }

/* whether [p, p + n) overlaps the guest's address space, where pages the GPU holds fault when read */
static int in_guest(const u8* p, u32 n) { return p < g_mem + AS_SIZE && p + n > g_mem; }

/* where landing_runs puts a run: guest memory through its host pointer, else gmem_write */
typedef struct {
    u64 addr;
    const u8* res;
    u8* dst;
} LandingDst;
static void landing_put(void* ctx, u32 from, u32 n) {
    const LandingDst* d = ctx;
    if (d->dst)
        memcpy(d->dst + from, d->res + from, n);
    else
        gmem_write(d->addr + from, d->res + from, n);
    if (g_perf_times > 1) gpu_mem_landed(d->addr + from, n); /* PERF-23, SNX_ARCEUS_PERF_TIMES=2 */
}
/* SNX_ARCEUS_PERF_TIMES (PERF-23): the span a 64-byte masked store of landing_masked wrote, for the record as
   written */
static void landing_note(void* ctx, u32 from, u32 n) {
    const LandingDst* d = ctx;
    gpu_mem_landed(d->addr + from, n);
}

/* runs NULL: every byte of the result. Else its changed runs, maximal and in order: a byte outside them is unchanged,
   and a maximal changed byte range never crosses from one into the next (PERF-28) */
static void write_changed(u64 addr, const u8* res, const u8* org, u32 size, const ChgRun* runs, u32 nruns) {
    /* res and org are host copies: the shader's output read back into host-cached memory, and the bytes it started
       from, snapshotted when it was bound (buffer.c, compute.c, surface.c). Comparing them reads no guest page, so it
       can't fault on one the GPU holds. Only cur below reads guest memory, after vk_rt_before_write */
    if (!size) return;
    if (in_guest(res, size) || in_guest(org, size)) fatal("vulkan: a storage result compared in guest memory");
    if (runs ? !nruns : !memcmp(res, org, size)) return; /* nothing written: no guest read, allocation or flush */
    vk_result_begin();
    vk_rt_before_write(addr, size);
    u8* dst = gmem_ptr(addr, size);
    u8* tmp = NULL;
    const u8* cur = dst;
    int changed = 0;
    u64 written = 0;
    if (!cur) {
        tmp = malloc(size);
        if (!tmp) fatal("Out of memory landing a shader storage buffer");
        gmem_read(addr, tmp, size);
        cur = tmp;
    }
    const ChgRun whole = {0, size};
    if (!runs) runs = &whole, nruns = 1;
    for (u32 r = 0; g_perf_landing && r < nruns; r++) { /* SNX_ARCEUS_PERF_LANDING=3: a 64-byte masked store a step */
        const u32 at = runs[r].from, len = runs[r].to - at;
        LandingDst d = {addr + at, res + at, dst ? dst + at : NULL};
        if (g_landing_masked && dst)
            written += landing_masked(res + at, org + at, dst + at, len, landing_put,
                                      g_perf_times > 1 ? landing_note : NULL, &d);
        else /* no AVX-512BW, or no contiguous host pointer: found a word at a time */
            written += landing_runs(res + at, org + at, cur + at, len, landing_put, &d);
    }
    if (g_perf_landing) changed = written != 0;
    for (u32 r = 0; !g_perf_landing && r < nruns; r++)
        for (u32 i = runs[r].from, to = runs[r].to; i < to;) {
            u32 n = to - i < 1024 ? to - i : 1024, end = i + n;
            /* Large storage bindings often change only a few bytes: the host's vectorized comparison of the two host
               copies skips their unchanged ranges, and guest memory is read only where the shader changed a byte */
            if (!memcmp(res + i, org + i, n)) {
                i = end;
                continue;
            }
            do {
                if (end - i >= 8 && !memcmp(res + i, org + i, 8)) {
                    i += 8;
                    continue;
                }
                if (res[i] == org[i] || cur[i] != org[i]) {
                    i++;
                    continue;
                }
                u32 start = i;
                while (i < to && res[i] != org[i] && cur[i] == org[i]) i++;
                if (dst)
                    memcpy(dst + start, res + start, i - start);
                else
                    gmem_write(addr + start, res + start, i - start);
                if (g_perf_times > 1) gpu_mem_landed(addr + start, i - start); /* PERF-23, SNX_ARCEUS_PERF_TIMES=2 */
                written += i - start;
                changed = 1;
            } while (i < end);
        }
    free(tmp);
    if (written && g_perf_times) perf_count(&g_perf, PC_WC_BYTES, written);
    if (changed) gpu_mem_written_shader(addr, size); /* other GPU virtual addresses can alias these guest pages */
    vk_result_end();
}
void vk_write_changed(u64 addr, const u8* res, const u8* org, u32 size) {
    write_changed(addr, res, org, size, NULL, 0);
}
void vk_write_changed_runs(u64 addr, const u8* res, const u8* org, u32 size, const ChgRun* runs, u32 n) {
    write_changed(addr, res, org, size, runs, n);
}
