/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The C library functions the game imports, as the harness answers them. Game code reaches an import through a PLT
   stub in main (ADRP, LDR, ADD, BR X17 through its GOT slot). The harness names the stub from the module's own jump
   slot relocations and runs a model of the function on guest memory, the same for the original and the
   reconstruction. A model only touches mapped guest memory and fails the run otherwise. */
#ifndef RECON_IMPORTS_H
#define RECON_IMPORTS_H

#include "common/cpu/aarch64/cpu.h"
#include "recon/guest.h"
#include <stddef.h>

typedef struct {
    Guest g;
    GAddr lo, hi; /* the mapped guest range */
    GAddr lconv;  /* a struct lconv in guest memory, for localeconv */
} ImportEnv;

/* NULL when it ran, else why the run must fail */
typedef const char* (*ImportModel)(CPU* c, const ImportEnv* env);

/* The import a PLT stub at the module offset calls: its name in name, 1, or 0 for no stub there */
int import_name_at(const uint8_t* image, size_t size, uint64_t offset, char* name, size_t name_size);
/* The model of the import with this name, or NULL when the harness has none */
ImportModel import_model(const char* name);
/* Writes the data the models hand out (localeconv's struct and its strings) at area, 64 bytes */
void import_env_init(ImportEnv* env, GAddr area);

#endif
