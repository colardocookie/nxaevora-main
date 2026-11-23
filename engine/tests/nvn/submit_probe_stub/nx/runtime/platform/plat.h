/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The submit probe test's stand-in: a lock that counts its depth, and a clock the test sets */
#pragma once
#include <stdint.h>
typedef struct { int held; } PMutex;
#define PMUTEX_INIT {0}
void pm_lock(PMutex* m);
void pm_unlock(PMutex* m);
uint64_t plat_ns(void);
