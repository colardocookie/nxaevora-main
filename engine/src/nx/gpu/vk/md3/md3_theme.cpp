// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// The MD3 theme of the mockup. The state layers and the roles follow the MD3
// dark scheme of the task brief.

#include "nx/gpu/vk/md3/md3_theme.h"

#include <algorithm>
#include <cmath>

namespace md3 {
namespace {

Theme g_theme{};
ImGuiStyle g_base_style{};
bool g_base_ready = false;

ImU32 RoleColor(uint32_t argb, float alpha) {
    const float a = std::clamp(alpha, 0.0f, 1.0f) * static_cast<float>(AlphaFromArgb(argb)) / 255.0f;
    return IM_COL32(static_cast<int>(RedFromArgb(argb)), static_cast<int>(GreenFromArgb(argb)),
                    static_cast<int>(BlueFromArgb(argb)),
                    static_cast<int>(std::round(a * 255.0f)));
}

ImU32 Blend(ImU32 container, ImU32 on_color, float amount) {
    const auto channel = [](ImU32 color, int shift) {
        return static_cast<float>((color >> shift) & 0xFFu);
    };
    const auto mix = [amount](float from, float to) { return from + (to - from) * amount; };
    return IM_COL32(static_cast<int>(std::round(mix(channel(container, 0), channel(on_color, 0)))),
                    static_cast<int>(std::round(mix(channel(container, 8), channel(on_color, 8)))),
                    static_cast<int>(std::round(mix(channel(container, 16), channel(on_color, 16)))),
                    255);
}

void ApplyStyleSizes(ImGuiStyle& style, float scale) {
    style.WindowPadding = ImVec2(16.0f, 16.0f);
    style.FramePadding = ImVec2(12.0f, 8.0f);
    style.CellPadding = ImVec2(8.0f, 4.0f);
    style.ItemSpacing = ImVec2(10.0f, 8.0f);
    style.ItemInnerSpacing = ImVec2(8.0f, 6.0f);
    style.IndentSpacing = 16.0f;
    style.ScrollbarSize = 10.0f;
    style.GrabMinSize = 20.0f;

    style.WindowRounding = kCornerLargePx;
    style.ChildRounding = kCornerMediumPx;
    style.PopupRounding = kCornerMediumPx;
    style.FrameRounding = kCornerSmallPx;
    style.ScrollbarRounding = 5.0f;
    style.GrabRounding = 10.0f;
    style.TabRounding = kCornerSmallPx;

    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;

    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    style.SelectableTextAlign = ImVec2(0.0f, 0.5f);

    style.ScaleAllSizes(scale);
    style.FontSizeBase = kBodyMediumPx;
    style.FontScaleMain = scale;
    style.FontScaleDpi = 1.0f;
}

void ApplyColors(ImGuiStyle& style, const Scheme& c) {
    const ImU32 surface = ToImU32(c.surface);
    const ImU32 on_surface = ToImU32(c.on_surface);
    const ImU32 surface_container = ToImU32(c.surface_container);
    const ImU32 surface_container_high = ToImU32(c.surface_container_high);
    const ImU32 surface_container_highest = ToImU32(c.surface_container_highest);
    const ImU32 primary = ToImU32(c.primary);
    const ImU32 on_primary = ToImU32(c.on_primary);
    const ImU32 secondary = ToImU32(c.secondary);
    const ImU32 secondary_container = ToImU32(c.secondary_container);
    const ImU32 on_secondary_container = ToImU32(c.on_secondary_container);
    const ImU32 outline = ToImU32(c.outline);
    const ImU32 outline_variant = ToImU32(c.outline_variant);
    const ImU32 transparent = IM_COL32(0, 0, 0, 0);

    style.Colors[ImGuiCol_Text] = ImGui::ColorConvertU32ToFloat4(on_surface);
    style.Colors[ImGuiCol_TextDisabled] = ImGui::ColorConvertU32ToFloat4(RoleColor(c.on_surface, 0.38f));
    style.Colors[ImGuiCol_WindowBg] = ImGui::ColorConvertU32ToFloat4(surface);
    style.Colors[ImGuiCol_ChildBg] = ImGui::ColorConvertU32ToFloat4(surface);
    style.Colors[ImGuiCol_PopupBg] = ImGui::ColorConvertU32ToFloat4(surface_container_high);
    style.Colors[ImGuiCol_Border] = ImGui::ColorConvertU32ToFloat4(outline_variant);
    style.Colors[ImGuiCol_BorderShadow] = ImGui::ColorConvertU32ToFloat4(transparent);

    style.Colors[ImGuiCol_FrameBg] = ImGui::ColorConvertU32ToFloat4(surface_container_highest);
    style.Colors[ImGuiCol_FrameBgHovered] =
        ImGui::ColorConvertU32ToFloat4(Blend(surface_container_highest, on_surface, 0.08f));
    style.Colors[ImGuiCol_FrameBgActive] =
        ImGui::ColorConvertU32ToFloat4(Blend(surface_container_highest, on_surface, 0.10f));

    style.Colors[ImGuiCol_TitleBg] = ImGui::ColorConvertU32ToFloat4(surface_container);
    style.Colors[ImGuiCol_TitleBgActive] = ImGui::ColorConvertU32ToFloat4(surface_container);
    style.Colors[ImGuiCol_TitleBgCollapsed] = ImGui::ColorConvertU32ToFloat4(surface_container);
    style.Colors[ImGuiCol_MenuBarBg] = ImGui::ColorConvertU32ToFloat4(surface_container);

    style.Colors[ImGuiCol_ScrollbarBg] = ImGui::ColorConvertU32ToFloat4(transparent);
    style.Colors[ImGuiCol_ScrollbarGrab] = ImGui::ColorConvertU32ToFloat4(outline_variant);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImGui::ColorConvertU32ToFloat4(outline);
    style.Colors[ImGuiCol_ScrollbarGrabActive] = ImGui::ColorConvertU32ToFloat4(primary);

    style.Colors[ImGuiCol_CheckMark] = ImGui::ColorConvertU32ToFloat4(primary);
    style.Colors[ImGuiCol_CheckboxSelectedBg] = ImGui::ColorConvertU32ToFloat4(primary);
    style.Colors[ImGuiCol_SliderGrab] = ImGui::ColorConvertU32ToFloat4(primary);
    style.Colors[ImGuiCol_SliderGrabActive] =
        ImGui::ColorConvertU32ToFloat4(Blend(primary, on_primary, 0.10f));

    style.Colors[ImGuiCol_Button] = ImGui::ColorConvertU32ToFloat4(primary);
    style.Colors[ImGuiCol_ButtonHovered] = ImGui::ColorConvertU32ToFloat4(Blend(primary, on_primary, 0.08f));
    style.Colors[ImGuiCol_ButtonActive] = ImGui::ColorConvertU32ToFloat4(Blend(primary, on_primary, 0.10f));

    style.Colors[ImGuiCol_Header] = ImGui::ColorConvertU32ToFloat4(secondary_container);
    style.Colors[ImGuiCol_HeaderHovered] =
        ImGui::ColorConvertU32ToFloat4(Blend(secondary_container, on_secondary_container, 0.08f));
    style.Colors[ImGuiCol_HeaderActive] =
        ImGui::ColorConvertU32ToFloat4(Blend(secondary_container, on_secondary_container, 0.10f));

    style.Colors[ImGuiCol_Separator] = ImGui::ColorConvertU32ToFloat4(outline_variant);
    style.Colors[ImGuiCol_SeparatorHovered] = ImGui::ColorConvertU32ToFloat4(outline);
    style.Colors[ImGuiCol_SeparatorActive] = ImGui::ColorConvertU32ToFloat4(primary);
    style.Colors[ImGuiCol_ResizeGrip] = ImGui::ColorConvertU32ToFloat4(transparent);
    style.Colors[ImGuiCol_ResizeGripHovered] = ImGui::ColorConvertU32ToFloat4(outline_variant);
    style.Colors[ImGuiCol_ResizeGripActive] = ImGui::ColorConvertU32ToFloat4(primary);

    style.Colors[ImGuiCol_InputTextCursor] = ImGui::ColorConvertU32ToFloat4(primary);
    style.Colors[ImGuiCol_Tab] = ImGui::ColorConvertU32ToFloat4(surface_container);
    style.Colors[ImGuiCol_TabHovered] = ImGui::ColorConvertU32ToFloat4(secondary_container);
    style.Colors[ImGuiCol_TabSelected] = ImGui::ColorConvertU32ToFloat4(secondary_container);
    style.Colors[ImGuiCol_TabSelectedOverline] = ImGui::ColorConvertU32ToFloat4(primary);
    style.Colors[ImGuiCol_TabDimmed] = ImGui::ColorConvertU32ToFloat4(surface);
    style.Colors[ImGuiCol_TabDimmedSelected] = ImGui::ColorConvertU32ToFloat4(surface_container);
    style.Colors[ImGuiCol_TabDimmedSelectedOverline] = ImGui::ColorConvertU32ToFloat4(outline);

    style.Colors[ImGuiCol_PlotLines] = ImGui::ColorConvertU32ToFloat4(primary);
    style.Colors[ImGuiCol_PlotLinesHovered] = ImGui::ColorConvertU32ToFloat4(ToImU32(c.tertiary));
    style.Colors[ImGuiCol_PlotHistogram] = ImGui::ColorConvertU32ToFloat4(ToImU32(c.tertiary));
    style.Colors[ImGuiCol_PlotHistogramHovered] = ImGui::ColorConvertU32ToFloat4(primary);

    style.Colors[ImGuiCol_TableHeaderBg] = ImGui::ColorConvertU32ToFloat4(surface_container_high);
    style.Colors[ImGuiCol_TableBorderStrong] = ImGui::ColorConvertU32ToFloat4(outline_variant);
    style.Colors[ImGuiCol_TableBorderLight] = ImGui::ColorConvertU32ToFloat4(outline_variant);
    style.Colors[ImGuiCol_TableRowBg] = ImGui::ColorConvertU32ToFloat4(transparent);
    style.Colors[ImGuiCol_TableRowBgAlt] = ImGui::ColorConvertU32ToFloat4(transparent);

    style.Colors[ImGuiCol_TextLink] = ImGui::ColorConvertU32ToFloat4(primary);
    style.Colors[ImGuiCol_TextSelectedBg] = ImGui::ColorConvertU32ToFloat4(secondary_container);
    style.Colors[ImGuiCol_TreeLines] = ImGui::ColorConvertU32ToFloat4(outline_variant);
    style.Colors[ImGuiCol_DragDropTarget] = ImGui::ColorConvertU32ToFloat4(primary);
    style.Colors[ImGuiCol_DragDropTargetBg] = ImGui::ColorConvertU32ToFloat4(transparent);
    style.Colors[ImGuiCol_UnsavedMarker] = ImGui::ColorConvertU32ToFloat4(secondary);

    style.Colors[ImGuiCol_NavCursor] = ImGui::ColorConvertU32ToFloat4(secondary);
    style.Colors[ImGuiCol_NavWindowingHighlight] = ImGui::ColorConvertU32ToFloat4(on_surface);
    style.Colors[ImGuiCol_NavWindowingDimBg] = ImGui::ColorConvertU32ToFloat4(IM_COL32(0, 0, 0, 51));
    style.Colors[ImGuiCol_ModalWindowDimBg] = ImGui::ColorConvertU32ToFloat4(IM_COL32(0, 0, 0, 102));
}

} // namespace

const Theme& CurrentTheme() { return g_theme; }

ImU32 ToImU32(uint32_t argb) {
    return IM_COL32(static_cast<int>(RedFromArgb(argb)), static_cast<int>(GreenFromArgb(argb)),
                    static_cast<int>(BlueFromArgb(argb)), static_cast<int>(AlphaFromArgb(argb)));
}

ImU32 FadeColor(ImU32 color, float alpha_mul) {
    return ImGui::GetColorU32(color, alpha_mul);
}

ImU32 StateLayer(ImU32 container, ImU32 on_color, float layer_alpha) {
    const float amount = std::clamp(layer_alpha, 0.0f, 1.0f);
    return Blend(container, on_color, amount);
}

ImU32 StateLayerOver(ImU32 on_color, float layer_alpha) {
    const float amount = std::clamp(layer_alpha, 0.0f, 1.0f);
    return (on_color & 0x00FFFFFFu) | (static_cast<ImU32>(std::lround(amount * 255.0f)) << 24);
}

void ApplyTheme(const Scheme& scheme, float ui_scale) {
    if (!g_base_ready) {
        // ImGui's defaults, not the current style: the engine's other windows set Alpha 0.94 (imgui_style.cpp)
        g_base_style = ImGuiStyle();
        ImGui::StyleColorsDark(&g_base_style);
        g_base_ready = true;
    }

    g_theme.scheme = scheme;
    g_theme.scale = std::clamp(ui_scale, kMinUiScale, kMaxUiScale);
    g_theme.corner_small = kCornerSmallPx * g_theme.scale;
    g_theme.corner_medium = kCornerMediumPx * g_theme.scale;
    g_theme.corner_large = kCornerLargePx * g_theme.scale;
    g_theme.title_large = kTitleLargePx * g_theme.scale;
    g_theme.title_medium = kTitleMediumPx * g_theme.scale;
    g_theme.body_large = kBodyLargePx * g_theme.scale;
    g_theme.body_medium = kBodyMediumPx * g_theme.scale;
    g_theme.label_large = kLabelLargePx * g_theme.scale;

    ImGuiStyle& style = ImGui::GetStyle();
    style = g_base_style;
    ApplyStyleSizes(style, g_theme.scale);
    ApplyColors(style, scheme);
}

void PushFontSize(float unscaled_px) { ImGui::PushFont(nullptr, unscaled_px); }

} // namespace md3
