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

void perf_tail_arm(PerfTimes* p) { p->tail_arm = 1; }
int perf_tail_take(PerfTimes* p) {
    const int armed = p->tail_arm;
    p->tail_arm = 0;
    return armed;
}

#define LOAD(x) __atomic_load_n(&(x), __ATOMIC_ACQUIRE)
/* sets a time once: 1 when this call set it */
static int stamp(uint64_t* t, uint64_t now) {
    uint64_t zero = 0;
    return __atomic_compare_exchange_n(t, &zero, now ? now : 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}
static void tail_site(PerfTimes* p, int site, uint64_t t0, uint64_t at, int* partial) {
    if (!at) {
        *partial = 1;
        return;
    }
    perf_time_add(p, site, at > t0 ? at - t0 : 0);
}
/* the second of the two closing points adds the times and frees the slot */
static void tail_close(PerfTimes* p, PerfTail* t) {
    if (__atomic_add_fetch(&t->closes, 1, __ATOMIC_ACQ_REL) != 2) return;
    int partial = 0;
    const uint64_t t0 = LOAD(t->t0); /* atomic: the GPU thread may be reusing the slot (reviewer 20:08) */
    if (LOAD(t->gate)) {
        tail_site(p, PT_TAIL_SUBMIT, t0, LOAD(t->t_submit), &partial);
        tail_site(p, PT_TAIL_DONE, t0, LOAD(t->t_done), &partial);
    }
    tail_site(p, PT_TAIL_INCR, t0, LOAD(t->t_incr), &partial);
    tail_site(p, PT_TAIL_FREE, t0, LOAD(t->t_free), &partial);
    tail_site(p, PT_TAIL_WAKE, t0, LOAD(t->t_wake), &partial);
    if (partial) perf_count(p, PC_TAIL_PARTIAL, 1);
    __atomic_store_n(&t->armed, 0, __ATOMIC_RELEASE);
}
void perf_tail_queued(PerfTimes* p, uint64_t seq, uint64_t gate, uint64_t submitted, uint64_t done, uint64_t now) {
    PerfTail* t = &p->tail[p->tail_next++ % PERF_TAILS];
    if (LOAD(t->armed)) perf_count(p, PC_TAIL_LOST, 1); /* reused unclosed: its late points may land in the new one */
    __atomic_store_n(&t->armed, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&t->gate, gate, __ATOMIC_RELAXED);
    __atomic_store_n(&t->seq, seq, __ATOMIC_RELAXED);
    __atomic_store_n(&t->t0, now, __ATOMIC_RELAXED);
    uint64_t* times[] = {&t->t_submit, &t->t_done, &t->t_incr, &t->t_free, &t->t_wake};
    for (unsigned i = 0; i < sizeof times / sizeof times[0]; i++) __atomic_store_n(times[i], 0, __ATOMIC_RELAXED);
    __atomic_store_n(&t->closes, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&t->armed, 1, __ATOMIC_RELEASE);
    const uint64_t last = gate > submitted ? gate : submitted; /* the increment's own submission counts */
    if (p->tail_mark) perf_count(p, PC_TAIL_SUBMITS, last - p->tail_mark);
    p->tail_mark = last;
    perf_count(p, PC_TAIL_AFTER, last > done ? last - done : 0);
    if (!gate) perf_count(p, PC_TAIL_UNGATED, 1);
    else if (LOAD(p->tail_handed) >= gate) stamp(&t->t_submit, now); /* handed over already */
}
void perf_tail_handed(PerfTimes* p, uint64_t value, uint64_t now) {
    uint64_t seen = LOAD(p->tail_handed);
    while (value > seen && !__atomic_compare_exchange_n(&p->tail_handed, &seen, value, 0, __ATOMIC_ACQ_REL,
                                                        __ATOMIC_ACQUIRE)) {
    }
    for (unsigned i = 0; i < PERF_TAILS; i++) {
        PerfTail* t = &p->tail[i];
        const uint64_t gate = LOAD(t->gate);
        if (LOAD(t->armed) && gate && gate <= value) stamp(&t->t_submit, now);
    }
}
void perf_tail_done(PerfTimes* p, uint64_t value, uint64_t now) {
    for (unsigned i = 0; i < PERF_TAILS; i++) {
        PerfTail* t = &p->tail[i];
        const uint64_t gate = LOAD(t->gate);
        if (LOAD(t->armed) && gate && gate <= value) stamp(&t->t_done, now);
    }
}
void perf_tail_performed(PerfTimes* p, uint64_t seq, uint64_t now) {
    for (unsigned i = 0; i < PERF_TAILS; i++) {
        PerfTail* t = &p->tail[i];
        if (LOAD(t->armed) && LOAD(t->seq) == seq) stamp(&t->t_incr, now);
    }
}
void perf_tail_freed(PerfTimes* p, uint64_t now) {
    for (unsigned i = 0; i < PERF_TAILS; i++) {
        PerfTail* t = &p->tail[i];
        if (LOAD(t->armed) && LOAD(t->t_incr) && stamp(&t->t_free, now)) tail_close(p, t);
    }
}
void perf_tail_woke(PerfTimes* p, uint64_t now) {
    PerfTail* oldest = NULL;
    for (unsigned i = 0; i < PERF_TAILS; i++) {
        PerfTail* t = &p->tail[i];
        if (!LOAD(t->armed) || !LOAD(t->t_incr) || LOAD(t->t_wake)) continue;
        if (!oldest || LOAD(t->seq) < LOAD(oldest->seq)) oldest = t;
    }
    if (oldest && stamp(&oldest->t_wake, now)) tail_close(p, oldest);
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
                                                "sync_wait", "acquire", "release_hold", "tail_submit", "tail_done",
                                                "tail_incr", "tail_free", "tail_wake"};
    static const char* const counts[PC_COUNTS] = {"wb_outputs", "wb_bytes", "wc_bytes", "tail_submits",
                                                  "tail_after", "tail_ungated", "tail_partial", "tail_lost"};
    if (!size) return 0;
    if (!frames) frames = 1;
    int n = snprintf(out, size, "perf-times: frames=%u (ms a frame/largest ms)", frames);
    size_t at = n > 0 ? (size_t)n : 0;
    for (int i = 0; i < PT_SITES; i++) {
        const PerfTime t = take(&p->site[i]);
        if (i < PT_TAIL_SUBMIT) {
            at = put(out, size, at, names[i], t, frames);
        } else if (at < size) { /* a tail's times: the mean over the tails that have it, the largest, how many */
            const double mean = t.count ? (double)t.sum / t.count / 1e6 : 0.0;
            n = snprintf(out + at, size - at, " %s=%.3f/%.3f/%llu", names[i], mean, (double)t.max / 1e6,
                         (unsigned long long)t.count);
            if (n > 0) at += (size_t)n;
        }
    }
    for (uint32_t i = 0; i < PERF_TIMES_NAMED; i++) {
        const char* what = __atomic_load_n(&p->named[i].what, __ATOMIC_ACQUIRE);
        const PerfTime t = take(&p->named[i].t);
        if (!what || !t.count || at >= size) continue;
        n = snprintf(out + at, size - at, " [%s]=%.3f/%.3f", what, (double)t.sum / frames / 1e6, (double)t.max / 1e6);
        if (n > 0) at += (size_t)n;
    }
    for (int i = 0; i < PC_COUNTS && at < size; i++) { /* a frame's mean, the tails' misses as totals */
        const uint64_t c = __atomic_exchange_n(&p->count[i], 0, __ATOMIC_RELAXED);
        if (i >= PC_TAIL_UNGATED)
            n = snprintf(out + at, size - at, " %s=%llu", counts[i], (unsigned long long)c);
        else
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
