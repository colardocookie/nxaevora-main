/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The arithmetic of the temporal passes (gpu/vk/temporal_math.h): the jitter, an object's vector matrices and the
   resolve's filter, clip and weights. */
#include "nx/gpu/vk/temporal_math.h"
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
        temporal_stream_used(now, frame, pair, draw);
        if (who[i] == ask) got = temporal_stream_before(now, last, frame, pair, draw);
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
    temporal_stream_used(&copy[41 & 1], 41, 0x2000, 77);
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
    temporal_stream_used(&k->copy[turn], frame, pair, draw);
    const int got = temporal_stream_before(&k->copy[turn], &k->copy[!turn], frame, pair, draw);
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
    temporal_stream_used(&t.copy[x][0], 10, 7, 77);
    assert(temporal_stream_before(&t.copy[x][1], &t.copy[x][0], 11, 7, 77) == 1);
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
    puts("temporal_math: ok");
    return 0;
}
