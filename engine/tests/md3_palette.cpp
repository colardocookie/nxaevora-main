// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// Expected values are copied from the test files of material-color-utilities at
// commit 0f07b68759989d4e065b806229daba6ad000695d (Apache-2.0). The file and the
// line of each case stand next to it. The scheme case uses only properties that
// follow from the role table, measured with HctFromInt of the result.

#include <cmath>
#include <cstdint>
#include <cstdio>

#include "nx/gpu/vk/md3/md3_palette.h"

namespace {

constexpr double CAM16_TOLERANCE = 0.0005;
constexpr double HCT_TOLERANCE = 0.005;
constexpr double SCHEME_TONE_TOLERANCE = 0.5;

bool Near(const char* name, double actual, double expected, double tolerance) {
    const double diff = std::fabs(actual - expected);
    if (diff > tolerance) {
        std::printf("FAIL %s: got %.6f, expected %.6f (tolerance %.6f)\n", name, actual, expected,
                    tolerance);
        return false;
    }
    return true;
}

bool NearArgB(const char* name, uint32_t actual, uint32_t expected) {
    if (actual != expected) {
        std::printf("FAIL %s: got 0x%08X, expected 0x%08X\n", name, actual, expected);
        return false;
    }
    return true;
}

} // namespace

int main() {
    // hct_test.ts:36-38
    const md3::Cam16 red = md3::Cam16FromInt(0xFFFF0000u);
    if (!Near("cam16 red hue", red.hue, 27.408, CAM16_TOLERANCE)) return 1;
    if (!Near("cam16 red chroma", red.chroma, 113.358, CAM16_TOLERANCE)) return 1;
    if (!Near("cam16 red j", red.j, 46.445, CAM16_TOLERANCE)) return 1;

    // hct_test.ts:58-60
    const md3::Cam16 blue = md3::Cam16FromInt(0xFF0000FFu);
    if (!Near("cam16 blue hue", blue.hue, 282.788, CAM16_TOLERANCE)) return 1;
    if (!Near("cam16 blue chroma", blue.chroma, 87.231, CAM16_TOLERANCE)) return 1;
    if (!Near("cam16 blue j", blue.j, 25.466, CAM16_TOLERANCE)) return 1;

    // hct_test.ts:112-114
    const md3::Hct green = md3::HctFromInt(0xFF00FF00u);
    if (!Near("hct green hue", green.hue, 142.139, HCT_TOLERANCE)) return 1;
    if (!Near("hct green chroma", green.chroma, 108.410, HCT_TOLERANCE)) return 1;
    if (!Near("hct green tone", green.tone, 87.737, HCT_TOLERANCE)) return 1;

    // hct_test.ts:119-121
    const md3::Hct blue_hct = md3::HctFromInt(0xFF0000FFu);
    if (!Near("hct blue hue", blue_hct.hue, 282.788, HCT_TOLERANCE)) return 1;
    if (!Near("hct blue chroma", blue_hct.chroma, 87.230, HCT_TOLERANCE)) return 1;
    if (!Near("hct blue tone", blue_hct.tone, 32.302, HCT_TOLERANCE)) return 1;

    // hct_test.ts:125-128
    const md3::Hct from_values = md3::HctFrom(282.788, 87.230, 90.0);
    if (!Near("hct blue tone 90 hue", from_values.hue, 282.239, HCT_TOLERANCE)) return 1;
    if (!Near("hct blue tone 90 chroma", from_values.chroma, 19.144, HCT_TOLERANCE)) return 1;
    if (!Near("hct blue tone 90 tone", from_values.tone, 90.035, HCT_TOLERANCE)) return 1;

    // palettes_test.ts:29 (tone 100), :32 (tone 80), :35 (tone 50), :40 (tone 0)
    const md3::TonalPalette palette = md3::TonalPalette::FromInt(0xFF0000FFu);
    if (!NearArgB("tonal blue tone 100", palette.Tone(100), 0xFFFFFFFFu)) return 1;
    if (!NearArgB("tonal blue tone 80", palette.Tone(80), 0xFFBEC2FFu)) return 1;
    if (!NearArgB("tonal blue tone 50", palette.Tone(50), 0xFF5A64FFu)) return 1;
    if (!NearArgB("tonal blue tone 0", palette.Tone(0), 0xFF000000u)) return 1;

    // Decision 7: the seed 0xF97316 maps primary to A1-80, surface to N1-6 and
    // onSurface to N1-90, so their tones are 80, 6 and 90.
    const md3::Scheme scheme = md3::SchemeFromSeed(0xFFF97316u);
    if (!Near("scheme primary tone", md3::HctFromInt(scheme.primary).tone, 80.0,
              SCHEME_TONE_TOLERANCE)) return 1;
    if (!Near("scheme surface tone", md3::HctFromInt(scheme.surface).tone, 6.0,
              SCHEME_TONE_TOLERANCE)) return 1;
    if (!Near("scheme onSurface tone", md3::HctFromInt(scheme.on_surface).tone, 90.0,
              SCHEME_TONE_TOLERANCE)) return 1;

    std::printf("md3_palette_test: all cases passed\n");
    return 0;
}
