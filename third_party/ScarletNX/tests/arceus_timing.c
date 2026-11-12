/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The pure parts of the Arceus clock layer: the shared elapsed step of the four
   main-loop clocks, and the fractional camera stick update, at fixed, unlocked
   and jittering rates. */
#include "nx/runtime/arceus_step.h"
#include "nx/runtime/arceus_camera.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static void batch(ArceusStep* state, uint64_t now, uint64_t expected) {
    uint64_t result;
    for (unsigned channel = 0; channel < 4; channel++) {
        assert(arceus_step(state, now + channel * 100, 123, channel, &result));
        assert(result == expected);
    }
}

static void test_step(void) {
    ArceusStep state = {0};
    uint64_t result = 0, now = 1000000000, total = 0;
    assert(!arceus_step(&state, now, 123, 1, &result));
    batch(&state, now, ARCEUS_INITIAL_STEP_NS);
    for (unsigned i = 0; i < 120; i++) {
        uint64_t elapsed = i % 2 ? 25000000 : 16666667;
        now += elapsed;
        batch(&state, now, elapsed);
        total += state.step;
    }
    assert(total == now - 1000000000);
    batch(&state, now + 1000000000, ARCEUS_MAX_STEP_NS);
    assert(!arceus_step(&state, now, 999, 1, &result));
    assert(!arceus_step(&state, now, 123, 4, &result));
    assert(arceus_step(&state, now, 999, 0, &result));
    assert(result == ARCEUS_INITIAL_STEP_NS);
    /* A clock that does not move gives the initial step, never zero. */
    assert(arceus_step(&state, now, 999, 0, &result));
    assert(result == ARCEUS_INITIAL_STEP_NS);
    state = (ArceusStep){.initial_ns = 8333333};
    now = 1000000000;
    total = 0;
    batch(&state, now, 8333333);
    for (unsigned i = 0; i < 240; i++) {
        uint64_t elapsed = i % 3 == 0 ? 16666667 : (i % 3 == 1 ? 8333333 : 8333334);
        now += elapsed;
        batch(&state, now, elapsed);
        total += state.step;
    }
    assert(total == now - 1000000000);
    assert(arceus_step(&state, now, 999, 0, &result));
    assert(result == 8333333);
}

/* Unlocked: no rate to start from, steps above 120 FPS and jitter. The steps always add
   up to the elapsed time, except what a stall above the cap drops, and are never zero. */
static void test_unlocked(void) {
    static const uint64_t rates[] = {144, 240, 360, 500, 1000};
    uint64_t result;
    for (unsigned r = 0; r < sizeof rates / sizeof rates[0]; r++) {
        ArceusStep state = {.initial_ns = ARCEUS_UNLOCKED_INITIAL_STEP_NS};
        uint64_t now = 1000000000, total = 0;
        batch(&state, now, ARCEUS_UNLOCKED_INITIAL_STEP_NS);
        for (unsigned i = 0; i < rates[r] * 2; i++) {
            const uint64_t elapsed = (1000000000 + rates[r] / 2) / rates[r] + (i % 2 ? 1 : 0);
            now += elapsed;
            batch(&state, now, elapsed);
            total += state.step;
        }
        assert(total == now - 1000000000);
    }
    ArceusStep state = {.initial_ns = ARCEUS_UNLOCKED_INITIAL_STEP_NS};
    uint64_t now = 1000000000, total = 0, dropped = 0;
    batch(&state, now, ARCEUS_UNLOCKED_INITIAL_STEP_NS);
    srand(11);
    for (unsigned i = 0; i < 20000; i++) {
        /* 0.25 ms to 40 ms, with a 2 s stall now and then. */
        uint64_t elapsed = 250000 + (uint64_t)(rand() % 39750) * 1000;
        if (i % 5000 == 4999) elapsed = 2000000000;
        now += elapsed;
        assert(arceus_step(&state, now, 123, 0, &result));
        assert(result > 0 && result <= ARCEUS_MAX_STEP_NS);
        for (unsigned channel = 1; channel < 4; channel++) {
            uint64_t same;
            assert(arceus_step(&state, now + channel, 123, channel, &same) && same == result);
        }
        total += result;
        if (elapsed > ARCEUS_MAX_STEP_NS) dropped += elapsed - ARCEUS_MAX_STEP_NS;
    }
    assert(total + dropped == now - 1000000000);
    /* A repeated timestamp gives the initial step, not zero. */
    assert(arceus_step(&state, now, 123, 0, &result) && result == ARCEUS_UNLOCKED_INITIAL_STEP_NS);
    /* The clamped ratio stays above zero far beyond unlocked rates. */
    assert(ARCEUS_MIN_STEP_RATIO > 0.0f && ARCEUS_MIN_STEP_RATIO * 33333334.0 < 1000000000.0 / 20000);
}

static void test_camera(void) {
    assert(arceus_camera_fraction(.005f, 0, 0, .1f, 1, .02f, .04f, .25) == 0);
    assert(arceus_camera_fraction(.995f, 1, 1, .1f, 1, .04f, .02f, .25) == 1);
    assert(arceus_camera_fraction(.005f, 0, .8f, .1f, 0, .02f, .04f, .25) == 0);
    static const int rates[] = {30, 60, 120, 144, 240, 360, 1000};
    for (unsigned r = 0; r < sizeof rates / sizeof rates[0]; r++) {
        const int fps = rates[r];
        float x = 0;
        double f = 30.0 / fps;
        for (int i = 0; i < fps; i++) x = arceus_camera_fraction(x, fminf(1, x + .04f), 1, .1f, 1, .04f, .04f, f);
        assert(x == 1);
        for (int i = 0; i < fps; i++) x = arceus_camera_fraction(x, fmaxf(0, x - .04f), 0, .1f, 1, .04f, .04f, f);
        assert(x == 0);
    }
    /* Jittering fractions: full stick for 1 s still saturates, and releasing for 1 s returns to zero. */
    srand(5);
    float x = 0;
    for (double t = 0; t < 1.0;) {
        const double f = (rand() % 1000 + 1) / 1000.0 * .99;
        x = arceus_camera_fraction(x, fminf(1, x + .04f), 1, .1f, 1, .04f, .04f, f);
        t += f / 30.0;
    }
    assert(x == 1);
    for (double t = 0; t < 1.0;) {
        const double f = (rand() % 1000 + 1) / 1000.0 * .99;
        x = arceus_camera_fraction(x, fmaxf(0, x - .04f), 0, .1f, 1, .04f, .04f, f);
        t += f / 30.0;
    }
    assert(x == 0);
    /* A partial stick keeps the game's own result. */
    assert(fabsf(arceus_camera_fraction(.01f, .01f, .5f, .1f, 1, .02f, .04f, .25) - .01f) < 1e-7f);
}

int main(void) {
    test_step();
    test_unlocked();
    test_camera();
    puts("arceus_timing: all checks passed");
    return 0;
}
