/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_VK_UPSCALER_SELFTEST_H
#define SNX_VK_UPSCALER_SELFTEST_H
/* The upscaler self-test's own oracle (SNX_ARCEUS_UPSCALER_SELFTEST): a scene that is a function of position, what a
   rasteriser draws of it, its true picture at any size, the regions of that picture and the verdicts. Pure.
   BY RULE this file shares no arithmetic with what it tests: it includes nothing of temporal_math.h and calls
   nothing of the adapter. A mistake in the conversion must not be able to repeat itself here. */
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* A viewport as Vulkan has it: a point at ndc n is at pixel (n x 0.5 + 0.5) x (w, h) + (x, y). h may be negative. */
/* Whether the switch's value asks for the self-test: any value but none, an empty one, "0" and "off". A launcher
   that cannot take a line back can so turn the mode off. */
static inline int selftest_asked(const char* value) {
    if (!value || !value[0]) return 0;
    if (value[0] == '0' && !value[1]) return 0;
    return !(value[0] == 'o' && value[1] == 'f' && value[2] == 'f' && !value[3]);
}
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
/* The projection a scene's depth is made with, in the game's form. The stored depth of a point at the distance d
   in front of the camera is (A z + B) / (-z) at z = -d, with A = n / (f - n) and B = n f / (f - n): 1 at the
   near plane and 0 at the far one. With the far plane at infinity A is 0 and B is n. The scene's two distances
   give the depths above for a near plane of 0.1 and a far plane at infinity. */
#define SELFTEST_FAR_DISTANCE 5.0
#define SELFTEST_NEAR_DISTANCE (1.0 / 6.0)
typedef struct {
    double near_plane, far_plane; /* far_plane may be INFINITY */
} SelftestProjection;
static inline double selftest_depth(const SelftestProjection* p, double distance) {
    if (isinf(p->far_plane)) return p->near_plane / distance;
    const double a = p->near_plane / (p->far_plane - p->near_plane), b = a * p->far_plane;
    return (a * -distance + b) / distance;
}
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
/* The edge picture, for the jitter's signs: grey, 0.5 everywhere but in two regions of 96 by 96 output pixels, each
   with one straight edge from 0.25 to 0.75 at a slope of 1 in 10. All numbers are output pixels, a pixel's centre
   at its number and a half, and are the same for either sign of the viewport's height. */
typedef struct {
    int x0, y0, x1, y1; /* the region, the last column and row not in it */
    int across_x;       /* 1: near vertical, 0.25 left of it. 0: near horizontal, 0.25 above it */
    double at, mid;     /* the edge is at this place across, on the line through mid along it */
} SelftestEdge;
#define SELFTEST_EDGE_LOW 0.25
#define SELFTEST_EDGE_HIGH 0.75
#define SELFTEST_EDGE_REACH 4 /* pixels: the profile's reach each side of the edge, and the region's unused rim */
#define SELFTEST_EDGE_BINS 32 /* a quarter of a pixel each */
static const SelftestEdge SELFTEST_EDGE_V = {60, 40, 156, 136, 1, 108.37, 88.0};
static const SelftestEdge SELFTEST_EDGE_H = {200, 150, 296, 246, 0, 198.37, 248.0};
/* how far the point is past the edge along its normal, in pixels: negative on the 0.25 side */
static inline double selftest_edge_distance(const SelftestEdge* e, double px, double py) {
    const double across = e->across_x ? px : py, along = e->across_x ? py : px;
    return (across - (e->at + (along - e->mid) / 10.0)) / sqrt(1.01);
}
static inline int selftest_edge_holds(const SelftestEdge* e, double px, double py) {
    return px >= e->x0 && px < e->x1 && py >= e->y0 && py < e->y1;
}
static inline double selftest_edge_picture(double px, double py) {
    const SelftestEdge* edges[2] = {&SELFTEST_EDGE_V, &SELFTEST_EDGE_H};
    for (int i = 0; i < 2; i++)
        if (selftest_edge_holds(edges[i], px, py))
            return selftest_edge_distance(edges[i], px, py) < 0.0 ? SELFTEST_EDGE_LOW : SELFTEST_EDGE_HIGH;
    return 0.5;
}
/* One frame of the scene. shift: how far the background has moved since the scene's start, in ndc, and the same
   a frame ago. patch: the patch's rectangle x0, y0, x1, y1 in ndc now and a frame ago. jitter: the perturbation
   added to the clip position when the frame is drawn, in ndc. */
#define SELFTEST_PATTERN_EDGES 2 /* the edge picture in place of the two patterns of the background */
typedef struct {
    int pattern, has_patch;
    double jitter[2], shift[2], shift_before[2], patch[4], patch_before[4];
    double depth[2];      /* the stored depth of the background and of the patch */
    SelftestViewport out; /* for the edge picture alone: the viewport that gives a point its output pixel */
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
        *depth = f->depth[1];
        before[0] = n[0] - (f->patch[0] - f->patch_before[0]);
        before[1] = n[1] - (f->patch[1] - f->patch_before[1]);
        return;
    }
    const double at[2] = {n[0] - f->shift[0], n[1] - f->shift[1]};
    if (f->pattern == SELFTEST_PATTERN_EDGES) {
        double pixel[2];
        selftest_place(&f->out, at, pixel);
        rgb[0] = rgb[1] = rgb[2] = selftest_edge_picture(pixel[0], pixel[1]);
    } else
        selftest_background(f->pattern, at, rgb);
    *depth = f->depth[0];
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
/* A frame's depth in the other convention, near 0 and far 1 (DLSS's case of a changed create property, research's
   ruling of N4 Q2): for a finite far plane that is 1 less the reversed depth, which in D24 is the largest code
   less the code */
static inline void selftest_depth_turn(uint32_t* depth, size_t texels) {
    for (size_t t = 0; t < texels; t++) depth[t] = 16777215u - (depth[t] & 16777215u);
}
/* The output image holds four 32 bit floats a pixel, 16 bytes, and is read back as that. One channel of one
   pixel out of the bytes of the read-back, copied out and not read through a pointer of another type. */
#define SELFTEST_OUTPUT_BYTES 16
static inline float selftest_output_value(const uint8_t* bytes, size_t pixel, int channel) {
    float value;
    memcpy(&value, bytes + pixel * SELFTEST_OUTPUT_BYTES + (size_t)channel * sizeof value, sizeof value);
    return value;
}
/* 1 when the one staging buffer holds a frame's uploads (colour and depth at 4 bytes a texel, vectors at 8) and
   the output's read-back. Sizes that are not positive, or that do not fit a size_t's product, do not fit. */
static inline int selftest_staging_fits(uint64_t staging, int w, int h, int ow, int oh) {
    if (w <= 0 || h <= 0 || ow <= 0 || oh <= 0) return 0;
    const uint64_t in = (uint64_t)w * (uint64_t)h, out = (uint64_t)ow * (uint64_t)oh;
    return in <= staging / 16 && out <= staging / SELFTEST_OUTPUT_BYTES;
}
/* a 16 bit float, as the motion image takes it */
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
/* The vectors' texels as the motion image takes them: four halves each as the inputs keep them (FSR's image), or
   their first two only (DLSS's two-channel image, research's ruling of N4 Q3). The bytes written: 8 or 4 a texel. */
static inline uint64_t selftest_motion_pack(uint16_t* to, const uint16_t* from, uint64_t texels, int two) {
    if (!two) {
        memcpy(to, from, (size_t)texels * 8);
        return texels * 8;
    }
    for (uint64_t t = 0; t < texels; t++) {
        to[2 * t] = from[4 * t];
        to[2 * t + 1] = from[4 * t + 1];
    }
    return texels * 4;
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
/* The verdict of a case whose variants turn the jitter's signs too (cases 1 and 2). The configured variant must
   have strictly the lowest error of all. The margin is asked only of the variants marked in needs: those that
   turn vector signs and leave the jitter's as configured, which the pattern excites. The jitter's own margin
   is the edge case's. Errors that are all one and the same tell nothing apart (BLIND). */
static inline int selftest_signs(const double* errors, const int* needs, int n, int own, double margin) {
    double best = INFINITY, worst = 0.0;
    for (int i = 0; i < n; i++) {
        if (!isfinite(errors[i]) || errors[i] < 0.0) return SELFTEST_FAIL;
        if (errors[i] < best) best = errors[i];
        if (errors[i] > worst) worst = errors[i];
    }
    if (n < 2 || own < 0 || own >= n) return SELFTEST_FAIL;
    if (!(worst > best)) return SELFTEST_BLIND;
    for (int i = 0; i < n; i++) {
        if (i == own) continue;
        if (!(errors[i] > errors[own])) return SELFTEST_FAIL;
        if (needs[i] && !(errors[i] >= margin * errors[own])) return SELFTEST_FAIL;
    }
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
/* The changes between consecutive outputs of a run, kept apart for its two cycles of offsets. A run evaluates
   twice the offsets frames, so it has that many less one changes. Change k is the one into the frame evaluated
   as number k (the first evaluated frame is number 0 and has none). The first cycle is the changes below offsets
   in number, one less than the offsets. The last cycle is the rest, the change at the boundary included. */
typedef struct {
    double sum[2];
    int n[2];
} SelftestSettling;
static inline void selftest_change(SelftestSettling* s, int k, int offsets, double change) {
    const int cycle = k < offsets ? 0 : 1;
    if (k < 1) return;
    s->sum[cycle] += change;
    s->n[cycle]++;
}
/* the mean change of a cycle, not a number when it has none */
static inline double selftest_cycle(const SelftestSettling* s, int cycle) {
    return s->n[cycle] ? s->sum[cycle] / s->n[cycle] : NAN;
}
/* An edge's profile in a picture: the mean value of the pixels at each distance from the edge, in bins a quarter of
   a pixel wide from 4 pixels before it to 4 after. The bin of the output pixel x, y, or -1 when the pixel is not
   used: outside the region, in its rim of 4 pixels, or further from the edge. Nothing is fitted or shifted. */
static inline int selftest_edge_bin(const SelftestEdge* e, int x, int y) {
    if (x < e->x0 + SELFTEST_EDGE_REACH || x >= e->x1 - SELFTEST_EDGE_REACH) return -1;
    if (y < e->y0 + SELFTEST_EDGE_REACH || y >= e->y1 - SELFTEST_EDGE_REACH) return -1;
    const double d = selftest_edge_distance(e, x + 0.5, y + 0.5);
    if (!(d >= -SELFTEST_EDGE_REACH) || !(d < SELFTEST_EDGE_REACH)) return -1;
    const int bin = (int)floor((d + SELFTEST_EDGE_REACH) * (SELFTEST_EDGE_BINS / (2.0 * SELFTEST_EDGE_REACH)));
    return bin < 0 ? 0 : bin >= SELFTEST_EDGE_BINS ? SELFTEST_EDGE_BINS - 1 : bin;
}
typedef struct {
    double sum[SELFTEST_EDGE_BINS];
    int n[SELFTEST_EDGE_BINS];
} SelftestProfile;
/* a pixel's value in a picture of three doubles a pixel: the mean of the three, which are one in a grey picture */
static inline double selftest_grey(const double* rgb, int w, int x, int y) {
    const double* p = rgb + 3 * ((size_t)y * (size_t)w + (size_t)x);
    return (p[0] + p[1] + p[2]) / 3.0;
}
static inline void selftest_profile_make(const SelftestEdge* e, const double* rgb, int w, SelftestProfile* p) {
    for (int i = 0; i < SELFTEST_EDGE_BINS; i++) p->sum[i] = 0.0, p->n[i] = 0;
    for (int y = e->y0; y < e->y1; y++)
        for (int x = e->x0; x < e->x1; x++) {
            const int bin = selftest_edge_bin(e, x, y);
            if (bin < 0) continue;
            p->sum[bin] += selftest_grey(rgb, w, x, y);
            p->n[bin]++;
        }
}
static inline double selftest_profile_mean(const SelftestProfile* p, int bin) {
    return p->n[bin] > 0 ? p->sum[bin] / p->n[bin] : NAN;
}
/* 1 when every bin holds pixels and has a finite mean: a profile that is not whole is compared with nothing */
static inline int selftest_profile_whole(const SelftestProfile* p) {
    for (int i = 0; i < SELFTEST_EDGE_BINS; i++)
        if (!isfinite(selftest_profile_mean(p, i))) return 0;
    return 1;
}
/* The mean absolute difference of two profiles over the bins, in fixed places. A shift of the edge, a wider rise,
   a loss of contrast and an overshoot all show in it. Not a number when either profile is not whole. */
static inline double selftest_profile_apart(const SelftestProfile* a, const SelftestProfile* b) {
    double sum = 0.0;
    if (!selftest_profile_whole(a) || !selftest_profile_whole(b)) return NAN;
    for (int i = 0; i < SELFTEST_EDGE_BINS; i++) sum += fabs(selftest_profile_mean(a, i) - selftest_profile_mean(b, i));
    return sum / SELFTEST_EDGE_BINS;
}
/* The mean absolute difference of two pictures over the pixels a profile uses, taken channel by channel before
   any mean. Averaging in bins can cancel what differs with opposite signs from pixel to pixel, and a grey value
   what differs with opposite signs between channels: this cancels neither. Not a number when a value is not
   finite or there is no pixel. */
static inline double selftest_edge_apart(const SelftestEdge* e, const double* a, const double* b, int w) {
    double sum = 0.0;
    long n = 0;
    for (int y = e->y0; y < e->y1; y++)
        for (int x = e->x0; x < e->x1; x++) {
            if (selftest_edge_bin(e, x, y) < 0) continue;
            const size_t at = 3 * ((size_t)y * (size_t)w + (size_t)x);
            for (int c = 0; c < 3; c++) sum += fabs(a[at + c] - b[at + c]);
            n += 3;
        }
    return n && isfinite(sum) ? sum / (double)n : NAN;
}
/* For the log, not for any verdict: the same difference kept apart by channel, red, green, blue. */
static inline void selftest_edge_channels(const SelftestEdge* e, const double* a, const double* b, int w,
                                          double out[3]) {
    double sum[3] = {0.0, 0.0, 0.0};
    long n = 0;
    for (int y = e->y0; y < e->y1; y++)
        for (int x = e->x0; x < e->x1; x++) {
            if (selftest_edge_bin(e, x, y) < 0) continue;
            const size_t at = 3 * ((size_t)y * (size_t)w + (size_t)x);
            for (int c = 0; c < 3; c++) sum[c] += fabs(a[at + c] - b[at + c]);
            n++;
        }
    for (int c = 0; c < 3; c++) out[c] = n ? sum[c] / (double)n : NAN;
}
/* For the log, not for any verdict. Where the profile first rises through a level, in pixels from the edge, taken
   between the two bins' centres. Not a number when it never does. */
static inline double selftest_profile_crossing(const SelftestProfile* p, double level) {
    const double width = 2.0 * SELFTEST_EDGE_REACH / SELFTEST_EDGE_BINS;
    for (int i = 0; i + 1 < SELFTEST_EDGE_BINS; i++) {
        const double a = selftest_profile_mean(p, i), b = selftest_profile_mean(p, i + 1);
        if (isfinite(a) && isfinite(b) && a < level && b >= level)
            return -SELFTEST_EDGE_REACH + (i + 0.5 + (level - a) / (b - a)) * width;
    }
    return NAN;
}
/* the rise from 10 to 90 per cent of the step, in pixels */
static inline double selftest_profile_rise(const SelftestProfile* p) {
    const double step = SELFTEST_EDGE_HIGH - SELFTEST_EDGE_LOW;
    return selftest_profile_crossing(p, SELFTEST_EDGE_LOW + 0.9 * step) -
           selftest_profile_crossing(p, SELFTEST_EDGE_LOW + 0.1 * step);
}
/* how far a bin's mean lies outside the two values of the picture, at most */
static inline double selftest_profile_overshoot(const SelftestProfile* p) {
    double most = 0.0;
    for (int i = 0; i < SELFTEST_EDGE_BINS; i++) {
        const double mean = selftest_profile_mean(p, i);
        if (mean - SELFTEST_EDGE_HIGH > most) most = mean - SELFTEST_EDGE_HIGH;
        if (SELFTEST_EDGE_LOW - mean > most) most = SELFTEST_EDGE_LOW - mean;
    }
    return most;
}
/* The edge case's verdict. Each run of the case leaves, for each of the two edges, its profile's difference from
   the reference's and its picture's difference from the reference over the same pixels. [0] is the near-vertical
   edge, which lies across x, [1] the near-horizontal one, across y. noise: the same two measures between the two
   configured runs, as differences of profiles and of pictures, not of errors. */
typedef struct {
    double profile, pixels;
} SelftestEdgeMeasure;
typedef struct {
    SelftestEdgeMeasure configured[2], turned_x[2], turned_y[2], turned_both[2], told_nothing[2], plain[2];
    SelftestEdgeMeasure noise[2];
    int settled; /* the configured run's output has stopped changing, by case 1's rule */
} SelftestEdgeRuns;
#define SELFTEST_EDGE_TURNED 0.010 /* a turned sign's error over the configured one's, at least, on its edge */
#define SELFTEST_EDGE_TOLD 0.003   /* the same for the jitter reported as zero: the case's own sensitivity */
#define SELFTEST_EDGE_NOISE 5.0    /* every such margin is at least this many times the noise of its measure */
/* What failed, as bits. NUMBERS, TOLD and NOISE mean the case could not decide: it fails as a test. */
enum {
    SELFTEST_EDGE_ORDER = 1,    /* across its axis: configured below told nothing below the turned sign */
    SELFTEST_EDGE_MARGIN = 2,   /* a turned sign is not worse by the budget, on the profile or over the pixels */
    SELFTEST_EDGE_BLIND = 4,    /* told nothing is not worse by its budget: no sensitivity to the jitter shown */
    SELFTEST_EDGE_PLAIN = 8,    /* the configured run is not better than one frame scaled plainly */
    SELFTEST_EDGE_UNSETTLED = 16,
    SELFTEST_EDGE_BOTH = 32,    /* both signs turned is not worse by the budget on each edge */
    SELFTEST_EDGE_NOISY = 64,   /* a margin is under five times the noise of its measure */
    SELFTEST_EDGE_NUMBERS = 128 /* a number is not finite: a profile was not whole, or a run gave nothing */
};
static inline int selftest_edge_margin(double worse, double configured, double budget, double noise, unsigned miss,
                                       unsigned* failed) {
    const double margin = worse - configured;
    if (!isfinite(margin) || !isfinite(noise)) {
        *failed |= SELFTEST_EDGE_NUMBERS;
        return 0;
    }
    if (!(margin >= budget)) *failed |= miss;
    if (!(margin >= SELFTEST_EDGE_NOISE * noise)) *failed |= SELFTEST_EDGE_NOISY;
    return 1;
}
/* 0 when the case passes, else the bits of what failed. For one orientation of the viewport. Every number it is
   handed must be one, also those no condition uses (the other axis's sign on an edge, the plain frame over the
   pixels): a run that left a number that is not finite has not been measured, and the case decides nothing. */
static inline unsigned selftest_edge_judge(const SelftestEdgeRuns* r) {
    unsigned failed = 0;
    const SelftestEdgeMeasure* all[7] = {r->configured, r->turned_x,     r->turned_y, r->turned_both,
                                         r->told_nothing, r->plain,      r->noise};
    for (int k = 0; k < 7; k++)
        for (int e = 0; e < 2; e++)
            if (!isfinite(all[k][e].profile) || !isfinite(all[k][e].pixels)) failed |= SELFTEST_EDGE_NUMBERS;
    for (int axis = 0; axis < 2; axis++) {
        /* the sign of this axis on the edge that lies across it */
        const SelftestEdgeMeasure turned = axis ? r->turned_y[axis] : r->turned_x[axis];
        const SelftestEdgeMeasure own = r->configured[axis], told = r->told_nothing[axis], noise = r->noise[axis];
        if (!(own.profile < told.profile && told.profile < turned.profile)) failed |= SELFTEST_EDGE_ORDER;
        selftest_edge_margin(turned.profile, own.profile, SELFTEST_EDGE_TURNED, noise.profile, SELFTEST_EDGE_MARGIN,
                             &failed);
        selftest_edge_margin(turned.pixels, own.pixels, SELFTEST_EDGE_TURNED, noise.pixels, SELFTEST_EDGE_MARGIN,
                             &failed);
        selftest_edge_margin(told.profile, own.profile, SELFTEST_EDGE_TOLD, noise.profile, SELFTEST_EDGE_BLIND,
                             &failed);
        selftest_edge_margin(told.pixels, own.pixels, SELFTEST_EDGE_TOLD, noise.pixels, SELFTEST_EDGE_BLIND, &failed);
        /* on either edge: both signs turned, and the plainly scaled frame */
        selftest_edge_margin(r->turned_both[axis].profile, own.profile, SELFTEST_EDGE_TURNED, noise.profile,
                             SELFTEST_EDGE_BOTH, &failed);
        selftest_edge_margin(r->turned_both[axis].pixels, own.pixels, SELFTEST_EDGE_TURNED, noise.pixels,
                             SELFTEST_EDGE_BOTH, &failed);
        if (!isfinite(r->plain[axis].profile)) failed |= SELFTEST_EDGE_NUMBERS;
        if (!(own.profile < r->plain[axis].profile)) failed |= SELFTEST_EDGE_PLAIN;
    }
    if (!r->settled) failed |= SELFTEST_EDGE_UNSETTLED;
    return failed;
}
/* 1 when what failed means the case decided nothing, which is a failure of the test and not of the signs */
static inline int selftest_edge_undecided(unsigned failed) {
    return (failed & (SELFTEST_EDGE_BLIND | SELFTEST_EDGE_NOISY | SELFTEST_EDGE_NUMBERS)) != 0;
}
/* The matched pair. What one frame handed an adapter, as numbers only, so that two runs can be held against each
   other: the jitter, the vector scale, the frame time and the camera in f, the rest in u (the rectangle's
   origin and size, reset, flags, the sizes the context was made for, and each image's width, height, format
   and layout). done is set only after the adapter's call and the read back of that frame succeeded. */
#define SELFTEST_HANDED_FLOATS 8
#define SELFTEST_HANDED_WORDS 28
typedef struct {
    float f[SELFTEST_HANDED_FLOATS];
    uint32_t u[SELFTEST_HANDED_WORDS];
    uint32_t done;
} SelftestHanded;
enum {
    SELFTEST_MATCHED,      /* both runs succeeded in every expected frame and handed over the same */
    SELFTEST_MATCH_FAILED, /* a run did not succeed */
    SELFTEST_MATCH_SHORT,  /* a run has fewer or other frames done than expected: nothing is not a match */
    SELFTEST_MATCH_DIFFERS /* a frame handed over something else in the two runs */
};
/* Two runs' records over the frames of the inputs. expected: how many frames a whole run hands over. Both runs
   must have succeeded, each must have exactly that many frames done, the same ones, and those must be the same
   bit for bit. first gets the first frame that is not, -1 else. Frames that were not done are not compared with
   each other: a record of nothing equals another of nothing and says nothing. */
static inline int selftest_matched(const SelftestHanded* a, const SelftestHanded* b, int frames, int expected,
                                   int a_ran, int b_ran, int* first) {
    int done[2] = {0, 0};
    *first = -1;
    if (!a_ran || !b_ran) return SELFTEST_MATCH_FAILED;
    for (int i = 0; i < frames; i++) {
        done[0] += a[i].done != 0;
        done[1] += b[i].done != 0;
        if (*first < 0 && (a[i].done != 0) != (b[i].done != 0)) *first = i;
    }
    if (expected <= 0 || done[0] != expected || done[1] != expected || *first >= 0) return SELFTEST_MATCH_SHORT;
    for (int i = 0; i < frames; i++) {
        if (!a[i].done) continue;
        for (int k = 0; k < SELFTEST_HANDED_FLOATS; k++) {
            uint32_t x, y;
            memcpy(&x, &a[i].f[k], 4);
            memcpy(&y, &b[i].f[k], 4);
            if (x != y && *first < 0) *first = i;
        }
        for (int k = 0; k < SELFTEST_HANDED_WORDS; k++)
            if (a[i].u[k] != b[i].u[k] && *first < 0) *first = i;
        if (*first >= 0) return SELFTEST_MATCH_DIFFERS;
    }
    return SELFTEST_MATCHED;
}
/* a checksum of generated images, so that a log says which images a result was made from (FNV-1a, 64 bits) */
#define SELFTEST_CHECKSUM_START 0xcbf29ce484222325ull
static inline uint64_t selftest_checksum(const void* bytes, size_t n, uint64_t sum) {
    const unsigned char* p = (const unsigned char*)bytes;
    for (size_t i = 0; i < n; i++) sum = (sum ^ p[i]) * 0x100000001b3ull;
    return sum;
}
static inline int selftest_same(double difference) { return isfinite(difference) && difference <= SELFTEST_SAME; }
/* A context that went through a change of size against a fresh one given the same frames: the two pictures'
   mean absolute difference is at most this budget, half a per cent of the range. A declared number, fixed
   before any such data existed. */
#define SELFTEST_RESIZED 0.005
/* apart: the resized context's picture against the fresh one's. noise: two fresh contexts on the same frames
   against each other. control: a fresh context that was fed wrongly on purpose, against the fresh one. The
   control must differ by more than the budget and more than the noise, or the comparison has not been shown to
   be able to fail and the case decides nothing (BLIND). */
static inline int selftest_resized(double apart, double noise, double control) {
    if (!isfinite(apart) || !isfinite(noise) || !isfinite(control) || apart < 0.0 || noise < 0.0)
        return SELFTEST_BLIND;
    if (!(control > SELFTEST_RESIZED) || !(control > noise)) return SELFTEST_BLIND;
    return apart <= SELFTEST_RESIZED ? SELFTEST_PASS : SELFTEST_FAIL;
}
static inline int selftest_apart(double difference) { return isfinite(difference) && difference >= SELFTEST_APART; }
/* How the loader's list of instance layers was read, from the results of the call for the count and of the call
   for the list: whole, in part (the list's call answered "incomplete", so a layer that is not in the part read
   may still be there), or not at all (either call failed). incomplete is Vulkan's VK_INCOMPLETE. */
enum { SELFTEST_LAYERS_WHOLE, SELFTEST_LAYERS_PART, SELFTEST_LAYERS_UNREAD };
static inline int selftest_layers(int counted, int listed, int incomplete) {
    if (counted < 0 || listed < 0) return SELFTEST_LAYERS_UNREAD;
    return listed == incomplete ? SELFTEST_LAYERS_PART : SELFTEST_LAYERS_WHOLE;
}
/* The names of variables in the texts of validation callbacks, found by the pattern the layer writes: the word
   variable, a space, a quoted name. Taken from a text by a pattern: no identity of a resource, and a callback
   worded another way is not found. A name is its whole text, and counts once for each callback it occurs in. */
#define SELFTEST_VARIABLES 64
#define SELFTEST_VARIABLE_NAME 63
typedef struct {
    unsigned callbacks, names;
    struct {
        char name[SELFTEST_VARIABLE_NAME + 1];
        unsigned count, last; /* callbacks it occurred in, and the last of them by its number */
    } name[SELFTEST_VARIABLES];
    /* callbacks with no occurrence of the pattern, and with a text not scanned to its end. Then occurrences:
       a quoted name that does not close, a name too long to keep, a name that found no room in the table */
    unsigned unmatched, unscanned, unclosed, overlong, full;
} SelftestVariables;
/* one callback's raw text of length bytes, of which at most limit are scanned */
static inline void selftest_variables(SelftestVariables* v, const char* text, size_t length, size_t limit) {
    static const char pattern[] = "variable \"";
    const size_t width = sizeof pattern - 1, end = length > limit ? limit : length;
    const unsigned serial = ++v->callbacks;
    unsigned found = 0;
    if (length > limit) v->unscanned++;
    for (size_t i = 0; i + width <= end;) {
        if (memcmp(text + i, pattern, width)) {
            i++;
            continue;
        }
        found++;
        const size_t from = i + width;
        size_t to = from;
        while (to < end && text[to] != '"') to++;
        if (to == end) { /* no closing quote in what was scanned: nothing after it can be a name */
            v->unclosed++;
            break;
        }
        i = to + 1;
        const size_t n = to - from;
        if (n > SELFTEST_VARIABLE_NAME) {
            v->overlong++;
            continue;
        }
        unsigned k = 0;
        while (k < v->names && (strlen(v->name[k].name) != n || memcmp(v->name[k].name, text + from, n))) k++;
        if (k == v->names) {
            if (k == SELFTEST_VARIABLES) {
                v->full++;
                continue;
            }
            memcpy(v->name[k].name, text + from, n);
            v->name[k].name[n] = 0;
            v->names++;
        }
        if (v->name[k].last != serial) {
            v->name[k].last = serial;
            v->name[k].count++;
        }
    }
    if (!found) v->unmatched++;
}
/* 1 when nothing limits what the table says: every text scanned to its end, every name closed, kept and listed */
static inline int selftest_variables_whole(const SelftestVariables* v) {
    return !v->unscanned && !v->unclosed && !v->overlong && !v->full;
}
/* 1 when a callback's text holds the validation layer's own sentence that it stops reporting a message id, as a
   run printed it: "This VUID has now been reported 10 times, which is the duplicate_message_limit value, this
   will be the last time reporting it". The number is not part of what is matched. A text that only names
   something called duplicate_message_limit is not that sentence. */
static inline int selftest_stop_notice(const char* text) {
    const char* from = strstr(text, "This VUID has now been reported ");
    return from && strstr(from, " times, which is the duplicate_message_limit value, this will be the last time "
                                "reporting it") != NULL;
}
/* What a session shows of the contexts it used, for the native suite's printed assertions: how many came into use,
   and of the first four each one's create flags, the frames it evaluated, whether the first of them had the
   reset, and the far plane it was handed (mixed: not the same in every frame). And the checksum of the depth
   image of the last frame evaluated. */
#define SELFTEST_PROOF_MOST 4
typedef struct {
    unsigned contexts;
    uint32_t flags[SELFTEST_PROOF_MOST];
    unsigned frames[SELFTEST_PROOF_MOST];
    int first_reset[SELFTEST_PROOF_MOST], far_mixed[SELFTEST_PROOF_MOST];
    float far_plane[SELFTEST_PROOF_MOST];
    uint64_t depth_sum;
} SelftestProof;
/* a context came into use, made with these flags */
static inline void selftest_proof_made(SelftestProof* p, uint32_t flags) {
    if (p->contexts < SELFTEST_PROOF_MOST) p->flags[p->contexts] = flags;
    p->contexts++;
}
/* a frame was evaluated on the newest context with these numbers and this depth image */
static inline void selftest_proof_frame(SelftestProof* p, uint32_t reset, float far_plane, uint64_t depth_sum) {
    const unsigned k = p->contexts - 1;
    p->depth_sum = depth_sum;
    if (!p->contexts || k >= SELFTEST_PROOF_MOST) return;
    if (!p->frames[k]) {
        p->first_reset[k] = reset != 0;
        p->far_plane[k] = far_plane;
    } else if (!(p->far_plane[k] == far_plane))
        p->far_mixed[k] = 1;
    p->frames[k]++;
}
/* The first thing that is not as a case expects, or OK. Exactly n contexts came into use and each evaluated a
   frame. Context k was made with the far-plane flag exactly when far[k] is infinite, and was handed far[k] as a
   float in every frame. Each context's first evaluate had the reset. The last depth image handed over has this
   checksum. */
enum {
    SELFTEST_PROOF_OK,
    SELFTEST_PROOF_COUNT,
    SELFTEST_PROOF_UNUSED,
    SELFTEST_PROOF_FLAG,
    SELFTEST_PROOF_FAR,
    SELFTEST_PROOF_RESET,
    SELFTEST_PROOF_DEPTH
};
static inline int selftest_proof(const SelftestProof* p, unsigned n, const double far[], uint32_t infinite_flag,
                                 uint64_t depth_sum) {
    if (p->contexts != n || !n || n > SELFTEST_PROOF_MOST) return SELFTEST_PROOF_COUNT;
    for (unsigned k = 0; k < n; k++)
        if (!p->frames[k]) return SELFTEST_PROOF_UNUSED;
    for (unsigned k = 0; k < n; k++)
        if (!(p->flags[k] & infinite_flag) != !(isinf(far[k]) && far[k] > 0.0)) return SELFTEST_PROOF_FLAG;
    for (unsigned k = 0; k < n; k++)
        if (p->far_mixed[k] || !(p->far_plane[k] == (float)far[k])) return SELFTEST_PROOF_FAR;
    for (unsigned k = 0; k < n; k++)
        if (p->first_reset[k] != 1) return SELFTEST_PROOF_RESET;
    return p->depth_sum == depth_sum ? SELFTEST_PROOF_OK : SELFTEST_PROOF_DEPTH;
}
#endif
