/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* Calls from a reconstruction into other game functions. A reconstruction calls a callee the way the game's code
   does: the arguments in the guest registers, X30 set to the return site, then the function snx_lookup finds for its
   address. In the runtime that is the recompiled code, in the harness whatever the case gives it. */
#ifndef RECON_CALL_H
#define RECON_CALL_H

#include "common/cpu/aarch64/cpu.h"
#include "recon/guest.h"

/* Calls the guest function at target as a BL at return_site - 4 would. Returns X0. */
static inline uint64_t recon_call(CPU* c, GAddr target, GAddr return_site) {
    c->x[30] = return_site;
    snx_lookup(c, target)(c, target);
    return c->x[0];
}

/* Reserves size bytes of guest stack below SP, as the original's frame does, so the addresses a reconstruction
   passes to callees are the original's. Returns the new SP. recon_stack_free gives them back. */
static inline GAddr recon_stack_alloc(CPU* c, uint64_t size) {
    c->x[31] -= size;
    return c->x[31];
}
static inline void recon_stack_free(CPU* c, uint64_t size) { c->x[31] += size; }

#endif
