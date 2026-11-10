/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_NVN_SELFTEST_INT_H
#define SNX_NVN_SELFTEST_INT_H

/* The internal parts of the NVN selftest (docs/nvn-design.md, "The selftest"): one side of a comparison, the call of
   a library function or of its host handler, the report of one case, and the object cases that nvn_selftest_obj.c
   adds. The public entry stays in nvn_selftest.h. */

#include "nx/gpu/nvn_selftest.h"
#include "nx/gpu/nvn_selftest_cmp.h"
#include "nx/gpu/nvn_selftest_mem.h"

typedef struct {
    GuestFn fn;
    u64 entry;
} NvnStCallee;

typedef struct {
    NvnStCallee lib, host;
    const char* name;
} NvnStProc;

typedef struct {
    unsigned cases, failed, skipped;
    NvnStArena arena;
} NvnStRun;

/* One call. x0..x5 are the integer arguments and s0..s3 the float arguments (their bit patterns). s0_out, when not
   NULL, receives the s0 bits of the result before the CPU context is restored. */
typedef struct {
    uint64_t x[6];
    uint32_t s[4];
    unsigned n_s;
    uint32_t* s0_out;
} NvnStCall;

int nvn_st_proc_find(NvnStProc* p, const char* name);
uint64_t nvn_st_call(CPU* c, const NvnStCallee* f, const NvnStCall* a);
void nvn_st_report(NvnStRun* run, const char* name, const char* args, const NvnStRec* ref, const NvnStDiff* d);
/* The device that the game made, for the builders that need one. */
uint64_t nvn_st_device(void);

/* The builder and state object cases, called from nvn_selftest_run after the two first cases. */
void nvn_selftest_object_cases(CPU* c, NvnStRun* run);

/* The resource object cases of nvn_selftest_res.c, called from nvn_selftest_run after the builder and state cases. */
void nvn_selftest_resource_cases(CPU* c, NvnStRun* run);

/* Builds one sampler object from the sampler builder values of nvn_selftest_obj.c. `variant` 2 gives a filter that
   makes the sampler different from variant 0. 1 when every function was found. */
int nvn_st_sampler_build(CPU* c, uint64_t builder, uint64_t sampler, uint64_t data, int host, unsigned variant);

#endif
