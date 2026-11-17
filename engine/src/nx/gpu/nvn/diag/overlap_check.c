/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The records go round in post order, which is the order the GPU thread ends present jobs in. A record's state says
   who has it (overlap_check_calc.h): the post collects, the game's thread hashes and hands it over, frame_end claims it
   and hashes again. A replay that ends before its post's hash is done skips it, and the game's thread then frees it,
   so no count compares against an unfinished hash. Every job missed so is counted and printed. */
#include "nx/gpu/nvn/diag/overlap_check.h"
#include "nx/gpu/nvn/diag/perf_reuse.h"
#include "nx/gpu/vk/vk.h"
#include "nx/runtime/platform/log.h"
#include "nx/runtime/platform/plat.h"
#include <stdlib.h>
#include <string.h>

#define OVERLAP_RECORDS 4u /* presents between a post and its end: one, two with SNX_ARCEUS_PERF_OVERLAP */
#define OVERLAP_WINDOW 600u

int g_overlap_check;
typedef struct {
    OverlapRanges* ranges; /* allocated at its first use, with the switch on */
    uint64_t seq;
    int state;
} Record;
static Record g_rec[OVERLAP_RECORDS];
static uint64_t g_post_seq; /* objs_lock */
static uint64_t g_end_seq;  /* the GPU thread's */
static PerfReuseWindow g_win; /* the GPU thread's */
static uint64_t g_game_changed[PERF_REUSE_KINDS], g_self_changed, g_lost, g_skipped, g_missed;
static uint64_t g_end_ns, g_w_overflow;
static uint64_t g_post_ns; /* the game's thread adds, the GPU thread takes: atomic */

static uint64_t hash_all(OverlapRanges* r, int compare) {
    uint64_t changed = 0;
    vk_lock(); /* own_hash's producer lock, as the reuse probe */
    for (uint32_t i = 0; i < r->set.n; i++) {
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
        changed++;
        if (overlap_self_written(r, x->gva, x->len)) {
            g_self_changed++;
            continue;
        }
        for (unsigned k = 0; k < PERF_REUSE_KINDS; k++) g_game_changed[k] += x->kinds >> k & 1;
    }
    vk_unlock();
    return changed;
}

int overlap_check_collect(const uint64_t* handles, uint32_t count, OverlapFind find, void* ctx) {
    const uint64_t seq = g_post_seq++;
    Record* rec = &g_rec[seq % OVERLAP_RECORDS];
    if (__atomic_load_n(&rec->state, __ATOMIC_ACQUIRE) != OVERLAP_FREE) {
        __atomic_fetch_add(&g_lost, 1, __ATOMIC_RELAXED);
        return -1;
    }
    if (!rec->ranges && !(rec->ranges = malloc(sizeof *rec->ranges))) {
        __atomic_fetch_add(&g_lost, 1, __ATOMIC_RELAXED);
        return -1;
    }
    overlap_clear(rec->ranges);
    for (uint32_t i = 0; i < count; i++) overlap_collect(rec->ranges, find(ctx, handles[i]), find, ctx, 0);
    rec->seq = seq;
    __atomic_store_n(&rec->state, OVERLAP_COLLECTED, __ATOMIC_RELEASE);
    return (int)(seq % OVERLAP_RECORDS);
}

void overlap_check_post_hash(int record) {
    if (record < 0) return;
    Record* rec = &g_rec[record];
    const uint64_t t0 = plat_ns();
    hash_all(rec->ranges, 0);
    __atomic_fetch_add(&g_post_ns, plat_ns() - t0, __ATOMIC_RELAXED);
    overlap_post_publish(&rec->state);
}

static void window_line(void) {
    const PerfReuseWindow* w = &g_win;
    char top[PERF_REUSE_TOP * 48] = "";
    for (uint32_t i = 0, at = 0; i < w->ntop && at < sizeof top; i++)
        at += (unsigned)snprintf(top + at, sizeof top - at, "%s%llx+%x(%s%s%s%s)", i ? " " : "",
                                 (unsigned long long)w->top[i].gva, w->top[i].len,
                                 w->top[i].kinds & PERF_REUSE_CB ? "c" : "",
                                 w->top[i].kinds & PERF_REUSE_VERTEX ? "v" : "",
                                 w->top[i].kinds & PERF_REUSE_INDEX ? "i" : "",
                                 w->top[i].kinds & PERF_REUSE_STORAGE ? "s" : "");
    const double f = w->frames ? (double)w->frames : 1.0;
    const uint64_t post_ns = __atomic_exchange_n(&g_post_ns, 0, __ATOMIC_RELAXED);
    const uint64_t lost = __atomic_exchange_n(&g_lost, 0, __ATOMIC_RELAXED);
    LOG("nvn-overlap-check: %llu presents, %llu of them not checked (skipped %llu, missed %llu, lost %llu of them). "
        "Candidates, not a bound: a range that meets a replay write counts as the replay's, textures and copy "
        "sources are not covered, owned runs are hashed by owner, and a change after its draw read it counts. "
        "%.1f ranges %.2f MB a frame, changed %llu ranges %.3f MB, by the game: constants %llu/%llu vertices "
        "%llu/%llu indices %llu/%llu storages %llu/%llu, by the replay itself %llu, owned %llu, overflow "
        "%llu+%llu, hashing %.3f ms a frame at the post and %.3f at the end, largest changed: %s\n",
        (unsigned long long)w->frames, (unsigned long long)(g_skipped + g_missed), /* a lost post is missed too */
        (unsigned long long)g_skipped, (unsigned long long)g_missed, (unsigned long long)lost, w->kept / f,
        w->bytes_kept / f / 1e6, (unsigned long long)w->changed,
        w->bytes_changed / 1e6, (unsigned long long)g_game_changed[0], (unsigned long long)w->kept_kind[0],
        (unsigned long long)g_game_changed[1], (unsigned long long)w->kept_kind[1],
        (unsigned long long)g_game_changed[2], (unsigned long long)w->kept_kind[2],
        (unsigned long long)g_game_changed[3], (unsigned long long)w->kept_kind[3],
        (unsigned long long)g_self_changed, (unsigned long long)w->owned, (unsigned long long)w->overflow,
        (unsigned long long)g_w_overflow, post_ns / f / 1e6, g_end_ns / f / 1e6, top[0] ? top : "none");
    memset(&g_win, 0, sizeof g_win);
    memset(g_game_changed, 0, sizeof g_game_changed);
    g_self_changed = g_skipped = g_missed = g_end_ns = g_w_overflow = 0;
}

void overlap_check_end(void) {
    const uint64_t seq = g_end_seq++;
    Record* rec = &g_rec[seq % OVERLAP_RECORDS];
    /* a lost post left no record of its own, so the record there is another job's: missed, not taken */
    const int claim = rec->seq == seq ? overlap_end_claim(&rec->state) : OVERLAP_FREE;
    if (claim == OVERLAP_ENDING) {
        const uint64_t t0 = plat_ns();
        hash_all(rec->ranges, 1);
        g_end_ns += plat_ns() - t0;
        g_win.overflow += rec->ranges->set.overflow;
        g_w_overflow += rec->ranges->w_overflow;
        __atomic_store_n(&rec->state, OVERLAP_FREE, __ATOMIC_RELEASE);
    } else if (claim == OVERLAP_SKIPPED) {
        g_skipped++;
    } else {
        g_missed++;
    }
    if (++g_win.frames == OVERLAP_WINDOW) window_line();
}
