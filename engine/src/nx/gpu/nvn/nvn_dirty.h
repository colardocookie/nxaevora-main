/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The bind dirty flags of the native replay with a count of the set ones per table (PERF-2, research 11:19). The
   counts are kept on every path. With SNX_ARCEUS_PERF_REPLAY=1 a walk stops once its table's count is 0, so it skips
   only clear flags and takes the same slots in the same order. No runtime header, so the host test links it alone. */
#ifndef SNX_GPU_NVN_DIRTY_H
#define SNX_GPU_NVN_DIRTY_H
#include <stdint.h>

/* the value of a set flag (nvn_native.h's NVN_BIND_DIRTY) */
#define NVN_DIRTY_SET 1u

/* sets a slot's flag, counting it when it was clear */
static inline void nvn_dirty_mark(uint8_t* flag, uint16_t* count) {
    if (!*flag) (*count)++;
    *flag = NVN_DIRTY_SET;
}
/* The next set flag of flags[0..n) at or after *slot: cleared, counted down, its index in *slot, and 1. 0 when none is
   left. counted: stop as soon as the count is 0, since every flag left is clear then */
static inline int nvn_dirty_take(uint8_t* flags, uint16_t* count, uint32_t n, uint32_t* slot, int counted) {
    for (uint32_t i = *slot; i < n; i++) {
        if (counted && !*count) return 0;
        if (!flags[i]) continue;
        flags[i] = 0;
        (*count)--;
        *slot = i;
        return 1;
    }
    return 0;
}
/* clears a whole table */
static inline void nvn_dirty_clear(uint8_t* flags, uint16_t* count, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) flags[i] = 0;
    *count = 0;
}

#endif
