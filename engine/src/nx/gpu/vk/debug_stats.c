/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/gpu/vk/debug_stats.h"
#include <stddef.h>
#include <string.h>
#include "common/filesystem/host.h"
#include "common/utilities/ascii.h"
#include "common/utilities/common.h"
#include "nx/runtime/platform/log.h"
#include "nx/runtime/platform/plat.h"
#include "nx/runtime/platform/tracy.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

enum {
    AVERAGED_FRAMES = 60, /* the frames the per-frame costs average over */
    THREAD_STACK = 1 << 18,
    NVML_TEMPERATURE_GPU = 0,
    NVML_CLOCK_MEM = 2,
    UUID_BYTES = 16,
    UUID_TEXT_MAX = 48, /* "GPU-" and 36 characters, as NVML writes it */
    MB = 1 << 20
};
static const uint64_t POLL_NS = 1000000000;  /* the machine is read once a second */

typedef struct {
    float ms, prep_ms, cpu_ms, wait_ms, uploads;
} FrameRecord;

static PMutex g_lock = PMUTEX_INIT;
static FrameRecord g_ring[DEBUG_STATS_FRAMES];
static int g_next, g_count;
static uint64_t g_last_ns, g_cpu_ns, g_wait_ms, g_uploads;
static DebugStats g_hw; /* only the machine's fields are kept here, under g_lock */
static int g_demand;       /* the window is open: atomic */
static int g_thread_tried; /* under g_lock: the thread is started once, and a failure is not tried again */
static uint8_t g_gpu_uuid[UUID_BYTES];

void debug_stats_frame(uint64_t now_ns, uint64_t prep_ns, uint64_t pipeline_wait_ms, uint64_t texture_uploads) {
    uint64_t cpu = snx_process_cpu_ns();
    pm_lock(&g_lock);
    if (g_last_ns) {
        FrameRecord r = {.ms = (float)(now_ns - g_last_ns) / 1e6f,
                         .prep_ms = (float)prep_ns / 1e6f,
                         .cpu_ms = cpu && g_cpu_ns ? (float)(cpu - g_cpu_ns) / 1e6f : -1,
                         .wait_ms = (float)(pipeline_wait_ms - g_wait_ms),
                         .uploads = (float)(texture_uploads - g_uploads)};
        g_ring[g_next] = r;
        g_next = (g_next + 1) % DEBUG_STATS_FRAMES;
        if (g_count < DEBUG_STATS_FRAMES) ++g_count;
    }
    g_last_ns = now_ns;
    g_cpu_ns = cpu;
    g_wait_ms = pipeline_wait_ms;
    g_uploads = texture_uploads;
    pm_unlock(&g_lock);
}

/* the record j frames back from the newest, j below g_count. Under g_lock */
static const FrameRecord* back(int j) { return &g_ring[(g_next + DEBUG_STATS_FRAMES - 1 - j) % DEBUG_STATS_FRAMES]; }

/* the mean of the field over the last AVERAGED_FRAMES, -1 when there are none or a record had no value (< 0) */
static float mean(size_t offset) {
    int n = g_count < AVERAGED_FRAMES ? g_count : AVERAGED_FRAMES, j, used = 0;
    double sum = 0;
    for (j = 0; j < n; ++j) {
        float v = *(const float*)((const char*)back(j) + offset);
        if (v < 0) continue;
        sum += v;
        ++used;
    }
    return used ? (float)(sum / used) : -1;
}

void debug_stats_get(DebugStats* out) {
    int j;
    *out = (DebugStats){0};
    pm_lock(&g_lock);
    out->frame_count = g_count;
    for (j = 0; j < g_count; ++j) out->frame_ms[g_count - 1 - j] = back(j)->ms;
    out->fps = out->frame_last_ms = out->frame_max_ms = -1;
    if (g_count >= 2) {
        double span = 0;
        float max = 0;
        for (j = 0; j < g_count; ++j) {
            span += back(j)->ms;
            if (back(j)->ms > max) max = back(j)->ms;
        }
        out->fps = span > 0 ? (float)(g_count * 1000.0 / span) : -1;
        out->frame_last_ms = back(0)->ms;
        out->frame_max_ms = max;
    }
    out->prep_ms = mean(offsetof(FrameRecord, prep_ms));
    out->cpu_ms = mean(offsetof(FrameRecord, cpu_ms));
    out->pipeline_wait_ms = mean(offsetof(FrameRecord, wait_ms));
    out->texture_uploads = mean(offsetof(FrameRecord, uploads));
    out->gpu_load = g_hw.gpu_load;
    out->gpu_temp = g_hw.gpu_temp;
    out->gpu_power = g_hw.gpu_power;
    out->vram_used_mb = g_hw.vram_used_mb;
    out->vram_total_mb = g_hw.vram_total_mb;
    out->vram_clock_mhz = g_hw.vram_clock_mhz;
    out->cpu_load = g_hw.cpu_load;
    out->cpu_temp = g_hw.cpu_temp;
    out->cpu_power = g_hw.cpu_power;
    out->ram_used_mb = g_hw.ram_used_mb;
    out->ram_total_mb = g_hw.ram_total_mb;
    out->game_ram_mb = g_hw.game_ram_mb;
    pm_unlock(&g_lock);
}

/* ---------------------------------------------------------------- NVIDIA: NVML, loaded at run time. It comes with
   the driver, so nothing of NVIDIA's is shipped or linked, and a machine without it reports the GPU as unknown.
   The types have NVML's shapes: an opaque device pointer, and int for its enum results */
typedef struct NvmlDeviceOpaque* NvmlDevice;
typedef struct {
    unsigned gpu, memory;
} NvmlUtilization;
typedef struct {
    unsigned long long total, free, used;
} NvmlMemory;
typedef struct {
    void* library;
    int initialized;
    int (*init)(void);
    int (*shutdown)(void);
    int (*count)(unsigned*);
    int (*handle)(unsigned, NvmlDevice*);
    int (*uuid)(NvmlDevice, char*, unsigned);
    int (*utilization)(NvmlDevice, NvmlUtilization*);
    int (*temperature)(NvmlDevice, int, unsigned*);
    int (*power)(NvmlDevice, unsigned*);
    int (*memory)(NvmlDevice, NvmlMemory*);
    int (*clock)(NvmlDevice, int, unsigned*);
    NvmlDevice device;
} Nvml;

static void nvml_unload(Nvml* n) {
    if (n->initialized) n->shutdown();
    if (n->library) snx_library_close(n->library);
    *n = (Nvml){0};
}

/* the symbol into the function pointer at slot, through memcpy: an object pointer and a function pointer don't alias */
static int nvml_bind(Nvml* n, void* slot, const char* symbol) {
    void* address = snx_library_symbol(n->library, symbol);
    if (!address) {
        LOG("debug: the NVIDIA management library has no %s\n", symbol);
        return 0;
    }
    memcpy(slot, &address, sizeof address);
    return 1;
}

/* the text of a Vulkan device UUID the way NVML writes its own: GPU-xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx */
static void uuid_text(const uint8_t uuid[UUID_BYTES], char* out, size_t size) {
    static const char HEX[] = "0123456789abcdef";
    size_t at = 4;
    if (size < UUID_TEXT_MAX) return;
    memcpy(out, "GPU-", 4);
    for (int i = 0; i < UUID_BYTES; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out[at++] = '-';
        out[at++] = HEX[uuid[i] >> 4];
        out[at++] = HEX[uuid[i] & 15];
    }
    out[at] = 0;
}

static int same_uuid(const char* a, const char* b) {
    for (; *a && *b; ++a, ++b)
        if (snx_to_lower(*a) != snx_to_lower(*b)) return 0;
    return *a == *b;
}

/* opens NVML and picks the device that has the rendering Vulkan device's UUID. 0 with the cause logged and nothing
   left open: the GPU then shows as unknown */
static int nvml_load(Nvml* n, const uint8_t vulkan_uuid[UUID_BYTES]) {
#ifdef _WIN32
    static const char* const NAMES[] = {"nvml.dll", "C:\\Program Files\\NVIDIA Corporation\\NVSMI\\nvml.dll"};
#else
    static const char* const NAMES[] = {"libnvidia-ml.so.1"};
#endif
    char error[160] = "", wanted[UUID_TEXT_MAX];
    unsigned count = 0;
    int result;
    for (size_t i = 0; i < sizeof NAMES / sizeof *NAMES && !n->library; ++i)
        n->library = snx_library_open(NAMES[i], error, sizeof error);
    if (!n->library) {
        LOG("debug: no NVIDIA management library (last try: %s)\n", error);
        return 0;
    }
    if (!nvml_bind(n, &n->init, "nvmlInit_v2") || !nvml_bind(n, &n->shutdown, "nvmlShutdown") ||
        !nvml_bind(n, &n->count, "nvmlDeviceGetCount_v2") || !nvml_bind(n, &n->handle, "nvmlDeviceGetHandleByIndex_v2") ||
        !nvml_bind(n, &n->uuid, "nvmlDeviceGetUUID") || !nvml_bind(n, &n->utilization, "nvmlDeviceGetUtilizationRates") ||
        !nvml_bind(n, &n->temperature, "nvmlDeviceGetTemperature") || !nvml_bind(n, &n->power, "nvmlDeviceGetPowerUsage") ||
        !nvml_bind(n, &n->memory, "nvmlDeviceGetMemoryInfo") || !nvml_bind(n, &n->clock, "nvmlDeviceGetClockInfo")) {
        nvml_unload(n);
        return 0;
    }
    if ((result = n->init())) {
        LOG("debug: nvmlInit_v2 failed with %d\n", result);
        nvml_unload(n);
        return 0;
    }
    n->initialized = 1;
    if ((result = n->count(&count))) {
        LOG("debug: nvmlDeviceGetCount_v2 failed with %d\n", result);
        nvml_unload(n);
        return 0;
    }
    uuid_text(vulkan_uuid, wanted, sizeof wanted);
    for (unsigned i = 0; i < count; ++i) {
        NvmlDevice dev;
        char text[UUID_TEXT_MAX];
        if (n->handle(i, &dev) || n->uuid(dev, text, sizeof text)) continue;
        if (same_uuid(text, wanted)) {
            n->device = dev;
            return 1;
        }
    }
    LOG("debug: none of the %u NVML devices has the rendering GPU's UUID (%s): the GPU shows as unknown\n", count, wanted);
    nvml_unload(n);
    return 0;
}

static void nvml_poll(const Nvml* n, DebugStats* s) {
    NvmlUtilization u;
    NvmlMemory m;
    unsigned v;
    if (!n->utilization(n->device, &u)) s->gpu_load = (int)u.gpu;
    if (!n->temperature(n->device, NVML_TEMPERATURE_GPU, &v)) s->gpu_temp = (int)v;
    if (!n->power(n->device, &v)) s->gpu_power = (int)((v + 500) / 1000); /* milliwatts */
    if (!n->memory(n->device, &m)) {
        s->vram_used_mb = (int)(m.used / MB);
        s->vram_total_mb = (int)(m.total / MB);
    }
    if (!n->clock(n->device, NVML_CLOCK_MEM, &v)) s->vram_clock_mhz = (int)v;
}

/* ---------------------------------------------------------------- the machine's processor and memory */
#ifdef _WIN32
static uint64_t filetime(FILETIME f) { return (uint64_t)f.dwHighDateTime << 32 | f.dwLowDateTime; }

static void system_poll(DebugStats* s) {
    static uint64_t idle_was, total_was;
    FILETIME idle, kernel, user;
    MEMORYSTATUSEX mem = {.dwLength = sizeof mem};
    if (GetSystemTimes(&idle, &kernel, &user)) {
        uint64_t i = filetime(idle), total = filetime(kernel) + filetime(user); /* the kernel's time holds the idle */
        if (total_was && total > total_was) s->cpu_load = (int)(100 - (i - idle_was) * 100 / (total - total_was));
        idle_was = i;
        total_was = total;
    }
    if (GlobalMemoryStatusEx(&mem)) {
        s->ram_total_mb = (int)(mem.ullTotalPhys / MB);
        s->ram_used_mb = (int)((mem.ullTotalPhys - mem.ullAvailPhys) / MB);
    }
}
#else
static void system_poll(DebugStats* s) { (void)s; }
#endif

static DebugStats unknown_machine(void) {
    DebugStats s = {0};
    s.gpu_load = s.gpu_temp = s.gpu_power = s.vram_used_mb = s.vram_total_mb = s.vram_clock_mhz = -1;
    s.cpu_load = s.cpu_temp = s.cpu_power = s.ram_used_mb = s.ram_total_mb = s.game_ram_mb = -1;
    return s;
}

static void machine_thread(void* arg) {
    Nvml nvml = {0};
    uint8_t uuid[UUID_BYTES];
    int have_nvml, polled = 0;
    (void)arg;
    SNX_THREAD_NAME("Debug stats");
    pm_lock(&g_lock);
    memcpy(uuid, g_gpu_uuid, sizeof uuid);
    pm_unlock(&g_lock);
    have_nvml = nvml_load(&nvml, uuid);
    for (;;) {
        if (__atomic_load_n(&g_demand, __ATOMIC_ACQUIRE)) {
            DebugStats s = unknown_machine();
            if (have_nvml) nvml_poll(&nvml, &s);
            system_poll(&s);
            s.game_ram_mb = (int)(snx_footprint() / MB);
            pm_lock(&g_lock);
            g_hw = s;
            pm_unlock(&g_lock);
            polled = 1;
        } else if (polled) { /* closed: the next open starts from unknown, not from the last reading */
            pm_lock(&g_lock);
            g_hw = unknown_machine();
            pm_unlock(&g_lock);
            polled = 0;
        }
        plat_sleep_ns((int64_t)POLL_NS);
    }
}

void debug_stats_hardware_demand(const uint8_t* gpu_uuid) {
    __atomic_store_n(&g_demand, gpu_uuid != NULL, __ATOMIC_RELEASE);
    if (!gpu_uuid) return;
    pm_lock(&g_lock);
    if (!g_thread_tried) {
        g_thread_tried = 1;
        g_hw = unknown_machine();
        memcpy(g_gpu_uuid, gpu_uuid, sizeof g_gpu_uuid);
        if (plat_thread(machine_thread, NULL, THREAD_STACK)) /* 0 is a thread started */
            LOG("debug: the thread that reads the machine did not start: the hardware lines show as unknown\n");
    }
    pm_unlock(&g_lock);
}
