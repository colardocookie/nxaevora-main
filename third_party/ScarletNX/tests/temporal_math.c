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

static void test_mip_bias(void) {
    assert(close_to(temporal_mip_bias(0.0f, -0.5f, 15.0f), -0.5, 1e-6));  /* ours alone */
    assert(close_to(temporal_mip_bias(-0.25f, -0.5f, 15.0f), -0.75, 1e-6)); /* added to the game's */
    assert(close_to(temporal_mip_bias(1.0f, -0.5f, 15.0f), 0.5, 1e-6));
    assert(close_to(temporal_mip_bias(-3.9f, -0.5f, 4.0f), -4.0, 1e-6)); /* within what the device takes */
    assert(close_to(temporal_mip_bias(5.0f, 0.0f, 4.0f), 4.0, 1e-6));
}

/* The matrix a block had a frame ago: its own when the address is bound every frame, the pair's other slot when
   the block takes turns, none when the other slot belongs to another object. */
static void test_before_choice(void) {
    float now[16], near[16], far[16];
    world(0.5f, 1, 10, 0, 40, now);
    world(0.5f, 1, 9.9f, 0, 40, near);
    world(0.1f, 1, 13, 0, 42, far);
    assert(temporal_before_choice(now, NULL, NULL, 0) == TEMPORAL_BEFORE_NONE);
    assert(temporal_before_choice(now, NULL, near, 2) == TEMPORAL_BEFORE_NONE);
    assert(temporal_before_choice(now, far, near, 1) == TEMPORAL_BEFORE_OWN);
    assert(temporal_before_choice(now, NULL, near, 1) == TEMPORAL_BEFORE_OWN);
    /* a pair in steady motion: a frame ago is nearer than two frames ago */
    assert(temporal_before_choice(now, near, far, 2) == TEMPORAL_BEFORE_PARTNER);
    assert(temporal_before_choice(now, near, NULL, 0) == TEMPORAL_BEFORE_PARTNER);
    assert(temporal_before_choice(now, near, far, 3) == TEMPORAL_BEFORE_PARTNER);
    /* a still pair: both equal now, the partner stands */
    assert(temporal_before_choice(now, now, now, 2) == TEMPORAL_BEFORE_PARTNER);
    /* the other slot holds another still object, 3 units away, and this address held this object two frames ago */
    assert(temporal_before_choice(now, far, now, 2) == TEMPORAL_BEFORE_UNPAIRED);
    assert(temporal_before_choice(now, far, near, 2) == TEMPORAL_BEFORE_UNPAIRED);
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
    test_mip_bias();
    test_before_choice();
    test_origin_shift();
    puts("temporal_math: ok");
    return 0;
}
