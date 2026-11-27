// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// The HCT color space, CAM16 and the tonal palette, ported from
// material-foundation/material-color-utilities (Apache-2.0). The source of
// md3_palette.cpp names the commit.

#pragma once

#include <cstdint>

namespace md3 {

// A packed color: 0xAARRGGBB, the argument form of the source library.
uint32_t ArgBFromRgb(unsigned red, unsigned green, unsigned blue);
unsigned AlphaFromArgb(uint32_t argb);
unsigned RedFromArgb(uint32_t argb);
unsigned GreenFromArgb(uint32_t argb);
unsigned BlueFromArgb(uint32_t argb);

// CAM16 appearance coordinates in the default viewing conditions.
struct Cam16 {
    double hue;    // degrees, 0 <= hue < 360
    double chroma; // colorfulness, like saturation but perceptually accurate
    double j;      // lightness, 0..100
};

Cam16 Cam16FromInt(uint32_t argb);

// HCT: CAM16 hue and chroma with L* tone. The tone is measured from 0 to 100.
struct Hct {
    double hue;
    double chroma;
    double tone;
    uint32_t argb;
};

Hct HctFromInt(uint32_t argb);
Hct HctFrom(double hue, double chroma, double tone);

// A color that is constant in hue and chroma and varies in tone.
class TonalPalette {
public:
    TonalPalette(double hue, double chroma);

    static TonalPalette FromInt(uint32_t argb);

    // The ARGB color at the given tone, 0 <= tone <= 100.
    uint32_t Tone(double tone) const;

    double hue() const { return hue_; }
    double chroma() const { return chroma_; }

private:
    double hue_;
    double chroma_;
};

// The 25 roles of the mockup's dark scheme. Tone numbers follow the old
// core palette of Scheme.dark, named in the task brief.
struct Scheme {
    uint32_t primary;
    uint32_t on_primary;
    uint32_t primary_container;
    uint32_t on_primary_container;
    uint32_t secondary;
    uint32_t on_secondary;
    uint32_t secondary_container;
    uint32_t on_secondary_container;
    uint32_t tertiary;
    uint32_t on_tertiary;
    uint32_t tertiary_container;
    uint32_t on_tertiary_container;
    uint32_t error;
    uint32_t on_error;
    uint32_t surface;
    uint32_t on_surface;
    uint32_t surface_container_lowest;
    uint32_t surface_container_low;
    uint32_t surface_container;
    uint32_t surface_container_high;
    uint32_t surface_container_highest;
    uint32_t surface_variant;
    uint32_t on_surface_variant;
    uint32_t outline;
    uint32_t outline_variant;
};

// The dark scheme of the seed: A1 = (hue, max(48, chroma)), A2 = (hue, 16),
// A3 = (hue + 60, 24), N1 = (hue, 4), N2 = (hue, 8), Error = (25, 84).
Scheme SchemeFromSeed(uint32_t seed);

} // namespace md3
