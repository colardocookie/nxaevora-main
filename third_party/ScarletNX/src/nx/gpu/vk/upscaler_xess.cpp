/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The XeSS adapter (upscaler.h, XESS-DESIGN.md revision 2): Intel's XeSS Super Resolution through its Vulkan API,
   whose MIT headers are in third_party/XeSS. The SDK's library, libxess.dll, is loaded by name from beside the
   program, supplied by the user and never part of this repository. Built on Windows only. Its prepare submits work
   of its own (submits_at_init): the runtime runs it between two idle waits (upscaler_protocol.h). */
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include "xess/xess_vk.h"
#include "nx/gpu/vk/upscaler.h"
#include "nx/gpu/vk/upscaler_modes.h"

extern "C" {
void* plat_dl_open(const char* path);
void* plat_dl_sym(void* h, const char* name);
const char* plat_dl_error(void);
uint64_t plat_ns(void);
void logf_(int lvl, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
const SnxUpscalerApi* snx_upscaler_xess(void);
}
#define XESS_LIBRARY "libxess.dll"
#define XESS_MUTABLE "VK_EXT_mutable_descriptor_type"

namespace {
struct Library {
    int tried;
    void* module;
    decltype(&xessGetVersion) version;
    decltype(&xessVKGetRequiredInstanceExtensions) instance_names;
    decltype(&xessVKGetRequiredDeviceExtensions) device_names;
    decltype(&xessVKGetRequiredDeviceFeatures) device_features;
    decltype(&xessVKCreateContext) create;
    decltype(&xessVKBuildPipelines) build;
    decltype(&xessVKInit) init;
    decltype(&xessVKExecute) execute;
    decltype(&xessDestroyContext) destroy;
    decltype(&xessGetOptimalInputResolution) optimal;
    decltype(&xessSetVelocityScale) velocity_scale;
    decltype(&xessSetJitterScale) jitter_scale;
    decltype(&xessSetLoggingCallback) logging;
    decltype(&xessIsOptimalDriver) driver;
};
Library g_library;

template <typename T> void symbol(T& out, const char* name) {
    void* p = plat_dl_sym(g_library.module, name);
    std::memcpy(&out, &p, sizeof out);
}

/* the SDK's library and its functions, found once: 0 with one line when the adapter cannot be used */
int library(void) {
    if (g_library.tried) return g_library.execute != nullptr;
    g_library.tried = 1;
    g_library.module = plat_dl_open(XESS_LIBRARY);
    if (!g_library.module) {
        logf_(0, "arceus-upscaler: xess is not available: %s was not found beside the program (%s)\n", XESS_LIBRARY,
              plat_dl_error());
        return 0;
    }
    Library& l = g_library;
    symbol(l.version, "xessGetVersion");
    symbol(l.instance_names, "xessVKGetRequiredInstanceExtensions");
    symbol(l.device_names, "xessVKGetRequiredDeviceExtensions");
    symbol(l.device_features, "xessVKGetRequiredDeviceFeatures");
    symbol(l.create, "xessVKCreateContext");
    symbol(l.build, "xessVKBuildPipelines");
    symbol(l.init, "xessVKInit");
    symbol(l.execute, "xessVKExecute");
    symbol(l.destroy, "xessDestroyContext");
    symbol(l.optimal, "xessGetOptimalInputResolution");
    symbol(l.velocity_scale, "xessSetVelocityScale");
    symbol(l.jitter_scale, "xessSetJitterScale");
    symbol(l.logging, "xessSetLoggingCallback");
    symbol(l.driver, "xessIsOptimalDriver");
    if (!l.version || !l.instance_names || !l.device_names || !l.device_features || !l.create || !l.build ||
        !l.init || !l.execute || !l.destroy || !l.optimal || !l.velocity_scale || !l.jitter_scale || !l.logging ||
        !l.driver) {
        logf_(0, "arceus-upscaler: xess is not available: %s lacks one of the API's functions\n", XESS_LIBRARY);
        l.execute = nullptr;
        return 0;
    }
    xess_version_t v = {};
    if (l.version(&v) == XESS_RESULT_SUCCESS)
        logf_(1, "arceus-upscaler: xess: %s is version %u.%u.%u\n", XESS_LIBRARY, v.major, v.minor, v.patch);
    return 1;
}

/* XeSS is called only from the thread that made its first context (R3): any other is refused with one line */
thread_local int t_owner;
int g_owner_set;
int on_owner(const char* what) {
    if (!g_owner_set) {
        g_owner_set = 1;
        t_owner = 1;
    }
    if (t_owner) return 1;
    static int said;
    if (!said++)
        logf_(0, "arceus-upscaler: xess: its %s was asked from another thread than the one that made its first "
                 "context: refused, as XeSS takes calls from one thread only\n", what);
    return 0;
}

void message(const char* text, xess_logging_level_t level) {
    logf_(level >= XESS_LOGGING_LEVEL_ERROR ? 0 : 1, "arceus-upscaler: xess says: %s\n", text ? text : "");
}

/* the library's warnings are positive and leave the call done: said, and taken as success */
int succeeded(xess_result_t r, const char* call) {
    if (r > 0) logf_(1, "arceus-upscaler: xess: %s answered the warning %d\n", call, (int)r);
    return r >= XESS_RESULT_SUCCESS;
}
}  // namespace

struct SnxUpscaler {
    xess_context_handle_t context;
    SnxUpscalerCreate made;
    xess_vk_init_params_t init;
    SnxMode mode;
    const char* mode_name;
    float velocity[2];
};

namespace {
int instance_extensions(SnxUpscalerExtensions* out) {
    out->count = 0;
    out->names = nullptr;
    if (!library()) return SNX_UPSCALER_REFUSED;
    uint32_t n = 0, api = 0;
    const char* const* names = nullptr;
    const xess_result_t r = g_library.instance_names(&n, &names, &api);
    if (!succeeded(r, "its question of the instance's extensions") || (n && !names)) {
        logf_(0, "arceus-upscaler: xess is not available: its question of the instance's extensions failed "
                 "(result %d)\n", (int)r);
        return SNX_UPSCALER_REFUSED;
    }
    static int said;
    if (!said++)
        logf_(1, "arceus-upscaler: xess names %u instance extensions and Vulkan %u.%u as the least version\n", n,
              VK_API_VERSION_MAJOR(api), VK_API_VERSION_MINOR(api));
    out->count = n;
    out->names = names;
    return SNX_UPSCALER_OK;
}

/* A structure's VkBool32 fields from start to its end: the first one set that is not in keep, as a byte offset, or
   -1 for none. keep holds the offsets this adapter maps, -1 for none. */
long other_field(const void* structure, size_t start, size_t size, long keep) {
    for (size_t at = start; at + sizeof(VkBool32) <= size; at += sizeof(VkBool32)) {
        VkBool32 value;
        std::memcpy(&value, (const unsigned char*)structure + at, sizeof value);
        if (value && (long)at != keep) return (long)at;
    }
    return -1;
}
/* handed to whoever makes the device, as a node of its feature chain: it lives as long as the adapter */
VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT g_mutable = {
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT, nullptr, VK_TRUE};

/* Gap 3: XeSS fills a private chain of its own question, read back here. Storage writes without a format, 8 bit
   integers and integer dot products become core bits, the mutable descriptor type goes on as an extension
   structure. Any other field or structure XeSS sets refuses the adapter, named, so nothing is left off silently. */
int device_extensions(VkInstance instance, VkPhysicalDevice device, PFN_vkGetInstanceProcAddr,
                      SnxUpscalerDeviceNeeds* out) {
    out->extensions.count = 0;
    out->extensions.names = nullptr;
    out->features = nullptr;
    out->core = 0;
    if (!library()) return SNX_UPSCALER_REFUSED;
    uint32_t n = 0;
    const char* const* names = nullptr;
    xess_result_t r = g_library.device_names(instance, device, &n, &names);
    if (!succeeded(r, "its question of the device's extensions") || (n && !names)) {
        logf_(0, "arceus-upscaler: xess is not available: its question of the device's extensions failed (result "
                 "%d)\n", (int)r);
        return SNX_UPSCALER_REFUSED;
    }
    VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT mut = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT, nullptr, VK_FALSE};
    VkPhysicalDeviceVulkan13Features v13 = {};
    v13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    v13.pNext = &mut;
    VkPhysicalDeviceVulkan12Features v12 = {};
    v12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    v12.pNext = &v13;
    VkPhysicalDeviceVulkan11Features v11 = {};
    v11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    v11.pNext = &v12;
    VkPhysicalDeviceFeatures2 f2 = {};
    f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    f2.pNext = &v11;
    void* chain = &f2;
    r = g_library.device_features(instance, device, &chain);
    if (!succeeded(r, "its question of the device's features")) {
        logf_(0, "arceus-upscaler: xess is not available: its question of the device's features failed (result "
                 "%d)\n", (int)r);
        return SNX_UPSCALER_REFUSED;
    }
    uint32_t core = 0;
    int mutable_on = 0, nodes = 0;
    for (const VkBaseInStructure* at = (const VkBaseInStructure*)chain; at; at = at->pNext) {
        long other = -1;
        const char* kind = "a structure";
        if (++nodes > 16) {
            logf_(0, "arceus-upscaler: xess is not available: its feature chain does not end within 16 structures\n");
            return SNX_UPSCALER_REFUSED;
        }
        if (at == (const void*)&f2) {
            kind = "the 1.0 features";
            const long keep = (long)offsetof(VkPhysicalDeviceFeatures, shaderStorageImageWriteWithoutFormat);
            if (f2.features.shaderStorageImageWriteWithoutFormat)
                core |= SNX_UPSCALER_CORE_STORAGE_WRITE_WITHOUT_FORMAT;
            other = other_field(&f2.features, 0, sizeof f2.features, keep);
        } else if (at == (const void*)&v11) {
            kind = "the 1.1 features";
            other = other_field(&v11, offsetof(VkPhysicalDeviceVulkan11Features, storageBuffer16BitAccess), sizeof v11,
                                -1);
        } else if (at == (const void*)&v12) {
            kind = "the 1.2 features";
            if (v12.shaderInt8) core |= SNX_UPSCALER_CORE_SHADER_INT8;
            other = other_field(&v12, offsetof(VkPhysicalDeviceVulkan12Features, samplerMirrorClampToEdge), sizeof v12,
                                (long)offsetof(VkPhysicalDeviceVulkan12Features, shaderInt8));
        } else if (at == (const void*)&v13) {
            kind = "the 1.3 features";
            if (v13.shaderIntegerDotProduct) core |= SNX_UPSCALER_CORE_INTEGER_DOT_PRODUCT;
            other = other_field(&v13, offsetof(VkPhysicalDeviceVulkan13Features, robustImageAccess), sizeof v13,
                                (long)offsetof(VkPhysicalDeviceVulkan13Features, shaderIntegerDotProduct));
        } else if (at == (const void*)&mut) {
            mutable_on = mut.mutableDescriptorType != 0;
        } else {
            logf_(0, "arceus-upscaler: xess is not available: its feature chain holds a structure of type %d that "
                     "this adapter does not map\n", (int)at->sType);
            return SNX_UPSCALER_REFUSED;
        }
        if (other >= 0) {
            logf_(0, "arceus-upscaler: xess is not available: it sets the field at byte %ld of %s, which this adapter "
                     "does not map\n", other, kind);
            return SNX_UPSCALER_REFUSED;
        }
    }
    int named = 0;
    for (uint32_t i = 0; i < n; i++) named |= names[i] && !std::strcmp(names[i], XESS_MUTABLE);
    if (mutable_on && !named) {
        logf_(0, "arceus-upscaler: xess is not available: it asks for the mutable descriptor type without naming "
                 XESS_MUTABLE "\n");
        return SNX_UPSCALER_REFUSED;
    }
    static int said;
    if (!said++) {
        char list[600] = "";
        size_t used = 0;
        for (uint32_t i = 0; i < n && used < sizeof list - 80; i++)
            used += (size_t)std::snprintf(list + used, sizeof list - used, "%s%s", i ? ", " : "", names[i]);
        logf_(1, "arceus-upscaler: xess names %u device extensions (%s), the mutable descriptor type %s, and the core "
                 "features 0x%x (4 storage writes without a format, 8 8 bit integers, 16 integer dot products)\n", n,
              n ? list : "none", mutable_on ? "on" : "off", core);
    }
    out->extensions.count = n;
    out->extensions.names = names;
    out->features = mutable_on ? &g_mutable : nullptr;
    out->core = core;
    return SNX_UPSCALER_OK;
}

const struct {
    xess_quality_settings_t setting;
    const char* name;
} kModes[7] = {{XESS_QUALITY_SETTING_ULTRA_PERFORMANCE, "ultra performance"},
               {XESS_QUALITY_SETTING_PERFORMANCE, "performance"},
               {XESS_QUALITY_SETTING_BALANCED, "balanced"},
               {XESS_QUALITY_SETTING_QUALITY, "quality"},
               {XESS_QUALITY_SETTING_ULTRA_QUALITY, "ultra quality"},
               {XESS_QUALITY_SETTING_ULTRA_QUALITY_PLUS, "ultra quality plus"},
               {XESS_QUALITY_SETTING_AA, "native anti-aliasing"}};

/* A context: XeSS's own, its modes asked and printed at the output size, the mode for the render size by the rule
   of D3-4 (upscaler_modes.h), the velocity scale of these images. No work for the GPU: prepare builds and inits. */
int create(const SnxUpscalerCreate* made, SnxUpscalerTraits* traits, SnxUpscaler** out) {
    *out = nullptr;
    if (!library() || !on_owner("create")) return SNX_UPSCALER_REFUSED;
    const uint32_t known = SNX_UPSCALER_COLOUR_LINEAR_HDR | SNX_UPSCALER_DEPTH_REVERSED |
                           SNX_UPSCALER_VECTORS_UNJITTERED | SNX_UPSCALER_EXPOSURE_AUTOMATIC |
                           SNX_UPSCALER_FAR_PLANE_INFINITE;
    if ((made->flags & ~known) || !(made->flags & SNX_UPSCALER_VECTORS_UNJITTERED) ||
        made->motion_format != VK_FORMAT_R16G16_SFLOAT || !made->sizes.render_w || !made->sizes.render_h ||
        made->sizes.render_w > made->sizes.output_w || made->sizes.render_h > made->sizes.output_h ||
        made->sizes.render_w > made->sizes.image_w || made->sizes.render_h > made->sizes.image_h) {
        logf_(0, "arceus-upscaler: xess: no context: flags 0x%x (unjittered vectors required), vectors' format %d "
                 "(R16G16_SFLOAT required), render %ux%u, images %ux%u, output %ux%u\n", made->flags,
              (int)made->motion_format, made->sizes.render_w, made->sizes.render_h, made->sizes.image_w,
              made->sizes.image_h, made->sizes.output_w, made->sizes.output_h);
        return SNX_UPSCALER_REFUSED;
    }
    SnxUpscaler* u = new (std::nothrow) SnxUpscaler();
    if (!u) return SNX_UPSCALER_FAILED;
    u->made = *made;
    const uint64_t began = plat_ns();
    xess_result_t r = g_library.create(made->instance, made->physical_device, made->device, &u->context);
    if (!succeeded(r, "its context's creation") || !u->context) {
        logf_(0, "arceus-upscaler: xess could not make a context (result %d)\n", (int)r);
        delete u;
        return SNX_UPSCALER_FAILED;
    }
    g_library.logging(u->context, XESS_LOGGING_LEVEL_WARNING, message);
    static int driver_said;
    if (!driver_said++) {
        const xess_result_t d = g_library.driver(u->context);
        logf_(1, "arceus-upscaler: xess: its driver check answered %d%s\n", (int)d,
              d == XESS_RESULT_WARNING_OLD_DRIVER ? " (an old driver, or the validation layer on: no refusal)" : "");
    }
    SnxMode modes[7] = {};
    const xess_2d_t output = {made->sizes.output_w, made->sizes.output_h};
    for (int k = 0; k < 7; k++) {
        xess_2d_t optimal = {}, least = {}, most = {};
        r = g_library.optimal(u->context, &output, kModes[k].setting, &optimal, &least, &most);
        if (r != XESS_RESULT_SUCCESS || !optimal.x || !optimal.y) {
            logf_(1, "arceus-upscaler: xess: %s at %ux%u: not available (result %d)\n", kModes[k].name, output.x,
                  output.y, (int)r);
            continue;
        }
        modes[k] = SnxMode{1, optimal.x, optimal.y, least.x, least.y, most.x, most.y};
        logf_(1, "arceus-upscaler: xess: %s at %ux%u: optimal %ux%u, from %ux%u to %ux%u\n", kModes[k].name,
              output.x, output.y, optimal.x, optimal.y, least.x, least.y, most.x, most.y);
    }
    const int pick =
        snx_mode_choose(modes, 7, 6, made->sizes.render_w, made->sizes.render_h, output.x, output.y);
    if (pick < 0) {
        logf_(0, "arceus-upscaler: xess is not available: no mode takes the render size %ux%u for the output %ux%u\n",
              made->sizes.render_w, made->sizes.render_h, output.x, output.y);
        g_library.destroy(u->context);
        delete u;
        return SNX_UPSCALER_REFUSED;
    }
    u->mode = modes[pick];
    u->mode_name = kModes[pick].name;
    /* our texels times the images' size with a minus are pixels from now to a frame ago, XeSS's direction */
    u->velocity[0] = -(float)made->sizes.image_w;
    u->velocity[1] = -(float)made->sizes.image_h;
    if (!succeeded(g_library.velocity_scale(u->context, u->velocity[0], u->velocity[1]), "its velocity scale") ||
        !succeeded(g_library.jitter_scale(u->context, 1.0f, 1.0f), "its jitter scale")) {
        logf_(0, "arceus-upscaler: xess: no context: its velocity or jitter scale could not be set\n");
        g_library.destroy(u->context);
        delete u;
        return SNX_UPSCALER_FAILED;
    }
    u->init.outputResolution = output;
    u->init.qualitySetting = kModes[pick].setting;
    u->init.initFlags = (made->flags & SNX_UPSCALER_DEPTH_REVERSED ? XESS_INIT_FLAG_INVERTED_DEPTH : 0) |
                        (made->flags & SNX_UPSCALER_EXPOSURE_AUTOMATIC ? XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE : 0) |
                        (made->flags & SNX_UPSCALER_COLOUR_LINEAR_HDR ? 0 : XESS_INIT_FLAG_LDR_INPUT_COLOR);
    traits->submits_itself = 0;
    traits->submits_at_init = 1;
    traits->takes_rectangle = 1;
    traits->rectangle_at_origin = 0; /* a base for each input */
    traits->colour_usage = traits->depth_usage = traits->motion_usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    traits->output_usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    logf_(1, "arceus-upscaler: xess: a context for %s at %ux%u, rendered at %ux%u in images of %ux%u (the mode's "
             "optimal %ux%u, from %ux%u to %ux%u), init flags 0x%x, output format %d, made in %.3f ms\n",
          u->mode_name, output.x, output.y, made->sizes.render_w, made->sizes.render_h, made->sizes.image_w,
          made->sizes.image_h, u->mode.w, u->mode.h, u->mode.min_w, u->mode.min_h, u->mode.max_w, u->mode.max_h,
          u->init.initFlags, (int)made->output_format, (double)(plat_ns() - began) / 1e6);
    *out = u;
    return SNX_UPSCALER_OK;
}

/* Its pipelines built (blocking) and its init, both of which may submit work of their own: the runtime calls this
   only between two idle waits (R1). Nothing is recorded into the command buffer. */
int prepare(SnxUpscaler* u, VkCommandBuffer) {
    if (!on_owner("prepare")) return SNX_UPSCALER_FAILED;
    const uint64_t began = plat_ns();
    const xess_result_t built = g_library.build(u->context, VK_NULL_HANDLE, true, u->init.initFlags);
    const int built_ok = succeeded(built, "its pipeline build");
    const uint64_t between = plat_ns();
    const xess_result_t inited = built_ok ? g_library.init(u->context, &u->init) : XESS_RESULT_ERROR_UNKNOWN;
    const uint64_t done = plat_ns();
    const int ok = built_ok && succeeded(inited, "its init");
    logf_(ok ? 1 : 0, "arceus-upscaler: xess: pipelines built in %.3f ms (result %d), init in %.3f ms (result %d)%s\n",
          (double)(between - began) / 1e6, (int)built, (double)(done - between) / 1e6, (int)inited,
          ok ? "" : ": the context is not used");
    return ok ? SNX_UPSCALER_OK : SNX_UPSCALER_FAILED;
}

xess_vk_image_view_info view(const SnxUpscalerImage& image, VkImageAspectFlags aspect) {
    xess_vk_image_view_info v = {};
    v.imageView = image.view;
    v.image = image.image;
    v.subresourceRange = VkImageSubresourceRange{aspect, 0, 1, 0, 1};
    v.format = image.format;
    v.width = image.w;
    v.height = image.h;
    return v;
}

int evaluate(SnxUpscaler* u, const SnxUpscalerFrame* f) {
    if (!on_owner("evaluate")) return SNX_UPSCALER_FAILED;
    /* inputs readable by shaders, the output in the general layout, the vectors' scale the context's, the render
       size inside the chosen mode's range: anything else is refused before XeSS is called */
    if (f->colour.layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
        f->depth.layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
        f->motion.layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL || f->output.layout != VK_IMAGE_LAYOUT_GENERAL ||
        f->vector_scale[0] != u->velocity[0] || f->vector_scale[1] != u->velocity[1] || f->render_x < 0 ||
        f->render_y < 0 || !snx_mode_holds(&u->mode, f->render_w, f->render_h) ||
        f->output.format != u->made.output_format) {
        logf_(0, "arceus-upscaler: xess: a frame not evaluated: a layout, the vectors' scale %.9g %.9g (the "
                 "context's %.9g %.9g), the rectangle %ux%u at %d,%d or the output's format is not the context's\n",
              (double)f->vector_scale[0], (double)f->vector_scale[1], (double)u->velocity[0], (double)u->velocity[1],
              f->render_w, f->render_h, f->render_x, f->render_y);
        return SNX_UPSCALER_FAILED;
    }
    xess_vk_execute_params_t e = {};
    e.colorTexture = view(f->colour, VK_IMAGE_ASPECT_COLOR_BIT);
    e.velocityTexture = view(f->motion, VK_IMAGE_ASPECT_COLOR_BIT);
    e.depthTexture = view(f->depth, VK_IMAGE_ASPECT_DEPTH_BIT);
    e.outputTexture = view(f->output, VK_IMAGE_ASPECT_COLOR_BIT);
    e.jitterOffsetX = f->jitter[0];
    e.jitterOffsetY = f->jitter[1];
    e.exposureScale = 1.0f;
    e.resetHistory = f->reset ? 1u : 0u;
    e.inputWidth = f->render_w;
    e.inputHeight = f->render_h;
    e.inputColorBase = xess_coord_t{(uint32_t)f->render_x, (uint32_t)f->render_y};
    e.inputMotionVectorBase = e.inputColorBase;
    e.inputDepthBase = e.inputColorBase;
    if (f->trace)
        logf_(1, "arceus-upscaler: xess dispatch: jitter %.9g %.9g, input %ux%u at %u,%u, reset %u, exposure scale "
                 "%.9g, velocity scale %.9g %.9g, %s, init flags 0x%x. Images as width x height, format: colour %ux%u "
                 "%d, depth %ux%u %d, vectors %ux%u %d, output %ux%u %d\n",
              (double)e.jitterOffsetX, (double)e.jitterOffsetY, e.inputWidth, e.inputHeight, e.inputColorBase.x,
              e.inputColorBase.y, e.resetHistory, (double)e.exposureScale, (double)u->velocity[0],
              (double)u->velocity[1], u->mode_name, u->init.initFlags, f->colour.w, f->colour.h, (int)f->colour.format,
              f->depth.w, f->depth.h, (int)f->depth.format, f->motion.w, f->motion.h, (int)f->motion.format,
              f->output.w, f->output.h, (int)f->output.format);
    const xess_result_t r = g_library.execute(u->context, f->cb, &e);
    if (succeeded(r, "its execute")) return SNX_UPSCALER_OK;
    logf_(0, "arceus-upscaler: xess: its execute failed (result %d)\n", (int)r);
    return SNX_UPSCALER_FAILED;
}

void destroy(SnxUpscaler* u) {
    if (!u) return;
    /* from another thread nothing of XeSS is called: the context is left to the process's end (R3) */
    if (!on_owner("destroy")) return;
    const xess_result_t r = g_library.destroy(u->context);
    if (r != XESS_RESULT_SUCCESS) logf_(0, "arceus-upscaler: xess could not destroy its context (result %d)\n", (int)r);
    delete u;
}

SnxUpscalerApi g_api = {SNX_UPSCALER_ABI, "xess", instance_extensions, device_extensions, create, prepare,
                        evaluate, destroy, nullptr, VK_FORMAT_UNDEFINED};
}  // namespace

/* The adapter, NULL with one line when the SDK's library is not beside the program. Its output's format is fixed
   here, before any create (R2): the interface's four floats, or B10G11R11 by the self-test's override
   SNX_ARCEUS_XESS_OUTPUT=b10g11r11. */
const SnxUpscalerApi* snx_upscaler_xess(void) {
    if (!library()) return nullptr;
    static int fixed;
    if (!fixed++) {
        const char* output = std::getenv("SNX_ARCEUS_XESS_OUTPUT");
        g_api.output_format = output && !std::strcmp(output, "b10g11r11") ? VK_FORMAT_B10G11R11_UFLOAT_PACK32
                                                                           : VK_FORMAT_UNDEFINED;
        logf_(1, "arceus-upscaler: xess: its output is declared as %s\n",
              g_api.output_format == VK_FORMAT_UNDEFINED ? "four 32 bit floats" : "B10G11R11 (the test override)");
    }
    return &g_api;
}
