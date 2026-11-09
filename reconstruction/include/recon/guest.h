/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* Guest memory as reconstructed functions see it: guest address 0 at mem, the game's main module at main_base.
   Guest memory is little-endian like every host the recompiler runs on, so the accessors copy bytes. Reconstructions
   only touch memory through these, which keeps them independent of the runtime and testable on their own. */
#ifndef RECON_GUEST_H
#define RECON_GUEST_H

#include <stdint.h>
#include <string.h>

typedef uint64_t GAddr;

typedef struct {
    uint8_t* mem;     /* host address of guest address 0 */
    GAddr main_base;  /* guest address of the main module (its offset 0) */
} Guest;

static inline uint8_t g_ld8(const Guest* g, GAddr a) { return g->mem[a]; }
static inline uint32_t g_ld32(const Guest* g, GAddr a) {
    uint32_t v;
    memcpy(&v, g->mem + a, 4);
    return v;
}
static inline uint64_t g_ld64(const Guest* g, GAddr a) {
    uint64_t v;
    memcpy(&v, g->mem + a, 8);
    return v;
}
static inline void g_st32(const Guest* g, GAddr a, uint32_t v) { memcpy(g->mem + a, &v, 4); }
static inline void g_st64(const Guest* g, GAddr a, uint64_t v) { memcpy(g->mem + a, &v, 8); }
/* A 16-byte copy, as the game's TValue moves (LDP/STP) do */
static inline void g_copy16(const Guest* g, GAddr to, GAddr from) {
    uint8_t t[16];
    memcpy(t, g->mem + from, 16);
    memcpy(g->mem + to, t, 16);
}

#endif
