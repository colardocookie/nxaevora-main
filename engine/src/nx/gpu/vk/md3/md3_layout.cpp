// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// The pure layout rules of the settings rows and page scroll (md3_layout.h).

#include "nx/gpu/vk/md3/md3_layout.h"

#include <algorithm>
#include <cmath>

namespace md3 {
namespace {

// A measured size that is not finite or is negative counts as 0. A zero size
// stays valid, so the callers can pass an empty right part or text.
float NonNegative(float value) {
    if (!std::isfinite(value) || value < 0.0f) return 0.0f;
    return value;
}

} // namespace

float TextColumnWidth(const TextColumnInput& in) {
    const float row_width = NonNegative(in.row_width);
    const float padding = NonNegative(in.row_padding);
    const float icon_slot = NonNegative(in.icon_slot);
    const float right_width = NonNegative(in.right_width);
    const float gap = NonNegative(in.gap);
    const float width = row_width - 2.0f * padding - icon_slot - right_width - gap;
    return width > 0.0f ? width : 0.0f;
}

float SettingsRowHeight(const RowHeightInput& in) {
    const float text_height = NonNegative(in.text_height);
    const float padding = NonNegative(in.vertical_padding);
    const float min_height = NonNegative(in.min_height);
    const float right_height = NonNegative(in.right_height);
    float height = 2.0f * padding + text_height;
    if (min_height > height) height = min_height;
    if (right_height > height) height = right_height;
    return height;
}

float PageScrollTarget(const PageScrollInput& in) {
    const float max_scroll = NonNegative(in.max_scroll);
    const float current = std::clamp(NonNegative(in.current_scroll), 0.0f, max_scroll);
    const float view_height = NonNegative(in.view_height);
    const float row_top = NonNegative(in.row_top);
    float row_bottom = NonNegative(in.row_bottom);
    if (row_bottom < row_top) row_bottom = row_top;

    float target = current;
    if (in.first_row) {
        target = 0.0f;
    } else if (in.last_row) {
        target = max_scroll;
    } else if (view_height > 0.0f) {
        if (row_top < current) {
            target = row_top;
        } else if (row_bottom > current + view_height) {
            target = row_bottom - view_height;
        }
    }
    return std::clamp(target, 0.0f, max_scroll);
}

} // namespace md3
