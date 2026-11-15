/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_VK_TEMPORAL_SPATIAL_H
#define SNX_VK_TEMPORAL_SPATIAL_H
/* D3-2, the hand-over with our own scaler (SNX_ARCEUS_UPSCALER=spatial, UPSCALER-DESIGN.md D3 revisions 2 to 4 and
   D3-2 with its revisions): the private resolve is scaled into the picture part of a larger texture whose rest
   repeats the picture's edge, and the audited readers of the scene colour are given that texture. Pure: what may be
   done, decided from numbers; the renderer (temporal.c, render.c) does it. */
#include <math.h>
#include <stdint.h>
#include <string.h>

/* The larger texture's size: M = W x W / w and N = H x H / h, only when both are whole and the fractions are w / W
   and h / H within 1e-6, both the same (D3 revision 2 rule 2, D2 rule 13). 1 with M and N, else 0. */
static inline int temporal_spatial_size(int W, int H, int w, int h, double fx, double fy, int* M, int* N) {
    if (W <= 0 || H <= 0 || w <= 0 || h <= 0 || w > W || h > H) return 0;
    if (!(fabs(fx - fy) <= 1e-6) || !(fabs(fx - (double)w / W) <= 1e-6) || !(fabs(fy - (double)h / H) <= 1e-6))
        return 0;
    const int64_t mw = (int64_t)W * W, nh = (int64_t)H * H;
    if (mw % w || nh % h) return 0;
    *M = (int)(mw / w);
    *N = (int)(nh / h);
    return 1;
}

/* Where the picture goes in the larger texture and what repeats its edge: the picture W x H at the origin, then three
   stretched copies (the last column over the right part, the last row over the bottom part, the last pixel over
   the corner), which equal a clamp at the picture's edge for a footprint of any size (rule 3). A part of no size is
   left out: count says how many regions there are. Each region: from x0, y0, x1, y1 to x0, y0, x1, y1. */
typedef struct {
    int from[4], to[4];
} TemporalSpatialRegion;
static inline int temporal_spatial_fill(int W, int H, int M, int N, TemporalSpatialRegion out[3]) {
    int count = 0;
    if (M > W) out[count++] = (TemporalSpatialRegion){{W - 1, 0, W, H}, {W, 0, M, H}};
    if (N > H) out[count++] = (TemporalSpatialRegion){{0, H - 1, W, H}, {0, H, W, N}};
    if (M > W && N > H) out[count++] = (TemporalSpatialRegion){{W - 1, H - 1, W, H}, {W, H, M, N}};
    return count;
}

/* The audited readers (D3 revision 4, measured in P1 and P2): fragment and vertex program by their whole hashes, the
   scene colour's handle offset in words, how the program's coordinate of that binding is made, and whether the
   composite's +794 and +798 must be 1. */
enum {
    TEMPORAL_SPATIAL_RAW,      /* the vertex coordinate as it is (26fc2347): B46268AD */
    TEMPORAL_SPATIAL_CLAMPED,  /* min(f - 1/Q, the coordinate times f) (c6aaf96f's f, the fragment's clamp): 21FBFBEC */
    TEMPORAL_SPATIAL_COMPOSITE /* min(f - 1/Q, f x the coordinate / +794): the composite */
};
typedef struct {
    uint64_t fs, vs;
    uint32_t handle; /* the binding's handle offset in words */
    int tap;         /* TEMPORAL_SPATIAL_* above */
    int scaled_vertex; /* the vertex program multiplies the coordinate by the fraction (c6aaf96f) */
} TemporalSpatialEntry;
#define TEMPORAL_SPATIAL_ENTRIES 3
static const TemporalSpatialEntry temporal_spatial_entries[TEMPORAL_SPATIAL_ENTRIES] = {
    {0x21fbfbecbf90f4a3ull, 0xc6aaf96f6c29a40eull, 0x2A, TEMPORAL_SPATIAL_CLAMPED, 1},
    {0xb46268adda62e218ull, 0x26fc2347977486d7ull, 0x28, TEMPORAL_SPATIAL_RAW, 0},
    {0x8a160a27d90af72dull, 0x26fc2347977486d7ull, 0x36, TEMPORAL_SPATIAL_COMPOSITE, 0}};
/* the entry of a fragment program and binding, or -1 */
static inline int temporal_spatial_entry(uint64_t fs, uint32_t handle) {
    for (int k = 0; k < TEMPORAL_SPATIAL_ENTRIES; k++)
        if (temporal_spatial_entries[k].fs == fs && temporal_spatial_entries[k].handle == handle) return k;
    return -1;
}

/* What a draw that binds the scene colour is, as the renderer sees it (D3-2 and its revisions 2 and 3) */
enum { TEMPORAL_SPATIAL_EDGE, TEMPORAL_SPATIAL_BORDER, TEMPORAL_SPATIAL_OTHER_ADDRESS };
typedef struct {
    uint64_t vs;              /* the vertex program's whole hash */
    int direct;               /* no index buffer, no count read from memory */
    int triangle_list;        /* the topology */
    uint32_t first_vertex, vertex_count, instance_count, first_instance;
    int other_stages;         /* a tessellation or geometry program bound */
    double vertex_f[2];       /* the fraction the vertex stage's scene block holds (NAN when not read or bound) */
    double fragment_f[2];     /* the fragment stage's */
    double divisor[2];        /* the fragment stage's +794 and +798 (NAN when not read or bound) */
    double viewport[4];       /* x, y, width, height, the height signed */
    int scissor_on;
    int scissor[4];           /* x0, y0, x1, y1 */
    int target[2];            /* the target's width and height */
    uint32_t query[2];        /* what the size query of the binding answers (today's, temporal_txq_today) */
    /* the binding's sampler and view */
    int normalised, mag_nearest, min_nearest, one_level, anisotropy, compare;
    int address[2];           /* TEMPORAL_SPATIAL_EDGE, _BORDER or _OTHER_ADDRESS for u and v */
    int view_format_same, view_2d, view_one_level, view_one_layer, view_identity;
} TemporalSpatialDraw;
enum {
    TEMPORAL_SPATIAL_TAKEN,
    TEMPORAL_SPATIAL_NOT_LISTED,   /* the program and binding are not an entry */
    TEMPORAL_SPATIAL_VERTEX,       /* another vertex program */
    TEMPORAL_SPATIAL_FORM,         /* not a direct, non-indexed triangle list of one instance from vertex 0, 3 long */
    TEMPORAL_SPATIAL_STAGES,       /* a tessellation or geometry program */
    TEMPORAL_SPATIAL_FRACTION,     /* a fraction the programs read is not the replacement's */
    TEMPORAL_SPATIAL_DIVISOR,      /* the composite's +794 or +798 not 1 */
    TEMPORAL_SPATIAL_SAMPLER,      /* not normalised, more than one level, anisotropy, compare, or an address mode */
    TEMPORAL_SPATIAL_VIEW,         /* another format, not 2D, not one level and layer, not the identity mapping */
    TEMPORAL_SPATIAL_COVERAGE,     /* a covered coordinate beyond the fraction, or a tap outside 0 to below 1 */
    TEMPORAL_SPATIAL_REASONS
};
static inline const char* temporal_spatial_text(int why) {
    static const char* const text[TEMPORAL_SPATIAL_REASONS] = {
        "taken",
        "the program and binding are not on the list",
        "its vertex program is not the audited one",
        "it is not a direct, non-indexed triangle list of one instance from vertex 0 and 3 vertices",
        "a tessellation or geometry program is bound",
        "a fraction its programs read is not the replacement's",
        "the composite's divisors are not 1",
        "its sampler is not the audited form",
        "its view is not the scene colour's own, 2D, one level and layer, with the identity mapping",
        "a coordinate it covers is beyond the fraction, or a tap lies outside 0 to below 1"};
    return why >= 0 && why < TEMPORAL_SPATIAL_REASONS ? text[why] : "?";
}

/* The range of one axis's vertex coordinate over the pixel centres the draw covers (the viewport, the scissor and the
   target), both mappings putting coordinate 0 where a flipped viewport starts: x (p - vx) / vw, y 1 - (p - vy) / vh.
   0 when nothing is covered. */
static inline int temporal_spatial_covered(const TemporalSpatialDraw* d, int axis, double* lo, double* hi) {
    const double v0 = d->viewport[axis], vs = d->viewport[axis + 2];
    if (!(fabs(vs) > 0.0)) return 0;
    double a = vs > 0 ? v0 : v0 + vs, b = vs > 0 ? v0 + vs : v0; /* the viewport's pixels */
    if (a < 0) a = 0;
    if (b > d->target[axis]) b = d->target[axis];
    if (d->scissor_on) {
        if (a < d->scissor[axis]) a = d->scissor[axis];
        if (b > d->scissor[axis + 2]) b = d->scissor[axis + 2];
    }
    const double first = ceil(a - 0.5) + 0.5, last = floor(b - 0.5) + 0.5; /* the centres inside [a, b) */
    if (!(last >= first)) return 0;
    double c0, c1;
    if (axis == 0) {
        c0 = (first - v0) / vs;
        c1 = (last - v0) / vs;
    } else {
        c0 = 1.0 - (first - v0) / vs;
        c1 = 1.0 - (last - v0) / vs;
    }
    *lo = c0 < c1 ? c0 : c1;
    *hi = c0 < c1 ? c1 : c0;
    return 1;
}

/* Whether entry k may be given the larger texture at this draw, for the replacement's fraction f: the first reason
   against it, or TEMPORAL_SPATIAL_TAKEN. Every covered coordinate must be at most f and every tap at least 0 and
   below 1 on both axes, from the audited mapping and the entry's tap (recon 2026-11-15 01:40). */
static inline int temporal_spatial_check(int k, const TemporalSpatialDraw* d, double f) {
    if (k < 0 || k >= TEMPORAL_SPATIAL_ENTRIES) return TEMPORAL_SPATIAL_NOT_LISTED;
    const TemporalSpatialEntry* e = &temporal_spatial_entries[k];
    if (d->vs != e->vs) return TEMPORAL_SPATIAL_VERTEX;
    if (!d->direct || !d->triangle_list || d->first_vertex != 0 || d->vertex_count != 3 || d->instance_count != 1 ||
        d->first_instance != 0)
        return TEMPORAL_SPATIAL_FORM;
    if (d->other_stages) return TEMPORAL_SPATIAL_STAGES;
    for (int a = 0; a < 2; a++) {
        /* the fraction where a program of the entry reads it: the clamp's and the composite's fragment stage,
           c6aaf96f's vertex stage. B46268AD reads none for its coordinate. */
        if (e->tap != TEMPORAL_SPATIAL_RAW && !(d->fragment_f[a] == f)) return TEMPORAL_SPATIAL_FRACTION;
        if (e->scaled_vertex && !(d->vertex_f[a] == f)) return TEMPORAL_SPATIAL_FRACTION;
        if (e->tap == TEMPORAL_SPATIAL_COMPOSITE && !(d->divisor[a] == 1.0)) return TEMPORAL_SPATIAL_DIVISOR;
    }
    if (!d->normalised || !d->one_level || d->anisotropy || d->compare) return TEMPORAL_SPATIAL_SAMPLER;
    for (int a = 0; a < 2; a++)
        if (!(d->address[a] == TEMPORAL_SPATIAL_EDGE ||
              (d->address[a] == TEMPORAL_SPATIAL_BORDER && d->mag_nearest && d->min_nearest)))
            return TEMPORAL_SPATIAL_SAMPLER;
    if (!d->view_format_same || !d->view_2d || !d->view_one_level || !d->view_one_layer || !d->view_identity)
        return TEMPORAL_SPATIAL_VIEW;
    for (int a = 0; a < 2; a++) {
        double lo, hi;
        if (!temporal_spatial_covered(d, a, &lo, &hi)) return TEMPORAL_SPATIAL_COVERAGE;
        if (e->scaled_vertex) lo *= f, hi *= f; /* c6aaf96f stretches the coordinate by the fraction */
        double t_lo = lo, t_hi = hi;
        if (e->tap == TEMPORAL_SPATIAL_COMPOSITE) t_lo = f * lo, t_hi = f * hi; /* divided by +794, which is 1 */
        /* the covered coordinate the fraction bounds: the one the scene colour is sampled at, before any clamp */
        const double covered_hi = e->tap == TEMPORAL_SPATIAL_RAW ? hi : t_hi;
        if (e->tap != TEMPORAL_SPATIAL_RAW) {
            const double clamp = d->query[a] ? f - 1.0 / d->query[a] : NAN; /* min(f - 1/Q, the coordinate) */
            if (!(clamp == clamp)) return TEMPORAL_SPATIAL_COVERAGE;
            if (t_hi > clamp) t_hi = clamp;
            if (t_lo > clamp) t_lo = clamp;
        }
        if (!(covered_hi <= f) || !(t_lo >= 0.0) || !(t_hi < 1.0)) return TEMPORAL_SPATIAL_COVERAGE;
    }
    return TEMPORAL_SPATIAL_TAKEN;
}

/* The mode's state across frames (D3-2 revision 3 and its tightening): off for the session at once after a failure
   to make what it needs, from the next frame after an unknown reader or a consumer not replaced (rule 5, the writes
   of research's ruling of 10:50), and after eight eligible frames in a row that were not published. Warm-up, gated
   and ineligible frames neither count nor reset the count. */
typedef struct {
    int off;        /* off for the session: only a new session starts clean */
    int off_next;   /* off from the next frame on */
    unsigned unpublished; /* eligible frames in a row not published */
    const char* why;      /* why it went off, for its one line */
} TemporalSpatialLatch;
#define TEMPORAL_SPATIAL_UNPUBLISHED_MOST 8
/* a thing the mode needs could not be made: off now */
static inline void temporal_spatial_make_failed(TemporalSpatialLatch* l, const char* why) {
    if (!l->off) l->why = why;
    l->off = 1;
}
/* an unknown reader or a consumer not replaced in this frame: off from the next */
static inline void temporal_spatial_unknown(TemporalSpatialLatch* l, const char* why) {
    if (!l->off && !l->off_next) l->why = why;
    l->off_next = 1;
}
/* at a frame's start: whether the mode is on for it (an off_next of the frame before takes effect here) */
static inline int temporal_spatial_frame(TemporalSpatialLatch* l) {
    if (l->off_next) l->off = 1;
    return !l->off;
}
/* a frame's outcome: eligible (the mode on, the fraction confirmed, the frame jittered for it) and whether its
   hand-over was published. An eligible frame not published counts, a published one clears the count. */
static inline void temporal_spatial_outcome(TemporalSpatialLatch* l, int eligible, int published) {
    if (!eligible || l->off) return;
    if (published) l->unpublished = 0;
    else if (++l->unpublished >= TEMPORAL_SPATIAL_UNPUBLISHED_MOST) {
        if (!l->off) l->why = "eight eligible frames in a row were not published";
        l->off = 1;
    }
}
#endif
