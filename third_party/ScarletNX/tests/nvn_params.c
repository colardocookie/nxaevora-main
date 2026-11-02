/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* Device and queue cases use the library results of 2026-11-02. Pool cases check the accepted disassembly table
   and the host GPU address allocation rule. */
#include "nx/gpu/nvn_params.h"
#include <stdio.h>
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

int main(void) {
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
