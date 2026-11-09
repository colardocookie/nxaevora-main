/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* Deliberately wrong reconstructions for the harness's --control check */
#ifndef RECON_MUTANTS_H
#define RECON_MUTANTS_H

#include "recon/registry.h"

typedef struct {
    const char* name; /* the function it imitates, as in the registry */
    GuestFn fn;
} Mutant;

extern const Mutant recon_lua53_mutants[];
extern const size_t recon_lua53_mutant_count;

#endif
