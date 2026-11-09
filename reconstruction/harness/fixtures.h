/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* Input generators for the differential harness. A fixture fills the scratch area with random bytes, builds the
   objects its function expects there (a lua_State with its stack, for example) and sets the argument registers. */
#ifndef RECON_FIXTURES_H
#define RECON_FIXTURES_H

#include "common/cpu/aarch64/cpu.h"
#include "recon/guest.h"

typedef struct {
    uint64_t state;
} Rng;

static inline uint64_t rng_next(Rng* r) { /* xorshift64* */
    r->state ^= r->state >> 12;
    r->state ^= r->state << 25;
    r->state ^= r->state >> 27;
    return r->state * 0x2545F4914F6CDD1Dull;
}
static inline uint32_t rng_below(Rng* r, uint32_t n) { return n ? (uint32_t)(rng_next(r) % n) : 0; }

typedef struct {
    const char* name; /* the function it feeds, as in the registry */
    void (*setup)(Rng* r, CPU* c, const Guest* g, GAddr scratch, uint64_t scratch_size);
} Fixture;

extern const Fixture recon_lua53_fixtures[];
extern const size_t recon_lua53_fixture_count;

#endif
