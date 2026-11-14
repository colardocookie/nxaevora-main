/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The arithmetic of the temporal passes (temporal.c and its shaders) without the renderer, so a host test can run
   it. Matrices are 4 x 4 in memory order with row vectors, as the game keeps them: clip = position x matrix. The
   functions for the resolve mirror shaders/temporal_resolve.frag, which holds the same formulas in GLSL. */
#ifndef SNX_VK_TEMPORAL_MATH_H
#define SNX_VK_TEMPORAL_MATH_H
#include <math.h>
#include <stdio.h>
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

/* A skinned character's three matrices for its vector draw, from a position in the object's own space: drawn takes
   it to this frame's clip with the jitter (where the game drew it), now to this frame's clip without, before takes
   last frame's position through last frame's world matrix and view-projection. */
static inline void temporal_skin_matrices(const float vp[16], const float world[16], const float world_before[16],
                                          const float vp_before[16], double jx, double jy, float drawn[16],
                                          float now[16], float before[16]) {
    double v[16], w[16], wb[16], vb[16], a[16], b[16];
    for (int i = 0; i < 16; i++) {
        v[i] = (double)vp[i];
        w[i] = (double)world[i];
        wb[i] = (double)world_before[i];
        vb[i] = (double)vp_before[i];
    }
    temporal_multiply_d(w, v, a);
    temporal_multiply_d(wb, vb, b);
    for (int i = 0; i < 16; i++) {
        now[i] = (float)a[i];
        before[i] = (float)b[i];
    }
    for (int r = 0; r < 4; r++) { /* a x J: clip x and y gain jitter x w */
        a[4 * r + 0] += jx * a[4 * r + 3];
        a[4 * r + 1] += jy * a[4 * r + 3];
    }
    for (int i = 0; i < 16; i++) drawn[i] = (float)a[i];
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
/* The sharpen after the resolve, for one colour channel already taken to 0..1: the middle c, the sum of its four
   direct neighbours, the least and greatest of the 3 x 3, the amount 0 to 1. The neighbours count with a negative
   weight that falls with the room the neighbourhood leaves to the ends of the range, and the result stays within
   least and greatest. */
static inline float temporal_sharpen(float c, float cross, float least, float greatest, float amount) {
    float room = (least < 1.0f - greatest ? least : 1.0f - greatest) / (greatest > 0.0001f ? greatest : 0.0001f);
    if (room < 0.0f) room = 0.0f;
    if (room > 1.0f) room = 1.0f;
    const float w = -sqrtf(room) * 0.2f * amount;
    float sharp = (c + cross * w) / (1.0f + 4.0f * w);
    if (sharp < least) sharp = least;
    if (sharp > greatest) sharp = greatest;
    return sharp;
}
/* The sharpen of one pixel as shaders/temporal_sharpen.frag does it: n is the 3x3 neighbourhood in rows, the pixel
   in the middle. Each colour is squeezed under 1 by its largest channel, sharpened per channel and widened again,
   then held to each channel's least and greatest of the neighbourhood as the picture holds them. */
static inline void temporal_sharpen_rgb(const float n[9][3], float amount, float out[3]) {
    float squeezed[9][3], sharp[3], top = 0;
    for (int i = 0; i < 9; i++) {
        float most = 0;
        for (int c = 0; c < 3; c++) most = n[i][c] > most ? n[i][c] : most;
        for (int c = 0; c < 3; c++) squeezed[i][c] = (n[i][c] > 0 ? n[i][c] : 0) / (1.0f + most);
    }
    for (int c = 0; c < 3; c++) {
        float least = squeezed[4][c], greatest = squeezed[4][c];
        for (int i = 0; i < 9; i++) {
            least = squeezed[i][c] < least ? squeezed[i][c] : least;
            greatest = squeezed[i][c] > greatest ? squeezed[i][c] : greatest;
        }
        const float cross = squeezed[1][c] + squeezed[3][c] + squeezed[5][c] + squeezed[7][c];
        sharp[c] = temporal_sharpen(squeezed[4][c], cross, least, greatest, amount);
        top = sharp[c] > top ? sharp[c] : top;
    }
    for (int c = 0; c < 3; c++) {
        float least = n[4][c] > 0 ? n[4][c] : 0, greatest = least;
        for (int i = 0; i < 9; i++) {
            const float v = n[i][c] > 0 ? n[i][c] : 0;
            least = v < least ? v : least;
            greatest = v > greatest ? v : greatest;
        }
        out[c] = sharp[c] / (1.0f - top > 0.0001f ? 1.0f - top : 0.0001f);
        if (out[c] < least) out[c] = least;
        if (out[c] > greatest) out[c] = greatest;
    }
}
/* the texture level shift a sampler gets: the game's own plus ours, within what the device takes */
static inline float temporal_mip_bias(float own, float shift, float device_most) {
    float bias = own + shift;
    if (bias > device_most) bias = device_most;
    if (bias < -device_most) bias = -device_most;
    return bias;
}
/* The two ends of the camera's vector for a pixel, as shaders/temporal_motion.frag makes them. ndc is the pixel's
   place as drawn, with the jitter, and z its depth. Both ends come out without jitter: where the point is now and
   where it was under the camera of a frame ago. 0 when it was behind that camera. */
static inline int temporal_camera_points(const float inverse_vp[16], const float vp_before[16], double ndc_x,
                                         double ndc_y, double z, double jx, double jy, double now[2],
                                         double before[2]) {
    double inverse[16], was[16], both[16];
    for (int i = 0; i < 16; i++) {
        inverse[i] = (double)inverse_vp[i];
        was[i] = (double)vp_before[i];
    }
    temporal_multiply_d(inverse, was, both);
    const double p[4] = {ndc_x - jx, ndc_y - jy, z, 1.0};
    double clip[4];
    for (int c = 0; c < 4; c++)
        clip[c] = p[0] * both[c] + p[1] * both[4 + c] + p[2] * both[8 + c] + p[3] * both[12 + c];
    now[0] = p[0];
    now[1] = p[1];
    if (!(clip[3] > 0.0)) return 0;
    before[0] = clip[0] / clip[3];
    before[1] = clip[1] / clip[3];
    return 1;
}
/* The viewport gate. The jitter and the resolve hold only while the scene's viewport is the whole target and the
   one of the frame before. Each frame is decided once (temporal_gate_decide), and a frame is steady, and may be
   resolved and kept as history, when it was decided with jitter and its G-buffer agreed. Frames count from 1. */
typedef struct {
    float x, y, width, height; /* height negative when y runs up: then y is the bottom edge */
} TemporalViewport;
typedef struct {
    TemporalViewport decided;
    unsigned decided_at, mismatch_at, early_at;
    int jitter;
} TemporalGate;
static inline int temporal_viewport_same(const TemporalViewport* a, const TemporalViewport* b) {
    return a->x == b->x && a->y == b->y && a->width == b->width && a->height == b->height;
}
/* The frame's first draw that binds the scene camera's block and draws into the scene's depth decides, from its
   own viewport: 1 for jitter. */
static inline int temporal_gate_decide(TemporalGate* g, unsigned frame, const TemporalViewport* v, float target_width,
                                       float target_height) {
    const int whole = target_width > 0.0f && target_height > 0.0f && v->x == 0.0f &&
                      fabsf(v->width) == target_width && fabsf(v->height) == target_height &&
                      v->y == (v->height < 0.0f ? target_height : 0.0f);
    const int same = g->decided_at && g->decided_at + 1 == frame && temporal_viewport_same(v, &g->decided);
    g->decided = *v;
    g->decided_at = frame;
    g->jitter = whole && same && g->early_at != frame;
    return g->jitter;
}
/* A draw that binds the block before the decision cannot be tied to the scene and gates the whole frame, since no
   two draws of a frame may differ in jitter: 1 when it did, 0 when the frame was decided already. */
static inline int temporal_gate_early(TemporalGate* g, unsigned frame) {
    if (g->decided_at == frame) return 0;
    g->early_at = frame;
    return 1;
}
static inline int temporal_gate_jitter(const TemporalGate* g, unsigned frame) {
    return g->decided_at == frame && g->jitter;
}
/* the G-buffer's viewport held against the decided one: 1 for a mismatch */
static inline int temporal_gate_scene(TemporalGate* g, unsigned frame, const TemporalViewport* scene) {
    if (!temporal_gate_jitter(g, frame) || temporal_viewport_same(scene, &g->decided)) return 0;
    g->mismatch_at = frame;
    return 1;
}
static inline int temporal_gate_steady(const TemporalGate* g, unsigned frame) {
    return temporal_gate_jitter(g, frame) && g->mismatch_at != frame;
}
/* Which matrix an object's block had a frame ago. The renderer does not see which object wrote a block, so this is
   learned and stays a guess: two slots one bound size apart are a pair once exactly one of them was bound in each
   of TEMPORAL_PAIR_FRAMES frames in a row, in turns, by the same draws. A slot bound by the same draws in that many
   frames in a row takes no turns and is its own. Anything else has no matrix of a frame ago. */
#define TEMPORAL_PAIR_FRAMES 4
/* what a draw draws with, in one number that is never 0 */
static inline unsigned long long temporal_draw_signature(unsigned long long vertex, unsigned long long index,
                                                         unsigned count, unsigned first,
                                                         unsigned long long program) {
    const unsigned long long words[5] = {vertex, index, count, first, program};
    unsigned long long h = 0xcbf29ce484222325ull;
    for (int i = 0; i < 5; i++) {
        h = (h ^ words[i]) * 0x100000001b3ull;
        h ^= h >> 29;
    }
    return h ? h : 1;
}
/* A slot whose matrix is bit for bit its own of two frames ago and not the other slot's of a frame ago: two things
   at rest, not one that moves. A true move of 0, 1, 0 looks the same and is given up with it. */
static inline int temporal_at_rest_alone(const float now[16], const float* own_two_ago, const float* partner) {
    return own_two_ago && partner && !memcmp(now, own_two_ago, 16 * sizeof(float)) &&
           memcmp(now, partner, 16 * sizeof(float)) != 0;
}
/* one complete frame of a slot that was bound in it */
typedef struct {
    int bound_before;    /* this slot was bound the frame before too */
    int partner_now;     /* the other slot was bound this frame */
    int partner_before;  /* the other slot was bound the frame before */
    int same_as_partner; /* this frame's draws here are the frame before's draws there */
    int same_as_own;     /* this frame's draws here are the frame before's draws here */
    int at_rest_alone;   /* temporal_at_rest_alone */
    unsigned partner_turns, own_stays; /* the other slot's turns and this slot's stays as judged a frame ago */
} TemporalSlotFrame;
/* The frames in a row, this one included, in which the pair took turns (turns) or the slot was bound alone by the
   same draws (stays). Either falls to 0 at once when its rule is broken. Both count from the second frame, so
   TEMPORAL_PAIR_FRAMES frames in a row give TEMPORAL_PAIR_FRAMES - 1. */
static inline void temporal_pair_judge(const TemporalSlotFrame* f, unsigned* turns, unsigned* stays) {
    const int turn =
        !f->partner_now && f->partner_before && !f->bound_before && f->same_as_partner && !f->at_rest_alone;
    const int stay = f->bound_before && f->same_as_own;
    *turns = turn ? f->partner_turns + 1 : 0;
    *stays = stay ? f->own_stays + 1 : 0;
}
/* For a draw of this frame, before the frame is complete: where its block's matrix of a frame ago is. own_last and
   partner_last: bound a frame ago. partner_now: bound this frame already. own_stays and partner_turns as judged for
   the frame before, own_has_draw and partner_has_draw: this draw was among that slot's draws a frame ago. */
enum {
    TEMPORAL_BEFORE_NONE,     /* nothing bound a frame ago here or there, or both slots bound */
    TEMPORAL_BEFORE_OWN,      /* this slot, which takes no turns */
    TEMPORAL_BEFORE_PARTNER,  /* the other slot of an established pair */
    TEMPORAL_BEFORE_LEARNING, /* not yet TEMPORAL_PAIR_FRAMES frames in a row */
    TEMPORAL_BEFORE_OTHER,    /* established, but not by this draw */
    TEMPORAL_BEFORE_LINK      /* a slot that is not the neighbour, found by its draws (temporal_link_before) */
};
static inline int temporal_before_choice(int own_last, unsigned own_stays, int own_has_draw, int partner_last,
                                         int partner_now, unsigned partner_turns, int partner_has_draw) {
    if (own_last) {
        if (own_stays + 1 < TEMPORAL_PAIR_FRAMES) return TEMPORAL_BEFORE_LEARNING;
        return own_has_draw ? TEMPORAL_BEFORE_OWN : TEMPORAL_BEFORE_OTHER;
    }
    if (!partner_last || partner_now) return TEMPORAL_BEFORE_NONE;
    if (partner_turns + 1 < TEMPORAL_PAIR_FRAMES) return TEMPORAL_BEFORE_LEARNING;
    return partner_has_draw ? TEMPORAL_BEFORE_PARTNER : TEMPORAL_BEFORE_OTHER;
}
/* The class of a pixel's vector, kept in the motion image's alpha. Where an object's own motion (its vector less the
   camera's of the same pixel) is over a bound, whose surface the history holds cannot be verified, so history is
   refused: the pixel is rejected and shows the frame as drawn. Valid history of fast objects is lost too. The
   shaders hold the same three values. */
#define TEMPORAL_CLASS_CAMERA 0
#define TEMPORAL_CLASS_OBJECT 1
#define TEMPORAL_CLASS_REJECTED 2
/* the bound in pixels for a scene of this height: an eighth of it, 0 for off */
static inline double temporal_reject_bound(int on, double height) { return on ? height / 8.0 : 0.0; }
/* The class of a pixel. object: an object's vector owns it. The vectors are in parts of the image. An object's
   vector is kept only when all of this holds: there is a camera vector (camera_valid), every number is finite and
   the two are at most the bound apart. Anything else is rejected, a NaN included. */
static inline int temporal_vector_class(int object, int camera_valid, const double vector[2], const double camera[2],
                                        double width, double height, double bound) {
    if (!object) return TEMPORAL_CLASS_CAMERA;
    if (!(bound > 0.0)) return TEMPORAL_CLASS_OBJECT;
    if (!camera_valid) return TEMPORAL_CLASS_REJECTED;
    const double dx = (vector[0] - camera[0]) * width, dy = (vector[1] - camera[1]) * height;
    const double apart = dx * dx + dy * dy;
    const int finite = isfinite(vector[0]) && isfinite(vector[1]) && isfinite(camera[0]) && isfinite(camera[1]) &&
                       isfinite(apart);
    return finite && apart <= bound * bound ? TEMPORAL_CLASS_OBJECT : TEMPORAL_CLASS_REJECTED;
}
/* The motion image's texel: the vector, what an object's vector carries (the depth its point had a frame ago, or
   the draw's number in the probe) and the class. Only alpha says the class, whatever the other three hold. */
static inline void temporal_motion_texel(const double vector[2], double carried, int class_, float texel[4]) {
    texel[0] = (float)vector[0];
    texel[1] = (float)vector[1];
    texel[2] = class_ == TEMPORAL_CLASS_CAMERA ? 0.0f : (float)carried;
    texel[3] = (float)class_;
}
static inline int temporal_motion_class(const float texel[4]) {
    return texel[3] > 1.5f ? TEMPORAL_CLASS_REJECTED : texel[3] > 0.5f ? TEMPORAL_CLASS_OBJECT : TEMPORAL_CLASS_CAMERA;
}
/* The resolve: 1 when the pixel shows the frame as drawn. centre is the pixel's own texel, selected the one whose
   vector it takes (another one only with the vector of the nearest depth). */
static inline int temporal_current_only(const float centre[4], const float selected[4]) {
    return temporal_motion_class(centre) == TEMPORAL_CLASS_REJECTED ||
           temporal_motion_class(selected) == TEMPORAL_CLASS_REJECTED;
}
/* Links: a block's other slot found by its draws where the two slots are not neighbours. Every object block a plain
   G-buffer draw binds in a frame is recorded with its bound size, its matrix and a digest of its draws: their
   number, the sum of their signatures and the first eight in order. Equal digests are not proof of equal draws,
   and equal draws are not proof of one object: a link is a guess, as a pair is. */
#define TEMPORAL_BLOCKS 4096      /* blocks recorded in a frame at most */
#define TEMPORAL_BLOCK_SLOTS 8192 /* of the frame's index by address and of the judging's by digest */
#define TEMPORAL_BLOCK_DRAWS 8
typedef struct {
    unsigned long long address, sum, link; /* link: the other slot's address as judged for this frame, 0 for none */
    unsigned long long first[TEMPORAL_BLOCK_DRAWS];
    unsigned size, count, turns;  /* turns: frames in a row the same two slots took turns, this one included */
    unsigned char usable, served; /* usable: an affine matrix and one size. served: a pair or its own slot had it */
    float world[16];
} TemporalBlock;
/* One frame's record. complete 0: a block or a draw was not seen, so nothing in it proves a block is the only one. */
typedef struct {
    unsigned frame, n;
    int complete;
    TemporalBlock blocks[TEMPORAL_BLOCKS];
    unsigned short slots[TEMPORAL_BLOCK_SLOTS]; /* by address: a block's number plus one */
} TemporalFrame;
static inline void temporal_frame_begin(TemporalFrame* f, unsigned frame) {
    f->frame = frame;
    f->n = 0;
    f->complete = 1;
    memset(f->slots, 0, sizeof f->slots);
}
static inline unsigned temporal_block_slot(unsigned long long address) {
    return (unsigned)(((address >> 8) * 0x9E3779B97F4A7C15ull) >> 40) % TEMPORAL_BLOCK_SLOTS;
}
static inline TemporalBlock* temporal_frame_find(const TemporalFrame* f, unsigned long long address) {
    if (!f) return NULL;
    for (unsigned at = temporal_block_slot(address);; at = (at + 1) % TEMPORAL_BLOCK_SLOTS) {
        const unsigned short number = f->slots[at];
        if (!number) return NULL;
        if (f->blocks[number - 1].address == address) return (TemporalBlock*)&f->blocks[number - 1];
    }
}
/* Whether the links' record takes a draw with an object block. by_address: its depth target is at the scene depth's
   address. by_surface: it is the surface the pairs go by. camera: the frame's camera block is known. Off, no draw
   is ever taken, so nothing of the record runs. */
enum {
    TEMPORAL_SCENE_NOT,   /* not a draw into the scene, or the links are off */
    TEMPORAL_SCENE_SEEN,  /* recorded */
    TEMPORAL_SCENE_UNSEEN /* a draw into the scene the record cannot place: the frame is not complete */
};
static inline int temporal_scene_gate(int on, int has_block, int by_address, int by_surface, int camera) {
    if (!on || !has_block || (!by_address && !by_surface)) return TEMPORAL_SCENE_NOT;
    return by_address && by_surface && camera ? TEMPORAL_SCENE_SEEN : TEMPORAL_SCENE_UNSEEN;
}
/* A draw binds a block: the block's record in this frame with the draw in its digest. NULL without a record, or
   when it is full, and the frame is then not complete. A block bound with two sizes keeps its first and is not
   usable. */
static inline TemporalBlock* temporal_frame_bind(TemporalFrame* f, unsigned long long address, unsigned size,
                                                 const float world[16], int affine, unsigned long long signature) {
    if (!f) return NULL;
    TemporalBlock* b = temporal_frame_find(f, address);
    if (!b) {
        if (f->n >= TEMPORAL_BLOCKS) {
            f->complete = 0;
            return NULL;
        }
        unsigned at = temporal_block_slot(address);
        while (f->slots[at]) at = (at + 1) % TEMPORAL_BLOCK_SLOTS;
        f->slots[at] = (unsigned short)(f->n + 1);
        b = &f->blocks[f->n++];
        memset(b, 0, sizeof *b);
        b->address = address;
        b->size = size;
        b->usable = affine != 0;
        memcpy(b->world, world, sizeof b->world);
    }
    if (b->size != size || !affine) b->usable = 0;
    if (b->count < TEMPORAL_BLOCK_DRAWS) b->first[b->count] = signature;
    if (b->count < 0xFFFFFFFFu) b->count++;
    b->sum += signature;
    return b;
}
/* the comparison a link stands on: size, number of draws, their sum and the first eight in order */
static inline int temporal_block_same(const TemporalBlock* a, const TemporalBlock* b) {
    if (a->size != b->size || a->count != b->count || a->sum != b->sum) return 0;
    for (unsigned i = 0; i < a->count && i < TEMPORAL_BLOCK_DRAWS; i++)
        if (a->first[i] != b->first[i]) return 0;
    return 1;
}
typedef struct {
    unsigned short heads[2][TEMPORAL_BLOCK_SLOTS], next[2][TEMPORAL_BLOCKS]; /* [0] this frame, [1] the one before */
} TemporalLinkScratch;
static inline unsigned temporal_digest_slot(const TemporalBlock* b) {
    const unsigned long long h = (b->sum ^ ((unsigned long long)b->size << 32) ^ b->count) * 0x9E3779B97F4A7C15ull;
    return (unsigned)(h >> 40) % TEMPORAL_BLOCK_SLOTS;
}
/* The blocks of a list that are the same as b by the comparison above, counted to two at most, and the first found
   in one. Every block of the list counts, whatever else is known of it. */
static inline unsigned temporal_link_rivals(const TemporalFrame* f, const unsigned short* heads,
                                            const unsigned short* next, const TemporalBlock* b,
                                            const TemporalBlock** one) {
    unsigned count = 0;
    for (unsigned short number = heads[temporal_digest_slot(b)]; number && count < 2; number = next[number - 1])
        if (temporal_block_same(&f->blocks[number - 1], b) && !count++) *one = &f->blocks[number - 1];
    return count;
}
/* Every block of now gets its link and turns anew. P, bound now and not the frame before, is linked to Q, bound the
   frame before and not now, when Q is the only such block the same as P and P the only such one the same as Q,
   both are usable, neither is served and P is not at rest alone. All counts are made before any link. Without
   two complete records of consecutive frames nothing is linked: every link ends and the turns start again. */
static inline void temporal_links_judge(TemporalFrame* now, const TemporalFrame* before, const TemporalFrame* older,
                                        TemporalLinkScratch* s) {
    for (unsigned i = 0; i < now->n; i++) now->blocks[i].link = now->blocks[i].turns = 0;
    if (!now->complete || !before || !before->complete || before->frame + 1 != now->frame) return;
    memset(s->heads, 0, sizeof s->heads);
    for (int side = 0; side < 2; side++) {
        const TemporalFrame *own = side ? before : now, *other = side ? now : before;
        for (unsigned i = 0; i < own->n; i++) {
            if (temporal_frame_find(other, own->blocks[i].address)) continue; /* bound in both frames: no turn */
            const unsigned at = temporal_digest_slot(&own->blocks[i]);
            s->next[side][i] = s->heads[side][at];
            s->heads[side][at] = (unsigned short)(i + 1);
        }
    }
    if (older && older->frame + 2 != now->frame) older = NULL;
    for (unsigned i = 0; i < now->n; i++) {
        TemporalBlock* p = &now->blocks[i];
        const TemporalBlock *q = NULL, *back = NULL;
        if (temporal_frame_find(before, p->address)) continue;
        if (temporal_link_rivals(before, s->heads[1], s->next[1], p, &q) != 1) continue;
        if (temporal_link_rivals(now, s->heads[0], s->next[0], q, &back) != 1 || back != p) continue;
        if (!p->usable || !q->usable || p->served || q->served) continue;
        const TemporalBlock* own = temporal_frame_find(older, p->address);
        if (temporal_at_rest_alone(p->world, own ? own->world : NULL, q->world)) continue;
        const unsigned turns = q->link == p->address ? q->turns : 0;
        p->link = q->address;
        p->turns = turns < 0xFFFF ? turns + 1 : turns;
    }
}
/* For a draw of this frame, before the frame is complete: the record of the block's linked slot in the frame
   before, whose matrix is the block's of a frame ago, or NULL with the reason in why. index: the draws that bound
   the block in this frame before this one. The end of the G-buffer judges the frame again (temporal_link_holds). */
enum {
    TEMPORAL_LINK_USED,
    TEMPORAL_LINK_UNKNOWN,  /* a record is not complete or not of the frame before */
    TEMPORAL_LINK_NONE,     /* no link, or the block was bound a frame ago, or the other slot this frame already */
    TEMPORAL_LINK_LEARNING, /* not yet TEMPORAL_PAIR_FRAMES frames in a row */
    TEMPORAL_LINK_OTHER,    /* not the draw the other slot had at this place in its order, or past the eighth */
    TEMPORAL_LINK_REST      /* at rest alone */
};
static inline const TemporalBlock* temporal_link_before(const TemporalFrame* now, const TemporalFrame* before,
                                                        const TemporalFrame* older, unsigned long long address,
                                                        unsigned size, unsigned index, unsigned long long signature,
                                                        const float world[16], int* why) {
    *why = TEMPORAL_LINK_UNKNOWN;
    if (!now->complete || !before || !before->complete || before->frame + 1 != now->frame) return NULL;
    if (!older || older->frame + 2 != now->frame) return NULL;
    *why = TEMPORAL_LINK_NONE;
    const TemporalBlock* own = temporal_frame_find(older, address);
    if (!own || !own->link || temporal_frame_find(before, address)) return NULL;
    const TemporalBlock* q = temporal_frame_find(before, own->link);
    if (!q || q->link != address || !q->usable || q->size != size || temporal_frame_find(now, q->address)) return NULL;
    *why = TEMPORAL_LINK_LEARNING;
    if (q->turns + 1 < TEMPORAL_PAIR_FRAMES) return NULL;
    *why = TEMPORAL_LINK_OTHER;
    if (index >= TEMPORAL_BLOCK_DRAWS || index >= q->count || q->first[index] != signature) return NULL;
    *why = TEMPORAL_LINK_REST;
    if (temporal_at_rest_alone(world, own->world, q->world)) return NULL;
    *why = TEMPORAL_LINK_USED;
    return q;
}
/* After temporal_links_judge on the frame's G-buffer: 1 when the draw's block is still linked to the slot whose
   matrix it took, with the turns a use needs. */
static inline int temporal_link_holds(const TemporalFrame* now, unsigned long long address,
                                      unsigned long long link) {
    const TemporalBlock* p = temporal_frame_find(now, address);
    return p && link && p->link == link && p->turns >= TEMPORAL_PAIR_FRAMES;
}
/* Whose a kept copy of skinned positions is. A copy is kept per capture address and frame, and the address does
   not say whose positions they are. So a copy is tagged with the G-buffer draws that read it, by block pair and
   signature, and a draw gets last frame's copy only with its tag on it and one capture in each frame. A pair is
   its lower slot with other 0. A link is both of its slots, lower and higher, so no two links share a tag. */
#define TEMPORAL_STREAM_OWNERS 4
typedef struct {
    unsigned at, captures, owners; /* the frame of the copy, the captures into its address in it, the tags */
    unsigned generation, copied;   /* of the last capture in that frame, and of the bytes held (0 for none) */
    unsigned long long pair[TEMPORAL_STREAM_OWNERS], other[TEMPORAL_STREAM_OWNERS], draw[TEMPORAL_STREAM_OWNERS];
} TemporalStreamCopy;
/* Every capture into the address is an event, counted before anything can fail: it takes a new generation of the
   address, drops the copy's tags and leaves its content unknown until the bytes were copied. */
static inline void temporal_stream_captured(TemporalStreamCopy* c, unsigned* generation, unsigned frame) {
    if (c->at != frame) {
        memset(c, 0, sizeof *c);
        c->at = frame;
    }
    if (!++*generation) ++*generation; /* never 0 */
    c->captures++;
    c->generation = *generation;
    c->copied = 0;
    c->owners = 0;
}
static inline void temporal_stream_copied(TemporalStreamCopy* c) { c->copied = c->generation; }
/* buffers made anew lose their content, never their counts */
static inline void temporal_stream_poison(TemporalStreamCopy* c) {
    c->copied = 0;
    c->owners = 0;
}
/* a copy counts only while it holds the bytes of its frame's last capture */
static inline int temporal_stream_valid(const TemporalStreamCopy* c) {
    return c->copied != 0 && c->copied == c->generation;
}
static inline int temporal_stream_owned(const TemporalStreamCopy* c, unsigned long long pair,
                                        unsigned long long other, unsigned long long draw) {
    for (unsigned i = 0; i < c->owners; i++)
        if (c->pair[i] == pair && c->other[i] == other && c->draw[i] == draw) return 1;
    return 0;
}
/* a G-buffer draw of an established pair reads this frame's capture: its tag is added to a copy that holds that
   capture's bytes, while there is room */
static inline void temporal_stream_used(TemporalStreamCopy* c, unsigned frame, unsigned long long pair,
                                        unsigned long long other, unsigned long long draw) {
    if (c->at != frame || !temporal_stream_valid(c) || temporal_stream_owned(c, pair, other, draw) ||
        c->owners >= TEMPORAL_STREAM_OWNERS)
        return;
    c->pair[c->owners] = pair;
    c->other[c->owners] = other;
    c->draw[c->owners++] = draw;
}
/* 1 when the draw may take last frame's copy as its positions of a frame ago, 0 when there is none that is its
   own, and -1 when the address was captured more than once in either frame */
static inline int temporal_stream_before(const TemporalStreamCopy* now, const TemporalStreamCopy* last,
                                         unsigned frame, unsigned long long pair, unsigned long long other,
                                         unsigned long long draw) {
    if (now->at != frame || !last->at || last->at + 1 != frame) return 0;
    if (now->captures != 1 || last->captures != 1) return -1;
    if (!temporal_stream_valid(now) || !temporal_stream_valid(last)) return 0;
    return temporal_stream_owned(last, pair, other, draw);
}
/* The frame of a slot's newest capture event, copied or not: what its age is told by */
static inline unsigned temporal_stream_newest(const TemporalStreamCopy copy[2]) {
    return copy[0].at > copy[1].at ? copy[0].at : copy[1].at;
}
/* Which of n candidate slots serves the address want: the one that holds it, else, when one may be made, a free
   one or the one whose newest capture is oldest, if that is TEMPORAL_SLOT_FRAMES old. -1 for none: the address
   then has no record and its content is unknown. A slot with a capture this frame or the last is never given
   away, so nothing a draw of this frame relied on can be wiped before the frame's vectors are checked. */
#define TEMPORAL_SLOT_FRAMES 600
static inline int temporal_slot_pick(const unsigned long long* address, const unsigned* newest, int n,
                                     unsigned long long want, unsigned frame, int make) {
    int oldest = -1;
    for (int i = 0; i < n; i++) {
        if (address[i] == want) return i;
        if (oldest < 0 || (address[oldest] && (!address[i] || newest[i] < newest[oldest]))) oldest = i;
    }
    if (!make || oldest < 0) return -1;
    if (address[oldest] && newest[oldest] + TEMPORAL_SLOT_FRAMES > frame) return -1;
    return oldest;
}
/* A second capture into the address after a draw of this frame already took last frame's copy: the vectors that
   draw left cannot be trusted, and the frame's object vectors are given up. */
static inline int temporal_stream_conflict(const TemporalStreamCopy* now, unsigned frame) {
    return now->at == frame && now->captures > 1;
}
/* How far an object's origin moved on a screen of width x height pixels between the frame before and this one,
   from its world matrices and the two view-projections. -1 when it is behind the camera in either. */
static inline double temporal_origin_shift(const float vp[16], const float world[16], const float vp_before[16],
                                           const float world_before[16], double width, double height) {
    double at[2][3];
    for (int k = 0; k < 2; k++) {
        const float* m = k ? vp_before : vp;
        const float* w = k ? world_before : world;
        for (int c = 0; c < 3; c++) {
            const int column = c == 2 ? 3 : c; /* x, y and w */
            at[k][c] = (double)w[12] * (double)m[column] + (double)w[13] * (double)m[4 + column] +
                       (double)w[14] * (double)m[8 + column] + (double)m[12 + column];
        }
        if (!(at[k][2] > 0.0)) return -1.0;
    }
    const double dx = (at[0][0] / at[0][2] - at[1][0] / at[1][2]) * 0.5 * width;
    const double dy = (at[0][1] / at[0][2] - at[1][1] / at[1][2]) * 0.5 * height;
    return sqrt(dx * dx + dy * dy);
}

/* The scene inside a rectangle of its targets (the game's own 3D buffer scale): the rules of the temporal passes
   for it, pure, not used by the renderer yet. The targets keep their size W by H. The scene is drawn into an
   active rectangle of them, and the game's scene block reports the used fraction of each axis. */
typedef struct {
    int x, y, w, h; /* in pixels of the target, y down */
} TemporalRect;
static inline int temporal_rect_same(const TemporalRect* a, const TemporalRect* b) {
    return a->x == b->x && a->y == b->y && a->w == b->w && a->h == b->h;
}
/* The active rectangle of a signed viewport, 1 when it is one: every edge finite, a whole number within 0.001,
   and inside the target. A negative height means y is the bottom edge. A width that is not positive is refused,
   and so is a target without a size, before anything is worked out from it. */
static inline int temporal_rect_from_viewport(const TemporalViewport* v, int target_w, int target_h, TemporalRect* r) {
    if (target_w <= 0 || target_h <= 0) return 0;
    const double edge[4] = {(double)v->x, v->height < 0.0f ? (double)v->y + (double)v->height : (double)v->y,
                            (double)v->width, fabs((double)v->height)};
    int whole[4];
    for (int i = 0; i < 4; i++) {
        if (!isfinite(edge[i]) || fabs(edge[i]) > 1e9 || fabs(edge[i] - round(edge[i])) > 0.001) return 0;
        whole[i] = (int)round(edge[i]);
    }
    if (whole[0] < 0 || whole[1] < 0 || whole[2] <= 0 || whole[3] <= 0) return 0;
    if (whole[2] > target_w - whole[0] || whole[3] > target_h - whole[1]) return 0;
    *r = (TemporalRect){whole[0], whole[1], whole[2], whole[3]};
    return 1;
}
/* 1 when a viewport is refused for one reason alone: it is finite, not empty and inside the target, and an edge
   of it is not on a whole pixel. The game draws a viewport of the target's size times its scale as it comes,
   so a scale such as 0.6667 ends inside a pixel. Any other fault of a viewport is not this one. */
static inline int temporal_viewport_fractional(const TemporalViewport* v, int target_w, int target_h) {
    if (target_w <= 0 || target_h <= 0) return 0;
    const double edge[4] = {(double)v->x, v->height < 0.0f ? (double)v->y + (double)v->height : (double)v->y,
                            (double)v->width, fabs((double)v->height)};
    int whole = 1;
    for (int i = 0; i < 4; i++) {
        if (!isfinite(edge[i]) || fabs(edge[i]) > 1e9) return 0;
        if (fabs(edge[i] - round(edge[i])) > 0.001) whole = 0;
    }
    if (!(edge[2] > 0.0) || !(edge[3] > 0.0) || edge[0] < -0.001 || edge[1] < -0.001) return 0;
    if (edge[0] + edge[2] > target_w + 0.001 || edge[1] + edge[3] > target_h + 0.001) return 0;
    return !whole;
}
/* The scales p/q with q up to 8, from a half to under one, that put the scene's viewport on whole pixels in both
   axes of this target, worked out as the game is given them: p/q divided in double, narrowed to a float, times
   the size in single precision, within the 0.001 the rectangle's rule allows. Written into text in rising
   order, as "1/2, 3/5". The count is returned. Advice on whole pixels and nothing more. */
static inline int temporal_whole_scales(int target_w, int target_h, char* text, size_t size) {
    int found = 0, ps[32], qs[32];
    size_t at = 0;
    if (size) text[0] = 0;
    for (int q = 2; q <= 8; q++)
        for (int p = 1; p < q; p++) {
            int a = p, b = q;
            while (b) { /* only fractions in lowest terms: 2/4 is 1/2 */
                const int r = a % b;
                a = b;
                b = r;
            }
            const float scale = (float)((double)p / (double)q);
            const float w = (float)target_w * scale, h = (float)target_h * scale;
            if (a != 1 || scale < 0.5f || found >= 32) continue;
            if (fabsf(w - roundf(w)) > 0.001f || fabsf(h - roundf(h)) > 0.001f) continue;
            int at_place = found++;
            while (at_place > 0 && ps[at_place - 1] * q > p * qs[at_place - 1]) { /* keep the rising order */
                ps[at_place] = ps[at_place - 1];
                qs[at_place] = qs[at_place - 1];
                at_place--;
            }
            ps[at_place] = p;
            qs[at_place] = q;
        }
    for (int i = 0; i < found && at + 1 < size; i++) {
        const int n = snprintf(text + at, size - at, "%s%d/%d", i ? ", " : "", ps[i], qs[i]);
        if (n < 0 || (size_t)n >= size - at) {
            text[at] = 0;
            break;
        }
        at += (size_t)n;
    }
    return found;
}
/* the corner of the target the game puts the rectangle in, in image axes */
enum { TEMPORAL_CORNER_TOP_LEFT, TEMPORAL_CORNER_TOP_RIGHT, TEMPORAL_CORNER_BOTTOM_LEFT, TEMPORAL_CORNER_BOTTOM_RIGHT };
/* What two frames must have in common: the viewport as it was given, with its signs (the same rectangle with y
   the other way round is another viewport), the target's size, and the used fraction the scene block reported
   for it once it was confirmed. fx and fy are 0 until then. */
typedef struct {
    TemporalRect rect;
    TemporalViewport viewport;
    int target_w, target_h;
    float fx, fy;
} TemporalRectState;
enum {
    TEMPORAL_RECT_OK,
    TEMPORAL_RECT_FRACTION, /* a fraction not finite or not in (0, 1] */
    TEMPORAL_RECT_UNEQUAL,  /* the two fractions more than 1e-6 apart: one scale for both axes only */
    TEMPORAL_RECT_VIEWPORT, /* no whole rectangle inside the target */
    TEMPORAL_RECT_SHAPE     /* the geometry is not a scene's, or the fraction is not the one of this geometry */
};
/* The geometry of a frame, from the viewport of the draw that decides it and nothing else. OK with the state
   filled in (no fraction yet) when the rectangle is the whole target, or sits at the game's corner, is at least
   half the target each way, and has the target's proportions within a pixel: |w x H - h x W| <= max(W, H). */
static inline int temporal_rect_geometry(const TemporalViewport* v, int target_w, int target_h, int corner,
                                         TemporalRectState* out) {
    TemporalRect r;
    if (target_w <= 0 || target_h <= 0 || !temporal_rect_from_viewport(v, target_w, target_h, &r))
        return TEMPORAL_RECT_VIEWPORT;
    if (r.x || r.y || r.w != target_w || r.h != target_h) {
        const int left = corner == TEMPORAL_CORNER_TOP_LEFT || corner == TEMPORAL_CORNER_BOTTOM_LEFT;
        const int top = corner == TEMPORAL_CORNER_TOP_LEFT || corner == TEMPORAL_CORNER_TOP_RIGHT;
        const long long skew = (long long)r.w * target_h - (long long)r.h * target_w;
        if (r.x != (left ? 0 : target_w - r.w) || r.y != (top ? 0 : target_h - r.h)) return TEMPORAL_RECT_SHAPE;
        if (2 * r.w < target_w || 2 * r.h < target_h) return TEMPORAL_RECT_SHAPE;
        if ((skew < 0 ? -skew : skew) > (target_w > target_h ? target_w : target_h)) return TEMPORAL_RECT_SHAPE;
    }
    *out = (TemporalRectState){r, *v, target_w, target_h, 0.0f, 0.0f};
    return TEMPORAL_RECT_OK;
}
/* Whether a used fraction is the one of a geometry: finite, in (0, 1], the same both ways within 1e-6, 1 within
   1e-6 for a whole target, and else round(W x fx) by round(H x fy) within one pixel of the rectangle. */
static inline int temporal_rect_fraction(const TemporalRectState* g, float fx, float fy) {
    if (!isfinite(fx) || !isfinite(fy) || !(fx > 0.0f) || !(fy > 0.0f) || fx > 1.0f || fy > 1.0f)
        return TEMPORAL_RECT_FRACTION;
    if (fabs((double)fx - (double)fy) > 1e-6) return TEMPORAL_RECT_UNEQUAL;
    if (!g->rect.x && !g->rect.y && g->rect.w == g->target_w && g->rect.h == g->target_h)
        return fabs((double)fx - 1.0) > 1e-6 || fabs((double)fy - 1.0) > 1e-6 ? TEMPORAL_RECT_SHAPE : TEMPORAL_RECT_OK;
    if (fabs((double)g->rect.w - round((double)g->target_w * (double)fx)) > 1.0 ||
        fabs((double)g->rect.h - round((double)g->target_h * (double)fy)) > 1.0)
        return TEMPORAL_RECT_SHAPE;
    return TEMPORAL_RECT_OK;
}
/* The scene block at one draw, for one stage. reads: that stage's program reads the block as far as the fraction.
   bound: a block long enough is bound there and its two words were read into fx and fy. The renderer says both. */
typedef struct {
    int reads, bound;
    float fx, fy;
} TemporalSceneBlock;
/* The scene block of one stage at a draw, from what the renderer knows. mask and extent: the constant banks the
   stage's translated program reads and how many bytes of the scene block's bank it reads. address and size: what
   is bound there. words: the two fraction words read from guest memory, NULL when they could not be read. */
#define TEMPORAL_SCENE_BANK 3
#define TEMPORAL_SCENE_FRACTION 0x628 /* the used fraction, width then height, two floats */
#define TEMPORAL_SCENE_BYTES 0x630
static inline TemporalSceneBlock temporal_scene_block(unsigned mask, unsigned extent, unsigned long long address,
                                                      unsigned size, const float* words) {
    TemporalSceneBlock b = {0, 0, 0.0f, 0.0f};
    b.reads = (mask >> TEMPORAL_SCENE_BANK & 1u) && extent >= TEMPORAL_SCENE_BYTES;
    b.bound = address && size >= TEMPORAL_SCENE_BYTES && words;
    if (b.bound) b.fx = words[0], b.fy = words[1];
    return b;
}
/* At the draw that decides the frame, a scene block that happens to be bound may refuse the frame early, before
   any jitter: 1 when it is bound, its fraction is finite and is not the geometry's. Bound with the geometry's
   fraction, not bound, or not finite: 0, which decides nothing. */
static inline int temporal_rect_early(const TemporalRectState* g, const TemporalSceneBlock* block) {
    if (!block || !block->bound || !isfinite(block->fx) || !isfinite(block->fy)) return 0;
    return temporal_rect_fraction(g, block->fx, block->fy) != TEMPORAL_RECT_OK;
}
static inline int temporal_rect_geometry_same(const TemporalRectState* a, const TemporalRectState* b) {
    return temporal_rect_same(&a->rect, &b->rect) && temporal_viewport_same(&a->viewport, &b->viewport) &&
           a->target_w == b->target_w && a->target_h == b->target_h;
}
/* what a history was made from: any difference and it is not used */
typedef struct {
    unsigned long long target, generation; /* the scene colour's identity and which making of it */
    int target_w, target_h, out_w, out_h;
    TemporalRect rect;
    float fx, fy; /* the used fraction it was made under */
} TemporalRectStamp;
static inline int temporal_rect_stamp_same(const TemporalRectStamp* a, const TemporalRectStamp* b) {
    return a->target == b->target && a->generation == b->generation && a->target_w == b->target_w &&
           a->target_h == b->target_h && a->out_w == b->out_w && a->out_h == b->out_h &&
           temporal_rect_same(&a->rect, &b->rect) && a->fx == b->fx && a->fy == b->fy;
}
/* The gate for the rectangle. A frame is decided at its first draw from its geometry alone, which allows its
   jitter when the geometry is the candidate's. It is confirmed later in the frame, at the first scene draw whose
   program reads the scene block, by the fraction. The candidate is the geometry and fraction of the frame before
   and exists only when that frame was confirmed. The stamp is set by a frame that wrote history. */
enum {
    TEMPORAL_FRAME_NONE,     /* no geometry, or the frame was found wrong: nothing of it counts */
    TEMPORAL_FRAME_DECIDED,  /* decided and not yet confirmed: it is not resolved */
    TEMPORAL_FRAME_STEADY,   /* accepted and confirmed with the candidate's fraction: it may be resolved */
    TEMPORAL_FRAME_SEEN      /* confirmed, but gated or with a new fraction: only the frame after gains by it */
};
typedef struct {
    int have_candidate, have_stamp;
    unsigned candidate_at, stamp_at; /* the frames they are of */
    TemporalRectState candidate;
    TemporalRectStamp stamp;
    unsigned decided_at; /* the frame decided last, and what became of it */
    int accepted, state;
    TemporalRectState now;
    /* scene draws that came before the frame's decision (temporal_rect_before): the frame, what they showed
       (0 no fraction yet, 1 a fraction, 2 something wrong), and their geometry with that fraction */
    unsigned early_at;
    int early_state, early_shaped;
    TemporalRectState early;
    /* the frame our passes were reached in, accepted frames in a row in which they were not, and whether the
       gate has stopped accepting for good (temporal_rect_hooked, temporal_rect_failed) */
    unsigned hooked_at, unhooked;
    int stopped;
} TemporalRectGate;
#define TEMPORAL_RECT_UNHOOKED 8 /* accepted frames in a row that our passes were not reached in: the gate stops */
static inline void temporal_rect_clear(TemporalRectGate* g) { g->have_candidate = g->have_stamp = 0; }
/* a frame found wrong after its decision: nothing of it or before it counts any more */
static inline void temporal_rect_spoil(TemporalRectGate* g) {
    temporal_rect_clear(g);
    g->state = TEMPORAL_FRAME_NONE;
}
/* The frame is decided, once, at its first draw that binds the scene camera's block. geometry is its own, NULL
   when it has none (temporal_rect_geometry refused, or an unknown draw bound the block earlier). refused: 1 when
   the scene block bound at this draw said no (temporal_rect_early). 1: accepted, the frame is jittered. 0: gated,
   the frame is drawn as it is, and whatever follows in it, it is never resolved. */
static inline int temporal_rect_decide(TemporalRectGate* g, unsigned frame, const TemporalRectState* geometry,
                                       int refused) {
    const int early = g->early_at == frame;
    /* The frame decided last was drawn with jitter and our passes were never reached in it: its picture was
       shown as drawn. The start begins again, and after enough such frames in a row the gate stops. */
    if (g->decided_at && g->decided_at != frame && g->accepted) {
        if (g->hooked_at == g->decided_at) g->unhooked = 0;
        else {
            temporal_rect_clear(g);
            if (++g->unhooked >= TEMPORAL_RECT_UNHOOKED) g->stopped = 1;
        }
    }
    /* Only a confirmation makes a candidate. So after a frame that was never confirmed, as after a missing one,
       the candidate is not of the frame before and goes, and the history with it. */
    if (g->have_candidate && g->candidate_at + 1 != frame) temporal_rect_clear(g);
    if (g->have_stamp && g->stamp_at + 1 != frame) g->have_stamp = 0;
    g->decided_at = frame;
    g->accepted = 0;
    g->state = TEMPORAL_FRAME_NONE;
    if (!geometry) {
        temporal_rect_clear(g);
        return 0;
    }
    g->now = *geometry;
    g->now.fx = g->now.fy = 0.0f;
    g->state = TEMPORAL_FRAME_DECIDED;
    g->accepted = !refused && !g->stopped && !early && g->have_candidate &&
                  temporal_rect_geometry_same(&g->candidate, &g->now);
    if (!g->accepted) g->have_stamp = 0; /* a change starts the history anew */
    if (early) {
        /* Scene draws came before this one. They were not jittered, so the frame is not either. What they
           showed must be this geometry, and a fraction they fixed is the frame's: it can make the candidate. */
        if (g->early_state == 2 || !g->early_shaped || !temporal_rect_geometry_same(&g->early, &g->now)) {
            temporal_rect_spoil(g);
            return 0;
        }
        if (g->early_state == 1) {
            g->now.fx = g->early.fx;
            g->now.fy = g->early.fy;
            g->have_candidate = 1;
            g->candidate_at = frame;
            g->candidate = g->now;
            g->state = TEMPORAL_FRAME_SEEN;
        }
    }
    return g->accepted;
}
/* A draw into the scene that was completed before the frame's decision. Its geometry is kept and every such draw
   must have the same. The first of them whose program reads the scene block is the frame's confirming draw, as
   it would be after the decision: a bad binding there is wrong for good, a good one fixes the fraction, and
   every later reader must show exactly that. The decision then takes it from here. */
static inline void temporal_rect_before(TemporalRectGate* g, unsigned frame, const TemporalRectState* geometry,
                                        const TemporalSceneBlock* vertex, const TemporalSceneBlock* fragment) {
    const TemporalSceneBlock* stage[2] = {vertex, fragment};
    if (g->decided_at == frame) return;
    if (g->early_at != frame) {
        g->early_at = frame;
        g->early_state = g->early_shaped = 0;
    }
    if (g->early_state == 2) return;
    if (!geometry || (g->early_shaped && !temporal_rect_geometry_same(&g->early, geometry))) {
        g->early_state = 2;
        return;
    }
    if (!g->early_shaped) {
        g->early = *geometry;
        g->early.fx = g->early.fy = 0.0f;
        g->early_shaped = 1;
    }
    for (int i = 0; i < 2 && g->early_state != 2; i++) {
        if (!stage[i] || !stage[i]->reads) continue;
        if (!stage[i]->bound) g->early_state = 2;
        else if (g->early_state == 1)
            g->early_state = stage[i]->fx == g->early.fx && stage[i]->fy == g->early.fy ? 1 : 2;
        else if (temporal_rect_fraction(&g->early, stage[i]->fx, stage[i]->fy) != TEMPORAL_RECT_OK)
            g->early_state = 2;
        else {
            g->early.fx = stage[i]->fx;
            g->early.fy = stage[i]->fy;
            g->early_state = 1;
        }
    }
}
/* our passes were reached in this frame, whatever they then did */
static inline void temporal_rect_hooked(TemporalRectGate* g, unsigned frame) {
    if (g->decided_at == frame) g->hooked_at = frame;
}
/* Our passes were reached and found the frame's structure not to be the decided one (the G-buffer's viewport or
   the target). That is a disagreement found late and no failure of the passes: nothing is left of the frame,
   and if it was drawn with jitter it counts like a frame they were not reached in. One such frame costs the
   start-up, TEMPORAL_RECT_UNHOOKED in a row stop the gate. */
static inline void temporal_rect_refused(TemporalRectGate* g, unsigned frame) {
    if (g->decided_at != frame) return;
    temporal_rect_spoil(g);
    if (g->hooked_at == frame) g->hooked_at = 0;
}
/* Our passes could not be made or run in a frame that was drawn with jitter, or one of its draws could not be
   given the jitter: no frame is accepted from here on, and nothing is left of this one, confirmed already or
   not. The gate still decides and confirms, so its lines go on, and it refuses. */
static inline void temporal_rect_failed(TemporalRectGate* g, unsigned frame) {
    g->stopped = 1;
    if (g->decided_at == frame) temporal_rect_spoil(g);
}
/* The frame's confirmation, at its first scene draw whose program reads the scene block in either stage. That
   draw alone confirms: a stage that reads the block and has none bound, two stages that disagree, or a fraction
   that is not the geometry's spoil the frame, and no later draw can mend it. With the candidate's fraction an
   accepted frame is STEADY. A gated frame, or one whose fraction is new, becomes the candidate and is SEEN. */
static inline int temporal_rect_confirm(TemporalRectGate* g, unsigned frame, const TemporalSceneBlock* vertex,
                                        const TemporalSceneBlock* fragment) {
    const TemporalSceneBlock* stage[2] = {vertex, fragment};
    const TemporalSceneBlock* read = NULL;
    if (g->decided_at != frame || g->state != TEMPORAL_FRAME_DECIDED) return g->decided_at == frame ? g->state : 0;
    for (int i = 0; i < 2; i++) {
        if (!stage[i] || !stage[i]->reads) continue;
        if (!stage[i]->bound || (read && (read->fx != stage[i]->fx || read->fy != stage[i]->fy))) {
            temporal_rect_spoil(g);
            return g->state;
        }
        read = stage[i];
    }
    if (!read || temporal_rect_fraction(&g->now, read->fx, read->fy) != TEMPORAL_RECT_OK) {
        temporal_rect_spoil(g);
        return g->state;
    }
    const int same = g->accepted && g->have_candidate && fabs((double)read->fx - (double)g->candidate.fx) <= 1e-6 &&
                     fabs((double)read->fy - (double)g->candidate.fy) <= 1e-6;
    g->now.fx = read->fx;
    g->now.fy = read->fy;
    if (!same) g->have_stamp = 0; /* found late to be another configuration: no history is used or written */
    g->have_candidate = 1;
    g->candidate_at = frame;
    g->candidate = g->now;
    g->state = same ? TEMPORAL_FRAME_STEADY : TEMPORAL_FRAME_SEEN;
    return g->state;
}
/* A later draw into the scene in the same frame. geometry: its viewport's, NULL when it has none. vertex and
   fragment: the scene block where that draw's programs read it. 1 when it disagrees with the frame: another
   geometry, or after the confirmation a stage that reads the block and has none bound or another fraction. The
   frame is then spoilt: left as drawn, and the frames after start again. */
static inline int temporal_rect_late(TemporalRectGate* g, unsigned frame, const TemporalRectState* geometry,
                                     const TemporalSceneBlock* vertex, const TemporalSceneBlock* fragment) {
    const TemporalSceneBlock* stage[2] = {vertex, fragment};
    if (g->decided_at != frame) return 0;
    if (g->state == TEMPORAL_FRAME_NONE) return 1;
    int wrong = !geometry || !temporal_rect_geometry_same(&g->now, geometry);
    for (int i = 0; i < 2 && !wrong && g->state != TEMPORAL_FRAME_DECIDED; i++) {
        if (!stage[i] || !stage[i]->reads) continue;
        wrong = !stage[i]->bound || stage[i]->fx != g->now.fx || stage[i]->fy != g->now.fy;
    }
    if (wrong) temporal_rect_spoil(g);
    return wrong;
}
/* 1 while the frame may be resolved: accepted at its decision, confirmed with the candidate's fraction, and not
   disagreed with since */
static inline int temporal_rect_steady(const TemporalRectGate* g, unsigned frame) {
    return g->decided_at == frame && g->accepted && g->state == TEMPORAL_FRAME_STEADY;
}
/* 1 when the history of the frame before may be used by this frame's resolve: made by that frame, from this */
static inline int temporal_rect_history(const TemporalRectGate* g, unsigned frame, const TemporalRectStamp* now) {
    return temporal_rect_steady(g, frame) && g->have_stamp && g->stamp_at + 1 == frame &&
           temporal_rect_stamp_same(&g->stamp, now);
}
/* a steady frame wrote history */
static inline void temporal_rect_wrote(TemporalRectGate* g, unsigned frame, const TemporalRectStamp* stamp) {
    if (!temporal_rect_steady(g, frame)) return;
    g->have_stamp = 1;
    g->stamp_at = frame;
    g->stamp = *stamp;
}
/* A history fetch: 1 when the centre it would fetch at, in pixels of the target, lies in the rectangle the history
   was made in. Tested before any sampling. Clamping the taps (temporal_rect_tap) does not stand in for it. */
static inline int temporal_rect_holds(const TemporalRect* prior, double x, double y) {
    return isfinite(x) && isfinite(y) && x >= (double)prior->x && y >= (double)prior->y &&
           x < (double)prior->x + (double)prior->w && y < (double)prior->y + (double)prior->h;
}
/* a neighbour's texel kept inside the rectangle: outside it the targets hold stale data */
static inline void temporal_rect_tap(const TemporalRect* r, int x, int y, int out[2]) {
    out[0] = x < r->x ? r->x : x > r->x + r->w - 1 ? r->x + r->w - 1 : x;
    out[1] = y < r->y ? r->y : y > r->y + r->h - 1 ? r->y + r->h - 1 : y;
}
/* the bound of an object's own motion: an eighth of the rectangle's height, not of the target's */
static inline double temporal_rect_reject_bound(int on, const TemporalRect* r) {
    return temporal_reject_bound(on, (double)r->h);
}
/* Sample i, j of the numbers' lattice of nx by ny: the pixel at the middle of its cell of the rectangle, in whole
   numbers as shaders/temporal_stats.frag does it. Every sample lies inside the rectangle. */
static inline void temporal_rect_sample(const TemporalRect* r, int i, int j, int nx, int ny, int out[2]) {
    out[0] = r->x + (int)(((long long)(2 * i + 1) * r->w) / (2LL * nx));
    out[1] = r->y + (int)(((long long)(2 * j + 1) * r->h) / (2LL * ny));
}
/* the jitter's offsets: eight for a whole target, more as the rectangle is smaller, ceil(8 x W x H / (w x h)) */
static inline unsigned temporal_rect_offsets(const TemporalRect* r, int target_w, int target_h) {
    const long long area = (long long)r->w * r->h, whole = 8LL * target_w * target_h;
    return area > 0 ? (unsigned)((whole + area - 1) / area) : 8u;
}
/* the texture level shift under a scale: log2 of it, unless a value was set, which wins. 0 for a whole target. */
static inline float temporal_rect_bias(int set, float value, const TemporalRect* r, int target_w) {
    if (set) return value;
    return r->w > 0 && r->w < target_w ? (float)log2((double)r->w / (double)target_w) : 0.0f;
}
/* the jitter in pixels of the image: half the jitter in clip units over w times the viewport's signed size */
static inline void temporal_jitter_pixels(const double jitter[2], const TemporalViewport* v, double out[2]) {
    out[0] = 0.5 * jitter[0] * (double)v->width;
    out[1] = 0.5 * jitter[1] * (double)v->height;
}
/* What an upscaler is given, in one fixed convention that each library turns into its own (gpu/vk/upscaler.h). Pure,
   not used by the renderer yet.
   The vector: where the pixel's point was a frame ago less where it is now, in pixels of the target, x to the
   right and y down the image, both ends without jitter. The motion image holds (now less before) over (W, H). */
static inline void temporal_upscaler_vector(const float texel[2], int target_w, int target_h, double out[2]) {
    out[0] = -(double)texel[0] * (double)target_w;
    out[1] = -(double)texel[1] * (double)target_h;
}
/* One frame's numbers for the upscaler. The vector's scale is what a motion texel is multiplied by: the size of
   the whole target with a minus, in full and in partial mode. The jitter is the shift of the drawn image against
   the unjittered one, in pixels of the rectangle. */
typedef struct {
    TemporalRect render; /* the active rectangle, inside images of target_w by target_h */
    int target_w, target_h, out_w, out_h;
    double jitter[2], vector_scale[2];
    int reset; /* the upscaler must not use what it kept */
} TemporalUpscalerFrame;
/* The conversion alone, which knows nothing of the game: any rectangle of the images, from its viewport. 1 with
   the numbers when the viewport is the stamp's rectangle, the sizes are positive and the jitter is finite and
   within a pixel each way, so every number handed on is a small one. reset is handed on as given. */
static inline int temporal_upscaler_numbers(const TemporalRectStamp* stamp, const TemporalViewport* viewport,
                                            const double jitter[2], int reset, TemporalUpscalerFrame* out) {
    TemporalRect r;
    if (!temporal_rect_from_viewport(viewport, stamp->target_w, stamp->target_h, &r)) return 0;
    if (!temporal_rect_same(&r, &stamp->rect)) return 0;
    if (stamp->out_w <= 0 || stamp->out_h <= 0 || !isfinite(jitter[0]) || !isfinite(jitter[1])) return 0;
    /* the jitter in pixels as well: finite, and no more than a pixel, or it is no jitter */
    double shift[2];
    temporal_jitter_pixels(jitter, viewport, shift);
    if (!(fabs(shift[0]) <= 1.0) || !(fabs(shift[1]) <= 1.0)) return 0;
    out->render = r;
    out->target_w = stamp->target_w;
    out->target_h = stamp->target_h;
    out->out_w = stamp->out_w;
    out->out_h = stamp->out_h;
    out->jitter[0] = shift[0];
    out->jitter[1] = shift[1];
    out->vector_scale[0] = -(double)stamp->target_w;
    out->vector_scale[1] = -(double)stamp->target_h;
    out->reset = reset;
    return 1;
}
/* 1 with the numbers when the game's frame may be given to the upscaler: it is steady and the viewport and the
   stamp are its own (temporal_upscaler_numbers for the rest). Reset when no history of the frame before may be
   used (temporal_rect_history) or the camera cut. 0: the frame is not evaluated and is left as drawn. Whoever
   evaluates with success calls temporal_rect_wrote. */
static inline int temporal_upscaler_frame(const TemporalRectGate* g, unsigned frame, const TemporalRectStamp* stamp,
                                          const TemporalViewport* viewport, const double jitter[2], int cut,
                                          TemporalUpscalerFrame* out) {
    if (!temporal_rect_steady(g, frame) || !temporal_viewport_same(viewport, &g->now.viewport)) return 0;
    if (!temporal_rect_same(&g->now.rect, &stamp->rect)) return 0;
    if (stamp->target_w != g->now.target_w || stamp->target_h != g->now.target_h) return 0;
    if (stamp->fx != g->now.fx || stamp->fy != g->now.fy) return 0;
    return temporal_upscaler_numbers(stamp, viewport, jitter, cut || !temporal_rect_history(g, frame, stamp), out);
}
/* The camera's numbers, checked on the floats that are handed on (near, far, field of view), since a double that
   passes may round to a float that does not. 1 when near is positive, the field of view positive and below pi,
   and far beyond near, or infinite exactly when the context was made for that. They are known when the
   projection is decoded, at the frame's first draw, and are checked there. */
static inline int temporal_upscaler_planes(double near_plane, double far_plane, double fov_y, int far_infinite,
                                           float out[3]) {
    const double most = 3.4028234663852886e38, pi = 3.14159265358979323846;
    if (!isfinite(near_plane) || !isfinite(fov_y) || isnan(far_plane)) return 0;
    if (fabs(near_plane) > most || fabs(fov_y) > most) return 0;
    if (isfinite(far_plane) && fabs(far_plane) > most) return 0;
    const float near_f = (float)near_plane, far_f = (float)far_plane, fov_f = (float)fov_y;
    if (!(near_f > 0.0f) || !(fov_f > 0.0f) || !((double)fov_f < pi)) return 0;
    if (far_infinite ? !(isinf(far_f) && far_f > 0.0f) : !(isfinite(far_f) && far_f > near_f)) return 0;
    out[0] = near_f;
    out[1] = far_f;
    out[2] = fov_f;
    return 1;
}
/* the frame time's part of that check, where a real frame time exists: at the dispatch */
static inline int temporal_upscaler_time(double seconds, float* out) {
    if (!isfinite(seconds) || fabs(seconds) > 3.4028234663852886e38) return 0;
    const float seconds_f = (float)seconds, ms = seconds_f * 1000.0f;
    if (!(seconds_f > 0.0f) || !isfinite(ms) || !(ms > 0.0f)) return 0;
    *out = seconds_f;
    return 1;
}
/* both parts, for a caller that has all four numbers at once: out is near, far, field of view, seconds */
static inline int temporal_upscaler_camera(double near_plane, double far_plane, double fov_y, double seconds,
                                           int far_infinite, float out[4]) {
    float planes[3], time;
    if (!temporal_upscaler_planes(near_plane, far_plane, fov_y, far_infinite, planes)) return 0;
    if (!temporal_upscaler_time(seconds, &time)) return 0;
    out[0] = planes[0];
    out[1] = planes[1];
    out[2] = planes[2];
    out[3] = time;
    return 1;
}
/* The game's projection, as its camera block holds it at +0x80 in memory order: x scale in word 0, y scale in
   word 5, A in word 10, -1 in word 11, B in word 14, nothing else. Clip z is A z + B and clip w is -z for a view
   depth z that is negative in front of the camera, so the stored depth is (A z + B) / (-z): reversed, 1 at the
   near plane. Seen in a run for two cameras. Any other form is refused, to be widened only on evidence. */
enum {
    TEMPORAL_PROJECTION_OK,
    TEMPORAL_PROJECTION_SCALE,  /* word 0 or word 5 is not finite and positive (a turned axis is another convention) */
    TEMPORAL_PROJECTION_W,      /* word 11 is not exactly -1: another form, or the same times a factor */
    TEMPORAL_PROJECTION_OFFSET, /* word 14 is not finite and positive */
    TEMPORAL_PROJECTION_SLOPE,  /* word 10 is not finite, or negative */
    TEMPORAL_PROJECTION_OTHER,  /* a word that must be zero is not exactly zero: shear, off-centre, w from x or y */
    TEMPORAL_PROJECTION_PLANES, /* near is not above 0, or far is not beyond near */
    TEMPORAL_PROJECTION_FOV,    /* the field of view is not above 0 and below pi */
    TEMPORAL_PROJECTION_FLOATS  /* the numbers do not pass the check on the floats that are handed on */
};
typedef struct {
    double near_plane, far_plane, fov_y; /* far is infinite when word 10 is 0 */
    int far_infinite;                    /* the decision a context is made with */
    int word;                            /* for a refusal by one word: which */
} TemporalProjection;
/* The first rule a matrix breaks, in the order above, or OK with the camera's numbers. No tolerance: the game's
   zeros are exact and its word 11 is exactly -1. The arithmetic goes through c = |word 11| all the same, so a
   later widening cannot leave a wrong formula behind. The numbers must also pass temporal_upscaler_planes on
   the narrowed floats. The frame time is no part of a projection and is checked where one exists. */
static inline int temporal_projection_camera(const float m[16], TemporalProjection* out) {
    static const int zero[11] = {1, 2, 3, 4, 6, 7, 8, 9, 12, 13, 15};
    float handed[3];
    *out = (TemporalProjection){0.0, 0.0, 0.0, 0, -1};
    for (int k = 0; k < 2; k++)
        if (!isfinite(m[k ? 5 : 0]) || !(m[k ? 5 : 0] > 0.0f)) {
            out->word = k ? 5 : 0;
            return TEMPORAL_PROJECTION_SCALE;
        }
    out->word = 11;
    if (!(m[11] == -1.0f)) return TEMPORAL_PROJECTION_W;
    out->word = 14;
    if (!isfinite(m[14]) || !(m[14] > 0.0f)) return TEMPORAL_PROJECTION_OFFSET;
    out->word = 10;
    if (!isfinite(m[10]) || m[10] < 0.0f) return TEMPORAL_PROJECTION_SLOPE;
    for (int i = 0; i < 11; i++)
        if (!(m[zero[i]] == 0.0f)) {
            out->word = zero[i];
            return TEMPORAL_PROJECTION_OTHER;
        }
    out->word = -1;
    const double c = fabs((double)m[11]);
    out->near_plane = (double)m[14] / (c + (double)m[10]);
    out->far_infinite = m[10] == 0.0f;
    out->far_plane = out->far_infinite ? INFINITY : (double)m[14] / (double)m[10];
    out->fov_y = 2.0 * atan(c / (double)m[5]);
    if (!(out->near_plane > 0.0) || !(out->far_plane > out->near_plane)) return TEMPORAL_PROJECTION_PLANES;
    if (!(out->fov_y > 0.0) || !(out->fov_y < 3.14159265358979323846)) return TEMPORAL_PROJECTION_FOV;
    if (!temporal_upscaler_planes(out->near_plane, out->far_plane, out->fov_y, out->far_infinite, handed))
        return TEMPORAL_PROJECTION_FLOATS;
    return TEMPORAL_PROJECTION_OK;
}
/* An upscaler's contexts. A context is made for one configuration, which is everything its creation was given:
   created, then what it must run once is recorded by its prepare step and submitted by the runtime, and when that
   submission has completed it is published and may be used. A change of anything in the configuration is a new
   context. One is on its way at a time, and until it is published the frames of its kind are not evaluated. */
typedef struct {
    int target_w, target_h, render_w, render_h, out_w, out_h;
    /* Everything else a context is created with, as a generation the caller raises whenever any of it changes: the
       formats, the inputs' properties, the library and its version, the device and its queue. */
    unsigned long long generation;
} TemporalUpscalerConfig;
typedef struct {
    int live;    /* a published context there is */
    /* 0 none on its way, 1 one submitted, 2 one whose prepare failed, held until it may be freed, 3 one submitted
       and given up by a stop, whose completion is still owed */
    int pending;
    TemporalUpscalerConfig live_config, pending_config;
} TemporalUpscalerContexts;
static inline int temporal_upscaler_config_same(const TemporalUpscalerConfig* a, const TemporalUpscalerConfig* b) {
    return a->target_w == b->target_w && a->target_h == b->target_h && a->render_w == b->render_w &&
           a->render_h == b->render_h && a->out_w == b->out_w && a->out_h == b->out_h && a->generation == b->generation;
}
/* 1 when the published context is for the frame's configuration: only then is the frame evaluated */
static inline int temporal_upscaler_ready(const TemporalUpscalerContexts* c, const TemporalUpscalerConfig* config) {
    return c->live && temporal_upscaler_config_same(&c->live_config, config);
}
/* 1 when a context has to be created and prepared for this configuration: none is published for it and none is
   on its way or held */
static inline int temporal_upscaler_must_create(const TemporalUpscalerContexts* c,
                                                const TemporalUpscalerConfig* config) {
    return !temporal_upscaler_ready(c, config) && !c->pending;
}
/* a context was created for this configuration and its prepare recorded and submitted (ok 1), or prepare failed */
static inline void temporal_upscaler_created(TemporalUpscalerContexts* c, const TemporalUpscalerConfig* config,
                                             int ok) {
    c->pending = ok ? 1 : 2;
    c->pending_config = *config;
}
/* The pending context's submission has completed, or the command buffer of a failed one was discarded. current
   is what the frames are now. PUBLISHED: it is the published context. RETIRE: also, and the one it replaced goes
   to be destroyed after its frames in flight. STALE: the frames moved on meanwhile, so it is never published or
   evaluated and goes to be destroyed. RELEASE: a failed one may be destroyed now. */
enum {
    TEMPORAL_UPSCALER_NOTHING,
    TEMPORAL_UPSCALER_PUBLISHED,
    TEMPORAL_UPSCALER_RETIRE,
    TEMPORAL_UPSCALER_STALE,
    TEMPORAL_UPSCALER_RELEASE
};
static inline int temporal_upscaler_completed(TemporalUpscalerContexts* c, const TemporalUpscalerConfig* current) {
    const int pending = c->pending, had = c->live;
    c->pending = 0;
    if (pending == 2) return TEMPORAL_UPSCALER_RELEASE;
    if (pending == 3) return TEMPORAL_UPSCALER_STALE;
    if (pending != 1) return TEMPORAL_UPSCALER_NOTHING;
    if (!temporal_upscaler_config_same(&c->pending_config, current)) return TEMPORAL_UPSCALER_STALE;
    c->live = 1;
    c->live_config = c->pending_config;
    return had ? TEMPORAL_UPSCALER_RETIRE : TEMPORAL_UPSCALER_PUBLISHED;
}
/* The upscaler is turned off or started again: nothing is published any more. 1 when a published context goes to
   be destroyed after its frames in flight. A context on its way is not forgotten: its completion still comes and
   then gives STALE or RELEASE, and no other is made before that. A camera cut or a gap in the frames does NOT
   call this: those only set the reset flag of a frame (temporal_upscaler_frame) and leave the contexts alone. */
static inline int temporal_upscaler_stop(TemporalUpscalerContexts* c) {
    const int had = c->live;
    c->live = 0;
    if (c->pending == 1) c->pending = 3;
    return had;
}
/* The reset flag of an evaluate. A context that has come into use since the last evaluate holds nothing of the
   frames before, whatever the frame's own history says: its first evaluate has the reset, same sizes or not.
   published counts the contexts that came into use, last is that count at the evaluate before, and is brought
   up to date here. Asked once for an evaluate, right before it, whichever way the frame's own reset was made. */
static inline int temporal_upscaler_reset(int frame_reset, unsigned published, unsigned* last) {
    const int another = published != *last;
    *last = published;
    return frame_reset || another;
}
/* The upscaler's failures in a row. A frame that fails writes no history, so the next one has the reset. 1 when
   the upscaler is to be off for the rest of the session. */
#define TEMPORAL_UPSCALER_FAILURES 8
static inline int temporal_upscaler_failed(unsigned* failures, int ok) {
    if (ok) *failures = 0;
    else if (*failures < TEMPORAL_UPSCALER_FAILURES) ++*failures;
    return *failures >= TEMPORAL_UPSCALER_FAILURES;
}
/* One channel of the write-back after an upscaler, as shaders/temporal_upscaled.frag does it: the library's value,
   0 for a negative one and for one that is no number, and at most largest, the largest finite value the scene
   colour's format holds for the channel. So nothing of a library that is not finite reaches the scene colour.
   A very large finite value can still overflow in a later pass of the game: that is not promised. */
static inline float temporal_upscaled(float c, float largest) {
    if (isnan(c) || !(c > 0.0f)) return 0.0f;
    return c < largest ? c : largest;
}
/* the largest finite value of an unsigned float with five bits of exponent and this many of mantissa: 65024 for
   the 11 bit channels (6) of B10G11R11, 64512 for its 10 bit channel (5) */
static inline float temporal_ufloat_largest(int mantissa_bits) {
    return (2.0f - 1.0f / (float)(1 << mantissa_bits)) * 32768.0f;
}
/* SNX_ARCEUS_UPSCALER in the game, the pure part. Not used by the renderer yet. It starts only with the
   anti-aliasing on, whose gate and failure rules it needs, and not with rejected vectors, which it does not have. */
enum { TEMPORAL_UPSCALER_START_OK, TEMPORAL_UPSCALER_START_NOT_TAA, TEMPORAL_UPSCALER_START_REJECT };
static inline int temporal_upscaler_start(int taa, int reject) {
    if (!taa) return TEMPORAL_UPSCALER_START_NOT_TAA;
    return reject ? TEMPORAL_UPSCALER_START_REJECT : TEMPORAL_UPSCALER_START_OK;
}
/* What a context is made with beside the sizes: the images' formats and layer counts and the create flags, which
   hold whether the far plane is at infinity. The configuration's generation is raised whenever any of it is not
   what the frame before it had, so a making that comes back is a new context too. */
typedef struct {
    unsigned colour_format, depth_format, motion_format, output_format, colour_layers, depth_layers, flags;
} TemporalUpscalerMaking;
static inline unsigned long long temporal_upscaler_generation(TemporalUpscalerMaking* last,
                                                              unsigned long long* generation,
                                                              const TemporalUpscalerMaking* now) {
    const int same = last->colour_format == now->colour_format && last->depth_format == now->depth_format &&
                     last->motion_format == now->motion_format && last->output_format == now->output_format &&
                     last->colour_layers == now->colour_layers && last->depth_layers == now->depth_layers &&
                     last->flags == now->flags;
    if (!same || !*generation) ++*generation;
    *last = *now;
    return *generation;
}
/* 1 when the frame's scene is the whole target at full size: the only frames this piece takes */
static inline int temporal_upscaler_whole(const TemporalRectState* s) {
    return s->fx == 1.0f && s->fy == 1.0f && s->rect.x == 0 && s->rect.y == 0 && s->rect.w == s->target_w &&
           s->rect.h == s->target_h && s->target_w > 0 && s->target_h > 0;
}
/* 1 when a private output of the scene's size is within what the physical device said of the output's format
   and uses (its largest width and height). Larger in either, or a size or a limit of 0: the image is not asked
   for at all. */
static inline int temporal_upscaler_output_fits(unsigned w, unsigned h, unsigned max_w, unsigned max_h) {
    return w > 0 && h > 0 && w <= max_w && h <= max_h;
}
/* Whether the upscaler is still on in this session. off is a latch: once set, by a stop or by eight frames in a
   row that were drawn with jitter and not evaluated, nothing clears it, and only a new session starts clean. */
typedef struct {
    unsigned failures; /* frames in a row that were drawn with jitter and then skipped or failed */
    int off, stopped;  /* off for the session, and whether a stop did it */
} TemporalUpscalerHealth;
/* Whether a frame is drawn with jitter, asked at the gate's draw before any jitter is staged. Only when the
   upscaler is on, the gate accepts the frame, its scene is the whole target, every part of the configuration is
   known there and a context for exactly that configuration is ready. So no frame is ever jittered without an
   evaluate that can follow. The first reason against it is the answer. */
enum {
    TEMPORAL_UPSCALER_JITTER,
    TEMPORAL_UPSCALER_NOT_ON,      /* off for the session: stopped, or eight in a row */
    TEMPORAL_UPSCALER_NOT_GATE,    /* the rectangle's gate does not accept the frame */
    TEMPORAL_UPSCALER_NOT_WHOLE,   /* a fraction below 1 or a viewport that is not the whole target */
    TEMPORAL_UPSCALER_NOT_KNOWN,   /* a part of the configuration is not known at the gate's draw */
    TEMPORAL_UPSCALER_NOT_READY,   /* no context is ready for the configuration */
    TEMPORAL_UPSCALER_REASONS
};
static inline int temporal_upscaler_decide(const TemporalUpscalerHealth* health, int accepted, int whole, int known,
                                           const TemporalUpscalerContexts* c, const TemporalUpscalerConfig* config) {
    if (health->off) return TEMPORAL_UPSCALER_NOT_ON;
    if (!accepted) return TEMPORAL_UPSCALER_NOT_GATE;
    if (!whole) return TEMPORAL_UPSCALER_NOT_WHOLE;
    if (!known) return TEMPORAL_UPSCALER_NOT_KNOWN;
    return temporal_upscaler_ready(c, config) ? TEMPORAL_UPSCALER_JITTER : TEMPORAL_UPSCALER_NOT_READY;
}
/* At the hook of a frame that was drawn with jitter: 1 when it is evaluated. The configuration seen there must
   be the one the frame was decided with, and the context must still be the ready one. 0 is a skipped evaluate. */
static inline int temporal_upscaler_hook(int decision, const TemporalUpscalerConfig* decided,
                                         const TemporalUpscalerConfig* seen, const TemporalUpscalerContexts* c) {
    return decision == TEMPORAL_UPSCALER_JITTER && temporal_upscaler_config_same(decided, seen) &&
           temporal_upscaler_ready(c, seen);
}
/* The frame time handed over, in seconds: the host clock's time between this frame's hook and the hook of the
   frame before, when that frame was evaluated. Else the run's nominal frame time, or a sixtieth without one,
   and *fallback is 1: such a frame has the reset flag. A time counts, measured or nominal, only when it is at
   most a quarter of a second and passes the dispatch's own check (temporal_upscaler_time). */
static inline double temporal_upscaler_seconds(unsigned long long now_ns, unsigned long long before_ns,
                                               int before_evaluated, double nominal, int* fallback) {
    float narrowed;
    const double usual = temporal_upscaler_time(nominal, &narrowed) && nominal <= 0.25 ? nominal : 1.0 / 60.0;
    *fallback = 1;
    if (!before_evaluated || now_ns <= before_ns) return usual;
    const double seconds = (double)(now_ns - before_ns) * 1e-9;
    if (!temporal_upscaler_time(seconds, &narrowed) || seconds > 0.25) return usual;
    *fallback = 0;
    return seconds;
}
/* What came of a frame that was drawn with jitter. EVALUATED: the library recorded and the picture was written
   back. SKIPPED: a number or the configuration did not pass at the hook. FAILED: the library refused. STOPPED:
   something of our own that the hand-over needs failed, or a submission's outcome is not known. */
enum { TEMPORAL_UPSCALER_EVALUATED, TEMPORAL_UPSCALER_SKIPPED, TEMPORAL_UPSCALER_FAILED, TEMPORAL_UPSCALER_STOPPED };
/* Something came of a frame. 1 when the upscaler is off for the rest of the session. A stop turns it off at once
   whenever it is reported, jittered frame or not: a prepare's submission can fail in a frame without jitter.
   The other outcomes count only for a frame drawn with jitter: eight in a row that were skipped or failed turn
   it off, an evaluated one sets that count to 0. Once off, no outcome turns it on. */
static inline int temporal_upscaler_after(TemporalUpscalerHealth* h, int jittered, int outcome) {
    if (h->off) return 1;
    if (outcome == TEMPORAL_UPSCALER_STOPPED) h->off = h->stopped = 1;
    else if (jittered && temporal_upscaler_failed(&h->failures, outcome == TEMPORAL_UPSCALER_EVALUATED)) h->off = 1;
    return h->off;
}
/* What a frame's recycle notes of a context on its way, and it only notes. number: the submission its prepare
   went into. done: the last one the timeline says is done, read 0 when it could not be read. failed: the
   recorder's mark, looked at after the timeline. COMPLETED: it may be published where the runtime next records.
   STOP: the context is kept, never used and never freed. */
/* STOP when a submission did not run as recorded, whatever else is so, and when the timeline could not be read
   eight times in a row (*unread counts them, and a read that works sets it to 0): without the timeline
   nothing is ever known to have completed, and that is not waited for in silence for the whole session. */
#define TEMPORAL_UPSCALER_UNREAD 8
enum { TEMPORAL_UPSCALER_WAIT, TEMPORAL_UPSCALER_COMPLETED, TEMPORAL_UPSCALER_STOP };
static inline int temporal_upscaler_poll(int read, unsigned long long done, int failed, unsigned long long number,
                                         unsigned* unread) {
    if (failed) return TEMPORAL_UPSCALER_STOP;
    if (!read) return ++*unread >= TEMPORAL_UPSCALER_UNREAD ? TEMPORAL_UPSCALER_STOP : TEMPORAL_UPSCALER_WAIT;
    *unread = 0;
    return number && done >= number ? TEMPORAL_UPSCALER_COMPLETED : TEMPORAL_UPSCALER_WAIT;
}
/* The scene colour a frame's decision counts on, before the frame's own is seen: what the frame before had at
   its hook, kept by value with that frame's number. It is no knowledge of this frame. The hook reads the
   frame's own and the two must be equal, or the frame is a skipped evaluate. */
typedef struct {
    unsigned frame; /* the frame whose hook saw it, 0 for none */
    unsigned format, layers;
    int w, h;
} TemporalUpscalerColour;
/* The projection the frame's draws were given, kept by value from the unjittered bytes each jittered copy was made
   from, at the draw that takes it. The decision's projection is a prediction: the camera's block may be another
   slot at the G-buffer, or rewritten since. mixed: the frame's draws did not all get the same one. */
typedef struct {
    unsigned frame;
    int mixed;
    float m[16];
} TemporalUpscalerUsed;
static inline void temporal_upscaler_used(TemporalUpscalerUsed* u, unsigned frame, const float m[16]) {
    if (u->frame != frame) {
        u->frame = frame;
        u->mixed = 0;
        memcpy(u->m, m, sizeof u->m);
    } else if (memcmp(u->m, m, sizeof u->m))
        u->mixed = 1;
}
/* A draw of the frame took a scene camera's block that is not accounted for: another camera's, one that could
   not be given its jittered copy, or one bound in a way that is not looked at. The frame is not evaluated,
   whatever its other draws were given, before or after. */
static inline void temporal_upscaler_poison(TemporalUpscalerUsed* u, unsigned frame) {
    if (u->frame != frame) {
        u->frame = frame;
        memset(u->m, 0, sizeof u->m);
    }
    u->mixed = 1;
}
/* A draw into the scene's targets whose program reads the camera's constant bank, however little of it. 1 when
   it leaves the frame's draws unaccounted for: once the frame is decided, any block there but the decided one
   (address against block), at a slot the camera is known to use or anywhere else. Before the decision, a
   block that cannot decide the frame (taken 0). Nothing is inferred from where a block is. */
static inline int temporal_upscaler_stranger(int decided, unsigned long long address, unsigned long long block,
                                             int taken) {
    return decided ? address != block : !taken;
}
/* 1 when the frame's jittered draws all used one projection and it is the decision's, bit for bit. Only then is
   the frame evaluated, with that projection's planes. Nothing is read again from guest memory at the hook. */
static inline int temporal_upscaler_used_same(const TemporalUpscalerUsed* u, unsigned frame, const float decided[16]) {
    return frame != 0 && u->frame == frame && !u->mixed && !memcmp(u->m, decided, sizeof u->m);
}
/* What a place where the runtime records as usual (the gate's draw, the hook) makes of the recycle's note. The
   recorder's mark is read again there, pending context or not, and outranks the note: a context noted as
   completed is not published once a submission did not run as recorded. */
static inline int temporal_upscaler_noted(int noted, int failed) { return failed ? TEMPORAL_UPSCALER_STOP : noted; }
/* Off means no publication, no jitter and no evaluate. Retirement goes on: 1 when the context on its way may be
   released while the upscaler is off, which is when its submission is noted as completed and the recorder's
   mark is clear. With the mark set everything is kept to the end of the session. It never publishes: after
   temporal_upscaler_stop a completion gives STALE or RELEASE only. */
static inline int temporal_upscaler_retire(int off, int noted, int failed) {
    return off && !failed && noted == TEMPORAL_UPSCALER_COMPLETED;
}
/* The colour target as a hook sees it. Only a target that may be handed to a library keeps a layer count: one
   layer, at the image's first layer, not a member of a shared array. Any other is kept with a count of 0, which
   no decision and no configuration takes. */
static inline TemporalUpscalerColour temporal_upscaler_colour(unsigned frame, unsigned format, unsigned layers,
                                                              unsigned base_layer, int member, int w, int h) {
    const TemporalUpscalerColour c = {frame, format, layers == 1 && !base_layer && !member ? 1u : 0u, w, h};
    return c;
}
/* 1 when the frame before this one reached its hook with a colour target of one layer: only then is there a
   prediction this piece takes. A layered target, which the hook keeps with a count that is not 1, gives none,
   so the decision after the frame it was seen in refuses jitter. */
static inline int temporal_upscaler_predicted(const TemporalUpscalerColour* c, unsigned frame) {
    return c->frame != 0 && frame != 0 && c->frame + 1 == frame && c->w > 0 && c->h > 0 && c->layers == 1;
}
static inline int temporal_upscaler_colour_same(const TemporalUpscalerColour* a, const TemporalUpscalerColour* b) {
    return a->format == b->format && a->layers == b->layers && a->w == b->w && a->h == b->h;
}
/* The reset flag of an evaluate from all its causes, none of which a later one can take back: the gate's history
   and the camera cut, the frame time's fallback (the frame before was not evaluated), and a context that came
   into use since the last evaluate (temporal_upscaler_reset, which is asked here and only here). */
static inline int temporal_upscaler_resets(int frame_reset, int fallback, unsigned published, unsigned* last) {
    const int another = temporal_upscaler_reset(0, published, last);
    return frame_reset || fallback || another;
}
/* The counts between two log lines, one line every TEMPORAL_UPSCALER_EVERY frames that were decided: frames
   evaluated, frames not jittered by their reason, and evaluates that were skipped or failed. */
#define TEMPORAL_UPSCALER_EVERY 600
typedef struct {
    unsigned frames, evaluated, skipped, failed, stopped, not_jittered[TEMPORAL_UPSCALER_REASONS];
    unsigned fated; /* the frame the last fate was counted for: it outlives a window */
} TemporalUpscalerTally;
/* A frame is counted once, its decision and what came of it in the same window. So a window is closed, and its
   line written, at the start of a new frame's decision, when the frame before is final (evaluated, skipped,
   failed, stopped, or its hook missed), and before the new frame is counted. 1 when a line is due then. */
static inline int temporal_upscaler_due(const TemporalUpscalerTally* t) { return t->frames >= TEMPORAL_UPSCALER_EVERY; }
/* a frame was decided */
static inline void temporal_upscaler_decided(TemporalUpscalerTally* t, int decision) {
    if (decision > TEMPORAL_UPSCALER_JITTER && decision < TEMPORAL_UPSCALER_REASONS) t->not_jittered[decision]++;
    t->frames++;
}
/* What came of a frame that was drawn with jitter. A frame has one fate: the first one reported for it is counted,
   and 1 is returned. Later ones for the same frame change nothing here and return 0. (A stop still latches in
   the health whenever it comes: temporal_upscaler_after.) */
static inline int temporal_upscaler_tallied(TemporalUpscalerTally* t, unsigned frame, int outcome) {
    if (frame && t->fated == frame) return 0;
    t->fated = frame;
    if (outcome == TEMPORAL_UPSCALER_EVALUATED) t->evaluated++;
    else if (outcome == TEMPORAL_UPSCALER_SKIPPED) t->skipped++;
    else if (outcome == TEMPORAL_UPSCALER_FAILED) t->failed++;
    else t->stopped++;
    return 1;
}
/* a window's line is written: its counts begin again, and the frame that had the last fate is still known */
static inline void temporal_upscaler_closed(TemporalUpscalerTally* t) {
    const unsigned fated = t->fated;
    memset(t, 0, sizeof *t);
    t->fated = fated;
}
/* SNX_ARCEUS_TEMPORAL_GPUTIME, the pure part: the approximate time between two completions on the GPU's clock.
   A bracket is two timestamps written at the pipeline's last stage, one before and one after a stretch of our
   commands. Each is taken when the commands before it have passed through the pipeline, so their difference is
   the time between those two completions. It is no exclusive time of the stretch and no bound on it. */
#define TEMPORAL_GPU_PAIRS 64    /* brackets that can be under way at once */
#define TEMPORAL_GPU_SAMPLES 600 /* times kept of a kind between two lines */
enum { TEMPORAL_GPU_HOOK, TEMPORAL_GPU_NUMBERS, TEMPORAL_GPU_EMPTY, TEMPORAL_GPU_KINDS };
enum {
    TEMPORAL_GPU_FREE,  /* nothing recorded refers to it */
    TEMPORAL_GPU_BEGUN, /* the first mark is recorded and the second is not */
    TEMPORAL_GPU_ENDED, /* both marks are recorded, in one submission */
    TEMPORAL_GPU_SPLIT, /* the submission ended between the marks: the second was not recorded */
    TEMPORAL_GPU_GIVEN_UP /* begun and given up by its caller before its second mark */
};
/* why a bracket gave no time */
enum {
    TEMPORAL_GPU_EXHAUSTED,   /* no free pair to begin on */
    TEMPORAL_GPU_CUT,         /* split between two submissions */
    TEMPORAL_GPU_PARTIAL,     /* begun and never ended */
    TEMPORAL_GPU_UNAVAILABLE, /* a result was not there though its submission had completed */
    TEMPORAL_GPU_OLD,         /* too long between the first mark and the completion to rule out a turn of the clock */
    TEMPORAL_GPU_ABSURD,      /* a difference longer than the host saw pass */
    TEMPORAL_GPU_OUTDATED,    /* begun before the last line or under another configuration: retired, not a time */
    TEMPORAL_GPU_REASONS
};
typedef struct {
    int state, kind;
    unsigned generation;         /* which bracket of this pair: raised at every begin */
    unsigned window;             /* the count of lines and changes of configuration when it began */
    unsigned long long submit;   /* the number of the submission both marks belong to */
    unsigned long long begun_ns; /* the host's clock when the first mark was recorded, 0 for not known */
} TemporalGpuPair;
/* A bracket's handle: the pair and which of its brackets, so that an end that comes late cannot close another
   bracket that has taken the pair since. -1 is no bracket. */
typedef long long TemporalGpuHandle;
static inline int temporal_gpu_index(TemporalGpuHandle h) { return h < 0 ? -1 : (int)(h % TEMPORAL_GPU_PAIRS); }
typedef struct {
    TemporalGpuPair pair[TEMPORAL_GPU_PAIRS];
    int off; /* switched off for the session: nothing is begun and nothing recorded earlier is read */
    /* Times and counts belong to one window: the stretch between two lines under one configuration. window
       counts them. For each kind: brackets asked for, and those that gave no time by their reason. late: ends
       and givings up for a bracket that was no longer its pair's, which have no kind. */
    unsigned window;
    unsigned long long asked[TEMPORAL_GPU_KINDS], lost[TEMPORAL_GPU_KINDS][TEMPORAL_GPU_REASONS], late;
    unsigned n[TEMPORAL_GPU_KINDS];
    float ms[TEMPORAL_GPU_KINDS][TEMPORAL_GPU_SAMPLES];
} TemporalGpuRing;
/* A bracket begins on a free pair only: its handle, or -1 when the measure is off or no pair is free. The caller
   then records the pair's reset and its first mark, in the submission it named. */
static inline TemporalGpuHandle temporal_gpu_begin(TemporalGpuRing* r, int kind, unsigned long long submit,
                                                   unsigned long long now_ns) {
    if (r->off || kind < 0 || kind >= TEMPORAL_GPU_KINDS) return -1;
    r->asked[kind]++;
    for (int i = 0; i < TEMPORAL_GPU_PAIRS; i++)
        if (r->pair[i].state == TEMPORAL_GPU_FREE) {
            const unsigned generation = r->pair[i].generation + 1;
            r->pair[i] = (TemporalGpuPair){TEMPORAL_GPU_BEGUN, kind, generation, r->window, submit, now_ns};
            return (TemporalGpuHandle)generation * TEMPORAL_GPU_PAIRS + i;
        }
    r->lost[kind][TEMPORAL_GPU_EXHAUSTED]++;
    return -1;
}
/* the pair of a handle when the handle is still that pair's own bracket and the bracket is begun, else NULL */
static inline TemporalGpuPair* temporal_gpu_own(TemporalGpuRing* r, TemporalGpuHandle h) {
    const int index = temporal_gpu_index(h);
    if (index < 0) return NULL;
    TemporalGpuPair* p = &r->pair[index];
    if ((TemporalGpuHandle)p->generation != h / TEMPORAL_GPU_PAIRS || p->state != TEMPORAL_GPU_BEGUN) {
        if (!r->off) r->late++;
        return NULL;
    }
    return p;
}
/* The bracket's end. 1 when the caller is to record the second mark: the submission is still the first mark's.
   0 when it is another (the pair is split, the mark is not recorded, no time will be made of it), or when the
   handle is not the pair's bracket any more, which is counted and changes nothing. */
static inline int temporal_gpu_end(TemporalGpuRing* r, TemporalGpuHandle h, unsigned long long submit) {
    TemporalGpuPair* p = r->off ? NULL : temporal_gpu_own(r, h);
    if (!p) return 0;
    p->state = submit == p->submit ? TEMPORAL_GPU_ENDED : TEMPORAL_GPU_SPLIT;
    return p->state == TEMPORAL_GPU_ENDED;
}
/* A bracket that will get no second mark, said by its caller. A begun pair leaves the ring only through its end
   or this: until one of them it is not settled and not free, whatever its submission did. */
static inline void temporal_gpu_abandon(TemporalGpuRing* r, TemporalGpuHandle h) {
    TemporalGpuPair* p = r->off ? NULL : temporal_gpu_own(r, h);
    if (p) p->state = TEMPORAL_GPU_GIVEN_UP;
}
/* 1 when a pair's results are to be fetched now: both marks recorded and its submission known to have completed */
static inline int temporal_gpu_fetch(const TemporalGpuRing* r, int index, unsigned long long done) {
    if (index < 0 || index >= TEMPORAL_GPU_PAIRS) return 0;
    return !r->off && r->pair[index].state == TEMPORAL_GPU_ENDED && done >= r->pair[index].submit;
}
/* the second tick less the first on a counter of so many valid bits, which may have gone round once between */
static inline unsigned long long temporal_gpu_ticks(unsigned long long first, unsigned long long second,
                                                    unsigned bits) {
    const unsigned long long delta = second - first;
    return bits >= 64 ? delta : delta & ((1ull << bits) - 1ull);
}
/* how long one turn of such a counter lasts in nanoseconds: the period times two to the valid bits, a finite
   number for every period a device gives, with all 64 bits too */
static inline double temporal_gpu_turn_ns(unsigned bits, double period_ns) {
    return ldexp(period_ns, bits > 64 ? 64 : (int)bits);
}
/* A pair whose submission has completed leaves the ring: it gives a time or is counted by its reason, and is
   free. ticks and available: its two results as fetched, when temporal_gpu_fetch named it, else NULL. A time
   needs both results, less than half a turn of the counter on the host's clock since the first mark, and a
   difference no longer than that. 0: not completed, begun and neither ended nor given up, or the measure off. */
static inline int temporal_gpu_settle(TemporalGpuRing* r, int index, unsigned long long done,
                                      unsigned long long now_ns, const unsigned long long* ticks,
                                      const int* available, unsigned bits, double period_ns) {
    TemporalGpuPair* p = &r->pair[index];
    if (r->off || p->state == TEMPORAL_GPU_FREE || p->state == TEMPORAL_GPU_BEGUN || done < p->submit) return 0;
    unsigned long long* lost = r->lost[p->kind];
    /* A bracket of an earlier window is retired here like any other, on its own completion and not before. It
       is no time of this window and is counted as that, whatever its results are. */
    if (p->window != r->window) lost[TEMPORAL_GPU_OUTDATED]++;
    else if (p->state == TEMPORAL_GPU_SPLIT) lost[TEMPORAL_GPU_CUT]++;
    else if (p->state == TEMPORAL_GPU_GIVEN_UP) lost[TEMPORAL_GPU_PARTIAL]++;
    else if (!ticks || !available || !available[0] || !available[1]) lost[TEMPORAL_GPU_UNAVAILABLE]++;
    else {
        const double age = p->begun_ns && now_ns >= p->begun_ns ? (double)(now_ns - p->begun_ns) : -1.0;
        const double ns = (double)temporal_gpu_ticks(ticks[0], ticks[1], bits) * period_ns;
        if (!(age >= 0.0) || !(age < 0.5 * temporal_gpu_turn_ns(bits, period_ns))) lost[TEMPORAL_GPU_OLD]++;
        else if (!isfinite(ns) || !(ns <= age)) lost[TEMPORAL_GPU_ABSURD]++;
        else if (r->n[p->kind] < TEMPORAL_GPU_SAMPLES) r->ms[p->kind][r->n[p->kind]++] = (float)(ns / 1e6);
    }
    p->state = TEMPORAL_GPU_FREE;
    return 1;
}
/* the brackets of a kind that began in this window and have not left the ring: asked for and not yet counted */
static inline unsigned temporal_gpu_pending(const TemporalGpuRing* r, int kind) {
    unsigned n = 0;
    for (int i = 0; i < TEMPORAL_GPU_PAIRS; i++)
        n += r->pair[i].state != TEMPORAL_GPU_FREE && r->pair[i].kind == kind && r->pair[i].window == r->window;
    return n;
}
/* 1 when a device's clock can be used: timestamps on the queue, some valid bits, a period that is a number, and
   a turn of the counter that is a finite time */
static inline int temporal_gpu_usable(int timestamps, unsigned bits, double period_ns) {
    if (!timestamps || bits == 0 || bits > 64 || !isfinite(period_ns) || !(period_ns > 0.0)) return 0;
    return isfinite(temporal_gpu_turn_ns(bits, period_ns)) && temporal_gpu_turn_ns(bits, period_ns) > 0.0;
}
/* Switched off for the session, after a failure: nothing begins and nothing recorded before is read. The pairs
   keep their state, since what was recorded with them may still be on its way. */
static inline void temporal_gpu_stop(TemporalGpuRing* r) { r->off = 1; }
/* Which stretch of presents, under which configuration, the kept times belong to. The configuration is whole
   numbers only, so that two can be held against each other. */
#define TEMPORAL_GPU_CONFIG 16
typedef struct {
    int have;
    unsigned config[TEMPORAL_GPU_CONFIG];
    unsigned began; /* the present the window began at */
} TemporalGpuWindow;
/* At a present's boundary, whether or not our passes ran: 1 when the window is full and is to be written and
   closed. presents gets its length. The next window begins at this present, under the same configuration. */
static inline int temporal_gpu_tick(TemporalGpuWindow* w, unsigned present, unsigned* presents) {
    *presents = w->have ? present - w->began : 0;
    if (!w->have || *presents < TEMPORAL_GPU_SAMPLES) return 0;
    w->began = present;
    return 1;
}
/* Before a frame's first bracket begins: the configuration it is taken under. 1 when the window was taken under
   another and is to be written and closed first, with its own configuration, which before gets. Then, and when
   there was no window yet, the window is this configuration's and begins at this present, so the bracket that
   follows is its first. */
static inline int temporal_gpu_configure(TemporalGpuWindow* w, const unsigned* config, unsigned present,
                                         unsigned* before, unsigned* presents) {
    const int had = w->have;
    if (had && !memcmp(w->config, config, sizeof w->config)) return 0;
    if (had) memcpy(before, w->config, sizeof w->config);
    *presents = had ? present - w->began : 0;
    w->have = 1;
    memcpy(w->config, config, sizeof w->config);
    w->began = present;
    return had;
}
/* the median, the mean and the 95th percentile of a kind's times, which are sorted in place: 0 without any */
static inline int temporal_gpu_summary(float* ms, unsigned n, double* median, double* mean, double* high) {
    double sum = 0.0;
    *median = *mean = *high = 0.0;
    if (!n) return 0;
    for (unsigned i = 1; i < n; i++) { /* few and nearly sorted is rare here, but 600 at most */
        const float v = ms[i];
        unsigned j = i;
        for (; j > 0 && ms[j - 1] > v; j--) ms[j] = ms[j - 1];
        ms[j] = v;
    }
    for (unsigned i = 0; i < n; i++) sum += (double)ms[i];
    *mean = sum / (double)n;
    *median = n & 1 ? (double)ms[n / 2] : 0.5 * ((double)ms[n / 2 - 1] + (double)ms[n / 2]);
    *high = (double)ms[(unsigned)ceil(0.95 * (double)n) - 1];
    return 1;
}
/* A window ends, at a line or at a change of configuration: the times and the counts start anew, empty. The
   pairs under way are not touched: each stays in the ring until its own submission has completed, and is then
   retired without giving a time to the new window. */
static inline void temporal_gpu_restart(TemporalGpuRing* r) {
    r->window++;
    r->late = 0;
    for (int k = 0; k < TEMPORAL_GPU_KINDS; k++) {
        r->n[k] = 0;
        r->asked[k] = 0;
        for (int why = 0; why < TEMPORAL_GPU_REASONS; why++) r->lost[k][why] = 0;
    }
}
#endif
