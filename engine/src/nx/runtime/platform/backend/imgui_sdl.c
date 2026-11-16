/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/runtime/platform/backend/imgui_sdl.h"
#include <string.h>
#include "nx/gpu/vk/imgui_input.h"

enum { NO_MOUSE = -1000000000 };
static int g_grabbed; /* the buttons whose press the windows took: bit n for SDL button n */
static int g_was_captured = 1;
static int g_text_on; /* this file started the window's text input */
static SDL_Rect g_area;

static void push_pos(float x, float y) {
    ImguiEvent ev = {.type = IMGUI_EV_MOUSE_POS, .x = x, .y = y};
    imgui_input_push(&ev);
}

/* the windows see no mouse while the mouse look holds the cursor, and the cursor's place once it is let go, if the
   cursor is over the window: else SDL has no place for it, and its last one would show the menu bar */
static void follow_capture(int captured, float density) {
    float x, y;
    if (captured == g_was_captured) return;
    g_was_captured = captured;
    if (captured || !SDL_GetMouseFocus()) {
        push_pos(NO_MOUSE, NO_MOUSE);
        return;
    }
    SDL_GetMouseState(&x, &y);
    push_pos(x * density, y * density);
}

static int button_event(const SDL_Event* e, ImguiWants w) {
    int bit = 1 << e->button.button, down = e->type == SDL_EVENT_MOUSE_BUTTON_DOWN, taken;
    ImguiEvent ev = {.type = IMGUI_EV_MOUSE_BUTTON, .button = e->button.button, .down = down};
    if (e->button.which == SDL_TOUCH_MOUSEID) return 0;
    imgui_input_push(&ev);
    if (down) {
        taken = w.mouse;
        if (taken) g_grabbed |= bit;
        return taken;
    }
    taken = (g_grabbed & bit) != 0; /* the release of a press that the windows took */
    g_grabbed &= ~bit;
    return taken;
}

static void focus_lost(void) {
    ImguiEvent ev = {.type = IMGUI_EV_FOCUS};
    g_grabbed = 0;
    imgui_input_push(&ev);
}

static int mouse_event(const SDL_Event* e, float density, ImguiWants w) {
    ImguiEvent ev = {.type = IMGUI_EV_WHEEL};
    switch (e->type) {
        case SDL_EVENT_MOUSE_MOTION:
            if (e->motion.which == SDL_TOUCH_MOUSEID) return 0;
            push_pos(e->motion.x * density, e->motion.y * density);
            return w.mouse;
        case SDL_EVENT_MOUSE_WHEEL:
            ev.x = e->wheel.x;
            ev.y = e->wheel.y;
            imgui_input_push(&ev);
            return w.mouse;
        default: return button_event(e, w);
    }
}

static int key_event(const SDL_Event* e, ImguiWants w) {
    int down = e->type == SDL_EVENT_KEY_DOWN;
    ImguiEvent ev = {.type = IMGUI_EV_KEY, .key = (int)e->key.key, .mods = (int)e->key.mod, .down = down};
    imgui_input_push(&ev);
    return down && w.keyboard;
}

/* the old menu or the loading screen has the controls: the windows get the ends of what they began (a release, a lost
   focus) and nothing new */
static void pass_ends(const SDL_Event* e) {
    ImguiWants none = {0};
    switch (e->type) {
        case SDL_EVENT_KEY_UP: key_event(e, none); break;
        case SDL_EVENT_MOUSE_BUTTON_UP: button_event(e, none); break;
        case SDL_EVENT_WINDOW_FOCUS_LOST: focus_lost(); break;
    }
}

int imgui_sdl_event(const SDL_Event* e, float density, int captured, int owned) {
    ImguiWants w = imgui_input_wants();
    follow_capture(captured, density);
    if (!owned) {
        pass_ends(e);
        return 0;
    }
    switch (e->type) {
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_WHEEL: return captured ? 0 : mouse_event(e, density, w);
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: return key_event(e, w);
        case SDL_EVENT_TEXT_INPUT:
            if (!w.text) return 0;
            imgui_input_push_text(e->text.text);
            return 1;
        case SDL_EVENT_WINDOW_MOUSE_LEAVE: /* no place: a bar at the top edge goes when the cursor leaves there */
            push_pos(NO_MOUSE, NO_MOUSE);
            return 0;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            focus_lost();
            return 0;
    }
    return 0;
}

void imgui_sdl_text_input(SDL_Window* win, int keyboard_box, float density) {
    ImguiWants w = imgui_input_wants();
    int want = w.text && !keyboard_box;
    SDL_Rect area;
    if (keyboard_box) { /* the box's own text input is on: not this file's to stop */
        g_text_on = 0;
        return;
    }
    if (want && density > 0) { /* where the input method puts its window: the field's cursor, in points */
        area = (SDL_Rect){(int)(w.ime_x / density), (int)(w.ime_y / density), 1, (int)(w.ime_h / density)};
        if (memcmp(&area, &g_area, sizeof area)) {
            SDL_SetTextInputArea(win, &area, 0);
            g_area = area;
        }
    }
    /* asked at each poll, not on a change: the box may have stopped the input that this file needs */
    if (want && !SDL_TextInputActive(win))
        SDL_StartTextInput(win);
    else if (!want && g_text_on && SDL_TextInputActive(win))
        SDL_StopTextInput(win);
    g_text_on = want;
}
