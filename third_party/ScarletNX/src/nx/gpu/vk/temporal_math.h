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
   and inside the target. A negative height means y is the bottom edge. A width that is not positive is refused. */
static inline int temporal_rect_from_viewport(const TemporalViewport* v, int target_w, int target_h, TemporalRect* r) {
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
/* the corner of the target the game puts the rectangle in, in image axes */
enum { TEMPORAL_CORNER_TOP_LEFT, TEMPORAL_CORNER_TOP_RIGHT, TEMPORAL_CORNER_BOTTOM_LEFT, TEMPORAL_CORNER_BOTTOM_RIGHT };
/* What two frames must have in common, and why a frame has none of it. The viewport is kept as it was given,
   with its signs: two viewports with the same rectangle and y the other way round are not the same. */
typedef struct {
    TemporalRect rect;
    TemporalViewport viewport;
    int target_w, target_h;
    float fx, fy; /* the used fraction as the scene block reports it */
} TemporalRectState;
enum {
    TEMPORAL_RECT_OK,
    TEMPORAL_RECT_FRACTION, /* a fraction not finite or not in (0, 1] */
    TEMPORAL_RECT_UNEQUAL,  /* the two fractions more than 1e-6 apart: one scale for both axes only */
    TEMPORAL_RECT_VIEWPORT, /* no whole rectangle inside the target */
    TEMPORAL_RECT_SHAPE     /* the whole target without a fraction of 1, or not the fraction's size at the corner */
};
/* A frame's structure from the viewport of the draw that decides it and the fraction read at that draw. OK with
   the state filled in when the rectangle is the whole target and the fraction is 1 within 1e-6, or it sits at the
   game's corner with a size within one pixel of round(W x fx) by round(H x fy). */
static inline int temporal_rect_structure(const TemporalViewport* v, int target_w, int target_h, float fx, float fy,
                                          int corner, TemporalRectState* out) {
    if (!isfinite(fx) || !isfinite(fy) || !(fx > 0.0f) || !(fy > 0.0f) || fx > 1.0f || fy > 1.0f)
        return TEMPORAL_RECT_FRACTION;
    if (fabs((double)fx - (double)fy) > 1e-6) return TEMPORAL_RECT_UNEQUAL;
    TemporalRect r;
    if (target_w <= 0 || target_h <= 0 || !temporal_rect_from_viewport(v, target_w, target_h, &r))
        return TEMPORAL_RECT_VIEWPORT;
    const int whole = r.x == 0 && r.y == 0 && r.w == target_w && r.h == target_h;
    if (whole) {
        /* a whole target the game reports as partly used is not understood */
        if (fabs((double)fx - 1.0) > 1e-6 || fabs((double)fy - 1.0) > 1e-6) return TEMPORAL_RECT_SHAPE;
    } else {
        const int left = corner == TEMPORAL_CORNER_TOP_LEFT || corner == TEMPORAL_CORNER_BOTTOM_LEFT;
        const int top = corner == TEMPORAL_CORNER_TOP_LEFT || corner == TEMPORAL_CORNER_TOP_RIGHT;
        const double want_w = round((double)target_w * (double)fx), want_h = round((double)target_h * (double)fy);
        if (r.x != (left ? 0 : target_w - r.w) || r.y != (top ? 0 : target_h - r.h)) return TEMPORAL_RECT_SHAPE;
        if (fabs((double)r.w - want_w) > 1.0 || fabs((double)r.h - want_h) > 1.0) return TEMPORAL_RECT_SHAPE;
    }
    *out = (TemporalRectState){r, *v, target_w, target_h, fx, fy};
    return TEMPORAL_RECT_OK;
}
static inline int temporal_rect_state_same(const TemporalRectState* a, const TemporalRectState* b) {
    return temporal_rect_same(&a->rect, &b->rect) && temporal_viewport_same(&a->viewport, &b->viewport) &&
           a->target_w == b->target_w && a->target_h == b->target_h && a->fx == b->fx && a->fy == b->fy;
}
/* what a history was made from: any difference and it is not used */
typedef struct {
    unsigned long long target, generation; /* the scene colour's identity and which making of it */
    int target_w, target_h, out_w, out_h;
    TemporalRect rect;
} TemporalRectStamp;
static inline int temporal_rect_stamp_same(const TemporalRectStamp* a, const TemporalRectStamp* b) {
    return a->target == b->target && a->generation == b->generation && a->target_w == b->target_w &&
           a->target_h == b->target_h && a->out_w == b->out_w && a->out_h == b->out_h &&
           temporal_rect_same(&a->rect, &b->rect);
}
/* The gate for the rectangle. The candidate is the structure of the frame before, kept whenever that frame was
   structurally valid, also when it was gated. The stamp is set by a frame that wrote history. A frame is accepted
   only when it equals the candidate: a new rectangle is gated once, then accepted without history, then with it.
   A frame without structure, a late disagreement or a missing frame clears both. Frames count from 1. */
typedef struct {
    int have_candidate, have_stamp;
    unsigned candidate_at, stamp_at; /* the frames they are of */
    TemporalRectState candidate;
    TemporalRectStamp stamp;
    unsigned decided_at; /* the frame decided last, and what was decided */
    int accepted;
} TemporalRectGate;
static inline void temporal_rect_clear(TemporalRectGate* g) { g->have_candidate = g->have_stamp = 0; }
/* The frame is decided, once, at its first draw that binds the scene camera's block. state is its structure, NULL
   when it has none (temporal_rect_structure refused, or an unknown draw bound the block earlier). 1: accepted,
   the frame is jittered. 0: gated, the frame is left as drawn. */
static inline int temporal_rect_decide(TemporalRectGate* g, unsigned frame, const TemporalRectState* state) {
    if (g->have_candidate && g->candidate_at + 1 != frame) temporal_rect_clear(g); /* a frame is missing */
    if (g->have_stamp && g->stamp_at + 1 != frame) g->have_stamp = 0;
    g->decided_at = frame;
    g->accepted = 0;
    if (!state) {
        temporal_rect_clear(g);
        return 0;
    }
    g->accepted = g->have_candidate && temporal_rect_state_same(&g->candidate, state);
    if (!g->accepted) g->have_stamp = 0; /* a change starts the history anew */
    g->have_candidate = 1;
    g->candidate_at = frame;
    g->candidate = *state;
    return g->accepted;
}
/* A later draw into the scene, or a later scene block, in the same frame: state is what it shows, NULL for none.
   1 when it disagrees with what the frame was decided on. The frame is then not valid: left as drawn, and the
   frames after start again. */
static inline int temporal_rect_late(TemporalRectGate* g, unsigned frame, const TemporalRectState* state) {
    if (g->decided_at != frame || !g->have_candidate || g->candidate_at != frame) return g->decided_at == frame;
    if (state && temporal_rect_state_same(&g->candidate, state)) return 0;
    temporal_rect_clear(g);
    g->accepted = 0;
    return 1;
}
/* 1 while the frame is accepted and nothing disagreed: it may be resolved */
static inline int temporal_rect_steady(const TemporalRectGate* g, unsigned frame) {
    return g->decided_at == frame && g->accepted && g->have_candidate && g->candidate_at == frame;
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
/* 1 with the numbers when the frame may be given to the upscaler: it is steady, the viewport is its own, the sizes
   are positive and the jitter is finite and within a pixel each way, so every number handed on is a small one.
   Reset when no history of the frame before may be used (temporal_rect_history) or the camera cut. 0: the frame is
   not evaluated and is left as drawn. Whoever evaluates with success calls temporal_rect_wrote. */
static inline int temporal_upscaler_frame(const TemporalRectGate* g, unsigned frame, const TemporalRectStamp* stamp,
                                          const TemporalViewport* viewport, const double jitter[2], int cut,
                                          TemporalUpscalerFrame* out) {
    TemporalRect r;
    if (!temporal_rect_steady(g, frame) || !temporal_viewport_same(viewport, &g->candidate.viewport)) return 0;
    if (!temporal_rect_from_viewport(viewport, stamp->target_w, stamp->target_h, &r)) return 0;
    if (!temporal_rect_same(&r, &g->candidate.rect) || !temporal_rect_same(&r, &stamp->rect)) return 0;
    if (stamp->target_w != g->candidate.target_w || stamp->target_h != g->candidate.target_h) return 0;
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
    out->reset = cut || !temporal_rect_history(g, frame, stamp);
    return 1;
}
/* The camera's numbers and the frame time, checked after the frame's numbers are made and before every dispatch
   to an upscaler. 1 with the frame time in milliseconds when all hold: near positive, the field of view positive
   and below pi, the frame time positive, far beyond near, or infinite exactly when the context was made for an
   infinite far plane, and every number finite and no bigger than a float holds. 0: the frame is not evaluated. */
static inline int temporal_upscaler_camera(double near_plane, double far_plane, double fov_y, double seconds,
                                           int far_infinite, float* milliseconds) {
    const double most = 3.4028234663852886e38, ms = seconds * 1000.0, pi = 3.14159265358979323846;
    if (!isfinite(near_plane) || !(near_plane > 0.0) || near_plane > most) return 0;
    if (!isfinite(fov_y) || !(fov_y > 0.0) || !(fov_y < pi)) return 0;
    if (!isfinite(seconds) || !(seconds > 0.0) || !isfinite(ms) || ms > most) return 0;
    if (far_infinite) {
        if (!(isinf(far_plane) && far_plane > 0.0)) return 0;
    } else if (!isfinite(far_plane) || !(far_plane > near_plane) || far_plane > most)
        return 0;
    *milliseconds = (float)ms;
    return *milliseconds > 0.0f;
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
/* The upscaler's failures in a row. A frame that fails writes no history, so the next one has the reset. 1 when
   the upscaler is to be off for the rest of the session. */
#define TEMPORAL_UPSCALER_FAILURES 8
static inline int temporal_upscaler_failed(unsigned* failures, int ok) {
    if (ok) *failures = 0;
    else if (*failures < TEMPORAL_UPSCALER_FAILURES) ++*failures;
    return *failures >= TEMPORAL_UPSCALER_FAILURES;
}
#endif
