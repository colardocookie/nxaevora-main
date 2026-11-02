/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The input of the ImGui windows. The main thread polls the window's events (runtime/platform/backend/imgui_sdl.c)
   and pushes them here. The ImGui context belongs to whoever holds vk_lock, which pops them at the start of its frame.
   The other way, the frame publishes what the windows want, and the main thread reads it with no lock. The queue has
   its own mutex, which is a leaf: nothing else is taken while it is held */
#ifndef SNX_IMGUI_INPUT_H
#define SNX_IMGUI_INPUT_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { IMGUI_EV_MOUSE_POS, IMGUI_EV_MOUSE_BUTTON, IMGUI_EV_WHEEL, IMGUI_EV_KEY, IMGUI_EV_TEXT, IMGUI_EV_FOCUS };
enum { IMGUI_TEXT_MAX = 32 };
/* an SDL button number (1 left, 2 middle, 3 right) in button; a key as the SDL keycode and the SDL modifier mask */
typedef struct {
    int type;
    float x, y; /* a position in pixels of the picture, or the wheel's steps. -1e9 for no mouse */
    int button, down, key, mods;
    char text[IMGUI_TEXT_MAX];
} ImguiEvent;

void imgui_input_push(const ImguiEvent* e); /* any thread; motion is merged and goes first when full, the rest stays */
int imgui_input_pop(ImguiEvent* e);         /* the context's owner: 1 with the oldest event, 0 when empty */

/* what the windows want, set by the frame and read by the main thread */
typedef struct {
    int mouse;    /* the cursor is over a window, or a widget holds the mouse: the game does not get it */
    int keyboard; /* a widget has the keyboard: the game does not get keys */
    int text;     /* a text field is active: the window's text input is on */
    int release;  /* the game's mouse look lets go of the cursor */
    int redraw;   /* the last frame drew something: it must be drawn again while the game presents nothing */
    /* where an input method puts its window: the text cursor, in pixels of the picture, and its line's height */
    int ime_x, ime_y, ime_h;
} ImguiWants;
void imgui_input_publish(const ImguiWants* w);
/* the frame runs (the backend is up and the setting is on), set by the frame's owner. The main thread asks whether the
   game's last picture must be shown again for the windows: they have something drawn, or an event waits for the next
   frame. This does not depend on a recent frame, so it also starts the windows again after a stall */
void imgui_input_running_set(int running);
int imgui_input_refresh_wanted(void);
/* the windows own what the player needs (the keyboard's box, the menu): the backend is up, the setting is on, and it
   has not failed. A part that would otherwise draw it leaves it to them while this is 1. It follows the backend's
   life, not the age of the last frame, so a stall in the game's presents does not hand it back */
int imgui_input_running(void);
ImguiWants imgui_input_wants(void);

#ifdef __cplusplus
}
#endif
#endif
