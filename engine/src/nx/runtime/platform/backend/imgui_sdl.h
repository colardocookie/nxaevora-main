/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The window's events for the ImGui windows (gpu/vk/imgui_input.h). Main thread only */
#ifndef SNX_IMGUI_SDL_H
#define SNX_IMGUI_SDL_H
#include <SDL3/SDL.h>

/* An event, given to the windows. 1 when they take it, so the game and the old menu do not get it. density is the
   pixels of the picture for each point of the window, captured whether the mouse look holds the cursor (the windows
   then see no mouse). owned is 0 while the old menu or the loading screen has the controls: the windows then get only
   releases and a lost focus, and take nothing. A key-up is never taken: a key the game saw go down must come up */
int imgui_sdl_event(const SDL_Event* e, float density, int captured, int owned);
/* turns the window's text input on while a text field is active, unless the game's keyboard box has it (keyboard_box) */
void imgui_sdl_text_input(SDL_Window* win, int keyboard_box, float density);
#endif
