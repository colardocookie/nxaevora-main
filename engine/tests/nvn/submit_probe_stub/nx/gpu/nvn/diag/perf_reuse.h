/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The submit probe test's stand-in: a range's hash from the test's guest bytes */
#pragma once
#include "nx/gpu/nvn/diag/perf_reuse_calc.h"
uint64_t perf_reuse_hash(uint64_t gva, uint64_t len, uint8_t* owned);
