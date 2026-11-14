/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The arithmetic of the temporal passes (gpu/vk/temporal_math.h): the jitter, an object's vector matrices and the
   resolve's filter, clip and weights. */
#include "nx/gpu/vk/temporal_math.h"
#include "nx/gpu/vk/upscaler_selftest.h"
#include <assert.h>
#include <stdio.h>

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
/* one frame through the gate as the renderer would drive it: decided, perhaps disagreed with, resolved */
enum { RECT_FINE, RECT_EARLY, RECT_LATE };
static int rect_frame(TemporalRectGate* g, unsigned frame, const TemporalRectState* s, int fault, int* history) {
    const TemporalRectStamp stamp = {0x5508c0000ull, 1, s->target_w, s->target_h, s->target_w, s->target_h, s->rect};
    const int jitter = temporal_rect_decide(g, frame, fault == RECT_EARLY ? NULL : s);
    if (fault == RECT_LATE) {
        TemporalRectState other = *s;
        other.rect.w -= 2;
        other.viewport.width -= 2.0f;
        assert(temporal_rect_late(g, frame, &other));
    } else
        assert(temporal_rect_late(g, frame, s) == (fault == RECT_EARLY)); /* the same again is no disagreement */
    *history = temporal_rect_history(g, frame, &stamp);
    const int steady = temporal_rect_steady(g, frame);
    assert(steady == (jitter && fault == RECT_FINE));
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
    assert(temporal_rect_structure(&whole, 3840, 2160, 1.0f, 1.0f, TEMPORAL_CORNER_TOP_LEFT, &s) == TEMPORAL_RECT_OK);
    assert(s.rect.w == 3840 && s.rect.h == 2160 && s.target_w == 3840 && s.fx == 1.0f);
    /* A whole target is understood only with a fraction of 1 within 1e-6. Reported as partly used it is refused,
       also at 0.9998, whose size rounds to the whole target within a pixel. */
    assert(temporal_rect_structure(&whole, 3840, 2160, 0.9999995f, 0.9999995f, 0, &s) == TEMPORAL_RECT_OK);
    assert(temporal_rect_structure(&whole, 3840, 2160, 0.75f, 0.75f, 0, &s) == TEMPORAL_RECT_SHAPE);
    assert(temporal_rect_structure(&whole, 3840, 2160, 0.5f, 0.5f, 0, &s) == TEMPORAL_RECT_SHAPE);
    assert(temporal_rect_structure(&whole, 3840, 2160, 0.9998f, 0.9998f, 0, &s) == TEMPORAL_RECT_SHAPE);
    assert(temporal_rect_structure(&whole, 3840, 2160, 0.99999f, 0.99999f, 0, &s) == TEMPORAL_RECT_SHAPE);
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
                    assert(temporal_rect_structure(&v, tw, th, f, f, game, &s) ==
                           (game == corner ? TEMPORAL_RECT_OK : TEMPORAL_RECT_SHAPE));
                assert(temporal_rect_structure(&v, tw, th, f, f, corner, &s) == TEMPORAL_RECT_OK);
                assert(s.rect.w == (int)w && s.rect.h == (int)h && s.rect.x == (left ? 0 : tw - (int)w));
                assert(s.rect.y == (top ? 0 : th - (int)h) && s.fx == f && s.fy == f);
            }
            /* one pixel off the fraction's size passes, two do not, on either axis */
            const TemporalViewport one = {0.0f, h + 1.0f, w - 1.0f, -(h + 1.0f)}, two = {0.0f, h, w + 2.0f, -h};
            const TemporalViewport tall = {0.0f, h - 2.0f, w, -(h - 2.0f)};
            assert(temporal_rect_structure(&one, tw, th, f, f, TEMPORAL_CORNER_TOP_LEFT, &s) == TEMPORAL_RECT_OK);
            assert(temporal_rect_structure(&two, tw, th, f, f, TEMPORAL_CORNER_TOP_LEFT, &s) == TEMPORAL_RECT_SHAPE);
            assert(temporal_rect_structure(&tall, tw, th, f, f, TEMPORAL_CORNER_TOP_LEFT, &s) == TEMPORAL_RECT_SHAPE);
        }
    /* The fraction: finite and in (0, 1], both the same within 1e-6. The game's table has pairs that differ. */
    const float fractions[] = {0.0f, -0.5f, 1.0001f, 2.0f, NAN, INFINITY, -INFINITY};
    for (unsigned i = 0; i < sizeof fractions / sizeof *fractions; i++) {
        assert(temporal_rect_structure(&up, 3840, 2160, fractions[i], 0.75f, 0, &s) == TEMPORAL_RECT_FRACTION);
        assert(temporal_rect_structure(&up, 3840, 2160, 0.75f, fractions[i], 0, &s) == TEMPORAL_RECT_FRACTION);
    }
    assert(temporal_rect_structure(&up, 3840, 2160, 0.9f, 0.85f, 0, &s) == TEMPORAL_RECT_UNEQUAL);
    assert(temporal_rect_structure(&up, 3840, 2160, 0.75f, 0.75001f, 0, &s) == TEMPORAL_RECT_UNEQUAL);
    assert(temporal_rect_structure(&up, 3840, 2160, 0.75f, 0.75f, 0, &s) == TEMPORAL_RECT_OK);
    /* a fraction that does not fit the rectangle, a viewport that is none, a target without a size */
    assert(temporal_rect_structure(&up, 3840, 2160, 0.5f, 0.5f, 0, &s) == TEMPORAL_RECT_SHAPE);
    assert(temporal_rect_structure(&bad[0], 3840, 2160, 0.75f, 0.75f, 0, &s) == TEMPORAL_RECT_VIEWPORT);
    assert(temporal_rect_structure(&up, 0, 2160, 0.75f, 0.75f, 0, &s) == TEMPORAL_RECT_VIEWPORT);

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
    /* the same rectangle in another target size, or with another fraction, is a change */
    const TemporalRectState other_target = rect_state(2880, 1620, 0.75f, 3840, 2176);
    TemporalRectState other_fraction = a;
    other_fraction.fx = other_fraction.fy = 0.7501f;
    assert(!rect_frame(&g, f++, &other_target, RECT_FINE, &history));
    assert(!rect_frame(&g, f++, &other_fraction, RECT_FINE, &history));
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
        assert(temporal_rect_structure(&up, 3840, 2160, 0.75f, 0.75f, 0, &up_way) == TEMPORAL_RECT_OK);
        assert(temporal_rect_structure(&flipped, 3840, 2160, 0.75f, 0.75f, 0, &down_way) == TEMPORAL_RECT_OK);
        assert(temporal_rect_same(&up_way.rect, &down_way.rect) && !temporal_rect_state_same(&up_way, &down_way));
        assert(up_way.viewport.height == -1620.0f && down_way.viewport.height == 1620.0f);
        memset(&g, 0, sizeof g);
        for (f = 500; f < 503; f++) assert(rect_frame(&g, f, &up_way, RECT_FINE, &history) == (f > 500));
        assert(history);
        /* late, in a steady frame with history */
        assert(temporal_rect_decide(&g, 503, &up_way) && temporal_rect_steady(&g, 503));
        assert(temporal_rect_late(&g, 503, &down_way) && !temporal_rect_steady(&g, 503));
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
        assert(!temporal_rect_state_same(&up_way, &moved));
    }
    /* a late disagreement leaves the frame not steady even after history was read for it, and no history is kept */
    memset(&g, 0, sizeof g);
    for (f = 200; f < 203; f++) rect_frame(&g, f, &a, RECT_FINE, &history);
    assert(history && temporal_rect_decide(&g, 203, &a) && temporal_rect_steady(&g, 203));
    assert(temporal_rect_late(&g, 203, &b) && !temporal_rect_steady(&g, 203) && !g.have_stamp && !g.have_candidate);
    assert(temporal_rect_late(&g, 203, &a)); /* it stays not valid, whatever comes after */
    /* The history's identity: another target, another making of it, another size, output size or rectangle, and
       it is not used. The same, and it is. */
    memset(&g, 0, sizeof g);
    const TemporalRectStamp made = {0x5508c0000ull, 3, 3840, 2160, 3840, 2160, {0, 0, 2880, 1620}};
    for (f = 300; f < 302; f++) temporal_rect_decide(&g, f, &a);
    temporal_rect_wrote(&g, 301, &made);
    assert(temporal_rect_decide(&g, 302, &a) && temporal_rect_history(&g, 302, &made));
    TemporalRectStamp changed[6] = {made, made, made, made, made, made};
    changed[0].target++;
    changed[1].generation++;
    changed[2].target_w = 1920;
    changed[3].out_h = 1080;
    changed[4].rect.w--;
    changed[5].rect.y = 540;
    for (int i = 0; i < 6; i++) assert(!temporal_rect_history(&g, 302, &changed[i]));
    /* a gated frame writes no history */
    memset(&g, 0, sizeof g);
    temporal_rect_decide(&g, 400, &a);
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
    const TemporalRectStamp full_stamp = {0x5508c0000ull, 1, 1920, 1080, 1920, 1080, {0, 0, 1920, 1080}};
    const TemporalRectStamp scaled_stamp = {0x5508c0000ull, 1, 1920, 1080, 1920, 1080, {0, 0, 1440, 810}};
    const double jitter[2] = {0.5 / 1920.0, 0.75 / 1080.0};
    TemporalRectGate g;
    TemporalUpscalerFrame out;
    memset(&g, 0, sizeof g);
    /* a gated frame is not given to the upscaler */
    assert(!temporal_rect_decide(&g, 10, &full));
    assert(!temporal_upscaler_frame(&g, 10, &full_stamp, &whole, jitter, 0, &out));
    /* the first accepted frame has the reset, the next has not, once the one before was evaluated with success */
    assert(temporal_rect_decide(&g, 11, &full));
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
    assert(temporal_rect_decide(&g, 12, &full));
    assert(temporal_upscaler_frame(&g, 12, &full_stamp, &whole, jitter, 0, &out) && !out.reset);
    /* a camera cut resets while the frame is steady */
    assert(temporal_upscaler_frame(&g, 12, &full_stamp, &whole, jitter, 1, &out) && out.reset);
    /* an evaluation that failed wrote nothing: the frame after has the reset */
    assert(temporal_rect_decide(&g, 13, &full));
    assert(temporal_upscaler_frame(&g, 13, &full_stamp, &whole, jitter, 0, &out) && out.reset);
    temporal_rect_wrote(&g, 13, &full_stamp);
    /* A resize: the scene goes into the rectangle of 0.75. The frame of the change is not evaluated, the next has
       the reset, the rectangle, a jitter sized by the rectangle and the vector scale of the whole target still. */
    assert(!temporal_rect_decide(&g, 14, &scaled));
    assert(!temporal_upscaler_frame(&g, 14, &scaled_stamp, &part, jitter, 0, &out));
    assert(temporal_rect_decide(&g, 15, &scaled));
    assert(temporal_upscaler_frame(&g, 15, &scaled_stamp, &part, jitter, 0, &out) && out.reset);
    assert(out.render.w == 1440 && out.render.h == 810 && out.target_w == 1920 && out.out_w == 1920);
    assert(close_to(out.jitter[0], 0.25 * 0.75, 1e-12) && close_to(out.jitter[1], -0.375 * 0.75, 1e-12));
    assert(out.vector_scale[0] == -1920.0 && out.vector_scale[1] == -1080.0);
    temporal_rect_wrote(&g, 15, &scaled_stamp);
    assert(temporal_rect_decide(&g, 16, &scaled));
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
        temporal_rect_decide(&h, 30, &vast_state);
        assert(temporal_rect_decide(&h, 31, &vast_state) && temporal_rect_steady(&h, 31));
        assert(!temporal_upscaler_frame(&h, 31, &scaled_stamp, &vast, tiny, 0, &out));
        assert(!temporal_upscaler_frame(&h, 31, &scaled_stamp, &vast, jitter, 0, &out));
    }
    /* a history made for another output size is not used: reset */
    other = scaled_stamp;
    other.out_w = 2560;
    assert(temporal_upscaler_frame(&g, 16, &other, &part, jitter, 0, &out) && out.reset && out.out_w == 2560);
    /* a late disagreement after the numbers were made: the frame is no longer steady */
    assert(temporal_rect_late(&g, 16, &full));
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
            temporal_rect_decide(&cut, 40, &scaled);
            assert(temporal_rect_decide(&cut, 41, &scaled));
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
        temporal_rect_decide(&g, 19, &full);
        for (unsigned frame = 20; frame < 24; frame++) {
            assert(temporal_rect_decide(&g, frame, &full));
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
    test_upscaler();
    test_selftest();
    puts("temporal_math: ok");
    return 0;
}
