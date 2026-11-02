// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the ScarletNX contributors
// FXAA, written for this project after the method of Timothy Lottes' FXAA 3.11 as it is published (NVIDIA):
//  1. A pixel whose cross of neighbours has little contrast is left as it is.
//  2. The edge's direction comes from the second differences of luma in the 3x3 neighbourhood.
//  3. The search walks along the edge both ways, in growing steps, until the luma changes by a quarter of the gradient:
//     the edge's ends. The pixel's distance to the nearer end decides how far the pixel is moved across the edge.
//  4. A blend with the neighbourhood's mean makes up for lines thinner than a pixel.
#version 450
layout(set = 0, binding = 0) uniform sampler2D img;
layout(push_constant) uniform Pass {
    vec2 texel; // one texel in uv
    uint srgb;  // the image decodes sRGB on sampling: luma is taken as it would be in the bytes
} pc;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o;

const float EDGE_MIN = 0.004;         // contrast below this is no edge (in luma): low enough for the green on green of grass
const float EDGE_RELATIVE = 0.0;      // no relative threshold, as the FXAA that Eden uses has none
const float SUBPIXEL_QUALITY = 0.75;  // how much of the mean's blend thin lines get
const int SEARCH_STEPS = 12;
// the length of each search step in texels: short where the end is likely near, long to cross the long edges
const float STEP[SEARCH_STEPS] = float[](1.0, 1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0);

float luma_of(vec3 c) {
    float l = dot(c, vec3(0.299, 0.587, 0.114));
    return pc.srgb != 0u ? sqrt(max(l, 0.0)) : l;
}
float luma_at(vec2 uv) { return luma_of(texture(img, uv).rgb); }

void main() {
    vec2 t = pc.texel;
    vec4 mid = texture(img, v_uv);
    float lM = luma_of(mid.rgb);
    float lN = luma_at(v_uv + vec2(0.0, -t.y));
    float lS = luma_at(v_uv + vec2(0.0, t.y));
    float lW = luma_at(v_uv + vec2(-t.x, 0.0));
    float lE = luma_at(v_uv + vec2(t.x, 0.0));
    float lMin = min(lM, min(min(lN, lS), min(lW, lE)));
    float lMax = max(lM, max(max(lN, lS), max(lW, lE)));
    float range = lMax - lMin;
    if (range < max(EDGE_MIN, lMax * EDGE_RELATIVE)) {
        o = mid;
        return;
    }
    float lNW = luma_at(v_uv + vec2(-t.x, -t.y));
    float lNE = luma_at(v_uv + vec2(t.x, -t.y));
    float lSW = luma_at(v_uv + vec2(-t.x, t.y));
    float lSE = luma_at(v_uv + vec2(t.x, t.y));

    // the edge runs along x (horizontal) when luma changes more from row to row than from column to column
    float edgeH = abs(lNW - 2.0 * lN + lNE) + 2.0 * abs(lW - 2.0 * lM + lE) + abs(lSW - 2.0 * lS + lSE);
    float edgeV = abs(lNW - 2.0 * lW + lSW) + 2.0 * abs(lN - 2.0 * lM + lS) + abs(lNE - 2.0 * lE + lSE);
    bool horizontal = edgeH >= edgeV;

    // the two neighbours across the edge: side 1 is the positive direction (down or right), side 2 the negative
    float l1 = horizontal ? lS : lE;
    float l2 = horizontal ? lN : lW;
    float g1 = l1 - lM;
    float g2 = l2 - lM;
    bool side1 = abs(g1) >= abs(g2);
    float gradient = 0.25 * max(abs(g1), abs(g2));
    float step_len = horizontal ? t.y : t.x;
    float average;
    if (side1) {
        average = 0.5 * (l1 + lM);
    } else {
        step_len = -step_len;
        average = 0.5 * (l2 + lM);
    }

    // the search runs on the edge's middle line, between the pixel and the neighbour across the steepest side
    vec2 uv = v_uv;
    if (horizontal) uv.y += 0.5 * step_len; else uv.x += 0.5 * step_len;
    vec2 along = horizontal ? vec2(t.x, 0.0) : vec2(0.0, t.y);
    vec2 uv1 = uv - along;
    vec2 uv2 = uv + along;
    float end1 = luma_at(uv1) - average;
    float end2 = luma_at(uv2) - average;
    bool done1 = abs(end1) >= gradient;
    bool done2 = abs(end2) >= gradient;
    for (int i = 1; i < SEARCH_STEPS && !(done1 && done2); i++) {
        if (!done1) {
            uv1 -= along * STEP[i];
            end1 = luma_at(uv1) - average;
            done1 = abs(end1) >= gradient;
        }
        if (!done2) {
            uv2 += along * STEP[i];
            end2 = luma_at(uv2) - average;
            done2 = abs(end2) >= gradient;
        }
    }

    float d1 = horizontal ? v_uv.x - uv1.x : v_uv.y - uv1.y;
    float d2 = horizontal ? uv2.x - v_uv.x : uv2.y - v_uv.y;
    bool nearer1 = d1 < d2;
    float nearest = min(d1, d2);
    float offset = 0.5 - nearest / (d1 + d2);
    // the move helps only when the luma at the nearer end changes toward the neighbour's side
    bool centre_smaller = lM < average;
    bool right_way = ((nearer1 ? end1 : end2) < 0.0) != centre_smaller;
    float across = right_way ? offset : 0.0;

    float mean = (2.0 * (lN + lS + lW + lE) + lNW + lNE + lSW + lSE) * (1.0 / 12.0);
    float thin = clamp(abs(mean - lM) / max(range, 1e-4), 0.0, 1.0);
    thin = (-2.0 * thin + 3.0) * thin * thin;
    across = max(across, thin * thin * SUBPIXEL_QUALITY);

    vec2 at = v_uv;
    if (horizontal) at.y += across * step_len; else at.x += across * step_len;
    o = vec4(texture(img, at).rgb, mid.a);
}
