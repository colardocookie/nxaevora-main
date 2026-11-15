/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* common/filesystem/host.c's shared mode of PMutex: a shared take and an exclusive take exclude each other, and on
   Windows two shared holders run at the same time */
#include "common/filesystem/host.h"
#include <stdio.h>
#include <stdlib.h>
#if defined(_WIN32)
#include <pthread.h>
#include <unistd.h>
#endif

static unsigned test_checks;
#define CHECK(expr)                                                                                                    \
    do {                                                                                                               \
        test_checks++;                                                                                                 \
        if (!(expr)) {                                                                                                 \
            fprintf(stderr, "host_shared_lock.c:%d: check failed: %s\n", __LINE__, #expr);                              \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)

static PMutex test_lock = PMUTEX_INIT; /* usable before any init runs */

#if defined(_WIN32)
#define WAIT_LIMIT_MS 5000 /* a lock fault must fail the test, not hang it */
#define POLL_US       1000 /* one flag poll */

/* the reader threads report by these flags, and main checks every wait against WAIT_LIMIT_MS */
static int a_in, a_release, a_out;
static int b_in, b_release, b_out;
static int b_took; /* reader B's shared take, read by main after b_in */

/* 1 when the flag is set, 0 when WAIT_LIMIT_MS passes without it */
static int wait_flag(const int* flag) {
    int polls = WAIT_LIMIT_MS * 1000 / POLL_US;
    int i;
    for (i = 0; i < polls; i++) {
        if (__atomic_load_n(flag, __ATOMIC_ACQUIRE)) return 1;
        usleep(POLL_US);
    }
    return __atomic_load_n(flag, __ATOMIC_ACQUIRE);
}

static void set_flag(int* flag) { __atomic_store_n(flag, 1, __ATOMIC_RELEASE); }

static void* reader_a(void* unused) {
    (void)unused;
    pm_lock_shared(&test_lock);
    set_flag(&a_in);
    wait_flag(&a_release); /* a timeout still unlocks, as the test exits on the failed check */
    pm_unlock_shared(&test_lock);
    set_flag(&a_out);
    return NULL;
}

static void* reader_b(void* unused) {
    (void)unused;
    b_took = pm_trylock_shared(&test_lock);
    set_flag(&b_in);
    wait_flag(&b_release);
    if (b_took) pm_unlock_shared(&test_lock); /* a failed take holds nothing to release */
    set_flag(&b_out);
    return NULL;
}
#endif

int main(void) {
    /* the shared release frees the mutex for an exclusive take */
    pm_lock_shared(&test_lock);
    pm_unlock_shared(&test_lock);
    CHECK(pm_trylock(&test_lock));
    pm_unlock(&test_lock);
    /* an exclusive holder keeps the shared take out */
    pm_lock(&test_lock);
    CHECK(!pm_trylock_shared(&test_lock));
    pm_unlock(&test_lock);
    CHECK(pm_trylock_shared(&test_lock));
    pm_unlock_shared(&test_lock);
#if defined(_WIN32)
    /* two shared holders at the same time, and the exclusive take waits for both */
    pthread_t a, b;
    CHECK(!pthread_create(&a, NULL, reader_a, NULL));
    CHECK(wait_flag(&a_in));
    CHECK(!pthread_create(&b, NULL, reader_b, NULL));
    CHECK(wait_flag(&b_in));
    CHECK(b_took == 1);
    CHECK(!pm_trylock(&test_lock)); /* both shared holders are in */
    set_flag(&a_release);
    CHECK(wait_flag(&a_out));
    CHECK(!pm_trylock(&test_lock)); /* the second shared holder is still in */
    set_flag(&b_release);
    CHECK(wait_flag(&b_out));
    CHECK(pm_trylock(&test_lock));
    pm_unlock(&test_lock);
    CHECK(!pthread_join(a, NULL));
    CHECK(!pthread_join(b, NULL));
#else
    /* the documented fallback: a shared take is a normal take, so a second one fails */
    pm_lock_shared(&test_lock);
    CHECK(!pm_trylock_shared(&test_lock));
    pm_unlock_shared(&test_lock);
#endif
    printf("host_shared_lock: %u checks passed\n", test_checks);
    return 0;
}
