/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/runtime/platform/profile_calc.h"
#include <string.h>

int prof_kind(uint64_t pc, int game, ProfRange image, const ProfRange* waits, uint32_t nwaits) {
    if (game) return PROF_GAME;
    for (uint32_t i = 0; i < nwaits; i++)
        if (pc >= waits[i].lo && pc < waits[i].hi) return PROF_WAIT;
    return pc >= image.lo && pc < image.hi ? PROF_RUNTIME : PROF_HOST;
}

void prof_count(ProfTable* t, uint16_t slot, uint16_t gen, uint64_t key) {
    if (!key) key = 16; /* 0 marks a free bucket */
    uint32_t i = (uint32_t)(((key >> 4) ^ ((uint64_t)slot << 20) ^ ((uint64_t)gen << 36)) * 0x9E3779B1u) &
                 (PROF_BUCKETS - 1);
    for (uint32_t probe = 0; probe < 64; probe++, i = (i + 1) & (PROF_BUCKETS - 1)) {
        ProfBucket* b = &t->buckets[i];
        if (!b->key) *b = (ProfBucket){key, 0, slot, gen};
        if (b->key == key && b->slot == slot && b->gen == gen) {
            b->count++;
            return;
        }
    }
    t->unplaced++;
}

uint32_t prof_top(const ProfTable* t, uint16_t slot, uint16_t gen, ProfBucket* out, uint32_t n) {
    uint32_t found = 0;
    if (!n) return 0;
    for (uint32_t i = 0; i < PROF_BUCKETS; i++) {
        const ProfBucket* b = &t->buckets[i];
        if (!b->key || b->slot != slot || b->gen != gen) continue;
        if (found == n && b->count <= out[n - 1].count) continue;
        uint32_t at = found < n ? found++ : n - 1;
        while (at && out[at - 1].count < b->count) {
            out[at] = out[at - 1];
            at--;
        }
        out[at] = *b;
    }
    return found;
}

void prof_clear(ProfTable* t) { memset(t, 0, sizeof *t); }
