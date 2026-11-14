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
    for (int c = 0; c < 4; c++) clip[c] = p[0] * both[c] + p[1] * both[4 + c] + p[2] * both[8 + c] + p[3] * both[12 + c];
    now[0] = p[0];
    now[1] = p[1];
    if (!(clip[3] > 0.0)) return 0;
    before[0] = clip[0] / clip[3];
    before[1] = clip[1] / clip[3];
    return 1;
}
/* The viewport gate. The jitter and the resolve hold only while the scene's viewport is the whole target and the
   one of the frame before. A frame is decided once, by its first draw that binds the scene camera's block and draws
   into the scene's depth, from that draw's viewport (temporal_gate_decide, 1 for jitter). A draw that binds the
   block before that one and cannot be tied to the scene gates the whole frame (temporal_gate_early): no draw of a
   frame may differ from another in jitter. The G-buffer's viewport is then held against the decided one
   (temporal_gate_scene, 1 for a mismatch). A frame is steady, and may be resolved and kept as history, when it
   was decided with jitter and did not mismatch. Frames count from 1. */
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
/* a binder before the frame's decision: 1 when it gated the frame, 0 when the frame was decided already */
static inline int temporal_gate_early(TemporalGate* g, unsigned frame) {
    if (g->decided_at == frame) return 0;
    g->early_at = frame;
    return 1;
}
static inline int temporal_gate_jitter(const TemporalGate* g, unsigned frame) {
    return g->decided_at == frame && g->jitter;
}
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
    TEMPORAL_BEFORE_OTHER     /* established, but not by this draw */
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
/* Whose a kept copy of skinned positions is. The game captures a character's positions into a buffer at some
   address, and a copy is kept per address and frame. The address does not say whose positions they are: another
   character may capture into it the next frame. So a copy is tagged with the G-buffer draws that read it, each by
   its block's pair and its signature, and a draw gets last frame's copy only when that copy has its tag and the
   address was captured exactly once in both frames.
   One rule keeps the tags honest. Every capture into the address is an event, counted before anything can fail:
   it takes a new generation of the address, drops the copy's tags and leaves the copy's content unknown until the
   bytes were really copied (temporal_stream_copied). A copy counts only while it holds the bytes of the last event
   of its frame. Buffers that are made anew lose their content (temporal_stream_poison), never their counts. */
#define TEMPORAL_STREAM_OWNERS 4
typedef struct {
    unsigned at, captures, owners; /* the frame of the copy, the captures into its address in it, the tags */
    unsigned generation, copied;   /* of the last capture in that frame, and of the bytes held (0 for none) */
    unsigned long long pair[TEMPORAL_STREAM_OWNERS], draw[TEMPORAL_STREAM_OWNERS];
} TemporalStreamCopy;
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
static inline void temporal_stream_poison(TemporalStreamCopy* c) {
    c->copied = 0;
    c->owners = 0;
}
static inline int temporal_stream_valid(const TemporalStreamCopy* c) {
    return c->copied != 0 && c->copied == c->generation;
}
static inline int temporal_stream_owned(const TemporalStreamCopy* c, unsigned long long pair,
                                        unsigned long long draw) {
    for (unsigned i = 0; i < c->owners; i++)
        if (c->pair[i] == pair && c->draw[i] == draw) return 1;
    return 0;
}
/* a G-buffer draw of an established pair reads this frame's capture: its tag is added to a copy that holds that
   capture's bytes, while there is room */
static inline void temporal_stream_used(TemporalStreamCopy* c, unsigned frame, unsigned long long pair,
                                        unsigned long long draw) {
    if (c->at != frame || !temporal_stream_valid(c) || temporal_stream_owned(c, pair, draw) ||
        c->owners >= TEMPORAL_STREAM_OWNERS)
        return;
    c->pair[c->owners] = pair;
    c->draw[c->owners++] = draw;
}
/* 1 when the draw may take last frame's copy as its positions of a frame ago, 0 when there is none that is its
   own, and -1 when the address was captured more than once in either frame */
static inline int temporal_stream_before(const TemporalStreamCopy* now, const TemporalStreamCopy* last,
                                         unsigned frame, unsigned long long pair, unsigned long long draw) {
    if (now->at != frame || !last->at || last->at + 1 != frame) return 0;
    if (now->captures != 1 || last->captures != 1) return -1;
    if (!temporal_stream_valid(now) || !temporal_stream_valid(last)) return 0;
    return temporal_stream_owned(last, pair, draw);
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
#endif
