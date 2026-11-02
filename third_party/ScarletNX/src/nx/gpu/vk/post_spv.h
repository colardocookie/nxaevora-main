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
extern const u32 bicubic_fs_spv[];
extern const u64 bicubic_fs_spv_size;
extern const u32 smaa_edges_fs_spv[];
extern const u64 smaa_edges_fs_spv_size;
extern const u32 smaa_weights_fs_spv[];
extern const u64 smaa_weights_fs_spv_size;
extern const u32 smaa_blend_fs_spv[];
extern const u64 smaa_blend_fs_spv_size;
/* the lookup tables of SMAA (cmake/smaa_tables.cmake): the area texture 160 x 560 with two 8-bit channels, the search
   texture 64 x 16 with one */
extern const u8 smaa_area_tex[];
extern const u64 smaa_area_tex_size;
extern const u8 smaa_search_tex[];
extern const u64 smaa_search_tex_size;

#endif
