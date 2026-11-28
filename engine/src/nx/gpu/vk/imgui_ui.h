/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The windows of the game: a menu bar that shows when the cursor is at the top of the picture, the game's keyboard box,
   the engine menu's panel and the debug overlays. Called between NewFrame and Render */
#ifndef SNX_IMGUI_UI_H
#define SNX_IMGUI_UI_H
#include "nx/gpu/vk/debug_overlays.h"
#include "nx/gpu/vk/imgui_glue.h"

/* What one frame drew: whether a menu or the panel shows (the input gate) and the draw lists of the overlay windows,
   which the recorder renders before the rest. */
struct ImguiUiDraw {
    bool menu_shown;
    debug_overlay::OverlayDrawLists overlays;
};

/* Draws one frame's windows and returns what it drew. */
ImguiUiDraw imgui_ui_draw(ImguiFrame* f);
/* the windows start again from a closed state: after the context is made again or the windows were off */
void imgui_ui_reset(void);

#endif
