// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// The MD3 motion tokens and the eased tweens of the mockup. The curves and the
// tokens need no ImGui, so the motion test links them without it.

#pragma once

#include <cstdint>

namespace md3 {

// A CSS cubic-bezier control pair. The curve runs from (0,0) to (1,1).
struct MotionCurve {
    float x1;
    float y1;
    float x2;
    float y2;
};

// The MD3 motion tokens: the three curves and the four durations.
inline constexpr MotionCurve kCurveStandard{0.2f, 0.0f, 0.0f, 1.0f};
inline constexpr MotionCurve kCurveEmphasizedDecelerate{0.05f, 0.7f, 0.1f, 1.0f};
inline constexpr MotionCurve kCurveEmphasizedAccelerate{0.3f, 0.0f, 0.8f, 0.15f};

inline constexpr int kDurationShortMs = 100;
inline constexpr int kDurationShortLongMs = 200;
inline constexpr int kDurationMediumMs = 300;
inline constexpr int kDurationMediumLongMs = 400;

// An ImGuiID is an unsigned int (imgui.h:170). This header names the type so
// that the motion test needs no ImGui.
using TweenId = unsigned int;

// y of the CSS cubic-bezier(x1,y1,x2,y2) curve at progress t in 0..1. t <= 0
// gives 0 and t >= 1 gives 1.
float CubicBezier(float x1, float y1, float x2, float y2, float t);

// The same curve with its token values.
inline float CubicBezier(const MotionCurve& curve, float t) {
    return CubicBezier(curve.x1, curve.y1, curve.x2, curve.y2, t);
}

// Moves the value of id toward target over duration_ms. The first call of an id
// returns the target. A target change starts from the current value.
float Tween(TweenId id, float target, int duration_ms, const MotionCurve& curve);

// Sets the value and the target of id at once, for example on a page change.
void ResetTween(TweenId id, float value);

// The global "reduce motion" flag. When it is on, every Tween returns its
// target at once.
void SetReduceMotion(bool reduce);
bool ReduceMotion();

} // namespace md3
