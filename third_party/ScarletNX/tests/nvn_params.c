/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The pure parts of the host's device and queue functions (gpu/nvn_params.c): the GetInteger table, the timestamp
   conversion and the queue memory size, against the values that the game's NVN library gave in the run of 2026-11-02. */
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

int main(void) {
    test_device_integer();
    test_timestamp();
    test_queue_memory();
    test_queue_defaults();
    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("nvn_params: ok\n");
    return 0;
}
