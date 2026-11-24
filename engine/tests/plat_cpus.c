/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* The host CPUs of plat_posix.c on Linux: plat_cpus lists the process's own, plat_thread_cpus keeps the calling thread
   on the ones given, and a thread plat_thread starts runs on all of them again, not only on the share of the game
   thread that started it (thread.c core_pin) */
#include "nx/runtime/platform/plat_posix.c"
#include <stdarg.h>

void log_crash_mode(void) {}
void pm_wait_note(const void* m, const void* at, int cond) {
    (void)m;
    (void)at;
    (void)cond;
}
const PmWaitHook* g_pm_hook; /* host.c's */
void crash_report(void* host_pc, void* fault, int sig) {
    (void)host_pc;
    (void)fault;
    (void)sig;
}
void fatal(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    vfprintf(stderr, fmt, a);
    va_end(a);
    abort();
}
void logf_(int lvl, const char* fmt, ...) {
    va_list a;
    (void)lvl;
    va_start(a, fmt);
    vfprintf(stderr, fmt, a);
    va_end(a);
}

static unsigned checks, failures;
#define CHECK(expr, ...)                                                                                               \
    do {                                                                                                               \
        checks++;                                                                                                      \
        if (!(expr)) {                                                                                                 \
            failures++;                                                                                                \
            fprintf(stderr, "plat_cpus.c:%d: %s: ", __LINE__, #expr);                                                  \
            fprintf(stderr, __VA_ARGS__);                                                                              \
            fputc('\n', stderr);                                                                                       \
        }                                                                                                              \
    } while (0)

/* the CPUs the calling thread may run on, as plat_cpus lists them */
static int mine(int* ids, int max) {
    cpu_set_t s;
    int c, n = 0;
    if (sched_getaffinity(0, sizeof s, &s)) return -1;
    for (c = 0; c < CPU_SETSIZE && n < max; c++)
        if (CPU_ISSET(c, &s)) ids[n++] = c;
    return n;
}
static int g_all[CPU_SETSIZE], g_nall;
static int g_child[CPU_SETSIZE], g_nchild = -1;
static pthread_mutex_t g_lk = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_done = PTHREAD_COND_INITIALIZER;
static void child(void* p) {
    (void)p;
    pthread_mutex_lock(&g_lk);
    g_nchild = mine(g_child, CPU_SETSIZE);
    pthread_cond_broadcast(&g_done);
    pthread_mutex_unlock(&g_lk);
}
/* restricted to the first CPU, as a game thread to its core's share, it starts a thread with plat_thread */
static void parent(void* p) {
    int one = g_all[0], got[CPU_SETSIZE], n;
    (void)p;
    plat_thread_cpus(&one, 1);
    n = mine(got, CPU_SETSIZE);
    CHECK(n == 1 && got[0] == one, "plat_thread_cpus(%d): the thread runs on %d CPUs, the first %d", one, n, got[0]);
    if (plat_thread(child, NULL, 0)) fatal("plat_thread failed\n");
}

/* PERF-32's wait hook: with one set, a yield, a signal, a broadcast and a timed wait each tell it once, with their
   caller, and with none set they tell nothing */
static int noted[PMW_N];
static const void* noted_at;
static uint64_t hook_now(void) { return 1; }
static void hook_note(int kind, const void* at, uint64_t ns) {
    if (kind >= 0 && kind < PMW_N && ns == 0) noted[kind]++;
    noted_at = at;
}
static void test_hook(void) {
    static const PmWaitHook hook = {hook_now, hook_note};
    PCond c;
    PMutex m = {.l = PTHREAD_MUTEX_INITIALIZER};
    pc_init(&c);
    plat_yield();
    pc_signal(&c); /* no hook yet: nothing noted */
    CHECK(!noted[PMW_YIELD] && !noted[PMW_WAKE], "the hook unset noted %d yields, %d wakes", noted[PMW_YIELD],
          noted[PMW_WAKE]);
    g_pm_hook = &hook;
    plat_yield();
    CHECK(noted[PMW_YIELD] == 1 && noted_at, "a yield noted %d times", noted[PMW_YIELD]);
    pc_signal(&c);
    pc_broadcast(&c);
    CHECK(noted[PMW_WAKE] == 2, "a signal and a broadcast noted %d wakes", noted[PMW_WAKE]);
    pthread_mutex_lock(&m.l);
    pc_wait(&c, &m, 1000000);
    pthread_mutex_unlock(&m.l);
    CHECK(noted[PMW_COND] == 1 && !noted[PMW_LOCK] && !noted[PMW_SLEEP], "a timed wait noted %d", noted[PMW_COND]);
    g_pm_hook = NULL;
}

int main(void) {
    int ids[CPU_SETSIZE], n;
    test_hook();
    g_nall = mine(g_all, CPU_SETSIZE);
    n = plat_cpus(ids, CPU_SETSIZE);
    CHECK(n == g_nall && !memcmp(ids, g_all, sizeof *ids * (size_t)(n > 0 ? n : 0)),
          "plat_cpus: %d CPUs, the thread %d", n, g_nall);
    CHECK(plat_cpus(ids, 1) == 1 && ids[0] == g_all[0], "plat_cpus with room for 1: CPU %d", ids[0]);
    if (g_nall < 2) {
        printf("1 host CPU: nothing to restrict, %u checks, %u failed\n", checks, failures);
        return failures != 0;
    }
    if (plat_thread(parent, NULL, 0)) fatal("plat_thread failed\n");
    pthread_mutex_lock(&g_lk);
    while (g_nchild < 0) pthread_cond_wait(&g_done, &g_lk);
    pthread_mutex_unlock(&g_lk);
    CHECK(g_nchild == g_nall && !memcmp(g_child, g_all, sizeof *g_all * (size_t)g_nall),
          "the thread a restricted thread started runs on %d CPUs, not the process's %d", g_nchild, g_nall);
    printf("%u checks, %u failed\n", checks, failures);
    return failures != 0;
}
