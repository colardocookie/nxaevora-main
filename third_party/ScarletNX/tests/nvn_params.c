/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* Device and queue cases use the library results of 2026-11-02. Pool cases check the accepted disassembly table
   and the host GPU address allocation rule. */
#include "nx/gpu/nvn_params.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

static int failures;
#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                            \
        }                                                          \
    } while (0)

static void test_window_sync_cycle_params(void) {
    const uint8_t free1[] = {NVN_IMAGE_FREE};
    const uint8_t free2[] = {NVN_IMAGE_FREE, NVN_IMAGE_FREE};
    const uint8_t free4[] = {NVN_IMAGE_FREE, NVN_IMAGE_FREE, NVN_IMAGE_FREE, NVN_IMAGE_FREE};
    const uint8_t acquired4[] = {NVN_IMAGE_ACQUIRED, NVN_IMAGE_ACQUIRED, NVN_IMAGE_ACQUIRED, NVN_IMAGE_ACQUIRED};
    const uint8_t mixed[] = {NVN_IMAGE_ACQUIRED, NVN_IMAGE_FREE, NVN_IMAGE_ACQUIRED, NVN_IMAGE_FREE};
    CHECK(nvn_window_pick_image(free1, 1, -1) == 0);
    CHECK(nvn_window_pick_image(free1, 1, 0) == 0);
    CHECK(nvn_window_pick_image(free2, 2, -1) == 0);
    CHECK(nvn_window_pick_image(free2, 2, 0) == 1);
    CHECK(nvn_window_pick_image(free2, 2, 1) == 0);
    CHECK(nvn_window_pick_image(free4, 4, -1) == 0);
    CHECK(nvn_window_pick_image(free4, 4, 1) == 2);
    CHECK(nvn_window_pick_image(free4, 4, 3) == 0);
    CHECK(nvn_window_pick_image(acquired4, 4, -1) == -1);
    CHECK(nvn_window_pick_image(mixed, 4, 0) == 1);
    CHECK(nvn_window_pick_image(mixed, 4, 1) == 3);
    CHECK(nvn_window_pick_image(mixed, 4, 3) == 1);
    CHECK(nvn_window_pick_image(free1, 0, -1) == -1);
    CHECK(nvn_window_pick_image(free4, 5, -1) == -1);
    CHECK(nvn_sync_wait_result(NVN_SYNC_SIGNALED) == NVN_SYNC_WAIT_SIGNALED);
    CHECK(nvn_sync_wait_result(NVN_SYNC_UNSIGNALED) == NVN_SYNC_WAIT_FAILED);
}

static void test_window_params(void) {
    NvnWindowBuilder b;
    nvn_window_builder_defaults(&b);
    CHECK(b.device == 0 && b.native_window == 0 && b.texture_count == 0 && b.textures == 0 && b.interval == 1);
    nvn_window_builder_set_device(&b, 0x1234);
    nvn_window_builder_set_native_window(&b, 0x5678);
    nvn_window_builder_set_textures(&b, 4, 0x9abc);
    nvn_window_builder_set_interval(&b, 0);
    CHECK(b.device == 0x1234 && b.native_window == 0x5678 && b.texture_count == 4 && b.textures == 0x9abc);
    CHECK(b.interval == 0);
    CHECK(!nvn_window_initialize_check(0));
    CHECK(nvn_window_initialize_check(1));
    CHECK(nvn_window_initialize_check(4));
    CHECK(!nvn_window_initialize_check(5));
    CHECK(!nvn_window_initialize_check(UINT32_MAX));
    NvnWindowCrop crop = nvn_window_crop(8, 9, 0, 12, 640, 480);
    CHECK(crop.x == 0 && crop.y == 0 && crop.width == 640 && crop.height == 480);
    crop = nvn_window_crop(8, 9, 12, 0, 640, 480);
    CHECK(crop.x == 0 && crop.y == 0 && crop.width == 640 && crop.height == 480);
    crop = nvn_window_crop(8, 9, 12, 13, 640, 480);
    CHECK(crop.x == 8 && crop.y == 9 && crop.width == 12 && crop.height == 13);
    crop = nvn_window_crop(UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, 640, 480);
    CHECK(crop.x == UINT32_MAX && crop.y == UINT32_MAX && crop.width == UINT32_MAX && crop.height == UINT32_MAX);
}

static void test_device_integer(void) {
    static const struct {
        uint32_t what;
        int32_t value;
    } seen[] = {{4, 256}, {15, 32}, {19, 4}, {20, 8}, {40, 1}, {76, 1}, {82, 65536}, {85, 262144}, {90, 0}, {92, 16384}};
    for (size_t i = 0; i < sizeof seen / sizeof seen[0]; i++) {
        int32_t v = -1;
        CHECK(nvn_device_integer(seen[i].what, &v) && v == seen[i].value);
    }
    int32_t v = 7;
    CHECK(!nvn_device_integer(0, &v) && v == 7); /* no run asked for it: no value, the output is untouched */
    CHECK(!nvn_device_integer(1000, &v));
}

static void test_timestamp(void) {
    CHECK(nvn_timestamp_ns(0) == 0);
    CHECK(nvn_timestamp_ns(384) == 625);
    CHECK(nvn_timestamp_ns(383) == 623); /* floor(383 * 625 / 384) */
    /* the library's split into whole and rest equals the exact floor of ticks * 625 / 384, also near 2^64 */
    uint64_t samples[] = {1, 2, 1000, 0xF53674EEull, 0x1201E96D2ull, 1ull << 40, 1ull << 62, ~0ull / 625};
    for (size_t i = 0; i < sizeof samples / sizeof samples[0]; i++)
        CHECK(nvn_timestamp_ns(samples[i]) == (uint64_t)((unsigned __int128)samples[i] * 625 / 384));
}

/* the builder that the game made in the run: flags 0, command 0x20000, compute 0x40000, control 0x20000 */
static void test_queue_memory(void) {
    NvnQueueBuilder b;
    memset(&b, 0, sizeof b);
    nvn_queue_builder_defaults(&b);
    b.has_command = b.has_compute = b.has_control = 1;
    b.command = 0x20000;
    b.compute = 0x40000;
    b.control = 0x20000;
    CHECK(nvn_queue_memory_size(&b, &nvn_queue_device_words) == 0x8A000); /* the library's answer in the run */

    /* The cases below have no value from the library. They check that the function follows its port. */
    nvn_queue_builder_defaults(&b); /* sizes not set: the library's defaults 0x10000, 0x40000 and 0x4000 */
    CHECK(nvn_queue_memory_size(&b, &nvn_queue_device_words) == 0x5E000);
}

static void test_queue_defaults(void) {
    NvnQueueBuilder b;
    memset(&b, 0xAB, sizeof b);
    b.device = 0x1234;
    nvn_queue_builder_defaults(&b);
    CHECK(b.device == 0x1234); /* SetDefaults leaves the device */
    CHECK(b.priority == NVN_QUEUE_PRIORITY_DEFAULT);
    CHECK(!b.has_command && !b.has_compute && !b.has_control && !b.has_flush_threshold);
    CHECK(b.flags == 0 && b.memory == 0 && b.memory_size == 0);
}

static void test_memory_pool_defaults(void) {
    NvnMemoryPoolBuilder b;
    memset(&b, 0xab, sizeof b);
    b.device = 0x1234;
    nvn_memory_pool_builder_defaults(&b);
    CHECK(b.device == 0x1234 && b.flags == 0x21);
    CHECK(!b.storage && !b.size && !b.handle);
    CHECK(!b.optional[0] && !b.optional[1] && !b.optional[2]);
}

static void test_memory_pool_params(void) {
    NvnMemoryPoolBuilder b = {.storage = 0x1000, .size = 0x1000};
    uint32_t flags;
    /* Each low-bit combination, with upper bits preserved. */
    static const uint32_t cpu[] = {2, 1, 2, 2, 4, 4, 2, 2};
    static const uint32_t gpu[] = {32, 8, 16, 16, 32, 32, 32, 32};
    for (uint32_t f = 0; f < 64; f++) {
        b.flags = f | 0x80000040u;
        CHECK(nvn_memory_pool_params(&b, 0x10000, &flags) == NVN_POOL_OK);
        CHECK(flags == (0x80000040u | cpu[f & 7] | gpu[f >> 3]));
    }
    b.flags = NVN_POOL_DEFAULT_FLAGS;
    CHECK(nvn_memory_pool_params(&b, NVN_POOL_PAGE_SIZE, &flags) == NVN_POOL_OK && flags == 0x21);
    b.flags = NVN_POOL_PHYSICAL | 0x3f;
    CHECK(nvn_memory_pool_params(&b, 0x10000, &flags) == NVN_POOL_INVALID_PAGE_SIZE);
    CHECK(flags == (NVN_POOL_PHYSICAL | 2 | 8));
    b.size = 0x10000;
    CHECK(nvn_memory_pool_params(&b, 0x10000, &flags) == NVN_POOL_OK);
    b.flags = NVN_POOL_VIRTUAL | 0x3f;
    CHECK(nvn_memory_pool_params(&b, 0x10000, &flags) == NVN_POOL_UNSUPPORTED_VIRTUAL);
    CHECK(flags == (NVN_POOL_VIRTUAL | 1 | 32));
    b.flags = NVN_POOL_IMPORT;
    CHECK(nvn_memory_pool_params(&b, 0x10000, &flags) == NVN_POOL_UNSUPPORTED_IMPORT);
    b.flags = 0;
    b.storage = 0;
    b.handle = 1;
    CHECK(nvn_memory_pool_params(&b, 0x1000, &flags) == NVN_POOL_INVALID_STORAGE);
    b.storage = 0x1001;
    CHECK(nvn_memory_pool_params(&b, 0x1000, &flags) == NVN_POOL_INVALID_STORAGE);
    b.storage = 0x1000;
    b.size = 0;
    CHECK(nvn_memory_pool_params(&b, 0x1000, &flags) == NVN_POOL_INVALID_STORAGE);
    b.size = 0x1001;
    CHECK(nvn_memory_pool_params(&b, 0x1000, &flags) == NVN_POOL_INVALID_STORAGE);
    b.size = 0x1000;
    CHECK(nvn_memory_pool_params(&b, 0, &flags) == NVN_POOL_INVALID_PAGE_SIZE);
    CHECK(nvn_memory_pool_map(NVN_POOL_CPU_NO_ACCESS, 0x123000) == 0);
    CHECK(nvn_memory_pool_map(NVN_POOL_CPU_CACHED, 0x123000) == 0x123000);
    CHECK(nvn_memory_pool_flags(0xffffffffu) == 0x01ffffffu);
}

static void test_pool_gpu_allocate(void) {
    uint64_t address = 7, next = 9;
    CHECK(nvn_pool_gpu_allocate(NVN_POOL_GPU_BASE, 0x1001, 0x1000, &address, &next));
    CHECK(address == NVN_POOL_GPU_BASE && next == address + 0x2000);
    uint64_t first_end = next;
    CHECK(nvn_pool_gpu_allocate(next, 0x10000, 0x10000, &address, &next));
    CHECK(address >= first_end && address % 0x10000 == 0 && next == address + 0x10000);
    CHECK(nvn_pool_gpu_allocate(NVN_POOL_GPU_END - 0x1000, 0x1000, 0x1000, &address, &next));
    CHECK(next == NVN_POOL_GPU_END);
    address = 7;
    next = 9;
    CHECK(!nvn_pool_gpu_allocate(NVN_POOL_GPU_END, 0x1000, 0x1000, &address, &next));
    CHECK(!nvn_pool_gpu_allocate(NVN_POOL_GPU_END - 1, 1, 0x1000, &address, &next));
    CHECK(!nvn_pool_gpu_allocate(NVN_POOL_GPU_BASE, UINT64_MAX, 0x1000, &address, &next));
    CHECK(!nvn_pool_gpu_allocate(NVN_POOL_GPU_BASE, 0, 0x1000, &address, &next));
    CHECK(!nvn_pool_gpu_allocate(NVN_POOL_GPU_BASE, 0x1000, 0, &address, &next));
    CHECK(!nvn_pool_gpu_allocate(NVN_POOL_GPU_BASE, 0x1000, 0x800, &address, &next));
    CHECK(!nvn_pool_gpu_allocate(NVN_POOL_GPU_BASE - 1, 0x1000, 0x1000, &address, &next));
    CHECK(address == 7 && next == 9);
}


static void test_sampler(void) {
    NvnSampler s = {.device = 0x1234};
    nvn_sampler_defaults(&s);
    CHECK(s.device == 0x1234 && s.min_filter == 1 && s.mag_filter == 1);
    CHECK(s.wrap[0] == 1 && s.wrap[1] == 1 && s.wrap[2] == 1);
    CHECK(s.min_lod == 0 && s.max_lod == 1000 && s.lod_bias == 0);
    CHECK(s.compare_enable == 0 && s.compare == 2 && s.anisotropy == 1);
    CHECK(!s.border[0] && !s.border[1] && !s.border[2] && !s.border[3] && !s.reduction && !s.lod_snap);
    NvnSamplerDeviceWords dev = nvn_sampler_device_words;
    uint32_t words[8];
    const uint32_t defaults[8] = {0x26000, 0x362, 0xf00000, 0, 0, 0, 0, 0};
    nvn_sampler_descriptor(&s, &dev, words);
    CHECK(!memcmp(words, defaults, sizeof words));
    s.wrap[0] = 0; s.wrap[1] = 2; s.wrap[2] = 7;
    nvn_sampler_descriptor(&s, &dev, words);
    CHECK(words[0] == 0x260bc); /* 4 | (7<<3) | (2<<6) */
    nvn_sampler_defaults(&s);
    s.compare_enable = 9; s.compare = 2;
    nvn_sampler_descriptor(&s, &dev, words);
    CHECK(words[0] == 0x26600); /* enable 0x200 and compare code 1 at bit 10 */
    nvn_sampler_defaults(&s);
    s.anisotropy = 16;
    nvn_sampler_descriptor(&s, &dev, words);
    CHECK(words[0] == 0x726000);
    s.anisotropy = 2.5f;
    nvn_sampler_descriptor(&s, &dev, words);
    CHECK(words[0] == 0x126000); /* round to even: 2, anisotropy code 1 */
    nvn_sampler_defaults(&s);
    s.border[0] = s.border[3] = 0x3f800000;
    s.border[2] = 0x3f000000;
    nvn_sampler_descriptor(&s, &dev, words);
    CHECK(words[2] == 0xfff00000 && words[3] == 0x0bc00000); /* sRGB(0.5) rounds to 188 */
    CHECK(!memcmp(words + 4, s.border, sizeof s.border));
    s.border[0] = 0x3a800000; /* linear sRGB branch: (1/1024)*12.92*255 rounds to 3 */
    s.border[1] = 0xbf800000;
    s.border[2] = 0x40000000;
    nvn_sampler_descriptor(&s, &dev, words);
    CHECK(words[2] == 0x03f00000 && words[3] == 0x0ff00000);
    nvn_sampler_defaults(&s);
    s.lod_snap = 0.25f;
    nvn_sampler_descriptor(&s, &dev, words);
    CHECK(words[1] == 0x21fc0362); /* index 8, bias -64/256 */
    s.lod_snap = 0; s.lod_bias = -100; s.min_lod = 20; s.max_lod = -1;
    dev = (NvnSamplerDeviceWords){16, 0x100};
    s.reduction = 2;
    nvn_sampler_descriptor(&s, &dev, words);
    CHECK(words[1] == 0x1100b62 && words[2] == 0xf00f00);
    nvn_sampler_defaults(&s);
    s.lod_bias = 0.5f; s.reduction = 1;
    nvn_sampler_descriptor(&s, &dev, words);
    CHECK(words[1] == 0x180762); /* device adds 1, bias 384/256 */
}
static void test_snap_formula(void) {
    CHECK(nvn_sampler_snap_bias(0) == 0 && !signbit(nvn_sampler_snap_bias(0)));
    CHECK(nvn_sampler_snap_bias(1) == -1.0f / 18.0f);
    CHECK(nvn_sampler_snap_bias(7) == -7.0f / 30.0f);
    CHECK(nvn_sampler_snap_bias(8) == -0.25f);
    CHECK(nvn_sampler_snap_bias(16) == -0.375f);
    CHECK(nvn_sampler_snap_bias(24) == -0.4375f);
    CHECK(nvn_sampler_snap_bias(31) == -7.0f / 15.0f);
    for (unsigned i = 1; i < 32; i++) CHECK(nvn_sampler_snap_bias(i) < nvn_sampler_snap_bias(i - 1));
}
static void test_texture_layout(void) {
    NvnTextureBuilder b = {.device = 0x1234};
    nvn_texture_builder_defaults(&b);
    CHECK(b.device == 0x1234 && b.target == 1 && b.levels == 1);
    CHECK(!b.width && !b.height && !b.depth && !b.flags && !b.pool && !b.offset && !b.packaged);
    CHECK(!b.format && !b.samples && !b.stride && !b.tile && !b.gl_name && !b.depth_stencil_mode);
    for (unsigned i = 0; i < 4; i++) CHECK(b.swizzle[i] == i + 2);
    NvnTextureLayout l;
    b = (NvnTextureBuilder){.flags = 9, .target = 1, .width = 1920, .height = 1080, .depth = 1,
                            .levels = 1, .format = 0x25};
    CHECK(nvn_texture_layout(&b, &nvn_texture_device_words, &l));
    CHECK(l.alignment == 0x10000 && l.size == 0x870000 && l.layer_stride == 0x870000);
    CHECK(l.bytes_per_block == 4 && l.log2_bytes == 2 && l.sample_x == 1 && l.sample_y == 1);
    CHECK(l.block_width == 1 && l.block_height == 1 && l.levels == 1 && l.dim == 2 && l.tile_y == 4 && !l.kind);
    b.flags = 8; b.width = 960; b.height = 540; b.format = 0x3f;
    CHECK(nvn_texture_layout(&b, &nvn_texture_device_words, &l));
    CHECK(l.alignment == 0x10000 && l.size == 0x260000 && l.layer_stride == 0x258000);
    b.flags = 0x10; b.format = 1; b.height = 1; b.stride = 0x20;
    CHECK(nvn_texture_layout(&b, &nvn_texture_device_words, &l));
    CHECK(l.alignment == 0x20 && l.size == 0x20 && l.linear_stride == 0x20 && l.levels == 1 && l.dim == 2);
    b.target = 10; b.flags = 0; b.width = 7; b.format = 0x22;
    CHECK(nvn_texture_layout(&b, &nvn_texture_device_words, &l));
    CHECK(l.alignment == 4 && l.size == 84 && l.layer_stride == 84 && l.dim == 1 && l.levels == 1);
    b = (NvnTextureBuilder){.target = 1, .width = 16, .height = 16, .depth = 1, .levels = 3, .format = 0x25};
    CHECK(nvn_texture_layout(&b, &nvn_texture_device_words, &l));
    CHECK(l.alignment == 512 && l.size == 2048 && l.layer_stride == 2048); /* mips: 1024+512+512 */
    b.target = 8; b.levels = 1;
    CHECK(nvn_texture_layout(&b, &nvn_texture_device_words, &l));
    CHECK(l.depth == 6 && l.layer_stride == 1024 && l.size == 6144);
    b.target = 3; b.height = 4;
    CHECK(nvn_texture_layout(&b, &nvn_texture_device_words, &l));
    CHECK(l.height == 1 && l.depth == 4 && l.dim == 1 && l.layer_stride == 512 && l.size == 2048);
    b.target = 1; b.depth = 1; b.width = 128; b.height = 128; b.flags = 0x20;
    CHECK(nvn_texture_layout(&b, &nvn_texture_device_words, &l));
    CHECK(l.tile_x == 3 && l.tile_y == 4 && l.tile_z == 0 && l.size == 65536);
    b.flags = 2; b.width = 16; b.height = 16;
    CHECK(nvn_texture_layout(&b, &nvn_texture_device_words, &l));
    CHECK(l.tile_y == 1 && l.size == 1024);
    b.flags = 0x200; b.height = 17;
    CHECK(nvn_texture_layout(&b, &nvn_texture_device_words, &l));
    CHECK(l.tile_y == 0 && l.size == 1536); /* automatic tile costs 2048, minimal tile costs 1536 */
    b.flags = 0; b.width = 8; b.height = 8; b.depth = 4; b.target = 2; b.levels = 2;
    CHECK(nvn_texture_layout(&b, &nvn_texture_device_words, &l));
    CHECK(l.dim == 3 && l.tile_z == 2 && l.size == 3072);
    b.target = 1; b.depth = 1; b.levels = 1; b.width = b.height = 16; b.samples = 4;
    CHECK(nvn_texture_layout(&b, &nvn_texture_device_words, &l));
    CHECK(l.sample_x == 2 && l.sample_y == 2 && l.samples == 4 && l.size == 4096);
    b.samples = 0; b.width = b.height = 4; b.format = 0x41;
    CHECK(nvn_texture_layout(&b, &nvn_texture_device_words, &l));
    CHECK(l.bytes_per_block == 8 && l.block_width == 4 && l.block_height == 4 && l.size == 512);
    b.flags = 0; b.format = 0x25; b.width = 16; b.height = 64; b.samples = 0; b.tile = 0x21;
    CHECK(nvn_texture_layout(&b, &nvn_texture_device_words, &l));
    CHECK(l.tile_x == 0 && l.tile_y == 2 && l.tile_z == 0 && l.size == 4096);
    b.tile = 0; b.flags = 0x20; b.width = 16; b.height = 16;
    CHECK(nvn_texture_layout(&b, &nvn_texture_device_words, &l));
    CHECK(l.tile_x == 0 && l.tile_y == 1 && l.size == 1024); /* first mip does not fit a sparse tile */
    b.width = b.height = 4; b.flags = 0x40;
    CHECK(nvn_texture_storage_alignment(&b, &nvn_texture_device_words) == 0x80);
    b.flags = 0; b.format = 0x35;
    CHECK(nvn_texture_layout(&b, &nvn_texture_device_words, &l));
    CHECK(l.depth_stencil == 3 && l.alignment == 0x10000 && l.size == 0x10000);
    b.gl_name = 1;
    CHECK(nvn_texture_layout(&b, &nvn_texture_device_words, &l) && l.size == 0 && l.alignment == 0x10000);
    b.gl_name = 0; b.format = 0x25; b.flags = 8;
    NvnTextureDeviceWords dev = nvn_texture_device_words;
    dev.w19f8 = NVN_TEXTURE_COMPRESSION_DISABLED;
    CHECK(nvn_texture_layout(&b, &dev, &l) && l.alignment == 512 && l.size == 512);
    b.levels = 0;
    CHECK(nvn_texture_layout(&b, &dev, &l) && l.size == 0);
    b.target = 5;
    CHECK(nvn_texture_layout(&b, &dev, &l) && l.levels == 1 && l.size == 512);
    b.target = 10; dev.w1b18 = 7;
    CHECK(nvn_texture_storage_alignment(&b, &dev) == 0x10000);
    dev.w1b18 = 0xfe;
    CHECK(nvn_texture_storage_alignment(&b, &dev) == 4);
    b.format = 0xa1;
    CHECK(!nvn_texture_layout(&b, &dev, &l) && !l.size);
    b.flags = 0x10;
    CHECK(nvn_texture_storage_alignment(&b, &dev) == 0x20);
}

/* Expected words below are hand-computed from the accepted encoder table. The synthetic seed
   has component codes (1,2,3,4), bit31, and low format bits 0x25. No game format data is used. */
static void check_texture_descriptor(const NvnTexture* t, const NvnTextureView* v,
    const NvnTextureDeviceWords* dev, const NvnTextureFormatRecord* r, const uint32_t expected[8]) {
    uint32_t words[8];
    CHECK(nvn_texture_descriptor(t, v, dev, r, words) == NVN_TEXTURE_DESCRIPTOR_OK);
    for (unsigned i = 0; i < 8; i++) {
        if (words[i] != expected[i])
            printf("descriptor word %u: actual=%08x expected=%08x\n", i, words[i], expected[i]);
        CHECK(words[i] == expected[i]);
    }
}
static void test_texture_descriptor(void) {
    NvnTexture t = {0};
    nvn_texture_builder_defaults(&t.builder);
    t.builder.format = 0x25;
    t.layout = (NvnTextureLayout){.width = 16, .height = 8, .depth = 1, .levels = 1,
        .tile_x = 3, .tile_y = 1, .tile_z = 2, .layer_stride = 0x1000};
    t.address = 0x0412345600ull;
    NvnTextureFormatRecord r = {.seed = 0xc6880025};
    NvnTextureDeviceWords dev = nvn_texture_device_words;
    /* q3: enable 0x10000, quality 0x60000, tile 0xc88. Row 2 gives q6=0x3000000.
       q4: fixed 0xe0000000, sector 1<<27, target 1<<23, width-1=15. */
    const uint32_t rgba[8] = {0xc6880025, 0x12345600, 0x00600004, 0x00070c88,
                              0xe880000f, 0x80000007, 0x03000000, 0};
    check_texture_descriptor(&t, NULL, &dev, &r, rgba);
    CHECK(dev.w08 == 0x4000 && dev.w1a48 == 2 && dev.w1a4c == 1 && dev.w1a50 == 0);
    CHECK(dev.w1a54 == 0xffffffff && dev.w1a88 == 2 && dev.w1b1c == 1);
    CHECK(nvn_sampler_device_words.w1a80 == 0 && nvn_sampler_device_words.w1a84 == 0x111);
    /* Linear: stride>>5=0x55e6f, low16=0x5e6f; clear bits10..12, insert tile_x=3.
       Pitch kind=2 and target code=7. No sector or mip fields. */
    t.builder.flags = NVN_TEXTURE_FLAG_LINEAR;
    t.layout.linear_stride = 0xabcde0;
    const uint32_t linear[8] = {0xc6880025, 0x12345600, 0x00400004, 0x00004e6f,
                                0xe380000f, 0x80000007, 0x03000000, 0};
    check_texture_descriptor(&t, NULL, &dev, &r, linear);
    /* Buffer: width-1=0x123455 splits across q3 and q4. Only target code 6 remains. */
    t.builder.flags = 0; t.builder.target = 10; t.layout.width = 0x123456;
    const uint32_t buffer[8] = {0xc6880025, 0x12345600, 4, 0x12, 0x03003455, 0, 0, 0};
    check_texture_descriptor(&t, NULL, &dev, &r, buffer);
    /* Cube uses target code 3 and depth 1, even with six faces in the layout. */
    t.builder.target = 8; t.layout.width = 16; t.layout.depth = 6;
    uint32_t cube[8]; memcpy(cube, rgba, sizeof cube); cube[4] = 0xe980000f;
    check_texture_descriptor(&t, NULL, &dev, &r, cube);
    /* Array: layer 0x109 adds 0x109000 to A, puts 1 in q4[16:18],
       1 in q2[16:20] and 1 in q2[29:31]. Two layers give q5[16:29]=1. */
    t.builder.target = 4; t.layout.depth = 300;
    NvnTextureView v = {.flags = NVN_VIEW_LAYERS, .base_layer = 0x109, .layer_count = 2};
    const uint32_t array[8] = {0xc6880025, 0x1244e600, 0x20610004, 0x00070c88,
                               0xea81000f, 0x80010007, 0x03000000, 0};
    check_texture_descriptor(&t, &v, &dev, &r, array);
    /* Count 0 uses Z-base=35. Clearing the flag stops the address change,
       but registration still copies raw base-layer bits. */
    v.layer_count = 0;
    uint32_t remaining[8]; memcpy(remaining, array, sizeof remaining); remaining[5] = 0x80220007;
    check_texture_descriptor(&t, &v, &dev, &r, remaining);
    v.flags = 0; memcpy(remaining, array, sizeof remaining);
    remaining[1] = 0x12345600; remaining[5] = 0x812b0007;
    check_texture_descriptor(&t, &v, &dev, &r, remaining);
    /* View selects 2D and another synthetic seed. Swizzle (A,ONE,ZERO,invalid)
       selects codes (4,6,0,1). sRGB adds bit22 in q4. */
    t.builder.target = 10; t.layout.depth = 1;
    v = (NvnTextureView){.flags = NVN_VIEW_FORMAT | NVN_VIEW_SWIZZLE | NVN_VIEW_TARGET,
        .format = 0x26, .target = 1, .swizzle = {5, 1, 0, 99}};
    r = (NvnTextureFormatRecord){.seed = 0xc6880055, .one_code = 1, .srgb = 1};
    uint32_t override[8]; memcpy(override, rgba, sizeof override);
    override[0] = 0x91a00055; override[4] = 0xe8c0000f;
    check_texture_descriptor(&t, &v, &dev, &r, override);
    r.one_code = 0; override[0] = 0x91e00055;
    check_texture_descriptor(&t, &v, &dev, &r, override);
    /* Depth/stencil mode 1 uses B=0x24900000 for f=0x35 and changes low6 to 14.
       Mode 9 uses B=0x76d80000. Identity swizzle preserves each changed seed. */
    t.builder.target = 1; t.builder.format = 0x35;
    r = (NvnTextureFormatRecord){.seed = 0xc6880025, .depth_size = 24, .stencil_size = 8};
    v = (NvnTextureView){.flags = NVN_VIEW_DEPTH_STENCIL_MODE, .depth_stencil_mode = 1};
    uint32_t ds[8]; memcpy(ds, rgba, sizeof ds); ds[0] = 0xa490000e;
    check_texture_descriptor(&t, &v, &dev, &r, ds);
    v.depth_stencil_mode = 9; ds[0] = 0xf6d80025;
    check_texture_descriptor(&t, &v, &dev, &r, ds);
    /* Ordinary depth format: B=0x36d80000 or 0x74900000. A 32/8 record uses low6=5. */
    t.builder.format = 0x36; r.depth_size = 32; v.depth_stencil_mode = 1; ds[0] = 0xb6d80005;
    check_texture_descriptor(&t, &v, &dev, &r, ds);
    t.builder.depth_stencil_mode = 2; ds[0] = 0xf4900025;
    check_texture_descriptor(&t, NULL, &dev, &r, ds);
    r.depth_size = 0; ds[0] = 0xa4900025;
    check_texture_descriptor(&t, NULL, &dev, &r, ds);
    /* Multisample target 6 has code 5. Four samples give sample code 2 at q7 bit8. */
    t.builder.format = 0x25; t.builder.target = 6; t.layout.samples = 4;
    r = (NvnTextureFormatRecord){.seed = 0xc6880025};
    uint32_t ms[8]; memcpy(ms, rgba, sizeof ms); ms[4] = 0xea80000f; ms[7] = 0x200;
    check_texture_descriptor(&t, NULL, &dev, &r, ms);
    /* M=6-1=5; hi=min(4-1,5)=3. View base2,count1 gives lo=hi=2. */
    t.builder.target = 1; t.builder.levels = 4; t.layout.levels = 6;
    uint32_t mip[8]; memcpy(mip, rgba, sizeof mip); mip[3] = 0x50070c88; mip[7] = 0x30;
    check_texture_descriptor(&t, NULL, &dev, &r, mip);
    v = (NvnTextureView){.flags = NVN_VIEW_LEVELS, .base_level = 2, .level_count = 1};
    mip[7] = 0x22; check_texture_descriptor(&t, &v, &dev, &r, mip);
    v.base_level = 9; v.level_count = 0; mip[7] = 0x35;
    check_texture_descriptor(&t, &v, &dev, &r, mip);
    /* Clamp row index to 5: q3 adds 31<<21; q6=(3<<25)|(3<<23)|(1<<30). */
    dev.w1a88 = UINT32_MAX; t.builder.levels = t.layout.levels = 1;
    uint32_t clamp[8]; memcpy(clamp, rgba, sizeof clamp);
    clamp[3] = 0x03e70c88; clamp[6] = 0x47800000;
    check_texture_descriptor(&t, NULL, &dev, &r, clamp);
    uint32_t out[8], unchanged[8];
    memset(out, 0xa5, sizeof out); memcpy(unchanged, out, sizeof out);
    t.builder.format = 0xa1;
    CHECK(nvn_texture_descriptor(&t, NULL, &dev, &r, out) == NVN_TEXTURE_DESCRIPTOR_BAD_FORMAT);
    CHECK(!memcmp(out, unchanged, sizeof out));
    t.builder.format = 0xa0; t.builder.target = 11;
    CHECK(nvn_texture_descriptor(&t, NULL, &dev, &r, out) == NVN_TEXTURE_DESCRIPTOR_BAD_TARGET);
    CHECK(!memcmp(out, unchanged, sizeof out));
    v = (NvnTextureView){.flags = NVN_VIEW_FORMAT | NVN_VIEW_TARGET, .format = 0xa1, .target = 1};
    CHECK(nvn_texture_descriptor(&t, &v, &dev, &r, out) == NVN_TEXTURE_DESCRIPTOR_BAD_FORMAT);
    v.format = 0; v.target = 11;
    CHECK(nvn_texture_descriptor(&t, &v, &dev, &r, out) == NVN_TEXTURE_DESCRIPTOR_BAD_TARGET);
    CHECK(!memcmp(out, unchanged, sizeof out));
}
static void test_texture_descriptor_fields(void) {
    NvnTexture t = {.address = 0x0412345600ull};
    nvn_texture_builder_defaults(&t.builder);
    t.builder.format = 0x25;
    t.layout = (NvnTextureLayout){.width = 1, .height = 1, .depth = 1, .levels = 1, .layer_stride = 0x1000};
    NvnTextureFormatRecord r = {.seed = 0xc6880025};
    NvnTextureDeviceWords dev = nvn_texture_device_words;
    uint32_t expected[8] = {0xc6880025, 0x12345600, 0x00600004, 0x70000, 0xe8800000, 0x80000000, 0x3000000, 0};
    /* Sector codes for device values 0,1,2,3,4 are 0,2,1,3,0 at q4 bit27. */
    const uint32_t sectors[] = {0xe0800000, 0xf0800000, 0xe8800000, 0xf8800000, 0xe0800000};
    for (unsigned i = 0; i < 5; i++) {
        dev.w1a48 = i; expected[4] = sectors[i];
        check_texture_descriptor(&t, NULL, &dev, &r, expected);
    }
    dev = nvn_texture_device_words; expected[4] = 0xe8800000;
    /* Enable adds bit16. Quality codes 1,2,3,0 occupy q3 bits17..18. */
    const uint32_t overrides[] = {0x914b83af, 0xab0341cf, 0x13478abe, 0x034e1413};
    const uint32_t quality[] = {0x30000, 0x50000, 0x70000, 0x10000};
    for (unsigned i = 0; i < 4; i++) {
        dev.w1a54 = overrides[i]; expected[3] = quality[i];
        check_texture_descriptor(&t, NULL, &dev, &r, expected);
    }
    dev = nvn_texture_device_words; dev.w1a50 = 20; expected[3] = 0x50000;
    check_texture_descriptor(&t, NULL, &dev, &r, expected);
    dev.w1a4c = 0; expected[3] = 0x40000;
    check_texture_descriptor(&t, NULL, &dev, &r, expected);
    dev = nvn_texture_device_words;
    /* Each row puts its five fields at q6:25, q6:23, q3:19, q6:30, q3:21. */
    const uint32_t row_q3[] = {0x70000, 0xf0000, 0x70000, 0x70000, 0x1270000, 0x3e70000};
    const uint32_t row_q6[] = {0x1000000, 0x41000000, 0x3000000, 0x3800000, 0x7800000, 0x47800000};
    for (unsigned i = 0; i < 6; i++) {
        dev.w1a88 = i; expected[3] = row_q3[i]; expected[6] = row_q6[i];
        check_texture_descriptor(&t, NULL, &dev, &r, expected);
    }
    dev = nvn_texture_device_words; expected[3] = 0x70000; expected[6] = 0x3000000;
    /* Targets 5 and 6 both use the sample table. Samples 0,2,4,8,16,3 give 0,5,2,4,6,0. */
    const uint32_t samples[] = {0, 2, 4, 8, 16, 3};
    const uint32_t sample_words[] = {0, 0x500, 0x200, 0x400, 0x600, 0};
    for (unsigned target = 5; target <= 6; target++) {
        t.builder.target = target; expected[4] = target == 5 ? 0xe8800000 : 0xea800000;
        for (unsigned i = 0; i < 6; i++) {
            t.layout.samples = samples[i]; expected[7] = sample_words[i];
            check_texture_descriptor(&t, NULL, &dev, &r, expected);
        }
    }
    /* Target 7 clears q5 bit31. A dimension above +8 changes its target code from 1 to 7. */
    t.builder.target = 7; expected[4] = 0xe8800000; expected[5] = 0; expected[7] = 0;
    check_texture_descriptor(&t, NULL, &dev, &r, expected);
    t.layout.width = 0x4001; expected[4] = 0xeb804000;
    check_texture_descriptor(&t, NULL, &dev, &r, expected);
    t.layout.width = 1; t.layout.height = 0x4001; expected[4] = 0xeb800000; expected[5] = 0x4000;
    check_texture_descriptor(&t, NULL, &dev, &r, expected);
    /* Cube array: twelve selected faces become two cubes. A adds six layer strides. */
    t.builder.target = 9; t.layout.height = 1; t.layout.depth = 18;
    NvnTextureView v = {.flags = NVN_VIEW_LAYERS, .base_layer = 6, .layer_count = 12};
    expected[1] = 0x1234b600; expected[4] = 0xec060000; expected[5] = 0x80010000;
    check_texture_descriptor(&t, &v, &dev, &r, expected);
    v.layer_count = 0;
    check_texture_descriptor(&t, &v, &dev, &r, expected);
    /* Target 6 also applies layer count; target 7 retains all 18 layout layers. */
    t.builder.target = 6; v.layer_count = 2; expected[4] = 0xea860000;
    check_texture_descriptor(&t, &v, &dev, &r, expected);
    t.builder.target = 7; expected[4] = 0xe8860000; expected[5] = 0x00110000;
    check_texture_descriptor(&t, &v, &dev, &r, expected);
    /* Registration: flags bit5 -> q5 bit30; bit3 -> q6 bit0 for block kind 3. */
    t.builder.target = 1; t.builder.flags = 0x28; t.layout.depth = 1;
    expected[1] = 0x12345600; expected[4] = 0xe8800000; expected[5] = 0xc0000000; expected[6] = 0x3000001;
    check_texture_descriptor(&t, NULL, &dev, &r, expected);
    /* Buffer kind 0 puts flags bit3 at q5 bit31 instead. */
    t.builder.target = 10;
    expected[2] = 4; expected[3] = 0; expected[4] = 0x03000000; expected[6] = 0;
    check_texture_descriptor(&t, NULL, &dev, &r, expected);
}
static void test_packaged_tile(void) {
    CHECK(nvn_texture_packaged_tile(0) == 1);
    CHECK(nvn_texture_packaged_tile(0x123) == 0x1231);
    CHECK(nvn_texture_packaged_tile(0xfffffabc) == 0xabc1);
    CHECK(nvn_texture_packaged_tile(UINT32_MAX) == 0xfff1);
}

/* Optional external diagnostic input. The game log remains outside the source tree. */
static void test_texture_queries(const char* path) {
    FILE* file = fopen(path, "r");
    CHECK(file != NULL);
    if (!file) return;
    char line[2048];
    unsigned count = 0, agree = 0;
    while (fgets(line, sizeof line, file)) {
        char* query = strstr(line, "nvn-size: nvnTextureBuilderGetStorage");
        if (!query) continue;
        int alignment = strstr(query, "GetStorageAlignment") != NULL;
        char* result = strstr(query, "result=");
        if (!result) continue;
        uint64_t expected = strtoull(result + 7, NULL, 16);
        uint32_t words[32] = {0};
        unsigned rows = 0;
        for (; rows < 8 && fgets(line, sizeof line, file); rows++) {
            char* row = strstr(line, "nvn-size:   word");
            unsigned at, a, b, c, d;
            if (!row || sscanf(row, "nvn-size:   word %u: %x %x %x %x", &at, &a, &b, &c, &d) != 5 || at != rows * 4) break;
            words[at] = a; words[at + 1] = b; words[at + 2] = c; words[at + 3] = d;
        }
        CHECK(rows == 8);
        if (rows != 8) break;
        NvnTextureBuilder b = {.flags = words[8], .target = words[9], .width = words[10], .height = words[11],
            .depth = words[12], .levels = words[13], .format = words[14], .samples = words[15],
            .tile = (uint16_t)words[24], .stride = words[25], .gl_name = words[26]};
        NvnTextureLayout l;
        nvn_texture_layout(&b, &nvn_texture_device_words, &l);
        uint64_t actual = alignment ? nvn_texture_storage_alignment(&b, &nvn_texture_device_words) : l.size;
        count++;
        if (expected == actual) agree++;
        else printf("query %u: %s actual=%llx expected=%llx\n", count, alignment ? "alignment" : "size",
                    (unsigned long long)actual, (unsigned long long)expected);
    }
    fclose(file);
    printf("texture queries: %u/%u agree\n", agree, count);
    CHECK(count == 40 && agree == count);
}

static void test_descriptor_offset(void) {
    uint64_t offset = 99;
    CHECK(nvn_descriptor_offset(0x100, 4, 0x1000, 32, 0, &offset) && offset == 0x100);
    CHECK(nvn_descriptor_offset(0x100, 4, 0x1000, 32, 3, &offset) && offset == 0x160);
    CHECK(nvn_descriptor_offset(0xf80, 4, 0x1000, 32, 3, &offset) && offset == 0xfe0);
    CHECK(!nvn_descriptor_offset(0x100, 4, 0x1000, 32, -1, &offset));
    CHECK(!nvn_descriptor_offset(0x100, 4, 0x1000, 32, 4, &offset));
    CHECK(!nvn_descriptor_offset(0x100, 4, 0x1000, 32, INT32_MIN, &offset));
    CHECK(!nvn_descriptor_offset(0xf81, 4, 0x1000, 32, 0, &offset));
    CHECK(!nvn_descriptor_offset(0x1001, 0, 0x1000, 32, 0, &offset));
    CHECK(!nvn_descriptor_offset(UINT64_MAX, 1, 0x1000, 32, 0, &offset));
    CHECK(!nvn_descriptor_offset(0, UINT32_MAX, 0x1000, 32, 0, &offset));
}

int main(int argc, char** argv) {
    test_window_params();
    test_window_sync_cycle_params();
    test_sampler();
    test_packaged_tile();
    test_texture_descriptor();
    test_texture_descriptor_fields();
    test_snap_formula();
    test_texture_layout();
    if (argc == 2) test_texture_queries(argv[1]);
    test_device_integer();
    test_timestamp();
    test_queue_memory();
    test_queue_defaults();
    test_memory_pool_defaults();
    test_memory_pool_params();
    test_pool_gpu_allocate();
    test_descriptor_offset();
    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("nvn_params: ok\n");
    return 0;
}
