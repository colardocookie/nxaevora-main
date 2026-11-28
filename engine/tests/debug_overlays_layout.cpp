// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// The pure rules of the debug overlays (src/nx/gpu/vk/debug_overlays_layout.cpp):
// the place of each overlay, the rows of each performance level and the HUD
// wrap.

#include "nx/gpu/vk/debug_overlays_layout.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

constexpr float kScreenW = 1280.0f;
constexpr float kScreenH = 720.0f;
constexpr float kMargin = 16.0f;
constexpr float kGap = 8.0f;
constexpr float kPerfW = 200.0f;
constexpr float kPerfH = 100.0f;
constexpr float kInputW = 220.0f;
constexpr float kInputH = 140.0f;
constexpr float kHudW = 400.0f;
constexpr float kHudH = 40.0f;
constexpr float kEpsilon = 0.001f;

bool Near(float a, float b) { return std::fabs(a - b) < kEpsilon; }

debug_overlay::OverlayPlacementInput Base() {
    debug_overlay::OverlayPlacementInput in{};
    in.screen_w = kScreenW;
    in.screen_h = kScreenH;
    in.margin = kMargin;
    in.gap = kGap;
    in.perf_w = kPerfW;
    in.perf_h = kPerfH;
    in.input_w = kInputW;
    in.input_h = kInputH;
    in.hud_w = kHudW;
    in.hud_h = kHudH;
    return in;
}

// The x of a corner by the margin and the overlay's own width.
float CornerX(int corner, float width) {
    const bool left = corner == debug_overlay::kCornerTopLeft || corner == debug_overlay::kCornerBottomLeft;
    return left ? kMargin : kScreenW - kMargin - width;
}

float CornerY(int corner, float height) {
    const bool top = corner == debug_overlay::kCornerTopLeft || corner == debug_overlay::kCornerTopRight;
    return top ? kMargin : kScreenH - kMargin - height;
}

void PerfAlone() {
    for (int corner = 0; corner < debug_overlay::kCornerCount; ++corner) {
        debug_overlay::OverlayPlacementInput in = Base();
        in.perf_shown = true;
        in.perf_corner = corner;
        const debug_overlay::OverlayPlacement out = debug_overlay::PlaceOverlays(in);
        assert(Near(out.perf.x, CornerX(corner, kPerfW)));
        assert(Near(out.perf.y, CornerY(corner, kPerfH)));
        assert(Near(out.perf.w, kPerfW) && Near(out.perf.h, kPerfH));
        assert(Near(out.input.w, 0.0f) && Near(out.input.h, 0.0f));
        assert(Near(out.hud.w, 0.0f) && Near(out.hud.h, 0.0f));
    }
}

void InputAlone() {
    for (int corner = 0; corner < debug_overlay::kCornerCount; ++corner) {
        debug_overlay::OverlayPlacementInput in = Base();
        in.input_shown = true;
        in.input_corner = corner;
        const debug_overlay::OverlayPlacement out = debug_overlay::PlaceOverlays(in);
        assert(Near(out.input.x, CornerX(corner, kInputW)));
        assert(Near(out.input.y, CornerY(corner, kInputH)));
        assert(Near(out.perf.w, 0.0f) && Near(out.perf.h, 0.0f));
    }
}

void SameCorner() {
    {
        debug_overlay::OverlayPlacementInput in = Base();
        in.perf_shown = true;
        in.input_shown = true;
        in.perf_corner = debug_overlay::kCornerTopLeft;
        in.input_corner = debug_overlay::kCornerTopLeft;
        const debug_overlay::OverlayPlacement out = debug_overlay::PlaceOverlays(in);
        assert(Near(out.perf.y, kMargin));
        assert(Near(out.input.y, kMargin + kPerfH + kGap));
        assert(Near(out.perf.x, kMargin) && Near(out.input.x, kMargin));
    }
    {
        debug_overlay::OverlayPlacementInput in = Base();
        in.perf_shown = true;
        in.input_shown = true;
        in.perf_corner = debug_overlay::kCornerBottomRight;
        in.input_corner = debug_overlay::kCornerBottomRight;
        const debug_overlay::OverlayPlacement out = debug_overlay::PlaceOverlays(in);
        assert(Near(out.perf.y, kScreenH - kMargin - kPerfH));
        assert(Near(out.input.y, out.perf.y - kGap - kInputH)); // the bottom corners stack upward
    }
}

void HudPlacement() {
    // The HUD is at the top middle.
    {
        debug_overlay::OverlayPlacementInput in = Base();
        in.hud_shown = true;
        const debug_overlay::OverlayPlacement out = debug_overlay::PlaceOverlays(in);
        assert(Near(out.hud.x, (kScreenW - kHudW) * 0.5f));
        assert(Near(out.hud.y, kMargin));
        assert(Near(out.hud.w, kHudW) && Near(out.hud.h, kHudH));
    }
    // A top corner overlay that overlaps it moves down by the HUD's height and the gap.
    {
        debug_overlay::OverlayPlacementInput in = Base();
        in.perf_shown = true;
        in.hud_shown = true;
        in.perf_corner = debug_overlay::kCornerTopLeft;
        in.hud_w = 1200.0f; // the HUD covers the corner
        const debug_overlay::OverlayPlacement out = debug_overlay::PlaceOverlays(in);
        assert(Near(out.hud.x, (kScreenW - 1200.0f) * 0.5f));
        assert(Near(out.perf.y, kMargin + kHudH + kGap));
    }
    // A top corner overlay that does not overlap it stays.
    {
        debug_overlay::OverlayPlacementInput in = Base();
        in.perf_shown = true;
        in.hud_shown = true;
        in.perf_corner = debug_overlay::kCornerTopLeft;
        const debug_overlay::OverlayPlacement out = debug_overlay::PlaceOverlays(in);
        assert(Near(out.perf.y, kMargin));
    }
    // A bottom corner overlay does not move.
    {
        debug_overlay::OverlayPlacementInput in = Base();
        in.perf_shown = true;
        in.hud_shown = true;
        in.perf_corner = debug_overlay::kCornerBottomLeft;
        in.hud_w = 1200.0f;
        const debug_overlay::OverlayPlacement out = debug_overlay::PlaceOverlays(in);
        assert(Near(out.perf.y, kScreenH - kMargin - kPerfH));
    }
    // Two overlays in one top corner with the HUD: both move down, the input below the performance overlay.
    {
        debug_overlay::OverlayPlacementInput in = Base();
        in.perf_shown = true;
        in.input_shown = true;
        in.hud_shown = true;
        in.perf_corner = debug_overlay::kCornerTopLeft;
        in.input_corner = debug_overlay::kCornerTopLeft;
        in.hud_w = 1200.0f;
        const debug_overlay::OverlayPlacement out = debug_overlay::PlaceOverlays(in);
        assert(Near(out.perf.y, kMargin + kHudH + kGap));
        assert(Near(out.input.y, out.perf.y + kPerfH + kGap));
    }
    // Nothing shown: every rect stays at zero.
    {
        const debug_overlay::OverlayPlacement out = debug_overlay::PlaceOverlays(Base());
        assert(Near(out.perf.w, 0.0f) && Near(out.perf.h, 0.0f));
        assert(Near(out.input.w, 0.0f) && Near(out.input.h, 0.0f));
        assert(Near(out.hud.w, 0.0f) && Near(out.hud.h, 0.0f));
    }
}

void Rows() {
    assert(debug_overlay::PerfRowCount(debug_overlay::kLevelOff) == 0);
    assert(debug_overlay::PerfRowCount(debug_overlay::kLevelMinimal) == 1);
    assert(debug_overlay::PerfRowCount(debug_overlay::kLevelStandard) == 3);
    assert(debug_overlay::PerfRowCount(debug_overlay::kLevelDetailed) == 12);
    assert(debug_overlay::PerfRowCount(debug_overlay::kLevelMinimal) <
           debug_overlay::PerfRowCount(debug_overlay::kLevelStandard));
    assert(debug_overlay::PerfRowCount(debug_overlay::kLevelStandard) <
           debug_overlay::PerfRowCount(debug_overlay::kLevelDetailed));
}

// The width of a text at 10 pixels for each byte.
float TenPxPerByte(const char* text, void* ctx) {
    (void)ctx;
    return 10.0f * static_cast<float>(std::strlen(text));
}

void HudWrap() {
    char out[96];
    // A list that fits gives one line.
    assert(debug_overlay::HudWrapText("a=1, b=2", 200.0f, TenPxPerByte, nullptr, out, sizeof out) == 1);
    assert(std::strcmp(out, "Debug on: a=1, b=2") == 0);
    // A list that is too wide breaks between the items; the second line starts with the item.
    assert(debug_overlay::HudWrapText("a=1, b=2", 150.0f, TenPxPerByte, nullptr, out, sizeof out) == 2);
    assert(std::strcmp(out, "Debug on: a=1\nb=2") == 0);
    // An item wider than the maximum stays on its own line.
    assert(debug_overlay::HudWrapText("a=1, very_long_item=1", 150.0f, TenPxPerByte, nullptr, out, sizeof out) == 2);
    assert(std::strcmp(out, "Debug on: a=1\nvery_long_item=1") == 0);
    // An empty and a NULL list give no HUD: 0 lines.
    assert(debug_overlay::HudWrapText("", 150.0f, TenPxPerByte, nullptr, out, sizeof out) == 0);
    assert(out[0] == '\0');
    assert(debug_overlay::HudWrapText(nullptr, 150.0f, TenPxPerByte, nullptr, out, sizeof out) == 0);
}

} // namespace

int main() {
    PerfAlone();
    InputAlone();
    SameCorner();
    HudPlacement();
    Rows();
    HudWrap();
    std::puts("debug_overlays_layout passed");
    return 0;
}
