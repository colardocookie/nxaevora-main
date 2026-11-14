/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_VK_UPSCALER_H
#define SNX_VK_UPSCALER_H
/* The interface between the runtime and an upscaler's adapter (FSR, XeSS, DLSS). An adapter turns these steps into
   one SDK's calls. NOT USED YET: no adapter exists, nothing is loaded and nothing calls this. It states the
   contract an adapter is written to. The numbers a frame carries are made by temporal_upscaler_frame
   (temporal_math.h). */
/* Where an adapter lives is the SDK's licence's matter, not this interface's. For FSR and XeSS, whose headers are
   MIT, it can be part of the runtime and load the SDK's own library by name. For DLSS it is a separate module
   that the runtime loads. No SDK's library is ever in this repository: the user supplies it. */
#include <stdint.h>
#ifndef VULKAN_CORE_H_
#include <vulkan/vulkan_core.h>
#endif

#define SNX_UPSCALER_ABI 2
/* the one symbol a separate module exports: it returns its functions for this ABI number, or NULL */
#define SNX_UPSCALER_ENTRY "snx_upscaler_api"

enum {
    SNX_UPSCALER_OK = 0,
    SNX_UPSCALER_REFUSED = 1, /* this device, size or format is not supported: the upscaler stays off */
    SNX_UPSCALER_FAILED = 2   /* this call failed: its output is not used */
};

/* What the library needs of Vulkan. The arrays and the chain are the library's and stay valid until it is
   unloaded, which the runtime does only when every context and all work in flight are gone (in this version: at
   the end of the session). If the instance or the device cannot be made with them, it is made again without
   them: the upscaler is off and everything else goes on as it was. */
typedef struct {
    uint32_t count;
    const char* const* names;
} SnxUpscalerExtensions;
typedef struct {
    SnxUpscalerExtensions extensions;
    const void* features; /* a pNext chain of feature structures to turn on, or NULL */
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
   then gives a copy in a single channel 32 bit float image. The output has a 16 bit float format, which the
   runtime turns into the scene's format itself. */
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

/* What the adapter says of itself at create. One that must submit work on its own is refused by the runtime. One
   that cannot take a rectangle of a larger image gets images of exactly the render size, and one that takes a
   rectangle only from an image's first texel gets copies whenever the rectangle starts elsewhere. The usages
   are what each image handed to evaluate must have been made with. */
typedef struct {
    uint32_t submits_itself;
    uint32_t takes_rectangle;
    uint32_t rectangle_at_origin; /* 1: a rectangle only when render_x and render_y are 0 */
    VkImageUsageFlags colour_usage, depth_usage, motion_usage, output_usage;
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
    /* 1: the adapter logs, for this frame, every number it hands to its library and what its context was made
       with, in a line that begins "arceus-upscaler: <name> dispatch". It changes nothing else. */
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
} SnxUpscalerApi;

typedef const SnxUpscalerApi* (*SnxUpscalerEntry)(uint32_t abi);
#endif
