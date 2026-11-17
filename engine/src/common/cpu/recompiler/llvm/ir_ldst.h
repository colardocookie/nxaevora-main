/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The load and store instructions' emitters, keyed on the generated decoder's ops (ir_ldst.c) */
#ifndef SNX_RECOMPILER_LLVM_IR_LDST_H
#define SNX_RECOMPILER_LLVM_IR_LDST_H

#include "common/cpu/recompiler/llvm/ir.h"
#include "common/cpu/aarch64/a64_decoder.h"

/* 1 when ir_ldst.c emits op, whatever feature it needs (the LSE, RCpc and MTE ones included) */
int snx_ir_ldst_handles(SnxA64Op op);
/* The body of insn (word at pc), an op snx_ir_ldst_handles */
void snx_ir_ldst(SnxIr* ir, const SnxA64Insn* insn, SnxU32 word, SnxU64 pc);

#endif
