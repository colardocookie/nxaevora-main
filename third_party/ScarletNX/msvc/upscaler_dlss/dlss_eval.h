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

// What a context may be made with (DL1b revision 2 and research's rulings of 08:40 and 08:45), before NGX is asked:
// the interface's flags (upscaler.h, mirrored here and held to it in the module), linear HDR colour, unjittered
// vectors and NGX's own exposure required. Vectors in R16G16_SFLOAT or R32G32_SFLOAT (Vulkan's numbers). DLAA only.
const uint32_t kLinearHdr = 1u << 0, kDepthReversed = 1u << 1, kUnjittered = 1u << 2, kExposureAutomatic = 1u << 3,
               kFarInfinite = 1u << 4;
const int kMotionR16G16Float = 83, kMotionR32G32Float = 103;
enum { CREATE_OK, CREATE_FLAG_UNKNOWN, CREATE_FLAG_MISSING, CREATE_MOTION_FORMAT, CREATE_NOT_NATIVE };
inline const char* create_text(int why) {
    switch (why) {
        case CREATE_OK: return "ok";
        case CREATE_FLAG_UNKNOWN: return "a flag this module does not know";
        case CREATE_FLAG_MISSING:
            return "linear HDR colour, unjittered vectors and the library's own exposure are required";
        case CREATE_MOTION_FORMAT: return "the vectors' format is not R16G16_SFLOAT or R32G32_SFLOAT";
        default:
            return "only DLAA at the native size: the render size is not the output's or is larger than the images";
    }
}
inline int create_check(uint32_t flags, int motion_format, uint32_t render_w, uint32_t render_h, uint32_t image_w,
                        uint32_t image_h, uint32_t output_w, uint32_t output_h) {
    const uint32_t known = kLinearHdr | kDepthReversed | kUnjittered | kExposureAutomatic | kFarInfinite;
    const uint32_t required = kLinearHdr | kUnjittered | kExposureAutomatic;
    if (flags & ~known) return CREATE_FLAG_UNKNOWN;
    if ((flags & required) != required) return CREATE_FLAG_MISSING;
    if (motion_format != kMotionR16G16Float && motion_format != kMotionR32G32Float) return CREATE_MOTION_FORMAT;
    if (render_w != output_w || render_h != output_h || render_w > image_w || render_h > image_h || !render_w ||
        !render_h)
        return CREATE_NOT_NATIVE;
    return CREATE_OK;
}
// A frame against what its context was made with (research's ruling of 09:10), before anything is recorded: the
// render size, each image's size and format, and every image and view handed. Images in the order colour, depth,
// motion, output, the first three of the images' size, the output of the output's.
struct Made {
    uint32_t render_w, render_h, image_w, image_h, output_w, output_h;
    int format[4];
};
struct Handed {
    uint64_t image, view; // only whether they are 0
    int format;
    uint32_t w, h;
};
enum { FRAME_OK, FRAME_RENDER, FRAME_HANDLE, FRAME_SIZE, FRAME_FORMAT };
inline const char* frame_text(int why) {
    switch (why) {
        case FRAME_OK: return "ok";
        case FRAME_RENDER: return "its render size is not the context's";
        case FRAME_HANDLE: return "an image or a view is not handed";
        case FRAME_SIZE: return "an image's size is not the context's";
        default: return "an image's format is not the context's";
    }
}
// FRAME_OK, or the first mismatch with the image's number (0 to 3) in *which
inline int frame_check(const Made& made, uint32_t render_w, uint32_t render_h, const Handed image[4], int* which) {
    *which = -1;
    if (render_w != made.render_w || render_h != made.render_h) return FRAME_RENDER;
    for (int k = 0; k < 4; k++) {
        *which = k;
        const uint32_t w = k < 3 ? made.image_w : made.output_w, h = k < 3 ? made.image_h : made.output_h;
        if (!image[k].image || !image[k].view) return FRAME_HANDLE;
        if (image[k].w != w || image[k].h != h) return FRAME_SIZE;
        if (image[k].format != made.format[k]) return FRAME_FORMAT;
    }
    *which = -1;
    return FRAME_OK;
}

// NGX's feature flags for flags create_check took (NGX's numbers, mirrored and held in the module): vectors at the
// render size, HDR, the library's exposure, and the depth inverted when it is reversed. MVJittered never.
const int kNgxIsHdr = 1 << 0, kNgxMvLowRes = 1 << 1, kNgxMvJittered = 1 << 2, kNgxDepthInverted = 1 << 3,
          kNgxAutoExposure = 1 << 6;
inline int ngx_flags(uint32_t flags) {
    return kNgxMvLowRes | kNgxIsHdr | kNgxAutoExposure | (flags & kDepthReversed ? kNgxDepthInverted : 0);
}
} // namespace snx_dlss
#endif
