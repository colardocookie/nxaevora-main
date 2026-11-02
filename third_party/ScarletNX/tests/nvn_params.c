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
    NvnSamplerDeviceWords dev = {0, 0};
    uint32_t words[8];
    const uint32_t defaults[8] = {0x26000, 0x262, 0xf00000, 0, 0, 0, 0, 0};
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
    CHECK(words[1] == 0x21fc0262); /* index 8, bias -64/256 */
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

int main(int argc, char** argv) {
    test_sampler();
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
    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("nvn_params: ok\n");
    return 0;
}
