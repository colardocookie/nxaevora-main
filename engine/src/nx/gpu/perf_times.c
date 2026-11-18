/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/gpu/perf_times.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

int g_perf_times;
PerfTimes g_perf;
int g_perf_pass_why;
int g_perf_submit_why;
uint64_t g_perf_submit_every_ns = PERF_SUBMIT_EVERY_NS;
int g_perf_compute_submit = 1;
int g_perf_inplace;
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
        "wb_outputs", "wb_bytes",    "wc_bytes",    "tail_submits", "tail_after",   "passes",       "end_other",
        "end_gpu_src", "end_copy",   "end_image",   "end_target",   "src_barriers", "src_clean",    "src_unknown",
        "rel_depth",  "win_outputs", "src_walks",   "src_results",  "tail_ungated", "tail_partial", "tail_lost"};
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

/* all but elsewhere, which other threads add to */
static void copies_clear(PerfCopies* c) {
    memset(c, 0, offsetof(PerfCopies, elsewhere));
    c->site = c->slot = c->draw = c->last = -1;
}
void perf_copies_reset(PerfCopies* c) {
    copies_clear(c);
    __atomic_store_n(&c->elsewhere, 0, __ATOMIC_RELAXED);
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
    if (why < 0 || why > PE_TARGET) why = PE_OTHER;
    const int i = copy_slot(c, t);
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
                     "image/target, reads reuse/several/part/align/submit/old/changed)", frames ? frames : 1);
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
        for (int w = 0; w <= PE_TARGET; w++) at = putf(out, size, at, "%s%.1f", w ? "/" : "=", g->why[w] / f);
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
