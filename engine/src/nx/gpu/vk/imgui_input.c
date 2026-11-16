/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/gpu/vk/imgui_input.h"
#include <string.h>
#include "common/filesystem/host.h"

enum { QUEUE_MAX = 4096 }; /* the frame pops them: a stalled frame (a loading interval) keeps what the player pressed */
static ImguiEvent g_queue[QUEUE_MAX];
static int g_head, g_count;
static PMutex g_lock = PMUTEX_INIT;
static int g_wants_mouse, g_wants_keyboard, g_wants_text, g_wants_release, g_wants_redraw, g_ime_x, g_ime_y, g_ime_h;
static int g_running;

static ImguiEvent* at(int i) { return &g_queue[(g_head + i) % QUEUE_MAX]; }

/* a position or a wheel step can be merged or lost, a button, a key, a text or a focus change cannot */
static int is_motion(const ImguiEvent* e) { return e->type == IMGUI_EV_MOUSE_POS || e->type == IMGUI_EV_WHEEL; }

/* makes room by removing the oldest motion event: 1 when there is room now */
static int evict_motion(void) {
    for (int i = 0; i < g_count; ++i) {
        if (!is_motion(at(i))) continue;
        for (int j = i; j + 1 < g_count; ++j) *at(j) = *at(j + 1);
        --g_count;
        return 1;
    }
    return 0;
}

void imgui_input_push(const ImguiEvent* e) {
    pm_lock(&g_lock);
    ImguiEvent* tail = g_count ? at(g_count - 1) : NULL;
    if (tail && e->type == IMGUI_EV_MOUSE_POS && tail->type == IMGUI_EV_MOUSE_POS) {
        *tail = *e; /* only the last place matters */
    } else if (tail && e->type == IMGUI_EV_WHEEL && tail->type == IMGUI_EV_WHEEL) {
        tail->x += e->x;
        tail->y += e->y;
    } else if (g_count < QUEUE_MAX || evict_motion()) {
        *at(g_count) = *e;
        ++g_count;
    } /* else the queue holds only transitions: the frame has not run for thousands of them */
    pm_unlock(&g_lock);
}

int imgui_input_pop(ImguiEvent* e) {
    int have = 0;
    pm_lock(&g_lock);
    if (g_count) {
        *e = g_queue[g_head];
        g_head = (g_head + 1) % QUEUE_MAX;
        --g_count;
        have = 1;
    }
    pm_unlock(&g_lock);
    return have;
}

void imgui_input_publish(const ImguiWants* w) {
    __atomic_store_n(&g_wants_mouse, w->mouse, __ATOMIC_RELAXED);
    __atomic_store_n(&g_wants_keyboard, w->keyboard, __ATOMIC_RELAXED);
    __atomic_store_n(&g_wants_text, w->text, __ATOMIC_RELAXED);
    __atomic_store_n(&g_wants_release, w->release, __ATOMIC_RELAXED);
    __atomic_store_n(&g_wants_redraw, w->redraw, __ATOMIC_RELAXED);
    __atomic_store_n(&g_ime_x, w->ime_x, __ATOMIC_RELAXED);
    __atomic_store_n(&g_ime_y, w->ime_y, __ATOMIC_RELAXED);
    __atomic_store_n(&g_ime_h, w->ime_h, __ATOMIC_RELAXED);
}

ImguiWants imgui_input_wants(void) {
    ImguiWants w = {__atomic_load_n(&g_wants_mouse, __ATOMIC_RELAXED), __atomic_load_n(&g_wants_keyboard, __ATOMIC_RELAXED),
                    __atomic_load_n(&g_wants_text, __ATOMIC_RELAXED), __atomic_load_n(&g_wants_release, __ATOMIC_RELAXED),
                    __atomic_load_n(&g_wants_redraw, __ATOMIC_RELAXED),
                    __atomic_load_n(&g_ime_x, __ATOMIC_RELAXED), __atomic_load_n(&g_ime_y, __ATOMIC_RELAXED),
                    __atomic_load_n(&g_ime_h, __ATOMIC_RELAXED)};
    return w;
}

void imgui_input_running_set(int running) { __atomic_store_n(&g_running, running, __ATOMIC_RELAXED); }

int imgui_input_running(void) { return __atomic_load_n(&g_running, __ATOMIC_RELAXED); }

int imgui_input_refresh_wanted(void) {
    int waiting;
    if (!__atomic_load_n(&g_running, __ATOMIC_RELAXED)) return 0;
    if (__atomic_load_n(&g_wants_redraw, __ATOMIC_RELAXED)) return 1;
    pm_lock(&g_lock);
    waiting = g_count > 0;
    pm_unlock(&g_lock);
    return waiting;
}

void imgui_input_push_text(const char* text) {
    size_t at = 0, n = strlen(text);
    while (at < n) {
        ImguiEvent ev = {.type = IMGUI_EV_TEXT};
        size_t take = n - at < IMGUI_TEXT_MAX - 1 ? n - at : IMGUI_TEXT_MAX - 1;
        if (at + take < n) /* back to the start of the character that the piece would cut */
            while (take && ((unsigned char)text[at + take] & 0xC0) == 0x80) --take;
        if (!take) break;
        memcpy(ev.text, text + at, take);
        imgui_input_push(&ev);
        at += take;
    }
}
