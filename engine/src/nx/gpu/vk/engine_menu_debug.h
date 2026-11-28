// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// The Debug page of the engine menu: the table of its settings rows and the
// cards that draw them. The table has no ImGui, so the pure test reads it.

#pragma once

#include <stddef.h>

#include "nx/gpu/vk/imgui_settings.h"

#if defined(__cplusplus) && !defined(ENGINE_MENU_PURE_TEST)
#include "nx/gpu/vk/imgui_glue.h" // ImguiFrame; the pure test leaves it out
#endif

// The cards of the Debug page, in the order of the page. Hardware, Frame and
// Display have no settings row: they show the frame's values only.
enum {
    ENGINE_MENU_DEBUG_OVERLAYS,
    ENGINE_MENU_DEBUG_HARDWARE,
    ENGINE_MENU_DEBUG_FRAME,
    ENGINE_MENU_DEBUG_DISPLAY,
    ENGINE_MENU_DEBUG_LOGS,
    ENGINE_MENU_DEBUG_TEXTURES,
    ENGINE_MENU_DEBUG_RECOMPILER,
    ENGINE_MENU_DEBUG_VULKAN,
    ENGINE_MENU_DEBUG_NVN,
    ENGINE_MENU_DEBUG_MACROS,
    ENGINE_MENU_DEBUG_PROFILERS,
    ENGINE_MENU_DEBUG_CARD_COUNT
};

// One settings row of the Debug page: the settings key, its card and the label
// that the page shows in place of the settings file's name. build_optional marks
// a key that a build can leave out, for example the Tracy keys of a build
// without SNX_TRACY; the page skips such a row and logs nothing.
typedef struct EngineMenuDebugEntry {
    const char* key;
    int card;
    const char* label;
    bool build_optional = false;
} EngineMenuDebugEntry;

// The page entries in page order, and their count.
const EngineMenuDebugEntry* EngineMenuDebugEntries(int* count);

// The entry of a settings key, or NULL when the key is not on the page.
const EngineMenuDebugEntry* EngineMenuDebugEntryOfKey(const char* key);

// The first entry of a card and its count; a card with no settings row gives
// NULL and a count of 0.
const EngineMenuDebugEntry* EngineMenuDebugCardEntries(int card, int* count);

// The title of a card, "" for a card outside the list.
const char* EngineMenuDebugCardName(int card);

#if defined(__cplusplus) && !defined(ENGINE_MENU_PURE_TEST)
// The content of the Debug page. engine_menu.cpp owns the page window and its
// scroll; this function draws the cards only.
void EngineMenuDrawDebugPage(const ImguiFrame* frame);

// The settings row helper of engine_menu.cpp that the page needs.
void EngineMenuDrawDebugSettingRow(const ImguiSettingRow& row);
#endif
