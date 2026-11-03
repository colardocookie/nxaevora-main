/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/gpu/nvn_params.h"
#include <stddef.h>
#include <math.h>
#include <string.h>

void nvn_window_builder_defaults(NvnWindowBuilder* b) {
    memset(b, 0, sizeof *b);
    b->interval = 1;
}

void nvn_window_builder_set_device(NvnWindowBuilder* b, uint64_t device) { b->device = device; }
void nvn_window_builder_set_native_window(NvnWindowBuilder* b, uint64_t native_window) { b->native_window = native_window; }
void nvn_window_builder_set_textures(NvnWindowBuilder* b, uint32_t count, uint64_t textures) {
    b->texture_count = count;
    b->textures = textures;
}
void nvn_window_builder_set_interval(NvnWindowBuilder* b, uint32_t interval) { b->interval = interval; }
int nvn_window_initialize_check(uint32_t texture_count) {
    return texture_count != 0 && texture_count <= NVN_WINDOW_MAX_TEXTURES;
}
int32_t nvn_window_pick_image(const uint8_t* states, uint32_t count, int32_t last) {
    if (!states || !count || count > NVN_WINDOW_MAX_TEXTURES) return NVN_WINDOW_NO_IMAGE;
    uint32_t start = last >= 0 && (uint32_t)last < count ? (uint32_t)last + 1u : 0u;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t index = (start + i) % count;
        if (states[index] == NVN_IMAGE_FREE) return (int32_t)index;
    }
    return NVN_WINDOW_NO_IMAGE;
}
NvnSyncWaitResult nvn_sync_wait_result(NvnSyncState state) {
    return state == NVN_SYNC_SIGNALED ? NVN_SYNC_WAIT_SIGNALED : NVN_SYNC_WAIT_FAILED;
}
NvnWindowCrop nvn_window_crop(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                              uint32_t texture_width, uint32_t texture_height) {
    if (!width || !height) return (NvnWindowCrop){0, 0, texture_width, texture_height};
    return (NvnWindowCrop){x, y, width, height};
}

/* nvnDeviceGetInteger of the run of 2026-11-02 (title screen). The library takes a value from a device field or from a
   constant, and the run does not say which. */
static const struct {
    uint32_t what;
    int32_t value;
} device_integers[] = {
    {4, 256},   {15, 32},   {16, 32},   {17, 256},  {18, 256},  {19, 4},    {20, 8},    {40, 1},
    {41, 1},    {42, 1},    {50, 1},    {59, 1},    {66, 1},    {67, 1},    {68, 1},    {69, 1},
    {71, 1},    {72, 1},    {73, 1},    {74, 1},    {75, 1},    {76, 1},    {82, 65536}, {85, 262144},
    {87, 1},    {90, 0},    {92, 16384},
};

int nvn_device_integer(uint32_t what, int32_t* value) {
    for (size_t i = 0; i < sizeof device_integers / sizeof device_integers[0]; i++)
        if (device_integers[i].what == what) {
            *value = device_integers[i].value;
            return 1;
        }
    return 0;
}

/* The library's ratio, at device +0x58 and +0x5c: 625 / 384 (the 614.4 MHz GPU timer to 1 GHz) */
#define TIMESTAMP_NUM 0x271u
#define TIMESTAMP_DEN 0x180u

uint64_t nvn_timestamp_ns(uint64_t ticks) {
    uint64_t whole = ticks / TIMESTAMP_DEN, rest = ticks % TIMESTAMP_DEN;
    return whole * TIMESTAMP_NUM + rest * TIMESTAMP_NUM / TIMESTAMP_DEN;
}

const NvnQueueDeviceWords nvn_queue_device_words = {.w8c = 0x80, .w94 = 2, .w98 = 1, .we0 = 1, .we4 = 0x1000, .w1af4 = 0};

void nvn_queue_builder_defaults(NvnQueueBuilder* b) {
    uint64_t device = b->device; /* SetDefaults does not touch the device */
    memset(b, 0, sizeof *b);
    b->device = device;
    b->priority = NVN_QUEUE_PRIORITY_DEFAULT;
}

#define QUEUE_FLAG_NO_A 0x1u /* skips the first term (NVN: NO_FRAGMENT_INTERLOCK, name not verified) */
#define QUEUE_FLAG_NO_B 0x2u /* skips the aligned term (NVN: NO_ZCULL, name not verified) */
#define DEFAULT_COMMAND_SIZE 0x10000
#define DEFAULT_COMPUTE_SIZE 0x40000
#define DEFAULT_CONTROL_SIZE 0x4000
#define DEV_FLAG_BIT8 0x100u /* of device word +0x1af4: it picks the sizes of the per-slot areas */

/* The port of the library function at d8383f0 (update 1.1.1), step by step, in the same integer widths. */
uint64_t nvn_queue_memory_size(const NvnQueueBuilder* b, const NvnQueueDeviceWords* dev) {
    int64_t command = b->has_command ? b->command : DEFAULT_COMMAND_SIZE;
    int64_t control = b->has_control ? b->control : DEFAULT_CONTROL_SIZE;
    int64_t compute = b->has_compute ? b->compute : DEFAULT_COMPUTE_SIZE;
    int bit8 = (dev->w1af4 & DEV_FLAG_BIT8) != 0;

    uint32_t area = 0;
    if (!(b->flags & QUEUE_FLAG_NO_A)) area = (0xFFu | (dev->w98 << 10)) + ((dev->w8c * dev->w94) << 2);
    area += bit8 ? 0x9400u : 0x7B00u;
    if (!(b->flags & QUEUE_FLAG_NO_B) && dev->we0) {
        uint32_t align = dev->we4, mask = -align, last = align - 1;
        uint32_t first = (dev->we0 + last) & mask;
        area = ((area + last) & mask);
        area += ((first + last) & mask);
    }
    uint64_t slot = bit8 ? 0x700u : 0x200u;
    uint64_t slots = (uint64_t)compute / (slot + 0x600u);
    area = (area + 0xFFu) & 0xFFFFFF00u;
    area += (uint32_t)slots * 0x600u;
    area += (uint32_t)slots * (uint32_t)slot;
    area = (area + 0xFFFu) & 0xFFFFF000u;
    return (uint64_t)(command + control) + area;
}

void nvn_memory_pool_builder_defaults(NvnMemoryPoolBuilder* b) {
    uint64_t device = b->device;
    memset(b, 0, sizeof *b);
    b->device = device;
    b->flags = NVN_POOL_DEFAULT_FLAGS;
}

NvnPoolResult nvn_memory_pool_params(const NvnMemoryPoolBuilder* b, uint64_t page_size, uint32_t* flags) {
    uint32_t f = b->flags;
    uint32_t cpu = (f & NVN_POOL_CPU_UNCACHED) ? NVN_POOL_CPU_UNCACHED :
                   (f & NVN_POOL_CPU_CACHED) ? NVN_POOL_CPU_CACHED :
                   (f & NVN_POOL_CPU_NO_ACCESS) ? NVN_POOL_CPU_NO_ACCESS : NVN_POOL_CPU_UNCACHED;
    uint32_t gpu = (f & NVN_POOL_GPU_CACHED) ? NVN_POOL_GPU_CACHED :
                   (f & NVN_POOL_GPU_UNCACHED) ? NVN_POOL_GPU_UNCACHED :
                   (f & NVN_POOL_GPU_NO_ACCESS) ? NVN_POOL_GPU_NO_ACCESS : NVN_POOL_GPU_CACHED;
    if (f & NVN_POOL_VIRTUAL) cpu = NVN_POOL_CPU_NO_ACCESS;
    if (f & NVN_POOL_PHYSICAL) gpu = NVN_POOL_GPU_NO_ACCESS;
    *flags = (f & ~(NVN_POOL_CPU_NO_ACCESS | NVN_POOL_CPU_UNCACHED | NVN_POOL_CPU_CACHED |
                    NVN_POOL_GPU_NO_ACCESS | NVN_POOL_GPU_UNCACHED | NVN_POOL_GPU_CACHED)) | cpu | gpu;
    if (f & NVN_POOL_VIRTUAL) return NVN_POOL_UNSUPPORTED_VIRTUAL;
    if (f & NVN_POOL_IMPORT) return NVN_POOL_UNSUPPORTED_IMPORT;
    if (!b->storage || !b->size || b->storage % NVN_POOL_STORAGE_ALIGNMENT ||
        b->size % NVN_POOL_STORAGE_ALIGNMENT) return NVN_POOL_INVALID_STORAGE;
    if (page_size < NVN_POOL_STORAGE_ALIGNMENT || page_size % NVN_POOL_STORAGE_ALIGNMENT ||
        ((f & NVN_POOL_PHYSICAL) && b->size % page_size)) return NVN_POOL_INVALID_PAGE_SIZE;
    return NVN_POOL_OK;
}

int nvn_pool_gpu_allocate(uint64_t cursor, uint64_t size, uint64_t page_size,
                          uint64_t* address, uint64_t* next) {
    if (!size || page_size < NVN_POOL_STORAGE_ALIGNMENT || page_size % NVN_POOL_STORAGE_ALIGNMENT ||
        cursor < NVN_POOL_GPU_BASE || cursor >= NVN_POOL_GPU_END) return 0;
    uint64_t padding = (page_size - cursor % page_size) % page_size;
    if (padding >= NVN_POOL_GPU_END - cursor) return 0;
    uint64_t base = cursor + padding;
    if (size > NVN_POOL_GPU_END - base) return 0;
    uint64_t end = base + size;
    padding = (page_size - end % page_size) % page_size;
    if (padding > NVN_POOL_GPU_END - end) return 0;
    *address = base;
    *next = end + padding;
    return 1;
}

uint64_t nvn_memory_pool_map(uint32_t flags, uint64_t storage) {
    return (flags & NVN_POOL_CPU_NO_ACCESS) ? 0 : storage;
}

uint32_t nvn_memory_pool_flags(uint32_t flags) { return flags & NVN_POOL_PUBLIC_FLAGS; }

const NvnSamplerDeviceWords nvn_sampler_device_words = {
    NVN_SAMPLER_DEVICE_LOD_BIAS, NVN_SAMPLER_DEVICE_FILTER_FLAGS
};
const NvnTextureDeviceWords nvn_texture_device_words = {
    NVN_TEXTURE_DEVICE_PAGE_SIZE, NVN_TEXTURE_DEVICE_COLOR_COMPRESSION,
    NVN_TEXTURE_DEVICE_DEPTH_COMPRESSION, NVN_TEXTURE_DEVICE_BUFFER_KIND,
    NVN_TEXTURE_DEVICE_COMPRESSION_ENABLE,
    NVN_TEXTURE_DEVICE_DIMENSION_LIMIT, NVN_TEXTURE_DEVICE_SECTOR_LAYOUT,
    NVN_TEXTURE_DEVICE_HEADER_ENABLE, NVN_TEXTURE_DEVICE_QUALITY,
    NVN_TEXTURE_DEVICE_QUALITY_OVERRIDE, NVN_TEXTURE_DEVICE_DESCRIPTOR_ROW
};

void nvn_sampler_defaults(NvnSampler* s) {
    uint64_t device = s->device;
    memset(s, 0, sizeof *s);
    s->device = device;
    s->min_filter = s->mag_filter = NVN_SAMPLER_FILTER_DEFAULT;
    for (unsigned i = 0; i < 3; i++) s->wrap[i] = NVN_SAMPLER_WRAP_DEFAULT;
    s->max_lod = NVN_SAMPLER_MAX_LOD_DEFAULT;
    s->compare = NVN_SAMPLER_COMPARE_DEFAULT;
    s->anisotropy = NVN_SAMPLER_ANISOTROPY_DEFAULT;
}

#define SAMPLER_SNAP_SEGMENT_SIZE 8u
#define SAMPLER_SNAP_LAST_INDEX 31u

/* T[i] = (8-d)/(2*d), d=(8+i%8)*2^(i/8). One division rounds the exact rational to f32.
   All 32 results match the module at de87870, including the positive zero at index 0. */
float nvn_sampler_snap_bias(uint32_t index) {
    uint32_t d = (SAMPLER_SNAP_SEGMENT_SIZE + (index & 7u)) << ((index >> 3) & 31u);
    return (float)(8.0 - (double)d) / (float)(2.0 * (double)d);
}

#define SAMPLER_ROUND_MAGIC 12582912.0f
#define SAMPLER_LOD_LIMIT 15.0f
#define SAMPLER_LOD_SCALE 256.0f
#define SAMPLER_DEVICE_BIAS_SCALE 0.0625f
#define SAMPLER_FIXED_WORD0 0x26000u
#define SAMPLER_FIXED_WORD1 0x200u
#define SAMPLER_SRGB_THRESHOLD 0x1.9a5c38p-9f
#define SAMPLER_SRGB_LINEAR 0x1.9d70a4p+3f
#define SAMPLER_SRGB_EXPONENT 0x1.aaa8ecp-2f
#define SAMPLER_SRGB_SCALE 0x1.0e147ap+0f
#define SAMPLER_SRGB_OFFSET (-0x1.c28f5cp-5f)

static uint32_t float_bits(float f) {
    uint32_t u;
    memcpy(&u, &f, sizeof u);
    return u;
}
static float bits_float(uint32_t u) {
    float f;
    memcpy(&f, &u, sizeof f);
    return f;
}
static uint32_t sampler_border_byte(uint32_t bits) {
    float x = bits_float(bits);
    if (x <= 0) return 0;
    if (x > 1) return 255;
    float y = x < SAMPLER_SRGB_THRESHOLD ? x * SAMPLER_SRGB_LINEAR :
              powf(x, SAMPLER_SRGB_EXPONENT) * SAMPLER_SRGB_SCALE + SAMPLER_SRGB_OFFSET;
    return float_bits(y * 255.0f + SAMPLER_ROUND_MAGIC) & 255u;
}
static uint32_t sampler_anisotropy(float value) {
    static const uint8_t codes[] = {0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 6, 6, 7};
    if (!(value > 1)) return 0;
    if (value >= 16) return 7;
    uint32_t rounded = float_bits(value + SAMPLER_ROUND_MAGIC) - float_bits(SAMPLER_ROUND_MAGIC);
    return codes[rounded - 1];
}
void nvn_sampler_descriptor(const NvnSampler* s, const NvnSamplerDeviceWords* dev,
                            uint32_t out[NVN_SAMPLER_DESCRIPTOR_WORDS]) {
    static const uint8_t wraps[] = {4, 0, 7, 5, 6, 3, 1, 2};
    memset(out, 0, NVN_SAMPLER_DESCRIPTOR_BYTES);
    for (unsigned i = 0; i < 3; i++) out[0] |= (uint32_t)wraps[s->wrap[i] & 7u] << (3 * i);
    out[0] |= SAMPLER_FIXED_WORD0 | (sampler_anisotropy(s->anisotropy) << 20);
    if (s->compare_enable) out[0] |= 0x200u | (((s->compare + 7u) & 7u) << 10);
    uint32_t min = s->min_filter;
    out[1] = 0x11u + (s->mag_filter & 1u) + ((min & 1u) << 4) +
             (((((min + ((int32_t)min < 0)) << 5) & 0xc0u) + 0x40u) & 0xc0u);
    uint32_t snap = 0;
    if (s->lod_snap > 0)
        while (snap < SAMPLER_SNAP_LAST_INDEX && s->lod_snap > -nvn_sampler_snap_bias(snap)) snap++;
    float bias = s->lod_bias + (float)dev->w1a80 * SAMPLER_DEVICE_BIAS_SCALE;
    bias = fminf(SAMPLER_LOD_LIMIT, fmaxf(-SAMPLER_LOD_LIMIT, bias + nvn_sampler_snap_bias(snap)));
    out[1] |= (((uint32_t)(int32_t)(bias * SAMPLER_LOD_SCALE) & 0x1fffu) << 12) |
              (snap << 26) | (dev->w1a84 & 0x100u) | SAMPLER_FIXED_WORD1;
    if (s->reduction == 1) out[1] |= 0x400u;
    if (s->reduction == 2) out[1] |= 0x800u;
    float lo = fminf(SAMPLER_LOD_LIMIT, fmaxf(0, s->min_lod));
    float hi = fminf(SAMPLER_LOD_LIMIT, fmaxf(lo, s->max_lod));
    out[2] = ((uint32_t)(lo * SAMPLER_LOD_SCALE) & 0xfffu) |
             (((uint32_t)(hi * SAMPLER_LOD_SCALE) & 0xfffu) << 12) |
             (sampler_border_byte(s->border[0]) << 24);
    out[3] = (sampler_border_byte(s->border[1]) << 12) | (sampler_border_byte(s->border[2]) << 20);
    memcpy(out + 4, s->border, sizeof s->border);
}

void nvn_texture_builder_defaults(NvnTextureBuilder* b) {
    uint64_t device = b->device;
    memset(b, 0, sizeof *b);
    b->device = device;
    b->target = NVN_TEXTURE_TARGET_2D;
    b->levels = 1;
    for (unsigned i = 0; i < 4; i++) b->swizzle[i] = i + 2;
}

typedef struct { uint8_t bytes, width, height, depth_stencil; } TextureFormat;
static int texture_format(uint32_t f, TextureFormat* out) {
    if (!f || f > NVN_TEXTURE_FORMAT_MAX || (f >= 0x19 && f <= 0x21) || (f >= 0x51 && f <= 0x6c)) return 0;
    *out = (TextureFormat){4, 1, 1, 0};
    if (f <= 4 || f == 0x31) out->bytes = 1;
    if ((f >= 5 && f <= 9) || (f >= 0xd && f <= 0x10) || f == 0x32 ||
        (f >= 0x39 && f <= 0x3c) || (f >= 0x95 && f <= 0x98)) out->bytes = 2;
    if ((f >= 0x16 && f <= 0x18) || (f >= 0x29 && f <= 0x2d) || f == 0x36 ||
        (f >= 0x71 && f <= 0x75) || f == 0x41 || f == 0x42 || f == 0x45 || f == 0x46 ||
        f == 0x49 || f == 0x4a || f == 0x9d || f == 0x9f) out->bytes = 8;
    if (f >= 0x22 && f <= 0x24) out->bytes = 12;
    if ((f >= 0x2e && f <= 0x30) || (f >= 0x76 && f <= 0x94) || f == 0x43 || f == 0x44 ||
        f == 0x47 || f == 0x48 || (f >= 0x4b && f <= 0x50) || f == 0x9e || f == 0xa0) out->bytes = 16;
    if ((f >= 0x41 && f <= 0x50) || (f >= 0x79 && f <= 0x94) || f >= 0x9d)
        out->width = out->height = 4;
    if ((f >= 0x7a && f <= 0x86) || (f >= 0x88 && f <= 0x94)) {
        static const uint8_t widths[] = {5, 5, 6, 6, 8, 8, 8, 10, 10, 10, 10, 12, 12};
        static const uint8_t heights[] = {4, 5, 5, 6, 5, 6, 8, 5, 6, 8, 10, 10, 12};
        unsigned i = f - (f >= 0x88 ? 0x88 : 0x7a);
        out->width = widths[i];
        out->height = heights[i];
    }
    if (f >= 0x32 && f <= 0x36) out->depth_stencil |= 1;
    if (f == 0x31 || f == 0x35 || f == 0x36) out->depth_stencil |= 2;
    return 1;
}

static int texture_page_round(const NvnTextureBuilder* b, const NvnTextureDeviceWords* dev, TextureFormat f) {
    return (b->format >= 0x31 && b->format <= 0x36) ||
           ((b->flags & NVN_TEXTURE_FLAG_COMPRESS) && dev->w1b1c && f.width == 1 && f.bytes >= 4 &&
            dev->w19f8 != NVN_TEXTURE_COMPRESSION_DISABLED);
}
static uint64_t texture_alignment(const NvnTextureBuilder* b, const NvnTextureDeviceWords* dev, TextureFormat f) {
    if (b->target == NVN_TEXTURE_TARGET_BUFFER) {
        uint32_t kind = dev->w1b18 & 255u;
        return kind != 0 && kind != NVN_TEXTURE_KIND_PITCH ? dev->wac : f.bytes == 12 ? 4 : f.bytes;
    }
    return texture_page_round(b, dev, f) ? dev->wac : NVN_TEXTURE_GOB_BYTES;
}
uint64_t nvn_texture_storage_alignment(const NvnTextureBuilder* b, const NvnTextureDeviceWords* dev) {
    if (b->flags & NVN_TEXTURE_FLAG_LINEAR_RENDER_TARGET) return NVN_TEXTURE_LINEAR_RT_ALIGNMENT;
    if (b->flags & NVN_TEXTURE_FLAG_LINEAR) return NVN_TEXTURE_LINEAR_ALIGNMENT;
    TextureFormat f;
    if (!texture_format(b->format, &f)) return 0;
    return texture_alignment(b, dev, f);
}
static uint32_t ceil_u32(uint32_t n, uint32_t d) { return d ? (n + d - 1) / d : 0; }
static uint64_t align_u64(uint64_t n, uint64_t a) { return (n + a - 1) & -a; }
static uint32_t tile_shrink(uint32_t exponent, uint32_t base, uint32_t n) {
    while (exponent && (base << ((exponent - 1) & 31u)) >= n) exponent--;
    return exponent;
}
static uint32_t mip_dimension(uint32_t n, uint32_t i) {
    n >>= i & 31u;
    return n ? n : 1;
}
static int texture_is_array(uint32_t target) {
    return target == NVN_TEXTURE_TARGET_1D_ARRAY || target == NVN_TEXTURE_TARGET_2D_ARRAY ||
           target == NVN_TEXTURE_TARGET_CUBE || target == NVN_TEXTURE_TARGET_CUBE_ARRAY;
}
static uint64_t texture_footprint(const NvnTextureLayout* l, uint32_t levels, int array,
                                 uint32_t x, uint32_t y, uint32_t z) {
    uint32_t p = 1u << (x & 31u);
    uint64_t sum = 0;
    uint32_t w = l->width * l->sample_x, h = l->height * l->sample_y;
    uint32_t depth = l->dim == 3 ? l->depth : 1;
    for (uint32_t i = 0; i < levels; i++) {
        uint32_t a = ceil_u32(mip_dimension(w, i), l->block_width);
        uint32_t rows = ceil_u32(mip_dimension(h, i), l->block_height);
        uint32_t d = l->dim == 3 ? mip_dimension(depth, i) : 1;
        uint32_t ey = tile_shrink(y, NVN_TEXTURE_GOB_HEIGHT, rows), ez = tile_shrink(z, 1, d);
        uint32_t q = rows >= (NVN_TEXTURE_GOB_HEIGHT << (y & 31u)) &&
                     a >= (NVN_TEXTURE_GOB_WIDTH / l->bytes_per_block) * p && (d >> (z & 31u)) ? p : 1;
        uint32_t nx = (ceil_u32(a * l->bytes_per_block, NVN_TEXTURE_GOB_WIDTH) + q - 1) & -q;
        sum += (uint64_t)nx * ceil_u32(rows, NVN_TEXTURE_GOB_HEIGHT << (ey & 31u)) *
               ceil_u32(d, 1u << (ez & 31u)) * ((uint64_t)NVN_TEXTURE_GOB_BYTES << ((ey + ez) & 63u));
    }
    if (array) {
        uint32_t shift = p >= 2 ? y + z :
            tile_shrink(y, NVN_TEXTURE_GOB_HEIGHT, ceil_u32(h, l->block_height)) + tile_shrink(z, 1, depth);
        uint32_t alignment = (NVN_TEXTURE_GOB_BYTES * (p >= 2 ? p : 1)) << (shift & 31u);
        sum = align_u64(sum, alignment);
    }
    return sum;
}
static int texture_sparse_tile(const NvnTextureBuilder* b, NvnTextureLayout* l, uint32_t levels) {
    if ((b->flags & NVN_TEXTURE_SPARSE_TILE_MASK) != NVN_TEXTURE_FLAG_SPARSE || b->target == NVN_TEXTURE_TARGET_1D ||
        b->target == NVN_TEXTURE_TARGET_1D_ARRAY || l->bytes_per_block == 12 || !levels) return 0;
    uint32_t bytes = l->bytes_per_block, x, y, z;
    if (l->dim == 3) {
        x = bytes == 4 ? 1 : bytes == 8 || bytes == 16 ? 2 : 0;
        y = bytes == 8 || bytes == 16 ? 1 : 2;
        z = bytes == 4 || bytes == 8 || bytes == 16 ? 4 : 5;
    } else {
        x = bytes == 2 || bytes == 4 ? 3 : bytes == 8 || bytes == 16 ? 4 : 2;
        y = bytes == 2 || bytes == 4 ? 4 : bytes == 8 || bytes == 16 ? 3 : 5;
        z = 0;
    }
    uint32_t w = ((NVN_TEXTURE_GOB_WIDTH / bytes) << x) / l->sample_x * l->block_width;
    uint32_t h = (NVN_TEXTURE_GOB_HEIGHT << y) / l->sample_y * l->block_height;
    if ((int32_t)l->width < (int32_t)w || (int32_t)l->height < (int32_t)h ||
        (int32_t)l->depth < (int32_t)(1u << z)) return 0;
    l->tile_x = x; l->tile_y = y; l->tile_z = z;
    return 1;
}
static void texture_choose_tile(const NvnTextureBuilder* b, NvnTextureLayout* l, uint32_t levels) {
    if (b->tile & 15u) {
        l->tile_x = (b->tile >> 12) & 15u;
        l->tile_y = (b->tile >> 4) & 15u;
        l->tile_z = (b->tile >> 8) & 15u;
        return;
    }
    if (b->flags & NVN_TEXTURE_FLAG_MINIMAL_TILE) { l->tile_y = 1; return; }
    if (texture_sparse_tile(b, l, levels)) return;
    if (l->dim == 3) {
        uint32_t v = l->depth + l->depth / 2;
        l->tile_z = v > 15 ? 4 : v >= 8 ? 3 : v > 3 ? 2 : v > 1 ? 1 : 0;
    } else {
        uint32_t hb = ceil_u32(l->height * l->sample_y, l->block_height), v = hb + hb / 2;
        l->tile_y = v >= 128 ? 4 : v >= 64 ? 3 : v > 31 ? 2 : v > 15 ? 1 : 0;
    }
    if ((b->flags & NVN_TEXTURE_FLAG_ADAPTIVE_TILE) &&
        texture_footprint(l, levels, texture_is_array(b->target), 0, l->tile_y, l->tile_z) !=
        texture_footprint(l, levels, texture_is_array(b->target), 0, 0, 0))
        l->tile_y = l->tile_z = 0;
}
int nvn_texture_layout(const NvnTextureBuilder* b, const NvnTextureDeviceWords* dev, NvnTextureLayout* l) {
    memset(l, 0, sizeof *l);
    l->alignment = nvn_texture_storage_alignment(b, dev);
    TextureFormat f;
    if (!texture_format(b->format, &f)) return 0;
    l->width = b->width; l->height = b->height; l->depth = b->depth;
    if (b->target == NVN_TEXTURE_TARGET_1D_ARRAY) { l->depth = l->height; l->height = 1; }
    if (b->target == NVN_TEXTURE_TARGET_CUBE) l->depth = NVN_TEXTURE_CUBE_FACES;
    l->bytes_per_block = f.bytes; l->block_width = f.width; l->block_height = f.height;
    l->depth_stencil = f.depth_stencil;
    for (uint32_t n = f.bytes; n > 1; n >>= 1) l->log2_bytes++;
    l->sample_x = b->samples == 2 || b->samples == 4 ? 2 : b->samples == 8 || b->samples == 16 ? 4 : 1;
    l->sample_y = b->samples == 4 || b->samples == 8 ? 2 : b->samples == 16 ? 4 : 1;
    l->samples = b->samples == 1 || b->samples == 2 ? 2 :
                 b->samples == 4 || b->samples == 8 || b->samples == 16 ? b->samples : 0;
    uint32_t levels = b->levels;
    if (b->target == NVN_TEXTURE_TARGET_BUFFER) {
        l->dim = l->levels = 1;
        l->layer_stride = l->size = l->width * f.bytes;
    } else if (b->flags & NVN_TEXTURE_LINEAR_FLAGS) {
        l->dim = 2; l->levels = 1; l->linear_stride = b->stride;
        l->layer_stride = l->size = l->height * b->stride;
    } else {
        if (b->target >= NVN_TEXTURE_TARGET_RECTANGLE && b->target <= NVN_TEXTURE_TARGET_2D_MS_ARRAY) levels = 1;
        l->levels = (uint8_t)levels;
        l->dim = b->target == NVN_TEXTURE_TARGET_3D ? 3 :
                 b->target == NVN_TEXTURE_TARGET_1D || b->target == NVN_TEXTURE_TARGET_1D_ARRAY ? 1 : 2;
        texture_choose_tile(b, l, levels);
        l->layer_stride = texture_footprint(l, levels, texture_is_array(b->target), l->tile_x, l->tile_y, l->tile_z);
        l->size = l->layer_stride * (b->target == NVN_TEXTURE_TARGET_3D ? 1 : l->depth);
        if (texture_page_round(b, dev, f)) l->size = align_u64(l->size, dev->wac);
    }
    if (b->gl_name) l->size = 0;
    return 1;
}

uint16_t nvn_texture_packaged_tile(uint32_t layout) { return (uint16_t)(((layout & 0xfffu) << 4) | 1u); }

/* Small encoder tables from de879e8, de87970 and the branches selected by de87948. */
static const uint8_t texture_target_codes[] = {0, 1, 2, 4, 5, 1, 5, 1, 3, 8};
static const uint8_t texture_device_rows[][5] = {
    {0, 2, 0, 0, 0}, {0, 2, 1, 1, 0}, {1, 2, 0, 0, 0},
    {1, 3, 0, 0, 0}, {3, 3, 0, 0, 9}, {3, 3, 0, 1, 31}
};
static const struct { uint32_t samples, code; } texture_sample_codes[] = {{2, 5}, {4, 2}, {8, 4}, {16, 6}};
#define TEXTURE_DEVICE_ROW_COUNT 6u
#define TEXTURE_QUALITY_LIMIT 19u
#define TEXTURE_MIP_LIMIT 16u
#define TEXTURE_QUALITY_SINGLE 0x914b83afu
#define TEXTURE_QUALITY_CLEAR_BIT 0xab0341cfu
#define TEXTURE_QUALITY_ALL_BITS 0x13478abeu
#define TEXTURE_QUALITY_ZERO 0x034e1413u

static void texture_bits(uint32_t* word, unsigned bit, unsigned count, uint32_t value) {
    uint32_t mask = (1u << count) - 1;
    *word = (*word & ~(mask << bit)) | ((value & mask) << bit);
}
static uint32_t texture_swizzle(uint32_t seed, const uint32_t sw[4], uint8_t one_code) {
    uint32_t codes[4], result = seed & 0x8007ffffu;
    for (unsigned i = 0; i < 4; i++) codes[i] = (seed >> (19 + 3 * i)) & 7u;
    for (unsigned i = 0; i < 4; i++) {
        uint32_t code = sw[i] == 0 ? 0 : sw[i] == 1 ? (one_code ? 6 : 7) :
                        sw[i] <= 5 ? codes[sw[i] - 2] : codes[0];
        result |= code << (19 + 3 * i);
    }
    return result;
}
static void texture_descriptor_storage(const NvnTexture* t, uint32_t target,
                                       const NvnTextureDeviceWords* dev, uint32_t q[8]) {
    int linear = (t->builder.flags & NVN_TEXTURE_LINEAR_FLAGS) != 0;
    if (target == NVN_TEXTURE_TARGET_BUFFER) {
        texture_bits(&q[4], 23, 4, 6);
    } else {
        texture_bits(&q[2], 21, 3, linear ? 2 : 3);
        texture_bits(&q[5], 31, 1, target != NVN_TEXTURE_TARGET_2D_MS_ARRAY);
        uint32_t code = linear ? 7 : texture_target_codes[target];
        if (!linear && target == NVN_TEXTURE_TARGET_2D_MS_ARRAY &&
            (t->layout.width > dev->w08 || t->layout.height > dev->w08)) code = 7;
        texture_bits(&q[4], 23, 4, code);
        q[4] |= 0xe0000000u;
    }
    if (linear) {
        texture_bits(&q[3], 0, 16, t->layout.linear_stride >> 5);
    } else if (target != NVN_TEXTURE_TARGET_BUFFER) {
        uint32_t sector = dev->w1a48 == 1 ? 2 : dev->w1a48 == 2 ? 1 : dev->w1a48 == 3 ? 3 : 0;
        texture_bits(&q[4], 27, 2, sector);
        if (dev->w1a4c) q[3] |= 0x10000u;
        texture_bits(&q[3], 3, 3, t->layout.tile_y);
        texture_bits(&q[3], 6, 3, t->layout.tile_z);
        texture_bits(&q[3], 17, 2, dev->w1a50 > TEXTURE_QUALITY_LIMIT ? 2 : 3);
        switch (dev->w1a54) {
        case TEXTURE_QUALITY_SINGLE: texture_bits(&q[3], 17, 2, 1); break;
        case TEXTURE_QUALITY_CLEAR_BIT: q[3] &= ~0x20000u; break;
        case TEXTURE_QUALITY_ALL_BITS: q[3] |= 0x60000u; break;
        case TEXTURE_QUALITY_ZERO: texture_bits(&q[3], 17, 2, 0); break;
        default: break;
        }
    }
    if (target == NVN_TEXTURE_TARGET_RECTANGLE || target == NVN_TEXTURE_TARGET_2D_MS)
        for (unsigned i = 0; i < sizeof texture_sample_codes / sizeof texture_sample_codes[0]; i++)
            if (t->layout.samples == texture_sample_codes[i].samples)
                texture_bits(&q[7], 8, 4, texture_sample_codes[i].code);
}
static uint64_t texture_descriptor_dimensions(const NvnTexture* t, const NvnTextureView* v,
    uint32_t target, const NvnTextureDeviceWords* dev, uint32_t q[8]) {
    uint64_t address = t->address;
    uint32_t depth = t->layout.depth;
    if (v && (v->flags & NVN_VIEW_LAYERS)) {
        address += t->layout.layer_stride * v->base_layer;
        if (target == NVN_TEXTURE_TARGET_1D_ARRAY || target == NVN_TEXTURE_TARGET_2D_ARRAY ||
            target == NVN_TEXTURE_TARGET_2D_MS || target == NVN_TEXTURE_TARGET_CUBE_ARRAY)
            depth = v->layer_count ? v->layer_count : depth - v->base_layer;
    }
    if (target == NVN_TEXTURE_TARGET_CUBE_ARRAY) depth /= NVN_TEXTURE_CUBE_FACES;
    if (target == NVN_TEXTURE_TARGET_CUBE) depth = 1;
    texture_bits(&q[4], 0, 16, t->layout.width - 1u);
    if (target == NVN_TEXTURE_TARGET_BUFFER) {
        texture_bits(&q[3], 0, 16, (t->layout.width - 1u) >> 16);
        return address;
    }
    texture_bits(&q[5], 0, 16, t->layout.height - 1u);
    texture_bits(&q[5], 16, 14, depth - 1u);
    q[3] &= 0xfc07e3ffu;
    q[6] &= 0x387fffffu;
    texture_bits(&q[3], 10, 3, t->layout.tile_x);
    uint32_t row = dev->w1a88 < TEXTURE_DEVICE_ROW_COUNT ? dev->w1a88 : TEXTURE_DEVICE_ROW_COUNT - 1;
    const uint8_t* r = texture_device_rows[row];
    texture_bits(&q[6], 25, 2, r[0]);
    texture_bits(&q[6], 23, 2, r[1]);
    texture_bits(&q[3], 19, 2, r[2]);
    texture_bits(&q[6], 30, 2, r[3]);
    q[3] |= (uint32_t)r[4] << 21;
    return address;
}
static uint32_t texture_descriptor_depth(uint32_t seed, uint32_t format, uint32_t mode,
                                         const NvnTextureFormatRecord* r, const uint32_t sw[4]) {
    uint32_t bits = 0x24900000u;
    if (r->depth_size) {
        int special = format == 0x33 || format == 0x35;
        bits = special ? (mode == 1 ? 0x24900000u : 0x76d80000u) :
                         (mode == 1 ? 0x36d80000u : 0x74900000u);
    }
    seed = (seed & 0x8007ffffu) | bits;
    if (mode == 1 && r->stencil_size == 8) {
        if (r->depth_size == 24) texture_bits(&seed, 0, 6, 14);
        if (r->depth_size == 32) texture_bits(&seed, 0, 6, 5);
    }
    return texture_swizzle(seed, sw, r->one_code);
}
static uint32_t texture_min(uint32_t a, uint32_t b) { return a < b ? a : b; }
static void texture_descriptor_mips(const NvnTexture* t, const NvnTextureView* v, uint32_t q[8]) {
    uint32_t m = t->layout.levels < TEXTURE_MIP_LIMIT ? (uint32_t)t->layout.levels - 1u : TEXTURE_MIP_LIMIT - 1;
    uint32_t hi = texture_min(t->builder.levels - 1u, m), lo = 0;
    if (v && (v->flags & NVN_VIEW_LEVELS)) {
        lo = texture_min(v->base_level, m);
        if (v->level_count) hi = texture_min(hi, (uint32_t)v->base_level + v->level_count - 1u);
    }
    texture_bits(&q[7], 0, 4, lo);
    texture_bits(&q[7], 4, 4, hi);
    texture_bits(&q[3], 28, 4, m);
}
/* Registration adds raw layer bits even when the view's LAYERS flag is clear (d83d99c). */
static void texture_descriptor_registration(uint32_t flags, uint32_t layer, uint32_t q[8]) {
    uint32_t kind = (q[2] >> 21) & 7u;
    texture_bits(&q[4], 16, 3, layer);
    q[2] = (q[2] & 0x1fe0ffffu) | (((layer << 21) | (layer << 13)) & 0xe01f0000u);
    texture_bits(&q[5], 30, 1, flags >> 5);
    if (kind == 0) texture_bits(&q[5], 31, 1, flags >> 3);
    if (kind == 2 || kind == 3) texture_bits(&q[6], 0, 1, flags >> 3);
    q[2] &= 0xfeffffffu;
}
NvnTextureDescriptorResult nvn_texture_descriptor(const NvnTexture* t, const NvnTextureView* v,
    const NvnTextureDeviceWords* dev, const NvnTextureFormatRecord* r, uint32_t out[NVN_TEXTURE_DESCRIPTOR_WORDS]) {
    uint32_t vf = v ? v->flags : 0;
    uint32_t format = (vf & NVN_VIEW_FORMAT) ? v->format : t->builder.format;
    uint32_t target = (vf & NVN_VIEW_TARGET) ? v->target : t->builder.target;
    if (format > NVN_TEXTURE_FORMAT_MAX) return NVN_TEXTURE_DESCRIPTOR_BAD_FORMAT;
    if (target > NVN_TEXTURE_TARGET_BUFFER) return NVN_TEXTURE_DESCRIPTOR_BAD_TARGET;
    uint32_t q[NVN_TEXTURE_DESCRIPTOR_WORDS] = {0};
    const uint32_t* sw = (vf & NVN_VIEW_SWIZZLE) ? v->swizzle : t->builder.swizzle;
    q[0] = texture_swizzle(r->seed, sw, r->one_code);
    texture_bits(&q[4], 22, 1, r->srgb != 0);
    texture_descriptor_storage(t, target, dev, q);
    uint64_t address = texture_descriptor_dimensions(t, v, target, dev, q);
    if (r->depth_size || r->stencil_size) {
        uint32_t mode = (vf & NVN_VIEW_DEPTH_STENCIL_MODE) ? v->depth_stencil_mode : t->builder.depth_stencil_mode;
        q[0] = texture_descriptor_depth(q[0], format, mode, r, sw);
    }
    if (!(t->builder.flags & NVN_TEXTURE_LINEAR_FLAGS) && target != NVN_TEXTURE_TARGET_BUFFER)
        texture_descriptor_mips(t, v, q);
    q[1] |= (uint32_t)address;
    q[2] |= (uint32_t)(address >> 32);
    texture_descriptor_registration(t->builder.flags, v ? v->base_layer : 0, q);
    memcpy(out, q, sizeof q);
    return NVN_TEXTURE_DESCRIPTOR_OK;
}
