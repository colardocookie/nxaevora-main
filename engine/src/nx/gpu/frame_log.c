/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/gpu/frame_log.h"
#include <stdio.h>
#include <string.h>

int g_frame_log;
int g_latency_probe;
FrameLog g_frame_log_buf;
Latency g_latency;
LatPresent g_lat_next;

void frame_log_present(FrameLog* f, uint64_t no, uint64_t now_ns) {
    const uint64_t last = f->last_ns;
    f->last_ns = now_ns;
    if (!last) return;
    const uint64_t us = (now_ns - last) / 1000;
    if (!f->n) f->first[f->cur] = no;
    f->us[f->cur][f->n++] = us > UINT32_MAX ? UINT32_MAX : (uint32_t)us;
    if (f->n < FRAME_LOG_WINDOW) return;
    f->n = 0;
    if (__atomic_load_n(&f->ready, __ATOMIC_ACQUIRE)) { /* the other one is still out: this one starts over */
        __atomic_fetch_add(&f->lost, 1, __ATOMIC_RELAXED);
        return;
    }
    __atomic_store_n(&f->ready, f->cur + 1, __ATOMIC_RELEASE);
    f->cur ^= 1;
}
int frame_log_ready(FrameLog* f) { return (int)__atomic_load_n(&f->ready, __ATOMIC_ACQUIRE); }
size_t frame_log_line(const FrameLog* f, int buffer, unsigned part, char* out, size_t size) {
    if (!size) return 0;
    const uint64_t a = f->first[buffer] + (uint64_t)part * FRAME_LOG_PART;
    int n = snprintf(out, size, "arceus-frame-log: presents %llu-%llu us:", (unsigned long long)a,
                     (unsigned long long)(a + FRAME_LOG_PART - 1));
    size_t at = n > 0 ? (size_t)n : 0;
    for (unsigned i = 0; i < FRAME_LOG_PART && at < size; i++) {
        n = snprintf(out + at, size - at, " %u", f->us[buffer][part * FRAME_LOG_PART + i]);
        at += n > 0 ? (size_t)n : 0;
    }
    const uint32_t lost = __atomic_load_n(&f->lost, __ATOMIC_RELAXED);
    if (lost && part + 1 == FRAME_LOG_WINDOW / FRAME_LOG_PART && at < size) {
        n = snprintf(out + at, size - at, " lost=%u", lost);
        at += n > 0 ? (size_t)n : 0;
    }
    return at < size ? at : size - 1;
}
void frame_log_done(FrameLog* f) { __atomic_store_n(&f->ready, 0, __ATOMIC_RELEASE); }

int latency_input(Latency* l, uint64_t mask, uint64_t now_ns) {
    const uint64_t pressed = mask & ~l->mask;
    l->mask = mask;
    if (!pressed) return 0;
    __atomic_store_n(&l->t0, now_ns, __ATOMIC_RELEASE);
    return 1;
}
void latency_posted(Latency* l, const LatPresent* p) {
    const uint32_t head = l->post_head, tail = __atomic_load_n(&l->post_tail, __ATOMIC_ACQUIRE);
    if (head - tail >= LAT_POSTED) {
        __atomic_fetch_add(&l->post_lost, 1, __ATOMIC_RELAXED);
        return;
    }
    l->posted[head % LAT_POSTED] = *p;
    __atomic_store_n(&l->post_head, head + 1, __ATOMIC_RELEASE);
}
int latency_take(Latency* l, LatPresent* out) {
    const uint32_t tail = l->post_tail;
    if (tail == __atomic_load_n(&l->post_head, __ATOMIC_ACQUIRE)) return 0;
    *out = l->posted[tail % LAT_POSTED];
    __atomic_store_n(&l->post_tail, tail + 1, __ATOMIC_RELEASE);
    return 1;
}
static void put_present(const LatPresent* p, void (*put)(const char* line)) {
    char line[192], diff[16] = "-";
    if (p->diff >= 0) snprintf(diff, sizeof diff, "%.2f", p->diff);
    snprintf(line, sizeof line, "arceus-latency: present %llu at_us=%llu diff=%s jobs=%u acquire_us=%llu shown=%u",
             (unsigned long long)p->no, (unsigned long long)(p->post_ns / 1000), diff, p->jobs,
             (unsigned long long)(p->acquire_ns / 1000), p->shown);
    put(line);
}
/* a line for a present not yet given one (each comes here once, from the ring or as it comes): before the
   press, or among the first LAT_AFTER from it on */
static int due(Latency* l, const LatPresent* p) {
    if (!l->seen_t0) return 0;
    if (p->post_ns < l->seen_t0) return 1;
    if (l->after >= LAT_AFTER) return 0;
    l->after++;
    return 1;
}
void latency_result(Latency* l, const LatPresent* p, void (*put)(const char* line)) {
    const uint64_t t0 = __atomic_load_n(&l->t0, __ATOMIC_ACQUIRE);
    if (t0 != l->seen_t0) { /* a new press: the presents kept from before it first, in order */
        l->seen_t0 = t0;
        l->after = 0;
        for (uint32_t i = 0; i < l->n; i++) {
            const LatPresent* q = &l->ring[(l->head + LAT_BEFORE - l->n + i) % LAT_BEFORE];
            if (due(l, q)) put_present(q, put);
        }
        l->n = 0;
    }
    if (due(l, p)) {
        put_present(p, put);
        return;
    }
    l->ring[l->head] = *p;
    l->head = (l->head + 1) % LAT_BEFORE;
    if (l->n < LAT_BEFORE) l->n++;
}

double latency_picture_diff(const uint8_t* a, const uint8_t* b, size_t pixels) {
    uint64_t sum = 0;
    for (size_t i = 0; i < pixels * 4; i++) {
        if ((i & 3) == 3) continue; /* alpha */
        sum += a[i] > b[i] ? (uint64_t)(a[i] - b[i]) : (uint64_t)(b[i] - a[i]);
    }
    return pixels ? (double)sum / (double)(pixels * 3) : 0;
}
