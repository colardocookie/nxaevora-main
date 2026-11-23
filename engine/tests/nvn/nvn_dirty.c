/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* PERF-2's counted dirty walks (gpu/nvn/nvn_dirty.h) against the full walks the replay ran before: random binds and
   draws on twin tables, the slots each draw takes in the same order, the flags left the same, and every count equal
   to a recount of its flags after each step. */
#include "nx/gpu/nvn/nvn_dirty.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

/* the replay's tables: five graphics stages and compute, as nvn_native.h sizes them */
#define STAGES 6
#define SLOTS 128
static const uint32_t sizes[4] = {32, 128, 128, 16}; /* uniforms, storages, textures, vertex buffers */
typedef struct {
    uint8_t flags[4][STAGES][SLOTS];
    uint16_t count[4][STAGES];
} Tables;

static uint32_t rnd_state = 99;
static uint32_t rnd(void) {
    rnd_state = rnd_state * 1664525u + 1013904223u;
    return rnd_state >> 8;
}
static int recount_ok(const Tables* t) {
    for (int g = 0; g < 4; g++)
        for (int s = 0; s < STAGES; s++) {
            uint16_t n = 0;
            for (uint32_t i = 0; i < sizes[g]; i++) n += t->flags[g][s][i] != 0;
            if (n != t->count[g][s]) return 0;
        }
    return 1;
}
/* one draw's walk of every graphics stage (vertex buffers: stage 0 only), into the list of taken slots */
static uint32_t walk(Tables* t, int counted, uint32_t* taken) {
    uint32_t n = 0;
    for (int g = 0; g < 4; g++)
        for (int s = 0; s < (g == 3 ? 1 : STAGES - 1); s++)
            for (uint32_t i = 0; nvn_dirty_take(t->flags[g][s], &t->count[g][s], sizes[g], &i, counted); i++)
                taken[n++] = (uint32_t)(g << 16 | s << 8) | i;
    return n;
}

static void test_random(void) {
    static Tables a, b;
    static uint32_t ta[4 * STAGES * SLOTS], tb[4 * STAGES * SLOTS];
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    unsigned draws = 0, taken_total = 0;
    for (unsigned step = 0; step < 200000; step++) {
        const uint32_t r = rnd() % 100;
        if (r < 80) { /* a bind, often the same slot again */
            const int g = (int)(rnd() % 4), s = g == 3 ? 0 : (int)(rnd() % STAGES);
            const uint32_t i = rnd() % 7 ? rnd() % 8 : rnd() % sizes[g];
            nvn_dirty_mark(&a.flags[g][s][i], &a.count[g][s]);
            nvn_dirty_mark(&b.flags[g][s][i], &b.count[g][s]);
        } else if (r < 98) { /* a draw */
            const uint32_t na = walk(&a, 0, ta), nb = walk(&b, 1, tb);
            CHECK(na == nb && !memcmp(ta, tb, na * sizeof *ta));
            draws++;
            taken_total += na;
        } else { /* a dispatch clears the compute stage */
            for (int g = 0; g < 3; g++) {
                nvn_dirty_clear(a.flags[g][STAGES - 1], &a.count[g][STAGES - 1], sizes[g]);
                nvn_dirty_clear(b.flags[g][STAGES - 1], &b.count[g][STAGES - 1], sizes[g]);
            }
        }
        CHECK(!memcmp(&a, &b, sizeof a));
        CHECK(recount_ok(&a));
        if (failures > 10) return;
    }
    CHECK(draws > 30000 && taken_total > 100000); /* the sequence did exercise the walks */
}

static void test_edges(void) {
    uint8_t flags[SLOTS] = {0};
    uint16_t count = 0;
    uint32_t i = 0;
    CHECK(!nvn_dirty_take(flags, &count, SLOTS, &i, 1));
    CHECK(!nvn_dirty_take(flags, &count, SLOTS, &i, 0));
    nvn_dirty_mark(&flags[SLOTS - 1], &count);
    nvn_dirty_mark(&flags[SLOTS - 1], &count); /* again: counted once */
    CHECK(count == 1);
    i = 0;
    CHECK(nvn_dirty_take(flags, &count, SLOTS, &i, 1) && i == SLOTS - 1 && count == 0 && !flags[SLOTS - 1]);
    i++;
    CHECK(!nvn_dirty_take(flags, &count, SLOTS, &i, 1));
    nvn_dirty_mark(&flags[0], &count);
    nvn_dirty_mark(&flags[5], &count);
    i = 1; /* a walk that starts past a set flag leaves it */
    CHECK(nvn_dirty_take(flags, &count, SLOTS, &i, 1) && i == 5 && count == 1 && flags[0]);
    nvn_dirty_clear(flags, &count, SLOTS);
    CHECK(count == 0 && !flags[0]);
    nvn_dirty_mark(&flags[0], &count);
    i = 0; /* a walk from 0 takes slot 0 */
    CHECK(nvn_dirty_take(flags, &count, SLOTS, &i, 1) && i == 0 && count == 0);
    flags[3] = NVN_DIRTY_SET; /* set behind the count's back: only the full walk, which reads no count, finds it */
    i = 0;
    CHECK(!nvn_dirty_take(flags, &count, SLOTS, &i, 1) && flags[3]);
    i = 0;
    CHECK(nvn_dirty_take(flags, &count, SLOTS, &i, 0) && i == 3 && !flags[3]);
}

int main(void) {
    test_edges();
    test_random();
    if (failures) return 1;
    printf("nvn_dirty: ok\n");
    return 0;
}
