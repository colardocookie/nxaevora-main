// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// The UI scale of the engine menu (md3_scale.h).

#include "nx/gpu/vk/md3/md3_scale.h"

#include <algorithm>

namespace md3 {

float UiScale(int pixel_height, float display_scale) {
    if (!(display_scale > 0.0f)) display_scale = 1.0f;
    float scale = display_scale;
    if (pixel_height > 0) {
        const float height_scale =
            static_cast<float>(pixel_height) / static_cast<float>(kReferenceHeightPx);
        scale = std::max(scale, height_scale);
    }
    return std::clamp(scale * kUiSizeFactor, kMinUiScale, kMaxUiScale);
}

} // namespace md3
