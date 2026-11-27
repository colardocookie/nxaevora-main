// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// The engine menu's settings pages' pure rules (src/nx/gpu/vk/menu_settings.cpp):
// the widget of a row, the two groups of the Performance page, and the number
// that a number field commits. Every expected value comes from the decisions:
// presets before words before the 0/1 switch before the slider before the
// number field, 64 slider steps and not 65, the five main keys in their order
// and the other rows in model order, and a whole number in range alone.

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "nx/gpu/vk/menu_settings.h"

namespace {

void Check(bool ok, const char* what) {
    if (ok) return;
    std::fprintf(stderr, "FAIL: %s\n", what);
    std::exit(1);
}

ImguiSettingRow Row(int type, double lo, double hi, int step, int has_words, int presets) {
    ImguiSettingRow row{};
    row.key = "key";
    row.name = "Key";
    row.help = "";
    row.type = type;
    row.lo = lo;
    row.hi = hi;
    row.step = step;
    row.has_words = has_words;
    row.presets = presets;
    return row;
}

void CheckWidget(const ImguiSettingRow& row, int expected, const char* what) {
    Check(menu_settings_widget(&row) == expected, what);
}

void CheckNumber(const char* text, int lo, int hi, bool ok, int expected, const char* what) {
    int value = -12345;
    const int result = menu_parse_number(text, lo, hi, &value);
    Check((result != 0) == ok, what);
    if (ok) Check(value == expected, what);
}

} // namespace

int main() {
    // Decision 5: presets first, then words, then the 0/1 switch, then the slider, then the number field.
    CheckWidget(Row(IMGUI_SETTING_INT, 10, 1000, 1, 0, 7), MENU_WIDGET_COMBO, "a row with presets is a combo");
    CheckWidget(Row(IMGUI_SETTING_INT, 0, 3, 1, 1, 0), MENU_WIDGET_COMBO, "an int with words is a combo");
    CheckWidget(Row(IMGUI_SETTING_TEXT, 0, 0, 1, 1, 0), MENU_WIDGET_COMBO, "a text with words is a combo");
    CheckWidget(Row(IMGUI_SETTING_INT, 0, 1, 1, 0, 0), MENU_WIDGET_SWITCH, "an int from 0 to 1 is a switch");
    CheckWidget(Row(IMGUI_SETTING_INT, 0, 10, 1, 0, 0), MENU_WIDGET_SLIDER, "a short int range is a slider");
    CheckWidget(Row(IMGUI_SETTING_INT, 0, 100, 1, 0, 0), MENU_WIDGET_NUMBER, "a long int range is a number field");
    CheckWidget(Row(IMGUI_SETTING_FLOAT, 0, 1, 1, 0, 0), MENU_WIDGET_UNSUPPORTED, "a float has no widget");
    CheckWidget(Row(IMGUI_SETTING_TEXT, 0, 0, 1, 0, 0), MENU_WIDGET_TEXT_ONLY, "a text with no words is text only");
    Check(menu_settings_widget(nullptr) == MENU_WIDGET_UNSUPPORTED, "a null row has no widget");

    // The edge of the slider: 64 steps is a slider, 65 is a number field. A step of 0 does not divide.
    CheckWidget(Row(IMGUI_SETTING_INT, 0, 640, 10, 0, 0), MENU_WIDGET_SLIDER, "64 steps is a slider");
    CheckWidget(Row(IMGUI_SETTING_INT, 0, 650, 10, 0, 0), MENU_WIDGET_NUMBER, "65 steps is a number field");
    CheckWidget(Row(IMGUI_SETTING_INT, 0, 10, 0, 0, 0), MENU_WIDGET_NUMBER, "a step of 0 is a number field");

    // Decision 4: the five main keys in the page's order, then the other rows in model order.
    {
        const char* keys[7] = {"prepare_shaders", "other_a", "async_pipelines", "pipeline_threads",
                               "other_b",         "fps_limit", "memory_gb"};
        ImguiSettingRow rows[7]{};
        for (int i = 0; i < 7; ++i) rows[i].key = keys[i];
        int main[MENU_PERFORMANCE_MAIN_COUNT] = {-1, -1, -1, -1, -1};
        int advanced[7] = {-1, -1, -1, -1, -1, -1, -1};
        int main_count = 0;
        int advanced_count = 0;
        menu_performance_groups(rows, 7, main, &main_count, advanced, &advanced_count);
        Check(main_count == 5, "the five main keys are found");
        Check(main[0] == 5 && main[1] == 6 && main[2] == 3 && main[3] == 2 && main[4] == 0,
              "the main keys keep the order of the page");
        Check(advanced_count == 2 && advanced[0] == 1 && advanced[1] == 4,
              "the other rows keep the model order");
    }

    // A main key that the model does not have is skipped.
    {
        const char* keys[5] = {"memory_gb", "other_a", "async_pipelines", "pipeline_threads", "fps_limit"};
        ImguiSettingRow rows[5]{};
        for (int i = 0; i < 5; ++i) rows[i].key = keys[i];
        int main[MENU_PERFORMANCE_MAIN_COUNT] = {-1, -1, -1, -1, -1};
        int advanced[5] = {-1, -1, -1, -1, -1};
        int main_count = 0;
        int advanced_count = 0;
        menu_performance_groups(rows, 5, main, &main_count, advanced, &advanced_count);
        Check(main_count == 4, "a missing main key is skipped");
        Check(main[0] == 4 && main[1] == 0 && main[2] == 3 && main[3] == 2, "the rest keep the page's order");
        Check(advanced_count == 1 && advanced[0] == 1, "the other rows keep the model order");
    }

    {
        int main_count = -1;
        int advanced_count = -1;
        menu_performance_groups(nullptr, 0, nullptr, &main_count, nullptr, &advanced_count);
        Check(main_count == 0 && advanced_count == 0, "no rows give no group");
    }

    // Decision 7: a whole number in lo to hi, and an int overflow is a failure.
    CheckNumber("0", 0, 1000, true, 0, "0");
    CheckNumber("10", 10, 1000, true, 10, "lo");
    CheckNumber("1000", 10, 1000, true, 1000, "hi");
    CheckNumber("9", 10, 1000, false, 0, "lo - 1");
    CheckNumber("1001", 10, 1000, false, 0, "hi + 1");
    CheckNumber("", 10, 1000, false, 0, "an empty text");
    CheckNumber("12a", 10, 1000, false, 0, "a letter");
    CheckNumber("-5", 0, 1000, false, 0, "a negative number");
    CheckNumber("99999999999", 0, 1000, false, 0, "an int overflow");

    std::puts("menu_settings: ok");
    return 0;
}
