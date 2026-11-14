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
    u64 edge_sums[2][3]; /* the edge case's images by orientation: colour, depth, vectors (edges_announce) */
    SelftestHanded* record; /* when set: what each frame of the inputs handed the adapter, by the frame's number */
    int record_n;
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
    int ungated;         /* a shape the game does not make: not put through the game's gate (frame_run) */
} Inputs;

/* What a variant makes of the numbers handed on: the factor on the reported jitter of each axis (1 as configured,
   -1 the sign turned, 0 told nothing), the vectors' signs, the depth convention it claims, whether every frame's
   inputs and dispatch are logged, and a create flag left out. */
typedef struct {
    double jitter_x, jitter_y;
    int vector_x, vector_y, depth_reversed, trace;
    int ldr; /* 1: the context is made without the flag for linear colours over 1 (a real create property) */
} Variant;
static const Variant CONFIGURED = {1, 1, 1, 1, 1};

/* a run's state across frames, as the runtime will keep it */
typedef struct {
    TemporalRectGate gate;
    TemporalUpscalerContexts contexts;
    SnxUpscaler *live, *pending;
    unsigned frame, failures, skipped, evaluated, published; /* published: contexts that came into use */
    unsigned wrote; /* the last frame an ungated run evaluated, 0 for none */
    u32 made_flags[4]; /* the create flags of the first contexts that came into use, in their order */
    unsigned evaluated_with; /* how many contexts had come into use at the last evaluate */
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

/* A run's inputs: the scene's rectangle of size[0] by size[1] at x, y in images of w by h, padded with what is
   none of the scene's, for an output of size[2] by size[3]. move: the background's move a frame in render pixels.
   patch: a nearer patch moving another way. cut: the last frame has the other pattern. up: a negative viewport
   height. edges: the edge picture. ahead: images one offset ahead of the reported jitter. turned: clip y negated. */
static const int SIZES[4] = {RENDER_W, RENDER_H, OUT_W, OUT_H};
static int inputs_build(Inputs* in, const int size[4], int w, int h, int x, int y, int up, const double move[2],
                        int patch, int cut, int edges, int ahead, int turned) {
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
        offset((unsigned)(i + (ahead ? 1 : 0)), (unsigned)in->offsets, px);
        f->pattern = edges ? SELFTEST_PATTERN_EDGES : cut_at >= 0 && i >= cut_at;
        f->out = (SelftestViewport){0, up ? in->oh : 0, in->ow, up ? -in->oh : in->oh};
        f->jitter[0] = 2.0 * px[0] / RW;
        f->jitter[1] = (turned ? -2.0 : 2.0) * px[1] / RH;
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
        if (ahead) { /* the images are made: from here the frame says it had its own offset */
            offset((unsigned)i, (unsigned)in->offsets, px);
            f->jitter[0] = 2.0 * px[0] / RW;
            f->jitter[1] = (turned ? -2.0 : 2.0) * px[1] / RH;
        }
    }
    return 1;
}
static int inputs_make(Inputs* in, const int size[4], int w, int h, int x, int y, int up, const double move[2],
                       int patch, int cut) {
    return inputs_build(in, size, w, h, x, y, up, move, patch, cut, 0, 0, 0);
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
        if (s->published < 4) s->made_flags[s->published] = flags;
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
    /* What a context is made with beside the sizes, as one number: the suite's generation and the variant's create
       properties. It is the configuration's generation, and part of what a history was made from: a context of
       another making has no history, so its first frame has the reset. */
    const u64 making = g.generation * 4 + (u64)(v->depth_reversed ? 2 : 0) + (u64)(v->ldr ? 1 : 0);
    const TemporalRectStamp stamp = {1, making, in->w, in->h, in->ow, in->oh, state.rect, state.fx, state.fy};
    /* the scene block as the first scene draw's fragment program would read it */
    const TemporalSceneBlock block = {1, 1, state.fx, state.fy};
    const double jitter[2] = {in->scene[i].jitter[0], in->scene[i].jitter[1]};
    int given;
    if (in->ungated) {
        /* The game's gate takes only shapes the game makes. A rectangle elsewhere in images of other proportions
           is for the conversion and the adapter alone, so it goes straight to them. The first frame is left out
           and a frame after one that was not evaluated has the reset, as in a run through the gate. */
        given = s->frame > 1 && temporal_upscaler_numbers(&stamp, &state.viewport, jitter,
                                                          cut || s->wrote + 1 != s->frame, &numbers);
    } else {
        temporal_rect_decide(&s->gate, s->frame, &state, 0);
        temporal_rect_confirm(&s->gate, s->frame, NULL, &block);
        temporal_rect_hooked(&s->gate, s->frame); /* the passes are reached in every frame of a run */
        given = temporal_upscaler_frame(&s->gate, s->frame, &stamp, &state.viewport, jitter, cut, &numbers);
    }
    if (!given || !temporal_upscaler_camera(NEAR_PLANE, (double)INFINITY, FOV_Y, FRAME_SECONDS, 1, camera)) {
        s->skipped++;
        return 0;
    }
    const u32 flags = (v->ldr ? 0 : SNX_UPSCALER_COLOUR_LINEAR_HDR) | SNX_UPSCALER_VECTORS_UNJITTERED |
                      SNX_UPSCALER_EXPOSURE_AUTOMATIC | SNX_UPSCALER_FAR_PLANE_INFINITE |
                      (v->depth_reversed ? SNX_UPSCALER_DEPTH_REVERSED : 0);
    const TemporalUpscalerConfig config = {in->w, in->h, in->rw, in->rh, in->ow, in->oh, making};
    if (!context_ready(s, in, &config, flags, &traits)) {
        s->skipped++;
        return s->contexts.pending || s->live ? 0 : -1;
    }
    /* The one place for it, for a run through the gate and for one without: a context that has come into use
       since the last evaluate has the reset on its first, whatever the frame's own history says. */
    numbers.reset = temporal_upscaler_reset(numbers.reset, s->published, &s->evaluated_with);
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
        .fov_y = camera[2],
        .trace = (u32)v->trace};
    /* what this frame hands over, kept for the matched pair: marked as done only when the frame has succeeded */
    SelftestHanded handed = {{frame.jitter[0], frame.jitter[1], frame.vector_scale[0], frame.vector_scale[1],
                              frame.frame_seconds, frame.near_plane, frame.far_plane, frame.fov_y},
                             {(u32)frame.render_x, (u32)frame.render_y, frame.render_w, frame.render_h, frame.reset,
                              flags, (u32)config.target_w, (u32)config.target_h, (u32)config.render_w,
                              (u32)config.render_h, (u32)config.out_w, (u32)config.out_h,
                              frame.colour.w, frame.colour.h, (u32)frame.colour.format, (u32)frame.colour.layout,
                              frame.depth.w, frame.depth.h, (u32)frame.depth.format, (u32)frame.depth.layout,
                              frame.motion.w, frame.motion.h, (u32)frame.motion.format, (u32)frame.motion.layout,
                              frame.output.w, frame.output.h, (u32)frame.output.format, (u32)frame.output.layout},
                             0};
    /* log only: this frame's inputs as they are handed over, beside the adapter's own line of its dispatch */
    if (v->trace)
        LOG(ST "handed over, frame %d of the inputs: clip offset %.9g %.9g, jitter %.9g %.9g px (the variant's "
               "factors %g %g), vector scale %.9g %.9g, rectangle %ux%u at %d,%d, reset %u, frame %.9g s, near "
               "%.9g, far %.9g, fov %.9g, context flags 0x%x. Images as width x height, format, layout: colour "
               "%ux%u %d %d, depth %ux%u %d %d, vectors %ux%u %d %d, output %ux%u %d %d\n",
            i, in->scene[i].jitter[0], in->scene[i].jitter[1], (double)frame.jitter[0], (double)frame.jitter[1],
            v->jitter_x, v->jitter_y, (double)frame.vector_scale[0], (double)frame.vector_scale[1], frame.render_w,
            frame.render_h, frame.render_x, frame.render_y, frame.reset, (double)frame.frame_seconds,
            (double)frame.near_plane, (double)frame.far_plane, (double)frame.fov_y, flags, frame.colour.w,
            frame.colour.h, (int)frame.colour.format, (int)frame.colour.layout, frame.depth.w, frame.depth.h,
            (int)frame.depth.format, (int)frame.depth.layout, frame.motion.w, frame.motion.h,
            (int)frame.motion.format, (int)frame.motion.layout, frame.output.w, frame.output.h,
            (int)frame.output.format, (int)frame.output.layout);
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
    if (g.record && i < g.record_n) {
        handed.done = 1;
        g.record[i] = handed;
    }
    if (in->ungated)
        s->wrote = s->frame;
    else
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

/* For the log alone: a picture's error against the true one by region (the background, inside the patch, the
   band the patch left) and in each by channel. A region without pixels is left out. */
static void errors_log(const char* name, const Inputs* in, const double* picture, const double* reference,
                       const u8* regions) {
    static const char* const names[3] = {"background", "patch", "band the patch left"};
    static const int which[3] = {SELFTEST_BACKGROUND, SELFTEST_INSIDE, SELFTEST_UNCOVERED};
    char line[320];
    size_t at = 0;
    line[0] = 0;
    for (int r = 0; r < 3; r++) {
        double sum[3] = {0.0, 0.0, 0.0};
        size_t n = 0;
        for (size_t p = 0; p < (size_t)in->ow * in->oh; p++) {
            if (regions[p] != which[r]) continue;
            for (int c = 0; c < 3; c++) sum[c] += fabs(picture[3 * p + c] - reference[3 * p + c]);
            n++;
        }
        if (n && at < sizeof line)
            at += (size_t)snprintf(line + at, sizeof line - at, "%s%s (%zu pixels) r %.6f g %.6f b %.6f",
                                   at ? ", " : "", names[r], n, sum[0] / (double)n, sum[1] / (double)n,
                                   sum[2] / (double)n);
    }
    LOG(ST "%s, logged only, error by region and channel: %s\n", name, line);
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
    int needs[16];
    int n = 0, own = -1, ran = 1;
    for (unsigned combo = 0; combo < 16 && ran; combo++) {
        if (combo & ~mask) continue;
        const Variant v = {combo & 1 ? -1 : 1, combo & 2 ? -1 : 1, combo & 4 ? -1 : 1, combo & 8 ? -1 : 1, 1};
        ran = run(in, &v, 0, output, NULL, NULL, NULL, NULL);
        errors[n] = ran ? difference(in, output, reference, regions, region) : NAN;
        LOG(ST "%s: jitter %c%c vector %c%c error %.6f%s\n", name, v.jitter_x < 0 ? '-' : '+',
            v.jitter_y < 0 ? '-' : '+', v.vector_x < 0 ? '-' : '+', v.vector_y < 0 ? '-' : '+', errors[n],
            combo ? "" : " (configured)");
        if (ran) {
            char variant[96];
            snprintf(variant, sizeof variant, "%s: jitter %c%c vector %c%c", name, v.jitter_x < 0 ? '-' : '+',
                     v.jitter_y < 0 ? '-' : '+', v.vector_x < 0 ? '-' : '+', v.vector_y < 0 ? '-' : '+');
            errors_log(variant, in, output, reference, regions);
        }
        if (!combo) own = n;
        needs[n] = combo && !(combo & 3); /* turns vector signs and leaves the jitter's as configured */
        n++;
    }
    /* A case that turns the jitter's signs asks only that the configured ones are strictly the lowest, and the
       margin of the variants that turn vector signs alone: the jitter's margin is the edge case's. A case of
       vector signs alone asks the margin of every variant, as it did. */
    const int jitter_too = (mask & 3) != 0;
    const int verdict = !ran         ? SELFTEST_FAIL
                        : jitter_too ? selftest_signs(errors, needs, n, own, SELFTEST_MARGIN)
                                     : selftest_verdict(errors, n, own, SELFTEST_MARGIN);
    if (jitter_too)
        LOG(ST "%s: %d variants, the configured one strictly the lowest, and %.2f times it at least for those "
               "that turn vector signs only: %s\n", name, n, SELFTEST_MARGIN, verdict_name(verdict));
    else LOG(ST "%s: %d variants, margin %.2f: %s\n", name, n, SELFTEST_MARGIN, verdict_name(verdict));
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
        /* 320x180 at 40,30 of 400x240 is no shape of the game's, whose gate would refuse it every frame */
        LOG(ST "case 6 %s: both runs are handed to the conversion and the adapter without the game's rectangle "
               "gate\n", up ? "up" : "down");
        in.ungated = 1;
        ok = inputs_make(&tight, SIZES, RENDER_W, RENDER_H, 0, 0, up, pan, 0, 0);
        tight.ungated = 1;
        ok = ok &&
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
    Inputs small, big, wrong;
    Session s;
    double *output = malloc(size), *previous = malloc(size), *reference = malloc(size), *scaled = malloc(size);
    double *fresh = malloc(size), *again = malloc(size), *control = malloc(size);
    u8* regions = malloc((size_t)BIG[2] * BIG[3]);
    SelftestSettling settling = {{0.0, 0.0}, {0, 0}};
    int evaluated = 0;
    /* everything starts empty, and only what was made is freed */
    memset(&small, 0, sizeof small);
    memset(&big, 0, sizeof big);
    memset(&wrong, 0, sizeof wrong);
    memset(&s, 0, sizeof s);
    int ok = !not_run("case 4", up) && output && previous && reference && scaled && regions && fresh && again &&
             control &&
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
    /* The picture after the change against a fresh context's, given the same frames at the new sizes: the same
       offsets in the same phase, the reset on its first frame, the same configuration. A second fresh context
       gives the noise. The control is a fresh context whose images are one offset ahead of the jitter reported
       for them: it must differ, or the comparison could not have failed (selftest_resized). */
    int same = SELFTEST_BLIND;
    if (ok) {
        const int ran = run(&big, &CONFIGURED, 0, fresh, NULL, NULL, NULL, NULL) &&
                        run(&big, &CONFIGURED, 0, again, NULL, NULL, NULL, NULL) &&
                        inputs_build(&wrong, BIG, BIG[0], BIG[1], 0, 0, up, still, 0, 0, 0, 1, 0) &&
                        run(&wrong, &CONFIGURED, 0, control, NULL, NULL, NULL, NULL);
        const double apart = ran ? difference(&big, output, fresh, regions, SELFTEST_BACKGROUND) : NAN;
        const double noise = ran ? difference(&big, fresh, again, regions, SELFTEST_BACKGROUND) : NAN;
        const double off = ran ? difference(&big, control, fresh, regions, SELFTEST_BACKGROUND) : NAN;
        same = ran ? selftest_resized(apart, noise, off) : SELFTEST_FAIL;
        LOG(ST "case 4 %s: after the change, the picture against a fresh context's on the same frames %.6f (at "
               "most %.3f). Two fresh contexts against each other %.6f. The control, images one offset ahead of "
               "the reported jitter, against the fresh one %.6f (must be over %.3f and over the noise): %s\n",
            up ? "up" : "down", apart, SELFTEST_RESIZED, noise, off, SELFTEST_RESIZED,
            !ran                     ? "FAIL (a run could not be made)"
            : same == SELFTEST_PASS  ? "PASS"
            : same == SELFTEST_BLIND ? "FAIL (the control did not differ enough, or a number is not one: nothing "
                                       "is decided)"
                                     : "FAIL");
    }
    inputs_free(&small);
    inputs_free(&big);
    inputs_free(&wrong);
    free(fresh);
    free(again);
    free(control);
    free(output);
    free(previous);
    free(reference);
    free(scaled);
    free(regions);
    return counted && beats && settles && same == SELFTEST_PASS;
}

/* The native suite: the render size is the output's size, so 8 offsets, as the game at full size has it. A reset,
   a changed create property and a changed size are each held by selftest_resized: the picture after the change
   against a fresh context's on the same frames, the noise of two fresh contexts, and a control that is wrong on
   purpose. docs/arceus-temporal.md has the three cases. The error against the true picture is only logged. */
#define NATIVE_W 480
#define NATIVE_H 270
static const char* native_word(int ran, int verdict) {
    return !ran                       ? "FAIL (a run could not be made)"
           : verdict == SELFTEST_PASS ? "PASS"
           : verdict == SELFTEST_BLIND ? "FAIL (the control did not differ enough, or a number is not one: nothing "
                                         "is decided)"
                                       : "FAIL";
}
static int native(int up) {
    static const int SIZE[4] = {NATIVE_W, NATIVE_H, NATIVE_W, NATIVE_H}, SMALL[4] = {320, 180, 320, 180};
    static const Variant without_hdr = {1, 1, 1, 1, 1, 0, 1};
    static const double still[2] = {0.0, 0.0};
    const size_t pixels = (size_t)NATIVE_W * NATIVE_H, size = sizeof(double) * 3 * pixels;
    const char* way = up ? "up" : "down";
    SnxUpscalerTraits traits = {.colour_usage = VK_IMAGE_USAGE_SAMPLED_BIT,
                                .depth_usage = VK_IMAGE_USAGE_SAMPLED_BIT,
                                .motion_usage = VK_IMAGE_USAGE_SAMPLED_BIT,
                                .output_usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT};
    enum { FRESH, AGAIN, AHEAD, OLD, CHANGED, RESIZED, WITH, WITHOUT, ALONE, ALONE_AGAIN, PLAIN, PICTURES };
    double* picture[PICTURES] = {0};
    double* reference = malloc(size);
    u8* regions = malloc(pixels);
    Inputs in, ahead, cut, small;
    Session s;
    int pass = 1;
    memset(&in, 0, sizeof in);
    memset(&ahead, 0, sizeof ahead);
    memset(&cut, 0, sizeof cut);
    memset(&small, 0, sizeof small);
    memset(&s, 0, sizeof s);
    int ok = !not_run("native suite", up) && reference && regions;
    for (int k = 0; k < PICTURES; k++) ok = (picture[k] = malloc(size)) != NULL && ok;
    ok = ok && inputs_make(&in, SIZE, NATIVE_W, NATIVE_H, 0, 0, up, still, 0, 0) &&
         inputs_build(&ahead, SIZE, NATIVE_W, NATIVE_H, 0, 0, up, still, 0, 0, 0, 1, 0) &&
         inputs_make(&cut, SIZE, NATIVE_W, NATIVE_H, 0, 0, up, still, 0, 1) &&
         inputs_make(&small, SMALL, 320, 180, 0, 0, up, still, 0, 0);
    if (!ok) {
        LOG(ST "native suite %s: could not be made: FAIL\n", way);
        pass = 0;
    } else {
        LOG(ST "native suite %s: render and output %dx%d, %d offsets, %d frames a run\n", way, NATIVE_W, NATIVE_H,
            in.offsets, in.frames);
        truth(&in, up, reference, regions);
        /* the fresh context, its repeat for the noise, the control one offset ahead, and the old property alone */
        double first = NAN, last = NAN;
        const int ran = run(&in, &CONFIGURED, 0, picture[FRESH], &first, &last, regions, NULL) &&
                        run(&in, &CONFIGURED, 0, picture[AGAIN], NULL, NULL, NULL, NULL) &&
                        run(&ahead, &CONFIGURED, 0, picture[AHEAD], NULL, NULL, NULL, NULL) &&
                        run(&in, &without_hdr, 0, picture[OLD], NULL, NULL, NULL, NULL);
        const double noise =
            ran ? difference(&in, picture[FRESH], picture[AGAIN], regions, SELFTEST_BACKGROUND) : NAN;
        const double off = ran ? difference(&in, picture[AHEAD], picture[FRESH], regions, SELFTEST_BACKGROUND) : NAN;
        if (ran) {
            plain(&in, in.frames - 1, up, picture[PLAIN]);
            LOG(ST "native suite %s: logged only, no margin: error against the true picture %.6f, of one frame "
                   "scaled plainly %.6f. Change a frame over the first cycle %.6f, over the last %.6f. Two fresh "
                   "contexts differ by %.6f, a context made without the flag for colours over 1 from one made "
                   "with it by %.6f\n",
                way, difference(&in, picture[FRESH], reference, regions, SELFTEST_BACKGROUND),
                difference(&in, picture[PLAIN], reference, regions, SELFTEST_BACKGROUND), first, last, noise,
                difference(&in, picture[OLD], picture[FRESH], regions, SELFTEST_BACKGROUND));
        }
        /* a change of a create property: the session's first frames without the flag, then every frame a fresh
           run hands over (all but the gate's first) with it, on a context made anew */
        int evaluated = 0, changed = ran && session_open(&s, &in, &traits);
        for (int i = 0; changed && i < 9; i++)
            changed = frame_run(&s, &in, i, &without_hdr, 0, picture[CHANGED]) >= 0;
        const unsigned before = changed ? s.evaluated : 0;
        for (int i = 1; changed && i < in.frames; i++) {
            const int r = frame_run(&s, &in, i, &CONFIGURED, 0, picture[CHANGED]);
            changed = r >= 0;
            evaluated += r > 0;
        }
        /* the property really changed: two contexts came into use, the first made without the flag and the
           second with it, and the picture compared is the second's, against a fresh context made with it */
        const int remade = s.published == 2 && !(s.made_flags[0] & SNX_UPSCALER_COLOUR_LINEAR_HDR) &&
                           (s.made_flags[1] & SNX_UPSCALER_COLOUR_LINEAR_HDR);
        const int counted = changed && before == 8 && evaluated == in.frames - 1 && remade;
        const unsigned contexts = s.published;
        const u32 flags_before = s.made_flags[0], flags_after = s.made_flags[1];
        session_close(&s);
        const double apart =
            changed ? difference(&in, picture[CHANGED], picture[FRESH], regions, SELFTEST_BACKGROUND) : NAN;
        int verdict = changed ? selftest_resized(apart, noise, off) : SELFTEST_FAIL;
        LOG(ST "native suite %s, a create property changed: %u frames evaluated without the flag, %d with it, %u "
               "contexts made with the flags 0x%x and then 0x%x: %s. The remade context's picture against a fresh "
               "context's made with the flag, on the same frames %.6f (at most %.3f), two fresh contexts %.6f, the "
               "control one offset ahead %.6f (over %.3f and over the noise): %s\n",
            way, before, evaluated, contexts, flags_before, flags_after, counted ? "PASS" : "FAIL", apart,
            SELFTEST_RESIZED, noise, off, SELFTEST_RESIZED, native_word(changed, verdict));
        pass &= counted && verdict == SELFTEST_PASS;
        /* a change of size, on one session: the small size's frames, then the native ones */
        memset(&s, 0, sizeof s);
        evaluated = 0;
        int resized = ran && session_open(&s, &small, &traits);
        for (int i = 0; resized && i < 9; i++)
            resized = frame_run(&s, &small, i, &CONFIGURED, 0, picture[RESIZED]) >= 0;
        const unsigned small_frames = resized ? s.evaluated : 0;
        resized = resized && pictures_make(&s, &in, &traits);
        for (int i = 0; resized && i < in.frames; i++) {
            const int r = frame_run(&s, &in, i, &CONFIGURED, 0, picture[RESIZED]);
            resized = r >= 0;
            evaluated += r > 0;
        }
        const int sized = resized && small_frames == 8 && evaluated == in.frames - 1 && s.published == 2;
        const unsigned made = s.published;
        session_close(&s);
        const double moved =
            resized ? difference(&in, picture[RESIZED], picture[FRESH], regions, SELFTEST_BACKGROUND) : NAN;
        verdict = resized ? selftest_resized(moved, noise, off) : SELFTEST_FAIL;
        LOG(ST "native suite %s, the size changed: %u frames evaluated at 320x180, %d at %dx%d, %u contexts made: "
               "%s. The picture against a fresh context's on the same frames %.6f (at most %.3f), two fresh "
               "contexts %.6f, the control one offset ahead %.6f: %s\n",
            way, small_frames, evaluated, NATIVE_W, NATIVE_H, made, sized ? "PASS" : "FAIL", moved, SELFTEST_RESIZED,
            noise, off, native_word(resized, verdict));
        pass &= sized && verdict == SELFTEST_PASS;
        /* a reset: the last frame shows the other pattern. With the flag, without it, and a context that is
           given the gate's first frame and that frame alone, twice. */
        Inputs alone = cut;
        const size_t skip = (size_t)cut.frames - 2;
        alone.scene += skip;
        alone.colour += skip * cut.w * cut.h;
        alone.depth += skip * cut.w * cut.h;
        alone.motion += skip * cut.w * cut.h * 4;
        alone.frames = 2;
        alone.cut_at = -1;
        u8* everywhere = malloc(pixels);
        const int reset = everywhere && run(&cut, &CONFIGURED, 1, picture[WITH], NULL, NULL, NULL, NULL) &&
                          run(&cut, &CONFIGURED, 0, picture[WITHOUT], NULL, NULL, NULL, NULL) &&
                          run(&alone, &CONFIGURED, 0, picture[ALONE], NULL, NULL, NULL, NULL) &&
                          run(&alone, &CONFIGURED, 0, picture[ALONE_AGAIN], NULL, NULL, NULL, NULL);
        if (everywhere) memset(everywhere, SELFTEST_BACKGROUND, pixels);
        const double same =
            reset ? difference(&cut, picture[WITH], picture[ALONE], everywhere, SELFTEST_BACKGROUND) : NAN;
        const double twice =
            reset ? difference(&cut, picture[ALONE], picture[ALONE_AGAIN], everywhere, SELFTEST_BACKGROUND) : NAN;
        const double kept =
            reset ? difference(&cut, picture[WITHOUT], picture[ALONE], everywhere, SELFTEST_BACKGROUND) : NAN;
        verdict = reset ? selftest_resized(same, twice, kept) : SELFTEST_FAIL;
        LOG(ST "native suite %s, a reset: the reset frame against a fresh context's %.6f (at most %.3f), two fresh "
               "contexts %.6f, the control without the flag %.6f (over %.3f and over the noise): %s\n",
            way, same, SELFTEST_RESIZED, twice, kept, SELFTEST_RESIZED, native_word(reset, verdict));
        pass &= verdict == SELFTEST_PASS;
        free(everywhere);
    }
    inputs_free(&in);
    inputs_free(&ahead);
    inputs_free(&cut);
    inputs_free(&small);
    for (int k = 0; k < PICTURES; k++) free(picture[k]);
    free(reference);
    free(regions);
    return pass;
}

/* The edge case, for the jitter's signs, on a still grey picture with two edges. Seven pictures of the same frozen
   images, each run a session of its own: the configured signs twice (for the noise), the three other combinations,
   the jitter reported as zero, and one frame scaled plainly. Each is held against the reference by its edges'
   profiles and over the same pixels. selftest_edge_judge has the conditions and the budgets. */
static const char* edge_failed(unsigned failed, char* text, size_t size) {
    static const char* const names[8] = {"the order of the errors", "a turned sign's margin",
                                         "told nothing's margin",   "the plainly scaled frame",
                                         "settling",                "both signs turned",
                                         "the noise",               "a number that is not one"};
    size_t at = 0;
    text[0] = 0;
    for (int bit = 0; bit < 8; bit++)
        if ((failed >> bit & 1u) && at < size)
            at += (size_t)snprintf(text + at, size - at, "%s%s", at ? ", " : "", names[bit]);
    return text;
}
static int edge_case(int up) {
    enum { OWN, AGAIN, TURNED_X, TURNED_Y, TURNED_BOTH, TOLD_NOTHING, PLAIN, PICTURES };
    static const char* const names[PICTURES] = {"configured",        "configured again", "jitter x turned",
                                                "jitter y turned",   "both turned",      "told nothing",
                                                "one frame plainly"};
    /* the two configured runs log every frame's inputs and dispatch: what FSR is given is to be known before
       anything is reasoned about what it does with it */
    static const Variant variants[PLAIN] = {{1, 1, 1, 1, 1, 1},  {1, 1, 1, 1, 1, 1},   {-1, 1, 1, 1, 1, 0},
                                            {1, -1, 1, 1, 1, 0}, {-1, -1, 1, 1, 1, 0}, {0, 0, 1, 1, 1, 0}};
    static const double still[2] = {0.0, 0.0};
    const SelftestEdge* edges[2] = {&SELFTEST_EDGE_V, &SELFTEST_EDGE_H};
    const size_t pixels = (size_t)OUT_W * OUT_H, size = sizeof(double) * 3 * pixels;
    const size_t texels = (size_t)RENDER_W * RENDER_H;
    const char* way = up ? "up" : "down";
    double* picture[PICTURES] = {0};
    double* reference = malloc(size);
    u8* regions = malloc(pixels);
    SelftestProfile profile[PICTURES][2], truth_profile[2];
    SelftestEdgeMeasure measure[PICTURES][2];
    SelftestEdgeRuns runs;
    double first = NAN, last = NAN;
    Inputs in;
    char text[256];
    int ok = !not_run("edge case", up) && reference && regions;
    memset(&in, 0, sizeof in);
    for (int k = 0; k < PICTURES; k++) ok = (picture[k] = malloc(size)) != NULL && ok;
    ok = ok && inputs_build(&in, SIZES, RENDER_W, RENDER_H, 0, 0, up, still, 0, 0, 1, 0, 0);
    if (ok) {
        /* the images are the ones named before any adapter was looked for, or the case does not run */
        const u64 sums[3] = {selftest_checksum(in.colour, (size_t)in.frames * texels * 4, SELFTEST_CHECKSUM_START),
                             selftest_checksum(in.depth, (size_t)in.frames * texels * 4, SELFTEST_CHECKSUM_START),
                             selftest_checksum(in.motion, (size_t)in.frames * texels * 8, SELFTEST_CHECKSUM_START)};
        ok = !memcmp(sums, g.edge_sums[up], sizeof sums);
        if (!ok) LOG(ST "edge case %s: the images are not the ones whose checksums were logged: FAIL\n", way);
    }
    if (ok) truth(&in, up, reference, regions);
    for (int k = 0; ok && k < PLAIN; k++)
        ok = run(&in, &variants[k], 0, picture[k], k == OWN ? &first : NULL, k == OWN ? &last : NULL,
                 k == OWN ? regions : NULL, NULL);
    if (ok) plain(&in, in.frames - 1, up, picture[PLAIN]);
    if (!ok) {
        LOG(ST "edge case %s: not every run could be made: FAIL\n", way);
    } else {
        for (int e = 0; e < 2; e++) {
            selftest_profile_make(edges[e], reference, OUT_W, &truth_profile[e]);
            for (int k = 0; k < PICTURES; k++) {
                selftest_profile_make(edges[e], picture[k], OUT_W, &profile[k][e]);
                measure[k][e].profile = selftest_profile_apart(&profile[k][e], &truth_profile[e]);
                measure[k][e].pixels = selftest_edge_apart(edges[e], picture[k], reference, OUT_W);
            }
            runs.configured[e] = measure[OWN][e];
            runs.turned_x[e] = measure[TURNED_X][e];
            runs.turned_y[e] = measure[TURNED_Y][e];
            runs.turned_both[e] = measure[TURNED_BOTH][e];
            runs.told_nothing[e] = measure[TOLD_NOTHING][e];
            runs.plain[e] = measure[PLAIN][e];
            /* the noise is between the two configured pictures themselves, not between their errors */
            runs.noise[e].profile = selftest_profile_apart(&profile[OWN][e], &profile[AGAIN][e]);
            runs.noise[e].pixels = selftest_edge_apart(edges[e], picture[OWN], picture[AGAIN], OUT_W);
        }
        runs.settled = selftest_settles(first, last);
        /* Each picture on each edge: the two measures that are judged, then for the log alone how far its edge
           sits from the reference's, how wide its rise is and how far it overshoots, and its error as a multiple
           of the configured run's. */
        for (int k = 0; k < PICTURES; k++)
            for (int e = 0; e < 2; e++)
                LOG(ST "edge case %s %s, %s edge: profile error %.6f, per pixel %.6f. Logged only: edge moved %.3f px, "
                       "rise %.3f px (reference %.3f), overshoot %.4f, %.2f times the configured profile error\n",
                    way, names[k], e ? "near-horizontal" : "near-vertical", measure[k][e].profile,
                    measure[k][e].pixels,
                    selftest_profile_crossing(&profile[k][e], 0.5) - selftest_profile_crossing(&truth_profile[e], 0.5),
                    selftest_profile_rise(&profile[k][e]), selftest_profile_rise(&truth_profile[e]),
                    selftest_profile_overshoot(&profile[k][e]), measure[k][e].profile / measure[OWN][e].profile);
        for (int k = 0; k < PICTURES; k++)
            for (int e = 0; e < 2; e++) {
                double by[3];
                selftest_edge_channels(edges[e], picture[k], reference, OUT_W, by);
                LOG(ST "edge case %s %s, %s edge, logged only, error by channel: r %.6f g %.6f b %.6f\n", way,
                    names[k], e ? "near-horizontal" : "near-vertical", by[0], by[1], by[2]);
            }
        for (int e = 0; e < 2; e++)
            LOG(ST "edge case %s noise, %s edge: between the two configured runs the profiles differ by %.6f and the "
                   "pictures by %.6f per pixel\n",
                way, e ? "near-horizontal" : "near-vertical", runs.noise[e].profile, runs.noise[e].pixels);
        const unsigned failed = selftest_edge_judge(&runs);
        ok = !failed;
        LOG(ST "edge case %s: change a frame over the first cycle %.6f, over the last %.6f. A turned sign at least "
               "%.3f worse on its edge, told nothing at least %.3f, each at least %.0f times the noise, on the "
               "profile and per pixel: %s%s\n",
            way, first, last, SELFTEST_EDGE_TURNED, SELFTEST_EDGE_TOLD, SELFTEST_EDGE_NOISE,
            !failed ? "PASS" : selftest_edge_undecided(failed) ? "FAIL (the case decided nothing): " : "FAIL: ",
            edge_failed(failed, text, sizeof text));
    }
    inputs_free(&in);
    for (int k = 0; k < PICTURES; k++) free(picture[k]);
    free(reference);
    free(regions);
    return ok;
}

/* Log only, no verdict: the matched pair. The edge images with y down, and with y up and the clip offset's y
   negated, are the same bytes with the same jitter in pixels, so both orientations hand the adapter identical
   inputs. The images and what each frame handed over are compared, and the two outputs' and a repeated run's
   differences printed. Whether the outputs count as equal is not said: no number is declared for that. */
static void edge_matched(void) {
    static const Variant traced = {1, 1, 1, 1, 1, 1};
    static const double still[2] = {0.0, 0.0};
    const SelftestEdge* edges[2] = {&SELFTEST_EDGE_V, &SELFTEST_EDGE_H};
    const size_t pixels = (size_t)OUT_W * OUT_H, size = sizeof(double) * 3 * pixels;
    const size_t texels = (size_t)RENDER_W * RENDER_H;
    Inputs down, up;
    double *reference = malloc(size), *picture[3] = {malloc(size), malloc(size), malloc(size)};
    u8* regions = malloc(pixels);
    SelftestHanded* handed[2] = {NULL, NULL};
    memset(&down, 0, sizeof down);
    memset(&up, 0, sizeof up);
    int ok = !g.unknown && reference && regions && picture[0] && picture[1] && picture[2] &&
             inputs_build(&down, SIZES, RENDER_W, RENDER_H, 0, 0, 0, still, 0, 0, 1, 0, 0) &&
             inputs_build(&up, SIZES, RENDER_W, RENDER_H, 0, 0, 1, still, 0, 0, 1, 0, 1);
    if (ok) {
        handed[0] = calloc((size_t)down.frames, sizeof **handed);
        handed[1] = calloc((size_t)down.frames, sizeof **handed);
        ok = handed[0] && handed[1] && up.frames == down.frames;
    }
    if (!ok) {
        LOG(ST "matched pair, log only: could not be made\n");
    } else {
        const size_t bytes[3] = {(size_t)down.frames * texels * 4, (size_t)down.frames * texels * 4,
                                 (size_t)down.frames * texels * 8};
        const void* images[2][3] = {{down.colour, down.depth, down.motion}, {up.colour, up.depth, up.motion}};
        u64 sums[2][3];
        int equal = 1;
        for (int k = 0; k < 3; k++) {
            for (int which = 0; which < 2; which++)
                sums[which][k] = selftest_checksum(images[which][k], bytes[k], SELFTEST_CHECKSUM_START);
            equal = equal && !memcmp(images[0][k], images[1][k], bytes[k]);
        }
        LOG(ST "matched pair, log only: %d frames. y down: colour %016llx, depth %016llx, vectors %016llx. y up with "
               "the clip offset's y negated: colour %016llx, depth %016llx, vectors %016llx. Byte for byte the "
               "same: %s\n",
            down.frames, (unsigned long long)sums[0][0], (unsigned long long)sums[0][1],
            (unsigned long long)sums[0][2], (unsigned long long)sums[1][0], (unsigned long long)sums[1][1],
            (unsigned long long)sums[1][2], equal ? "yes" : "NO, the pair is not matched and is not run");
        if (equal) {
            truth(&down, 0, reference, regions);
            g.record_n = down.frames;
            g.record = handed[0];
            const int ran_down = run(&down, &traced, 0, picture[0], NULL, NULL, NULL, NULL);
            g.record = handed[1];
            const int ran_up = run(&up, &traced, 0, picture[1], NULL, NULL, NULL, NULL);
            g.record = NULL;
            /* A run hands over every frame of its inputs but the first, which the gate does not let through. The
               outputs are held against each other only when both runs succeeded, each handed over all of those
               frames, and what they handed over is the same (selftest_matched). */
            int first = -1, done[2] = {0, 0};
            const int whole = down.frames - 1;
            const int match = selftest_matched(handed[0], handed[1], down.frames, whole, ran_down, ran_up, &first);
            for (int i = 0; i < down.frames; i++) {
                done[0] += handed[0][i].done != 0;
                done[1] += handed[1][i].done != 0;
            }
            if (match == SELFTEST_MATCHED)
                LOG(ST "matched pair, log only: both runs succeeded and handed the adapter the same in %d of %d "
                       "frames each, the first being the gate's (jitter, vector scale, frame time, near, far, field "
                       "of view, the rectangle's origin and size, reset, flags, the context's sizes, each image's "
                       "size, format and layout)\n", whole, down.frames);
            else if (match == SELFTEST_MATCH_FAILED)
                LOG(ST "matched pair, log only: a run did not succeed (y down %s, y up %s, %d and %d frames done of "
                       "%d): the outputs are not compared\n", ran_down ? "ran" : "FAILED", ran_up ? "ran" : "FAILED",
                    done[0], done[1], whole);
            else if (match == SELFTEST_MATCH_SHORT)
                LOG(ST "matched pair, log only: the runs did not hand over all frames (%d and %d done of %d, first "
                       "frame done in one run only: %d): the outputs are not compared\n", done[0], done[1], whole,
                    first);
            else
                LOG(ST "matched pair, log only: what was handed to the adapter differs first in frame %d (jitter "
                       "%.9g %.9g against %.9g %.9g, reset %u against %u): the outputs are not compared\n",
                    first, (double)handed[0][first].f[0], (double)handed[0][first].f[1],
                    (double)handed[1][first].f[0], (double)handed[1][first].f[1], handed[0][first].u[4],
                    handed[1][first].u[4]);
            if (match == SELFTEST_MATCHED && run(&down, &CONFIGURED, 0, picture[2], NULL, NULL, NULL, NULL)) {
                LOG(ST "matched pair, log only, not judged: the two outputs differ by %.9f over the picture, a "
                       "repeated run of the first by %.9f\n",
                    difference(&down, picture[0], picture[1], regions, SELFTEST_BACKGROUND),
                    difference(&down, picture[0], picture[2], regions, SELFTEST_BACKGROUND));
                for (int e = 0; e < 2; e++) {
                    SelftestProfile p[3];
                    for (int k = 0; k < 3; k++) selftest_profile_make(edges[e], picture[k], OUT_W, &p[k]);
                    LOG(ST "matched pair, log only, not judged, %s edge: profiles differ by %.9f and pictures by "
                           "%.9f per pixel between the two orientations, by %.9f and %.9f in the repeated run\n",
                        e ? "near-horizontal" : "near-vertical", selftest_profile_apart(&p[0], &p[1]),
                        selftest_edge_apart(edges[e], picture[0], picture[1], OUT_W),
                        selftest_profile_apart(&p[0], &p[2]),
                        selftest_edge_apart(edges[e], picture[0], picture[2], OUT_W));
                }
            } else if (match == SELFTEST_MATCHED)
                LOG(ST "matched pair, log only: the repeated run could not be made: the outputs are not compared\n");
        }
    }
    inputs_free(&down);
    inputs_free(&up);
    free(handed[0]);
    free(handed[1]);
    for (int k = 0; k < 3; k++) free(picture[k]);
    free(reference);
    free(regions);
}

/* Log only, exploratory, no verdict: the response table. For one axis at a time the jitter reported for it is
   multiplied by a factor, the images and the other axis unchanged: 1 is configured, 0 told nothing, -1 the sign
   turned. Each run's error on both edges is printed. A lowest error among these factors on one picture does not
   name the factor a library wants: the table is for looking, and nothing is tuned or judged from it. */
static void edge_response(int up) {
    static const double factors[11] = {-1.0, -0.5, 0.0, 0.5, 9.0 / 16.0, 0.75, 1.0, 1.25, 1.5, 16.0 / 9.0, 2.0};
    static const double still[2] = {0.0, 0.0};
    const SelftestEdge* edges[2] = {&SELFTEST_EDGE_V, &SELFTEST_EDGE_H};
    const size_t pixels = (size_t)OUT_W * OUT_H, size = sizeof(double) * 3 * pixels;
    const char* way = up ? "up" : "down";
    double *reference = malloc(size), *picture = malloc(size);
    u8* regions = malloc(pixels);
    SelftestProfile truth_profile[2];
    Inputs in;
    memset(&in, 0, sizeof in);
    int ok = !g.unknown && reference && picture && regions &&
             inputs_build(&in, SIZES, RENDER_W, RENDER_H, 0, 0, up, still, 0, 0, 1, 0, 0);
    if (ok) {
        truth(&in, up, reference, regions);
        for (int e = 0; e < 2; e++) selftest_profile_make(edges[e], reference, OUT_W, &truth_profile[e]);
    }
    for (int axis = 0; ok && axis < 2; axis++)
        for (int k = 0; ok && k < 11; k++) {
            const Variant v = {axis ? 1.0 : factors[k], axis ? factors[k] : 1.0, 1, 1, 1, 0};
            double profile[2], per_pixel[2];
            ok = run(&in, &v, 0, picture, NULL, NULL, NULL, NULL);
            if (!ok) break;
            for (int e = 0; e < 2; e++) {
                SelftestProfile p;
                selftest_profile_make(edges[e], picture, OUT_W, &p);
                profile[e] = selftest_profile_apart(&p, &truth_profile[e]);
                per_pixel[e] = selftest_edge_apart(edges[e], picture, reference, OUT_W);
            }
            /* the edge across the axis first: the near-vertical one for x, the near-horizontal one for y */
            LOG(ST "response table %s, exploratory, not judged: reported jitter %c times %.4f: edge across it profile "
                   "error %.6f, per pixel %.6f. The other edge %.6f, %.6f\n",
                way, axis ? 'y' : 'x', factors[k], profile[axis], per_pixel[axis], profile[!axis], per_pixel[!axis]);
        }
    if (!ok) LOG(ST "response table %s, exploratory: could not be made or finished\n", way);
    inputs_free(&in);
    free(reference);
    free(picture);
    free(regions);
}

/* The edge case's images, made and named by their checksums before any adapter is looked for: they depend on no
   library. The checksums are kept, and the case holds the images it runs on against them. The reference's two
   profiles must be whole, or the case could compare nothing. 1 when all of it is in order. */
static int edges_announce(void) {
    static const double still[2] = {0.0, 0.0};
    int ok = 1;
    for (int up = 1; up >= 0 && ok; up--) {
        Inputs in;
        const size_t pixels = (size_t)RENDER_W * RENDER_H;
        ok = inputs_build(&in, SIZES, RENDER_W, RENDER_H, 0, 0, up, still, 0, 0, 1, 0, 0);
        double* reference = ok ? malloc(sizeof(double) * 3 * OUT_W * OUT_H) : NULL;
        if (!reference) {
            LOG(ST "edge case %s: the images could not be made: FAIL\n", up ? "up" : "down");
            inputs_free(&in);
            return 0;
        }
        const SelftestViewport out = {0, up ? OUT_H : 0, OUT_W, up ? -OUT_H : OUT_H};
        for (int y = 0; y < OUT_H; y++)
            for (int x = 0; x < OUT_W; x++)
                selftest_reference(&in.scene[0], &out, x, y, SAMPLES, &reference[3 * ((size_t)y * OUT_W + x)]);
        SelftestProfile v, h;
        selftest_profile_make(&SELFTEST_EDGE_V, reference, OUT_W, &v);
        selftest_profile_make(&SELFTEST_EDGE_H, reference, OUT_W, &h);
        g.edge_sums[up][0] = selftest_checksum(in.colour, (size_t)in.frames * pixels * 4, SELFTEST_CHECKSUM_START);
        g.edge_sums[up][1] = selftest_checksum(in.depth, (size_t)in.frames * pixels * 4, SELFTEST_CHECKSUM_START);
        g.edge_sums[up][2] = selftest_checksum(in.motion, (size_t)in.frames * pixels * 8, SELFTEST_CHECKSUM_START);
        ok = selftest_profile_whole(&v) && selftest_profile_whole(&h);
        LOG(ST "edge case %s: %d frames of %dx%d made before any adapter. Checksums: colour %016llx, depth %016llx, "
               "vectors %016llx. The reference's two profiles hold pixels in every bin: %s\n",
            up ? "up" : "down", in.frames, RENDER_W, RENDER_H, (unsigned long long)g.edge_sums[up][0],
            (unsigned long long)g.edge_sums[up][1], (unsigned long long)g.edge_sums[up][2], ok ? "yes" : "NO: FAIL");
        free(reference);
        inputs_free(&in);
    }
    return ok;
}

/* The mode. The process's result: 0 when every case passed and the validation layer was there and silent. */
int vk_upscaler_selftest(const char* adapter) {
    int pass;
    LOG(ST "started for the adapter \"%s\". No game is loaded and no window is opened\n", adapter);
    if (!edges_announce()) return 1;
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
    g.generation++;
    pass &= native(1);
    g.generation++;
    pass &= native(0);
    g.generation++;
    pass &= edge_case(1);
    g.generation++;
    pass &= edge_case(0);
    /* log only from here: nothing below is a case, and none of it enters the result */
    g.generation++;
    edge_matched();
    g.generation++;
    edge_response(1);
    g.generation++;
    edge_response(0);
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
