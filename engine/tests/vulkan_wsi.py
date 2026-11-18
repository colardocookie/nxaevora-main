# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
"""Run the renderer's acquire and recorder's present against a controlled two-thread Vulkan stand-in.
Usage on a POSIX host: python3 engine/tests/vulkan_wsi.py clang -O2 -fsanitize=thread
"""
from pathlib import Path
import subprocess
import sys
import tempfile

SRC = Path(__file__).resolve().parents[1] / "src/nx"


def section(source, first, following):
    start = source.index(first)
    return source[start:source.index(following, start)]


def fixture():
    record = (SRC / "gpu/vk/record.c").read_text()
    renderer = (SRC / "gpu/vk/renderer.c").read_text()
    main = (SRC / "runtime/main.c").read_text()
    return "\n".join([
        HEAD,
        section(record, "int g_vk_wsi_lock;", "/* A present,"),
        section(record, "typedef struct {\n    VkSwapchainKHR sc;", "/* ----"),
        "static void acquire(int game, VkSemaphore acq, u32* out, VkResult* result) { u32 idx = 77;",
        section(renderer, "VkResult r;\n        if (g_vk_wsi_lock)", "if (r == VK_NOT_READY"),
        "*out = idx; *result = r; }",
        REFRESH,
        section(renderer, "static void vkr_refresh(void) {", "/* a part of a surface"),
        "static void configure(void) {",
        section(main, 'const char* wsi_lock = getenv', "\n    }"),
        "}",
        CASES,
    ])


HEAD = r'''
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define FRAMES 1200

typedef int VkSemaphore, VkSwapchainKHR, VkResult;
typedef uint64_t u64;
typedef uint32_t u32;
typedef pthread_mutex_t PMutex;
#define PMUTEX_INIT PTHREAD_MUTEX_INITIALIZER
#define VK_NULL_HANDLE 0
#define VK_SUCCESS 0
#define VK_STRUCTURE_TYPE_PRESENT_INFO_KHR 1
#define SNX_ZONE(zone, name) ((void)0)
static struct { int dev, swapchain, gfx; } g_vk = {1, 2, 3};
typedef struct {
    int sType, waitSemaphoreCount;
    const VkSemaphore* pWaitSemaphores;
    int swapchainCount;
    const VkSwapchainKHR* pSwapchains;
    const u32* pImageIndices;
} VkPresentInfoKHR;

static int g_rec, acquiring_result, presenting_result, lines;
static _Thread_local int held, producer;
static atomic_int inside_present, may_finish;
static int ui_case, render_ends, redraws, renderer_locked;
static u64 ui_now, g_pending, g_tail;
static int force_contention, acquire_calls, present_calls, locks, tries, waited;
static char line[200];
static void log_message(const char* fmt, ...) {
    assert(!held);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    lines++;
}
#define LOG(...) log_message(__VA_ARGS__)
static void pm_lock(PMutex* m) {
    assert(!held);
    if (producer) {
        /* During a UI redraw only a return to dispatch releases the recorder. */
        assert(!ui_case);
        locks++;
        if (force_contention) atomic_store(&may_finish, 1);
    }
    assert(!pthread_mutex_lock(m));
    held = 1;
}
static int pm_trylock(PMutex* m) {
    assert(producer && !held);
    tries++;
    if (pthread_mutex_trylock(m)) return 0;
    held = 1;
    return 1;
}
static void pm_unlock(PMutex* m) {
    assert(held);
    held = 0;
    assert(!pthread_mutex_unlock(m));
}
static u64 plat_ns(void) { return ui_now; }
extern int g_vk_wsi_lock;
static VkResult vkAcquireNextImageKHR(int dev, int sc, u64 timeout, int sem, int fence, u32* idx) {
    assert(producer && held == g_vk_wsi_lock);
    assert(dev == 1 && sc == 2 && timeout == 0 && sem == 4 && !fence);
    if (force_contention) {
        assert(atomic_load(&inside_present) == !g_vk_wsi_lock);
        if (!ui_case) atomic_store(&may_finish, 1);
    }
    acquire_calls++;
    if (acquiring_result == VK_SUCCESS || acquiring_result == 1000001003) *idx = 5;
    return acquiring_result;
}
static VkResult vkQueuePresentKHR(int queue, const VkPresentInfoKHR* pi) {
    assert(held == g_vk_wsi_lock && queue == 3 && pi->sType == VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);
    assert(pi->waitSemaphoreCount == 1 && *pi->pWaitSemaphores == 6);
    assert(pi->swapchainCount == 1 && *pi->pSwapchains == 2 && *pi->pImageIndices == 5);
    present_calls++;
    if (force_contention && g_rec) {
        atomic_store(&inside_present, 1);
        while (!atomic_load(&may_finish)) sched_yield();
        atomic_store(&inside_present, 0);
    }
    return presenting_result;
}
static void* packet;
static void (*queued)(const void*);
static void* rec_push(void (*fn)(const void*), u64 size) {
    assert(!held && !queued && !packet);
    g_pending += size;
    packet = malloc(size);
    assert(packet);
    queued = fn;
    return packet;
}
static void publish(void) { assert(!held); }
static VkResult vk_rec_present_result(void);
'''

REFRESH = r'''
static u64 g_shown_ns;
static u32 g_last_w, g_last_h, g_last_transform;
static void* g_last;
static int backend_window_shown(void) { return 1; }
static int vk_trylock(void) { assert(!renderer_locked); return renderer_locked = 1; }
static void vk_unlock(void) { assert(renderer_locked); renderer_locked = 0; }
static void vk_render_end(void) { render_ends++; }
static void show(void* src, u32 w, u32 h, u32 transform, int game) {
    (void)src; (void)w; (void)h; (void)transform;
    assert(renderer_locked && !game);
    u32 idx = 77;
    VkResult result = -1;
    acquire(game, 4, &idx, &result);
    if (result == -1) { assert(idx == 77); return; }
    assert(idx == 5 && result == VK_SUCCESS);
    redraws++;
}
'''

CASES = r'''
static void* recorder(void* unused) {
    (void)unused;
    queued(packet);
    assert(!held);
    return NULL;
}
static void check_acquire(void) {
    u32 idx;
    VkResult result;
    acquire(1, 4, &idx, &result);
    assert(result == acquiring_result && !held);
    assert(idx == (acquiring_result == VK_SUCCESS || acquiring_result == 1000001003 ? 5u : 77u));
}
static void check_dispatch(void) {
    acquiring_result = presenting_result = VK_SUCCESS;
    force_contention = g_rec;
    atomic_store(&may_finish, 0);
    assert(vk_rec_present(2, 5, 6) == VK_SUCCESS);
    pthread_t thread;
    if (g_rec) {
        assert(!pthread_create(&thread, NULL, recorder, NULL));
        while (!atomic_load(&inside_present)) sched_yield();
    }
    ui_case = 1;
    ui_now = 1000000000;

    const int defer = g_vk_wsi_lock && g_rec;
    vkr_refresh();
    assert(!renderer_locked && render_ends == !defer && redraws == !defer);

    /* Isolate the acquire decision too, with the earlier recorder-work guard bypassed in the model. */
    ui_now += 40000000;
    __atomic_store_n(&g_tail, g_pending, __ATOMIC_RELEASE);
    vkr_refresh();
    assert(!renderer_locked && render_ends == 2 - defer && redraws == 2 * !defer);
    assert(!atomic_load(&may_finish));
    atomic_store(&may_finish, 1); /* only dispatch on the window owner releases this present */
    if (g_rec) {
        assert(!pthread_join(thread, NULL));
        free(packet);
        packet = NULL;
        queued = NULL;
    }
    force_contention = 0;
    g_pending += 32; /* unrelated later commands are still queued during the retry */
    ui_now += 40000000;
    vkr_refresh();
    assert(!renderer_locked && redraws == 2 * !defer + 1);
}
int main(int argc, char** argv) {
    assert(argc == 4);
    int enabled = atoi(argv[1]), threaded = atoi(argv[2]), frames = FRAMES;
    if (!strcmp(argv[3], "unset")) unsetenv("SNX_ARCEUS_WSI_LOCK");
    else setenv("SNX_ARCEUS_WSI_LOCK", argv[3], 1);
    configure();
    assert(g_vk_wsi_lock == enabled && lines == enabled);
    producer = 1;
    g_rec = threaded;
    for (int i = 0; i < frames; i++) {
        force_contention = threaded && (i == 0 || i == 17);
        presenting_result = i % 2 ? 1000001003 : -1000001004;
        /* Success, suboptimal, not ready, timeout, out of date and surface lost. */
        static const int results[] = {0, 1000001003, 1, 2, -1000001004, -1000000000};
        acquiring_result = results[i % (sizeof results / sizeof results[0])];
        VkResult r = vk_rec_present(2, 5, 6);
        assert(r == (threaded ? VK_SUCCESS : presenting_result) && !held);
        if (threaded) {
            assert(queued);
            if (force_contention) {
                pthread_t thread;
                atomic_store(&may_finish, 0);
                assert(!pthread_create(&thread, NULL, recorder, NULL));
                while (!atomic_load(&inside_present)) sched_yield();
                check_acquire();
                assert(!pthread_join(thread, NULL));
                waited++;
            } else {
                queued(packet);
                check_acquire();
            }
            free(packet);
            packet = NULL;
            queued = NULL;
            assert(vk_rec_present_result() == presenting_result);
            assert(vk_rec_present_result() == VK_SUCCESS);
        } else check_acquire();
        assert(lines == enabled);
    }
    assert(acquire_calls == frames && present_calls == frames);
    assert(tries == 0);
    /* The uncontended queued packets above run on the producer to reuse the same mock. */
    int same_thread_presents = frames - waited;
    assert(locks == enabled * (same_thread_presents + frames));
    check_dispatch();
    return 0;
}
'''

if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as directory:
        exe = Path(directory) / "wsi"
        cc = sys.argv[1:] or ["cc", "-O2"]
        subprocess.run(cc + ["-std=gnu11", "-Wall", "-Wextra", "-Werror", "-pthread",
                             "-I", str(SRC.parent), "-x", "c", "-", "-o", str(exe)],
                       input=fixture(), text=True, check=True)
        for value in ("unset", "0", "1", "01", "2"):
            for threaded in ("0", "1"):
                subprocess.run([exe, str(int(value == "1")), threaded, value], check=True, timeout=12)
    print("WSI acquire/present: 10 configurations passed")
