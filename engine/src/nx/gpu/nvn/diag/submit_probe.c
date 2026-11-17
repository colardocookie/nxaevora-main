/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The frame is the present queue's lists since its last post. A list's ranges are hashed right after its submit and
   again just before the present's post, so a change between is the game's, unless the frame's lists write the range
   themselves (storage, transform feedback, uniform updates, copies, clears, reports), which an earlier replay may
   also have done. A flush posts the frame early: its calls are counted apart and its ranges dropped. */
#include "nx/gpu/nvn/diag/submit_probe.h"
#include "nx/gpu/nvn/diag/perf_reuse.h"
#include "nx/gpu/vk/vk.h"
#include "nx/runtime/platform/log.h"
#include "nx/runtime/platform/plat.h"
#include <stdlib.h>
#include <string.h>

#define SUBMIT_WINDOW 600u

int g_submit_probe;
static PMutex g_lk = PMUTEX_INIT; /* everything below but the atomics */
static uint64_t g_queue;          /* the present queue, 0 until its first present: atomic */
static uint64_t g_flushes;        /* flush jobs posted on it: atomic */
static uint64_t g_wait_ns;        /* the last nvnSyncWait return: atomic */
static SubmitFrame g_frame;
static OverlapRanges* g_ranges; /* allocated at its first use, with the switch on */
static uint64_t g_flushes_seen, g_post_ns, g_wait_used;
static SubmitTotals g_tot;
static SubmitTotals g_flushed; /* the calls a flush posted before their present */
static PerfReuseWindow g_win;
static uint64_t g_game_changed[PERF_REUSE_KINDS], g_self_changed, g_other_calls, g_early_calls, g_no_memory;
static uint64_t g_first_n, g_first_sum, g_first_max, g_submit_hash_ns, g_post_hash_ns, g_w_overflow;

void submit_probe_wait_return(void) { __atomic_store_n(&g_wait_ns, plat_ns(), __ATOMIC_RELAXED); }
uint64_t submit_probe_flushes(void) { return __atomic_load_n(&g_flushes, __ATOMIC_ACQUIRE); }
void submit_probe_flush_posted(uint64_t queue) {
    if (queue && queue == __atomic_load_n(&g_queue, __ATOMIC_RELAXED))
        __atomic_fetch_add(&g_flushes, 1, __ATOMIC_RELEASE);
}

/* hashes ranges [from, set.n): kept, or compared with the kept hash */
static void hash_ranges(uint32_t from, int compare) {
    OverlapRanges* r = g_ranges;
    vk_lock(); /* perf_reuse_hash's lock */
    for (uint32_t i = from; i < r->set.n; i++) {
        PerfReuseRange* x = &r->set.r[i];
        uint8_t owned = 0;
        const uint64_t h = perf_reuse_hash(x->gva, x->len, &owned);
        if (!compare) {
            x->hash = h;
            x->owned = owned;
            continue;
        }
        const int differs = h != x->hash;
        perf_reuse_count(&g_win, x, differs);
        if (!differs) continue;
        if (overlap_self_written(r, x->gva, x->len)) {
            g_self_changed++;
            continue;
        }
        for (unsigned k = 0; k < PERF_REUSE_KINDS; k++) g_game_changed[k] += x->kinds >> k & 1;
    }
    vk_unlock();
}

/* a flush posted the frame: its calls are counted apart, its ranges dropped. Caller holds g_lk */
static void frame_flushed(void) {
    for (uint32_t i = 0; i < g_frame.n; i++) {
        g_flushed.calls++;
        g_flushed.lists += g_frame.call[i].lists;
        g_flushed.ops += g_frame.call[i].ops;
    }
    g_flushed.dropped += g_frame.dropped;
    g_frame.n = 0;
    g_frame.dropped = 0;
    if (g_ranges) overlap_clear(g_ranges);
}

void submit_probe_submit(uint64_t queue, const uint64_t* handles, uint32_t count, uint64_t entry_ns,
                         uint64_t flushes, OverlapFind find, void* ctx, PMutex* lists_lock) {
    const uint64_t present_queue = __atomic_load_n(&g_queue, __ATOMIC_RELAXED);
    pm_lock(&g_lk);
    if (!present_queue || queue != present_queue) {
        if (present_queue) g_other_calls++;
        else g_early_calls++;
        pm_unlock(&g_lk);
        return;
    }
    const uint64_t now = submit_probe_flushes();
    if (now != g_flushes_seen) { /* a flush posted the frame before or during this call */
        frame_flushed();
        g_flushes_seen = now;
    }
    const int hashing = g_submit_probe == 1; /* 2: the times alone, no hash to delay the game */
    if (hashing && !g_ranges) {
        if (!(g_ranges = malloc(sizeof *g_ranges))) {
            g_no_memory++;
            pm_unlock(&g_lk);
            return;
        }
        overlap_clear(g_ranges);
    }
    if (!g_frame.n) {
        const uint64_t wait = __atomic_load_n(&g_wait_ns, __ATOMIC_RELAXED);
        if (wait > g_post_ns && wait <= entry_ns && wait != g_wait_used) { /* the first submit after a wait */
            const uint64_t d = entry_ns - wait;
            g_first_n++;
            g_first_sum += d;
            if (d > g_first_max) g_first_max = d;
            g_wait_used = wait;
        }
    }
    const uint32_t from = hashing ? g_ranges->set.n : 0;
    uint32_t lists = 0;
    uint64_t ops = 0;
    pm_lock(lists_lock);
    for (uint32_t i = 0; i < count; i++) {
        const NvnOpList* list = find(ctx, handles[i]);
        if (!list) continue;
        lists++;
        ops += submit_list_ops(list, find, ctx, 0);
        if (hashing) overlap_collect(g_ranges, list, find, ctx, 0);
    }
    pm_unlock(lists_lock);
    submit_frame_add(&g_frame, entry_ns, lists, ops > UINT32_MAX ? UINT32_MAX : (uint32_t)ops);
    if (flushes != now) { /* this call's own flush posted its lists: no time before a present to measure */
        frame_flushed();
    } else if (hashing) {
        const uint64_t t0 = plat_ns();
        hash_ranges(from, 0);
        g_submit_hash_ns += plat_ns() - t0;
    }
    pm_unlock(&g_lk);
}

static void window_line(void) {
    const SubmitTotals* t = &g_tot;
    const double p = t->posts ? (double)t->posts : 1.0, ops = t->ops ? (double)t->ops : 1.0;
    const double calls = t->calls ? (double)t->calls : 1.0, fp = g_first_n ? (double)g_first_n : 1.0;
    const PerfReuseWindow* w = &g_win;
    LOG("perf-submit-probe: %llu presents, log only, %s. A present: %.1f submit calls, %.1f lists, %.0f ops. Ops "
        "submitted at least 1 ms before the post %.1f%%, 2 ms %.1f%%, 3 ms %.1f%%. Submit call to post mean %.3f ms, "
        "largest %.3f. nvnSyncWait return to the frame's first submit mean %.3f ms, largest %.3f (%llu presents). "
        "Posted early by a flush: %llu calls, %.0f ops a present. Other queues %llu calls, before the first present "
        "%llu, dropped %llu, no memory %llu. Candidates, not a bound (ranges a submitted list reads, hashed after its "
        "submit and again before the post, textures and copy sources not covered): %.1f ranges %.2f MB a present, "
        "changed by the game: constants %llu/%llu vertices %llu/%llu indices %llu/%llu storages %llu/%llu, by a "
        "write of the frame's lists %llu, owned %llu, overflow %llu+%llu, hashing %.3f ms a present after the "
        "submits and %.3f before the post\n",
        (unsigned long long)t->posts, g_submit_probe == 1 ? "hashing" : "times alone, nothing hashed",
        t->calls / p, t->lists / p, t->ops / p, 100.0 * t->ops_lead[0] / ops,
        100.0 * t->ops_lead[1] / ops, 100.0 * t->ops_lead[2] / ops, t->lead_ns_sum / calls / 1e6,
        t->lead_ns_max / 1e6, g_first_sum / fp / 1e6, g_first_max / 1e6, (unsigned long long)g_first_n,
        (unsigned long long)g_flushed.calls, g_flushed.ops / p, (unsigned long long)g_other_calls,
        (unsigned long long)g_early_calls, (unsigned long long)(t->dropped + g_flushed.dropped),
        (unsigned long long)g_no_memory, w->kept / p, w->bytes_kept / p / 1e6,
        (unsigned long long)g_game_changed[0], (unsigned long long)w->kept_kind[0],
        (unsigned long long)g_game_changed[1], (unsigned long long)w->kept_kind[1],
        (unsigned long long)g_game_changed[2], (unsigned long long)w->kept_kind[2],
        (unsigned long long)g_game_changed[3], (unsigned long long)w->kept_kind[3],
        (unsigned long long)g_self_changed, (unsigned long long)w->owned, (unsigned long long)w->overflow,
        (unsigned long long)g_w_overflow, g_submit_hash_ns / p / 1e6, g_post_hash_ns / p / 1e6);
    memset(&g_tot, 0, sizeof g_tot);
    memset(&g_flushed, 0, sizeof g_flushed);
    memset(&g_win, 0, sizeof g_win);
    memset(g_game_changed, 0, sizeof g_game_changed);
    g_self_changed = g_other_calls = g_early_calls = g_no_memory = 0;
    g_first_n = g_first_sum = g_first_max = g_submit_hash_ns = g_post_hash_ns = g_w_overflow = 0;
}

void submit_probe_present(uint64_t queue) {
    const uint64_t post = plat_ns();
    uint64_t expected = 0;
    __atomic_compare_exchange_n(&g_queue, &expected, queue, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
    if (queue != __atomic_load_n(&g_queue, __ATOMIC_RELAXED)) return;
    pm_lock(&g_lk);
    const uint64_t now = submit_probe_flushes();
    if (now != g_flushes_seen) {
        frame_flushed();
        g_flushes_seen = now;
    }
    if (g_ranges && g_frame.n) {
        const uint64_t t0 = plat_ns();
        hash_ranges(0, 1);
        g_post_hash_ns += plat_ns() - t0;
        g_win.overflow += g_ranges->set.overflow;
        g_w_overflow += g_ranges->w_overflow;
        overlap_clear(g_ranges);
    }
    submit_frame_close(&g_frame, post, &g_tot);
    g_post_ns = post;
    if (++g_win.frames == SUBMIT_WINDOW) window_line();
    pm_unlock(&g_lk);
}
