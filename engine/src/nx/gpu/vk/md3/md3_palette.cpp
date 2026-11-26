// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
//
// The HCT color space, CAM16 and the tonal palette. Ported from
// material-foundation/material-color-utilities, commit
// 0f07b68759989d4e065b806229daba6ad000695d. Files read:
// typescript/utils/math_utils.ts, typescript/utils/color_utils.ts,
// typescript/hct/viewing_conditions.ts, typescript/hct/cam16.ts,
// typescript/hct/hct.ts, typescript/hct/hct_solver.ts and
// typescript/palettes/tonal_palette.ts.
//
// Copyright 2021 Google LLC
// Licensed under the Apache License, Version 2.0 (the "License"); you may not
// use this file except in compliance with the License. You may obtain a copy of
// the License at http://www.apache.org/licenses/LICENSE-2.0
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#include "nx/gpu/vk/md3/md3_palette.h"

#include <algorithm>
#include <cmath>

namespace md3 {
namespace {

constexpr double kPi = 3.14159265358979323846;

constexpr double kSrgbToXyz[3][3] = {
    {0.41233895, 0.35762064, 0.18051042},
    {0.2126, 0.7152, 0.0722},
    {0.01932141, 0.11916382, 0.95034478},
};

constexpr double kWhitePointD65[3] = {95.047, 100.0, 108.883};
constexpr double kYFromLinrgb[3] = {0.2126, 0.7152, 0.0722};

double Signum(double value) {
    if (value < 0.0) return -1.0;
    if (value == 0.0) return 0.0;
    return 1.0;
}

double Lerp(double start, double stop, double amount) {
    return (1.0 - amount) * start + amount * stop;
}

double SanitizeDegreesDouble(double degrees) {
    degrees = std::fmod(degrees, 360.0);
    if (degrees < 0.0) degrees += 360.0;
    return degrees;
}

// out[i] = dot(row, matrix row i), the ordering of matrixMultiply in the source.
void MultiplyRow(const double row[3], const double matrix[3][3], double out[3]) {
    for (int i = 0; i < 3; ++i) {
        out[i] = row[0] * matrix[i][0] + row[1] * matrix[i][1] + row[2] * matrix[i][2];
    }
}

double Linearized(double rgb_component) {
    const double normalized = rgb_component / 255.0;
    if (normalized <= 0.040449936) return normalized / 12.92 * 100.0;
    return std::pow((normalized + 0.055) / 1.055, 2.4) * 100.0;
}

int Delinearized(double rgb_component) {
    const double normalized = rgb_component / 100.0;
    double delinearized = 0.0;
    if (normalized <= 0.0031308) {
        delinearized = normalized * 12.92;
    } else {
        delinearized = 1.055 * std::pow(normalized, 1.0 / 2.4) - 0.055;
    }
    const double scaled = std::round(delinearized * 255.0);
    return static_cast<int>(std::clamp(scaled, 0.0, 255.0));
}

// The unclamped delinearization, used by the HCT solver's plane search.
double TrueDelinearized(double rgb_component) {
    const double normalized = rgb_component / 100.0;
    if (normalized <= 0.0031308) return normalized * 12.92 * 255.0;
    return (1.055 * std::pow(normalized, 1.0 / 2.4) - 0.055) * 255.0;
}

double LabF(double t) {
    constexpr double kE = 216.0 / 24389.0;
    constexpr double kKappa = 24389.0 / 27.0;
    if (t > kE) return std::pow(t, 1.0 / 3.0);
    return (kKappa * t + 16.0) / 116.0;
}

double LabInvf(double ft) {
    constexpr double kE = 216.0 / 24389.0;
    constexpr double kKappa = 24389.0 / 27.0;
    const double ft3 = ft * ft * ft;
    if (ft3 > kE) return ft3;
    return (116.0 * ft - 16.0) / kKappa;
}

double YFromLstar(double lstar) {
    return 100.0 * LabInvf((lstar + 16.0) / 116.0);
}

double LstarFromY(double y) {
    return LabF(y / 100.0) * 116.0 - 16.0;
}

void XyzFromArgb(uint32_t argb, double out[3]) {
    const double row[3] = {Linearized(RedFromArgb(argb)), Linearized(GreenFromArgb(argb)),
                           Linearized(BlueFromArgb(argb))};
    MultiplyRow(row, kSrgbToXyz, out);
}

double LstarFromArgb(uint32_t argb) {
    double xyz[3];
    XyzFromArgb(argb, xyz);
    return LstarFromY(xyz[1]);
}

uint32_t ArgBFromLinrgb(const double linrgb[3]) {
    return ArgBFromRgb(static_cast<unsigned>(Delinearized(linrgb[0])),
                       static_cast<unsigned>(Delinearized(linrgb[1])),
                       static_cast<unsigned>(Delinearized(linrgb[2])));
}

uint32_t ArgBFromLstar(double lstar) {
    const double component = static_cast<double>(Delinearized(YFromLstar(lstar)));
    return ArgBFromRgb(static_cast<unsigned>(component), static_cast<unsigned>(component),
                       static_cast<unsigned>(component));
}

struct ViewingConditions {
    double n;
    double aw;
    double nbb;
    double ncb;
    double c;
    double nc;
    double rgb_d[3];
    double fl;
    double fl_root;
    double z;
};

ViewingConditions MakeViewingConditions() {
    const double adapting_luminance = (200.0 / kPi) * YFromLstar(50.0) / 100.0;
    const double background_lstar = 50.0;
    const double surround = 2.0;
    const double xyz[3] = {kWhitePointD65[0], kWhitePointD65[1], kWhitePointD65[2]};

    const double r_w = xyz[0] * 0.401288 + xyz[1] * 0.650173 + xyz[2] * -0.051461;
    const double g_w = xyz[0] * -0.250268 + xyz[1] * 1.204414 + xyz[2] * 0.045854;
    const double b_w = xyz[0] * -0.002079 + xyz[1] * 0.048952 + xyz[2] * 0.953127;

    const double f = 0.8 + surround / 10.0;
    const double c = f >= 0.9 ? Lerp(0.59, 0.69, (f - 0.9) * 10.0)
                              : Lerp(0.525, 0.59, (f - 0.8) * 10.0);
    double d = f * (1.0 - (1.0 / 3.6) * std::exp((-adapting_luminance - 42.0) / 92.0));
    d = std::clamp(d, 0.0, 1.0);
    const double nc = f;

    ViewingConditions vc{};
    vc.rgb_d[0] = d * (100.0 / r_w) + 1.0 - d;
    vc.rgb_d[1] = d * (100.0 / g_w) + 1.0 - d;
    vc.rgb_d[2] = d * (100.0 / b_w) + 1.0 - d;

    const double k = 1.0 / (5.0 * adapting_luminance + 1.0);
    const double k4 = k * k * k * k;
    const double k4_f = 1.0 - k4;
    const double fl =
        k4 * adapting_luminance + 0.1 * k4_f * k4_f * std::cbrt(5.0 * adapting_luminance);
    const double n = YFromLstar(background_lstar) / kWhitePointD65[1];
    const double z = 1.48 + std::sqrt(n);
    const double nbb = 0.725 / std::pow(n, 0.2);
    const double ncb = nbb;
    const double rgb_a_factors[3] = {
        std::pow((fl * vc.rgb_d[0] * r_w) / 100.0, 0.42),
        std::pow((fl * vc.rgb_d[1] * g_w) / 100.0, 0.42),
        std::pow((fl * vc.rgb_d[2] * b_w) / 100.0, 0.42),
    };
    const double rgb_a[3] = {
        (400.0 * rgb_a_factors[0]) / (rgb_a_factors[0] + 27.13),
        (400.0 * rgb_a_factors[1]) / (rgb_a_factors[1] + 27.13),
        (400.0 * rgb_a_factors[2]) / (rgb_a_factors[2] + 27.13),
    };
    const double aw = (2.0 * rgb_a[0] + rgb_a[1] + 0.05 * rgb_a[2]) * nbb;

    vc.n = n;
    vc.aw = aw;
    vc.nbb = nbb;
    vc.ncb = ncb;
    vc.c = c;
    vc.nc = nc;
    vc.fl = fl;
    vc.fl_root = std::pow(fl, 0.25);
    vc.z = z;
    return vc;
}

const ViewingConditions& DefaultViewingConditions() {
    static const ViewingConditions conditions = MakeViewingConditions();
    return conditions;
}

struct Cam16Values {
    double hue;
    double chroma;
    double j;
};

Cam16Values Cam16InViewingConditions(uint32_t argb, const ViewingConditions& vc) {
    const double red_l = Linearized(RedFromArgb(argb));
    const double green_l = Linearized(GreenFromArgb(argb));
    const double blue_l = Linearized(BlueFromArgb(argb));

    const double x = 0.41233895 * red_l + 0.35762064 * green_l + 0.18051042 * blue_l;
    const double y = 0.2126 * red_l + 0.7152 * green_l + 0.0722 * blue_l;
    const double z = 0.01932141 * red_l + 0.11916382 * green_l + 0.95034478 * blue_l;

    const double r_c = 0.401288 * x + 0.650173 * y - 0.051461 * z;
    const double g_c = -0.250268 * x + 1.204414 * y + 0.045854 * z;
    const double b_c = -0.002079 * x + 0.048952 * y + 0.953127 * z;

    const double r_d = vc.rgb_d[0] * r_c;
    const double g_d = vc.rgb_d[1] * g_c;
    const double b_d = vc.rgb_d[2] * b_c;

    const double r_af = std::pow((vc.fl * std::fabs(r_d)) / 100.0, 0.42);
    const double g_af = std::pow((vc.fl * std::fabs(g_d)) / 100.0, 0.42);
    const double b_af = std::pow((vc.fl * std::fabs(b_d)) / 100.0, 0.42);

    const double r_a = (Signum(r_d) * 400.0 * r_af) / (r_af + 27.13);
    const double g_a = (Signum(g_d) * 400.0 * g_af) / (g_af + 27.13);
    const double b_a = (Signum(b_d) * 400.0 * b_af) / (b_af + 27.13);

    const double a = (11.0 * r_a + -12.0 * g_a + b_a) / 11.0;
    const double b = (r_a + g_a - 2.0 * b_a) / 9.0;
    const double u = (20.0 * r_a + 20.0 * g_a + 21.0 * b_a) / 20.0;
    const double p2 = (40.0 * r_a + 20.0 * g_a + b_a) / 20.0;

    const double atan_degrees = std::atan2(b, a) * 180.0 / kPi;
    const double hue = SanitizeDegreesDouble(atan_degrees);
    const double hue_radians = hue * kPi / 180.0;

    const double ac = p2 * vc.nbb;
    const double j = 100.0 * std::pow(ac / vc.aw, vc.c * vc.z);
    const double hue_prime = hue < 20.14 ? hue + 360.0 : hue;
    const double e_hue = 0.25 * (std::cos(hue_prime * kPi / 180.0 + 2.0) + 3.8);
    const double p1 = (50000.0 / 13.0) * e_hue * vc.nc * vc.ncb;
    const double t = (p1 * std::sqrt(a * a + b * b)) / (u + 0.305);
    const double alpha = std::pow(t, 0.9) * std::pow(1.64 - std::pow(0.29, vc.n), 0.73);
    const double chroma = alpha * std::sqrt(j / 100.0);
    (void)hue_radians;

    return Cam16Values{hue, chroma, j};
}

// The linear-RGB value of the boundary between the 8-bit codes index and
// index+1. The published CRITICAL_PLANES table has these 255 values.
double CriticalPlane(int index) {
    const int clamped = std::clamp(index, 0, 254);
    const double normalized = (clamped + 0.5) / 255.0;
    if (normalized <= 0.040449936) return normalized / 12.92 * 100.0;
    return std::pow((normalized + 0.055) / 1.055, 2.4) * 100.0;
}

double SanitizeRadians(double angle) { return std::fmod(angle + kPi * 8.0, kPi * 2.0); }

double ChromaticAdaptation(double component) {
    const double af = std::pow(std::fabs(component), 0.42);
    return Signum(component) * 400.0 * af / (af + 27.13);
}

double HueOf(const double linrgb[3]) {
    constexpr double kScaledDiscountFromLinrgb[3][3] = {
        {0.001200833568784504, 0.002389694492170889, 0.0002795742885861124},
        {0.0005891086651375999, 0.0029785502573438758, 0.0003270666104008398},
        {0.00010146692491640572, 0.0005364214359186694, 0.0032979401770712076},
    };
    double scaled_discount[3];
    MultiplyRow(linrgb, kScaledDiscountFromLinrgb, scaled_discount);
    const double r_a = ChromaticAdaptation(scaled_discount[0]);
    const double g_a = ChromaticAdaptation(scaled_discount[1]);
    const double b_a = ChromaticAdaptation(scaled_discount[2]);
    const double a = (11.0 * r_a + -12.0 * g_a + b_a) / 11.0;
    const double b = (r_a + g_a - 2.0 * b_a) / 9.0;
    return std::atan2(b, a);
}

bool AreInCyclicOrder(double a, double b, double c) {
    const double delta_ab = SanitizeRadians(b - a);
    const double delta_ac = SanitizeRadians(c - a);
    return delta_ab < delta_ac;
}

double Intercept(double source, double mid, double target) {
    return (mid - source) / (target - source);
}

void LerpPoint(const double source[3], double t, const double target[3], double out[3]) {
    for (int i = 0; i < 3; ++i) out[i] = source[i] + (target[i] - source[i]) * t;
}

void SetCoordinate(const double source[3], double coordinate, const double target[3], int axis,
                   double out[3]) {
    const double t = Intercept(source[axis], coordinate, target[axis]);
    LerpPoint(source, t, target, out);
}

bool IsBounded(double value) { return value >= 0.0 && value <= 100.0; }

// Returns false when the vertex lies outside the RGB cube.
bool NthVertex(double y, int n, double out[3]) {
    const double k_r = kYFromLinrgb[0];
    const double k_g = kYFromLinrgb[1];
    const double k_b = kYFromLinrgb[2];
    const double coord_a = n % 4 <= 1 ? 0.0 : 100.0;
    const double coord_b = n % 2 == 0 ? 0.0 : 100.0;
    if (n < 4) {
        const double g = coord_a;
        const double b = coord_b;
        const double r = (y - g * k_g - b * k_b) / k_r;
        out[0] = r;
        out[1] = g;
        out[2] = b;
        return IsBounded(r);
    }
    if (n < 8) {
        const double b = coord_a;
        const double r = coord_b;
        const double g = (y - r * k_r - b * k_b) / k_g;
        out[0] = r;
        out[1] = g;
        out[2] = b;
        return IsBounded(g);
    }
    const double r = coord_a;
    const double g = coord_b;
    const double b = (y - r * k_r - g * k_g) / k_b;
    out[0] = r;
    out[1] = g;
    out[2] = b;
    return IsBounded(b);
}

void BisectToSegment(double y, double target_hue, double left[3], double right[3]) {
    double left_hue = 0.0;
    double right_hue = 0.0;
    bool initialized = false;
    bool uncut = true;
    for (int n = 0; n < 12; ++n) {
        double mid[3];
        if (!NthVertex(y, n, mid)) continue;
        const double mid_hue = HueOf(mid);
        if (!initialized) {
            for (int i = 0; i < 3; ++i) left[i] = right[i] = mid[i];
            left_hue = right_hue = mid_hue;
            initialized = true;
            continue;
        }
        if (uncut || AreInCyclicOrder(left_hue, mid_hue, right_hue)) {
            uncut = false;
            if (AreInCyclicOrder(left_hue, target_hue, mid_hue)) {
                for (int i = 0; i < 3; ++i) right[i] = mid[i];
                right_hue = mid_hue;
            } else {
                for (int i = 0; i < 3; ++i) left[i] = mid[i];
                left_hue = mid_hue;
            }
        }
    }
}

void Midpoint(const double a[3], const double b[3], double out[3]) {
    for (int i = 0; i < 3; ++i) out[i] = (a[i] + b[i]) / 2.0;
}

int CriticalPlaneBelow(double x) { return static_cast<int>(std::floor(x - 0.5)); }
int CriticalPlaneAbove(double x) { return static_cast<int>(std::ceil(x - 0.5)); }

void BisectToLimit(double y, double target_hue, double out[3]) {
    double left[3];
    double right[3];
    BisectToSegment(y, target_hue, left, right);
    double left_hue = HueOf(left);
    for (int axis = 0; axis < 3; ++axis) {
        if (left[axis] == right[axis]) continue;
        int l_plane = -1;
        int r_plane = 255;
        if (left[axis] < right[axis]) {
            l_plane = CriticalPlaneBelow(TrueDelinearized(left[axis]));
            r_plane = CriticalPlaneAbove(TrueDelinearized(right[axis]));
        } else {
            l_plane = CriticalPlaneAbove(TrueDelinearized(left[axis]));
            r_plane = CriticalPlaneBelow(TrueDelinearized(right[axis]));
        }
        for (int i = 0; i < 8; ++i) {
            if (std::abs(r_plane - l_plane) <= 1) break;
            const int m_plane = static_cast<int>(std::floor((l_plane + r_plane) / 2.0));
            const double mid_plane_coordinate = CriticalPlane(m_plane);
            double mid[3];
            SetCoordinate(left, mid_plane_coordinate, right, axis, mid);
            const double mid_hue = HueOf(mid);
            if (AreInCyclicOrder(left_hue, target_hue, mid_hue)) {
                for (int k = 0; k < 3; ++k) right[k] = mid[k];
                r_plane = m_plane;
            } else {
                for (int k = 0; k < 3; ++k) left[k] = mid[k];
                left_hue = mid_hue;
                l_plane = m_plane;
            }
        }
    }
    Midpoint(left, right, out);
}

double InverseChromaticAdaptation(double adapted) {
    const double adapted_abs = std::fabs(adapted);
    const double denominator = 400.0 - adapted_abs;
    if (denominator <= 0.0) return 0.0;
    const double base = std::max(0.0, 27.13 * adapted_abs / denominator);
    return Signum(adapted) * std::pow(base, 1.0 / 0.42);
}

// Returns the ARGB result, or 0 when the Newton iteration leaves the cube.
uint32_t FindResultByJ(double hue_radians, double chroma, double y) {
    const ViewingConditions& vc = DefaultViewingConditions();
    double j = std::sqrt(y) * 11.0;
    const double t_inner_coeff = 1.0 / std::pow(1.64 - std::pow(0.29, vc.n), 0.73);
    const double e_hue = 0.25 * (std::cos(hue_radians + 2.0) + 3.8);
    const double p1 = e_hue * (50000.0 / 13.0) * vc.nc * vc.ncb;
    const double h_sin = std::sin(hue_radians);
    const double h_cos = std::cos(hue_radians);
    for (int iteration = 0; iteration < 5; ++iteration) {
        const double j_normalized = j / 100.0;
        const double alpha = chroma == 0.0 || j == 0.0 ? 0.0 : chroma / std::sqrt(j_normalized);
        const double t = std::pow(alpha * t_inner_coeff, 1.0 / 0.9);
        const double ac = vc.aw * std::pow(j_normalized, 1.0 / vc.c / vc.z);
        const double p2 = ac / vc.nbb;
        const double gamma =
            23.0 * (p2 + 0.305) * t / (23.0 * p1 + 11.0 * t * h_cos + 108.0 * t * h_sin);
        const double a = gamma * h_cos;
        const double b = gamma * h_sin;
        const double r_a = (460.0 * p2 + 451.0 * a + 288.0 * b) / 1403.0;
        const double g_a = (460.0 * p2 - 891.0 * a - 261.0 * b) / 1403.0;
        const double b_a = (460.0 * p2 - 220.0 * a - 6300.0 * b) / 1403.0;
        const double r_c_scaled = InverseChromaticAdaptation(r_a);
        const double g_c_scaled = InverseChromaticAdaptation(g_a);
        const double b_c_scaled = InverseChromaticAdaptation(b_a);

        constexpr double kLinrgbFromScaledDiscount[3][3] = {
            {1373.2198709594231, -1100.4251190754821, -7.278681089101213},
            {-271.815969077903, 559.6580465940733, -32.46047482791194},
            {1.9622899599665666, -57.173814538844006, 308.7233197812385},
        };
        const double row[3] = {r_c_scaled, g_c_scaled, b_c_scaled};
        double linrgb[3];
        MultiplyRow(row, kLinrgbFromScaledDiscount, linrgb);

        if (linrgb[0] < 0.0 || linrgb[1] < 0.0 || linrgb[2] < 0.0) return 0;
        const double fnj =
            kYFromLinrgb[0] * linrgb[0] + kYFromLinrgb[1] * linrgb[1] + kYFromLinrgb[2] * linrgb[2];
        if (fnj <= 0.0) return 0;
        if (iteration == 4 || std::fabs(fnj - y) < 0.002) {
            if (linrgb[0] > 100.01 || linrgb[1] > 100.01 || linrgb[2] > 100.01) return 0;
            return ArgBFromLinrgb(linrgb);
        }
        j = j - (fnj - y) * j / (2.0 * fnj);
    }
    return 0;
}

uint32_t SolveToInt(double hue_degrees, double chroma, double lstar) {
    if (chroma < 0.0001 || lstar < 0.0001 || lstar > 99.9999) return ArgBFromLstar(lstar);
    hue_degrees = SanitizeDegreesDouble(hue_degrees);
    const double hue_radians = hue_degrees / 180.0 * kPi;
    const double y = YFromLstar(lstar);
    const uint32_t exact_answer = FindResultByJ(hue_radians, chroma, y);
    if (exact_answer != 0) return exact_answer;
    double linrgb[3];
    BisectToLimit(y, hue_radians, linrgb);
    return ArgBFromLinrgb(linrgb);
}

} // namespace

uint32_t ArgBFromRgb(unsigned red, unsigned green, unsigned blue) {
    return 0xFF000000u | ((red & 0xFFu) << 16) | ((green & 0xFFu) << 8) | (blue & 0xFFu);
}

unsigned AlphaFromArgb(uint32_t argb) { return (argb >> 24) & 0xFFu; }
unsigned RedFromArgb(uint32_t argb) { return (argb >> 16) & 0xFFu; }
unsigned GreenFromArgb(uint32_t argb) { return (argb >> 8) & 0xFFu; }
unsigned BlueFromArgb(uint32_t argb) { return argb & 0xFFu; }

Cam16 Cam16FromInt(uint32_t argb) {
    const Cam16Values values = Cam16InViewingConditions(argb, DefaultViewingConditions());
    return Cam16{values.hue, values.chroma, values.j};
}

Hct HctFromInt(uint32_t argb) {
    const Cam16Values values = Cam16InViewingConditions(argb, DefaultViewingConditions());
    return Hct{values.hue, values.chroma, LstarFromArgb(argb), argb};
}

Hct HctFrom(double hue, double chroma, double tone) {
    return HctFromInt(SolveToInt(hue, chroma, tone));
}

TonalPalette::TonalPalette(double hue, double chroma) : hue_(hue), chroma_(chroma) {}

TonalPalette TonalPalette::FromInt(uint32_t argb) {
    const Hct hct = HctFromInt(argb);
    return TonalPalette(hct.hue, hct.chroma);
}

uint32_t TonalPalette::Tone(double tone) const { return SolveToInt(hue_, chroma_, tone); }

Scheme SchemeFromSeed(uint32_t seed) {
    const Hct source = HctFromInt(seed);
    const double hue = source.hue;
    const double chroma = source.chroma;

    const TonalPalette a1(hue, std::max(48.0, chroma));
    const TonalPalette a2(hue, 16.0);
    const TonalPalette a3(hue + 60.0, 24.0);
    const TonalPalette n1(hue, 4.0);
    const TonalPalette n2(hue, 8.0);
    const TonalPalette error(25.0, 84.0);

    Scheme scheme{};
    scheme.primary = a1.Tone(80.0);
    scheme.on_primary = a1.Tone(20.0);
    scheme.primary_container = a1.Tone(30.0);
    scheme.on_primary_container = a1.Tone(90.0);

    scheme.secondary = a2.Tone(80.0);
    scheme.on_secondary = a2.Tone(20.0);
    scheme.secondary_container = a2.Tone(30.0);
    scheme.on_secondary_container = a2.Tone(90.0);

    scheme.tertiary = a3.Tone(80.0);
    scheme.on_tertiary = a3.Tone(20.0);
    scheme.tertiary_container = a3.Tone(30.0);
    scheme.on_tertiary_container = a3.Tone(90.0);

    scheme.error = error.Tone(80.0);
    scheme.on_error = error.Tone(20.0);

    scheme.surface = n1.Tone(6.0);
    scheme.on_surface = n1.Tone(90.0);
    scheme.surface_container_lowest = n1.Tone(4.0);
    scheme.surface_container_low = n1.Tone(10.0);
    scheme.surface_container = n1.Tone(12.0);
    scheme.surface_container_high = n1.Tone(17.0);
    scheme.surface_container_highest = n1.Tone(22.0);

    scheme.surface_variant = n2.Tone(30.0);
    scheme.on_surface_variant = n2.Tone(80.0);
    scheme.outline = n2.Tone(60.0);
    scheme.outline_variant = n2.Tone(30.0);
    return scheme;
}

} // namespace md3
