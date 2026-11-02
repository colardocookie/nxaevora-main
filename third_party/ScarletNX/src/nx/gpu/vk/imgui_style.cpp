/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The theme of the ImGui windows (imgui_style.h) */
#include "nx/gpu/vk/imgui_style.h"
#include "imgui.h"

namespace {
constexpr ImVec4 ACCENT = {0x46 / 255.0f, 0x47 / 255.0f, 0x8B / 255.0f, 1.0f};
constexpr float WINDOW_ALPHA = 0.94f; /* the whole window: its background, text and widgets */
constexpr float WINDOW_ROUNDING = 8.0f, WIDGET_ROUNDING = 5.0f, POPUP_ROUNDING = 6.0f, SCROLLBAR_ROUNDING = 9.0f;
/* the blue of ImGui's dark theme has this blue channel at its brightest, and the others are shades of it */
constexpr float DEFAULT_BLUE_PEAK = 0.98f;
constexpr float PRESSED_LIGHTEN = 0.25f; /* a pressed widget is its accent moved this far toward white */

const ImGuiCol PRESSED[] = {ImGuiCol_FrameBgActive,   ImGuiCol_SliderGrabActive, ImGuiCol_ButtonActive,
                            ImGuiCol_HeaderActive,    ImGuiCol_SeparatorActive,  ImGuiCol_ResizeGripActive};

/* a colour of ImGui's dark theme is its blue when the blue channel is clearly the strongest: at least this bright, and
   this far over the red and over the green. The greys and the other hues of the theme fail one of the three */
constexpr float BLUE_MIN = 0.4f, BLUE_OVER_RED = 0.2f, BLUE_OVER_GREEN = 0.15f;

bool is_default_blue(const ImVec4& c) { return c.z > BLUE_MIN && c.z > c.x + BLUE_OVER_RED && c.z > c.y + BLUE_OVER_GREEN; }

/* the accent at the brightness that the default blue c has, with c's alpha */
ImVec4 accent_like(const ImVec4& c) {
    float k = c.z / DEFAULT_BLUE_PEAK;
    return ImVec4(ACCENT.x * k, ACCENT.y * k, ACCENT.z * k, c.w);
}

ImVec4 lighten(const ImVec4& c, float amount) {
    return ImVec4(c.x + (1.0f - c.x) * amount, c.y + (1.0f - c.y) * amount, c.z + (1.0f - c.z) * amount, c.w);
}
} // namespace

void imgui_style_apply(void) {
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark(&style);
    for (int i = 0; i < ImGuiCol_COUNT; ++i)
        if (is_default_blue(style.Colors[i])) style.Colors[i] = accent_like(style.Colors[i]);
    for (ImGuiCol i : PRESSED) style.Colors[i] = lighten(style.Colors[i], PRESSED_LIGHTEN);
    style.Alpha = WINDOW_ALPHA;
    style.Colors[ImGuiCol_WindowBg].w = 1.0f; /* Style.Alpha already makes it translucent */
    style.Colors[ImGuiCol_PopupBg].w = 1.0f;
    style.WindowRounding = WINDOW_ROUNDING;
    style.ChildRounding = WIDGET_ROUNDING;
    style.FrameRounding = WIDGET_ROUNDING;
    style.GrabRounding = WIDGET_ROUNDING;
    style.TabRounding = WIDGET_ROUNDING;
    style.PopupRounding = POPUP_ROUNDING;
    style.ScrollbarRounding = SCROLLBAR_ROUNDING;
}
