// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the ScarletNX contributors
// FXAA: the edge-directed blur of Timothy Lottes' whitepaper "FXAA" (NVIDIA, 2009), written for this project. A pixel
// whose 3x3 neighbourhood has little contrast stays as it is. Elsewhere the blur runs along the edge's direction, which
// the luma gradient gives, with two taps, or with four when two would reach past the neighbourhood's range.
#version 450
layout(set = 0, binding = 0) uniform sampler2D img;
layout(push_constant) uniform Pass {
    vec2 texel; // one texel in uv
    uint srgb;  // the image decodes sRGB on sampling: luma is taken as it would be in the bytes
} pc;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o;

const float EDGE_MIN = 0.0312;      // contrast below this is no edge (in luma)
const float EDGE_RELATIVE = 0.125;  // nor is contrast below this part of the brightest neighbour
const float SPAN_MAX = 8.0;         // the longest blur step, in texels
const float REDUCE_MIN = 1.0 / 128.0;
const float REDUCE_MUL = 1.0 / 8.0;

float luma(vec3 c) {
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    return pc.srgb != 0u ? sqrt(max(l, 0.0)) : l;
}
vec3 tap(vec2 uv) { return texture(img, uv).rgb; }

void main() {
    vec4 m = texture(img, v_uv);
    vec2 t = pc.texel;
    float lM = luma(m.rgb);
    float lNW = luma(tap(v_uv + vec2(-1.0, -1.0) * t));
    float lNE = luma(tap(v_uv + vec2(1.0, -1.0) * t));
    float lSW = luma(tap(v_uv + vec2(-1.0, 1.0) * t));
    float lSE = luma(tap(v_uv + vec2(1.0, 1.0) * t));
    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));
    if (lMax - lMin < max(EDGE_MIN, lMax * EDGE_RELATIVE)) {
        o = m;
        return;
    }
    vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), (lNW + lSW) - (lNE + lSE));
    float reduce = max((lNW + lNE + lSW + lSE) * (0.25 * REDUCE_MUL), REDUCE_MIN);
    dir = clamp(dir / (min(abs(dir.x), abs(dir.y)) + reduce), vec2(-SPAN_MAX), vec2(SPAN_MAX)) * t;
    vec3 two = 0.5 * (tap(v_uv + dir * (1.0 / 3.0 - 0.5)) + tap(v_uv + dir * (2.0 / 3.0 - 0.5)));
    vec3 four = two * 0.5 + 0.25 * (tap(v_uv + dir * -0.5) + tap(v_uv + dir * 0.5));
    float lFour = luma(four);
    o = vec4(lFour < lMin || lFour > lMax ? two : four, m.a);
}
