/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* SNX_ARCEUS_UPSCALER_SELFTEST=<adapter>: the runtime makes an instance and a device of its own, without a window,
   drives one upscaler adapter (upscaler.h) with a generated scene whose true picture is known, writes its lines
   and ends. No game is loaded. The scene, the reference and the verdicts are upscaler_selftest.h, which shares
   nothing with the conversion (temporal_math.h) and the adapter that are under test here. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nx/gpu/renderer.h"
#include "nx/gpu/vk/vk.h"
#include "nx/gpu/vk/upscaler.h"
#include "nx/gpu/vk/upscaler_selftest.h"
#include "nx/gpu/vk/temporal_math.h"
#include "nx/runtime/platform/log.h"

#define ST "arceus-upscaler-selftest: "
#define RENDER_W 320
#define RENDER_H 180
#define OUT_W 480
#define OUT_H 270
#define OFFSETS 18              /* ceil(8 x 1.5 x 1.5): the jitter's offsets for this ratio */
#define FRAMES (2 * OFFSETS)    /* evaluated frames of a run: twice the offsets */
#define SAMPLES 8               /* the reference averages 8 x 8 points of a pixel */
#define NEAR_PLANE 0.1          /* the scene's depth is 0.1 / distance: an infinite far plane, reversed */
#define FOV_Y 0.8726646259971648 /* 50 degrees */
#define FRAME_SECONDS (1.0 / 60.0)

typedef struct {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    VkFormat format;
    u32 w, h;
} Picture;

static struct {
    VkInstance instance;
    VkPhysicalDevice physical;
    VkDevice device;
    VkQueue queue;
    u32 family;
    VkCommandPool pool;
    VkCommandBuffer cb;
    VkDebugUtilsMessengerEXT messenger;
    VkBuffer staging;
    VkDeviceMemory staging_memory;
    u8* staging_bytes;
    u64 staging_size;
    const SnxUpscalerApi* api;
    int validation;
    unsigned messages;
    u64 generation; /* of the configuration: raised for every context made with other flags or formats */
} g;

/* one run's inputs, made once and never changed: a sign variant changes only the numbers handed to the adapter */
typedef struct {
    int w, h;            /* the images' size */
    SelftestViewport vp; /* the scene's viewport inside them */
    int frames;          /* with the first, which the gate does not let through */
    SelftestFrame* scene;
    u32 *colour, *depth;
    u16* motion;
    int cut_at;          /* the frame whose pattern is new, -1 for none */
} Inputs;

/* the signs a variant turns, and the depth convention it claims */
typedef struct {
    int jitter_x, jitter_y, vector_x, vector_y, depth_reversed;
} Variant;
static const Variant CONFIGURED = {1, 1, 1, 1, 1};

/* a run's state across frames, as the runtime will keep it */
typedef struct {
    TemporalRectGate gate;
    TemporalUpscalerContexts contexts;
    SnxUpscaler *live, *pending;
    unsigned frame, failures, skipped, evaluated;
    Picture colour, depth, motion, output; /* what the adapter is handed */
    Picture wide_colour, wide_depth, wide_motion; /* for inputs that sit inside larger images */
} Session;

static VKAPI_ATTR VkBool32 VKAPI_CALL validation_says(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                      VkDebugUtilsMessageTypeFlagsEXT type,
                                                      const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) {
    (void)type;
    (void)user;
    if (severity < VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) return VK_FALSE;
    if (g.messages++ < 16) LOG(ST "validation: %.300s\n", data && data->pMessage ? data->pMessage : "");
    return VK_FALSE;
}

static int memory_type(u32 bits, VkMemoryPropertyFlags want) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(g.physical, &mp);
    for (u32 i = 0; i < mp.memoryTypeCount; i++)
        if ((bits >> i & 1) && (mp.memoryTypes[i].propertyFlags & want) == want) return (int)i;
    return -1;
}

/* The instance and the device, through the steps the runtime will take with an adapter (upscaler_host.c). The
   validation layer with its synchronisation checks is asked for when it is installed. */
static int device_make(void) {
    const char *instance_names[32], *device_names[64], *layers[1] = {"VK_LAYER_KHRONOS_validation"};
    u32 instance_count = 0, device_count = 0, n = 0;
    const void* features = NULL;
    if (!VK_OK(volkInitialize())) {
        LOG(ST "FAIL: no Vulkan loader\n");
        return 0;
    }
    vkEnumerateInstanceLayerProperties(&n, NULL);
    VkLayerProperties* have = calloc(n ? n : 1, sizeof *have);
    if (have) vkEnumerateInstanceLayerProperties(&n, have);
    for (u32 i = 0; have && i < n; i++) g.validation |= !strcmp(have[i].layerName, layers[0]);
    free(have);
    if (g.validation) instance_names[instance_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    if (!vk_upscaler_instance_extensions(g.api, instance_names, &instance_count, 32)) {
        LOG(ST "FAIL: the adapter's instance extensions could not be taken\n");
        return 0;
    }
    const VkValidationFeatureEnableEXT enables[1] = {VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT};
    const VkValidationFeaturesEXT checks = {.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
                                            .enabledValidationFeatureCount = 1,
                                            .pEnabledValidationFeatures = enables};
    const VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                                   .pApplicationName = "scarletnx upscaler self-test",
                                   .apiVersion = VK_API_VERSION_1_2};
    const VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                      .pNext = g.validation ? &checks : NULL,
                                      .pApplicationInfo = &app,
                                      .enabledLayerCount = g.validation ? 1 : 0,
                                      .ppEnabledLayerNames = layers,
                                      .enabledExtensionCount = instance_count,
                                      .ppEnabledExtensionNames = instance_names};
    if (!VK_OK(vkCreateInstance(&ici, NULL, &g.instance))) {
        LOG(ST "FAIL: the instance could not be made with %u extensions\n", instance_count);
        return 0;
    }
    volkLoadInstance(g.instance);
    if (g.validation) {
        const VkDebugUtilsMessengerCreateInfoEXT mi = {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT,
            .pfnUserCallback = validation_says};
        if (!VK_OK(vkCreateDebugUtilsMessengerEXT(g.instance, &mi, NULL, &g.messenger))) g.validation = 0;
    }
    /* the first device with a queue family for graphics and compute */
    VkPhysicalDevice devices[8];
    n = 8;
    vkEnumeratePhysicalDevices(g.instance, &n, devices);
    for (u32 d = 0; d < n && !g.physical; d++) {
        VkQueueFamilyProperties families[16];
        u32 count = 16;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[d], &count, families);
        for (u32 f = 0; f < count && !g.physical; f++)
            if ((families[f].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
                (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) {
                g.physical = devices[d];
                g.family = f;
            }
    }
    if (!g.physical) {
        LOG(ST "FAIL: no device with a graphics and compute queue\n");
        return 0;
    }
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(g.physical, &props);
    if (!vk_upscaler_device_needs(g.api, g.instance, g.physical, vkGetInstanceProcAddr, device_names, &device_count,
                                  64, &features)) {
        LOG(ST "FAIL: the adapter's device extensions could not be taken\n");
        return 0;
    }
    /* every feature the device has is turned on, and the adapter's own chain, if it has one, goes behind */
    VkPhysicalDeviceVulkan12Features f12 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
                                            .pNext = (void*)features};
    VkPhysicalDeviceVulkan11Features f11 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
                                            .pNext = &f12};
    VkPhysicalDeviceFeatures2 f2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &f11};
    f12.pNext = NULL;
    vkGetPhysicalDeviceFeatures2(g.physical, &f2);
    f12.pNext = (void*)features;
    const float priority = 1.0f;
    const VkDeviceQueueCreateInfo qi = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                        .queueFamilyIndex = g.family,
                                        .queueCount = 1,
                                        .pQueuePriorities = &priority};
    const VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                                    .pNext = &f2,
                                    .queueCreateInfoCount = 1,
                                    .pQueueCreateInfos = &qi,
                                    .enabledExtensionCount = device_count,
                                    .ppEnabledExtensionNames = device_names};
    if (!VK_OK(vkCreateDevice(g.physical, &dci, NULL, &g.device))) {
        LOG(ST "FAIL: the device could not be made with %u extensions of the adapter\n", device_count);
        return 0;
    }
    volkLoadDevice(g.device);
    vkGetDeviceQueue(g.device, g.family, 0, &g.queue);
    const VkCommandPoolCreateInfo pi = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                                        .queueFamilyIndex = g.family};
    const VkCommandBufferAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                            .commandBufferCount = 1};
    VkCommandBufferAllocateInfo alloc = ai;
    if (!VK_OK(vkCreateCommandPool(g.device, &pi, NULL, &g.pool))) return 0;
    alloc.commandPool = g.pool;
    if (!VK_OK(vkAllocateCommandBuffers(g.device, &alloc, &g.cb))) return 0;
    /* one buffer the host can write and read, for the uploads and for reading the output back */
    g.staging_size = 8ull << 20;
    const VkBufferCreateInfo bi = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                   .size = g.staging_size,
                                   .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    VkMemoryRequirements need;
    if (!VK_OK(vkCreateBuffer(g.device, &bi, NULL, &g.staging))) return 0;
    vkGetBufferMemoryRequirements(g.device, g.staging, &need);
    const int type = memory_type(need.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    const VkMemoryAllocateInfo mi = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                     .allocationSize = need.size,
                                     .memoryTypeIndex = (u32)(type < 0 ? 0 : type)};
    void* mapped = NULL;
    if (type < 0 || !VK_OK(vkAllocateMemory(g.device, &mi, NULL, &g.staging_memory)) ||
        !VK_OK(vkBindBufferMemory(g.device, g.staging, g.staging_memory, 0)) ||
        !VK_OK(vkMapMemory(g.device, g.staging_memory, 0, VK_WHOLE_SIZE, 0, &mapped)))
        return 0;
    g.staging_bytes = mapped;
    LOG(ST "device %s, adapter %s, validation layer %s\n", props.deviceName, g.api->name,
        g.validation ? "on, with synchronisation checks" : "NOT INSTALLED: no validation message can be seen");
    return 1;
}

static int picture_make(Picture* p, VkFormat format, u32 w, u32 h, VkImageUsageFlags usage) {
    const int is_depth = format == VK_FORMAT_D24_UNORM_S8_UINT;
    const VkImageCreateInfo ii = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                                  .imageType = VK_IMAGE_TYPE_2D,
                                  .format = format,
                                  .extent = {w, h, 1},
                                  .mipLevels = 1,
                                  .arrayLayers = 1,
                                  .samples = VK_SAMPLE_COUNT_1_BIT,
                                  .usage = usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT};
    VkMemoryRequirements need;
    memset(p, 0, sizeof *p);
    if (!VK_OK(vkCreateImage(g.device, &ii, NULL, &p->image))) return 0;
    vkGetImageMemoryRequirements(g.device, p->image, &need);
    const int type = memory_type(need.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    const VkMemoryAllocateInfo mi = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                     .allocationSize = need.size,
                                     .memoryTypeIndex = (u32)(type < 0 ? 0 : type)};
    if (type < 0 || !VK_OK(vkAllocateMemory(g.device, &mi, NULL, &p->memory)) ||
        !VK_OK(vkBindImageMemory(g.device, p->image, p->memory, 0)))
        return 0;
    const VkImageViewCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = p->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange = {is_depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    p->format = format;
    p->w = w;
    p->h = h;
    return VK_OK(vkCreateImageView(g.device, &vi, NULL, &p->view));
}

static void picture_free(Picture* p) {
    if (p->view) vkDestroyImageView(g.device, p->view, NULL);
    if (p->image) vkDestroyImage(g.device, p->image, NULL);
    if (p->memory) vkFreeMemory(g.device, p->memory, NULL);
    memset(p, 0, sizeof *p);
}

static VkImageAspectFlags aspects(const Picture* p) {
    return p->format == VK_FORMAT_D24_UNORM_S8_UINT ? VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT
                                                    : VK_IMAGE_ASPECT_COLOR_BIT;
}

/* a layout change with every command before it finished and every command after it waiting: nothing of what an
   adapter did last is relied on */
static void layout(const Picture* p, VkImageLayout from, VkImageLayout to) {
    const VkImageMemoryBarrier b = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                    .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT,
                                    .dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT,
                                    .oldLayout = from,
                                    .newLayout = to,
                                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                    .image = p->image,
                                    .subresourceRange = {aspects(p), 0, 1, 0, 1}};
    vkCmdPipelineBarrier(g.cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0,
                         NULL, 1, &b);
}

/* everything recorded so far is done before anything recorded after this starts */
static void settle(void) {
    const VkMemoryBarrier b = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                               .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT,
                               .dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT};
    vkCmdPipelineBarrier(g.cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &b, 0,
                         NULL, 0, NULL);
}

static int begin(void) {
    const VkCommandBufferBeginInfo bi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                         .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    return VK_OK(vkResetCommandBuffer(g.cb, 0)) && VK_OK(vkBeginCommandBuffer(g.cb, &bi));
}

/* the command buffer is submitted and has completed when this returns 1 */
static int submit(void) {
    const VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &g.cb};
    return VK_OK(vkEndCommandBuffer(g.cb)) && VK_OK(vkQueueSubmit(g.queue, 1, &si, VK_NULL_HANDLE)) &&
           VK_OK(vkQueueWaitIdle(g.queue));
}

/* bytes from the staging buffer at offset into a picture, which is then readable by shaders or by a copy */
static void upload(const Picture* p, u64 offset, VkImageLayout then) {
    const VkBufferImageCopy region = {
        .bufferOffset = offset,
        .imageSubresource = {p->format == VK_FORMAT_D24_UNORM_S8_UINT ? VK_IMAGE_ASPECT_DEPTH_BIT
                                                                        : VK_IMAGE_ASPECT_COLOR_BIT,
                             0, 0, 1},
        .imageExtent = {p->w, p->h, 1}};
    layout(p, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vkCmdCopyBufferToImage(g.cb, g.staging, p->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    layout(p, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, then);
}

/* the rectangle at x, y of a larger picture into a picture of its own size: the copy the runtime makes for an
   adapter that reads from the first texel only */
static void crop(const Picture* from, const Picture* to, int x, int y) {
    const VkImageAspectFlags aspect =
        to->format == VK_FORMAT_D24_UNORM_S8_UINT ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    const VkImageCopy region = {.srcSubresource = {aspect, 0, 0, 1},
                                .srcOffset = {x, y, 0},
                                .dstSubresource = {aspect, 0, 0, 1},
                                .extent = {to->w, to->h, 1}};
    layout(to, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vkCmdCopyImage(g.cb, from->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, to->image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    layout(to, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

/* The jitter's offset k in pixels, within half a pixel each way: Halton bases 2 and 3. The test's own, so that the
   perturbations are inputs it made itself. */
static void offset(unsigned k, double out[2]) {
    static const unsigned bases[2] = {2, 3};
    for (int axis = 0; axis < 2; axis++) {
        double f = 1.0, value = 0.0;
        for (unsigned i = k % OFFSETS + 1; i; i /= bases[axis]) {
            f /= bases[axis];
            value += f * (i % bases[axis]);
        }
        out[axis] = value - 0.5;
    }
}

/* The inputs of a run. The scene's rectangle is RENDER_W by RENDER_H at x, y inside images of w by h, whose rest
   holds a colour, a depth and a vector that are none of the scene's. move: the background's move a frame in render
   pixels (x right, y up). patch: 1 for a nearer patch that moves another way. cut_at: the frame from which the
   other pattern is shown. up: 1 for a viewport with a negative height. */
static int inputs_make(Inputs* in, int w, int h, int x, int y, int up, const double move[2], int patch, int cut_at) {
    memset(in, 0, sizeof *in);
    in->w = w;
    in->h = h;
    in->vp = (SelftestViewport){x, up ? y + RENDER_H : y, RENDER_W, up ? -RENDER_H : RENDER_H};
    in->frames = FRAMES + 1;
    in->cut_at = cut_at;
    in->scene = calloc((size_t)in->frames, sizeof *in->scene);
    in->colour = malloc((size_t)in->frames * w * h * 4);
    in->depth = malloc((size_t)in->frames * w * h * 4);
    in->motion = malloc((size_t)in->frames * w * h * 8);
    if (!in->scene || !in->colour || !in->depth || !in->motion) return 0;
    const double step[2] = {2.0 * move[0] / RENDER_W, 2.0 * move[1] / RENDER_H};
    const double patch_step[2] = {2.0 * -0.9 / RENDER_W, 2.0 * 1.1 / RENDER_H};
    const double padding[3] = {0.9, 0.0, 0.9};
    for (int i = 0; i < in->frames; i++) {
        SelftestFrame* f = &in->scene[i];
        double px[2];
        offset((unsigned)i, px);
        f->pattern = cut_at >= 0 && i >= cut_at;
        f->jitter[0] = 2.0 * px[0] / RENDER_W;
        f->jitter[1] = 2.0 * px[1] / RENDER_H;
        for (int a = 0; a < 2; a++) {
            f->shift[a] = step[a] * i;
            f->shift_before[a] = step[a] * (i - 1);
        }
        f->has_patch = patch;
        f->patch[0] = 0.15 + patch_step[0] * i;
        f->patch[1] = -0.55 + patch_step[1] * i;
        f->patch[2] = f->patch[0] + 0.5;
        f->patch[3] = f->patch[1] + 0.6;
        for (int a = 0; a < 4; a++) f->patch_before[a] = f->patch[a] - patch_step[a & 1];
        u32* colour = in->colour + (size_t)i * w * h;
        u32* depth = in->depth + (size_t)i * w * h;
        u16* motion = in->motion + (size_t)i * w * h * 4;
        for (int py = 0; py < h; py++)
            for (int qx = 0; qx < w; qx++) {
                double rgb[3], z, texel[2];
                const size_t at = (size_t)py * w + qx;
                if (qx >= x && qx < x + RENDER_W && py >= y && py < y + RENDER_H) {
                    selftest_input(f, &in->vp, w, h, qx, py, rgb, &z, texel);
                } else {
                    memcpy(rgb, padding, sizeof rgb);
                    z = 1.0;
                    texel[0] = texel[1] = 0.5;
                }
                colour[at] = selftest_b10g11r11(rgb);
                depth[at] = selftest_d24(z);
                motion[4 * at] = selftest_half(texel[0]);
                motion[4 * at + 1] = selftest_half(texel[1]);
                motion[4 * at + 2] = 0;
                motion[4 * at + 3] = selftest_half(z == SELFTEST_NEAR ? 1.0 : 0.0);
            }
    }
    return 1;
}

static void inputs_free(Inputs* in) {
    free(in->scene);
    free(in->colour);
    free(in->depth);
    free(in->motion);
    memset(in, 0, sizeof *in);
}

static int session_open(Session* s, const Inputs* in, const SnxUpscalerTraits* traits) {
    const int wide = in->w != RENDER_W || in->h != RENDER_H;
    memset(s, 0, sizeof *s);
    if (!picture_make(&s->colour, VK_FORMAT_B10G11R11_UFLOAT_PACK32, RENDER_W, RENDER_H, traits->colour_usage) ||
        !picture_make(&s->depth, VK_FORMAT_D24_UNORM_S8_UINT, RENDER_W, RENDER_H, traits->depth_usage) ||
        !picture_make(&s->motion, VK_FORMAT_R16G16B16A16_SFLOAT, RENDER_W, RENDER_H, traits->motion_usage) ||
        !picture_make(&s->output, VK_FORMAT_R16G16B16A16_SFLOAT, OUT_W, OUT_H, traits->output_usage))
        return 0;
    if (!wide) return 1;
    return picture_make(&s->wide_colour, VK_FORMAT_B10G11R11_UFLOAT_PACK32, (u32)in->w, (u32)in->h, 0) &&
           picture_make(&s->wide_depth, VK_FORMAT_D24_UNORM_S8_UINT, (u32)in->w, (u32)in->h, 0) &&
           picture_make(&s->wide_motion, VK_FORMAT_R16G16B16A16_SFLOAT, (u32)in->w, (u32)in->h, 0);
}

static void session_close(Session* s) {
    vkDeviceWaitIdle(g.device);
    if (s->live) g.api->destroy(s->live);
    if (s->pending) g.api->destroy(s->pending);
    picture_free(&s->colour);
    picture_free(&s->depth);
    picture_free(&s->motion);
    picture_free(&s->output);
    picture_free(&s->wide_colour);
    picture_free(&s->wide_depth);
    picture_free(&s->wide_motion);
    memset(s, 0, sizeof *s);
}

/* The context for a configuration, by the states the runtime will keep (temporal_upscaler_*): created, prepared into
   a command buffer that is submitted and has completed, and only then published. 1 when the frame may be
   evaluated on s->live. */
static int context_ready(Session* s, const TemporalUpscalerConfig* config, u32 flags, SnxUpscalerTraits* traits) {
    if (temporal_upscaler_ready(&s->contexts, config)) return 1;
    if (!temporal_upscaler_must_create(&s->contexts, config)) return 0;
    const SnxUpscalerCreate make = {.sizes = {RENDER_W, RENDER_H, RENDER_W, RENDER_H, OUT_W, OUT_H},
                                    .instance = g.instance,
                                    .physical_device = g.physical,
                                    .device = g.device,
                                    .instance_lookup = vkGetInstanceProcAddr,
                                    .device_lookup = vkGetDeviceProcAddr,
                                    .queue = g.queue,
                                    .queue_family = g.family,
                                    .flags = flags,
                                    .colour_format = VK_FORMAT_B10G11R11_UFLOAT_PACK32,
                                    .depth_format = VK_FORMAT_D24_UNORM_S8_UINT,
                                    .motion_format = VK_FORMAT_R16G16B16A16_SFLOAT,
                                    .output_format = VK_FORMAT_R16G16B16A16_SFLOAT,
                                    .folder = "."};
    /* An adapter that submits on its own is refused, and so is one that needs a usage the pictures were not made
       with: inputs sampled, the output sampled and storage, all of them copy sources and destinations. */
    const VkImageUsageFlags copies = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    const VkImageUsageFlags inputs = VK_IMAGE_USAGE_SAMPLED_BIT | copies, output = inputs | VK_IMAGE_USAGE_STORAGE_BIT;
    int made = g.api->create(&make, traits, &s->pending) == SNX_UPSCALER_OK && s->pending && !traits->submits_itself;
    if (made && ((traits->colour_usage | traits->depth_usage | traits->motion_usage) & ~inputs ||
                 traits->output_usage & ~output)) {
        LOG(ST "the adapter needs image usages the self-test's pictures do not have (%x %x %x %x)\n",
            traits->colour_usage, traits->depth_usage, traits->motion_usage, traits->output_usage);
        made = 0;
    }
    const int begun = made && begin();
    int ok = begun && g.api->prepare(s->pending, g.cb) == SNX_UPSCALER_OK;
    /* the preparation's commands are submitted, and it has completed when submit returns */
    if (begun && !submit()) ok = 0;
    temporal_upscaler_created(&s->contexts, config, ok);
    switch (temporal_upscaler_completed(&s->contexts, config)) {
    case TEMPORAL_UPSCALER_RETIRE:
        vkDeviceWaitIdle(g.device);
        g.api->destroy(s->live);
        /* fall through */
    case TEMPORAL_UPSCALER_PUBLISHED:
        s->live = s->pending;
        s->pending = NULL;
        break;
    default: /* a failed or stale one is freed now that its command buffer is done */
        if (s->pending) g.api->destroy(s->pending);
        s->pending = NULL;
        break;
    }
    return ok && temporal_upscaler_ready(&s->contexts, config);
}

/* One frame of a run through the gate, the conversion, the check and the adapter. out gets the output picture as
   three doubles a pixel when the frame was evaluated. 1: evaluated. 0: skipped, as a gated frame or one whose
   context was not ready is. -1: a failure. */
static int frame_run(Session* s, const Inputs* in, int i, const Variant* v, int cut, double* out) {
    const size_t pixels = (size_t)in->w * in->h;
    const int wide = in->w != RENDER_W || in->h != RENDER_H;
    const int x = (int)in->vp.x, y = (int)(in->vp.h < 0 ? in->vp.y + in->vp.h : in->vp.y);
    SnxUpscalerTraits traits = {0};
    TemporalRectState state = {0};
    TemporalUpscalerFrame numbers;
    float ms;
    s->frame++;
    /* what the renderer would know of the frame: its viewport, the images' size, the fraction of them in use */
    state.viewport = (TemporalViewport){(float)in->vp.x, (float)in->vp.y, (float)in->vp.w, (float)in->vp.h};
    state.target_w = in->w;
    state.target_h = in->h;
    state.fx = (float)RENDER_W / (float)in->w;
    state.fy = (float)RENDER_H / (float)in->h;
    if (!temporal_rect_from_viewport(&state.viewport, in->w, in->h, &state.rect)) return -1;
    const TemporalRectStamp stamp = {1, g.generation, in->w, in->h, OUT_W, OUT_H, state.rect};
    const double jitter[2] = {in->scene[i].jitter[0], in->scene[i].jitter[1]};
    temporal_rect_decide(&s->gate, s->frame, &state);
    if (!temporal_upscaler_frame(&s->gate, s->frame, &stamp, &state.viewport, jitter, cut, &numbers) ||
        !temporal_upscaler_camera(NEAR_PLANE, (double)INFINITY, FOV_Y, FRAME_SECONDS, 1, &ms)) {
        s->skipped++;
        return 0;
    }
    const u32 flags = SNX_UPSCALER_COLOUR_LINEAR_HDR | SNX_UPSCALER_VECTORS_UNJITTERED |
                      SNX_UPSCALER_EXPOSURE_AUTOMATIC | SNX_UPSCALER_FAR_PLANE_INFINITE |
                      (v->depth_reversed ? SNX_UPSCALER_DEPTH_REVERSED : 0);
    const TemporalUpscalerConfig config = {in->w, in->h, RENDER_W, RENDER_H, OUT_W, OUT_H,
                                           g.generation * 2 + (u64)v->depth_reversed};
    if (!context_ready(s, &config, flags, &traits)) {
        s->skipped++;
        return s->contexts.pending || s->live ? 0 : -1;
    }
    /* the frame's three images, frozen, into the pictures the adapter reads */
    memcpy(g.staging_bytes, in->colour + (size_t)i * pixels, pixels * 4);
    memcpy(g.staging_bytes + pixels * 4, in->depth + (size_t)i * pixels, pixels * 4);
    memcpy(g.staging_bytes + pixels * 8, in->motion + (size_t)i * pixels * 4, pixels * 8);
    if (!begin()) return -1;
    if (wide) {
        upload(&s->wide_colour, 0, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        upload(&s->wide_depth, pixels * 4, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        upload(&s->wide_motion, pixels * 8, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        crop(&s->wide_colour, &s->colour, x, y);
        crop(&s->wide_depth, &s->depth, x, y);
        crop(&s->wide_motion, &s->motion, x, y);
    } else {
        upload(&s->colour, 0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        upload(&s->depth, pixels * 4, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        upload(&s->motion, pixels * 8, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    layout(&s->output, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    const SnxUpscalerFrame frame = {
        .cb = g.cb,
        .colour = {s->colour.image, s->colour.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, s->colour.format,
                   RENDER_W, RENDER_H},
        .depth = {s->depth.image, s->depth.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, s->depth.format, RENDER_W,
                  RENDER_H},
        .motion = {s->motion.image, s->motion.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, s->motion.format,
                   RENDER_W, RENDER_H},
        .output = {s->output.image, s->output.view, VK_IMAGE_LAYOUT_GENERAL, s->output.format, OUT_W, OUT_H},
        .render_x = 0,
        .render_y = 0,
        .render_w = RENDER_W,
        .render_h = RENDER_H,
        /* the conversion's numbers, with the variant's signs: the configured variant hands them on as they are */
        .jitter = {(float)(numbers.jitter[0] * v->jitter_x), (float)(numbers.jitter[1] * v->jitter_y)},
        .vector_scale = {(float)(numbers.vector_scale[0] * v->vector_x),
                         (float)(numbers.vector_scale[1] * v->vector_y)},
        .reset = (u32)numbers.reset,
        .frame_seconds = ms / 1000.0f,
        .near_plane = (float)NEAR_PLANE,
        .far_plane = INFINITY,
        .fov_y = (float)FOV_Y};
    const int evaluated = g.api->evaluate(s->live, &frame) == SNX_UPSCALER_OK;
    /* whatever the adapter left bound or pending: the output is taken over for the copy with a full barrier */
    const VkBufferImageCopy region = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                      .imageExtent = {OUT_W, OUT_H, 1}};
    layout(&s->output, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    settle(); /* the staging buffer was read by the uploads and is written now */
    vkCmdCopyImageToBuffer(g.cb, s->output.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g.staging, 1, &region);
    /* the copy's writes are made available to the host, which reads the buffer when the submission has completed */
    const VkBufferMemoryBarrier read = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                                        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                        .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
                                        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                        .buffer = g.staging,
                                        .size = VK_WHOLE_SIZE};
    vkCmdPipelineBarrier(g.cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1, &read, 0,
                         NULL);
    const int done = submit();
    if (temporal_upscaler_failed(&s->failures, evaluated && done)) LOG(ST "the adapter failed eight times in a row\n");
    if (!evaluated || !done) return -1;
    temporal_rect_wrote(&s->gate, s->frame, &stamp);
    s->evaluated++;
    const u16* words = (const u16*)g.staging_bytes;
    for (size_t p = 0; p < (size_t)OUT_W * OUT_H; p++)
        for (int c = 0; c < 3; c++) {
            out[3 * p + c] = selftest_half_value(words[4 * p + c]);
            if (!isfinite(out[3 * p + c])) return -1;
        }
    return 1;
}

/* the mean absolute difference of two pictures over the pixels of one region, per channel of a range of 1. -1 when
   the region has no pixel. */
static double difference(const double* a, const double* b, const u8* regions, int region) {
    double sum = 0.0;
    size_t n = 0;
    for (size_t p = 0; p < (size_t)OUT_W * OUT_H; p++) {
        if (regions[p] != region) continue;
        for (int c = 0; c < 3; c++) sum += fabs(a[3 * p + c] - b[3 * p + c]);
        n += 3;
    }
    return n ? sum / (double)n : -1.0;
}

/* the true picture of the run's last frame at the output's size, and each pixel's region */
static void truth(const Inputs* in, int up, double* reference, u8* regions) {
    const SelftestViewport out = {0, up ? OUT_H : 0, OUT_W, up ? -OUT_H : OUT_H};
    const SelftestFrame* f = &in->scene[in->frames - 1];
    for (int y = 0; y < OUT_H; y++)
        for (int x = 0; x < OUT_W; x++) {
            selftest_reference(f, &out, x, y, SAMPLES, &reference[3 * ((size_t)y * OUT_W + x)]);
            regions[(size_t)y * OUT_W + x] = (u8)selftest_region(f, &out, OUT_W, OUT_H, x, y);
        }
}

/* One run of every frame with one variant, on a session of its own. last gets the last output. first_change and
   last_change, when asked for, get the mean change between consecutive outputs over the first and the last cycle
   of offsets. 1 when every frame after the gate's first was evaluated. */
static int run(const Inputs* in, const Variant* v, int use_cut, double* last, double* first_change,
               double* last_change, const u8* regions) {
    Session s;
    SnxUpscalerTraits traits = {.colour_usage = VK_IMAGE_USAGE_SAMPLED_BIT,
                                .depth_usage = VK_IMAGE_USAGE_SAMPLED_BIT,
                                .motion_usage = VK_IMAGE_USAGE_SAMPLED_BIT,
                                .output_usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT};
    double* previous = malloc(sizeof(double) * 3 * OUT_W * OUT_H);
    double sums[2] = {0.0, 0.0};
    int counts[2] = {0, 0};
    int ok = previous && session_open(&s, in, &traits), evaluated = 0;
    for (int i = 0; ok && i < in->frames; i++) {
        const int r = frame_run(&s, in, i, v, use_cut && i == in->cut_at, last);
        if (r < 0) ok = 0;
        if (r <= 0) continue;
        if (evaluated && regions) {
            const double change = difference(last, previous, regions, SELFTEST_BACKGROUND);
            const int cycle = evaluated <= OFFSETS ? 0 : evaluated >= FRAMES - OFFSETS ? 1 : -1;
            if (cycle >= 0) {
                sums[cycle] += change;
                counts[cycle]++;
            }
        }
        memcpy(previous, last, sizeof(double) * 3 * OUT_W * OUT_H);
        evaluated++;
    }
    if (ok && evaluated != in->frames - 1) {
        LOG(ST "a run evaluated %d of %d frames (%u skipped)\n", evaluated, in->frames - 1, s.skipped);
        ok = 0;
    }
    if (first_change) *first_change = counts[0] ? sums[0] / counts[0] : NAN;
    if (last_change) *last_change = counts[1] ? sums[1] / counts[1] : NAN;
    if (previous) session_close(&s);
    free(previous);
    return ok;
}

/* one frame of the inputs scaled up plainly, with two-by-two linear filtering and no knowledge of the jitter: what
   an upscaler has to beat */
static void plain(const Inputs* in, int i, int up, double* out) {
    const SelftestViewport to = {0, up ? OUT_H : 0, OUT_W, up ? -OUT_H : OUT_H};
    const u32* colour = in->colour + (size_t)i * in->w * in->h;
    for (int y = 0; y < OUT_H; y++)
        for (int x = 0; x < OUT_W; x++) {
            double n[2], at[2], sum[3] = {0, 0, 0};
            selftest_ndc(&to, x + 0.5, y + 0.5, n);
            selftest_place(&in->vp, n, at);
            const double fx = at[0] - 0.5, fy = at[1] - 0.5;
            const int x0 = (int)floor(fx), y0 = (int)floor(fy);
            for (int j = 0; j < 2; j++)
                for (int k = 0; k < 2; k++) {
                    const int sx = x0 + k < 0 ? 0 : x0 + k >= in->w ? in->w - 1 : x0 + k;
                    const int sy = y0 + j < 0 ? 0 : y0 + j >= in->h ? in->h - 1 : y0 + j;
                    const double weight = (k ? fx - x0 : 1.0 - (fx - x0)) * (j ? fy - y0 : 1.0 - (fy - y0));
                    double rgb[3];
                    selftest_b10g11r11_value(colour[(size_t)sy * in->w + sx], rgb);
                    for (int c = 0; c < 3; c++) sum[c] += weight * rgb[c];
                }
            memcpy(&out[3 * ((size_t)y * OUT_W + x)], sum, sizeof sum);
        }
}

static const char* verdict_name(int verdict) {
    return verdict == SELFTEST_PASS ? "PASS" : verdict == SELFTEST_BLIND ? "FAIL (the case cannot tell its variants "
                                                                           "apart)"
                                                                         : "FAIL";
}

/* A sign case: the same frozen inputs run once per combination of the signs in mask (bit 0 jitter x, 1 jitter y,
   2 vector x, 3 vector y), judged in one region against the reference. 1 for a pass. */
static int signs(const char* name, const Inputs* in, unsigned mask, int region, const double* reference,
                 const u8* regions, double* output) {
    double errors[16];
    int n = 0, own = -1, ran = 1;
    for (unsigned combo = 0; combo < 16 && ran; combo++) {
        if (combo & ~mask) continue;
        const Variant v = {combo & 1 ? -1 : 1, combo & 2 ? -1 : 1, combo & 4 ? -1 : 1, combo & 8 ? -1 : 1, 1};
        ran = run(in, &v, 0, output, NULL, NULL, NULL);
        errors[n] = ran ? difference(output, reference, regions, region) : NAN;
        LOG(ST "%s: jitter %c%c vector %c%c error %.6f%s\n", name, v.jitter_x < 0 ? '-' : '+',
            v.jitter_y < 0 ? '-' : '+', v.vector_x < 0 ? '-' : '+', v.vector_y < 0 ? '-' : '+', errors[n],
            combo ? "" : " (configured)");
        if (!combo) own = n;
        n++;
    }
    const int verdict = ran ? selftest_verdict(errors, n, own, SELFTEST_MARGIN) : SELFTEST_FAIL;
    LOG(ST "%s: %d variants, margin %.2f: %s\n", name, n, SELFTEST_MARGIN, verdict_name(verdict));
    return verdict == SELFTEST_PASS;
}

/* the whole suite for one sign of the viewport's height: 1 when every case passed */
static int suite(int up) {
    const size_t size = sizeof(double) * 3 * OUT_W * OUT_H;
    const double still[2] = {0.0, 0.0}, pan[2] = {1.3, 0.7};
    double *reference = malloc(size), *output = malloc(size), *other = malloc(size);
    u8* regions = malloc((size_t)OUT_W * OUT_H);
    Inputs in;
    int pass = 1, ok;
    char name[64];
    if (!reference || !output || !other || !regions) return 0;
    LOG(ST "suite with a viewport height of %s: render %dx%d, output %dx%d, %d offsets, %d frames a run\n",
        up ? "minus the image's (y up)" : "plus the image's (y down)", RENDER_W, RENDER_H, OUT_W, OUT_H, OFFSETS,
        FRAMES);

    /* Case 1: jitter alone over a still pattern. The jitter's signs, the plain scale, and settling. */
    ok = inputs_make(&in, RENDER_W, RENDER_H, 0, 0, up, still, 0, -1);
    if (ok) {
        double first = 0, last = 0;
        truth(&in, up, reference, regions);
        snprintf(name, sizeof name, "case 1 %s jitter signs", up ? "up" : "down");
        ok = signs(name, &in, 3, SELFTEST_BACKGROUND, reference, regions, output);
        const int ran = run(&in, &CONFIGURED, 0, output, &first, &last, regions);
        const double error = ran ? difference(output, reference, regions, SELFTEST_BACKGROUND) : NAN;
        plain(&in, in.frames - 1, up, other);
        const double scaled = difference(other, reference, regions, SELFTEST_BACKGROUND);
        const int beats = ran && selftest_beats(error, scaled, SELFTEST_MARGIN);
        const int settles = selftest_settles(first, last);
        LOG(ST "case 1 %s: error %.6f against one frame scaled plainly %.6f: %s\n", up ? "up" : "down", error, scaled,
            beats ? "PASS" : "FAIL");
        LOG(ST "case 1 %s: change a frame over the first cycle %.6f, over the last %.6f: %s\n", up ? "up" : "down",
            first, last, settles ? "PASS" : "FAIL");
        ok = ok && beats && settles;
    }
    pass &= ok;
    inputs_free(&in);

    /* Case 2: a translation that is unequal in both axes. All sixteen combinations of the four signs. */
    ok = inputs_make(&in, RENDER_W, RENDER_H, 0, 0, up, pan, 0, -1);
    if (ok) {
        truth(&in, up, reference, regions);
        snprintf(name, sizeof name, "case 2 %s all signs", up ? "up" : "down");
        ok = signs(name, &in, 15, SELFTEST_BACKGROUND, reference, regions, output);
    }
    pass &= ok;
    inputs_free(&in);

    /* Case 3: a nearer patch that moves on its own over a still background. The vector's signs inside the patch,
       and the depth convention in the band the patch left. */
    ok = inputs_make(&in, RENDER_W, RENDER_H, 0, 0, up, still, 1, -1);
    if (ok) {
        const Variant flipped = {1, 1, 1, 1, 0};
        truth(&in, up, reference, regions);
        snprintf(name, sizeof name, "case 3 %s vector signs in the patch", up ? "up" : "down");
        ok = signs(name, &in, 12, SELFTEST_INSIDE, reference, regions, output);
        const int ran = run(&in, &CONFIGURED, 0, output, NULL, NULL, NULL) &&
                        run(&in, &flipped, 0, other, NULL, NULL, NULL);
        const double own = ran ? difference(output, reference, regions, SELFTEST_UNCOVERED) : NAN;
        const double wrong = ran ? difference(other, reference, regions, SELFTEST_UNCOVERED) : NAN;
        const int beats = ran && own >= 0.0 && selftest_beats(own, wrong, SELFTEST_MARGIN);
        LOG(ST "case 3 %s: the band the patch left: error %.6f, with the depth convention turned %.6f, background "
               "%.6f, patch %.6f: %s\n", up ? "up" : "down", own, wrong,
            ran ? difference(output, reference, regions, SELFTEST_BACKGROUND) : NAN,
            ran ? difference(output, reference, regions, SELFTEST_INSIDE) : NAN, beats ? "PASS" : "FAIL");
        ok = ok && beats;
    }
    pass &= ok;
    inputs_free(&in);

    /* Case 5: a new pattern with the reset flag. The output of that frame against a context that never saw the old
       pattern, and the same without the flag as the control of the test itself. */
    ok = inputs_make(&in, RENDER_W, RENDER_H, 0, 0, up, still, 0, FRAMES);
    if (ok) {
        Inputs fresh = in;
        const int with = run(&in, &CONFIGURED, 1, output, NULL, NULL, NULL);
        double* without = malloc(size);
        const int control = without && run(&in, &CONFIGURED, 0, without, NULL, NULL, NULL);
        /* the fresh context: the gate's first frame, then the frame of the cut alone */
        fresh.scene += FRAMES - 1;
        fresh.colour += (size_t)(FRAMES - 1) * in.w * in.h;
        fresh.depth += (size_t)(FRAMES - 1) * in.w * in.h;
        fresh.motion += (size_t)(FRAMES - 1) * in.w * in.h * 4;
        fresh.frames = 2;
        fresh.cut_at = -1;
        memset(regions, SELFTEST_BACKGROUND, (size_t)OUT_W * OUT_H);
        const int alone = run(&fresh, &CONFIGURED, 0, other, NULL, NULL, NULL);
        const double same = with && alone ? difference(output, other, regions, SELFTEST_BACKGROUND) : NAN;
        const double apart = control && alone ? difference(without, other, regions, SELFTEST_BACKGROUND) : NAN;
        LOG(ST "case 5 %s: the reset frame against a fresh context %.6f (at most %.2f): %s. Without the flag %.6f "
               "(at least %.2f, the test's own control): %s\n", up ? "up" : "down", same, SELFTEST_SAME,
            selftest_same(same) ? "PASS" : "FAIL", apart, SELFTEST_APART,
            selftest_apart(apart) ? "PASS" : "FAIL (the control saw nothing)");
        ok = selftest_same(same) && selftest_apart(apart);
        free(without);
    }
    pass &= ok;
    inputs_free(&in);

    /* Case 6: the same content at a place that is not the first texel of larger images of other proportions, with
       padding that is none of the scene's, against the content at the first texel of images of its own size. */
    ok = inputs_make(&in, 400, 240, 40, 30, up, pan, 0, -1);
    if (ok) {
        Inputs tight;
        ok = inputs_make(&tight, RENDER_W, RENDER_H, 0, 0, up, pan, 0, -1) &&
             run(&in, &CONFIGURED, 0, output, NULL, NULL, NULL) && run(&tight, &CONFIGURED, 0, other, NULL, NULL, NULL);
        memset(regions, SELFTEST_BACKGROUND, (size_t)OUT_W * OUT_H);
        const double same = ok ? difference(output, other, regions, SELFTEST_BACKGROUND) : NAN;
        LOG(ST "case 6 %s: a rectangle at 40,30 of 400x240 images against the same at 0,0 of %dx%d: %.6f (at most "
               "%.2f): %s\n", up ? "up" : "down", RENDER_W, RENDER_H, same, SELFTEST_SAME,
            selftest_same(same) ? "PASS" : "FAIL");
        ok = selftest_same(same);
        inputs_free(&tight);
    }
    pass &= ok;
    inputs_free(&in);

    free(reference);
    free(output);
    free(other);
    free(regions);
    return pass;
}

/* Case 4: the size changes in mid-run. The frames until the new context is ready are skipped, and then the frames
   are evaluated again. Run on one session with images that grow from the rectangle to a wider image around it. */
static int resize(void) {
    const double still[2] = {0.0, 0.0};
    const Variant* v = &CONFIGURED;
    SnxUpscalerTraits traits = {.colour_usage = VK_IMAGE_USAGE_SAMPLED_BIT,
                                .depth_usage = VK_IMAGE_USAGE_SAMPLED_BIT,
                                .motion_usage = VK_IMAGE_USAGE_SAMPLED_BIT,
                                .output_usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT};
    Inputs small, wide;
    Session s;
    double* output = malloc(sizeof(double) * 3 * OUT_W * OUT_H);
    int ok = output && inputs_make(&small, RENDER_W, RENDER_H, 0, 0, 1, still, 0, -1) &&
             inputs_make(&wide, 400, 240, 0, 0, 1, still, 0, -1) && session_open(&s, &wide, &traits);
    unsigned before = 0, skipped_at_change = 0, after = 0;
    for (int i = 0; ok && i < 12; i++) ok = frame_run(&s, &small, i, v, 0, output) >= 0;
    before = s.evaluated;
    const unsigned skipped = s.skipped;
    for (int i = 12; ok && i < 24; i++) ok = frame_run(&s, &wide, i, v, 0, output) >= 0;
    skipped_at_change = s.skipped - skipped;
    after = s.evaluated - before;
    const int pass = ok && before == 11 && skipped_at_change >= 1 && after >= 10 && after + skipped_at_change == 12;
    LOG(ST "case 4: %u frames evaluated before the images' size changed, %u skipped at the change, %u evaluated "
           "after it: %s\n", before, skipped_at_change, after, pass ? "PASS" : "FAIL");
    if (output) session_close(&s);
    inputs_free(&small);
    inputs_free(&wide);
    free(output);
    return pass;
}

/* The mode. The process's result: 0 when every case passed and the validation layer was there and silent. */
int vk_upscaler_selftest(const char* adapter) {
    int pass;
    LOG(ST "started for the adapter \"%s\". No game is loaded and no window is opened\n", adapter);
    g.api = vk_upscaler_adapter(adapter);
    if (!g.api) {
        LOG(ST "FAIL: the adapter \"%s\" is not available\n", adapter);
        return 1;
    }
    g.generation = 1;
    if (!device_make()) return 1;
    pass = suite(1);
    g.generation++;
    pass &= suite(0);
    g.generation++;
    pass &= resize();
    vkDeviceWaitIdle(g.device);
    /* Case 7: no message of the validation layer in any case. Without the layer the case was not run, which is
       not a pass. */
    if (!g.validation) LOG(ST "case 7: NOT RUN, the validation layer is not installed: FAIL\n");
    else LOG(ST "case 7: %u validation messages: %s\n", g.messages, g.messages ? "FAIL" : "PASS");
    pass = pass && g.validation && !g.messages;
    LOG(ST "RESULT: %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
