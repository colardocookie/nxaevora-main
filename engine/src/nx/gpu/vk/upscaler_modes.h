/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_VK_UPSCALER_MODES_H
#define SNX_VK_UPSCALER_MODES_H
/* A library's quality modes as it reports them at an output size (optimal, least and largest render size), and the
   one for a render size: D3-4's rule (msvc/upscaler_dlss/dlss_eval.h mode_choose) for the XeSS adapter, in C. Pure. */
#include <stdint.h>

typedef struct {
    int got; /* the library answered with a render size */
    uint32_t w, h, min_w, min_h, max_w, max_h;
} SnxMode;
static inline int snx_mode_holds(const SnxMode* m, uint32_t w, uint32_t h) {
    return m->got && w >= m->min_w && w <= m->max_w && h >= m->min_h && h <= m->max_h;
}
/* Modes in rising quality, native the index of the one for the output's own size. At the output's size that mode
   when its range holds it. Below it a mode whose optimal is the render size, else the nearest optimal (width and
   height apart, added) among the modes whose range holds it, ties to the higher quality. -1 for none. */
static inline int snx_mode_choose(const SnxMode* modes, int n, int native, uint32_t render_w, uint32_t render_h,
                                  uint32_t output_w, uint32_t output_h) {
    if (render_w == output_w && render_h == output_h)
        return native >= 0 && native < n && snx_mode_holds(&modes[native], render_w, render_h) ? native : -1;
    for (int k = n - 1; k >= 0; k--)
        if (modes[k].got && modes[k].w == render_w && modes[k].h == render_h) return k;
    int best = -1;
    uint64_t nearest = 0;
    for (int k = n - 1; k >= 0; k--) {
        if (!snx_mode_holds(&modes[k], render_w, render_h)) continue;
        const uint64_t apart = (uint64_t)(modes[k].w > render_w ? modes[k].w - render_w : render_w - modes[k].w) +
                               (uint64_t)(modes[k].h > render_h ? modes[k].h - render_h : render_h - modes[k].h);
        if (best < 0 || apart < nearest) best = k, nearest = apart;
    }
    return best;
}
#endif
