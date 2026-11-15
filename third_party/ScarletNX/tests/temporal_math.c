/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The arithmetic of the temporal passes (gpu/vk/temporal_math.h): the jitter, an object's vector matrices and the
   resolve's filter, clip and weights. */
#include "nx/gpu/vk/temporal_math.h"
#include "nx/gpu/vk/temporal_spatial.h"
#include "nx/gpu/vk/upscaler_selftest.h"
#include "nx/gpu/vk/upscaler_module.h"
#include "nx/gpu/vk/upscaler_needs.h"
#include "nx/gpu/vk/validation_lines.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

static int close_to(double a, double b, double tolerance) { return fabs(a - b) <= tolerance; }

/* row vector x matrix */
static void transform(const float p[4], const float m[16], double out[4]) {
    for (int c = 0; c < 4; c++) {
        out[c] = 0;
        for (int r = 0; r < 4; r++) out[c] += (double)p[r] * (double)m[4 * r + c];
    }
}
static void multiply(const float a[16], const float b[16], float out[16]) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            double sum = 0;
            for (int k = 0; k < 4; k++) sum += (double)a[4 * r + k] * (double)b[4 * k + c];
            out[4 * r + c] = (float)sum;
        }
}
/* a camera as the game keeps it: view-projection with reversed depth, z = 0.1 / distance, w = distance, and its
   inverse. The camera looks along +z from (x, y, 0) and scales x by sx and y by sy. */
static void camera(float x, float y, float sx, float sy, float vp[16], float inverse[16]) {
    const float m[16] = {sx, 0, 0, 0, 0, sy, 0, 0, 0, 0, 0, 1, -x * sx, -y * sy, 0.1f, 0};
    const float back[16] = {1 / sx, 0, 0, 0, 0, 1 / sy, 0, 0, 10 * x, 10 * y, 0, 10, 0, 0, 1, 0};
    memcpy(vp, m, sizeof m);
    memcpy(inverse, back, sizeof back);
}
/* an object's world matrix: turned by angle about y, scaled, moved */
static void world(float angle, float scale, float tx, float ty, float tz, float m[16]) {
    const float c = cosf(angle) * scale, s = sinf(angle) * scale;
    const float w[16] = {c, 0, -s, 0, 0, scale, 0, 0, s, 0, c, 0, tx, ty, tz, 1};
    memcpy(m, w, sizeof w);
}

static void test_jitter(void) {
    float sum[2] = {0, 0}, seen[8][2];
    for (unsigned k = 0; k < 8; k++) {
        float j[2];
        temporal_jitter(k, 8, j);
        assert(j[0] > -0.5f && j[0] < 0.5f && j[1] > -0.5f && j[1] < 0.5f);
        for (unsigned e = 0; e < k; e++) assert(seen[e][0] != j[0] || seen[e][1] != j[1]);
        seen[k][0] = j[0];
        seen[k][1] = j[1];
        sum[0] += j[0];
        sum[1] += j[1];
        float again[2];
        temporal_jitter(k + 8, 8, again); /* the sequence repeats after its length */
        assert(again[0] == j[0] && again[1] == j[1]);
    }
    /* eight offsets average close to the pixel's middle */
    assert(fabsf(sum[0] / 8) < 0.07f && fabsf(sum[1] / 8) < 0.07f);
}

static void test_jitter_matrices(void) {
    float vp[16], inverse[16], jittered[16], back[16], unit[16];
    camera(3, -2, 1.2f, 2.1f, vp, inverse);
    const float jx = 2.0f * 0.25f / 1920.0f, jy = 2.0f * -0.375f / 1080.0f;
    multiply(vp, inverse, unit); /* the test's own camera first */
    for (int i = 0; i < 16; i++) assert(close_to(unit[i], i % 5 == 0 ? 1.0 : 0.0, 1e-5));
    memcpy(jittered, vp, sizeof vp);
    memcpy(back, inverse, sizeof inverse);
    temporal_jitter_forward(jittered, jx, jy);
    temporal_jitter_inverse(back, jx, jy);
    /* a point's clip position moves by the jitter x w, and nothing else */
    const float p[4] = {4.5f, 1.25f, 20, 1};
    double plain[4], moved[4];
    transform(p, vp, plain);
    transform(p, jittered, moved);
    assert(close_to(moved[0], plain[0] + (double)jx * plain[3], 1e-5));
    assert(close_to(moved[1], plain[1] + (double)jy * plain[3], 1e-5));
    assert(close_to(moved[2], plain[2], 1e-7) && close_to(moved[3], plain[3], 1e-7));
    /* in pixels of a 1920 x 1080 viewport that is the offset asked for */
    assert(close_to((moved[0] / moved[3] - plain[0] / plain[3]) * 0.5 * 1920.0, 0.25, 1e-3));
    assert(close_to((moved[1] / moved[3] - plain[1] / plain[3]) * 0.5 * 1080.0, -0.375, 1e-3));
    /* the shifted inverse is the inverse of the shifted matrix */
    multiply(jittered, back, unit);
    for (int i = 0; i < 16; i++) assert(close_to(unit[i], i % 5 == 0 ? 1.0 : 0.0, 1e-5));
}

static void test_affine_inverse(void) {
    float w[16];
    double m[16], inverse[16], product[16];
    world(0.7f, 1.5f, 12, -3, 40, w);
    for (int i = 0; i < 16; i++) m[i] = (double)w[i];
    assert(temporal_affine_inverse_d(m, inverse));
    temporal_multiply_d(m, inverse, product);
    for (int i = 0; i < 16; i++) assert(close_to(product[i], i % 5 == 0 ? 1.0 : 0.0, 1e-9));
    memset(m, 0, sizeof m); /* no volume: refused */
    m[15] = 1;
    assert(!temporal_affine_inverse_d(m, inverse));
}

/* A vertex of an object that moved and turned, under a camera that moved too: the matrices take its clip position
   as drawn this frame (with jitter) to its unjittered position now and to where it was a frame ago. */
static void test_object_matrices(void) {
    float vp_now[16], inverse_now[16], vp_before[16], unused[16], world_now[16], world_before[16];
    camera(3, -2, 1.2f, 2.1f, vp_now, inverse_now);
    camera(2.6f, -1.9f, 1.2f, 2.1f, vp_before, unused);
    world(0.70f, 1.5f, 12, -3, 40, world_now);
    world(0.62f, 1.5f, 11.4f, -3, 40.5f, world_before);
    const double jx = 2.0 * 0.25 / 1920.0, jy = 2.0 * -0.375 / 1080.0;
    float now[16], before[16], jittered[16];
    assert(temporal_object_matrices(inverse_now, world_now, world_before, vp_before, jx, jy, now, before));
    memcpy(jittered, vp_now, sizeof vp_now);
    temporal_jitter_forward(jittered, (float)jx, (float)jy);
    const float vertices[3][4] = {{0.5f, 1.0f, -0.25f, 1}, {-2.0f, 0.1f, 3.0f, 1}, {0, 0, 0, 1}};
    for (int v = 0; v < 3; v++) {
        double at[4], drawn[4], plain[4], was_at[4], was[4], got_now[4], got_before[4];
        transform(vertices[v], world_now, at);
        const float at_f[4] = {(float)at[0], (float)at[1], (float)at[2], 1};
        transform(at_f, jittered, drawn);
        transform(at_f, vp_now, plain);
        transform(vertices[v], world_before, was_at);
        const float was_f[4] = {(float)was_at[0], (float)was_at[1], (float)was_at[2], 1};
        transform(was_f, vp_before, was);
        /* what the fragment program has: the screen position and depth as drawn */
        const float clip[4] = {(float)(drawn[0] / drawn[3]), (float)(drawn[1] / drawn[3]),
                               (float)(drawn[2] / drawn[3]), 1};
        transform(clip, now, got_now);
        transform(clip, before, got_before);
        assert(close_to(got_now[0] / got_now[3], plain[0] / plain[3], 2e-5));
        assert(close_to(got_now[1] / got_now[3], plain[1] / plain[3], 2e-5));
        assert(close_to(got_before[0] / got_before[3], was[0] / was[3], 2e-4));
        assert(close_to(got_before[1] / got_before[3], was[1] / was[3], 2e-4));
        /* the object did move on screen, so the check is not empty */
        assert(fabs(was[0] / was[3] - plain[0] / plain[3]) > 1e-3);
    }
    /* an object that did not move under a camera that did not: no vector */
    assert(temporal_object_matrices(inverse_now, world_now, world_now, vp_now, 0, 0, now, before));
    const float clip[4] = {0.3f, -0.2f, 0.004f, 1};
    double a[4], b[4];
    transform(clip, now, a);
    transform(clip, before, b);
    assert(close_to(a[0] / a[3], b[0] / b[3], 1e-5) && close_to(a[1] / a[3], b[1] / b[3], 1e-5));
    /* a world matrix without volume is refused */
    float flat[16] = {0};
    flat[15] = 1;
    assert(!temporal_object_matrices(inverse_now, flat, world_before, vp_before, 0, 0, now, before));
}

/* A skinned vertex: its position in the object's space changed (a limb moved), the object moved and the camera
   moved. The three matrices give where the game drew it, where it is without jitter and where it was. */
static void test_skin_matrices(void) {
    float vp_now[16], vp_before[16], unused[16], world_now[16], world_before[16], drawn[16], now[16], before[16];
    camera(3, -2, 1.2f, 2.1f, vp_now, unused);
    camera(2.6f, -1.9f, 1.2f, 2.1f, vp_before, unused);
    world(0.70f, 1.5f, 12, -3, 40, world_now);
    world(0.62f, 1.5f, 11.4f, -3, 40.5f, world_before);
    const double jx = 2.0 * 0.25 / 1920.0, jy = 2.0 * -0.375 / 1080.0;
    temporal_skin_matrices(vp_now, world_now, world_before, vp_before, jx, jy, drawn, now, before);
    const float limb_now[4] = {0.5f, 1.0f, -0.25f, 1}, limb_before[4] = {0.42f, 1.1f, -0.2f, 1};
    double at[4], was_at[4], plain[4], was[4], got_drawn[4], got_now[4], got_before[4];
    transform(limb_now, world_now, at);
    transform(limb_before, world_before, was_at);
    const float at_f[4] = {(float)at[0], (float)at[1], (float)at[2], 1};
    const float was_f[4] = {(float)was_at[0], (float)was_at[1], (float)was_at[2], 1};
    transform(at_f, vp_now, plain);
    transform(was_f, vp_before, was);
    transform(limb_now, drawn, got_drawn);
    transform(limb_now, now, got_now);
    transform(limb_before, before, got_before);
    for (int i = 0; i < 4; i++) {
        assert(close_to(got_now[i], plain[i], 1e-4));
        assert(close_to(got_before[i], was[i], 1e-4));
    }
    /* as drawn: the unjittered clip position moved by the jitter x w, depth and w untouched */
    assert(close_to(got_drawn[0], plain[0] + jx * plain[3], 1e-4));
    assert(close_to(got_drawn[1], plain[1] + jy * plain[3], 1e-4));
    assert(close_to(got_drawn[2], plain[2], 1e-6) && close_to(got_drawn[3], plain[3], 1e-4));
    /* It agrees with the rigid path: the object's matrices take the drawn clip position to the same unjittered
       position, and for a vertex that did not move in the object's space to the same position before. */
    float inverse_now[16], object_now[16], object_before[16];
    camera(3, -2, 1.2f, 2.1f, unused, inverse_now);
    assert(temporal_object_matrices(inverse_now, world_now, world_before, vp_before, jx, jy, object_now,
                                    object_before));
    const float clip[4] = {(float)(got_drawn[0] / got_drawn[3]), (float)(got_drawn[1] / got_drawn[3]),
                           (float)(got_drawn[2] / got_drawn[3]), 1};
    double rigid_now[4], rigid_before[4], still_before[4];
    transform(clip, object_now, rigid_now);
    transform(clip, object_before, rigid_before);
    transform(limb_now, before, still_before);
    assert(close_to(rigid_now[0] / rigid_now[3], got_now[0] / got_now[3], 2e-5));
    assert(close_to(rigid_before[0] / rigid_before[3], still_before[0] / still_before[3], 2e-4));
    assert(close_to(rigid_before[1] / rigid_before[3], still_before[1] / still_before[3], 2e-4));
}

static void test_catmull(void) {
    float w[4];
    temporal_catmull_weights(0.0f, w); /* on a texel: that texel alone */
    assert(w[0] == 0 && w[1] == 1 && w[2] == 0 && w[3] == 0);
    for (int i = 0; i <= 20; i++) {
        const float f = (float)i / 20.0f;
        temporal_catmull_weights(f, w);
        assert(close_to(w[0] + w[1] + w[2] + w[3], 1.0, 1e-6));
        /* a straight ramp of texel values comes back exact, and so does a parabola */
        assert(close_to(-1 * w[0] + 0 * w[1] + 1 * w[2] + 2 * w[3], f, 1e-6));
        assert(close_to(1 * w[0] + 0 * w[1] + 1 * w[2] + 4 * w[3], f * f, 1e-6));
        /* the outer two are never positive: that is what keeps an edge sharper than a linear fetch */
        assert(w[0] <= 0 && w[3] <= 0);
    }
    /* The shader's five fetches leave the four corners out and divide by the weights it used: a flat picture stays
       flat, and the weight left out is small. */
    for (int i = 0; i <= 10; i++)
        for (int j = 0; j <= 10; j++) {
            float x[4], y[4];
            temporal_catmull_weights((float)i / 10.0f, x);
            temporal_catmull_weights((float)j / 10.0f, y);
            const float x12 = x[1] + x[2], y12 = y[1] + y[2];
            const float used = x12 * y[0] + x[0] * y12 + x12 * y12 + x[3] * y12 + x12 * y[3];
            assert(used > 0.9f && used < 1.0001f);
        }
}

static void test_clip(void) {
    const float lo[3] = {0, -1, -1}, hi[3] = {2, 1, 1};
    float out[3];
    const float inside[3] = {1, 0.5f, -0.5f}, frame[3] = {1, 0, 0};
    temporal_clip_toward(inside, frame, lo, hi, out); /* inside the box: unchanged */
    assert(out[0] == inside[0] && out[1] == inside[1] && out[2] == inside[2]);
    const float outside[3] = {5, 0, 0};
    temporal_clip_toward(outside, frame, lo, hi, out); /* outside: onto the box, on the line to the frame's colour */
    assert(close_to(out[0], 2, 1e-6) && close_to(out[1], 0, 1e-6) && close_to(out[2], 0, 1e-6));
    const float corner[3] = {5, 4, 0};
    temporal_clip_toward(corner, frame, lo, hi, out);
    assert(out[0] <= 2.00001f && out[1] <= 1.00001f);
    /* still on the line from the frame's colour: the same direction, shorter */
    assert(close_to((out[0] - 1) * 4, (out[1] - 0) * 4, 1e-5));
    assert(close_to(out[0], 2, 1e-5) || close_to(out[1], 1, 1e-5));
    /* a frame colour outside the box is taken into it first, so the result is in the box whatever comes in */
    const float far_frame[3] = {9, 9, 9};
    temporal_clip_toward(corner, far_frame, lo, hi, out);
    for (int i = 0; i < 3; i++) assert(out[i] >= lo[i] - 1e-5f && out[i] <= hi[i] + 1e-5f);
}

static void test_weights(void) {
    assert(close_to(temporal_frame_weight(0, 0), 0.05, 1e-6));    /* standing */
    assert(close_to(temporal_frame_weight(1, 0), 0.075, 1e-6));   /* half way */
    assert(close_to(temporal_frame_weight(2, 0), 0.1, 1e-6));     /* moving */
    assert(close_to(temporal_frame_weight(500, 0), 0.1, 1e-6));   /* motion alone never gives more */
    assert(close_to(temporal_frame_weight(0, 0.5f), 0.2, 1e-6));  /* the clip changed the history a lot */
    assert(close_to(temporal_frame_weight(50, 9.0f), 0.2, 1e-6)); /* and never more than 0.2 */
    assert(temporal_frame_weight(50, 0.1f) > 0.1f && temporal_frame_weight(50, 0.1f) < 0.2f);
    assert(close_to(temporal_frame_weight(-3, -1), 0.05, 1e-6));
    /* blend by brightness: equal colours stay, the plain blend comes back for equal brightness */
    float out[3];
    const float grey[3] = {0.5f, 0.5f, 0.5f}, other[3] = {0.7f, 0.5f, 0.3f};
    temporal_blend_by_brightness(grey, grey, 0.1f, out);
    assert(close_to(out[0], 0.5, 1e-6) && close_to(out[1], 0.5, 1e-6) && close_to(out[2], 0.5, 1e-6));
    temporal_blend_by_brightness(grey, other, 0.1f, out); /* both have brightness 0.5 */
    assert(close_to(out[0], 0.9 * 0.5 + 0.1 * 0.7, 1e-6) && close_to(out[2], 0.9 * 0.5 + 0.1 * 0.3, 1e-6));
    /* a spark of 100 in the frame over a history of 0.5: the plain blend jumps to 10.45, this one stays near */
    const float spark[3] = {100, 100, 100};
    temporal_blend_by_brightness(grey, spark, 0.1f, out);
    assert(out[0] > 0.5f && out[0] < 0.7f);
    /* and a spark in the history fades fast: one frame of 0.5 takes it from 100 to 12.3, where the plain blend
       keeps 90 */
    temporal_blend_by_brightness(spark, grey, 0.1f, out);
    assert(close_to(out[0], 12.28, 0.05));
}

static void test_sharpen(void) {
    /* a flat place stays as it is, whatever the amount */
    assert(close_to(temporal_sharpen(0.4f, 1.6f, 0.4f, 0.4f, 1.0f), 0.4, 1e-6));
    /* amount 0 changes nothing */
    assert(close_to(temporal_sharpen(0.5f, 1.2f, 0.2f, 0.6f, 0.0f), 0.5, 1e-6));
    /* a pixel brighter than its neighbours gets brighter, a darker one darker, more with more amount */
    const float bright_half = temporal_sharpen(0.5f, 4 * 0.4f, 0.4f, 0.6f, 0.5f);
    const float bright_full = temporal_sharpen(0.5f, 4 * 0.4f, 0.4f, 0.6f, 1.0f);
    assert(bright_half > 0.5f && bright_full > bright_half);
    assert(temporal_sharpen(0.4f, 4 * 0.5f, 0.3f, 0.5f, 1.0f) < 0.4f);
    /* never outside the neighbourhood's least and greatest */
    for (int i = 0; i <= 10; i++) {
        const float c = (float)i / 10.0f;
        const float least = c < 0.3f ? c : 0.3f, greatest = c > 0.7f ? c : 0.7f;
        const float out = temporal_sharpen(c, 4 * 0.5f, least, greatest, 1.0f);
        assert(out >= least && out <= greatest);
    }
    /* a neighbourhood that already spans the range (a hard edge) is left alone */
    assert(close_to(temporal_sharpen(0.6f, 0.0f + 1.0f + 0.5f + 0.5f, 0.0f, 1.0f, 1.0f), 0.6, 1e-6));
}

/* Whole colours in the picture's range: the squeeze ties the channels, so the scalar bound alone let the middle of
   this neighbourhood come out at blue 0.83 with no blue under 1 around it. */
static void test_sharpen_rgb(void) {
    const float n[9][3] = {{0, 1, 20}, {4, 0, 100}, {0.1f, 4, 100}, {1, 0.5f, 1}, {20, 0, 1},
                           {0, 0.5f, 20}, {100, 4, 4}, {4, 1, 20}, {0.1f, 0.1f, 4}};
    float out[3];
    temporal_sharpen_rgb(n, 1.0f, out);
    for (int c = 0; c < 3; c++) {
        float least = n[0][c], greatest = n[0][c];
        for (int i = 0; i < 9; i++) {
            least = n[i][c] < least ? n[i][c] : least;
            greatest = n[i][c] > greatest ? n[i][c] : greatest;
        }
        assert(out[c] >= least && out[c] <= greatest);
    }
    assert(close_to(out[2], 1.0, 1e-6)); /* held at the least blue */
    /* a flat place and amount 0 change nothing */
    float flat[9][3];
    for (int i = 0; i < 9; i++) flat[i][0] = 3, flat[i][1] = 0.5f, flat[i][2] = 12;
    temporal_sharpen_rgb(flat, 1.0f, out);
    assert(close_to(out[0], 3, 1e-4) && close_to(out[1], 0.5, 1e-4) && close_to(out[2], 12, 1e-4));
    temporal_sharpen_rgb(n, 0.0f, out);
    assert(close_to(out[0], 20, 1e-3) && close_to(out[1], 0, 1e-6) && close_to(out[2], 1, 1e-4));
    /* a bright pixel among darker ones still gets brighter */
    float spot[9][3];
    for (int i = 0; i < 9; i++) spot[i][0] = spot[i][1] = spot[i][2] = i == 0 ? 2.0f : i == 4 ? 1.0f : 0.8f;
    temporal_sharpen_rgb(spot, 1.0f, out);
    assert(out[0] > 1.0f && out[0] <= 2.0f);
}

/* The camera's vector of a pixel equals the vector of an object that stands still there, at every offset of the
   jitter and under a camera that moved, turned its scale and changed its projection. */
static void test_camera_points(void) {
    float vp_now[16], inverse_now[16], vp_before[16], unused[16], still[16];
    camera(3, -2, 1.2f, 2.1f, vp_now, inverse_now);
    world(0.3f, 1.0f, 5, 1, 30, still);
    const float cameras[3][4] = {{3, -2, 1.2f, 2.1f}, {2.6f, -1.9f, 1.2f, 2.1f}, {3.5f, -2.5f, 2.4f, 1.7f}};
    for (int k = 0; k < 3; k++) {
        camera(cameras[k][0], cameras[k][1], cameras[k][2], cameras[k][3], vp_before, unused);
        for (unsigned phase = 0; phase < 8; phase++) {
            float pixels[2], now_m[16], before_m[16];
            temporal_jitter(phase, 8, pixels);
            const double jx = 2.0 * (double)pixels[0] / 1920.0, jy = 2.0 * (double)pixels[1] / 1080.0;
            assert(temporal_object_matrices(inverse_now, still, still, vp_before, jx, jy, now_m, before_m));
            const float clip[4] = {0.31f, -0.44f, 0.004f, 1};
            double a[4], b[4], now[2], before[2];
            transform(clip, now_m, a);
            transform(clip, before_m, b);
            assert(temporal_camera_points(inverse_now, vp_before, clip[0], clip[1], clip[2], jx, jy, now, before));
            assert(close_to(now[0], a[0] / a[3], 1e-6) && close_to(now[1], a[1] / a[3], 1e-6));
            assert(close_to(before[0], b[0] / b[3], 2e-5) && close_to(before[1], b[1] / b[3], 2e-5));
            /* a camera that did not change gives no vector, with or without jitter */
            if (k == 0) assert(close_to(now[0], before[0], 1e-6) && close_to(now[1], before[1], 1e-6));
            else assert(fabs(now[0] - before[0]) > 1e-3);
        }
    }
    /* the audit's case: the projection doubles x, the pixel is at 0.1 without jitter, the jitter is a quarter pixel
       of 1920. The vector is 96 pixels, not 96.25. */
    float one[16], inverse_one[16], two[16];
    camera(0, 0, 1, 1, one, inverse_one);
    camera(0, 0, 2, 1, two, unused);
    const double jx = 2.0 * 0.25 / 1920.0;
    double now[2], before[2];
    assert(temporal_camera_points(inverse_one, two, 0.1 + jx, 0.0, 0.01, jx, 0.0, now, before));
    assert(close_to((now[0] - before[0]) * 0.5 * 1920.0, -96.0, 1e-3));
    /* behind the camera of a frame ago */
    float back[16];
    const float turned[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, -1, 0, 0, 0.1f, 0};
    memcpy(back, turned, sizeof back);
    assert(!temporal_camera_points(inverse_one, back, 0.1, 0.0, 0.01, 0, 0, now, before));
}

/* One frame of the gate as temporal.c drives it: a draw that binds the camera's block before the depth pass and is
   not tied to the scene (early), the depth pass's viewport (none when the frame has no draw into the scene depth),
   the G-buffer's. Returns 1 for jitter + 2 for steady. No draw of the frame may get jitter when early. */
static int gate_frame(TemporalGate* g, unsigned frame, int early, const TemporalViewport* depth_pass,
                      const TemporalViewport* gbuffer, float w, float h) {
    if (early) {
        assert(!temporal_gate_jitter(g, frame));
        assert(temporal_gate_early(g, frame));
        assert(!temporal_gate_jitter(g, frame));
    }
    int jitter = 0;
    if (depth_pass) jitter = temporal_gate_decide(g, frame, depth_pass, w, h);
    assert(jitter == temporal_gate_jitter(g, frame) && !(early && jitter));
    /* once decided, a later binder that is not tied to the scene changes nothing */
    if (depth_pass) assert(!temporal_gate_early(g, frame) && jitter == temporal_gate_jitter(g, frame));
    if (gbuffer) temporal_gate_scene(g, frame, gbuffer);
    return jitter + 2 * temporal_gate_steady(g, frame);
}

static void test_gate(void) {
    const TemporalViewport full = {0, 0, 1920, 1080}, small = {0, 0, 1440, 810}, shifted = {8, 0, 1920, 1080};
    const TemporalViewport up = {0, 1080, 1920, -1080}, up_small = {0, 810, 1440, -810}, up_wrong = {0, 0, 1920, -1080};
    for (int sign = 0; sign < 2; sign++) {
        const TemporalViewport *f = sign ? &up : &full, *s = sign ? &up_small : &small;
        TemporalGate g = {0};
        /* the first frame has no frame before: no jitter, not steady. Then steady. */
        assert(gate_frame(&g, 1, 0, f, f, 1920, 1080) == 0);
        assert(gate_frame(&g, 2, 0, f, f, 1920, 1080) == 3);
        assert(gate_frame(&g, 3, 0, f, f, 1920, 1080) == 3);
        /* full -> 0.75 -> full: off while small, off for the first full frame after (not last frame's), then on */
        assert(gate_frame(&g, 4, 0, s, s, 1920, 1080) == 0);
        assert(gate_frame(&g, 5, 0, s, s, 1920, 1080) == 0);
        assert(gate_frame(&g, 6, 0, f, f, 1920, 1080) == 0);
        assert(gate_frame(&g, 7, 0, f, f, 1920, 1080) == 3);
        /* a binder before the depth pass that is not tied to the scene: the whole frame has none. The depth pass
           still tells the viewport, so the frame after can be steady. */
        assert(gate_frame(&g, 8, 1, f, f, 1920, 1080) == 0);
        assert(gate_frame(&g, 9, 0, f, f, 1920, 1080) == 3);
        /* a frame without a draw into the scene depth: nothing decided, not steady, and the next has no last frame */
        assert(gate_frame(&g, 10, 1, NULL, NULL, 1920, 1080) == 0);
        assert(gate_frame(&g, 11, 0, f, f, 1920, 1080) == 0);
        assert(gate_frame(&g, 12, 0, f, f, 1920, 1080) == 3);
        /* the depth pass and the G-buffer disagree: jittered already, but not steady */
        assert(gate_frame(&g, 13, 0, f, s, 1920, 1080) == 1);
        assert(gate_frame(&g, 14, 0, f, f, 1920, 1080) == 3);
        /* an unknown target size */
        assert(gate_frame(&g, 15, 0, f, f, 0, 0) == 0);
        assert(gate_frame(&g, 16, 0, f, f, 1920, 1080) == 3);
        /* a gap of a frame */
        assert(gate_frame(&g, 18, 0, f, f, 1920, 1080) == 0);
    }
    /* whole means from the origin: a shifted viewport and a flipped one from the wrong edge are not */
    TemporalGate g = {0};
    gate_frame(&g, 1, 0, &shifted, &shifted, 1920, 1080);
    assert(gate_frame(&g, 2, 0, &shifted, &shifted, 1920, 1080) == 0);
    gate_frame(&g, 3, 0, &up_wrong, &up_wrong, 1920, 1080);
    assert(gate_frame(&g, 4, 0, &up_wrong, &up_wrong, 1920, 1080) == 0);
}

static void test_mip_bias(void) {
    assert(close_to(temporal_mip_bias(0.0f, -0.5f, 15.0f), -0.5, 1e-6));  /* ours alone */
    assert(close_to(temporal_mip_bias(-0.25f, -0.5f, 15.0f), -0.75, 1e-6)); /* added to the game's */
    assert(close_to(temporal_mip_bias(1.0f, -0.5f, 15.0f), 0.5, 1e-6));
    assert(close_to(temporal_mip_bias(-3.9f, -0.5f, 4.0f), -4.0, 1e-6)); /* within what the device takes */
    assert(close_to(temporal_mip_bias(5.0f, 0.0f, 4.0f), 4.0, 1e-6));
}

/* A tiny scene for the learned pairs: each frame says which of two slots is bound, by which draw, with which
   matrix. The slots are judged at the end of each frame as temporal.c does, and the choice a draw of the frame
   would get is returned. */
typedef struct {
    unsigned at, before_at, turns, stays, judged_at;
    unsigned long long draw, before_draw;
    float world[16], before[16];
} TestSlot;
static int pair_frame(TestSlot slot[2], unsigned frame, int bind0, int bind1, unsigned long long draw0,
                      unsigned long long draw1, float x0, float x1, int ask) {
    const int bind[2] = {bind0, bind1};
    const unsigned long long draw[2] = {draw0, draw1};
    const float x[2] = {x0, x1};
    int choice = -1;
    frame += 10; /* 0 is never */
    for (int i = 0; i < 2; i++) {
        if (!bind[i]) continue;
        TestSlot *a = &slot[i], *q = &slot[!i];
        memcpy(a->before, a->world, sizeof a->before);
        a->before_at = a->at;
        a->before_draw = a->draw;
        world(0, 1, x[i], 0, 10, a->world);
        a->at = frame;
        a->draw = draw[i];
        if (i != ask) continue;
        /* the draw's choice, made before the frame is complete (slot 1 is bound after slot 0) */
        const int own_last = a->before_at && a->before_at + 1 == frame;
        const int partner_now = q->at == frame;
        const int partner_last = q->at + 1 == frame || (partner_now && q->before_at + 1 == frame);
        choice = temporal_before_choice(own_last, a->judged_at + 1 == frame ? a->stays : 0,
                                        a->before_draw == a->draw, partner_last, partner_now,
                                        q->judged_at + 1 == frame && q->at + 1 == frame ? q->turns : 0,
                                        q->draw == a->draw);
        if (choice == TEMPORAL_BEFORE_PARTNER &&
            temporal_at_rest_alone(a->world, a->before_at + 2 == frame ? a->before : NULL, q->world))
            choice = TEMPORAL_BEFORE_NONE;
    }
    for (int i = 0; i < 2; i++) {
        TestSlot *a = &slot[i], *q = &slot[!i];
        if (a->at != frame) continue;
        TemporalSlotFrame f = {0};
        f.bound_before = a->before_at && a->before_at + 1 == frame;
        f.partner_now = q->at == frame;
        f.partner_before = q->at + 1 == frame || (f.partner_now && q->before_at + 1 == frame);
        f.same_as_partner = q->at + 1 == frame && q->draw == a->draw;
        f.same_as_own = f.bound_before && a->before_draw == a->draw;
        f.at_rest_alone = q->at + 1 == frame &&
                          temporal_at_rest_alone(a->world, a->before_at + 2 == frame ? a->before : NULL, q->world);
        f.partner_turns = q->judged_at + 1 == frame && q->at + 1 == frame ? q->turns : 0;
        f.own_stays = a->judged_at + 1 == frame ? a->stays : 0;
        temporal_pair_judge(&f, &a->turns, &a->stays);
        a->judged_at = frame;
    }
    return choice;
}

static void test_pairs(void) {
    const unsigned long long program = 0xaf37fc644332986cull;
    const unsigned long long mesh = temporal_draw_signature(0x50e238000ull, 0x51000000ull, 300, 0, program);
    const unsigned long long other = temporal_draw_signature(0x50e238000ull, 0x51000000ull, 300, 300, program);
    assert(mesh && other && mesh != other);
    assert(temporal_draw_signature(0, 0, 0, 0, 0) != 0);
    assert(temporal_draw_signature(1, 2, 3, 4, 5) != temporal_draw_signature(2, 1, 3, 4, 5));

    /* one object that takes turns and moves steadily: no matrix for its first four frames, the other slot's from
       the fifth */
    TestSlot s[2];
    memset(s, 0, sizeof s);
    for (unsigned f = 1; f <= 8; f++) {
        const int slot = (int)(f & 1);
        const int got = pair_frame(s, f, slot == 0, slot == 1, mesh, mesh, 0.1f * (float)f, 0.1f * (float)f, slot);
        assert(got == (f <= 4 ? (f == 1 ? TEMPORAL_BEFORE_NONE : TEMPORAL_BEFORE_LEARNING) : TEMPORAL_BEFORE_PARTNER));
    }
    /* both slots bound in one frame: two objects. The pair is gone at once and learned again from nothing. */
    assert(pair_frame(s, 9, 1, 1, mesh, mesh, 0.9f, 0.9f, 1) == TEMPORAL_BEFORE_NONE);
    for (unsigned f = 10; f <= 13; f++)
        assert(pair_frame(s, f, !(f & 1), f & 1, mesh, mesh, 0.1f * (float)f, 0.1f * (float)f, (int)(f & 1)) !=
               TEMPORAL_BEFORE_PARTNER);
    assert(pair_frame(s, 14, 1, 0, mesh, mesh, 1.4f, 0, 0) == TEMPORAL_BEFORE_PARTNER);
    /* another draw in the slot: not this pair's, and the pair is learned again */
    assert(pair_frame(s, 15, 0, 1, mesh, other, 0, 1.5f, 1) == TEMPORAL_BEFORE_OTHER);
    assert(pair_frame(s, 16, 1, 0, other, other, 1.6f, 0, 0) == TEMPORAL_BEFORE_LEARNING);
    /* a missed turn */
    memset(s, 0, sizeof s);
    for (unsigned f = 1; f <= 6; f++) pair_frame(s, f, !(f & 1), f & 1, mesh, mesh, (float)f, (float)f, 0);
    assert(pair_frame(s, 8, 1, 0, mesh, mesh, 8, 0, 0) == TEMPORAL_BEFORE_NONE);
    assert(pair_frame(s, 9, 0, 1, mesh, mesh, 0, 9, 1) == TEMPORAL_BEFORE_LEARNING);

    /* a slot that takes no turns: its own matrix from the fifth frame, whatever the neighbour does */
    memset(s, 0, sizeof s);
    for (unsigned f = 1; f <= 8; f++) {
        const int got = pair_frame(s, f, 1, f >= 6, mesh, other, 0.1f * (float)f, 5, 0);
        assert(got == (f <= 4 ? (f == 1 ? TEMPORAL_BEFORE_NONE : TEMPORAL_BEFORE_LEARNING) : TEMPORAL_BEFORE_OWN));
    }

    /* two still things with the same mesh, each bound every other frame (the block 50d61f500 pattern): each slot's
       matrix is its own of two frames ago and not the other's, so they never become a pair */
    memset(s, 0, sizeof s);
    for (unsigned f = 1; f <= 12; f++)
        assert(pair_frame(s, f, !(f & 1), f & 1, mesh, mesh, 360.97f, 370.06f, (int)(f & 1)) !=
               TEMPORAL_BEFORE_PARTNER);
    /* The cost, expected: one object that truly moves 0, 1, 0, 1 looks the same and gets the camera's vector. */
    memset(s, 0, sizeof s);
    for (unsigned f = 1; f <= 12; f++)
        assert(pair_frame(s, f, !(f & 1), f & 1, mesh, mesh, (float)(f & 1), (float)(f & 1), (int)(f & 1)) !=
               TEMPORAL_BEFORE_PARTNER);
    /* and a pair at rest (every matrix equal) stays a pair */
    memset(s, 0, sizeof s);
    for (unsigned f = 1; f <= 8; f++) {
        const int got = pair_frame(s, f, !(f & 1), f & 1, mesh, mesh, 2, 2, (int)(f & 1));
        assert((got == TEMPORAL_BEFORE_PARTNER) == (f >= 5));
    }
    /* The limit that stays: two different things with one mesh that swap visibility every frame and move are
       taken for one object. Nothing at this layer tells them apart. */
    memset(s, 0, sizeof s);
    int fooled = 0;
    for (unsigned f = 1; f <= 8; f++)
        fooled |= pair_frame(s, f, !(f & 1), f & 1, mesh, mesh, 1 + 0.01f * (float)f, 3 + 0.01f * (float)f,
                             (int)(f & 1)) == TEMPORAL_BEFORE_PARTNER;
    assert(fooled);
}

/* One frame at one capture address: who captures into it, in order, and each one's G-buffer draw reading the
   capture right after. Returns for character `ask` what its draw gets: 1 last frame's copy, 0 or -1 none. The
   copy kept is the last capture's, as in temporal.c. *holder says whose positions the copy of a frame ago held. */
static unsigned stream_generation;
static int stream_frame(TemporalStreamCopy copy[2], unsigned frame, const int* who, int count, int ask, int* held,
                        int* holder) {
    TemporalStreamCopy *now = &copy[frame & 1], *last = &copy[!(frame & 1)];
    int got = -2;
    *holder = *held;
    for (int i = 0; i < count; i++) {
        temporal_stream_captured(now, &stream_generation, frame);
        temporal_stream_copied(now);
        const unsigned long long pair = 0x1000ull * (unsigned)(who[i] + 1), draw = 77; /* one mesh: the same draw */
        temporal_stream_used(now, frame, pair, 0, draw);
        if (who[i] == ask) got = temporal_stream_before(now, last, frame, pair, 0, draw);
    }
    if (count) *held = who[count - 1];
    return got;
}

static void test_streams(void) {
    const int a[1] = {0}, b[1] = {1}, ab[2] = {0, 1}, ba[2] = {1, 0};
    TemporalStreamCopy copy[2];
    int held = -1, holder;
    /* one character alone: its own copy from the second frame */
    memset(copy, 0, sizeof copy);
    assert(stream_frame(copy, 11, a, 1, 0, &held, &holder) == 0);
    assert(stream_frame(copy, 12, a, 1, 0, &held, &holder) == 1 && holder == 0);
    assert(stream_frame(copy, 13, a, 1, 0, &held, &holder) == 1 && holder == 0);
    /* the audit's trace: A then B capture into the address every frame. The copy is B's. A never gets it, and
       neither does B while the address is captured twice. */
    memset(copy, 0, sizeof copy);
    held = -1;
    for (unsigned f = 21; f <= 26; f++) {
        assert(stream_frame(copy, f, ab, 2, 0, &held, &holder) <= 0);
        held = holder;
        assert(stream_frame(copy, f, ab, 2, 1, &held, &holder) <= 0);
    }
    /* B alone, then A alone, at the same address: the copy of a frame ago is B's, so A gets none. A frame later
       the copy is A's. */
    memset(copy, 0, sizeof copy);
    held = -1;
    assert(stream_frame(copy, 31, b, 1, 1, &held, &holder) == 0);
    assert(stream_frame(copy, 32, b, 1, 1, &held, &holder) == 1 && holder == 1);
    assert(stream_frame(copy, 33, a, 1, 0, &held, &holder) == 0 && holder == 1);
    assert(stream_frame(copy, 34, a, 1, 0, &held, &holder) == 1 && holder == 0);
    /* twice in the frame before only, or in this one only: none either way */
    assert(stream_frame(copy, 35, ba, 2, 0, &held, &holder) == -1);
    assert(stream_frame(copy, 36, a, 1, 0, &held, &holder) == -1);
    assert(stream_frame(copy, 37, a, 1, 0, &held, &holder) == 1 && holder == 0);
    /* a gap of a frame */
    assert(stream_frame(copy, 39, a, 1, 0, &held, &holder) == 0);
    /* From one capture to two, late: A captures and its draw takes last frame's copy, then B captures into the
       address in the same frame. That is seen before the vectors are read, and the copy is no good a frame later. */
    assert(stream_frame(copy, 40, a, 1, 0, &held, &holder) == 1);
    assert(stream_frame(copy, 41, a, 1, 0, &held, &holder) == 1 && !temporal_stream_conflict(&copy[41 & 1], 41));
    temporal_stream_captured(&copy[41 & 1], &stream_generation, 41);
    temporal_stream_copied(&copy[41 & 1]);
    temporal_stream_used(&copy[41 & 1], 41, 0x2000, 0, 77);
    assert(temporal_stream_conflict(&copy[41 & 1], 41));
    assert(stream_frame(copy, 42, a, 1, 0, &held, &holder) == -1);
    assert(stream_frame(copy, 43, a, 1, 0, &held, &holder) == 1 && holder == 0);
    /* two captures a frame in changing order: never a copy, whichever came last */
    for (unsigned f = 44; f <= 49; f++) {
        assert(stream_frame(copy, f, f & 1 ? ab : ba, 2, 0, &held, &holder) == -1);
        assert(temporal_stream_conflict(&copy[f & 1], f));
    }
    /* B's tag on last frame's copy does not serve A even with one capture in each frame */
    assert(stream_frame(copy, 50, b, 1, 1, &held, &holder) == -1);
    assert(stream_frame(copy, 51, b, 1, 1, &held, &holder) == 1);
    assert(stream_frame(copy, 52, a, 1, 0, &held, &holder) == 0 && holder == 1);
    /* across many orders, a draw that gets a copy always gets its own character's */
    memset(copy, 0, sizeof copy);
    held = -1;
    unsigned seed = 12345;
    for (unsigned f = 100; f < 600; f++) {
        seed = seed * 1103515245u + 12345u;
        const int pick = (int)(seed >> 16) % 5, ask = (int)(seed >> 20) % 2;
        const int* who = pick == 0 ? a : pick == 1 ? b : pick == 2 ? ab : pick == 3 ? ba : a;
        const int count = pick == 4 ? 0 : pick >= 2 ? 2 : 1;
        if (stream_frame(copy, f, who, count, ask, &held, &holder) == 1) assert(holder == ask);
    }
}

/* The keeping of copies as temporal.c does it, with the two buffers' bytes modelled by whose positions they hold
   (-1 unknown). A capture may be too large to keep, may fail to get its buffers, or may need larger buffers, which
   are made anew for both copies. The character's draw then reads its capture: 1 when it gets last frame's copy. */
typedef struct {
    TemporalStreamCopy copy[2];
    unsigned generation, capacity;
    int bytes[2];
} TestKeep;
enum { KEEP_OK, KEEP_TOO_LARGE, KEEP_NO_MEMORY };
static int keep_capture(TestKeep* k, unsigned frame, int who, unsigned size, int outcome) {
    const int turn = (int)(frame & 1);
    temporal_stream_captured(&k->copy[turn], &k->generation, frame); /* before anything can fail */
    if (outcome == KEEP_OK && size > k->capacity) {
        for (int i = 0; i < 2; i++) {
            temporal_stream_poison(&k->copy[i]);
            k->bytes[i] = -1;
        }
        k->capacity = size;
    }
    if (outcome == KEEP_OK) {
        k->bytes[turn] = who;
        temporal_stream_copied(&k->copy[turn]);
    }
    const unsigned long long pair = 0x1000ull * (unsigned)(who + 1), draw = 77;
    temporal_stream_used(&k->copy[turn], frame, pair, 0, draw);
    const int got = temporal_stream_before(&k->copy[turn], &k->copy[!turn], frame, pair, 0, draw);
    /* the one thing that must never happen: a draw handed another character's bytes, or unknown ones */
    if (got == 1) assert(k->bytes[!turn] == who);
    return got;
}

static void test_stream_keeping(void) {
    TestKeep k;
    /* A is kept for two frames. Then B's capture is too large to keep: the copy of that turn still holds A's
       bytes of two frames ago, and B's draw must not put its tag on them. */
    memset(&k, 0, sizeof k);
    k.bytes[0] = k.bytes[1] = -1;
    assert(keep_capture(&k, 10, 0, 100, KEEP_OK) == 0);
    assert(keep_capture(&k, 11, 0, 100, KEEP_OK) == 1);
    assert(keep_capture(&k, 12, 1, 100, KEEP_TOO_LARGE) == 0);
    assert(k.bytes[0] == 0 && !temporal_stream_valid(&k.copy[0]) && !k.copy[0].owners);
    /* B again, kept this time: last frame's copy is unknown, so none. And A, back a frame later, gets none of B's */
    assert(keep_capture(&k, 13, 1, 100, KEEP_OK) == 0);
    assert(keep_capture(&k, 14, 1, 100, KEEP_OK) == 1);
    assert(keep_capture(&k, 15, 0, 100, KEEP_OK) == 0);
    assert(keep_capture(&k, 16, 0, 100, KEEP_OK) == 1);
    /* the buffers could not be made: content unknown this frame and the next */
    assert(keep_capture(&k, 17, 0, 100, KEEP_NO_MEMORY) == 0);
    assert(keep_capture(&k, 18, 0, 100, KEEP_OK) == 0);
    assert(keep_capture(&k, 19, 0, 100, KEEP_OK) == 1);
    /* Growth: A's larger capture makes both buffers anew. Last frame's bytes are gone with the old buffer, the
       counts are not: a second capture in the same frame is still a second capture. */
    assert(keep_capture(&k, 20, 0, 400, KEEP_OK) == 0);
    assert(k.copy[1].at == 19 && k.copy[1].captures == 1 && !temporal_stream_valid(&k.copy[1]));
    assert(keep_capture(&k, 21, 0, 400, KEEP_OK) == 1);
    assert(keep_capture(&k, 22, 0, 400, KEEP_OK) == 1);
    assert(keep_capture(&k, 22, 1, 900, KEEP_OK) == -1); /* a second capture, and it grows the buffers */
    assert(k.copy[0].captures == 2 && temporal_stream_conflict(&k.copy[0], 22));
    assert(keep_capture(&k, 23, 1, 900, KEEP_OK) == -1); /* last frame had two */
    assert(keep_capture(&k, 24, 1, 900, KEEP_OK) == 1);
    /* a failed capture after a kept one in the same frame leaves nothing to tag or use */
    assert(keep_capture(&k, 25, 1, 900, KEEP_OK) == 1);
    assert(keep_capture(&k, 25, 0, 900, KEEP_TOO_LARGE) == -1);
    assert(!temporal_stream_valid(&k.copy[1]) && !k.copy[1].owners);
    assert(keep_capture(&k, 26, 0, 900, KEEP_OK) == -1);
    /* any order of characters, sizes and failures: the assert in keep_capture holds throughout */
    unsigned seed = 4242, frame = 100;
    int handed = 0;
    for (int i = 0; i < 20000; i++) {
        seed = seed * 1103515245u + 12345u;
        const int who = (int)(seed >> 16) & 1, outcome = (seed >> 18) % 13 == 0 ? KEEP_TOO_LARGE
                                                         : (seed >> 18) % 17 == 0 ? KEEP_NO_MEMORY : KEEP_OK;
        const unsigned size = 100 + ((seed >> 8) % 7 == 0 ? k.capacity : 0);
        frame += (seed >> 24) % 5 ? 1 : (seed >> 27) % 3; /* mostly the next frame, sometimes the same, or a gap */
        handed += keep_capture(&k, frame, who, size, outcome) == 1;
    }
    assert(handed > 1000); /* and copies are handed out, so the check is not empty */
}

/* The table of kept copies: a few slots that addresses share, as temporal.c's probe of eight. A capture finds its
   slot or takes one, and when the buffers must grow and cannot, the slot stays as it was. */
#define TEST_SLOTS 3
typedef struct {
    unsigned long long address[TEST_SLOTS];
    TemporalStreamCopy copy[TEST_SLOTS][2];
    unsigned generation[TEST_SLOTS], capacity[TEST_SLOTS];
} TestTable;
static int table_capture(TestTable* t, unsigned long long address, unsigned frame, unsigned size, int memory) {
    unsigned newest[TEST_SLOTS];
    for (int i = 0; i < TEST_SLOTS; i++) newest[i] = temporal_stream_newest(t->copy[i]);
    const int i = temporal_slot_pick(t->address, newest, TEST_SLOTS, address, frame, 1);
    if (i < 0) return -1;
    if (t->address[i] != address) {
        memset(t->copy[i], 0, sizeof t->copy[i]);
        t->generation[i] = t->capacity[i] = 0;
        t->address[i] = address;
    }
    temporal_stream_captured(&t->copy[i][frame & 1], &t->generation[i], frame);
    if (size > t->capacity[i]) {
        if (!memory) return i; /* the buffers could not grow: the slot keeps what it had */
        temporal_stream_poison(&t->copy[i][0]);
        temporal_stream_poison(&t->copy[i][1]);
        t->capacity[i] = size;
    }
    temporal_stream_copied(&t->copy[i][frame & 1]);
    return i;
}

static void test_stream_table(void) {
    TestTable t;
    memset(&t, 0, sizeof t);
    /* The audit's case. A is kept at X in frames 10 and 11, and its draw of frame 11 takes frame 10's copy. Then a
       second, larger capture into X cannot get its buffers, and a new address needs a slot. X had a capture this
       frame, so its slot is not given away, and the late conflict is still seen. */
    const int x = table_capture(&t, 0x1000, 10, 100, 1);
    assert(x >= 0 && table_capture(&t, 0x1000, 11, 100, 1) == x);
    temporal_stream_used(&t.copy[x][0], 10, 7, 0, 77);
    assert(temporal_stream_before(&t.copy[x][1], &t.copy[x][0], 11, 7, 0, 77) == 1);
    const TemporalStreamCopy* emitted = &t.copy[x][1];
    assert(table_capture(&t, 0x1000, 11, 900, 0) == x);
    assert(t.capacity[x] == 100 && temporal_stream_valid(&t.copy[x][0])); /* last frame's copy untouched */
    assert(table_capture(&t, 0x2000, 11, 100, 1) != x);
    assert(table_capture(&t, 0x3000, 11, 100, 1) != x);
    /* the table is full of this frame's captures: a fourth address gets none, and nothing is wiped */
    assert(table_capture(&t, 0x4000, 11, 100, 1) == -1);
    assert(t.address[x] == 0x1000 && emitted->at == 11 && temporal_stream_conflict(emitted, 11));
    /* nor a frame later, nor 599 frames later. After 600 frames without a capture the oldest slot goes. */
    assert(table_capture(&t, 0x4000, 12, 100, 1) == -1);
    assert(table_capture(&t, 0x4000, 11 + TEMPORAL_SLOT_FRAMES - 1, 100, 1) == -1);
    const int y = table_capture(&t, 0x4000, 11 + TEMPORAL_SLOT_FRAMES, 100, 1);
    assert(y >= 0 && t.address[y] == 0x4000 && t.copy[y][(11 + TEMPORAL_SLOT_FRAMES) & 1].captures == 1);
    /* a capture that was never copied still counts as the slot's newest event */
    memset(&t, 0, sizeof t);
    const int z = table_capture(&t, 0x1000, 20, 100, 0);
    assert(z >= 0 && !temporal_stream_valid(&t.copy[z][0]) && temporal_stream_newest(t.copy[z]) == 20);
    table_capture(&t, 0x2000, 20, 100, 1);
    table_capture(&t, 0x3000, 20, 100, 1);
    assert(table_capture(&t, 0x4000, 21, 100, 1) == -1 && t.address[z] == 0x1000);
    /* a lookup makes nothing */
    unsigned newest[TEST_SLOTS] = {0};
    assert(temporal_slot_pick(t.address, newest, TEST_SLOTS, 0x9000, 5000, 0) == -1);
}

static void test_origin_shift(void) {
    float vp_now[16], vp_before[16], unused[16], w_now[16], w_before[16];
    camera(0, 0, 1, 1, vp_now, unused);
    camera(0, 0, 1, 1, vp_before, unused);
    world(0, 1, 2, 1, 10, w_now);
    world(0, 1, 2, 1, 10, w_before);
    assert(close_to(temporal_origin_shift(vp_now, w_now, vp_before, w_before, 1920, 1080), 0, 1e-9));
    /* one unit to the side at a distance of 10: 0.1 of the half width */
    world(0, 1, 1, 1, 10, w_before);
    assert(close_to(temporal_origin_shift(vp_now, w_now, vp_before, w_before, 1920, 1080), 96, 1e-3));
    /* the camera moved instead, by 0.5 up */
    camera(0, 0.5f, 1, 1, vp_before, unused);
    assert(close_to(temporal_origin_shift(vp_now, w_now, vp_before, w_now, 1920, 1080), 27, 1e-3));
    world(0, 1, 2, 1, -1, w_before);
    assert(temporal_origin_shift(vp_now, w_now, vp_now, w_before, 1920, 1080) < 0);
}

static void camera_reference_multiply(const double a[16], const double b[16], double out[16]) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            out[4*r+c] = 0;
            for (int k = 0; k < 4; k++) out[4*r+c] += a[4*r+k] * b[4*k+c];
        }
}
static void camera_reference_make(double x, double y, double z, double yaw, double sx, double sy, float vp[16], float inv[16]) {
    const double c = cos(yaw), s = sin(yaw);
    const double t[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, -x,-y,-z,1};
    const double ti[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, x,y,z,1};
    const double r[16] = {c,0,-s,0, 0,1,0,0, s,0,c,0, 0,0,0,1};
    const double ri[16] = {c,0,s,0, 0,1,0,0, -s,0,c,0, 0,0,0,1};
    const double p[16] = {sx,0,0,0, 0,sy,0,0, 0,0,0,1, 0,0,0.1,0};
    const double pi[16] = {1/sx,0,0,0, 0,1/sy,0,0, 0,0,0,10, 0,0,1,0};
    double v[16], vi[16], a[16], b[16];
    camera_reference_multiply(t,r,v); camera_reference_multiply(ri,ti,vi);
    camera_reference_multiply(v,p,a); camera_reference_multiply(pi,vi,b);
    for (int i = 0; i < 16; i++) { vp[i] = (float)a[i]; inv[i] = (float)b[i]; }
}
static void camera_reference_project(const double point[4], const float m[16], double p[4]) {
    for (int c = 0; c < 4; c++) {
        p[c] = 0;
        for (int k = 0; k < 4; k++) p[c] += point[k] * m[4*k+c];
    }
}
static void test_camera_world_points(void) {
    const double settings[4][6] = {
        {3,-2,1,0.2,1.2,2.1}, {2.6,-1.9,1,0.2,1.2,2.1},
        {3.5,-2.5,0.7,-0.15,2.4,1.7}, {-1,0,2,0.4,0.9,1.2}
    };
    const double points[3][4] = {{2,1,20,1}, {-2,-1,30,1}, {4,3,40,1}};
    unsigned checks = 0;
    for (int current = 0; current < 4; current++)
        for (int previous = 0; previous < 4; previous++) {
            float vp[16], inv[16], before_vp[16], unused[16];
            const double *a = settings[current], *b = settings[previous];
            camera_reference_make(a[0],a[1],a[2],a[3],a[4],a[5],vp,inv);
            camera_reference_make(b[0],b[1],b[2],b[3],b[4],b[5],before_vp,unused);
            for (int p = 0; p < 3; p++) {
                double at[4], was[4];
                camera_reference_project(points[p],vp,at); camera_reference_project(points[p],before_vp,was);
                assert(at[3] > 0 && was[3] > 0);
                for (int sign = -1; sign <= 1; sign += 2)
                    for (unsigned phase = 0; phase < 8; phase++) {
                        float jitter[2]; temporal_jitter(phase,8,jitter);
                        const double jx = 2.0*jitter[0]/1920, jy = 2.0*jitter[1]/1080;
                        double now[2], before[2];
                        assert(temporal_camera_points(inv,before_vp,at[0]/at[3]+jx,at[1]/at[3]+jy,
                                                      at[2]/at[3],jx,jy,now,before));
                        for (int axis = 0; axis < 2; axis++) {
                            const double viewport = axis ? sign*1080.0 : 1920.0;
                            const double expected = (at[axis]/at[3]-was[axis]/was[3])*0.5*viewport;
                            const double got = (now[axis]-before[axis])*0.5*viewport;
                            assert(fabs(got-expected) < 0.002);
                        }
                        checks++;
                    }
            }
        }
    assert(checks == 768);
}

/* The resolve's choice of a texel as the shader makes it: the pixel's own, or with the option (8) the one of the
   nearest depth in the 3x3 around it, clamped at the image's edge (reversed depth: the largest). */
static int class_selected(int options, const float* depth, int w, int h, int x, int y) {
    int at = y * w + x;
    if (!(options & 8)) return at;
    float nearest = depth[at];
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            const int cx = x + dx < 0 ? 0 : x + dx >= w ? w - 1 : x + dx;
            const int cy = y + dy < 0 ? 0 : y + dy >= h ? h - 1 : y + dy;
            if (depth[cy * w + cx] > nearest) {
                nearest = depth[cy * w + cx];
                at = cy * w + cx;
            }
        }
    return at;
}

static void test_classes(void) {
    const double none[2] = {0.0, 0.0};
    /* the bound: an eighth of the height, nothing when off */
    assert(temporal_reject_bound(1, 1080.0) == 135.0 && temporal_reject_bound(1, 2160.0) == 270.0);
    assert(temporal_reject_bound(0, 2160.0) == 0.0);
    for (int tall = 0; tall < 2; tall++) {
        const double w = tall ? 3840.0 : 1920.0, h = tall ? 2160.0 : 1080.0, bound = temporal_reject_bound(1, h);
        /* met exactly is kept, one pixel over is rejected, along either axis */
        const double at_x[2] = {bound / w, 0.0}, over_x[2] = {(bound + 1.0) / w, 0.0};
        const double at_y[2] = {0.0, -bound / h}, over_y[2] = {0.0, -(bound + 1.0) / h};
        assert(temporal_vector_class(1, 1, at_x, none, w, h, bound) == TEMPORAL_CLASS_OBJECT);
        assert(temporal_vector_class(1, 1, over_x, none, w, h, bound) == TEMPORAL_CLASS_REJECTED);
        assert(temporal_vector_class(1, 1, at_y, none, w, h, bound) == TEMPORAL_CLASS_OBJECT);
        assert(temporal_vector_class(1, 1, over_y, none, w, h, bound) == TEMPORAL_CLASS_REJECTED);
        /* a pan: a camera vector of four bounds that the object shares is no reason, the object's own part is */
        const double pan[2] = {4.0 * bound / w, 0.0}, with[2] = {(4.5 * bound) / w, 0.0};
        const double far[2] = {(5.5 * bound) / w, 0.0};
        assert(temporal_vector_class(1, 1, pan, pan, w, h, bound) == TEMPORAL_CLASS_OBJECT);
        assert(temporal_vector_class(1, 1, with, pan, w, h, bound) == TEMPORAL_CLASS_OBJECT);
        assert(temporal_vector_class(1, 1, far, pan, w, h, bound) == TEMPORAL_CLASS_REJECTED);
        /* the length is in pixels: the same part of the image is longer across than down */
        const double part_x[2] = {0.11, 0.0}, part_y[2] = {0.0, 0.11};
        assert(temporal_vector_class(1, 1, part_x, none, w, h, bound) == TEMPORAL_CLASS_REJECTED); /* 0.11 w > h/8 */
        assert(temporal_vector_class(1, 1, part_y, none, w, h, bound) == TEMPORAL_CLASS_OBJECT);   /* 0.11 h < h/8 */
        /* a diagonal: 0.7 of the bound each way is 0.99 of it, 0.75 each way is 1.06 */
        const double in[2] = {0.7 * bound / w, 0.7 * bound / h}, out[2] = {-0.75 * bound / w, 0.75 * bound / h};
        assert(temporal_vector_class(1, 1, in, none, w, h, bound) == TEMPORAL_CLASS_OBJECT);
        assert(temporal_vector_class(1, 1, out, none, w, h, bound) == TEMPORAL_CLASS_REJECTED);
        /* without a camera vector an object's pixel is rejected, however short its vector */
        assert(temporal_vector_class(1, 0, none, none, w, h, bound) == TEMPORAL_CLASS_REJECTED);
        /* Fails closed: a NaN or an infinity in any input, and infinity less infinity, are rejected. The test
           that keeps a vector is the positive one, so a comparison with a NaN cannot keep it. */
        const double bad[3] = {NAN, INFINITY, -INFINITY};
        for (int b = 0; b < 3; b++)
            for (int slot = 0; slot < 4; slot++) {
                double v[2] = {0.0, 0.0}, c[2] = {0.0, 0.0};
                (slot < 2 ? v : c)[slot & 1] = bad[b];
                assert(temporal_vector_class(1, 1, v, c, w, h, bound) == TEMPORAL_CLASS_REJECTED);
                assert(temporal_vector_class(1, 0, v, c, w, h, bound) == TEMPORAL_CLASS_REJECTED);
                /* the same broken number on both sides: their difference is a NaN or 0, never a reason to keep */
                v[slot & 1] = c[slot & 1] = bad[b];
                assert(temporal_vector_class(1, 1, v, c, w, h, bound) == TEMPORAL_CLASS_REJECTED);
            }
        /* finite inputs whose difference in pixels overflows */
        const double huge[2] = {1e308, 0.0}, other[2] = {-1e308, 0.0};
        assert(temporal_vector_class(1, 1, huge, other, w, h, bound) == TEMPORAL_CLASS_REJECTED);
        /* off: nothing is rejected. A pixel no object owns is the camera's whatever else is said. */
        assert(temporal_vector_class(1, 1, over_x, none, w, h, 0.0) == TEMPORAL_CLASS_OBJECT);
        assert(temporal_vector_class(1, 0, over_x, none, w, h, 0.0) == TEMPORAL_CLASS_OBJECT);
        assert(temporal_vector_class(0, 1, over_x, none, w, h, bound) == TEMPORAL_CLASS_CAMERA);
        assert(temporal_vector_class(0, 0, over_x, none, w, h, bound) == TEMPORAL_CLASS_CAMERA);
    }
    /* the three values are apart, and only alpha says the class: a depth before of 2 or draw number 2 in blue, or
       a vector of 2, does not make an object's texel a rejected one */
    assert(TEMPORAL_CLASS_CAMERA != TEMPORAL_CLASS_OBJECT && TEMPORAL_CLASS_OBJECT != TEMPORAL_CLASS_REJECTED &&
           TEMPORAL_CLASS_CAMERA != TEMPORAL_CLASS_REJECTED);
    const double two[2] = {2.0, 2.0};
    float texel[3][4];
    for (int c = 0; c < 3; c++) {
        temporal_motion_texel(two, 2.0, c, texel[c]);
        assert(temporal_motion_class(texel[c]) == c);
        assert(texel[c][0] == 2.0f && texel[c][1] == 2.0f);
    }
    assert(texel[TEMPORAL_CLASS_OBJECT][2] == 2.0f && texel[TEMPORAL_CLASS_REJECTED][2] == 2.0f);
    assert(texel[TEMPORAL_CLASS_CAMERA][2] == 0.0f);
    /* the resolve: the pixel's own texel or the selected one rejected gives the frame as drawn, nothing else does */
    for (int centre = 0; centre < 3; centre++)
        for (int selected = 0; selected < 3; selected++)
            assert(temporal_current_only(texel[centre], texel[selected]) ==
                   (centre == TEMPORAL_CLASS_REJECTED || selected == TEMPORAL_CLASS_REJECTED));
    /* A 3x3 image under every combination of the resolve's options. The middle is nearest. Case 0: the middle is
       rejected. Case 1: a corner is rejected and the middle is a good object's. */
    for (int options = 0; options < 32; options++)
        for (int which = 0; which < 2; which++) {
            float depth[9], image[9][4];
            for (int i = 0; i < 9; i++) {
                depth[i] = i == 4 ? 0.5f : 0.1f + 0.01f * (float)i;
                temporal_motion_texel(none, 0.0, i == 4 ? TEMPORAL_CLASS_OBJECT : TEMPORAL_CLASS_CAMERA, image[i]);
            }
            const int marked = which ? 0 : 4;
            temporal_motion_texel(none, 0.3, TEMPORAL_CLASS_REJECTED, image[marked]);
            for (int y = 0; y < 3; y++)
                for (int x = 0; x < 3; x++) {
                    const int at = y * 3 + x, pick = class_selected(options, depth, 3, 3, x, y);
                    const int only = temporal_current_only(image[at], image[pick]);
                    /* with the option every pixel takes the middle's vector: a rejected middle takes them all
                       along, a good middle never restores the history of the rejected corner */
                    if (options & 8) assert(pick == 4);
                    if (!which) assert(only == ((options & 8) || at == 4));
                    else assert(only == (at == 0));
                }
        }
    /* at the image's edge the clamped neighbourhood still finds the marker: a rejected corner that is nearest */
    {
        float depth[4] = {0.9f, 0.1f, 0.1f, 0.1f}, image[4][4];
        for (int i = 0; i < 4; i++)
            temporal_motion_texel(none, 0.3, i ? TEMPORAL_CLASS_CAMERA : TEMPORAL_CLASS_REJECTED, image[i]);
        for (int at = 0; at < 4; at++) {
            assert(temporal_current_only(image[at], image[class_selected(8, depth, 2, 2, at % 2, at / 2)]));
            assert(temporal_current_only(image[at], image[class_selected(0, depth, 2, 2, at % 2, at / 2)]) == !at);
        }
    }
}

/* The links. A small scene driven the way the renderer drives the record: each draw asks for a link first, then goes
   into the frame's record, and the frame is judged at its end. Frames count from 10. */
static TemporalFrame g_lf[4];
static TemporalLinkScratch g_ls;
typedef unsigned long long ull;

static TemporalFrame* lrec(unsigned f) {
    TemporalFrame* r = &g_lf[f & 3];
    return f && r->frame == f ? r : NULL;
}
static void lreset(void) { memset(g_lf, 0, sizeof g_lf); }
static void lbegin(unsigned f) { temporal_frame_begin(&g_lf[f & 3], f); }
static void lend(unsigned f) { temporal_links_judge(lrec(f), lrec(f - 1), lrec(f - 2), &g_ls); }
/* one draw of a block with its origin at x. The linked slot's record when a link gives the matrix, else NULL. */
static const TemporalBlock* ldraw(unsigned f, ull address, unsigned size, ull signature, float x, int affine,
                                  int served, int* why) {
    float world[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, 0, 0, 1};
    TemporalFrame* now = lrec(f);
    const TemporalBlock* mine = temporal_frame_find(now, address);
    const TemporalBlock* q = NULL;
    int reason = TEMPORAL_LINK_NONE;
    if (!served && affine)
        q = temporal_link_before(now, lrec(f - 1), lrec(f - 2), address, size, mine ? mine->count : 0, signature,
                                 world, &reason);
    TemporalBlock* b = temporal_frame_bind(now, address, size, world, affine, signature);
    if (b && served) b->served = 1;
    if (why) *why = reason;
    return b ? q : NULL;
}
/* the plain case: one usable draw of a block that moves a tenth of a unit a frame */
static const TemporalBlock* lmove(unsigned f, ull address, ull signature) {
    return ldraw(f, address, 0x200, signature, 0.1f * (float)f, 1, 0, NULL);
}
/* the G-buffer's end: the frame judged as it is, and whether the draw's link still stands */
static int lholds(unsigned f, ull address, const TemporalBlock* q) {
    lend(f);
    return q && temporal_link_holds(lrec(f), address, q->address);
}

/* the judging written the long way, to hold the indexed one against */
static int naive_in(const TemporalFrame* f, ull address) {
    for (unsigned i = 0; f && i < f->n; i++)
        if (f->blocks[i].address == address) return 1;
    return 0;
}
static int naive_same(const TemporalBlock* a, const TemporalBlock* b) {
    int same = a->size == b->size && a->count == b->count && a->sum == b->sum;
    for (unsigned i = 0; same && i < a->count && i < 8; i++) same = a->first[i] == b->first[i];
    return same;
}
static void naive_judge(const TemporalFrame* now, const TemporalFrame* before, const TemporalFrame* older, ull* link,
                        unsigned* turns) {
    for (unsigned i = 0; i < now->n; i++) link[i] = turns[i] = 0;
    if (!now->complete || !before || !before->complete || before->frame + 1 != now->frame) return;
    if (older && older->frame + 2 != now->frame) older = NULL;
    for (unsigned i = 0; i < now->n; i++) {
        const TemporalBlock *p = &now->blocks[i], *q = NULL, *own = NULL;
        unsigned forward = 0, back = 0;
        if (naive_in(before, p->address)) continue;
        for (unsigned j = 0; j < before->n; j++)
            if (!naive_in(now, before->blocks[j].address) && naive_same(p, &before->blocks[j])) {
                forward++;
                q = &before->blocks[j];
            }
        if (forward != 1) continue;
        for (unsigned j = 0; j < now->n; j++)
            back += !naive_in(before, now->blocks[j].address) && naive_same(q, &now->blocks[j]);
        if (back != 1 || !p->usable || !q->usable || p->served || q->served) continue;
        for (unsigned j = 0; older && j < older->n; j++)
            if (older->blocks[j].address == p->address) own = &older->blocks[j];
        if (own && !memcmp(p->world, own->world, sizeof p->world) && memcmp(p->world, q->world, sizeof p->world))
            continue;
        link[i] = q->address;
        turns[i] = (q->link == p->address ? q->turns : 0) + 1;
    }
}

static void test_links(void) {
    const ull A = 0x50c60fb00ull, S1 = 0x1111, S2 = 0x2222;
    int why;
    /* The slots seen in play: 0x300, 0x400, 0x600, 0x1b00 and about 1 MB apart. Four frames of turns, and the
       fifth frame's draw has the other slot's matrix of the frame before. */
    const ull apart[5] = {0x300, 0x400, 0x600, 0x1b00, 0x114a00};
    for (int k = 0; k < 5; k++) {
        lreset();
        for (unsigned i = 0; i < 8; i++) {
            const unsigned f = 10 + i;
            const ull at = i & 1 ? A + apart[k] : A;
            lbegin(f);
            const TemporalBlock* q = ldraw(f, at, 0x200, S1, 0.1f * (float)f, 1, 0, &why);
            assert((q != NULL) == (i >= 4));
            if (i == 3) assert(why == TEMPORAL_LINK_LEARNING);
            if (q) {
                /* the one candidate each frame is the block's own other slot, which counts like any other */
                assert(q->address == (i & 1 ? A : A + apart[k]));
                assert(q->world[12] == 0.1f * (float)(f - 1)); /* the matrix of the frame before */
                assert(lholds(f, at, q));
            }
            lend(f);
        }
    }
    /* A stranger between the two slots: two objects two sizes apart, the other's slot one size away. Each finds
       its own. */
    lreset();
    for (unsigned i = 0; i < 8; i++) {
        const unsigned f = 10 + i;
        lbegin(f);
        const TemporalBlock* q1 = lmove(f, i & 1 ? A + 0x400 : A, S1);
        const TemporalBlock* q2 = lmove(f, i & 1 ? A + 0x600 : A + 0x200, S2);
        assert((q1 != NULL) == (i >= 4) && (q2 != NULL) == (i >= 4));
        if (q1) assert(q1->address == (i & 1 ? A : A + 0x400) && q2->address == (i & 1 ? A + 0x200 : A + 0x600));
        lend(f);
    }
    /* Two of one mesh, in step: each side sees two candidates every frame, no link ever. The same when the other
       one is served by a pair, or has a matrix that is not affine: it still counts. */
    for (int kind = 0; kind < 3; kind++) {
        lreset();
        for (unsigned i = 0; i < 12; i++) {
            const unsigned f = 10 + i;
            lbegin(f);
            assert(!lmove(f, i & 1 ? A + 0x400 : A, S1));
            assert(!ldraw(f, i & 1 ? A + 0x90400 : A + 0x90000, 0x200, S1, 7.0f + 0.1f * (float)f, kind != 2,
                          kind == 1, NULL));
            lend(f);
            for (unsigned b = 0; b < lrec(f)->n; b++) assert(!lrec(f)->blocks[b].link);
        }
    }
    /* a slot that is not usable never gets or gives a link, alone as it is */
    lreset();
    for (unsigned i = 0; i < 12; i++) {
        lbegin(10 + i);
        assert(!ldraw(10 + i, i & 1 ? A + 0x400 : A, 0x200, S1, 0.1f * (float)i, !(i & 1), 0, NULL));
        lend(10 + i);
        assert(!lrec(10 + i)->blocks[0].link);
    }
    /* The order of the draws in a frame does not matter: three objects, one of them with a twin, in every order */
    for (int order = 0; order < 7; order++) {
        lreset();
        for (unsigned i = 0; i < 10; i++) {
            const unsigned f = 10 + i;
            const ull at[4] = {i & 1 ? A + 0x400 : A, i & 1 ? A + 0x600 : A + 0x200, i & 1 ? A + 0x9400 : A + 0x9000,
                               i & 1 ? A + 0x9a00 : A + 0x9600};
            const ull draws[4] = {S1, S2, 0x3333, 0x3333};
            const TemporalBlock* q[4];
            lbegin(f);
            for (int n = 0; n < 4; n++) {
                /* orders 0 to 5 are fixed turns of the four, order 6 changes with the frame */
                const int which = (n * (order % 2 ? 3 : 1) + (order < 6 ? order : (int)i)) % 4;
                q[which] = lmove(f, at[which], draws[which]);
            }
            assert((q[0] != NULL) == (i >= 4) && (q[1] != NULL) == (i >= 4) && !q[2] && !q[3]);
            if (q[0]) assert(lholds(f, at[0], q[0]) && lholds(f, at[1], q[1]));
            lend(f);
        }
    }
    /* What ends a link that was used in frame 16, found at the G-buffer's end. After slot A's draw with the link:
       0 nothing, 1 a second block with the same draws (two claim the other slot in 16, two candidates in 17), 2 the
       other slot bound too, 3 one more draw of the block, 4 the record runs full, 5 and 6 the frame marked not
       seen whole (table, early draw), 7 the other slot bound with a size the pairs leave alone, 8 the block so. */
    for (int late = 0; late < 9; late++) {
        lreset();
        for (unsigned i = 0; i < 6; i++) {
            lbegin(10 + i);
            lmove(10 + i, i & 1 ? A + 0x400 : A, S1);
            lend(10 + i);
        }
        lbegin(16);
        const TemporalBlock* q = lmove(16, A, S1);
        assert(q && q->address == A + 0x400);
        if (late == 1) lmove(16, A + 0x7000, S1);
        if (late == 2) lmove(16, A + 0x400, S1);
        if (late == 3) lmove(16, A, S2);
        if (late == 4)
            for (unsigned n = 0; n < TEMPORAL_BLOCKS; n++) lmove(16, 0x600000000ull + 0x200 * n, 0x9000 + n);
        if (late == 5 || late == 6) lrec(16)->complete = 0;
        if (late == 7) assert(!ldraw(16, A + 0x400, 32, S1, 0.0f, 0, 0, NULL));
        if (late == 8) assert(!ldraw(16, A, 32, S1, 0.0f, 0, 0, NULL));
        if (late == 4) assert(!lrec(16)->complete && lrec(16)->n == TEMPORAL_BLOCKS);
        assert(lholds(16, A, q) == !late);
        lend(16);
        /* after a frame that was not seen whole every link is gone, and the turns start again from nothing */
        if (late >= 4 && late <= 6)
            for (unsigned b = 0; b < lrec(16)->n; b++) assert(!lrec(16)->blocks[b].link && !lrec(16)->blocks[b].turns);
        /* The frames after. Undisturbed the link goes on. Else it is learned again over four frames of turns, 17
           to 20, and a draw has a link again in frame 21. */
        for (unsigned f = 17; f < 24; f++) {
            lbegin(f);
            q = ldraw(f, f & 1 ? A + 0x400 : A, 0x200, S1, 0.1f * (float)f, 1, 0, &why);
            if (f == 17 && late >= 4 && late <= 6) assert(why == TEMPORAL_LINK_UNKNOWN);
            assert((q != NULL) == (!late || f >= 21));
            lend(f);
        }
    }
    /* another size, another draw at that place in the order, a ninth draw: no link for the draw */
    lreset();
    for (unsigned i = 0; i < 6; i++) {
        lbegin(10 + i);
        lmove(10 + i, i & 1 ? A + 0x400 : A, S1);
        lend(10 + i);
    }
    lbegin(16);
    assert(!ldraw(16, A, 0x100, S1, 1.6f, 1, 0, &why) && why == TEMPORAL_LINK_NONE);
    lbegin(16);
    assert(!ldraw(16, A, 0x200, S2, 1.6f, 1, 0, &why) && why == TEMPORAL_LINK_OTHER);
    lbegin(16);
    assert(ldraw(16, A, 0x200, S1, 1.6f, 1, 0, &why) && why == TEMPORAL_LINK_USED);
    assert(!ldraw(16, A, 0x200, S1, 1.6f, 1, 0, &why) && why == TEMPORAL_LINK_OTHER); /* the slot had one draw */
    /* a missed turn: the same slot twice in a row has no link, and the turns start again */
    lreset();
    for (unsigned i = 0; i < 14; i++) {
        const unsigned f = 10 + i, turn = i < 7 ? i : i - 1; /* frame 17 repeats frame 16's slot */
        lbegin(f);
        const TemporalBlock* q = lmove(f, turn & 1 ? A + 0x400 : A, S1);
        assert((q != NULL) == ((i >= 4 && i < 7) || i >= 11));
        lend(f);
    }
    /* At rest alone: two slots that each keep their own matrix are two things, not one that moves. One thing at
       rest in both slots is linked, its matrix a frame ago being the same. */
    for (int one = 0; one < 2; one++) {
        lreset();
        for (unsigned i = 0; i < 10; i++) {
            lbegin(10 + i);
            const float x = one || !(i & 1) ? 1.0f : 2.0f;
            const TemporalBlock* q = ldraw(10 + i, i & 1 ? A + 0x400 : A, 0x200, S1, x, 1, 0, NULL);
            assert((q != NULL) == (one && i >= 4));
            lend(10 + i);
        }
    }
    /* The other slot replaced while the link is learned: the turns with the old slot do not count for the new
       one. Frame 13 brings another address for the second slot, and the first draw with a link is in frame 16,
       not 14. Its skinned copies are tagged with both of the link's slots: the copy of the frame before a link's
       first use has no tag, so the first use is refused and the next one is owned. */
    lreset();
    {
        TemporalStreamCopy copy[2];
        unsigned generation = 0;
        const ull B = A + 0x400, C = A + 0x2000;
        memset(copy, 0, sizeof copy);
        for (unsigned i = 0; i < 9; i++) {
            const unsigned f = 10 + i;
            const ull at = !(i & 1) ? A : i < 3 ? B : C;
            lbegin(f);
            const TemporalBlock* q = lmove(f, at, S1);
            assert((q != NULL) == (i >= 6));
            /* the character's positions are captured and copied each frame, and the draw reads them */
            temporal_stream_captured(&copy[f & 1], &generation, f);
            temporal_stream_copied(&copy[f & 1]);
            if (q) {
                /* the tag is both slots, the lower first, whichever of them the draw is at */
                const ull low = at < q->address ? at : q->address, high = at < q->address ? q->address : at;
                assert(low == A && high == C);
                temporal_stream_used(&copy[f & 1], f, low, high, S1);
                assert(temporal_stream_before(&copy[f & 1], &copy[!(f & 1)], f, low, high, S1) == (i >= 7));
                /* Not owned by any other tag: the link with the replaced slot, a link that shares the higher
                   slot, a pair at the lower slot, the slots the other way round. */
                const TemporalStreamCopy* tagged = &copy[f & 1];
                assert(!temporal_stream_owned(tagged, A, B, S1) && !temporal_stream_owned(tagged, B, C, S1));
                assert(!temporal_stream_owned(tagged, A, 0, S1) && !temporal_stream_owned(tagged, C, A, S1));
                assert(temporal_stream_owned(tagged, A, C, S1));
            }
            lend(f);
            if (i == 3) assert(lrec(f)->blocks[0].link == A && lrec(f)->blocks[0].turns == 1);
        }
    }
    /* The rules held against the long way of judging, over a scene made at random: objects that share meshes, slots
       near and far, missed frames, both slots at once, served and unusable blocks, frames not seen whole, frames
       left out, draws in any order. Every block's link and turns must be what the rules give, and a draw's link
       stands at the G-buffer's end exactly when the rules still give it with four turns. */
    {
        enum { OBJECTS = 14, FRAMES = 600 };
        struct {
            ull slot[2], draws[3];
            unsigned n, size, phase;
            float x, speed;
            int served, affine;
        } o[OBJECTS];
        static ull link[TEMPORAL_BLOCKS];
        static unsigned turns[TEMPORAL_BLOCKS];
        unsigned seed = 12345, f = 30, used = 0, held = 0, linked = 0;
#define RANDOM() ((seed = seed * 1103515245u + 12345u) >> 16 & 0x7FFF)
        lreset();
        for (int k = 0; k < OBJECTS; k++) {
            const unsigned mesh = RANDOM() % 5;
            o[k].slot[0] = 0x50c000000ull + 0x10000ull * (unsigned)k;
            o[k].slot[1] = o[k].slot[0] + (RANDOM() % 2 ? 0x200 : 0x200 * (2 + RANDOM() % 9));
            o[k].n = 1 + mesh % 3;
            for (unsigned d = 0; d < 3; d++) o[k].draws[d] = 0x7000 + 16 * mesh + d;
            o[k].size = mesh == 4 ? 0x100 : 0x200;
            o[k].phase = RANDOM() % 2;
            o[k].x = (float)(RANDOM() % 100);
            o[k].speed = RANDOM() % 3 ? 0.05f : 0.0f;
            o[k].served = RANDOM() % 7 == 0;
            o[k].affine = RANDOM() % 9 != 0;
        }
        for (unsigned step = 0; step < FRAMES; step++) {
            struct {
                ull address;
                const TemporalBlock* q;
            } uses[OBJECTS * 3];
            unsigned n_uses = 0;
            f += RANDOM() % 50 ? 1 : 2; /* now and then a frame without any draw */
            lbegin(f);
            const unsigned first = RANDOM() % OBJECTS;
            for (int n = 0; n < OBJECTS; n++) {
                const int k = (int)((first + (unsigned)n * 5) % OBJECTS);
                const unsigned chance = RANDOM() % 100;
                if (chance < 8) continue; /* not drawn this frame */
                if (chance < 12) o[k].phase ^= 1; /* a missed turn */
                o[k].x += o[k].speed;
                for (unsigned side = 0; side < (chance >= 96 ? 2u : 1u); side++) /* now and then both slots */
                    for (unsigned d = 0; d < o[k].n; d++) {
                        const ull at = o[k].slot[(f + o[k].phase + side) & 1];
                        const TemporalBlock* q =
                            ldraw(f, at, o[k].size, o[k].draws[d], o[k].x, o[k].affine, o[k].served, NULL);
                        if (!q) continue;
                        uses[n_uses].address = at;
                        uses[n_uses++].q = q;
                    }
            }
            if (RANDOM() % 40 == 0) lrec(f)->complete = 0;
            lend(f);
            naive_judge(lrec(f), lrec(f - 1), lrec(f - 2), link, turns);
            for (unsigned b = 0; b < lrec(f)->n; b++) {
                assert(lrec(f)->blocks[b].link == link[b] && lrec(f)->blocks[b].turns == turns[b]);
                linked += link[b] != 0;
            }
            for (unsigned u = 0; u < n_uses; u++) {
                unsigned b = 0;
                while (lrec(f)->blocks[b].address != uses[u].address) b++;
                const int stands = link[b] == uses[u].q->address && turns[b] >= TEMPORAL_PAIR_FRAMES;
                assert(temporal_link_holds(lrec(f), uses[u].address, uses[u].q->address) == stands);
                used++;
                held += stands;
            }
        }
#undef RANDOM
        /* the scene did exercise the rules: links were made, used, and some uses did not stand */
        assert(linked > 100 && used > 50 && held > 20 && held < used);
    }
    /* Which draws the record takes. Off, none at all, whatever the draw: nothing of the record runs. On, a draw
       into the scene is recorded only when the address, the surface and the camera all say so. Any other draw
       into the scene leaves the frame not seen whole: the case found in review is the surface still the old one
       while the scene depth's address has moved, with the camera known. */
    for (int bits = 0; bits < 16; bits++) {
        const int block = bits & 1, by_address = bits >> 1 & 1, by_surface = bits >> 2 & 1, camera = bits >> 3 & 1;
        assert(temporal_scene_gate(0, block, by_address, by_surface, camera) == TEMPORAL_SCENE_NOT);
        const int gate = temporal_scene_gate(1, block, by_address, by_surface, camera);
        if (!block || (!by_address && !by_surface)) assert(gate == TEMPORAL_SCENE_NOT);
        else assert(gate == (by_address && by_surface && camera ? TEMPORAL_SCENE_SEEN : TEMPORAL_SCENE_UNSEEN));
    }
    assert(temporal_scene_gate(1, 1, 0, 1, 1) == TEMPORAL_SCENE_UNSEEN);
    /* A bind without a record does nothing and says so. The renderer opens the record before every bind, and a
       frame whose first draw is of the kind above is opened as not complete: a used link then does not stand. */
    {
        const float world[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        assert(!temporal_frame_bind(NULL, A, 0x200, world, 0, S1));
        assert(!temporal_frame_find(NULL, A));
        lreset();
        for (unsigned i = 0; i < 6; i++) {
            lbegin(10 + i);
            lmove(10 + i, i & 1 ? A + 0x400 : A, S1);
            lend(10 + i);
        }
        lbegin(16);
        lrec(16)->complete = 0; /* the unseen draw came first */
        assert(!ldraw(16, A, 0x200, S1, 1.6f, 1, 0, &why) && why == TEMPORAL_LINK_UNKNOWN);
        assert(temporal_frame_bind(lrec(16), A + 0x9000, 0x200, world, 0, S2)); /* the later bind has its record */
        lend(16);
        for (unsigned b = 0; b < lrec(16)->n; b++) assert(!lrec(16)->blocks[b].link);
    }
    /* THE KNOWN LIMIT, not a pass: two moving things of one mesh that are seen in turns, each at its own single
       address. To every rule they are one block taking turns between two slots, so the link is made and the draw
       of one gets the matrix of the other. Equal draws and strict turns are evidence of one object, not proof. */
    lreset();
    for (unsigned i = 0; i < 8; i++) {
        const unsigned f = 10 + i;
        lbegin(f);
        const TemporalBlock* q =
            ldraw(f, i & 1 ? A + 0x5000 : A, 0x200, S1, (i & 1 ? 1.0f : 0.0f) + 0.1f * (float)f, 1, 0, NULL);
        assert((q != NULL) == (i >= 4));
        if (q) assert(q->world[12] == (i & 1 ? 0.0f : 1.0f) + 0.1f * (float)(f - 1)); /* the other thing's matrix */
        lend(f);
    }
}

/* The scene inside a rectangle of its targets. A target of 3840 by 2160 (and 1920 by 1080) with the game's scale. */
static TemporalRectState rect_state(int w, int h, float f, int tw, int th) {
    /* the viewport as the game has it: y is the bottom edge and the height is negative */
    return (TemporalRectState){{0, 0, w, h}, {0.0f, (float)h, (float)w, -(float)h}, tw, th, f, f};
}
/* a frame's geometry and then its fraction, as the two rules give them one after the other */
static int rect_structure(const TemporalViewport* v, int tw, int th, float fx, float fy, int corner,
                          TemporalRectState* out) {
    int result = temporal_rect_geometry(v, tw, th, corner, out);
    if (result == TEMPORAL_RECT_OK) result = temporal_rect_fraction(out, fx, fy);
    if (result == TEMPORAL_RECT_OK) out->fx = fx, out->fy = fy;
    return result;
}
/* a scene block the fragment program reads, bound, with the state's fraction */
static TemporalSceneBlock rect_block(const TemporalRectState* s) {
    return (TemporalSceneBlock){1, 1, s->fx, s->fy};
}
/* A frame decided and confirmed by its own fraction, in which our passes were not reached. What the decision
   said: 1 jittered, 0 gated. */
static int rect_unreached(TemporalRectGate* g, unsigned frame, const TemporalRectState* s) {
    const TemporalSceneBlock block = rect_block(s);
    const int jitter = temporal_rect_decide(g, frame, s, 0);
    temporal_rect_confirm(g, frame, NULL, &block);
    return jitter;
}
/* the same, and our passes were reached */
static int rect_go(TemporalRectGate* g, unsigned frame, const TemporalRectState* s) {
    const int jitter = rect_unreached(g, frame, s);
    temporal_rect_hooked(g, frame);
    return jitter;
}
static TemporalRectStamp rect_stamp(const TemporalRectState* s) {
    return (TemporalRectStamp){0x5508c0000ull, 1, s->target_w, s->target_h, s->target_w, s->target_h, s->rect, s->fx,
                               s->fy};
}
/* one frame through the gate as the renderer would drive it: decided, confirmed, perhaps disagreed with, resolved */
enum { RECT_FINE, RECT_EARLY, RECT_LATE };
static int rect_frame(TemporalRectGate* g, unsigned frame, const TemporalRectState* s, int fault, int* history) {
    const TemporalRectStamp stamp = rect_stamp(s);
    const TemporalSceneBlock block = rect_block(s);
    const int jitter = temporal_rect_decide(g, frame, fault == RECT_EARLY ? NULL : s, 0);
    temporal_rect_confirm(g, frame, NULL, &block);
    temporal_rect_hooked(g, frame);
    if (fault == RECT_LATE) {
        TemporalRectState other = *s;
        other.rect.w -= 2;
        other.viewport.width -= 2.0f;
        assert(temporal_rect_late(g, frame, &other, NULL, &block));
    } else /* the same again is no disagreement */
        assert(temporal_rect_late(g, frame, s, NULL, &block) == (fault == RECT_EARLY));
    *history = temporal_rect_history(g, frame, &stamp);
    const int steady = temporal_rect_steady(g, frame);
    assert(!steady || (jitter && fault == RECT_FINE));
    assert(!*history || steady);
    temporal_rect_wrote(g, frame, &stamp);
    return steady;
}

static void test_rect(void) {
    TemporalRect r;
    TemporalRectState s;
    /* The rectangle of a signed viewport. The game's is 0,2160 3840x-2160 for a whole target: y is the bottom
       edge and the height is negative. */
    const TemporalViewport whole = {0.0f, 2160.0f, 3840.0f, -2160.0f};
    assert(temporal_rect_from_viewport(&whole, 3840, 2160, &r) && r.x == 0 && r.y == 0 && r.w == 3840 && r.h == 2160);
    const TemporalViewport up = {0.0f, 1620.0f, 2880.0f, -1620.0f}, down = {0.0f, 0.0f, 2880.0f, 1620.0f};
    assert(temporal_rect_from_viewport(&up, 3840, 2160, &r) && r.x == 0 && r.y == 0 && r.w == 2880 && r.h == 1620);
    assert(temporal_rect_from_viewport(&down, 3840, 2160, &r) && r.y == 0 && r.h == 1620);
    const TemporalViewport low = {960.0f, 2160.0f, 2880.0f, -1620.0f};
    assert(temporal_rect_from_viewport(&low, 3840, 2160, &r) && r.x == 960 && r.y == 540 && r.w == 2880 && r.h == 1620);
    /* within 0.001 of whole numbers, else not a rectangle. Outside the target, empty, a width that is not
       positive, not finite: none. */
    const TemporalViewport near_whole = {0.0004f, 1620.0004f, 2879.9996f, -1620.0f};
    assert(temporal_rect_from_viewport(&near_whole, 3840, 2160, &r) && r.w == 2880 && r.h == 1620 && r.y == 0);
    const TemporalViewport bad[] = {
        {0.5f, 1620.0f, 2880.0f, -1620.0f},  {0.0f, 1620.0f, 2880.25f, -1620.0f}, {0.0f, 1620.0f, 2880.0f, -1620.5f},
        {-1.0f, 1620.0f, 2880.0f, -1620.0f}, {0.0f, 1619.0f, 2880.0f, -1620.0f},  {0.0f, 2161.0f, 3840.0f, -2160.0f},
        {1.0f, 2160.0f, 3840.0f, -2160.0f},  {0.0f, 2160.0f, 3841.0f, -2160.0f},  {0.0f, 0.0f, 3840.0f, 2161.0f},
        {0.0f, 0.0f, 0.0f, 2160.0f},         {0.0f, 0.0f, 3840.0f, 0.0f},         {3840.0f, 0.0f, -3840.0f, 2160.0f},
        {NAN, 0.0f, 3840.0f, 2160.0f},       {0.0f, 0.0f, INFINITY, 2160.0f},     {0.0f, 0.0f, 3840.0f, -INFINITY},
        {0.0f, 3e9f, 3840.0f, -3e9f}};
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++)
        assert(!temporal_rect_from_viewport(&bad[i], 3840, 2160, &r));

    /* The structure of a frame. A whole target, and the game's rectangle at three scales and two target sizes. */
    assert(rect_structure(&whole, 3840, 2160, 1.0f, 1.0f, TEMPORAL_CORNER_TOP_LEFT, &s) == TEMPORAL_RECT_OK);
    assert(s.rect.w == 3840 && s.rect.h == 2160 && s.target_w == 3840 && s.fx == 1.0f);
    /* A whole target is understood only with a fraction of 1 within 1e-6. Reported as partly used it is refused,
       also at 0.9998, whose size rounds to the whole target within a pixel. */
    assert(rect_structure(&whole, 3840, 2160, 0.9999995f, 0.9999995f, 0, &s) == TEMPORAL_RECT_OK);
    assert(rect_structure(&whole, 3840, 2160, 0.75f, 0.75f, 0, &s) == TEMPORAL_RECT_SHAPE);
    assert(rect_structure(&whole, 3840, 2160, 0.5f, 0.5f, 0, &s) == TEMPORAL_RECT_SHAPE);
    assert(rect_structure(&whole, 3840, 2160, 0.9998f, 0.9998f, 0, &s) == TEMPORAL_RECT_SHAPE);
    assert(rect_structure(&whole, 3840, 2160, 0.99999f, 0.99999f, 0, &s) == TEMPORAL_RECT_SHAPE);
    const float scales[3] = {0.5f, 0.75f, 0.9f};
    for (int big = 0; big < 2; big++)
        for (int k = 0; k < 3; k++) {
            const int tw = big ? 3840 : 1920, th = big ? 2160 : 1080;
            const float f = scales[k], w = roundf((float)tw * f), h = roundf((float)th * f);
            /* the rectangle in each corner of the target passes only when that corner is the game's */
            for (int corner = 0; corner < 4; corner++) {
                const int left = corner == TEMPORAL_CORNER_TOP_LEFT || corner == TEMPORAL_CORNER_BOTTOM_LEFT;
                const int top = corner == TEMPORAL_CORNER_TOP_LEFT || corner == TEMPORAL_CORNER_TOP_RIGHT;
                const TemporalViewport v = {left ? 0.0f : (float)tw - w, (top ? 0.0f : (float)th - h) + h, w, -h};
                for (int game = 0; game < 4; game++)
                    assert(rect_structure(&v, tw, th, f, f, game, &s) ==
                           (game == corner ? TEMPORAL_RECT_OK : TEMPORAL_RECT_SHAPE));
                assert(rect_structure(&v, tw, th, f, f, corner, &s) == TEMPORAL_RECT_OK);
                assert(s.rect.w == (int)w && s.rect.h == (int)h && s.rect.x == (left ? 0 : tw - (int)w));
                assert(s.rect.y == (top ? 0 : th - (int)h) && s.fx == f && s.fy == f);
            }
            /* one pixel off the fraction's size on both axes passes, two on either do not */
            const TemporalViewport one = {0.0f, h + 1.0f, w + 1.0f, -(h + 1.0f)}, two = {0.0f, h, w + 2.0f, -h};
            const TemporalViewport tall = {0.0f, h - 2.0f, w, -(h - 2.0f)};
            assert(rect_structure(&one, tw, th, f, f, TEMPORAL_CORNER_TOP_LEFT, &s) == TEMPORAL_RECT_OK);
            assert(rect_structure(&two, tw, th, f, f, TEMPORAL_CORNER_TOP_LEFT, &s) == TEMPORAL_RECT_SHAPE);
            assert(rect_structure(&tall, tw, th, f, f, TEMPORAL_CORNER_TOP_LEFT, &s) == TEMPORAL_RECT_SHAPE);
        }
    /* The fraction: finite and in (0, 1], both the same within 1e-6. The game's table has pairs that differ. */
    const float fractions[] = {0.0f, -0.5f, 1.0001f, 2.0f, NAN, INFINITY, -INFINITY};
    for (unsigned i = 0; i < sizeof fractions / sizeof *fractions; i++) {
        assert(rect_structure(&up, 3840, 2160, fractions[i], 0.75f, 0, &s) == TEMPORAL_RECT_FRACTION);
        assert(rect_structure(&up, 3840, 2160, 0.75f, fractions[i], 0, &s) == TEMPORAL_RECT_FRACTION);
    }
    assert(rect_structure(&up, 3840, 2160, 0.9f, 0.85f, 0, &s) == TEMPORAL_RECT_UNEQUAL);
    assert(rect_structure(&up, 3840, 2160, 0.75f, 0.75001f, 0, &s) == TEMPORAL_RECT_UNEQUAL);
    assert(rect_structure(&up, 3840, 2160, 0.75f, 0.75f, 0, &s) == TEMPORAL_RECT_OK);
    /* a fraction that does not fit the rectangle, a viewport that is none, a target without a size */
    assert(rect_structure(&up, 3840, 2160, 0.5f, 0.5f, 0, &s) == TEMPORAL_RECT_SHAPE);
    assert(rect_structure(&bad[0], 3840, 2160, 0.75f, 0.75f, 0, &s) == TEMPORAL_RECT_VIEWPORT);
    assert(rect_structure(&up, 0, 2160, 0.75f, 0.75f, 0, &s) == TEMPORAL_RECT_VIEWPORT);

    /* The gate. Frame N brings a rectangle and is gated, N+1 is accepted and has no history, N+2 has it. */
    const TemporalRectState a = rect_state(2880, 1620, 0.75f, 3840, 2160), b = rect_state(1920, 1080, 0.5f, 3840, 2160);
    const TemporalRectState full = rect_state(3840, 2160, 1.0f, 3840, 2160);
    TemporalRectGate g;
    int history;
    memset(&g, 0, sizeof g);
    assert(!rect_frame(&g, 10, &a, RECT_FINE, &history) && !history);
    assert(rect_frame(&g, 11, &a, RECT_FINE, &history) && !history);
    assert(rect_frame(&g, 12, &a, RECT_FINE, &history) && history);
    assert(rect_frame(&g, 13, &a, RECT_FINE, &history) && history);
    /* a change from A to B and back, then the whole target and back to a rectangle: each change is gated once and
       starts the history anew */
    const TemporalRectState* turns[4] = {&b, &a, &full, &a};
    unsigned f = 14;
    for (int t = 0; t < 4; t++) {
        assert(!rect_frame(&g, f++, turns[t], RECT_FINE, &history) && !history);
        assert(rect_frame(&g, f++, turns[t], RECT_FINE, &history) && !history);
        assert(rect_frame(&g, f++, turns[t], RECT_FINE, &history) && history);
    }
    /* the same rectangle with another fraction that fits it is a change found at the confirmation: the frame is
       jittered and not resolved. In another target size it is a change at the decision. */
    const TemporalRectState other_target = rect_state(2880, 1620, 0.75f, 3840, 2176);
    TemporalRectState other_fraction = a;
    other_fraction.fx = other_fraction.fy = 0.7501f;
    const TemporalSceneBlock other_block = rect_block(&other_fraction);
    assert(temporal_rect_decide(&g, f, &other_fraction, 0));
    temporal_rect_hooked(&g, f);
    assert(temporal_rect_confirm(&g, f, NULL, &other_block) == TEMPORAL_FRAME_SEEN && !temporal_rect_steady(&g, f));
    f++;
    assert(!temporal_rect_decide(&g, f++, &other_target, 0));
    /* Each of the three breaks (an unknown early binder, a late disagreement, a missing frame) at each of the
       three positions. The frame of the break is not steady, and the frames after start again: gated, accepted
       without history, then with it. */
    for (int fault = 0; fault < 3; fault++)
        for (int at = 0; at < 3; at++) {
            memset(&g, 0, sizeof g);
            f = 100;
            for (int i = 0; i < at; i++) assert(rect_frame(&g, f++, &a, RECT_FINE, &history) == (i > 0));
            if (fault == 0) assert(!rect_frame(&g, f++, &a, RECT_EARLY, &history) && !history);
            if (fault == 1) assert(!rect_frame(&g, f++, &a, RECT_LATE, &history) && !history);
            if (fault == 2) f++; /* a frame in which nothing was decided */
            /* a break before anything was learned leaves nothing to lose: the sequence just starts */
            assert(!rect_frame(&g, f++, &a, RECT_FINE, &history) && !history);
            assert(rect_frame(&g, f++, &a, RECT_FINE, &history) && !history);
            assert(rect_frame(&g, f++, &a, RECT_FINE, &history) && history);
        }
    /* The viewport's signs are part of what must stay the same. The same rectangle with y running the other way
       gives the same pixels and the opposite jitter in y, so it is another viewport: as a late draw it makes the
       frame not valid, and from one frame to the next it is a change, gated once and without history after. */
    {
        TemporalRectState up_way, down_way;
        const TemporalViewport flipped = {0.0f, 0.0f, 2880.0f, 1620.0f};
        assert(rect_structure(&up, 3840, 2160, 0.75f, 0.75f, 0, &up_way) == TEMPORAL_RECT_OK);
        assert(rect_structure(&flipped, 3840, 2160, 0.75f, 0.75f, 0, &down_way) == TEMPORAL_RECT_OK);
        assert(temporal_rect_same(&up_way.rect, &down_way.rect) && !temporal_rect_geometry_same(&up_way, &down_way));
        assert(up_way.viewport.height == -1620.0f && down_way.viewport.height == 1620.0f);
        memset(&g, 0, sizeof g);
        for (f = 500; f < 503; f++) assert(rect_frame(&g, f, &up_way, RECT_FINE, &history) == (f > 500));
        assert(history);
        /* late, in a steady frame with history */
        assert(rect_go(&g, 503, &up_way) && temporal_rect_steady(&g, 503));
        assert(temporal_rect_late(&g, 503, &down_way, NULL, NULL) && !temporal_rect_steady(&g, 503));
        assert(!g.have_candidate && !g.have_stamp);
        /* from one frame to the next */
        memset(&g, 0, sizeof g);
        for (f = 510; f < 513; f++) rect_frame(&g, f, &up_way, RECT_FINE, &history);
        assert(history);
        assert(!rect_frame(&g, 513, &down_way, RECT_FINE, &history) && !history);
        assert(rect_frame(&g, 514, &down_way, RECT_FINE, &history) && !history);
        assert(rect_frame(&g, 515, &down_way, RECT_FINE, &history) && history);
        /* a moved origin with the same size and signs is another viewport as well */
        TemporalRectState moved = up_way;
        moved.viewport.x = 1.0f;
        assert(!temporal_rect_geometry_same(&up_way, &moved));
    }
    /* a late disagreement leaves the frame not steady even after history was read for it, and no history is kept */
    memset(&g, 0, sizeof g);
    for (f = 200; f < 203; f++) rect_frame(&g, f, &a, RECT_FINE, &history);
    assert(history && rect_go(&g, 203, &a) && temporal_rect_steady(&g, 203));
    assert(temporal_rect_late(&g, 203, &b, NULL, NULL) && !temporal_rect_steady(&g, 203));
    assert(!g.have_stamp && !g.have_candidate);
    assert(temporal_rect_late(&g, 203, &a, NULL, NULL)); /* it stays not valid, whatever comes after */
    /* The history's identity: another target, another making of it, another size, output size, rectangle or
       fraction, and it is not used. The same, and it is. */
    memset(&g, 0, sizeof g);
    const TemporalRectStamp made = {0x5508c0000ull, 3, 3840, 2160, 3840, 2160, {0, 0, 2880, 1620}, 0.75f, 0.75f};
    for (f = 300; f < 302; f++) rect_go(&g, f, &a);
    temporal_rect_wrote(&g, 301, &made);
    assert(rect_go(&g, 302, &a) && temporal_rect_history(&g, 302, &made));
    TemporalRectStamp changed[8] = {made, made, made, made, made, made, made, made};
    changed[0].target++;
    changed[1].generation++;
    changed[2].target_w = 1920;
    changed[3].out_h = 1080;
    changed[4].rect.w--;
    changed[5].rect.y = 540;
    changed[6].fx = 0.7501f;
    changed[7].fy = 0.7501f;
    for (int i = 0; i < 8; i++) assert(!temporal_rect_history(&g, 302, &changed[i]));
    /* a gated frame writes no history */
    memset(&g, 0, sizeof g);
    rect_go(&g, 400, &a);
    temporal_rect_wrote(&g, 400, &made);
    assert(!g.have_stamp);

    /* A history fetch whose centre is outside the rectangle the history was made in is refused: the right and
       bottom edges are outside, the left and top inside. The taps are kept inside as well, which is not that. */
    const TemporalRect prior = {0, 0, 2880, 1620}, inset = {960, 540, 2880, 1620};
    assert(temporal_rect_holds(&prior, 0.0, 0.0) && temporal_rect_holds(&prior, 2879.99, 1619.99));
    assert(!temporal_rect_holds(&prior, 2880.0, 100.0) && !temporal_rect_holds(&prior, 100.0, 1620.0));
    assert(!temporal_rect_holds(&prior, -0.01, 100.0) && !temporal_rect_holds(&prior, 100.0, -0.01));
    assert(!temporal_rect_holds(&prior, 3000.5, 800.5) && !temporal_rect_holds(&prior, NAN, 1.0));
    assert(!temporal_rect_holds(&prior, 1.0, INFINITY));
    assert(temporal_rect_holds(&inset, 960.0, 540.0) && !temporal_rect_holds(&inset, 959.9, 540.0));
    assert(!temporal_rect_holds(&inset, 3840.0, 600.0));
    int tap[2];
    temporal_rect_tap(&prior, 2880, 1620, tap);
    assert(tap[0] == 2879 && tap[1] == 1619);
    temporal_rect_tap(&inset, 0, 5000, tap);
    assert(tap[0] == 960 && tap[1] == 2159);
    temporal_rect_tap(&inset, 1000, 600, tap);
    assert(tap[0] == 1000 && tap[1] == 600);

    /* the bound of an object's own motion goes by the rectangle's height: 270 px whole, 202.5 at 0.75, 135 at 0.5 */
    assert(temporal_rect_reject_bound(1, &full.rect) == 270.0 && temporal_rect_reject_bound(1, &a.rect) == 202.5);
    assert(temporal_rect_reject_bound(1, &b.rect) == 135.0 && temporal_rect_reject_bound(0, &a.rect) == 0.0);

    /* The numbers' lattice of 96 by 54 spans the rectangle: every sample inside it, the first and last cells'
       middles where they belong, no sample twice while the rectangle has room, also away from the origin. */
    const TemporalRect lattices[3] = {{0, 0, 3840, 2160}, {0, 0, 2880, 1620}, {960, 540, 1920, 1080}};
    for (int k = 0; k < 3; k++) {
        const TemporalRect* l = &lattices[k];
        int at[2], last[2] = {-1, -1};
        for (int j = 0; j < 54; j++)
            for (int i = 0; i < 96; i++) {
                temporal_rect_sample(l, i, j, 96, 54, at);
                assert(temporal_rect_holds(l, at[0] + 0.5, at[1] + 0.5));
                if (i) assert(at[0] > last[0]);
                if (!i && j) assert(at[1] > last[1]);
                last[0] = at[0];
                last[1] = at[1];
            }
        temporal_rect_sample(l, 0, 0, 96, 54, at);
        assert(at[0] == l->x + l->w / 192 && at[1] == l->y + l->h / 108);
        temporal_rect_sample(l, 95, 53, 96, 54, at);
        assert(at[0] == l->x + (191 * l->w) / 192 && at[1] == l->y + (107 * l->h) / 108);
    }

    /* the jitter's offsets: 8 whole, 15 at 0.75 (8 / 0.5625 = 14.2), 32 at 0.5, 10 at 0.9 (9.9) */
    assert(temporal_rect_offsets(&full.rect, 3840, 2160) == 8 && temporal_rect_offsets(&a.rect, 3840, 2160) == 15);
    assert(temporal_rect_offsets(&b.rect, 3840, 2160) == 32);
    const TemporalRect nine = {0, 0, 3456, 1944};
    assert(temporal_rect_offsets(&nine, 3840, 2160) == 10);
    /* the texture level shift: log2 of the scale, 0 whole, and a set value wins */
    assert(temporal_rect_bias(0, 0.0f, &full.rect, 3840) == 0.0f);
    assert(temporal_rect_bias(0, 0.0f, &b.rect, 3840) == -1.0f);
    assert(fabsf(temporal_rect_bias(0, 0.0f, &a.rect, 3840) - -0.41504f) < 1e-4f);
    assert(temporal_rect_bias(1, -0.25f, &b.rect, 3840) == -0.25f);
    assert(temporal_rect_bias(1, 0.0f, &b.rect, 3840) == 0.0f);
    /* the jitter in pixels through the signed viewport: a quarter pixel right and up in clip is right and DOWN
       the image by the same when the height is negative, and the rectangle's size scales it */
    const double clip[2] = {0.5 / 2880.0, 0.5 / 1620.0};
    double px[2];
    temporal_jitter_pixels(clip, &up, px);
    assert(fabs(px[0] - 0.25) < 1e-12 && fabs(px[1] - -0.25) < 1e-12);
    temporal_jitter_pixels(clip, &down, px);
    assert(fabs(px[0] - 0.25) < 1e-12 && fabs(px[1] - 0.25) < 1e-12);
}

/* What an upscaler is given. The places are worked out by hand from the test's camera: a point at (px, py) and
   distance d under a camera at (cx, cy) with scales 1 is at ndc ((px - cx) / d, (py - cy) / d). */
static void image_place(const double ndc[2], const TemporalViewport* v, double out[2]) {
    out[0] = (ndc[0] * 0.5 + 0.5) * (double)v->width + (double)v->x;
    out[1] = (ndc[1] * 0.5 + 0.5) * (double)v->height + (double)v->y;
}
/* the motion image's texel as shaders/temporal_motion.frag writes it: (now less before) in pixels over the size */
static void motion_texel(const double now[2], const double before[2], const TemporalViewport* v, int w, int h,
                         float texel[2]) {
    double is[2], was[2];
    image_place(now, v, is);
    image_place(before, v, was);
    texel[0] = (float)((is[0] - was[0]) / (double)w);
    texel[1] = (float)((is[1] - was[1]) / (double)h);
}

/* The rectangle's rules in two steps: the geometry at the frame's first draw, the fraction at the first scene draw
   whose program reads the scene block. */
static void test_rect_confirm(void) {
    TemporalRectState s, a, whole_state;
    TemporalRectGate g;
    const TemporalViewport up = {0.0f, 1620.0f, 2880.0f, -1620.0f}, whole = {0.0f, 2160.0f, 3840.0f, -2160.0f};
    /* The geometry alone. The proportions within a pixel: |w x H - h x W| <= max(W, H), the bound itself passing.
       2880 by 1621 is the bound in a target of 3840 by 2160, 2880 by 1622 is past it. A square target takes one
       pixel either way. Under half the target on an axis is no scene. */
    assert(temporal_rect_geometry(&up, 3840, 2160, TEMPORAL_CORNER_TOP_LEFT, &a) == TEMPORAL_RECT_OK);
    assert(a.rect.w == 2880 && a.rect.h == 1620 && a.fx == 0.0f && a.fy == 0.0f && a.viewport.height == -1620.0f);
    assert(temporal_rect_geometry(&whole, 3840, 2160, TEMPORAL_CORNER_TOP_LEFT, &whole_state) == TEMPORAL_RECT_OK);
    a.fx = a.fy = 0.75f; /* for the helpers that confirm a frame with its state's fraction */
    const TemporalViewport bound = {0.0f, 1621.0f, 2880.0f, -1621.0f}, past = {0.0f, 1622.0f, 2880.0f, -1622.0f};
    const TemporalViewport under = {0.0f, 1619.0f, 2880.0f, -1619.0f}, far_under = {0.0f, 1618.0f, 2880.0f, -1618.0f};
    assert(temporal_rect_geometry(&bound, 3840, 2160, 0, &s) == TEMPORAL_RECT_OK);
    assert(temporal_rect_geometry(&past, 3840, 2160, 0, &s) == TEMPORAL_RECT_SHAPE);
    assert(temporal_rect_geometry(&under, 3840, 2160, 0, &s) == TEMPORAL_RECT_OK);
    assert(temporal_rect_geometry(&far_under, 3840, 2160, 0, &s) == TEMPORAL_RECT_SHAPE);
    const TemporalViewport square[3] = {{0.0f, 769.0f, 768.0f, -769.0f}, {0.0f, 767.0f, 768.0f, -767.0f},
                                        {0.0f, 770.0f, 768.0f, -770.0f}};
    assert(temporal_rect_geometry(&square[0], 1024, 1024, 0, &s) == TEMPORAL_RECT_OK);
    assert(temporal_rect_geometry(&square[1], 1024, 1024, 0, &s) == TEMPORAL_RECT_OK);
    assert(temporal_rect_geometry(&square[2], 1024, 1024, 0, &s) == TEMPORAL_RECT_SHAPE);
    const TemporalViewport half = {0.0f, 1080.0f, 1920.0f, -1080.0f}, less = {0.0f, 1079.0f, 1918.0f, -1079.0f};
    const TemporalViewport narrow = {0.0f, 2160.0f, 1900.0f, -2160.0f}, off = {8.0f, 1620.0f, 2880.0f, -1620.0f};
    assert(temporal_rect_geometry(&half, 3840, 2160, 0, &s) == TEMPORAL_RECT_OK);
    assert(temporal_rect_geometry(&less, 3840, 2160, 0, &s) == TEMPORAL_RECT_SHAPE);
    assert(temporal_rect_geometry(&narrow, 3840, 2160, 0, &s) == TEMPORAL_RECT_SHAPE);
    assert(temporal_rect_geometry(&off, 3840, 2160, 0, &s) == TEMPORAL_RECT_SHAPE);
    assert(temporal_rect_geometry(&up, 3840, 0, 0, &s) == TEMPORAL_RECT_VIEWPORT);
    /* A viewport that is not on whole pixels and has no other fault: the run at 0.6667 had 0,1440.07
       2560.13x-1440.07 in 3840 by 2160. Whole, not finite, empty, outside the target and a target without a
       size are other things. */
    {
        const TemporalViewport run = {0.0f, 1440.07f, 2560.13f, -1440.07f}, flat = {0.0f, 0.0f, 2560.13f, 1440.07f};
        const TemporalViewport part = {0.0f, 1620.0f, 2880.0f, -1620.0f}, wide = {0.0f, 100.5f, 3840.5f, -100.5f};
        const TemporalViewport nan_one = {NAN, 0.0f, 100.5f, 100.5f}, endless = {0.0f, 0.0f, INFINITY, 100.5f};
        const TemporalViewport none = {0.0f, 0.0f, 0.0f, 100.5f}, back = {100.5f, 0.0f, -50.5f, 100.5f};
        const TemporalViewport left = {-0.5f, 0.0f, 100.0f, 100.0f}, low = {0.0f, 2100.5f, 100.0f, 100.0f};
        const TemporalViewport moved = {0.5f, 0.0f, 100.0f, 100.0f}, level = {0.0f, 0.0f, 100.0f, 0.0f};
        assert(temporal_viewport_fractional(&run, 3840, 2160) && temporal_viewport_fractional(&flat, 3840, 2160));
        assert(temporal_viewport_fractional(&moved, 3840, 2160));
        assert(!temporal_viewport_fractional(&part, 3840, 2160) && !temporal_viewport_fractional(&whole, 3840, 2160));
        assert(!temporal_viewport_fractional(&wide, 3840, 2160) && !temporal_viewport_fractional(&nan_one, 3840, 2160));
        assert(!temporal_viewport_fractional(&endless, 3840, 2160) && !temporal_viewport_fractional(&none, 3840, 2160));
        assert(!temporal_viewport_fractional(&back, 3840, 2160) && !temporal_viewport_fractional(&left, 3840, 2160));
        assert(!temporal_viewport_fractional(&low, 3840, 2160) && !temporal_viewport_fractional(&level, 3840, 2160));
        assert(!temporal_viewport_fractional(&run, 0, 2160) && !temporal_viewport_fractional(&run, 3840, -1));
        /* every such viewport is one the rectangle's rule refuses */
        assert(!temporal_rect_from_viewport(&run, 3840, 2160, &s.rect));
        /* The scales that give whole pixels, for the very target: worked out, none by heart. 2/3 does at
           3840 by 2160 and not at 2560 by 1440, and at 1366 by 768 a half is the only one. */
        char text[96];
        assert(temporal_whole_scales(3840, 2160, text, sizeof text) == 8);
        assert(!strcmp(text, "1/2, 3/5, 5/8, 2/3, 3/4, 4/5, 5/6, 7/8"));
        assert(temporal_whole_scales(2560, 1440, text, sizeof text) == 6);
        assert(!strcmp(text, "1/2, 3/5, 5/8, 3/4, 4/5, 7/8"));
        assert(temporal_whole_scales(1366, 768, text, sizeof text) == 1 && !strcmp(text, "1/2"));
        assert(temporal_whole_scales(1920, 1080, text, sizeof text) == 8);
        assert(temporal_whole_scales(1365, 767, text, sizeof text) == 0 && !text[0]);
        assert(temporal_whole_scales(7, 7, text, sizeof text) == 3 && !strcmp(text, "4/7, 5/7, 6/7"));
        /* each one listed is what it says: the float the game is given, times the size, is whole */
        for (int q = 2; q <= 8; q++)
            for (int p = 1; p < q; p++) {
                char one[8];
                const float scale = (float)((double)p / (double)q);
                snprintf(one, sizeof one, "%d/%d", p, q);
                temporal_whole_scales(3840, 2160, text, sizeof text);
                if (strstr(text, one)) {
                    assert(scale >= 0.5f && fabsf(3840.0f * scale - roundf(3840.0f * scale)) <= 0.001f);
                    assert(fabsf(2160.0f * scale - roundf(2160.0f * scale)) <= 0.001f);
                }
            }
        /* text that is too short holds what fits, whole entries only, and the count is the true one */
        char small[8];
        assert(temporal_whole_scales(3840, 2160, small, sizeof small) == 8 && !strcmp(small, "1/2"));
        assert(temporal_whole_scales(3840, 2160, small, 1) == 8 && !small[0]);
    }
    /* The fraction of a geometry: not finite, out of (0, 1], unequal, the wrong size, 1 with a rectangle, below 1
       with the whole target. 0.75 and 0.7501 both fit 2880 by 1620. */
    assert(temporal_rect_fraction(&a, 0.75f, 0.75f) == TEMPORAL_RECT_OK);
    assert(temporal_rect_fraction(&a, 0.7501f, 0.7501f) == TEMPORAL_RECT_OK);
    assert(temporal_rect_fraction(&a, NAN, 0.75f) == TEMPORAL_RECT_FRACTION);
    assert(temporal_rect_fraction(&a, 0.75f, INFINITY) == TEMPORAL_RECT_FRACTION);
    assert(temporal_rect_fraction(&a, 0.0f, 0.0f) == TEMPORAL_RECT_FRACTION);
    assert(temporal_rect_fraction(&a, 1.5f, 1.5f) == TEMPORAL_RECT_FRACTION);
    assert(temporal_rect_fraction(&a, 0.75f, 0.7501f) == TEMPORAL_RECT_UNEQUAL);
    assert(temporal_rect_fraction(&a, 0.7f, 0.7f) == TEMPORAL_RECT_SHAPE);
    assert(temporal_rect_fraction(&a, 1.0f, 1.0f) == TEMPORAL_RECT_SHAPE);
    assert(temporal_rect_fraction(&whole_state, 1.0f, 1.0f) == TEMPORAL_RECT_OK);
    assert(temporal_rect_fraction(&whole_state, 0.9998f, 0.9998f) == TEMPORAL_RECT_SHAPE);
    assert(temporal_rect_fraction(&whole_state, 0.75f, 0.75f) == TEMPORAL_RECT_SHAPE);

    /* The scene block at the deciding draw can only refuse. Bound with a finite fraction that is not the
       geometry's: refused. Bound and agreeing, not bound, not finite, none: nothing is decided by it. */
    const TemporalSceneBlock good = {1, 1, 0.75f, 0.75f}, other = {1, 1, 0.7501f, 0.7501f};
    const TemporalSceneBlock wrong = {1, 1, 0.5f, 0.5f}, unbound = {1, 0, 0.5f, 0.5f}, lost = {1, 1, NAN, 0.75f};
    const TemporalSceneBlock unread = {0, 1, 0.5f, 0.5f}, one = {1, 1, 1.0f, 1.0f};
    assert(temporal_rect_early(&a, &wrong) && temporal_rect_early(&a, &unread) && temporal_rect_early(&a, &one));
    assert(!temporal_rect_early(&a, &good) && !temporal_rect_early(&a, &other));
    assert(!temporal_rect_early(&a, &unbound) && !temporal_rect_early(&a, &lost) && !temporal_rect_early(&a, NULL));
    assert(temporal_rect_early(&whole_state, &good) && !temporal_rect_early(&whole_state, &one));

    /* A stage's scene block from what the renderer knows. It reads the block when its program reads bank 3 as far
       as the fraction's words. It is bound when a block that long is there and its words could be read. */
    {
        const float words[2] = {0.75f, 0.75f};
        TemporalSceneBlock s0 = temporal_scene_block(0x3d8, 0x630, 0x512be6e00ull, 0xa00, words);
        assert(s0.reads && s0.bound && s0.fx == 0.75f && s0.fy == 0.75f);
        s0 = temporal_scene_block(0x3d8, 0x620, 0x512be6e00ull, 0xa00, words); /* reads less than the fraction */
        assert(!s0.reads && s0.bound);
        s0 = temporal_scene_block(0x350, 0x10000, 0x512be6e00ull, 0xa00, words); /* the pre-pass: bank 3 not read */
        assert(!s0.reads && s0.bound);
        s0 = temporal_scene_block(0x3d8, 0x10000, 0x512be6e00ull, 0x620, words); /* a block too short */
        assert(s0.reads && !s0.bound && s0.fx == 0.0f);
        s0 = temporal_scene_block(0x3d8, 0x630, 0, 0xa00, words); /* nothing bound */
        assert(s0.reads && !s0.bound);
        s0 = temporal_scene_block(0x3d8, 0x630, 0x512be6e00ull, 0x630, NULL); /* words not readable */
        assert(s0.reads && !s0.bound);
        s0 = temporal_scene_block(0x3d8, 0, 0x512be6e00ull, 0xa00, words); /* no read with a known place */
        assert(!s0.reads && s0.bound);
    }
    /* The sequence. Frame N has a new geometry: gated, and confirmed, which makes it the candidate. N+1 is
       accepted and confirmed with the same fraction: steady, it seeds the history. N+2 uses it. */
    TemporalRectStamp stamp = {0x5508c0000ull, 1, 3840, 2160, 3840, 2160, {0, 0, 2880, 1620}, 0.75f, 0.75f};
    memset(&g, 0, sizeof g);
    assert(!temporal_rect_decide(&g, 10, &a, 0) && !temporal_rect_steady(&g, 10));
    assert(temporal_rect_confirm(&g, 10, NULL, &good) == TEMPORAL_FRAME_SEEN && !temporal_rect_steady(&g, 10));
    assert(g.have_candidate && g.candidate.fx == 0.75f && !g.have_stamp);
    temporal_rect_wrote(&g, 10, &stamp); /* a gated frame writes nothing, confirmed or not */
    assert(!g.have_stamp);
    assert(temporal_rect_decide(&g, 11, &a, 0));
    temporal_rect_hooked(&g, 11);
    /* decided and not yet confirmed: not to be resolved, no history, nothing written */
    assert(!temporal_rect_steady(&g, 11) && !temporal_rect_history(&g, 11, &stamp));
    temporal_rect_wrote(&g, 11, &stamp);
    assert(!g.have_stamp);
    /* a draw before the confirmation is held to the geometry only */
    assert(!temporal_rect_late(&g, 11, &a, &wrong, &wrong));
    assert(temporal_rect_confirm(&g, 11, NULL, &good) == TEMPORAL_FRAME_STEADY && temporal_rect_steady(&g, 11));
    assert(temporal_rect_confirm(&g, 11, NULL, &wrong) == TEMPORAL_FRAME_STEADY); /* once a frame */
    assert(temporal_rect_confirm(&g, 12, NULL, &good) == 0);                      /* not this frame's */
    assert(!temporal_rect_history(&g, 11, &stamp));
    temporal_rect_wrote(&g, 11, &stamp);
    assert(temporal_rect_decide(&g, 12, &a, 0) && !temporal_rect_history(&g, 12, &stamp));
    temporal_rect_hooked(&g, 12);
    assert(temporal_rect_confirm(&g, 12, &good, &good) == TEMPORAL_FRAME_STEADY); /* both stages read it and agree */
    assert(temporal_rect_history(&g, 12, &stamp));
    temporal_rect_wrote(&g, 12, &stamp);
    /* Later draws of the frame are held to the confirmed fraction in each stage that reads the block. A stage
       that does not read it may have anything bound. */
    assert(!temporal_rect_late(&g, 12, &a, &good, &good) && !temporal_rect_late(&g, 12, &a, &unread, NULL));
    assert(!temporal_rect_late(&g, 12, &a, NULL, NULL) && temporal_rect_steady(&g, 12));
    for (int stage = 0; stage < 2; stage++)
        for (int kind = 0; kind < 3; kind++) {
            const TemporalSceneBlock gone = {1, 0, 0.75f, 0.75f}; /* not bound, whatever the words say */
            const TemporalSceneBlock* bad[3] = {&other, &gone, &wrong};
            TemporalRectGate h = g;
            assert(temporal_rect_late(&h, 12, &a, stage ? &good : bad[kind], stage ? bad[kind] : &good));
            assert(!temporal_rect_steady(&h, 12) && !h.have_candidate && !h.have_stamp);
            assert(!temporal_rect_history(&h, 12, &stamp));
            assert(temporal_rect_late(&h, 12, &a, &good, &good)); /* and it stays so */
            assert(!temporal_rect_decide(&h, 13, &a, 0));
        }

    /* A frame decided and never confirmed (no reading draw came): never steady, and the next decision finds
       nothing left of it or of the frames before. */
    assert(temporal_rect_decide(&g, 13, &a, 0) && !temporal_rect_steady(&g, 13) && g.have_stamp);
    assert(!temporal_rect_decide(&g, 14, &a, 0) && !g.have_candidate && !g.have_stamp);
    assert(temporal_rect_confirm(&g, 14, NULL, &good) == TEMPORAL_FRAME_SEEN);
    assert(rect_go(&g, 15, &a) && temporal_rect_steady(&g, 15) && !temporal_rect_history(&g, 15, &stamp));

    /* The confirming draw fails, at each position of the sequence. That draw alone confirms: a later good one does
       not mend the frame. The frame is not steady, nothing is left, and the frames after start again. */
    const TemporalSceneBlock skew = {1, 1, 0.75f, 0.7501f}, small = {1, 1, 0.7f, 0.7f}, none = {0, 0, 0.0f, 0.0f};
    const TemporalSceneBlock* fails[8][2] = {{NULL, &lost},  {NULL, &skew},   {NULL, &small}, {NULL, &one},
                                             {NULL, &unbound}, {&good, &other}, {&unbound, &good}, {&none, &none}};
    for (int kind = 0; kind < 8; kind++)
        for (int at = 0; at < 3; at++) {
            unsigned f = 100;
            int history;
            memset(&g, 0, sizeof g);
            for (int i = 0; i < at; i++) assert(rect_frame(&g, f++, &a, RECT_FINE, &history) == (i > 0));
            assert(temporal_rect_decide(&g, f, &a, 0) == (at > 0));
            assert(temporal_rect_confirm(&g, f, fails[kind][0], fails[kind][1]) == TEMPORAL_FRAME_NONE);
            assert(temporal_rect_confirm(&g, f, NULL, &good) == TEMPORAL_FRAME_NONE);
            assert(!temporal_rect_steady(&g, f) && !temporal_rect_history(&g, f, &stamp));
            assert(!g.have_candidate && !g.have_stamp);
            temporal_rect_wrote(&g, f, &stamp);
            assert(!g.have_stamp && temporal_rect_late(&g, f, &a, NULL, &good));
            f++;
            assert(!rect_frame(&g, f++, &a, RECT_FINE, &history) && !history);
            assert(rect_frame(&g, f++, &a, RECT_FINE, &history) && !history);
            assert(rect_frame(&g, f++, &a, RECT_FINE, &history) && history);
        }
    /* the whole target reported as partly used fails the same way */
    memset(&g, 0, sizeof g);
    assert(!temporal_rect_decide(&g, 150, &whole_state, 0));
    assert(temporal_rect_confirm(&g, 150, NULL, &good) == TEMPORAL_FRAME_NONE && !g.have_candidate);
    assert(!temporal_rect_decide(&g, 151, &whole_state, 0));
    assert(temporal_rect_confirm(&g, 151, NULL, &one) == TEMPORAL_FRAME_SEEN && g.candidate.fx == 1.0f);

    /* The fraction changes and the viewport does not: 0.75, then 0.7501, then back. The frame of the change was
       jittered and is not resolved, uses and writes no history, and is the new candidate. The frame after seeds,
       the one after that has history. A history made under the other fraction is not used. */
    TemporalRectState b = a;
    int history;
    b.fx = b.fy = 0.7501f;
    memset(&g, 0, sizeof g);
    for (unsigned f = 200; f < 203; f++) rect_frame(&g, f, &a, RECT_FINE, &history);
    assert(history && g.have_stamp);
    const TemporalRectState* turn[2] = {&b, &a};
    for (unsigned t = 0, f = 203; t < 2; t++, f += 3) {
        const TemporalRectStamp now = rect_stamp(turn[t]), before = rect_stamp(turn[!t]);
        const TemporalSceneBlock block = rect_block(turn[t]);
        assert(temporal_rect_decide(&g, f, turn[t], 0) && g.have_stamp);
        temporal_rect_hooked(&g, f);
        assert(temporal_rect_confirm(&g, f, NULL, &block) == TEMPORAL_FRAME_SEEN);
        assert(!temporal_rect_steady(&g, f) && !g.have_stamp && g.have_candidate && g.candidate.fx == turn[t]->fx);
        assert(!temporal_rect_history(&g, f, &now) && !temporal_rect_history(&g, f, &before));
        temporal_rect_wrote(&g, f, &now);
        assert(!g.have_stamp);
        assert(rect_frame(&g, f + 1, turn[t], RECT_FINE, &history) && !history);
        assert(temporal_rect_decide(&g, f + 2, turn[t], 0));
        temporal_rect_hooked(&g, f + 2);
        assert(temporal_rect_confirm(&g, f + 2, NULL, &block) == TEMPORAL_FRAME_STEADY);
        assert(temporal_rect_history(&g, f + 2, &now) && !temporal_rect_history(&g, f + 2, &before));
        temporal_rect_wrote(&g, f + 2, &now);
    }
    /* the change of fraction together with a late disagreement of the viewport: nothing is left */
    assert(temporal_rect_decide(&g, 209, &b, 0));
    assert(temporal_rect_confirm(&g, 209, NULL, &other) == TEMPORAL_FRAME_SEEN && g.have_candidate);
    assert(temporal_rect_late(&g, 209, &whole_state, NULL, &other) && !g.have_candidate && !g.have_stamp);
    assert(!temporal_rect_decide(&g, 210, &b, 0));

    /* Scene draws before the frame's decision. The first that reads the scene block is the confirming draw,
       whenever it comes. Such a frame is never accepted, since those draws had no jitter: at most it makes the
       candidate. */
    {
        TemporalRectGate e;
        /* a bad binding early, the decision, then a good reader: wrong for good */
        memset(&e, 0, sizeof e);
        for (unsigned f = 400; f < 403; f++) rect_frame(&e, f, &a, RECT_FINE, &history);
        assert(history);
        temporal_rect_before(&e, 403, &a, NULL, &unbound);
        assert(!temporal_rect_decide(&e, 403, &a, 0) && !e.have_candidate && !e.have_stamp);
        assert(temporal_rect_confirm(&e, 403, NULL, &good) == TEMPORAL_FRAME_NONE && !temporal_rect_steady(&e, 403));
        assert(temporal_rect_late(&e, 403, &a, NULL, &good) && !temporal_rect_decide(&e, 404, &a, 0));
        /* a good reader early, the decision, then a reader with another fraction: wrong */
        memset(&e, 0, sizeof e);
        for (unsigned f = 410; f < 413; f++) rect_frame(&e, f, &a, RECT_FINE, &history);
        temporal_rect_before(&e, 413, &a, NULL, &none); /* a scene draw that reads nothing: geometry only */
        temporal_rect_before(&e, 413, &a, &good, &good);
        assert(!temporal_rect_decide(&e, 413, &a, 0)); /* the candidate's geometry, and still not accepted */
        assert(e.state == TEMPORAL_FRAME_SEEN && e.have_candidate && e.candidate.fx == 0.75f && !e.have_stamp);
        assert(!temporal_rect_steady(&e, 413) && !temporal_rect_late(&e, 413, &a, NULL, &good));
        assert(temporal_rect_late(&e, 413, &a, NULL, &other) && !e.have_candidate);
        /* the same without the later reader: the frame after is accepted and seeds */
        memset(&e, 0, sizeof e);
        temporal_rect_before(&e, 420, &a, NULL, &good);
        assert(!temporal_rect_decide(&e, 420, &a, 0) && e.state == TEMPORAL_FRAME_SEEN);
        assert(rect_frame(&e, 421, &a, RECT_FINE, &history) && !history);
        assert(rect_frame(&e, 422, &a, RECT_FINE, &history) && history);
        /* early draws that read nothing leave the confirmation to a draw after the decision */
        memset(&e, 0, sizeof e);
        temporal_rect_before(&e, 430, &a, &unread, &none);
        assert(!temporal_rect_decide(&e, 430, &a, 0) && e.state == TEMPORAL_FRAME_DECIDED);
        assert(temporal_rect_confirm(&e, 430, NULL, &good) == TEMPORAL_FRAME_SEEN);
        /* early: two readers that differ, a fraction that does not fit, another viewport, no geometry, and a
           decision on another geometry than the early draws had */
        const TemporalSceneBlock* bad_early[2][2] = {{&good, &other}, {NULL, &small}};
        for (int i = 0; i < 2; i++) {
            memset(&e, 0, sizeof e);
            temporal_rect_before(&e, 440, &a, bad_early[i][0], bad_early[i][1]);
            assert(!temporal_rect_decide(&e, 440, &a, 0) && e.state == TEMPORAL_FRAME_NONE);
        }
        memset(&e, 0, sizeof e);
        temporal_rect_before(&e, 450, &a, NULL, &good);
        temporal_rect_before(&e, 450, &whole_state, NULL, &none);
        assert(!temporal_rect_decide(&e, 450, &a, 0) && e.state == TEMPORAL_FRAME_NONE);
        memset(&e, 0, sizeof e);
        temporal_rect_before(&e, 451, NULL, NULL, &good);
        temporal_rect_before(&e, 451, &a, NULL, &good); /* nothing mends it */
        assert(!temporal_rect_decide(&e, 451, &a, 0) && e.state == TEMPORAL_FRAME_NONE);
        memset(&e, 0, sizeof e);
        temporal_rect_before(&e, 452, &a, NULL, &good);
        assert(!temporal_rect_decide(&e, 452, &whole_state, 0) && e.state == TEMPORAL_FRAME_NONE);
        /* early draws of the frame before say nothing of this one, and a draw after the decision is not early */
        memset(&e, 0, sizeof e);
        for (unsigned f = 460; f < 462; f++) rect_frame(&e, f, &a, RECT_FINE, &history);
        temporal_rect_before(&e, 461, &a, NULL, &unbound);
        assert(e.early_at != 461 && rect_frame(&e, 462, &a, RECT_FINE, &history) && history);
        /* The start as the renderer has it: the pre-pass's first draw decides and is not an earlier draw of its
           own frame, the G-buffer's first draw confirms. Accepted from the second such frame. */
        memset(&e, 0, sizeof e);
        assert(!temporal_rect_decide(&e, 470, &a, 0));
        assert(temporal_rect_confirm(&e, 470, &good, &good) == TEMPORAL_FRAME_SEEN);
        assert(temporal_rect_decide(&e, 471, &a, 0));
        assert(temporal_rect_confirm(&e, 471, &good, &good) == TEMPORAL_FRAME_STEADY && temporal_rect_steady(&e, 471));
    }
    /* Our passes. A frame drawn with jitter in which they were never reached leaves nothing: the start begins
       again. Eight such frames in a row and the gate stops for good. One that is reached starts the count anew.
       A failure of the passes stops it at once. Stopped, the gate still confirms, and accepts nothing. */
    {
        TemporalRectGate u;
        unsigned f = 500;
        memset(&u, 0, sizeof u);
        for (int round = 0; round < TEMPORAL_RECT_UNHOOKED; round++) {
            assert(!rect_unreached(&u, f++, &a) && u.unhooked == (unsigned)round); /* gated: the candidate */
            assert(rect_unreached(&u, f, &a) && temporal_rect_steady(&u, f));      /* accepted, never reached */
            f++;
            assert(!u.stopped);
        }
        assert(!rect_unreached(&u, f++, &a) && u.stopped && u.unhooked == TEMPORAL_RECT_UNHOOKED);
        for (int i = 0; i < 4; i++) {
            assert(!rect_unreached(&u, f, &a) && u.state == TEMPORAL_FRAME_SEEN && !temporal_rect_steady(&u, f));
            f++;
        }
        /* seven in a row, then one that is reached: the count starts again and seven more do not stop it */
        memset(&u, 0, sizeof u);
        f = 550;
        for (int part = 0; part < 2; part++) {
            unsigned frames = 0;
            while (u.unhooked < TEMPORAL_RECT_UNHOOKED - 1 && frames++ < 40) rect_unreached(&u, f++, &a);
            assert(u.unhooked == TEMPORAL_RECT_UNHOOKED - 1 && !u.stopped);
            assert(rect_go(&u, f++, &a));
            assert(rect_go(&u, f++, &a) && u.unhooked == 0);
        }
        assert(!u.stopped);
        /* Our passes reached and refusing the frame for its structure: one odd frame costs the start-up and
           stops nothing, eight in a row stop the gate, and a good frame between starts the count again. */
        memset(&u, 0, sizeof u);
        f = 900;
        for (int i = 0; i < 3; i++) rect_go(&u, f++, &a);
        assert(rect_go(&u, f, &a) && temporal_rect_steady(&u, f));
        temporal_rect_refused(&u, f);
        assert(!temporal_rect_steady(&u, f) && !u.have_candidate && !u.have_stamp && !u.stopped);
        f++;
        assert(!rect_go(&u, f++, &a) && u.unhooked == 1 && !u.stopped); /* gated: the start again */
        assert(rect_go(&u, f++, &a));                                   /* accepted, reached, resolved */
        assert(rect_go(&u, f++, &a) && u.unhooked == 0);
        for (int round = 0; round < TEMPORAL_RECT_UNHOOKED; round++) {
            assert(!u.stopped && rect_go(&u, f, &a)); /* accepted, then refused at the passes */
            temporal_rect_refused(&u, f++);
            assert(!rect_go(&u, f++, &a) && u.unhooked == (unsigned)round + 1); /* gated, the candidate again */
        }
        assert(u.stopped && !rect_go(&u, f, &a));
        temporal_rect_refused(&u, f + 5); /* not the frame decided: nothing */
        assert(u.state == TEMPORAL_FRAME_SEEN);
        /* a gated frame refused there is not counted */
        memset(&u, 0, sizeof u);
        for (f = 950; f < 970; f++) {
            assert(!rect_go(&u, f, &a));
            temporal_rect_refused(&u, f);
        }
        assert(!u.stopped && u.unhooked == 0);
        /* a frame that was reached is not counted, and a gated frame never is */
        memset(&u, 0, sizeof u);
        for (f = 600; f < 640; f++) {
            rect_unreached(&u, f, &a);
            temporal_rect_hooked(&u, f);
        }
        assert(!u.stopped && u.unhooked == 0 && temporal_rect_steady(&u, 639));
        memset(&u, 0, sizeof u);
        for (f = 700; f < 740; f++) assert(!temporal_rect_decide(&u, f, &a, 1));
        assert(!u.stopped && u.unhooked == 0);
        /* a failure */
        memset(&u, 0, sizeof u);
        rect_go(&u, 800, &a);
        assert(rect_go(&u, 801, &a));
        temporal_rect_failed(&u, 801);
        assert(!temporal_rect_steady(&u, 801) && !u.have_candidate && !u.have_stamp);
        assert(!rect_go(&u, 802, &a) && !rect_go(&u, 803, &a) && u.stopped);
        /* A failure before the frame is confirmed, as when its first draw gets no jittered block: the frame is
           wrong for good, whatever a later draw shows and though our passes are reached, and the gate has
           stopped. */
        memset(&u, 0, sizeof u);
        rect_go(&u, 810, &a);
        assert(temporal_rect_decide(&u, 811, &a, 0) && u.state == TEMPORAL_FRAME_DECIDED);
        temporal_rect_failed(&u, 811);
        assert(u.stopped && temporal_rect_confirm(&u, 811, &good, &good) == TEMPORAL_FRAME_NONE);
        temporal_rect_hooked(&u, 811);
        assert(!temporal_rect_steady(&u, 811) && !temporal_rect_history(&u, 811, &stamp));
        temporal_rect_wrote(&u, 811, &stamp);
        assert(!u.have_stamp && temporal_rect_late(&u, 811, &a, NULL, &good));
        assert(!rect_go(&u, 812, &a) && !rect_go(&u, 813, &a));
    }
    /* The early refusal gates a frame whose geometry is the candidate's. Confirmed later in the frame, it stays
       gated: never resolved, no history written. It only makes the frame after acceptable. */
    memset(&g, 0, sizeof g);
    for (unsigned f = 300; f < 303; f++) rect_frame(&g, f, &a, RECT_FINE, &history);
    assert(history && !temporal_rect_decide(&g, 303, &a, 1) && !g.have_stamp);
    assert(temporal_rect_confirm(&g, 303, NULL, &good) == TEMPORAL_FRAME_SEEN && !temporal_rect_steady(&g, 303));
    temporal_rect_wrote(&g, 303, &stamp);
    assert(!g.have_stamp && !temporal_rect_history(&g, 303, &stamp));
    assert(rect_frame(&g, 304, &a, RECT_FINE, &history) && !history);
    assert(rect_frame(&g, 305, &a, RECT_FINE, &history) && history);
    /* a gated frame that is not confirmed either leaves nothing */
    assert(!temporal_rect_decide(&g, 306, &a, 1));
    assert(temporal_rect_confirm(&g, 306, NULL, &small) == TEMPORAL_FRAME_NONE && !g.have_candidate);
}

/* The reset of an evaluate with a context that has come into use since the last one. */
static void test_upscaler_reset(void) {
    unsigned last = 0;
    /* the first context: its first evaluate has the reset whatever the frame says, the next ones the frame's */
    assert(temporal_upscaler_reset(0, 1, &last) == 1 && last == 1);
    assert(temporal_upscaler_reset(0, 1, &last) == 0 && temporal_upscaler_reset(1, 1, &last) == 1);
    assert(temporal_upscaler_reset(0, 1, &last) == 0);
    /* The audit's sequence, as a run without the gate makes its frames' reset: every frame follows an evaluated
       one, so the frame's own reset is 0 throughout. A second context of the same sizes comes into use (a
       create property changed) between two such frames. Its first evaluate must have the reset. */
    {
        unsigned wrote = 0, frame = 0, published = 1, seen = 0;
        int resets[8];
        for (int i = 0; i < 8; i++) {
            frame++;
            if (i == 4) published = 2; /* the property changed: another context, the frames go on unbroken */
            const int own = wrote + 1 != frame; /* the ungated path's rule */
            resets[i] = temporal_upscaler_reset(i == 0 ? 1 : own, published, &seen);
            wrote = frame;
        }
        assert(resets[0] == 1 && !resets[1] && !resets[2] && !resets[3]);
        assert(resets[4] == 1 && !resets[5] && !resets[6] && !resets[7]);
        /* without the rule the fifth frame's own reset is 0: that was the fault */
        assert(wrote == 8 && seen == 2);
    }
    /* two contexts between two evaluates still give one reset, and a count that went back gives one too */
    last = 3;
    assert(temporal_upscaler_reset(0, 5, &last) == 1 && temporal_upscaler_reset(0, 5, &last) == 0);
    assert(temporal_upscaler_reset(0, 4, &last) == 1 && last == 4);
}

/* The game's projection decoded. A matrix of that form built from near, far and a field of view, by the
   formulas of the form written out here: y scale 1 / tan(fov / 2), x scale that over the aspect, A = n / (f - n)
   and B = n f / (f - n), or A = 0 and B = n for a far plane at infinity. */
static void projection_make(double n, double f, double fov, double aspect, float m[16]) {
    memset(m, 0, 16 * sizeof *m);
    m[5] = (float)(1.0 / tan(0.5 * fov));
    m[0] = (float)(1.0 / tan(0.5 * fov) / aspect);
    m[10] = isinf(f) ? 0.0f : (float)(n / (f - n));
    m[11] = -1.0f;
    m[14] = isinf(f) ? (float)n : (float)(n * f / (f - n));
}
/* the stored depth of a point at the distance d in front of the camera, by the form: (A z + B) / (-z), z = -d */
static double projection_depth(const float m[16], double d) {
    return ((double)m[10] * -d + (double)m[14]) / d;
}
static void test_projection_camera(void) {
    TemporalProjection p;
    float m[16];
    /* The two cameras a run printed (report 20261114-110024-report-84b8be), word for word, and what they are by
       hand: the field camera near 0.1, far 5000, 50 degrees, the first one near 0.01, far 120, 30 degrees. */
    const float field[16] = {1.20628512f, 0, 0, 0, 0, 2.14450693f, 0, 0, -0.0f, -0.0f, 2.00004015e-05f, -1, 0, 0,
                             0.100002006f, 0};
    const float first[16] = {2.09927869f, 0, 0, 0, 0, 3.7320509f, 0, 0, -0.0f, -0.0f, 8.3340281e-05f, -1, 0, 0,
                             0.0100008333f, 0};
    assert(temporal_projection_camera(field, &p) == TEMPORAL_PROJECTION_OK && !p.far_infinite && p.word == -1);
    assert(close_to(p.near_plane, 0.1, 1e-7) && close_to(p.far_plane, 5000.0, 0.01));
    assert(close_to(p.fov_y, 50.0 * 3.14159265358979323846 / 180.0, 1e-7));
    assert(close_to(projection_depth(field, p.near_plane), 1.0, 1e-12));
    assert(close_to(projection_depth(field, p.far_plane), 0.0, 1e-12));
    assert(temporal_projection_camera(first, &p) == TEMPORAL_PROJECTION_OK && !p.far_infinite);
    assert(close_to(p.near_plane, 0.01, 1e-8) && close_to(p.far_plane, 120.0, 1e-4));
    assert(close_to(p.fov_y, 30.0 * 3.14159265358979323846 / 180.0, 1e-7));
    /* Round trips: near, far and field of view into the form and out again, with the depth 1 at the near plane
       and 0 at the far one, for finite far planes and a far plane at infinity, where the depth only approaches 0. */
    static const double nears[4] = {0.01, 0.1, 0.5, 2.0}, fars[4] = {120.0, 5000.0, 60000.0, INFINITY};
    static const double fovs[3] = {0.5235987755982988, 0.8726646259971648, 1.5707963267948966};
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            for (int k = 0; k < 3; k++) {
                projection_make(nears[i], fars[j], fovs[k], 16.0 / 9.0, m);
                assert(temporal_projection_camera(m, &p) == TEMPORAL_PROJECTION_OK);
                assert(close_to(p.near_plane, nears[i], 1e-6 * nears[i]) && close_to(p.fov_y, fovs[k], 1e-6));
                assert(p.far_infinite == isinf(fars[j]));
                assert(close_to(projection_depth(m, p.near_plane), 1.0, 1e-9));
                if (isinf(fars[j])) {
                    assert(isinf(p.far_plane) && projection_depth(m, 1e12) > 0.0 && projection_depth(m, 1e12) < 1e-9);
                } else {
                    assert(close_to(p.far_plane, fars[j], 2e-3 * fars[j]));
                    assert(close_to(projection_depth(m, p.far_plane), 0.0, 1e-9));
                }
            }
    /* Refusals, each by the first rule it breaks, and the word it is. A scale that is not positive, which is a
       turned axis and another convention, or not finite. */
    static const float bad_scales[4] = {0.0f, -2.14450693f, NAN, INFINITY};
    for (int which = 0; which < 2; which++)
        for (int k = 0; k < 4; k++) {
            memcpy(m, field, sizeof m);
            m[which ? 5 : 0] = bad_scales[k];
            assert(temporal_projection_camera(m, &p) == TEMPORAL_PROJECTION_SCALE && p.word == (which ? 5 : 0));
        }
    /* word 11 must be exactly -1: the two floats beside it, +1, 0, and the whole matrix doubled */
    const float beside[5] = {nextafterf(-1.0f, 0.0f), nextafterf(-1.0f, -2.0f), 1.0f, 0.0f, NAN};
    for (int k = 0; k < 5; k++) {
        memcpy(m, field, sizeof m);
        m[11] = beside[k];
        assert(temporal_projection_camera(m, &p) == TEMPORAL_PROJECTION_W && p.word == 11);
    }
    for (int i = 0; i < 16; i++) m[i] = field[i] * 2.0f;
    assert(temporal_projection_camera(m, &p) == TEMPORAL_PROJECTION_W);
    /* word 14 positive and finite, word 10 finite and not negative */
    static const float bad_offsets[4] = {0.0f, -0.1f, NAN, INFINITY}, bad_slopes[3] = {-2.00004015e-05f, NAN, INFINITY};
    for (int k = 0; k < 4; k++) {
        memcpy(m, field, sizeof m);
        m[14] = bad_offsets[k];
        assert(temporal_projection_camera(m, &p) == TEMPORAL_PROJECTION_OFFSET && p.word == 14);
    }
    for (int k = 0; k < 3; k++) {
        memcpy(m, field, sizeof m);
        m[10] = bad_slopes[k];
        assert(temporal_projection_camera(m, &p) == TEMPORAL_PROJECTION_SLOPE && p.word == 10);
    }
    memcpy(m, field, sizeof m);
    m[10] = -0.0f; /* minus zero is zero: a far plane at infinity */
    assert(temporal_projection_camera(m, &p) == TEMPORAL_PROJECTION_OK && p.far_infinite && isinf(p.far_plane));
    /* Every word that must be zero, at the smallest float that is not zero, with either sign, and as no number.
       Either sign of zero itself is taken. */
    static const int zero[11] = {1, 2, 3, 4, 6, 7, 8, 9, 12, 13, 15};
    for (int i = 0; i < 11; i++) {
        const float tiny = nextafterf(0.0f, 1.0f), wrong[3] = {tiny, -tiny, NAN};
        for (int k = 0; k < 3; k++) {
            memcpy(m, field, sizeof m);
            m[zero[i]] = wrong[k];
            assert(temporal_projection_camera(m, &p) == TEMPORAL_PROJECTION_OTHER && p.word == zero[i]);
        }
        memcpy(m, field, sizeof m);
        m[zero[i]] = -0.0f;
        assert(temporal_projection_camera(m, &p) == TEMPORAL_PROJECTION_OK);
    }
    /* the view matrix the same run printed at +0x40: refused at word 11, which is 0 there */
    const float view[16] = {0.125333071f, 0.00251713023f, 0.992111564f, 0, 0, 0.999996781f, -0.00253713643f, 0,
                            -0.992114782f, 0.000317987171f, 0.125332713f, 0, 386.492981f, -62.9608231f, -598.443054f,
                            1};
    assert(temporal_projection_camera(view, &p) == TEMPORAL_PROJECTION_W && p.word == 11);
    /* an orthographic matrix has a w that is not from z: refused at word 11 as well */
    const float ortho[16] = {0.01f, 0, 0, 0, 0, 0.01f, 0, 0, 0, 0, 0.001f, 0, 0, 0, 0.5f, 1};
    assert(temporal_projection_camera(ortho, &p) == TEMPORAL_PROJECTION_W);
    /* Planes: with word 10 so large that 1 + A equals A in a double, near and far come out as the same number,
       so far is not beyond near. A field of view of pi, one the floats handed on cannot hold apart from pi,
       and the smallest near plane a float holds. */
    memcpy(m, field, sizeof m);
    m[14] = nextafterf(0.0f, 1.0f);
    m[10] = 3.0e38f;
    assert(temporal_projection_camera(m, &p) == TEMPORAL_PROJECTION_PLANES);
    memcpy(m, field, sizeof m);
    m[5] = nextafterf(0.0f, 1.0f); /* a y scale next to nothing: the field of view is pi */
    assert(temporal_projection_camera(m, &p) == TEMPORAL_PROJECTION_FOV);
    m[5] = 1e-8f; /* under pi by 2e-8 as a double, and pi or more as the float that is handed on */
    assert(temporal_projection_camera(m, &p) == TEMPORAL_PROJECTION_FLOATS && p.fov_y < 3.14159265358979323846);
    memcpy(m, field, sizeof m);
    m[14] = nextafterf(0.0f, 1.0f);
    m[10] = 0.0f; /* near 1.4e-45 is a float, the smallest: taken, since it is above 0 */
    assert(temporal_projection_camera(m, &p) == TEMPORAL_PROJECTION_OK && p.far_infinite);
    /* The check on the narrowed floats in its two parts. The camera's numbers alone, as the decoder asks at the
       frame's first draw, where no frame time exists. The frame time alone, at the dispatch. Both together
       give what the one check gave. */
    {
        float planes[3], time = -1.0f, all[4];
        assert(temporal_upscaler_planes(0.1, 5000.0, 0.8726646259971648, 0, planes));
        assert(planes[0] == 0.1f && planes[1] == 5000.0f && planes[2] == 0.8726646259971648f);
        assert(temporal_upscaler_planes(0.1, (double)INFINITY, 0.8726646259971648, 1, planes) && isinf(planes[1]));
        assert(!temporal_upscaler_planes(0.1, 5000.0, 0.8726646259971648, 1, planes));
        assert(!temporal_upscaler_planes(0.1, (double)INFINITY, 0.8726646259971648, 0, planes));
        /* near not positive, far not beyond near (as floats too), a field of view of 0, over pi, and pi or more
           as a float, no numbers, a near plane no float holds, a far plane over the largest float */
        static const double refused[13][3] = {{0.0, 5000.0, 0.8},  {-0.1, 5000.0, 0.8},        {0.1, 0.1, 0.8},
                                              {0.1, 0.05, 0.8},    {0.1, 0.1000000001, 0.8},   {0.1, 5000.0, 0.0},
                                              {0.1, 5000.0, 3.2},  {0.1, 5000.0, 3.14159264},  {NAN, 5000.0, 0.8},
                                              {0.1, NAN, 0.8},     {0.1, 5000.0, NAN},         {1e-50, 5000.0, 0.8},
                                              {0.1, 1e39, 0.8}};
        for (int k = 0; k < 13; k++)
            assert(!temporal_upscaler_planes(refused[k][0], refused[k][1], refused[k][2], 0, planes));
        assert(temporal_upscaler_time(1.0 / 60.0, &time) && time == (float)(1.0 / 60.0));
        assert(temporal_upscaler_time(0.25, &time) && time == 0.25f);
        time = -1.0f;
        assert(!temporal_upscaler_time(0.0, &time) && !temporal_upscaler_time(-0.016, &time) && time == -1.0f);
        assert(!temporal_upscaler_time(NAN, &time) && !temporal_upscaler_time((double)INFINITY, &time));
        assert(!temporal_upscaler_time(1e-50, &time) && !temporal_upscaler_time(1e36, &time)); /* 0, and no ms */
        assert(temporal_upscaler_camera(0.1, 5000.0, 0.8726646259971648, 1.0 / 60.0, 0, all));
        assert(all[0] == 0.1f && all[1] == 5000.0f && all[2] == 0.8726646259971648f && all[3] == (float)(1.0 / 60.0));
        assert(!temporal_upscaler_camera(0.1, 5000.0, 0.8726646259971648, 0.0, 0, all));
        assert(!temporal_upscaler_camera(0.0, 5000.0, 0.8726646259971648, 1.0 / 60.0, 0, all));
        /* the decoder asks for no frame time: its answer does not depend on one */
        assert(temporal_projection_camera(field, &p) == TEMPORAL_PROJECTION_OK);
    }
    /* what the decoder gives for the game's cameras is what the check before a dispatch takes */
    {
        float camera[4];
        assert(temporal_projection_camera(field, &p) == TEMPORAL_PROJECTION_OK);
        assert(temporal_upscaler_camera(p.near_plane, p.far_plane, p.fov_y, 1.0 / 60.0, p.far_infinite, camera));
        assert(close_to((double)camera[0], 0.1, 1e-7) && close_to((double)camera[1], 5000.0, 0.01));
        /* and it would not be taken as a far plane at infinity, nor an infinite one as finite */
        assert(!temporal_upscaler_camera(p.near_plane, p.far_plane, p.fov_y, 1.0 / 60.0, 1, camera));
        assert(!temporal_upscaler_camera(p.near_plane, (double)INFINITY, p.fov_y, 1.0 / 60.0, 0, camera));
    }
}

/* An adapter that is a module of its own (upscaler_module.h). */
static void test_upscaler_module(void) {
    /* Where a name is known. dlss in the game is refused by its name, before anything could be loaded or asked:
       also when a module would answer with no needs at all, or with needs the game's device already meets,
       either of which would otherwise start the upscaler. */
    assert(snx_module_name("fsr", 0) == SNX_MODULE_NAME_BUILT_IN);
    assert(snx_module_name("fsr", 1) == SNX_MODULE_NAME_BUILT_IN);
    assert(snx_module_name("dlss", 1) == SNX_MODULE_NAME_MODULE);
    assert(snx_module_name("dlss", 0) == SNX_MODULE_NAME_NOT_IN_GAME);
    assert(snx_module_name("DLSS", 0) == SNX_MODULE_NAME_UNKNOWN);
    assert(snx_module_name("dlss ", 0) == SNX_MODULE_NAME_UNKNOWN);
    assert(snx_module_name("xess", 1) == SNX_MODULE_NAME_UNKNOWN && snx_module_name("", 0) == SNX_MODULE_NAME_UNKNOWN);
    assert(snx_module_name(NULL, 0) == SNX_MODULE_NAME_UNKNOWN && snx_module_name(NULL, 1) == SNX_MODULE_NAME_UNKNOWN);
    {
        /* the two answers that would pass every other rule of the game's device and start */
        const char* const one[1] = {"VK_KHR_compute_shader_derivatives"};
        const SnxNeedsBits nothing = {0, 0, 0};
        SnxNeedsRecord r = {0};
        assert(snx_needs_none(0, NULL, 0));                                              /* an empty answer */
        assert(snx_needs_record(&r, "dlss", one, 1, &nothing, 0) == SNX_NEEDS_OK);
        assert(snx_needs_met(&r, "dlss", one, 1, NULL, 0) == SNX_NEEDS_OK);              /* one already met */
        assert(snx_module_name("dlss", 0) == SNX_MODULE_NAME_NOT_IN_GAME);               /* and still refused */
    }
    /* a full path, on Windows and elsewhere */
    assert(snx_module_full_path("C:\\games\\run\\snx_upscaler_dlss.dll", 1));
    assert(snx_module_full_path("c:/games/run/snx_upscaler_dlss.dll", 1));
    assert(snx_module_full_path("\\\\server\\share\\m.dll", 1));
    assert(!snx_module_full_path("snx_upscaler_dlss.dll", 1) && !snx_module_full_path(".\\m.dll", 1));
    assert(!snx_module_full_path("C:m.dll", 1));           /* relative to a drive's current folder */
    assert(!snx_module_full_path("\\games\\m.dll", 1));    /* rooted on the current drive */
    assert(!snx_module_full_path("/games/m.dll", 1));
    assert(!snx_module_full_path("C:\\games\\..\\m.dll", 1) && !snx_module_full_path("C:\\games\\.\\m.dll", 1));
    assert(!snx_module_full_path("C:\\games\\", 1) && !snx_module_full_path("C:\\", 1));
    assert(!snx_module_full_path("C:", 1));
    assert(!snx_module_full_path("\\\\.\\pipe\\m", 1) && !snx_module_full_path("\\\\?\\C:\\m.dll", 1));
    assert(!snx_module_full_path("1:\\m.dll", 1) && !snx_module_full_path("", 1) && !snx_module_full_path(NULL, 1));
    assert(snx_module_full_path("C:\\games\\a..b\\m.dll", 1) && snx_module_full_path("C:\\games\\...\\m.dll", 1));
    assert(snx_module_full_path("/opt/run/libm.so", 0) && snx_module_full_path("/m.so", 0));
    assert(!snx_module_full_path("m.so", 0) && !snx_module_full_path("./m.so", 0) && !snx_module_full_path("/", 0));
    assert(!snx_module_full_path("/opt/../m.so", 0) && !snx_module_full_path("/opt/run/", 0));
    assert(!snx_module_full_path("C:\\m.dll", 0)); /* a relative name there */
    /* The table. The version number is read first and alone: a table of another number may be four bytes long,
       and under the address sanitizer a read past them would be seen. */
    {
        void* calls[8];
        unsigned char table[16 + 7 * sizeof(void*)];
        const uint32_t ours = 3, other = 2;
        for (int k = 0; k < 8; k++) calls[k] = &calls[k];
        memset(table, 0, sizeof table);
        memcpy(table, &ours, 4);
        memcpy(table + 16, calls, 6 * sizeof(void*));
        assert(snx_module_table(table, 3) == SNX_MODULE_OK);
        /* the seventh, shutdown, may be NULL (it is here) or a call */
        memcpy(table + 16 + 6 * sizeof(void*), &calls[6], sizeof(void*));
        assert(snx_module_table(table, 3) == SNX_MODULE_OK);
        memset(table + 16 + 6 * sizeof(void*), 0, sizeof(void*));
        assert(snx_module_table(table, 4) == SNX_MODULE_OTHER_VERSION);
        assert(snx_module_table(NULL, 3) == SNX_MODULE_NO_TABLE);
        for (int k = 0; k < 6; k++) { /* each call alone missing */
            unsigned char lacks[sizeof table];
            memcpy(lacks, table, sizeof table);
            memset(lacks + 16 + (size_t)k * sizeof(void*), 0, sizeof(void*));
            assert(snx_module_table(lacks, 3) == SNX_MODULE_MISSING_CALL);
        }
        /* the name, at 8, is not one of the calls: a table without a name is whole */
        memset(table + 8, 0, 8);
        assert(snx_module_table(table, 3) == SNX_MODULE_OK);
        unsigned char* four = malloc(4);
        assert(four);
        memcpy(four, &other, 4);
        assert(snx_module_table(four, 3) == SNX_MODULE_OTHER_VERSION);
        free(four);
        /* a table of another number with every call missing is of another version, not one that lacks calls */
        memset(table + 16, 0, 6 * sizeof(void*));
        memcpy(table, &other, 4);
        assert(snx_module_table(table, 3) == SNX_MODULE_OTHER_VERSION);
    }
    for (int a = SNX_MODULE_OK; a <= SNX_MODULE_MISSING_CALL; a++)
        for (int b = a + 1; b <= SNX_MODULE_MISSING_CALL; b++) assert(strcmp(snx_module_text(a), snx_module_text(b)));
}

/* D3-2's pure rules: the larger texture's size, the edge fill, the entries, each draw condition alone, the latch */
static TemporalSpatialDraw spatial_draw(int k, double f) {
    /* the draws as P1 and P2 measured them: 21FBFBEC and B46268AD into a half-size target of 1920x1080 (B46268AD's
       scissor the fraction of it), the composite over the whole 3840x2160, every viewport flipped */
    TemporalSpatialDraw d;
    memset(&d, 0, sizeof d);
    const int big = k == 2, W = big ? 3840 : 1920, H = big ? 2160 : 1080;
    d.vs = temporal_spatial_entries[k].vs;
    d.direct = d.triangle_list = 1;
    d.vertex_count = 3;
    d.instance_count = 1;
    d.vertex_f[0] = d.vertex_f[1] = d.fragment_f[0] = d.fragment_f[1] = f;
    d.vertex_origin[0] = -1.0, d.vertex_origin[1] = 1.0; /* c6aaf96f's bank 1 as measured */
    d.vertex_f_ok = d.vertex_origin_ok = d.fragment_f_ok = d.divisor_ok = 1;
    d.divisor[0] = d.divisor[1] = 1.0;
    d.viewport[0] = 0, d.viewport[1] = H, d.viewport[2] = W, d.viewport[3] = -H;
    d.scissor_on = 1;
    d.scissor[2] = k == 1 ? (int)(W * f) : W;
    d.scissor[3] = k == 1 ? (int)(H * f) : H;
    d.target[0] = W, d.target[1] = H;
    d.query[0] = 1920, d.query[1] = 1080;
    d.normalised = d.one_level = d.filters_same = 1;
    d.mag_nearest = d.min_nearest = k != 1; /* B46268AD's sampler is linear with clamp to edge */
    d.address[0] = d.address[1] = k == 1 ? TEMPORAL_SPATIAL_EDGE : TEMPORAL_SPATIAL_BORDER;
    d.view_format_same = d.view_2d = d.view_one_level = d.view_one_layer = d.view_identity = 1;
    return d;
}
static void test_spatial(void) {
    int M, N;
    /* the scales D3 revision 2 allows at 3840x2160, and the ones it does not */
    static const int allowed[5][2] = {{1920, 1080}, {2304, 1296}, {2560, 1440}, {2880, 1620}, {3072, 1728}};
    static const int sizes[5][2] = {{7680, 4320}, {6400, 3600}, {5760, 3240}, {5120, 2880}, {4800, 2700}};
    for (int k = 0; k < 5; k++) {
        const double f = (double)allowed[k][0] / 3840;
        assert(temporal_spatial_size(3840, 2160, allowed[k][0], allowed[k][1], f, f, &M, &N));
        assert(M == sizes[k][0] && N == sizes[k][1]);
    }
    assert(!temporal_spatial_size(3840, 2160, 2560, 1440, 0.6667, 0.6667, &M, &N));      /* the fraction off */
    assert(!temporal_spatial_size(3840, 2160, 3456, 1944, 0.9, 0.9, &M, &N));            /* 3840x3840/3456 not whole */
    assert(!temporal_spatial_size(3840, 2160, 1920, 1080, 0.5, 0.5000011, &M, &N));     /* unequal */
    /* whole, each fraction its own, unequal */
    assert(!temporal_spatial_size(3840, 2160, 1920, 1440, 0.5, 1440.0 / 2160, &M, &N));
    assert(temporal_spatial_size(3840, 2160, 3840, 2160, 1.0, 1.0, &M, &N) && M == 3840 && N == 2160);
    assert(!temporal_spatial_size(3840, 2160, 3841, 2160, 1.0, 1.0, &M, &N));
    assert(!temporal_spatial_size(0, 2160, 0, 1080, 0.5, 0.5, &M, &N));
    /* the listed scales only (astra's O4): the five, on both axes alike; 1, 5/6, 9/10 and mixed ones are not */
    for (int k = 0; k < 5; k++) assert(temporal_spatial_scale_listed(3840, 2160, allowed[k][0], allowed[k][1]) == k);
    assert(temporal_spatial_scale_listed(1920, 1080, 960, 540) == 0);
    assert(temporal_spatial_scale_listed(1920, 1080, 1536, 864) == 4);
    assert(temporal_spatial_scale_listed(3840, 2160, 3840, 2160) < 0);
    assert(temporal_spatial_scale_listed(3840, 2160, 3200, 1800) < 0); /* 5/6: whole, and not listed */
    assert(temporal_spatial_size(3840, 2160, 3200, 1800, 3200.0 / 3840, 1800.0 / 2160, &M, &N));
    assert(temporal_spatial_scale_listed(3840, 2160, 3456, 1944) < 0);
    assert(temporal_spatial_scale_listed(3840, 2160, 1920, 1440) < 0); /* 1/2 by 2/3 */
    assert(temporal_spatial_scale_listed(3840, 2160, 1921, 1080) < 0);
    /* each scale's fraction, the float nearest p / q (research 12:50) */
    assert(temporal_spatial_scale_fraction(0) == 0.5f && temporal_spatial_scale_fraction(1) == 0.6f);
    assert(temporal_spatial_scale_fraction(2) == 2.0f / 3.0f && temporal_spatial_scale_fraction(3) == 0.75f);
    assert(temporal_spatial_scale_fraction(4) == 0.8f);
    /* the device's limits (astra's O5): an 8192 scene at 1/2 needs 16384, and the framebuffer's limit counts too */
    assert(temporal_spatial_fits(16384, 16384, 16384, 16384, 16384, 16384));
    assert(!temporal_spatial_fits(16384, 9216, 8192, 16384, 16384, 16384));
    assert(!temporal_spatial_fits(7680, 16385, 16384, 16384, 16384, 16384));
    assert(!temporal_spatial_fits(0, 1, 16384, 16384, 16384, 16384));
    assert(!temporal_spatial_fits(16384, 16384, 16384, 16384, 8192, 16384)); /* the framebuffer's width */
    assert(!temporal_spatial_fits(16384, 16384, 16384, 16384, 16384, 8192)); /* and height */
    /* a quotient past an int is refused before it is narrowed */
    /* equal fractions of 1/4096, one quotient 2^32 (past an int), the other 2^24 */
    assert(!temporal_spatial_size(1048576, 4096, 256, 1, 1.0 / 4096, 1.0 / 4096, &M, &N));
    assert(!temporal_spatial_size(4096, 1048576, 1, 256, 1.0 / 4096, 1.0 / 4096, &M, &N));
    assert(temporal_spatial_size(4096, 4096, 1, 1, 1.0 / 4096, 1.0 / 4096, &M, &N) && M == 16777216 && N == M);
    /* the confirmed fractions bit for bit (research 14:00): each table float, and one float step either way */
    for (int k = 0; k < 5; k++) {
        const float f = temporal_spatial_scale_fraction(k);
        assert(temporal_spatial_confirmed(f, f, k));
        assert(!temporal_spatial_confirmed(nextafterf(f, 1.0f), f, k));
        assert(!temporal_spatial_confirmed(f, nextafterf(f, 0.0f), k));
        assert(!temporal_spatial_confirmed(f, f, (k + 1) % 5));
    }
    assert(!temporal_spatial_confirmed(0.5000001f, 0.5f, 0) && !temporal_spatial_confirmed(-0.0f, -0.0f, 0));
    /* the final decision (astra's O2): a variant not made or the mode gone off draws the scene unkeyed, never drops */
    {
        int fault = 0;
        assert(temporal_spatial_final(4, 1, 1, &fault) == 4 && !fault);
        assert(temporal_spatial_final(4, 0, 1, &fault) == 0 && fault);
        assert(temporal_spatial_final(4, 1, 0, &fault) == 0 && fault);
        assert(temporal_spatial_final(0, 0, 0, &fault) == 0 && !fault);
    }
    /* the edge fill: right, bottom, corner, from the last column, row and pixel */
    TemporalSpatialRegion fill[3];
    assert(temporal_spatial_fill(3840, 2160, 7680, 4320, fill) == 3);
    assert(!memcmp(fill[0].from, (int[4]){3839, 0, 3840, 2160}, sizeof fill[0].from));
    assert(!memcmp(fill[0].to, (int[4]){3840, 0, 7680, 2160}, sizeof fill[0].to));
    assert(!memcmp(fill[1].from, (int[4]){0, 2159, 3840, 2160}, sizeof fill[1].from));
    assert(!memcmp(fill[1].to, (int[4]){0, 2160, 3840, 4320}, sizeof fill[1].to));
    assert(!memcmp(fill[2].from, (int[4]){3839, 2159, 3840, 2160}, sizeof fill[2].from));
    assert(!memcmp(fill[2].to, (int[4]){3840, 2160, 7680, 4320}, sizeof fill[2].to));
    assert(temporal_spatial_fill(3840, 2160, 3840, 2160, fill) == 0);
    /* the scale: weights that sum to 1 and pass through the taps */
    for (int k = 0; k <= 8; k++) {
        double wt[4];
        temporal_spatial_weights(k / 8.0, wt);
        assert(fabs(wt[0] + wt[1] + wt[2] + wt[3] - 1.0) < 1e-12);
        if (k == 0) assert(wt[1] == 1.0 && wt[0] == 0.0 && wt[2] == 0.0 && wt[3] == 0.0);
    }
    {
        enum { PW = 12, PH = 8 };
        float ramp[PW * PH], flat[PW * PH], step[PW * PH];
        for (int y = 0; y < PH; y++)
            for (int x = 0; x < PW; x++) {
                ramp[y * PW + x] = (float)(3 * x + 2 * y + 1);
                flat[y * PW + x] = 0.25f;
                step[y * PW + x] = x < PW / 2 ? 0.0f : 1.0f;
            }
        for (int y = 0; y < PH; y++) /* scale 1: the picture itself */
            for (int x = 0; x < PW; x++)
                assert(fabs(temporal_spatial_catmull_rom(ramp, PW, PH, PW, PH, x, y) - ramp[y * PW + x]) < 1e-9);
        for (int y = 0; y < 2 * PH; y++) /* twice the size: a constant everywhere, the edges included */
            for (int x = 0; x < 2 * PW; x++)
                assert(fabs(temporal_spatial_catmull_rom(flat, PW, PH, 2 * PW, 2 * PH, x, y) - 0.25) < 1e-9);
        for (int y = 4; y < 2 * PH - 4; y++) /* a ramp exactly, away from the clamped edge */
            for (int x = 4; x < 2 * PW - 4; x++) {
                const double sx = (x + 0.5) / 2 - 0.5, sy = (y + 0.5) / 2 - 0.5;
                assert(fabs(temporal_spatial_catmull_rom(ramp, PW, PH, 2 * PW, 2 * PH, x, y) - (3 * sx + 2 * sy + 1)) <
                       1e-9);
            }
        for (int x = 0; x < 2 * PW; x++) /* a hard edge: never below 0, the undershoot cut */
            assert(temporal_spatial_catmull_rom(step, PW, PH, 2 * PW, 2 * PH, x, 3) >= 0.0);
        assert(temporal_spatial_catmull_rom(step, PW, PH, 2 * PW, 2 * PH, PW - 2, 3) == 0.0);
        { /* the right edge: x 23 of 24 falls at 11.25, its taps 10 and 11, then 11 twice held to the picture.
             y 5 of 16 falls at 2.25, inside, where the ramp's y part 2 y comes out exactly. */
            double wt[4];
            temporal_spatial_weights(0.25, wt);
            const double expect = wt[0] * 30 + (wt[1] + wt[2] + wt[3]) * 33 + 2 * 2.25 + 1;
            assert(fabs(temporal_spatial_catmull_rom(ramp, PW, PH, 2 * PW, 2 * PH, 2 * PW - 1, 5) - expect) < 1e-9);
            assert(fabs(expect - 38.7109375) < 1e-12);
            /* the top edge the same way: y 0 of 16 falls at -0.25, its taps -2, -1 held to row 0, then rows 0, 1 */
            temporal_spatial_weights(0.75, wt);
            const double top = wt[3] * 2 + 3 * 2.25 + 1; /* row 1's y part is 2, row 0's 0; x 5 falls at 2.25 */
            assert(fabs(temporal_spatial_catmull_rom(ramp, PW, PH, 2 * PW, 2 * PH, 5, 0) - top) < 1e-9);
        }
        /* 2/3: W = 18 from w = 12, the first and last outputs from the picture's edge pixels and their neighbours */
        assert(fabs(temporal_spatial_catmull_rom(flat, PW, PH, 18, 12, 17, 11) - 0.25) < 1e-9);
    }
    /* the entries, by whole hash and handle */
    assert(temporal_spatial_entry(0x21fbfbecbf90f4a3ull, 0x28) == 0); /* revision 4: 0x28 for all three */
    assert(temporal_spatial_entry(0xb46268adda62e218ull, 0x28) == 1);
    assert(temporal_spatial_entry(0x8a160a27d90af72dull, 0x28) == 2);
    assert(temporal_spatial_entry(0x21fbfbecbf90f4a3ull, 0x2A) == -1); /* recon's earlier pairing, withdrawn */
    assert(temporal_spatial_entry(0x8a160a27d90af72dull, 0x36) == -1);
    assert(temporal_spatial_entry(0x8a160a27d90af72dull, 0x2A) == -1);
    assert(temporal_spatial_entry(0x63b3848c1a6e23e0ull, 0x36) == -1);
    /* each measured draw is taken at both fractions; each condition alone refuses it */
    for (int k = 0; k < 3; k++)
        for (int fi = 0; fi < 2; fi++) {
            const double f = fi ? 0.5 : 1.0, ff[2] = {f, f};
            TemporalSpatialDraw d = spatial_draw(k, f), x;
            assert(temporal_spatial_check(k, &d, ff) == TEMPORAL_SPATIAL_TAKEN);
            assert(temporal_spatial_check(-1, &d, ff) == TEMPORAL_SPATIAL_NOT_LISTED);
#define REFUSED(change, why)                                                                                   \
    do {                                                                                                       \
        x = d;                                                                                                 \
        change;                                                                                                \
        assert(temporal_spatial_check(k, &x, ff) == (why));                                                    \
        assert(temporal_spatial_failed(k, &x, ff) == 1u << (why)); /* the log's mask: that one alone */      \
    } while (0)
            REFUSED(x.vs ^= 1, TEMPORAL_SPATIAL_VERTEX);
            REFUSED(x.direct = 0, TEMPORAL_SPATIAL_FORM);
            REFUSED(x.triangle_list = 0, TEMPORAL_SPATIAL_FORM);
            REFUSED(x.first_vertex = 1, TEMPORAL_SPATIAL_FORM);
            REFUSED(x.vertex_count = 4, TEMPORAL_SPATIAL_FORM);
            REFUSED(x.instance_count = 2, TEMPORAL_SPATIAL_FORM);
            REFUSED(x.first_instance = 1, TEMPORAL_SPATIAL_FORM);
            REFUSED(x.other_stages = 1, TEMPORAL_SPATIAL_STAGES);
            { /* the classification (d08b624c, research 14:35, 14:45): only the fraction and the tap and coverage
                 bounds are class N, everything else class U, whatever fails with it */
                int v;
                assert(temporal_spatial_classify(k, &d, ff, &v) == k && v == TEMPORAL_SPATIAL_TAKEN);
                /* availability (research 15:00): a required value not available is class U before any comparison,
                   with a readable disagreement too; one the entry's programs do not read may be absent */
                const int needs_fragment = k != 1, needs_vertex = k == 0, needs_divisor = k == 2;
                for (int which = 0; which < 4; which++) {
                    x = d;
                    if (which == 0) x.fragment_f_ok = 0;
                    if (which == 1) x.vertex_f_ok = 0;
                    if (which == 2) x.vertex_origin_ok = 0;
                    if (which == 3) x.divisor_ok = 0;
                    const int needed = which == 0 ? needs_fragment : which == 3 ? needs_divisor : needs_vertex;
                    const int k2 = temporal_spatial_classify(k, &x, ff, &v);
                    if (needed) assert(k2 == -1 && v == TEMPORAL_SPATIAL_UNAVAILABLE);
                    else assert(k2 == k && v == TEMPORAL_SPATIAL_TAKEN);
                    x.fragment_f[1] = f * 0.999; /* and a fraction that disagrees */
                    x.viewport[2] = 0;
                    const int k3 = temporal_spatial_classify(k, &x, ff, &v);
                    if (needed) assert(k3 == -1 && v == TEMPORAL_SPATIAL_UNAVAILABLE);
                    else
                        assert(k3 == k && v == (k == 1 ? TEMPORAL_SPATIAL_COVERAGE : TEMPORAL_SPATIAL_FRACTION));
                }
                x = d; /* a readable NaN is a value that disagrees: class N for the fraction */
                x.fragment_f[0] = NAN;
                assert(temporal_spatial_classify(k, &x, ff, &v) == k && v == (k == 1 ? TEMPORAL_SPATIAL_TAKEN
                                                                                     : TEMPORAL_SPATIAL_FRACTION));
                assert(temporal_spatial_classify(-1, &d, ff, &v) == -1 && v == TEMPORAL_SPATIAL_NOT_LISTED);
#define CLASS_U(change, why)                                                                                    \
    do {                                                                                                        \
        x = d;                                                                                                  \
        change;                                                                                                 \
        assert(temporal_spatial_classify(k, &x, ff, &v) == -1 && v == (why));                                 \
        if (k != 1) x.fragment_f[0] = f * 0.999; /* with a class N failure too: still class U */                 \
        x.viewport[2] = 0;                                                                                      \
        assert(temporal_spatial_classify(k, &x, ff, &v) == -1 && v == (why));                                 \
    } while (0)
                CLASS_U(x.vs ^= 1, TEMPORAL_SPATIAL_VERTEX);
                CLASS_U(x.first_vertex = 1, TEMPORAL_SPATIAL_FORM);
                CLASS_U(x.direct = 0, TEMPORAL_SPATIAL_FORM);
                CLASS_U(x.other_stages = 1, TEMPORAL_SPATIAL_STAGES);
                CLASS_U(x.normalised = 0, TEMPORAL_SPATIAL_SAMPLER);
                CLASS_U(x.filters_same = 0, TEMPORAL_SPATIAL_SAMPLER);
                CLASS_U(x.address[0] = TEMPORAL_SPATIAL_OTHER_ADDRESS, TEMPORAL_SPATIAL_SAMPLER);
                CLASS_U(x.view_format_same = 0, TEMPORAL_SPATIAL_VIEW);
                CLASS_U(x.view_2d = 0, TEMPORAL_SPATIAL_VIEW);
                CLASS_U(x.view_one_level = 0, TEMPORAL_SPATIAL_VIEW);
                CLASS_U(x.view_one_layer = 0, TEMPORAL_SPATIAL_VIEW);
                CLASS_U(x.view_identity = 0, TEMPORAL_SPATIAL_VIEW);
                if (k == 0) CLASS_U(x.vertex_origin[0] = -2.0, TEMPORAL_SPATIAL_ORIGIN);
                if (k == 0) CLASS_U(x.vertex_origin[1] = NAN, TEMPORAL_SPATIAL_ORIGIN);
                if (k == 2) CLASS_U(x.divisor[1] = 2.0, TEMPORAL_SPATIAL_DIVISOR);
                /* astra's combined case: a bad origin with a bad form is class U, not a listed reader refused */
                x = d;
                x.first_vertex = 1;
                x.vertex_origin[0] = -2.0;
                assert(temporal_spatial_classify(k, &x, ff, &v) == -1 && v == TEMPORAL_SPATIAL_FORM);
#undef CLASS_U
                /* class N alone: listed, refused, with its reason */
                x = d;
                x.viewport[2] = 0;
                assert(temporal_spatial_classify(k, &x, ff, &v) == k && v == TEMPORAL_SPATIAL_COVERAGE);
                if (k != 1) {
                    x = d;
                    x.fragment_f[1] = f * 0.999;
                    assert(temporal_spatial_classify(k, &x, ff, &v) == k && v == TEMPORAL_SPATIAL_FRACTION);
                    x.viewport[2] = 0; /* both of class N: the fraction is the one said */
                    assert(temporal_spatial_classify(k, &x, ff, &v) == k && v == TEMPORAL_SPATIAL_FRACTION);
                }
            }
            if (k == 0) { /* c6aaf96f's origin, as measured or not (astra's O1) */
                REFUSED(x.vertex_origin[0] = -2.0, TEMPORAL_SPATIAL_ORIGIN);
                REFUSED(x.vertex_origin[1] = -1.0, TEMPORAL_SPATIAL_ORIGIN);
                REFUSED(x.vertex_origin[0] = NAN, TEMPORAL_SPATIAL_ORIGIN);
            } else { /* 26fc2347 reads no constant: its bank 1 is not looked at */
                x = d;
                x.vertex_origin[0] = x.vertex_origin[1] = NAN;
                assert(temporal_spatial_check(k, &x, ff) == TEMPORAL_SPATIAL_TAKEN);
                x.vertex_origin_ok = x.vertex_f_ok = 0; /* not available either: not read, so taken */
                assert(temporal_spatial_check(k, &x, ff) == TEMPORAL_SPATIAL_TAKEN);
            }
            if (k != 1) {
                REFUSED(x.fragment_f[1] = f * 0.999, TEMPORAL_SPATIAL_FRACTION);
                REFUSED(x.fragment_f[0] = NAN, TEMPORAL_SPATIAL_FRACTION);
            } else {
                x = d;
                x.fragment_f[0] = NAN; /* B46268AD reads no fraction: its block's value is not held */
                x.fragment_f_ok = 0;
                assert(temporal_spatial_check(k, &x, ff) == TEMPORAL_SPATIAL_TAKEN);
            }
            if (k == 0) REFUSED(x.vertex_f[0] = f * 1.001, TEMPORAL_SPATIAL_FRACTION);
            if (k == 2) {
                REFUSED(x.divisor[0] = 2.0, TEMPORAL_SPATIAL_DIVISOR);
                REFUSED(x.divisor[1] = NAN, TEMPORAL_SPATIAL_DIVISOR);
            }
            REFUSED(x.normalised = 0, TEMPORAL_SPATIAL_SAMPLER);
            REFUSED(x.one_level = 0, TEMPORAL_SPATIAL_SAMPLER);
            REFUSED(x.anisotropy = 1, TEMPORAL_SPATIAL_SAMPLER);
            REFUSED(x.compare = 1, TEMPORAL_SPATIAL_SAMPLER);
            REFUSED(x.filters_same = 0, TEMPORAL_SPATIAL_SAMPLER);
            if (k == 1 && fi) { /* the coverage of each axis by its own fraction: the height whole at 1 is taken */
                const double wide[2] = {0.5, 1.0};
                x = d;
                x.scissor[3] = d.target[1];
                assert(temporal_spatial_check(k, &x, wide) == TEMPORAL_SPATIAL_TAKEN);
            }
            { /* each axis is held to its own fraction: a height fraction one float step away refuses */
                const double other[2] = {f, nextafter(f, 0.0)};
                x = d;
                if (k != 1) assert(temporal_spatial_check(k, &x, other) == TEMPORAL_SPATIAL_FRACTION);
            }
            REFUSED(x.address[1] = TEMPORAL_SPATIAL_OTHER_ADDRESS, TEMPORAL_SPATIAL_SAMPLER);
            REFUSED((x.address[0] = TEMPORAL_SPATIAL_BORDER, x.min_nearest = 0), TEMPORAL_SPATIAL_SAMPLER);
            REFUSED((x.address[0] = TEMPORAL_SPATIAL_BORDER, x.mag_nearest = 0), TEMPORAL_SPATIAL_SAMPLER);
            REFUSED(x.view_format_same = 0, TEMPORAL_SPATIAL_VIEW);
            REFUSED(x.view_2d = 0, TEMPORAL_SPATIAL_VIEW);
            REFUSED(x.view_one_level = 0, TEMPORAL_SPATIAL_VIEW);
            REFUSED(x.view_one_layer = 0, TEMPORAL_SPATIAL_VIEW);
            REFUSED(x.view_identity = 0, TEMPORAL_SPATIAL_VIEW);
            /* not flipped: coordinate 1 at the top */
            REFUSED(x.viewport[3] = -x.viewport[3], TEMPORAL_SPATIAL_COVERAGE);
            REFUSED(x.viewport[2] = 0, TEMPORAL_SPATIAL_COVERAGE);
            REFUSED(x.scissor[2] = x.scissor[0], TEMPORAL_SPATIAL_COVERAGE); /* nothing covered */
            /* no size of the scene colour is in its taps any more (revision 4): a query answer of 0 changes nothing */
            x = d;
            x.query[0] = x.query[1] = 0;
            assert(temporal_spatial_check(k, &x, ff) == TEMPORAL_SPATIAL_TAKEN);

            if (fi && k == 1) REFUSED(x.scissor[2] = d.target[0], TEMPORAL_SPATIAL_COVERAGE); /* beyond f */
            if (fi && k == 1) REFUSED(x.scissor[2] += 1, TEMPORAL_SPATIAL_COVERAGE); /* one pixel beyond f */
            if (!fi && k == 1) /* a centre at coordinate 1 exactly: within f = 1, and a tap not below 1 */
                REFUSED((x.viewport[0] = -0.5, x.scissor_on = 0), TEMPORAL_SPATIAL_COVERAGE);
#undef REFUSED
        }
    /* The reviewer's six combined cases (reviewer.md 04:02, /tmp/reviewer-d3-0a-combined.c, astra's combined case),
       ported: c6aaf96f's origin missing or (-2, +1), each with a vertex count of 4, a draw not direct, or another
       stage. Each is a reader not on the list, given the scene as drawn, the mode off from the next frame. So are the
       three faults with the measured origin (d08b624c: class U). */
    for (int origin = 0; origin < 3; origin++)
        for (int fault = 1; fault < 4; fault++) {
            TemporalSpatialDraw x = spatial_draw(0, 0.5);
            const double half[2] = {0.5, 0.5};
            if (origin == 0) x.vertex_origin_ok = 0; /* missing: not available (research 15:00) */
            if (origin == 2) x.vertex_origin[0] = -2.0;
            if (fault == 1) x.vertex_count = 4;
            if (fault == 2) x.direct = 0;
            if (fault == 3) x.other_stages = 1;
            int v;
            const int k = temporal_spatial_classify(0, &x, half, &v);
            assert(k == -1 && v == (origin == 0       ? TEMPORAL_SPATIAL_UNAVAILABLE
                                    : fault == 3        ? TEMPORAL_SPATIAL_STAGES
                                                        : TEMPORAL_SPATIAL_FORM));
            TemporalSpatialLatch l = {0, 0, 0, NULL};
            TemporalSpatialFrame fr = {1, 0, 0};
            assert(temporal_spatial_draw(&fr, &l, k, v) == TEMPORAL_SPATIAL_AS_DRAWN && !fr.decided && !fr.published);
            assert(l.off_next && !l.off && temporal_spatial_use(&fr, &l, 0, TEMPORAL_SPATIAL_AS_DRAWN, "a binding") ==
                                                  TEMPORAL_SPATIAL_CONSUMER);
        }
    /* the log's mask of every failure (research 15:40): several at once, each named, none when taken */
    {
        const double half[2] = {0.5, 0.5};
        for (int k = 0; k < 3; k++) assert(temporal_spatial_failed(k, &(TemporalSpatialDraw){0}, half) != 0);
        TemporalSpatialDraw x = spatial_draw(2, 0.5);
        assert(temporal_spatial_failed(2, &x, half) == 0);
        x.first_vertex = 1, x.vertex_origin[0] = -2.0, x.divisor[0] = 2.0, x.view_2d = 0, x.fragment_f[0] = 0.4;
        x.viewport[2] = 0;
        const uint32_t m = temporal_spatial_failed(2, &x, half); /* the composite reads no origin */
        assert(m == (1u << TEMPORAL_SPATIAL_FORM | 1u << TEMPORAL_SPATIAL_DIVISOR | 1u << TEMPORAL_SPATIAL_VIEW |
                     1u << TEMPORAL_SPATIAL_FRACTION | 1u << TEMPORAL_SPATIAL_COVERAGE));
        assert(temporal_spatial_check(2, &x, half) == TEMPORAL_SPATIAL_FORM); /* the mask's lowest is the check's */
        x = spatial_draw(0, 0.5);
        x.vs ^= 1, x.fragment_f_ok = 0, x.vertex_origin[1] = 3.0, x.anisotropy = 1; /* the origin readable */
        const uint32_t four = 1u << TEMPORAL_SPATIAL_VERTEX | 1u << TEMPORAL_SPATIAL_UNAVAILABLE |
                              1u << TEMPORAL_SPATIAL_ORIGIN | 1u << TEMPORAL_SPATIAL_SAMPLER;
        assert(temporal_spatial_failed(0, &x, half) == four);
        assert(temporal_spatial_failed(-1, &x, half) == 1u << TEMPORAL_SPATIAL_NOT_LISTED);
        /* a value not available is named by its availability alone, its placeholder never compared (reviewer
           04:42); a readable fault beside it is still named */
        x = spatial_draw(0, 0.5);
        x.fragment_f_ok = x.vertex_f_ok = x.vertex_origin_ok = 0;
        x.fragment_f[0] = x.vertex_f[1] = x.vertex_origin[0] = 0.0; /* placeholders */
        assert(temporal_spatial_failed(0, &x, half) == 1u << TEMPORAL_SPATIAL_UNAVAILABLE);
        x.view_2d = 0;
        const uint32_t two = 1u << TEMPORAL_SPATIAL_UNAVAILABLE | 1u << TEMPORAL_SPATIAL_VIEW;
        assert(temporal_spatial_failed(0, &x, half) == two);
        x = spatial_draw(2, 0.5);
        x.divisor_ok = 0, x.divisor[0] = 0.0;
        assert(temporal_spatial_failed(2, &x, half) == 1u << TEMPORAL_SPATIAL_UNAVAILABLE);
        x.divisor_ok = 1; /* readable and wrong: named */
        assert(temporal_spatial_failed(2, &x, half) == 1u << TEMPORAL_SPATIAL_DIVISOR);
    }
    for (int a = 0; a < TEMPORAL_SPATIAL_REASONS; a++)
        for (int b = a + 1; b < TEMPORAL_SPATIAL_REASONS; b++)
            assert(strcmp(temporal_spatial_text(a), temporal_spatial_text(b)));
    /* the latch: a make failure off at once; an unknown reader off from the next frame; eight eligible frames in
       a row not published; ineligible frames neither count nor reset */
    TemporalSpatialLatch l = {0, 0, 0, NULL};
    assert(temporal_spatial_frame(&l));
    temporal_spatial_make_failed(&l, "the larger image");
    assert(!temporal_spatial_frame(&l) && l.why && !strcmp(l.why, "the larger image"));
    memset(&l, 0, sizeof l);
    assert(temporal_spatial_frame(&l));
    temporal_spatial_unknown(&l, "an unknown reader");
    assert(!l.off); /* the frame it was found in goes on */
    assert(!temporal_spatial_frame(&l) && l.off && !strcmp(l.why, "an unknown reader"));
    memset(&l, 0, sizeof l);
    for (int i = 0; i < 7; i++) {
        temporal_spatial_outcome(&l, 1, 0);
        temporal_spatial_outcome(&l, 0, 1); /* an ineligible frame between: no reset */
        assert(temporal_spatial_frame(&l));
    }
    temporal_spatial_outcome(&l, 1, 0); /* the eighth, across the ineligible ones */
    assert(!temporal_spatial_frame(&l) && l.off);
    memset(&l, 0, sizeof l);
    for (int i = 0; i < 7; i++) temporal_spatial_outcome(&l, 1, 0);
    temporal_spatial_outcome(&l, 1, 1); /* a published frame resets the count */
    assert(l.unpublished == 0 && temporal_spatial_frame(&l));
    for (int i = 0; i < 7; i++) temporal_spatial_outcome(&l, 1, 0);
    assert(temporal_spatial_frame(&l));
    temporal_spatial_outcome(&l, 1, 0);
    assert(!temporal_spatial_frame(&l) && l.off);
    /* a frame draw by draw (research 11:10). Nothing recorded at the hook: every reader as drawn, nothing counted */
    TemporalSpatialFrame fr = {0, 0, 0};
    memset(&l, 0, sizeof l);
    assert(temporal_spatial_draw(&fr, &l, 0, TEMPORAL_SPATIAL_TAKEN) == TEMPORAL_SPATIAL_AS_DRAWN);
    assert(temporal_spatial_draw(&fr, &l, -1, TEMPORAL_SPATIAL_NOT_LISTED) == TEMPORAL_SPATIAL_AS_DRAWN);
    temporal_spatial_frame_end(&fr, &l);
    assert(!l.off_next && l.unpublished == 0 && temporal_spatial_frame(&l));
    /* published: the first listed reader taken, the later ones taken too */
    fr = (TemporalSpatialFrame){1, 0, 0};
    assert(temporal_spatial_draw(&fr, &l, 0, TEMPORAL_SPATIAL_TAKEN) == TEMPORAL_SPATIAL_LARGER && fr.published);
    assert(temporal_spatial_draw(&fr, &l, 1, TEMPORAL_SPATIAL_TAKEN) == TEMPORAL_SPATIAL_LARGER);
    assert(temporal_spatial_draw(&fr, &l, 2, TEMPORAL_SPATIAL_TAKEN) == TEMPORAL_SPATIAL_LARGER);
    temporal_spatial_frame_end(&fr, &l);
    assert(!l.off_next && l.unpublished == 0 && temporal_spatial_frame(&l));
    /* (b) the first listed reader refused: not published, every later reader as drawn, counted, no stop */
    fr = (TemporalSpatialFrame){1, 0, 0};
    assert(temporal_spatial_draw(&fr, &l, 0, TEMPORAL_SPATIAL_FORM) == TEMPORAL_SPATIAL_AS_DRAWN && !fr.published);
    assert(temporal_spatial_draw(&fr, &l, 1, TEMPORAL_SPATIAL_TAKEN) == TEMPORAL_SPATIAL_AS_DRAWN);
    assert(temporal_spatial_draw(&fr, &l, 2, TEMPORAL_SPATIAL_COVERAGE) == TEMPORAL_SPATIAL_AS_DRAWN);
    temporal_spatial_frame_end(&fr, &l);
    assert(!l.off_next && l.unpublished == 1 && temporal_spatial_frame(&l));
    /* a reader not on the list in an unpublished frame: nothing is mixed in it, but the list is not whole, so the
       mode is off from the next frame */
    fr = (TemporalSpatialFrame){1, 0, 0};
    assert(temporal_spatial_draw(&fr, &l, 0, TEMPORAL_SPATIAL_VIEW) == TEMPORAL_SPATIAL_AS_DRAWN);
    assert(temporal_spatial_draw(&fr, &l, -1, TEMPORAL_SPATIAL_NOT_LISTED) == TEMPORAL_SPATIAL_AS_DRAWN);
    assert(l.off_next && !strcmp(l.why, "a reader not on the list"));
    temporal_spatial_frame_end(&fr, &l);
    assert(l.unpublished == 2 && !temporal_spatial_frame(&l));
    /* one before the frame's first listed reader: it does not decide the frame, the listed one does, and the mode is
       off from the next frame */
    memset(&l, 0, sizeof l);
    fr = (TemporalSpatialFrame){1, 0, 0};
    assert(temporal_spatial_draw(&fr, &l, -1, TEMPORAL_SPATIAL_NOT_LISTED) == TEMPORAL_SPATIAL_AS_DRAWN);
    assert(!fr.decided && l.off_next);
    assert(temporal_spatial_draw(&fr, &l, 0, TEMPORAL_SPATIAL_TAKEN) == TEMPORAL_SPATIAL_LARGER && fr.published);
    temporal_spatial_frame_end(&fr, &l);
    assert(!temporal_spatial_frame(&l));
    memset(&l, 0, sizeof l);
    /* (a) a later listed reader refused in a published frame: as drawn, the frame stays published, off next frame */
    fr = (TemporalSpatialFrame){1, 0, 0};
    assert(temporal_spatial_draw(&fr, &l, 0, TEMPORAL_SPATIAL_TAKEN) == TEMPORAL_SPATIAL_LARGER);
    assert(temporal_spatial_draw(&fr, &l, 2, TEMPORAL_SPATIAL_DIVISOR) == TEMPORAL_SPATIAL_AS_DRAWN);
    assert(l.off_next && !l.off && fr.published && !strcmp(l.why, "a listed reader refused in a published frame"));
    temporal_spatial_frame_end(&fr, &l);
    assert(l.unpublished == 0 && !temporal_spatial_frame(&l));
    /* a reader not on the list in a published frame: the same */
    memset(&l, 0, sizeof l);
    fr = (TemporalSpatialFrame){1, 0, 0};
    assert(temporal_spatial_draw(&fr, &l, 1, TEMPORAL_SPATIAL_TAKEN) == TEMPORAL_SPATIAL_LARGER);
    assert(temporal_spatial_draw(&fr, &l, -1, TEMPORAL_SPATIAL_NOT_LISTED) == TEMPORAL_SPATIAL_AS_DRAWN);
    assert(l.off_next && !strcmp(l.why, "a reader not on the list"));
    temporal_spatial_draw(&fr, &l, 2, TEMPORAL_SPATIAL_DIVISOR); /* a second reason: the first is kept */
    temporal_spatial_make_failed(&l, "the larger image");
    assert(!strcmp(l.why, "a reader not on the list"));
    temporal_spatial_frame_end(&fr, &l);
    assert(!temporal_spatial_frame(&l));
    /* the eighth unpublished frame after an unknown reader in the same frame: the reader's reason is kept */
    memset(&l, 0, sizeof l);
    for (int i = 0; i < 7; i++) temporal_spatial_outcome(&l, 1, 0);
    temporal_spatial_unknown(&l, "a reader not on the list");
    temporal_spatial_outcome(&l, 1, 0);
    assert(l.off && !strcmp(l.why, "a reader not on the list"));
    /* a bound surface over the scene's memory (A2, research 12:15): the scene's own range, one that shares a byte,
       one that only touches it, one apart, a parked one (bit 63) over it, and empty ranges */
    {
        const uint64_t scene = 0x400000000ull, bytes = 0x2000000ull;
        assert(temporal_spatial_overlaps(scene, bytes, scene, bytes));
        assert(temporal_spatial_overlaps(scene + bytes - 1, 0x1000, scene, bytes));
        assert(temporal_spatial_overlaps(scene - 0x1000, 0x1001, scene, bytes));
        assert(temporal_spatial_overlaps(scene + 0x100000, 0x1000, scene, bytes)); /* a crop inside it */
        assert(!temporal_spatial_overlaps(scene + bytes, 0x1000, scene, bytes));
        assert(!temporal_spatial_overlaps(scene - 0x1000, 0x1000, scene, bytes));
        assert(!temporal_spatial_overlaps(0x500000000ull, bytes, scene, bytes));
        assert(temporal_spatial_overlaps(scene | 1ull << 63, bytes, scene, bytes));
        assert(temporal_spatial_overlaps(scene, bytes, scene | 1ull << 63, bytes));
        assert(!temporal_spatial_overlaps(scene, 0, scene, bytes));
        assert(!temporal_spatial_overlaps(scene, bytes, scene, 0));
        assert(!temporal_spatial_overlaps(scene + 16, 0, scene, bytes)); /* empty, inside it */
    }
    /* the watch (research 11:25): not armed before the hook's end */
    memset(&l, 0, sizeof l);
    fr = (TemporalSpatialFrame){0, 0, 0};
    assert(temporal_spatial_use(&fr, &l, 0, -1, "a draw into it") == TEMPORAL_SPATIAL_UNARMED && !l.off_next);
    /* armed in an unpublished frame: the listed reader's own binding of the scene is expected, nothing counted */
    fr = (TemporalSpatialFrame){1, 0, 0};
    assert(temporal_spatial_draw(&fr, &l, 0, TEMPORAL_SPATIAL_FORM) == TEMPORAL_SPATIAL_AS_DRAWN);
    assert(temporal_spatial_use(&fr, &l, 1, TEMPORAL_SPATIAL_AS_DRAWN, "a binding") == TEMPORAL_SPATIAL_EXPECTED);
    assert(temporal_spatial_draw(&fr, &l, 2, TEMPORAL_SPATIAL_TAKEN) == TEMPORAL_SPATIAL_AS_DRAWN);
    assert(temporal_spatial_use(&fr, &l, 1, TEMPORAL_SPATIAL_AS_DRAWN, "a binding") == TEMPORAL_SPATIAL_EXPECTED);
    assert(!l.off_next && !l.off);
    /* a listed reader's draw that also draws into the scene: not a binding, so a consumer */
    {
        TemporalSpatialLatch l2 = {0, 0, 0, NULL};
        assert(temporal_spatial_use(&fr, &l2, 1, -1, "a draw into it") == TEMPORAL_SPATIAL_CONSUMER && l2.off_next);
    }
    /* but a copy, a write or an unlisted binding in that frame is a consumer: off from the next frame */
    assert(temporal_spatial_use(&fr, &l, 0, -1, "a copy from it") == TEMPORAL_SPATIAL_CONSUMER);
    assert(l.off_next && !l.off && !strcmp(l.why, "a copy from it"));
    memset(&l, 0, sizeof l);
    fr = (TemporalSpatialFrame){1, 0, 0};
    temporal_spatial_draw(&fr, &l, 0, TEMPORAL_SPATIAL_FORM);
    assert(temporal_spatial_use(&fr, &l, 0, TEMPORAL_SPATIAL_AS_DRAWN, "a binding") == TEMPORAL_SPATIAL_CONSUMER);
    /* a listed reader's binding before the frame is decided is not expected */
    memset(&l, 0, sizeof l);
    fr = (TemporalSpatialFrame){1, 0, 0};
    assert(temporal_spatial_use(&fr, &l, 1, TEMPORAL_SPATIAL_AS_DRAWN, "a binding") == TEMPORAL_SPATIAL_CONSUMER);
    /* in a published frame: any use is a consumer, the refused listed reader's binding included */
    memset(&l, 0, sizeof l);
    fr = (TemporalSpatialFrame){1, 0, 0};
    temporal_spatial_draw(&fr, &l, 0, TEMPORAL_SPATIAL_TAKEN);
    assert(temporal_spatial_use(&fr, &l, 0, -1, "a draw into it") == TEMPORAL_SPATIAL_CONSUMER);
    assert(l.off_next && !l.off && !strcmp(l.why, "a draw into it"));
    memset(&l, 0, sizeof l);
    fr = (TemporalSpatialFrame){1, 0, 0};
    temporal_spatial_draw(&fr, &l, 0, TEMPORAL_SPATIAL_TAKEN);
    assert(temporal_spatial_draw(&fr, &l, 1, TEMPORAL_SPATIAL_VIEW) == TEMPORAL_SPATIAL_AS_DRAWN);
    assert(temporal_spatial_use(&fr, &l, 1, TEMPORAL_SPATIAL_AS_DRAWN, "a binding") == TEMPORAL_SPATIAL_CONSUMER);
    assert(l.off_next && !l.off);
    /* a binding given the larger texture that reaches the scene's Surf: a fault, off at once */
    memset(&l, 0, sizeof l);
    fr = (TemporalSpatialFrame){1, 0, 0};
    temporal_spatial_draw(&fr, &l, 0, TEMPORAL_SPATIAL_TAKEN);
    assert(temporal_spatial_use(&fr, &l, 1, TEMPORAL_SPATIAL_LARGER, "a binding") == TEMPORAL_SPATIAL_FAULT);
    assert(l.off && !temporal_spatial_frame(&l));
    /* (b) eight times in a row turns the mode off */
    memset(&l, 0, sizeof l);
    for (int i = 0; i < 8; i++) {
        assert(temporal_spatial_frame(&l));
        fr = (TemporalSpatialFrame){1, 0, 0};
        temporal_spatial_draw(&fr, &l, 0, TEMPORAL_SPATIAL_FRACTION);
        temporal_spatial_frame_end(&fr, &l);
    }
    assert(!temporal_spatial_frame(&l) && !strcmp(l.why, "eight eligible frames in a row were not published"));
}

/* the readers' probe: its programs by their whole hashes as the census prints them, and not by either half */
static void test_readers_program(void) {
    assert(temporal_readers_program(0x21fbfbecbf90f4a3ull) == 0);
    assert(temporal_readers_program(0xb46268adda62e218ull) == 1);
    assert(temporal_readers_program(0x8a160a27d90af72dull) == 2);
    assert(temporal_readers_program(0x63b3848c1a6e23e0ull) == 2);
    assert(temporal_readers_program(0x21fbfbecull) == -1 && temporal_readers_program(0xbf90f4a3ull) == -1);
    assert(temporal_readers_program(0x21fbfbec00000000ull) == -1);
    assert(temporal_readers_program(0x21fbfbecbf90f4a2ull) == -1 && temporal_readers_program(0) == -1);
}

/* D3's fixed size answer is today's: the four render scales, the game's sizes in use, both scale bits */
static void test_txq_today(void) {
    static const float scales[4] = {2.0f / 3.0f, 1.0f, 4.0f / 3.0f, 2.0f}; /* scale.h vk_render_scale */
    static const unsigned games[3][2] = {{1920, 1080}, {1280, 720}, {960, 540}};
    unsigned out[2];
    for (int s = 0; s < 4; s++)
        for (int g = 0; g < 3; g++) {
            unsigned image[2];
            for (int k = 0; k < 2; k++) { /* a scaled surface's image, as surface.c vk_sc makes it */
                volatile float v = (float)games[g][k] * scales[s];
                v = v + 0.5f;
                image[k] = (unsigned)v;
            }
            temporal_txq_today(image[0], image[1], scales[s], 1, out);
            assert(out[0] == games[g][0] && out[1] == games[g][1]);
            temporal_txq_today(image[0], image[1], scales[s], 0, out);
            assert(out[0] == image[0] && out[1] == image[1]);
        }
    /* 32 bit floats, as the program: 1 / (2/3) is 1.5 as a float, 1.49999995 as a double, and one texel answers 2 */
    temporal_txq_today(1, 1, 2.0f / 3.0f, 1, out);
    assert(out[0] == 2 && out[1] == 2);
    temporal_txq_today(3840, 2160, 2.0f, 0, out);
    assert(out[0] == 3840 && out[1] == 2160);
    temporal_txq_today(1281, 853, 2.0f / 3.0f, 1, out);
    assert(out[0] == 1922 && out[1] == 1280);
}

/* the line of the images handed to the library: when it is said */
static void test_upscaler_handed(void) {
    TemporalUpscalerHanded h = {0};
    unsigned long long image[4] = {0x10, 0x20, 0x30, 0x40};
    /* the first hand-over is said, also with handles of 0 and a context count of 0 */
    {
        TemporalUpscalerHanded zero = {0};
        const unsigned long long none[4] = {0, 0, 0, 0};
        assert(temporal_upscaler_handed(&zero, none, 0) == TEMPORAL_UPSCALER_HANDED_SAY);
        assert(temporal_upscaler_handed(&zero, none, 0) == TEMPORAL_UPSCALER_HANDED_QUIET);
    }
    assert(temporal_upscaler_handed(&h, image, 1) == TEMPORAL_UPSCALER_HANDED_SAY && h.said == 1);
    assert(temporal_upscaler_handed(&h, image, 1) == TEMPORAL_UPSCALER_HANDED_QUIET && h.said == 1);
    /* each of the four alone is a change, and so is a new context with the same four */
    for (int k = 0; k < 4; k++) {
        image[k]++;
        assert(temporal_upscaler_handed(&h, image, 1) == TEMPORAL_UPSCALER_HANDED_SAY);
        assert(h.image[k] == image[k]);
        assert(temporal_upscaler_handed(&h, image, 1) == TEMPORAL_UPSCALER_HANDED_QUIET);
    }
    assert(temporal_upscaler_handed(&h, image, 2) == TEMPORAL_UPSCALER_HANDED_SAY && h.said == 6 && h.context == 2);
    /* the sixteenth line is the last and says so, and nothing is said after it whatever changes */
    for (unsigned n = 7; n < TEMPORAL_UPSCALER_HANDED_MOST; n++) {
        image[1] += 2;
        assert(temporal_upscaler_handed(&h, image, 2) == TEMPORAL_UPSCALER_HANDED_SAY && h.said == n);
    }
    image[1] += 2;
    assert(temporal_upscaler_handed(&h, image, 2) == TEMPORAL_UPSCALER_HANDED_LAST);
    assert(h.said == TEMPORAL_UPSCALER_HANDED_MOST);
    image[1] += 2;
    assert(temporal_upscaler_handed(&h, image, 3) == TEMPORAL_UPSCALER_HANDED_QUIET);
    assert(h.said == TEMPORAL_UPSCALER_HANDED_MOST);
}

/* The game's validation lines (validation_lines.h). The lines go into one buffer, a line break after each. */
static char g_lines[1 << 20];
static size_t g_lines_at;
static unsigned g_lines_n;
static void lines_emit(void* user, const char* line) {
    const size_t n = strlen(line);
    (void)user;
    /* every line is bounded, and holds printable ASCII only */
    assert(n <= VALIDATION_LINE && g_lines_at + n + 2 < sizeof g_lines);
    for (size_t i = 0; i < n; i++) assert((unsigned char)line[i] >= 0x20 && (unsigned char)line[i] < 0x7f);
    memcpy(g_lines + g_lines_at, line, n);
    g_lines_at += n;
    g_lines[g_lines_at++] = '\n';
    g_lines[g_lines_at] = 0;
    g_lines_n++;
}
static void lines_reset(ValidationLines* v) {
    memset(v, 0, sizeof *v);
    g_lines_at = 0;
    g_lines_n = 0;
    g_lines[0] = 0;
}
static unsigned lines_count(const char* what) {
    unsigned n = 0;
    for (const char* p = g_lines; (p = strstr(p, what)) != NULL; p += strlen(what)) n++;
    return n;
}
static void test_validation_lines(void) {
    static ValidationLines v;
    char text[256];
    /* a callback before the status lines: it is handled from a state of zeros, and the status follows whole */
    lines_reset(&v);
    validation_lines_callback(&v, "VUID-early", 7, "a message while the instance is made", lines_emit, NULL);
    assert(v.callbacks == 1 && v.rules == 1 && v.printed == 1 && v.unmatched == 1);
    assert(lines_count("callback 1, the first of its rule and no variable's name found in it by the pattern, rule "
                       "VUID-early (7), 36 bytes of text: a message while the instance is made\n") == 1);
    assert(lines_count("1 callbacks so far, 1 of them printed whole and 0 printed with a cut, 1 rules and 0 variable "
                       "names listed.") == 1);
    assert(lines_count("Limits met: none\n") == 1);
    assert(lines_count("at 1 callbacks, rule 1 of 1: 1 callbacks of VUID-early (7)\n") == 1);
    validation_lines_status(VALIDATION_LIST_HAS, "", 0, 1, "", 0, "", 0, 1, "", 0, lines_emit, NULL);
    assert(lines_count("the layer VK_LAYER_KHRONOS_validation is enabled\n") == 1);
    assert(lines_count("its synchronisation checks were asked for and the instance is made with them\n") == 1);
    assert(lines_count("the message callback is ready\n") == 1);
    /* the status in its other states */
    lines_reset(&v);
    /* Only a list read whole without the layer says "not installed". The other ways of not finding it say that
       it is not known, with a result where a call was made and failed or the list stayed incomplete. */
    validation_lines_status(VALIDATION_LIST_LACKS, "VK_SUCCESS", 0, 0, "", 0, "", 0, -1, "", 0, lines_emit, NULL);
    assert(g_lines_n == 1 && !lines_count("result") && !lines_count("not known"));
    assert(lines_count("is NOT enabled: it is not installed: the loader's list of layers, read whole, does not hold "
                       "it. No message of the layer will be seen in this run\n") == 1);
    {
        static const char* const words[6] = {"", "", "stayed incomplete after three tries",
                                             "the call for the count of the loader's layers failed",
                                             "the call for the loader's list of layers failed", "no memory"};
        for (int state = VALIDATION_LIST_PART; state <= VALIDATION_LIST_NO_MEMORY; state++) {
            lines_reset(&v);
            validation_lines_status(state, "VK_RESULT_X", -7, 0, "", 0, "", 0, -1, "", 0, lines_emit, NULL);
            assert(g_lines_n == 1 && lines_count(words[state]) == 1 && !lines_count("it is not installed"));
            assert(lines_count("is NOT enabled: ") == 1 && lines_count("whether it is installed is not known") == 1);
            assert(lines_count("(the last call's result VK_RESULT_X, -7)") == (state != VALIDATION_LIST_NO_MEMORY));
            assert(lines_count("No message of the layer will be seen in this run\n") == 1);
        }
        lines_reset(&v);
        validation_lines_status(77, "x", 0, 0, "", 0, "", 0, -1, "", 0, lines_emit, NULL);
        assert(g_lines_n == 1 && lines_count("is NOT enabled, and why was not kept (77)") == 1);
    }
    lines_reset(&v);
    validation_lines_status(VALIDATION_LIST_HAS, "", 0, 0, "the call failed", 1, "VK_ERROR_X", -3, 0, "VK_ERROR_Y", -4,
                            lines_emit, NULL);
    assert(g_lines_n == 3 && lines_count("were NOT asked for (VK_EXT_validation_features): the call failed (the last "
                                         "call's result VK_ERROR_X, -3)\n") == 1);
    assert(lines_count("the message callback could NOT be made (VK_ERROR_Y, -4)") == 1);
    lines_reset(&v);
    validation_lines_status(VALIDATION_LIST_HAS, "", 0, 0, "no memory", 0, "unused", 9, -1, "", 0, lines_emit, NULL);
    assert(lines_count("(VK_EXT_validation_features): no memory\n") == 1 && !lines_count("result"));
    assert(lines_count("no message callback was asked for") == 1);

    /* the layer's limit of repeats: asked and not confirmed, or each way it was not asked */
    lines_reset(&v);
    validation_lines_limit(1, 1, VALIDATION_LIST_HAS, "VK_SUCCESS", 0, lines_emit, NULL);
    assert(g_lines_n == 1 && lines_count("was asked off (enable_message_limit = false). Asked, not confirmed: the "
                                         "setting cannot be read back\n") == 1 && !lines_count("NOT"));
    lines_reset(&v);
    validation_lines_limit(0, 0, VALIDATION_LIST_HAS, "VK_SUCCESS", 0, lines_emit, NULL);
    assert(g_lines_n == 1 && lines_count("was NOT asked off by the program: the headers this was built with do not "
                                         "have the layer's settings. The layer's default stops") == 1);
    {
        static const char* const words[6] = {"", "read whole, does not hold its settings", "stayed incomplete",
                                             "the call for the count of the layer's extensions failed",
                                             "the call for the list of the layer's extensions failed", "no memory"};
        for (int state = VALIDATION_LIST_LACKS; state <= VALIDATION_LIST_NO_MEMORY; state++) {
            lines_reset(&v);
            validation_lines_limit(0, 1, state, "VK_RESULT_Z", -9, lines_emit, NULL);
            assert(g_lines_n == 1 && lines_count("was NOT asked off by the program: ") == 1);
            assert(lines_count(words[state]) == 1);
            assert(lines_count("(the last call's result VK_RESULT_Z, -9)") ==
                   (state != VALIDATION_LIST_LACKS && state != VALIDATION_LIST_NO_MEMORY));
            /* ten is the layer's default and nothing more: the limit in effect is said to be unknown */
            assert(lines_count(". The layer's default stops a rule after ten repeats, and the limit in effect in "
                               "this run is not known\n") == 1);
            assert(!lines_count("asked off (") && !lines_count("The layer stops"));
        }
        lines_reset(&v);
        validation_lines_limit(0, 1, VALIDATION_LIST_HAS, "x", 0, lines_emit, NULL); /* found and not asked */
        assert(g_lines_n == 1 && lines_count("was NOT asked off by the program, and why was not kept (0). The "
                                             "layer's default stops") == 1);
    }

    /* What is printed whole: the first of a rule, the first naming a name not seen before however late, and
       the callbacks without a match up to 64. The count lines come at 1, 2, 4, 8 and so on. */
    lines_reset(&v);
    for (int i = 0; i < 300; i++)
        validation_lines_callback(&v, "VUID-format", 1,
                                  i == 250 ? "at variable \"rw_late\" now" : "at variable \"rw_a\"", lines_emit, NULL);
    assert(v.callbacks == 300 && v.printed == 2 && v.rules == 1 && v.names == 2 && !v.unmatched);
    assert(lines_count("callback 1, the first of its rule and the first naming a variable's name not seen before, "
                       "rule VUID-format (1)") == 1);
    assert(lines_count("callback 251, the first naming a variable's name not seen before, rule VUID-format (1), 25 "
                       "bytes of text: at variable \"rw_late\" now\n") == 1);
    assert(lines_count("callback ") == 2 + 9 * 0 && lines_count(" callbacks so far") == 9); /* 1 to 256 */
    assert(lines_count("\n256 callbacks so far, 2 of them printed whole and 0 printed with a cut, 1 rules and 2 "
                       "variable names listed.") == 1);
    assert(lines_count("at 256 callbacks, rule 1 of 1: 256 callbacks of VUID-format (1)\n") == 1);
    assert(!lines_count("LIMIT MET") && lines_count("Limits met: none") == 9);
    /* a new rule on known names is printed when it first occurs, and the same name with another number is
       another rule */
    validation_lines_callback(&v, "VUID-other", 1, "at variable \"rw_a\"", lines_emit, NULL);
    validation_lines_callback(&v, "VUID-format", 2, "at variable \"rw_a\"", lines_emit, NULL);
    validation_lines_callback(&v, "VUID-format", 2, "at variable \"rw_a\"", lines_emit, NULL);
    assert(v.rules == 3 && v.printed == 4 && v.rule[2].count == 2 && v.rule[0].count == 300);
    assert(lines_count("callback 301, the first of its rule, rule VUID-other (1)") == 1);
    assert(lines_count("callback 302, the first of its rule, rule VUID-format (2)") == 1);

    /* callbacks without a match: 64 printed, the 65th says once that they are not printed any more */
    lines_reset(&v);
    for (int i = 0; i < 70; i++) validation_lines_callback(&v, "VUID-plain", 3, "nothing named here", lines_emit, NULL);
    assert(v.unmatched == 70 && v.printed == 64 && v.loss[VALIDATION_UNPRINTED] == 6);
    assert(lines_count("no variable's name found in it by the pattern, rule VUID-plain (3)") == 64);
    assert(lines_count("LIMIT MET, first at callback 65: a callback without a match of the pattern that is not "
                       "printed any more.") == 1 && lines_count("LIMIT MET") == 1);
    /* the count line that follows repeats the state, the one before did not have it */
    assert(lines_count("\n64 callbacks so far, 64 of them printed whole and 0 printed with a cut, 1 rules and 0 "
                       "variable names listed. Written at 1, 2, 4, 8 and so on: the last such line of a log is "
                       "within a factor of two of the callbacks received. Limits met: none\n") == 1);
    for (int i = 70; i < 128; i++)
        validation_lines_callback(&v, "VUID-plain", 3, "nothing named here", lines_emit, NULL);
    assert(lines_count("\n128 callbacks so far, 64 of them printed whole and 0 printed with a cut") == 1);
    assert(lines_count("Limits met: a callback without a match of the pattern that is not printed any more (64 "
                       "times).\n") == 1 && lines_count("LIMIT MET") == 1);
    /* A callback without a match past the 64 that is the first of a new rule is printed, and so it is not
       lost: no line of a limit, no count of one. The next one of that rule is not printed and is. */
    lines_reset(&v);
    for (int i = 0; i < 64; i++) validation_lines_callback(&v, "VUID-plain", 3, "nothing named here", lines_emit, NULL);
    validation_lines_callback(&v, "VUID-new", 4, "nothing named here either", lines_emit, NULL);
    assert(v.unmatched == 65 && v.printed == 65 && v.unmatched_printed == 65 && !v.loss[VALIDATION_UNPRINTED]);
    assert(!lines_count("LIMIT MET") && lines_count("callback 65, the first of its rule, rule VUID-new (4)") == 1);
    validation_lines_callback(&v, "VUID-new", 4, "nothing named here either", lines_emit, NULL);
    assert(v.unmatched == 66 && v.printed == 65 && v.loss[VALIDATION_UNPRINTED] == 1);
    assert(lines_count("LIMIT MET, first at callback 66: a callback without a match of the pattern that is not "
                       "printed any more.") == 1);

    /* the rules table: no name, a name too long, and a 33rd rule. Each said once, at its first occurrence. */
    lines_reset(&v);
    for (int i = 0; i < 32; i++) {
        snprintf(text, sizeof text, "VUID-%d", i);
        validation_lines_callback(&v, text, i, "variable \"x\"", lines_emit, NULL);
    }
    assert(v.rules == 32 && !lines_count("LIMIT MET") && v.printed == 32);
    validation_lines_callback(&v, "VUID-33rd", 0, "variable \"x\"", lines_emit, NULL);
    validation_lines_callback(&v, "VUID-34th", 0, "variable \"x\"", lines_emit, NULL);
    assert(v.rules == 32 && v.unlisted == 2 && v.loss[VALIDATION_RULES_FULL] == 2 && v.printed == 32);
    assert(lines_count("LIMIT MET, first at callback 33: a rule that found no room in the table of rules.") == 1);
    validation_lines_callback(&v, NULL, 5, "variable \"x\"", lines_emit, NULL);
    validation_lines_callback(&v, NULL, 5, "variable \"y\"", lines_emit, NULL);
    assert(v.loss[VALIDATION_NO_NAME] == 2 && v.unlisted == 4);
    assert(lines_count("LIMIT MET, first at callback 35: a callback whose rule has no name") == 1);
    assert(lines_count("callback 36, the first naming a variable's name not seen before, rule without a name (5), "
                       "12 bytes of text: variable \"y\"\n") == 1);
    memset(text, 'r', 128);
    text[128] = 0;
    validation_lines_callback(&v, text, 6, "variable \"z\"", lines_emit, NULL);
    assert(v.loss[VALIDATION_RULE_LONG] == 1 && v.unlisted == 5 && v.rules == 32);
    assert(lines_count("LIMIT MET, first at callback 37: a rule's name too long to list.") == 1);
    assert(lines_count("rrrr [the name's first 127 bytes] (6), 12 bytes of text") == 1);
    text[127] = 0; /* 127 characters are a name that is listed: but the table is full */
    validation_lines_callback(&v, text, 6, "variable \"z\"", lines_emit, NULL);
    assert(v.loss[VALIDATION_RULE_LONG] == 1 && v.loss[VALIDATION_RULES_FULL] == 3);
    assert(lines_count("LIMIT MET") == 3);
    {
        uint64_t sum = v.unlisted;
        for (unsigned k = 0; k < v.rules; k++) sum += v.rule[k].count;
        assert(sum == v.callbacks && v.callbacks == 38);
    }
    lines_reset(&v);
    validation_lines_callback(&v, text, 6, "variable \"z\"", lines_emit, NULL);
    assert(v.rules == 1 && strlen(v.rule[0].name) == 127 && !lines_count("LIMIT MET"));

    /* the names table: 64 names, a 65th, a name of 64 characters, a quote that does not close */
    lines_reset(&v);
    for (int i = 0; i < 64; i++) {
        snprintf(text, sizeof text, "a variable \"name%d\" and variable \"name0\"", i);
        validation_lines_callback(&v, "VUID-n", 1, text, lines_emit, NULL);
    }
    assert(v.names == 64 && v.printed == 64 && !lines_count("LIMIT MET"));
    validation_lines_callback(&v, "VUID-n", 1, "variable \"one more\"", lines_emit, NULL);
    assert(v.names == 64 && v.printed == 64 && v.loss[VALIDATION_NAMES_FULL] == 1);
    assert(lines_count("LIMIT MET, first at callback 65: a variable's name that found no room in the table of "
                       "names.") == 1);
    lines_reset(&v);
    memset(text, 0, sizeof text);
    memcpy(text, "variable \"", 10);
    memset(text + 10, 'v', 64);
    text[74] = '"';
    validation_lines_callback(&v, "VUID-n", 1, text, lines_emit, NULL);
    assert(!v.names && v.loss[VALIDATION_NAME_LONG] == 1 && !v.unmatched && v.printed == 1);
    assert(lines_count("LIMIT MET, first at callback 1: a variable's name too long to list.") == 1);
    text[73] = '"'; /* 63 characters are kept */
    text[74] = 0;
    validation_lines_callback(&v, "VUID-n", 1, text, lines_emit, NULL);
    assert(v.names == 1 && strlen(v.name[0]) == 63 && v.printed == 2);
    validation_lines_callback(&v, "VUID-n", 1, "variable \"never closed", lines_emit, NULL);
    assert(v.loss[VALIDATION_UNCLOSED] == 1 && v.names == 1 && !v.unmatched);
    assert(lines_count("LIMIT MET, first at callback 3: a quoted variable's name that does not close") == 1);

    /* A long text with line breaks, a backslash and bytes above ASCII: one line, bounded, the marks written
       out, and the cut said with what is in the line. */
    lines_reset(&v);
    {
        static char big[5001];
        for (int i = 0; i < 5000; i++)
            big[i] = i % 50 == 49 ? '\n' : i % 50 == 48 ? '\\' : i % 50 == 47 ? (char)0xc3 : 'q';
        big[5000] = 0;
        validation_lines_callback(&v, "VUID-big", 1, big, lines_emit, NULL);
        /* printed, with a cut: it is not among the ones printed whole, and the count line names both */
        assert(v.loss[VALIDATION_CUT] == 1 && v.printed == 0 && v.printed_cut == 1);
        assert(lines_count("\n1 callbacks so far, 0 of them printed whole and 1 printed with a cut, 1 rules") == 1);
        assert(lines_count("LIMIT MET, first at callback 1: a printed line that was cut.") == 1);
        assert(lines_count("\\n") >= 20 && lines_count("\\\\") >= 20 && lines_count("\\xc3") >= 20);
        const char* cut = strstr(g_lines, " [CUT: the first ");
        unsigned put = 0, of = 0;
        assert(cut && sscanf(cut, " [CUT: the first %u of %u bytes of the text are in this line]", &put, &of) == 2);
        assert(of == 5000 && put > 1000 && put < 1800);
        /* what is in the line reads back to exactly the first put bytes */
        const char* body = strstr(g_lines, "5000 bytes of text: ");
        assert(body);
        body += strlen("5000 bytes of text: ");
        unsigned back = 0;
        for (const char* p = body; p < cut; back++) {
            unsigned char c = (unsigned char)*p++;
            if (c == '\\') {
                c = (unsigned char)*p++;
                if (c == 'n') c = '\n';
                else if (c == 'x') {
                    unsigned value = 0;
                    assert(sscanf(p, "%2x", &value) == 1);
                    c = (unsigned char)value;
                    p += 2;
                } else
                    assert(c == '\\');
            }
            assert(c == (unsigned char)big[back]);
        }
        assert(back == put);
        /* a second cut line is not said again, and is counted */
        validation_lines_callback(&v, "VUID-big2", 1, big, lines_emit, NULL);
        assert(v.loss[VALIDATION_CUT] == 2 && lines_count("LIMIT MET") == 1 && lines_count(" [CUT: ") == 2);
        assert(v.printed == 0 && v.printed_cut == 2);
        validation_lines_callback(&v, "VUID-small", 1, "short", lines_emit, NULL);
        assert(v.printed == 1 && v.printed_cut == 2);
    }
    /* a text longer than what is looked through: said, and its line says more than */
    lines_reset(&v);
    {
        static char huge[VALIDATION_SCAN + 40];
        memset(huge, 'h', sizeof huge - 1);
        memcpy(huge + VALIDATION_SCAN + 2, "variable \"past the end\"", 23);
        validation_lines_callback(&v, "VUID-huge", 1, huge, lines_emit, NULL);
        assert(v.loss[VALIDATION_UNSCANNED] == 1 && !v.names && v.unmatched == 1);
        assert(lines_count("LIMIT MET, first at callback 1: a text that was not looked through to its end.") == 1);
        assert(lines_count("more than 1048576 bytes of text: hhh") == 1);
        assert(lines_count("of more than 1048576 bytes of the text are in this line]") == 1);
    }
    /* no text at all */
    lines_reset(&v);
    validation_lines_callback(&v, "VUID-mute", 1, NULL, lines_emit, NULL);
    assert(v.unmatched == 1 && lines_count("rule VUID-mute (1), 0 bytes of text: \n") == 1);
    /* a rule's name with a line break is one line too */
    validation_lines_callback(&v, "VUID\nbroken", 1, "variable \"a\\b\"", lines_emit, NULL);
    assert(lines_count("rule VUID\\nbroken (1), 14 bytes of text: variable \"a\\\\b\"\n") == 1);
    assert(lines_count("callbacks of VUID\\nbroken (1)\n") == 1);

    /* a counter at its largest value stays there, and says so once */
    lines_reset(&v);
    v.callbacks = UINT64_MAX - 1;
    v.next = 0;
    validation_lines_callback(&v, "VUID-last", 1, "variable \"a\"", lines_emit, NULL);
    assert(v.callbacks == UINT64_MAX && v.loss[VALIDATION_SATURATED] == 1);
    assert(lines_count("LIMIT MET, first at callback 18446744073709551615: a counter that stopped at its largest "
                       "value.") == 1);
    validation_lines_callback(&v, "VUID-last", 1, "variable \"a\"", lines_emit, NULL);
    assert(v.callbacks == UINT64_MAX && v.loss[VALIDATION_SATURATED] == 2 && lines_count("LIMIT MET") == 1);
    {
        uint64_t c = UINT64_MAX - 2;
        assert(!validation_lines_count(&c) && validation_lines_count(&c) && validation_lines_count(&c));
        assert(c == UINT64_MAX);
    }
    /* the count lines' doubling ends without wrapping to a count that is never reached again */
    lines_reset(&v);
    v.callbacks = (UINT64_MAX / 2 + 1) - 1;
    v.next = UINT64_MAX / 2 + 1;
    validation_lines_callback(&v, "VUID-half", 1, "variable \"a\"", lines_emit, NULL);
    assert(lines_count("9223372036854775808 callbacks so far") == 1 && v.next == 0);
    /* and no count line follows it: the next would be at a count a 64 bit counter does not reach */
    validation_lines_callback(&v, "VUID-half", 1, "variable \"a\"", lines_emit, NULL);
    assert(lines_count(" callbacks so far") == 1);
    /* every loss has words of its own */
    for (int a = 0; a < VALIDATION_LOSSES; a++)
        for (int b = a + 1; b < VALIDATION_LOSSES; b++)
            assert(strcmp(validation_lines_loss(a), validation_lines_loss(b)));
    /* through the lock: the inherited counter goes up with the callback */
    lines_reset(&v);
    {
        int lock = 0;
        uint32_t inherited = 41;
        validation_lines_said(&lock, &v, &inherited, "VUID-l", 1, "variable \"a\"", lines_emit, NULL);
        assert(inherited == 42 && v.callbacks == 1 && lock == 0 && !lines_count("LIMIT MET"));
        /* the inherited counter has 32 bits: it stops at its largest value, said once, and does not wrap */
        inherited = UINT32_MAX - 1;
        validation_lines_said(&lock, &v, &inherited, "VUID-l", 1, "variable \"a\"", lines_emit, NULL);
        assert(inherited == UINT32_MAX && v.loss[VALIDATION_SATURATED] == 1);
        assert(lines_count("LIMIT MET, first at callback 2: a counter that stopped at its largest value.") == 1);
        validation_lines_said(&lock, &v, &inherited, "VUID-l", 1, "variable \"a\"", lines_emit, NULL);
        assert(inherited == UINT32_MAX && v.loss[VALIDATION_SATURATED] == 2 && lines_count("LIMIT MET") == 1);
        assert(v.callbacks == 3 && lock == 0);
        /* without an inherited counter there is nothing to stop */
        validation_lines_said(&lock, &v, NULL, "VUID-l", 1, "variable \"a\"", lines_emit, NULL);
        assert(v.callbacks == 4 && v.loss[VALIDATION_SATURATED] == 2);
    }
}

/* the private output's size against the limits the physical device gave for its format and uses */
static void test_upscaler_output_fits(void) {
    assert(temporal_upscaler_output_fits(3840, 2160, 16384, 16384));
    assert(temporal_upscaler_output_fits(16384, 16384, 16384, 16384)); /* the limit itself fits */
    assert(!temporal_upscaler_output_fits(16385, 16384, 16384, 16384));
    assert(!temporal_upscaler_output_fits(16384, 16385, 16384, 16384));
    assert(!temporal_upscaler_output_fits(4096, 2160, 4095, 16384)); /* the width alone */
    assert(!temporal_upscaler_output_fits(3840, 2160, 16384, 2159)); /* the height alone */
    assert(temporal_upscaler_output_fits(1, 1, 1, 1));
    assert(!temporal_upscaler_output_fits(0, 1, 16384, 16384) && !temporal_upscaler_output_fits(1, 0, 16384, 16384));
    assert(!temporal_upscaler_output_fits(1, 1, 0, 16384) && !temporal_upscaler_output_fits(1, 1, 16384, 0));
    assert(temporal_upscaler_output_fits(UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX));
    assert(!temporal_upscaler_output_fits(UINT32_MAX, 1, UINT32_MAX - 1, UINT32_MAX));
}

/* What the host makes of an adapter's needs of the game's device (upscaler_needs.h). */
static void test_upscaler_needs(void) {
    SnxNeedsBits asked = {9, 9, 9};
    /* the adapter's chain: nothing, the one accepted structure, and what is refused */
    assert(snx_needs_chain(NULL, &asked) == SNX_NEEDS_OK && !asked.have && !asked.quads && !asked.linear);
    SnxNeedsDerivatives linear = {SNX_NEEDS_DERIVATIVES, NULL, 0, 1};
    assert(snx_needs_chain(&linear, &asked) == SNX_NEEDS_OK && asked.have && !asked.quads && asked.linear);
    SnxNeedsDerivatives both = {SNX_NEEDS_DERIVATIVES, NULL, 7, 1}; /* any value that is not 0 is a bit asked */
    assert(snx_needs_chain(&both, &asked) == SNX_NEEDS_OK && asked.quads == 1 && asked.linear == 1);
    /* the chain is read only: the adapter's structure is as it was */
    const SnxNeedsDerivatives before = linear;
    snx_needs_chain(&linear, &asked);
    assert(!memcmp(&before, &linear, sizeof linear));
    /* a core feature structure (the 1.2 features), the promoted 16 bit float features, and one that is no
       feature structure at all: each a type the host does not accept, first in the chain or behind */
    static const int32_t refused[4] = {51, 1000082000, 1000059000, 0};
    for (int k = 0; k < 4; k++) {
        SnxNeedsNode other = {refused[k], NULL};
        assert(snx_needs_chain(&other, &asked) == SNX_NEEDS_TYPE);
        SnxNeedsDerivatives first = {SNX_NEEDS_DERIVATIVES, &other, 0, 1};
        assert(snx_needs_chain(&first, &asked) == SNX_NEEDS_TYPE);
        SnxNeedsNode front = {refused[k], &linear};
        assert(snx_needs_chain(&front, &asked) == SNX_NEEDS_TYPE);
    }
    /* the accepted type twice, and a structure that points at itself */
    SnxNeedsDerivatives second = {SNX_NEEDS_DERIVATIVES, NULL, 1, 0}, first = {SNX_NEEDS_DERIVATIVES, &second, 0, 1};
    assert(snx_needs_chain(&first, &asked) == SNX_NEEDS_REPEATED);
    SnxNeedsDerivatives loop = {SNX_NEEDS_DERIVATIVES, NULL, 0, 1};
    loop.next = &loop;
    assert(snx_needs_chain(&loop, &asked) == SNX_NEEDS_REPEATED);
    /* A null ends a chain where it stands: what is behind it is not part of the chain. And a chain of nine
       never gets past its second structure. The bound of eight is the walk's guard against a chain that does
       not end, not a first refusal that can be reached with one accepted type. */
    SnxNeedsDerivatives nine[9];
    for (int k = 0; k < 9; k++)
        nine[k] = (SnxNeedsDerivatives){SNX_NEEDS_DERIVATIVES, k < 8 ? &nine[k + 1] : NULL, 0, 1};
    assert(snx_needs_chain(nine, &asked) == SNX_NEEDS_REPEATED);
    nine[0].next = NULL;
    assert(snx_needs_chain(nine, &asked) == SNX_NEEDS_OK && asked.linear);
    /* each bit asked for against what the physical device has */
    {
        const SnxNeedsBits none = {0, 0, 0}, want_linear = {1, 0, 1}, want_both = {1, 1, 1}, want_quads = {1, 1, 0};
        const SnxNeedsBits has_linear = {1, 0, 1}, has_both = {1, 1, 1}, has_none = {1, 0, 0}, absent = {0, 1, 1};
        assert(snx_needs_bits(&none, &absent) == SNX_NEEDS_OK && snx_needs_bits(&none, &has_none) == SNX_NEEDS_OK);
        assert(snx_needs_bits(&want_linear, &has_linear) == SNX_NEEDS_OK);
        assert(snx_needs_bits(&want_linear, &has_both) == SNX_NEEDS_OK);
        assert(snx_needs_bits(&want_both, &has_both) == SNX_NEEDS_OK);
        assert(snx_needs_bits(&want_both, &has_linear) == SNX_NEEDS_BIT);
        assert(snx_needs_bits(&want_quads, &has_linear) == SNX_NEEDS_BIT);
        assert(snx_needs_bits(&want_linear, &has_none) == SNX_NEEDS_BIT);
        assert(snx_needs_bits(&want_linear, &absent) == SNX_NEEDS_BIT); /* the structure itself was not answered */
    }
    /* the names against the physical device's list, which must have been read whole */
    {
        const char* const device[3] = {"VK_KHR_swapchain", "VK_KHR_compute_shader_derivatives", "VK_EXT_other"};
        const char* const one[1] = {"VK_KHR_compute_shader_derivatives"};
        const char* const two[2] = {"VK_EXT_other", "VK_KHR_missing"};
        const char* const near[1] = {"VK_KHR_compute_shader_derivative"}; /* a prefix is another name */
        const char* const hole[2] = {"VK_EXT_other", NULL};
        assert(snx_needs_names(one, 1, device, 3, 1) == SNX_NEEDS_OK);
        assert(snx_needs_names(NULL, 0, device, 3, 1) == SNX_NEEDS_OK);
        assert(snx_needs_names(two, 2, device, 3, 1) == SNX_NEEDS_NAME);
        assert(snx_needs_names(near, 1, device, 3, 1) == SNX_NEEDS_NAME);
        assert(snx_needs_names(hole, 2, device, 3, 1) == SNX_NEEDS_NAME);
        assert(snx_needs_names(one, 1, device, 0, 1) == SNX_NEEDS_NAME);
        /* a list read in part or not at all refuses, also when it holds the name and when nothing is asked */
        assert(snx_needs_names(one, 1, device, 3, 0) == SNX_NEEDS_LIST);
        assert(snx_needs_names(NULL, 0, device, 3, 0) == SNX_NEEDS_LIST);
    }
    /* The device's list and chain: the additions are a pair of their own, and the fallback is the game's pair
       as it was. */
    {
        const int game_node = 1, host_node = 2;
        const SnxNeedsPlan with = snx_needs_plan(7, &game_node, 1, 8, &host_node);
        assert(with.count == 8 && with.head == &host_node && with.augmented == 1);
        const SnxNeedsPlan refused_plan = snx_needs_plan(7, &game_node, 0, 8, &host_node);
        assert(refused_plan.count == 7 && refused_plan.head == &game_node && refused_plan.augmented == 0);
        /* after a device that could not be made with the additions: the game's pair again */
        const SnxNeedsPlan again = snx_needs_plan(7, &game_node, 0, with.count, with.head);
        assert(again.count == 7 && again.head == &game_node && !again.augmented);
    }
    /* the record, and the start's comparison with it */
    {
        const char* const one[1] = {"VK_KHR_compute_shader_derivatives"};
        const char* const two[2] = {"VK_KHR_compute_shader_derivatives", "VK_EXT_other"};
        const SnxNeedsBits on_linear = {1, 0, 1}, on_both = {1, 1, 1}, on_none = {0, 0, 0};
        SnxNeedsRecord r = {0};
        /* no record: an adapter with needs is not met, whatever it asks */
        assert(snx_needs_met(&r, "fsr", one, 1, &linear, 0) == SNX_NEEDS_ADAPTER);
        assert(snx_needs_record(&r, "fsr", one, 1, &on_linear, 0) == SNX_NEEDS_OK && r.made && r.names == 1);
        assert(snx_needs_met(&r, "fsr", one, 1, &linear, 0) == SNX_NEEDS_OK);
        assert(snx_needs_met(&r, "fsr", NULL, 0, NULL, 0) == SNX_NEEDS_OK); /* asking less is met */
        assert(snx_needs_met(&r, "xess", one, 1, &linear, 0) == SNX_NEEDS_ADAPTER);
        assert(snx_needs_met(&r, NULL, one, 1, &linear, 0) == SNX_NEEDS_ADAPTER);
        assert(snx_needs_met(&r, "fsr", two, 2, &linear, 0) == SNX_NEEDS_NOT_RECORDED); /* one more extension */
        assert(snx_needs_met(&r, "fsr", one, 1, &both, 0) == SNX_NEEDS_NOT_RECORDED);   /* a bit that is not on */
        assert(snx_needs_met(&r, "fsr", one, 1, &first, 0) == SNX_NEEDS_REPEATED);      /* a chain the host refuses */
        SnxNeedsNode core = {51, NULL};
        assert(snx_needs_met(&r, "fsr", one, 1, &core, 0) == SNX_NEEDS_TYPE);
        /* the record is by value: the strings and the bits it was made from may go */
        char name[40] = "VK_KHR_compute_shader_derivatives";
        const char* gone[1] = {name};
        SnxNeedsBits bits = on_both;
        assert(snx_needs_record(&r, "fsr", gone, 1, &bits, 0) == SNX_NEEDS_OK);
        memset(name, 'x', sizeof name - 1);
        bits = on_none;
        assert(snx_needs_met(&r, "fsr", one, 1, &both, 0) == SNX_NEEDS_OK);
        /* a record with the extension and without the structure: a bit asked for is not recorded */
        assert(snx_needs_record(&r, "fsr", one, 1, &on_none, 0) == SNX_NEEDS_OK);
        assert(snx_needs_met(&r, "fsr", one, 1, &linear, 0) == SNX_NEEDS_NOT_RECORDED);
        assert(snx_needs_met(&r, "fsr", one, 1, NULL, 0) == SNX_NEEDS_OK);
        /* each clause of the bits' comparison alone: the quad groups on and the linear ones asked, and a record
           whose structure is not there whatever its bits say */
        const SnxNeedsBits on_quads = {1, 1, 0}, not_there = {0, 1, 1};
        assert(snx_needs_record(&r, "fsr", one, 1, &on_quads, 0) == SNX_NEEDS_OK);
        assert(snx_needs_met(&r, "fsr", one, 1, &linear, 0) == SNX_NEEDS_NOT_RECORDED);
        assert(snx_needs_record(&r, "fsr", one, 1, &not_there, 0) == SNX_NEEDS_OK);
        assert(snx_needs_met(&r, "fsr", one, 1, &linear, 0) == SNX_NEEDS_NOT_RECORDED);
        /* what cannot be kept whole makes no record */
        const char* many[9] = {"a", "b", "c", "d", "e", "f", "g", "h", "i"};
        assert(snx_needs_record(&r, "fsr", many, 9, &on_linear, 0) == SNX_NEEDS_ROOM && !r.made);
        char long_name[80];
        memset(long_name, 'n', 64);
        long_name[64] = 0;
        const char* too_long[1] = {long_name};
        assert(snx_needs_record(&r, "fsr", too_long, 1, &on_linear, 0) == SNX_NEEDS_ROOM && !r.made);
        long_name[63] = 0;
        assert(snx_needs_record(&r, "fsr", too_long, 1, &on_linear, 0) == SNX_NEEDS_OK && strlen(r.name[0]) == 63);
        assert(snx_needs_record(&r, "an adapter's name of thirty-two characters", one, 1, &on_linear, 0) ==
               SNX_NEEDS_ROOM);
        assert(!r.made && snx_needs_met(&r, "fsr", one, 1, &linear, 0) == SNX_NEEDS_ADAPTER);
    }
    assert(snx_needs_none(0, NULL, 0) && !snx_needs_none(1, NULL, 0) && !snx_needs_none(0, &linear, 0));
    /* Core feature bits: a plain field, one bit known. Unknown bits refuse whatever the device has, the known one
       needs the physical device to have it, and nothing asked is met by any device. */
    assert(snx_needs_core(0, 0) == SNX_NEEDS_OK && snx_needs_core(0, 1) == SNX_NEEDS_OK);
    assert(snx_needs_core(SNX_NEEDS_CORE_SEPARATE_DEPTH_STENCIL, 1) == SNX_NEEDS_OK);
    assert(snx_needs_core(SNX_NEEDS_CORE_SEPARATE_DEPTH_STENCIL, 0) == SNX_NEEDS_CORE_BIT);
    for (int bit = 1; bit < 32; bit++) {
        assert(snx_needs_core(1u << bit, 1) == SNX_NEEDS_CORE_UNKNOWN);
        assert(snx_needs_core(1u << bit | SNX_NEEDS_CORE_SEPARATE_DEPTH_STENCIL, 1) == SNX_NEEDS_CORE_UNKNOWN);
        assert(snx_needs_core(1u << bit | SNX_NEEDS_CORE_SEPARATE_DEPTH_STENCIL, 0) == SNX_NEEDS_CORE_UNKNOWN);
    }
    /* the bit in a maker's own 1.2 structure: on for the creation with the additions when asked, the maker's
       own otherwise, so the fallback has what the maker had */
    assert(snx_needs_core_bit(0, 1, SNX_NEEDS_CORE_SEPARATE_DEPTH_STENCIL, SNX_NEEDS_CORE_SEPARATE_DEPTH_STENCIL) == 1);
    assert(snx_needs_core_bit(0, 0, SNX_NEEDS_CORE_SEPARATE_DEPTH_STENCIL, SNX_NEEDS_CORE_SEPARATE_DEPTH_STENCIL) == 0);
    assert(snx_needs_core_bit(0, 1, 0, SNX_NEEDS_CORE_SEPARATE_DEPTH_STENCIL) == 0);
    assert(snx_needs_core_bit(1, 0, 0, SNX_NEEDS_CORE_SEPARATE_DEPTH_STENCIL) == 1);
    assert(snx_needs_core_bit(1, 1, 0, SNX_NEEDS_CORE_SEPARATE_DEPTH_STENCIL) == 1);
    assert(snx_needs_core_bit(1, 1, SNX_NEEDS_CORE_SEPARATE_DEPTH_STENCIL, SNX_NEEDS_CORE_SEPARATE_DEPTH_STENCIL) == 1);
    /* an answer with the core bit alone is a need: no shortcut for an adapter that names nothing else */
    assert(!snx_needs_none(0, NULL, SNX_NEEDS_CORE_SEPARATE_DEPTH_STENCIL));
    /* the record holds the bit by value and the start asks for it */
    {
        const char* const one[1] = {"VK_KHR_compute_shader_derivatives"};
        const SnxNeedsBits on_linear = {1, 0, 1}, nothing = {0, 0, 0};
        const uint32_t separate = SNX_NEEDS_CORE_SEPARATE_DEPTH_STENCIL;
        SnxNeedsRecord r = {0};
        assert(snx_needs_record(&r, "fsr", one, 1, &on_linear, separate) == SNX_NEEDS_OK && r.core == separate);
        assert(snx_needs_met(&r, "fsr", one, 1, &linear, separate) == SNX_NEEDS_OK);
        assert(snx_needs_met(&r, "fsr", one, 1, &linear, 0) == SNX_NEEDS_OK); /* asking less is met */
        assert(snx_needs_met(&r, "fsr", one, 1, &linear, separate | 2u) == SNX_NEEDS_CORE_UNKNOWN);
        assert(snx_needs_met(&r, "fsr", one, 1, &linear, 0x80000000u) == SNX_NEEDS_CORE_UNKNOWN);
        /* a device made without the bit does not meet an adapter that asks for it */
        assert(snx_needs_record(&r, "fsr", one, 1, &on_linear, 0) == SNX_NEEDS_OK && !r.core);
        assert(snx_needs_met(&r, "fsr", one, 1, &linear, separate) == SNX_NEEDS_NOT_RECORDED);
        assert(snx_needs_met(&r, "fsr", one, 1, &linear, 0) == SNX_NEEDS_OK);
        /* a record of the core bit alone: no name, no structure */
        assert(snx_needs_record(&r, "fsr", NULL, 0, &nothing, separate) == SNX_NEEDS_OK && r.made && !r.names);
        assert(snx_needs_met(&r, "fsr", NULL, 0, NULL, separate) == SNX_NEEDS_OK);
        assert(snx_needs_met(&r, "fsr", one, 1, NULL, separate) == SNX_NEEDS_NOT_RECORDED);
        assert(snx_needs_met(&r, "other", NULL, 0, NULL, separate) == SNX_NEEDS_ADAPTER);
        /* what cannot be kept makes no record, and then the bit is not met either */
        const char* many[9] = {"a", "b", "c", "d", "e", "f", "g", "h", "i"};
        assert(snx_needs_record(&r, "fsr", many, 9, &nothing, separate) == SNX_NEEDS_ROOM && !r.made && !r.core);
        assert(snx_needs_met(&r, "fsr", NULL, 0, NULL, separate) == SNX_NEEDS_ADAPTER);
    }
    /* a bit asked for needs the KHR extension among the names. The NV one alone, or beside others, is refused */
    {
        const char* const khr[2] = {"VK_EXT_other", "VK_KHR_compute_shader_derivatives"};
        const char* const nv[1] = {"VK_NV_compute_shader_derivatives"};
        const char* const other[1] = {"VK_EXT_other"};
        const SnxNeedsBits want = {1, 0, 1}, nothing = {0, 0, 0};
        const char* const nv_and[2] = {"VK_NV_compute_shader_derivatives", "VK_EXT_other"};
        const char* const with_khr[2] = {"VK_NV_compute_shader_derivatives", "VK_KHR_compute_shader_derivatives"};
        assert(snx_needs_owner(khr, 2, &want) == SNX_NEEDS_OK);
        assert(snx_needs_owner(with_khr, 2, &want) == SNX_NEEDS_OK);
        assert(snx_needs_owner(nv, 1, &want) == SNX_NEEDS_OWNER);
        assert(snx_needs_owner(nv_and, 2, &want) == SNX_NEEDS_OWNER);
        assert(snx_needs_owner(nv, 1, &nothing) == SNX_NEEDS_OK); /* named without a bit: only a name to check */
        assert(snx_needs_owner(other, 1, &want) == SNX_NEEDS_OWNER);
        assert(snx_needs_owner(NULL, 0, &want) == SNX_NEEDS_OWNER);
        assert(snx_needs_owner(other, 1, &nothing) == SNX_NEEDS_OK);
        assert(snx_needs_owner(NULL, 0, &nothing) == SNX_NEEDS_OK);
    }
    /* every answer has words of its own */
    for (int a = SNX_NEEDS_OK; a <= SNX_NEEDS_NOT_RECORDED; a++)
        for (int b = a + 1; b <= SNX_NEEDS_NOT_RECORDED; b++) assert(strcmp(snx_needs_text(a), snx_needs_text(b)));
    /* The chain is read as bytes at the stand-ins' offsets: a structure of another type of the same layout, as
       the adapter's Vulkan structure is, reads the same. */
    {
        struct Other {
            int32_t kind;
            const void* after;
            uint32_t first, second;
        } other = {SNX_NEEDS_DERIVATIVES, NULL, 0, 5};
        assert(snx_needs_chain(&other, &asked) == SNX_NEEDS_OK && asked.have && !asked.quads && asked.linear == 1);
        unsigned char raw[sizeof(SnxNeedsDerivatives) + 1] = {0}; /* and from an address that is not aligned */
        memcpy(raw + 1, &other, sizeof other);
        assert(snx_needs_chain(raw + 1, &asked) == SNX_NEEDS_OK && asked.linear == 1 && !asked.quads);
    }
    /* the stand-in structures lay their members out as Vulkan's do: a type, a pointer, two 32 bit booleans */
    assert(offsetof(SnxNeedsDerivatives, next) == offsetof(SnxNeedsNode, next));
    assert(offsetof(SnxNeedsDerivatives, quads) == 2 * sizeof(void*));
    assert(sizeof(SnxNeedsDerivatives) == 3 * sizeof(void*));
}

/* The pure rules of the upscaler's wiring in the game. */
static void test_upscaler_wiring(void) {
    /* The write-back: a value as it is, 0 for a negative one, for minus zero and for no number, and no more
       than the largest finite value of the channel, which infinity is brought down to. */
    {
        const float r = temporal_ufloat_largest(6), b = temporal_ufloat_largest(5);
        assert(r == 65024.0f && b == 64512.0f); /* (1 + 63/64) x 2^15 and (1 + 31/32) x 2^15 */
        assert(temporal_upscaled(0.25f, r) == 0.25f && temporal_upscaled(7.5f, r) == 7.5f);
        assert(temporal_upscaled(0.0f, r) == 0.0f && !signbit(temporal_upscaled(-0.0f, r)));
        assert(temporal_upscaled(-0.25f, r) == 0.0f && temporal_upscaled(-INFINITY, r) == 0.0f);
        assert(temporal_upscaled(NAN, r) == 0.0f && temporal_upscaled(1e-40f, r) == 1e-40f);
        assert(temporal_upscaled(INFINITY, r) == 65024.0f && temporal_upscaled(INFINITY, b) == 64512.0f);
        assert(temporal_upscaled(3.0e38f, r) == 65024.0f && temporal_upscaled(65024.0f, r) == 65024.0f);
        assert(temporal_upscaled(65023.0f, r) == 65023.0f && temporal_upscaled(65025.0f, r) == 65024.0f);
        /* the limits are per channel: a value the red and green channels hold is over the blue one's */
        assert(temporal_upscaled(64800.0f, r) == 64800.0f && temporal_upscaled(64800.0f, b) == 64512.0f);
        /* and they are the format's own: the self-test's packing gives them the all-ones finite pattern */
        assert(selftest_ufloat((double)r, 6) == ((30u << 6) | 63u));
        assert(selftest_ufloat((double)b, 5) == ((30u << 5) | 31u));
        assert(selftest_ufloat_value((30u << 6) | 63u, 6) == (double)r);
        assert(selftest_ufloat_value((30u << 5) | 31u, 5) == (double)b);
    }
    /* the start: only with the anti-aliasing on, and not with rejected vectors */
    assert(temporal_upscaler_start(1, 0) == TEMPORAL_UPSCALER_START_OK);
    assert(temporal_upscaler_start(0, 0) == TEMPORAL_UPSCALER_START_NOT_TAA);
    assert(temporal_upscaler_start(0, 1) == TEMPORAL_UPSCALER_START_NOT_TAA);
    assert(temporal_upscaler_start(1, 1) == TEMPORAL_UPSCALER_START_REJECT);
    /* The generation: 1 for the first making, the same while nothing changes, another for a change of any one
       member, and another again when the first making comes back. */
    {
        const TemporalUpscalerMaking first = {10, 20, 30, 40, 1, 1, 0xf};
        TemporalUpscalerMaking last = {0}, now = first;
        unsigned long long generation = 0, seen[9];
        assert(temporal_upscaler_generation(&last, &generation, &now) == 1);
        assert(temporal_upscaler_generation(&last, &generation, &now) == 1);
        unsigned* members[7] = {&now.colour_format, &now.depth_format, &now.motion_format, &now.output_format,
                                &now.colour_layers, &now.depth_layers, &now.flags};
        for (int k = 0; k < 7; k++) {
            ++*members[k];
            seen[k] = temporal_upscaler_generation(&last, &generation, &now);
            assert(seen[k] == 2 + (unsigned long long)k);
            assert(temporal_upscaler_generation(&last, &generation, &now) == seen[k]);
        }
        now = first;
        assert(temporal_upscaler_generation(&last, &generation, &now) == 9);
        /* a making of all zeros is a making too: the first call always gives a generation that is not 0 */
        TemporalUpscalerMaking none = {0}, zero = {0};
        unsigned long long g = 0;
        assert(temporal_upscaler_generation(&none, &g, &zero) == 1);
        assert(temporal_upscaler_generation(&none, &g, &zero) == 1);
    }
    /* the whole target at full size, and nothing else */
    {
        const TemporalRectState whole = {{0, 0, 3840, 2160}, {0, 2160, 3840, -2160}, 3840, 2160, 1.0f, 1.0f};
        TemporalRectState s = whole;
        assert(temporal_upscaler_whole(&whole));
        s.fx = 0.75f;
        assert(!temporal_upscaler_whole(&s));
        s = whole;
        s.fy = 0.75f;
        assert(!temporal_upscaler_whole(&s));
        s = whole;
        s.fx = s.fy = 0.0f; /* not confirmed yet */
        assert(!temporal_upscaler_whole(&s));
        s = whole;
        s.rect.w = 2880;
        assert(!temporal_upscaler_whole(&s));
        s = whole;
        s.rect.h = 1620;
        assert(!temporal_upscaler_whole(&s));
        s = whole;
        s.rect.x = 1;
        assert(!temporal_upscaler_whole(&s));
        s = whole;
        s.rect.y = 1;
        assert(!temporal_upscaler_whole(&s));
        s = (TemporalRectState){0};
        assert(!temporal_upscaler_whole(&s));
    }
    /* The decision at the gate's draw: jitter only with a context that is ready for exactly the frame's
       configuration, and the first reason against it otherwise. */
    {
        const TemporalUpscalerConfig config = {3840, 2160, 3840, 2160, 3840, 2160, 3};
        TemporalUpscalerConfig other = config;
        TemporalUpscalerContexts c = {0};
        const TemporalUpscalerHealth on = {0}, off = {0, 1, 0};
        assert(temporal_upscaler_decide(&on, 1, 1, 1, &c, &config) == TEMPORAL_UPSCALER_NOT_READY);
        temporal_upscaler_created(&c, &config, 1);
        assert(temporal_upscaler_decide(&on, 1, 1, 1, &c, &config) == TEMPORAL_UPSCALER_NOT_READY); /* on its way */
        assert(temporal_upscaler_completed(&c, &config) == TEMPORAL_UPSCALER_PUBLISHED);
        assert(temporal_upscaler_decide(&on, 1, 1, 1, &c, &config) == TEMPORAL_UPSCALER_JITTER);
        assert(temporal_upscaler_decide(&off, 1, 1, 1, &c, &config) == TEMPORAL_UPSCALER_NOT_ON);
        assert(temporal_upscaler_decide(&on, 0, 1, 1, &c, &config) == TEMPORAL_UPSCALER_NOT_GATE);
        assert(temporal_upscaler_decide(&on, 1, 0, 1, &c, &config) == TEMPORAL_UPSCALER_NOT_WHOLE);
        assert(temporal_upscaler_decide(&on, 1, 1, 0, &c, &config) == TEMPORAL_UPSCALER_NOT_KNOWN);
        assert(temporal_upscaler_decide(&off, 0, 0, 0, &c, &config) == TEMPORAL_UPSCALER_NOT_ON); /* the first reason */
        assert(temporal_upscaler_decide(&on, 0, 0, 0, &c, &config) == TEMPORAL_UPSCALER_NOT_GATE);
        assert(temporal_upscaler_decide(&on, 1, 0, 0, &c, &config) == TEMPORAL_UPSCALER_NOT_WHOLE);
        other.generation = 4; /* the far plane's decision changed, say: another context is needed */
        assert(temporal_upscaler_decide(&on, 1, 1, 1, &c, &other) == TEMPORAL_UPSCALER_NOT_READY);
        other = config;
        other.render_w = 2880;
        assert(temporal_upscaler_decide(&on, 1, 1, 1, &c, &other) == TEMPORAL_UPSCALER_NOT_READY);
        /* at the hook: evaluated only when the frame was jittered, the configuration is the decided one and the
           context is still the ready one */
        assert(temporal_upscaler_hook(TEMPORAL_UPSCALER_JITTER, &config, &config, &c));
        for (int reason = TEMPORAL_UPSCALER_NOT_ON; reason < TEMPORAL_UPSCALER_REASONS; reason++)
            assert(!temporal_upscaler_hook(reason, &config, &config, &c));
        other = config;
        other.generation = 4;
        assert(!temporal_upscaler_hook(TEMPORAL_UPSCALER_JITTER, &config, &other, &c));
        other = config;
        other.out_h = 1080;
        assert(!temporal_upscaler_hook(TEMPORAL_UPSCALER_JITTER, &config, &other, &c));
        /* another context came into use between the decision and the hook, and the hook sees its
           configuration: ready for what is seen, and still not the frame that was decided */
        {
            TemporalUpscalerContexts replaced = c;
            other = config;
            other.generation = 4;
            temporal_upscaler_created(&replaced, &other, 1);
            assert(temporal_upscaler_completed(&replaced, &other) == TEMPORAL_UPSCALER_RETIRE);
            assert(temporal_upscaler_ready(&replaced, &other));
            assert(!temporal_upscaler_hook(TEMPORAL_UPSCALER_JITTER, &config, &other, &replaced));
            assert(temporal_upscaler_hook(TEMPORAL_UPSCALER_JITTER, &other, &other, &replaced));
        }
        assert(temporal_upscaler_stop(&c) == 1); /* stopped between the decision and the hook */
        assert(!temporal_upscaler_hook(TEMPORAL_UPSCALER_JITTER, &config, &config, &c));
        assert(temporal_upscaler_decide(&on, 1, 1, 1, &c, &config) == TEMPORAL_UPSCALER_NOT_READY);
    }
    /* The frame time: the measured one between two evaluated hooks, else the nominal one with the fallback set. */
    {
        int fallback = -1;
        const unsigned long long t0 = 5000000000ull;
        assert(close_to(temporal_upscaler_seconds(t0 + 8333333, t0, 1, 1.0 / 120.0, &fallback), 0.008333333, 1e-12));
        assert(fallback == 0);
        assert(temporal_upscaler_seconds(t0 + 8333333, t0, 0, 1.0 / 120.0, &fallback) == 1.0 / 120.0 && fallback == 1);
        assert(temporal_upscaler_seconds(t0, t0, 1, 1.0 / 120.0, &fallback) == 1.0 / 120.0 && fallback == 1);
        assert(temporal_upscaler_seconds(t0 - 5, t0, 1, 1.0 / 120.0, &fallback) == 1.0 / 120.0 && fallback == 1);
        /* a quarter of a second is taken, a nanosecond more is not */
        assert(temporal_upscaler_seconds(t0 + 250000000, t0, 1, 1.0 / 30.0, &fallback) == 0.25 && fallback == 0);
        assert(temporal_upscaler_seconds(t0 + 250000001, t0, 1, 1.0 / 30.0, &fallback) == 1.0 / 30.0 && fallback == 1);
        assert(temporal_upscaler_seconds(t0 + 1, t0, 1, 1.0 / 30.0, &fallback) == 1e-9 && fallback == 0);
        /* No nominal time, or one the dispatch's check would refuse or that is over a quarter of a second, is
           absent: a sixtieth. 1e-300 and 1e-46 are 0 as floats, 1e-42 is a float whose milliseconds are fine. */
        static const double none[9] = {0.0, -1.0, NAN, INFINITY, 0.3, -INFINITY, 1e-300, 1e-46, 0.2500001};
        float narrowed;
        for (int k = 0; k < 9; k++) {
            assert(temporal_upscaler_seconds(t0 + 5, t0, 0, none[k], &fallback) == 1.0 / 60.0 && fallback == 1);
            assert(temporal_upscaler_seconds(t0 + 400000000, t0, 1, none[k], &fallback) == 1.0 / 60.0);
            assert(!temporal_upscaler_time(none[k], &narrowed) || none[k] > 0.25);
        }
        assert(temporal_upscaler_seconds(t0 + 5, t0, 0, 0.25, &fallback) == 0.25 && fallback == 1);
        assert(temporal_upscaler_seconds(t0 + 5, t0, 0, 1e-42, &fallback) == 1e-42 && fallback == 1);
        /* every answer passes the dispatch's check, whatever the clocks and the nominal time are */
        {
            static const unsigned long long later[7] = {0, 1, 1000, 8333333, 250000000, 250000001, 9000000000ull};
            static const double nominal[8] = {1.0 / 120.0, 1.0 / 30.0, 0.0, 1e-300, 1e-46, NAN, INFINITY, 5.0};
            for (int i = 0; i < 7; i++)
                for (int j = 0; j < 8; j++)
                    for (int evaluated = 0; evaluated < 2; evaluated++) {
                        const double given =
                            temporal_upscaler_seconds(t0 + later[i], t0, evaluated, nominal[j], &fallback);
                        assert(temporal_upscaler_time(given, &narrowed) && given <= 0.25);
                    }
        }
        /* whatever comes out passes the dispatch's check of the frame time */
        float handed;
        assert(temporal_upscaler_time(temporal_upscaler_seconds(t0 + 1, t0, 1, 0.0, &fallback), &handed));
        assert(temporal_upscaler_time(temporal_upscaler_seconds(t0, t0, 1, NAN, &fallback), &handed));
    }
    /* What came of a frame. Eight jittered frames in a row that were skipped or failed turn it off, and an
       evaluated one between them begins the count again. */
    {
        TemporalUpscalerHealth h = {0};
        for (int k = 0; k < 7; k++)
            assert(!temporal_upscaler_after(&h, 1, k & 1 ? TEMPORAL_UPSCALER_SKIPPED : TEMPORAL_UPSCALER_FAILED));
        assert(!temporal_upscaler_after(&h, 1, TEMPORAL_UPSCALER_EVALUATED) && h.failures == 0);
        for (int k = 0; k < 7; k++) assert(!temporal_upscaler_after(&h, 1, TEMPORAL_UPSCALER_SKIPPED));
        assert(temporal_upscaler_after(&h, 1, TEMPORAL_UPSCALER_FAILED) && h.off && !h.stopped);
    }
    /* A frame that was not drawn with jitter is no failure and no success: an evaluated, a skipped or a failed
       named for it changes nothing. Failures with such frames between them still reach eight, and an
       "evaluated" named for one does not begin the count again. */
    {
        TemporalUpscalerHealth h = {0};
        for (int k = 0; k < 7; k++) {
            assert(!temporal_upscaler_after(&h, 1, TEMPORAL_UPSCALER_FAILED) && h.failures == (unsigned)k + 1);
            for (int outcome = TEMPORAL_UPSCALER_EVALUATED; outcome <= TEMPORAL_UPSCALER_FAILED; outcome++) {
                const TemporalUpscalerHealth before = h;
                assert(!temporal_upscaler_after(&h, 0, outcome) && !memcmp(&h, &before, sizeof h));
            }
        }
        assert(temporal_upscaler_after(&h, 1, TEMPORAL_UPSCALER_SKIPPED) && h.off && !h.stopped);
    }
    /* A stop is not an evaluate's outcome: it latches whenever it is reported, also for a frame that was never
       jittered (a prepare whose submission's outcome is not known, before any context is ready). The next
       decision refuses jitter with a ready context and everything else in order. */
    {
        const TemporalUpscalerConfig config = {3840, 2160, 3840, 2160, 3840, 2160, 3};
        TemporalUpscalerContexts c = {0};
        TemporalUpscalerHealth h = {0};
        temporal_upscaler_after(&h, 1, TEMPORAL_UPSCALER_FAILED);
        assert(temporal_upscaler_after(&h, 0, TEMPORAL_UPSCALER_STOPPED) == 1 && h.off && h.stopped && h.failures == 1);
        temporal_upscaler_created(&c, &config, 1);
        assert(temporal_upscaler_completed(&c, &config) == TEMPORAL_UPSCALER_PUBLISHED);
        assert(temporal_upscaler_decide(&h, 1, 1, 1, &c, &config) == TEMPORAL_UPSCALER_NOT_ON);
        assert(temporal_upscaler_after(&h, 1, TEMPORAL_UPSCALER_EVALUATED) == 1 && h.off);
    }
    /* A stop is a latch, and so is the eighth failure: after it every outcome, an evaluated one too, leaves it
       off, jittered or not, and the decision allows no jitter with everything else in order. */
    for (int by_stop = 0; by_stop < 2; by_stop++) {
        const TemporalUpscalerConfig config = {3840, 2160, 3840, 2160, 3840, 2160, 3};
        TemporalUpscalerContexts c = {0};
        TemporalUpscalerHealth h = {0};
        temporal_upscaler_created(&c, &config, 1);
        assert(temporal_upscaler_completed(&c, &config) == TEMPORAL_UPSCALER_PUBLISHED);
        assert(temporal_upscaler_decide(&h, 1, 1, 1, &c, &config) == TEMPORAL_UPSCALER_JITTER);
        if (by_stop) assert(temporal_upscaler_after(&h, 1, TEMPORAL_UPSCALER_STOPPED));
        else
            for (int k = 0; k < 8; k++) temporal_upscaler_after(&h, 1, TEMPORAL_UPSCALER_FAILED);
        assert(h.off && h.stopped == by_stop);
        const TemporalUpscalerHealth latched = h;
        for (int jittered = 0; jittered < 2; jittered++)
            for (int outcome = TEMPORAL_UPSCALER_EVALUATED; outcome <= TEMPORAL_UPSCALER_STOPPED; outcome++)
                for (int times = 0; times < 20; times++) {
                    assert(temporal_upscaler_after(&h, jittered, outcome) == 1);
                    assert(!memcmp(&h, &latched, sizeof h));
                    assert(temporal_upscaler_decide(&h, 1, 1, 1, &c, &config) == TEMPORAL_UPSCALER_NOT_ON);
                }
        /* only a new session is clean */
        const TemporalUpscalerHealth fresh = {0};
        assert(temporal_upscaler_decide(&fresh, 1, 1, 1, &c, &config) == TEMPORAL_UPSCALER_JITTER);
    }
    /* What a recycle notes of a context on its way: nothing until the timeline, read with success, has reached
       the prepare's submission, and a stop whenever the recorder's mark is set, reached or not. */
    {
        unsigned unread = 0;
        assert(temporal_upscaler_poll(1, 9, 0, 10, &unread) == TEMPORAL_UPSCALER_WAIT);
        assert(temporal_upscaler_poll(1, 10, 0, 10, &unread) == TEMPORAL_UPSCALER_COMPLETED);
        assert(temporal_upscaler_poll(1, 11, 0, 10, &unread) == TEMPORAL_UPSCALER_COMPLETED);
        assert(temporal_upscaler_poll(1, 11, 0, 0, &unread) == TEMPORAL_UPSCALER_WAIT);  /* no submission is named */
        assert(temporal_upscaler_poll(1, 11, 1, 10, &unread) == TEMPORAL_UPSCALER_STOP); /* signalled by the host */
        assert(temporal_upscaler_poll(1, 9, 1, 10, &unread) == TEMPORAL_UPSCALER_STOP && unread == 0);
        /* the mark outranks a timeline that cannot be read and a number that is not named */
        assert(temporal_upscaler_poll(0, 0, 1, 10, &unread) == TEMPORAL_UPSCALER_STOP && unread == 0);
        assert(temporal_upscaler_poll(0, 0, 1, 0, &unread) == TEMPORAL_UPSCALER_STOP);
        /* The timeline could not be read: waited for, seven times in a row, and a read that works begins the
           count again and gives its answer. */
        for (unsigned k = 1; k <= 7; k++)
            assert(temporal_upscaler_poll(0, 11, 0, 10, &unread) == TEMPORAL_UPSCALER_WAIT && unread == k);
        assert(temporal_upscaler_poll(1, 11, 0, 10, &unread) == TEMPORAL_UPSCALER_COMPLETED && unread == 0);
        for (unsigned k = 1; k <= 7; k++)
            assert(temporal_upscaler_poll(0, 11, 0, 10, &unread) == TEMPORAL_UPSCALER_WAIT);
        assert(temporal_upscaler_poll(1, 9, 0, 10, &unread) == TEMPORAL_UPSCALER_WAIT && unread == 0);
        /* eight in a row: a stop, also when the number it could not read had long been reached */
        for (unsigned k = 1; k <= 7; k++)
            assert(temporal_upscaler_poll(0, 11, 0, 10, &unread) == TEMPORAL_UPSCALER_WAIT);
        assert(temporal_upscaler_poll(0, 11, 0, 10, &unread) == TEMPORAL_UPSCALER_STOP && unread == 8);
    }
    /* The note at a place where the runtime records: the mark, read again there, outranks it. A context noted
       as completed is not published with the mark set, and with nothing on its way the mark is a stop too. */
    assert(temporal_upscaler_noted(TEMPORAL_UPSCALER_COMPLETED, 1) == TEMPORAL_UPSCALER_STOP);
    assert(temporal_upscaler_noted(TEMPORAL_UPSCALER_WAIT, 1) == TEMPORAL_UPSCALER_STOP);
    assert(temporal_upscaler_noted(TEMPORAL_UPSCALER_STOP, 1) == TEMPORAL_UPSCALER_STOP);
    assert(temporal_upscaler_noted(TEMPORAL_UPSCALER_COMPLETED, 0) == TEMPORAL_UPSCALER_COMPLETED);
    assert(temporal_upscaler_noted(TEMPORAL_UPSCALER_WAIT, 0) == TEMPORAL_UPSCALER_WAIT);
    assert(temporal_upscaler_noted(TEMPORAL_UPSCALER_STOP, 0) == TEMPORAL_UPSCALER_STOP);
    {
        /* and that stop latches for a frame without jitter, so the next decision refuses a ready context */
        const TemporalUpscalerConfig config = {3840, 2160, 3840, 2160, 3840, 2160, 3};
        TemporalUpscalerContexts c = {0};
        TemporalUpscalerHealth h = {0};
        temporal_upscaler_created(&c, &config, 1);
        assert(temporal_upscaler_completed(&c, &config) == TEMPORAL_UPSCALER_PUBLISHED);
        if (temporal_upscaler_noted(TEMPORAL_UPSCALER_WAIT, 1) == TEMPORAL_UPSCALER_STOP)
            temporal_upscaler_after(&h, 0, TEMPORAL_UPSCALER_STOPPED);
        assert(temporal_upscaler_decide(&h, 1, 1, 1, &c, &config) == TEMPORAL_UPSCALER_NOT_ON);
    }
    /* A completed context is held against what the frames want NOW. The context for 1920 wide frames is in use,
       one for 3840 is on its way from earlier frames, and this frame is 1920: the one that completed is stale
       and the one in use stays, so the frame is jittered. Held against what was wanted when it was made, it
       would replace the one in use and the frame would find no ready context. */
    {
        const TemporalUpscalerConfig small = {1920, 1080, 1920, 1080, 1920, 1080, 3};
        const TemporalUpscalerConfig large = {3840, 2160, 3840, 2160, 3840, 2160, 3};
        const TemporalUpscalerHealth on = {0};
        TemporalUpscalerContexts c = {0};
        temporal_upscaler_created(&c, &small, 1);
        assert(temporal_upscaler_completed(&c, &small) == TEMPORAL_UPSCALER_PUBLISHED);
        temporal_upscaler_created(&c, &large, 1);
        TemporalUpscalerContexts wrong = c;
        assert(temporal_upscaler_completed(&c, &small) == TEMPORAL_UPSCALER_STALE);
        assert(temporal_upscaler_ready(&c, &small) && !c.pending);
        assert(temporal_upscaler_decide(&on, 1, 1, 1, &c, &small) == TEMPORAL_UPSCALER_JITTER);
        assert(temporal_upscaler_completed(&wrong, &large) == TEMPORAL_UPSCALER_RETIRE);
        assert(temporal_upscaler_decide(&on, 1, 1, 1, &wrong, &small) == TEMPORAL_UPSCALER_NOT_READY);
    }
    /* The projection the frame's draws were given, against the decision's. The pre-pass's draw decided with a
       finite far plane, and the G-buffer's draws were given one with the far plane at infinity: not the same, so
       the evaluate is skipped. */
    {
        float finite[16], infinite[16], other[16];
        TemporalUpscalerUsed used = {0};
        TemporalProjection p;
        projection_make(0.1, 5000.0, 0.8726646259971648, 16.0 / 9.0, finite);
        projection_make(0.1, INFINITY, 0.8726646259971648, 16.0 / 9.0, infinite);
        assert(temporal_projection_camera(finite, &p) == TEMPORAL_PROJECTION_OK && !p.far_infinite);
        assert(temporal_projection_camera(infinite, &p) == TEMPORAL_PROJECTION_OK && p.far_infinite);
        assert(!temporal_upscaler_used_same(&used, 7, finite)); /* no draw took a projection in frame 7 */
        temporal_upscaler_used(&used, 7, infinite);
        assert(!temporal_upscaler_used_same(&used, 7, finite) && temporal_upscaler_used_same(&used, 7, infinite));
        /* the same projection for every draw of the frame, and the decision's: evaluated */
        temporal_upscaler_used(&used, 8, finite);
        temporal_upscaler_used(&used, 8, finite);
        assert(temporal_upscaler_used_same(&used, 8, finite) && !used.mixed);
        assert(!temporal_upscaler_used_same(&used, 9, finite)); /* frame 8's draws say nothing of frame 9 */
        assert(!temporal_upscaler_used_same(&used, 0, finite));
        /* two projections in one frame: mixed, whichever came first and whatever the decision's was */
        temporal_upscaler_used(&used, 8, infinite);
        assert(used.mixed && !temporal_upscaler_used_same(&used, 8, finite));
        assert(!temporal_upscaler_used_same(&used, 8, infinite));
        temporal_upscaler_used(&used, 8, finite);
        assert(!temporal_upscaler_used_same(&used, 8, finite));
        /* A draw that took a camera's block which is not accounted for poisons its frame: before the draws that
           were given the decision's projection, between them, or after them. */
        {
            TemporalUpscalerUsed first = {0}, between = {0}, last = {0}, zero = {0};
            float none[16] = {0};
            temporal_upscaler_poison(&first, 20);
            temporal_upscaler_used(&first, 20, finite);
            temporal_upscaler_used(&first, 20, finite);
            assert(first.mixed && !temporal_upscaler_used_same(&first, 20, finite));
            temporal_upscaler_used(&between, 20, finite);
            temporal_upscaler_poison(&between, 20);
            temporal_upscaler_used(&between, 20, finite);
            assert(!temporal_upscaler_used_same(&between, 20, finite));
            temporal_upscaler_used(&last, 20, finite);
            assert(temporal_upscaler_used_same(&last, 20, finite));
            temporal_upscaler_poison(&last, 20);
            assert(!temporal_upscaler_used_same(&last, 20, finite));
            /* the frame after is clean again, and a poisoned frame matches no projection, one of zeros included */
            temporal_upscaler_used(&last, 21, finite);
            assert(temporal_upscaler_used_same(&last, 21, finite));
            temporal_upscaler_poison(&zero, 30);
            assert(!temporal_upscaler_used_same(&zero, 30, none));
        }
        /* A scene draw that reads the camera's bank with another block there. The frame was decided at block P.
           A later draw has a third address R, which is no slot the camera is known to use: a stranger, and the
           frame that had only the decision's projection so far is not evaluated. */
        {
            const unsigned long long P = 0x512ba6000ull, Q = 0x512ba7000ull, R = 0x77001000ull;
            TemporalUpscalerUsed seen = {0};
            temporal_upscaler_used(&seen, 40, finite);
            assert(temporal_upscaler_used_same(&seen, 40, finite));
            assert(!temporal_upscaler_stranger(1, P, P, 1));
            assert(temporal_upscaler_stranger(1, R, P, 0) && temporal_upscaler_stranger(1, R, P, 1));
            assert(temporal_upscaler_stranger(1, Q, P, 1)); /* the camera's other slot is another block too */
            assert(temporal_upscaler_stranger(1, 0, P, 0)); /* nothing bound where the program reads */
            if (temporal_upscaler_stranger(1, R, P, 0)) temporal_upscaler_poison(&seen, 40);
            assert(!temporal_upscaler_used_same(&seen, 40, finite));
            /* before the decision: a block that decides the frame is no stranger, any other is */
            assert(!temporal_upscaler_stranger(0, P, 0, 1) && temporal_upscaler_stranger(0, R, 0, 0));
            assert(temporal_upscaler_stranger(0, 0, 0, 0));
        }
        /* a new frame begins clean */
        temporal_upscaler_used(&used, 9, finite);
        assert(!used.mixed && temporal_upscaler_used_same(&used, 9, finite));
        /* one word of difference is another projection, the smallest there is included */
        memcpy(other, finite, sizeof other);
        other[14] = nextafterf(other[14], 1.0f);
        assert(!temporal_upscaler_used_same(&used, 9, other));
        temporal_upscaler_used(&used, 9, other);
        assert(used.mixed);
    }
    /* The same order at the hook. The decision predicted configuration A, which has no ready context, and the
       frame was left without jitter. A recycle then notes that the context made for A has completed. The hook
       sees that the frame as drawn is B, whose context is in use: settled against B, the completed one goes
       unused and B stays. Settled against the decision's A it would replace the one in use. */
    {
        const TemporalUpscalerConfig a = {3840, 2160, 3840, 2160, 3840, 2160, 3};
        const TemporalUpscalerConfig b = {3840, 2160, 3840, 2160, 3840, 2160, 4};
        const TemporalUpscalerHealth on = {0};
        unsigned unread = 0;
        TemporalUpscalerContexts c = {0};
        temporal_upscaler_created(&c, &b, 1);
        assert(temporal_upscaler_completed(&c, &b) == TEMPORAL_UPSCALER_PUBLISHED);
        temporal_upscaler_created(&c, &a, 1);
        assert(temporal_upscaler_decide(&on, 1, 1, 1, &c, &a) == TEMPORAL_UPSCALER_NOT_READY);
        const int noted = temporal_upscaler_noted(temporal_upscaler_poll(1, 20, 0, 20, &unread), 0);
        assert(noted == TEMPORAL_UPSCALER_COMPLETED);
        TemporalUpscalerContexts by_decision = c;
        assert(temporal_upscaler_completed(&c, &b) == TEMPORAL_UPSCALER_STALE);
        assert(temporal_upscaler_ready(&c, &b) && !temporal_upscaler_ready(&c, &a) && !c.pending);
        assert(temporal_upscaler_decide(&on, 1, 1, 1, &c, &b) == TEMPORAL_UPSCALER_JITTER);
        assert(temporal_upscaler_completed(&by_decision, &a) == TEMPORAL_UPSCALER_RETIRE);
        assert(!temporal_upscaler_ready(&by_decision, &b));
    }
    /* Off, and retirement goes on. A context on its way when a stop came, and one whose prepare was refused:
       with the mark clear and the completion noted each is released, and neither is ever published. With the
       mark set nothing is released. While the upscaler is on this path is not taken. */
    {
        const TemporalUpscalerConfig config = {3840, 2160, 3840, 2160, 3840, 2160, 3};
        assert(temporal_upscaler_retire(1, TEMPORAL_UPSCALER_COMPLETED, 0) == 1);
        assert(temporal_upscaler_retire(1, TEMPORAL_UPSCALER_COMPLETED, 1) == 0); /* the mark: kept */
        assert(temporal_upscaler_retire(1, temporal_upscaler_noted(TEMPORAL_UPSCALER_COMPLETED, 1), 1) == 0);
        assert(temporal_upscaler_retire(1, TEMPORAL_UPSCALER_WAIT, 0) == 0);      /* not completed yet */
        assert(temporal_upscaler_retire(1, TEMPORAL_UPSCALER_STOP, 0) == 0);
        assert(temporal_upscaler_retire(0, TEMPORAL_UPSCALER_COMPLETED, 0) == 0); /* on: settled, not retired */
        for (int refused = 0; refused < 2; refused++) {
            TemporalUpscalerContexts c = {0};
            TemporalUpscalerHealth h = {0};
            temporal_upscaler_created(&c, &config, !refused);
            assert(temporal_upscaler_after(&h, 0, TEMPORAL_UPSCALER_STOPPED) == 1);
            assert(temporal_upscaler_stop(&c) == 0); /* nothing was in use */
            assert(c.pending == (refused ? 2 : 3));
            assert(temporal_upscaler_retire(h.off, TEMPORAL_UPSCALER_COMPLETED, 0));
            /* held against the very configuration it was made for, it is still not published */
            assert(temporal_upscaler_completed(&c, &config) ==
                   (refused ? TEMPORAL_UPSCALER_RELEASE : TEMPORAL_UPSCALER_STALE));
            assert(!c.live && !c.pending && !temporal_upscaler_ready(&c, &config));
            assert(temporal_upscaler_decide(&h, 1, 1, 1, &c, &config) == TEMPORAL_UPSCALER_NOT_ON);
        }
        /* one that was in use when the stop came goes too (the stop says so), and the one on its way after it */
        TemporalUpscalerContexts c = {0};
        temporal_upscaler_created(&c, &config, 1);
        assert(temporal_upscaler_completed(&c, &config) == TEMPORAL_UPSCALER_PUBLISHED);
        TemporalUpscalerConfig next = config;
        next.generation = 4;
        temporal_upscaler_created(&c, &next, 1);
        assert(temporal_upscaler_stop(&c) == 1 && !c.live && c.pending == 3);
        assert(temporal_upscaler_completed(&c, &next) == TEMPORAL_UPSCALER_STALE && !c.live);
    }
    /* Eight reads of the timeline fail with the mark clear: a stop, and no proof of anything. The context on its
       way is held, with its submission's number. A later read works and shows the number reached: only then
       is it released, once, and it is never published and the upscaler stays off. */
    {
        const TemporalUpscalerConfig config = {3840, 2160, 3840, 2160, 3840, 2160, 3};
        const unsigned long long number = 20;
        TemporalUpscalerContexts c = {0};
        TemporalUpscalerHealth h = {0};
        unsigned unread = 0;
        int noted = TEMPORAL_UPSCALER_WAIT, released = 0;
        temporal_upscaler_created(&c, &config, 1);
        for (int k = 0; k < 8; k++) noted = temporal_upscaler_poll(0, 25, 0, number, &unread);
        assert(temporal_upscaler_noted(noted, 0) == TEMPORAL_UPSCALER_STOP);
        /* the stop: off, the context given up for publication and held, and no release by the stop or the count */
        assert(!temporal_upscaler_retire(h.off, noted, 0));
        assert(temporal_upscaler_after(&h, 0, TEMPORAL_UPSCALER_STOPPED) == 1);
        temporal_upscaler_stop(&c);
        assert(c.pending == 3 && !temporal_upscaler_retire(h.off, TEMPORAL_UPSCALER_STOP, 0));
        unread = 0;
        noted = TEMPORAL_UPSCALER_WAIT;
        /* reads that work and do not show the number yet, and more that fail: still held */
        noted = temporal_upscaler_poll(1, 19, 0, number, &unread);
        assert(noted == TEMPORAL_UPSCALER_WAIT && !temporal_upscaler_retire(h.off, noted, 0) && c.pending == 3);
        noted = temporal_upscaler_poll(0, 25, 0, number, &unread);
        assert(noted == TEMPORAL_UPSCALER_WAIT && !temporal_upscaler_retire(h.off, noted, 0));
        /* the read that shows it reached */
        noted = temporal_upscaler_poll(1, 20, 0, number, &unread);
        assert(noted == TEMPORAL_UPSCALER_COMPLETED && temporal_upscaler_retire(h.off, noted, 0));
        if (temporal_upscaler_retire(h.off, noted, 0)) {
            assert(temporal_upscaler_completed(&c, &config) == TEMPORAL_UPSCALER_STALE);
            released++;
        }
        /* once: nothing is on its way any more, and a second completion gives nothing to release */
        assert(released == 1 && !c.pending && !c.live);
        assert(temporal_upscaler_completed(&c, &config) == TEMPORAL_UPSCALER_NOTHING);
        assert(temporal_upscaler_decide(&h, 1, 1, 1, &c, &config) == TEMPORAL_UPSCALER_NOT_ON);
        /* the same, but the mark is set by the time the read works: not released */
        assert(!temporal_upscaler_retire(h.off, temporal_upscaler_noted(TEMPORAL_UPSCALER_COMPLETED, 1), 1));
    }
    /* The colour a decision counts on: only the frame before's, seen at its hook. */
    {
        const TemporalUpscalerColour seen = {100, 122, 1, 3840, 2160};
        TemporalUpscalerColour other = seen;
        assert(temporal_upscaler_predicted(&seen, 101));
        assert(!temporal_upscaler_predicted(&seen, 102)); /* a gap */
        assert(!temporal_upscaler_predicted(&seen, 100)); /* the same frame is no frame before */
        assert(!temporal_upscaler_predicted(&seen, 99) && !temporal_upscaler_predicted(&seen, 0));
        other.frame = 0;
        assert(!temporal_upscaler_predicted(&other, 1)); /* none seen yet */
        other = seen;
        other.frame = ~0u; /* the number wraps: no frame follows it as 0 */
        assert(!temporal_upscaler_predicted(&other, 0));
        other = seen;
        other.layers = 0;
        assert(!temporal_upscaler_predicted(&other, 101));
        /* The hook saw a colour target of two layers in frame 100. The decision of frame 101 has no prediction,
           so with the gate accepting, the whole target, a ready context and the upscaler on it is not JITTER. */
        {
            const TemporalUpscalerConfig config = {3840, 2160, 3840, 2160, 3840, 2160, 3};
            const TemporalUpscalerHealth on = {0};
            TemporalUpscalerContexts c = {0};
            temporal_upscaler_created(&c, &config, 1);
            assert(temporal_upscaler_completed(&c, &config) == TEMPORAL_UPSCALER_PUBLISHED);
            other = seen;
            other.layers = 2;
            assert(!temporal_upscaler_predicted(&other, 101));
            const int known = temporal_upscaler_predicted(&other, 101);
            assert(temporal_upscaler_decide(&on, 1, 1, known, &c, &config) == TEMPORAL_UPSCALER_NOT_KNOWN);
            /* The hook saw a colour target on layer 1 of an array, or a member of a shared one, each a single
               layer by its own count: kept as no layer, and the next decision is not JITTER either. */
            const TemporalUpscalerColour layer1 = temporal_upscaler_colour(100, 122, 1, 1, 0, 3840, 2160);
            const TemporalUpscalerColour member = temporal_upscaler_colour(100, 122, 1, 0, 1, 3840, 2160);
            const TemporalUpscalerColour two = temporal_upscaler_colour(100, 122, 2, 0, 0, 3840, 2160);
            const TemporalUpscalerColour plain = temporal_upscaler_colour(100, 122, 1, 0, 0, 3840, 2160);
            assert(layer1.layers == 0 && member.layers == 0 && two.layers == 0 && plain.layers == 1);
            assert(plain.frame == 100 && plain.format == 122 && plain.w == 3840 && plain.h == 2160);
            assert(temporal_upscaler_colour_same(&plain, &seen) && !temporal_upscaler_colour_same(&layer1, &seen));
            const TemporalUpscalerColour* refused[3] = {&layer1, &member, &two};
            for (int k = 0; k < 3; k++)
                assert(temporal_upscaler_decide(&on, 1, 1, temporal_upscaler_predicted(refused[k], 101), &c, &config) ==
                       TEMPORAL_UPSCALER_NOT_KNOWN);
            assert(temporal_upscaler_decide(&on, 1, 1, temporal_upscaler_predicted(&plain, 101), &c, &config) ==
                   TEMPORAL_UPSCALER_JITTER);
            /* the same frames with one layer are jittered: the layer count is what refused */
            assert(temporal_upscaler_decide(&on, 1, 1, temporal_upscaler_predicted(&seen, 101), &c, &config) ==
                   TEMPORAL_UPSCALER_JITTER);
        }
        other = seen;
        other.w = 0;
        assert(!temporal_upscaler_predicted(&other, 101));
        other = seen;
        other.frame = 500; /* the frame is no part of what is compared */
        assert(temporal_upscaler_colour_same(&seen, &other));
        unsigned* members[2] = {&other.format, &other.layers};
        for (int k = 0; k < 2; k++) {
            other = seen;
            ++*members[k];
            assert(!temporal_upscaler_colour_same(&seen, &other));
        }
        other = seen;
        other.w = 1920;
        assert(!temporal_upscaler_colour_same(&seen, &other));
        other = seen;
        other.h = 1080;
        assert(!temporal_upscaler_colour_same(&seen, &other));
    }
    /* The reset's causes add up. Each alone sets it, and the new context's is used up by the asking. */
    {
        unsigned last = 0;
        assert(temporal_upscaler_resets(0, 0, 1, &last) == 1 && last == 1); /* the first context */
        assert(temporal_upscaler_resets(0, 0, 1, &last) == 0);
        assert(temporal_upscaler_resets(1, 0, 1, &last) == 1);
        assert(temporal_upscaler_resets(0, 1, 1, &last) == 1);
        assert(temporal_upscaler_resets(1, 1, 2, &last) == 1 && last == 2); /* all three: the count is brought up */
        assert(temporal_upscaler_resets(0, 0, 2, &last) == 0);
        assert(temporal_upscaler_resets(0, 0, 3, &last) == 1 && temporal_upscaler_resets(0, 0, 3, &last) == 0);
    }
    /* The counts between two lines. A frame is counted once, with its outcome, in one window: the window is
       closed at the start of the next frame's decision, not at its own. */
    {
        TemporalUpscalerTally t = {0};
        /* 599 frames, each decided and evaluated: no line is due at any of their decisions */
        for (int k = 0; k < 599; k++) {
            assert(!temporal_upscaler_due(&t));
            temporal_upscaler_decided(&t, TEMPORAL_UPSCALER_JITTER);
            temporal_upscaler_tallied(&t, 0, TEMPORAL_UPSCALER_EVALUATED);
        }
        /* the 600th: not due at its own decision, so its outcome is still to come into this window */
        assert(!temporal_upscaler_due(&t));
        temporal_upscaler_decided(&t, TEMPORAL_UPSCALER_JITTER);
        assert(t.frames == 600 && t.evaluated == 599);
        temporal_upscaler_tallied(&t, 0, TEMPORAL_UPSCALER_EVALUATED);
        /* the 601st's decision: the line is due, and holds 600 frames with 600 outcomes */
        assert(temporal_upscaler_due(&t) && t.frames == 600 && t.evaluated == 600);
        temporal_upscaler_closed(&t);
        temporal_upscaler_decided(&t, TEMPORAL_UPSCALER_NOT_READY);
        assert(t.frames == 1 && t.evaluated == 0 && t.not_jittered[TEMPORAL_UPSCALER_NOT_READY] == 1);
    }
    {
        /* The window's last frame was drawn with jitter and its hook was missed. That is found at the next
           frame's decision, counted as skipped first, and is in the line that is then due. */
        TemporalUpscalerTally t = {0};
        for (int k = 0; k < 600; k++) {
            assert(!temporal_upscaler_due(&t));
            temporal_upscaler_decided(&t, k % 3 ? TEMPORAL_UPSCALER_JITTER : TEMPORAL_UPSCALER_NOT_READY);
            if (k % 3 && k != 599) temporal_upscaler_tallied(&t, 0, TEMPORAL_UPSCALER_EVALUATED);
        }
        assert(t.frames == 600 && t.not_jittered[TEMPORAL_UPSCALER_NOT_READY] == 200 && t.evaluated == 399);
        temporal_upscaler_tallied(&t, 0, TEMPORAL_UPSCALER_SKIPPED); /* the missed hook, found at frame 601 */
        assert(temporal_upscaler_due(&t) && t.skipped == 1 && t.evaluated + t.skipped == 400);
        assert(t.not_jittered[TEMPORAL_UPSCALER_JITTER] == 0);
    }
    {
        TemporalUpscalerTally t = {0};
        temporal_upscaler_decided(&t, TEMPORAL_UPSCALER_NOT_GATE);
        temporal_upscaler_decided(&t, TEMPORAL_UPSCALER_REASONS); /* no reason: counted as a frame alone */
        temporal_upscaler_decided(&t, -1);
        assert(t.frames == 3 && t.not_jittered[TEMPORAL_UPSCALER_NOT_GATE] == 1);
        temporal_upscaler_tallied(&t, 0, TEMPORAL_UPSCALER_EVALUATED);
        temporal_upscaler_tallied(&t, 0, TEMPORAL_UPSCALER_EVALUATED);
        temporal_upscaler_tallied(&t, 0, TEMPORAL_UPSCALER_SKIPPED);
        temporal_upscaler_tallied(&t, 0, TEMPORAL_UPSCALER_FAILED);
        temporal_upscaler_tallied(&t, 0, TEMPORAL_UPSCALER_STOPPED);
        assert(t.evaluated == 2 && t.skipped == 1 && t.failed == 1 && t.stopped == 1);
    }
    /* One frame, one fate. Frame 5 was drawn with jitter. Staging its jitter failed, a stop, and at its hook the
       gate did not take it, a skip: the frame counts once, as stopped, and the stop latches in the health. */
    {
        TemporalUpscalerTally t = {0};
        TemporalUpscalerHealth h = {0};
        temporal_upscaler_decided(&t, TEMPORAL_UPSCALER_JITTER);
        assert(temporal_upscaler_tallied(&t, 5, TEMPORAL_UPSCALER_STOPPED) == 1);
        assert(temporal_upscaler_after(&h, 1, TEMPORAL_UPSCALER_STOPPED) == 1);
        assert(temporal_upscaler_tallied(&t, 5, TEMPORAL_UPSCALER_SKIPPED) == 0);
        assert(t.frames == 1 && t.stopped == 1 && t.skipped == 0 && t.evaluated == 0 && t.failed == 0);
        /* the other order: a skip first, then a stop for the same frame. One fate in the counts, the skip,
           and the stop still latches. */
        TemporalUpscalerTally u = {0};
        TemporalUpscalerHealth g = {0};
        temporal_upscaler_decided(&u, TEMPORAL_UPSCALER_JITTER);
        const int first = temporal_upscaler_tallied(&u, 6, TEMPORAL_UPSCALER_SKIPPED);
        assert(first == 1 && !temporal_upscaler_after(&g, first, TEMPORAL_UPSCALER_SKIPPED) && g.failures == 1);
        const int second = temporal_upscaler_tallied(&u, 6, TEMPORAL_UPSCALER_STOPPED);
        assert(second == 0 && temporal_upscaler_after(&g, second, TEMPORAL_UPSCALER_STOPPED) == 1 && g.stopped);
        assert(u.skipped == 1 && u.stopped == 0 && g.failures == 1);
        /* a second skip for the same frame does not count towards the eight either */
        TemporalUpscalerHealth e = {0};
        TemporalUpscalerTally v = {0};
        for (int k = 0; k < 20; k++)
            temporal_upscaler_after(&e, temporal_upscaler_tallied(&v, 9, TEMPORAL_UPSCALER_SKIPPED),
                                    TEMPORAL_UPSCALER_SKIPPED);
        assert(e.failures == 1 && !e.off && v.skipped == 1);
        /* the next frame has a fate of its own, and closing a window keeps which frame had the last one */
        assert(temporal_upscaler_tallied(&v, 10, TEMPORAL_UPSCALER_EVALUATED) == 1 && v.evaluated == 1);
        temporal_upscaler_closed(&v);
        assert(v.frames == 0 && v.evaluated == 0 && v.skipped == 0 && v.fated == 10);
        assert(temporal_upscaler_tallied(&v, 10, TEMPORAL_UPSCALER_FAILED) == 0 && v.failed == 0);
        assert(temporal_upscaler_tallied(&v, 11, TEMPORAL_UPSCALER_FAILED) == 1 && v.failed == 1);
    }
}

/* Acceptance item: the conversion equals the SDK's documented jitter for an added clip offset. AMD's documents
   for FSR 3.1 in the FidelityFX SDK at tag v2.4.0: projection x += 2 jx / width, y -= 2 jy / height, clip y up.
   Solved by hand: jx = dx width / 2, jy = -dy height / 2, and +dy height / 2 for a viewport of positive height.
   The rows are that by hand, the first two the frames the device printed as handed over. */
static void test_jitter_conformance(void) {
    static const struct {
        double w, h, dx, dy, jx, jy_up, jy_down;
    } rows[] = {{320.0, 180.0, -0.0015625, 0.00185185185185185, -0.25, -0.166666666666667, 0.166666666666667},
                {320.0, 180.0, 0.0015625, -0.00432098765432099, 0.25, 0.388888888888889, -0.388888888888889},
                {3840.0, 2160.0, 0.000130208333333333, -0.000347222222222222, 0.25, 0.375, -0.375},
                {2880.0, 1620.0, 0.000277777777777778, 0.000123456790123457, 0.4, -0.1, 0.1},
                {1920.0, 1080.0, -0.000520833333333333, -0.000925925925925926, -0.5, 0.5, -0.5},
                {400.0, 224.0, 0.001, 0.001, 0.2, -0.112, 0.112},
                {2560.0, 1440.0, 0.0, 0.000694444444444444, 0.0, -0.5, 0.5},
                {480.0, 270.0, -0.002, 0.0, -0.48, 0.0, 0.0}};
    int caught[2] = {0, 0};
    for (unsigned i = 0; i < sizeof rows / sizeof *rows; i++)
        for (int up = 0; up < 2; up++)
            for (int moved = 0; moved < 2; moved++) {
                /* the viewport of the height's sign, at the image's first pixel and at 40, 30 of a larger image:
                   where it sits does not enter */
                const float x = moved ? 40.0f : 0.0f, y = moved ? 30.0f : 0.0f;
                const TemporalViewport v = {x, up ? y + (float)rows[i].h : y, (float)rows[i].w,
                                            up ? -(float)rows[i].h : (float)rows[i].h};
                const double clip[2] = {rows[i].dx, rows[i].dy}, jy = up ? rows[i].jy_up : rows[i].jy_down;
                double px[2];
                temporal_jitter_pixels(clip, &v, px);
                assert(close_to(px[0], rows[i].jx, 1e-9) && close_to(px[1], jy, 1e-9));
                /* Two conversions that are wrong on purpose must not pass these rows: y with the other sign,
                   and the width where the height belongs. */
                const double turned[2] = {0.5 * clip[0] * (double)v.width, -0.5 * clip[1] * (double)v.height};
                const double widths[2] = {0.5 * clip[0] * (double)v.width,
                                          0.5 * clip[1] * (v.height < 0.0f ? -(double)v.width : (double)v.width)};
                caught[0] += !close_to(turned[1], jy, 1e-9);
                caught[1] += !close_to(widths[1], jy, 1e-9);
            }
    /* every row with a y offset catches the turned sign, and every such row of a target that is not square the
       width for the height: seven rows of eight, four cases each */
    assert(caught[0] == 28 && caught[1] == 28);
}

/* The GPU time measure's ring of pairs and its arithmetic. */
static void test_gpu_time(void) {
    static TemporalGpuRing r;
    const double period = 1.0; /* a tick is a nanosecond */
    const unsigned long long t0 = 1000000ull;
    const int there[2] = {1, 1};
    double median, mean, high;
    memset(&r, 0, sizeof r);
    /* a bracket whose two marks are in one submission, read when that has completed: its time is kept */
    TemporalGpuHandle a = temporal_gpu_begin(&r, TEMPORAL_GPU_HOOK, 7, t0);
    assert(a >= 0 && temporal_gpu_index(a) == 0 && r.pair[0].state == TEMPORAL_GPU_BEGUN);
    assert(!temporal_gpu_settle(&r, 0, 100, t0, NULL, NULL, 64, period)); /* begun: not settled, whatever is done */
    assert(temporal_gpu_end(&r, a, 7) && r.pair[0].state == TEMPORAL_GPU_ENDED);
    assert(!temporal_gpu_fetch(&r, 0, 6) && temporal_gpu_fetch(&r, 0, 7));
    assert(!temporal_gpu_fetch(&r, -1, 7) && !temporal_gpu_fetch(&r, TEMPORAL_GPU_PAIRS, 7));
    {
        const unsigned long long ticks[2] = {500ull, 2500500ull};
        assert(!temporal_gpu_settle(&r, 0, 6, t0 + 5000000ull, ticks, there, 64, period)); /* not completed */
        assert(r.pair[0].state == TEMPORAL_GPU_ENDED && r.n[TEMPORAL_GPU_HOOK] == 0);
        assert(temporal_gpu_settle(&r, 0, 7, t0 + 5000000ull, ticks, there, 64, period));
        assert(r.pair[0].state == TEMPORAL_GPU_FREE && r.n[TEMPORAL_GPU_HOOK] == 1);
        assert(r.ms[TEMPORAL_GPU_HOOK][0] == 2.5f);
        assert(!temporal_gpu_settle(&r, 0, 7, t0, ticks, there, 64, period)); /* free: nothing to settle */
    }
    /* split: the submission ended between the marks. The second mark is not recorded, no time is made, and the
       pair is not free before the first mark's submission has completed. */
    a = temporal_gpu_begin(&r, TEMPORAL_GPU_HOOK, 8, t0);
    assert(temporal_gpu_index(a) == 0 && !temporal_gpu_end(&r, a, 9) && r.pair[0].state == TEMPORAL_GPU_SPLIT);
    assert(!temporal_gpu_fetch(&r, 0, 20) && !temporal_gpu_end(&r, a, 8) && r.late == 1);
    TemporalGpuHandle open = temporal_gpu_begin(&r, TEMPORAL_GPU_NUMBERS, 8, t0);
    assert(temporal_gpu_index(open) == 1); /* another pair, not the split one */
    assert(!temporal_gpu_settle(&r, 0, 7, t0, NULL, NULL, 64, period) && r.pair[0].state == TEMPORAL_GPU_SPLIT);
    assert(temporal_gpu_settle(&r, 0, 8, t0, NULL, NULL, 64, period));
    assert(r.lost[TEMPORAL_GPU_HOOK][TEMPORAL_GPU_CUT] == 1);
    /* Given up: begun and no second mark to come, as when our passes return between the marks. Until its caller
       says so the pair stays begun and is not freed, whatever its submission did. */
    assert(!temporal_gpu_settle(&r, 1, 50, t0, NULL, NULL, 64, period) && r.pair[1].state == TEMPORAL_GPU_BEGUN);
    temporal_gpu_abandon(&r, open);
    assert(r.pair[1].state == TEMPORAL_GPU_GIVEN_UP && !temporal_gpu_end(&r, open, 8));
    assert(!temporal_gpu_settle(&r, 1, 7, t0, NULL, NULL, 64, period));
    assert(temporal_gpu_settle(&r, 1, 8, t0, NULL, NULL, 64, period));
    assert(r.lost[TEMPORAL_GPU_NUMBERS][TEMPORAL_GPU_PARTIAL] == 1);
    assert(r.n[TEMPORAL_GPU_HOOK] == 1 && r.n[TEMPORAL_GPU_NUMBERS] == 0);
    /* An end that comes late must not close another bracket that took the pair since (the audit's case):
       a bracket in submission 10 is given up and settled, the next takes the same pair in submission 11, and
       the first one's end arrives. It is ignored and counted, the new bracket is still begun, and its own end
       closes it. The same for a late giving up. */
    memset(&r, 0, sizeof r);
    {
        const TemporalGpuHandle old = temporal_gpu_begin(&r, TEMPORAL_GPU_HOOK, 10, t0);
        temporal_gpu_abandon(&r, old);
        assert(temporal_gpu_settle(&r, 0, 10, t0, NULL, NULL, 64, period));
        const TemporalGpuHandle fresh = temporal_gpu_begin(&r, TEMPORAL_GPU_HOOK, 11, t0);
        assert(temporal_gpu_index(fresh) == temporal_gpu_index(old) && fresh != old);
        assert(!temporal_gpu_end(&r, old, 11) && r.pair[0].state == TEMPORAL_GPU_BEGUN);
        temporal_gpu_abandon(&r, old);
        assert(r.pair[0].state == TEMPORAL_GPU_BEGUN && r.late == 2);
        assert(temporal_gpu_end(&r, fresh, 11) && r.pair[0].state == TEMPORAL_GPU_ENDED);
        assert(!temporal_gpu_end(&r, fresh, 11) && r.late == 3); /* twice is once too many */
        assert(!temporal_gpu_end(&r, -1, 11) && r.late == 3);    /* no bracket: nothing */
        temporal_gpu_abandon(&r, -1);
    }
    /* a result that is not there, either of the two, or none handed over: a normal loss, counted */
    memset(&r, 0, sizeof r);
    for (int which = 0; which < 3; which++) {
        const unsigned long long ticks[2] = {10, 20};
        const int partly[2] = {which != 0, which != 1};
        a = temporal_gpu_begin(&r, TEMPORAL_GPU_EMPTY, 10, t0);
        assert(temporal_gpu_index(a) == 0 && temporal_gpu_end(&r, a, 10));
        assert(temporal_gpu_settle(&r, 0, 10, t0 + 1000, which == 2 ? NULL : ticks, partly, 64, period));
        assert(r.lost[TEMPORAL_GPU_EMPTY][TEMPORAL_GPU_UNAVAILABLE] == (unsigned)which + 1);
        assert(r.n[TEMPORAL_GPU_EMPTY] == 0);
    }
    /* The ring exhausted: 64 brackets under way, the next has no pair and is counted. One settled, one free. */
    memset(&r, 0, sizeof r);
    for (int i = 0; i < TEMPORAL_GPU_PAIRS; i++) {
        a = temporal_gpu_begin(&r, TEMPORAL_GPU_HOOK, 30 + (unsigned)i, t0);
        assert(temporal_gpu_index(a) == i && temporal_gpu_end(&r, a, 30 + (unsigned)i));
    }
    assert(temporal_gpu_begin(&r, TEMPORAL_GPU_HOOK, 99, t0) == -1);
    assert(r.lost[TEMPORAL_GPU_HOOK][TEMPORAL_GPU_EXHAUSTED] == 1);
    assert(r.asked[TEMPORAL_GPU_HOOK] == TEMPORAL_GPU_PAIRS + 1);
    assert(temporal_gpu_settle(&r, 17, 47, t0, NULL, NULL, 64, period));
    assert(temporal_gpu_index(temporal_gpu_begin(&r, TEMPORAL_GPU_HOOK, 99, t0)) == 17);
    /* The difference of two ticks on a counter of 36 bits that went round between them, and of 64 bits. */
    assert(temporal_gpu_ticks(100, 350, 36) == 250 && temporal_gpu_ticks(100, 350, 64) == 250);
    assert(temporal_gpu_ticks((1ull << 36) - 10, 40, 36) == 50);
    assert(temporal_gpu_ticks(0xfffffffffffffff0ull, 0x10, 64) == 0x20);
    assert(temporal_gpu_ticks(5, 5, 36) == 0 && temporal_gpu_ticks(0, (1ull << 36) - 1, 36) == (1ull << 36) - 1);
    assert(temporal_gpu_ticks(7, 3, 1) == 0 && temporal_gpu_ticks(0, 1, 1) == 1);
    /* a turn of the counter: the period times two to the valid bits, finite with all 64 bits too */
    assert(temporal_gpu_turn_ns(36, 1.0) == 68719476736.0 && temporal_gpu_turn_ns(32, 52.08) == 52.08 * 4294967296.0);
    assert(temporal_gpu_turn_ns(64, 1.0) == 18446744073709551616.0 && isfinite(temporal_gpu_turn_ns(64, 1e6)));
    assert(temporal_gpu_turn_ns(64, 0.001) == 18446744073709551.616);
    /* A wrapped counter gives its time when the host saw less than half a turn pass. With 36 bits and a
       nanosecond a tick a turn is 68.7 s: at 34 s the pair is taken, at 35 s it is refused as too old, since a
       whole turn more could no longer be told apart. An age that is not known is refused too. */
    {
        const unsigned long long ticks[2] = {(1ull << 36) - 1000ull, 2000000ull};
        const unsigned long long ages[2] = {34000000000ull, 35000000000ull};
        const unsigned long long begun[4] = {t0, t0, 0, t0 + 50};
        const unsigned long long now[4] = {t0 + ages[0], t0 + ages[1], t0, t0};
        memset(&r, 0, sizeof r);
        for (int i = 0; i < 4; i++) {
            a = temporal_gpu_begin(&r, TEMPORAL_GPU_HOOK, 5, begun[i]);
            assert(temporal_gpu_index(a) == 0 && temporal_gpu_end(&r, a, 5));
            assert(temporal_gpu_settle(&r, 0, 5, now[i], ticks, there, 36, period));
        }
        assert(r.n[TEMPORAL_GPU_HOOK] == 1 && close_to((double)r.ms[TEMPORAL_GPU_HOOK][0], 2.001, 1e-6));
        assert(r.lost[TEMPORAL_GPU_HOOK][TEMPORAL_GPU_OLD] == 3 && r.lost[TEMPORAL_GPU_HOOK][TEMPORAL_GPU_ABSURD] == 0);
        /* a difference longer than the host saw pass is absurd and dropped: 2 ms of ticks in 1 ms of host time */
        a = temporal_gpu_begin(&r, TEMPORAL_GPU_HOOK, 6, t0);
        temporal_gpu_end(&r, a, 6);
        assert(temporal_gpu_settle(&r, 0, 6, t0 + 1000000ull, ticks, there, 36, period));
        assert(r.lost[TEMPORAL_GPU_HOOK][TEMPORAL_GPU_ABSURD] == 1 && r.n[TEMPORAL_GPU_HOOK] == 1);
        /* The half turn holds with all 64 bits as well. At a period of a thousandth of a nanosecond a turn is
           213 days: after 100 days of host time a pair is taken, after 107 it is too old. */
        const unsigned long long day = 86400ull * 1000000000ull;
        const unsigned long long fine[2] = {1000ull, 3000001000ull}; /* 3e9 ticks of 0.001 ns: 3 ms */
        a = temporal_gpu_begin(&r, TEMPORAL_GPU_HOOK, 7, t0);
        temporal_gpu_end(&r, a, 7);
        assert(temporal_gpu_settle(&r, 0, 7, t0 + 100 * day, fine, there, 64, 0.001));
        assert(r.n[TEMPORAL_GPU_HOOK] == 2 && close_to((double)r.ms[TEMPORAL_GPU_HOOK][1], 3.0, 1e-6));
        a = temporal_gpu_begin(&r, TEMPORAL_GPU_HOOK, 8, t0);
        temporal_gpu_end(&r, a, 8);
        assert(temporal_gpu_settle(&r, 0, 8, t0 + 107 * day, fine, there, 64, 0.001));
        assert(r.n[TEMPORAL_GPU_HOOK] == 2 && r.lost[TEMPORAL_GPU_HOOK][TEMPORAL_GPU_OLD] == 4);
        /* with 64 bits and a nanosecond a tick, a difference that wrapped the whole counter is longer than any
           host time and is dropped as absurd */
        a = temporal_gpu_begin(&r, TEMPORAL_GPU_HOOK, 9, t0);
        temporal_gpu_end(&r, a, 9);
        assert(temporal_gpu_settle(&r, 0, 9, t0 + 3600000000000ull, ticks, there, 64, period));
        assert(r.lost[TEMPORAL_GPU_HOOK][TEMPORAL_GPU_ABSURD] == 2);
        /* a period that is no number makes no time */
        a = temporal_gpu_begin(&r, TEMPORAL_GPU_HOOK, 10, t0);
        temporal_gpu_end(&r, a, 10);
        assert(temporal_gpu_settle(&r, 0, 10, t0 + 1000, ticks, there, 64, NAN) && r.n[TEMPORAL_GPU_HOOK] == 2);
    }
    /* which devices can be measured on: a turn that is no finite time is not one */
    assert(temporal_gpu_usable(1, 36, 1.0) && temporal_gpu_usable(1, 64, 52.08) && !temporal_gpu_usable(0, 36, 1.0));
    assert(temporal_gpu_usable(1, 64, 0.001) && temporal_gpu_usable(1, 1, 1.0));
    assert(!temporal_gpu_usable(1, 0, 1.0) && !temporal_gpu_usable(1, 65, 1.0) && !temporal_gpu_usable(1, 36, 0.0));
    assert(!temporal_gpu_usable(1, 36, -1.0) && !temporal_gpu_usable(1, 36, NAN));
    assert(!temporal_gpu_usable(1, 36, INFINITY) && !temporal_gpu_usable(1, 64, 1e300));
    /* Switched off: nothing begins, nothing ends or is given up, nothing recorded before is read or freed. */
    memset(&r, 0, sizeof r);
    a = temporal_gpu_begin(&r, TEMPORAL_GPU_HOOK, 3, t0);
    temporal_gpu_end(&r, a, 3);
    {
        const unsigned long long ticks[2] = {10, 20};
        open = temporal_gpu_begin(&r, TEMPORAL_GPU_NUMBERS, 3, t0);
        temporal_gpu_stop(&r);
        assert(temporal_gpu_begin(&r, TEMPORAL_GPU_HOOK, 4, t0) == -1 && r.asked[TEMPORAL_GPU_HOOK] == 1);
        assert(!temporal_gpu_end(&r, open, 3) && r.pair[1].state == TEMPORAL_GPU_BEGUN);
        temporal_gpu_abandon(&r, open);
        assert(r.pair[1].state == TEMPORAL_GPU_BEGUN && r.late == 0);
        assert(!temporal_gpu_fetch(&r, 0, 100));
        assert(!temporal_gpu_settle(&r, 0, 100, t0 + 1000, ticks, there, 64, period));
        assert(r.pair[0].state == TEMPORAL_GPU_ENDED && r.n[TEMPORAL_GPU_HOOK] == 0);
    }
    /* a kind that is none */
    memset(&r, 0, sizeof r);
    assert(temporal_gpu_begin(&r, TEMPORAL_GPU_KINDS, 1, t0) == -1 && temporal_gpu_begin(&r, -1, 1, t0) == -1);
    assert(temporal_gpu_index(-1) == -1 && !temporal_gpu_end(&r, 0, 1) && r.late == 1);
    /* the samples of a kind beyond the room for them are not kept and break nothing */
    for (int i = 0; i < TEMPORAL_GPU_SAMPLES + 5; i++) {
        const unsigned long long ticks[2] = {0, 1000000ull + (unsigned long long)i};
        a = temporal_gpu_begin(&r, TEMPORAL_GPU_EMPTY, 2, t0);
        temporal_gpu_end(&r, a, 2);
        assert(temporal_gpu_settle(&r, 0, 2, t0 + 9000000ull, ticks, there, 64, period));
    }
    assert(r.n[TEMPORAL_GPU_EMPTY] == TEMPORAL_GPU_SAMPLES && r.asked[TEMPORAL_GPU_EMPTY] == TEMPORAL_GPU_SAMPLES + 5);
    /* the summary: median, mean and 95th percentile, of an odd and an even count, of one and of none */
    {
        float five[5] = {5.0f, 1.0f, 4.0f, 2.0f, 3.0f}, four[4] = {4.0f, 1.0f, 3.0f, 2.0f}, one[1] = {0.25f};
        float hundred[100];
        assert(temporal_gpu_summary(five, 5, &median, &mean, &high) && median == 3.0 && mean == 3.0 && high == 5.0);
        assert(five[0] == 1.0f && five[4] == 5.0f);
        assert(temporal_gpu_summary(four, 4, &median, &mean, &high) && median == 2.5 && mean == 2.5 && high == 4.0);
        assert(temporal_gpu_summary(one, 1, &median, &mean, &high) && median == 0.25 && mean == 0.25 && high == 0.25);
        for (int i = 0; i < 100; i++) hundred[i] = (float)(100 - i);
        assert(temporal_gpu_summary(hundred, 100, &median, &mean, &high) && median == 50.5 && high == 95.0);
        assert(!temporal_gpu_summary(one, 0, &median, &mean, &high) && median == 0.0 && mean == 0.0 && high == 0.0);
    }
    /* A window ends, at a line or a change of configuration. Times and counts start anew and empty. A bracket
       under way keeps its pair and its handle: it is not cleared and not reused. When its own submission has
       completed it is retired without giving the new window a time, and counted as that. The audit's case: a
       sample that was pending must not turn up as "measured 1 of asked 0". */
    memset(&r, 0, sizeof r);
    {
        const unsigned long long ticks[2] = {0, 1000000ull};
        const TemporalGpuHandle before = temporal_gpu_begin(&r, TEMPORAL_GPU_HOOK, 50, t0);
        const TemporalGpuHandle open_one = temporal_gpu_begin(&r, TEMPORAL_GPU_NUMBERS, 50, t0);
        assert(temporal_gpu_end(&r, before, 50) && temporal_gpu_pending(&r, TEMPORAL_GPU_HOOK) == 1);
        assert(temporal_gpu_pending(&r, TEMPORAL_GPU_NUMBERS) == 1);
        assert(temporal_gpu_pending(&r, TEMPORAL_GPU_EMPTY) == 0);
        r.lost[TEMPORAL_GPU_HOOK][TEMPORAL_GPU_CUT] = 3;
        r.late = 2;
        temporal_gpu_restart(&r);
        assert(r.window == 1 && r.asked[TEMPORAL_GPU_HOOK] == 0);
        assert(r.lost[TEMPORAL_GPU_HOOK][TEMPORAL_GPU_CUT] == 0);
        assert(r.late == 0 && temporal_gpu_pending(&r, TEMPORAL_GPU_HOOK) == 0); /* none of this window */
        /* kept as they are */
        assert(r.pair[0].state == TEMPORAL_GPU_ENDED && r.pair[1].state == TEMPORAL_GPU_BEGUN);
        /* the pair is not free for a new bracket, which takes another */
        const TemporalGpuHandle after = temporal_gpu_begin(&r, TEMPORAL_GPU_HOOK, 51, t0);
        assert(temporal_gpu_index(after) == 2 && temporal_gpu_end(&r, after, 51));
        /* not completed yet: nothing happens. Completed: retired, no time, counted, free. */
        assert(!temporal_gpu_settle(&r, 0, 49, t0 + 5000000ull, ticks, there, 64, period));
        assert(temporal_gpu_settle(&r, 0, 50, t0 + 5000000ull, ticks, there, 64, period));
        assert(r.n[TEMPORAL_GPU_HOOK] == 0 && r.asked[TEMPORAL_GPU_HOOK] == 1);
        assert(r.lost[TEMPORAL_GPU_HOOK][TEMPORAL_GPU_OUTDATED] == 1 && r.pair[0].state == TEMPORAL_GPU_FREE);
        /* the old window's begun bracket still ends by its own handle, and is retired the same way */
        assert(temporal_gpu_end(&r, open_one, 50));
        assert(temporal_gpu_settle(&r, 1, 50, t0 + 5000000ull, ticks, there, 64, period));
        assert(r.n[TEMPORAL_GPU_NUMBERS] == 0 && r.lost[TEMPORAL_GPU_NUMBERS][TEMPORAL_GPU_OUTDATED] == 1);
        /* the new window's own bracket gives its time */
        assert(temporal_gpu_settle(&r, 2, 51, t0 + 5000000ull, ticks, there, 64, period));
        assert(r.n[TEMPORAL_GPU_HOOK] == 1 && r.asked[TEMPORAL_GPU_HOOK] == 1);
        assert(r.ms[TEMPORAL_GPU_HOOK][0] == 1.0f);
        /* a second change before an old bracket completed: still retired, once */
        const TemporalGpuHandle slow = temporal_gpu_begin(&r, TEMPORAL_GPU_EMPTY, 60, t0);
        temporal_gpu_end(&r, slow, 60);
        temporal_gpu_restart(&r);
        temporal_gpu_restart(&r);
        assert(temporal_gpu_settle(&r, temporal_gpu_index(slow), 60, t0 + 5000000ull, ticks, there, 64, period));
        assert(r.n[TEMPORAL_GPU_EMPTY] == 0 && r.lost[TEMPORAL_GPU_EMPTY][TEMPORAL_GPU_OUTDATED] == 1);
    }
    /* The window's clock is the presents, not our passes. With no run of our passes for 1200 presents the
       window is written at 600 and again at 1200, each of 600, never one of 1200 (the review's case). */
    {
        TemporalGpuWindow w;
        const unsigned first[TEMPORAL_GPU_CONFIG] = {2, 0, 31, 1, 1, 0, 1, 3840, 2160, 0, 0, 3840, 2160};
        unsigned second[TEMPORAL_GPU_CONFIG], before[TEMPORAL_GPU_CONFIG], presents = 99;
        int lines = 0;
        memset(&w, 0, sizeof w);
        memset(before, 0, sizeof before);
        assert(!temporal_gpu_tick(&w, 5000, &presents) && presents == 0); /* no window yet: nothing to write */
        assert(!temporal_gpu_configure(&w, first, 1000, before, &presents) && w.have && w.began == 1000);
        for (unsigned present = 1001; present <= 2200; present++)
            if (temporal_gpu_tick(&w, present, &presents)) {
                assert(presents == TEMPORAL_GPU_SAMPLES && (present == 1600 || present == 2200));
                lines++;
            }
        assert(lines == 2 && w.began == 2200);
        /* The tick is taken once at every present, right after the count is raised (the review's phases case:
           a tick tied to something that can come before or after the count would see 599 and then 601). Asked
           at each count in turn, a window is exactly 600, and a count asked twice ends nothing twice. */
        {
            TemporalGpuWindow phased;
            int written = 0;
            memset(&phased, 0, sizeof phased);
            temporal_gpu_configure(&phased, first, 0, before, &presents);
            for (unsigned present = 1; present <= 1800; present++) {
                if (temporal_gpu_tick(&phased, present, &presents)) {
                    assert(presents == TEMPORAL_GPU_SAMPLES && present % TEMPORAL_GPU_SAMPLES == 0);
                    written++;
                }
                assert(!temporal_gpu_tick(&phased, present, &presents)); /* the same count again */
            }
            assert(written == 3);
            /* what a tick that misses a count does, so the caller must not: it sees 601 */
            memset(&phased, 0, sizeof phased);
            temporal_gpu_configure(&phased, first, 0, before, &presents);
            assert(!temporal_gpu_tick(&phased, 599, &presents) && temporal_gpu_tick(&phased, 601, &presents));
            assert(presents == 601);
        }
        /* The same configuration again changes nothing. Another one closes the window first, with its own
           configuration and its length so far, and the bracket begun after that is the new window's first:
           it is measured there, not retired as the old one's (the review's second case). */
        assert(!temporal_gpu_configure(&w, first, 2300, before, &presents) && w.began == 2200);
        memcpy(second, first, sizeof second);
        second[11] = 2880;
        second[12] = 1620;
        memset(&r, 0, sizeof r);
        const TemporalGpuHandle old = temporal_gpu_begin(&r, TEMPORAL_GPU_HOOK, 70, t0);
        temporal_gpu_end(&r, old, 70);
        assert(temporal_gpu_configure(&w, second, 2350, before, &presents) && presents == 150);
        assert(!memcmp(before, first, sizeof first) && !memcmp(w.config, second, sizeof second) && w.began == 2350);
        temporal_gpu_restart(&r); /* what the caller does when told to close */
        const TemporalGpuHandle fresh = temporal_gpu_begin(&r, TEMPORAL_GPU_HOOK, 71, t0);
        temporal_gpu_end(&r, fresh, 71);
        const unsigned long long ticks[2] = {0, 2000000ull};
        assert(temporal_gpu_settle(&r, temporal_gpu_index(old), 71, t0 + 5000000ull, ticks, there, 64, period));
        assert(temporal_gpu_settle(&r, temporal_gpu_index(fresh), 71, t0 + 5000000ull, ticks, there, 64, period));
        assert(r.asked[TEMPORAL_GPU_HOOK] == 1 && r.n[TEMPORAL_GPU_HOOK] == 1);
        assert(r.lost[TEMPORAL_GPU_HOOK][TEMPORAL_GPU_OUTDATED] == 1);
        /* each number of the configuration counts, and the window's length restarts at a change */
        for (int k = 0; k < TEMPORAL_GPU_CONFIG; k++) {
            unsigned other[TEMPORAL_GPU_CONFIG];
            memcpy(other, w.config, sizeof other);
            other[k]++;
            assert(temporal_gpu_configure(&w, other, 2400 + (unsigned)k, before, &presents));
        }
        assert(!temporal_gpu_tick(&w, 2415 + 599, &presents) && temporal_gpu_tick(&w, 2415 + 600, &presents));
    }
    /* the counts are kept by kind: a loss of one kind is not another's */
    memset(&r, 0, sizeof r);
    a = temporal_gpu_begin(&r, TEMPORAL_GPU_NUMBERS, 5, t0);
    assert(!temporal_gpu_end(&r, a, 6) && temporal_gpu_settle(&r, 0, 5, t0, NULL, NULL, 64, period));
    assert(r.lost[TEMPORAL_GPU_NUMBERS][TEMPORAL_GPU_CUT] == 1 && r.lost[TEMPORAL_GPU_HOOK][TEMPORAL_GPU_CUT] == 0);
    assert(r.lost[TEMPORAL_GPU_EMPTY][TEMPORAL_GPU_CUT] == 0 && r.asked[TEMPORAL_GPU_NUMBERS] == 1);
}

static void test_upscaler(void) {
    const TemporalViewport whole = {0.0f, 1080.0f, 1920.0f, -1080.0f}, part = {0.0f, 810.0f, 1440.0f, -810.0f};
    float vp_now[16], inverse_now[16], vp_before[16], unused[16], texel[2];
    double now[2], before[2], vector[2];
    /* A camera that moved from (0, 0) to (0.5, 0.25), a still point at (1, 0.5) and distance 10. By hand: ndc
       (0.05, 0.025) now and (0.1, 0.05) before. In a 1920 by 1080 image whose y runs down: x 1008 now and 1056
       before, y 526.5 now and 513 before. A frame ago the point was 48 px to the right and 13.5 px further up. */
    camera(0.5f, 0.25f, 1, 1, vp_now, inverse_now);
    camera(0, 0, 1, 1, vp_before, unused);
    for (int jittered = 0; jittered < 2; jittered++) {
        /* the pixel is drawn with the jitter, and both ends of the vector are without it */
        const double jx = jittered ? 2.0 * 0.25 / 1920.0 : 0.0, jy = jittered ? 2.0 * -0.375 / 1080.0 : 0.0;
        assert(temporal_camera_points(inverse_now, vp_before, 0.05 + jx, 0.025 + jy, 0.01, jx, jy, now, before));
        motion_texel(now, before, &whole, 1920, 1080, texel);
        temporal_upscaler_vector(texel, 1920, 1080, vector);
        assert(close_to(vector[0], 48.0, 1e-3) && close_to(vector[1], -13.5, 1e-3));
    }
    /* The same in the rectangle of a scale of 0.75 (1440 by 810 of the 1920 by 1080 target): x 756 now and 792
       before, y 394.875 now and 384.75 before. So 36 px and -10.125 px, three quarters of the whole target's.
       The texel is still over the whole target's size. */
    assert(temporal_camera_points(inverse_now, vp_before, 0.05, 0.025, 0.01, 0, 0, now, before));
    motion_texel(now, before, &part, 1920, 1080, texel);
    temporal_upscaler_vector(texel, 1920, 1080, vector);
    assert(close_to(vector[0], 36.0, 1e-3) && close_to(vector[1], -10.125, 1e-3));
    assert(close_to((double)texel[0], -36.0 / 1920.0, 1e-7));
    /* An object that moves on its own, its origin from (1, 0, 20) to (2, 0.4, 20). Against a still camera at
       (0, 0): ndc (0.05, 0) then (0.1, 0.02), x 1008 then 1056, y 540 then 529.2. It was 48 px to the left and
       10.8 px lower. Against the camera that moved to (0.5, 0.25): ndc (0.075, 0.0075) now, x 1032, y 535.95.
       It was 24 px to the left and 4.05 px lower. */
    float world_now[16], world_before[16], m_now[16], m_before[16], jittered_vp[16];
    world(0, 1, 2, 0.4f, 20, world_now);
    world(0, 1, 1, 0, 20, world_before);
    for (int moving = 0; moving < 2; moving++) {
        const double jx = 2.0 * 0.25 / 1920.0, jy = 2.0 * 0.125 / 1080.0;
        camera(moving ? 0.5f : 0, moving ? 0.25f : 0, 1, 1, vp_now, inverse_now);
        assert(temporal_object_matrices(inverse_now, world_now, world_before, vp_before, jx, jy, m_now, m_before));
        /* the fragment's place as drawn: the object's origin through this frame's jittered view-projection */
        memcpy(jittered_vp, vp_now, sizeof vp_now);
        temporal_jitter_forward(jittered_vp, (float)jx, (float)jy);
        const float origin[4] = {2, 0.4f, 20, 1};
        double drawn[4], a[4], b[4];
        transform(origin, jittered_vp, drawn);
        const float clip[4] = {(float)(drawn[0] / drawn[3]), (float)(drawn[1] / drawn[3]),
                               (float)(drawn[2] / drawn[3]), 1};
        transform(clip, m_now, a);
        transform(clip, m_before, b);
        const double is[2] = {a[0] / a[3], a[1] / a[3]}, was[2] = {b[0] / b[3], b[1] / b[3]};
        motion_texel(is, was, &whole, 1920, 1080, texel);
        temporal_upscaler_vector(texel, 1920, 1080, vector);
        assert(close_to(vector[0], moving ? -24.0 : -48.0, 2e-2));
        assert(close_to(vector[1], moving ? 4.05 : 10.8, 2e-2));
    }

    /* The frame's numbers through the gate. Jitter alone over the game's viewport, whose height is negative:
       j = (0.5 / 1920, 0.75 / 1080) in clip units. The drawn place less the unjittered one is
       0.5 x j x (1920, -1080) = (0.25, -0.375) px. Up in clip is up the image, so y is negative. */
    const TemporalRectState full = {{0, 0, 1920, 1080}, whole, 1920, 1080, 1.0f, 1.0f};
    const TemporalRectState scaled = {{0, 0, 1440, 810}, part, 1920, 1080, 0.75f, 0.75f};
    const TemporalRectStamp full_stamp = {0x5508c0000ull, 1, 1920, 1080, 1920, 1080, {0, 0, 1920, 1080}, 1.0f, 1.0f};
    const TemporalRectStamp scaled_stamp = {0x5508c0000ull, 1,    1920,  1080,
                                            1920,           1080, {0, 0, 1440, 810}, 0.75f, 0.75f};
    const double jitter[2] = {0.5 / 1920.0, 0.75 / 1080.0};
    TemporalRectGate g;
    TemporalUpscalerFrame out;
    memset(&g, 0, sizeof g);
    /* a gated frame is not given to the upscaler */
    assert(!rect_go(&g, 10, &full));
    assert(!temporal_upscaler_frame(&g, 10, &full_stamp, &whole, jitter, 0, &out));
    /* the first accepted frame has the reset, the next has not, once the one before was evaluated with success */
    assert(rect_go(&g, 11, &full));
    assert(temporal_upscaler_frame(&g, 11, &full_stamp, &whole, jitter, 0, &out) && out.reset);
    assert(close_to(out.jitter[0], 0.25, 1e-12) && close_to(out.jitter[1], -0.375, 1e-12));
    {
        /* by hand as well: a place with the jitter less the same place without it */
        const double plain[2] = {0.3, -0.2}, with[2] = {0.3 + jitter[0], -0.2 + jitter[1]};
        double at[2], at_with[2];
        image_place(plain, &whole, at);
        image_place(with, &whole, at_with);
        assert(close_to(at_with[0] - at[0], out.jitter[0], 1e-9) && close_to(at_with[1] - at[1], out.jitter[1], 1e-9));
    }
    assert(out.vector_scale[0] == -1920.0 && out.vector_scale[1] == -1080.0);
    assert(out.render.w == 1920 && out.render.h == 1080 && out.out_w == 1920 && out.out_h == 1080);
    temporal_rect_wrote(&g, 11, &full_stamp);
    assert(rect_go(&g, 12, &full));
    assert(temporal_upscaler_frame(&g, 12, &full_stamp, &whole, jitter, 0, &out) && !out.reset);
    /* a camera cut resets while the frame is steady */
    assert(temporal_upscaler_frame(&g, 12, &full_stamp, &whole, jitter, 1, &out) && out.reset);
    /* an evaluation that failed wrote nothing: the frame after has the reset */
    assert(rect_go(&g, 13, &full));
    assert(temporal_upscaler_frame(&g, 13, &full_stamp, &whole, jitter, 0, &out) && out.reset);
    temporal_rect_wrote(&g, 13, &full_stamp);
    /* A resize: the scene goes into the rectangle of 0.75. The frame of the change is not evaluated, the next has
       the reset, the rectangle, a jitter sized by the rectangle and the vector scale of the whole target still. */
    assert(!rect_go(&g, 14, &scaled));
    assert(!temporal_upscaler_frame(&g, 14, &scaled_stamp, &part, jitter, 0, &out));
    assert(rect_go(&g, 15, &scaled));
    assert(temporal_upscaler_frame(&g, 15, &scaled_stamp, &part, jitter, 0, &out) && out.reset);
    assert(out.render.w == 1440 && out.render.h == 810 && out.target_w == 1920 && out.out_w == 1920);
    assert(close_to(out.jitter[0], 0.25 * 0.75, 1e-12) && close_to(out.jitter[1], -0.375 * 0.75, 1e-12));
    assert(out.vector_scale[0] == -1920.0 && out.vector_scale[1] == -1080.0);
    temporal_rect_wrote(&g, 15, &scaled_stamp);
    assert(rect_go(&g, 16, &scaled));
    assert(temporal_upscaler_frame(&g, 16, &scaled_stamp, &part, jitter, 0, &out) && !out.reset);
    /* a viewport that is not the frame's own, also the same rectangle with y the other way round, or a stamp of
       another rectangle or target size, gives nothing */
    const TemporalViewport flipped = {0.0f, 0.0f, 1440.0f, 810.0f};
    assert(!temporal_upscaler_frame(&g, 16, &scaled_stamp, &whole, jitter, 0, &out));
    assert(!temporal_upscaler_frame(&g, 16, &scaled_stamp, &flipped, jitter, 0, &out));
    assert(!temporal_upscaler_frame(&g, 16, &full_stamp, &part, jitter, 0, &out));
    TemporalRectStamp other = scaled_stamp;
    other.target_w = 3840;
    assert(!temporal_upscaler_frame(&g, 16, &other, &part, jitter, 0, &out));
    /* An output without a size, or a jitter that is not finite, gives nothing: no number that is not finite is
       ever handed on. */
    other = scaled_stamp;
    other.out_w = 0;
    assert(!temporal_upscaler_frame(&g, 16, &other, &part, jitter, 0, &out));
    other = scaled_stamp;
    other.out_h = -1080;
    assert(!temporal_upscaler_frame(&g, 16, &other, &part, jitter, 0, &out));
    const double broken[3] = {NAN, INFINITY, -INFINITY};
    for (int i = 0; i < 3; i++)
        for (int axis = 0; axis < 2; axis++) {
            double j[2] = {jitter[0], jitter[1]};
            j[axis] = broken[i];
            assert(!temporal_upscaler_frame(&g, 16, &scaled_stamp, &part, j, 0, &out));
        }
    /* The jitter in pixels is held to a pixel each way. A finite jitter in clip units that is huge would be
       infinite in pixels or too big for the float it is handed on in: refused. One pixel passes, a little more
       does not. */
    const double huge[4] = {1.7976931348623157e308, -1e300, 3.4028234663852886e38, 1e30}; /* DBL_MAX, FLT_MAX */
    for (int i = 0; i < 4; i++)
        for (int axis = 0; axis < 2; axis++) {
            double j[2] = {jitter[0], jitter[1]};
            j[axis] = huge[i];
            assert(!temporal_upscaler_frame(&g, 16, &scaled_stamp, &part, j, 0, &out));
        }
    const double one[2] = {2.0 / 1440.0, -2.0 / 810.0}, more[2] = {2.01 / 1440.0, 0.0}, tall[2] = {0.0, 2.01 / 810.0};
    assert(temporal_upscaler_frame(&g, 16, &scaled_stamp, &part, one, 0, &out));
    assert(out.jitter[0] == 1.0 && out.jitter[1] == 1.0);
    assert(!temporal_upscaler_frame(&g, 16, &scaled_stamp, &part, more, 0, &out));
    assert(!temporal_upscaler_frame(&g, 16, &scaled_stamp, &part, tall, 0, &out));
    assert(temporal_upscaler_frame(&g, 16, &scaled_stamp, &part, jitter, 0, &out));
    assert(fabs(out.jitter[0]) <= 1.0 && fabs(out.jitter[1]) <= 1.0 && isfinite(out.vector_scale[0]));
    assert(isfinite((float)out.jitter[0]) && isfinite((float)out.jitter[1]) && isfinite((float)out.vector_scale[1]));
    /* A huge viewport, put into the gate's state by hand since no structure test would let it in: it is no
       rectangle of the target, so nothing is given, whatever the jitter. */
    {
        const TemporalViewport vast = {0.0f, 3e9f, 3e9f, -3e9f};
        const TemporalRectState vast_state = {{0, 0, 1440, 810}, vast, 1920, 1080, 0.75f, 0.75f};
        const double tiny[2] = {1e-12, 1e-12};
        TemporalRectGate h;
        memset(&h, 0, sizeof h);
        rect_go(&h, 30, &vast_state);
        assert(rect_go(&h, 31, &vast_state) && temporal_rect_steady(&h, 31));
        assert(!temporal_upscaler_frame(&h, 31, &scaled_stamp, &vast, tiny, 0, &out));
        assert(!temporal_upscaler_frame(&h, 31, &scaled_stamp, &vast, jitter, 0, &out));
    }
    /* a history made for another output size is not used: reset */
    other = scaled_stamp;
    other.out_w = 2560;
    assert(temporal_upscaler_frame(&g, 16, &other, &part, jitter, 0, &out) && out.reset && out.out_w == 2560);
    /* A rectangle the game does not make: 320 by 180 at 40,30 of images of 400 by 240, with either sign of the
       height. The game's rules refuse it at every step, and stay as they are. The conversion alone takes it, so
       a test of an adapter can hand it on without the gate. */
    for (int up = 0; up < 2; up++) {
        const TemporalViewport crop = {40.0f, up ? 210.0f : 30.0f, 320.0f, up ? -180.0f : 180.0f};
        const TemporalRectStamp crop_stamp = {1, 1, 400, 240, 640, 360, {40, 30, 320, 180}, 0.8f, 0.75f};
        const TemporalSceneBlock crop_block = {1, 1, 0.8f, 0.75f};
        const TemporalRectState by_hand = {{40, 30, 320, 180}, crop, 400, 240, 0.8f, 0.75f};
        const double crop_jitter[2] = {0.5 / 320.0, 0.75 / 180.0};
        TemporalRectState crop_state;
        TemporalRectGate h;
        TemporalUpscalerFrame numbers;
        assert(temporal_rect_geometry(&crop, 400, 240, TEMPORAL_CORNER_TOP_LEFT, &crop_state) == TEMPORAL_RECT_SHAPE);
        assert(temporal_rect_fraction(&by_hand, 0.8f, 0.75f) == TEMPORAL_RECT_UNEQUAL);
        memset(&h, 0, sizeof h);
        for (unsigned frame = 50; frame < 54; frame++) {
            temporal_rect_decide(&h, frame, &by_hand, 0);
            assert(temporal_rect_confirm(&h, frame, NULL, &crop_block) == TEMPORAL_FRAME_NONE && !h.have_candidate);
            assert(!temporal_upscaler_frame(&h, frame, &crop_stamp, &crop, crop_jitter, 0, &numbers));
        }
        for (int reset = 0; reset < 2; reset++) {
            assert(temporal_upscaler_numbers(&crop_stamp, &crop, crop_jitter, reset, &numbers));
            assert(numbers.render.x == 40 && numbers.render.y == 30 && numbers.render.w == 320);
            assert(numbers.render.h == 180 && numbers.target_w == 400 && numbers.target_h == 240);
            assert(numbers.out_w == 640 && numbers.out_h == 360 && numbers.reset == reset);
            assert(close_to(numbers.jitter[0], 0.25, 1e-9) && close_to(numbers.jitter[1], up ? -0.375 : 0.375, 1e-9));
            assert(numbers.vector_scale[0] == -400.0 && numbers.vector_scale[1] == -240.0);
        }
        /* A target without a size, down to the lowest number there is, on either axis: refused before anything is
           worked out from it, by the rectangle's rule and by the conversion, and what was there stays. */
        const int sizes[4] = {0, -1, -400, (-2147483647 - 1)};
        const TemporalUpscalerFrame kept = numbers;
        for (int i = 0; i < 4; i++)
            for (int axis = 0; axis < 2; axis++) {
                TemporalRectStamp sizeless = crop_stamp;
                TemporalRect r = {7, 7, 7, 7};
                *(axis ? &sizeless.target_h : &sizeless.target_w) = sizes[i];
                assert(!temporal_rect_from_viewport(&crop, sizeless.target_w, sizeless.target_h, &r));
                assert(r.x == 7 && r.y == 7 && r.w == 7 && r.h == 7);
                assert(!temporal_upscaler_numbers(&sizeless, &crop, crop_jitter, 0, &numbers));
                assert(!memcmp(&numbers, &kept, sizeof kept));
                sizeless = crop_stamp;
                *(axis ? &sizeless.out_h : &sizeless.out_w) = sizes[i];
                assert(!temporal_upscaler_numbers(&sizeless, &crop, crop_jitter, 0, &numbers));
                assert(!memcmp(&numbers, &kept, sizeof kept));
            }
        /* the conversion's own refusals: a viewport that is not the stamp's rectangle, a jitter over a pixel */
        const TemporalViewport elsewhere = {41.0f, crop.y, 320.0f, crop.height};
        const double far_jitter[2] = {4.0 / 320.0, 0.0};
        assert(!temporal_upscaler_numbers(&crop_stamp, &elsewhere, crop_jitter, 0, &numbers));
        assert(!temporal_upscaler_numbers(&crop_stamp, &crop, far_jitter, 0, &numbers));
    }
    /* a stamp of another fraction than the frame's is not the frame's: nothing is given */
    other = scaled_stamp;
    other.fx = other.fy = 0.7501f;
    assert(!temporal_upscaler_frame(&g, 16, &other, &part, jitter, 0, &out));
    /* a late disagreement after the numbers were made: the frame is no longer steady */
    assert(temporal_rect_late(&g, 16, &full, NULL, NULL));
    assert(!temporal_upscaler_frame(&g, 16, &scaled_stamp, &part, jitter, 0, &out));

    /* The contexts. One is made for one set of sizes and may be used only when the submission of its preparation
       has completed. A change of any size is a new context, and until that one is published the frames of the
       new size are not evaluated, while the old one stays the published one. */
    {
        const TemporalUpscalerConfig at_full = {1920, 1080, 1920, 1080, 1920, 1080, 7};
        const TemporalUpscalerConfig at_part = {1920, 1080, 1440, 810, 1920, 1080, 7};
        TemporalUpscalerConfig at_other = at_part;
        at_other.out_h = 2160;
        TemporalUpscalerContexts c = {0};
        assert(!temporal_upscaler_ready(&c, &at_full) && temporal_upscaler_must_create(&c, &at_full));
        assert(temporal_upscaler_completed(&c, &at_full) == TEMPORAL_UPSCALER_NOTHING);
        temporal_upscaler_created(&c, &at_full, 1);
        /* submitted, not completed: not ready, and no second one is made, for these sizes or others */
        assert(!temporal_upscaler_ready(&c, &at_full) && !temporal_upscaler_must_create(&c, &at_full));
        assert(!temporal_upscaler_must_create(&c, &at_part));
        assert(temporal_upscaler_completed(&c, &at_full) == TEMPORAL_UPSCALER_PUBLISHED);
        assert(temporal_upscaler_ready(&c, &at_full) && !temporal_upscaler_must_create(&c, &at_full));
        assert(temporal_upscaler_completed(&c, &at_full) == TEMPORAL_UPSCALER_NOTHING);
        /* A change of size. The frames of the new size are not ready, a new context is made, and while it is
           on its way the old one is still the published one. When it completes the old one is retired. */
        assert(!temporal_upscaler_ready(&c, &at_part) && temporal_upscaler_must_create(&c, &at_part));
        temporal_upscaler_created(&c, &at_part, 1);
        assert(!temporal_upscaler_ready(&c, &at_part) && temporal_upscaler_ready(&c, &at_full));
        assert(!temporal_upscaler_must_create(&c, &at_part) && !temporal_upscaler_must_create(&c, &at_other));
        assert(temporal_upscaler_completed(&c, &at_part) == TEMPORAL_UPSCALER_RETIRE);
        assert(temporal_upscaler_ready(&c, &at_part) && !temporal_upscaler_ready(&c, &at_full));
        /* The sizes move on while a context is on its way. When it completes it is stale: never published, never
           evaluated, and the published one stays. Then the context for what the frames are now is made. */
        {
            TemporalUpscalerContexts moved = c; /* the context for the rectangle is published */
            temporal_upscaler_created(&moved, &at_full, 1);
            assert(!temporal_upscaler_must_create(&moved, &at_other)); /* one on its way at a time */
            /* While that one is still on its way: a frame of the new sizes is not evaluated, not on the pending
               context and not on the published one, which is for other sizes. The published one still serves
               its own. */
            assert(!temporal_upscaler_ready(&moved, &at_other) && !temporal_upscaler_ready(&moved, &at_full));
            assert(temporal_upscaler_ready(&moved, &at_part));
            assert(temporal_upscaler_completed(&moved, &at_other) == TEMPORAL_UPSCALER_STALE);
            assert(!temporal_upscaler_ready(&moved, &at_full) && !temporal_upscaler_ready(&moved, &at_other));
            assert(temporal_upscaler_ready(&moved, &at_part) && temporal_upscaler_must_create(&moved, &at_other));
            temporal_upscaler_created(&moved, &at_other, 1);
            assert(temporal_upscaler_completed(&moved, &at_other) == TEMPORAL_UPSCALER_RETIRE);
            assert(temporal_upscaler_ready(&moved, &at_other) && !temporal_upscaler_ready(&moved, &at_part));
            /* back to the published sizes before the pending one completes: stale again, and nothing to make */
            moved = c;
            temporal_upscaler_created(&moved, &at_full, 1);
            assert(temporal_upscaler_completed(&moved, &at_part) == TEMPORAL_UPSCALER_STALE);
            assert(temporal_upscaler_ready(&moved, &at_part) && !temporal_upscaler_must_create(&moved, &at_part));
            /* The configuration is more than the sizes. The same sizes with another format, other properties,
               another library or device (the caller's generation) are another configuration: the published
               context does not serve it, and a pending one of the old generation is stale. */
            TemporalUpscalerConfig other_rest = at_part;
            other_rest.generation = 8;
            moved = c;
            assert(temporal_upscaler_ready(&moved, &at_part) && !temporal_upscaler_ready(&moved, &other_rest));
            assert(temporal_upscaler_must_create(&moved, &other_rest));
            TemporalUpscalerConfig full_rest = at_full;
            full_rest.generation = 8;
            temporal_upscaler_created(&moved, &at_full, 1);
            assert(temporal_upscaler_completed(&moved, &full_rest) == TEMPORAL_UPSCALER_STALE);
            assert(!temporal_upscaler_ready(&moved, &full_rest) && temporal_upscaler_must_create(&moved, &full_rest));
            /* A stop (the upscaler turned off, or started again) while one context is published and one is on
               its way. The published one is retired. The pending one is still owed its completion: nothing is
               made before that, and it then comes out stale, also for its own configuration, and is destroyed. */
            moved = c;
            temporal_upscaler_created(&moved, &at_full, 1);
            assert(temporal_upscaler_stop(&moved) == 1);
            assert(!temporal_upscaler_ready(&moved, &at_part) && !temporal_upscaler_ready(&moved, &at_full));
            assert(!temporal_upscaler_must_create(&moved, &at_full));
            assert(!temporal_upscaler_must_create(&moved, &at_part));
            assert(temporal_upscaler_completed(&moved, &at_full) == TEMPORAL_UPSCALER_STALE && !moved.live);
            assert(temporal_upscaler_must_create(&moved, &at_full));
            assert(temporal_upscaler_completed(&moved, &at_full) == TEMPORAL_UPSCALER_NOTHING);
            /* a stop with nothing published and a failed context held: the held one is still released */
            TemporalUpscalerContexts held = {0};
            temporal_upscaler_created(&held, &at_full, 0);
            assert(temporal_upscaler_stop(&held) == 0 && !temporal_upscaler_must_create(&held, &at_full));
            assert(temporal_upscaler_completed(&held, &at_full) == TEMPORAL_UPSCALER_RELEASE);
            assert(temporal_upscaler_must_create(&held, &at_full) && temporal_upscaler_stop(&held) == 0);
            /* A camera cut is not a stop. It sets the frame's reset flag and nothing else: the published context
               is still ready and nothing is to be made. */
            TemporalRectGate cut;
            memset(&cut, 0, sizeof cut);
            rect_go(&cut, 40, &scaled);
            assert(rect_go(&cut, 41, &scaled));
            moved = c;
            assert(temporal_upscaler_frame(&cut, 41, &scaled_stamp, &part, jitter, 1, &out) && out.reset);
            assert(temporal_upscaler_ready(&moved, &at_part) && !temporal_upscaler_must_create(&moved, &at_part));
            /* the very first context, stale: nothing is published at all */
            TemporalUpscalerContexts none = {0};
            temporal_upscaler_created(&none, &at_full, 1);
            assert(temporal_upscaler_completed(&none, &at_part) == TEMPORAL_UPSCALER_STALE && !none.live);
            assert(temporal_upscaler_must_create(&none, &at_part));
        }
        /* each size counts: another output height is another context */
        assert(!temporal_upscaler_ready(&c, &at_other) && temporal_upscaler_must_create(&c, &at_other));
        /* A prepare that fails publishes nothing. It counts as a failure like an evaluation's. The failed context
           is held, and no new one is made, until its command buffer is gone: then it may be freed, the published
           one is as it was, and the next frame tries again. */
        unsigned failed = 0;
        temporal_upscaler_created(&c, &at_other, 0);
        assert(!temporal_upscaler_failed(&failed, 0) && failed == 1);
        assert(!temporal_upscaler_ready(&c, &at_other) && !temporal_upscaler_must_create(&c, &at_other));
        assert(temporal_upscaler_ready(&c, &at_part));
        assert(temporal_upscaler_completed(&c, &at_other) == TEMPORAL_UPSCALER_RELEASE);
        assert(temporal_upscaler_ready(&c, &at_part) && temporal_upscaler_must_create(&c, &at_other));
        /* With the gate: frames 20 and 21 are steady while the first context is on its way and are not
           evaluated. Frame 22 is the first evaluated one and has the reset, frame 23 has not. */
        TemporalUpscalerContexts contexts = {0};
        memset(&g, 0, sizeof g);
        rect_go(&g, 19, &full);
        for (unsigned frame = 20; frame < 24; frame++) {
            assert(rect_go(&g, frame, &full));
            assert(temporal_upscaler_frame(&g, frame, &full_stamp, &whole, jitter, 0, &out));
            const TemporalUpscalerConfig sizes = {out.target_w, out.target_h, out.render.w, out.render.h, out.out_w,
                                                  out.out_h, 7};
            if (temporal_upscaler_must_create(&contexts, &sizes)) temporal_upscaler_created(&contexts, &sizes, 1);
            const int ready = temporal_upscaler_ready(&contexts, &sizes);
            assert(ready == (frame >= 22));
            /* the submission made in frame 20 completes after frame 21 was looked at */
            if (frame == 21) assert(temporal_upscaler_completed(&contexts, &sizes) == TEMPORAL_UPSCALER_PUBLISHED);
            if (!ready) continue;
            assert(out.reset == (frame == 22));
            temporal_rect_wrote(&g, frame, &full_stamp);
        }
    }
    /* The camera's numbers and the frame time before a dispatch. A camera as the game has it: near 0.1, far at
       infinity or at 10000, 50 degrees, a frame of 1/60 s. What comes out are the floats that are handed on. */
    {
        float cam[4] = {0, 0, 0, 0};
        const double fov = 50.0 * 3.14159265358979323846 / 180.0, frame = 1.0 / 60.0, pi = 3.14159265358979323846;
        assert(temporal_upscaler_camera(0.1, 10000.0, fov, frame, 0, cam));
        assert(cam[0] == 0.1f && cam[1] == 10000.0f && cam[2] == (float)fov && cam[3] == (float)frame);
        assert(fabsf(cam[3] * 1000.0f - 16.666666f) < 1e-4f);
        assert(temporal_upscaler_camera(0.1, INFINITY, fov, frame, 1, cam) && isinf(cam[1]) && cam[1] > 0.0f);
        /* an infinite far plane only with a context made for one, and such a context only with it */
        assert(!temporal_upscaler_camera(0.1, INFINITY, fov, frame, 0, cam));
        assert(!temporal_upscaler_camera(0.1, 10000.0, fov, frame, 1, cam));
        assert(!temporal_upscaler_camera(0.1, -INFINITY, fov, frame, 1, cam));
        /* far must lie beyond near */
        assert(!temporal_upscaler_camera(0.1, 0.1, fov, frame, 0, cam));
        assert(!temporal_upscaler_camera(0.1, 0.05, fov, frame, 0, cam));
        assert(!temporal_upscaler_camera(0.1, -5.0, fov, frame, 0, cam));
        /* The field of view: positive and below pi. Under pi by enough to stay under as a float passes. Pi does
           not, and neither does a double just under pi whose float is above it. */
        assert(temporal_upscaler_camera(0.1, 10000.0, 3.1415925, frame, 0, cam) && (double)cam[2] < pi);
        assert(!temporal_upscaler_camera(0.1, 10000.0, pi, frame, 0, cam));
        assert(!temporal_upscaler_camera(0.1, 10000.0, pi - 1e-9, frame, 0, cam));
        assert(!temporal_upscaler_camera(0.1, 10000.0, 4.0, frame, 0, cam));
        assert(!temporal_upscaler_camera(0.1, 10000.0, 0.0, frame, 0, cam));
        assert(!temporal_upscaler_camera(0.1, 10000.0, -fov, frame, 0, cam));
        /* zero, negative, not finite, or too big for a float, in each of the four numbers */
        const double bad[7] = {0.0, -1.0, NAN, INFINITY, -INFINITY, 1e39, 1.7976931348623157e308};
        for (int i = 0; i < 7; i++) {
            assert(!temporal_upscaler_camera(bad[i], 10000.0, fov, frame, 0, cam));
            assert(!temporal_upscaler_camera(0.1, bad[i], fov, frame, 0, cam));
            assert(!temporal_upscaler_camera(0.1, 10000.0, bad[i], frame, 0, cam));
            assert(!temporal_upscaler_camera(0.1, 10000.0, fov, bad[i], 0, cam));
        }
        /* Doubles that pass as doubles and do not as the floats handed on: a near plane that rounds to 0, a far
           plane that rounds onto the near one, a frame time that rounds to no time, in seconds or after the
           change to milliseconds. And one that is too long in milliseconds. */
        assert(!temporal_upscaler_camera(1e-50, 10000.0, fov, frame, 0, cam));
        assert(!temporal_upscaler_camera(1.0, 1.0 + 1e-9, fov, frame, 0, cam));
        assert(!temporal_upscaler_camera(0.1, 10000.0, fov, 1e-46, 0, cam));
        assert(!temporal_upscaler_camera(0.1, 10000.0, fov, 1e-50, 0, cam));
        assert(!temporal_upscaler_camera(0.1, 10000.0, fov, 1e36, 0, cam));
        assert(temporal_upscaler_camera(0.1, 10000.0, fov, 5.0, 0, cam) && cam[3] * 1000.0f == 5000.0f);
        /* a refusal leaves what was there */
        cam[0] = cam[1] = cam[2] = cam[3] = -7.0f;
        assert(!temporal_upscaler_camera(1e-50, 10000.0, fov, frame, 0, cam) && cam[0] == -7.0f && cam[3] == -7.0f);
    }
    /* failures in a row: the eighth turns the upscaler off, a success before that starts the count again */
    unsigned failures = 0;
    for (int i = 0; i < 7; i++) assert(!temporal_upscaler_failed(&failures, 0));
    assert(!temporal_upscaler_failed(&failures, 1) && failures == 0);
    for (int i = 0; i < 7; i++) assert(!temporal_upscaler_failed(&failures, 0));
    assert(temporal_upscaler_failed(&failures, 0) && temporal_upscaler_failed(&failures, 0));
    assert(failures == TEMPORAL_UPSCALER_FAILURES);
}

/* The upscaler self-test's oracle (upscaler_selftest.h), checked against places and values worked out by hand. It
   shares nothing with the conversion it is there to judge. */
static void st_colour(const SelftestFrame* f, const SelftestViewport* v, int x, int y, double rgb[3]) {
    double depth, texel[2];
    selftest_input(f, v, 640, 360, x, y, rgb, &depth, texel);
}
/* the edge picture's reference at the output's size, as three doubles a pixel */
#define EDGE_W 480
#define EDGE_H 270
static double* edge_reference(int up) {
    SelftestFrame f;
    double* rgb = malloc(sizeof(double) * 3 * EDGE_W * EDGE_H);
    memset(&f, 0, sizeof f);
    f.pattern = SELFTEST_PATTERN_EDGES;
    f.out = (SelftestViewport){0, up ? EDGE_H : 0, EDGE_W, up ? -EDGE_H : EDGE_H};
    assert(rgb);
    for (int y = 0; y < EDGE_H; y++)
        for (int x = 0; x < EDGE_W; x++) selftest_reference(&f, &f.out, x, y, 8, &rgb[3 * (y * EDGE_W + x)]);
    return rgb;
}
static void edge_set(double* rgb, int x, int y, double value) {
    for (int c = 0; c < 3; c++) rgb[3 * (y * EDGE_W + x) + c] = value;
}

/* The jitter-sign case's picture, regions, profile and measures. */
static void test_selftest_edges(void) {
    const SelftestEdge *v = &SELFTEST_EDGE_V, *h = &SELFTEST_EDGE_H;
    const size_t bytes = sizeof(double) * 3 * EDGE_W * EDGE_H;
    /* the picture: 0.5 outside the regions, 0.25 before each edge and 0.75 after it */
    assert(selftest_edge_picture(10.0, 10.0) == 0.5 && selftest_edge_picture(59.9, 88.0) == 0.5);
    assert(selftest_edge_picture(156.0, 88.0) == 0.5 && selftest_edge_picture(100.0, 39.9) == 0.5);
    assert(selftest_edge_picture(108.3, 88.0) == 0.25 && selftest_edge_picture(108.4, 88.0) == 0.75);
    assert(selftest_edge_picture(109.3, 98.0) == 0.25 && selftest_edge_picture(109.4, 98.0) == 0.75);
    assert(selftest_edge_picture(248.0, 198.3) == 0.25 && selftest_edge_picture(248.0, 198.4) == 0.75);
    assert(selftest_edge_picture(238.0, 197.3) == 0.25 && selftest_edge_picture(238.0, 197.4) == 0.75);
    /* the distance along the normal: nothing on the edge, a pixel across is a pixel less the slope's share */
    assert(close_to(selftest_edge_distance(v, 108.37, 88.0), 0.0, 1e-12));
    assert(close_to(selftest_edge_distance(v, 109.37, 88.0), 1.0 / sqrt(1.01), 1e-12));
    assert(close_to(selftest_edge_distance(v, 108.37, 98.0), -1.0 / sqrt(1.01), 1e-12));
    assert(close_to(selftest_edge_distance(h, 248.0, 196.37), -2.0 / sqrt(1.01), 1e-12));
    /* the pixels a profile uses: 4 pixels inside the region and within 4 pixels of the edge */
    assert(selftest_edge_bin(v, 108, 88) == 16 && selftest_edge_bin(v, 107, 88) == 12);
    assert(selftest_edge_bin(v, 104, 88) == 0 && selftest_edge_bin(v, 103, 88) == -1);
    assert(selftest_edge_bin(v, 111, 88) == 28 && selftest_edge_bin(v, 112, 88) == -1);
    assert(selftest_edge_bin(v, 108, 43) == -1 && selftest_edge_bin(v, 104, 44) >= 0);
    assert(selftest_edge_bin(v, 113, 131) >= 0 && selftest_edge_bin(v, 113, 132) == -1);
    assert(selftest_edge_bin(v, 248, 198) == -1 && selftest_edge_bin(h, 248, 198) == 16);
    assert(selftest_edge_bin(h, 203, 194) == -1 && selftest_edge_bin(h, 204, 194) >= 0);

    /* The reference's own profile, the same picture for either sign of the viewport's height: every bin holds
       pixels, it is its own match, the edge is where it was put, a pixel wide and without overshoot. */
    double* ref = edge_reference(0);
    double* other = edge_reference(1);
    double* copy = malloc(bytes);
    assert(copy);
    for (int i = 0; i < 3 * EDGE_W * EDGE_H; i++) assert(close_to(ref[i], other[i], 1e-9));
    for (int which = 0; which < 2; which++) {
        const SelftestEdge* e = which ? h : v;
        SelftestProfile p;
        selftest_profile_make(e, ref, EDGE_W, &p);
        assert(selftest_profile_whole(&p));
        for (int i = 0; i < SELFTEST_EDGE_BINS; i++) assert(p.n[i] >= 16);
        assert(selftest_profile_apart(&p, &p) == 0.0 && selftest_edge_apart(e, ref, ref, EDGE_W) == 0.0);
        assert(close_to(selftest_profile_mean(&p, 0), 0.25, 1e-12));
        assert(close_to(selftest_profile_mean(&p, 31), 0.75, 1e-12));
        assert(close_to(selftest_profile_crossing(&p, 0.5), 0.0, 0.1));
        assert(selftest_profile_rise(&p) > 0.5 && selftest_profile_rise(&p) < 1.2);
        assert(selftest_profile_overshoot(&p) < 1e-12);
    }
    /* A picture moved half a pixel in x: across the near-vertical edge the profile differs by the step times the
       move over the window, 0.5 x 0.5 / 8, and the edge is seen half a pixel on. Along the other edge a twentieth
       of that. Nothing is fitted, so a moved edge is an error. */
    SelftestProfile pv, ph, rv, rh;
    selftest_profile_make(v, ref, EDGE_W, &rv);
    selftest_profile_make(h, ref, EDGE_W, &rh);
    {
        SelftestFrame f;
        memset(&f, 0, sizeof f);
        f.pattern = SELFTEST_PATTERN_EDGES;
        f.out = (SelftestViewport){0, 0, EDGE_W, EDGE_H};
        f.shift[0] = 2.0 * 0.5 / EDGE_W;
        for (int y = 0; y < EDGE_H; y++)
            for (int x = 0; x < EDGE_W; x++) selftest_reference(&f, &f.out, x, y, 8, &copy[3 * (y * EDGE_W + x)]);
    }
    selftest_profile_make(v, copy, EDGE_W, &pv);
    selftest_profile_make(h, copy, EDGE_W, &ph);
    assert(close_to(selftest_profile_apart(&pv, &rv), 0.5 * 0.5 / 8.0, 0.004));
    assert(close_to(selftest_profile_crossing(&pv, 0.5) - selftest_profile_crossing(&rv, 0.5), 0.5, 0.1));
    assert(selftest_profile_apart(&ph, &rh) < 0.004);
    assert(selftest_edge_apart(v, copy, ref, EDGE_W) > 0.02 && selftest_edge_apart(h, copy, ref, EDGE_W) < 0.004);
    /* A picture spread over three pixels in x, as samples put back in the wrong place in x would be: the
       near-vertical edge is wider and differs, the near-horizontal one hardly. */
    for (int y = 0; y < EDGE_H; y++)
        for (int x = 0; x < EDGE_W; x++) {
            const int l = x > 0 ? x - 1 : x, r = x < EDGE_W - 1 ? x + 1 : x;
            edge_set(copy, x, y, (selftest_grey(ref, EDGE_W, l, y) + selftest_grey(ref, EDGE_W, x, y) +
                                  selftest_grey(ref, EDGE_W, r, y)) / 3.0);
        }
    selftest_profile_make(v, copy, EDGE_W, &pv);
    selftest_profile_make(h, copy, EDGE_W, &ph);
    assert(selftest_profile_apart(&pv, &rv) > 0.008 && selftest_profile_apart(&ph, &rh) < 0.002);
    assert(selftest_profile_rise(&pv) > selftest_profile_rise(&rv) + 0.8);
    assert(selftest_profile_rise(&ph) < selftest_profile_rise(&rh) + 0.2);
    /* Deviations of opposite sign from pixel to pixel cancel in a bin and not in the measure over the pixels. */
    memcpy(copy, ref, bytes);
    for (int y = 0; y < EDGE_H; y++)
        for (int x = 0; x < EDGE_W; x++)
            edge_set(copy, x, y, selftest_grey(ref, EDGE_W, x, y) + (((x + y) & 1) ? 0.1 : -0.1));
    selftest_profile_make(v, copy, EDGE_W, &pv);
    assert(close_to(selftest_edge_apart(v, copy, ref, EDGE_W), 0.1, 1e-9));
    assert(selftest_profile_apart(&pv, &rv) < 0.04);
    /* A colour fringe, one channel up and another down by the same: the grey value and so the profile do not see
       it, the measure over the pixels does, as two thirds of it. One channel alone counts a third. */
    memcpy(copy, ref, bytes);
    for (int i = 0; i < EDGE_W * EDGE_H; i++) {
        copy[3 * i] += 0.1;
        copy[3 * i + 2] -= 0.1;
    }
    selftest_profile_make(v, copy, EDGE_W, &pv);
    assert(selftest_profile_apart(&pv, &rv) < 1e-12);
    assert(close_to(selftest_edge_apart(v, copy, ref, EDGE_W), 0.2 / 3.0, 1e-12));
    assert(close_to(selftest_edge_apart(h, ref, copy, EDGE_W), 0.2 / 3.0, 1e-12));
    /* by channel, for the log: the fringe is 0.1 in red, nothing in green, 0.1 in blue, and their mean is the
       measure */
    {
        double by[3];
        selftest_edge_channels(v, copy, ref, EDGE_W, by);
        assert(close_to(by[0], 0.1, 1e-12) && close_to(by[1], 0.0, 1e-12) && close_to(by[2], 0.1, 1e-12));
        assert(close_to((by[0] + by[1] + by[2]) / 3.0, selftest_edge_apart(v, copy, ref, EDGE_W), 1e-12));
        selftest_edge_channels(h, ref, ref, EDGE_W, by);
        assert(by[0] == 0.0 && by[1] == 0.0 && by[2] == 0.0);
    }
    /* the audit's case: plus and minus an eighth in two channels is a twelfth over the three */
    memcpy(copy, ref, bytes);
    for (int i = 0; i < EDGE_W * EDGE_H; i++) {
        copy[3 * i] += 0.125;
        copy[3 * i + 1] -= 0.125;
    }
    assert(close_to(selftest_edge_apart(v, copy, ref, EDGE_W), 0.25 / 3.0, 1e-12));
    assert(close_to(selftest_edge_apart(h, copy, ref, EDGE_W), 0.25 / 3.0, 1e-12));
    memcpy(copy, ref, bytes);
    for (int i = 0; i < EDGE_W * EDGE_H; i++) copy[3 * i + 1] += 0.03;
    assert(close_to(selftest_edge_apart(v, copy, ref, EDGE_W), 0.01, 1e-12));
    /* an overshoot beside the edge, and a loss of contrast */
    memcpy(copy, ref, bytes);
    for (int y = v->y0; y < v->y1; y++)
        for (int x = v->x0; x < v->x1; x++) {
            const int bin = selftest_edge_bin(v, x, y);
            if (bin >= 20 && bin < 24) edge_set(copy, x, y, 0.85);
        }
    selftest_profile_make(v, copy, EDGE_W, &pv);
    assert(close_to(selftest_profile_overshoot(&pv), 0.1, 1e-9) && selftest_profile_apart(&pv, &rv) > 0.01);
    for (int i = 0; i < 3 * EDGE_W * EDGE_H; i++) copy[i] = 0.5 + 0.5 * (ref[i] - 0.5);
    selftest_profile_make(v, copy, EDGE_W, &pv);
    assert(selftest_profile_apart(&pv, &rv) > 0.1 && isnan(selftest_profile_rise(&pv)));
    /* a profile with a bin that holds nothing, or a value that is not finite, is compared with nothing */
    pv = rv;
    pv.n[5] = 0;
    assert(!selftest_profile_whole(&pv) && isnan(selftest_profile_apart(&pv, &rv)));
    assert(isnan(selftest_profile_apart(&rv, &pv)));
    pv = rv;
    pv.sum[31] = INFINITY;
    assert(!selftest_profile_whole(&pv) && isnan(selftest_profile_apart(&pv, &rv)));
    memcpy(copy, ref, bytes);
    edge_set(copy, 108, 88, NAN);
    assert(isnan(selftest_edge_apart(v, copy, ref, EDGE_W)));
    selftest_profile_make(v, copy, EDGE_W, &pv);
    assert(!selftest_profile_whole(&pv));
    /* The verdict, for one orientation. A set of runs as the blur model has them: configured 0.012, told
       nothing 0.019, a turned sign 0.035 on its own edge, both turned 0.036, one frame scaled plainly 0.030, the
       other axis's sign leaving an edge as it is, and a noise far below the margins. */
    {
        const SelftestEdgeMeasure own = {0.012, 0.016}, told = {0.019, 0.024}, turned = {0.035, 0.040};
        const SelftestEdgeMeasure both = {0.036, 0.041}, scaled = {0.030, 0.034}, quiet = {0.0002, 0.0003};
        const SelftestEdgeRuns fine = {{own, own}, {turned, own}, {own, turned}, {both, both}, {told, told},
                                       {scaled, scaled}, {quiet, quiet}, 1};
        SelftestEdgeRuns r = fine;
        assert(selftest_edge_judge(&fine) == 0 && !selftest_edge_undecided(0));
        /* the sign of the other axis is not asked of an edge: it may even be better there */
        r.turned_x[1] = (SelftestEdgeMeasure){0.001, 0.001};
        r.turned_y[0] = (SelftestEdgeMeasure){0.001, 0.001};
        assert(selftest_edge_judge(&r) == 0);
        for (int axis = 0; axis < 2; axis++) {
            SelftestEdgeMeasure* sign = axis ? &r.turned_y[axis] : &r.turned_x[axis];
            /* the order: told nothing above the turned sign, or level with the configured run */
            r = fine;
            r.told_nothing[axis].profile = 0.0355;
            assert(selftest_edge_judge(&r) == SELFTEST_EDGE_ORDER);
            /* a turned sign under its budget, on the profile and over the pixels: each alone fails */
            r = fine;
            sign->profile = 0.0219;
            assert(selftest_edge_judge(&r) == SELFTEST_EDGE_MARGIN);
            sign->profile = 0.0221;
            assert(selftest_edge_judge(&r) == 0);
            r = fine;
            sign->pixels = 0.0259;
            assert(selftest_edge_judge(&r) == SELFTEST_EDGE_MARGIN);
            /* told nothing under its budget: the case has shown no sensitivity and decides nothing */
            r = fine;
            r.told_nothing[axis].profile = 0.0149;
            assert(selftest_edge_judge(&r) == SELFTEST_EDGE_BLIND && selftest_edge_undecided(SELFTEST_EDGE_BLIND));
            r = fine;
            r.told_nothing[axis].pixels = 0.0189;
            assert(selftest_edge_judge(&r) == SELFTEST_EDGE_BLIND);
            r = fine;
            r.told_nothing[axis].profile = 0.012; /* level with the configured run: the order and the noise too */
            assert(selftest_edge_judge(&r) == (SELFTEST_EDGE_BLIND | SELFTEST_EDGE_ORDER | SELFTEST_EDGE_NOISY));
            /* both signs turned must be worse by the budget on each edge: not better is not enough */
            r = fine;
            r.turned_both[axis].profile = 0.0215;
            assert(selftest_edge_judge(&r) == SELFTEST_EDGE_BOTH);
            r = fine;
            r.turned_both[axis].pixels = 0.0255;
            assert(selftest_edge_judge(&r) == SELFTEST_EDGE_BOTH);
            /* not better than one frame scaled plainly */
            r = fine;
            r.plain[axis].profile = 0.012;
            assert(selftest_edge_judge(&r) == SELFTEST_EDGE_PLAIN);
            /* Noise, each measure against its own: told nothing's margin of 0.007 on the profile is under five
               times a noise of 0.0015, and so is its 0.008 over the pixels under five times 0.0017. */
            r = fine;
            r.noise[axis].profile = 0.0015;
            assert(selftest_edge_judge(&r) == SELFTEST_EDGE_NOISY && selftest_edge_undecided(SELFTEST_EDGE_NOISY));
            r.noise[axis].profile = 0.0013;
            assert(selftest_edge_judge(&r) == 0);
            r = fine;
            r.noise[axis].pixels = 0.0017;
            assert(selftest_edge_judge(&r) == SELFTEST_EDGE_NOISY);
            /* a number that is not one: a profile that was not whole */
            r = fine;
            r.configured[axis].profile = NAN;
            assert(selftest_edge_judge(&r) & SELFTEST_EDGE_NUMBERS);
            r = fine;
            r.noise[axis].pixels = NAN;
            assert(selftest_edge_judge(&r) == SELFTEST_EDGE_NUMBERS);
            r = fine;
            r.plain[axis].profile = NAN;
            assert(selftest_edge_judge(&r) == (SELFTEST_EDGE_NUMBERS | SELFTEST_EDGE_PLAIN));
        }
        /* Every number handed over must be one, used by a condition or not: each of the seven runs' two
           measures on each edge, set to no number or to infinity alone, fails the case as one that decided
           nothing. The other axis's sign on an edge and the plain frame over the pixels are used by none. */
        for (int k = 0; k < 7; k++)
            for (int e = 0; e < 2; e++)
                for (int which = 0; which < 4; which++) {
                    r = fine;
                    SelftestEdgeMeasure* runs[7] = {r.configured, r.turned_x,     r.turned_y, r.turned_both,
                                                    r.told_nothing, r.plain,      r.noise};
                    *(which & 1 ? &runs[k][e].pixels : &runs[k][e].profile) = which & 2 ? INFINITY : NAN;
                    const unsigned failed = selftest_edge_judge(&r);
                    assert((failed & SELFTEST_EDGE_NUMBERS) && selftest_edge_undecided(failed));
                }
        r = fine;
        r.turned_x[1].pixels = NAN; /* x turned on the near-horizontal edge: no condition reads it */
        assert(selftest_edge_judge(&r) == SELFTEST_EDGE_NUMBERS);
        r = fine;
        r.plain[0].pixels = NAN;
        assert(selftest_edge_judge(&r) == SELFTEST_EDGE_NUMBERS);
        r = fine;
        r.settled = 0;
        assert(selftest_edge_judge(&r) == SELFTEST_EDGE_UNSETTLED && !selftest_edge_undecided(SELFTEST_EDGE_UNSETTLED));
        assert(!selftest_edge_undecided(SELFTEST_EDGE_ORDER | SELFTEST_EDGE_MARGIN | SELFTEST_EDGE_PLAIN |
                                        SELFTEST_EDGE_BOTH));
        assert(selftest_edge_undecided(SELFTEST_EDGE_NUMBERS));
    }
    /* Case 4's picture comparison: a resized context against a fresh one. Within the budget of 0.005 it passes,
       the budget itself included. The control must be over the budget and over the noise, or nothing is
       decided, however close the two pictures are. Numbers that are not ones decide nothing. */
    assert(selftest_resized(0.0, 0.0, 0.02) == SELFTEST_PASS);
    assert(selftest_resized(0.005, 0.0001, 0.02) == SELFTEST_PASS);
    assert(selftest_resized(0.0051, 0.0001, 0.02) == SELFTEST_FAIL);
    assert(selftest_resized(0.1, 0.0, 0.2) == SELFTEST_FAIL);
    assert(selftest_resized(0.0, 0.0, 0.005) == SELFTEST_BLIND && selftest_resized(0.0, 0.0, 0.0) == SELFTEST_BLIND);
    assert(selftest_resized(0.001, 0.03, 0.02) == SELFTEST_BLIND);
    assert(selftest_resized(0.001, 0.02, 0.02) == SELFTEST_BLIND);
    assert(selftest_resized(0.001, 0.019, 0.02) == SELFTEST_PASS);
    assert(selftest_resized(0.1, 0.03, 0.02) == SELFTEST_BLIND); /* far apart, and still nothing decided */
    assert(selftest_resized(NAN, 0.0, 0.02) == SELFTEST_BLIND);
    assert(selftest_resized(0.0, NAN, 0.02) == SELFTEST_BLIND);
    assert(selftest_resized(0.0, 0.0, NAN) == SELFTEST_BLIND);
    assert(selftest_resized(0.0, 0.0, INFINITY) == SELFTEST_BLIND);
    assert(selftest_resized(INFINITY, 0.0, 0.02) == SELFTEST_BLIND);
    assert(selftest_resized(-0.001, 0.0, 0.02) == SELFTEST_BLIND);
    assert(selftest_resized(0.0, -0.001, 0.02) == SELFTEST_BLIND);
    /* The matched pair's records. 37 frames of inputs, of which a whole run hands over 36 (the first is the
       gate's). Both whole and the same: matched. Nothing else is. */
    {
        static SelftestHanded one[37], two[37];
        int first = 0;
        memset(one, 0, sizeof one);
        for (int i = 1; i < 37; i++) {
            for (int k = 0; k < SELFTEST_HANDED_FLOATS; k++) one[i].f[k] = 0.25f * (float)(i + k);
            for (int k = 0; k < SELFTEST_HANDED_WORDS; k++) one[i].u[k] = (uint32_t)(100 * i + k);
            one[i].done = 1;
        }
        memcpy(two, one, sizeof two);
        assert(selftest_matched(one, two, 37, 36, 1, 1, &first) == SELFTEST_MATCHED && first == -1);
        /* a first run that failed before it recorded anything, against a whole second one, and both failed:
           two records of nothing are not a match */
        memset(two, 0, sizeof two);
        assert(selftest_matched(two, one, 37, 36, 0, 1, &first) == SELFTEST_MATCH_FAILED);
        assert(selftest_matched(two, one, 37, 36, 1, 1, &first) == SELFTEST_MATCH_SHORT && first == 1);
        assert(selftest_matched(two, two, 37, 36, 1, 1, &first) == SELFTEST_MATCH_SHORT);
        assert(selftest_matched(two, two, 37, 36, 0, 0, &first) == SELFTEST_MATCH_FAILED);
        assert(selftest_matched(one, one, 37, 36, 1, 0, &first) == SELFTEST_MATCH_FAILED);
        /* a short run: the same frames missing in both, or in one, or one frame too many */
        memcpy(two, one, sizeof two);
        two[36].done = 0;
        assert(selftest_matched(one, two, 37, 36, 1, 1, &first) == SELFTEST_MATCH_SHORT && first == 36);
        one[36].done = 0;
        assert(selftest_matched(one, two, 37, 36, 1, 1, &first) == SELFTEST_MATCH_SHORT);
        assert(selftest_matched(one, two, 37, 35, 1, 1, &first) == SELFTEST_MATCHED); /* when 35 is the whole */
        one[36].done = two[36].done = 1;
        one[0].done = two[0].done = 1;
        assert(selftest_matched(one, two, 37, 36, 1, 1, &first) == SELFTEST_MATCH_SHORT);
        one[0].done = two[0].done = 0;
        assert(selftest_matched(one, two, 37, 0, 1, 1, &first) == SELFTEST_MATCH_SHORT);
        /* every number of a frame counts, one at a time: a float by its bits, so 0 and -0 differ */
        for (int k = 0; k < SELFTEST_HANDED_FLOATS + SELFTEST_HANDED_WORDS; k++) {
            memcpy(two, one, sizeof two);
            if (k < SELFTEST_HANDED_FLOATS) two[20].f[k] += 1.0f;
            else two[20].u[k - SELFTEST_HANDED_FLOATS]++;
            assert(selftest_matched(one, two, 37, 36, 1, 1, &first) == SELFTEST_MATCH_DIFFERS && first == 20);
        }
        memcpy(two, one, sizeof two);
        one[5].f[0] = 0.0f;
        two[5].f[0] = -0.0f;
        assert(selftest_matched(one, two, 37, 36, 1, 1, &first) == SELFTEST_MATCH_DIFFERS && first == 5);
        /* what a frame that was not done holds is not compared */
        memcpy(two, one, sizeof two);
        two[0].u[3] = 77;
        assert(selftest_matched(one, two, 37, 36, 1, 1, &first) == SELFTEST_MATCHED);
    }
    /* the checksum: the published values of FNV-1a for nothing, "a" and "foobar", and it can be carried on */
    assert(selftest_checksum("", 0, SELFTEST_CHECKSUM_START) == 0xcbf29ce484222325ull);
    assert(selftest_checksum("a", 1, SELFTEST_CHECKSUM_START) == 0xaf63dc4c8601ec8cull);
    assert(selftest_checksum("foobar", 6, SELFTEST_CHECKSUM_START) == 0x85944171f73967e8ull);
    assert(selftest_checksum("bar", 3, selftest_checksum("foo", 3, SELFTEST_CHECKSUM_START)) == 0x85944171f73967e8ull);
    free(ref);
    free(other);
    free(copy);
}

/* The depth the self-test's scene is made with, against numbers that share nothing with selftest_depth or with the
   decoder: fractions worked out by hand from depth = n (f - d) / (d (f - n)) for near 1/10 and far 5000, and the
   field camera's matrix as a run printed it, with a point taken through it as a row vector. */
static void test_selftest_projection(void) {
    const SelftestProjection finite = {0.1, 5000.0}, infinite = {0.1, INFINITY};
    /* by hand, as fractions: at 1/10 it is 1, at 1/6 29999/49999, at 1 4999/49999, at 5 999/49999, at 100
       49/49999, at 2500 1/49999, at 5000 it is 0 */
    static const double by_hand[7][2] = {{0.1, 1.0},
                                         {1.0 / 6.0, 29999.0 / 49999.0},
                                         {1.0, 4999.0 / 49999.0},
                                         {5.0, 999.0 / 49999.0},
                                         {100.0, 49.0 / 49999.0},
                                         {2500.0, 1.0 / 49999.0},
                                         {5000.0, 0.0}};
    /* report 20261114-110024-report-84b8be, the field camera at +0x80, memory order */
    static const float field[16] = {1.20628512f, 0, 0, 0, 0, 2.14450693f, 0, 0, 0, 0, 2.00004015e-05f, -1, 0, 0,
                                    0.100002006f, 0};
    for (int k = 0; k < 7; k++) {
        const double d = by_hand[k][0], depth = selftest_depth(&finite, d);
        assert(close_to(depth, by_hand[k][1], 1e-12));
        /* the point (0.3 d, -0.2 d, -d, 1) as a row vector times the matrix: clip z over clip w */
        const double point[4] = {0.3 * d, -0.2 * d, -d, 1.0};
        double clip[4] = {0.0, 0.0, 0.0, 0.0};
        for (int column = 0; column < 4; column++)
            for (int row = 0; row < 4; row++) clip[column] += point[row] * (double)field[4 * row + column];
        assert(close_to(clip[3], d, 1e-12) && close_to(clip[2] / clip[3], depth, 2e-7));
    }
    assert(selftest_depth(&finite, 0.1) > selftest_depth(&finite, 0.2)); /* reversed: nearer is larger */
    /* the far plane at infinity: n / d, 1 at the near plane, towards 0 and never 0 */
    assert(selftest_depth(&infinite, 0.1) == 1.0 && close_to(selftest_depth(&infinite, 0.4), 0.25, 1e-15));
    assert(selftest_depth(&infinite, 1e9) > 0.0 && selftest_depth(&infinite, 1e9) < 1e-9);
    /* The scene's two distances. With the far plane at infinity they give the depths every case had so far, to
       the bit of a 24 bit depth image. With the far plane at 5000 they give other bits, the patch still nearer. */
    assert(selftest_d24(selftest_depth(&infinite, SELFTEST_FAR_DISTANCE)) == selftest_d24(SELFTEST_FAR));
    assert(selftest_d24(selftest_depth(&infinite, SELFTEST_NEAR_DISTANCE)) == selftest_d24(SELFTEST_NEAR));
    assert(selftest_d24(SELFTEST_FAR) == 335544u && selftest_d24(SELFTEST_NEAR) == 10066329u);
    assert(selftest_d24(selftest_depth(&finite, SELFTEST_FAR_DISTANCE)) == 335215u);
    assert(selftest_d24(selftest_depth(&finite, SELFTEST_NEAR_DISTANCE)) == 10066195u);
    assert(selftest_depth(&finite, SELFTEST_NEAR_DISTANCE) > selftest_depth(&finite, SELFTEST_FAR_DISTANCE));
    /* One more check, not the proof: the decoder reads the form back from a matrix made for it, and the depth
       at the decoded planes is 1 and 0. */
    {
        TemporalProjection p;
        float m[16];
        projection_make(finite.near_plane, finite.far_plane, 0.8726646259971648, 16.0 / 9.0, m);
        assert(temporal_projection_camera(m, &p) == TEMPORAL_PROJECTION_OK && !p.far_infinite);
        assert(close_to(selftest_depth(&finite, p.near_plane), 1.0, 1e-6));
        assert(close_to(selftest_depth(&finite, p.far_plane), 0.0, 1e-6));
        projection_make(infinite.near_plane, infinite.far_plane, 0.8726646259971648, 16.0 / 9.0, m);
        assert(temporal_projection_camera(m, &p) == TEMPORAL_PROJECTION_OK && p.far_infinite);
        assert(close_to(selftest_depth(&infinite, p.near_plane), 1.0, 1e-6));
    }
    /* the scene hands a frame's two depths on as they are given */
    {
        SelftestFrame f = {0};
        const SelftestViewport v = {0, 0, 640, 360};
        double rgb[3], depth = -1.0, texel[2];
        f.depth[0] = selftest_depth(&finite, SELFTEST_FAR_DISTANCE);
        f.depth[1] = selftest_depth(&finite, SELFTEST_NEAR_DISTANCE);
        selftest_input(&f, &v, 640, 360, 320, 180, rgb, &depth, texel);
        assert(depth == f.depth[0]);
        f.has_patch = 1;
        f.patch[0] = f.patch[1] = f.patch_before[0] = f.patch_before[1] = -0.5;
        f.patch[2] = f.patch[3] = f.patch_before[2] = f.patch_before[3] = 0.5;
        selftest_input(&f, &v, 640, 360, 320, 180, rgb, &depth, texel);
        assert(depth == f.depth[1]);
        selftest_input(&f, &v, 640, 360, 20, 20, rgb, &depth, texel);
        assert(depth == f.depth[0]);
    }
}

/* The native suite's printed assertions: what a session's contexts were made with and handed, against what a
   case expects of them. */
/* how the loader's list of layers was read: whole, in part, or not at all */
static void test_selftest_layers(void) {
    const int incomplete = 5, success = 0;
    assert(selftest_layers(success, success, incomplete) == SELFTEST_LAYERS_WHOLE);
    assert(selftest_layers(success, incomplete, incomplete) == SELFTEST_LAYERS_PART);
    assert(selftest_layers(incomplete, success, incomplete) == SELFTEST_LAYERS_WHOLE); /* the count is no list */
    assert(selftest_layers(-1, success, incomplete) == SELFTEST_LAYERS_UNREAD);
    assert(selftest_layers(success, -3, incomplete) == SELFTEST_LAYERS_UNREAD);
    assert(selftest_layers(-1, incomplete, incomplete) == SELFTEST_LAYERS_UNREAD); /* a failure outranks a part */
    assert(selftest_layers(-1, -1, incomplete) == SELFTEST_LAYERS_UNREAD);
    assert(selftest_layers(success, 1, incomplete) == SELFTEST_LAYERS_WHOLE); /* another success code */
}

/* the names of variables that validation callbacks' texts hold, by the layer's pattern */
static void test_selftest_variables(void) {
    SelftestVariables v = {0};
    const char* one = "vkCmdDispatch(): the storage image descriptor [Set 0, Binding 128, variable \"rw_a\"] is "
                      "accessed";
    selftest_variables(&v, one, strlen(one), 65536);
    assert(v.callbacks == 1 && v.names == 1 && !strcmp(v.name[0].name, "rw_a") && v.name[0].count == 1);
    assert(!v.unmatched && selftest_variables_whole(&v));
    /* the same name twice in one callback counts once, and again in another callback */
    const char* twice = "variable \"rw_a\" and variable \"rw_a\" and variable \"rw_b\"";
    selftest_variables(&v, twice, strlen(twice), 65536);
    assert(v.names == 2 && v.name[0].count == 2 && !strcmp(v.name[1].name, "rw_b") && v.name[1].count == 1);
    /* a name is its whole text: a prefix of another is another name, and so is one that differs at its end */
    const char* near = "variable \"rw_\" variable \"rw_ab\" variable \"rw_aB\"";
    selftest_variables(&v, near, strlen(near), 65536);
    assert(v.names == 5 && v.name[0].count == 2 && v.name[2].count == 1 && v.name[4].count == 1);
    /* no match: other words, the word without a quote, a quote without the word, an empty text */
    static const char* const none[5] = {"a shader module declares a capability", "the variable rw_a is bound",
                                        "\"rw_a\"", "", "Variable \"rw_a\""};
    for (int k = 0; k < 5; k++) selftest_variables(&v, none[k], strlen(none[k]), 65536);
    assert(v.unmatched == 5 && v.names == 5 && v.callbacks == 8);
    /* an empty name is a name, found and kept */
    selftest_variables(&v, "variable \"\"", 11, 65536);
    assert(v.names == 6 && v.name[5].name[0] == 0 && v.name[5].count == 1 && v.unmatched == 5);
    assert(selftest_variables_whole(&v));
    /* marks: a quoted name that does not close, counted as an occurrence and not as "no match" */
    selftest_variables(&v, "variable \"rw_open", 17, 65536);
    assert(v.unclosed == 1 && v.unmatched == 5 && v.names == 6 && !selftest_variables_whole(&v));
    /* a name of 63 characters is kept, one of 64 is too long, and the scan goes on behind it */
    {
        SelftestVariables w = {0};
        char text[200];
        int at = snprintf(text, sizeof text, "variable \"");
        memset(text + at, 'n', 63);
        at += 63;
        at += snprintf(text + at, sizeof text - (size_t)at, "\" variable \"");
        memset(text + at, 'm', 64);
        at += 64;
        at += snprintf(text + at, sizeof text - (size_t)at, "\" variable \"rw_after\"");
        selftest_variables(&w, text, (size_t)at, 65536);
        assert(w.names == 2 && strlen(w.name[0].name) == 63 && !strcmp(w.name[1].name, "rw_after"));
        assert(w.overlong == 1 && !w.unmatched && !selftest_variables_whole(&w));
    }
    /* a text longer than what is scanned: marked, and a name past the limit is not found */
    {
        SelftestVariables w = {0};
        const char* text = "variable \"rw_a\" ................ variable \"rw_late\"";
        selftest_variables(&w, text, strlen(text), 20);
        assert(w.unscanned == 1 && w.names == 1 && !strcmp(w.name[0].name, "rw_a") && !selftest_variables_whole(&w));
        /* the limit falls inside a name: it does not close within what was scanned */
        SelftestVariables x = {0};
        selftest_variables(&x, text, strlen(text), 12);
        assert(x.unscanned == 1 && x.unclosed == 1 && x.names == 0 && x.unmatched == 0);
    }
    /* a full table: 64 names are kept, the 65th is marked, and a kept name is still counted after that */
    {
        SelftestVariables w = {0};
        char text[64];
        for (int k = 0; k < 65; k++) {
            const int n = snprintf(text, sizeof text, "variable \"name%d\"", k);
            selftest_variables(&w, text, (size_t)n, 65536);
        }
        assert(w.names == 64 && w.full == 1 && !selftest_variables_whole(&w));
        selftest_variables(&w, "variable \"name3\"", 16, 65536);
        assert(w.name[3].count == 2 && w.full == 1 && w.callbacks == 66 && w.unmatched == 0);
    }
}

/* the layer's own sentence of stopping a message id, and texts that only look like it */
static void test_selftest_stop_notice(void) {
    assert(selftest_stop_notice("(Warning - This VUID has now been reported 10 times, which is the "
                                "duplicate_message_limit value, this will be the last time reporting it)."));
    assert(selftest_stop_notice("x This VUID has now been reported 250 times, which is the duplicate_message_limit "
                                "value, this will be the last time reporting it"));
    assert(!selftest_stop_notice("the storage image descriptor [variable \"duplicate_message_limit\"] is accessed"));
    assert(!selftest_stop_notice("duplicate_message_limit"));
    assert(!selftest_stop_notice("This VUID has now been reported 10 times"));
    assert(!selftest_stop_notice("which is the duplicate_message_limit value, this will be the last time reporting it"));
    /* the two halves the wrong way round are not the sentence */
    assert(!selftest_stop_notice(" times, which is the duplicate_message_limit value, this will be the last time "
                                 "reporting it. This VUID has now been reported "));
    assert(!selftest_stop_notice(""));
}

static void test_selftest_proof(void) {
    const uint32_t flag = 1u << 4, other = 0xf;
    const double finite[2] = {5000.0, 5000.0}, mixed[2] = {5000.0, INFINITY}, back[2] = {INFINITY, 5000.0};
    SelftestProof p = {0};
    /* nothing happened: no count is right for it, and a count of zero is no expectation */
    assert(selftest_proof(&p, 1, finite, flag, 7) == SELFTEST_PROOF_COUNT);
    assert(selftest_proof(&p, 0, finite, flag, 0) == SELFTEST_PROOF_COUNT);
    selftest_proof_frame(&p, 1, 5000.0f, 7); /* a frame with no context is no one's */
    assert(p.frames[0] == 0 && p.depth_sum == 7);
    /* one context made for a finite far plane, three frames, the first with the reset */
    selftest_proof_made(&p, other);
    assert(selftest_proof(&p, 1, finite, flag, 7) == SELFTEST_PROOF_UNUSED);
    selftest_proof_frame(&p, 1, 5000.0f, 5);
    selftest_proof_frame(&p, 0, 5000.0f, 6);
    selftest_proof_frame(&p, 0, 5000.0f, 7);
    assert(p.contexts == 1 && p.frames[0] == 3 && p.first_reset[0] == 1 && !p.far_mixed[0]);
    assert(selftest_proof(&p, 1, finite, flag, 7) == SELFTEST_PROOF_OK);
    assert(selftest_proof(&p, 2, finite, flag, 7) == SELFTEST_PROOF_COUNT);
    assert(selftest_proof(&p, 1, back, flag, 7) == SELFTEST_PROOF_FLAG); /* expected at infinity, made finite */
    assert(selftest_proof(&p, 1, finite, flag, 6) == SELFTEST_PROOF_DEPTH); /* the image of the frame before */
    {
        const double other_far[1] = {120.0};
        assert(selftest_proof(&p, 1, other_far, flag, 7) == SELFTEST_PROOF_FAR);
    }
    /* a second context for a far plane at infinity: the flag, the value and the reset are each needed */
    selftest_proof_made(&p, other | flag);
    assert(selftest_proof(&p, 2, mixed, flag, 7) == SELFTEST_PROOF_UNUSED);
    selftest_proof_frame(&p, 1, INFINITY, 9);
    selftest_proof_frame(&p, 0, INFINITY, 9);
    assert(selftest_proof(&p, 2, mixed, flag, 9) == SELFTEST_PROOF_OK);
    assert(selftest_proof(&p, 2, finite, flag, 9) == SELFTEST_PROOF_FLAG);
    assert(selftest_proof(&p, 2, back, flag, 9) == SELFTEST_PROOF_FLAG);
    assert(selftest_proof(&p, 1, finite, flag, 9) == SELFTEST_PROOF_COUNT);
    {
        SelftestProof q = p;
        q.flags[1] = other; /* made without the flag and handed infinity */
        assert(selftest_proof(&q, 2, mixed, flag, 9) == SELFTEST_PROOF_FLAG);
        q = p;
        q.flags[0] = other | flag; /* made with the flag and handed 5000 */
        assert(selftest_proof(&q, 2, mixed, flag, 9) == SELFTEST_PROOF_FLAG);
        q = p;
        q.far_plane[1] = 3.4028234663852886e38f; /* the largest float is not infinity */
        assert(selftest_proof(&q, 2, mixed, flag, 9) == SELFTEST_PROOF_FAR);
        q = p;
        q.far_plane[1] = -INFINITY;
        assert(selftest_proof(&q, 2, mixed, flag, 9) == SELFTEST_PROOF_FAR);
        q = p;
        q.far_plane[0] = NAN;
        assert(selftest_proof(&q, 2, mixed, flag, 9) == SELFTEST_PROOF_FAR);
        q = p;
        q.first_reset[1] = 0; /* the new context's first evaluate without the reset */
        assert(selftest_proof(&q, 2, mixed, flag, 9) == SELFTEST_PROOF_RESET);
        q = p;
        q.first_reset[0] = 0;
        assert(selftest_proof(&q, 2, mixed, flag, 9) == SELFTEST_PROOF_RESET);
    }
    /* a context handed two far planes is mixed, whatever the last one was */
    {
        SelftestProof q = {0};
        selftest_proof_made(&q, other);
        selftest_proof_frame(&q, 1, 5000.0f, 1);
        selftest_proof_frame(&q, 0, 120.0f, 1);
        selftest_proof_frame(&q, 0, 5000.0f, 1);
        assert(q.far_mixed[0] && selftest_proof(&q, 1, finite, flag, 1) == SELFTEST_PROOF_FAR);
        /* a first evaluate without the reset stays without it, however many resets follow */
        SelftestProof r = {0};
        selftest_proof_made(&r, other);
        selftest_proof_frame(&r, 0, 5000.0f, 1);
        selftest_proof_frame(&r, 1, 5000.0f, 1);
        assert(selftest_proof(&r, 1, finite, flag, 1) == SELFTEST_PROOF_RESET);
    }
    /* more contexts than are kept: counted, never judged as right */
    {
        SelftestProof q = {0};
        const double five[5] = {5000.0, 5000.0, 5000.0, 5000.0, 5000.0};
        for (int k = 0; k < 5; k++) {
            selftest_proof_made(&q, other);
            selftest_proof_frame(&q, 1, 5000.0f, 1);
        }
        assert(q.contexts == 5 && q.frames[3] == 1);
        assert(selftest_proof(&q, 5, five, flag, 1) == SELFTEST_PROOF_COUNT);
        assert(selftest_proof(&q, 4, five, flag, 1) == SELFTEST_PROOF_COUNT);
    }
}

static void test_selftest(void) {
    const SelftestViewport down = {0, 0, 640, 360}, up = {0, 360, 640, -360};
    double n[2], p[2], a[3], b[3], depth, texel[2];
    /* a viewport and its inverse, with a positive and a negative height */
    selftest_ndc(&down, 160, 90, n);
    assert(close_to(n[0], -0.5, 1e-12) && close_to(n[1], -0.5, 1e-12));
    selftest_ndc(&up, 160, 90, n);
    assert(close_to(n[0], -0.5, 1e-12) && close_to(n[1], 0.5, 1e-12)); /* the upper quarter is positive ndc y */
    selftest_place(&up, n, p);
    assert(close_to(p[0], 160, 1e-9) && close_to(p[1], 90, 1e-9));

    /* THE INPUT RULE. A frame drawn with a perturbation j shows, at a pixel's centre, the scene's point at the
       centre's ndc LESS j. With j one pixel to the right in ndc the whole picture moves one pixel to the right:
       pixel x shows what pixel x - 1 shows without it. Centre plus j would give pixel x + 1 and the wrong sign. */
    SelftestFrame still = {0}, moved = {0};
    moved.jitter[0] = 2.0 / 640.0;
    for (int x = 20; x < 620; x += 37) {
        st_colour(&moved, &down, x, 100, a);
        st_colour(&still, &down, x - 1, 100, b);
        assert(!memcmp(a, b, sizeof a));
    }
    /* In y the viewport's sign decides. One pixel "up" in ndc is one pixel down the image for a positive height
       and one pixel up the image for a negative one. */
    memset(&moved, 0, sizeof moved);
    moved.jitter[1] = 2.0 / 360.0;
    int differs = 0;
    for (int y = 20; y < 340; y += 23) {
        st_colour(&moved, &down, 300, y, a);
        st_colour(&still, &down, 300, y - 1, b);
        assert(!memcmp(a, b, sizeof a));
        st_colour(&moved, &up, 300, y, a);
        st_colour(&still, &up, 300, y + 1, b);
        assert(!memcmp(a, b, sizeof a));
        st_colour(&still, &up, 300, y - 1, b);
        differs += memcmp(a, b, sizeof a) != 0;
    }
    assert(differs > 3); /* the two signs are told apart by the pattern */

    /* The motion texel. The background moved by (0.1, 0.05) in ndc since the frame before. Through the viewport
       with the negative height that is 32 px to the right and 9 px up the image, over 640 by 360. A perturbation
       changes which point a pixel shows, not the vector of that point. */
    SelftestFrame pan = {0};
    pan.depth[0] = SELFTEST_FAR;
    pan.shift[0] = 0.3;
    pan.shift[1] = 0.15;
    pan.shift_before[0] = 0.2;
    pan.shift_before[1] = 0.1;
    for (int jittered = 0; jittered < 2; jittered++) {
        pan.jitter[0] = jittered ? 0.7 / 640.0 : 0.0;
        pan.jitter[1] = jittered ? -0.5 / 360.0 : 0.0;
        selftest_input(&pan, &up, 640, 360, 200, 120, a, &depth, texel);
        assert(close_to(texel[0], 32.0 / 640.0, 1e-12) && close_to(texel[1], -9.0 / 360.0, 1e-12));
        assert(depth == SELFTEST_FAR);
        selftest_input(&pan, &down, 640, 360, 200, 120, a, &depth, texel);
        assert(close_to(texel[0], 32.0 / 640.0, 1e-12) && close_to(texel[1], 9.0 / 360.0, 1e-12));
    }
    /* the texel is over the images' size, not the viewport's: a rectangle of 320 by 180 in images of 640 by 360 */
    const SelftestViewport part = {0, 180, 320, -180};
    pan.jitter[0] = pan.jitter[1] = 0.0;
    selftest_input(&pan, &part, 640, 360, 100, 60, a, &depth, texel);
    assert(close_to(texel[0], 16.0 / 640.0, 1e-12) && close_to(texel[1], -4.5 / 360.0, 1e-12));

    /* The patch: nearer, with its own pattern and its own vector. It covers ndc -0.25 to 0.25 by -0.3 to 0.2 and
       came from 0.05 further left. In the 640 by 360 picture with y up that is x 240 to 400 and y 144 to 234. */
    SelftestFrame scene = {0};
    scene.has_patch = 1;
    scene.depth[0] = SELFTEST_FAR;
    scene.depth[1] = SELFTEST_NEAR;
    const double rect[4] = {-0.25, -0.3, 0.25, 0.2}, rect_before[4] = {-0.3, -0.3, 0.2, 0.2};
    memcpy(scene.patch, rect, sizeof rect);
    memcpy(scene.patch_before, rect_before, sizeof rect_before);
    selftest_input(&scene, &up, 640, 360, 320, 180, a, &depth, texel);
    assert(depth == SELFTEST_NEAR && close_to(texel[0], 16.0 / 640.0, 1e-12) && texel[1] == 0.0);
    selftest_input(&scene, &up, 640, 360, 100, 180, b, &depth, texel);
    assert(depth == SELFTEST_FAR && texel[0] == 0.0 && texel[1] == 0.0 && memcmp(a, b, sizeof a));
    /* the patch's pattern goes with it: a point of the patch has the colour it had a frame ago */
    SelftestFrame earlier = scene;
    memcpy(earlier.patch, rect_before, sizeof rect_before);
    st_colour(&scene, &up, 320, 180, a);
    st_colour(&earlier, &up, 304, 180, b);
    assert(!memcmp(a, b, sizeof a));
    /* The regions. Border and the patch's rim are no one's. The band is exactly what the patch left this frame:
       it moved 16 px to the right, so 16 columns of its height, 90 rows. */
    int counts[4] = {0, 0, 0, 0}, left = 640, right = 0;
    for (int y = 0; y < 360; y++)
        for (int x = 0; x < 640; x++) {
            const int region = selftest_region(&scene, &up, 640, 360, x, y);
            counts[region]++;
            if (region == SELFTEST_UNCOVERED) {
                if (x < left) left = x;
                if (x > right) right = x;
            }
        }
    assert(counts[SELFTEST_UNCOVERED] == 16 * 90 && left == 224 && right == 239);
    assert(counts[SELFTEST_INSIDE] == (160 - 16) * (90 - 16));
    assert(selftest_region(&scene, &up, 640, 360, 3, 100) == SELFTEST_NONE);
    assert(selftest_region(&scene, &up, 640, 360, 243, 180) == SELFTEST_NONE); /* the patch's rim */
    assert(selftest_region(&scene, &up, 640, 360, 248, 180) == SELFTEST_INSIDE);
    assert(selftest_region(&scene, &up, 640, 360, 100, 180) == SELFTEST_BACKGROUND);
    assert(counts[0] + counts[1] + counts[2] + counts[3] == 640 * 360);
    /* The reference. Inside one cell of the patterns it is the scene's colour there. The patch's edge is at
       x 240, so pixel 240 is all patch. In a picture 641 across the edge falls inside pixel 240, and the
       reference there is a mix of patch and background by area. */
    selftest_reference(&scene, &up, 100, 180, 8, a);
    st_colour(&scene, &up, 100, 180, b);
    assert(close_to(a[0], b[0], 1e-12));
    selftest_reference(&scene, &up, 240, 180, 8, a);
    st_colour(&scene, &up, 240, 180, b);
    assert(close_to(a[1], 0.3, 1e-12) && close_to(b[1], 0.3, 1e-12)); /* the patch's green */
    const SelftestViewport odd = {0, 360, 641, -360};
    selftest_reference(&scene, &odd, 240, 180, 8, a); /* x 240 to 241 of 641: the patch starts at 240.375 */
    assert(a[1] > 0.2 && a[1] < 0.75 && !close_to(a[1], 0.3, 1e-3));

    /* The game's formats, bit for bit where the value is exact */
    assert(selftest_ufloat(1.0, 6) == 0x3C0 && selftest_ufloat(0.5, 6) == 0x380 && selftest_ufloat(2.0, 6) == 0x400);
    assert(selftest_ufloat(1.5, 6) == 0x3E0 && selftest_ufloat(1.0, 5) == 0x1E0 && selftest_ufloat(0.0, 6) == 0);
    assert(selftest_ufloat(-3.0, 6) == 0);
    const double white[3] = {1.0, 1.0, 1.0};
    assert(selftest_b10g11r11(white) == (0x3C0u | 0x3C0u << 11 | 0x1E0u << 22));
    for (double value = 0.01; value < 40.0; value *= 1.37) {
        const double rgb[3] = {value, value * 0.5, value * 0.25};
        double back[3];
        selftest_b10g11r11_value(selftest_b10g11r11(rgb), back);
        assert(fabs(back[0] - rgb[0]) <= rgb[0] / 128.0 && fabs(back[1] - rgb[1]) <= rgb[1] / 128.0);
        assert(fabs(back[2] - rgb[2]) <= rgb[2] / 64.0);
        assert(fabs(selftest_half_value(selftest_half(value)) - value) <= value / 2048.0);
        assert(selftest_half_value(selftest_half(-value)) == -selftest_half_value(selftest_half(value)));
    }
    assert(selftest_d24(1.0) == 0xFFFFFF && selftest_d24(0.0) == 0 && selftest_d24(0.5) == 0x800000);
    assert(selftest_d24(7.0) == 0xFFFFFF && selftest_d24(-1.0) == 0);
    /* the output's read-back: four floats a pixel, each channel where it lies, values no half can hold kept,
       and what is not finite still not finite when it is read */
    {
        const float pixels[3][4] = {{1.0f, 2.0f, 3.0f, 4.0f},
                                    {70000.0f, 1.0e-30f, -0.5f, 0.0f},
                                    {INFINITY, NAN, 0.0f, 1.0f}};
        uint8_t bytes[1 + sizeof pixels]; /* from an address that is not aligned */
        memcpy(bytes + 1, pixels, sizeof pixels);
        assert(sizeof pixels == 3 * SELFTEST_OUTPUT_BYTES);
        for (size_t p = 0; p < 2; p++)
            for (int c = 0; c < 4; c++) assert(selftest_output_value(bytes + 1, p, c) == pixels[p][c]);
        assert(isinf(selftest_output_value(bytes + 1, 2, 0)) && isnan(selftest_output_value(bytes + 1, 2, 1)));
        assert(!isfinite((double)selftest_output_value(bytes + 1, 2, 0)));
        assert(selftest_output_value(bytes + 1, 2, 3) == 1.0f);
    }
    /* the staging buffer against a frame's uploads and the read-back, at their edges */
    assert(selftest_staging_fits(8u << 20, 480, 270, 640, 360) && selftest_staging_fits(16, 1, 1, 1, 1));
    assert(!selftest_staging_fits(15, 1, 1, 1, 1) && !selftest_staging_fits(31, 2, 1, 1, 1));
    assert(selftest_staging_fits(32, 2, 1, 1, 2) && !selftest_staging_fits(32, 1, 1, 3, 1));
    assert(selftest_staging_fits(8u << 20, 1, 1, 1024, 512) && !selftest_staging_fits(8u << 20, 1, 1, 1024, 513));
    assert(selftest_staging_fits(8u << 20, 1024, 512, 1, 1) && !selftest_staging_fits(8u << 20, 1025, 512, 1, 1));
    assert(!selftest_staging_fits(8u << 20, 0, 1, 1, 1) && !selftest_staging_fits(8u << 20, 1, 1, 1, -1));
    /* sizes whose 16 bytes a texel are past 64 bits do not fit the largest buffer either */
    assert(!selftest_staging_fits(UINT64_MAX, 2147483647, 2147483647, 1, 1));
    assert(!selftest_staging_fits(UINT64_MAX, 1, 1, 2147483647, 2147483647));
    assert(selftest_half(1.0) == 0x3C00 && selftest_half(-2.0) == 0xC000 && selftest_half(0.0) == 0);
    assert(selftest_half_value(0x3C00) == 1.0 && selftest_half_value(0x7C00) == INFINITY);
    assert(isnan(selftest_half_value(0x7E00)) && selftest_half_value(0x0001) > 0.0);

    /* The verdicts. Four variants of a case, the configured one first. */
    const double clear[4] = {0.010, 0.030, 0.025, 0.040}, close[4] = {0.010, 0.012, 0.025, 0.040};
    const double wrong[4] = {0.030, 0.010, 0.025, 0.040}, flat[4] = {0.020, 0.021, 0.020, 0.022};
    const double tied[4] = {0.010, 0.010, 0.030, 0.040}, broken[4] = {0.010, NAN, 0.030, 0.040};
    assert(selftest_verdict(clear, 4, 0, SELFTEST_MARGIN) == SELFTEST_PASS);
    assert(selftest_verdict(close, 4, 0, SELFTEST_MARGIN) == SELFTEST_FAIL);  /* one variant under the margin */
    assert(selftest_verdict(wrong, 4, 0, SELFTEST_MARGIN) == SELFTEST_FAIL);  /* a flipped sign is better */
    assert(selftest_verdict(wrong, 4, 1, SELFTEST_MARGIN) == SELFTEST_PASS);
    assert(selftest_verdict(flat, 4, 0, SELFTEST_MARGIN) == SELFTEST_BLIND);  /* the case tells nothing apart */
    assert(selftest_verdict(tied, 4, 0, SELFTEST_MARGIN) == SELFTEST_FAIL);   /* inconclusive counts as failed */
    assert(selftest_verdict(broken, 4, 0, SELFTEST_MARGIN) == SELFTEST_FAIL);
    assert(selftest_verdict(clear, 1, 0, SELFTEST_MARGIN) == SELFTEST_FAIL);
    assert(selftest_verdict(clear, 4, 4, SELFTEST_MARGIN) == SELFTEST_FAIL);
    /* Cases 1 and 2: the configured signs strictly the lowest of all, and the margin only for the variants
       marked (vector signs turned, the jitter's as configured). The device run's four jitter variants, 0.0518
       against 0.0607, 0.0668 and 0.0753, pass this and failed the margin for all. */
    {
        const double jitter[4] = {0.051835, 0.060744, 0.066839, 0.075324};
        const int none[4] = {0, 0, 0, 0}, all[4] = {0, 1, 1, 1};
        assert(selftest_verdict(jitter, 4, 0, SELFTEST_MARGIN) == SELFTEST_FAIL);
        assert(selftest_signs(jitter, none, 4, 0, SELFTEST_MARGIN) == SELFTEST_PASS);
        assert(selftest_signs(jitter, all, 4, 0, SELFTEST_MARGIN) == SELFTEST_FAIL);
        assert(selftest_signs(jitter, none, 4, 1, SELFTEST_MARGIN) == SELFTEST_FAIL); /* another is lower */
        /* strictly: a variant level with the configured one fails, with or without a margin asked of it */
        assert(selftest_signs(tied, none, 4, 0, SELFTEST_MARGIN) == SELFTEST_FAIL);
        assert(selftest_signs(wrong, none, 4, 0, SELFTEST_MARGIN) == SELFTEST_FAIL);
        /* The margin is asked where it is marked and nowhere else. Of case 2's sixteen, three turn vector
           signs only: numbers 4, 8 and 12. */
        double sixteen[16];
        int marked[16];
        for (int i = 0; i < 16; i++) {
            sixteen[i] = i ? 0.0105 + 0.0001 * i : 0.010; /* every jitter variant a little worse */
            marked[i] = i && !(i & 3);
            if (marked[i]) sixteen[i] = 0.030;
        }
        assert(selftest_signs(sixteen, marked, 16, 0, SELFTEST_MARGIN) == SELFTEST_PASS);
        for (int i = 4; i < 16; i += 4) {
            double one[16];
            memcpy(one, sixteen, sizeof one);
            one[i] = 0.0124; /* worse than the configured one, and under 1.25 times it */
            assert(selftest_signs(one, marked, 16, 0, SELFTEST_MARGIN) == SELFTEST_FAIL);
            one[i] = 0.0125;
            assert(selftest_signs(one, marked, 16, 0, SELFTEST_MARGIN) == SELFTEST_PASS);
        }
        for (int i = 1; i < 16; i++) {
            double one[16];
            memcpy(one, sixteen, sizeof one);
            one[i] = 0.010; /* level with the configured one */
            assert(selftest_signs(one, marked, 16, 0, SELFTEST_MARGIN) == SELFTEST_FAIL);
            one[i] = 0.0099;
            assert(selftest_signs(one, marked, 16, 0, SELFTEST_MARGIN) == SELFTEST_FAIL);
        }
        /* all the same tells nothing apart, numbers that are not ones fail, as do too few and a wrong index */
        const double same[4] = {0.02, 0.02, 0.02, 0.02};
        assert(selftest_signs(same, none, 4, 0, SELFTEST_MARGIN) == SELFTEST_BLIND);
        assert(selftest_signs(broken, none, 4, 0, SELFTEST_MARGIN) == SELFTEST_FAIL);
        assert(selftest_signs(jitter, none, 1, 0, SELFTEST_MARGIN) == SELFTEST_FAIL);
        assert(selftest_signs(jitter, none, 4, 4, SELFTEST_MARGIN) == SELFTEST_FAIL);
        assert(selftest_signs(jitter, none, 4, -1, SELFTEST_MARGIN) == SELFTEST_FAIL);
    }
    const double exact[2] = {0.008, 0.010}, under[2] = {0.008, 0.0099};
    assert(selftest_verdict(exact, 2, 0, SELFTEST_MARGIN) == SELFTEST_PASS); /* exactly 1.25 times passes */
    assert(selftest_verdict(under, 2, 0, SELFTEST_MARGIN) == SELFTEST_BLIND);
    assert(selftest_beats(0.008, 0.010, SELFTEST_MARGIN) && !selftest_beats(0.008, 0.0099, SELFTEST_MARGIN));
    assert(!selftest_beats(0.010, 0.008, SELFTEST_MARGIN) && !selftest_beats(NAN, 1.0, SELFTEST_MARGIN));
    assert(!selftest_beats(0.0, 0.0, SELFTEST_MARGIN) && selftest_beats(0.0, 0.001, SELFTEST_MARGIN));
    assert(selftest_settles(0.02, 0.01) && !selftest_settles(0.02, 0.0101) && !selftest_settles(0.0, 0.0));
    assert(!selftest_settles(0.02, NAN) && selftest_settles(0.02, 0.0));
    /* Settling, for the runs' two sizes: 18 offsets (36 evaluated frames, 35 changes) and 21 (42 and 41). The
       first cycle has one change less than the offsets, the last has the rest with the one at the boundary. A
       big change exactly at the boundary belongs to the last cycle, so such a run has NOT stopped changing. Put
       into the first cycle it would look settled. */
    for (int which = 0; which < 2; which++) {
        const int offsets = which ? 21 : 18, frames = 2 * offsets;
        SelftestSettling calm = {{0, 0}, {0, 0}}, jolt = {{0, 0}, {0, 0}}, good = {{0, 0}, {0, 0}};
        for (int k = 0; k < frames; k++) {
            selftest_change(&calm, k, offsets, 1.0);
            selftest_change(&jolt, k, offsets, k == offsets ? 100.0 : 1.0);
            selftest_change(&good, k, offsets, k < offsets ? 1.0 : 0.25);
        }
        assert(calm.n[0] == offsets - 1 && calm.n[1] == offsets && calm.n[0] + calm.n[1] == frames - 1);
        assert(selftest_cycle(&calm, 0) == 1.0 && selftest_cycle(&calm, 1) == 1.0);
        assert(!selftest_settles(selftest_cycle(&calm, 0), selftest_cycle(&calm, 1)));
        assert(selftest_cycle(&jolt, 0) == 1.0 && selftest_cycle(&jolt, 1) == (offsets - 1 + 100.0) / offsets);
        assert(!selftest_settles(selftest_cycle(&jolt, 0), selftest_cycle(&jolt, 1)));
        assert(selftest_cycle(&good, 0) == 1.0 && selftest_cycle(&good, 1) == 0.25);
        assert(selftest_settles(selftest_cycle(&good, 0), selftest_cycle(&good, 1)));
    }
    SelftestSettling none = {{0, 0}, {0, 0}};
    selftest_change(&none, 0, 18, 5.0); /* the first evaluated frame has no change into it */
    assert(none.n[0] == 0 && isnan(selftest_cycle(&none, 0)) && isnan(selftest_cycle(&none, 1)));
    /* the switch: off without a value and with "0" or "off", on with anything else, an unknown name included */
    assert(!selftest_asked(NULL) && !selftest_asked("") && !selftest_asked("0") && !selftest_asked("off"));
    assert(selftest_asked("fsr") && selftest_asked("1") && selftest_asked("00") && selftest_asked("of"));
    assert(selftest_asked("offs") && selftest_asked("OFF") && selftest_asked("o") && selftest_asked(" "));
    assert(selftest_same(0.01) && !selftest_same(0.0101) && !selftest_same(NAN));
    assert(selftest_apart(0.05) && !selftest_apart(0.0499) && !selftest_apart(NAN));
}

int main(void) {
    test_jitter();
    test_jitter_matrices();
    test_affine_inverse();
    test_object_matrices();
    test_skin_matrices();
    test_catmull();
    test_clip();
    test_weights();
    test_sharpen();
    test_sharpen_rgb();
    test_camera_points();
    test_camera_world_points();
    test_mip_bias();
    test_pairs();
    test_gate();
    test_streams();
    test_stream_keeping();
    test_stream_table();
    test_origin_shift();
    test_classes();
    test_links();
    test_rect();
    test_rect_confirm();
    test_upscaler();
    test_upscaler_reset();
    test_projection_camera();
    test_upscaler_needs();
    test_upscaler_output_fits();
    test_validation_lines();
    test_upscaler_handed();
    test_txq_today();
    test_readers_program();
    test_spatial();
    test_upscaler_module();
    test_upscaler_wiring();
    test_jitter_conformance();
    test_gpu_time();
    test_selftest();
    test_selftest_projection();
    test_selftest_layers();
    test_selftest_variables();
    test_selftest_stop_notice();
    test_selftest_proof();
    test_selftest_edges();
    puts("temporal_math: ok");
    return 0;
}
