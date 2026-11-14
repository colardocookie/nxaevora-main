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
#define OFFSETS 18              /* ceil(8 x 1.5 x 1.5): the jitter's offsets for these sizes */
#define FRAMES (2 * OFFSETS)    /* evaluated frames of a run at these sizes: twice the offsets */
#define SAMPLES 8               /* the reference averages 8 x 8 points of a pixel */
#define NEAR_PLANE 0.1          /* the scene's depth is 0.1 / distance: an infinite far plane, reversed */
#define FOV_Y 0.8726646259971648 /* 50 degrees */
#define FRAME_SECONDS (1.0 / 60.0)
#define BAND_LEAST 500          /* pixels of a band under which it judges nothing */

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
    /* A submission's end is not known, or a command buffer could not be reset. From then on nothing is recorded,
       reset, submitted or freed on this device: the command buffer, the buffer and the queue are shared by all
       cases. The cases left are not run and the mode fails. */
    int unknown;
} g;

/* one run's inputs, made once and never changed: a sign variant changes only the numbers handed to the adapter */
typedef struct {
    int w, h;            /* the images' size */
    int rw, rh, ow, oh;  /* the scene's rectangle in them, and the output's size */
    int offsets;         /* of the jitter: ceil(8 x output's pixels / rectangle's pixels) */
    SelftestViewport vp; /* the scene's viewport inside them */
    int frames;          /* twice the offsets, and the first, which the gate does not let through */
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
    unsigned frame, failures, skipped, evaluated, published; /* published: contexts that came into use */
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
    p->format = format;
    p->w = w;
    p->h = h;
    /* a picture that is only copied to and from has no usage a view may be made for, and needs none */
    if (!usage) return 1;
    const VkImageViewCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = p->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange = {is_depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
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

static void unknown(const char* what) {
    if (!g.unknown)
        LOG(ST "FAIL: %s. What the device still uses is not known, so nothing more is recorded, reset, submitted "
               "or freed, and the cases left are not run\n", what);
    g.unknown = 1;
}

/* the command buffer's contents are discarded: 1 only when the reset itself succeeded */
static int discard(void) {
    if (g.unknown) return 0;
    if (VK_OK(vkResetCommandBuffer(g.cb, 0))) return 1;
    unknown("the command buffer could not be reset");
    return 0;
}

static int begin(void) {
    const VkCommandBufferBeginInfo bi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                         .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    return discard() && VK_OK(vkBeginCommandBuffer(g.cb, &bi));
}

/* the device has finished everything: 1, or its state is unknown from now on */
static int idle(void) {
    if (g.unknown) return 0;
    if (VK_OK(vkDeviceWaitIdle(g.device))) return 1;
    unknown("waiting for the device failed");
    return 0;
}

/* The command buffer is submitted. 1: it has completed. 0: it was not submitted and its contents were discarded
   by a reset that succeeded. -1: its end is not known (submitted and the wait failed, or the reset failed), and
   the whole mode stops using the device. */
static int submit(void) {
    const VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &g.cb};
    if (g.unknown) return -1;
    if (!VK_OK(vkEndCommandBuffer(g.cb)) || !VK_OK(vkQueueSubmit(g.queue, 1, &si, VK_NULL_HANDLE)))
        return discard() ? 0 : -1;
    if (VK_OK(vkQueueWaitIdle(g.queue))) return 1;
    unknown("a submission was made and waiting for it failed");
    return -1;
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
static void offset(unsigned k, unsigned n, double out[2]) {
    static const unsigned bases[2] = {2, 3};
    for (int axis = 0; axis < 2; axis++) {
        double f = 1.0, value = 0.0;
        for (unsigned i = k % n + 1; i; i /= bases[axis]) {
            f /= bases[axis];
            value += f * (i % bases[axis]);
        }
        out[axis] = value - 0.5;
    }
}

/* The inputs of a run. The scene's rectangle is size[0] by size[1] at x, y inside images of w by h, whose rest
   holds a colour, a depth and a vector that are none of the scene's, for an output of size[2] by size[3]. move:
   the background's move a frame in render pixels (x right, y up). patch: 1 for a nearer patch that moves another
   way. cut: 1 when the last frame shows the other pattern. up: 1 for a viewport with a negative height. */
static const int SIZES[4] = {RENDER_W, RENDER_H, OUT_W, OUT_H};
static int inputs_make(Inputs* in, const int size[4], int w, int h, int x, int y, int up, const double move[2],
                       int patch, int cut) {
    const int RW = size[0], RH = size[1];
    memset(in, 0, sizeof *in);
    in->w = w;
    in->h = h;
    in->rw = RW;
    in->rh = RH;
    in->ow = size[2];
    in->oh = size[3];
    in->offsets = (int)((8LL * in->ow * in->oh + (long long)RW * RH - 1) / ((long long)RW * RH));
    in->vp = (SelftestViewport){x, up ? y + RH : y, RW, up ? -RH : RH};
    in->frames = 2 * in->offsets + 1;
    const int cut_at = cut ? in->frames - 1 : -1;
    in->cut_at = cut_at;
    in->scene = calloc((size_t)in->frames, sizeof *in->scene);
    in->colour = malloc((size_t)in->frames * w * h * 4);
    in->depth = malloc((size_t)in->frames * w * h * 4);
    in->motion = malloc((size_t)in->frames * w * h * 8);
    if (!in->scene || !in->colour || !in->depth || !in->motion) return 0;
    const double step[2] = {2.0 * move[0] / RW, 2.0 * move[1] / RH};
    const double patch_step[2] = {2.0 * -0.9 / RW, 2.0 * 1.1 / RH};
    const double padding[3] = {0.9, 0.0, 0.9};
    for (int i = 0; i < in->frames; i++) {
        SelftestFrame* f = &in->scene[i];
        double px[2];
        offset((unsigned)i, (unsigned)in->offsets, px);
        f->pattern = cut_at >= 0 && i >= cut_at;
        f->jitter[0] = 2.0 * px[0] / RW;
        f->jitter[1] = 2.0 * px[1] / RH;
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
                if (qx >= x && qx < x + RW && py >= y && py < y + RH) {
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

static void pictures_free(Session* s) {
    picture_free(&s->colour);
    picture_free(&s->depth);
    picture_free(&s->motion);
    picture_free(&s->output);
    picture_free(&s->wide_colour);
    picture_free(&s->wide_depth);
    picture_free(&s->wide_motion);
}

/* the session's pictures for these inputs' sizes, in place of the ones it had */
static int pictures_make(Session* s, const Inputs* in, const SnxUpscalerTraits* traits) {
    const u32 rw = (u32)in->rw, rh = (u32)in->rh;
    if (!idle()) return 0;
    pictures_free(s);
    if (!picture_make(&s->colour, VK_FORMAT_B10G11R11_UFLOAT_PACK32, rw, rh, traits->colour_usage) ||
        !picture_make(&s->depth, VK_FORMAT_D24_UNORM_S8_UINT, rw, rh, traits->depth_usage) ||
        !picture_make(&s->motion, VK_FORMAT_R16G16B16A16_SFLOAT, rw, rh, traits->motion_usage) ||
        !picture_make(&s->output, VK_FORMAT_R16G16B16A16_SFLOAT, (u32)in->ow, (u32)in->oh, traits->output_usage))
        return 0;
    if (in->w == in->rw && in->h == in->rh) return 1;
    return picture_make(&s->wide_colour, VK_FORMAT_B10G11R11_UFLOAT_PACK32, (u32)in->w, (u32)in->h, 0) &&
           picture_make(&s->wide_depth, VK_FORMAT_D24_UNORM_S8_UINT, (u32)in->w, (u32)in->h, 0) &&
           picture_make(&s->wide_motion, VK_FORMAT_R16G16B16A16_SFLOAT, (u32)in->w, (u32)in->h, 0);
}

static int session_open(Session* s, const Inputs* in, const SnxUpscalerTraits* traits) {
    memset(s, 0, sizeof *s);
    return pictures_make(s, in, traits);
}

/* Once the device's state is not known a session frees nothing: the device may still be using it all. */
static void session_close(Session* s) {
    if (!idle()) return;
    if (s->live) g.api->destroy(s->live);
    if (s->pending) g.api->destroy(s->pending);
    pictures_free(s);
    memset(s, 0, sizeof *s);
}

/* The context for a configuration, by the states the runtime will keep (temporal_upscaler_*): created, prepared into
   a command buffer that is submitted and has completed, and only then published. 1 when the frame may be
   evaluated on s->live. A context is freed only when its command buffer was discarded or has completed. When
   that is not known (g.unknown) nothing is freed and nothing more is done on the device. */
static int context_ready(Session* s, const Inputs* in, const TemporalUpscalerConfig* config, u32 flags,
                         SnxUpscalerTraits* traits) {
    if (g.unknown) return 0;
    if (temporal_upscaler_ready(&s->contexts, config)) return 1;
    if (!temporal_upscaler_must_create(&s->contexts, config)) return 0;
    const SnxUpscalerCreate make = {.sizes = {(u32)in->rw, (u32)in->rh, (u32)in->rw, (u32)in->rh, (u32)in->ow,
                                              (u32)in->oh},
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
    /* Three ends. Nothing was submitted and the command buffer's reset succeeded: what it held is discarded. It
       was submitted and has completed. Or its end is not known: then the context is held and nothing is freed. */
    int prepared = 0, submitted = 0;
    if (made && begin()) {
        prepared = g.api->prepare(s->pending, g.cb) == SNX_UPSCALER_OK;
        if (prepared) submitted = submit();
        else discard();
    }
    if (g.unknown) return 0;
    temporal_upscaler_created(&s->contexts, config, prepared && submitted == 1);
    switch (temporal_upscaler_completed(&s->contexts, config)) {
    case TEMPORAL_UPSCALER_RETIRE:
        if (!idle()) return 0;
        g.api->destroy(s->live);
        /* fall through */
    case TEMPORAL_UPSCALER_PUBLISHED:
        s->live = s->pending;
        s->pending = NULL;
        s->published++;
        break;
    default: /* a failed or stale one: its command buffer was discarded or has completed, so it may go */
        if (s->pending) g.api->destroy(s->pending);
        s->pending = NULL;
        break;
    }
    return temporal_upscaler_ready(&s->contexts, config);
}

/* One frame of a run through the gate, the conversion, the check and the adapter. out gets the output picture as
   three doubles a pixel when the frame was evaluated. 1: evaluated. 0: skipped, as a gated frame or one whose
   context was not ready is. -1: a failure. */
static int frame_run(Session* s, const Inputs* in, int i, const Variant* v, int cut, double* out) {
    const size_t pixels = (size_t)in->w * in->h;
    const int wide = in->w != in->rw || in->h != in->rh;
    const int x = (int)in->vp.x, y = (int)(in->vp.h < 0 ? in->vp.y + in->vp.h : in->vp.y);
    SnxUpscalerTraits traits = {0};
    TemporalRectState state = {0};
    TemporalUpscalerFrame numbers;
    float camera[4];
    if (g.unknown) return -1;
    s->frame++;
    /* what the renderer would know of the frame: its viewport, the images' size, the fraction of them in use */
    state.viewport = (TemporalViewport){(float)in->vp.x, (float)in->vp.y, (float)in->vp.w, (float)in->vp.h};
    state.target_w = in->w;
    state.target_h = in->h;
    state.fx = (float)in->rw / (float)in->w;
    state.fy = (float)in->rh / (float)in->h;
    if (!temporal_rect_from_viewport(&state.viewport, in->w, in->h, &state.rect)) return -1;
    const TemporalRectStamp stamp = {1, g.generation, in->w, in->h, in->ow, in->oh, state.rect};
    const double jitter[2] = {in->scene[i].jitter[0], in->scene[i].jitter[1]};
    temporal_rect_decide(&s->gate, s->frame, &state);
    if (!temporal_upscaler_frame(&s->gate, s->frame, &stamp, &state.viewport, jitter, cut, &numbers) ||
        !temporal_upscaler_camera(NEAR_PLANE, (double)INFINITY, FOV_Y, FRAME_SECONDS, 1, camera)) {
        s->skipped++;
        return 0;
    }
    const u32 flags = SNX_UPSCALER_COLOUR_LINEAR_HDR | SNX_UPSCALER_VECTORS_UNJITTERED |
                      SNX_UPSCALER_EXPOSURE_AUTOMATIC | SNX_UPSCALER_FAR_PLANE_INFINITE |
                      (v->depth_reversed ? SNX_UPSCALER_DEPTH_REVERSED : 0);
    const TemporalUpscalerConfig config = {in->w, in->h, in->rw, in->rh, in->ow, in->oh,
                                           g.generation * 2 + (u64)v->depth_reversed};
    if (!context_ready(s, in, &config, flags, &traits)) {
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
                   s->colour.w, s->colour.h},
        .depth = {s->depth.image, s->depth.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, s->depth.format,
                  s->depth.w, s->depth.h},
        .motion = {s->motion.image, s->motion.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, s->motion.format,
                   s->motion.w, s->motion.h},
        .output = {s->output.image, s->output.view, VK_IMAGE_LAYOUT_GENERAL, s->output.format, s->output.w,
                   s->output.h},
        .render_x = 0,
        .render_y = 0,
        .render_w = (u32)in->rw,
        .render_h = (u32)in->rh,
        /* the conversion's numbers, with the variant's signs: the configured variant hands them on as they are */
        .jitter = {(float)(numbers.jitter[0] * v->jitter_x), (float)(numbers.jitter[1] * v->jitter_y)},
        .vector_scale = {(float)(numbers.vector_scale[0] * v->vector_x),
                         (float)(numbers.vector_scale[1] * v->vector_y)},
        .reset = (u32)numbers.reset,
        /* the camera's numbers exactly as the check passed them */
        .frame_seconds = camera[3],
        .near_plane = camera[0],
        .far_plane = camera[1],
        .fov_y = camera[2]};
    const int evaluated = g.api->evaluate(s->live, &frame) == SNX_UPSCALER_OK;
    /* whatever the adapter left bound or pending: the output is taken over for the copy with a full barrier */
    const VkBufferImageCopy region = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                      .imageExtent = {(u32)in->ow, (u32)in->oh, 1}};
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
    if (temporal_upscaler_failed(&s->failures, evaluated && done == 1))
        LOG(ST "the adapter failed eight times in a row\n");
    if (!evaluated || done != 1) return -1;
    temporal_rect_wrote(&s->gate, s->frame, &stamp);
    s->evaluated++;
    const u16* words = (const u16*)g.staging_bytes;
    for (size_t p = 0; p < (size_t)in->ow * in->oh; p++)
        for (int c = 0; c < 3; c++) {
            out[3 * p + c] = selftest_half_value(words[4 * p + c]);
            if (!isfinite(out[3 * p + c])) return -1;
        }
    return 1;
}

/* the mean absolute difference of two pictures over the pixels of one region, per channel of a range of 1. -1 when
   the region has no pixel. */
static double difference(const Inputs* in, const double* a, const double* b, const u8* regions, int region) {
    double sum = 0.0;
    size_t n = 0;
    for (size_t p = 0; p < (size_t)in->ow * in->oh; p++) {
        if (regions[p] != region) continue;
        for (int c = 0; c < 3; c++) sum += fabs(a[3 * p + c] - b[3 * p + c]);
        n += 3;
    }
    return n ? sum / (double)n : -1.0;
}

/* the true picture of the run's last frame at the output's size, and each pixel's region */
static void truth(const Inputs* in, int up, double* reference, u8* regions) {
    const int W = in->ow, H = in->oh;
    const SelftestViewport out = {0, up ? H : 0, W, up ? -H : H};
    const SelftestFrame* f = &in->scene[in->frames - 1];
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            selftest_reference(f, &out, x, y, SAMPLES, &reference[3 * ((size_t)y * W + x)]);
            regions[(size_t)y * W + x] = (u8)selftest_region(f, &out, W, H, x, y);
        }
}

/* The band a patch left in frame i of a run, in an output picture: the error against the true picture summed over
   its pixels and channels, and the pixels counted. */
typedef struct {
    int up;
    double sum;
    size_t pixels;
} Band;
static void band_add(Band* band, const Inputs* in, int i, const double* output) {
    const int W = in->ow, H = in->oh;
    const SelftestViewport out = {0, band->up ? H : 0, W, band->up ? -H : H};
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            double reference[3];
            if (selftest_region(&in->scene[i], &out, W, H, x, y) != SELFTEST_UNCOVERED) continue;
            selftest_reference(&in->scene[i], &out, x, y, SAMPLES, reference);
            for (int c = 0; c < 3; c++) band->sum += fabs(output[3 * ((size_t)y * W + x) + c] - reference[c]);
            band->pixels++;
        }
}

/* One run of every frame with one variant, on a session of its own. last gets the last output. first_change and
   last_change, when asked for, get the mean change between consecutive outputs over the first and the last cycle
   of offsets. band, when given, takes every evaluated frame after the first cycle. 1 when every frame after the
   gate's first was evaluated. */
static int run(const Inputs* in, const Variant* v, int use_cut, double* last, double* first_change,
               double* last_change, const u8* regions, Band* band) {
    Session s;
    SnxUpscalerTraits traits = {.colour_usage = VK_IMAGE_USAGE_SAMPLED_BIT,
                                .depth_usage = VK_IMAGE_USAGE_SAMPLED_BIT,
                                .motion_usage = VK_IMAGE_USAGE_SAMPLED_BIT,
                                .output_usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT};
    const size_t size = sizeof(double) * 3 * (size_t)in->ow * in->oh;
    double* previous = malloc(size);
    SelftestSettling settling = {{0.0, 0.0}, {0, 0}};
    memset(&s, 0, sizeof s);
    int ok = !g.unknown && previous && session_open(&s, in, &traits), evaluated = 0;
    for (int i = 0; ok && i < in->frames; i++) {
        const int r = frame_run(&s, in, i, v, use_cut && i == in->cut_at, last);
        if (r < 0) ok = 0;
        if (r <= 0) continue;
        if (evaluated && regions) {
            const double change = difference(in, last, previous, regions, SELFTEST_BACKGROUND);
            selftest_change(&settling, evaluated, in->offsets, change);
        }
        memcpy(previous, last, size);
        evaluated++;
        if (band && evaluated > in->offsets) band_add(band, in, i, last);
    }
    if (ok && evaluated != in->frames - 1) {
        LOG(ST "a run evaluated %d of %d frames (%u skipped)\n", evaluated, in->frames - 1, s.skipped);
        ok = 0;
    }
    if (first_change) *first_change = selftest_cycle(&settling, 0);
    if (last_change) *last_change = selftest_cycle(&settling, 1);
    session_close(&s);
    free(previous);
    return ok;
}

/* one frame of the inputs scaled up plainly, with two-by-two linear filtering and no knowledge of the jitter: what
   an upscaler has to beat */
static void plain(const Inputs* in, int i, int up, double* out) {
    const int W = in->ow, H = in->oh;
    const SelftestViewport to = {0, up ? H : 0, W, up ? -H : H};
    const u32* colour = in->colour + (size_t)i * in->w * in->h;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
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
            memcpy(&out[3 * ((size_t)y * W + x)], sum, sizeof sum);
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
        ran = run(in, &v, 0, output, NULL, NULL, NULL, NULL);
        errors[n] = ran ? difference(in, output, reference, regions, region) : NAN;
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

/* 1 with a line when a case cannot be run because the device's state is not known any more */
static int not_run(const char* name, int up) {
    if (g.unknown) LOG(ST "%s %s: NOT RUN\n", name, up ? "up" : "down");
    return g.unknown;
}

/* the whole suite for one sign of the viewport's height: 1 when every case passed */
static int suite(int up) {
    const size_t pixels = (size_t)OUT_W * OUT_H, size = sizeof(double) * 3 * pixels;
    const double still[2] = {0.0, 0.0}, pan[2] = {1.3, 0.7};
    double *reference = malloc(size), *output = malloc(size), *other = malloc(size);
    u8* regions = malloc(pixels);
    Inputs in;
    int pass = 1, ok;
    char name[64];
    if (!reference || !output || !other || !regions) return 0;
    LOG(ST "suite with a viewport height of %s: render %dx%d, output %dx%d, %d offsets, %d frames a run\n",
        up ? "minus the image's (y up)" : "plus the image's (y down)", RENDER_W, RENDER_H, OUT_W, OUT_H, OFFSETS,
        FRAMES);

    /* Case 1: jitter alone over a still pattern. The jitter's signs, the plain scale, and settling. */
    memset(&in, 0, sizeof in);
    ok = !not_run("case 1", up) && inputs_make(&in, SIZES, RENDER_W, RENDER_H, 0, 0, up, still, 0, 0);
    if (ok) {
        double first = 0, last = 0;
        truth(&in, up, reference, regions);
        snprintf(name, sizeof name, "case 1 %s jitter signs", up ? "up" : "down");
        ok = signs(name, &in, 3, SELFTEST_BACKGROUND, reference, regions, output);
        const int ran = run(&in, &CONFIGURED, 0, output, &first, &last, regions, NULL);
        const double error = ran ? difference(&in, output, reference, regions, SELFTEST_BACKGROUND) : NAN;
        plain(&in, in.frames - 1, up, other);
        const double scaled = difference(&in, other, reference, regions, SELFTEST_BACKGROUND);
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
    memset(&in, 0, sizeof in);
    ok = !not_run("case 2", up) && inputs_make(&in, SIZES, RENDER_W, RENDER_H, 0, 0, up, pan, 0, 0);
    if (ok) {
        truth(&in, up, reference, regions);
        snprintf(name, sizeof name, "case 2 %s all signs", up ? "up" : "down");
        ok = signs(name, &in, 15, SELFTEST_BACKGROUND, reference, regions, output);
    }
    pass &= ok;
    inputs_free(&in);

    /* Case 3: a nearer patch that moves on its own over a still background. The vector's signs inside the patch,
       and the depth convention in the band the patch left. */
    memset(&in, 0, sizeof in);
    ok = !not_run("case 3", up) && inputs_make(&in, SIZES, RENDER_W, RENDER_H, 0, 0, up, still, 1, 0);
    if (ok) {
        const Variant flipped = {1, 1, 1, 1, 0};
        truth(&in, up, reference, regions);
        snprintf(name, sizeof name, "case 3 %s vector signs in the patch", up ? "up" : "down");
        ok = signs(name, &in, 12, SELFTEST_INSIDE, reference, regions, output);
        /* the band the patch left, over every evaluated frame after the first cycle of offsets. Under BAND_LEAST
           pixels it is too little to judge by, and the case fails as a test. */
        Band own_band = {up, 0.0, 0}, wrong_band = {up, 0.0, 0};
        const int ran = run(&in, &CONFIGURED, 0, output, NULL, NULL, NULL, &own_band) &&
                        run(&in, &flipped, 0, other, NULL, NULL, NULL, &wrong_band);
        const int enough = own_band.pixels >= BAND_LEAST && wrong_band.pixels == own_band.pixels;
        const double own = ran && enough ? own_band.sum / (3.0 * (double)own_band.pixels) : NAN;
        const double wrong = ran && enough ? wrong_band.sum / (3.0 * (double)wrong_band.pixels) : NAN;
        const int beats = ran && enough && selftest_beats(own, wrong, SELFTEST_MARGIN);
        LOG(ST "case 3 %s: the band the patch left, %zu pixels over %d frames (at least %d): error %.6f, with the "
               "depth convention turned %.6f, background %.6f, patch %.6f: %s\n", up ? "up" : "down",
            own_band.pixels, in.offsets, BAND_LEAST, own, wrong,
            ran ? difference(&in, output, reference, regions, SELFTEST_BACKGROUND) : NAN,
            ran ? difference(&in, output, reference, regions, SELFTEST_INSIDE) : NAN,
            beats ? "PASS" : enough ? "FAIL" : "FAIL (too few pixels to judge by)");
        ok = ok && beats;
    }
    pass &= ok;
    inputs_free(&in);

    /* Case 5: a new pattern with the reset flag. The output of that frame against a context that never saw the old
       pattern, and the same without the flag as the control of the test itself. */
    memset(&in, 0, sizeof in);
    ok = !not_run("case 5", up) && inputs_make(&in, SIZES, RENDER_W, RENDER_H, 0, 0, up, still, 0, 1);
    if (ok) {
        Inputs fresh = in;
        const int with = run(&in, &CONFIGURED, 1, output, NULL, NULL, NULL, NULL);
        double* without = malloc(size);
        const int control = without && run(&in, &CONFIGURED, 0, without, NULL, NULL, NULL, NULL);
        /* the fresh context: the gate's first frame, then the frame of the cut alone */
        const size_t skip = (size_t)in.frames - 2;
        fresh.scene += skip;
        fresh.colour += skip * in.w * in.h;
        fresh.depth += skip * in.w * in.h;
        fresh.motion += skip * in.w * in.h * 4;
        fresh.frames = 2;
        fresh.cut_at = -1;
        memset(regions, SELFTEST_BACKGROUND, pixels);
        const int alone = run(&fresh, &CONFIGURED, 0, other, NULL, NULL, NULL, NULL);
        const double same = with && alone ? difference(&in, output, other, regions, SELFTEST_BACKGROUND) : NAN;
        const double apart = control && alone ? difference(&in, without, other, regions, SELFTEST_BACKGROUND) : NAN;
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
    memset(&in, 0, sizeof in);
    ok = !not_run("case 6", up) && inputs_make(&in, SIZES, 400, 240, 40, 30, up, pan, 0, 0);
    if (ok) {
        Inputs tight;
        ok = inputs_make(&tight, SIZES, RENDER_W, RENDER_H, 0, 0, up, pan, 0, 0) &&
             run(&in, &CONFIGURED, 0, output, NULL, NULL, NULL, NULL) &&
             run(&tight, &CONFIGURED, 0, other, NULL, NULL, NULL, NULL);
        memset(regions, SELFTEST_BACKGROUND, pixels);
        const double same = ok ? difference(&in, output, other, regions, SELFTEST_BACKGROUND) : NAN;
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

/* Case 4: the sizes change in mid-run, on one session: 320x180 to 480x270, then 400x224 to 640x360, so the
   adapter gets a context of other sizes. The frames until that one is ready are skipped. Then the still pattern
   is measured as in case 1 against the new size's true picture: after twice the offsets the error must beat
   one frame scaled plainly by the margin, and the output must have stopped changing. */
static int resize(int up) {
    static const int BIG[4] = {400, 224, 640, 360};
    const size_t size = sizeof(double) * 3 * (size_t)BIG[2] * BIG[3];
    const double still[2] = {0.0, 0.0};
    SnxUpscalerTraits traits = {.colour_usage = VK_IMAGE_USAGE_SAMPLED_BIT,
                                .depth_usage = VK_IMAGE_USAGE_SAMPLED_BIT,
                                .motion_usage = VK_IMAGE_USAGE_SAMPLED_BIT,
                                .output_usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT};
    Inputs small, big;
    Session s;
    double *output = malloc(size), *previous = malloc(size), *reference = malloc(size), *scaled = malloc(size);
    u8* regions = malloc((size_t)BIG[2] * BIG[3]);
    SelftestSettling settling = {{0.0, 0.0}, {0, 0}};
    int evaluated = 0;
    /* everything starts empty, and only what was made is freed */
    memset(&small, 0, sizeof small);
    memset(&big, 0, sizeof big);
    memset(&s, 0, sizeof s);
    int ok = !not_run("case 4", up) && output && previous && reference && scaled && regions &&
             inputs_make(&small, SIZES, RENDER_W, RENDER_H, 0, 0, up, still, 0, 0) &&
             inputs_make(&big, BIG, BIG[0], BIG[1], 0, 0, up, still, 0, 0) && session_open(&s, &small, &traits);
    for (int i = 0; ok && i < 12; i++) ok = frame_run(&s, &small, i, &CONFIGURED, 0, output) >= 0;
    const unsigned before = ok ? s.evaluated : 0, skipped = ok ? s.skipped : 0;
    /* the change: pictures of the new sizes, and the same session, gate and contexts */
    ok = ok && pictures_make(&s, &big, &traits);
    if (ok) truth(&big, up, reference, regions);
    for (int i = 0; ok && i < big.frames; i++) {
        const int r = frame_run(&s, &big, i, &CONFIGURED, 0, output);
        if (r < 0) ok = 0;
        if (r <= 0) continue;
        if (evaluated)
            selftest_change(&settling, evaluated, big.offsets,
                            difference(&big, output, previous, regions, SELFTEST_BACKGROUND));
        memcpy(previous, output, size);
        evaluated++;
    }
    const unsigned skipped_at_change = ok ? s.skipped - skipped : 0;
    const double first = selftest_cycle(&settling, 0), last = selftest_cycle(&settling, 1);
    const double error = ok ? difference(&big, output, reference, regions, SELFTEST_BACKGROUND) : NAN;
    if (ok) plain(&big, big.frames - 1, up, scaled);
    const double plainly = ok ? difference(&big, scaled, reference, regions, SELFTEST_BACKGROUND) : NAN;
    const int counted = ok && before == 11 && skipped_at_change >= 1 && evaluated == 2 * big.offsets &&
                        s.published == 2;
    const int beats = ok && selftest_beats(error, plainly, SELFTEST_MARGIN), settles = selftest_settles(first, last);
    LOG(ST "case 4 %s: %u frames evaluated at %dx%d to %dx%d, %u skipped at the change, %d evaluated at %dx%d to "
           "%dx%d, %u contexts made: %s\n", up ? "up" : "down", before, RENDER_W, RENDER_H, OUT_W, OUT_H,
        skipped_at_change, evaluated, BIG[0], BIG[1], BIG[2], BIG[3], s.published, counted ? "PASS" : "FAIL");
    LOG(ST "case 4 %s: after the change, error %.6f against one frame scaled plainly %.6f: %s. Change a frame over "
           "the first cycle %.6f, over the last %.6f: %s\n", up ? "up" : "down", error, plainly,
        beats ? "PASS" : "FAIL", first, last, settles ? "PASS" : "FAIL");
    session_close(&s);
    inputs_free(&small);
    inputs_free(&big);
    free(output);
    free(previous);
    free(reference);
    free(scaled);
    free(regions);
    return counted && beats && settles;
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
    pass &= resize(1);
    g.generation++;
    pass &= resize(0);
    pass = pass && idle();
    /* Case 7: no message of the validation layer in any case. Without the layer the case was not run, which is
       not a pass, and neither is a run that had to stop. */
    if (!g.validation) LOG(ST "case 7: NOT RUN, the validation layer is not installed: FAIL\n");
    else if (g.unknown)
        LOG(ST "case 7: NOT JUDGED, the run stopped (%u validation messages until then): FAIL\n", g.messages);
    else LOG(ST "case 7: %u validation messages: %s\n", g.messages, g.messages ? "FAIL" : "PASS");
    pass = pass && g.validation && !g.messages && !g.unknown;
    LOG(ST "RESULT: %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
