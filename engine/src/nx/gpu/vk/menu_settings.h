// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// The rules of the engine menu's settings pages: which MD3 widget draws a row of
// the settings model (imgui_settings.h), the two groups of the Performance page,
// and the number that a number field commits. No ImGui, so a test links this
// file alone.

#pragma once

#include "nx/gpu/vk/imgui_settings.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    MENU_WIDGET_COMBO = 0,  /* a choice of words or presets */
    MENU_WIDGET_NUMBER,     /* a number field */
    MENU_WIDGET_SWITCH,     /* an int from 0 to 1 */
    MENU_WIDGET_SLIDER,     /* an int whose range is at most 64 steps */
    MENU_WIDGET_TEXT_ONLY,  /* a text with no words: the supporting line alone */
    MENU_WIDGET_UNSUPPORTED /* a FLOAT row: no widget of these pages draws it */
};

/* The widget of a row, from its type, flags, range, step, words and presets. A NULL row has no widget. */
int menu_settings_widget(const ImguiSettingRow* row);

/* The five main keys of the Performance page, in the order of the page. */
enum { MENU_PERFORMANCE_MAIN_COUNT = 5 };

/* main gets the indices of the five main keys in their order, a missing key skipped, and advanced the indices of the
   other rows in their model order. Both give their count. rows may be NULL only with count <= 0. */
void menu_performance_groups(const ImguiSettingRow* rows, int count, int main[MENU_PERFORMANCE_MAIN_COUNT],
                             int* main_count, int* advanced, int* advanced_count);

/* 1 and the number in out when text is a whole number in lo to hi, else 0. An int overflow is a failure. */
int menu_parse_number(const char* text, int lo, int hi, int* out);

#ifdef __cplusplus
}
#endif
