/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_NVN_NATIVE_H
#define SNX_NVN_NATIVE_H

#include "common/cpu/aarch64/cpu.h"

/* nvn_native=1: the host answers the NVN functions that the game resolves, and the game's NVN library does not run
   (docs/nvn-design.md). Init runs once the game's symbols and dispatch table are loaded, before nvn_trace_init and
   before any guest thread starts. */
void nvn_native_init(void);
/* at a clean exit: the functions that the game called and the host does not have */
void nvn_native_report(void);
/* 1 once nvn_native_init found nvn_native=1 and hooked the loader */
int nvn_native_active(void);
/* the host handler of an NVN function by name, or NULL when the host has none (the selftest compares it with the
   library). It does not need nvn_native=1. */
GuestFn nvn_native_handler(const char* name);

#endif
