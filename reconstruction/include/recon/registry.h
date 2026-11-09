/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* Reconstructed functions in the runtime's calling form (a GuestFn: arguments and results in the guest registers,
   return through X30), so the harness and, later, the runtime's dispatch table can use them in place of the
   recompiled code. Each entry names the main-module function it replaces by offset and size. */
#ifndef RECON_REGISTRY_H
#define RECON_REGISTRY_H

#include "common/cpu/aarch64/cpu.h"
#include "recon/guest.h"

typedef enum {
    RECON_RET_NONE, /* void */
    RECON_RET_W0,   /* a 32-bit result: only W0 is defined */
    RECON_RET_X0,   /* a 64-bit result or a pointer */
} ReconReturn;

typedef struct {
    const char* name;      /* the function's name in its library (lua_settop) */
    const char* source;    /* the reconstruction's file, from reconstruction/ */
    uint64_t main_offset;  /* the original's offset in the main module */
    uint32_t size;         /* the original's size in bytes */
    ReconReturn ret;
    GuestFn fn;
} ReconFunction;

/* The guest address of the main module's offset 0, set by whoever runs the reconstructions */
extern GAddr recon_main_base;

static inline Guest recon_guest(const CPU* c) {
    Guest g = {c->mem, recon_main_base};
    return g;
}

extern const ReconFunction recon_lua53_functions[];
extern const size_t recon_lua53_count;

#endif
