// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// The layout rules of the settings rows and page scroll (md3_layout.h). Every
// expected value comes from the decisions of the display layout brief.

#include <cmath>
#include <cstdio>

#include "nx/gpu/vk/md3/md3_layout.h"

namespace {

constexpr double kTolerance = 1e-4;

bool Near(const char* name, double actual, double expected) {
    const double diff = std::fabs(actual - expected);
    if (!(diff <= kTolerance)) { // a NaN result fails too
        std::printf("FAIL %s: got %.6f, expected %.6f (tolerance %.1e)\n", name, actual, expected,
                    kTolerance);
        return false;
    }
    return true;
}

struct ColumnCase {
    const char* name;
    md3::TextColumnInput in;
    double expected;
};

const ColumnCase kColumnCases[] = {
    // 1000 - 2 x 16 row padding - 22 icon slot - 240 right part - 16 gap = 690.
    {"text column", {1000.0f, 16.0f, 22.0f, 240.0f, 16.0f}, 690.0},
    // The right part is wider than the row: the width is 0, not negative.
    {"right wider than row", {1000.0f, 16.0f, 22.0f, 2000.0f, 16.0f}, 0.0},
    // 1000 - 0 padding - 22 icon slot - 0 right part - 16 gap = 962.
    {"no right part", {1000.0f, 0.0f, 22.0f, 0.0f, 16.0f}, 962.0},
    // A negative size counts as 0: 1000 - 32 - 22 - 0 - 16 = 930.
    {"negative right part", {1000.0f, 16.0f, 22.0f, -240.0f, 16.0f}, 930.0},
    // A negative padding counts as 0: 1000 - 0 - 22 - 240 - 16 = 722.
    {"negative padding", {1000.0f, -16.0f, 22.0f, 240.0f, 16.0f}, 722.0},
    {"zero row width", {0.0f, 16.0f, 22.0f, 240.0f, 16.0f}, 0.0},
};

struct HeightCase {
    const char* name;
    md3::RowHeightInput in;
    double expected;
};

const HeightCase kHeightCases[] = {
    // 2 x 16 padding + 32 text = 64, the two-line height is also 64.
    {"short text keeps the two-line height", {32.0f, 16.0f, 64.0f, 0.0f}, 64.0},
    // 2 x 20 padding + (16 label + 2 gap + 5 x 20 text) = 158.
    {"five lines grow the row", {118.0f, 20.0f, 64.0f, 0.0f}, 158.0},
    // A right part taller than the text and the minimum sets the height.
    {"right part taller", {32.0f, 16.0f, 64.0f, 200.0f}, 200.0},
    // 2 x 16 padding + 0 text = 32, below the one-line height of 48.
    {"no text keeps the one-line height", {0.0f, 16.0f, 48.0f, 0.0f}, 48.0},
    // A negative text height counts as 0, so the one-line height wins.
    {"negative text", {-32.0f, 16.0f, 48.0f, 0.0f}, 48.0},
};

struct ScrollCase {
    const char* name;
    md3::PageScrollInput in;
    double expected;
};

const ScrollCase kScrollCases[] = {
    // The first row goes to 0 even when the row is visible at scroll 50.
    {"first row", {100.0f, 180.0f, 100.0f, 50.0f, 500.0f, true, false}, 0.0},
    // The last row goes to the maximum.
    {"last row", {400.0f, 480.0f, 100.0f, 100.0f, 500.0f, false, true}, 500.0},
    // A middle row above the view (visible 200..300) scrolls to its top.
    {"row above view", {120.0f, 160.0f, 100.0f, 200.0f, 500.0f, false, false}, 120.0},
    // A middle row below the view (visible 100..200) puts its bottom at 340 - 100.
    {"row below view", {300.0f, 340.0f, 100.0f, 100.0f, 500.0f, false, false}, 240.0},
    // A middle row fully visible (visible 100..200) keeps the scroll.
    {"row visible", {130.0f, 180.0f, 100.0f, 100.0f, 500.0f, false, false}, 100.0},
    // 640 - 100 = 540, clamped to the maximum of 500.
    {"clamped to maximum", {600.0f, 640.0f, 100.0f, 0.0f, 500.0f, false, false}, 500.0},
    // The row starts at the view top, so the target stays at 0.
    {"clamped to zero", {0.0f, 60.0f, 100.0f, 10.0f, 500.0f, false, false}, 0.0},
    // A view height of 0 keeps the clamped current scroll.
    {"zero view height", {300.0f, 340.0f, 0.0f, 30.0f, 500.0f, false, false}, 30.0},
    // A maximum of 0 gives 0 for the last row.
    {"zero maximum", {400.0f, 480.0f, 100.0f, 0.0f, 0.0f, false, true}, 0.0},
    // A bottom above the top counts as the top: 200 is below the view of 0..100.
    {"bottom above top", {200.0f, 150.0f, 100.0f, 0.0f, 500.0f, false, false}, 100.0},
};

} // namespace

int main() {
    for (const ColumnCase& test : kColumnCases) {
        if (!Near(test.name, md3::TextColumnWidth(test.in), test.expected)) return 1;
    }
    for (const HeightCase& test : kHeightCases) {
        if (!Near(test.name, md3::SettingsRowHeight(test.in), test.expected)) return 1;
    }
    for (const ScrollCase& test : kScrollCases) {
        if (!Near(test.name, md3::PageScrollTarget(test.in), test.expected)) return 1;
    }

    std::printf("md3_layout_test: all cases passed\n");
    return 0;
}
