/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* What the resolution scale costs in GPU memory (memory.c). No Vulkan types, so that the settings view includes it. */
#ifndef SNX_VK_SCALE_H
#define SNX_VK_SCALE_H
#include <stdint.h>

/* render_resolution's choices, the sizes of 1920 x 1080: 720p, 1080p, 1440p and 2160p (settings.txt) */
enum { VK_RENDER_CHOICES = 4 };

/* The scale of a choice. One outside 0..3 keeps 1: the default, 1080p, which the .ini's range also gives. */
static inline float vk_render_scale(int choice) {
    static const float scale[VK_RENDER_CHOICES] = {2.0f / 3.0f, 1.0f, 4.0f / 3.0f, 2.0f};
    return choice >= 0 && choice < VK_RENDER_CHOICES ? scale[choice] : 1.0f;
}
/* the word of a choice, 1080p for one outside 0..3 */
static inline const char* vk_render_word(int choice) {
    static const char* const words[VK_RENDER_CHOICES] = {"720p", "1080p", "1440p", "2160p"};
    return choice >= 0 && choice < VK_RENDER_CHOICES ? words[choice] : words[1];
}
/* As Ryujinx's TextureBindingsManager: a scaled fetch keeps the fragment's position (spirv_translate.c,
   scaled_coords) only when the texture has the bound target's aspect ratio, in guest pixels. A zero size never does */
static inline int vk_scale_same_aspect(uint32_t tex_w, uint32_t tex_h, uint32_t target_w, uint32_t target_h) {
    if (!tex_w || !tex_h || !target_w || !target_h) return 0;
    return (uint64_t)target_w * tex_h == (uint64_t)target_h * tex_w;
}

/* the heap memory (MB) the game needs with the render targets at this scale, and whether it fits the heap's budget
   (1 too when the budget is not known). An estimate from the logs, for a warning: the targets are made as the game asks */
uint64_t vk_scale_memory_mb(float scale);
int vk_scale_fits(float scale);

#endif
