// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// The UI scale rule: 1.25 x max(display scale, height / 1080), clamped to
// 1.25..3.75, and a display scale that is not positive counts as 1.0. Every
// expected value is computed from that rule.

#include <cmath>
#include <cstdio>

#include "nx/gpu/vk/md3/md3_scale.h"

namespace {

constexpr double kTolerance = 1e-4;

struct Case {
    const char* name;
    int pixel_height;
    float display_scale;
    double expected;
};

const Case kCases[] = {
    {"720p at scale 1.0", 720, 1.0f, 1.25},
    {"1080p at scale 1.0", 1080, 1.0f, 1.25},
    {"2160p at scale 1.0", 2160, 1.0f, 2.5},
    {"1080p at scale 1.5", 1080, 1.5f, 1.875},
    {"1440p at scale 1.0", 1440, 1.0f, 1.6666666666666667},
    {"8640p at scale 1.0", 8640, 1.0f, 3.75},
    {"1080p at scale 0.0", 1080, 0.0f, 1.25},
    {"1080p at scale -2.0", 1080, -2.0f, 1.25},
    // NaN is not positive: without the guard it passes max() and clamp() as NaN.
    {"1080p at scale NaN", 1080, std::nanf(""), 1.25},
};

bool Near(const char* name, double actual, double expected) {
    const double diff = std::fabs(actual - expected);
    if (!(diff <= kTolerance)) {  // a NaN result fails too
        std::printf("FAIL %s: got %.6f, expected %.6f (tolerance %.1e)\n", name, actual, expected,
                    kTolerance);
        return false;
    }
    return true;
}

bool CheckConstants() {
    if (!Near("kUiSizeFactor", md3::kUiSizeFactor, 1.25)) return false;
    if (!Near("kMinUiScale", md3::kMinUiScale, 1.25)) return false;
    if (!Near("kMaxUiScale", md3::kMaxUiScale, 3.75)) return false;
    if (md3::kReferenceHeightPx != 1080) {
        std::printf("FAIL kReferenceHeightPx: got %d, expected 1080\n", md3::kReferenceHeightPx);
        return false;
    }
    return true;
}

} // namespace

int main() {
    if (!CheckConstants()) return 1;
    for (const Case& test : kCases) {
        if (!Near(test.name, md3::UiScale(test.pixel_height, test.display_scale), test.expected)) {
            return 1;
        }
    }

    std::printf("md3_scale_test: all cases passed\n");
    return 0;
}
