/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The overlap check's record hand-over (gpu/nvn/diag/overlap_check_calc.c): each order of the post's publish and the
   end's claim leaves the record with one owner and free in the end, including the post finishing between the end's
   look and its claim (reviewer 11:55, N1). The compare-exchange is wrapped to run the other side at that point. */
#include <stdio.h>
static int run_post_at, cas_calls;
static int* post_state;
static int publish_result = -1;
static int test_cas(int* p, int* expected, int value);
#define OVERLAP_CAS(p, e, v) test_cas(p, e, v)
#include "nx/gpu/nvn/diag/overlap_check_calc.c"

static int test_cas(int* p, int* expected, int value) {
    /* the other thread's publish, just before this compare-exchange of the end */
    ++cas_calls;
    if (run_post_at && cas_calls == run_post_at && p == post_state && value != OVERLAP_HASHED) {
        run_post_at = 0;
        publish_result = overlap_post_publish(post_state);
    }
    return __atomic_compare_exchange_n(p, expected, value, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

static int failures;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                     \
            failures++;                                                                                                \
        }                                                                                                              \
    } while (0)

int main(void) {
    int state = OVERLAP_COLLECTED;
    /* the post first: the end claims it to compare, then frees it */
    CHECK(overlap_post_publish(&state) == 1 && state == OVERLAP_HASHED);
    CHECK(overlap_end_claim(&state) == OVERLAP_ENDING && state == OVERLAP_ENDING);
    /* the end first: it skips, and the post's publish frees it */
    state = OVERLAP_COLLECTED;
    CHECK(overlap_end_claim(&state) == OVERLAP_SKIPPED && state == OVERLAP_SKIPPED);
    CHECK(overlap_post_publish(&state) == 0 && state == OVERLAP_FREE);
    /* the post between the end's look (COLLECTED) and its claim: the claim fails, the end looks again and compares */
    state = OVERLAP_COLLECTED;
    post_state = &state;
    run_post_at = 1;
    cas_calls = 0;
    const int claim = overlap_end_claim(&state);
    CHECK(publish_result == 1);
    CHECK(claim == OVERLAP_ENDING && state == OVERLAP_ENDING); /* not stranded HASHED */
    CHECK(cas_calls >= 2);
    /* a record that is not the end's: returned as it is, untouched */
    state = OVERLAP_FREE;
    CHECK(overlap_end_claim(&state) == OVERLAP_FREE && state == OVERLAP_FREE);
    state = OVERLAP_ENDING;
    CHECK(overlap_end_claim(&state) == OVERLAP_ENDING && state == OVERLAP_ENDING);
    /* four records in a row with the post landing in the gap: none is left behind */
    int ring[4];
    for (int i = 0; i < 4; i++) {
        ring[i] = OVERLAP_COLLECTED;
        post_state = &ring[i];
        run_post_at = 1;
        cas_calls = 0;
        CHECK(overlap_end_claim(&ring[i]) == OVERLAP_ENDING);
        ring[i] = OVERLAP_FREE; /* as overlap_check_end frees it after its compare */
    }
    for (int i = 0; i < 4; i++) CHECK(ring[i] == OVERLAP_FREE);
    if (failures) return 1;
    printf("overlap_handoff: ok\n");
    return 0;
}
