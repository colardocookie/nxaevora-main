// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// The pure rules of the debug overlays: where the three overlays go, the row
// count of each performance level, and the HUD line. No ImGui, no locks and no
// globals, so the host test links this file alone.

#pragma once

#include <stddef.h>

namespace debug_overlay {

enum Corner {
    kCornerTopLeft = 0,
    kCornerTopRight,
    kCornerBottomLeft,
    kCornerBottomRight,
    kCornerCount
};

enum Level {
    kLevelOff = 0,
    kLevelMinimal,
    kLevelStandard,
    kLevelDetailed,
    kLevelCount
};

// The coordinates start at the top left of the viewport, in pixels.
struct OverlayRect {
    float x, y, w, h;
};

struct OverlayPlacementInput {
    bool perf_shown;
    bool input_shown;
    bool hud_shown;
    int perf_corner;
    int input_corner;
    float screen_w;
    float screen_h;
    float margin;
    float gap;
    float perf_w;
    float perf_h;
    float input_w;
    float input_h;
    float hud_w;
    float hud_h;
};

struct OverlayPlacement {
    OverlayRect perf;
    OverlayRect input;
    OverlayRect hud;
};

// The place of each shown overlay. The HUD is at the top middle, a top corner
// overlay moves down by its height and the gap, and two overlays in one corner
// stack with the gap between them (the bottom corners upward).
OverlayPlacement PlaceOverlays(const OverlayPlacementInput& in);

// The rows of a performance level: Off 0, Minimal 1, Standard 3, Detailed 12.
int PerfRowCount(int level);

// Wraps a HUD list into lines within max_width. The first line starts with
// HUD_DEBUG_LEAD and a line breaks only between the items (at ", ") when the
// next item would make the line wider than max_width. measure gives the width
// of a text. Returns the line count and writes the lines with "\n" to out; a
// NULL or empty list gives 0 lines and no HUD.
int HudWrapText(const char* list, float max_width, float (*measure)(const char* text, void* ctx), void* ctx,
                char* out, size_t size);

} // namespace debug_overlay
