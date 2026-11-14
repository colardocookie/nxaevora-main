/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_VK_UPSCALER_SELFTEST_H
#define SNX_VK_UPSCALER_SELFTEST_H
/* The upscaler self-test's own oracle (SNX_ARCEUS_UPSCALER_SELFTEST): a scene that is a function of position, what a
   rasteriser draws of it, its true picture at any size, the regions of that picture and the verdicts. Pure.
   BY RULE this file shares no arithmetic with what it tests: it includes nothing of temporal_math.h and calls
   nothing of the adapter. A mistake in the conversion must not be able to repeat itself here. */
#include <math.h>
#include <stdint.h>

/* A viewport as Vulkan has it: a point at ndc n is at pixel (n x 0.5 + 0.5) x (w, h) + (x, y). h may be negative. */
typedef struct {
    double x, y, w, h;
} SelftestViewport;
static inline void selftest_place(const SelftestViewport* v, const double n[2], double pixel[2]) {
    pixel[0] = (n[0] * 0.5 + 0.5) * v->w + v->x;
    pixel[1] = (n[1] * 0.5 + 0.5) * v->h + v->y;
}
static inline void selftest_ndc(const SelftestViewport* v, double px, double py, double n[2]) {
    n[0] = ((px - v->x) / v->w - 0.5) * 2.0;
    n[1] = ((py - v->y) / v->h - 0.5) * 2.0;
}

/* The scene, in the ndc of the projection without any perturbation. The background is a far plane with one of
   two patterns: a checker, slanted edges and a fine grid, each in one colour channel, all values in 0 to 1. The
   patch is a nearer rectangle with a pattern of its own that moves with it. */
#define SELFTEST_FAR 0.02   /* reversed depth: far is near 0 */
#define SELFTEST_NEAR 0.6
static inline double selftest_fract(double a) { return a - floor(a); }
static inline void selftest_background(int pattern, const double n[2], double rgb[3]) {
    const double cell = pattern ? 0.19 : 0.25, band = pattern ? 0.23 : 0.31, pitch = pattern ? 0.031 : 0.02;
    const int checker = ((int)floor(n[0] / cell) + (int)floor(n[1] / cell)) & 1;
    const int slanted = (int)floor((0.6 * n[0] + 0.8 * n[1]) / band) & 1;
    const int grid = selftest_fract(n[0] / pitch) < 0.2 || selftest_fract(n[1] / (pitch * 1.15)) < 0.2;
    rgb[0] = 0.15 + 0.6 * checker;
    rgb[1] = pattern ? 0.75 - 0.5 * slanted : 0.2 + 0.5 * slanted;
    rgb[2] = 0.1 + 0.7 * grid;
}
static inline void selftest_patch(const double local[2], double rgb[3]) {
    const int checker = ((int)floor(local[0] / 0.05) + (int)floor(local[1] / 0.05)) & 1;
    const int stripe = selftest_fract((local[0] - 0.5 * local[1]) / 0.03) < 0.5;
    rgb[0] = 0.9 - 0.5 * checker;
    rgb[1] = 0.3;
    rgb[2] = 0.8 * stripe;
}
/* One frame of the scene. shift: how far the background has moved since the scene's start, in ndc, and the same
   a frame ago. patch: the patch's rectangle x0, y0, x1, y1 in ndc now and a frame ago. jitter: the perturbation
   added to the clip position when the frame is drawn, in ndc. */
typedef struct {
    int pattern, has_patch;
    double jitter[2], shift[2], shift_before[2], patch[4], patch_before[4];
} SelftestFrame;
static inline int selftest_in(const double r[4], const double n[2]) {
    return n[0] >= r[0] && n[0] < r[2] && n[1] >= r[1] && n[1] < r[3];
}
/* the scene's colour and depth at a point, and where that point was a frame ago */
static inline void selftest_scene(const SelftestFrame* f, const double n[2], double rgb[3], double* depth,
                                  double before[2]) {
    if (f->has_patch && selftest_in(f->patch, n)) {
        const double local[2] = {n[0] - f->patch[0], n[1] - f->patch[1]};
        selftest_patch(local, rgb);
        *depth = SELFTEST_NEAR;
        before[0] = n[0] - (f->patch[0] - f->patch_before[0]);
        before[1] = n[1] - (f->patch[1] - f->patch_before[1]);
        return;
    }
    const double at[2] = {n[0] - f->shift[0], n[1] - f->shift[1]};
    selftest_background(f->pattern, at, rgb);
    *depth = SELFTEST_FAR;
    before[0] = n[0] - (f->shift[0] - f->shift_before[0]);
    before[1] = n[1] - (f->shift[1] - f->shift_before[1]);
}
/* What a rasteriser draws at the render pixel x, y of images w by h through the viewport: the pixel's centre lies
   at the ndc the viewport gives it, and the scene's point drawn there is that less the perturbation. The motion
   texel is the renderer's: the point's place now less its place a frame ago, both without the perturbation, in
   pixels over the images' size. */
static inline void selftest_input(const SelftestFrame* f, const SelftestViewport* v, int w, int h, int x, int y,
                                  double rgb[3], double* depth, double texel[2]) {
    double centre[2], before[2], now_at[2], before_at[2];
    selftest_ndc(v, x + 0.5, y + 0.5, centre);
    const double n[2] = {centre[0] - f->jitter[0], centre[1] - f->jitter[1]};
    selftest_scene(f, n, rgb, depth, before);
    selftest_place(v, n, now_at);
    selftest_place(v, before, before_at);
    texel[0] = (now_at[0] - before_at[0]) / w;
    texel[1] = (now_at[1] - before_at[1]) / h;
}
/* The true picture at the output pixel x, y: the scene without any perturbation, averaged over samples by samples
   points of the pixel. */
static inline void selftest_reference(const SelftestFrame* f, const SelftestViewport* v, int x, int y, int samples,
                                      double rgb[3]) {
    rgb[0] = rgb[1] = rgb[2] = 0.0;
    for (int j = 0; j < samples; j++)
        for (int i = 0; i < samples; i++) {
            double n[2], c[3], depth, before[2];
            selftest_ndc(v, x + (i + 0.5) / samples, y + (j + 0.5) / samples, n);
            selftest_scene(f, n, c, &depth, before);
            for (int k = 0; k < 3; k++) rgb[k] += c[k];
        }
    for (int k = 0; k < 3; k++) rgb[k] /= (double)samples * samples;
}
/* The region of the output pixel x, y, each known exactly from the scene. The picture's border and the patch's own
   rim, both SELFTEST_RIM pixels wide, are judged by no one. The uncovered band is exactly the pixels whose centre
   the patch covered a frame ago and does not cover now. */
#define SELFTEST_RIM 8
enum { SELFTEST_NONE, SELFTEST_BACKGROUND, SELFTEST_INSIDE, SELFTEST_UNCOVERED };
static inline void selftest_pixels(const SelftestViewport* v, const double r[4], double out[4]) {
    const double a[2] = {r[0], r[1]}, b[2] = {r[2], r[3]};
    double pa[2], pb[2];
    selftest_place(v, a, pa);
    selftest_place(v, b, pb);
    out[0] = pa[0] < pb[0] ? pa[0] : pb[0];
    out[1] = pa[1] < pb[1] ? pa[1] : pb[1];
    out[2] = pa[0] < pb[0] ? pb[0] : pa[0];
    out[3] = pa[1] < pb[1] ? pb[1] : pa[1];
}
static inline int selftest_region(const SelftestFrame* f, const SelftestViewport* v, int w, int h, int x, int y) {
    if (x < SELFTEST_RIM || y < SELFTEST_RIM || x >= w - SELFTEST_RIM || y >= h - SELFTEST_RIM) return SELFTEST_NONE;
    if (!f->has_patch) return SELFTEST_BACKGROUND;
    double now[4], before[4];
    const double c[2] = {x + 0.5, y + 0.5};
    selftest_pixels(v, f->patch, now);
    selftest_pixels(v, f->patch_before, before);
    if (selftest_in(now, c)) {
        const double inner[4] = {now[0] + SELFTEST_RIM, now[1] + SELFTEST_RIM, now[2] - SELFTEST_RIM,
                                 now[3] - SELFTEST_RIM};
        return selftest_in(inner, c) ? SELFTEST_INSIDE : SELFTEST_NONE;
    }
    return selftest_in(before, c) ? SELFTEST_UNCOVERED : SELFTEST_BACKGROUND;
}

/* The game's formats. B10G11R11: three unsigned floats without a sign, red in the low 11 bits (6 of mantissa),
   green in the next 11, blue in the top 10 (5 of mantissa), 5 bits of exponent each with a bias of 15. */
static inline uint32_t selftest_ufloat(double value, int mantissa_bits) {
    const uint32_t one = 1u << mantissa_bits;
    if (!(value > 0.0)) return 0;
    if (value >= 65024.0) return (30u << mantissa_bits) | (one - 1); /* the largest finite value */
    int exponent;
    const double m = frexp(value, &exponent); /* value = m x 2^exponent, m in 0.5 to 1 */
    int e = exponent - 1 + 15;
    if (e <= 0) { /* denormal: value = mantissa / one x 2^-14 */
        const double scaled = value * 16384.0 * one;
        return (uint32_t)(scaled + 0.5);
    }
    uint32_t mantissa = (uint32_t)((m * 2.0 - 1.0) * one + 0.5);
    if (mantissa == one) {
        mantissa = 0;
        e++;
    }
    return ((uint32_t)e << mantissa_bits) | mantissa;
}
static inline double selftest_ufloat_value(uint32_t bits, int mantissa_bits) {
    const uint32_t one = 1u << mantissa_bits, e = bits >> mantissa_bits & 31, mantissa = bits & (one - 1);
    if (!e) return (double)mantissa / one / 16384.0;
    return (1.0 + (double)mantissa / one) * ldexp(1.0, (int)e - 15);
}
static inline uint32_t selftest_b10g11r11(const double rgb[3]) {
    return selftest_ufloat(rgb[0], 6) | selftest_ufloat(rgb[1], 6) << 11 | selftest_ufloat(rgb[2], 5) << 22;
}
static inline void selftest_b10g11r11_value(uint32_t word, double rgb[3]) {
    rgb[0] = selftest_ufloat_value(word & 0x7FF, 6);
    rgb[1] = selftest_ufloat_value(word >> 11 & 0x7FF, 6);
    rgb[2] = selftest_ufloat_value(word >> 22, 5);
}
/* D24S8's depth as a buffer copy carries it: 24 bits of unsigned normalised depth in the low bits of 32 */
static inline uint32_t selftest_d24(double depth) {
    const double clamped = depth < 0.0 ? 0.0 : depth > 1.0 ? 1.0 : depth;
    return (uint32_t)(clamped * 16777215.0 + 0.5);
}
/* a 16 bit float, as the motion image takes it and the output image gives it */
static inline double selftest_half_value(uint16_t bits) {
    const int e = bits >> 10 & 31, mantissa = bits & 1023;
    const double sign = bits & 0x8000 ? -1.0 : 1.0;
    if (e == 31) return mantissa ? NAN : sign * INFINITY;
    if (!e) return sign * mantissa / 1024.0 / 16384.0;
    return sign * (1.0 + mantissa / 1024.0) * ldexp(1.0, e - 15);
}
static inline uint16_t selftest_half(double value) {
    const uint16_t sign = value < 0.0 ? 0x8000 : 0;
    const double a = fabs(value);
    if (!(a > 0.0)) return sign;
    if (a >= 65520.0) return (uint16_t)(sign | 0x7C00);
    int exponent;
    const double m = frexp(a, &exponent);
    int e = exponent - 1 + 15;
    if (e <= 0) return (uint16_t)(sign | (uint16_t)(a * 16384.0 * 1024.0 + 0.5));
    uint32_t mantissa = (uint32_t)((m * 2.0 - 1.0) * 1024.0 + 0.5);
    if (mantissa == 1024) {
        mantissa = 0;
        e++;
    }
    return (uint16_t)(sign | (uint32_t)e << 10 | mantissa);
}

/* The verdicts. Every one is a comparison with a control from the same run. The numbers are the design's. */
#define SELFTEST_MARGIN 1.25  /* a wrong variant, or a control, has at least this many times the error */
#define SELFTEST_SAME 0.01    /* two outputs that must be one: mean difference at most this, of a range of 1 */
#define SELFTEST_APART 0.05   /* a control that must have seen something: mean difference at least this */
enum { SELFTEST_PASS, SELFTEST_FAIL, SELFTEST_BLIND };
/* The errors of n variants of one case, the configured one at index own. BLIND: the case cannot tell its variants
   apart (the worst is not a margin above the best), so it decides nothing and fails as a test. PASS: the
   configured one is the lowest and every other is a margin above it. Anything not finite fails. */
static inline int selftest_verdict(const double* errors, int n, int own, double margin) {
    double best = INFINITY, worst = 0.0;
    for (int i = 0; i < n; i++) {
        if (!isfinite(errors[i]) || errors[i] < 0.0) return SELFTEST_FAIL;
        if (errors[i] < best) best = errors[i];
        if (errors[i] > worst) worst = errors[i];
    }
    if (n < 2 || own < 0 || own >= n) return SELFTEST_FAIL;
    if (!(worst > 0.0) || !(worst >= margin * best)) return SELFTEST_BLIND;
    for (int i = 0; i < n; i++)
        if (i != own && !(errors[i] > errors[own] && errors[i] >= margin * errors[own])) return SELFTEST_FAIL;
    return SELFTEST_PASS;
}
/* a control must be a margin worse than the configured run */
static inline int selftest_beats(double own, double control, double margin) {
    return isfinite(own) && isfinite(control) && own >= 0.0 && control > own && control >= margin * own;
}
/* the output stops changing: the mean change between frames over the last cycle of offsets is at most half of
   that over the first */
static inline int selftest_settles(double first_cycle, double last_cycle) {
    return isfinite(first_cycle) && isfinite(last_cycle) && first_cycle > 0.0 && last_cycle >= 0.0 &&
           last_cycle <= 0.5 * first_cycle;
}
static inline int selftest_same(double difference) { return isfinite(difference) && difference <= SELFTEST_SAME; }
static inline int selftest_apart(double difference) { return isfinite(difference) && difference >= SELFTEST_APART; }
#endif
