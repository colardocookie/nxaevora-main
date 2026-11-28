// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// The pure rules of the debug overlays (debug_overlays_layout.h).

#include "nx/gpu/vk/debug_overlays_layout.h"

#include <cstdio>
#include <cstring>

extern "C" {
#include "nx/runtime/platform/hud.h"
}

namespace debug_overlay {
namespace {

bool IsTop(int corner) { return corner == kCornerTopLeft || corner == kCornerTopRight; }

bool IsLeft(int corner) { return corner == kCornerTopLeft || corner == kCornerBottomLeft; }

bool OverlapX(const OverlayRect& a, const OverlayRect& b) { return a.x < b.x + b.w && b.x < a.x + a.w; }

// Appends text to out, cut to its room, and returns the appended length.
size_t AppendText(char* out, size_t size, const char* text) {
    const size_t used = std::strlen(out);
    if (used + 1 >= size) return 0;
    const size_t room = size - used - 1;
    size_t length = std::strlen(text);
    if (length > room) length = room;
    std::memcpy(out + used, text, length);
    out[used + length] = '\0';
    return length;
}

} // namespace

int PerfRowCount(int level) {
    switch (level) {
    case kLevelMinimal:
        return 1;
    case kLevelStandard:
        return 3;
    case kLevelDetailed:
        return 12;
    default:
        return 0;
    }
}

int HudWrapText(const char* list, float max_width, float (*measure)(const char* text, void* ctx), void* ctx,
                char* out, size_t size) {
    if (out == nullptr || size == 0) return 0;
    out[0] = '\0';
    if (list == nullptr || list[0] == '\0') return 0;

    AppendText(out, size, HUD_DEBUG_LEAD);
    int lines = 1;
    bool line_has_item = false;
    size_t line_start = 0;

    const char* cursor = list;
    while (*cursor != '\0') {
        const char* end = std::strstr(cursor, ", ");
        const size_t item_length = end != nullptr ? static_cast<size_t>(end - cursor) : std::strlen(cursor);
        char item[128];
        const size_t copy = item_length < sizeof(item) - 1 ? item_length : sizeof(item) - 1;
        std::memcpy(item, cursor, copy);
        item[copy] = '\0';

        const size_t line_end = std::strlen(out);
        if (line_has_item) AppendText(out, size, ", ");
        AppendText(out, size, item);
        const bool too_wide = line_has_item && measure != nullptr && measure(out + line_start, ctx) > max_width;
        if (too_wide) {
            out[line_end] = '\0';
            AppendText(out, size, "\n");
            line_start = std::strlen(out);
            AppendText(out, size, item);
            lines += 1;
        }
        line_has_item = true;
        cursor = end != nullptr ? end + 2 : cursor + item_length;
    }
    return lines;
}

OverlayPlacement PlaceOverlays(const OverlayPlacementInput& in) {
    OverlayPlacement out{};
    const auto x_for = [&](int corner, float width) {
        return IsLeft(corner) ? in.margin : in.screen_w - in.margin - width;
    };

    if (in.hud_shown) {
        out.hud.w = in.hud_w;
        out.hud.h = in.hud_h;
        out.hud.x = (in.screen_w - in.hud_w) * 0.5f;
        if (out.hud.x < in.margin) out.hud.x = in.margin;
        out.hud.y = in.margin;
    }
    const float hud_bottom = out.hud.y + out.hud.h + in.gap;

    const bool same_corner = in.perf_shown && in.input_shown && in.perf_corner == in.input_corner;
    if (in.perf_shown) {
        out.perf.w = in.perf_w;
        out.perf.h = in.perf_h;
        out.perf.x = x_for(in.perf_corner, in.perf_w);
        out.perf.y = IsTop(in.perf_corner) ? in.margin : in.screen_h - in.margin - in.perf_h;
    }
    if (in.input_shown) {
        out.input.w = in.input_w;
        out.input.h = in.input_h;
        out.input.x = x_for(in.input_corner, in.input_w);
        out.input.y = IsTop(in.input_corner) ? in.margin : in.screen_h - in.margin - in.input_h;
    }

    if (same_corner) {
        if (IsTop(in.perf_corner)) {
            out.perf.y = in.margin;
            out.input.y = out.perf.y + in.perf_h + in.gap;
            if (in.hud_shown && OverlapX(out.perf, out.hud)) {
                out.perf.y = hud_bottom;
                out.input.y = out.perf.y + in.perf_h + in.gap;
            }
        } else {
            out.perf.y = in.screen_h - in.margin - in.perf_h;
            out.input.y = out.perf.y - in.gap - in.input_h;
        }
        return out;
    }

    if (in.perf_shown && IsTop(in.perf_corner) && in.hud_shown && OverlapX(out.perf, out.hud)) {
        out.perf.y = hud_bottom;
    }
    if (in.input_shown && IsTop(in.input_corner) && in.hud_shown && OverlapX(out.input, out.hud)) {
        out.input.y = hud_bottom;
    }
    return out;
}

} // namespace debug_overlay
