/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* What a function under test may call (verify.c). By default every main function its code reaches by a direct
   branch is compiled from the dump too, and imports run as models (imports.h). A stub replaces a callee instead: it
   records the call's arguments, which the original and the reconstruction must make alike, and returns a value
   drawn for the run. A case stubs a callee whose inputs the fixtures do not build, such as a table's hash part. */
#ifndef RECON_CALLEES_H
#define RECON_CALLEES_H

#include "common/cpu/aarch64/cpu.h"
#include "recon/guest.h"
#include <stddef.h>
#include <stdint.h>

typedef enum {
    STUB_INT,     /* X0 any integer, and D0 any bits */
    STUB_POINTER, /* X0 NULL or an address in scratch memory */
} StubResult;

typedef struct {
    uint64_t main_offset;
    const char* name;  /* the callee, for messages */
    uint8_t int_args;  /* X0 up to X(int_args - 1) are compared */
    uint8_t fp_args;   /* D0 up to D(fp_args - 1) are compared */
    StubResult result;
    /* NULL, or what the callee leaves behind that its caller relies on, drawn from h. The arguments are in c. */
    void (*effect)(CPU* c, const Guest* g, GAddr scratch, uint64_t h);
} Stub;

typedef struct {
    const char* name; /* the function under test, as in the registry */
    const Stub* stubs;
    size_t stub_count;
} CalleePlan;

extern const CalleePlan recon_lua53_callees[];
extern const size_t recon_lua53_callee_count;

#endif
