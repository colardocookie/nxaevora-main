/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The post-processing passes' SPIR-V (shaders/post.vert, shaders/fxaa.frag). The words are in post_vs_spv.c and
   fxaa_fs_spv.c, which the build makes from the shaders with glslang (cmake/glsl.cmake). */
#ifndef SNX_POST_SPV_H
#define SNX_POST_SPV_H
#include "common/cpu/aarch64/cpu.h"

extern const u32 post_vs_spv[];
extern const u64 post_vs_spv_size; /* in bytes */
extern const u32 fxaa_fs_spv[];
extern const u64 fxaa_fs_spv_size;

#endif
