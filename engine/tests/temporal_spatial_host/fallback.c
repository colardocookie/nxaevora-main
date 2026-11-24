/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* A refused first adapter context restores TAA without enabling the spatial debug scaler. */
int main(void) {
    const Surf scene = {.sw = 480, .sh = 270, .levels = 1, .f = {VK_FORMAT_B10G11R11_UFLOAT_PACK32}};
    const TemporalRect area = {0, 0, 240, 135};
    TemporalRectGate gate = {0};
    gate.now.fx = gate.now.fy = 0.5f;
    g_vk.props.limits.maxFramebufferWidth = 16384;
    g_vk.props.limits.maxFramebufferHeight = 16384;
    g_t.point = (VkSampler)1;
    for (int below = 0; below < 2; below++)
        for (int rect = 0; rect < 2; rect++)
            for (int view = 0; view <= 4; view++)
                for (int extras = 0; extras < 2; extras++) {
                    g_u.asked = 1;
                    g_u.below = g_s.asked = below;
                    g_u.armed_spatial = below;
                    g_u.was_rect = rect;
                    g_u.was_view = view;
                    g_u.was_extras = extras;
                    g_t.mode = MODE_TAA;
                    g_t.rect = g_t.rect_extras_off = 1;
                    g_t.view = 0;
                    upscaler_unavailable("first context refused");
                    assert(!g_u.asked && !g_u.below && !g_u.armed_spatial && !g_s.asked && !g_vk_watch);
                    assert(g_t.rect == rect && g_t.view == view && g_t.rect_extras_off == extras);
                    VkDescriptorSet set = VK_NULL_HANDLE;
                    assert(!spatial_ready(2, &scene, rect ? &area : NULL, &gate, &set));
                    assert(!set && !creates && !views_made && !pipes && !sets && !queries);
                }
    /* The same eligible frame still makes the debug scaler when it was explicitly enabled. */
    g_s.asked = 1;
    VkDescriptorSet set = VK_NULL_HANDLE;
    assert(spatial_ready(2, &scene, &area, &gate, &set));
    assert(set && creates == 2 && views_made == 2 && pipes == 1 && sets == 1);
    const VkImage picture = g_s.picture.img, larger = g_s.larger.img;
    const VkImageView picture_view = g_s.picture.view, larger_view = g_s.larger.view;
    g_s.fr = (TemporalSpatialFrame){1, 1, 1};
    g_s.scene = g_vk_watch = &scene;
    g_u.asked = g_u.below = 1;
    /* Without adapter ownership, rollback leaves the explicitly enabled mode and its live frame intact. */
    upscaler_unavailable("adapter did not arm spatial");
    assert(g_s.asked && g_s.fr.published && g_vk_watch == &scene && !deferred);
    assert(g_s.picture.img == picture && g_s.larger.img == larger);
    g_s.views[0].view = (VkImageView)999;
    g_s.nviews = 1;
    g_s.views_of = larger;
    g_u.asked = g_u.below = g_u.armed_spatial = 1;
    upscaler_unavailable("adapter-owned hand-over with resources");
    assert(!g_s.asked && !g_s.fr.recorded && !g_s.fr.published && !g_vk_watch && !g_s.scene);
    assert(!g_s.picture.img && !g_s.larger.img && !g_s.nviews && !g_s.views_of);
    assert(deferred == 3);
    assert(!retired_images[0] && retired_views[0] == (VkImageView)999);
    assert(retired_images[1] == picture && retired_views[1] == picture_view);
    assert(retired_images[2] == larger && retired_views[2] == larger_view);
    upscaler_unavailable("already withdrawn");
    assert(deferred == 3);
    puts("adapter fallback: 40 restoration cases, explicit ownership and deferred retirement PASS");
}
