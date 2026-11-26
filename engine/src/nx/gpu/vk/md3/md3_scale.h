// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// The UI scale of the engine menu: the display scale and the window pixel
// height against the 1080p reference. It needs no ImGui, so the scale test
// links it alone.

#pragma once

namespace md3 {

// The MD3 sizes read small on a PC screen (user run 5), so every scale is
// multiplied by this factor. The UI is never smaller than its 1080p size.
inline constexpr float kUiSizeFactor = 1.25f;
inline constexpr float kMinUiScale = kUiSizeFactor;
inline constexpr float kMaxUiScale = 3.0f * kUiSizeFactor;
inline constexpr int kReferenceHeightPx = 1080;

// The larger of the display scale and window pixel height / 1080, times
// kUiSizeFactor, clamped to kMinUiScale..kMaxUiScale. A display_scale that
// is not positive counts as 1.0.
float UiScale(int pixel_height, float display_scale);

} // namespace md3
