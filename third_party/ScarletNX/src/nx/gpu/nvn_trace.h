/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#ifndef SNX_NVN_TRACE_H
#define SNX_NVN_TRACE_H
#include <stdint.h>

/* nvn_trace=1: an inventory of the NVN functions the game resolves and calls (gpu/nvn_trace.c). Init runs once the
   game's symbols and dispatch table are loaded, before any guest thread starts. */
void nvn_trace_init(void);
void nvn_trace_report(void);
/* 1 once nvn_trace_init found nvn_trace=1: wrappers replace dispatch slots */
int nvn_trace_active(void);
/* the NUL-terminated name at a guest pointer (up to 127 bytes, checked a page at a time): 1 when it is readable and
   not empty. Shared with gpu/nvn_native.c. */
int nvn_guest_name(uint64_t ptr, char name[128]);

#endif
