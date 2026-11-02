/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#ifndef SNX_ARM_SUCCESSORS_H
#define SNX_ARM_SUCCESSORS_H
#include "common/cpu/arm/arm.h"
#include <stddef.h>
#include <stdint.h>

/* What may follow a block for profile of units units (A32 words, or Thumb halfwords when thumb is set) at pc, which
   the block compiler made from count units of code (the guest's little-endian bytes from pc): each direct branch's
   target, and the instruction after the block unless control can't reach it (a branch without link, a write to the
   PC, BKPT, when the last instruction isn't conditional). found gets each, with thumb for its instruction set. A
   successor left out only sends the program through a run-time compile there; one too many costs a block that never
   runs. Ahead-of-time discovery's, the Vita's and a 32-bit Switch game's */
void snx_arm_successors(SnxArmProfile profile, uint32_t pc, const unsigned char* code, size_t units, size_t count,
                        int thumb, void (*found)(void* context, uint32_t address, int thumb), void* context);
#endif
