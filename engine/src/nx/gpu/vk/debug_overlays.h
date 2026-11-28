// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// The three debug overlays over the game while the engine menu is closed: the
// performance overlay, the input display and the HUD. The drawing is ImGui; the
// placement and the pure rules are in debug_overlays_layout.h. The line format
// helpers are shared with the Debug page.

#pragma once

#include <stddef.h>

#include "imgui.h"
#include "nx/gpu/vk/debug_stats.h"
#include "nx/gpu/vk/imgui_glue.h"

namespace debug_overlay {

// The draw lists of the overlay windows that Draw made in this frame. They stay
// valid until the next frame starts.
struct OverlayDrawLists {
    static constexpr int kMaxCount = 3;
    ImDrawList* lists[kMaxCount];
    int count;
};

// Draws the overlays for one frame after the panel, the menu bar and the
// keyboard window. panel_closed is true only when the panel has finished its
// close. Returns the lists that it drew.
OverlayDrawLists Draw(const ImguiFrame* frame, bool panel_closed);

// Forgets the fades, the last level and the HUD text: a new ImGui context.
void Reset();

// The line format helpers of the Debug page: a value that the machine does not
// tell shows as an em dash.
void DashedInt(char* out, size_t size, int value, const char* format);
void DashedTenths(char* out, size_t size, float value);
void GpuLine(const DebugStats& stats, char* line, size_t size);
void CpuLine(const DebugStats& stats, char* line, size_t size);
void RamLine(const DebugStats& stats, char* line, size_t size);
void VramLine(const DebugStats& stats, char* line, size_t size);
void FrameLine(const DebugStats& stats, char* line, size_t size);

// The frame target of fps_limit's text: the game's pace, a number of frames a
// second, or no line at 0 and Unlimited.
float FrameTargetMs(const char* fps_limit);

} // namespace debug_overlay
