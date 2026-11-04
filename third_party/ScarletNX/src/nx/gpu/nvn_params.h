/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_NVN_PARAMS_H
#define SNX_NVN_PARAMS_H
#include <stdint.h>

#define NVN_WINDOW_MAX_TEXTURES 4u
#define NVN_WINDOW_NO_IMAGE (-1)
#define NVN_WINDOW_INVALID_INDEX UINT32_MAX
typedef enum { NVN_IMAGE_FREE = 0, NVN_IMAGE_ACQUIRED = 1, NVN_IMAGE_PRESENTED = 2 } NvnImageState;
typedef enum { NVN_SYNC_WAIT_SIGNALED = 0, NVN_SYNC_WAIT_SATISFIED = 1,
               NVN_SYNC_WAIT_TIMEOUT = 2, NVN_SYNC_WAIT_FAILED = 3 } NvnSyncWaitResult;
typedef struct {
    uint64_t device, native_window, textures;
    uint32_t texture_count, interval;
} NvnWindowBuilder;
typedef struct { uint32_t x, y, width, height; } NvnWindowCrop;
typedef enum { NVN_SYNC_UNSIGNALED = 0, NVN_SYNC_SIGNALED = 1, NVN_SYNC_FENCED = 2 } NvnSyncState;
/* nvnQueueFenceSync condition: the only value that the game used. A frame job signals the sync after all its work. */
#define NVN_SYNC_CONDITION_ALL_GPU_COMMANDS_COMPLETE 0u
void nvn_window_builder_defaults(NvnWindowBuilder* b);
void nvn_window_builder_set_device(NvnWindowBuilder* b, uint64_t device);
void nvn_window_builder_set_native_window(NvnWindowBuilder* b, uint64_t native_window);
void nvn_window_builder_set_textures(NvnWindowBuilder* b, uint32_t count, uint64_t textures);
void nvn_window_builder_set_interval(NvnWindowBuilder* b, uint32_t interval);
int nvn_window_initialize_check(uint32_t texture_count);
int32_t nvn_window_pick_image(const uint8_t* states, uint32_t count, int32_t last);
NvnSyncWaitResult nvn_sync_wait_result(NvnSyncState state);
NvnWindowCrop nvn_window_crop(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                              uint32_t texture_width, uint32_t texture_height);

/* The pure parts of the host's device, queue and pool functions (docs/nvn-design.md, 3b-3). The game's NVN library computes
   these from device fields that it fills at DeviceInitialize. The host has no device object, so it uses the values that
   a run of the library showed (the "3b-2 Result" and "3b-3 Result" sections). */

/* The library answers a `what` above this with 0 (the default of the switch at d831f68). Below it, a table gives the value. */
#define NVN_DEVICE_INTEGER_LIMIT 0x62u
/* What nvnDeviceGetInteger answers for `what`. 0 when the host has no value for it (the library has one, no run asked). */
int nvn_device_integer(uint32_t what, int32_t* value);

/* nvnDeviceGetTimestampInNanoseconds: the GPU ticks of a counter report as nanoseconds. Same integer steps as the
   library, so the results match to the last digit and a large tick count does not overflow. */
uint64_t nvn_timestamp_ns(uint64_t ticks);

/* The NVN texture and image handles (sdk d82d0d0, d82d050). The library masks no index: a texture index above 0xFFFFF
   spills into the sampler field, and index bit 31 is sign extended. Both properties are kept. */
#define NVN_HANDLE_SAMPLER_SHIFT 20u
#define NVN_TEXTURE_HANDLE_TAG   0x100000000ull
uint64_t nvn_texture_handle(uint32_t texture_index, uint32_t sampler_index);
uint64_t nvn_image_handle(uint32_t image_index);

/* The four kinds of buffer bind of a command buffer. A uniform and a vertex bind are active with a nonzero size, a
   transform feedback bind with a nonzero address, and a storage bind is always active. */
typedef enum { NVN_BIND_UNIFORM = 0, NVN_BIND_STORAGE, NVN_BIND_VERTEX, NVN_BIND_TRANSFORM_FEEDBACK } NvnBindKind;
int nvn_bind_buffer_active(NvnBindKind kind, uint64_t addr, uint64_t size);

/* The six state objects of a command buffer. The order is the kind word of the bind state op. */
typedef enum {
    NVN_STATE_COLOR = 0,
    NVN_STATE_DEPTH_STENCIL,
    NVN_STATE_BLEND,
    NVN_STATE_POLYGON,
    NVN_STATE_MULTISAMPLE,
    NVN_STATE_CHANNEL_MASK,
    NVN_STATE_COUNT
} NvnStateKind;

#define NVN_STATE_WORDS_MAX 5u /* the multisample bind reads word 0 and four sample location words */

/* The library stores these fields in plain words; the value is not masked, so its high bits spill as in the library. */
uint32_t nvn_state_insert(uint32_t word, uint32_t mask, uint32_t shift, uint32_t value);
uint32_t nvn_state_set_flag(uint32_t word, uint32_t bit, uint32_t value);

uint32_t nvn_blend_state_set_blend_func(uint32_t word, uint32_t src_color, uint32_t dst_color,
                                        uint32_t src_alpha, uint32_t dst_alpha);
uint32_t nvn_blend_state_set_blend_equation(uint32_t word, uint32_t rgb, uint32_t alpha);
uint32_t nvn_channel_mask_state_set_channel_mask(uint32_t word, uint32_t target, uint32_t r, uint32_t g,
                                                 uint32_t b, uint32_t a);
uint32_t nvn_color_state_set_blend_enable(uint32_t word, uint32_t target, uint32_t value);
uint32_t nvn_depth_stencil_state_set_stencil_func(uint32_t word, uint32_t face, uint32_t func);
uint32_t nvn_depth_stencil_state_set_stencil_op(uint32_t word, uint32_t face, uint32_t fail, uint32_t zfail,
                                                uint32_t zpass);

void nvn_color_state_defaults(uint32_t* words);
void nvn_depth_stencil_state_defaults(uint32_t* words);
void nvn_blend_state_defaults(uint32_t* words);
void nvn_polygon_state_defaults(uint32_t* words);
void nvn_multisample_state_defaults(uint32_t* words);
void nvn_channel_mask_state_defaults(uint32_t* words);

uint32_t nvn_blend_state_get_blend_target(uint32_t word);
void nvn_blend_state_get_blend_func(uint32_t word, uint32_t out[4]);
void nvn_channel_mask_state_get_channel_mask(uint32_t word, uint32_t target, uint8_t out[4]);
uint32_t nvn_color_state_get_alpha_test(uint32_t word);
uint32_t nvn_color_state_get_blend_enable(uint32_t word, uint32_t target);
uint32_t nvn_depth_stencil_state_get_depth_func(uint32_t word);
uint32_t nvn_depth_stencil_state_get_depth_test_enable(uint32_t word);
uint32_t nvn_depth_stencil_state_get_depth_write_enable(uint32_t word);
uint32_t nvn_multisample_state_get_multisample_enable(uint32_t word);
uint32_t nvn_multisample_state_get_samples(uint32_t word);
uint32_t nvn_multisample_state_get_alpha_to_coverage_enable(uint32_t word);
uint32_t nvn_multisample_state_get_alpha_to_coverage_dither(uint32_t word);
uint32_t nvn_multisample_state_get_coverage_modulation_mode(uint32_t word);
uint32_t nvn_multisample_state_get_raster_samples(uint32_t word);
uint32_t nvn_multisample_state_get_coverage_to_color_enable(uint32_t word);
uint32_t nvn_multisample_state_get_coverage_to_color_output(uint32_t word);
uint32_t nvn_polygon_state_get_cull_face(uint32_t word);
uint32_t nvn_polygon_state_get_front_face(uint32_t word);

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
/* The byte offset of a descriptor inside its memory pool. Fails (0) when index is outside [0, count), or when the
   descriptor range [pool_offset, pool_offset + count * entry_bytes) does not fit in the memory pool. */
/* 1 when [offset, offset + size) lies inside a memory pool of pool_size bytes, without overflow. */
int nvn_pool_range_fits(uint64_t pool_size, uint64_t offset, uint64_t size);
typedef enum { NVN_BUFFER_OK = 0, NVN_BUFFER_PHYSICAL_POOL, NVN_BUFFER_OUT_OF_POOL } NvnBufferResult;
/* The library fails BufferInitialize only for a physical pool (flag 0x100). The range check is the host's: the library
   has none, and the replay reads the buffer's bytes from the pool. */
NvnBufferResult nvn_buffer_check(uint32_t pool_flags, uint64_t pool_size, uint64_t offset, uint64_t size);
int nvn_descriptor_offset(uint64_t pool_offset, uint32_t count, uint64_t memory_size, uint32_t entry_bytes,
                          int32_t index, uint64_t* offset);
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
/* 0 (out unchanged) when a wrap enum is above 7: the library reads its table unchecked. NaN anisotropy has no defined
   library result. */
int nvn_sampler_descriptor(const NvnSampler* s, const NvnSamplerDeviceWords* dev,
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
#define NVN_TEXTURE_TARGET_2D_MS 5u
#define NVN_TEXTURE_TARGET_2D_MS_ARRAY 6u
#define NVN_TEXTURE_TARGET_RECTANGLE 7u
#define NVN_TEXTURE_TARGET_CUBE 8u
#define NVN_TEXTURE_TARGET_CUBE_ARRAY 9u
#define NVN_TEXTURE_TARGET_BUFFER 10u
#define NVN_TEXTURE_CUBE_FACES 6u
#define NVN_TEXTURE_FORMAT_MAX 0xa0u
#define NVN_TEXTURE_GOB_WIDTH 64u
#define NVN_TEXTURE_GOB_HEIGHT 8u
#define NVN_TEXTURE_GOB_BYTES 512u
/* Automatic tile choice: the 1.5 times size of the depth (3D) or of the block rows (other targets) selects the tile exponent. */
#define NVN_TEXTURE_AUTO_TILE_DEPTH_4 15u
#define NVN_TEXTURE_AUTO_TILE_DEPTH_3 8u
#define NVN_TEXTURE_AUTO_TILE_DEPTH_2 3u
#define NVN_TEXTURE_AUTO_TILE_DEPTH_1 1u
#define NVN_TEXTURE_AUTO_TILE_ROWS_4 128u
#define NVN_TEXTURE_AUTO_TILE_ROWS_3 64u
#define NVN_TEXTURE_AUTO_TILE_ROWS_2 31u
#define NVN_TEXTURE_AUTO_TILE_ROWS_1 15u
#define NVN_TEXTURE_LINEAR_ALIGNMENT 0x20u
#define NVN_TEXTURE_LINEAR_RT_ALIGNMENT 0x80u
#define NVN_TEXTURE_COMPRESSION_DISABLED 0x76165656u
/* Device initialization values checked against the library disassembly. */
#define NVN_TEXTURE_DEVICE_PAGE_SIZE 0x10000u
#define NVN_TEXTURE_DEVICE_COLOR_COMPRESSION 0u
#define NVN_TEXTURE_DEVICE_DEPTH_COMPRESSION 0u
#define NVN_TEXTURE_DEVICE_BUFFER_KIND 0u
/* The game's device holds 1 at +0x1b1c (selftest run, 2026-11-03), and the 24 size cases match with it. The store that
   sets it is not found: the value is from one run. */
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
    NVN_TEXTURE_DESCRIPTOR_BAD_TARGET,
    NVN_TEXTURE_DESCRIPTOR_BAD_SWIZZLE
} NvnTextureDescriptorResult;
#define NVN_TEXTURE_SWIZZLE_MAX 5u /* ZERO, ONE, R, G, B, A */
uint16_t nvn_texture_packaged_tile(uint32_t layout);
/* The caller supplies the effective format record. Errors leave out unchanged. */
NvnTextureDescriptorResult nvn_texture_descriptor(const NvnTexture* texture, const NvnTextureView* view,
    const NvnTextureDeviceWords* dev, const NvnTextureFormatRecord* format,
    uint32_t out[NVN_TEXTURE_DESCRIPTOR_WORDS]);

/* Returns the Maxwell RT format word of an NVN format, or 0 when the format has none or is above NVN_TEXTURE_FORMAT_MAX.
   A 0 result is a fault of the caller: log it and stop. */
uint32_t nvn_rt_format_word(uint32_t format);

/* The Android buffer format and libnx NvLayout values of the window image show (3c decision 9). */
#define NVN_PRESENT_FORMAT_RGBA8888 1u
#define NVN_PRESENT_LAYOUT_PITCH    1u
#define NVN_PRESENT_LAYOUT_BLOCK    3u
typedef struct {
    uint64_t cpu_addr;
    uint32_t w, h, pitch, layout, bhl, fmt;
    int32_t crop[4];
} NvnPresentArgs;
/* The arguments of present_buffer for the window image in texture t, its storage in pool_storage. 0 with out
   unchanged when the format is not RGBA8, the address sum overflows or a crop value does not fit int32_t. Pure. */
int nvn_window_present_args(const NvnTexture* t, uint64_t pool_storage, const NvnWindowCrop* crop, NvnPresentArgs* out);
/* The sync state after one token of a frame job: fences goes down by one when it is above 0, and the state is
   SIGNALED at 0. Pure. */
NvnSyncState nvn_sync_after_token(NvnSyncState state, uint32_t* fences);

/* The pipeline stages of a program: 0 vertex, 1 fragment (the only two the game uses), 2 to 5 accepted by the library. */
#define NVN_PROGRAM_STAGES 6u
/* Header +8 is the layout version (5 to 16), not the stage. The stage index is the u32 at header +0x714 (sdk d837228). */
#define NVN_PROGRAM_LAYOUT_FIRST 5u
#define NVN_PROGRAM_LAYOUT_LAST 16u
#define NVN_PROGRAM_STAGE_OFFSET 0x714u
/* The words of a shader header at +4, +0xc and +0x10 that the library checks. */
#define NVN_PROGRAM_HEADER_VERSION 1u
#define NVN_PROGRAM_HEADER_SIZE 0x120u
#define NVN_PROGRAM_HEADER_KIND 0xbu
/* 1 and *index_out = stage_word for a valid header; 0 (index_out unchanged) for any other word or a NULL index_out. */
int nvn_program_header_stage(uint32_t version, uint32_t layout, uint32_t size_word, uint32_t kind_word,
                             uint32_t stage_word, uint32_t* index_out);

#endif
