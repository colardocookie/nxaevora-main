// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// The eased tweens of the mockup. CubicBezier needs no ImGui; Tween reads the
// frame time and keeps one state for each id.

#include "nx/gpu/vk/md3/md3_motion.h"

#include <cmath>

#ifndef MD3_MOTION_TEST

#include <unordered_map>

#include "imgui.h"

#endif

namespace md3 {
namespace {

constexpr float kSolveEpsilon = 1e-6f;
constexpr float kMinSlope = 1e-6f;
constexpr int kNewtonSteps = 8;
constexpr int kBisectionSteps = 40;

// One axis of the CSS cubic-bezier polynomial at u in 0..1.
float AxisAt(float p1, float p2, float u) {
    const float v = 1.0f - u;
    return 3.0f * v * v * u * p1 + 3.0f * v * u * u * p2 + u * u * u;
}

// The derivative of AxisAt with respect to u.
float SlopeAt(float p1, float p2, float u) {
    const float v = 1.0f - u;
    return 3.0f * v * v * p1 + 6.0f * v * u * (p2 - p1) + 3.0f * u * u * (1.0f - p2);
}

// Solves x(u) = t: Newton steps first, then bisection on the same bracket.
float SolveU(float x1, float x2, float t) {
    float u = t;
    for (int i = 0; i < kNewtonSteps; ++i) {
        const float error = AxisAt(x1, x2, u) - t;
        if (std::fabs(error) < kSolveEpsilon) return u;
        const float slope = SlopeAt(x1, x2, u);
        if (std::fabs(slope) < kMinSlope) break;
        u -= error / slope;
        if (u < 0.0f || u > 1.0f) break;
    }
    float low = 0.0f;
    float high = 1.0f;
    u = t;
    for (int i = 0; i < kBisectionSteps; ++i) {
        const float x = AxisAt(x1, x2, u);
        if (std::fabs(x - t) < kSolveEpsilon) break;
        if (x < t) {
            low = u;
        } else {
            high = u;
        }
        u = (low + high) * 0.5f;
    }
    return u;
}

} // namespace

float CubicBezier(float x1, float y1, float x2, float y2, float t) {
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;
    return AxisAt(y1, y2, SolveU(x1, x2, t));
}

#ifndef MD3_MOTION_TEST

namespace {

struct TweenState {
    float value;
    float target;
    float start;
    double start_time;
    int duration_ms;
    MotionCurve curve;
    bool initialized;
};

std::unordered_map<TweenId, TweenState> g_tweens;
bool g_reduce_motion = false;

} // namespace

float Tween(TweenId id, float target, int duration_ms, const MotionCurve& curve) {
    TweenState& state = g_tweens[id];
    if (!state.initialized || g_reduce_motion) {
        state.value = target;
        state.target = target;
        state.start = target;
        state.start_time = ImGui::GetTime();
        state.duration_ms = duration_ms;
        state.curve = curve;
        state.initialized = true;
        return target;
    }
    if (target != state.target) {
        // Start from the current value, so a quick toggle back does not jump.
        state.start = state.value;
        state.target = target;
        state.start_time = ImGui::GetTime();
        state.duration_ms = duration_ms;
        state.curve = curve;
    }
    if (state.duration_ms <= 0) {
        state.value = state.target;
        return state.value;
    }
    const double elapsed = ImGui::GetTime() - state.start_time;
    float progress =
        static_cast<float>(elapsed * 1000.0 / static_cast<double>(state.duration_ms));
    if (progress < 0.0f) progress = 0.0f;
    if (progress >= 1.0f) {
        state.value = state.target;
    } else {
        state.value = state.start + (state.target - state.start) * CubicBezier(state.curve, progress);
    }
    return state.value;
}

void ResetTween(TweenId id, float value) {
    TweenState& state = g_tweens[id];
    state.value = value;
    state.target = value;
    state.start = value;
    state.start_time = ImGui::GetTime();
    state.duration_ms = 0;
    state.curve = kCurveStandard;
    state.initialized = true;
}

void SetReduceMotion(bool reduce) { g_reduce_motion = reduce; }

bool ReduceMotion() { return g_reduce_motion; }

#endif // MD3_MOTION_TEST

} // namespace md3
