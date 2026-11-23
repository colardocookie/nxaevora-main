/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The pure part of the reuse probe (gpu/nvn/diag/perf_reuse.c): the set of guest ranges one frame's replay read,
   each once with the kinds it was read as, and a window's counts of the ranges whose bytes changed before the next
   frame's replay, with the largest of them. No runtime header, no global state, so the host test links it alone. */
#ifndef SNX_GPU_NVN_PERF_REUSE_CALC_H
#define SNX_GPU_NVN_PERF_REUSE_CALC_H
#include <stdint.h>

#define PERF_REUSE_CAP 8192u    /* the ranges a frame keeps, the rest only counted */
#define PERF_REUSE_SLOTS 16384u /* the set's index, a power of two, at most half used */
#define PERF_REUSE_TOP 10u
/* what a range was read as, a bit each */
enum { PERF_REUSE_CB = 1, PERF_REUSE_VERTEX = 2, PERF_REUSE_INDEX = 4, PERF_REUSE_STORAGE = 8 };
#define PERF_REUSE_KINDS 4u /* the overlap check also names storage binds (diag/overlap_check.c) */

typedef struct {
    uint64_t gva, hash;
    uint32_t len;
    uint8_t kinds, owned; /* owned: GPU-owned bytes there, hashed by their owners, not read */
} PerfReuseRange;

typedef struct {
    PerfReuseRange r[PERF_REUSE_CAP];
    uint32_t slot[PERF_REUSE_SLOTS]; /* an index into r plus 1, 0 for an empty slot */
    uint32_t n;
    uint64_t overflow; /* reads of a range that did not fit */
} PerfReuseSet;

typedef struct {
    uint64_t frames, kept, changed, bytes_kept, bytes_changed, owned, overflow;
    uint64_t kept_kind[PERF_REUSE_KINDS], changed_kind[PERF_REUSE_KINDS]; /* constants, vertices, indices, storages */
    PerfReuseRange top[PERF_REUSE_TOP];     /* the largest changed ranges, each range once, largest first */
    uint32_t ntop;
} PerfReuseWindow;

/* Empties the set. */
void perf_reuse_clear(PerfReuseSet* s);
/* Adds [gva, gva + len) read as kind, or adds kind to the same range already there. 0 when the set is full or len is
   0 or above 4 GiB, which a full set counts as overflow. */
int perf_reuse_add(PerfReuseSet* s, uint64_t gva, uint64_t len, uint32_t kind);
/* Counts one kept range into the window, changed when its bytes' hash is not the one kept. */
void perf_reuse_count(PerfReuseWindow* w, const PerfReuseRange* r, int changed);

#endif
