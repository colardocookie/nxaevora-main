/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* picture.comp's SPIR-V. The words are in vita_picture_spv.c, which the build makes from the shader with glslang
   (cmake/glsl.cmake): nothing compiled is checked in. */
#ifndef SNX_VITA_PICTURE_SPV_H
#define SNX_VITA_PICTURE_SPV_H
#include "common/cpu/aarch64/cpu.h"

extern const u32 vita_picture_spv[];
extern const u64 vita_picture_spv_size; /* in bytes */

#endif
