/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* What the renderer draws over the game besides the on-screen controls (vpad.h) and the menu (overlay.h): the
   Debug settings not at their default and the GDB stub, a line at the top's middle while the game runs, so a game
   slowed or stopped by one says so on its screen. The ImGui overlays (debug_overlays.cpp) draw it, and only with
   imgui at 1 */
#ifndef SNX_HUD_H
#define SNX_HUD_H
#include <stddef.h>

#define HUD_DEBUG_MAX  2048 /* the list's room, with its NUL */
#define HUD_DEBUG_LEAD "Debug on: "
/* the Debug settings in effect, key=value between ", " (options_debug), "" for none: 1 when it differs from the one
   before. On the main thread; it takes ui_lock */
int hud_debug_set(const char* text);
/* a copy of the list, "" for none, cut to size. Takes ui_lock */
void hud_debug_get(char* out, size_t size);
#endif
