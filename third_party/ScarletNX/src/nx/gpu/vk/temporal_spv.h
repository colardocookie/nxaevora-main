/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The temporal probe's SPIR-V (shaders/temporal_motion.frag, shaders/temporal_view.frag). The words are in
   temporal_motion_fs_spv.c and temporal_view_fs_spv.c, which the build makes from the shaders with glslang. */
#ifndef SNX_TEMPORAL_SPV_H
#define SNX_TEMPORAL_SPV_H
#include "common/cpu/aarch64/cpu.h"

extern const u32 temporal_motion_fs_spv[];
extern const u64 temporal_motion_fs_spv_size; /* in bytes */
extern const u32 temporal_view_fs_spv[];
extern const u64 temporal_view_fs_spv_size;

#endif
