// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// A stand-in for an upscaler's module (upscaler.h), no SDK, no GPU, built by each compiler of the boundary. Every
// call checks the sentinels it is handed and hands sentinels back. Loading it starts nothing.
// SNX_STANDIN: 0 answers, 1 a table of another version, 2 a table without a call, 3 no table, 4 no export. In 1 and
// 2 any call ends the process with 99. The export keeps the host's log, which 0 writes one line through.
#include <stdlib.h>
#include "sentinels.h"

#ifndef SNX_STANDIN
#define SNX_STANDIN 0
#endif
#ifdef _WIN32
#define STANDIN_EXPORT extern "C" __declspec(dllexport)
#else
#define STANDIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {
#if SNX_STANDIN == 1 || SNX_STANDIN == 2
[[noreturn]] void never() { _Exit(99); }
int never_instance(SnxUpscalerExtensions*) { never(); }
int never_device(VkInstance, VkPhysicalDevice, PFN_vkGetInstanceProcAddr, SnxUpscalerDeviceNeeds*) { never(); }
#if SNX_STANDIN == 1
int never_create(const SnxUpscalerCreate*, SnxUpscalerTraits*, SnxUpscaler**) { never(); }
#endif
int never_prepare(SnxUpscaler*, VkCommandBuffer) { never(); }
int never_evaluate(SnxUpscaler*, const SnxUpscalerFrame*) { never(); }
void never_destroy(SnxUpscaler*) { never(); }
int never_shutdown(VkDevice) { never(); }
#elif SNX_STANDIN == 0
struct Context {
    int made, prepared, evaluated, destroyed, shut;
};
Context g_context;
PFN_vkGetInstanceProcAddr g_lookup;
SnxUpscalerLog g_log;
const char* const g_instance_names[2] = {SENTINEL_INSTANCE_EXTENSION_A, SENTINEL_INSTANCE_EXTENSION_B};
const char* const g_device_names[1] = {SENTINEL_DEVICE_EXTENSION};

int instance_extensions(SnxUpscalerExtensions* out) {
    if (g_log) g_log(SENTINEL_LOG_LINE);
    out->count = 2;
    out->names = g_instance_names;
    return SNX_UPSCALER_OK;
}
// 101 to 104: the handle or the function that was not the sentinel
int device_extensions(VkInstance instance, VkPhysicalDevice device, PFN_vkGetInstanceProcAddr lookup,
                      SnxUpscalerDeviceNeeds* out) {
    if (instance != SENTINEL_INSTANCE) return 101;
    if (device != SENTINEL_PHYSICAL) return 102;
    if (!lookup) return 103;
    // the host's function is called back through the boundary, and what it returns is called too
    const PFN_vkVoidFunction found = lookup(instance, SENTINEL_LOOKUP_INSTANCE);
    if (!found) return 104;
    found();
    g_lookup = lookup;
    out->extensions.count = 1;
    out->extensions.names = g_device_names;
    out->features = SENTINEL_FEATURES;
    out->core = SENTINEL_CORE;
    return SNX_UPSCALER_OK;
}
// 200 and the member's number: the member of the create structure that was not the sentinel
int create(const SnxUpscalerCreate* made, SnxUpscalerTraits* traits, SnxUpscaler** out) {
    *out = nullptr;
    const int wrong = sentinel_create_wrong(made);
    if (wrong) return 200 + wrong;
    if (made->instance_lookup != g_lookup) return 210;
    if (!made->device_lookup) return 211;
    const PFN_vkVoidFunction found = made->device_lookup(made->device, SENTINEL_LOOKUP_DEVICE);
    if (!found) return 211;
    found();
    sentinel_traits(traits);
    g_context.made++;
    *out = reinterpret_cast<SnxUpscaler*>(&g_context);
    return SNX_UPSCALER_OK;
}
int prepare(SnxUpscaler* upscaler, VkCommandBuffer cb) {
    if (upscaler != reinterpret_cast<SnxUpscaler*>(&g_context)) return 301;
    if (g_context.destroyed) return SENTINEL_DESTROYED;
    if (cb != SENTINEL_CB) return 302;
    g_context.prepared++;
    return SNX_UPSCALER_OK;
}
// 400 and the member's number: the member of the frame that was not the sentinel
int evaluate(SnxUpscaler* upscaler, const SnxUpscalerFrame* frame) {
    if (upscaler != reinterpret_cast<SnxUpscaler*>(&g_context)) return 301;
    const int wrong = sentinel_frame_wrong(frame);
    if (wrong) return 400 + wrong;
    g_context.evaluated++;
    return SNX_UPSCALER_OK;
}
void destroy(SnxUpscaler* upscaler) {
    if (upscaler == reinterpret_cast<SnxUpscaler*>(&g_context)) g_context.destroyed++;
}
// 501 another device, 502 a context made and not destroyed, 503 a second shutdown
int shutdown(VkDevice device) {
    if (device != SENTINEL_DEVICE) return 501;
    if (g_context.made > g_context.destroyed) return 502;
    if (g_context.shut) return 503;
    g_context.shut++;
    return SNX_UPSCALER_OK;
}
#endif

#if SNX_STANDIN == 0
const SnxUpscalerApi g_api = {SNX_UPSCALER_ABI, SENTINEL_NAME, instance_extensions, device_extensions, create,
                              prepare, evaluate, destroy, shutdown, VK_FORMAT_UNDEFINED};
#elif SNX_STANDIN == 1
const SnxUpscalerApi g_api = {SNX_UPSCALER_ABI + 1, SENTINEL_NAME, never_instance, never_device, never_create,
                              never_prepare,        never_evaluate, never_destroy, never_shutdown,
                              VK_FORMAT_UNDEFINED};
#elif SNX_STANDIN == 2
const SnxUpscalerApi g_api = {SNX_UPSCALER_ABI, SENTINEL_NAME, never_instance, never_device, nullptr,
                              never_prepare,    never_evaluate, never_destroy, never_shutdown, VK_FORMAT_UNDEFINED};
#endif
}  // namespace

#if SNX_STANDIN == 3
STANDIN_EXPORT const SnxUpscalerApi* snx_upscaler_api(uint32_t, SnxUpscalerLog) { return nullptr; }
#elif SNX_STANDIN == 4
STANDIN_EXPORT int snx_standin_other_export(void) { return 0; }
#else
// the table, whatever number is asked for: the host reads the table's own number and decides
STANDIN_EXPORT const SnxUpscalerApi* snx_upscaler_api(uint32_t, SnxUpscalerLog log) {
#if SNX_STANDIN == 0
    g_log = log;
#else
    (void)log;
#endif
    return &g_api;
}
#endif
