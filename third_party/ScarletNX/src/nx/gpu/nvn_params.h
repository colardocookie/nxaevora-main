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

#endif
