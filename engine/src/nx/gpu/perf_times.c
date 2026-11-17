/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/gpu/perf_times.h"
#include <stdio.h>

int g_perf_times;
PerfTimes g_perf;

static void time_add(PerfTime* t, uint64_t ns) {
    __atomic_fetch_add(&t->sum, ns, __ATOMIC_RELAXED);
    __atomic_fetch_add(&t->count, 1, __ATOMIC_RELAXED);
    uint64_t old = __atomic_load_n(&t->max, __ATOMIC_RELAXED);
    while (ns > old && !__atomic_compare_exchange_n(&t->max, &old, ns, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
    }
}
void perf_time_add(PerfTimes* p, int site, uint64_t ns) {
    if (site >= 0 && site < PT_SITES) time_add(&p->site[site], ns);
}
void perf_time_named(PerfTimes* p, const char* what, uint64_t ns) {
    for (uint32_t i = 0; what && i < PERF_TIMES_NAMED; i++) {
        const char* seen = __atomic_load_n(&p->named[i].what, __ATOMIC_ACQUIRE);
        if (!seen &&
            __atomic_compare_exchange_n(&p->named[i].what, &seen, what, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            seen = what;
        if (seen != what) continue;
        time_add(&p->named[i].t, ns);
        return;
    }
    __atomic_fetch_add(&p->named_lost, 1, __ATOMIC_RELAXED);
}

void perf_count(PerfTimes* p, int counter, uint64_t n) {
    if (counter >= 0 && counter < PC_COUNTS) __atomic_fetch_add(&p->count[counter], n, __ATOMIC_RELAXED);
}

static PerfTime take(PerfTime* t) {
    PerfTime out;
    out.sum = __atomic_exchange_n(&t->sum, 0, __ATOMIC_RELAXED);
    out.max = __atomic_exchange_n(&t->max, 0, __ATOMIC_RELAXED);
    out.count = __atomic_exchange_n(&t->count, 0, __ATOMIC_RELAXED);
    return out;
}
/* "name=a/b": a the mean ms a frame, b the largest single value in ms */
static size_t put(char* out, size_t size, size_t at, const char* name, PerfTime t, uint32_t frames) {
    if (at >= size) return at;
    const int n = snprintf(out + at, size - at, " %s=%.3f/%.3f", name, (double)t.sum / frames / 1e6,
                           (double)t.max / 1e6);
    return n > 0 ? at + (size_t)n : at;
}
size_t perf_times_line(PerfTimes* p, uint32_t frames, char* out, size_t size) {
    static const char* const names[PT_SITES] = {"post_start", "start_end", "end_post", "period", "idle", "show",
                                                "tl_recycle", "tl_finish", "tl_submit", "rec_sync", "rec_full",
                                                "sync_wait", "acquire", "release_hold"};
    static const char* const counts[PC_COUNTS] = {"wb_outputs", "wb_bytes", "wc_bytes"};
    if (!size) return 0;
    if (!frames) frames = 1;
    int n = snprintf(out, size, "perf-times: frames=%u (ms a frame/largest ms)", frames);
    size_t at = n > 0 ? (size_t)n : 0;
    for (int i = 0; i < PT_SITES; i++) at = put(out, size, at, names[i], take(&p->site[i]), frames);
    for (uint32_t i = 0; i < PERF_TIMES_NAMED; i++) {
        const char* what = __atomic_load_n(&p->named[i].what, __ATOMIC_ACQUIRE);
        const PerfTime t = take(&p->named[i].t);
        if (!what || !t.count || at >= size) continue;
        n = snprintf(out + at, size - at, " [%s]=%.3f/%.3f", what, (double)t.sum / frames / 1e6, (double)t.max / 1e6);
        if (n > 0) at += (size_t)n;
    }
    for (int i = 0; i < PC_COUNTS && at < size; i++) { /* a frame's mean */
        const uint64_t c = __atomic_exchange_n(&p->count[i], 0, __ATOMIC_RELAXED);
        n = snprintf(out + at, size - at, " %s=%.1f", counts[i], (double)c / frames);
        if (n > 0) at += (size_t)n;
    }
    const uint64_t overlaps = __atomic_exchange_n(&p->overlaps, 0, __ATOMIC_RELAXED);
    const uint64_t lost = __atomic_exchange_n(&p->named_lost, 0, __ATOMIC_RELAXED);
    if (at < size) {
        n = snprintf(out + at, size - at, " overlaps=%llu named_lost=%llu", (unsigned long long)overlaps,
                     (unsigned long long)lost);
        if (n > 0) at += (size_t)n;
    }
    return at < size ? at : size - 1;
}
