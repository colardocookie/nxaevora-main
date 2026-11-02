/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_NVN_PARAMS_H
#define SNX_NVN_PARAMS_H
#include <stdint.h>

/* The pure parts of the host's device, queue and pool functions (docs/nvn-design.md, 3b-3). The game's NVN library computes
   these from device fields that it fills at DeviceInitialize. The host has no device object, so it uses the values that
   a run of the library showed (the "3b-2 Result" and "3b-3 Result" sections). */

/* What nvnDeviceGetInteger answers for `what`. 0 when the host has no value for it (nothing in the run asked for it). */
int nvn_device_integer(uint32_t what, int32_t* value);

/* nvnDeviceGetTimestampInNanoseconds: the GPU ticks of a counter report as nanoseconds. Same integer steps as the
   library, so the results match to the last digit and a large tick count does not overflow. */
uint64_t nvn_timestamp_ns(uint64_t ticks);

/* The device words that nvnQueueBuilderGetQueueMemorySize reads (offsets in the names). */
typedef struct {
    uint32_t w8c, w94, w98, we0, we4, w1af4;
} NvnQueueDeviceWords;
extern const NvnQueueDeviceWords nvn_queue_device_words;

/* The host's copy of an nvnQueueBuilder. A size counts only when its has_ field is 1 (the setter ran). */
typedef struct {
    uint64_t device, memory;
    uint32_t flags, memory_size, priority;
    int32_t command, compute, control, flush_threshold;
    uint8_t has_command, has_compute, has_control, has_flush_threshold;
} NvnQueueBuilder;

#define NVN_QUEUE_PRIORITY_DEFAULT 100 /* what the library's SetDefaults stores */
void nvn_queue_builder_defaults(NvnQueueBuilder* b);
/* The bytes of memory that a queue with this builder needs: the answer of nvnQueueBuilderGetQueueMemorySize */
uint64_t nvn_queue_memory_size(const NvnQueueBuilder* b, const NvnQueueDeviceWords* dev);

#define NVN_POOL_CPU_NO_ACCESS 0x1u
#define NVN_POOL_CPU_UNCACHED 0x2u
#define NVN_POOL_CPU_CACHED 0x4u
#define NVN_POOL_GPU_NO_ACCESS 0x8u
#define NVN_POOL_GPU_UNCACHED 0x10u
#define NVN_POOL_GPU_CACHED 0x20u
#define NVN_POOL_PHYSICAL 0x100u
#define NVN_POOL_VIRTUAL 0x200u
#define NVN_POOL_IMPORT 0x20000000u
#define NVN_POOL_PUBLIC_FLAGS 0x01ffffffu
#define NVN_POOL_DEFAULT_FLAGS 0x21u
#define NVN_POOL_STORAGE_ALIGNMENT 0x1000ull
/* The host device uses the minimum page size. No NvRm device is constructed. */
#define NVN_POOL_PAGE_SIZE NVN_POOL_STORAGE_ALIGNMENT
#define NVN_POOL_GPU_BASE 0x400000000ull
#define NVN_POOL_GPU_END (1ull << 40)

typedef struct {
    uint64_t device, storage, size;
    uint32_t flags, handle;
    uint64_t optional[3];
} NvnMemoryPoolBuilder;

typedef enum {
    NVN_POOL_OK = 0,
    NVN_POOL_UNSUPPORTED_VIRTUAL,
    NVN_POOL_UNSUPPORTED_IMPORT,
    NVN_POOL_INVALID_STORAGE,
    NVN_POOL_INVALID_PAGE_SIZE
} NvnPoolResult;

void nvn_memory_pool_builder_defaults(NvnMemoryPoolBuilder* b);
/* Normalizes flags even on failure. Service-owned handles are not supported. */
NvnPoolResult nvn_memory_pool_params(const NvnMemoryPoolBuilder* b, uint64_t page_size, uint32_t* flags);
/* Pure bump allocation: no address reuse. Outputs stay unchanged on failure. End is exclusive. */
int nvn_pool_gpu_allocate(uint64_t cursor, uint64_t size, uint64_t page_size,
                          uint64_t* address, uint64_t* next);
uint64_t nvn_memory_pool_map(uint32_t flags, uint64_t storage);
uint32_t nvn_memory_pool_flags(uint32_t flags);

#define NVN_SAMPLER_FILTER_DEFAULT 1u
#define NVN_SAMPLER_WRAP_DEFAULT 1u
#define NVN_SAMPLER_COMPARE_DEFAULT 2u
#define NVN_SAMPLER_MAX_LOD_DEFAULT 1000.0f
#define NVN_SAMPLER_ANISOTROPY_DEFAULT 1.0f
#define NVN_SAMPLER_DESCRIPTOR_WORDS 8u
#define NVN_SAMPLER_DESCRIPTOR_BYTES 32u

typedef struct {
    uint64_t device;
    uint32_t min_filter, mag_filter, wrap[3];
    float min_lod, max_lod, lod_bias;
    uint32_t compare_enable, compare, border[4];
    float anisotropy;
    uint32_t reduction;
    float lod_snap;
} NvnSampler;
typedef struct { uint32_t w1a80, w1a84; } NvnSamplerDeviceWords;
/* Device initialization stores these values (SDK d8312e4 and d8312fc). */
#define NVN_SAMPLER_DEVICE_LOD_BIAS 0u
#define NVN_SAMPLER_DEVICE_FILTER_FLAGS 0x111u
extern const NvnSamplerDeviceWords nvn_sampler_device_words;
void nvn_sampler_defaults(NvnSampler* s);
float nvn_sampler_snap_bias(uint32_t index);
/* A wrap enum is masked to 0..7 (the library reads its table unchecked). NaN anisotropy has no defined library result. */
void nvn_sampler_descriptor(const NvnSampler* s, const NvnSamplerDeviceWords* dev,
                            uint32_t out[NVN_SAMPLER_DESCRIPTOR_WORDS]);

#define NVN_TEXTURE_FLAG_MINIMAL_TILE 0x2u
#define NVN_TEXTURE_FLAG_COMPRESS 0x8u
#define NVN_TEXTURE_FLAG_LINEAR 0x10u
#define NVN_TEXTURE_FLAG_SPARSE 0x20u
#define NVN_TEXTURE_FLAG_LINEAR_RENDER_TARGET 0x40u
#define NVN_TEXTURE_FLAG_ADAPTIVE_TILE 0x200u
#define NVN_TEXTURE_POOL_STORAGE_FLAGS 0x280u
#define NVN_TEXTURE_SPARSE_TILE_MASK 0x32u
#define NVN_TEXTURE_KIND_PITCH 0xfeu
#define NVN_TEXTURE_LINEAR_FLAGS (NVN_TEXTURE_FLAG_LINEAR | NVN_TEXTURE_FLAG_LINEAR_RENDER_TARGET)
#define NVN_TEXTURE_TARGET_1D 0u
#define NVN_TEXTURE_TARGET_2D 1u
#define NVN_TEXTURE_TARGET_3D 2u
#define NVN_TEXTURE_TARGET_1D_ARRAY 3u
#define NVN_TEXTURE_TARGET_2D_ARRAY 4u
#define NVN_TEXTURE_TARGET_RECTANGLE 5u
#define NVN_TEXTURE_TARGET_2D_MS 6u
#define NVN_TEXTURE_TARGET_2D_MS_ARRAY 7u
#define NVN_TEXTURE_TARGET_CUBE 8u
#define NVN_TEXTURE_TARGET_CUBE_ARRAY 9u
#define NVN_TEXTURE_TARGET_BUFFER 10u
#define NVN_TEXTURE_CUBE_FACES 6u
#define NVN_TEXTURE_FORMAT_MAX 0xa0u
#define NVN_TEXTURE_GOB_WIDTH 64u
#define NVN_TEXTURE_GOB_HEIGHT 8u
#define NVN_TEXTURE_GOB_BYTES 512u
#define NVN_TEXTURE_LINEAR_ALIGNMENT 0x20u
#define NVN_TEXTURE_LINEAR_RT_ALIGNMENT 0x80u
#define NVN_TEXTURE_COMPRESSION_DISABLED 0x76165656u
/* Device initialization values checked against the library disassembly. */
#define NVN_TEXTURE_DEVICE_PAGE_SIZE 0x10000u
#define NVN_TEXTURE_DEVICE_COLOR_COMPRESSION 0u
#define NVN_TEXTURE_DEVICE_DEPTH_COMPRESSION 0u
#define NVN_TEXTURE_DEVICE_BUFFER_KIND 0u
/* Provisional: keep R true for flags 9 and format 0x25. The 40 archived size
   queries need this non-zero value, although device initialization clears +0x1b1c. */
#define NVN_TEXTURE_DEVICE_COMPRESSION_ENABLE 1u
#define NVN_TEXTURE_DEVICE_DIMENSION_LIMIT 0x4000u
#define NVN_TEXTURE_DEVICE_SECTOR_LAYOUT 2u
#define NVN_TEXTURE_DEVICE_HEADER_ENABLE 1u
#define NVN_TEXTURE_DEVICE_QUALITY 0u
#define NVN_TEXTURE_DEVICE_QUALITY_OVERRIDE 0xffffffffu
#define NVN_TEXTURE_DEVICE_DESCRIPTOR_ROW 2u

typedef struct {
    uint64_t device, pool, offset, packaged;
    uint32_t flags, target, width, height, depth, levels, format, samples;
    uint32_t swizzle[4], depth_stencil_mode, stride, gl_name;
    uint16_t tile;
} NvnTextureBuilder;
typedef struct {
    uint32_t wac, w19f8, w19fc, w1b18, w1b1c;
    uint32_t w08, w1a48, w1a4c, w1a50, w1a54, w1a88;
} NvnTextureDeviceWords;
extern const NvnTextureDeviceWords nvn_texture_device_words;
typedef struct {
    uint64_t size, alignment, layer_stride;
    uint32_t linear_stride, samples, width, height, depth;
    uint8_t bytes_per_block, log2_bytes, sample_x, sample_y, block_width, block_height;
    uint8_t levels, dim, kind, depth_stencil, tile_x, tile_y, tile_z;
} NvnTextureLayout;
void nvn_texture_builder_defaults(NvnTextureBuilder* b);
uint64_t nvn_texture_storage_alignment(const NvnTextureBuilder* b, const NvnTextureDeviceWords* dev);
/* Returns 0 for unsupported formats. GL imports have size 0 but retain alignment. Kind stays 0. */
int nvn_texture_layout(const NvnTextureBuilder* b, const NvnTextureDeviceWords* dev, NvnTextureLayout* out);

#define NVN_TEXTURE_DESCRIPTOR_WORDS 8u
#define NVN_TEXTURE_DESCRIPTOR_BYTES 32u
#define NVN_VIEW_LEVELS 0x1u
#define NVN_VIEW_LAYERS 0x2u
#define NVN_VIEW_FORMAT 0x4u
#define NVN_VIEW_SWIZZLE 0x8u
#define NVN_VIEW_DEPTH_STENCIL_MODE 0x10u
#define NVN_VIEW_TARGET 0x20u

typedef struct {
    uint8_t flags, base_level, level_count;
    uint16_t base_layer, layer_count;
    uint32_t format, swizzle[4], depth_stencil_mode, target;
} NvnTextureView;
typedef struct {
    NvnTextureBuilder builder;
    NvnTextureLayout layout;
    uint64_t address;
} NvnTexture;
/* seed is the u32 at byte offset 0x24, not u32 array index 0x24 (d858d28). */
typedef struct {
    uint32_t seed;
    uint8_t depth_size, stencil_size, one_code, srgb;
} NvnTextureFormatRecord;
typedef enum {
    NVN_TEXTURE_DESCRIPTOR_OK = 0,
    NVN_TEXTURE_DESCRIPTOR_BAD_FORMAT,
    NVN_TEXTURE_DESCRIPTOR_BAD_TARGET
} NvnTextureDescriptorResult;
uint16_t nvn_texture_packaged_tile(uint32_t layout);
/* The caller supplies the effective format record. Errors leave out unchanged. */
NvnTextureDescriptorResult nvn_texture_descriptor(const NvnTexture* texture, const NvnTextureView* view,
    const NvnTextureDeviceWords* dev, const NvnTextureFormatRecord* format,
    uint32_t out[NVN_TEXTURE_DESCRIPTOR_WORDS]);

#endif
