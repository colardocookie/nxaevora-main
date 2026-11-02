/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/gpu/nvn_params.h"
#include <stddef.h>
#include <string.h>

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
