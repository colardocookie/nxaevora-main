/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/gpu/nvn_selftest_regdiff.h"
#include "nx/gpu/gpu.h"
#include <stdio.h>

#define ST_REG_NAME_BYTES 12u

static char st_reg_names[MAXWELL3D_NUM_REGS][ST_REG_NAME_BYTES];
static int st_reg_names_ready;

unsigned nvn_st_regdiff(const u32* base, const u32* a, const u32* b, unsigned n, u32* out_regs, unsigned cap,
                        int* overflow) {
    unsigned count = 0;
    int more = 0;
    if (overflow) *overflow = 0;
    if (!base || !a || !b || (n && !out_regs)) {
        if (overflow) *overflow = 1;
        return 0;
    }
    for (unsigned r = 0; r < n; r++) {
        if (a[r] == base[r] && b[r] == base[r]) continue;
        if (count < cap)
            out_regs[count++] = r;
        else
            more = 1;
    }
    if (more && overflow) *overflow = 1;
    return count;
}

const char* nvn_st_reg_name(unsigned reg) {
    if (reg >= MAXWELL3D_NUM_REGS) return "r3d.out_of_range";
    if (!st_reg_names_ready) {
        for (unsigned r = 0; r < MAXWELL3D_NUM_REGS; r++) snprintf(st_reg_names[r], ST_REG_NAME_BYTES, "r3d.0x%03x", r);
        st_reg_names_ready = 1;
    }
    return st_reg_names[reg];
}
