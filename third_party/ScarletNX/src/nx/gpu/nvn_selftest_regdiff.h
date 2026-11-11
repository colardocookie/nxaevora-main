/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_NVN_SELFTEST_REGDIFF_H
#define SNX_NVN_SELFTEST_REGDIFF_H

/* The register diff of the command buffer selftest (nvn-design.md, "The selftest"), a pure part: it names the
   registers that the two sides left different from the channel's base state, so the snapshot adds one field for each
   of them. It has no game state, so host tests can run it. */

#include "common/cpu/aarch64/cpu.h"

/* Writes the indexes, in register order, of the registers r below n where a[r] or b[r] differs from base[r]. Returns
   the number written, which is at most cap; *overflow is 1 when more registers differ than fit. It sets *overflow to
   0 at the start, and to 1 when a pointer is NULL. */
unsigned nvn_st_regdiff(const u32* base, const u32* a, const u32* b, unsigned n, u32* out_regs, unsigned cap,
                        int* overflow);

/* The field name "r3d.0x<reg>" of one register. The table is built once on first use. The selftest runs on one guest
   thread, so no lock is needed; a record keeps the pointer, so the table outlives every record. */
const char* nvn_st_reg_name(unsigned reg);

#endif
