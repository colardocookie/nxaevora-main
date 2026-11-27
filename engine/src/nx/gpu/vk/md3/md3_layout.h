// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// The pure layout rules of the engine menu's settings rows and page scroll.
// No ImGui, so the layout test links this file alone.

#pragma once

namespace md3 {

// Decision 2 of the display layout brief: the width that a row's label and
// supporting text may use. Each size is in pixels at the current theme scale.
struct TextColumnInput {
    float row_width;
    float row_padding; // on each side
    float icon_slot;   // leading restart icon slot, 0 when there is none
    float right_width; // the right part
    float gap;         // between the text and the right part
};

// The row width minus the two row paddings, the icon slot, the right part and
// the gap, never below 0. A negative or not finite size counts as 0.
float TextColumnWidth(const TextColumnInput& in);

// Decision 1: the row height from the measured text block.
struct RowHeightInput {
    float text_height;      // label + gap + wrapped supporting text
    float vertical_padding; // above and below the text
    float min_height;       // the one-line or two-line row metric
    float right_height;     // the right part
};

// The larger of min_height, right_height and the text block with its two
// paddings. A negative or not finite size counts as 0.
float SettingsRowHeight(const RowHeightInput& in);

// Decision 5: the page scroll that shows the row of the focused nav item.
struct PageScrollInput {
    float row_top;     // in the page's content coordinates
    float row_bottom;  // in the page's content coordinates
    float view_height; // the visible height of the page
    float current_scroll;
    float max_scroll;
    bool first_row; // the first nav item of the page
    bool last_row;  // the last nav item of the page
};

// The first row gives 0 and the last row gives the maximum. Else a row above
// the view scrolls to its top, a row below the view to its bottom, and a row
// already visible keeps the scroll. The result is clamped to 0..max_scroll.
float PageScrollTarget(const PageScrollInput& in);

} // namespace md3
