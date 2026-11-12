/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The rules of a group of scaled render targets that one 2D array texture samples: the fit of a member, and whether a
   member list is already one group */
#include "nx/gpu/vk/vk.h"
#include <stdio.h>

static int failures;
static void check(int ok, const char* what) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

/* a scaled single-layer target that fits a 640x360 texture of f */
static Surf fitting_target(const VkFmt* f) {
    Surf m = {0};
    m.gva = 0x1000;
    m.w = 640;
    m.h = 360;
    m.sw = 1280;
    m.sh = 720;
    m.layers = 1;
    m.levels = 1;
    m.type = VK_IMAGE_VIEW_TYPE_2D;
    m.f = *f;
    m.is_rt = 1;
    return m;
}

int main(void) {
    VkFmt f = {0};
    f.vk = VK_FORMAT_R8G8B8A8_UNORM;
    f.bpp = 4;
    f.bw = 1;
    f.bh = 1;
    const u32 w = 640, h = 360;
    const Surf base = fitting_target(&f);
    Surf m;

    check(vk_rt_group_member_unfit(&base, w, h, &f) == NULL, "a fitting target");
    check(vk_rt_group_member_unfit(NULL, w, h, &f) != NULL, "a NULL member");

    m = base;
    m.is_rt = 0;
    check(vk_rt_group_member_unfit(&m, w, h, &f) != NULL, "not a render target");

    m = base;
    m.gva |= 1ull << 63;
    check(vk_rt_group_member_unfit(&m, w, h, &f) != NULL, "a parked target");

    m = base;
    m.layers = 2;
    check(vk_rt_group_member_unfit(&m, w, h, &f) != NULL, "not one layer");

    m = base;
    m.levels = 2;
    check(vk_rt_group_member_unfit(&m, w, h, &f) != NULL, "not one level");

    m = base;
    m.volume = 1;
    check(vk_rt_group_member_unfit(&m, w, h, &f) != NULL, "a volume");

    m = base;
    m.type = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    check(vk_rt_group_member_unfit(&m, w, h, &f) != NULL, "not a 2D image");

    m = base;
    m.w = 1024;
    check(vk_rt_group_member_unfit(&m, w, h, &f) != NULL, "another width");

    m = base;
    m.h = 512;
    check(vk_rt_group_member_unfit(&m, w, h, &f) != NULL, "another height");

    m = base;
    m.sw = m.w;
    m.sh = m.h;
    check(vk_rt_group_member_unfit(&m, w, h, &f) != NULL, "an unscaled target");

    VkFmt bad = f;
    bad.bw = 4;
    check(vk_rt_group_member_unfit(&base, w, h, &bad) != NULL, "a block-compressed format");

    bad = f;
    bad.conv = 1;
    check(vk_rt_group_member_unfit(&base, w, h, &bad) != NULL, "a converted format");

    m = base;
    m.f.depth = 1;
    check(vk_rt_group_member_unfit(&m, w, h, &f) != NULL, "another depth kind");

    m = base;
    m.f.stencil = 1;
    check(vk_rt_group_member_unfit(&m, w, h, &f) != NULL, "another stencil kind");

    m = base;
    m.f.bpp = 2;
    check(vk_rt_group_member_unfit(&m, w, h, &f) != NULL, "another texel size");

    m = base;
    m.f.vk = VK_FORMAT_R8G8B8A8_SRGB;
    check(vk_rt_group_member_unfit(&m, w, h, &f) != NULL, "another format");

    Surf g = {0}, a = {0}, b = {0}, c = {0}, other = {0};
    g.layers = 3;
    Surf* members[3] = {&a, &b, &c};
    a.group = &g;
    a.base_layer = 0;
    b.group = &g;
    b.base_layer = 1;
    c.group = &g;
    c.base_layer = 2;
    check(vk_rt_group_current(members, 3) == &g, "the group of a full match");

    a.group = b.group = c.group = NULL;
    check(vk_rt_group_current(members, 3) == NULL, "no group");
    a.group = b.group = c.group = &g;

    g.layers = 2;
    check(vk_rt_group_current(members, 3) == NULL, "another layer count");
    g.layers = 3;

    b.group = &other;
    check(vk_rt_group_current(members, 3) == NULL, "a member of another group");
    b.group = &g;

    c.base_layer = 0;
    check(vk_rt_group_current(members, 3) == NULL, "a member at a wrong base layer");

    if (!failures) puts("The fit of a group member and the group of a full member list");
    return failures ? 1 : 0;
}
