/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The arithmetic of the temporal passes (temporal.c and its shaders) without the renderer, so a host test can run
   it. Matrices are 4 x 4 in memory order with row vectors, as the game keeps them: clip = position x matrix. The
   functions for the resolve mirror shaders/temporal_resolve.frag, which holds the same formulas in GLSL. */
#ifndef SNX_VK_TEMPORAL_MATH_H
#define SNX_VK_TEMPORAL_MATH_H
#include <math.h>
#include <string.h>

/* offset k of n of the jitter in pixels, within half a pixel each way: Halton bases 2 and 3 */
static inline void temporal_jitter(unsigned k, unsigned n, float out[2]) {
    static const unsigned bases[2] = {2, 3};
    for (int axis = 0; axis < 2; axis++) {
        float f = 1.0f, value = 0.0f;
        for (unsigned i = k % n + 1; i; i /= bases[axis]) {
            f /= (float)bases[axis];
            value += f * (float)(i % bases[axis]);
        }
        out[axis] = value - 0.5f;
    }
}
/* m x J, J the unit matrix with jx and jy in row 3: clip x and y gain jitter x w */
static inline void temporal_jitter_forward(float m[16], float jx, float jy) {
    for (int r = 0; r < 4; r++) {
        m[4 * r + 0] += jx * m[4 * r + 3];
        m[4 * r + 1] += jy * m[4 * r + 3];
    }
}
/* J's inverse x m, for the inverse of a matrix that got temporal_jitter_forward */
static inline void temporal_jitter_inverse(float m[16], float jx, float jy) {
    for (int c = 0; c < 4; c++) m[12 + c] -= jx * m[c] + jy * m[4 + c];
}
static inline void temporal_multiply_d(const double a[16], const double b[16], double out[16]) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            double sum = 0;
            for (int k = 0; k < 4; k++) sum += a[4 * r + k] * b[4 * k + c];
            out[4 * r + c] = sum;
        }
}
/* the inverse of an affine matrix (the translation in row 3): 0 when it has no volume */
static inline int temporal_affine_inverse_d(const double m[16], double out[16]) {
    const double a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
    const double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (!(fabs(det) > 1e-30)) return 0;
    const double r[9] = {(e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det,
                         (f * g - d * i) / det, (a * i - c * g) / det, (c * d - a * f) / det,
                         (d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det};
    memset(out, 0, 16 * sizeof *out);
    for (int row = 0; row < 3; row++)
        for (int col = 0; col < 3; col++) out[4 * row + col] = r[3 * row + col];
    for (int col = 0; col < 3; col++) out[12 + col] = -(m[12] * r[col] + m[13] * r[3 + col] + m[14] * r[6 + col]);
    out[15] = 1;
    return 1;
}
/* An object's two matrices for its vector draw. From this frame's clip position as drawn (with the jitter jx, jy in
   clip units): now takes the jitter off, before goes on through the inverse view-projection, the inverse of the
   object's world matrix, its world matrix a frame ago and last frame's view-projection. 0 when world has no inverse. */
static inline int temporal_object_matrices(const float inverse_vp[16], const float world[16],
                                           const float world_before[16], const float vp_before[16], double jx,
                                           double jy, float now[16], float before[16]) {
    double unjitter[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    double inverse[16], world_d[16], was[16], vp[16], back[16], a[16], b[16];
    unjitter[12] = -jx;
    unjitter[13] = -jy;
    for (int i = 0; i < 16; i++) {
        inverse[i] = (double)inverse_vp[i];
        world_d[i] = (double)world[i];
        was[i] = (double)world_before[i];
        vp[i] = (double)vp_before[i];
    }
    if (!temporal_affine_inverse_d(world_d, back)) return 0;
    temporal_multiply_d(unjitter, inverse, a);
    temporal_multiply_d(a, back, b);
    temporal_multiply_d(b, was, a);
    temporal_multiply_d(a, vp, b);
    for (int i = 0; i < 16; i++) {
        before[i] = (float)b[i];
        now[i] = (float)unjitter[i];
    }
    return 1;
}

/* The resolve. Catmull-Rom weights of the four texels around a position f (0 to 1) past the second one. */
static inline void temporal_catmull_weights(float f, float w[4]) {
    const float f2 = f * f, f3 = f2 * f;
    w[0] = -0.5f * f3 + f2 - 0.5f * f;
    w[1] = 1.5f * f3 - 2.5f * f2 + 1.0f;
    w[2] = -1.5f * f3 + 2.0f * f2 + 0.5f * f;
    w[3] = 0.5f * f3 - 0.5f * f2;
}
/* A colour outside the box lo to hi moved along the line toward the frame's colour until it is on the box. The
   frame's colour is taken into the box first, so the line ends inside. */
static inline void temporal_clip_toward(const float colour[3], const float frame[3], const float lo[3],
                                        const float hi[3], float out[3]) {
    float t = 1.0f, inside[3];
    for (int i = 0; i < 3; i++) {
        inside[i] = frame[i] < lo[i] ? lo[i] : frame[i] > hi[i] ? hi[i] : frame[i];
        const float away = colour[i] - inside[i];
        if (away > 0.000001f && (hi[i] - inside[i]) / away < t) t = (hi[i] - inside[i]) / away;
        if (away < -0.000001f && (lo[i] - inside[i]) / away < t) t = (lo[i] - inside[i]) / away;
    }
    for (int i = 0; i < 3; i++) out[i] = inside[i] + (colour[i] - inside[i]) * t;
}
/* The frame's share of the blend: 0.05 standing, 0.1 from two pixels of motion a frame on, and up to 0.2 where the
   clip changed the history's brightness (clipped: the relative change, 0.5 and more counts in full) */
static inline float temporal_frame_weight(float pixels, float clipped) {
    float t = pixels / 2.0f, c = clipped * 2.0f;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    if (c < 0.0f) c = 0.0f;
    if (c > 1.0f) c = 1.0f;
    const float base = 0.05f + 0.05f * t;
    return base + (0.2f - base) * c;
}
/* The blend with each colour weighted by 1 / (1 + its brightness), so a bright spark counts no more than its
   neighbours. weight is the frame's share. */
static inline void temporal_blend_by_brightness(const float history[3], const float frame[3], float weight,
                                                float out[3]) {
    const float lh = 0.25f * history[0] + 0.5f * history[1] + 0.25f * history[2];
    const float lf = 0.25f * frame[0] + 0.5f * frame[1] + 0.25f * frame[2];
    const float wh = (1.0f - weight) / (1.0f + lh), wf = weight / (1.0f + lf);
    for (int i = 0; i < 3; i++) out[i] = (history[i] * wh + frame[i] * wf) / (wh + wf);
}
#endif
