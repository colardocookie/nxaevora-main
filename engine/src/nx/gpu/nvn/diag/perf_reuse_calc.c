/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/gpu/nvn/diag/perf_reuse_calc.h"
#include <string.h>

static uint32_t home(uint64_t gva, uint32_t len) {
    return (uint32_t)((((gva ^ ((uint64_t)len << 40)) * 0x9E3779B97F4A7C15ull) >> 32) & (PERF_REUSE_SLOTS - 1));
}

void perf_reuse_clear(PerfReuseSet* s) {
    memset(s->slot, 0, sizeof s->slot);
    s->n = 0;
    s->overflow = 0;
}

int perf_reuse_add(PerfReuseSet* s, uint64_t gva, uint64_t len, uint32_t kind) {
    if (!len || len > 0xFFFFFFFFull) return 0;
    for (uint32_t i = home(gva, (uint32_t)len);; i = (i + 1) & (PERF_REUSE_SLOTS - 1)) {
        uint32_t at = s->slot[i];
        if (!at) {
            if (s->n == PERF_REUSE_CAP) {
                s->overflow++;
                return 0;
            }
            s->r[s->n] = (PerfReuseRange){gva, 0, (uint32_t)len, (uint8_t)kind, 0};
            s->slot[i] = ++s->n;
            return 1;
        }
        PerfReuseRange* r = &s->r[at - 1];
        if (r->gva == gva && r->len == (uint32_t)len) {
            r->kinds |= (uint8_t)kind;
            return 1;
        }
    }
}

static void top_add(PerfReuseWindow* w, const PerfReuseRange* r) {
    uint32_t i;
    for (i = 0; i < w->ntop; i++)
        if (w->top[i].gva == r->gva && w->top[i].len == r->len) {
            w->top[i].kinds |= r->kinds;
            return;
        }
    if (w->ntop == PERF_REUSE_TOP && w->top[PERF_REUSE_TOP - 1].len >= r->len) return;
    i = w->ntop < PERF_REUSE_TOP ? w->ntop++ : PERF_REUSE_TOP - 1;
    for (; i && w->top[i - 1].len < r->len; i--) w->top[i] = w->top[i - 1];
    w->top[i] = *r;
}

void perf_reuse_count(PerfReuseWindow* w, const PerfReuseRange* r, int changed) {
    w->kept++;
    w->bytes_kept += r->len;
    w->owned += r->owned;
    for (int k = 0; k < 3; k++)
        if (r->kinds >> k & 1) {
            w->kept_kind[k]++;
            w->changed_kind[k] += changed != 0;
        }
    if (!changed) return;
    w->changed++;
    w->bytes_changed += r->len;
    top_add(w, r);
}
