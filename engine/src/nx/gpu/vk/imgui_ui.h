/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The windows of the game: a menu bar that shows when the cursor is at the top of the picture, and what its menus
   open. Called between NewFrame and Render */
#ifndef SNX_IMGUI_UI_H
#define SNX_IMGUI_UI_H
#include "nx/gpu/vk/imgui_glue.h"

/* Returns whether the menu bar shows this frame. f->open_test is the test window's first state, from the .ini */
bool imgui_ui_draw(ImguiFrame* f);
/* the windows start again from a closed state: after the context is made again or the windows were off */
void imgui_ui_reset(void);

#endif
