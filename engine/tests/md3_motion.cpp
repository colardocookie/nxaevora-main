// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// Every expected value is computed here. The reference is a bisection on x(u)
// with 60 steps, then y(u); no value is guessed.

#include <cmath>
#include <cstdio>

#include "nx/gpu/vk/md3/md3_motion.h"

namespace {

constexpr double kTolerance = 1e-4;
constexpr int kSteps = 1000;

double AxisAt(double p1, double p2, double u) {
    const double v = 1.0 - u;
    return 3.0 * v * v * u * p1 + 3.0 * v * u * u * p2 + u * u * u;
}

// The slow reference: bisection on x(u) = t for 60 steps, then y(u).
double ReferenceY(const md3::MotionCurve& curve, double t) {
    double low = 0.0;
    double high = 1.0;
    for (int i = 0; i < 60; ++i) {
        const double u = (low + high) * 0.5;
        if (AxisAt(curve.x1, curve.x2, u) < t) {
            low = u;
        } else {
            high = u;
        }
    }
    return AxisAt(curve.y1, curve.y2, (low + high) * 0.5);
}

bool Near(const char* name, double actual, double expected) {
    const double diff = std::fabs(actual - expected);
    if (diff > kTolerance) {
        std::printf("FAIL %s: got %.9f, expected %.9f (tolerance %.1e)\n", name, actual, expected,
                    kTolerance);
        return false;
    }
    return true;
}

bool CheckCurve(const char* name, const md3::MotionCurve& curve) {
    if (!Near(name, md3::CubicBezier(curve, 0.0f), 0.0)) return false;
    if (!Near(name, md3::CubicBezier(curve, 1.0f), 1.0)) return false;

    double previous = -1.0;
    for (int i = 0; i <= kSteps; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(kSteps);
        const double y = md3::CubicBezier(curve, static_cast<float>(t));
        if (y < previous - 1e-5) {
            std::printf("FAIL %s: y decreases at t = %.4f (%.9f after %.9f)\n", name, t, y,
                        previous);
            return false;
        }
        previous = y;
    }

    const double samples[] = {0.1, 0.25, 0.5, 0.75, 0.9};
    for (double t : samples) {
        const double actual = md3::CubicBezier(curve, static_cast<float>(t));
        char label[96];
        std::snprintf(label, sizeof(label), "%s at t = %.2f", name, t);
        if (!Near(label, actual, ReferenceY(curve, t))) return false;
    }
    return true;
}

} // namespace

int main() {
    if (!CheckCurve("standard", md3::kCurveStandard)) return 1;
    if (!CheckCurve("emphasized decelerate", md3::kCurveEmphasizedDecelerate)) return 1;
    if (!CheckCurve("emphasized accelerate", md3::kCurveEmphasizedAccelerate)) return 1;

    const md3::MotionCurve linear{0.0f, 0.0f, 1.0f, 1.0f};
    for (int i = 0; i <= kSteps; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(kSteps);
        char label[64];
        std::snprintf(label, sizeof(label), "linear at t = %.4f", t);
        if (!Near(label, md3::CubicBezier(linear, static_cast<float>(t)), t)) return 1;
    }

    std::printf("md3_motion_test: all cases passed\n");
    return 0;
}
