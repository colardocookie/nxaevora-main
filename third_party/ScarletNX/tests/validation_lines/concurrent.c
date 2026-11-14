/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The game's validation lines under callbacks from several threads at once (validation_lines.h): every callback
   counted once, the inherited counter with them, each rule and each name first printed once, every line whole.
   One callback arrives before the status lines, and the status lines are written while the threads run. */
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include "nx/gpu/vk/validation_lines.h"

#define THREADS 8
#define EACH 6000
#define RULES 4
#define VARIABLES 100 /* more than the table holds */

static ValidationLines g_state;
static int g_lock; /* beside the state, never in it */
static uint32_t g_inherited;
/* written by emit only, which runs under the lock */
static char* g_lines;
static size_t g_lines_at, g_lines_room;
static unsigned g_firsts, g_fresh, g_counts, g_limits, g_status;

static void emit(void* user, const char* line) {
    const size_t n = strlen(line);
    (void)user;
    assert(n <= VALIDATION_LINE && g_lines_at + n + 2 < g_lines_room);
    for (size_t i = 0; i < n; i++) assert((unsigned char)line[i] >= 0x20 && (unsigned char)line[i] < 0x7f);
    memcpy(g_lines + g_lines_at, line, n);
    g_lines_at += n;
    g_lines[g_lines_at++] = '\n';
    g_firsts += strstr(line, "the first of its rule") != NULL;
    g_fresh += strstr(line, "the first naming a variable's name not seen before") != NULL;
    g_counts += strstr(line, " callbacks so far, ") != NULL;
    g_limits += !strncmp(line, "LIMIT MET", 9);
    g_status += strstr(line, "the layer VK_LAYER_KHRONOS_validation is enabled") != NULL ||
                strstr(line, "its synchronisation checks were asked for") != NULL ||
                strstr(line, "the message callback is ready") != NULL;
}

static void* callbacks(void* arg) {
    const int thread = (int)(size_t)arg;
    char name[32], text[96];
    for (int i = 0; i < EACH; i++) {
        snprintf(name, sizeof name, "VUID-%d", (thread + i) % RULES);
        snprintf(text, sizeof text, "thread %d: variable \"v%d\" has a format", thread, (thread * 7 + i) % VARIABLES);
        validation_lines_said(&g_lock, &g_state, &g_inherited, name, (thread + i) % RULES, text, emit, NULL);
    }
    return NULL;
}

int main(void) {
    pthread_t threads[THREADS];
    const uint64_t total = (uint64_t)THREADS * EACH + 1;
    g_lines_room = 64u << 20;
    g_lines = malloc(g_lines_room);
    assert(g_lines);
    /* a callback before the status lines, as one that arrives while the instance is made */
    validation_lines_said(&g_lock, &g_state, &g_inherited, "VUID-early", 99, "no name in this one", emit, NULL);
    for (size_t t = 0; t < THREADS; t++) assert(!pthread_create(&threads[t], NULL, callbacks, (void*)t));
    /* the status lines go out under the same lock, in the middle of the callbacks */
    validation_lines_lock(&g_lock);
    validation_lines_status(1, 1, "", 0, "", 0, 1, "", 0, emit, NULL);
    validation_lines_unlock(&g_lock);
    for (size_t t = 0; t < THREADS; t++) assert(!pthread_join(threads[t], NULL));
    validation_lines_lock(&g_lock);
    assert(g_state.callbacks == total && g_inherited == total);
    uint64_t sum = g_state.unlisted, powers = 0;
    for (unsigned k = 0; k < g_state.rules; k++) sum += g_state.rule[k].count;
    assert(sum == total && g_state.rules == RULES + 1 && !g_state.unlisted);
    /* each rule printed as the first of its rule once, each name that was listed as a new name once at most
       per callback, and the table full with the limit said once */
    assert(g_firsts == RULES + 1 && g_state.names == VALIDATION_NAMES);
    assert(g_fresh == VALIDATION_NAMES && g_state.loss[VALIDATION_NAMES_FULL] > 0);
    assert(g_limits == 1 && g_status == 3);
    for (uint64_t p = 1; p <= total; p *= 2) powers++;
    /* a callback can be the first of its rule and name a new name at once: which ones depends on the order */
    assert(g_counts == powers && g_state.printed >= VALIDATION_NAMES + 1);
    assert(g_state.printed <= VALIDATION_NAMES + RULES + 1);
    validation_lines_unlock(&g_lock);
    printf("validation_lines concurrent: %llu callbacks from %d threads, %u count lines, %zu bytes of lines\n",
           (unsigned long long)total, THREADS, g_counts, g_lines_at);
    free(g_lines);
    return 0;
}
