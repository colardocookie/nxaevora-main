/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* SNX_ARCEUS_PERF_SMALL bit 1 (PERF-34, gpu/vk/release.c): the release thread is woken only while it sleeps. A model
   of release.c's protocol with real threads: the release thread's wait (release_thread, "while (g_rtail == g_rheld)"
   with g_rsleep set and cleared around pc_wait) and its wakers (release, vk_release_submit, release_match: g_rheld
   moved and release_wake under g_rlock). Producers race the waiter, and a lost wake would leave it asleep past the
   alarm. Run with and without the bit, under the thread sanitizer too. Usage: release_wake 0|1 */
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define PRODUCERS 3
#define EACH 200000u

static pthread_mutex_t g_rlock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_rcv = PTHREAD_COND_INITIALIZER;
static unsigned long g_rtail, g_rheld, g_rhead;
static int g_bit, g_rsleep;
static unsigned long broadcasts, waits;

static void release_wake(void) { /* release.c's, the bit as g_perf_small & PSM_WAKE */
    if (!g_bit || g_rsleep) {
        broadcasts++;
        pthread_cond_broadcast(&g_rcv);
    }
}
static void* producer(void* arg) {
    for (unsigned i = 0; i < EACH; i++) {
        pthread_mutex_lock(&g_rlock);
        g_rhead++;
        if (i % 3 != 1 || arg) g_rheld = g_rhead; /* most releases are let go at once, some by a later one */
        release_wake();
        pthread_mutex_unlock(&g_rlock);
        if (!(i % 64)) sched_yield();
    }
    return NULL;
}
static void* finisher(void* arg) { /* vk_release_submit at the end: whatever is left goes */
    (void)arg;
    pthread_mutex_lock(&g_rlock);
    g_rheld = g_rhead;
    release_wake();
    pthread_mutex_unlock(&g_rlock);
    return NULL;
}
static void on_alarm(int sig) {
    (void)sig;
    static const char msg[] = "release_wake: FAILED, the waiter slept through a release (lost wake)\n";
    if (write(2, msg, sizeof msg - 1) < 0) _exit(1);
    _exit(1);
}

int main(int argc, char** argv) {
    g_bit = argc > 1 && argv[1][0] == '1';
    signal(SIGALRM, on_alarm);
    alarm(60);
    pthread_t p[PRODUCERS], f;
    static int let_go_all = 1; /* the first producer lets every one of its releases go */
    for (int k = 0; k < PRODUCERS; k++) pthread_create(&p[k], NULL, producer, k == 0 ? &let_go_all : NULL);
    unsigned long done = 0;
    const unsigned long total = (unsigned long)PRODUCERS * EACH;
    int finished = 0;
    while (done < total) { /* the release thread: take what is let go, perform it, wait for more */
        pthread_mutex_lock(&g_rlock);
        while (g_rtail == g_rheld) {
            if (g_bit) g_rsleep = 1;
            waits++;
            pthread_cond_wait(&g_rcv, &g_rlock);
            if (g_bit) g_rsleep = 0;
        }
        done += g_rheld - g_rtail;
        g_rtail = g_rheld;
        pthread_mutex_unlock(&g_rlock);
        if (!finished && done >= total - EACH) { /* the producers are near their end: join them, then let go */
            for (int k = 0; k < PRODUCERS; k++) pthread_join(p[k], NULL);
            pthread_create(&f, NULL, finisher, NULL);
            pthread_join(f, NULL);
            finished = 1;
        }
    }
    if (done != total) {
        printf("release_wake: FAILED, %lu of %lu performed\n", done, total);
        return 1;
    }
    printf("release_wake: ok, bit %d, %lu releases, %lu waits, %lu broadcasts\n", g_bit, done, waits, broadcasts);
    return 0;
}
