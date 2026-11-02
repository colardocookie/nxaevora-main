/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* What the resolution scale costs in GPU memory (memory.c). No Vulkan types, so that the settings view includes it. */
#ifndef SNX_VK_SCALE_H
#define SNX_VK_SCALE_H
#include <stdint.h>

/* the heap memory (MB) the game needs with the render targets at this scale, and whether it fits the heap's budget
   (1 too when the budget is not known). An estimate from the logs, for a warning: the targets are made as the game asks */
uint64_t vk_scale_memory_mb(float scale);
int vk_scale_fits(float scale);

#endif
