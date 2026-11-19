/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* Device and adapter endpoints for the actual startup functions. No adapter library or GPU is loaded. */
#include "nx/gpu/vk/upscaler_needs.h"
static unsigned adapter_lookups, instance_questions, device_questions, name_checks;
static int missing_adapter;
static PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
static int instance_extensions(SnxUpscalerExtensions* out) {
    instance_questions++;
    *out = (SnxUpscalerExtensions){0};
    return SNX_UPSCALER_OK;
}
static int device_extensions(VkInstance instance, VkPhysicalDevice physical, PFN_vkGetInstanceProcAddr lookup,
                             SnxUpscalerDeviceNeeds* out) {
    assert(instance == g_vk.instance && physical == g_vk.phys && lookup == vkGetInstanceProcAddr);
    device_questions++;
    *out = (SnxUpscalerDeviceNeeds){0};
    return SNX_UPSCALER_OK;
}
static int create_context(const SnxUpscalerCreate* info, SnxUpscalerTraits* traits, SnxUpscaler** out) {
    (void)info; (void)traits; (void)out;
    assert(!"startup must not create a context");
    return SNX_UPSCALER_REFUSED;
}
static int prepare_context(SnxUpscaler* context, VkCommandBuffer commands) {
    (void)context; (void)commands;
    assert(!"startup must not record commands");
    return SNX_UPSCALER_FAILED;
}
static int evaluate_context(SnxUpscaler* context, const SnxUpscalerFrame* frame) {
    (void)context; (void)frame;
    assert(!"startup must not evaluate a frame");
    return SNX_UPSCALER_FAILED;
}
static void destroy_context(SnxUpscaler* context) {
    (void)context;
    assert(!"startup has no context to destroy");
}
static const SnxUpscalerApi test_adapter = {
    .abi = SNX_UPSCALER_ABI, .name = "host adapter",
    .instance_extensions = instance_extensions, .device_extensions = device_extensions,
    .create = create_context, .prepare = prepare_context, .evaluate = evaluate_context, .destroy = destroy_context
};
static int vk_upscaler_not_in_game(const char* name) {
    name_checks++;
    assert(!strcmp(name, "fsr") || !strcmp(name, "dlss"));
    return 0;
}
static const SnxUpscalerApi* vk_upscaler_adapter(const char* name) {
    (void)name;
    adapter_lookups++;
    return missing_adapter ? NULL : &test_adapter;
}
static int vk_upscaler_instance_met(const SnxUpscalerApi* api) {
    (void)api;
    assert(!"the host adapter asks for no instance extensions");
    return 0;
}
static int vk_upscaler_device_met(const SnxUpscalerApi* api, const char* name, const char* const* extensions,
                                  uint32_t count, const void* features, uint32_t core) {
    (void)api; (void)name; (void)extensions; (void)count; (void)features; (void)core;
    assert(!"the host adapter asks for no extra device features");
    return SNX_NEEDS_TYPE;
}
