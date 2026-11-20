/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/gpu/perf_times.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_perf_times;
int g_perf_probes;
PerfTimes g_perf;
int g_perf_pass_why;
int g_perf_submit_why;
uint64_t g_perf_submit_every_ns = PERF_SUBMIT_EVERY_NS;
int g_perf_compute_submit = 1;
int g_perf_inplace;
int g_perf_query;
int g_perf_sample;
int g_perf_compare;
int g_patch_hash;
PerfCaps g_perf_caps;
PerfP29 g_perf_p29;
PerfMem g_perf_mem;
PerfWaits g_perf_waits;
PerfHandoff g_perf_handoff;
_Thread_local int g_perf_release_thread;
int g_perf_bind;
uint64_t g_perf_exact;

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
    if (counter == PC_WB_OUTPUTS) __atomic_fetch_add(&p->wb_total, n, __ATOMIC_RELAXED); /* never reset */
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
    __atomic_store_n(&t->first, p->tail_mark ? p->tail_mark + 1 : 0, __ATOMIC_RELAXED); /* PERF-33 */
    __atomic_store_n(&t->last, gate > submitted ? gate : submitted, __ATOMIC_RELAXED);
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
void perf_rel_interval(PerfTimes* p, int state, uint64_t start, uint64_t end, uint64_t outputs) {
    if (state < 0 || state >= REL_STATES || end < start) return;
    p->rel[p->rel_next++ % PERF_RELS] = (PerfRel){start, end, outputs, state};
}
/* a tail's window [t0, now) split by the release thread's intervals in it, the rest counted as other */
static void rel_window(PerfTimes* p, uint64_t t0, uint64_t now) {
    static const int sites[REL_STATES] = {PT_WIN_GPU, PT_WIN_LOCK, PT_WIN_HOLD};
    uint64_t in[REL_STATES] = {0}, outputs = 0, covered = 0;
    const uint64_t n = p->rel_next < PERF_RELS ? p->rel_next : PERF_RELS;
    for (uint64_t k = 0; k < n; k++) {
        const PerfRel* r = &p->rel[(p->rel_next - 1 - k) % PERF_RELS];
        if (r->end <= t0) break; /* newest first: the rest are older */
        const uint64_t a = r->start > t0 ? r->start : t0, b = r->end < now ? r->end : now;
        if (b <= a) continue;
        in[r->state] += b - a;
        covered += b - a;
        if (r->state == REL_HOLD) outputs += r->outputs;
    }
    for (int s = 0; s < REL_STATES; s++) perf_time_add(p, sites[s], in[s]);
    perf_time_add(p, PT_WIN_OTHER, now > t0 + covered ? now - t0 - covered : 0);
    perf_count(p, PC_WIN_OUTPUTS, outputs);
}
void perf_tail_done(PerfTimes* p, uint64_t value, uint64_t now) {
    for (unsigned i = 0; i < PERF_TAILS; i++) {
        PerfTail* t = &p->tail[i];
        const uint64_t gate = LOAD(t->gate);
        if (LOAD(t->armed) && gate && gate <= value && stamp(&t->t_done, now)) rel_window(p, LOAD(t->t0), now);
    }
}
/* PERF-33: the frame of tail t at its increment, when every landing of its submissions is in (they come first) */
static void handoff_frame(PerfTimes* p, PerfHandoff* h, const PerfTail* t, uint64_t now) {
    const uint64_t first = LOAD(t->first), last = LOAD(t->last), t0 = LOAD(t->t0);
    if (!first || last < first || last - first >= PERF_SUBS) return;
    uint64_t subs = 0, carry = 0, newest = 0, newest_noimg = 0, landed = 0;
    for (uint64_t v = first; v <= last; v++) {
        const PerfSub* s = &p->sub[v % PERF_SUBS];
        if (LOAD(s->v) != v) {
            __atomic_fetch_add(&h->lost, 1, __ATOMIC_RELAXED);
            continue;
        }
        subs++;
        const int c = LOAD(s->carry_v) == v ? LOAD(s->carry) : 0;
        if (!c) continue;
        carry++, newest = v;
        if (c & ~PCY_IMAGE) newest_noimg = v;
        for (int k = 0; k < PCY_KINDS; k++)
            if (c & 1 << k) __atomic_fetch_add(&h->kinds[k], 1, __ATOMIC_RELAXED);
        if (LOAD(s->landed) > landed) landed = LOAD(s->landed);
    }
    const uint64_t pos = !newest ? PH_POS : last - newest < PH_POS - 1 ? last - newest : PH_POS - 1;
    const uint64_t pos_noimg = !newest_noimg               ? PH_POS
                               : last - newest_noimg < PH_POS - 1 ? last - newest_noimg
                                                                 : PH_POS - 1;
    __atomic_fetch_add(&h->pos_noimg[pos_noimg], 1, __ATOMIC_RELAXED);
    const PerfSub* ls = &p->sub[last % PERF_SUBS];
    if (LOAD(ls->carry_v) == last) /* the last submission's results by kind (research 08:52) */
        for (int k = 0; k < PCY_KINDS; k++) {
            const uint32_t n = LOAD(ls->kind_n[k]);
            __atomic_fetch_add(&h->last_n[k], n, __ATOMIC_RELAXED);
            if (n) __atomic_fetch_add(&h->last_has[k], 1, __ATOMIC_RELAXED);
        }
    const uint64_t last_seen = LOAD(p->sub[last % PERF_SUBS].seen);
    const uint64_t carry_seen = newest ? LOAD(p->sub[newest % PERF_SUBS].seen) : 0;
    __atomic_fetch_add(&h->frames, 1, __ATOMIC_RELAXED);
    __atomic_fetch_add(&h->subs, subs, __ATOMIC_RELAXED);
    __atomic_fetch_add(&h->carry, carry, __ATOMIC_RELAXED);
    __atomic_fetch_add(&h->pos[pos], 1, __ATOMIC_RELAXED);
    if (landed && landed < t0) __atomic_fetch_add(&h->land_before, 1, __ATOMIC_RELAXED);
    __atomic_fetch_add(&h->to_land, landed > t0 ? landed - t0 : 0, __ATOMIC_RELAXED);
    __atomic_fetch_add(&h->to_carry_gpu, carry_seen > t0 ? carry_seen - t0 : 0, __ATOMIC_RELAXED);
    __atomic_fetch_add(&h->to_last_gpu, last_seen > t0 ? last_seen - t0 : 0, __ATOMIC_RELAXED);
    __atomic_fetch_add(&h->to_incr, now > t0 ? now - t0 : 0, __ATOMIC_RELAXED);
    const uint64_t cl = LOAD(h->compute_last);
    if (cl > t0) {
        __atomic_fetch_add(&h->compute_after, 1, __ATOMIC_RELAXED);
        __atomic_fetch_add(&h->compute_ns, cl - t0, __ATOMIC_RELAXED);
    }
}
void perf_tail_performed(PerfTimes* p, uint64_t seq, uint64_t now) {
    for (unsigned i = 0; i < PERF_TAILS; i++) {
        PerfTail* t = &p->tail[i];
        if (LOAD(t->armed) && LOAD(t->seq) == seq && stamp(&t->t_incr, now) && g_perf_probes)
            handoff_frame(p, &g_perf_handoff, t, now);
    }
}
void perf_sub_carry(PerfTimes* p, uint64_t v, int kind) { /* the registrations are under vk_lock */
    PerfSub* s = &p->sub[v % PERF_SUBS];
    if (LOAD(s->carry_v) != v) {
        for (int k = 0; k < PCY_KINDS; k++) __atomic_store_n(&s->kind_n[k], 0, __ATOMIC_RELAXED);
        __atomic_store_n(&s->carry, kind, __ATOMIC_RELAXED);
        __atomic_store_n(&s->carry_v, v, __ATOMIC_RELEASE);
    } else
        __atomic_fetch_or(&s->carry, kind, __ATOMIC_RELAXED);
    for (int k = 0; k < PCY_KINDS; k++)
        if (kind & 1 << k) __atomic_fetch_add(&s->kind_n[k], 1, __ATOMIC_RELAXED);
}
void perf_sub_landed(PerfTimes* p, uint64_t v, uint64_t now) {
    PerfSub* s = &p->sub[v % PERF_SUBS];
    if (LOAD(s->carry_v) != v) __atomic_fetch_add(&g_perf_handoff.unregistered, 1, __ATOMIC_RELAXED);
    if (LOAD(s->v) == v) __atomic_store_n(&s->landed, now, __ATOMIC_RELEASE); /* under vk_lock, so in order */
}
void perf_handoff_compute(PerfHandoff* h, uint64_t now) { __atomic_store_n(&h->compute_last, now, __ATOMIC_RELEASE); }
void perf_handoff_time(PerfHandoff* h, int part, uint64_t ns, uint64_t ops, uint64_t marked) {
    __atomic_fetch_add(&h->n[part], 1, __ATOMIC_RELAXED);
    __atomic_fetch_add(&h->ns[part], ns, __ATOMIC_RELAXED);
    if (ops) __atomic_fetch_add(&h->ops, ops, __ATOMIC_RELAXED);
    if (marked) __atomic_fetch_add(&h->marked, marked, __ATOMIC_RELAXED);
}
void perf_handoff_result(PerfHandoff* h, int kind, uint64_t drawn) {
    for (int k = 0; k < PCY_KINDS; k++)
        if (kind & 1 << k) h->res_at[k] = drawn;
}
void perf_handoff_frame_draws(PerfHandoff* h, uint64_t drawn) {
    for (int k = 0; k < PCY_KINDS; k++) /* each kind's last registration this frame, by its place */
        if (h->res_at[k] >= h->draws0 && h->res_at[k]) {
            __atomic_fetch_add(&h->res_place[k], h->res_at[k] - h->draws0, __ATOMIC_RELAXED);
            __atomic_fetch_add(&h->res_frames[k], 1, __ATOMIC_RELAXED);
            h->res_at[k] = 0;
        }
    __atomic_fetch_add(&h->draws_sum, drawn - h->draws0, __ATOMIC_RELAXED);
    __atomic_fetch_add(&h->presents, 1, __ATOMIC_RELAXED);
    h->frame_draws = drawn - h->draws0;
    if (h->img_open) h->img_last_frame = h->frame_draws, h->img_open = 0; /* the frame of the last image's write */
    h->draws0 = drawn;
}
void perf_handoff_image(PerfHandoff* h, uint64_t drawn, uint64_t gva, uint32_t w, uint32_t hgt, uint32_t fkey) {
    h->img_n++;
    h->img_at += drawn - h->draws0;
    h->img_last_at = drawn - h->draws0, h->img_open = 1;
    h->img_gva = gva, h->img_w = w, h->img_h = hgt, h->img_fkey = fkey;
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

enum { SUB_SEEN = 1, SUB_LAST5 = 2 };
void perf_sub_queued(PerfTimes* p, uint64_t v, int site, uint64_t now) {
    PerfSub* s = &p->sub[v % PERF_SUBS];
    if (site < 0 || site >= (int)SW_SITES) site = SW_OTHER;
    if (LOAD(s->v) && LOAD(s->v) > LOAD(p->sub_seen))
        __atomic_fetch_add(&p->sub_lost, 1, __ATOMIC_RELAXED); /* its completion was never seen */
    __atomic_store_n(&s->v, 0, __ATOMIC_RELEASE);
    __atomic_store_n(&s->state, site << 2, __ATOMIC_RELAXED); /* the site, for frame_end, under the flag bits */
    __atomic_store_n(&s->handed, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&s->seen, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&s->queued, now, __ATOMIC_RELAXED);
    __atomic_store_n(&s->landed, 0, __ATOMIC_RELAXED); /* PERF-33 (its carry is by number: registered before this) */
    __atomic_store_n(&s->v, v, __ATOMIC_RELEASE);
    __atomic_fetch_add(&p->sub_site[site][0], 1, __ATOMIC_RELAXED);
    __atomic_store_n(&p->sub_last, v, __ATOMIC_RELEASE);
}
void perf_sub_handed(PerfTimes* p, uint64_t v, uint64_t now) {
    PerfSub* s = &p->sub[v % PERF_SUBS];
    if (LOAD(s->v) == v) stamp(&s->handed, now);
}
/* one submission's completion seen: its time, and the last five's if frame_end marked it */
static void sub_done(PerfTimes* p, PerfSub* s, uint64_t v, uint64_t now) {
    if (LOAD(s->v) != v) return;
    const uint64_t handed = LOAD(s->handed);
    const uint64_t d = handed && now > handed ? now - handed : 0;
    if (handed)
        perf_time_add(p, PT_SUB_DONE, d);
    else /* the recorder's time not published yet: no sample (reviewer N1) */
        __atomic_fetch_add(&p->sub_unhanded, 1, __ATOMIC_RELAXED);
    __atomic_store_n(&s->seen, now, __ATOMIC_RELAXED); /* published by the fetch_or below */
    const int old = __atomic_fetch_or(&s->state, SUB_SEEN, __ATOMIC_ACQ_REL);
    if (handed && old & SUB_LAST5) perf_time_add(p, PT_SUB_LAST5, d);
}
void perf_sub_seen(PerfTimes* p, uint64_t value, uint64_t now) {
    uint64_t seen = LOAD(p->sub_seen);
    while (value > seen) { /* claim (seen, value]: each submission is done by one caller */
        if (!__atomic_compare_exchange_n(&p->sub_seen, &seen, value, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) continue;
        const uint64_t from = value - seen > PERF_SUBS ? value - PERF_SUBS : seen;
        for (uint64_t v = from + 1; v <= value; v++) sub_done(p, &p->sub[v % PERF_SUBS], v, now);
        return;
    }
}
void perf_sub_frame_end(PerfTimes* p, uint64_t now) {
    unsigned marked = 0;
    for (uint64_t v = LOAD(p->sub_last), n = 0; v && n < PERF_SUBS; v--, n++) {
        PerfSub* s = &p->sub[v % PERF_SUBS];
        const uint64_t queued = LOAD(s->queued);
        if (LOAD(s->v) != v || queued <= p->sub_end) break; /* the frame before */
        const int site = LOAD(s->state) >> 2;
        for (unsigned k = 0; k < PERF_SUB_LEADS; k++)
            if (now - queued <= (1000000ull << k))
                __atomic_fetch_add(&p->sub_site[site][1 + k], 1, __ATOMIC_RELAXED);
        if (marked < 5) {
            marked++;
            const int old = __atomic_fetch_or(&s->state, SUB_LAST5, __ATOMIC_ACQ_REL);
            const uint64_t handed = LOAD(s->handed), seen = LOAD(s->seen);
            if (old & SUB_SEEN && handed) perf_time_add(p, PT_SUB_LAST5, seen > handed ? seen - handed : 0);
        }
    }
    p->sub_end = now;
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
    static const char* const names[PT_SITES] = {
        "post_start",   "start_end",       "end_post",       "period",        "idle",        "show",
        "tl_recycle",   "tl_finish",       "tl_submit",      "rec_sync",      "rec_full",    "sync_wait",
        "acquire",      "release_hold",    "pass_end",       "pass_begin",    "rel_lock_all", "src_walk",
        "class_walk",   "compose_empty",   "compose_copy",   "compose_w_empty", "compose_w_copy", "tail_submit",
        "tail_done",    "tail_incr",       "tail_free",      "tail_wake",     "sub_done",    "sub_last5",
        "win_gpu",      "win_lock",        "win_hold",       "win_other"};
    static const char* const sites[SW_SITES] = {"other",   "slice", "release", "finish",  "frame",
                                                "present", "image", "compute", "readback"};
    static const char* const counts[PC_COUNTS] = {
        "wb_outputs",  "wb_bytes",    "wc_bytes",   "tail_submits", "tail_after",   "passes",       "end_other",
        "end_gpu_src", "end_copy",    "end_image",  "end_target",   "end_query",    "src_barriers", "src_clean",
        "src_unknown", "rel_depth",   "win_outputs", "src_walks",   "src_results",  "tail_ungated", "tail_partial",
        "tail_lost"};
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
    /* a window's submissions by site, a frame's mean: all, then those in the last 1, 2 and 4 ms before frame_end */
    for (int i = 0; i < (int)SW_SITES; i++) {
        uint64_t c[1 + PERF_SUB_LEADS];
        for (unsigned k = 0; k <= PERF_SUB_LEADS; k++)
            c[k] = __atomic_exchange_n(&p->sub_site[i][k], 0, __ATOMIC_RELAXED);
        if (!c[0] || at >= size) continue;
        n = snprintf(out + at, size - at, " sub_%s=%.1f/%.1f/%.1f/%.1f", sites[i], (double)c[0] / frames,
                     (double)c[1] / frames, (double)c[2] / frames, (double)c[3] / frames);
        if (n > 0) at += (size_t)n;
    }
    const uint64_t sub_lost = __atomic_exchange_n(&p->sub_lost, 0, __ATOMIC_RELAXED);
    const uint64_t sub_unhanded = __atomic_exchange_n(&p->sub_unhanded, 0, __ATOMIC_RELAXED);
    if (at < size) {
        n = snprintf(out + at, size - at, " sub_lost=%llu sub_unhanded=%llu", (unsigned long long)sub_lost,
                     (unsigned long long)sub_unhanded);
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

PerfCopies g_perf_cp = {.site = -1, .slot = -1, .draw = -1, .last = -1};

/* a window's counts: not elsewhere, which other threads add to, nor the totals since the start */
static void copies_clear(PerfCopies* c) {
    memset(c, 0, offsetof(PerfCopies, elsewhere));
    c->site = c->slot = c->draw = c->last = -1;
}
void perf_copies_reset(PerfCopies* c) {
    copies_clear(c);
    __atomic_store_n(&c->elsewhere, 0, __ATOMIC_RELAXED);
    c->atlas_total = 0;
}
static int target_eq(const PerfTarget* a, const PerfTarget* b) {
    return a->gva == b->gva && a->w == b->w && a->h == b->h && a->fmt == b->fmt;
}
/* t's slot, claimed when it has none: -1 for no target or no slot left */
static int copy_slot(PerfCopies* c, const PerfTarget* t) {
    if (!t || !t->gva) return -1;
    if (c->last >= 0 && target_eq(&c->tg[c->last].t, t)) return c->last;
    for (int i = 0; i < (int)PERF_COPY_TARGETS; i++) {
        PerfCopyTarget* g = &c->tg[i];
        if (g->t.gva && !target_eq(&g->t, t)) continue;
        if (!g->t.gva) g->t = *t;
        return c->last = i;
    }
    c->lost++;
    return -1;
}
void perf_copy_begin(PerfCopies* c, int site) {
    c->site = site >= 0 && site < PK_SITES ? site : -1;
    c->slot = -1;
    if (c->site >= 0) c->ops[c->site]++;
}
void perf_copy_bytes(PerfCopies* c, uint64_t n) {
    if (c->site < 0) return;
    c->bytes[c->site] += n;
    if (c->slot >= 0) c->tg[c->slot].bytes[c->site] += n;
}
void perf_copy_pass_begin(PerfCopies* c, const PerfTarget* t) {
    const int i = copy_slot(c, t);
    if (i >= 0) c->tg[i].passes++;
}
void perf_copy_pass_end(PerfCopies* c, const PerfTarget* t, int why) {
    if (why < 0 || why >= PE_N) why = PE_OTHER;
    const int i = copy_slot(c, t);
    c->pass_end_n++;
    c->end_slot = i;
    if (i >= 0) c->tg[i].why[why]++;
    if (why != PE_COPY || c->site < 0) return;
    c->ends[c->site]++;
    c->slot = i;
    if (i >= 0) c->tg[i].ends[c->site]++;
}
void perf_copy_draw(PerfCopies* c, const PerfTarget* t) {
    c->draw = t ? copy_slot(c, t) : -1;
    if (c->draw >= 0) c->tg[c->draw].draws++;
}
void perf_copy_src(PerfCopies* c, int kind, int outcome) {
    if (kind < 0 || kind >= PB_KINDS || outcome < 0 || outcome >= PS_N) return;
    c->src[kind][outcome]++;
    c->bind[g_perf_bind > 0 && g_perf_bind < PBK_KINDS ? g_perf_bind : PBK_OTHER][outcome]++;
    if (c->draw >= 0)
        c->tg[c->draw].src[kind][outcome]++;
    else
        c->nodraw++;
}

void perf_copy_class(PerfCopies* c, int cls) {
    if (cls < 0 || cls >= PI_CLASSES) return;
    c->cls[cls]++;
    if (c->draw >= 0) c->tg[c->draw].cls[cls]++;
}
void perf_copy_empty(PerfCopies* c, int site, uint64_t bytes) {
    if (site < 0 || site >= PK_SITES) return;
    c->empty[site]++;
    c->empty_bytes[site] += bytes;
}
void perf_copy_extra(PerfCopies* c, int which, uint64_t n) {
    if (which >= 0 && which < PX_N) c->extra[which] += n;
}
void perf_copy_made(PerfCopies* c, int producer) {
    if (producer >= 0 && producer < PR_N) c->made[producer]++;
    if (producer == PR_ATLAS) c->atlas_total++;
}
void perf_copy_writable(PerfCopies* c, int path, const uint64_t* ns, uint64_t bytes) {
    if (path < 0 || path >= PW_PATHS) return;
    for (int k = 0; k < PWS_N; k++) c->wr_ns[path][k] += ns[k];
    c->wr_calls[path]++;
    c->wr_bytes[path] += bytes;
}
void perf_copy_xfb(PerfCopies* c, int cls, uint64_t size, uint64_t captured) {
    if (cls < 0 || cls >= PXF_N) return;
    c->xfb[cls]++;
    c->xfb_bound += size;
    c->xfb_captured += captured;
}
uint64_t perf_xfb_vertices(int prim, uint64_t count, uint64_t instances) {
    uint64_t n;
    switch (prim) {
        case PXP_POINTS: n = count; break;
        case PXP_LINES: n = count - count % 2; break;
        case PXP_LINE_STRIP: n = count >= 2 ? (count - 1) * 2 : 0; break;
        case PXP_TRIANGLES: n = count - count % 3; break;
        case PXP_TRIANGLE_STRIP:
        case PXP_TRIANGLE_FAN: n = count >= 3 ? (count - 2) * 3 : 0; break;
        default: return ~0ull;
    }
    return n * instances;
}
PerfLanded g_perf_landed[PERF_LANDED];
static uint32_t landed_home(uint64_t page) { return (uint32_t)((page * 0x9E3779B97F4A7C15ull) >> 51); }
#define LANDED_PROBE 8u
static uint64_t g_landed_seq;
void perf_landed_add(PerfLanded* t, uint64_t start, uint64_t size, uint64_t frame, uint64_t made, uint64_t submit,
                     uint64_t gen, uint32_t wclock) {
    if (!size) return;
    const uint64_t seq = ++g_landed_seq;
    const uint64_t first = start >> 12, last = (start + size - 1) >> 12;
    for (uint64_t p = first; p <= last && p - first < PERF_LANDED_PAGES; p++) {
        uint32_t at = landed_home(p), pick = at;
        for (uint32_t k = 0; k < LANDED_PROBE; k++) { /* a free or stale slot, else the oldest of the probe */
            PerfLanded* e = &t[(at + k) % PERF_LANDED];
            if (!e->end || e->frame + 1 < frame) {
                pick = (at + k) % PERF_LANDED;
                break;
            }
            if (e->frame < t[pick].frame) pick = (at + k) % PERF_LANDED;
        }
        t[pick] = (PerfLanded){p, start, start + size, frame, made, submit, gen, seq, wclock, 0};
    }
}
/* the newest result holding byte a, landed in frame or the one before */
static const PerfLanded* landed_at(const PerfLanded* t, uint64_t a, uint64_t frame) {
    const uint64_t page = a >> 12;
    const PerfLanded* best = NULL;
    for (uint32_t k = 0; k < LANDED_PROBE; k++) {
        const PerfLanded* e = &t[(landed_home(page) + k) % PERF_LANDED];
        if (e->end && e->page == page && e->frame + 1 >= frame && e->start <= a && a < e->end &&
            (!best || e->seq > best->seq))
            best = e;
    }
    return best;
}
int perf_landed_find(const PerfLanded* t, uint64_t gva, uint64_t len, uint64_t frame, PerfLanded r[2]) {
    if (!len) return PLK_NONE;
    const PerfLanded* a = landed_at(t, gva, frame);
    if (!a) return PLK_NONE;
    r[0] = *a;
    if (gva + len <= a->end) return a->start == gva && a->end == gva + len ? PLK_WHOLE : PLK_PART;
    const PerfLanded* b = landed_at(t, a->end, frame); /* one holding the first's end byte: it adjoins or overlaps */
    if (!b || gva + len > b->end) return PLK_NONE;
    r[1] = *b;
    return PLK_TWO;
}
int perf_landed_later(const PerfLanded* t, uint64_t seq, uint64_t gva, uint64_t len, uint64_t frame) {
    for (uint64_t p = gva >> 12; len && p <= (gva + len - 1) >> 12; p++)
        for (uint32_t k = 0; k < LANDED_PROBE; k++) {
            const PerfLanded* e = &t[(landed_home(p) + k) % PERF_LANDED];
            if (e->end && e->page == p && e->frame + 1 >= frame && e->seq > seq && e->start < gva + len &&
                gva < e->end)
                return 1;
        }
    return 0;
}
uint64_t perf_landed_keep(PerfLanded* t, const PerfLanded* r) {
    const uint64_t page = r->start >> 12;
    for (uint32_t k = 0; k < LANDED_PROBE; k++) {
        PerfLanded* e = &t[(landed_home(page) + k) % PERF_LANDED];
        if (e->end && e->page == page && e->seq == r->seq) {
            if (e->kept) return 0;
            e->kept = 1;
            return r->end - r->start;
        }
    }
    return r->end - r->start; /* its first page's slot taken since: counted again, an upper bound */
}
int perf_landed_inside(const PerfLanded* t, uint64_t gva, uint64_t len, uint64_t frame) {
    const uint64_t page = gva >> 12;
    const uint32_t at = landed_home(page);
    for (uint32_t k = 0; k < LANDED_PROBE; k++) {
        const PerfLanded* e = &t[(at + k) % PERF_LANDED];
        if (e->end && e->page == page && e->frame + 1 >= frame && e->start <= gva && gva + len <= e->end) return 1;
    }
    return 0;
}
void perf_copy_own(PerfCopies* c, int what, int why, uint64_t bytes, int landed, const uint64_t* ns) {
    if (what < 0 || what >= PO_N) return;
    if (why < 0 || why >= POW_N) why = POW_NEW;
    c->own_n[what][why]++;
    c->own_bytes[what] += bytes;
    c->own_landed[what] += !!landed;
    for (int k = 0; k < POT_N; k++) c->own_ns[what][k] += ns[k];
    if (c->draw >= 0) {
        c->tg[c->draw].own[what]++;
        c->tg[c->draw].own_landed[what] += !!landed;
    }
}
void perf_copy_own_drop(PerfCopies* c, uint64_t uses) {
    const unsigned b = uses == 0 ? 0 : uses == 1 ? 1 : uses < 4 ? 2 : uses < 16 ? 3 : uses < 64 ? 4 : 5;
    c->own_uses[b]++;
}

/* PERF-23: the stamps by page. A page's slot is found within STAMP_PROBE of its home, and a page that makes room
   leaves its newest generation in gone[] by a hash of its own, so a page not found, or made again since (floor), is
   known clean only for what is asked after that */
PerfStamps g_perf_stamps, g_perf_real;
#define STAMP_PROBE 16u
#define STAMP_GONE (1u << 16)
#define STAMP_SPAN_MAX (1u << 18) /* pages of one write whose span is kept: past them every span is unknown */
static uint32_t stamp_home(uint64_t page) { return (uint32_t)((page * 0x9E3779B97F4A7C15ull) >> 47); }
static uint32_t stamp_gone_at(uint64_t page) { return (uint32_t)((page * 0xC2B2AE3D27D4EB4Full) >> 48); }
static PerfStampPage* stamp_find(const PerfStamps* t, uint64_t page) {
    const uint32_t at = stamp_home(page);
    for (uint32_t k = 0; k < STAMP_PROBE; k++) {
        PerfStampPage* p = &t->page[(at + k) % PERF_STAMP_SLOTS];
        if (p->page == page + 1) return p;
    }
    return NULL;
}
static PerfStampPage* stamp_slot(PerfStamps* t, uint64_t page) {
    const uint32_t at = stamp_home(page);
    PerfStampPage* pick = NULL;
    for (uint32_t k = 0; k < STAMP_PROBE; k++) {
        PerfStampPage* p = &t->page[(at + k) % PERF_STAMP_SLOTS];
        if (p->page == page + 1) return p;
        if (!p->page) {
            if (!pick || pick->page) pick = p;
        } else if (!pick || (pick->page && p->newest < pick->newest))
            pick = p;
    }
    if (pick->page) {
        uint64_t* g = &t->gone[stamp_gone_at(pick->page - 1)];
        if (pick->newest > *g) *g = pick->newest;
    }
    *pick = (PerfStampPage){.page = page + 1, .floor = t->gone[stamp_gone_at(page)]};
    return pick;
}
/* [lo, hi) of the page written at gen: the runs it covers give way, and past PERF_STAMP_RUNS the two closest become
   one run over both and the gap between them, with the newer generation */
static void stamp_runs(PerfStamps* t, PerfStampPage* p, uint16_t lo, uint16_t hi, uint64_t gen, int kind) {
    PerfStampRun r[PERF_STAMP_RUNS + 2];
    uint32_t n = 0;
    for (uint32_t i = 0; i < p->n; i++) {
        const PerfStampRun o = p->r[i];
        if (o.hi <= lo || o.lo >= hi) {
            r[n++] = o;
            continue;
        }
        if (o.lo < lo) r[n++] = (PerfStampRun){o.gen, o.lo, lo, o.kinds};
        if (o.hi > hi) r[n++] = (PerfStampRun){o.gen, hi, o.hi, o.kinds};
    }
    r[n++] = (PerfStampRun){gen, lo, hi, (uint8_t)(1u << kind)};
    for (uint32_t i = 1; i < n; i++) /* by start */
        for (uint32_t j = i; j && r[j - 1].lo > r[j].lo; j--) {
            const PerfStampRun x = r[j];
            r[j] = r[j - 1];
            r[j - 1] = x;
        }
    while (n > PERF_STAMP_RUNS) {
        uint32_t b = 0;
        for (uint32_t i = 1; i + 1 < n; i++)
            if (r[i + 1].lo - r[i].hi < r[b + 1].lo - r[b].hi) b = i;
        r[b].hi = r[b + 1].hi;
        if (r[b + 1].gen > r[b].gen) r[b].gen = r[b + 1].gen;
        r[b].kinds |= r[b + 1].kinds;
        memmove(&r[b + 1], &r[b + 2], (n - b - 2) * sizeof *r);
        n--;
        __atomic_fetch_add(&t->merged, 1, __ATOMIC_RELAXED);
    }
    memcpy(p->r, r, n * sizeof *r);
    p->n = n;
    if (gen > p->newest) p->newest = gen;
}
/* a page's span: 255 minus its first 16 byte step, then its last, so a zeroed page has none */
static void span_add(PerfStamps* t, uint64_t page, uint32_t lo, uint32_t hi) {
    uint8_t** leaf = &t->span[(page >> 12) & 0xFFFF];
    if (page >> 28 || (!*leaf && !(*leaf = calloc(4096, 2)))) {
        __atomic_store_n(&t->span_lost, 1, __ATOMIC_RELAXED);
        return;
    }
    uint8_t* s = *leaf + (page & 4095) * 2;
    const unsigned first = lo / 16, last = (hi - 1) / 16;
    if (255u - s[0] > s[1]) { /* none yet */
        s[0] = (uint8_t)(255 - first);
        s[1] = (uint8_t)last;
        return;
    }
    if (first < 255u - s[0]) s[0] = (uint8_t)(255 - first);
    if (last > s[1]) s[1] = (uint8_t)last;
}
void perf_stamp_add(PerfStamps* t, uint64_t a, uint64_t size, uint64_t gen, int kind) {
    if (!size || __atomic_load_n(&t->failed, __ATOMIC_RELAXED) || kind < 0 || kind >= PSK_N) return;
    if (!t->page) {
        t->page = calloc(PERF_STAMP_SLOTS, sizeof *t->page);
        t->gone = calloc(STAMP_GONE, sizeof *t->gone);
        if (!t->page || !t->gone) {
            __atomic_store_n(&t->failed, 1, __ATOMIC_RELAXED);
            return;
        }
    }
    __atomic_fetch_add(&t->stamps, 1, __ATOMIC_RELAXED);
    const uint64_t first = a >> 12, last = (a + size - 1) >> 12;
    if (last - first < STAMP_SPAN_MAX)
        for (uint64_t p = first; p <= last; p++)
            span_add(t, p, p == first ? (uint32_t)(a & 0xFFF) : 0,
                     p == last ? (uint32_t)((a + size - 1) & 0xFFF) + 1 : 4096);
    else
        __atomic_store_n(&t->span_lost, 1, __ATOMIC_RELAXED);
    if (last - first >= PERF_STAMP_WIDE) {
        PerfStampWide* w = &t->wide[t->nwide++ % PERF_STAMP_WIDES];
        if (t->nwide > PERF_STAMP_WIDES && w->gen > t->wide_gone) t->wide_gone = w->gen;
        *w = (PerfStampWide){a, a + size, gen, (uint8_t)(1u << kind)};
        __atomic_fetch_add(&t->wides, 1, __ATOMIC_RELAXED);
        return;
    }
    for (uint64_t p = first; p <= last; p++) {
        stamp_runs(t, stamp_slot(t, p), p == first ? (uint16_t)(a & 0xFFF) : 0,
                   p == last ? (uint16_t)(((a + size - 1) & 0xFFF) + 1) : 4096, gen, kind);
        __atomic_fetch_add(&t->pages, 1, __ATOMIC_RELAXED);
    }
}
int perf_stamp_ask(const PerfStamps* t, uint64_t since, uint64_t a, uint64_t len, int* outside, unsigned* kinds) {
    *outside = -1;
    *kinds = 0;
    if (!len) return PSA_NONE;
    if (__atomic_load_n(&t->failed, __ATOMIC_RELAXED) || !t->page) return PSA_UNKNOWN;
    const uint64_t first = a >> 12, last = (a + len - 1) >> 12;
    int in = 0, unknown = 0, out = __atomic_load_n(&t->span_lost, __ATOMIC_RELAXED) ? -1 : 1;
    for (uint64_t p = first; p <= last; p++) {
        const uint32_t lo = p == first ? (uint32_t)(a & 0xFFF) : 0;
        const uint32_t hi = p == last ? (uint32_t)((a + len - 1) & 0xFFF) + 1 : 4096;
        const PerfStampPage* e = stamp_find(t, p);
        if (e) {
            unknown |= e->floor > since;
            for (uint32_t i = 0; i < e->n; i++)
                if (e->r[i].gen > since) {
                    *kinds |= e->r[i].kinds;
                    in |= e->r[i].lo < hi && e->r[i].hi > lo;
                }
        } else if (t->gone[stamp_gone_at(p)] > since)
            unknown = 1;
        const uint8_t* leaf = out == 1 ? t->span[(p >> 12) & 0xFFFF] : NULL;
        if (leaf && !(p >> 28)) {
            const uint8_t* s = leaf + (p & 4095) * 2;
            if (255u - s[0] <= s[1] && (255u - s[0]) * 16 < hi && (s[1] + 1u) * 16 > lo) out = 0;
        }
    }
    for (uint64_t i = 0; i < t->nwide && i < PERF_STAMP_WIDES; i++) {
        const PerfStampWide* w = &t->wide[i];
        if (w->gen <= since || w->start >= (last + 1) << 12 || w->end <= first << 12) continue;
        *kinds |= w->kinds;
        in |= w->start < a + len && w->end > a;
    }
    if (t->wide_gone > since) unknown = 1;
    *outside = out;
    return in ? PSA_IN : unknown ? PSA_UNKNOWN : PSA_NONE;
}
void perf_copy_steps(PerfCopies* c, int what, const uint64_t* ns, const uint8_t* ran) {
    if (what < 0 || what >= PO_N) return;
    for (int k = 0; k < POS_N; k++) {
        c->step_ns[what][k] += ns[k];
        c->step_n[what][k] += ran[k];
    }
}
void perf_copy_gsame(PerfCopies* c, int real, int q, unsigned kinds) {
    if (q < 0 || q >= PSQ_N) return;
    real = !!real;
    c->gsame[real][q]++;
    if (q <= PSQ_SPAN_UNKNOWN)
        for (int k = 0; k < PSK_N; k++) c->gkinds[real][k] += kinds >> k & 1;
}
void perf_copy_cleared(PerfCopies* c, unsigned tests) { c->cleared[tests & 7]++; }
void perf_copy_probe(PerfCopies* c, uint64_t ns) {
    c->probe_ns += ns;
    c->probe_n++;
}
void perf_copy_csame(PerfCopies* c, int tracked, int big, uint64_t bytes, uint64_t written) {
    c->csame[!tracked]++;
    c->cbig += !!big;
    c->cbytes += bytes;
    c->cbytes_written += written;
}
void perf_copy_sample_skip(PerfCopies* c) { c->sample_skips++; }
void perf_copy_landed_kept(PerfCopies* c, uint64_t bytes) { c->lnd_kept += bytes; }
void perf_copy_landed(PerfCopies* c, int made, int kind, int age, int freed, int clear, unsigned wrote, uint64_t ns,
                      uint64_t ends, int slot) {
    made = !!made;
    if (kind < PLK_WHOLE || kind > PLK_TWO || age < 0 || age > 3 || freed < 0 || freed > 4 || clear < 0 || clear > 2)
        return;
    c->lnd_n[made][kind - 1]++;
    c->lnd_age[made][age]++;
    c->lnd_freed[made][freed]++;
    c->lnd_clear[made][clear]++;
    for (int k = 0; k < PLW_N; k++) c->lnd_wrote[made][k] += wrote >> k & 1;
    c->lnd_ns[made] += ns;
    c->lnd_ends[made] += ends;
    if (ends && slot >= 0 && slot < (int)PERF_COPY_TARGETS) c->tg[slot].lnd_ends += ends;
}
void perf_copy_quiet(PerfCopies* c, uint64_t ns) {
    c->quiet++;
    c->quiet_ns += ns;
}
void perf_copy_wpage(PerfCopies* c, uint64_t page, uint64_t frame) {
    const uint32_t at = (uint32_t)((page * 0x9E3779B97F4A7C15ull) >> 52);
    for (uint32_t k = 0; k < 16; k++) {
        PerfWPage* w = &c->wpage[(at + k) % PERF_WPAGES];
        if (w->page == page + 1) {
            if (w->last != frame) {
                w->last = frame;
                w->frames++;
            }
            return;
        }
        if (!w->page) {
            w->page = page + 1;
            w->last = frame;
            w->frames = 1;
            return;
        }
    }
    c->wpages_lost++;
}
static int result_covers(const PerfResult* r, uint64_t gva, uint64_t len) {
    return gva >= r->addr && gva + len <= r->addr + r->size;
}
int perf_inplace_class(const PerfResult* r, uint32_t n, uint64_t gva, uint64_t len, uint64_t exact) {
    if (!n) return PI_NONE;
    if (n > PERF_RESULTS) return PI_SEVERAL;
    if (result_covers(&r[n - 1], gva, len)) return PI_ONE;
    if (exact && exact < len)
        for (uint32_t i = n; i--;) /* the newest sharing a byte at the exact length */
            if (r[i].exact) {
                if (result_covers(&r[i], gva, exact)) return PI_ONE_EXACT;
                break;
            }
    if (n == 1) return PI_PART;
    for (uint32_t i = 0; i < n; i++)
        if (!r[i].same || r[i].buf != r[0].buf || r[i].off - r[i].addr != r[0].off - r[0].addr) return PI_SEVERAL;
    for (uint64_t at = gva, end = gva + len; at < end;) { /* no hole: each byte from at on is in one of them */
        uint64_t next = at;
        for (uint32_t i = 0; i < n; i++)
            if (r[i].addr <= at && at < r[i].addr + r[i].size && r[i].addr + r[i].size > next)
                next = r[i].addr + r[i].size;
        if (next == at) return PI_SEVERAL;
        at = next;
    }
    return PI_RUN;
}

static size_t putf(char* out, size_t size, size_t at, const char* fmt, ...) __attribute__((format(printf, 4, 5)));
static size_t putf(char* out, size_t size, size_t at, const char* fmt, ...) {
    if (at >= size) return at;
    va_list ap;
    va_start(ap, fmt);
    const int n = vsnprintf(out + at, size - at, fmt, ap);
    va_end(ap);
    return n > 0 ? at + (size_t)n : at;
}
/* what a target is printed by: its copy pass ends and its reads over results that were not bound in place */
static uint64_t copy_score(const PerfCopyTarget* g) {
    uint64_t s = 0;
    for (int k = 0; k < PK_SITES; k++) s += g->ends[k];
    for (int b = 0; b < PB_KINDS; b++)
        for (int o = PS_REUSE + 1; o < PS_N; o++) s += g->src[b][o];
    return s;
}
static size_t put_src(char* out, size_t size, size_t at, const char* name, const uint64_t* v, double f) {
    uint64_t any = 0;
    for (int o = 0; o < PS_N; o++) any |= v[o];
    if (!any) return at;
    at = putf(out, size, at, " %s=", name);
    for (int o = 0; o < PS_N; o++) at = putf(out, size, at, o ? "/%.1f" : "%.1f", (double)v[o] / f);
    return at;
}
size_t perf_copies_line(PerfCopies* c, uint32_t frames, char* out, size_t size) {
    static const char* const sites[PK_SITES] = {"compose", "compose_w", "atlas", "owned_base",
                                                "patch",   "own",       "owned_read", "dma"};
    static const char* const kinds[PB_KINDS] = {"in", "rd", "wr"};
    if (!size) return 0;
    const double f = frames ? frames : 1;
    size_t at = putf(out, size, 0, "perf-copies: frames=%u (a frame: ops/pass ends/KB, ends other/gpu_src/copy/"
                     "image/target/query, reads reuse/several/part/align/submit/old/changed)", frames ? frames : 1);
    /* first, so a line cut at its size keeps them */
    at = putf(out, size, at, " lost=%llu nodraw=%.1f elsewhere=%llu", (unsigned long long)c->lost, c->nodraw / f,
              (unsigned long long)__atomic_exchange_n(&c->elsewhere, 0, __ATOMIC_RELAXED));
    for (int k = 0; k < PK_SITES; k++)
        if (c->ops[k])
            at = putf(out, size, at, " %s=%.1f/%.1f/%.1f", sites[k], c->ops[k] / f, c->ends[k] / f,
                      c->bytes[k] / f / 1024);
    for (int b = 0; b < PB_KINDS; b++) at = put_src(out, size, at, kinds[b], c->src[b], f);
    int shown[PERF_COPY_PRINT];
    unsigned nshown = 0;
    for (; nshown < PERF_COPY_PRINT; nshown++) { /* the highest scores, each once, ties to the lower slot */
        int best = -1;
        for (int i = 0; i < (int)PERF_COPY_TARGETS; i++) {
            int seen = 0;
            for (unsigned j = 0; j < nshown; j++) seen |= shown[j] == i;
            if (seen || !c->tg[i].t.gva || !copy_score(&c->tg[i])) continue;
            if (best < 0 || copy_score(&c->tg[i]) > copy_score(&c->tg[best])) best = i;
        }
        if (best < 0) break;
        shown[nshown] = best;
    }
    for (unsigned j = 0; j < nshown; j++) {
        const PerfCopyTarget* g = &c->tg[shown[j]];
        at = putf(out, size, at, " [%llx %ux%u f%u] draws=%.1f passes=%.1f ends", (unsigned long long)g->t.gva, g->t.w,
                  g->t.h, g->t.fmt, g->draws / f, g->passes / f);
        for (int w = 0; w < PE_N; w++) at = putf(out, size, at, "%s%.1f", w ? "/" : "=", g->why[w] / f);
        for (int k = 0; k < PK_SITES; k++)
            if (g->ends[k])
                at = putf(out, size, at, " end_%s=%.1f/%.1f", sites[k], g->ends[k] / f, g->bytes[k] / f / 1024);
        for (int b = 0; b < PB_KINDS; b++) at = put_src(out, size, at, kinds[b], g->src[b], f);
    }
    copies_clear(c);
    return at < size ? at : size - 1;
}

static size_t put_counts(char* out, size_t size, size_t at, const char* name, const uint64_t* v, int n, double f) {
    uint64_t any = 0;
    for (int i = 0; i < n; i++) any |= v[i];
    if (!any) return at;
    at = putf(out, size, at, " %s=", name);
    for (int i = 0; i < n; i++) at = putf(out, size, at, i ? "/%.1f" : "%.1f", (double)v[i] / f);
    return at;
}
static uint64_t class_score(const PerfCopyTarget* g) {
    uint64_t s = 0;
    for (int k = 0; k < PI_CLASSES; k++) s += g->cls[k];
    return s;
}
size_t perf_inplace_line(const PerfCopies* c, uint32_t frames, char* out, size_t size) {
    static const char* const sites[PK_SITES] = {"compose", "compose_w", "atlas", "owned_base",
                                                "patch",   "own",       "owned_read", "dma"};
    static const char* const binds[PBK_KINDS] = {"b_other",   "b_vertex", "b_index",   "b_const",
                                                 "b_storage", "b_xfb",    "b_dispatch"};
    if (!size) return 0;
    const double f = frames ? frames : 1;
    size_t at = putf(out, size, 0, "perf-inplace: frames=%u level=%d (a frame: classes none/one/one_exact/run/"
                     "several/part, bindings reuse/several/part/align/submit/old/changed, empty ops/KB, made "
                     "ordinary/owned/composed/xfb/dispatch/atlas/inplace/repeat)",
                     frames ? frames : 1, g_perf_inplace);
    at = putf(out, size, at, " last_pending=%.1f exact=%.1f barriers=%.1f skipped=%.1f wide=%.1f",
              c->extra[PX_LAST_PENDING] / f, c->extra[PX_EXACT] / f, c->extra[PX_BARRIER] / f,
              c->extra[PX_SKIPPED] / f, c->extra[PX_WIDE] / f);
    at = putf(out, size, at, " overlay_w_entered=%.1f skipped=%.1f", c->extra[PX_OVERLAY_ENTERED] / f,
              c->extra[PX_OVERLAY_SKIPPED] / f);
    at = putf(out, size, at, " query_reports=%.1f kept=%.1f guard=%.1f atlas_total=%llu",
              c->extra[PX_QUERY_REPORTS] / f, c->extra[PX_QUERY_KEPT] / f, c->extra[PX_QUERY_GUARD] / f,
              (unsigned long long)c->atlas_total);
    at = put_counts(out, size, at, "made", c->made, PR_N, f);
    at = put_counts(out, size, at, "cls", c->cls, PI_CLASSES, f);
    for (int b = 0; b < PBK_KINDS; b++) at = put_counts(out, size, at, binds[b], c->bind[b], PS_N, f);
    for (int k = 0; k < PK_SITES; k++)
        if (c->empty[k])
            at = putf(out, size, at, " empty_%s=%.1f/%.1f", sites[k], c->empty[k] / f, c->empty_bytes[k] / f / 1024);
    int shown[PERF_COPY_PRINT];
    unsigned nshown = 0;
    for (; nshown < PERF_COPY_PRINT; nshown++) { /* the most classified bindings, each once, ties to the lower slot */
        int best = -1;
        for (int i = 0; i < (int)PERF_COPY_TARGETS; i++) {
            int seen = 0;
            for (unsigned j = 0; j < nshown; j++) seen |= shown[j] == i;
            if (seen || !c->tg[i].t.gva || !class_score(&c->tg[i])) continue;
            if (best < 0 || class_score(&c->tg[i]) > class_score(&c->tg[best])) best = i;
        }
        if (best < 0) break;
        shown[nshown] = best;
    }
    for (unsigned j = 0; j < nshown; j++) {
        const PerfCopyTarget* g = &c->tg[shown[j]];
        at = putf(out, size, at, " [%llx %ux%u f%u]", (unsigned long long)g->t.gva, g->t.w, g->t.h, g->t.fmt);
        at = put_counts(out, size, at, "cls", g->cls, PI_CLASSES, f);
    }
    return at < size ? at : size - 1;
}

size_t perf_writable_line(const PerfCopies* c, uint32_t frames, char* out, size_t size) {
    static const char* const paths[PW_PATHS] = {"plain", "composed_empty", "composed_bytes", "owned"};
    static const char* const xfbs[PXF_N] = {"whole", "short", "stage", "resume", "topology"};
    if (!size) return 0;
    const double f = frames ? frames : 1;
    size_t at = putf(out, size, 0, "perf-writable: frames=%u (a frame: calls/KB, then ms in setup:record:original:"
                     "map:stage:copy:overlay:tail)", frames ? frames : 1);
    for (int p = 0; p < PW_PATHS; p++) {
        if (!c->wr_calls[p]) continue;
        at = putf(out, size, at, " %s=%.1f/%.1f", paths[p], c->wr_calls[p] / f, c->wr_bytes[p] / f / 1024);
        for (int k = 0; k < PWS_N; k++) at = putf(out, size, at, "%s%.3f", k ? ":" : " ", c->wr_ns[p][k] / f / 1e6);
    }
    at = putf(out, size, at, " xfb");
    for (int k = 0; k < PXF_N; k++) at = putf(out, size, at, " %s=%.1f", xfbs[k], c->xfb[k] / f);
    at = putf(out, size, at, " xfb_KB=%.1f captured_KB=%.1f", c->xfb_bound / f / 1024, c->xfb_captured / f / 1024);
    return at < size ? at : size - 1;
}

static uint64_t own_score(const PerfCopyTarget* g) {
    uint64_t s = 0;
    for (int k = 0; k < PO_N; k++) s += g->own[k];
    return s;
}
size_t perf_own_line(const PerfCopies* c, uint32_t frames, char* out, size_t size) {
    static const char* const whats[PO_N] = {"staged", "own_made", "own_same", "owned_read", "patch"};
    if (!size) return 0;
    const double f = frames ? frames : 1;
    size_t at = putf(out, size, 0,
                     "perf-own: frames=%u (a frame: calls by reason new/nobuf/submit/dirty/written/epoch, KB, inside "
                     "a landed result, ms hash+copy:create:rest; draws on an own buffer at its drop "
                     "0/1/2-3/4-15/16-63/64+; by target staged/own_made/own_same/owned_read/patch, then of each "
                     "inside a landed result)",
                     frames ? frames : 1);
    for (int w = 0; w < PO_N; w++) {
        uint64_t n = 0;
        for (int r = 0; r < POW_N; r++) n += c->own_n[w][r];
        if (!n) continue;
        at = put_counts(out, size, at, whats[w], c->own_n[w], POW_N, f);
        at = putf(out, size, at, " KB=%.1f landed=%.1f ms=%.3f:%.3f:%.3f", c->own_bytes[w] / f / 1024,
                  c->own_landed[w] / f, c->own_ns[w][POT_HASH] / f / 1e6, c->own_ns[w][POT_CREATE] / f / 1e6,
                  c->own_ns[w][POT_REST] / f / 1e6);
    }
    at = put_counts(out, size, at, "uses", c->own_uses, PERF_OWN_USES, f);
    int shown[PERF_COPY_PRINT];
    unsigned nshown = 0;
    for (; nshown < PERF_COPY_PRINT; nshown++) { /* the targets with the most slow paths, ties to the lower slot */
        int best = -1;
        for (int i = 0; i < (int)PERF_COPY_TARGETS; i++) {
            int seen = 0;
            for (unsigned j = 0; j < nshown; j++) seen |= shown[j] == i;
            if (seen || !c->tg[i].t.gva || !own_score(&c->tg[i])) continue;
            if (best < 0 || own_score(&c->tg[i]) > own_score(&c->tg[best])) best = i;
        }
        if (best < 0) break;
        shown[nshown] = best;
    }
    for (unsigned j = 0; j < nshown; j++) {
        const PerfCopyTarget* g = &c->tg[shown[j]];
        at = putf(out, size, at, " [%llx %ux%u f%u]", (unsigned long long)g->t.gva, g->t.w, g->t.h, g->t.fmt);
        at = put_counts(out, size, at, "own", g->own, PO_N, f);
        at = put_counts(out, size, at, "landed", g->own_landed, PO_N, f);
    }
    return at < size ? at : size - 1;
}
static size_t put_stamps(char* out, size_t size, size_t at, const char* name, PerfStamps* s, double f) {
    const uint64_t stamps = __atomic_exchange_n(&s->stamps, 0, __ATOMIC_RELAXED);
    const uint64_t pages = __atomic_exchange_n(&s->pages, 0, __ATOMIC_RELAXED);
    const uint64_t merged = __atomic_exchange_n(&s->merged, 0, __ATOMIC_RELAXED);
    const uint64_t wides = __atomic_exchange_n(&s->wides, 0, __ATOMIC_RELAXED);
    return putf(out, size, at, " %s=%.1f/%.1f/%.1f/%.1f%s%s", name, stamps / f, pages / f, merged / f, wides / f,
                __atomic_load_n(&s->failed, __ATOMIC_RELAXED) ? " failed" : "",
                __atomic_load_n(&s->span_lost, __ATOMIC_RELAXED) ? " span_lost" : "");
}
static size_t put_list(char* out, size_t size, size_t at, const char* name, const uint64_t* v, int n, double f) {
    at = putf(out, size, at, " %s=", name);
    for (int i = 0; i < n; i++) at = putf(out, size, at, i ? "/%.1f" : "%.1f", (double)v[i] / f);
    return at;
}
size_t perf_landed_line(PerfCopies* c, uint32_t frames, char* out, size_t size) {
    static const char* const made[2] = {"staged", "own_made"};
    if (!size) return 0;
    const double f = frames ? frames : 1;
    size_t at = putf(out, size, 0,
                     "perf-landed: frames=%u (a frame: buf_get's lookups that end staged or own_made inside a landed "
                     "result, each: the whole result/a part/two results; submissions since it landed 0/1/2-3/more; "
                     "its buffer alive/freed 0/1/2-3/more submissions before; cleared by page tests/with the byte "
                     "record/not; written since by a later result/the front end by page/by byte/the CPU by page; ms "
                     "at the site; passes ended. Then KB kept alive, and the targets of those pass ends)",
                     frames ? frames : 1);
    for (int m = 0; m < 2; m++) {
        if (!(c->lnd_n[m][0] | c->lnd_n[m][1] | c->lnd_n[m][2])) continue;
        at = put_list(out, size, at, made[m], c->lnd_n[m], 3, f);
        at = put_list(out, size, at, "age", c->lnd_age[m], 4, f);
        at = put_list(out, size, at, "buffer", c->lnd_freed[m], 5, f);
        at = put_list(out, size, at, "cleared", c->lnd_clear[m], 3, f);
        at = put_list(out, size, at, "wrote", c->lnd_wrote[m], PLW_N, f);
        at = putf(out, size, at, " ms=%.3f ends=%.1f", c->lnd_ns[m] / f / 1e6, c->lnd_ends[m] / f);
    }
    at = putf(out, size, at, " kept_KB=%.1f", c->lnd_kept / f / 1024);
    for (unsigned n = 0; n < PERF_COPY_PRINT; n++) { /* the most pass ends first */
        int best = -1;
        for (int i = 0; i < (int)PERF_COPY_TARGETS; i++)
            if (c->tg[i].lnd_ends && (best < 0 || c->tg[i].lnd_ends > c->tg[best].lnd_ends)) best = i;
        if (best < 0) break;
        const PerfCopyTarget* g = &c->tg[best];
        at = putf(out, size, at, " [%llx %ux%u f%u] ends=%.1f", (unsigned long long)g->t.gva, g->t.w, g->t.h, g->t.fmt,
                  g->lnd_ends / f);
        c->tg[best].lnd_ends = 0; /* printed: the window's reset clears the rest */
    }
    return at < size ? at : size - 1;
}
size_t perf_same_line(const PerfCopies* c, PerfStamps* s, PerfStamps* real, uint32_t frames, double clock_ns, char* out,
                      size_t size) {
    static const char* const whats[PO_N] = {"staged", "own_made", "own_same", "owned_read", "patch"};
    if (!size) return 0;
    const double f = frames ? frames : 1;
    size_t at = putf(out, size, 0,
                     "perf-same: frames=%u (a frame: own_same after a GPU-side write by the record as stamped and "
                     "as written out/span/span_unknown/in/unknown, of the first three the writers on their pages "
                     "result/gpu/publish/landed; cleared by bytes as stamped/span as stamped/bytes as written/the "
                     "first and third/the second and third of all; after a CPU write tracked/untracked, at or past the patch size, KB, "
                     "KB on pages written; pages written under them in the window: distinct, every frame, half the "
                     "frames or more, not kept; written checks finding nothing, ms; full checks taking no sample; by "
                     "outcome ms and calls by step "
                     "patch/written/why/owned/bytes/hash/sample/use/other; this probe's asks after them, ms; each "
                     "record's writes, pages entered, runs merged, wide runs; ns a clock read)",
                     frames ? frames : 1);
    at = put_counts(out, size, at, "gpu", c->gsame[0], PSQ_N, f);
    at = put_counts(out, size, at, "writers", c->gkinds[0], PSK_N, f);
    at = put_counts(out, size, at, "real", c->gsame[1], PSQ_N, f);
    at = put_counts(out, size, at, "real_writers", c->gkinds[1], PSK_N, f);
    if (c->csame[0] | c->csame[1])
        at = putf(out, size, at, " cpu=%.1f/%.1f big=%.1f KB=%.1f KB_written=%.1f", c->csame[0] / f, c->csame[1] / f,
                  c->cbig / f, c->cbytes / f / 1024, c->cbytes_written / f / 1024);
    uint32_t pages = 0, every = 0, half = 0;
    for (uint32_t i = 0; i < PERF_WPAGES; i++) {
        if (!c->wpage[i].page) continue;
        pages++;
        every += c->wpage[i].frames >= frames;
        half += 2ull * c->wpage[i].frames >= frames;
    }
    if (pages || c->wpages_lost)
        at = putf(out, size, at, " pages=%u/%u/%u/%llu", pages, every, half, (unsigned long long)c->wpages_lost);
    if (c->quiet) at = putf(out, size, at, " quiet=%.1f ms=%.3f", c->quiet / f, c->quiet_ns / f / 1e6);
    if (c->sample_skips) at = putf(out, size, at, " sample_skipped=%.1f", c->sample_skips / f);
    for (int w = 0; w < PO_N; w++) {
        uint64_t any = 0;
        for (int k = 0; k < POS_N; k++) any |= c->step_n[w][k];
        if (!any) continue;
        at = putf(out, size, at, " %s_ms=", whats[w]);
        for (int k = 0; k < POS_N; k++) at = putf(out, size, at, k ? "/%.3f" : "%.3f", c->step_ns[w][k] / f / 1e6);
        at = put_counts(out, size, at, "n", c->step_n[w], POS_N, f);
    }
    uint64_t cl[5] = {0}, cl_n = 0; /* by bytes, by span, as written, bytes and written, span and written */
    for (unsigned b = 0; b < 8; b++) {
        cl_n += c->cleared[b];
        cl[0] += b & PCL_BYTES ? c->cleared[b] : 0;
        cl[1] += b & PCL_SPAN ? c->cleared[b] : 0;
        cl[2] += b & PCL_REAL ? c->cleared[b] : 0;
        cl[3] += (b & (PCL_BYTES | PCL_REAL)) == (PCL_BYTES | PCL_REAL) ? c->cleared[b] : 0;
        cl[4] += (b & (PCL_SPAN | PCL_REAL)) == (PCL_SPAN | PCL_REAL) ? c->cleared[b] : 0;
    }
    if (cl_n && real)
        at = putf(out, size, at, " cleared=%.1f/%.1f/%.1f/%.1f/%.1f of %.1f", cl[0] / f, cl[1] / f, cl[2] / f,
                  cl[3] / f, cl[4] / f, cl_n / f);
    else if (cl_n)
        at = putf(out, size, at, " cleared=%.1f/%.1f/-/-/- of %.1f", cl[0] / f, cl[1] / f, cl_n / f);
    if (c->probe_n) at = putf(out, size, at, " probe=%.1f ms=%.3f", c->probe_n / f, c->probe_ns / f / 1e6);
    at = put_stamps(out, size, at, "stamps", s, f);
    if (real) at = put_stamps(out, size, at, "written", real, f);
    at = putf(out, size, at, " clock=%.1f", clock_ns);
    return at < size ? at : size - 1;
}

static int cap_cmp(const void* x, const void* y) {
    const PerfCapRange *a = x, *b = y;
    return a->a != b->a ? (a->a > b->a) - (a->a < b->a) : (a->n > b->n) - (a->n < b->n);
}
void perf_caps_frame_end(PerfCaps* c) {
    if (!c->ncur) return;
    qsort(c->cur, c->ncur, sizeof *c->cur, cap_cmp);
    uint32_t d = 0;
    for (uint32_t i = 0; i < c->ncur; i++) { /* one range bound twice counts once, front if either was */
        if (d && c->cur[d - 1].a == c->cur[i].a && c->cur[d - 1].n == c->cur[i].n)
            c->cur[d - 1].front |= c->cur[i].front;
        else
            c->cur[d++] = c->cur[i];
    }
    c->count[PCN_DISTINCT] += d;
    for (uint32_t i = 0; i < d; i++) {
        const PerfCapRange* r = &c->cur[i];
        const uint64_t end = r->a + r->n;
        c->count[PCN_BYTES] += r->n;
        c->count[PCN_FRONT] += r->front;
        c->count[PCN_SIZE + (r->n <= 1024 ? 0 : r->n <= 4096 ? 1 : r->n <= 16384 ? 2 : r->n <= 65536 ? 3 : 4)]++;
        if (i + 1 < d) { /* the next one in address */
            const uint64_t next = c->cur[i + 1].a;
            c->count[next < end ? PCN_OVERLAP : next == end ? PCN_ABUT : (end - 1) >> 12 == next >> 12 ? PCN_PAGE
                                                                                                    : PCN_APART]++;
        }
        if (bsearch(r, c->prev, c->nprev, sizeof *c->prev, cap_cmp)) c->count[PCN_RECUR]++;
    }
    memcpy(c->prev, c->cur, d * sizeof *c->cur);
    c->nprev = d;
    c->ncur = 0;
}
void perf_caps_add(PerfCaps* c, uint64_t frame, uint64_t a, uint32_t n, int front) {
    if (frame != c->frame) {
        perf_caps_frame_end(c);
        c->frame = frame;
    }
    c->count[PCN_RANGES]++;
    if (c->ncur == PERF_CAP_MAX) {
        c->count[PCN_LOST]++;
        return;
    }
    c->cur[c->ncur++] = (PerfCapRange){a, n, (uint8_t)(front != 0)};
}
void perf_caps_landed(PerfCaps* c, int release, const uint64_t ns[PCL_STEPS], const uint64_t walk[PCW_N]) {
    const int k = release ? 0 : 1;
    __atomic_fetch_add(&c->landed[k], 1, __ATOMIC_RELAXED);
    for (int i = 0; i < PCL_STEPS; i++) __atomic_fetch_add(&c->land_ns[k][i], ns[i], __ATOMIC_RELAXED);
    for (int i = 0; i < PCW_N; i++) __atomic_fetch_add(&c->walk[k][i], walk[i], __ATOMIC_RELAXED);
}
void perf_caps_hold(PerfCaps* c, uint64_t compute, uint64_t graphics, uint64_t releases) {
    __atomic_fetch_add(&c->hold_ns[PCH_COMPUTE], compute, __ATOMIC_RELAXED);
    __atomic_fetch_add(&c->hold_ns[PCH_GRAPHICS], graphics, __ATOMIC_RELAXED);
    __atomic_fetch_add(&c->hold_ns[PCH_RELEASES], releases, __ATOMIC_RELAXED);
    __atomic_fetch_add(&c->holds, 1, __ATOMIC_RELAXED);
}
static uint64_t take64(uint64_t* v) { return __atomic_exchange_n(v, 0, __ATOMIC_RELAXED); }
void perf_hash_note(PerfP29* p, uint64_t ra, uint64_t base, uint64_t n, uint64_t ns) {
    uint32_t i = 0;
    for (; i < PERF_HASH_SITES; i++) { /* the caller's slot, or the first free one taken for it */
        uint64_t was = __atomic_load_n(&p->ra[i], __ATOMIC_ACQUIRE);
        if (!was && __atomic_compare_exchange_n(&p->ra[i], &was, ra, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) break;
        if (was == ra) break;
    }
    __atomic_store_n(&p->base, base, __ATOMIC_RELAXED);
    __atomic_fetch_add(&p->calls[i], 1, __ATOMIC_RELAXED);
    __atomic_fetch_add(&p->bytes[i], n, __ATOMIC_RELAXED);
    if (ns) __atomic_fetch_add(&p->long_ns[i], ns, __ATOMIC_RELAXED);
}
void perf_replay_add(PerfP29* p, int what, uint64_t v) { __atomic_fetch_add(&p->replay[what], v, __ATOMIC_RELAXED); }
void perf_release_add(PerfP29* p, int kind, uint64_t ns, uint64_t book_ns) {
    __atomic_fetch_add(&p->rel_n[kind], 1, __ATOMIC_RELAXED);
    __atomic_fetch_add(&p->rel_ns[kind], ns, __ATOMIC_RELAXED);
    __atomic_fetch_add(&p->rel_book_ns, book_ns, __ATOMIC_RELAXED);
}
void perf_index_note(PerfP29* p, int which, int fell_back, uint64_t n) {
    if (fell_back) __atomic_fetch_add(&p->idx_fell[which], 1, __ATOMIC_RELAXED);
    uint64_t was = __atomic_load_n(&p->idx_max[which], __ATOMIC_RELAXED);
    while (n > was && !__atomic_compare_exchange_n(&p->idx_max[which], &was, n, 1, __ATOMIC_RELAXED, __ATOMIC_RELAXED))
        continue;
}
int perf_mem_thread(PerfMem* m, int who, uintptr_t token, uint64_t kernel_ns, uint64_t user_ns) {
    uintptr_t none = 0;
    if (!__atomic_compare_exchange_n(&m->owner[who], &none, token, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED) &&
        none != token) {
        __atomic_fetch_add(&m->others, 1, __ATOMIC_RELAXED);
        return 0;
    }
    __atomic_store_n(&m->kernel[who], kernel_ns, __ATOMIC_RELAXED);
    __atomic_store_n(&m->user[who], user_ns, __ATOMIC_RELAXED);
    return 1;
}
_Static_assert(PW_N <= 16 && PWHO_N <= 16, "a wait's kind and thread take 4 bits each of its key");
static _Thread_local int t_perf_who = PWHO_OTHER;
void perf_who_set(int who) { t_perf_who = who; }
void perf_waits_note(PerfWaits* w, int who, int kind, const void* at, uint64_t ns) {
    const uint64_t key = 1ull << 56 | (uint64_t)kind << 52 | (uint64_t)who << 48 | ((uintptr_t)at & 0xFFFFFFFFFFFFull);
    for (uint32_t i = (uint32_t)((key * 0x9E3779B97F4A7C15ull) >> 55), k = 0; k < PERF_WAIT_SLOTS; k++, i++) {
        i %= PERF_WAIT_SLOTS;
        uint64_t s = __atomic_load_n(&w->key[i], __ATOMIC_ACQUIRE);
        if (!s && !__atomic_compare_exchange_n(&w->key[i], &s, key, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE) && s != key)
            continue; /* another key took the free slot */
        if (s && s != key) continue;
        __atomic_fetch_add(&w->n[i], 1, __ATOMIC_RELAXED);
        __atomic_fetch_add(&w->ns[i], ns, __ATOMIC_RELAXED);
        return;
    }
    __atomic_fetch_add(&w->lost, 1, __ATOMIC_RELAXED);
}
void perf_wait_note(int kind, const void* at, uint64_t ns) { perf_waits_note(&g_perf_waits, t_perf_who, kind, at, ns); }
typedef struct {
    uint64_t key, n, ns;
} WaitSite;
static int site_cmp(const void* a, const void* b) { /* most ms first, then by key, so the order is fixed */
    const WaitSite *x = a, *y = b;
    return x->ns != y->ns ? (x->ns < y->ns ? 1 : -1) : (x->key > y->key) - (x->key < y->key);
}
size_t perf_handoff_line(PerfHandoff* h, uint32_t frames, char* out, size_t size) {
    static const char* const part[PH_PARTS] = {"submit", "submit_lock", "submit_add", "submit_scan", "fence",
                                                "present"};
    if (!size) return 0;
    const uint64_t n = take64(&h->frames);
    const double fn = n ? n : 1, f = frames ? frames : 1;
    size_t at = putf(out, size, 0,
                     "perf-handoff: frames=%u (a frame's tail at its increment: submissions, those a guest result "
                     "queued for the release thread in, by kind out/image/compute/payload/samples, where the last of "
                     "those sits (the last, 1, 2, 3, "
                     "4+ before it, none: share of frames), the same with the storage image left out, ms from "
                     "frame_end to its last landing, to the GPU's end of "
                     "it and of the last submission, and to the increment; frames with a compute landing after "
                     "frame_end and its ms; unregistered: landings from a submission never registered; then a "
                     "frame's handler calls and ms, ops walked whole and read at a mark; the storage images written "
                     "a frame, their draws' mean place after frame_end, and the last one's place/its frame's draws, "
                     "guest address, size and format; the last submission's results by kind out/image/compute/"
                     "payload/samples, count@share of frames; each kind's last registration's mean place among the "
                     "frame's draws, -1 none; the frame's mean draws) tails=%llu",
                     frames, (unsigned long long)n);
    at = putf(out, size, at, " subs=%.1f carry=%.1f kinds=", take64(&h->subs) / fn, take64(&h->carry) / fn);
    for (int k = 0; k < PCY_KINDS; k++) at = putf(out, size, at, "%s%.1f", k ? "/" : "", take64(&h->kinds[k]) / fn);
    at = putf(out, size, at, " pos=");
    for (unsigned k = 0; k <= PH_POS; k++) at = putf(out, size, at, "%s%.2f", k ? "/" : "", take64(&h->pos[k]) / fn);
    at = putf(out, size, at, " pos_noimg=");
    for (unsigned k = 0; k <= PH_POS; k++)
        at = putf(out, size, at, "%s%.2f", k ? "/" : "", take64(&h->pos_noimg[k]) / fn);
    at = putf(out, size, at, " to_land=%.3f to_carry_gpu=%.3f to_last_gpu=%.3f to_incr=%.3f land_before=%.2f",
              take64(&h->to_land) / fn / 1e6, take64(&h->to_carry_gpu) / fn / 1e6, take64(&h->to_last_gpu) / fn / 1e6,
              take64(&h->to_incr) / fn / 1e6, take64(&h->land_before) / fn);
    const uint64_t ca = take64(&h->compute_after);
    at = putf(out, size, at, " compute_after=%.2f/%.3f lost=%llu unregistered=%llu", ca / fn,
              ca ? take64(&h->compute_ns) / (double)ca / 1e6 : 0.0, (unsigned long long)take64(&h->lost),
              (unsigned long long)take64(&h->unregistered));
    if (!ca) take64(&h->compute_ns);
    for (int k = 0; k < PH_PARTS; k++)
        at = putf(out, size, at, " %s=%.2f/%.3f", part[k], take64(&h->n[k]) / f, take64(&h->ns[k]) / f / 1e6);
    at = putf(out, size, at, " ops=%.1f marked=%.1f", take64(&h->ops) / f, take64(&h->marked) / f);
    const uint64_t in = take64(&h->img_n); /* the GPU thread writes the line and these */
    at = putf(out, size, at, " images=%.2f at=%.1f last=%llu/%llu@%llx %llux%llu fkey=%llx", in / f,
              in ? take64(&h->img_at) / (double)in : 0.0, (unsigned long long)h->img_last_at,
              (unsigned long long)h->img_last_frame, (unsigned long long)h->img_gva, (unsigned long long)h->img_w,
              (unsigned long long)h->img_h, (unsigned long long)h->img_fkey);
    if (!in) take64(&h->img_at);
    at = putf(out, size, at, " last_kinds=");
    for (int k = 0; k < PCY_KINDS; k++)
        at = putf(out, size, at, "%s%.1f@%.2f", k ? "/" : "", take64(&h->last_n[k]) / fn, take64(&h->last_has[k]) / fn);
    const uint64_t np = take64(&h->presents);
    at = putf(out, size, at, " res_at=");
    for (int k = 0; k < PCY_KINDS; k++) {
        const uint64_t nf = take64(&h->res_frames[k]);
        at = putf(out, size, at, "%s%.1f", k ? "/" : "", nf ? take64(&h->res_place[k]) / (double)nf : -1.0);
        if (!nf) take64(&h->res_place[k]);
    }
    at = putf(out, size, at, " draws=%.1f", np ? take64(&h->draws_sum) / (double)np : 0.0);
    if (!np) take64(&h->draws_sum);
    return at < size ? at : size - 1;
}
size_t perf_waits_line(PerfWaits* w, uint32_t frames, char* out, size_t size) {
    static const char* const who[PWHO_N] = {"gpu", "release", "recorder", "guest", "scanner", "other"};
    static const char* const kind[PW_N] = {"lock",   "cond", "wake",  "yield",  "sleep",   "vkwait", "scan",
                                           "submit", "endcb", "hooks", "copy", "written", "query"};
    static WaitSite site[PERF_WAIT_SLOTS]; /* the GPU thread's, which writes the line */
    uint64_t tn[PWHO_N][PW_N] = {{0}}, tns[PWHO_N][PW_N] = {{0}};
    uint32_t nsite = 0;
    if (!size) return 0;
    for (uint32_t i = 0; i < PERF_WAIT_SLOTS; i++) {
        const uint64_t key = __atomic_load_n(&w->key[i], __ATOMIC_ACQUIRE), n = take64(&w->n[i]), t = take64(&w->ns[i]);
        if (!key || !n) continue;
        tn[key >> 48 & 15][key >> 52 & 15] += n, tns[key >> 48 & 15][key >> 52 & 15] += t;
        site[nsite++] = (WaitSite){key, n, t};
    }
    qsort(site, nsite, sizeof *site, site_cmp);
    const double f = frames ? frames : 1;
    size_t at = putf(out, size, 0,
                     "perf-waits: frames=%u (a frame by thread.kind, calls/ms: lock a contended take, cond a condition "
                     "wait, wake a signal or broadcast call, vkwait the graphics timeline, scan the write watch's, "
                     "submit and endcb the recorder's vkQueueSubmit2 and vkEndCommandBuffer, hooks, copy and written "
                     "a payload release's write in its three steps, query a samples report's sum; then the 12 callers "
                     "with "
                     "most ms, at their offset from pm_wait_note)",
                     frames);
    for (int a = 0; a < PWHO_N; a++)
        for (int k = 0; k < PW_N; k++)
            if (tn[a][k])
                at = putf(out, size, at, " %s.%s=%.1f/%.3f", who[a], kind[k], tn[a][k] / f, tns[a][k] / f / 1e6);
    at = putf(out, size, at, " lost=%llu sites", (unsigned long long)take64(&w->lost));
    for (uint32_t i = 0; i < nsite && i < 12; i++) {
        const uint64_t ra = site[i].key & 0xFFFFFFFFFFFFull;
        at = putf(out, size, at, " %s.%s@%c0x%llx=%.1f/%.3f", who[site[i].key >> 48 & 15], kind[site[i].key >> 52 & 15],
                  ra >= w->base ? '+' : '-', (unsigned long long)(ra >= w->base ? ra - w->base : w->base - ra),
                  site[i].n / f, site[i].ns / f / 1e6);
    }
    return at < size ? at : size - 1;
}
void perf_mem_alloc(PerfMem* m, int site, uint64_t bytes) {
    __atomic_fetch_add(&m->alloc_n[site], 1, __ATOMIC_RELAXED);
    __atomic_fetch_add(&m->alloc_b[site], bytes, __ATOMIC_RELAXED);
}
void perf_mem_free(PerfMem* m, int release, uint64_t bytes) {
    __atomic_fetch_add(&m->free_n[release ? 0 : 1], 1, __ATOMIC_RELAXED);
    __atomic_fetch_add(&m->free_b[release ? 0 : 1], bytes, __ATOMIC_RELAXED);
}
size_t perf_mem_line(PerfMem* m, uint32_t frames, uint64_t faults, char* out, size_t size) {
    static const char* const who[PTH_N] = {"gpu", "release", "recorder", "guest"};
    static const char* const site[PMA_N] = {"result", "result_small", "range_map", "list", "own"};
    if (!size) return 0;
    const double f = frames ? frames : 1;
    size_t at = putf(out, size, 0,
                     "perf-mem: frames=%u (a frame: the process's page faults; each thread's ms in the kernel/in user "
                     "mode, as it last stored them (the guest thread: the first posting presents); blocks made by "
                     "site, "
                     "count/KB (result_small: results under 4 KB); results freed on the release thread/elsewhere, "
                     "count/KB; others: stores refused, from a second thread for a slot) faults=%.1f",
                     frames, (double)(faults - m->faults0) / f);
    m->faults0 = faults;
    for (int i = 0; i < PTH_N; i++) {
        const uint64_t k = __atomic_load_n(&m->kernel[i], __ATOMIC_RELAXED);
        const uint64_t u = __atomic_load_n(&m->user[i], __ATOMIC_RELAXED);
        at = putf(out, size, at, " %s=%.3f/%.3f", who[i], (double)(k - m->kernel0[i]) / f / 1e6,
                  (double)(u - m->user0[i]) / f / 1e6);
        m->kernel0[i] = k, m->user0[i] = u;
    }
    for (int i = 0; i < PMA_N; i++)
        at = putf(out, size, at, " %s=%.1f/%.1f", site[i], take64(&m->alloc_n[i]) / f,
                  take64(&m->alloc_b[i]) / f / 1024);
    at = putf(out, size, at, " freed=%.1f/%.1f,%.1f/%.1f others=%llu", take64(&m->free_n[0]) / f,
              take64(&m->free_b[0]) / f / 1024, take64(&m->free_n[1]) / f, take64(&m->free_b[1]) / f / 1024,
              (unsigned long long)take64(&m->others));
    return at < size ? at : size - 1;
}
size_t perf_p29_line(PerfP29* p, uint32_t frames, char* out, size_t size) {
    if (!size) return 0;
    const double f = frames ? frames : 1;
    uint64_t c[PERF_HASH_SITES + 1], b[PERF_HASH_SITES + 1], t[PERF_HASH_SITES + 1], ra[PERF_HASH_SITES];
    for (uint32_t i = 0; i <= PERF_HASH_SITES; i++) {
        c[i] = take64(&p->calls[i]), b[i] = take64(&p->bytes[i]), t[i] = take64(&p->long_ns[i]);
        if (i < PERF_HASH_SITES) ra[i] = __atomic_load_n(&p->ra[i], __ATOMIC_ACQUIRE); /* slots stay a caller's */
    }
    const uint64_t base = __atomic_load_n(&p->base, __ATOMIC_RELAXED);
    size_t at = putf(out, size, 0,
                     "perf-p29: frames=%u (a frame: vk_hash and vk_hash_copy by caller, its return address less "
                     "vk_hash's: calls/KB/ms of its calls of 1 KB or more, most KB first, then the rest. The replay's "
                     "draws and the ones timed, one in %u: ms a timed draw for its binds and draw words in all/their "
                     "sends/a decode-only pass over the same words, and words and methods a timed draw. A draw's 3D "
                     "register methods, from its binds/from its draw words, each as all of them and those that write "
                     "the value the register holds, and the ms of the draws not timed. The release "
                     "thread's releases performed, syncpoint/payload/samples report: how many, ms performing, and ms "
                     "of the queue's bookkeeping under its lock. The page index's lookups, a binding's/a landing's: "
                     "those past the bound that walked as without it, and the most candidates one met, as a "
                     "count) vk_hash=%llx hash=",
                     frames, PERF_REPLAY_SAMPLE, (unsigned long long)base);
    for (int shown = 0;; shown++) { /* the sites by KB, the largest first */
        int best = -1;
        for (uint32_t i = 0; i < PERF_HASH_SITES; i++)
            if (c[i] && (best < 0 || b[i] > b[best])) best = (int)i;
        if (best < 0) break;
        const int64_t off = (int64_t)(ra[best] - base);
        at = putf(out, size, at, "%s%c%llx:%.1f/%.1f/%.3f", shown ? "," : "", off < 0 ? '-' : '+',
                  (unsigned long long)(off < 0 ? -off : off), c[best] / f, b[best] / f / 1024, t[best] / f / 1e6);
        c[best] = 0;
    }
    at = putf(out, size, at, " rest=%.1f/%.1f/%.3f", c[PERF_HASH_SITES] / f, b[PERF_HASH_SITES] / f / 1024,
              t[PERF_HASH_SITES] / f / 1e6);
    uint64_t r[PRP_N];
    for (int i = 0; i < PRP_N; i++) r[i] = take64(&p->replay[i]);
    const double s = r[PRP_SAMPLED] ? (double)r[PRP_SAMPLED] : 1;
    at = putf(out, size, at, " replay=%.1f/%.1f ms=%.4f/%.4f/%.4f words=%.1f methods=%.1f", r[PRP_DRAWS] / f,
              r[PRP_SAMPLED] / f, r[PRP_ALL_NS] / s / 1e6, r[PRP_SEND_NS] / s / 1e6, r[PRP_DECODE_NS] / s / 1e6,
              r[PRP_WORDS] / s, r[PRP_METHODS] / s);
    const double d = r[PRP_DRAWS] ? (double)r[PRP_DRAWS] : 1;
    at = putf(out, size, at, " regs=%.1f:%.1f/%.1f:%.1f draws_ms=%.3f", r[PRP_BIND_REGS] / d, r[PRP_BIND_SAME] / d,
              r[PRP_DRAW_REGS] / d, r[PRP_DRAW_SAME] / d, r[PRP_PLAIN_NS] / f / 1e6);
    uint64_t n[PRL_KINDS], ns[PRL_KINDS];
    for (int i = 0; i < PRL_KINDS; i++) n[i] = take64(&p->rel_n[i]), ns[i] = take64(&p->rel_ns[i]);
    at = putf(out, size, at, " releases=%.1f/%.1f/%.1f ms=%.3f/%.3f/%.3f book=%.3f", n[0] / f, n[1] / f, n[2] / f,
              ns[0] / f / 1e6, ns[1] / f / 1e6, ns[2] / f / 1e6, take64(&p->rel_book_ns) / f / 1e6);
    at = putf(out, size, at, " index=%.1f/%.1f max=%llu/%llu", take64(&p->idx_fell[0]) / f,
              take64(&p->idx_fell[1]) / f, (unsigned long long)take64(&p->idx_max[0]),
              (unsigned long long)take64(&p->idx_max[1]));
    return at < size ? at : size - 1;
}
size_t perf_caps_line(PerfCaps* c, uint32_t frames, char* out, size_t size) {
    static const char* const where[2] = {"release", "elsewhere"};
    if (!size) return 0;
    const double f = frames ? frames : 1;
    const uint64_t* n = c->count;
    size_t at = putf(out, size, 0,
                     "perf-captures: frames=%u (a frame: captures made, lost, distinct; the next one in address "
                     "overlapping/abutting/on the same page/apart; at the same place as the frame before; with another "
                     "GPU-side write or publication on their pages since the frame before; KB; sizes to 1/4/16/64 "
                     "KB/more. Graphics results landed on the release thread and elsewhere: results, ms by step "
                     "invalidate/write/pages/runs/walk/other, the walk's results passed/composed ones asked/met, "
                     "their changed runs/results whose write found them. The "
                     "release thread's holds, ms: compute landing/graphics landing/releases, and how many) "
                     "captures=%.1f lost=%.1f distinct=%.1f next=%.1f/%.1f/%.1f/%.1f same=%.1f front=%.1f KB=%.1f",
                     frames, n[PCN_RANGES] / f, n[PCN_LOST] / f, n[PCN_DISTINCT] / f, n[PCN_OVERLAP] / f,
                     n[PCN_ABUT] / f, n[PCN_PAGE] / f, n[PCN_APART] / f, n[PCN_RECUR] / f, n[PCN_FRONT] / f,
                     n[PCN_BYTES] / f / 1024);
    at = put_list(out, size, at, "sizes", n + PCN_SIZE, 5, f);
    for (int k = 0; k < 2; k++) {
        uint64_t ns[PCL_STEPS], w[PCW_N];
        for (int i = 0; i < PCL_STEPS; i++) ns[i] = take64(&c->land_ns[k][i]);
        for (int i = 0; i < PCW_N; i++) w[i] = take64(&c->walk[k][i]);
        at = putf(out, size, at, " %s=%.1f ms=", where[k], take64(&c->landed[k]) / f);
        for (int i = 0; i < PCL_STEPS; i++) at = putf(out, size, at, i ? "/%.3f" : "%.3f", ns[i] / f / 1e6);
        at = put_list(out, size, at, "walk", w, PCW_N, f);
    }
    at = putf(out, size, at, " hold=");
    for (int i = 0; i < PCH_N; i++) at = putf(out, size, at, i ? "/%.3f" : "%.3f", take64(&c->hold_ns[i]) / f / 1e6);
    at = putf(out, size, at, " holds=%.1f", take64(&c->holds) / f);
    memset(c->count, 0, sizeof c->count);
    return at < size ? at : size - 1;
}
