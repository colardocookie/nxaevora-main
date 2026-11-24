/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* Startup owns the hand-over only when it enables it. Exercise the real environment readers and both starts. */
static const Surf startup_scene = {.sw = 480, .sh = 270, .levels = 1, .f = {VK_FORMAT_B10G11R11_UFLOAT_PACK32}};
static const TemporalRect startup_area = {0, 0, 240, 135};
static void flag(const char* name, const char* value) {
#ifdef _WIN32
    assert(!_putenv_s(name, value ? value : ""));
#else
    if (value) assert(!setenv(name, value, 1));
    else assert(!unsetenv(name));
#endif
}
static void reset_start(const char* name) {
    memset(&g_t, 0, sizeof g_t);
    memset(&g_s, 0, sizeof g_s);
    memset(&g_u, 0, sizeof g_u);
    g_u.asked = -1;
    g_vk_watch = NULL;
    g_t.mode = MODE_TAA;
    g_t.rect = 1;
    g_t.point = (VkSampler)1;
    g_vk.instance = (VkInstance)1;
    g_vk.phys = (VkPhysicalDevice)2;
    g_vk.props.limits.maxFramebufferWidth = g_vk.props.limits.maxFramebufferHeight = 16384;
    adapter_lookups = instance_questions = device_questions = name_checks = 0;
    missing_adapter = query_fail = queries = creates = views_made = deferred = pipes = sets = 0;
    logs[0] = 0;
    flag("SNX_ARCEUS_UPSCALER", name);
    flag("SNX_ARCEUS_FIXED_RESOLUTION", "1");
    flag("SNX_ARCEUS_TARGET_FPS", "120");
    flag("SNX_ARCEUS_TEMPORAL_VIEW", NULL);
    flag("SNX_ARCEUS_TEMPORAL_SHARPEN", NULL);
    flag("SNX_ARCEUS_DLSS_JITTER_SIGN", NULL);
}
static int ready(void) {
    TemporalRectGate gate = {0};
    gate.now.fx = gate.now.fy = 0.5f;
    VkDescriptorSet set = VK_NULL_HANDLE;
    return spatial_ready(2, &startup_scene, &startup_area, &gate, &set);
}
static void no_factories(void) {
    assert(!creates && !views_made && !pipes && !sets && !deferred);
}
int main(void) {
    const char* adapters[] = {"fsr", "dlss"};
    unsigned restorations = 0;
    for (unsigned a = 0; a < 2; a++)
        for (int rect = 0; rect < 2; rect++)
            for (int view = 0; view <= 4; view++)
                for (int extras = 0; extras < 2; extras++) {
                    reset_start(adapters[a]);
                    g_t.rect = rect;
                    g_t.view = view;
                    g_t.rect_extras_off = extras;
                    upscaler_start();
                    assert(g_u.asked && g_u.below && g_u.armed_spatial && g_s.asked);
                    assert(g_u.two_channel == (a == 1) && g_u.api == &test_adapter);
                    assert(adapter_lookups == 1 && instance_questions == 1 && device_questions == 1 && queries == 1);
                    assert(g_t.rect == 1 && !g_t.view && g_t.rect_extras_off == 1);
                    assert(g_u.was_rect == rect && g_u.was_view == view && g_u.was_extras == extras);
                    assert(g_u.nominal == 1.0 / 120 && g_u.output_max[0] == max_w && g_u.output_max[1] == max_h);
                    no_factories();
                    /* Model a first-context refusal by entering its actual rollback function after real startup. */
                    upscaler_unavailable("host first-context refusal");
                    assert(!g_u.asked && !g_u.below && !g_u.armed_spatial && !g_s.asked);
                    assert(g_t.rect == rect && g_t.view == view && g_t.rect_extras_off == extras);
                    assert(!ready() && queries == 1);
                    no_factories();
                    restorations++;
                }
    /* The fixed resolution is on by default (arceus_clock.c): unset goes below native as 1 does, 0 or any other
       value keeps the game's governor and stays at native size */
    for (unsigned a = 0; a < 2; a++) {
        reset_start(adapters[a]);
        flag("SNX_ARCEUS_FIXED_RESOLUTION", NULL);
        upscaler_start();
        assert(g_u.asked && g_u.below && g_u.armed_spatial && g_s.asked);
        no_factories();
    }
    const char* native[] = {"0", "2"};
    for (unsigned a = 0; a < 2; a++)
        for (unsigned f = 0; f < 2; f++) {
            reset_start(adapters[a]);
            flag("SNX_ARCEUS_FIXED_RESOLUTION", native[f]);
            upscaler_start();
            assert(g_u.asked && !g_u.below && !g_u.armed_spatial && !g_s.asked);
            no_factories();
        }
    const char* off[] = {NULL, "", "0", "off"};
    for (unsigned i = 0; i < 4; i++) {
        reset_start(off[i]);
        upscaler_start();
        assert(!g_u.asked && !g_s.asked && !g_u.armed_spatial);
        assert(!name_checks && !adapter_lookups && !instance_questions && !device_questions && !queries);
        no_factories();
    }
    for (unsigned why = 0; why < 4; why++) {
        reset_start("fsr");
        if (why == 0) g_t.mode = 0;
        if (why == 1) g_t.reject = 1;
        if (why == 2) missing_adapter = 1;
        if (why == 3) query_fail = 1;
        upscaler_start();
        assert(!g_u.asked && !g_s.asked && !g_u.armed_spatial);
        assert(g_t.rect == 1 && !g_t.view && !g_t.rect_extras_off);
        if (why < 2) assert(!adapter_lookups && !queries);
        no_factories();
    }
    /* Explicit spatial starts through its own switch branch and does not look up an adapter. */
    reset_start("spatial");
    upscaler_start();
    assert(g_s.asked && !g_u.asked && !g_u.below && !g_u.armed_spatial);
    assert(!name_checks && !adapter_lookups && !instance_questions && !device_questions && !queries);
    no_factories();
    assert(ready() && creates == 2 && views_made == 2 && pipes == 1 && sets == 1);
    for (unsigned why = 0; why < 5; why++) {
        reset_start("spatial");
        if (why == 0) g_t.mode = 0;
        if (why == 1) g_t.rect = 0;
        if (why == 2) flag("SNX_ARCEUS_FIXED_RESOLUTION", "0");
        if (why == 3) flag("SNX_ARCEUS_TEMPORAL_SHARPEN", "0.5");
        if (why == 4) flag("SNX_ARCEUS_TEMPORAL_VIEW", "1");
        upscaler_start();
        assert(!g_s.asked && !g_u.asked && !g_u.armed_spatial && !ready());
        assert(!name_checks && !adapter_lookups && !queries);
        no_factories();
    }
    assert(restorations == 40);
    printf("startup ownership: %u real start/rollback cases, native/off/refusal and explicit spatial controls PASS\n",
           restorations);
}
