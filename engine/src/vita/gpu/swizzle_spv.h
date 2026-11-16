/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* swizzle.comp's SPIR-V. The words are in vita_swizzle_spv.c, which the build makes from the shader with glslang
   (cmake/glsl.cmake): nothing compiled is checked in. */
#ifndef SNX_VITA_SWIZZLE_SPV_H
#define SNX_VITA_SWIZZLE_SPV_H
#include "common/cpu/aarch64/cpu.h"

extern const u32 vita_swizzle_spv[];
extern const u64 vita_swizzle_spv_size; /* in bytes */

#endif
