/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The runtime's side of an upscaler's adapter before a device exists (upscaler.h): which adapter a name means, and
   what the instance and the device have to be made with for it. Written once, as the runtime will use it. So far
   the self-test mode calls the first part (SNX_ARCEUS_UPSCALER_SELFTEST). The game's device asks an adapter only
   with SNX_ARCEUS_UPSCALER naming one (the last part): without the switch it is made as it always was. */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nx/gpu/vk/vk.h"
#include "nx/gpu/vk/upscaler.h"
#include "nx/gpu/vk/upscaler_needs.h"
#include "nx/runtime/platform/log.h"

#ifdef _WIN32
const SnxUpscalerApi* snx_upscaler_fsr(void); /* gpu/vk/upscaler_fsr.cpp */
#endif

/* built: whether an adapter of that name is part of this program at all */
static const SnxUpscalerApi* adapter_find(const char* name, int* built) {
    *built = 0;
#ifdef _WIN32
    if (name && !strcmp(name, "fsr")) {
        *built = 1;
        return snx_upscaler_fsr();
    }
#endif
    return NULL;
}
/* The adapter of that name, NULL for none: one that is not built into this program, or one whose SDK library is not
   beside it. The adapter says why in a line of its own. */
const SnxUpscalerApi* vk_upscaler_adapter(const char* name) {
    int built = 0;
    const SnxUpscalerApi* api = adapter_find(name, &built);
    if (name && !built) WARN("arceus-upscaler: no adapter named \"%s\" is built into this program\n", name);
    return api;
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

/* After the instance, before the device: the adapter's device extensions are added to the list, features gets
   the chain of feature structures it wants on, or NULL, and core the core feature bits it needs, which the
   caller turns on in its own structure after its own question to the physical device. An adapter of another
   version of the contract is refused before anything of it is called, and so is a core bit the host does not
   know. 0 as above: the device is then made without the adapter. */
int vk_upscaler_device_needs(const SnxUpscalerApi* api, VkInstance instance, VkPhysicalDevice device,
                             PFN_vkGetInstanceProcAddr lookup, const char** names, uint32_t* count, uint32_t room,
                             const void** features, uint32_t* core) {
    SnxUpscalerDeviceNeeds needs = {0};
    *features = NULL;
    *core = 0;
    if (!api || api->abi != SNX_UPSCALER_ABI || !api->device_extensions) return 0;
    if (api->device_extensions(instance, device, lookup, &needs) != SNX_UPSCALER_OK) return 0;
    if (needs.core & ~SNX_NEEDS_CORE_KNOWN) {
        WARN("arceus-upscaler: the adapter asks for a core feature bit the host does not know (0x%x)\n", needs.core);
        return 0;
    }
    if (!names_add(names, count, room, &needs.extensions)) return 0;
    *features = needs.features;
    *core = needs.core;
    return 1;
}

/* The game's device (upscaler_needs.h has the rules). The one structure the host accepts is Vulkan's. The pure
   header copies its fields out of the object's bytes at its stand-in's offsets, which must be Vulkan's. */
_Static_assert(SNX_NEEDS_CORE_SEPARATE_DEPTH_STENCIL == SNX_UPSCALER_CORE_SEPARATE_DEPTH_STENCIL,
               "the core bit of the contract and of the pure rules");
_Static_assert(SNX_NEEDS_DERIVATIVES == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_NV,
               "the accepted structure's type");
_Static_assert(offsetof(SnxNeedsNode, type) == offsetof(VkBaseInStructure, sType) &&
                   offsetof(SnxNeedsNode, next) == offsetof(VkBaseInStructure, pNext) &&
                   sizeof(((SnxNeedsNode*)0)->type) == sizeof(VkStructureType) && sizeof(VkBool32) == sizeof(uint32_t),
               "every structure's first two members");
_Static_assert(sizeof(SnxNeedsDerivatives) == sizeof(VkPhysicalDeviceComputeShaderDerivativesFeaturesNV) &&
                   offsetof(SnxNeedsDerivatives, next) ==
                       offsetof(VkPhysicalDeviceComputeShaderDerivativesFeaturesNV, pNext) &&
                   offsetof(SnxNeedsDerivatives, quads) ==
                       offsetof(VkPhysicalDeviceComputeShaderDerivativesFeaturesNV, computeDerivativeGroupQuads) &&
                   offsetof(SnxNeedsDerivatives, linear) ==
                       offsetof(VkPhysicalDeviceComputeShaderDerivativesFeaturesNV, computeDerivativeGroupLinear),
               "the accepted structure's layout");

static SnxNeedsRecord g_needs; /* what the game's device was made with for an adapter: none until then */
/* the host's own copies of the names it adds: the device's list points here, never at the adapter's strings */
static char g_names[SNX_NEEDS_NAMES][SNX_NEEDS_NAME_LENGTH + 1];

static const char* adapter_name(const SnxUpscalerApi* api, const char* switch_name) {
    return api->name ? api->name : switch_name;
}
static void device_refused(const char* name, const char* why) {
    LOG("arceus-upscaler: the game's device is made as without the switch, not with the needs of %s: %s\n", name,
        why);
}

/* The physical device's list of extensions against the adapter's names. Read whole or not used: an incomplete
   list is asked again, three times at most. */
static int device_names(VkPhysicalDevice device, const char* const* names, uint32_t count) {
    VkExtensionProperties* list = NULL;
    const char** have = NULL;
    uint32_t n = 0;
    VkResult r = VK_INCOMPLETE;
    for (int tries = 0; tries < 3 && r == VK_INCOMPLETE; tries++) {
        free(list);
        list = NULL;
        n = 0;
        r = vkEnumerateDeviceExtensionProperties(device, NULL, &n, NULL);
        if (r != VK_SUCCESS) break;
        list = calloc(n ? n : 1, sizeof *list);
        if (!list) {
            r = VK_ERROR_OUT_OF_HOST_MEMORY;
            break;
        }
        r = vkEnumerateDeviceExtensionProperties(device, NULL, &n, list);
    }
    if (r == VK_SUCCESS) have = calloc(n ? n : 1, sizeof *have);
    for (uint32_t i = 0; have && i < n; i++) have[i] = list[i].extensionName;
    const int answer = snx_needs_names(names, count, have, n, r == VK_SUCCESS && have != NULL);
    free(have);
    free(list);
    return answer;
}

/* Before the game's device is made, with SNX_ARCEUS_UPSCALER naming an adapter: what the adapter needs of it,
   checked by the host and copied by value. 1 when the device is to be tried with it. 0 with one line, or with
   none when there is no such adapter: the device is made as without the switch. The adapter's storage is read
   and never written or kept. */
int vk_upscaler_device_ask(const char* name, VkInstance instance, VkPhysicalDevice device,
                           PFN_vkGetInstanceProcAddr lookup, int has_separate_depth_stencil,
                           VkUpscalerDeviceAsk* ask) {
    SnxUpscalerExtensions of_instance = {0};
    SnxUpscalerDeviceNeeds needs = {0};
    SnxNeedsBits asked = {0, 0, 0}, has = {0, 0, 0};
    memset(ask, 0, sizeof *ask);
    int built = 0;
    const SnxUpscalerApi* api = adapter_find(name, &built);
    /* No adapter: nothing is said here. The upscaler's start has the one line for a name that is not built in. */
    if (!api) return 0;
    if (api->abi != SNX_UPSCALER_ABI || !api->instance_extensions || !api->device_extensions) {
        device_refused(name, "the adapter is of another version of the contract, or cannot be asked");
        return 0;
    }
    if (api->instance_extensions(&of_instance) != SNX_UPSCALER_OK ||
        api->device_extensions(instance, device, lookup, &needs) != SNX_UPSCALER_OK) {
        device_refused(name, "its needs of the instance and the device could not be asked");
        return 0;
    }
    if (of_instance.count) {
        device_refused(name, "it names needs of the instance, which is already made");
        return 0;
    }
    if (snx_needs_none(needs.extensions.count, needs.features, needs.core)) {
        device_refused(name, "it names no needs of the device");
        return 0;
    }
    int why = SNX_NEEDS_OK;
    if (needs.extensions.count > SNX_NEEDS_NAMES || (needs.extensions.count && !needs.extensions.names))
        why = SNX_NEEDS_ROOM;
    for (uint32_t i = 0; why == SNX_NEEDS_OK && i < needs.extensions.count; i++)
        if (!needs.extensions.names[i] || strlen(needs.extensions.names[i]) > SNX_NEEDS_NAME_LENGTH)
            why = SNX_NEEDS_ROOM;
    /* the core bits against the caller's own question to the physical device */
    if (why == SNX_NEEDS_OK) why = snx_needs_core(needs.core, has_separate_depth_stencil);
    if (why == SNX_NEEDS_OK) why = snx_needs_chain(needs.features, &asked);
    if (why == SNX_NEEDS_OK) why = snx_needs_owner(needs.extensions.names, needs.extensions.count, &asked);
    if (why == SNX_NEEDS_OK) why = device_names(device, needs.extensions.names, needs.extensions.count);
    if (why == SNX_NEEDS_OK && asked.have) {
        /* the host's own question to the physical device, in a structure of its own */
        VkPhysicalDeviceComputeShaderDerivativesFeaturesNV q = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_NV};
        VkPhysicalDeviceFeatures2 f2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &q};
        vkGetPhysicalDeviceFeatures2(device, &f2);
        has = (SnxNeedsBits){1, q.computeDerivativeGroupQuads != 0, q.computeDerivativeGroupLinear != 0};
        why = snx_needs_bits(&asked, &has);
    }
    if (why != SNX_NEEDS_OK) {
        device_refused(name, snx_needs_text(why));
        return 0;
    }
    snprintf(ask->adapter, sizeof ask->adapter, "%s", adapter_name(api, name));
    if (strcmp(ask->adapter, adapter_name(api, name))) {
        device_refused(name, snx_needs_text(SNX_NEEDS_ROOM));
        return 0;
    }
    for (uint32_t i = 0; i < needs.extensions.count; i++) {
        memcpy(g_names[i], needs.extensions.names[i], strlen(needs.extensions.names[i]) + 1);
        ask->names[i] = g_names[i];
    }
    ask->count = needs.extensions.count;
    ask->have = asked.have;
    ask->quads = asked.quads;
    ask->linear = asked.linear;
    ask->core = needs.core;
    /* bits the adapter does not ask for stay off. The caller sets the next pointer. */
    ask->derivatives = (VkPhysicalDeviceComputeShaderDerivativesFeaturesNV){
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_NV,
        .computeDerivativeGroupQuads = asked.quads ? VK_TRUE : VK_FALSE,
        .computeDerivativeGroupLinear = asked.linear ? VK_TRUE : VK_FALSE};
    return 1;
}
/* The names behind the game's own count, on a count of the caller's own: the game's list is not changed in
   place. 0 with a line when the list has no room. */
int vk_upscaler_device_names(const VkUpscalerDeviceAsk* ask, const char** names, uint32_t* count, uint32_t room) {
    for (uint32_t i = 0; i < ask->count; i++)
        if (!name_add(names, count, room, ask->names[i])) {
            device_refused(ask->adapter, "the device's list of extensions has no room for its names");
            return 0;
        }
    return 1;
}
/* The device could not be made with the additions: it is made again from the game's own list and chain */
void vk_upscaler_device_failed(const VkUpscalerDeviceAsk* ask, int result) {
    LOG("arceus-upscaler: the game's device could not be made with the needs of %s (result %d). It is made as "
        "without the switch, and the adapter is not available\n", ask->adapter, result);
}
/* The device was made with the additions: the record, by value, and the one line */
void vk_upscaler_device_made(const VkUpscalerDeviceAsk* ask) {
    char names[SNX_NEEDS_NAMES * (SNX_NEEDS_NAME_LENGTH + 2) + 1] = "";
    const SnxNeedsBits on = {ask->have, ask->quads, ask->linear};
    const int kept = snx_needs_record(&g_needs, ask->adapter, ask->names, ask->count, &on, ask->core);
    for (uint32_t i = 0; i < ask->count; i++) {
        if (i) strcat(names, ", ");
        strcat(names, ask->names[i]);
    }
    LOG("arceus-upscaler: the game's device is made with %s%s%s%s%s for %s%s\n",
        ask->count ? names : "no extension", ask->have ? " and the compute derivatives features" : "",
        ask->quads ? " (quad groups)" : "", ask->linear ? " (linear groups)" : "",
        ask->core & SNX_NEEDS_CORE_SEPARATE_DEPTH_STENCIL ? " and separate depth and stencil layouts" : "",
        ask->adapter,
        kept == SNX_NEEDS_OK ? "" : ". That could NOT be recorded: the adapter stays not available");
}
/* At the start of the upscaler: the adapter's needs, asked again by the caller, against what the game's device
   was made with. SNX_NEEDS_OK, or the reason (snx_needs_text). */
int vk_upscaler_device_met(const SnxUpscalerApi* api, const char* switch_name, const char* const* names,
                           uint32_t count, const void* features, uint32_t core) {
    return snx_needs_met(&g_needs, adapter_name(api, switch_name), names, count, features, core);
}
