/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The ranges a native frame's replay read are hashed when the frame ends, before its syncpoint lets the game build
   the next one, and again when the next job starts, before its replay. A change between the two is the game's write
   while it built the next frame. GPU-owned bytes are not read (that would land them): their owners' identity is
   hashed instead. Nothing runs and nothing is allocated with the switch unset. */
#include "nx/gpu/nvn/diag/perf_reuse.h"
#include "nx/gpu/vk/vk.h"
#include "nx/hle/services/nvdrv.h"
#include "nx/memory/mem.h"
#include "nx/memory/own.h"
#include "nx/runtime/platform/log.h"
#include "nx/runtime/platform/plat.h"
#include <stdlib.h>
#include <string.h>

#define PERF_REUSE_WINDOW 600u /* frames a line */

int g_perf_reuse_rec;
static int g_on = -1;
static int g_pending; /* a frame's ranges are hashed and wait for the next job */
static PerfReuseSet* g_set;
static PerfReuseWindow g_win;
static u64 g_hash_ns;

static int probe_on(void) {
    if (g_on < 0) {
        const char* v = getenv("SNX_ARCEUS_PERF_REUSE_PROBE");
        g_on = v && !strcmp(v, "1");
        if (g_on && !(g_set = calloc(1, sizeof *g_set))) {
            WARN("arceus-perf-reuse: no memory for the range set, the probe stays off\n");
            g_on = 0;
        }
        if (g_on) {
            perf_reuse_clear(g_set);
            LOG("arceus-perf-reuse: log-only probe on, a line every %u frames (SNX_ARCEUS_PERF_REUSE_PROBE)\n",
                PERF_REUSE_WINDOW);
        }
    }
    return g_on;
}

void perf_reuse_note(uint64_t gva, uint64_t len, uint32_t kind) {
    if (gva) perf_reuse_add(g_set, gva, len, kind);
}

static u64 mix(u64 h, u64 v) {
    h = (h ^ v) * 0x9E3779B97F4A7C15ull;
    return h ^ (h >> 29);
}
/* the range's bytes run by run of the guest memory behind it, an unmapped page as its offset */
static u64 range_hash(u64 gva, u64 len, u8* owned) {
    u64 h = 0x243F6A8885A308D3ull;
    for (u64 o = 0; o < len;) {
        u64 cpu = 0, oh = 0, n = gmmu_run(gva + o, len - o, &cpu);
        if (!n) {
            n = 0x1000 - ((gva + o) & 0xFFF);
            if (n > len - o) n = len - o;
            h = mix(h, ~o);
        } else if (__atomic_load_n(&g_own_any, __ATOMIC_ACQUIRE) && own_hash(cpu, n, &oh)) {
            *owned = 1;
            h = mix(h, oh);
        } else
            h = mix(h, vk_hash(GP(cpu), n));
        o += n;
    }
    return h;
}

static void window_line(void) {
    const PerfReuseWindow* w = &g_win;
    char top[PERF_REUSE_TOP * 48] = "";
    for (u32 i = 0, at = 0; i < w->ntop && at < sizeof top; i++)
        at += (unsigned)snprintf(top + at, sizeof top - at, "%s%llx+%x(%s%s%s)", i ? " " : "",
                                 (unsigned long long)w->top[i].gva, w->top[i].len,
                                 w->top[i].kinds & PERF_REUSE_CB ? "c" : "",
                                 w->top[i].kinds & PERF_REUSE_VERTEX ? "v" : "",
                                 w->top[i].kinds & PERF_REUSE_INDEX ? "i" : "");
    const double f = w->frames ? (double)w->frames : 1.0;
    LOG("arceus-perf-reuse: %llu frames: kept %.1f ranges %.2f MB a frame, changed %llu of %llu ranges (%.2f%%) "
        "%.3f MB of %.1f MB (%.2f%%), constants %llu/%llu vertices %llu/%llu indices %llu/%llu, owned %llu, "
        "overflow %llu, hashing %.3f ms a frame, largest changed: %s\n",
        (unsigned long long)w->frames, w->kept / f, w->bytes_kept / f / 1e6, (unsigned long long)w->changed,
        (unsigned long long)w->kept, w->kept ? 100.0 * w->changed / w->kept : 0.0, w->bytes_changed / 1e6,
        w->bytes_kept / 1e6, w->bytes_kept ? 100.0 * w->bytes_changed / w->bytes_kept : 0.0,
        (unsigned long long)w->changed_kind[0], (unsigned long long)w->kept_kind[0],
        (unsigned long long)w->changed_kind[1], (unsigned long long)w->kept_kind[1],
        (unsigned long long)w->changed_kind[2], (unsigned long long)w->kept_kind[2], (unsigned long long)w->owned,
        (unsigned long long)w->overflow, g_hash_ns / f / 1e6, top[0] ? top : "none");
    memset(&g_win, 0, sizeof g_win);
    g_hash_ns = 0;
}

void perf_reuse_job_begin(void) {
    if (!probe_on()) return;
    if (g_pending) {
        const u64 t0 = plat_ns();
        vk_lock(); /* own_hash's producer lock */
        for (u32 i = 0; i < g_set->n; i++) {
            PerfReuseRange* r = &g_set->r[i];
            u8 owned = 0;
            perf_reuse_count(&g_win, r, range_hash(r->gva, r->len, &owned) != r->hash);
        }
        vk_unlock();
        g_hash_ns += plat_ns() - t0;
        g_win.overflow += g_set->overflow;
        g_win.frames++;
        g_pending = 0;
        perf_reuse_clear(g_set);
        if (g_win.frames == PERF_REUSE_WINDOW) window_line();
    }
    g_perf_reuse_rec = 1;
}

void perf_reuse_job_end(int frame) {
    if (g_on <= 0) return;
    g_perf_reuse_rec = 0;
    if (!frame) return;
    const u64 t0 = plat_ns();
    vk_lock();
    for (u32 i = 0; i < g_set->n; i++) {
        PerfReuseRange* r = &g_set->r[i];
        r->hash = range_hash(r->gva, r->len, &r->owned);
    }
    vk_unlock();
    g_hash_ns += plat_ns() - t0;
    g_pending = 1;
}
