// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// The engine menu's settings pages read these rules (menu_settings.h). A row of
// the settings model picks its widget from the model alone, so the pages show
// every row the model has, and the number field parses on its own.

#include "nx/gpu/vk/menu_settings.h"

#include <climits>
#include <cstring>

namespace {

constexpr int kSliderMaxSteps = 64;

// The main section of the Performance page, in the order of the page.
const char* const kPerformanceMainKeys[MENU_PERFORMANCE_MAIN_COUNT] = {
    "fps_limit", "memory_gb", "pipeline_threads", "async_pipelines", "prepare_shaders",
};

bool KeyIs(const char* key, const char* wanted) {
    return key != nullptr && std::strcmp(key, wanted) == 0;
}

bool IsMainKey(const char* key) {
    for (int i = 0; i < MENU_PERFORMANCE_MAIN_COUNT; ++i)
        if (KeyIs(key, kPerformanceMainKeys[i])) return true;
    return false;
}

} // namespace

extern "C" int menu_settings_widget(const ImguiSettingRow* row) {
    if (row == nullptr) return MENU_WIDGET_UNSUPPORTED;
    if (row->presets > 0) return MENU_WIDGET_COMBO;
    if (row->has_words && (row->type == IMGUI_SETTING_INT || row->type == IMGUI_SETTING_TEXT))
        return MENU_WIDGET_COMBO;
    if (row->type == IMGUI_SETTING_INT && row->lo == 0.0 && row->hi == 1.0) return MENU_WIDGET_SWITCH;
    if (row->type == IMGUI_SETTING_FLOAT) return MENU_WIDGET_UNSUPPORTED;
    if (row->type == IMGUI_SETTING_INT) {
        // A step of 0 or less has no slider: the number field takes the value.
        if (row->lo <= row->hi && row->step > 0) {
            const double steps = (row->hi - row->lo) / static_cast<double>(row->step);
            if (steps <= static_cast<double>(kSliderMaxSteps)) return MENU_WIDGET_SLIDER;
        }
        return MENU_WIDGET_NUMBER;
    }
    if (row->type == IMGUI_SETTING_TEXT) return MENU_WIDGET_TEXT_FIELD;
    return MENU_WIDGET_UNSUPPORTED;
}

extern "C" void menu_performance_groups(const ImguiSettingRow* rows, int count,
                                        int main[MENU_PERFORMANCE_MAIN_COUNT], int* main_count, int* advanced,
                                        int* advanced_count) {
    int main_n = 0;
    int advanced_n = 0;
    if (rows != nullptr && count > 0) {
        for (int k = 0; k < MENU_PERFORMANCE_MAIN_COUNT; ++k) {
            for (int i = 0; i < count; ++i) {
                if (!KeyIs(rows[i].key, kPerformanceMainKeys[k])) continue;
                if (main != nullptr) main[main_n] = i;
                ++main_n;
                break;
            }
        }
        for (int i = 0; i < count; ++i) {
            if (IsMainKey(rows[i].key)) continue;
            if (advanced != nullptr) advanced[advanced_n] = i;
            ++advanced_n;
        }
    }
    if (main_count != nullptr) *main_count = main_n;
    if (advanced_count != nullptr) *advanced_count = advanced_n;
}

extern "C" int menu_parse_number(const char* text, int lo, int hi, int* out) {
    if (text == nullptr || out == nullptr || lo > hi) return 0;
    int at = 0;
    bool negative = false;
    if (text[0] == '-') {
        negative = true;
        at = 1;
    }
    if (text[at] == '\0') return 0;
    long long value = 0;
    for (; text[at] != '\0'; ++at) {
        if (text[at] < '0' || text[at] > '9') return 0;
        value = value * 10 + (text[at] - '0');
        if (value > static_cast<long long>(INT_MAX) + 1) return 0;
    }
    if (negative) value = -value;
    if (value < static_cast<long long>(lo) || value > static_cast<long long>(hi)) return 0;
    *out = static_cast<int>(value);
    return 1;
}
