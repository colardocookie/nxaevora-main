/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The window's input events (imgui_input.h) into the current ImGui context. Under vk_lock, before NewFrame */
#ifndef SNX_IMGUI_EVENTS_H
#define SNX_IMGUI_EVENTS_H

/* the context's platform hooks (the input method's place) */
void imgui_events_init(void);
/* every queued event into the context's input */
void imgui_events_feed(void);
/* what the windows want, from the frame just begun, for the main thread (imgui_input_publish). menu_visible: the menu
   bar shows, which also lets go of the cursor */
void imgui_events_publish(bool menu_visible);

#endif
