/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* nvn_native=1: the host answers the NVN functions that the game resolves through nvnBootstrapLoader and
   nvnDeviceGetProcAddress. Each resolved address gets a host handler in its dispatch slot, so the game's NVN library
   never runs for a call through the table. A function without a handler gets the missing-function handler: it logs the
   first call and returns 0, so one run lists every missing function (docs/nvn-design.md, 2-3 and 2-6).
   The loader hooks are the ones of gpu/nvn_trace.c, one level below it: nvn_native_init runs first, and the trace
   wraps what the slots then hold, so it counts the host calls too. */
#include "nx/gpu/nvn_native.h"
#include "nx/gpu/nvn_objtab.h"
#include "nx/gpu/nvn_params.h"
#include "nx/gpu/nvn_trace.h"
#include "nx/runtime/loader.h"
#include "nx/runtime/main.h"
#include "nx/runtime/options.h"
#include "nx/runtime/rtapi.h"
#include "nx/hle/kernel/kernel.h"
#include "nx/memory/mem.h"
#include "nx/runtime/platform/log.h"
#include "nx/runtime/platform/plat.h"
#include <stdlib.h>
#include <string.h>

/* The host's records of the NVN objects. The game's object memory is never written (docs/nvn-design.md, 2-1). Handlers
   take objs_lock for the whole call, and their pointers into a record end with the call. */
static NvnObjTab objs;
static PMutex objs_lock = PMUTEX_INIT;

typedef struct {
    uint32_t flags;
} DeviceBuilderRec;
typedef struct {
    uint32_t flags, window_origin, depth_mode;
} DeviceRec;
typedef struct {
    NvnQueueBuilder builder; /* the builder as it was at QueueInitialize */
} QueueRec;
typedef struct {
    uint64_t device, native_window, textures[NVN_WINDOW_MAX_TEXTURES];
    uint32_t texture_count, interval;
    NvnWindowCrop crop;
    uint8_t states[NVN_WINDOW_MAX_TEXTURES];
    int32_t last_index;
} WindowRec;
typedef struct { uint64_t device; NvnSyncState state; } SyncRec;

/* the record of kind at addr, made zeroed (with the defaults of that kind) when the game never called SetDefaults */
static void* rec_get(u64 addr, NvnObjKind kind, size_t size) {
    NvnObj* o = nvn_objtab_find(&objs, addr);
    if (o && o->kind == kind) return o->data;
    o = nvn_objtab_put(&objs, addr, kind, size);
    if (!o) fatal("nvn-native: out of memory for the record of an NVN object (%llx)\n", (unsigned long long)addr);
    if (kind == NVN_OBJ_QUEUE_BUILDER) nvn_queue_builder_defaults(o->data);
    if (kind == NVN_OBJ_MEMORY_POOL_BUILDER) nvn_memory_pool_builder_defaults(o->data);
    if (kind == NVN_OBJ_SAMPLER_BUILDER) nvn_sampler_defaults(o->data);
    if (kind == NVN_OBJ_TEXTURE_BUILDER) nvn_texture_builder_defaults(o->data);
    if (kind == NVN_OBJ_WINDOW_BUILDER) nvn_window_builder_defaults(o->data);
    return o->data;
}
static void* known_record(u64 addr, NvnObjKind kind);
static NvnQueueBuilder* queue_builder(u64 addr) { return rec_get(addr, NVN_OBJ_QUEUE_BUILDER, sizeof(NvnQueueBuilder)); }

/* G1: device builder and device (docs/nvn-design.md, 2-6). The library's functions are plain field accesses. */
static void h_device_builder_set_defaults(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    DeviceBuilderRec* b = rec_get(c->x[0], NVN_OBJ_DEVICE_BUILDER, sizeof *b);
    b->flags = 0;
    pm_unlock(&objs_lock);
}
static void h_device_builder_set_flags(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    ((DeviceBuilderRec*)rec_get(c->x[0], NVN_OBJ_DEVICE_BUILDER, sizeof(DeviceBuilderRec)))->flags = (u32)c->x[1];
    pm_unlock(&objs_lock);
}
static void h_device_builder_get_flags(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    c->x[0] = ((DeviceBuilderRec*)rec_get(c->x[0], NVN_OBJ_DEVICE_BUILDER, sizeof(DeviceBuilderRec)))->flags;
    pm_unlock(&objs_lock);
}
/* nvnDeviceInitialize(device, builder): the host has no device to set up, so it records the flags and says yes */
static void h_device_initialize(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    DeviceBuilderRec* b = rec_get(c->x[1], NVN_OBJ_DEVICE_BUILDER, sizeof *b);
    NvnObj* o = nvn_objtab_put(&objs, c->x[0], NVN_OBJ_DEVICE, sizeof(DeviceRec));
    if (!o) fatal("nvn-native: out of memory for the record of the device\n");
    ((DeviceRec*)o->data)->flags = b->flags;
    pm_unlock(&objs_lock);
    c->x[0] = 1;
}
static DeviceRec* device_rec(u64 addr) { return rec_get(addr, NVN_OBJ_DEVICE, sizeof(DeviceRec)); }
static void h_device_set_window_origin_mode(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    device_rec(c->x[0])->window_origin = (u32)c->x[1];
    pm_unlock(&objs_lock);
}
static void h_device_get_window_origin_mode(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    c->x[0] = device_rec(c->x[0])->window_origin;
    pm_unlock(&objs_lock);
}
static void h_device_set_depth_mode(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    device_rec(c->x[0])->depth_mode = (u32)c->x[1];
    pm_unlock(&objs_lock);
}
static void h_device_get_depth_mode(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    c->x[0] = device_rec(c->x[0])->depth_mode;
    pm_unlock(&objs_lock);
}

/* nvnDeviceGetInteger(device, what, int* out). A `what` that no run asked for gets 0 and one log line. */
static void h_device_get_integer(CPU* c, u64 entry) {
    (void)entry;
    u32 what = (u32)c->x[1];
    int32_t value = 0;
    if (!nvn_device_integer(what, &value))
        LOG_LIMIT(32, "nvn-native: nvnDeviceGetInteger what=%u has no known value, answers 0\n", what);
    gw32(c->x[2], (u32)value);
}
/* nvnDeviceGetTimestampInNanoseconds(device, counter): the counter's tick count is at +8 */
static void h_device_get_timestamp_ns(CPU* c, u64 entry) {
    (void)entry;
    c->x[0] = nvn_timestamp_ns(gr64(c->x[1] + 8));
}

/* G1: queue builder and queue */
static void h_queue_builder_set_device(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    queue_builder(c->x[0])->device = c->x[1];
    pm_unlock(&objs_lock);
}
static void h_queue_builder_get_device(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    c->x[0] = queue_builder(c->x[0])->device;
    pm_unlock(&objs_lock);
}
static void h_queue_builder_set_defaults(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    nvn_queue_builder_defaults(queue_builder(c->x[0]));
    pm_unlock(&objs_lock);
}
static void h_queue_builder_set_flags(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    queue_builder(c->x[0])->flags = (u32)c->x[1];
    pm_unlock(&objs_lock);
}
static void h_queue_builder_get_flags(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    c->x[0] = queue_builder(c->x[0])->flags;
    pm_unlock(&objs_lock);
}
static void h_queue_builder_set_priority(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    queue_builder(c->x[0])->priority = (u32)c->x[1];
    pm_unlock(&objs_lock);
}
static void h_queue_builder_get_priority(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    c->x[0] = queue_builder(c->x[0])->priority;
    pm_unlock(&objs_lock);
}
static void h_queue_builder_set_queue_memory(CPU* c, u64 entry) { /* (builder, memory, size) */
    (void)entry;
    pm_lock(&objs_lock);
    NvnQueueBuilder* b = queue_builder(c->x[0]);
    b->memory = c->x[1];
    b->memory_size = (u32)c->x[2];
    pm_unlock(&objs_lock);
}
static void h_queue_builder_get_memory(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    c->x[0] = queue_builder(c->x[0])->memory;
    pm_unlock(&objs_lock);
}
static void h_queue_builder_get_memory_size(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    c->x[0] = queue_builder(c->x[0])->memory_size;
    pm_unlock(&objs_lock);
}
/* The three memory sizes and the flush threshold have a setter that marks the value as set, and a getter that writes
   the value through a pointer and returns whether it was set. */
#define QUEUE_BUILDER_SIZE_HANDLERS(name, field, has)                                  \
    static void h_queue_builder_set_##name(CPU* c, u64 entry) {                        \
        (void)entry;                                                                   \
        pm_lock(&objs_lock);                                                           \
        NvnQueueBuilder* b = queue_builder(c->x[0]);                                   \
        b->has = 1;                                                                    \
        b->field = (int32_t)c->x[1];                                                   \
        pm_unlock(&objs_lock);                                                         \
    }                                                                                  \
    static void h_queue_builder_get_##name(CPU* c, u64 entry) {                        \
        (void)entry;                                                                   \
        pm_lock(&objs_lock);                                                           \
        NvnQueueBuilder* b = queue_builder(c->x[0]);                                   \
        gw32(c->x[1], (u32)b->field);                                                  \
        c->x[0] = b->has;                                                              \
        pm_unlock(&objs_lock);                                                         \
    }
QUEUE_BUILDER_SIZE_HANDLERS(command_memory_size, command, has_command)
QUEUE_BUILDER_SIZE_HANDLERS(compute_memory_size, compute, has_compute)
QUEUE_BUILDER_SIZE_HANDLERS(control_memory_size, control, has_control)
QUEUE_BUILDER_SIZE_HANDLERS(command_flush_threshold, flush_threshold, has_flush_threshold)

static void h_queue_builder_get_queue_memory_size(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    c->x[0] = nvn_queue_memory_size(queue_builder(c->x[0]), &nvn_queue_device_words);
    pm_unlock(&objs_lock);
}
/* nvnQueueInitialize(queue, builder): records the builder, says yes. The recording and the submission are in 3c. */
static void h_queue_initialize(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    NvnQueueBuilder b = *queue_builder(c->x[1]);
    NvnObj* o = nvn_objtab_put(&objs, c->x[0], NVN_OBJ_QUEUE, sizeof(QueueRec));
    if (!o) fatal("nvn-native: out of memory for the record of a queue\n");
    ((QueueRec*)o->data)->builder = b;
    pm_unlock(&objs_lock);
    c->x[0] = 1;
}

/* G2 and G5: pool state stays in host records. GPU page-table mapping is not connected yet. */
typedef struct {
    uint64_t device, storage, size, gpu_address;
    uint32_t flags;
} MemoryPoolRec;
typedef struct {
    uint64_t memory_pool, offset;
    uint32_t count;
} DescriptorPoolRec;
static uint64_t pool_gpu_cursor = NVN_POOL_GPU_BASE;

static NvnMemoryPoolBuilder* memory_pool_builder(u64 addr) {
    return rec_get(addr, NVN_OBJ_MEMORY_POOL_BUILDER, sizeof(NvnMemoryPoolBuilder));
}
static MemoryPoolRec* memory_pool_rec(u64 addr) {
    return rec_get(addr, NVN_OBJ_MEMORY_POOL, sizeof(MemoryPoolRec));
}
static void h_memory_pool_builder_set_defaults(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    nvn_memory_pool_builder_defaults(memory_pool_builder(c->x[0]));
    pm_unlock(&objs_lock);
}
static void h_memory_pool_builder_set_device(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    memory_pool_builder(c->x[0])->device = c->x[1];
    pm_unlock(&objs_lock);
}
static void h_memory_pool_builder_set_flags(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    memory_pool_builder(c->x[0])->flags = (u32)c->x[1];
    pm_unlock(&objs_lock);
}
static void h_memory_pool_builder_set_storage(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    NvnMemoryPoolBuilder* b = memory_pool_builder(c->x[0]);
    b->storage = c->x[1];
    b->size = c->x[2];
    b->handle = 0;
    pm_unlock(&objs_lock);
}
static void pool_initialize_error(NvnPoolResult result, u64 pool, const NvnMemoryPoolBuilder* b) {
    if (result == NVN_POOL_UNSUPPORTED_VIRTUAL)
        LOG("nvn-native: virtual memory pool not supported\n");
    else if (result == NVN_POOL_UNSUPPORTED_IMPORT)
        LOG("nvn-native: imported memory pool not supported\n");
    else
        LOG("nvn-native: MemoryPoolInitialize pool=%llx storage=%llx size=%llx flags=%x check=%u\n",
            (unsigned long long)pool, (unsigned long long)b->storage, (unsigned long long)b->size,
            b->flags, (unsigned)result);
}
static void h_memory_pool_initialize(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    NvnMemoryPoolBuilder b = *memory_pool_builder(c->x[1]);
    uint32_t flags;
    NvnPoolResult result = nvn_memory_pool_params(&b, NVN_POOL_PAGE_SIZE, &flags);
    /* Replacing the record also discards state after a failed initialization. */
    NvnObj* o = nvn_objtab_put(&objs, c->x[0], NVN_OBJ_MEMORY_POOL, sizeof(MemoryPoolRec));
    if (!o) fatal("nvn-native: out of memory for the record of memory pool %llx\n", (unsigned long long)c->x[0]);
    if (result != NVN_POOL_OK) {
        pool_initialize_error(result, c->x[0], &b);
        c->x[0] = 0;
        pm_unlock(&objs_lock);
        return;
    }
    uint64_t address, next;
    if (!nvn_pool_gpu_allocate(pool_gpu_cursor, b.size, NVN_POOL_PAGE_SIZE, &address, &next)) {
        LOG("nvn-native: MemoryPoolInitialize pool=%llx size=%llx GPU address allocation failed\n",
            (unsigned long long)c->x[0], (unsigned long long)b.size);
        c->x[0] = 0;
        pm_unlock(&objs_lock);
        return;
    }
    MemoryPoolRec* p = o->data;
    *p = (MemoryPoolRec){.device = b.device, .storage = b.storage, .size = b.size,
                         .gpu_address = address, .flags = flags};
    pool_gpu_cursor = next;
    c->x[0] = 1;
    pm_unlock(&objs_lock);
}

static void h_memory_pool_finalize(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    NvnObj* o = nvn_objtab_find(&objs, c->x[0]);
    if (o && o->kind == NVN_OBJ_MEMORY_POOL) nvn_objtab_remove(&objs, c->x[0]);
    pm_unlock(&objs_lock);
}
#define MEMORY_POOL_GETTER(name, expression)                                        \
    static void h_memory_pool_##name(CPU* c, u64 entry) {                           \
        (void)entry;                                                                \
        pm_lock(&objs_lock);                                                        \
        MemoryPoolRec* p = memory_pool_rec(c->x[0]);                                \
        c->x[0] = (expression);                                                     \
        pm_unlock(&objs_lock);                                                      \
    }
MEMORY_POOL_GETTER(map, nvn_memory_pool_map(p->flags, p->storage))
MEMORY_POOL_GETTER(get_flags, nvn_memory_pool_flags(p->flags))
MEMORY_POOL_GETTER(get_buffer_address, p->gpu_address)

#define DESCRIPTOR_POOL_HANDLERS(name, kind)                                        \
    static void h_##name##_pool_initialize(CPU* c, u64 entry) {                     \
        (void)entry;                                                                \
        pm_lock(&objs_lock);                                                        \
        DescriptorPoolRec* p = rec_get(c->x[0], kind, sizeof *p);                   \
        *p = (DescriptorPoolRec){c->x[1], c->x[2], (u32)c->x[3]};                   \
        c->x[0] = 1;                                                                \
        pm_unlock(&objs_lock);                                                      \
    }                                                                               \
    static void h_##name##_pool_get_size(CPU* c, u64 entry) {                       \
        (void)entry;                                                                \
        pm_lock(&objs_lock);                                                        \
        DescriptorPoolRec* p = rec_get(c->x[0], kind, sizeof *p);                   \
        c->x[0] = p->count;                                                         \
        pm_unlock(&objs_lock);                                                      \
    }
DESCRIPTOR_POOL_HANDLERS(texture, NVN_OBJ_TEXTURE_POOL)
DESCRIPTOR_POOL_HANDLERS(sampler, NVN_OBJ_SAMPLER_POOL)

/* G3 and G5: the sampler and texture records are independent of the guest allocation sizes. */
typedef NvnTexture TextureRec;
static NvnSampler* sampler_builder(u64 addr) {
    return rec_get(addr, NVN_OBJ_SAMPLER_BUILDER, sizeof(NvnSampler));
}
static NvnTextureBuilder* texture_builder(u64 addr) {
    return rec_get(addr, NVN_OBJ_TEXTURE_BUILDER, sizeof(NvnTextureBuilder));
}
static NvnTextureView* texture_view(u64 addr) {
    return rec_get(addr, NVN_OBJ_TEXTURE_VIEW, sizeof(NvnTextureView));
}
/* Check each memory region before using the guest read helpers. */
static int native_guest_readable(u64 addr, u64 size) {
    if (!addr || addr >= AS_SIZE || size > AS_SIZE - addr) return 0;
    u64 end = addr + size;
    while (addr < end) {
        MemBlock block;
        mem_query(addr, &block);
        if (!(block.perm & PERM_R) || !block.size || block.addr > addr ||
            block.size > AS_SIZE - block.addr || block.addr + block.size <= addr) return 0;
        addr = block.addr + block.size;
    }
    return 1;
}
static float native_float_arg(const CPU* c, unsigned index) {
    uint32_t bits = SNX_VS(c->v[index], 0);
    float value;
    memcpy(&value, &bits, sizeof value);
    return value;
}
#define RESOURCE_BUILDER_HANDLER(name, type, get, statements) \
    static void h_##name(CPU* c, u64 entry) {                 \
        (void)entry;                                         \
        pm_lock(&objs_lock);                                 \
        type* b = get(c->x[0]);                              \
        statements;                                          \
        pm_unlock(&objs_lock);                               \
    }
RESOURCE_BUILDER_HANDLER(sampler_builder_set_device, NvnSampler, sampler_builder,
    b->device = c->x[1])
RESOURCE_BUILDER_HANDLER(sampler_builder_set_defaults, NvnSampler, sampler_builder,
    nvn_sampler_defaults(b))
RESOURCE_BUILDER_HANDLER(sampler_builder_set_min_mag_filter, NvnSampler, sampler_builder,
    b->min_filter = (u32)c->x[1]; b->mag_filter = (u32)c->x[2])
RESOURCE_BUILDER_HANDLER(sampler_builder_set_wrap_mode, NvnSampler, sampler_builder,
    for (unsigned i = 0; i < 3; i++) b->wrap[i] = (u32)c->x[i + 1])
RESOURCE_BUILDER_HANDLER(sampler_builder_set_lod_clamp, NvnSampler, sampler_builder,
    b->min_lod = native_float_arg(c, 0); b->max_lod = native_float_arg(c, 1))
RESOURCE_BUILDER_HANDLER(sampler_builder_set_lod_bias, NvnSampler, sampler_builder,
    b->lod_bias = native_float_arg(c, 0))
RESOURCE_BUILDER_HANDLER(sampler_builder_set_compare, NvnSampler, sampler_builder,
    b->compare_enable = (u32)c->x[1]; b->compare = (u32)c->x[2])
RESOURCE_BUILDER_HANDLER(sampler_builder_set_border_color, NvnSampler, sampler_builder,
    for (unsigned i = 0; i < 4; i++) b->border[i] = gr32(c->x[1] + i * 4))
RESOURCE_BUILDER_HANDLER(sampler_builder_set_max_anisotropy, NvnSampler, sampler_builder,
    b->anisotropy = native_float_arg(c, 0))
RESOURCE_BUILDER_HANDLER(sampler_builder_set_reduction_filter, NvnSampler, sampler_builder,
    b->reduction = (u32)c->x[1])
RESOURCE_BUILDER_HANDLER(sampler_builder_set_lod_snap, NvnSampler, sampler_builder,
    b->lod_snap = native_float_arg(c, 0))
RESOURCE_BUILDER_HANDLER(texture_builder_set_device, NvnTextureBuilder, texture_builder,
    b->device = c->x[1])
RESOURCE_BUILDER_HANDLER(texture_builder_set_defaults, NvnTextureBuilder, texture_builder,
    nvn_texture_builder_defaults(b))
RESOURCE_BUILDER_HANDLER(texture_builder_set_flags, NvnTextureBuilder, texture_builder,
    b->flags = (u32)c->x[1])
RESOURCE_BUILDER_HANDLER(texture_builder_set_target, NvnTextureBuilder, texture_builder,
    b->target = (u32)c->x[1])
RESOURCE_BUILDER_HANDLER(texture_builder_set_size_2d, NvnTextureBuilder, texture_builder,
    b->width = (u32)c->x[1]; b->height = (u32)c->x[2]; b->depth = 1)
RESOURCE_BUILDER_HANDLER(texture_builder_set_levels, NvnTextureBuilder, texture_builder,
    b->levels = (u32)c->x[1])
RESOURCE_BUILDER_HANDLER(texture_builder_set_format, NvnTextureBuilder, texture_builder,
    b->format = (u32)c->x[1])
RESOURCE_BUILDER_HANDLER(texture_builder_set_swizzle, NvnTextureBuilder, texture_builder,
    for (unsigned i = 0; i < 4; i++) b->swizzle[i] = (u32)c->x[i + 1])
RESOURCE_BUILDER_HANDLER(texture_builder_set_depth_stencil_mode, NvnTextureBuilder, texture_builder,
    b->depth_stencil_mode = (u32)c->x[1])
RESOURCE_BUILDER_HANDLER(texture_builder_set_storage, NvnTextureBuilder, texture_builder,
    b->pool = c->x[1]; b->offset = c->x[2])
RESOURCE_BUILDER_HANDLER(texture_builder_set_stride, NvnTextureBuilder, texture_builder,
    b->stride = (u32)c->x[1])
RESOURCE_BUILDER_HANDLER(texture_builder_get_width, NvnTextureBuilder, texture_builder,
    c->x[0] = b->width)
RESOURCE_BUILDER_HANDLER(texture_builder_get_height, NvnTextureBuilder, texture_builder,
    c->x[0] = b->height)
RESOURCE_BUILDER_HANDLER(texture_builder_get_storage_alignment, NvnTextureBuilder, texture_builder,
    c->x[0] = nvn_texture_storage_alignment(b, &nvn_texture_device_words))
RESOURCE_BUILDER_HANDLER(texture_builder_get_storage_size, NvnTextureBuilder, texture_builder,
    NvnTextureLayout l; nvn_texture_layout(b, &nvn_texture_device_words, &l); c->x[0] = l.size)

RESOURCE_BUILDER_HANDLER(texture_builder_set_width, NvnTextureBuilder, texture_builder,
    b->width = (u32)c->x[1])
RESOURCE_BUILDER_HANDLER(texture_builder_set_height, NvnTextureBuilder, texture_builder,
    b->height = (u32)c->x[1])
RESOURCE_BUILDER_HANDLER(texture_builder_set_depth, NvnTextureBuilder, texture_builder,
    b->depth = (u32)c->x[1])
RESOURCE_BUILDER_HANDLER(texture_view_set_defaults, NvnTextureView, texture_view,
    memset(b, 0, sizeof *b))
RESOURCE_BUILDER_HANDLER(texture_view_set_levels, NvnTextureView, texture_view,
    b->flags |= NVN_VIEW_LEVELS; b->base_level = (uint8_t)c->x[1]; b->level_count = (uint8_t)c->x[2])
RESOURCE_BUILDER_HANDLER(texture_view_set_layers, NvnTextureView, texture_view,
    b->flags |= NVN_VIEW_LAYERS; b->base_layer = (uint16_t)c->x[1]; b->layer_count = (uint16_t)c->x[2])
RESOURCE_BUILDER_HANDLER(texture_view_set_format, NvnTextureView, texture_view,
    b->flags |= NVN_VIEW_FORMAT; b->format = (u32)c->x[1])
RESOURCE_BUILDER_HANDLER(texture_view_set_swizzle, NvnTextureView, texture_view,
    b->flags |= NVN_VIEW_SWIZZLE; for (unsigned i = 0; i < 4; i++) b->swizzle[i] = (u32)c->x[i + 1])
RESOURCE_BUILDER_HANDLER(texture_view_set_depth_stencil_mode, NvnTextureView, texture_view,
    b->flags |= NVN_VIEW_DEPTH_STENCIL_MODE; b->depth_stencil_mode = (u32)c->x[1])
RESOURCE_BUILDER_HANDLER(texture_view_set_target, NvnTextureView, texture_view,
    b->flags |= NVN_VIEW_TARGET; b->target = (u32)c->x[1])

static void h_texture_builder_set_packaged_texture_layout(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    if (native_guest_readable(c->x[1], sizeof(uint32_t)))
        texture_builder(c->x[0])->tile = nvn_texture_packaged_tile(gr32(c->x[1]));
    else
        LOG("nvn-native: packaged texture layout unreadable address=%llx\n", (unsigned long long)c->x[1]);
    pm_unlock(&objs_lock);
}

#undef RESOURCE_BUILDER_HANDLER

static void h_sampler_initialize(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    NvnSampler b = *sampler_builder(c->x[1]);
    *(NvnSampler*)rec_get(c->x[0], NVN_OBJ_SAMPLER, sizeof b) = b;
    c->x[0] = 1;
    pm_unlock(&objs_lock);
}
/* Find only: a failed link must not create a pool record. Caller holds objs_lock. */
static void* known_record(u64 addr, NvnObjKind kind) {
    NvnObj* o = nvn_objtab_find(&objs, addr);
    return o && o->kind == (uint32_t)kind ? o->data : NULL;
}
/* Caller holds objs_lock. Both descriptor types use signed indices and 32-byte entries. */
static int descriptor_destination(CPU* c, NvnObjKind kind, const char* operation, uint64_t* at) {
    DescriptorPoolRec* pool = known_record(c->x[0], kind);
    if (!pool) {
        LOG("nvn-native: %s unknown descriptor pool=%llx\n", operation, (unsigned long long)c->x[0]);
        return 0;
    }
    MemoryPoolRec* memory = known_record(pool->memory_pool, NVN_OBJ_MEMORY_POOL);
    if (!memory) {
        LOG("nvn-native: %s unknown memory pool=%llx\n", operation, (unsigned long long)pool->memory_pool);
        return 0;
    }
    uint64_t base = nvn_memory_pool_map(memory->flags, memory->storage);
    if (!base) {
        LOG("nvn-native: %s has no CPU base, memory pool=%llx flags=%x\n",
            operation, (unsigned long long)pool->memory_pool, memory->flags);
        return 0;
    }
    *at = base + pool->offset + (uint64_t)((int64_t)(int32_t)c->x[1] * NVN_SAMPLER_DESCRIPTOR_BYTES);
    return 1;
}
static void sampler_pool_register(CPU* c) {
    uint64_t at;
    if (!descriptor_destination(c, NVN_OBJ_SAMPLER_POOL, "sampler registration", &at)) return;
    NvnSampler* sampler = known_record(c->x[2], NVN_OBJ_SAMPLER);
    if (!sampler) {
        LOG("nvn-native: sampler registration unknown sampler=%llx\n", (unsigned long long)c->x[2]);
        return;
    }
    uint32_t words[NVN_SAMPLER_DESCRIPTOR_WORDS];
    nvn_sampler_descriptor(sampler, &nvn_sampler_device_words, words);
    for (unsigned i = 0; i < NVN_SAMPLER_DESCRIPTOR_WORDS; i++) gw32(at + i * sizeof(uint32_t), words[i]);
}
static void h_sampler_pool_register_sampler(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    sampler_pool_register(c);
    pm_unlock(&objs_lock);
}
#define TEXTURE_FORMAT_TABLE_POINTER 0xe03fd40ull
#define TEXTURE_FORMAT_RECORD_BYTES 0x30u
#define TEXTURE_FORMAT_SEED_OFFSET 0x24u
#define TEXTURE_FORMAT_DEPTH_OFFSET 0x0eu
#define TEXTURE_FORMAT_STENCIL_OFFSET 0x0fu
#define TEXTURE_FORMAT_ONE_OFFSET 0x11u
#define TEXTURE_FORMAT_SRGB_OFFSET 0x12u

static int texture_format_record(uint32_t format, NvnTextureFormatRecord* record) {
    if (format > NVN_TEXTURE_FORMAT_MAX) {
        LOG("nvn-native: texture registration invalid format=%x\n", format);
        return 0;
    }
    if (!native_guest_readable(TEXTURE_FORMAT_TABLE_POINTER, sizeof(uint64_t))) {
        LOG("nvn-native: texture registration unreadable table pointer at=%llx\n",
            (unsigned long long)TEXTURE_FORMAT_TABLE_POINTER);
        return 0;
    }
    uint64_t table = gr64(TEXTURE_FORMAT_TABLE_POINTER);
    uint64_t offset = (uint64_t)format * TEXTURE_FORMAT_RECORD_BYTES;
    if (!table || table > UINT64_MAX - offset || !native_guest_readable(table + offset, TEXTURE_FORMAT_RECORD_BYTES)) {
        LOG("nvn-native: texture registration unreadable format=%x table=%llx\n", format, (unsigned long long)table);
        return 0;
    }
    uint64_t at = table + offset;
    record->seed = gr32(at + TEXTURE_FORMAT_SEED_OFFSET);
    record->depth_size = *GP(at + TEXTURE_FORMAT_DEPTH_OFFSET);
    record->stencil_size = *GP(at + TEXTURE_FORMAT_STENCIL_OFFSET);
    record->one_code = *GP(at + TEXTURE_FORMAT_ONE_OFFSET);
    record->srgb = *GP(at + TEXTURE_FORMAT_SRGB_OFFSET);
    return 1;
}
static void texture_pool_register(CPU* c) {
    uint64_t at;
    if (!descriptor_destination(c, NVN_OBJ_TEXTURE_POOL, "texture registration", &at)) return;
    TextureRec* texture = known_record(c->x[2], NVN_OBJ_TEXTURE);
    if (!texture) {
        LOG("nvn-native: texture registration unknown texture=%llx\n", (unsigned long long)c->x[2]);
        return;
    }
    NvnTextureView* view = c->x[3] ? known_record(c->x[3], NVN_OBJ_TEXTURE_VIEW) : NULL;
    if (c->x[3] && !view) {
        LOG("nvn-native: texture registration unknown view=%llx\n", (unsigned long long)c->x[3]);
        return;
    }
    uint32_t format = view && (view->flags & NVN_VIEW_FORMAT) ? view->format : texture->builder.format;
    NvnTextureFormatRecord record;
    if (!texture_format_record(format, &record)) return;
    uint32_t words[NVN_TEXTURE_DESCRIPTOR_WORDS];
    NvnTextureDescriptorResult result = nvn_texture_descriptor(texture, view, &nvn_texture_device_words, &record, words);
    if (result != NVN_TEXTURE_DESCRIPTOR_OK) {
        LOG("nvn-native: texture registration invalid descriptor result=%u texture=%llx\n",
            (unsigned)result, (unsigned long long)c->x[2]);
        return;
    }
    for (unsigned i = 0; i < NVN_TEXTURE_DESCRIPTOR_WORDS; i++) gw32(at + i * sizeof(uint32_t), words[i]);
}
static void h_texture_pool_register_texture(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    texture_pool_register(c);
    pm_unlock(&objs_lock);
}
#define TEXTURE_GETTER(name, value) \
    static void h_texture_get_##name(CPU* c, u64 entry) { \
        (void)entry; \
        pm_lock(&objs_lock); \
        TextureRec* t = known_record(c->x[0], NVN_OBJ_TEXTURE); \
        if (!t) LOG("nvn-native: texture getter " #name " unknown texture=%llx\n", (unsigned long long)c->x[0]); \
        c->x[0] = t ? (value) : 0; \
        pm_unlock(&objs_lock); \
    }
TEXTURE_GETTER(width, t->builder.width)
TEXTURE_GETTER(height, t->builder.height)
TEXTURE_GETTER(stride, t->layout.linear_stride)
#undef TEXTURE_GETTER

static int texture_initialize(TextureRec* t, const NvnTextureBuilder* b) {
    if (b->gl_name) {
        LOG("nvn-native: texture import not supported\n");
        return 0;
    }
    MemoryPoolRec* pool = known_record(b->pool, NVN_OBJ_MEMORY_POOL);
    if (!pool) {
        LOG("nvn-native: texture initialization %s pool=%llx\n", b->pool ? "unknown" : "null",
            (unsigned long long)b->pool);
        return 0;
    }
    if (pool->flags & NVN_POOL_PHYSICAL) {
        LOG("nvn-native: texture initialization physical pool=%llx flags=%x\n",
            (unsigned long long)b->pool, pool->flags);
        return 0;
    }
    t->builder = *b;
    if (!nvn_texture_layout(b, &nvn_texture_device_words, &t->layout)) {
        LOG("nvn-native: texture initialization unsupported format=%x\n", b->format);
        return 0;
    }
    if ((b->flags & NVN_TEXTURE_FLAG_COMPRESS) && !(pool->flags & NVN_TEXTURE_POOL_STORAGE_FLAGS)) {
        LOG("nvn-native: texture initialization pool=%llx flags=%x needs texture storage flags=280\n",
            (unsigned long long)b->pool, pool->flags);
        return 0;
    }
    t->address = pool->gpu_address + b->offset;
    if (nvn_trace_active())
        LOG("nvn-native: texture format=%x flags=%x target=%u width=%u height=%u levels=%u size=%llx\n",
            b->format, b->flags, b->target, b->width, b->height, b->levels, (unsigned long long)t->layout.size);
    return 1;
}
static void h_texture_initialize(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    NvnTextureBuilder b = *texture_builder(c->x[1]);
    TextureRec* t = rec_get(c->x[0], NVN_OBJ_TEXTURE, sizeof *t);
    memset(t, 0, sizeof *t);
    c->x[0] = texture_initialize(t, &b);
    pm_unlock(&objs_lock);
}

/* G4: window and sync state stays in host records; guest object bytes stay untouched. */
#define WINDOW_BUILDER_HANDLER(name, call) \
    static void h_##name(CPU* c, u64 entry) { \
        (void)entry; pm_lock(&objs_lock); \
        NvnWindowBuilder* b = rec_get(c->x[0], NVN_OBJ_WINDOW_BUILDER, sizeof *b); \
        call; pm_unlock(&objs_lock); \
    }
WINDOW_BUILDER_HANDLER(window_builder_set_defaults, nvn_window_builder_defaults(b))
WINDOW_BUILDER_HANDLER(window_builder_set_device, nvn_window_builder_set_device(b, c->x[1]))
WINDOW_BUILDER_HANDLER(window_builder_set_native_window, nvn_window_builder_set_native_window(b, c->x[1]))
WINDOW_BUILDER_HANDLER(window_builder_set_textures, nvn_window_builder_set_textures(b, (u32)c->x[1], c->x[2]))
WINDOW_BUILDER_HANDLER(window_builder_set_present_interval, nvn_window_builder_set_interval(b, (u32)c->x[1]))
#undef WINDOW_BUILDER_HANDLER

static void h_window_initialize(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    NvnWindowBuilder* b = known_record(c->x[1], NVN_OBJ_WINDOW_BUILDER);
    if (!b) {
        LOG("nvn-native: window initialize failed: unknown builder (%llx)\n", (unsigned long long)c->x[1]);
        c->x[0] = 0; pm_unlock(&objs_lock); return;
    }
    if (!nvn_window_initialize_check(b->texture_count)) {
        LOG("nvn-native: window initialize failed: invalid texture count (%u)\n", b->texture_count);
        c->x[0] = 0; pm_unlock(&objs_lock); return;
    }
    uint64_t bytes = (uint64_t)b->texture_count * sizeof(uint64_t);
    if (!native_guest_readable(b->textures, bytes)) {
        LOG("nvn-native: window initialize failed: unreadable texture array (%llx, %u)\n",
            (unsigned long long)b->textures, b->texture_count);
        c->x[0] = 0; pm_unlock(&objs_lock); return;
    }
    WindowRec next = {0};
    next.device = b->device; next.native_window = b->native_window;
    next.texture_count = b->texture_count; next.interval = b->interval; next.last_index = -1;
    for (uint32_t i = 0; i < b->texture_count; i++) {
        uint64_t addr = gr64(b->textures + (uint64_t)i * sizeof(uint64_t));
        TextureRec* texture = known_record(addr, NVN_OBJ_TEXTURE);
        if (!texture) {
            LOG("nvn-native: window initialize failed: unknown texture (%u, %llx)\n", i, (unsigned long long)addr);
            c->x[0] = 0; pm_unlock(&objs_lock); return;
        }
        next.textures[i] = addr;
        next.states[i] = NVN_IMAGE_FREE;
        if (!i) next.crop = nvn_window_crop(0, 0, 0, 0, texture->builder.width, texture->builder.height);
    }
    NvnObj* o = nvn_objtab_put(&objs, c->x[0], NVN_OBJ_WINDOW, sizeof next);
    if (!o) fatal("nvn-native: out of memory for the record of a window\n");
    memcpy(o->data, &next, sizeof next);
    c->x[0] = 1;
    pm_unlock(&objs_lock);
}
static void h_window_set_present_interval(CPU* c, u64 entry) {
    (void)entry; pm_lock(&objs_lock);
    WindowRec* w = known_record(c->x[0], NVN_OBJ_WINDOW);
    if (!w) LOG("nvn-native: nvnWindowSetPresentInterval unknown window=%llx\n", (unsigned long long)c->x[0]);
    else w->interval = (u32)c->x[1];
    pm_unlock(&objs_lock);
}
static void h_window_set_crop(CPU* c, u64 entry) {
    (void)entry; pm_lock(&objs_lock);
    WindowRec* w = known_record(c->x[0], NVN_OBJ_WINDOW);
    if (!w) { LOG("nvn-native: nvnWindowSetCrop unknown window=%llx\n", (unsigned long long)c->x[0]); pm_unlock(&objs_lock); return; }
    TextureRec* t = known_record(w->textures[0], NVN_OBJ_TEXTURE);
    if (!t) {
        LOG("nvn-native: nvnWindowSetCrop first texture of window %llx is gone\n", (unsigned long long)c->x[0]);
        pm_unlock(&objs_lock);
        return;
    }
    w->crop = nvn_window_crop((u32)c->x[1], (u32)c->x[2], (u32)c->x[3], (u32)c->x[4],
                              t->builder.width, t->builder.height);
    pm_unlock(&objs_lock);
}
static void h_sync_initialize(CPU* c, u64 entry) {
    (void)entry; pm_lock(&objs_lock);
    SyncRec* s = rec_get(c->x[0], NVN_OBJ_SYNC, sizeof *s);
    s->device = c->x[1]; s->state = NVN_SYNC_UNSIGNALED;
    c->x[0] = 1; pm_unlock(&objs_lock);
}

enum { NVN_ACQUIRE_OK = 0, NVN_ACQUIRE_FAILED = 1 };
static void h_window_acquire_texture(CPU* c, u64 entry) {
    (void)entry;
    const u64 window_addr = c->x[0], sync_addr = c->x[1], index_addr = c->x[2];
    pm_lock(&objs_lock);
    WindowRec* window = known_record(window_addr, NVN_OBJ_WINDOW);
    SyncRec* sync = known_record(sync_addr, NVN_OBJ_SYNC);
    if (!window || !sync || !native_guest_readable(index_addr, sizeof(uint32_t))) {
        const char* reason = !window ? "unknown window" : !sync ? "unknown sync" : "unreadable index";
        LOG("nvn-native: nvnWindowAcquireTexture failed: %s (window=%llx sync=%llx index=%llx)\n",
            reason, (unsigned long long)window_addr, (unsigned long long)sync_addr, (unsigned long long)index_addr);
        if (native_guest_readable(index_addr, sizeof(uint32_t))) gw32(index_addr, NVN_WINDOW_INVALID_INDEX);
        c->x[0] = NVN_ACQUIRE_FAILED;
        pm_unlock(&objs_lock);
        return;
    }
    uint32_t interval = window->interval;
    pm_unlock(&objs_lock);
    present_pace(interval);
    pm_lock(&objs_lock);
    window = known_record(window_addr, NVN_OBJ_WINDOW);
    sync = known_record(sync_addr, NVN_OBJ_SYNC);
    if (!window || !sync || !native_guest_readable(index_addr, sizeof(uint32_t))) {
        const char* reason = !window ? "window changed" : !sync ? "sync changed" : "index changed";
        LOG("nvn-native: nvnWindowAcquireTexture failed: %s (window=%llx sync=%llx index=%llx)\n",
            reason, (unsigned long long)window_addr, (unsigned long long)sync_addr, (unsigned long long)index_addr);
        if (native_guest_readable(index_addr, sizeof(uint32_t))) gw32(index_addr, NVN_WINDOW_INVALID_INDEX);
        c->x[0] = NVN_ACQUIRE_FAILED;
        pm_unlock(&objs_lock);
        return;
    }
    int32_t index = nvn_window_pick_image(window->states, window->texture_count, window->last_index);
    if (index < 0) {
        LOG("nvn-native: nvnWindowAcquireTexture failed: no free window image (window=%llx states=%u,%u,%u,%u)\n",
            (unsigned long long)window_addr, window->states[0], window->states[1], window->states[2], window->states[3]);
        gw32(index_addr, NVN_WINDOW_INVALID_INDEX);
        c->x[0] = NVN_ACQUIRE_FAILED;
        pm_unlock(&objs_lock);
        return;
    }
    window->states[index] = NVN_IMAGE_ACQUIRED;
    window->last_index = index;
    /* A free image means its previous frame job has finished. */
    sync->state = NVN_SYNC_SIGNALED;
    gw32(index_addr, (uint32_t)index);
    c->x[0] = NVN_ACQUIRE_OK;
    pm_unlock(&objs_lock);
}
static void h_sync_wait(CPU* c, u64 entry) {
    (void)entry;
    pm_lock(&objs_lock);
    SyncRec* sync = known_record(c->x[0], NVN_OBJ_SYNC);
    if (!sync) {
        LOG("nvn-native: nvnSyncWait unknown sync=%llx\n", (unsigned long long)c->x[0]);
        c->x[0] = NVN_SYNC_WAIT_FAILED;
        pm_unlock(&objs_lock);
        return;
    }
    /* 3c adds the waiting states; timeout is ignored here. */
    c->x[0] = nvn_sync_wait_result(sync->state);
    pm_unlock(&objs_lock);
}

/* The host handlers by NVN function name. Later steps add their entries here (G2 to G5 of docs/nvn-design.md, 2-6). */
typedef struct {
    const char* name;
    GuestFn handler;
} NativeProc;
static const NativeProc native_procs[] = {
    {"nvnDeviceBuilderSetDefaults", h_device_builder_set_defaults},
    {"nvnDeviceBuilderSetFlags", h_device_builder_set_flags},
    {"nvnDeviceBuilderGetFlags", h_device_builder_get_flags},
    {"nvnDeviceInitialize", h_device_initialize},
    {"nvnDeviceSetWindowOriginMode", h_device_set_window_origin_mode},
    {"nvnDeviceGetWindowOriginMode", h_device_get_window_origin_mode},
    {"nvnDeviceSetDepthMode", h_device_set_depth_mode},
    {"nvnDeviceGetDepthMode", h_device_get_depth_mode},
    {"nvnDeviceGetInteger", h_device_get_integer},
    {"nvnDeviceGetTimestampInNanoseconds", h_device_get_timestamp_ns},
    {"nvnQueueBuilderSetDevice", h_queue_builder_set_device},
    {"nvnQueueBuilderGetDevice", h_queue_builder_get_device},
    {"nvnQueueBuilderSetDefaults", h_queue_builder_set_defaults},
    {"nvnQueueBuilderSetFlags", h_queue_builder_set_flags},
    {"nvnQueueBuilderGetFlags", h_queue_builder_get_flags},
    {"nvnQueueBuilderSetQueuePriority", h_queue_builder_set_priority},
    {"nvnQueueBuilderGetQueuePriority", h_queue_builder_get_priority},
    {"nvnQueueBuilderSetQueueMemory", h_queue_builder_set_queue_memory},
    {"nvnQueueBuilderGetMemory", h_queue_builder_get_memory},
    {"nvnQueueBuilderGetMemorySize", h_queue_builder_get_memory_size},
    {"nvnQueueBuilderSetCommandMemorySize", h_queue_builder_set_command_memory_size},
    {"nvnQueueBuilderGetCommandMemorySize", h_queue_builder_get_command_memory_size},
    {"nvnQueueBuilderSetComputeMemorySize", h_queue_builder_set_compute_memory_size},
    {"nvnQueueBuilderGetComputeMemorySize", h_queue_builder_get_compute_memory_size},
    {"nvnQueueBuilderSetControlMemorySize", h_queue_builder_set_control_memory_size},
    {"nvnQueueBuilderGetControlMemorySize", h_queue_builder_get_control_memory_size},
    {"nvnQueueBuilderSetCommandFlushThreshold", h_queue_builder_set_command_flush_threshold},
    {"nvnQueueBuilderGetCommandFlushThreshold", h_queue_builder_get_command_flush_threshold},
    {"nvnQueueBuilderGetQueueMemorySize", h_queue_builder_get_queue_memory_size},
    {"nvnQueueInitialize", h_queue_initialize},
    {"nvnMemoryPoolBuilderSetDefaults", h_memory_pool_builder_set_defaults},
    {"nvnMemoryPoolBuilderSetDevice", h_memory_pool_builder_set_device},
    {"nvnMemoryPoolBuilderSetFlags", h_memory_pool_builder_set_flags},
    {"nvnMemoryPoolBuilderSetStorage", h_memory_pool_builder_set_storage},
    {"nvnMemoryPoolInitialize", h_memory_pool_initialize},
    {"nvnMemoryPoolFinalize", h_memory_pool_finalize},
    {"nvnMemoryPoolMap", h_memory_pool_map},
    {"nvnMemoryPoolGetFlags", h_memory_pool_get_flags},
    {"nvnMemoryPoolGetBufferAddress", h_memory_pool_get_buffer_address},
    {"nvnTexturePoolInitialize", h_texture_pool_initialize},
    {"nvnTexturePoolGetSize", h_texture_pool_get_size},
    {"nvnSamplerPoolInitialize", h_sampler_pool_initialize},
    {"nvnSamplerPoolGetSize", h_sampler_pool_get_size},
    {"nvnSamplerBuilderSetDevice", h_sampler_builder_set_device},
    {"nvnSamplerBuilderSetDefaults", h_sampler_builder_set_defaults},
    {"nvnSamplerBuilderSetMinMagFilter", h_sampler_builder_set_min_mag_filter},
    {"nvnSamplerBuilderSetWrapMode", h_sampler_builder_set_wrap_mode},
    {"nvnSamplerBuilderSetLodClamp", h_sampler_builder_set_lod_clamp},
    {"nvnSamplerBuilderSetLodBias", h_sampler_builder_set_lod_bias},
    {"nvnSamplerBuilderSetCompare", h_sampler_builder_set_compare},
    {"nvnSamplerBuilderSetBorderColor", h_sampler_builder_set_border_color},
    {"nvnSamplerBuilderSetMaxAnisotropy", h_sampler_builder_set_max_anisotropy},
    {"nvnSamplerBuilderSetReductionFilter", h_sampler_builder_set_reduction_filter},
    {"nvnSamplerBuilderSetLodSnap", h_sampler_builder_set_lod_snap},
    {"nvnTextureBuilderSetDevice", h_texture_builder_set_device},
    {"nvnTextureBuilderSetDefaults", h_texture_builder_set_defaults},
    {"nvnTextureBuilderSetFlags", h_texture_builder_set_flags},
    {"nvnTextureBuilderSetTarget", h_texture_builder_set_target},
    {"nvnTextureBuilderSetSize2D", h_texture_builder_set_size_2d},
    {"nvnTextureBuilderSetLevels", h_texture_builder_set_levels},
    {"nvnTextureBuilderSetFormat", h_texture_builder_set_format},
    {"nvnTextureBuilderSetSwizzle", h_texture_builder_set_swizzle},
    {"nvnTextureBuilderSetDepthStencilMode", h_texture_builder_set_depth_stencil_mode},
    {"nvnTextureBuilderSetStorage", h_texture_builder_set_storage},
    {"nvnTextureBuilderSetStride", h_texture_builder_set_stride},
    {"nvnTextureBuilderGetWidth", h_texture_builder_get_width},
    {"nvnTextureBuilderGetHeight", h_texture_builder_get_height},
    {"nvnTextureBuilderGetStorageAlignment", h_texture_builder_get_storage_alignment},
    {"nvnTextureBuilderGetStorageSize", h_texture_builder_get_storage_size},
    {"nvnSamplerInitialize", h_sampler_initialize},
    {"nvnSamplerPoolRegisterSampler", h_sampler_pool_register_sampler},
    {"nvnTextureInitialize", h_texture_initialize},
    {"nvnWindowBuilderSetDefaults", h_window_builder_set_defaults},
    {"nvnWindowBuilderSetDevice", h_window_builder_set_device},
    {"nvnWindowBuilderSetNativeWindow", h_window_builder_set_native_window},
    {"nvnWindowBuilderSetTextures", h_window_builder_set_textures},
    {"nvnWindowBuilderSetPresentInterval", h_window_builder_set_present_interval},
    {"nvnWindowInitialize", h_window_initialize},
    {"nvnWindowSetPresentInterval", h_window_set_present_interval},
    {"nvnWindowSetCrop", h_window_set_crop},
    {"nvnSyncInitialize", h_sync_initialize},
    {"nvnWindowAcquireTexture", h_window_acquire_texture},
    {"nvnSyncWait", h_sync_wait},
    {"nvnTextureBuilderSetWidth", h_texture_builder_set_width},
    {"nvnTextureBuilderSetHeight", h_texture_builder_set_height},
    {"nvnTextureBuilderSetDepth", h_texture_builder_set_depth},
    {"nvnTextureBuilderSetPackagedTextureLayout", h_texture_builder_set_packaged_texture_layout},
    {"nvnTextureViewSetDefaults", h_texture_view_set_defaults},
    {"nvnTextureViewSetLevels", h_texture_view_set_levels},
    {"nvnTextureViewSetLayers", h_texture_view_set_layers},
    {"nvnTextureViewSetFormat", h_texture_view_set_format},
    {"nvnTextureViewSetSwizzle", h_texture_view_set_swizzle},
    {"nvnTextureViewSetDepthStencilMode", h_texture_view_set_depth_stencil_mode},
    {"nvnTextureViewSetTarget", h_texture_view_set_target},
    {"nvnTextureGetWidth", h_texture_get_width},
    {"nvnTextureGetHeight", h_texture_get_height},
    {"nvnTextureGetStride", h_texture_get_stride},
    {"nvnTexturePoolRegisterTexture", h_texture_pool_register_texture},
    {NULL, NULL},
};

typedef struct {
    u64 addr, calls;
    GuestFn handler;
    int missing; /* the handler is the missing-function handler */
    char name[128];
} NativeSlot;
#define NATIVE_SLOTS 1024 /* the game resolves 523 addresses */
static NativeSlot slots[NATIVE_SLOTS];
static u32 nslots;
/* Registration takes the lock and publishes a slot (addr, release ordering) once it is filled in. Calls read the slots
   without the lock. */
static PMutex slots_lock = PMUTEX_INIT;
static GuestFn bootstrap_original, getproc_original;
static u64 getproc_addr; /* the address of nvnDeviceGetProcAddress, hooked */
static int active;
static void getproc_native(CPU* c, u64 entry);

static u32 slot_hash(u64 addr) { return (u32)((addr >> 2) * 2654435761u) & (NATIVE_SLOTS - 1); }
static NativeSlot* slot_find(u64 addr) {
    for (u32 n = 0, i = slot_hash(addr); n < NATIVE_SLOTS; n++, i = (i + 1) & (NATIVE_SLOTS - 1)) {
        u64 a = __atomic_load_n(&slots[i].addr, __ATOMIC_ACQUIRE);
        if (a == addr) return &slots[i];
        if (!a) return NULL;
    }
    return NULL;
}

/* The function that g_disp holds for every host handler. A guest function ends with RET, which sets c->pc to x30, and
   the caller's check_return compares c->pc with its return address. A handler leaves c->pc at the caller's last block
   entry, so without this step the caller runs that block again. The handlers do not change x30. */
static void native_call(CPU* c, u64 entry) {
    NativeSlot* s = slot_find(entry);
    if (!s) fatal("nvn-native: a host handler runs for an address it never registered (%llx)\n", (unsigned long long)entry);
    u64 lr = c->x[30];
    s->handler(c, entry);
    c->pc = lr;
}

static void missing_handler(CPU* c, u64 entry) {
    NativeSlot* s = slot_find(entry);
    if (!s) fatal("nvn-native: the missing-function handler runs for an address it never registered (%llx)\n",
                  (unsigned long long)entry);
    if (!__atomic_fetch_add(&s->calls, 1, __ATOMIC_RELAXED)) /* the first call: what the game asked for */
        LOG("nvn-native: not implemented: %s (x0=%llx x1=%llx x2=%llx x3=%llx), returns 0\n", s->name,
            (unsigned long long)c->x[0], (unsigned long long)c->x[1], (unsigned long long)c->x[2],
            (unsigned long long)c->x[3]);
    c->x[0] = 0;
}

static GuestFn handler_for(const char* name, int* missing) {
    for (const NativeProc* p = native_procs; p->name; p++)
        if (!strcmp(p->name, name)) {
            *missing = 0;
            return p->handler;
        }
    *missing = 1;
    return missing_handler;
}

/* The handler of the function that the game resolved as name at addr */
static void install(const char* name, u64 addr) {
    if (addr < g_code_lo || addr >= g_code_hi || (addr & 3)) {
        LOG_LIMIT(8, "nvn-native: %s resolved to %llx, which is not a recompiled function\n", name,
                  (unsigned long long)addr);
        return;
    }
    pm_lock(&slots_lock);
    NativeSlot* s = slot_find(addr);
    if (addr == getproc_addr) { /* the lookup function keeps its hook */
        pm_unlock(&slots_lock);
        return;
    }
    if (s) {
        /* identical functions folded to one address: their calls count under the first name */
        if (strcmp(s->name, name)) LOG_LIMIT(8, "nvn-native: %s shares its address with %s\n", name, s->name);
    } else if (nslots < NATIVE_SLOTS) {
        u32 i = slot_hash(addr);
        while (slots[i].addr) i = (i + 1) & (NATIVE_SLOTS - 1);
        s = &slots[i];
        memcpy(s->name, name, strnlen(name, sizeof s->name - 1) + 1);
        s->handler = handler_for(s->name, &s->missing);
        if (!s->missing) LOG_LIMIT(64, "nvn-native: host handler for %s at %llx\n", s->name, (unsigned long long)addr);
        __atomic_store_n(&s->addr, addr, __ATOMIC_RELEASE);
        nslots++;
        __atomic_store_n(&g_disp[(addr - g_code_lo) / 4], native_call, __ATOMIC_RELEASE);
    } else {
        fatal("nvn-native: more than %u resolved NVN functions\n", NATIVE_SLOTS);
    }
    pm_unlock(&slots_lock);
}

/* nvnDeviceGetProcAddress(device, name). The library's own function runs for the lookup: it returns the address, and
   the host puts its handler there. Open (docs/nvn-design.md, 2-3, item 6): if it reads the device object, which the
   library never initialized in this mode. The first run shows it. */
static void getproc_native(CPU* c, u64 entry) {
    char name[128];
    int valid = nvn_guest_name(c->x[1], name);
    GuestFn original = __atomic_load_n(&getproc_original, __ATOMIC_ACQUIRE);
    if (!original) fatal("nvn-native: the nvnDeviceGetProcAddress hook runs before its original is known\n");
    original(c, entry);
    if (valid && c->x[0]) install(name, c->x[0]);
}

static void bootstrap_native(CPU* c, u64 entry) {
    char name[128];
    int valid = nvn_guest_name(c->x[0], name); /* nvnBootstrapLoader(name) */
    bootstrap_original(c, entry);
    if (!valid || !c->x[0]) return;
    if (strcmp(name, "nvnDeviceGetProcAddress")) {
        install(name, c->x[0]);
        return;
    }
    u64 resolver = c->x[0];
    if (resolver < g_code_lo || resolver >= g_code_hi || (resolver & 3)) return;
    pm_lock(&slots_lock);
    GuestFn* slot = &g_disp[(resolver - g_code_lo) / 4];
    if (*slot && *slot != getproc_native) {
        __atomic_store_n(&getproc_original, *slot, __ATOMIC_RELEASE);
        getproc_addr = resolver;
        __atomic_store_n(slot, getproc_native, __ATOMIC_RELEASE);
    }
    pm_unlock(&slots_lock);
}

void nvn_native_init(void) {
    if (!g_settings.nvn_native) return;
    u64 entry = kernel_find_symbol_exact("nvnBootstrapLoader");
    if (!entry || entry < g_code_lo || entry >= g_code_hi || (entry & 3)) {
        WARN("nvn-native: bootstrap loader symbol is unavailable\n");
        return;
    }
    GuestFn* slot = &g_disp[(entry - g_code_lo) / 4];
    if (!*slot) {
        WARN("nvn-native: bootstrap loader has no recompiled entry\n");
        return;
    }
    bootstrap_original = *slot;
    *slot = bootstrap_native; /* before any guest thread runs */
    active = 1;
    LOG("nvn-native: on, the host answers the NVN functions that the game resolves (%u handlers)\n",
        (u32)(sizeof native_procs / sizeof native_procs[0] - 1));
}

int nvn_native_active(void) { return active; }

GuestFn nvn_native_handler(const char* name) {
    int missing;
    GuestFn h = handler_for(name, &missing);
    return missing ? NULL : h;
}

void nvn_native_report(void) {
    if (!active) return;
    pm_lock(&slots_lock);
    u32 missing = 0, called = 0;
    for (u32 i = 0; i < NATIVE_SLOTS; i++) {
        if (!slots[i].addr) continue;
        u64 calls = __atomic_load_n(&slots[i].calls, __ATOMIC_RELAXED);
        if (!slots[i].missing) continue; /* a real handler: the trace counts its calls */
        missing++;
        if (!calls) continue;
        called++;
        LOG("nvn-native: missing %s %llu\n", slots[i].name, (unsigned long long)calls);
    }
    LOG("nvn-native: %u resolved addresses, %u without a handler, %u of them called\n", nslots, missing, called);
    pm_unlock(&slots_lock);
}
