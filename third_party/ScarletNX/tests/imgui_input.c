/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The event queue of the ImGui windows (gpu/vk/imgui_input.c): motion is merged, a transition is never dropped to make
   room for motion, and the order of the events holds */
#include "nx/gpu/vk/imgui_input.h"
#include <stdio.h>
#include <stdlib.h>
#include "common/filesystem/host.h"

/* the queue's lock, without the platform layer */
void pm_lock(PMutex* m) { (void)m; }
void pm_unlock(PMutex* m) { (void)m; }

enum { QUEUE_MAX = 4096 }; /* imgui_input.c's size */

static void check(int ok, const char* what) {
    if (ok) return;
    fprintf(stderr, "FAIL: %s\n", what);
    exit(1);
}

static void push(int type, float x, float y, int key) {
    ImguiEvent e = {.type = type, .x = x, .y = y, .key = key};
    imgui_input_push(&e);
}

static int drain(int* motion, int* last_type, int* last_key) {
    ImguiEvent e;
    int n = 0;
    *motion = 0;
    while (imgui_input_pop(&e)) {
        ++n;
        if (e.type == IMGUI_EV_MOUSE_POS || e.type == IMGUI_EV_WHEEL) ++*motion;
        *last_type = e.type;
        *last_key = e.key;
    }
    return n;
}

int main(void) {
    ImguiEvent e;
    int motion, type = -1, key = -1, n;

    /* positions in a row: only the last one stays. Wheel steps in a row add up */
    push(IMGUI_EV_MOUSE_POS, 1, 2, 0);
    push(IMGUI_EV_MOUSE_POS, 3, 4, 0);
    push(IMGUI_EV_WHEEL, 0, 1, 0);
    push(IMGUI_EV_WHEEL, 0, 2, 0);
    check(imgui_input_pop(&e) && e.type == IMGUI_EV_MOUSE_POS && e.x == 3 && e.y == 4, "positions merge into the last");
    check(imgui_input_pop(&e) && e.type == IMGUI_EV_WHEEL && e.y == 3, "wheel steps add up");
    check(!imgui_input_pop(&e), "the queue is empty after both");

    /* a position on each side of a key is two positions: the order holds */
    push(IMGUI_EV_MOUSE_POS, 1, 1, 0);
    push(IMGUI_EV_KEY, 0, 0, 7);
    push(IMGUI_EV_MOUSE_POS, 2, 2, 0);
    check(drain(&motion, &type, &key) == 3 && motion == 2 && type == IMGUI_EV_MOUSE_POS, "a key splits two positions");

    /* a full queue gives its place to a transition by removing motion */
    push(IMGUI_EV_MOUSE_POS, 5, 5, 0);
    for (int i = 1; i < QUEUE_MAX; ++i) push(IMGUI_EV_KEY, 0, 0, i);
    push(IMGUI_EV_KEY, 0, 0, 99999); /* the key-up that must arrive */
    n = drain(&motion, &type, &key);
    check(n == QUEUE_MAX && motion == 0 && key == 99999, "a full queue removes the motion and keeps the transition");

    /* a queue of transitions only: the one event past its end is the only one lost */
    for (int i = 0; i < QUEUE_MAX + 5; ++i) push(IMGUI_EV_KEY, 0, 0, i);
    n = drain(&motion, &type, &key);
    check(n == QUEUE_MAX && key == QUEUE_MAX - 1, "a queue of transitions keeps the oldest and drops the newest past its end");

    puts("imgui_input: ok");
    return 0;
}
