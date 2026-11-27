/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The engine menu shell (docs/dev/ui/engine-menu.md): the open state, the rule that picks the menu of a run, the
   gamepad and finger mapping and the MD3 panel. The state is one atomic: the main thread opens and closes it, the
   frame thread closes it for the ImGui close gestures and at the end of the fade, and the GPU thread reads it. */
#ifndef SNX_ENGINE_MENU_H
#define SNX_ENGINE_MENU_H
#include <stdint.h>

#if defined(__cplusplus) && !defined(ENGINE_MENU_PURE_TEST)
#include "nx/gpu/vk/imgui_glue.h" /* ImguiFrame; the pure test leaves it out */
#endif
#ifdef __cplusplus
extern "C" {
#endif

/* Which menu a run uses: none when the engine menu is chosen but its windows did not start. */
enum { ENGINE_MENU_LEGACY, ENGINE_MENU_ENGINE, ENGINE_MENU_NONE };

/* The menu of a run from its settings and whether the ImGui windows run: the legacy menu without the windows
   (imgui=0) or when legacy_menu is 1, the engine menu with imgui=1, legacy_menu=0 and running=1, and no menu with
   imgui=1, legacy_menu=0 and running=0. Pure. */
int engine_menu_choice(int imgui, int legacy_menu, int running);

/* One gamepad control to push to the ImGui queue. key is an ImGuiKey value. */
enum { ENGINE_MENU_PAD_BUTTON, ENGINE_MENU_PAD_AXIS };
typedef struct {
    int key;     /* an ImGuiKey value, -1 for none */
    int down;    /* 1 down, 0 up */
    float value; /* the analog value of a stick or a trigger, 0 for a button */
} EngineMenuPadEvent;

/* The ImGui events of one SDL gamepad control. kind is ENGINE_MENU_PAD_BUTTON (control is an SDL_GamepadButton, value
   its down flag) or ENGINE_MENU_PAD_AXIS (control is an SDL_GamepadAxis, value its reading). A stick axis makes two
   events, toward the positive and the negative side, so the other side is let go. Returns the count, 0 when the control
   is not mapped. Pure. */
int engine_menu_pad_events(int kind, int control, int value, EngineMenuPadEvent out[2]);

/* A finger's phase for the engine menu's pointer. */
enum { ENGINE_MENU_FINGER_DOWN, ENGINE_MENU_FINGER_MOTION, ENGINE_MENU_FINGER_UP, ENGINE_MENU_FINGER_CANCELED };

/* The pointer action of one finger event, and its place in the window's pixels. */
enum { ENGINE_MENU_POINTER_IGNORED, ENGINE_MENU_POINTER_MOVE, ENGINE_MENU_POINTER_PRESS, ENGINE_MENU_POINTER_RELEASE };
typedef struct {
    int action;
    float x, y;
} EngineMenuPointer;

/* One finger as the engine menu's pointer: fx and fy are 0..1 of the window, window_w and window_h its pixels. Only
   the first finger down drives the pointer, until it is up; the other fingers are ignored. *first holds that finger's
   id (0 when none), which the function keeps. Pure. */
EngineMenuPointer engine_menu_pointer(int phase, uint64_t id, float fx, float fy, int window_w, int window_h,
                                      uint64_t* first);

/* The main thread: opens, closes or reopens the engine menu. */
void engine_menu_toggle(void);
/* The main thread: 1 while the menu is open or closing, for the input gate and the refresh. */
int engine_menu_active(void);
/* The GPU thread: the panel alpha of the last frame, for the offscreen composite. */
float engine_menu_panel_alpha(void);
/* The ImGui context is made again: the menu is closed. */
void engine_menu_reset(void);
/* The main thread: one SDL gamepad control into the ImGui queue while the menu is active. */
void engine_menu_gamepad_button(int sdl_button, int down);
void engine_menu_gamepad_axis(int sdl_axis, int value);

#ifdef __cplusplus
}

#if !defined(ENGINE_MENU_PURE_TEST)
/* The GPU thread: draws the menu for the frame and returns whether the panel showed. */
bool engine_menu_draw(ImguiFrame* f);
#endif
/* The GPU thread: 1 while the menu is open or closing, before NewFrame. */
bool engine_menu_visible(void);
#endif
#endif
