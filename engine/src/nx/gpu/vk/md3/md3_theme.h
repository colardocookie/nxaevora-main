// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// The MD3 theme of the mockup: the scheme roles as ImGuiStyle colors, the
// shape corners and the type scale, all scaled from the window pixel height and
// the display scale.

#pragma once

#include <cstdint>

#include "imgui.h"
#include "nx/gpu/vk/md3/md3_palette.h"
#include "nx/gpu/vk/md3/md3_scale.h"

namespace md3 {

// Type scale in pixels at 1080p (MD3). The values are unscaled: PushFontSize
// takes them unscaled, while AddText and CalcTextSizeA take theme.<role> after
// the scale is applied.
inline constexpr float kTitleLargePx = 22.0f;
inline constexpr float kTitleMediumPx = 16.0f;
inline constexpr float kBodyLargePx = 16.0f;
inline constexpr float kBodyMediumPx = 14.0f;
inline constexpr float kLabelLargePx = 14.0f;

// Shape corners in pixels at 1080p (MD3). A pill uses half of its height.
inline constexpr float kCornerSmallPx = 8.0f;
inline constexpr float kCornerMediumPx = 12.0f;
inline constexpr float kCornerLargePx = 16.0f;

struct Theme {
    Scheme scheme;
    float scale;
    float corner_small;
    float corner_medium;
    float corner_large;
    // Final pixel size: for AddText and CalcTextSizeA, never for PushFont.
    float title_large;
    float title_medium;
    float body_large;
    float body_medium;
    float label_large;
};

// The theme of the current frame.
const Theme& CurrentTheme();

// Sets the ImGuiStyle colors and sizes for the scheme and scale. Call it
// before NewFrame and again after a window resize.
void ApplyTheme(const Scheme& scheme, float ui_scale);

// Pushes an unscaled MD3 size. ImGui applies style.FontScaleMain over it.
void PushFontSize(float unscaled_px);

// 0xAARRGGBB to the packed RGBA that ImGui expects.
ImU32 ToImU32(uint32_t argb);

// A draw list color with ImGui::GetStyle().Alpha and alpha_mul applied to its
// alpha (imgui.cpp:3718). PushStyleColor colors already get style alpha.
ImU32 FadeColor(ImU32 color, float alpha_mul = 1.0f);

// A state layer: the "on" color over the container at the given alpha.
ImU32 StateLayer(ImU32 container, ImU32 on_color, float layer_alpha);

// A state layer over a fully transparent container, for hover and press.
ImU32 StateLayerOver(ImU32 on_color, float layer_alpha);

} // namespace md3
