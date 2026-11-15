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
    if (mw % w || nh % h || mw / w > INT32_MAX || nh / h > INT32_MAX) return 0; /* whole, and an int (astra's O5) */
    *M = (int)(mw / w);
    *N = (int)(nh / h);
    return 1;
}

/* The scales the mode takes (D3 revision 2 rule 2, astra's O4): w / W and h / H each exactly 1/2, 3/5, 2/3, 3/4 or
   4/5, the same on both axes, by whole numbers. The scale's index, or -1 for any other, 1 included: the frame is then
   D2's, not eligible and not counted. */
static const int temporal_spatial_scales[5][2] = {{1, 2}, {3, 5}, {2, 3}, {3, 4}, {4, 5}};
static inline int temporal_spatial_scale_listed(int W, int H, int w, int h) {
    for (int k = 0; k < 5; k++) {
        const int64_t p = temporal_spatial_scales[k][0], q = temporal_spatial_scales[k][1];
        if ((int64_t)w * q == (int64_t)W * p && (int64_t)h * q == (int64_t)H * p) return k;
    }
    return -1;
}
/* the fraction a block must hold for scale k: the float nearest p / q (research 12:50), 0.6f for 3/5 */
static inline float temporal_spatial_scale_fraction(int k) {
    return (float)((double)temporal_spatial_scales[k][0] / temporal_spatial_scales[k][1]);
}
/* the frame's confirmed fractions, bit for bit the float of scale k: no tolerance (research 14:00) */
static inline int temporal_spatial_confirmed(float fx, float fy, int k) {
    const float f = temporal_spatial_scale_fraction(k);
    return memcmp(&fx, &f, sizeof f) == 0 && memcmp(&fy, &f, sizeof f) == 0;
}
/* whether images of w x h fit the device's limits for the format and the uses they are made with, and, as colour
   attachments, its framebuffer's (astra's O5) */
static inline int temporal_spatial_fits(uint32_t w, uint32_t h, uint32_t max_w, uint32_t max_h, uint32_t fb_w,
                                        uint32_t fb_h) {
    return w && h && w <= max_w && h <= max_h && w <= fb_w && h <= fb_h;
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

/* The scale (shaders/temporal_spatial.frag): Catmull-Rom's weights of the four taps at -1, 0, 1, 2 from the tap
   below the point, t its distance from it, and one channel of output pixel x, y of W x H from a picture of w x h
   (rows of w floats), the taps held to the picture, no value below 0 */
static inline void temporal_spatial_weights(double t, double out[4]) {
    const double t2 = t * t, t3 = t2 * t;
    out[0] = -0.5 * t3 + t2 - 0.5 * t;
    out[1] = 1.5 * t3 - 2.5 * t2 + 1.0;
    out[2] = -1.5 * t3 + 2.0 * t2 + 0.5 * t;
    out[3] = 0.5 * t3 - 0.5 * t2;
}
static inline double temporal_spatial_catmull_rom(const float* picture, int w, int h, int W, int H, int x, int y) {
    const double ax = (x + 0.5) * w / W - 0.5, ay = (y + 0.5) * h / H - 0.5;
    const double bx = floor(ax), by = floor(ay);
    double wx[4], wy[4], sum = 0.0;
    temporal_spatial_weights(ax - bx, wx);
    temporal_spatial_weights(ay - by, wy);
    for (int j = 0; j < 4; j++) {
        double row = 0.0;
        int ty = (int)by + j - 1;
        ty = ty < 0 ? 0 : ty > h - 1 ? h - 1 : ty;
        for (int i = 0; i < 4; i++) {
            int tx = (int)bx + i - 1;
            tx = tx < 0 ? 0 : tx > w - 1 ? w - 1 : tx;
            row += wx[i] * picture[(size_t)ty * w + tx];
        }
        sum += wy[j] * row;
    }
    return sum > 0.0 ? sum : 0.0;
}

/* The audited readers (D3-2 revision 4, the scene colour at handle 0x28 in all three as S-1 and S-2 measured, recon
   2026-11-15 16:30): fragment and vertex program by their whole hashes, the scene colour's handle offset in words,
   how the program's coordinate of that binding is made, none of them clamped, and whether the composite's +794 and
   +798 must be 1. */
enum {
    TEMPORAL_SPATIAL_RAW,      /* the vertex coordinate as it is (26fc2347): B46268AD */
    TEMPORAL_SPATIAL_SCALED,   /* the coordinate times f, c6aaf96f's: 21FBFBEC */
    TEMPORAL_SPATIAL_COMPOSITE /* f x the coordinate / +794 and +798: the composite */
};
typedef struct {
    uint64_t fs, vs;
    uint32_t handle; /* the binding's handle offset in words */
    int tap;         /* TEMPORAL_SPATIAL_* above */
    int scaled_vertex; /* the vertex program multiplies the coordinate by the fraction (c6aaf96f) */
} TemporalSpatialEntry;
#define TEMPORAL_SPATIAL_ENTRIES 3
static const TemporalSpatialEntry temporal_spatial_entries[TEMPORAL_SPATIAL_ENTRIES] = {
    {0x21fbfbecbf90f4a3ull, 0xc6aaf96f6c29a40eull, 0x28, TEMPORAL_SPATIAL_SCALED, 1},
    {0xb46268adda62e218ull, 0x26fc2347977486d7ull, 0x28, TEMPORAL_SPATIAL_RAW, 0},
    {0x8a160a27d90af72dull, 0x26fc2347977486d7ull, 0x28, TEMPORAL_SPATIAL_COMPOSITE, 0}};
/* the entry of a fragment program and binding, or -1 */
static inline int temporal_spatial_entry(uint64_t fs, uint32_t handle) {
    for (int k = 0; k < TEMPORAL_SPATIAL_ENTRIES; k++)
        if (temporal_spatial_entries[k].fs == fs && temporal_spatial_entries[k].handle == handle) return k;
    return -1;
}

/* What a draw that binds the scene colour is, as the renderer sees it (D3-2 and its revisions 2 and 3) */
enum { TEMPORAL_SPATIAL_EDGE, TEMPORAL_SPATIAL_BORDER, TEMPORAL_SPATIAL_OTHER_ADDRESS };
typedef struct TemporalSpatialDraw {
    uint64_t vs;              /* the vertex program's whole hash */
    int direct;               /* no index buffer, no count read from memory */
    int triangle_list;        /* the topology */
    uint32_t first_vertex, vertex_count, instance_count, first_instance;
    int other_stages;         /* a tessellation or geometry program bound */
    int offset_size;          /* the translation reads the slot's size for texel offsets, which no key rewrites */
    /* each value with whether it is available: read by the program, bound, long enough, readable, and neither
       pending nor GPU-owned (research 15:00). A value not available is never compared. */
    double vertex_f[2];       /* the fraction the vertex stage's scene block holds */
    double vertex_origin[2];  /* the vertex stage's bank 1 +0 and +4, where c6aaf96f's position starts */
    double fragment_f[2];     /* the fragment stage's */
    double divisor[2];        /* the fragment stage's +794 and +798 */
    int vertex_f_ok, vertex_origin_ok, fragment_f_ok, divisor_ok;
    double viewport[4];       /* x, y, width, height, the height signed */
    int scissor_on;
    int scissor[4];           /* x0, y0, x1, y1 */
    int target[2];            /* the target's width and height */
    uint32_t query[2];        /* what the size query of the binding answers (today's, temporal_txq_today) */
    /* the binding's sampler and view */
    int normalised, mag_nearest, min_nearest, one_level, anisotropy, compare;
    int filters_same;         /* minification and magnification alike: the larger texture is read at about one texel
                                 a pixel, where the level of detail chooses between them */
    int address[2];           /* TEMPORAL_SPATIAL_EDGE, _BORDER or _OTHER_ADDRESS for u and v */
    /* view_mapping_same: the replacement's view is made with exactly the game view's component mapping (research
       19:40, astra's O2: B10G11R11 has no alpha, which the game's view may map to one) */
    int view_format_same, view_2d, view_one_level, view_one_layer, view_mapping_same;
} TemporalSpatialDraw;
enum {
    TEMPORAL_SPATIAL_TAKEN,
    TEMPORAL_SPATIAL_NOT_LISTED,   /* the program and binding are not an entry */
    TEMPORAL_SPATIAL_VERTEX,       /* another vertex program */
    TEMPORAL_SPATIAL_UNAVAILABLE,  /* a value the entry's programs read is not available */
    TEMPORAL_SPATIAL_FORM,         /* not a direct, non-indexed triangle list of one instance from vertex 0, 3 long */
    TEMPORAL_SPATIAL_STAGES,       /* a tessellation or geometry program */
    TEMPORAL_SPATIAL_SIZE_READ,    /* the translation reads the slot's size for its offsets (research 17:35) */
    TEMPORAL_SPATIAL_FRACTION,     /* a fraction the programs read is not the replacement's */
    TEMPORAL_SPATIAL_ORIGIN,       /* c6aaf96f's position origin (bank 1 +0, +4) is not the measured -1, +1 */
    TEMPORAL_SPATIAL_DIVISOR,      /* the composite's +794 or +798 not 1 */
    TEMPORAL_SPATIAL_SAMPLER,      /* not normalised, more than one level, anisotropy, compare, or an address mode */
    TEMPORAL_SPATIAL_VIEW_FORMAT,  /* the view's format is not the scene colour's own */
    TEMPORAL_SPATIAL_VIEW_TYPE,    /* the view is not 2D */
    TEMPORAL_SPATIAL_VIEW_LEVELS,  /* the view is not one level */
    TEMPORAL_SPATIAL_VIEW_LAYERS,  /* the view is not one layer */
    TEMPORAL_SPATIAL_VIEW_MAPPING, /* the replacement's view cannot have the game view's mapping */
    TEMPORAL_SPATIAL_COVERAGE,     /* a covered coordinate beyond the fraction, or a tap outside 0 to below 1 */
    TEMPORAL_SPATIAL_REASONS
};
static inline const char* temporal_spatial_text(int why) {
    static const char* const text[TEMPORAL_SPATIAL_REASONS] = {
        "taken",
        "the program and binding are not on the list",
        "its vertex program is not the audited one",
        "a value its programs read is missing, short, unreadable, pending or held on the GPU",
        "it is not a direct, non-indexed triangle list of one instance from vertex 0 and 3 vertices",
        "a tessellation or geometry program is bound",
        "its translation reads the slot's size for texel offsets, which no key rewrites",
        "a fraction its programs read is not the replacement's",
        "its vertex program's position origin is not the measured -1, +1",
        "the composite's divisors are not 1",
        "its sampler is not the audited form",
        "its view's format is not the scene colour's own",
        "its view is not 2D",
        "its view is not one level",
        "its view is not one layer",
        "the replacement's view does not have the game view's component mapping",
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

/* Whether entry k may be given the larger texture at this draw, for the replacement's fractions fs (each axis's): the
   first reason against it, or TEMPORAL_SPATIAL_TAKEN. Every covered coordinate must be at most the fraction and
   every tap at least 0 and below 1 on both axes, from the audited mapping and the entry's tap (recon 2026-11-15
   01:40). */
static inline int temporal_spatial_check(int k, const TemporalSpatialDraw* d, const double fs[2]) {
    if (k < 0 || k >= TEMPORAL_SPATIAL_ENTRIES) return TEMPORAL_SPATIAL_NOT_LISTED;
    const TemporalSpatialEntry* e = &temporal_spatial_entries[k];
    if (d->vs != e->vs) return TEMPORAL_SPATIAL_VERTEX;
    /* the values the entry's programs read, before any is compared: the fragment fraction for the clamp and the
       composite, c6aaf96f's fraction and origin, the composite's divisors. 26fc2347 reads none (research 15:00) */
    if ((e->tap != TEMPORAL_SPATIAL_RAW && !d->fragment_f_ok) ||
        (e->scaled_vertex && (!d->vertex_f_ok || !d->vertex_origin_ok)) ||
        (e->tap == TEMPORAL_SPATIAL_COMPOSITE && !d->divisor_ok))
        return TEMPORAL_SPATIAL_UNAVAILABLE;
    if (!d->direct || !d->triangle_list || d->first_vertex != 0 || d->vertex_count != 3 || d->instance_count != 1 ||
        d->first_instance != 0)
        return TEMPORAL_SPATIAL_FORM;
    if (d->other_stages) return TEMPORAL_SPATIAL_STAGES;
    if (d->offset_size) return TEMPORAL_SPATIAL_SIZE_READ;
    /* c6aaf96f positions from bank 1 +0, +4, measured -1 and +1 in P1 and P2: the audited mapping holds only there */
    if (e->scaled_vertex && !(d->vertex_origin[0] == -1.0 && d->vertex_origin[1] == 1.0))
        return TEMPORAL_SPATIAL_ORIGIN;
    for (int a = 0; a < 2; a++)
        if (e->tap == TEMPORAL_SPATIAL_COMPOSITE && !(d->divisor[a] == 1.0)) return TEMPORAL_SPATIAL_DIVISOR;
    if (!d->normalised || !d->one_level || d->anisotropy || d->compare || !d->filters_same)
        return TEMPORAL_SPATIAL_SAMPLER;
    for (int a = 0; a < 2; a++)
        if (!(d->address[a] == TEMPORAL_SPATIAL_EDGE ||
              (d->address[a] == TEMPORAL_SPATIAL_BORDER && d->mag_nearest && d->min_nearest)))
            return TEMPORAL_SPATIAL_SAMPLER;
    if (!d->view_format_same) return TEMPORAL_SPATIAL_VIEW_FORMAT;
    if (!d->view_2d) return TEMPORAL_SPATIAL_VIEW_TYPE;
    if (!d->view_one_level) return TEMPORAL_SPATIAL_VIEW_LEVELS;
    if (!d->view_one_layer) return TEMPORAL_SPATIAL_VIEW_LAYERS;
    if (!d->view_mapping_same) return TEMPORAL_SPATIAL_VIEW_MAPPING;
    /* every condition above is of class U (the audited reader), the two below of class N (this frame against the
       replacement): checked after all of class U, so a class U failure is the one reported (research 14:35) */
    for (int a = 0; a < 2; a++) {
        /* the fraction where a program of the entry reads it: the clamp's and the composite's fragment stage,
           c6aaf96f's vertex stage. B46268AD reads none for its coordinate. */
        if (e->tap != TEMPORAL_SPATIAL_RAW && !(d->fragment_f[a] == fs[a])) return TEMPORAL_SPATIAL_FRACTION;
        if (e->scaled_vertex && !(d->vertex_f[a] == fs[a])) return TEMPORAL_SPATIAL_FRACTION;
    }
    for (int a = 0; a < 2; a++) {
        const double f = fs[a];
        double lo, hi;
        if (!temporal_spatial_covered(d, a, &lo, &hi)) return TEMPORAL_SPATIAL_COVERAGE;
        if (e->scaled_vertex) lo *= f, hi *= f; /* c6aaf96f stretches the coordinate by the fraction */
        if (e->tap == TEMPORAL_SPATIAL_COMPOSITE) lo *= f, hi *= f; /* divided by +794 and +798, which are 1 */
        /* the scene colour's tap, unclamped in all three (revision 4): at least 0, at most the fraction, below 1 */
        if (!(hi <= f) || !(lo >= 0.0) || !(hi < 1.0)) return TEMPORAL_SPATIAL_COVERAGE;
    }
    return TEMPORAL_SPATIAL_TAKEN;
}

/* Whether a surface bound at guest address at, bytes long, lies over the published scene colour's memory: the two
   ranges share a byte. A parked surface's address carries bit 63 (surface.c), which is not part of the range. */
static inline int temporal_spatial_overlaps(uint64_t at, uint64_t bytes, uint64_t scene, uint64_t scene_bytes) {
    const uint64_t low = ~(1ull << 63);
    at &= low;
    scene &= low;
    return bytes && scene_bytes && at < scene + scene_bytes && scene < at + bytes;
}

/* For the log alone (research 15:40): every condition of entry k that fails at this draw, as a mask of reasons (bit
   r for reason r), not only the first. Each group of conditions is asked of the check alone, the others made to
   hold and the draw's coverage kept, which the check asks last. A value that is not available is named by its
   availability alone: its number is never compared (reviewer 04:42). */
static inline uint32_t temporal_spatial_failed(int k, const TemporalSpatialDraw* d, const double fs[2]) {
    if (k < 0 || k >= TEMPORAL_SPATIAL_ENTRIES) return 1u << TEMPORAL_SPATIAL_NOT_LISTED;
    TemporalSpatialDraw pass = *d; /* every group but the coverage made to hold */
    pass.vs = temporal_spatial_entries[k].vs;
    pass.vertex_f_ok = pass.vertex_origin_ok = pass.fragment_f_ok = pass.divisor_ok = 1;
    pass.direct = pass.triangle_list = 1;
    pass.first_vertex = pass.first_instance = 0;
    pass.vertex_count = 3;
    pass.instance_count = 1;
    pass.other_stages = pass.offset_size = 0;
    pass.vertex_origin[0] = -1.0, pass.vertex_origin[1] = 1.0;
    pass.divisor[0] = pass.divisor[1] = 1.0;
    pass.normalised = pass.one_level = pass.filters_same = 1;
    pass.anisotropy = pass.compare = 0;
    pass.address[0] = pass.address[1] = TEMPORAL_SPATIAL_EDGE;
    pass.view_format_same = pass.view_2d = pass.view_one_level = pass.view_one_layer = pass.view_mapping_same = 1;
    for (int a = 0; a < 2; a++) pass.vertex_f[a] = pass.fragment_f[a] = fs[a];
    uint32_t mask = 0;
    for (int group = 0; group < 15; group++) {
        TemporalSpatialDraw x = pass;
        int why = TEMPORAL_SPATIAL_COVERAGE;
        switch (group) {
            case 0: x.vs = d->vs, why = TEMPORAL_SPATIAL_VERTEX; break;
            case 1:
                x.vertex_f_ok = d->vertex_f_ok, x.vertex_origin_ok = d->vertex_origin_ok;
                x.fragment_f_ok = d->fragment_f_ok, x.divisor_ok = d->divisor_ok;
                why = TEMPORAL_SPATIAL_UNAVAILABLE;
                break;
            case 2:
                x.direct = d->direct, x.triangle_list = d->triangle_list, x.first_vertex = d->first_vertex;
                x.vertex_count = d->vertex_count, x.instance_count = d->instance_count;
                x.first_instance = d->first_instance, why = TEMPORAL_SPATIAL_FORM;
                break;
            case 3: x.other_stages = d->other_stages, why = TEMPORAL_SPATIAL_STAGES; break;
            case 4:
                if (d->vertex_origin_ok) memcpy(x.vertex_origin, d->vertex_origin, sizeof x.vertex_origin);
                why = TEMPORAL_SPATIAL_ORIGIN;
                break;
            case 5:
                if (d->divisor_ok) memcpy(x.divisor, d->divisor, sizeof x.divisor);
                why = TEMPORAL_SPATIAL_DIVISOR;
                break;
            case 6:
                x.normalised = d->normalised, x.one_level = d->one_level, x.anisotropy = d->anisotropy;
                x.compare = d->compare, x.filters_same = d->filters_same, x.mag_nearest = d->mag_nearest;
                x.min_nearest = d->min_nearest, x.address[0] = d->address[0], x.address[1] = d->address[1];
                why = TEMPORAL_SPATIAL_SAMPLER;
                break;
            /* the view's five tests, each its own (research 19:40) */
            case 7: x.view_format_same = d->view_format_same, why = TEMPORAL_SPATIAL_VIEW_FORMAT; break;
            case 8: x.view_2d = d->view_2d, why = TEMPORAL_SPATIAL_VIEW_TYPE; break;
            case 9: x.view_one_level = d->view_one_level, why = TEMPORAL_SPATIAL_VIEW_LEVELS; break;
            case 10: x.view_one_layer = d->view_one_layer, why = TEMPORAL_SPATIAL_VIEW_LAYERS; break;
            case 11: x.view_mapping_same = d->view_mapping_same, why = TEMPORAL_SPATIAL_VIEW_MAPPING; break;
            case 12:
                if (d->vertex_f_ok) memcpy(x.vertex_f, d->vertex_f, sizeof x.vertex_f);
                if (d->fragment_f_ok) memcpy(x.fragment_f, d->fragment_f, sizeof x.fragment_f);
                why = TEMPORAL_SPATIAL_FRACTION;
                break;
            case 13: x.offset_size = d->offset_size, why = TEMPORAL_SPATIAL_SIZE_READ; break;
            default: break; /* the coverage, the draw's own */
        }
        if (temporal_spatial_check(k, &x, fs) == why) mask |= 1u << why;
    }
    return mask;
}
/* A binding of entry k at a draw, classified (UPSCALER-DESIGN.md d08b624c, research 14:35, 14:45 and 15:00): class
   N is only the fraction and the tap and coverage bounds of available values, the frame against the replacement: k,
   with the verdict. Every other failure, a required value not available included, is class U whatever fails with
   it: -1, a reader not on the list, *verdict its reason. Taken: k, TEMPORAL_SPATIAL_TAKEN. */
static inline int temporal_spatial_class_n(int verdict) {
    return verdict == TEMPORAL_SPATIAL_TAKEN || verdict == TEMPORAL_SPATIAL_FRACTION ||
           verdict == TEMPORAL_SPATIAL_COVERAGE;
}
static inline int temporal_spatial_classify(int k, const TemporalSpatialDraw* d, const double fs[2], int* verdict) {
    *verdict = TEMPORAL_SPATIAL_NOT_LISTED;
    if (k < 0 || k >= TEMPORAL_SPATIAL_ENTRIES) return -1;
    *verdict = temporal_spatial_check(k, d, fs);
    return temporal_spatial_class_n(*verdict) ? k : -1;
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
/* the first reason the mode goes off is the one its line gives */
static inline void temporal_spatial_reason(TemporalSpatialLatch* l, const char* why) {
    if (!l->off && !l->off_next) l->why = why;
}
/* a thing the mode needs could not be made: off now */
static inline void temporal_spatial_make_failed(TemporalSpatialLatch* l, const char* why) {
    temporal_spatial_reason(l, why);
    l->off = 1;
}
/* an unknown reader or a consumer not replaced in this frame: off from the next */
static inline void temporal_spatial_unknown(TemporalSpatialLatch* l, const char* why) {
    temporal_spatial_reason(l, why);
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
        temporal_spatial_reason(l, "eight eligible frames in a row were not published");
        l->off = 1;
    }
}
/* One frame's hand-over, draw by draw (research's ruling of 11:10). The replacement is recorded at the hook; the
   frame's first listed reader decides whether it is published: taken, it is, and so are the later readers that are
   taken; refused, the frame is not published at all and every reader gets the scene as drawn (an eligible frame not
   published, counted). A later listed reader refused in a published frame, or any reader not on the list in a frame
   whose replacement was recorded, read the scene as drawn: an unknown reader, the mode off from the next frame. */
enum { TEMPORAL_SPATIAL_AS_DRAWN, TEMPORAL_SPATIAL_LARGER };
typedef struct {
    int recorded;  /* the replacement was recorded at the hook in this frame */
    int decided;   /* the first listed reader was met */
    int published; /* it was taken */
} TemporalSpatialFrame;
/* What a draw that samples the scene colour is given: entry k (-1 not listed) with temporal_spatial_check's answer */
static inline int temporal_spatial_draw(TemporalSpatialFrame* fr, TemporalSpatialLatch* l, int k, int verdict) {
    if (!fr->recorded) return TEMPORAL_SPATIAL_AS_DRAWN;
    const int taken = k >= 0 && verdict == TEMPORAL_SPATIAL_TAKEN;
    if (k >= 0 && !fr->decided) { /* the frame's first listed reader */
        fr->decided = 1;
        fr->published = taken;
        return taken ? TEMPORAL_SPATIAL_LARGER : TEMPORAL_SPATIAL_AS_DRAWN;
    }
    if (taken && fr->published) return TEMPORAL_SPATIAL_LARGER;
    if (k < 0 || fr->published) temporal_spatial_unknown(l, k < 0 ? "a reader not on the list" : "a listed reader "
                                                                    "refused in a published frame");
    return TEMPORAL_SPATIAL_AS_DRAWN;
}
/* A use of the scene colour's Surf while the watch is armed: from the end of the hook of a frame whose replacement
   was recorded to its present (research 11:25). given is what the binding being made was given, when the use is a
   draw's binding of it (-1 otherwise: a draw into it, a copy, a write-back, a reload, its end). A listed reader of
   an unpublished frame binds the scene as drawn: expected. A binding given the larger texture never reaches the
   Surf: such a use is a fault of ours, TEMPORAL_SPATIAL_FAULT, and the caller turns the mode off at once. Any other
   use is a consumer not replaced: off from the next frame. */
enum { TEMPORAL_SPATIAL_UNARMED, TEMPORAL_SPATIAL_EXPECTED, TEMPORAL_SPATIAL_CONSUMER, TEMPORAL_SPATIAL_FAULT };
static inline int temporal_spatial_use(const TemporalSpatialFrame* fr, TemporalSpatialLatch* l, int listed, int given,
                                       const char* what) {
    if (!fr->recorded) return TEMPORAL_SPATIAL_UNARMED;
    if (given == TEMPORAL_SPATIAL_LARGER) {
        temporal_spatial_make_failed(l, "a binding given the larger texture used the scene colour (a fault)");
        return TEMPORAL_SPATIAL_FAULT;
    }
    if (listed && given == TEMPORAL_SPATIAL_AS_DRAWN && fr->decided && !fr->published) return TEMPORAL_SPATIAL_EXPECTED;
    temporal_spatial_unknown(l, what);
    return TEMPORAL_SPATIAL_CONSUMER;
}
/* What a draw is finally drawn with, decided once before its textures are bound (astra's O2): the slots given the
   larger texture with the keyed variant, or, when the variant could not be made or the mode went off since the
   decision, none of them, the scene with the unkeyed program. The draw is never dropped for it. fault: 1 when the
   mode must go off for it. */
static inline uint64_t temporal_spatial_final(uint64_t larger, int variant_made, int mode_on, int* fault) {
    *fault = larger && (!variant_made || !mode_on);
    return *fault ? 0 : larger;
}
/* the frame's end, for the latch: eligible when its replacement was recorded */
static inline void temporal_spatial_frame_end(const TemporalSpatialFrame* fr, TemporalSpatialLatch* l) {
    temporal_spatial_outcome(l, fr->recorded, fr->published);
}
#endif
