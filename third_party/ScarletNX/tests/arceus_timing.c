/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The pure parts of the Arceus clock layer: the shared elapsed step of the four
   main-loop clocks, and the fractional camera stick update. */
#include "nx/runtime/arceus_step.h"
#include "nx/runtime/arceus_camera.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

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

static void test_camera(void) {
    assert(arceus_camera_fraction(.005f, 0, 0, .1f, 1, .02f, .04f, .25) == 0);
    assert(arceus_camera_fraction(.995f, 1, 1, .1f, 1, .04f, .02f, .25) == 1);
    assert(arceus_camera_fraction(.005f, 0, .8f, .1f, 0, .02f, .04f, .25) == 0);
    for (int fps = 30; fps <= 120; fps *= 2) {
        float x = 0;
        double f = 30.0 / fps;
        for (int i = 0; i < fps; i++) x = arceus_camera_fraction(x, fminf(1, x + .04f), 1, .1f, 1, .04f, .04f, f);
        assert(x == 1);
        for (int i = 0; i < fps; i++) x = arceus_camera_fraction(x, fmaxf(0, x - .04f), 0, .1f, 1, .04f, .04f, f);
        assert(x == 0);
    }
    /* A partial stick keeps the game's own result. */
    assert(fabsf(arceus_camera_fraction(.01f, .01f, .5f, .1f, 1, .02f, .04f, .25) - .01f) < 1e-7f);
}

int main(void) {
    test_step();
    test_camera();
    puts("arceus_timing: all checks passed");
    return 0;
}
