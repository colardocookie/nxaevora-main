// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// The numbers DLSS's module hands NGX for one frame (UPSCALER-DESIGN.md, DL1b point 6), from the interface's frame
// (upscaler.h): pure, no NGX type, so that both compilers test it. The module copies them into NGX's parameters.
#ifndef SNX_DLSS_EVAL_H
#define SNX_DLSS_EVAL_H
#include <math.h>
#include <stdint.h>

namespace snx_dlss {
// what NGX is given (the names of nvsdk_ngx_helpers_vk.h's evaluation parameters)
struct Eval {
    float mv_scale[2];         // InMVScaleX, Y
    float jitter[2];           // InJitterOffsetX, Y
    uint32_t render_w, render_h; // InRenderSubrectDimensions
    uint32_t base_x, base_y;   // every Subrect base
    int reset;                 // InReset
    float frame_ms;            // InFrameTimeDeltaInMsec
    float pre_exposure;        // InPreExposure
    float exposure_scale;      // InExposureScale
};
enum {
    EVAL_OK,
    EVAL_NOT_FINITE,  // a number of the frame is not finite
    EVAL_SCALE_ZERO,  // a vector scale of 0, which NGX says must never be
    EVAL_JITTER,      // a jitter outside -0.5 to 0.5, NGX's stated range
    EVAL_RECTANGLE,   // a rectangle not at the origin (this step's traits), empty, or beyond the colour image
    EVAL_SECONDS,     // a frame time below 0
    EVAL_SIGN,        // a sign other than 1 or -1
    EVAL_DERIVED      // a number made from the frame's is not finite (a frame time too long in milliseconds)
};
inline const char* eval_text(int why) {
    switch (why) {
        case EVAL_OK: return "ok";
        case EVAL_NOT_FINITE: return "a number of the frame is not finite";
        case EVAL_SCALE_ZERO: return "a vector scale is 0";
        case EVAL_JITTER: return "the jitter is outside -0.5 to 0.5";
        case EVAL_RECTANGLE: return "the rectangle is not at the origin, is empty, or is larger than the colour image";
        case EVAL_SECONDS: return "the frame time is below 0";
        case EVAL_SIGN: return "a jitter sign is not 1 or -1";
        default: return "a number made from the frame's is not finite";
    }
}
// The vectors' scale as the frame has it (NGX's convention is the same: from the current frame to the previous one,
// in render pixels, the origin at the upper left). The jitter times the case's signs, as its sign against NGX's is
// not documented. Exposure as 1, NGX computes it (AutoExposure). Rectangle bases 0. Made apart, held finite, and
// written to *out only when every number is taken: a refusal leaves *out as it was.
inline int eval_numbers(const float vector_scale[2], const float jitter[2], const int sign[2], int32_t render_x,
                        int32_t render_y, uint32_t render_w, uint32_t render_h, uint32_t image_w, uint32_t image_h,
                        uint32_t reset, float frame_seconds, Eval* out) {
    for (int k = 0; k < 2; k++) {
        if (!isfinite(vector_scale[k]) || !isfinite(jitter[k])) return EVAL_NOT_FINITE;
        if (sign[k] != 1 && sign[k] != -1) return EVAL_SIGN;
    }
    if (!isfinite(frame_seconds)) return EVAL_NOT_FINITE;
    if (vector_scale[0] == 0.0f || vector_scale[1] == 0.0f) return EVAL_SCALE_ZERO;
    if (fabsf(jitter[0]) > 0.5f || fabsf(jitter[1]) > 0.5f) return EVAL_JITTER;
    if (render_x || render_y || !render_w || !render_h || render_w > image_w || render_h > image_h)
        return EVAL_RECTANGLE;
    if (frame_seconds < 0.0f) return EVAL_SECONDS;
    Eval e = {};
    for (int k = 0; k < 2; k++) {
        e.mv_scale[k] = vector_scale[k];
        e.jitter[k] = jitter[k] * (float)sign[k];
    }
    e.render_w = render_w;
    e.render_h = render_h;
    e.reset = reset ? 1 : 0;
    e.frame_ms = frame_seconds * 1000.0f;
    e.pre_exposure = 1.0f;
    e.exposure_scale = 1.0f;
    for (int k = 0; k < 2; k++)
        if (!isfinite(e.mv_scale[k]) || !isfinite(e.jitter[k])) return EVAL_DERIVED;
    if (!isfinite(e.frame_ms)) return EVAL_DERIVED;
    *out = e;
    return EVAL_OK;
}
} // namespace snx_dlss
#endif
