/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* A fixture for vk_rec_direct (gpu/vk/record.c), which hands a library the frame's command buffer. The recorder's
   own file is compiled as it is, ring, recording thread, drain and failure mark, against stand-in headers for
   Vulkan and the renderer (include/), with the host's own mutex. Every Vulkan call is a fake that notes itself.
   Usage: fixture thread|once <scenario>. One scenario a process, since the failure mark is never cleared. */
#include "nx/gpu/vk/vk.h"
#include "nx/gpu/vk/upscaler.h" /* the answers, generated from the real header by run.sh */
#include "nx/runtime/options.h"
#include "nx/runtime/platform/plat.h"
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

/* ------------------------------------------------------------ what the recorder's file needs of the runtime */
FixtureSettings g_settings;
VkState g_vk = {(VkDevice)0x10, (VkQueue)0x20, VK_NULL_HANDLE};
/* the renderer's outer lock, as gpu/vk/device.c has it: reentrant on the thread that holds it */
static PMutex g_vklock = PMUTEX_INIT;
static __thread int t_vkdepth;
void vk_lock(void) {
    if (!t_vkdepth++) pm_lock(&g_vklock);
}
void vk_unlock(void) {
    assert(t_vkdepth > 0);
    if (!--t_vkdepth) pm_unlock(&g_vklock);
}
void pc_init(PCond* c) { pthread_cond_init(&c->c, NULL); }
void pc_signal(PCond* c) { pthread_cond_signal(&c->c); }
int pc_wait(PCond* c, PMutex* m, i64 timeout_ns) {
    assert(timeout_ns < 0);
    pthread_cond_wait(&c->c, &m->l);
    return 0;
}
typedef struct {
    PThreadFn fn;
    void* arg;
} Start;
static void* start_run(void* p) {
    Start s = *(Start*)p;
    free(p);
    s.fn(s.arg);
    return NULL;
}
int plat_thread(PThreadFn fn, void* arg, size_t stack) {
    Start* s = malloc(sizeof *s);
    pthread_attr_t a;
    pthread_t t;
    (void)stack;
    if (!s) return -1;
    *s = (Start){fn, arg};
    pthread_attr_init(&a);
    pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
    const int r = pthread_create(&t, &a, start_run, s);
    pthread_attr_destroy(&a);
    if (r) free(s);
    return r ? -1 : 0;
}
void plat_yield(void) { sched_yield(); }
char* snx_os_error(char* out, size_t size) {
    snprintf(out, size, "the fixture has no reason");
    return out;
}
int vk_ok_(VkResult r, const char* what, const char* file, int line) {
    (void)what;
    (void)file;
    (void)line;
    return r >= 0;
}
void vk_wait_foreground(void) {}
VkCommandBuffer vk_cmd(void) { return VK_NULL_HANDLE; }
u32 vk_frame_slot(void) { return 0; }
VkDescriptorSet vk_desc_alloc(VkDescriptorSetLayout layout) {
    (void)layout;
    return VK_NULL_HANDLE;
}
VkDescriptorPool vk_desc_pool_new(void) { return VK_NULL_HANDLE; }
VkPipeline vk_pipeline_resolve(VkPipeline h) { return h; }
VkPipeline vk_pipeline_now(VkPipeline h, int* pending) {
    *pending = 0;
    return h;
}

/* ------------------------------------------------------------ the list of what was made, and by whom */
/* One list for every fake, written without a lock on purpose: the recording thread writes it while it records,
   and the thread that calls a library writes it while the library records. If the two ever did so at the same
   time the thread sanitizer would say so. who: 0 the recording thread, 1 the main thread, 2 and 3 intruders. */
enum { BEGIN, COMMAND, DRAW, END, SUBMIT, SIGNAL, FORGET };
typedef struct {
    int kind, who;
    u32 tag;
    const char* name;
    VkCommandBuffer cb;
} Event;
#define EVENTS (1u << 20)
static Event g_events[EVENTS];
static size_t g_count;
static __thread int t_who;
struct VkCommandBuffer_T {
    int begun, ended, submitted;
};
static int g_fail_begin, g_fail_end, g_fail_submit, g_slow_ms; /* set before the recorder starts, then only read */
static void note(int kind, VkCommandBuffer cb, const char* name, u32 tag) {
    assert(g_count < EVENTS);
    g_events[g_count++] = (Event){kind, t_who, tag, name, cb};
}
static void pause_ms(int ms) {
    const struct timespec t = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&t, NULL);
}
VkResult vkBeginCommandBuffer(VkCommandBuffer cb, const VkCommandBufferBeginInfo* info) {
    (void)info;
    if (g_fail_begin) {
        pause_ms(g_slow_ms);
        return VK_ERROR_UNKNOWN;
    }
    cb->begun = 1;
    note(BEGIN, cb, "vkBeginCommandBuffer", 0);
    return VK_SUCCESS;
}
VkResult vkEndCommandBuffer(VkCommandBuffer cb) {
    if (g_fail_end) {
        pause_ms(g_slow_ms);
        return VK_ERROR_UNKNOWN;
    }
    cb->ended = 1;
    note(END, cb, "vkEndCommandBuffer", 0);
    return VK_SUCCESS;
}
VkResult vkQueueSubmit2(VkQueue queue, u32 n, const VkSubmitInfo2* submits, VkFence fence) {
    VkCommandBuffer cb = submits->pCommandBufferInfos[0].commandBuffer;
    (void)fence;
    assert(queue == g_vk.gfx && n == 1);
    if (g_fail_submit) {
        pause_ms(g_slow_ms);
        return VK_ERROR_UNKNOWN;
    }
    cb->submitted = 1;
    note(SUBMIT, cb, "vkQueueSubmit2", 0);
    return VK_SUCCESS;
}
VkResult vkSignalSemaphore(VkDevice dev, const VkSemaphoreSignalInfo* info) {
    (void)dev;
    note(SIGNAL, NULL, "vkSignalSemaphore", (u32)info->value);
    return VK_SUCCESS;
}
VkResult vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* info) {
    (void)queue;
    (void)info;
    return VK_SUCCESS;
}
VkResult vkAcquireNextImageKHR(VkDevice dev, VkSwapchainKHR sc, uint64_t timeout, VkSemaphore sem, VkFence fence,
                               u32* idx) {
    (void)dev, (void)sc, (void)timeout, (void)sem, (void)fence, (void)idx;
    assert(0); /* the WSI lock's acquire is not part of vk_rec_direct */
    return VK_ERROR_DEVICE_LOST;
}
VkResult vkAllocateDescriptorSets(VkDevice dev, const VkDescriptorSetAllocateInfo* info, VkDescriptorSet* sets) {
    (void)dev;
    (void)info;
    (void)sets;
    return VK_ERROR_UNKNOWN;
}
VkResult vkResetDescriptorPool(VkDevice dev, VkDescriptorPool pool, VkFlags flags) {
    (void)dev;
    (void)pool;
    (void)flags;
    return VK_SUCCESS;
}
void vkUpdateDescriptorSets(VkDevice dev, u32 n, const VkWriteDescriptorSet* writes, u32 copies, const void* none) {
    (void)dev;
    (void)n;
    (void)writes;
    (void)copies;
    (void)none;
}
void vkCmdDraw(VkCommandBuffer cb, u32 vertices, u32 instances, u32 first_vertex, u32 first_instance) {
    (void)instances;
    (void)first_vertex;
    (void)first_instance;
    note(DRAW, cb, "vkCmdDraw", vertices);
}
/* a barrier's tag: how many of each kind it has, and the sum of the marks the fixture put into them, so that
   what arrives is known to be what was queued */
void vkCmdPipelineBarrier2(VkCommandBuffer cb, const VkDependencyInfo* d) {
    u32 marks = 0;
    for (u32 i = 0; i < d->memoryBarrierCount; i++) marks += (u32)d->pMemoryBarriers[i].sType;
    for (u32 i = 0; i < d->bufferMemoryBarrierCount; i++) marks += (u32)d->pBufferMemoryBarriers[i].sType;
    for (u32 i = 0; i < d->imageMemoryBarrierCount; i++) marks += (u32)d->pImageMemoryBarriers[i].sType;
    note(COMMAND, cb, "vkCmdPipelineBarrier2",
         d->memoryBarrierCount | d->bufferMemoryBarrierCount << 4 | d->imageMemoryBarrierCount << 8 | marks << 12);
}
void fake_command(VkCommandBuffer cb, const char* name, ...) { note(COMMAND, cb, name, 0); }
void plat_exit_now(int code) { _exit(code); } /* vk_rec_end_run's, not called by a scenario */
void vk_render_forget_state(void) {
    assert(t_vkdepth > 0); /* inside the stretch, under the lock */
    note(FORGET, NULL, "vk_render_forget_state", 0);
}

/* ------------------------------------------------------------ helpers of the scenarios */
static int g_threaded;
#define CHECK(what)                                                                                                    \
    do {                                                                                                               \
        if (!(what)) {                                                                                                 \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #what);                                           \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)
static void begin(VkCommandBuffer cb) {
    memset(cb, 0, sizeof *cb);
    vk_rec_begin(cb);
}
static void submit(VkCommandBuffer cb, u64 value) {
    const VkSemaphoreSubmitInfo none = {0}, signal = {.semaphore = (VkSemaphore)0x30, .value = value};
    vk_rec_submit(cb, &none, 0, &signal, 1);
}
static void draws(VkCommandBuffer cb, u32 first, u32 n) {
    for (u32 i = 0; i < n; i++) rec_CmdDraw(cb, first + i, 1, 0, 0);
}
/* who made a queued command: the recording thread when there is one, else whoever queued it */
static int queued_by(int who) { return g_threaded ? 0 : who; }
/* the list from *at on holds these draws in this order, each made by who. *at moves past them. */
static void expect_draws(size_t* at, VkCommandBuffer cb, u32 first, u32 n, int who) {
    for (u32 i = 0; i < n; i++, (*at)++) {
        const Event* e = &g_events[*at];
        CHECK(*at < g_count && e->kind == DRAW && e->tag == first + i && e->cb == cb && e->who == who);
    }
}
static void expect(size_t* at, int kind, const char* name, int who) {
    const Event* e = &g_events[*at];
    CHECK(*at < g_count && e->kind == kind && e->who == who && (!name || !strcmp(e->name, name)));
    (*at)++;
}
static size_t find_draw(u32 tag) {
    for (size_t i = 0; i < g_count; i++)
        if (g_events[i].kind == DRAW && g_events[i].tag == tag) return i;
    return (size_t)-1;
}
static size_t count_kind(int kind) {
    size_t n = 0;
    for (size_t i = 0; i < g_count; i++) n += g_events[i].kind == kind;
    return n;
}
/* another thread can take the renderer's lock: 1. It is asked from a thread of its own, since the lock is
   reentrant on the thread that holds it. */
static void* lock_probe(void* p) {
    *(int*)p = pm_trylock(&g_vklock);
    if (*(int*)p) pm_unlock(&g_vklock);
    return NULL;
}
static int lock_is_free(void) {
    pthread_t t;
    int got = 0;
    CHECK(!pthread_create(&t, NULL, lock_probe, &got));
    pthread_join(t, NULL);
    return got;
}

/* What a scenario hands vk_rec_direct: how many commands before queues and the library makes, what the library
   answers, and the counts of the calls. */
typedef struct {
    u32 before_first, before_n, direct_first, direct_n;
    int answer, before_calls, direct_calls;
    size_t made_before; /* the list's length the library must find: everything queued so far has been made */
    int before_submits; /* a misuse: before submits the command buffer */
    int* release[2];    /* intruders let go in before and in the library, and whether they have got in */
    int* landed[2];
} Stretch;
static void wait_blocked(int* release, const int* landed) {
    if (!release) return;
    __atomic_store_n(release, 1, __ATOMIC_SEQ_CST);
    pause_ms(150); /* time enough for a thread that is not kept out to get in */
    CHECK(!__atomic_load_n(landed, __ATOMIC_SEQ_CST));
}
static void before(VkCommandBuffer cb, void* user) {
    Stretch* s = user;
    s->before_calls++;
    CHECK(t_vkdepth > 0);
    if (s->before_n) rec_CmdDraw(cb, s->before_first, 1, 0, 0);
    wait_blocked(s->release[0], s->landed[0]);
    if (s->before_n > 1) draws(cb, s->before_first + 1, s->before_n - 1);
    rec_CmdEndRendering(cb);
    if (s->before_submits) submit(cb, 99);
}
/* the library: it makes Vulkan calls at once on the command buffer it is handed, as an SDK does */
static int direct(VkCommandBuffer cb, void* user) {
    Stretch* s = user;
    s->direct_calls++;
    CHECK(t_vkdepth > 0);
    CHECK(vk_rec_behind() == 0);                      /* the ring is drained */
    CHECK(g_count == s->made_before);                 /* and all of it has been made */
    CHECK(cb->begun && !cb->ended && !cb->submitted); /* on the frame's buffer, begun and not submitted */
    CHECK(!vk_rec_submit_failed());
    if (s->direct_n) vkCmdDraw(cb, s->direct_first, 1, 0, 0);
    wait_blocked(s->release[1], s->landed[1]);
    vkCmdDispatch(cb, 1, 1, 1);
    for (u32 i = 1; i < s->direct_n; i++) vkCmdDraw(cb, s->direct_first + i, 1, 0, 0);
    return s->answer;
}

/* ------------------------------------------------------------ the scenarios */
/* The order of the commands: what was queued before the call and what before queues is made before the
   library's, the library's at once by the caller, then the state is forgotten, then what is queued after. */
static void scenario_order(void) {
    struct VkCommandBuffer_T buffer;
    VkCommandBuffer cb = &buffer;
    Stretch s = {.before_first = 100000, .before_n = 40, .direct_first = 200000, .direct_n = 3,
                 .answer = SNX_UPSCALER_OK};
    size_t at = 0;
    vk_lock();
    begin(cb);
    draws(cb, 1, 5000); /* more than a batch of the ring: part of it is published, part of it is not yet */
    s.made_before = 1 + 5000 + 40 + 1;
    CHECK(vk_rec_direct(cb, before, direct, &s) == VK_REC_DIRECT_OK);
    CHECK(s.before_calls == 1 && s.direct_calls == 1);
    draws(cb, 300000, 100);
    submit(cb, 1);
    vk_rec_sync();
    vk_unlock();
    expect(&at, BEGIN, NULL, queued_by(1));
    expect_draws(&at, cb, 1, 5000, queued_by(1));
    expect_draws(&at, cb, 100000, 40, queued_by(1));
    expect(&at, COMMAND, "vkCmdEndRendering", queued_by(1));
    expect_draws(&at, cb, 200000, 1, 1); /* the library's, made by the calling thread itself */
    expect(&at, COMMAND, "vkCmdDispatch", 1);
    expect_draws(&at, cb, 200001, 2, 1);
    expect(&at, FORGET, NULL, 1);
    expect_draws(&at, cb, 300000, 100, queued_by(1));
    expect(&at, END, NULL, queued_by(1));
    expect(&at, SUBMIT, NULL, queued_by(1));
    CHECK(at == g_count && lock_is_free());
}

/* Two other producers try to add a command during the stretch, one from the moment before has queued its first
   command, one from the moment the library has made its first. Neither gets in until the state is forgotten.
   The caller does not hold the lock itself here: the function's own hold is what keeps them out. */
static VkCommandBuffer g_intruded;
static int g_release[2], g_landed[2];
static void* intruder(void* p) {
    const int k = (int)(intptr_t)p;
    t_who = 2 + k;
    while (!__atomic_load_n(&g_release[k], __ATOMIC_SEQ_CST)) sched_yield();
    vk_lock();
    rec_CmdDraw(g_intruded, 900000 + (u32)k, 1, 0, 0);
    vk_unlock();
    __atomic_store_n(&g_landed[k], 1, __ATOMIC_SEQ_CST);
    return NULL;
}
static void scenario_intruder(void) {
    struct VkCommandBuffer_T buffer;
    VkCommandBuffer cb = &buffer;
    Stretch s = {.before_first = 100000, .before_n = 3, .direct_first = 200000, .direct_n = 2,
                 .answer = SNX_UPSCALER_OK,
                 .release = {&g_release[0], &g_release[1]}, .landed = {&g_landed[0], &g_landed[1]}};
    pthread_t threads[2];
    g_intruded = cb;
    vk_lock();
    begin(cb);
    draws(cb, 1, 10);
    vk_unlock();
    for (intptr_t k = 0; k < 2; k++) CHECK(!pthread_create(&threads[k], NULL, intruder, (void*)k));
    s.made_before = 1 + 10 + 3 + 1;
    CHECK(vk_rec_direct(cb, before, direct, &s) == VK_REC_DIRECT_OK);
    for (int k = 0; k < 2; k++) pthread_join(threads[k], NULL);
    CHECK(g_landed[0] && g_landed[1]);
    vk_lock();
    submit(cb, 1);
    vk_rec_sync();
    vk_unlock();
    /* up to the forgetting the list is exactly the stretch's, and both intruders come after it */
    size_t at = 0;
    expect(&at, BEGIN, NULL, queued_by(1));
    expect_draws(&at, cb, 1, 10, queued_by(1));
    expect_draws(&at, cb, 100000, 3, queued_by(1));
    expect(&at, COMMAND, "vkCmdEndRendering", queued_by(1));
    expect_draws(&at, cb, 200000, 1, 1);
    expect(&at, COMMAND, "vkCmdDispatch", 1);
    expect_draws(&at, cb, 200001, 1, 1);
    expect(&at, FORGET, NULL, 1);
    const size_t forgot = at - 1, first = find_draw(900000), second = find_draw(900001);
    CHECK(first != (size_t)-1 && second != (size_t)-1 && first > forgot && second > forgot);
    CHECK(g_events[first].who == queued_by(2) && g_events[second].who == queued_by(3));
    CHECK(g_count == at + 2 + 2 && g_events[g_count - 1].kind == SUBMIT && lock_is_free());
}

/* A begin that fails. The mark is set by the recording thread, late (the fake takes its time), so only a look
   at the mark after the drain can see it. The library is not called. before was, and the state is forgotten. */
static void scenario_failed_begin(void) {
    struct VkCommandBuffer_T buffer;
    VkCommandBuffer cb = &buffer;
    Stretch s = {.before_first = 100000, .before_n = 2, .direct_first = 200000, .direct_n = 2,
                 .answer = SNX_UPSCALER_OK};
    g_fail_begin = 1;
    g_slow_ms = 200;
    vk_lock();
    begin(cb);
    draws(cb, 1, 10);
    CHECK(vk_rec_direct(cb, before, direct, &s) == VK_REC_DIRECT_FAILED);
    CHECK(s.before_calls == 1 && s.direct_calls == 0 && vk_rec_submit_failed());
    CHECK(count_kind(FORGET) == 1 && g_events[g_count - 1].kind == FORGET);
    CHECK(find_draw(200000) == (size_t)-1 && count_kind(BEGIN) == 0);
    /* for good: a second call is refused the same way */
    CHECK(vk_rec_direct(cb, before, direct, &s) == VK_REC_DIRECT_FAILED && s.direct_calls == 0);
    vk_unlock();
    CHECK(lock_is_free());
}

/* An end or a submission that failed in an earlier frame: the next frame's buffer is begun without a fault, and
   the library is still not called. A refused submission is signalled by the host. */
static void scenario_failed_earlier(int submission) {
    struct VkCommandBuffer_T first, second;
    Stretch s = {.before_first = 100000, .before_n = 2, .direct_first = 200000, .direct_n = 2,
                 .answer = SNX_UPSCALER_OK};
    g_slow_ms = 200;
    vk_lock();
    begin(&first);
    draws(&first, 1, 10);
    if (submission) g_fail_submit = 1;
    else g_fail_end = 1;
    submit(&first, 7);
    begin(&second);
    draws(&second, 50, 10);
    /* nothing has waited for the recording thread: the mark may not be set yet, and the call must still see it */
    CHECK(vk_rec_direct(&second, before, direct, &s) == VK_REC_DIRECT_FAILED);
    CHECK(s.before_calls == 1 && s.direct_calls == 0 && vk_rec_submit_failed());
    CHECK(second.begun && find_draw(59) != (size_t)-1 && find_draw(100001) != (size_t)-1);
    CHECK(find_draw(200000) == (size_t)-1 && g_events[g_count - 1].kind == FORGET);
    if (submission) {
        CHECK(count_kind(SIGNAL) == 1 && count_kind(SUBMIT) == 0);
        for (size_t i = 0; i < g_count; i++)
            if (g_events[i].kind == SIGNAL) CHECK(g_events[i].tag == 7);
    }
    vk_unlock();
    CHECK(lock_is_free());
}

/* A library that answers UNKNOWN (DL2, a955a146): the call answers UNKNOWN and sets the mark, the state is
   forgotten and the lock is free, and the library's command was made at once like any other */
static void scenario_unknown(void) {
    struct VkCommandBuffer_T buffer;
    VkCommandBuffer cb = &buffer;
    Stretch s = {.direct_first = 200000, .direct_n = 1, .answer = SNX_UPSCALER_UNKNOWN};
    size_t at = 0;
    vk_lock();
    begin(cb);
    draws(cb, 1, 3);
    s.made_before = 1 + 3 + 1;
    CHECK(!vk_rec_unknown());
    CHECK(vk_rec_direct(cb, before, direct, &s) == VK_REC_DIRECT_UNKNOWN);
    CHECK(s.direct_calls == 1 && vk_rec_unknown() && !vk_rec_submit_failed());
    vk_rec_sync();
    vk_unlock();
    CHECK(lock_is_free());
    expect(&at, BEGIN, NULL, queued_by(1));
    expect_draws(&at, cb, 1, 3, queued_by(1));
    expect(&at, COMMAND, "vkCmdEndRendering", queued_by(1));
    expect_draws(&at, cb, 200000, 1, 1);
    expect(&at, COMMAND, "vkCmdDispatch", 1);
    expect(&at, FORGET, NULL, 1);
}

/* A library that fails after one command, as a prepare, and one that succeeds after it in the same frame, as an
   evaluate. The failure costs nothing but its own call: the state is forgotten, the lock is free, the ring goes
   on, the mark is not set, and the next call is made. */
static void scenario_refused(void) {
    struct VkCommandBuffer_T buffer;
    VkCommandBuffer cb = &buffer;
    Stretch prepare = {.direct_first = 200000, .direct_n = 1, .answer = SNX_UPSCALER_REFUSED};
    Stretch evaluate = {.before_first = 100000, .before_n = 2, .direct_first = 210000, .direct_n = 2,
                        .answer = SNX_UPSCALER_OK};
    size_t at = 0;
    vk_lock();
    begin(cb);
    draws(cb, 1, 10);
    prepare.made_before = 1 + 10 + 1;
    CHECK(vk_rec_direct(cb, before, direct, &prepare) == VK_REC_DIRECT_REFUSED);
    CHECK(prepare.direct_calls == 1 && !vk_rec_submit_failed());
    draws(cb, 300000, 5);
    vk_rec_sync(); /* the list is read here: nothing may be writing it */
    evaluate.made_before = g_count + 2 + 1;
    CHECK(vk_rec_direct(cb, before, direct, &evaluate) == VK_REC_DIRECT_OK);
    draws(cb, 400000, 5);
    submit(cb, 1);
    vk_rec_sync();
    vk_unlock();
    expect(&at, BEGIN, NULL, queued_by(1));
    expect_draws(&at, cb, 1, 10, queued_by(1));
    expect(&at, COMMAND, "vkCmdEndRendering", queued_by(1));
    expect_draws(&at, cb, 200000, 1, 1);
    expect(&at, COMMAND, "vkCmdDispatch", 1);
    expect(&at, FORGET, NULL, 1);
    expect_draws(&at, cb, 300000, 5, queued_by(1));
    expect_draws(&at, cb, 100000, 2, queued_by(1));
    expect(&at, COMMAND, "vkCmdEndRendering", queued_by(1));
    expect_draws(&at, cb, 210000, 1, 1);
    expect(&at, COMMAND, "vkCmdDispatch", 1);
    expect_draws(&at, cb, 210001, 1, 1);
    expect(&at, FORGET, NULL, 1);
    expect_draws(&at, cb, 400000, 5, queued_by(1));
    expect(&at, END, NULL, queued_by(1));
    expect(&at, SUBMIT, NULL, queued_by(1));
    CHECK(at == g_count && lock_is_free());
}

/* A command buffer that is not the frame's unsubmitted one: none begun yet, another one, none at all, one that
   was submitted, and one that before itself submits. Nothing is called for the first four. For the last the
   library is not called, and the state is forgotten since before ran. */
static void scenario_no_buffer(void) {
    struct VkCommandBuffer_T buffer, other;
    VkCommandBuffer cb = &buffer;
    Stretch s = {.before_first = 100000, .before_n = 2, .direct_first = 200000, .direct_n = 2,
                 .answer = SNX_UPSCALER_OK};
    memset(&other, 0, sizeof other);
    vk_rec_init();
    CHECK(vk_rec_direct(cb, before, direct, &s) == VK_REC_DIRECT_NO_BUFFER);
    vk_lock();
    begin(cb);
    CHECK(vk_rec_direct(&other, before, direct, &s) == VK_REC_DIRECT_NO_BUFFER);
    CHECK(vk_rec_direct(VK_NULL_HANDLE, before, direct, &s) == VK_REC_DIRECT_NO_BUFFER);
    submit(cb, 1);
    CHECK(vk_rec_direct(cb, before, direct, &s) == VK_REC_DIRECT_NO_BUFFER);
    vk_rec_sync();
    CHECK(s.before_calls == 0 && s.direct_calls == 0 && count_kind(FORGET) == 0);
    begin(cb);
    s.before_submits = 1;
    CHECK(vk_rec_direct(cb, before, direct, &s) == VK_REC_DIRECT_NO_BUFFER);
    CHECK(s.before_calls == 1 && s.direct_calls == 0 && count_kind(FORGET) == 1 && cb->submitted);
    CHECK(find_draw(200000) == (size_t)-1);
    /* without a before the call is the same: NULL is taken */
    begin(cb);
    vk_rec_sync(); /* the list is read here: nothing may be writing it */
    s = (Stretch){.direct_first = 200000, .direct_n = 1, .answer = SNX_UPSCALER_OK, .made_before = g_count};
    CHECK(vk_rec_direct(cb, NULL, direct, &s) == VK_REC_DIRECT_OK && s.direct_calls == 1);
    submit(cb, 3);
    vk_rec_sync();
    vk_unlock();
    CHECK(lock_is_free());
}

/* Many frames, so that the ring wraps several times with a call in each: 60 frames of 3000 queued draws, the
   library, 500 more, a submission. More than twice the ring's four megabytes. */
static void scenario_wrap(void) {
    static struct VkCommandBuffer_T buffers[60];
    size_t at = 0;
    vk_lock();
    for (u32 f = 0; f < 60; f++) {
        VkCommandBuffer cb = &buffers[f];
        Stretch s = {.before_first = 100000 + f, .before_n = 1, .direct_first = 200000 + 10 * f, .direct_n = 2,
                     .answer = SNX_UPSCALER_OK};
        begin(cb);
        draws(cb, 1, 3000);
        s.made_before = (size_t)f * 3509 + 1 + 3000 + 1 + 1;
        CHECK(vk_rec_direct(cb, before, direct, &s) == VK_REC_DIRECT_OK && s.direct_calls == 1);
        draws(cb, 5000, 500);
        submit(cb, f + 1);
    }
    vk_rec_sync();
    vk_unlock();
    for (u32 f = 0; f < 60; f++) {
        VkCommandBuffer cb = &buffers[f];
        expect(&at, BEGIN, NULL, queued_by(1));
        expect_draws(&at, cb, 1, 3000, queued_by(1));
        expect_draws(&at, cb, 100000 + f, 1, queued_by(1));
        expect(&at, COMMAND, "vkCmdEndRendering", queued_by(1));
        expect_draws(&at, cb, 200000 + 10 * f, 1, 1);
        expect(&at, COMMAND, "vkCmdDispatch", 1);
        expect_draws(&at, cb, 200001 + 10 * f, 1, 1);
        expect(&at, FORGET, NULL, 1);
        expect_draws(&at, cb, 5000, 500, queued_by(1));
        expect(&at, END, NULL, queued_by(1));
        expect(&at, SUBMIT, NULL, queued_by(1));
    }
    CHECK(at == g_count && !vk_rec_submit_failed() && lock_is_free());
}

/* Barriers with each of the eight combinations of their three arrays present or empty, an empty one NULL: the
   renderer's own are image barriers alone. Copying an empty array that is NULL is not a valid call, which the
   sanitizer of undefined behaviour reports. Each barrier is made, in order, with the counts and the contents
   it was queued with. */
static void scenario_barrier(void) {
    struct VkCommandBuffer_T buffer;
    VkCommandBuffer cb = &buffer;
    const VkMemoryBarrier2 memory[2] = {{.sType = 1}, {.sType = 2}};
    const VkBufferMemoryBarrier2 bytes[1] = {{.sType = 16}};
    const VkImageMemoryBarrier2 image[3] = {{.sType = 256}, {.sType = 512}, {.sType = 1024}};
    u32 tags[8];
    size_t at = 0;
    vk_lock();
    begin(cb);
    draws(cb, 1, 2);
    for (u32 k = 0; k < 8; k++) {
        const VkDependencyInfo d = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                    .memoryBarrierCount = k & 1 ? 2 : 0,
                                    .pMemoryBarriers = k & 1 ? memory : NULL,
                                    .bufferMemoryBarrierCount = k & 2 ? 1 : 0,
                                    .pBufferMemoryBarriers = k & 2 ? bytes : NULL,
                                    .imageMemoryBarrierCount = k & 4 ? 3 : 0,
                                    .pImageMemoryBarriers = k & 4 ? image : NULL};
        tags[k] = d.memoryBarrierCount | d.bufferMemoryBarrierCount << 4 | d.imageMemoryBarrierCount << 8 |
                  ((k & 1 ? 3u : 0u) + (k & 2 ? 16u : 0u) + (k & 4 ? 1792u : 0u)) << 12;
        rec_CmdPipelineBarrier2(cb, &d);
    }
    draws(cb, 3, 2);
    submit(cb, 1);
    vk_rec_sync();
    vk_unlock();
    expect(&at, BEGIN, NULL, queued_by(1));
    expect_draws(&at, cb, 1, 2, queued_by(1));
    for (u32 k = 0; k < 8; k++) {
        CHECK(at < g_count && g_events[at].tag == tags[k]);
        expect(&at, COMMAND, "vkCmdPipelineBarrier2", queued_by(1));
    }
    expect_draws(&at, cb, 3, 2, queued_by(1));
    expect(&at, END, NULL, queued_by(1));
    expect(&at, SUBMIT, NULL, queued_by(1));
    CHECK(at == g_count && lock_is_free());
}

int main(int argc, char** argv) {
    static const struct {
        const char* name;
        void (*run)(void);
    } plain[] = {{"order", scenario_order},
                 {"intruder", scenario_intruder},
                 {"failed-begin", scenario_failed_begin},
                 {"refused", scenario_refused},
                 {"unknown", scenario_unknown},
                 {"no-buffer", scenario_no_buffer},
                 {"wrap", scenario_wrap},
                 {"barrier", scenario_barrier}};
    if (argc != 3 || (strcmp(argv[1], "thread") && strcmp(argv[1], "once"))) {
        fprintf(stderr, "usage: fixture thread|once <scenario>\n");
        return 2;
    }
    g_threaded = !strcmp(argv[1], "thread");
    g_settings.vulkan_recording_thread = g_threaded;
    t_who = 1;
    vk_rec_init();
    CHECK(vk_rec_threaded() == g_threaded);
    if (!strcmp(argv[2], "failed-end")) scenario_failed_earlier(0);
    else if (!strcmp(argv[2], "failed-submit")) scenario_failed_earlier(1);
    else {
        size_t k = 0;
        while (k < sizeof plain / sizeof plain[0] && strcmp(argv[2], plain[k].name)) k++;
        if (k == sizeof plain / sizeof plain[0]) {
            fprintf(stderr, "no scenario \"%s\"\n", argv[2]);
            return 2;
        }
        plain[k].run();
    }
    printf("vk_record %s %s: ok (%zu calls noted)\n", argv[1], argv[2], g_count);
    return 0;
}
