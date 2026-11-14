/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The FSR adapter (upscaler.h): AMD's FSR upscaler through the FidelityFX SDK's API, whose headers are in
   third_party/FidelityFX-SDK. The SDK's own library, amd_fidelityfx_loader_vk.dll, is loaded by name from beside
   the program and is never part of this repository. Built on Windows only, where that library exists. Used by
   nothing but the self-test mode so far. C++ because the SDK's Vulkan helpers are. */
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include "api/include/vk/ffx_api_vk.h"
#include "upscalers/include/ffx_upscale.h"
#include "nx/gpu/vk/upscaler.h"

extern "C" {
void* plat_dl_open(const char* path);
void* plat_dl_sym(void* h, const char* name);
const char* plat_dl_error(void);
void logf_(int lvl, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
const SnxUpscalerApi* snx_upscaler_fsr(void);
}
#define FSR_LIBRARY "amd_fidelityfx_loader_vk.dll"

namespace {
struct Library {
    int tried;
    void* module;
    PfnFfxCreateContext create;
    PfnFfxDestroyContext destroy;
    PfnFfxQuery query;
    PfnFfxDispatch dispatch;
};
Library g_library;

template <typename T> void symbol(T& out, const char* name) {
    void* p = plat_dl_sym(g_library.module, name);
    std::memcpy(&out, &p, sizeof out);
}

/* the SDK's library and its functions, found once: 0 with one line when the adapter cannot be used */
int library(void) {
    if (g_library.tried) return g_library.dispatch != nullptr;
    g_library.tried = 1;
    g_library.module = plat_dl_open(FSR_LIBRARY);
    if (!g_library.module) {
        logf_(0, "arceus-upscaler: fsr is not available: %s was not found beside the program (%s)\n", FSR_LIBRARY,
              plat_dl_error());
        return 0;
    }
    symbol(g_library.create, "ffxCreateContext");
    symbol(g_library.destroy, "ffxDestroyContext");
    symbol(g_library.query, "ffxQuery");
    symbol(g_library.dispatch, "ffxDispatch");
    if (!g_library.create || !g_library.destroy || !g_library.query || !g_library.dispatch) {
        logf_(0, "arceus-upscaler: fsr is not available: %s lacks one of the API's functions\n", FSR_LIBRARY);
        g_library.dispatch = nullptr;
        return 0;
    }
    return 1;
}

void message(uint32_t type, const wchar_t* text) {
    char narrow[512];
    size_t n = 0;
    for (; text && text[n] && n + 1 < sizeof narrow; n++) narrow[n] = text[n] < 127 ? (char)text[n] : '?';
    narrow[n] = 0;
    logf_(type == FFX_API_MESSAGE_TYPE_ERROR ? 0 : 1, "arceus-upscaler: fsr says: %s\n", narrow);
}

/* The patch number of a version name of the 3.1 line ("3.1.6"), -1 for a name of another line or another form.
   Nothing but the line is fixed here: the newest patch the library holds is taken. */
long patch_of_3_1(const char* name) {
    if (!name || std::strncmp(name, "3.1.", 4)) return -1;
    char* end = nullptr;
    const long patch = std::strtol(name + 4, &end, 10);
    return end == name + 4 || patch < 0 ? -1 : patch;
}
}  // namespace

/* The descriptions given to the SDK at create must stay alive until the context is destroyed: they live here. */
struct SnxUpscaler {
    ffxContext context;
    SnxUpscalerCreate made;
    ffxCreateContextDescUpscale upscale;
    ffxCreateBackendVKDesc backend;
    ffxOverrideVersion version;
    ffxCreateContextDescUpscaleVersion built_for;
};

namespace {
int instance_extensions(SnxUpscalerExtensions* out) {
    out->count = 0;
    out->names = nullptr;
    return library() ? SNX_UPSCALER_OK : SNX_UPSCALER_REFUSED;
}

/* What this library needs of a device. A conservative policy for the one provider that was seen, not a complete
   list: in a run of the self-test its shader modules declared SPV_KHR_compute_shader_derivatives with linear
   derivative groups, and the layer reported the device as made without them. The feature's structure is the
   one the NV extension named first: the KHR extension has the same structure and the same type number. */
#define FSR_DERIVATIVES "VK_KHR_compute_shader_derivatives"
const char* const g_device_names[1] = {FSR_DERIVATIVES};
/* handed to whoever makes the device, as a node of its feature chain: it lives as long as the adapter */
VkPhysicalDeviceComputeShaderDerivativesFeaturesNV g_derivatives = {
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_NV, nullptr, VK_FALSE, VK_TRUE};

/* The extension and its feature for linear groups are required: a device without either is refused, since the
   library would load the same shaders there. A list or a query that fails refuses too, and nothing is taken
   from a part of a list. 16 bit floats in shaders are a core feature that whoever makes the device turns on
   where the physical device has it: the library picks those shaders by the physical device. It is only said. */
int device_extensions(VkInstance instance, VkPhysicalDevice device, PFN_vkGetInstanceProcAddr lookup,
                      SnxUpscalerDeviceNeeds* out) {
    out->extensions.count = 0;
    out->extensions.names = nullptr;
    out->features = nullptr;
    if (!library()) return SNX_UPSCALER_REFUSED;
    if (!instance || !device || !lookup) {
        logf_(0, "arceus-upscaler: fsr is not available: its needs of the device cannot be asked without one\n");
        return SNX_UPSCALER_REFUSED;
    }
    const auto enumerate =
        (PFN_vkEnumerateDeviceExtensionProperties)lookup(instance, "vkEnumerateDeviceExtensionProperties");
    const auto features = (PFN_vkGetPhysicalDeviceFeatures2)lookup(instance, "vkGetPhysicalDeviceFeatures2");
    if (!enumerate || !features) {
        logf_(0, "arceus-upscaler: fsr is not available: the instance has no call to list a device's extensions or "
                 "to ask its features\n");
        return SNX_UPSCALER_REFUSED;
    }
    /* the list, whole: asked again with a fresh count when it comes back incomplete, three times at most */
    VkExtensionProperties* list = nullptr;
    uint32_t n = 0;
    VkResult listed = VK_INCOMPLETE;
    for (int tries = 0; tries < 3 && listed == VK_INCOMPLETE; tries++) {
        std::free(list);
        list = nullptr;
        n = 0;
        listed = enumerate(device, nullptr, &n, nullptr);
        if (listed != VK_SUCCESS) break;
        list = (VkExtensionProperties*)std::calloc(n ? n : 1, sizeof *list);
        listed = list ? enumerate(device, nullptr, &n, list) : VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    if (listed != VK_SUCCESS) {
        logf_(0, "arceus-upscaler: fsr is not available: the device's extensions could not be listed whole (result "
                 "%d), and nothing is taken from a part of the list\n", (int)listed);
        std::free(list);
        return SNX_UPSCALER_REFUSED;
    }
    int found = 0;
    for (uint32_t i = 0; i < n; i++) found |= !std::strcmp(list[i].extensionName, FSR_DERIVATIVES);
    std::free(list);
    if (!found) {
        logf_(0, "arceus-upscaler: fsr is not available: the device has no " FSR_DERIVATIVES ", which the "
                 "library's shaders declare\n");
        return SNX_UPSCALER_REFUSED;
    }
    /* asked in a chain of the query's own: the derivatives' feature, and whether shaders have 16 bit floats */
    VkPhysicalDeviceShaderFloat16Int8Features halves = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES,
                                                        nullptr, VK_FALSE, VK_FALSE};
    VkPhysicalDeviceComputeShaderDerivativesFeaturesNV has = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COMPUTE_SHADER_DERIVATIVES_FEATURES_NV, &halves, VK_FALSE, VK_FALSE};
    VkPhysicalDeviceFeatures2 asked = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &has, {}};
    features(device, &asked);
    if (!has.computeDerivativeGroupLinear) {
        logf_(0, "arceus-upscaler: fsr is not available: the device has " FSR_DERIVATIVES " without linear "
                 "derivative groups, which the library's shaders declare\n");
        return SNX_UPSCALER_REFUSED;
    }
    static int said;
    if (!said++)
        logf_(1, "arceus-upscaler: fsr names its needs of the device: " FSR_DERIVATIVES " with linear derivative "
                 "groups. The physical device %s 16 bit floats in shaders, a core feature the device's maker turns "
                 "on where it is there. A conservative policy for the library that was seen, not a complete list\n",
              halves.shaderFloat16 ? "has" : "does not have");
    out->extensions.count = 1;
    out->extensions.names = g_device_names;
    out->features = &g_derivatives;
    return SNX_UPSCALER_OK;
}

int create(const SnxUpscalerCreate* made, SnxUpscalerTraits* traits, SnxUpscaler** out) {
    *out = nullptr;
    if (!library()) return SNX_UPSCALER_REFUSED;
    SnxUpscaler* u = new (std::nothrow) SnxUpscaler();
    if (!u) return SNX_UPSCALER_FAILED;
    u->made = *made;
    u->backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK;
    u->backend.deviceContext.vkDevice = made->device;
    u->backend.deviceContext.vkPhysicalDevice = made->physical_device;
    u->backend.deviceContext.vkDeviceProcAddr = made->device_lookup;
    /* The library's versions are asked with the device. It fills the places it is given and says how many it
       wrote, so a list is whole only when it wrote fewer than there was room for: it is asked with room to spare,
       three times at most. The newest of the 3.1 line is taken and its name copied at once, since the strings are
       the library's and a later query may replace them. */
    uint64_t count = 0, room = 0;
    ffxQueryDescGetVersions versions = {};
    versions.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
    versions.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    versions.device = &u->backend.deviceContext;
    versions.outputCount = &count;
    ffxReturnCode_t rc = g_library.query(nullptr, &versions.header);
    const uint64_t offered = count;
    long best = -1;
    uint64_t chosen = 0;
    char chosen_name[64] = "";
    int complete = 0;
    for (int attempt = 0; rc == FFX_API_RETURN_OK && !complete && attempt < 3; attempt++) {
        room = (offered + 1) << attempt;
        if (room > 4096) break;
        uint64_t* ids = new (std::nothrow) uint64_t[room]();
        const char** names = new (std::nothrow) const char*[room]();
        count = room;
        versions.versionIds = ids;
        versions.versionNames = names;
        if (ids && names) rc = g_library.query(nullptr, &versions.header);
        complete = ids && names && rc == FFX_API_RETURN_OK && count < room;
        best = -1;
        for (uint64_t i = 0; complete && i < count; i++) {
            const long patch = patch_of_3_1(names[i]);
            if (patch <= best) continue;
            best = patch;
            chosen = ids[i];
            std::snprintf(chosen_name, sizeof chosen_name, "%s", names[i]);
        }
        delete[] ids;
        delete[] names;
        if (!ids || !names) break;
    }
    if (!complete || best < 0) {
        logf_(0, "arceus-upscaler: fsr is not available: %s (query %u, %llu versions offered, %llu written into "
                 "%llu places)\n",
              complete ? "the library offers no version of the 3.1 line" : "its versions could not be read whole",
              rc, (unsigned long long)offered, (unsigned long long)count, (unsigned long long)room);
        delete u;
        return SNX_UPSCALER_REFUSED;
    }
    u->version.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION;
    u->version.versionId = chosen;
    u->built_for.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION;
    u->built_for.version = FFX_UPSCALER_VERSION;
    u->upscale.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
    /* no flag for jitter in the vectors: ours are without it. Dynamic resolution: the render size may be smaller
       than the largest one. */
    u->upscale.flags = FFX_UPSCALE_ENABLE_DYNAMIC_RESOLUTION |
                       (made->flags & SNX_UPSCALER_COLOUR_LINEAR_HDR ? FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE : 0) |
                       (made->flags & SNX_UPSCALER_DEPTH_REVERSED ? FFX_UPSCALE_ENABLE_DEPTH_INVERTED : 0) |
                       (made->flags & SNX_UPSCALER_FAR_PLANE_INFINITE ? FFX_UPSCALE_ENABLE_DEPTH_INFINITE : 0) |
                       (made->flags & SNX_UPSCALER_EXPOSURE_AUTOMATIC ? FFX_UPSCALE_ENABLE_AUTO_EXPOSURE : 0);
    u->upscale.maxRenderSize.width = made->sizes.render_w;
    u->upscale.maxRenderSize.height = made->sizes.render_h;
    u->upscale.maxUpscaleSize.width = made->sizes.output_w;
    u->upscale.maxUpscaleSize.height = made->sizes.output_h;
    u->upscale.fpMessage = message;
    u->upscale.header.pNext = &u->backend.header;
    u->backend.header.pNext = &u->version.header;
    u->version.header.pNext = &u->built_for.header;
    rc = g_library.create(&u->context, &u->upscale.header, nullptr);
    if (rc != FFX_API_RETURN_OK) {
        logf_(0, "arceus-upscaler: fsr %s could not make a context for %ux%u to %ux%u (code %u)\n", chosen_name,
              made->sizes.render_w, made->sizes.render_h, made->sizes.output_w, made->sizes.output_h, rc);
        delete u;
        return SNX_UPSCALER_FAILED;
    }
    /* What really runs, as the context itself says. It must say so, and it must be the version asked for: a
       context that runs anything else, or cannot tell, is destroyed and the adapter refuses. */
    ffxQueryGetProviderVersion runs = {};
    runs.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
    const ffxReturnCode_t asked = g_library.query(&u->context, &runs.header);
    char runs_name[64] = "";
    if (asked == FFX_API_RETURN_OK && runs.versionName)
        std::snprintf(runs_name, sizeof runs_name, "%s", runs.versionName);
    if (asked != FFX_API_RETURN_OK || runs.versionId != chosen || !runs_name[0]) {
        logf_(0, "arceus-upscaler: fsr refused: asked for %s (id %llx), and the context says it runs \"%s\" (id "
                 "%llx, query %u)\n", chosen_name, (unsigned long long)chosen, runs_name,
              (unsigned long long)runs.versionId, asked);
        const ffxReturnCode_t gone = g_library.destroy(&u->context, nullptr);
        if (gone != FFX_API_RETURN_OK)
            logf_(0, "arceus-upscaler: fsr could not destroy that context (code %u)\n", gone);
        delete u;
        return SNX_UPSCALER_REFUSED;
    }
    logf_(1, "arceus-upscaler: fsr context made by %s: asked for %s (id %llx), runs %s (id %llx), %ux%u to %ux%u, "
             "flags %x\n", FSR_LIBRARY, chosen_name, (unsigned long long)chosen, runs_name,
          (unsigned long long)runs.versionId, made->sizes.render_w, made->sizes.render_h, made->sizes.output_w,
          made->sizes.output_h, u->upscale.flags);
    traits->submits_itself = 0;
    traits->takes_rectangle = 1;
    traits->rectangle_at_origin = 1; /* the SDK reads its inputs from the first texel */
    traits->colour_usage = traits->depth_usage = traits->motion_usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    traits->output_usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    *out = u;
    return SNX_UPSCALER_OK;
}

/* FSR has nothing to run once: its context is ready when it is made */
int prepare(SnxUpscaler*, VkCommandBuffer) { return SNX_UPSCALER_OK; }

FfxApiResource resource(const SnxUpscalerImage& image, VkImageUsageFlags usage, uint32_t state) {
    VkImageCreateInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = image.format;
    info.extent.width = image.w;
    info.extent.height = image.h;
    info.extent.depth = 1;
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.usage = usage;
    return ffxApiGetResourceVK((void*)image.image, ffxApiGetImageResourceDescriptionVK(image.image, info, 0), state);
}

int evaluate(SnxUpscaler* u, const SnxUpscalerFrame* f) {
    /* the SDK wants its inputs readable by shaders and has no offset into them */
    if (f->render_x || f->render_y || f->colour.layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
        f->depth.layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
        f->motion.layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL || f->output.layout != VK_IMAGE_LAYOUT_GENERAL) {
        logf_(0, "arceus-upscaler: fsr was given a rectangle at %d,%d or an image in a layout it does not take\n",
              f->render_x, f->render_y);
        return SNX_UPSCALER_FAILED;
    }
    ffxDispatchDescUpscale d = {};
    d.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
    d.commandList = f->cb;
    d.color = resource(f->colour, VK_IMAGE_USAGE_SAMPLED_BIT, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    d.depth = resource(f->depth, VK_IMAGE_USAGE_SAMPLED_BIT, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    d.motionVectors = resource(f->motion, VK_IMAGE_USAGE_SAMPLED_BIT, FFX_API_RESOURCE_STATE_COMPUTE_READ);
    d.output = resource(f->output, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                        FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    d.jitterOffset.x = f->jitter[0];
    d.jitterOffset.y = f->jitter[1];
    d.motionVectorScale.x = f->vector_scale[0];
    d.motionVectorScale.y = f->vector_scale[1];
    d.renderSize.width = f->render_w;
    d.renderSize.height = f->render_h;
    d.upscaleSize.width = f->output.w;
    d.upscaleSize.height = f->output.h;
    d.enableSharpening = false;
    d.sharpness = 0.0f;
    d.frameTimeDelta = f->frame_seconds * 1000.0f; /* the SDK counts in milliseconds */
    d.preExposure = 1.0f;
    d.reset = f->reset != 0;
    d.cameraNear = f->near_plane;
    /* With reversed depth and an infinite far plane FSR 3.1 uses only the smaller of near and far (its source at
       3.1.6: view depth = that over depth plus FLT_EPSILON), so far is the largest a float holds. Read for the
       3.1 line only, which is the one this adapter asks for. */
    d.cameraFar = std::isinf(f->far_plane) ? 3.4028234663852886e38f : f->far_plane;
    d.cameraFovAngleVertical = f->fov_y;
    d.viewSpaceToMetersFactor = 1.0f;
    /* Asked for by the caller: the dispatch as it stands in the SDK's own structure, read back from it field by
       field after it is filled, every resource with the description and the state the SDK gets, an unset one
       named as such. A third line, marked as ours, has what the caller's record said of the same images. */
    if (f->trace) {
        const struct {
            const char* name;
            const FfxApiResource* r;
        } resources[7] = {{"color", &d.color},
                          {"depth", &d.depth},
                          {"motionVectors", &d.motionVectors},
                          {"exposure", &d.exposure},
                          {"reactive", &d.reactive},
                          {"transparencyAndComposition", &d.transparencyAndComposition},
                          {"output", &d.output}};
        char text[1400];
        size_t at = 0;
        text[0] = 0;
        for (const auto& one : resources) {
            const FfxApiResourceDescription& n = one.r->description;
            const int wrote =
                snprintf(text + at, sizeof text - at,
                         "%s%s %s, type %u, format %u, %ux%ux%u, mips %u, flags 0x%x, usage 0x%x, state %u",
                         at ? ". " : "", one.name, one.r->resource ? "set" : "NOT SET (null)", n.type, n.format,
                         n.width, n.height, n.depth, n.mipCount, n.flags, n.usage, one.r->state);
            if (wrote < 0 || (size_t)wrote >= sizeof text - at) break;
            at += (size_t)wrote;
        }
        logf_(1, "arceus-upscaler: fsr dispatch, the SDK's structure: header type 0x%llx, commandList %s, "
                 "jitterOffset %.9g %.9g, motionVectorScale %.9g %.9g, renderSize %ux%u, upscaleSize %ux%u, "
                 "enableSharpening %d, sharpness %.9g, frameTimeDelta %.9g ms, preExposure %.9g, reset %d, "
                 "cameraNear %.9g, cameraFar %.9g, cameraFovAngleVertical %.9g, viewSpaceToMetersFactor %.9g, flags "
                 "0x%x\n",
              (unsigned long long)d.header.type, d.commandList ? "set" : "NOT SET (null)", (double)d.jitterOffset.x,
              (double)d.jitterOffset.y, (double)d.motionVectorScale.x, (double)d.motionVectorScale.y,
              d.renderSize.width, d.renderSize.height, d.upscaleSize.width, d.upscaleSize.height,
              (int)d.enableSharpening, (double)d.sharpness, (double)d.frameTimeDelta, (double)d.preExposure,
              (int)d.reset, (double)d.cameraNear, (double)d.cameraFar, (double)d.cameraFovAngleVertical,
              (double)d.viewSpaceToMetersFactor, d.flags);
        logf_(1, "arceus-upscaler: fsr dispatch, the SDK's resources: %s\n", text);
        logf_(1, "arceus-upscaler: fsr dispatch, ours and not the SDK's: the context was made with flags 0x%x, "
                 "maxRenderSize %ux%u, maxUpscaleSize %ux%u. The caller's images as width x height, Vulkan format, "
                 "Vulkan layout: colour %ux%u %d %d, depth %ux%u %d %d, vectors %ux%u %d %d, output %ux%u %d %d\n",
              (unsigned)u->upscale.flags, u->upscale.maxRenderSize.width, u->upscale.maxRenderSize.height,
              u->upscale.maxUpscaleSize.width, u->upscale.maxUpscaleSize.height, f->colour.w, f->colour.h,
              (int)f->colour.format, (int)f->colour.layout, f->depth.w, f->depth.h, (int)f->depth.format,
              (int)f->depth.layout, f->motion.w, f->motion.h, (int)f->motion.format, (int)f->motion.layout,
              f->output.w, f->output.h, (int)f->output.format, (int)f->output.layout);
    }
    const ffxReturnCode_t rc = g_library.dispatch(&u->context, &d.header);
    if (rc == FFX_API_RETURN_OK) return SNX_UPSCALER_OK;
    logf_(0, "arceus-upscaler: fsr dispatch failed (code %u)\n", rc);
    return SNX_UPSCALER_FAILED;
}

void destroy(SnxUpscaler* u) {
    if (!u) return;
    const ffxReturnCode_t rc = g_library.destroy(&u->context, nullptr);
    if (rc != FFX_API_RETURN_OK) logf_(0, "arceus-upscaler: fsr could not destroy its context (code %u)\n", rc);
    delete u;
}

const SnxUpscalerApi g_api = {SNX_UPSCALER_ABI, "fsr", instance_extensions, device_extensions, create, prepare,
                              evaluate, destroy};
}  // namespace

/* the adapter, NULL with one line when the SDK's library is not beside the program */
const SnxUpscalerApi* snx_upscaler_fsr(void) { return library() ? &g_api : nullptr; }
