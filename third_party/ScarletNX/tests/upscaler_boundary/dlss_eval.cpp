// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// The numbers DLSS's module hands NGX (msvc/upscaler_dlss/dlss_eval.h): passed through, the jitter by the case's
// signs, and every number NGX's documents bound refused outside it, with nothing written. Run by dlss_core.cpp.
#undef NDEBUG /* the checks are asserts, in a Release build too */
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "dlss_eval.h"

int dlss_eval_cases() {
    // NGX's configuration lines as DL-S2 logged them, and lines that only look alike
    assert(snx_dlss::ngx_config_line("[2026-11-14 21:42:06] [NGXLoadConfig:1145] [dlss]"));
    assert(snx_dlss::ngx_config_line("[2026-11-14 21:42:06] [NGXLoadConfig:1151] app_E658702=0.0.0"));
    assert(!snx_dlss::ngx_config_line("[2026-11-14 21:42:06] [NGXLoadFeature:88] loading nvngx_dlss.dll"));
    assert(!snx_dlss::ngx_config_line("NGXLoadConfig without its bracket") && !snx_dlss::ngx_config_line(nullptr));
    assert(!snx_dlss::ngx_config_line(""));
    using namespace snx_dlss;
    const float scale[2] = {-1280.0f, -720.0f}, jitter[2] = {0.25f, -0.125f};
    const int plus[2] = {1, 1};
    Eval e;
    // as the frame has them: the scale as it is, the jitter by the signs, the time in milliseconds, exposure 1
    assert(eval_numbers(scale, jitter, plus, 0, 0, 1280, 720, 1280, 720, 5, 1.0f / 60.0f, &e) == EVAL_OK);
    assert(e.mv_scale[0] == -1280.0f && e.mv_scale[1] == -720.0f && e.jitter[0] == 0.25f && e.jitter[1] == -0.125f);
    assert(e.render_w == 1280 && e.render_h == 720 && !e.base_x && !e.base_y && e.reset == 1);
    assert(fabsf(e.frame_ms - 1000.0f / 60.0f) < 1e-4f && e.pre_exposure == 1.0f && e.exposure_scale == 1.0f);
    assert(eval_numbers(scale, jitter, plus, 0, 0, 1280, 720, 1280, 720, 0, 0.0f, &e) == EVAL_OK && e.reset == 0);
    // the four sign variants of the jitter case
    static const int signs[4][2] = {{1, 1}, {-1, 1}, {1, -1}, {-1, -1}};
    for (int v = 0; v < 4; v++) {
        assert(eval_numbers(scale, jitter, signs[v], 0, 0, 64, 64, 64, 64, 0, 0.0f, &e) == EVAL_OK);
        assert(e.jitter[0] == jitter[0] * (float)signs[v][0] && e.jitter[1] == jitter[1] * (float)signs[v][1]);
        assert(e.mv_scale[0] == scale[0] && e.mv_scale[1] == scale[1]); /* the vectors never take the signs */
    }
    // refused, and nothing written
    struct Case {
        float scale[2], jitter[2];
        int sign[2];
        int32_t x, y;
        uint32_t w, h;
        float seconds;
        int why;
    };
    const float nan = nanf(""), inf = INFINITY;
    const Case cases[] = {{{nan, -720}, {0, 0}, {1, 1}, 0, 0, 64, 64, 0, EVAL_NOT_FINITE},
                          {{-1280, -720}, {0, inf}, {1, 1}, 0, 0, 64, 64, 0, EVAL_NOT_FINITE},
                          {{-1280, -720}, {0, 0}, {1, 1}, 0, 0, 64, 64, nan, EVAL_NOT_FINITE},
                          {{0.0f, -720}, {0, 0}, {1, 1}, 0, 0, 64, 64, 0, EVAL_SCALE_ZERO},
                          {{-1280, -0.0f}, {0, 0}, {1, 1}, 0, 0, 64, 64, 0, EVAL_SCALE_ZERO},
                          {{-1280, -720}, {0.5000001f, 0}, {1, 1}, 0, 0, 64, 64, 0, EVAL_JITTER},
                          {{-1280, -720}, {0, -0.51f}, {1, 1}, 0, 0, 64, 64, 0, EVAL_JITTER},
                          {{-1280, -720}, {0, 0}, {1, 1}, 1, 0, 64, 64, 0, EVAL_RECTANGLE},
                          {{-1280, -720}, {0, 0}, {1, 1}, 0, -1, 64, 64, 0, EVAL_RECTANGLE},
                          {{-1280, -720}, {0, 0}, {1, 1}, 0, 0, 0, 64, 0, EVAL_RECTANGLE},
                          {{-1280, -720}, {0, 0}, {1, 1}, 0, 0, 65, 64, 0, EVAL_RECTANGLE},
                          {{-1280, -720}, {0, 0}, {1, 1}, 0, 0, 64, 65, 0, EVAL_RECTANGLE},
                          {{-1280, -720}, {0, 0}, {1, 1}, 0, 0, 64, 64, -0.001f, EVAL_SECONDS},
                          {{-1280, -720}, {0, 0}, {0, 1}, 0, 0, 64, 64, 0, EVAL_SIGN},
                          {{-1280, -720}, {0, 0}, {1, 2}, 0, 0, 64, 64, 0, EVAL_SIGN},
                          {{-1280, -720}, {0, 0}, {1, 1}, 0, 0, 64, 64, FLT_MAX, EVAL_DERIVED},
                          {{-1280, -720}, {0, 0}, {1, 1}, 0, 0, 64, 64, FLT_MAX / 999.0f, EVAL_DERIVED}};
    for (const Case& c : cases) {
        Eval kept;
        memset(&kept, 0x5A, sizeof kept);
        e = kept;
        assert(eval_numbers(c.scale, c.jitter, c.sign, c.x, c.y, c.w, c.h, 64, 64, 0, c.seconds, &e) == c.why);
        assert(!memcmp(&e, &kept, sizeof e));
    }
    // the bounds themselves are taken: the jitter's, and the longest frame time whose milliseconds are finite
    const float edge[2] = {0.5f, -0.5f};
    assert(eval_numbers(scale, edge, plus, 0, 0, 64, 64, 64, 64, 0, 0.0f, &e) == EVAL_OK);
    assert(eval_numbers(scale, jitter, plus, 0, 0, 64, 64, 64, 64, 0, FLT_MAX / 1000.0f, &e) == EVAL_OK);
    assert(isfinite(e.frame_ms));
    for (int a = EVAL_OK; a <= EVAL_DERIVED; a++)
        for (int b = a + 1; b <= EVAL_DERIVED; b++) assert(strcmp(eval_text(a), eval_text(b)));
    // what a context may be made with: the three flags required, the two others either way, two vector formats, DLAA
    const uint32_t need = kLinearHdr | kUnjittered | kExposureAutomatic;
    assert(create_check(need, kMotionR16G16Float, 64, 64, 64, 64, 64, 64) == CREATE_OK);
    assert(create_check(need | kDepthReversed | kFarInfinite, kMotionR32G32Float, 64, 64, 80, 80, 64, 64) == CREATE_OK);
    const uint32_t required[3] = {kLinearHdr, kUnjittered, kExposureAutomatic};
    for (uint32_t one : required)
        assert(create_check(need & ~one, kMotionR16G16Float, 64, 64, 64, 64, 64, 64) == CREATE_FLAG_MISSING);
    assert(create_check(need | 1u << 5, kMotionR16G16Float, 64, 64, 64, 64, 64, 64) == CREATE_FLAG_UNKNOWN);
    const int formats[4] = {0, 97 /* R16G16B16A16_SFLOAT */, 82 /* R16G16_SNORM */, 106 /* R32G32B32_SFLOAT */};
    for (int format : formats)
        assert(create_check(need, format, 64, 64, 64, 64, 64, 64) == CREATE_MOTION_FORMAT);
    assert(create_check(need, kMotionR16G16Float, 32, 32, 64, 64, 64, 64) == CREATE_NOT_NATIVE); /* not DLAA */
    assert(create_check(need, kMotionR16G16Float, 64, 64, 63, 64, 64, 64) == CREATE_NOT_NATIVE); /* beyond the images */
    assert(create_check(need, kMotionR16G16Float, 0, 0, 64, 64, 0, 0) == CREATE_NOT_NATIVE);
    for (int a = CREATE_OK; a <= CREATE_NOT_NATIVE; a++)
        for (int b = a + 1; b <= CREATE_NOT_NATIVE; b++) assert(strcmp(create_text(a), create_text(b)));
    // NGX's flags: vectors at the render size, HDR, the library's exposure, the depth inverted only when reversed
    const int base = kNgxMvLowRes | kNgxIsHdr | kNgxAutoExposure;
    assert(ngx_flags(need) == base && ngx_flags(need | kFarInfinite) == base);
    assert(ngx_flags(need | kDepthReversed) == (base | kNgxDepthInverted));
    assert(!(ngx_flags(need | kDepthReversed | kFarInfinite) & kNgxMvJittered));
    // NGX's capabilities: Available and NeedsUpdatedDriver must be read, then available and no driver update needed
    const Field yes = {1, 1, 0}, no = {1, 0, 0}, unread = {0, 1, 0xBAD00001u};
    assert(cap_verdict(yes, no) == CAP_OK);
    assert(cap_verdict(unread, no) == CAP_NOT_READ && cap_verdict(yes, unread) == CAP_NOT_READ);
    assert(cap_verdict(Field{0, 0, 0xBAD00001u}, Field{0, 0, 0xBAD00001u}) == CAP_NOT_READ);
    assert(cap_verdict(no, no) == CAP_NOT_AVAILABLE && cap_verdict(yes, yes) == CAP_DRIVER);
    assert(cap_verdict(no, yes) == CAP_NOT_AVAILABLE); /* not available comes first */
    for (int a = CAP_OK; a <= CAP_DRIVER; a++)
        for (int b = a + 1; b <= CAP_DRIVER; b++) assert(strcmp(cap_text(a), cap_text(b)));
    // a frame held to its context: render size, each image's handles, size and format, before anything is recorded
    const Made made = {64, 64, 80, 80, 64, 64, {97, 126, kMotionR16G16Float, 109}};
    const Handed good[4] = {{1, 2, 97, 80, 80}, {3, 4, 126, 80, 80}, {5, 6, kMotionR16G16Float, 80, 80},
                            {7, 8, 109, 64, 64}};
    int which = 0;
    assert(frame_check(made, 64, 64, good, &which) == FRAME_OK && which == -1);
    assert(frame_check(made, 63, 64, good, &which) == FRAME_RENDER && which == -1);
    assert(frame_check(made, 64, 65, good, &which) == FRAME_RENDER);
    for (int k = 0; k < 4; k++) {
        Handed bad[4];
        memcpy(bad, good, sizeof bad);
        bad[k].image = 0;
        assert(frame_check(made, 64, 64, bad, &which) == FRAME_HANDLE && which == k);
        memcpy(bad, good, sizeof bad);
        bad[k].view = 0;
        assert(frame_check(made, 64, 64, bad, &which) == FRAME_HANDLE && which == k);
        memcpy(bad, good, sizeof bad);
        bad[k].w++;
        assert(frame_check(made, 64, 64, bad, &which) == FRAME_SIZE && which == k);
        memcpy(bad, good, sizeof bad);
        bad[k].h--;
        assert(frame_check(made, 64, 64, bad, &which) == FRAME_SIZE && which == k);
        memcpy(bad, good, sizeof bad);
        bad[k].format++;
        assert(frame_check(made, 64, 64, bad, &which) == FRAME_FORMAT && which == k);
    }
    for (int a = FRAME_OK; a <= FRAME_FORMAT; a++)
        for (int b = a + 1; b <= FRAME_FORMAT; b++) assert(strcmp(frame_text(a), frame_text(b)));
    puts("dlss_eval: ok");
    return 0;
}
