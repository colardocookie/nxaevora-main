/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The engine menu's composite SPIR-V (shaders/menu_composite.vert, menu_composite.frag). The words are in
   menu_composite_vs_spv.c and menu_composite_fs_spv.c, which the build makes from the shaders with glslang
   (cmake/glsl.cmake). */
#ifndef SNX_MENU_SPV_H
#define SNX_MENU_SPV_H
#include "common/cpu/aarch64/cpu.h"

extern const u32 menu_composite_vs_spv[];
extern const u64 menu_composite_vs_spv_size; /* in bytes */
extern const u32 menu_composite_fs_spv[];
extern const u64 menu_composite_fs_spv_size;

#endif
