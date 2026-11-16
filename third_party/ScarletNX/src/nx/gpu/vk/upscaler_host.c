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
#include "nx/gpu/vk/upscaler_module.h"
#include "nx/gpu/vk/upscaler_needs.h"
#include "common/gpu/vk/vk_common.h"
#include "common/utilities/common.h"
#include "nx/runtime/platform/log.h"

#ifdef _WIN32
const SnxUpscalerApi* snx_upscaler_fsr(void); /* gpu/vk/upscaler_fsr.cpp */
#endif

static const SnxUpscalerApi* module_adapter(const char* name);
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
    if (snx_module_name(name, 0) == SNX_MODULE_NAME_MODULE) return module_adapter(name);
    const SnxUpscalerApi* api = adapter_find(name, &built);
    if (name && !built) WARN("arceus-upscaler: no adapter named \"%s\" is built into this program\n", name);
    return api;
}

/* A name that is known to the self-test's lookup only gets one line in the game, and nothing else: no module is
   loaded, nothing is asked, no context is made. 1 when the name is such a one. The device's creation and the
   upscaler's start both ask this before anything else of the name. */
int vk_upscaler_not_in_game(const char* name) {
    if (snx_module_name(name, 0) != SNX_MODULE_NAME_NOT_IN_GAME) return 0;
    LOG("arceus-upscaler: %s is not available in the game in this build\n", name);
    return 1;
}
/* The log a module of its own is handed (SnxUpscalerLog): the line is copied, a line break or other control byte
   in it becomes a space so that it stays one line, and it is cut at 1023 bytes. Any thread may call it. */
static void module_log(const char* line) {
    char copy[1024];
    size_t n = 0;
    if (!line) return;
    for (; line[n] && n < sizeof copy - 1; n++) copy[n] = (unsigned char)line[n] < 0x20 ? ' ' : line[n];
    copy[n] = 0;
    LOG("%s%s\n", copy, line[n] ? " (cut)" : "");
}
/* An adapter that is a module of its own (snx_module_name): one file beside the program, named by its full path and
   loaded as upscaler_module.c says, once for the session. A load that failed is not tried again. */
static const SnxUpscalerApi* module_adapter(const char* name) {
    static const SnxUpscalerApi* api;
    static int tried;
    if (tried) return api;
    tried = 1;
#ifdef _WIN32
    static const char file[] = "snx_upscaler_dlss.dll";
#else
    static const char file[] = "snx_upscaler_dlss.so";
#endif
    char* program = snx_executable(NULL);
    char path[4096];
    size_t folder = 0;
    int why = SNX_MODULE_NOT_FULL_PATH;
    /* the program's own folder: up to its last separator */
    for (size_t i = 0; program && program[i]; i++)
        if (program[i] == '/' || program[i] == '\\') folder = i + 1;
    if (program && folder && folder + sizeof file <= sizeof path) {
        memcpy(path, program, folder);
        memcpy(path + folder, file, sizeof file);
        api = vk_upscaler_module_load(path, module_log, &why);
    } else
        path[0] = 0;
    free(program);
    if (!api)
        WARN("arceus-upscaler: %s is not available: %s (%s beside the program%s%s)\n", name, snx_module_text(why),
             file, path[0] ? ", " : "", path);
    return api;
}
/* The self-test's lookup: the same as the game's */
const SnxUpscalerApi* vk_upscaler_adapter_selftest(const char* name) { return vk_upscaler_adapter(name); }

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

/* The instance extensions added for an adapter when the instance was made, kept for its device and its start */
static const char* g_instance_names[SNX_NEEDS_NAMES];
static char g_instance_name[SNX_NEEDS_NAMES][SNX_NEEDS_NAME_LENGTH + 1];
static uint32_t g_instance_count;
/* Before the game's instance is made, with SNX_ARCEUS_UPSCALER naming an adapter (DL2): its instance extensions,
   each added to the list only when the instance lists it, as classified, and kept. 0 with one line when one is not
   listed or the list has no room: the instance is made without them, and the adapter is refused later. */
int vk_upscaler_instance_ask(const char* name, const char** names, uint32_t* count, uint32_t room) {
    SnxUpscalerExtensions more = {0};
    if (!name || vk_upscaler_not_in_game(name)) return 0;
    const SnxUpscalerApi* api = vk_upscaler_adapter(name);
    if (!api || api->abi != SNX_UPSCALER_ABI || !api->instance_extensions) return 0;
    if (api->instance_extensions(&more) != SNX_UPSCALER_OK || more.count > SNX_NEEDS_NAMES ||
        (more.count && !more.names)) {
        LOG("arceus-upscaler: %s: its needs of the instance could not be asked: it is not available\n", name);
        return 0;
    }
    const uint32_t before = *count;
    for (uint32_t i = 0; i < more.count; i++) {
        const char* n = more.names[i];
        if (!n || strlen(n) > SNX_NEEDS_NAME_LENGTH || !snx_vk_has_instance_extension(n) ||
            !name_add(names, count, room, n)) {
            LOG("arceus-upscaler: %s: the instance extension %s is not listed by the instance or has no room: the "
                "instance is made without its needs, and it is not available\n", name, n ? n : "(none)");
            for (uint32_t k = before; k < *count; k++) names[k] = NULL;
            *count = before;
            return 0;
        }
    }
    for (uint32_t i = 0; i < more.count; i++) {
        memcpy(g_instance_name[i], more.names[i], strlen(more.names[i]) + 1);
        g_instance_names[i] = g_instance_name[i];
    }
    g_instance_count = more.count;
    if (more.count) LOG("arceus-upscaler: %s: %u instance extensions added to the game's instance\n", name, more.count);
    return 1;
}
/* At the upscaler's start: whether every instance extension the adapter names was added when the instance was made */
int vk_upscaler_instance_met(const SnxUpscalerApi* api) {
    SnxUpscalerExtensions more = {0};
    if (!api || !api->instance_extensions || api->instance_extensions(&more) != SNX_UPSCALER_OK) return 0;
    return snx_needs_instance(more.names, more.count, g_instance_names, g_instance_count) == SNX_NEEDS_OK;
}
/* Before the instance is made: the adapter's instance extensions are added to the list. 0 when the adapter refuses
   or the list has no room. The instance is then made without the adapter, and the upscaler is off. */
int vk_upscaler_instance_extensions(const SnxUpscalerApi* api, const char** names, uint32_t* count, uint32_t room) {
    SnxUpscalerExtensions more = {0};
    if (!api || api->abi != SNX_UPSCALER_ABI || !api->instance_extensions) return 0;
    if (api->instance_extensions(&more) != SNX_UPSCALER_OK) return 0;
    return names_add(names, count, room, &more);
}

/* After the instance, before the device: the adapter's device extensions go onto the list, features gets the chain
   it wants on (or NULL) and core its core bits, which the caller turns on in its own structure. An adapter of another
   contract version is refused before any call, and so is a core bit the self-test does not know. 0: the device is
   then made without the adapter. */
int vk_upscaler_device_needs(const SnxUpscalerApi* api, VkInstance instance, VkPhysicalDevice device,
                             PFN_vkGetInstanceProcAddr lookup, const char** names, uint32_t* count, uint32_t room,
                             const void** features, uint32_t* core) {
    SnxUpscalerDeviceNeeds needs = {0};
    *features = NULL;
    *core = 0;
    if (!api || api->abi != SNX_UPSCALER_ABI || !api->device_extensions) return 0;
    if (api->device_extensions(instance, device, lookup, &needs) != SNX_UPSCALER_OK) return 0;
    if (needs.core & ~SNX_UPSCALER_CORE_SELFTEST_KNOWN) {
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
_Static_assert(SNX_NEEDS_CORE_SEPARATE_DEPTH_STENCIL == SNX_UPSCALER_CORE_SEPARATE_DEPTH_STENCIL &&
                   SNX_NEEDS_CORE_BUFFER_DEVICE_ADDRESS == SNX_UPSCALER_CORE_BUFFER_DEVICE_ADDRESS &&
                   SNX_NEEDS_CORE_STORAGE_WRITE_WITHOUT_FORMAT == SNX_UPSCALER_CORE_STORAGE_WRITE_WITHOUT_FORMAT &&
                   SNX_NEEDS_CORE_SHADER_INT8 == SNX_UPSCALER_CORE_SHADER_INT8 &&
                   SNX_NEEDS_CORE_INTEGER_DOT_PRODUCT == SNX_UPSCALER_CORE_INTEGER_DOT_PRODUCT &&
                   SNX_NEEDS_CORE_KNOWN == SNX_UPSCALER_CORE_SELFTEST_KNOWN,
               "the core bits of the contract and of the pure rules");
_Static_assert(SNX_NEEDS_DERIVATIVES == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_NV &&
                   SNX_NEEDS_MUTABLE == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT,
               "the accepted structures' types");
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
_Static_assert(sizeof(SnxNeedsMutable) == sizeof(VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT) &&
                   offsetof(SnxNeedsMutable, next) ==
                       offsetof(VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT, pNext) &&
                   offsetof(SnxNeedsMutable, on) ==
                       offsetof(VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT, mutableDescriptorType),
               "the mutable descriptor type structure's layout");

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
                           PFN_vkGetInstanceProcAddr lookup, uint32_t has_core, VkUpscalerDeviceAsk* ask) {
    SnxUpscalerExtensions of_instance = {0};
    SnxUpscalerDeviceNeeds needs = {0};
    SnxNeedsBits asked = {0, 0, 0, 0, 0}, has = {0, 0, 0, 0, 0};
    memset(ask, 0, sizeof *ask);
    if (vk_upscaler_not_in_game(name)) return 0; /* before any lookup: nothing of such a name is loaded or asked */
    int built = 0;
    const SnxUpscalerApi* api =
        snx_module_name(name, 0) == SNX_MODULE_NAME_MODULE ? module_adapter(name) : adapter_find(name, &built);
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
    /* its instance names: every one enabled when the instance was made for it (vk_upscaler_instance_ask) */
    if (snx_needs_instance(of_instance.names, of_instance.count, g_instance_names, g_instance_count) != SNX_NEEDS_OK) {
        device_refused(name, snx_needs_text(SNX_NEEDS_INSTANCE));
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
    if (why == SNX_NEEDS_OK) why = snx_needs_core(needs.core, has_core);
    if (why == SNX_NEEDS_OK) why = snx_needs_chain(needs.features, &asked);
    if (why == SNX_NEEDS_OK) why = snx_needs_owner(needs.extensions.names, needs.extensions.count, &asked);
    if (why == SNX_NEEDS_OK) why = device_names(device, needs.extensions.names, needs.extensions.count);
    if (why == SNX_NEEDS_OK && (asked.have || asked.mutable_have)) {
        /* the host's own question to the physical device, in structures of its own, each only when asked for */
        VkPhysicalDeviceComputeShaderDerivativesFeaturesNV q = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_NV};
        VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT m = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT};
        if (asked.have && asked.mutable_have) q.pNext = &m;
        VkPhysicalDeviceFeatures2 f2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                                        .pNext = asked.have ? (void*)&q : (void*)&m};
        vkGetPhysicalDeviceFeatures2(device, &f2);
        has = (SnxNeedsBits){asked.have, q.computeDerivativeGroupQuads != 0, q.computeDerivativeGroupLinear != 0,
                             asked.mutable_have, m.mutableDescriptorType != 0};
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
    ask->mutable_have = asked.mutable_have;
    ask->mutable_on = asked.mutable_on;
    ask->core = needs.core;
    /* bits the adapter does not ask for stay off. The caller sets the next pointer. */
    ask->derivatives = (VkPhysicalDeviceComputeShaderDerivativesFeaturesNV){
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_NV,
        .computeDerivativeGroupQuads = asked.quads ? VK_TRUE : VK_FALSE,
        .computeDerivativeGroupLinear = asked.linear ? VK_TRUE : VK_FALSE};
    ask->mutable_desc = (VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT){
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT,
        .mutableDescriptorType = asked.mutable_on ? VK_TRUE : VK_FALSE};
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
    const SnxNeedsBits on = {ask->have, ask->quads, ask->linear, ask->mutable_have, ask->mutable_on};
    const int kept = snx_needs_record(&g_needs, ask->adapter, ask->names, ask->count, &on, ask->core);
    for (uint32_t i = 0; i < ask->count; i++) {
        if (i) strcat(names, ", ");
        strcat(names, ask->names[i]);
    }
    LOG("arceus-upscaler: the game's device is made with %s%s%s%s%s%s%s%s%s for %s%s\n",
        ask->count ? names : "no extension", ask->have ? " and the compute derivatives features" : "",
        ask->quads ? " (quad groups)" : "", ask->linear ? " (linear groups)" : "",
        ask->mutable_on ? " and the mutable descriptor type" : "",
        ask->core & SNX_NEEDS_CORE_SEPARATE_DEPTH_STENCIL ? " and separate depth and stencil layouts" : "",
        ask->core & SNX_NEEDS_CORE_STORAGE_WRITE_WITHOUT_FORMAT ? " and storage writes without a format" : "",
        ask->core & SNX_NEEDS_CORE_SHADER_INT8 ? " and 8 bit integers in shaders" : "",
        ask->core & SNX_NEEDS_CORE_INTEGER_DOT_PRODUCT ? " and integer dot products" : "", ask->adapter,
        kept == SNX_NEEDS_OK ? "" : ". That could NOT be recorded: the adapter stays not available");
}
/* At the start of the upscaler: the adapter's needs, asked again by the caller, against what the game's device
   was made with. SNX_NEEDS_OK, or the reason (snx_needs_text). */
int vk_upscaler_device_met(const SnxUpscalerApi* api, const char* switch_name, const char* const* names,
                           uint32_t count, const void* features, uint32_t core) {
    return snx_needs_met(&g_needs, adapter_name(api, switch_name), names, count, features, core);
}
