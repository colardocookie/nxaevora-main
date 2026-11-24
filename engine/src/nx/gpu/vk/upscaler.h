/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_VK_UPSCALER_H
#define SNX_VK_UPSCALER_H
/* The interface between the runtime and an upscaler's adapter (FSR, XeSS, DLSS). An adapter turns these steps into
   one SDK's calls. It states the contract an adapter is written to. The numbers a frame carries are made by
   temporal_upscaler_frame (temporal_math.h). */
/* Where an adapter lives is the SDK's licence's matter, not this interface's. For FSR and XeSS, whose headers are
   MIT, it can be part of the runtime and load the SDK's own library by name. For DLSS it is a separate module
   that the runtime loads. No SDK's library is ever in this repository: the user supplies it. */
#include <stdint.h>
#ifndef VULKAN_CORE_H_
#include <vulkan/vulkan_core.h>
#endif

#define SNX_UPSCALER_ABI 5
/* the one symbol a separate module exports: it returns its functions for this ABI number, or NULL */
#define SNX_UPSCALER_ENTRY "snx_upscaler_api"

enum {
    SNX_UPSCALER_OK = 0,
    SNX_UPSCALER_REFUSED = 1, /* this device, size or format is not supported: the upscaler stays off */
    SNX_UPSCALER_FAILED = 2,  /* this call failed: its output is not used */
    /* Since ABI 4, from evaluate only: it failed, and the state of the images and of the command buffer is not
       known. That command buffer is never submitted. No adapter the game takes returns it. */
    SNX_UPSCALER_UNKNOWN = 3
};

/* What the library needs of Vulkan. The arrays and the chain are the library's and stay valid until it is
   unloaded, which the runtime does only when every context and all work in flight are gone (in this version: at
   the end of the session). If the instance or the device cannot be made with them, it is made again without
   them: the upscaler is off and everything else goes on as it was. */
typedef struct {
    uint32_t count;
    const char* const* names;
} SnxUpscalerExtensions;
/* Core feature bits a library needs on, as a plain field and never as a structure in the chain: whoever makes
   the device turns each on in its own structure of the core features, after its own question to the physical
   device. A bit the runtime does not know refuses the adapter. The game's device knows the first only (DL2). */
#define SNX_UPSCALER_CORE_SEPARATE_DEPTH_STENCIL 1u /* separateDepthStencilLayouts of the 1.2 features */
#define SNX_UPSCALER_CORE_BUFFER_DEVICE_ADDRESS  2u /* bufferDeviceAddress of the 1.2 features */
/* since ABI 5, for XeSS: of the 1.0, the 1.2 and the 1.3 features */
#define SNX_UPSCALER_CORE_STORAGE_WRITE_WITHOUT_FORMAT 4u /* shaderStorageImageWriteWithoutFormat */
#define SNX_UPSCALER_CORE_SHADER_INT8                  8u /* shaderInt8 */
#define SNX_UPSCALER_CORE_INTEGER_DOT_PRODUCT          16u /* shaderIntegerDotProduct */
#define SNX_UPSCALER_CORE_SCALAR_BLOCK_LAYOUT          32u /* scalarBlockLayout (1.2), as XS2-a found XeSS asks */
#define SNX_UPSCALER_CORE_SELFTEST_KNOWN                                                                                \
    (SNX_UPSCALER_CORE_SEPARATE_DEPTH_STENCIL | SNX_UPSCALER_CORE_BUFFER_DEVICE_ADDRESS |                              \
     SNX_UPSCALER_CORE_STORAGE_WRITE_WITHOUT_FORMAT | SNX_UPSCALER_CORE_SHADER_INT8 |                                 \
     SNX_UPSCALER_CORE_INTEGER_DOT_PRODUCT | SNX_UPSCALER_CORE_SCALAR_BLOCK_LAYOUT)
typedef struct {
    SnxUpscalerExtensions extensions;
    const void* features; /* a pNext chain of feature structures to turn on, or NULL. Never a core structure */
    uint32_t core;        /* SNX_UPSCALER_CORE_* (since ABI 3) */
} SnxUpscalerDeviceNeeds;

/* The inputs' fixed properties. The runtime states all of them: a library that cannot take one refuses. */
enum {
    SNX_UPSCALER_COLOUR_LINEAR_HDR = 1 << 0,   /* scene-referred linear colour, not tone mapped, pre-exposure 1 */
    SNX_UPSCALER_DEPTH_REVERSED = 1 << 1,      /* far is 0, near is 1, range zero to one */
    SNX_UPSCALER_VECTORS_UNJITTERED = 1 << 2,  /* both ends of a vector are without the jitter */
    SNX_UPSCALER_EXPOSURE_AUTOMATIC = 1 << 3,  /* no exposure value is given: the library finds its own */
    SNX_UPSCALER_FAR_PLANE_INFINITE = 1 << 4   /* far_plane may be INFINITY */
};

/* The images are the runtime's own or the game's. Depth is the game's depth image where the adapter's SDK takes its
   format, and depth_format says which that is. An adapter that cannot take it refuses at create, and the runtime
   then gives a copy in a single channel 32 bit float image. The output has a format of four 32 bit floats
   (output_format says which), which the runtime turns into the scene's format itself. */
typedef struct {
    uint32_t render_w, render_h; /* the largest render rectangle */
    uint32_t image_w, image_h;   /* the size of the colour, depth and motion images, which hold the rectangle */
    uint32_t output_w, output_h;
} SnxUpscalerSizes;
/* A context is made for one configuration, which is everything in this structure, and keeps it. A change of any
   of it is a new context, not only a change of a size. */
typedef struct {
    SnxUpscalerSizes sizes;
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    PFN_vkGetInstanceProcAddr instance_lookup;
    PFN_vkGetDeviceProcAddr device_lookup;
    VkQueue queue; /* the runtime's one queue: the library records into the runtime's command buffers for it */
    uint32_t queue_family;
    uint32_t flags; /* SNX_UPSCALER_* above */
    VkFormat colour_format, depth_format, motion_format, output_format;
    const char* folder; /* one the library may write to, UTF-8 */
} SnxUpscalerCreate;

/* What the adapter says of itself at create. One that must submit work on its own is refused by the runtime, but one
   that submits only in its prepare (submits_at_init, since ABI 5) has that prepare run with the device idle before and
   after it, under the runtime's lock, and its frames are never evaluated before both waits succeeded. One
   that cannot take a rectangle of a larger image gets images of exactly the render size, and one that takes a
   rectangle only from an image's first texel gets copies whenever the rectangle starts elsewhere. The usages
   are what each image handed to evaluate must have been made with. */
typedef struct {
    uint32_t submits_itself;
    uint32_t takes_rectangle;
    uint32_t rectangle_at_origin; /* 1: a rectangle only when render_x and render_y are 0 */
    VkImageUsageFlags colour_usage, depth_usage, motion_usage, output_usage;
    uint32_t submits_at_init; /* since ABI 5 */
} SnxUpscalerTraits;

typedef struct {
    VkImage image;
    VkImageView view;
    VkImageLayout layout; /* as it is at the call. On return the image is in this layout again */
    VkFormat format;
    uint32_t w, h;
} SnxUpscalerImage;

/* One frame. Before the call the runtime has ended its render pass and put a barrier on every image, to read for
   the inputs and to read and write for the output. After it the runtime treats all command buffer state as lost
   and puts its own barriers again: it does not rely on what the library did last. An output is shown only in
   the frame that wrote it. */
typedef struct {
    VkCommandBuffer cb;
    SnxUpscalerImage colour, depth, motion, output;
    int32_t render_x, render_y; /* the active rectangle inside colour, depth and motion */
    uint32_t render_w, render_h;
    /* The shift of the drawn image against the unjittered one, in pixels of the rectangle, x right and y down. */
    float jitter[2];
    /* A motion texel times this is the vector in pixels: where the point was a frame ago less where it is now, x
       right and y down, without jitter. It is the size of the whole image with a minus, also for a rectangle. */
    float vector_scale[2];
    uint32_t reset;      /* 1: nothing kept from earlier frames may be used */
    float frame_seconds; /* since the frame before */
    float near_plane, far_plane, fov_y; /* of the scene's camera without jitter, fov in radians */
    /* Diagnostic only. 1: the adapter logs, for this frame, every number it hands to its library and what its
       context was made with, in a line that begins "arceus-upscaler: <name> dispatch". With 0 nothing is
       printed, and in either case what is handed to the library and what comes back is the same. Only the
       self-test sets it. The frame grew by it with ABI 2. A module built for another number is refused. */
    uint32_t trace;
} SnxUpscalerFrame;

typedef struct SnxUpscaler SnxUpscaler; /* a library's context */

/* The steps, in the order the runtime takes them. A change of the configuration is a new context: create,
   prepare, the submission's completion, and only then it takes the old one's place, if the frames still have its
   configuration. Else it is destroyed unused. evaluate is only called on a context made for the frame's. One
   context is on its way at a time, and one that is replaced is destroyed after its frames in flight. */
typedef struct SnxUpscalerApi {
    uint32_t abi;     /* SNX_UPSCALER_ABI as the library was built with */
    const char* name; /* for the log */
    /* before the instance is made: no handles */
    int (*instance_extensions)(SnxUpscalerExtensions* out);
    /* after the instance, before the device */
    int (*device_extensions)(VkInstance instance, VkPhysicalDevice device, PFN_vkGetInstanceProcAddr lookup,
                             SnxUpscalerDeviceNeeds* out);
    /* A context for one configuration. No work for the GPU is done or recorded here. */
    int (*create)(const SnxUpscalerCreate* create, SnxUpscalerTraits* traits, SnxUpscaler** out);
    /* What the context must run once is recorded into the runtime's command buffer, or nothing. The runtime
       submits it. The context is ready, and evaluate may be called, only when that submission has completed. */
    int (*prepare)(SnxUpscaler* upscaler, VkCommandBuffer cb);
    int (*evaluate)(SnxUpscaler* upscaler, const SnxUpscalerFrame* frame);
    /* The runtime calls it only after every frame in flight that used the context has completed, and for a
       context whose prepare failed only after that command buffer was discarded or its submission completed. */
    void (*destroy)(SnxUpscaler* upscaler);
    /* Since ABI 4, NULL when the library starts nothing for a device. Called once for a device, after every
       context of it was destroyed and its work completed, before the device is destroyed, and never otherwise.
       OK or FAILED, with the errors destroy could not return. The game never destroys its device: only the
       self-test calls it. */
    int (*shutdown)(VkDevice device);
    /* Since ABI 5: the output's format, fixed when the adapter is loaded and the same in every create.
       VK_FORMAT_UNDEFINED is the four 32 bit floats above. */
    VkFormat output_format;
} SnxUpscalerApi;

/* The host's log for a module of its own, which cannot use the runtime's: one whole line without its newline. It
   copies the line, may be called from any thread, and never calls into the module. */
typedef void (*SnxUpscalerLog)(const char* line);
/* A separate module's one export (since ABI 4 it is handed the log): its table for this ABI number, or NULL.
   It starts nothing. */
typedef const SnxUpscalerApi* (*SnxUpscalerEntry)(uint32_t abi, SnxUpscalerLog log);
#ifdef __cplusplus
extern "C"
#endif
/* gpu/vk/upscaler_module.c: the table of the module at this full path, handed the log, or NULL with why
   (upscaler_module.h) */
const SnxUpscalerApi* vk_upscaler_module_load(const char* path, SnxUpscalerLog log, int* why);
#ifdef __cplusplus
extern "C" {
#endif
/* gpu/vk/upscaler_host.c: an SDK's own library, the file of this name in the program's folder, loaded by its full
   path with what it depends on looked for in that folder and the system's only (not the current folder, not the
   search path), or NULL. tried says the path tried, or why none was. Windows only, where the SDKs are */
void* vk_upscaler_library_beside(const char* file, char* tried, size_t tried_size);
/* a function of a library from vk_upscaler_library_beside, or NULL */
void* vk_upscaler_library_symbol(void* library, const char* name);
/* Whether the adapter of name and its SDK library are there, asked before any Vulkan instance: the adapter's lookup
   and its question of the instance's needs, which loads the library. The lines they log are caught, then written to
   the log again in their order, and why gets the last arceus-upscaler line's words. 1 when it is there */
int vk_upscaler_present(const char* name, char* why, size_t size);
#ifdef __cplusplus
}
#endif

/* The boundary to a module of its own, stated: a 64 bit system in both files, C linkage and the one export
   name, the platform's one calling convention, natural alignment with no packing pragma, and Vulkan's handles
   as the pointers and 64 bit values its headers define. The size, the alignment and every member's offset of
   the table and of each structure that crosses are asserted here, for whichever compiler reads this header. */
#include <stddef.h>
#if defined(_WIN64) || defined(__LP64__)
#ifdef __cplusplus
#define SNX_UPSCALER_LAID(type, size, align) \
    static_assert(sizeof(type) == (size) && alignof(type) == (align), "the layout of " #type)
#define SNX_UPSCALER_AT(type, member, offset) \
    static_assert(offsetof(type, member) == (offset), "the place of " #member " in " #type)
#else
#define SNX_UPSCALER_LAID(type, size, align) \
    _Static_assert(sizeof(type) == (size) && _Alignof(type) == (align), "the layout of " #type)
#define SNX_UPSCALER_AT(type, member, offset) \
    _Static_assert(offsetof(type, member) == (offset), "the place of " #member " in " #type)
#endif
SNX_UPSCALER_LAID(SnxUpscalerExtensions, 16, 8);
SNX_UPSCALER_AT(SnxUpscalerExtensions, count, 0);
SNX_UPSCALER_AT(SnxUpscalerExtensions, names, 8);
SNX_UPSCALER_LAID(SnxUpscalerDeviceNeeds, 32, 8);
SNX_UPSCALER_AT(SnxUpscalerDeviceNeeds, extensions, 0);
SNX_UPSCALER_AT(SnxUpscalerDeviceNeeds, features, 16);
SNX_UPSCALER_AT(SnxUpscalerDeviceNeeds, core, 24);
SNX_UPSCALER_LAID(SnxUpscalerSizes, 24, 4);
SNX_UPSCALER_AT(SnxUpscalerSizes, render_w, 0);
SNX_UPSCALER_AT(SnxUpscalerSizes, render_h, 4);
SNX_UPSCALER_AT(SnxUpscalerSizes, image_w, 8);
SNX_UPSCALER_AT(SnxUpscalerSizes, image_h, 12);
SNX_UPSCALER_AT(SnxUpscalerSizes, output_w, 16);
SNX_UPSCALER_AT(SnxUpscalerSizes, output_h, 20);
SNX_UPSCALER_LAID(SnxUpscalerCreate, 104, 8);
SNX_UPSCALER_AT(SnxUpscalerCreate, sizes, 0);
SNX_UPSCALER_AT(SnxUpscalerCreate, instance, 24);
SNX_UPSCALER_AT(SnxUpscalerCreate, physical_device, 32);
SNX_UPSCALER_AT(SnxUpscalerCreate, device, 40);
SNX_UPSCALER_AT(SnxUpscalerCreate, instance_lookup, 48);
SNX_UPSCALER_AT(SnxUpscalerCreate, device_lookup, 56);
SNX_UPSCALER_AT(SnxUpscalerCreate, queue, 64);
SNX_UPSCALER_AT(SnxUpscalerCreate, queue_family, 72);
SNX_UPSCALER_AT(SnxUpscalerCreate, flags, 76);
SNX_UPSCALER_AT(SnxUpscalerCreate, colour_format, 80);
SNX_UPSCALER_AT(SnxUpscalerCreate, depth_format, 84);
SNX_UPSCALER_AT(SnxUpscalerCreate, motion_format, 88);
SNX_UPSCALER_AT(SnxUpscalerCreate, output_format, 92);
SNX_UPSCALER_AT(SnxUpscalerCreate, folder, 96);
SNX_UPSCALER_LAID(SnxUpscalerTraits, 32, 4);
SNX_UPSCALER_AT(SnxUpscalerTraits, submits_itself, 0);
SNX_UPSCALER_AT(SnxUpscalerTraits, takes_rectangle, 4);
SNX_UPSCALER_AT(SnxUpscalerTraits, rectangle_at_origin, 8);
SNX_UPSCALER_AT(SnxUpscalerTraits, colour_usage, 12);
SNX_UPSCALER_AT(SnxUpscalerTraits, depth_usage, 16);
SNX_UPSCALER_AT(SnxUpscalerTraits, motion_usage, 20);
SNX_UPSCALER_AT(SnxUpscalerTraits, output_usage, 24);
SNX_UPSCALER_AT(SnxUpscalerTraits, submits_at_init, 28);
SNX_UPSCALER_LAID(SnxUpscalerImage, 32, 8);
SNX_UPSCALER_AT(SnxUpscalerImage, image, 0);
SNX_UPSCALER_AT(SnxUpscalerImage, view, 8);
SNX_UPSCALER_AT(SnxUpscalerImage, layout, 16);
SNX_UPSCALER_AT(SnxUpscalerImage, format, 20);
SNX_UPSCALER_AT(SnxUpscalerImage, w, 24);
SNX_UPSCALER_AT(SnxUpscalerImage, h, 28);
SNX_UPSCALER_LAID(SnxUpscalerFrame, 192, 8);
SNX_UPSCALER_AT(SnxUpscalerFrame, cb, 0);
SNX_UPSCALER_AT(SnxUpscalerFrame, colour, 8);
SNX_UPSCALER_AT(SnxUpscalerFrame, depth, 40);
SNX_UPSCALER_AT(SnxUpscalerFrame, motion, 72);
SNX_UPSCALER_AT(SnxUpscalerFrame, output, 104);
SNX_UPSCALER_AT(SnxUpscalerFrame, render_x, 136);
SNX_UPSCALER_AT(SnxUpscalerFrame, render_y, 140);
SNX_UPSCALER_AT(SnxUpscalerFrame, render_w, 144);
SNX_UPSCALER_AT(SnxUpscalerFrame, render_h, 148);
SNX_UPSCALER_AT(SnxUpscalerFrame, jitter, 152);
SNX_UPSCALER_AT(SnxUpscalerFrame, vector_scale, 160);
SNX_UPSCALER_AT(SnxUpscalerFrame, reset, 168);
SNX_UPSCALER_AT(SnxUpscalerFrame, frame_seconds, 172);
SNX_UPSCALER_AT(SnxUpscalerFrame, near_plane, 176);
SNX_UPSCALER_AT(SnxUpscalerFrame, far_plane, 180);
SNX_UPSCALER_AT(SnxUpscalerFrame, fov_y, 184);
SNX_UPSCALER_AT(SnxUpscalerFrame, trace, 188);
SNX_UPSCALER_LAID(SnxUpscalerApi, 80, 8);
SNX_UPSCALER_AT(SnxUpscalerApi, abi, 0);
SNX_UPSCALER_AT(SnxUpscalerApi, name, 8);
SNX_UPSCALER_AT(SnxUpscalerApi, instance_extensions, 16);
SNX_UPSCALER_AT(SnxUpscalerApi, device_extensions, 24);
SNX_UPSCALER_AT(SnxUpscalerApi, create, 32);
SNX_UPSCALER_AT(SnxUpscalerApi, prepare, 40);
SNX_UPSCALER_AT(SnxUpscalerApi, evaluate, 48);
SNX_UPSCALER_AT(SnxUpscalerApi, destroy, 56);
SNX_UPSCALER_AT(SnxUpscalerApi, shutdown, 64);
SNX_UPSCALER_AT(SnxUpscalerApi, output_format, 72);
/* what the members are made of, as far as this header can state it without naming a compiler */
SNX_UPSCALER_LAID(VkInstance, 8, 8);
SNX_UPSCALER_LAID(VkImage, 8, 8);
SNX_UPSCALER_LAID(VkFormat, 4, 4);
SNX_UPSCALER_LAID(VkImageLayout, 4, 4);
SNX_UPSCALER_LAID(VkImageUsageFlags, 4, 4);
SNX_UPSCALER_LAID(PFN_vkGetInstanceProcAddr, 8, 8);
SNX_UPSCALER_LAID(SnxUpscalerLog, 8, 8);
SNX_UPSCALER_LAID(SnxUpscalerEntry, 8, 8);
SNX_UPSCALER_LAID(float, 4, 4);
#endif
#endif
