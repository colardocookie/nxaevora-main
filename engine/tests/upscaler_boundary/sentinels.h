/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_TEST_UPSCALER_SENTINELS_H
#define SNX_TEST_UPSCALER_SENTINELS_H
/* The values a host and a stand-in module exchange through every call of the upscaler's table
   (tests/upscaler_boundary). Each member of each structure that crosses has a value of its own, so that a member
   read at a wrong place, or with a wrong size, is seen as that member. Read by a C host and a C++ module. */
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "nx/gpu/vk/upscaler.h"

#define SENTINEL_HANDLE(type, n) ((type)(uintptr_t)(0x5A00000000000000ull + (uint64_t)(n) * 0x0101010101ull))
#define SENTINEL_INSTANCE SENTINEL_HANDLE(VkInstance, 1)
#define SENTINEL_PHYSICAL SENTINEL_HANDLE(VkPhysicalDevice, 2)
#define SENTINEL_DEVICE SENTINEL_HANDLE(VkDevice, 3)
#define SENTINEL_QUEUE SENTINEL_HANDLE(VkQueue, 4)
#define SENTINEL_CB SENTINEL_HANDLE(VkCommandBuffer, 5)
#define SENTINEL_FOLDER "a folder of the host's, with a letter outside ASCII: \xc3\xa9"
#define SENTINEL_LOOKUP_INSTANCE "snx_sentinel_instance_function"
#define SENTINEL_LOOKUP_DEVICE "snx_sentinel_device_function"
#define SENTINEL_INSTANCE_EXTENSION_A "SNX_sentinel_instance_extension_a"
#define SENTINEL_INSTANCE_EXTENSION_B "SNX_sentinel_instance_extension_b"
#define SENTINEL_DEVICE_EXTENSION "SNX_sentinel_device_extension"
#define SENTINEL_CORE 0xC0DE0003u
#define SENTINEL_NAME "stand-in"
#define SENTINEL_DESTROYED 77 /* what prepare answers for a context that was destroyed */
#define SENTINEL_LOG_LINE "arceus-upscaler: stand-in: a line through the host's log, with \xc3\xa9"
static const unsigned char SENTINEL_FEATURES[16] = {0xF0, 0xE1, 0xD2, 0xC3, 0xB4, 0xA5, 0x96, 0x87,
                                                    0x78, 0x69, 0x5A, 0x4B, 0x3C, 0x2D, 0x1E, 0x0F};

static inline void sentinel_image(SnxUpscalerImage* image, int n) {
    image->image = SENTINEL_HANDLE(VkImage, 16 * n + 1);
    image->view = SENTINEL_HANDLE(VkImageView, 16 * n + 2);
    image->layout = (VkImageLayout)(1000 * n + 3);
    image->format = (VkFormat)(1000 * n + 4);
    image->w = (uint32_t)(1000 * n + 5);
    image->h = (uint32_t)(1000 * n + 6);
}
/* 0 when the image is the sentinel of that number, else 1 to 6 for the member that is not */
static inline int sentinel_image_wrong(const SnxUpscalerImage* image, int n) {
    if (image->image != SENTINEL_HANDLE(VkImage, 16 * n + 1)) return 1;
    if (image->view != SENTINEL_HANDLE(VkImageView, 16 * n + 2)) return 2;
    if ((int)image->layout != 1000 * n + 3) return 3;
    if ((int)image->format != 1000 * n + 4) return 4;
    if (image->w != (uint32_t)(1000 * n + 5)) return 5;
    if (image->h != (uint32_t)(1000 * n + 6)) return 6;
    return 0;
}

static inline void sentinel_create(SnxUpscalerCreate* c, PFN_vkGetInstanceProcAddr instance_lookup,
                                   PFN_vkGetDeviceProcAddr device_lookup) {
    memset(c, 0, sizeof *c);
    c->sizes.render_w = 11;
    c->sizes.render_h = 12;
    c->sizes.image_w = 13;
    c->sizes.image_h = 14;
    c->sizes.output_w = 15;
    c->sizes.output_h = 16;
    c->instance = SENTINEL_INSTANCE;
    c->physical_device = SENTINEL_PHYSICAL;
    c->device = SENTINEL_DEVICE;
    c->instance_lookup = instance_lookup;
    c->device_lookup = device_lookup;
    c->queue = SENTINEL_QUEUE;
    c->queue_family = 0x21u;
    c->flags = 0x80000022u;
    c->colour_format = (VkFormat)0x31;
    c->depth_format = (VkFormat)0x32;
    c->motion_format = (VkFormat)0x33;
    c->output_format = (VkFormat)0x34;
    c->folder = SENTINEL_FOLDER;
}
/* 0 when every member but the two lookups is the sentinel, else the member's number from 1 */
static inline int sentinel_create_wrong(const SnxUpscalerCreate* c) {
    if (c->sizes.render_w != 11) return 1;
    if (c->sizes.render_h != 12) return 2;
    if (c->sizes.image_w != 13) return 3;
    if (c->sizes.image_h != 14) return 4;
    if (c->sizes.output_w != 15) return 5;
    if (c->sizes.output_h != 16) return 6;
    if (c->instance != SENTINEL_INSTANCE) return 7;
    if (c->physical_device != SENTINEL_PHYSICAL) return 8;
    if (c->device != SENTINEL_DEVICE) return 9;
    if (c->queue != SENTINEL_QUEUE) return 12;
    if (c->queue_family != 0x21u) return 13;
    if (c->flags != 0x80000022u) return 14;
    if ((int)c->colour_format != 0x31) return 15;
    if ((int)c->depth_format != 0x32) return 16;
    if ((int)c->motion_format != 0x33) return 17;
    if ((int)c->output_format != 0x34) return 18;
    if (!c->folder || strcmp(c->folder, SENTINEL_FOLDER)) return 19;
    return 0;
}

static inline void sentinel_traits(SnxUpscalerTraits* t) {
    t->submits_itself = 0x71u;
    t->takes_rectangle = 0x72u;
    t->rectangle_at_origin = 0x73u;
    t->colour_usage = 0x74u;
    t->depth_usage = 0x75u;
    t->motion_usage = 0x76u;
    t->output_usage = 0x77u;
}
static inline int sentinel_traits_wrong(const SnxUpscalerTraits* t) {
    if (t->submits_itself != 0x71u) return 1;
    if (t->takes_rectangle != 0x72u) return 2;
    if (t->rectangle_at_origin != 0x73u) return 3;
    if (t->colour_usage != 0x74u) return 4;
    if (t->depth_usage != 0x75u) return 5;
    if (t->motion_usage != 0x76u) return 6;
    if (t->output_usage != 0x77u) return 7;
    return 0;
}

static inline void sentinel_frame(SnxUpscalerFrame* f) {
    memset(f, 0, sizeof *f);
    f->cb = SENTINEL_CB;
    sentinel_image(&f->colour, 1);
    sentinel_image(&f->depth, 2);
    sentinel_image(&f->motion, 3);
    sentinel_image(&f->output, 4);
    f->render_x = -7;
    f->render_y = 8;
    f->render_w = 91;
    f->render_h = 92;
    f->jitter[0] = 0.25f;
    f->jitter[1] = -0.375f;
    f->vector_scale[0] = -3840.0f;
    f->vector_scale[1] = -2160.0f;
    f->reset = 0x93u;
    f->frame_seconds = 0.0078125f;
    f->near_plane = 0.1f;
    f->far_plane = INFINITY;
    f->fov_y = 0.87266463f;
    f->trace = 0x94u;
}
/* 0 when every member is the sentinel. Else the member's number: 1 the command buffer, 10 to 46 an image's
   member (ten times the image's number plus the member's), 50 and up the numbers behind the images. */
static inline int sentinel_frame_wrong(const SnxUpscalerFrame* f) {
    int wrong;
    if (f->cb != SENTINEL_CB) return 1;
    if ((wrong = sentinel_image_wrong(&f->colour, 1)) != 0) return 10 + wrong;
    if ((wrong = sentinel_image_wrong(&f->depth, 2)) != 0) return 20 + wrong;
    if ((wrong = sentinel_image_wrong(&f->motion, 3)) != 0) return 30 + wrong;
    if ((wrong = sentinel_image_wrong(&f->output, 4)) != 0) return 40 + wrong;
    if (f->render_x != -7) return 50;
    if (f->render_y != 8) return 51;
    if (f->render_w != 91) return 52;
    if (f->render_h != 92) return 53;
    if (f->jitter[0] != 0.25f) return 54;
    if (f->jitter[1] != -0.375f) return 55;
    if (f->vector_scale[0] != -3840.0f) return 56;
    if (f->vector_scale[1] != -2160.0f) return 57;
    if (f->reset != 0x93u) return 58;
    if (f->frame_seconds != 0.0078125f) return 59;
    if (f->near_plane != 0.1f) return 60;
    if (!(f->far_plane > 3.0e38f)) return 61; /* the infinite far plane, as it is */
    if (f->fov_y != 0.87266463f) return 62;
    if (f->trace != 0x94u) return 63;
    return 0;
}
#endif
