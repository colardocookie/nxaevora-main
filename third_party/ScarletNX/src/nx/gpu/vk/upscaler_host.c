/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The runtime's side of an upscaler's adapter before a device exists (upscaler.h): which adapter a name means, and
   what the instance and the device have to be made with for it. Written once, as the runtime will use it. So far
   only the self-test mode calls it (SNX_ARCEUS_UPSCALER_SELFTEST): the game's start makes its instance and its
   device as it always did, and no adapter is asked. */
#include <string.h>
#include "nx/gpu/vk/vk.h"
#include "nx/gpu/vk/upscaler.h"
#include "nx/runtime/platform/log.h"

#ifdef _WIN32
const SnxUpscalerApi* snx_upscaler_fsr(void); /* gpu/vk/upscaler_fsr.cpp */
#endif

/* The adapter of that name, NULL for none: one that is not built into this program, or one whose SDK library is not
   beside it. The adapter says why in a line of its own. */
const SnxUpscalerApi* vk_upscaler_adapter(const char* name) {
#ifdef _WIN32
    if (name && !strcmp(name, "fsr")) return snx_upscaler_fsr();
#endif
    if (name) WARN("arceus-upscaler: no adapter named \"%s\" is built into this program\n", name);
    return NULL;
}

/* one more name on a list without repeats: 0 when there is no room */
static int name_add(const char** names, uint32_t* count, uint32_t room, const char* name) {
    for (uint32_t i = 0; i < *count; i++)
        if (!strcmp(names[i], name)) return 1;
    if (*count >= room) return 0;
    names[(*count)++] = name;
    return 1;
}

static int names_add(const char** names, uint32_t* count, uint32_t room, const SnxUpscalerExtensions* more) {
    if (more->count && !more->names) return 0;
    for (uint32_t i = 0; i < more->count; i++)
        if (!more->names[i] || !name_add(names, count, room, more->names[i])) return 0;
    return 1;
}

/* Before the instance is made: the adapter's instance extensions are added to the list. 0 when the adapter refuses
   or the list has no room. The instance is then made without the adapter, and the upscaler is off. */
int vk_upscaler_instance_extensions(const SnxUpscalerApi* api, const char** names, uint32_t* count, uint32_t room) {
    SnxUpscalerExtensions more = {0};
    if (!api || api->abi != SNX_UPSCALER_ABI || !api->instance_extensions) return 0;
    if (api->instance_extensions(&more) != SNX_UPSCALER_OK) return 0;
    return names_add(names, count, room, &more);
}

/* After the instance, before the device: the adapter's device extensions are added to the list, and features gets
   the chain of feature structures it wants on, or NULL. 0 as above: the device is then made without the adapter. */
int vk_upscaler_device_needs(const SnxUpscalerApi* api, VkInstance instance, VkPhysicalDevice device,
                             PFN_vkGetInstanceProcAddr lookup, const char** names, uint32_t* count, uint32_t room,
                             const void** features) {
    SnxUpscalerDeviceNeeds needs = {0};
    *features = NULL;
    if (!api || api->abi != SNX_UPSCALER_ABI || !api->device_extensions) return 0;
    if (api->device_extensions(instance, device, lookup, &needs) != SNX_UPSCALER_OK) return 0;
    if (!names_add(names, count, room, &needs.extensions)) return 0;
    *features = needs.features;
    return 1;
}
