/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The temporal passes' SPIR-V (shaders/temporal_motion.frag, _view.frag, _stats.frag and _resolve.frag). The words
   are in files of the same names ending in _fs_spv.c, which the build makes from the shaders with glslang. */
#ifndef SNX_TEMPORAL_SPV_H
#define SNX_TEMPORAL_SPV_H
#include "common/cpu/aarch64/cpu.h"

extern const u32 temporal_motion_fs_spv[];
extern const u64 temporal_motion_fs_spv_size; /* in bytes */
extern const u32 temporal_view_fs_spv[];
extern const u64 temporal_view_fs_spv_size;
extern const u32 temporal_stats_fs_spv[];
extern const u64 temporal_stats_fs_spv_size;
extern const u32 temporal_resolve_fs_spv[];
extern const u64 temporal_resolve_fs_spv_size;
extern const u32 temporal_sharpen_fs_spv[];
extern const u64 temporal_sharpen_fs_spv_size;
extern const u32 temporal_upscaled_fs_spv[]; /* the write-back after an upscaler: used by nothing yet */
extern const u64 temporal_upscaled_fs_spv_size;
extern const u32 temporal_motion2_fs_spv[]; /* DL2: the vectors in two channels for DLSS */
extern const u64 temporal_motion2_fs_spv_size;
extern const u32 temporal_skin_vs_spv[];
extern const u64 temporal_skin_vs_spv_size;
extern const u32 temporal_skin_fs_spv[];
extern const u64 temporal_skin_fs_spv_size;
extern const u32 temporal_object_fs_spv[];
extern const u64 temporal_object_fs_spv_size;

#endif
