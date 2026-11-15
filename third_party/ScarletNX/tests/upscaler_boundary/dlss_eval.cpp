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
    puts("dlss_eval: ok");
    return 0;
}
