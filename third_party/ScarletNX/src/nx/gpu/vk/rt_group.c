/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The pure rules of a group of scaled render targets that one 2D array texture samples: no Vulkan call, no global
   state. surface.c vk_rt_group_for makes the group, and texture.c tex_from_tic asks for it. */
#include "nx/gpu/vk/vk.h"

const char* vk_rt_group_member_unfit(const Surf* m, u32 w, u32 h, const VkFmt* f) {
    if (!m) return "missing";
    if (!m->is_rt) return "not a render target";
    if (m->gva >> 63) return "parked";
    if (m->layers != 1) return "not one layer";
    if (m->levels != 1) return "not one level";
    if (m->volume) return "a volume";
    if (m->type != VK_IMAGE_VIEW_TYPE_2D) return "not a 2D image";
    if (m->w != w || m->h != h) return "another size";
    if (m->sw == m->w && m->sh == m->h) return "unscaled";
    if (f->bw != 1) return "a block-compressed format";
    if (f->conv && !f->depth) return "a converted format";
    if (m->f.depth != f->depth) return "another depth kind";
    if (m->f.stencil != f->stencil) return "another stencil kind";
    if (m->f.bpp != f->bpp) return "another texel size";
    if (m->f.vk != f->vk) return "another format";
    return NULL;
}

Surf* vk_rt_group_current(Surf* const* members, u32 n) {
    if (!members || !n || !members[0]) return NULL;
    Surf* g = members[0]->group;
    if (!g || g->layers != n) return NULL;
    for (u32 i = 0; i < n; i++)
        if (!members[i] || members[i]->group != g || members[i]->base_layer != i) return NULL;
    return g;
}
