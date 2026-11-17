// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the ScarletNX contributors
//
// SMAA 1x, a port to GLSL of the luma edge detection, the blending weight calculation and the neighborhood blending
// of SMAA.hlsl (https://github.com/iryoku/smaa), with the preset HIGH, without predication, reprojection and
// subpixel indices. The pass is chosen by a macro: SMAA_PASS_EDGES, SMAA_PASS_WEIGHTS or SMAA_PASS_BLEND.
// The original is under the MIT license of the file shaders/smaa/LICENSE.txt:
//
//   Copyright (C) 2013 Jorge Jimenez (jorge@iryoku.com)
//   Copyright (C) 2013 Jose I. Echevarria (joseignacioechevarria@gmail.com)
//   Copyright (C) 2013 Belen Masia (bmasia@unizar.es)
//   Copyright (C) 2013 Fernando Navarro (fernandn@microsoft.com)
//   Copyright (C) 2013 Diego Gutierrez (diegog@unizar.es)
#version 450

layout(push_constant) uniform Pass {
    vec4 metrics; // 1 / width, 1 / height, width, height of the frame
    uint srgb;    // the frame decodes sRGB on sampling: luma is taken as it would be in the bytes
} pc;
layout(location = 0) in vec2 v_uv;

// the preset HIGH
const float THRESHOLD = 0.1;
const int MAX_SEARCH_STEPS = 16;
const int MAX_SEARCH_STEPS_DIAG = 8;
const float CORNER_ROUNDING_NORM = 0.25;
const float LOCAL_CONTRAST_ADAPTATION_FACTOR = 2.0;

#define RT_METRICS pc.metrics

#if defined(SMAA_PASS_EDGES)
// ---------------------------------------------------------------------------------------------------------------
// pass 1: the edges at the left and the top of each pixel (red, green), by luma
layout(set = 0, binding = 0) uniform sampler2D colorTex;
layout(location = 0) out vec2 o;

float luma_at(vec2 uv) {
    vec3 c = textureLod(colorTex, uv, 0.0).rgb;
    if (pc.srgb != 0u) c = pow(max(c, vec3(0.0)), vec3(1.0 / 2.2));
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

void main() {
    vec2 texcoord = v_uv;
    vec4 offset0 = RT_METRICS.xyxy * vec4(-1.0, 0.0, 0.0, -1.0) + texcoord.xyxy;
    vec4 offset1 = RT_METRICS.xyxy * vec4(1.0, 0.0, 0.0, 1.0) + texcoord.xyxy;
    vec4 offset2 = RT_METRICS.xyxy * vec4(-2.0, 0.0, 0.0, -2.0) + texcoord.xyxy;
    float L = luma_at(texcoord);
    float Lleft = luma_at(offset0.xy);
    float Ltop = luma_at(offset0.zw);
    vec4 delta;
    delta.xy = abs(L - vec2(Lleft, Ltop));
    vec2 edges = step(vec2(THRESHOLD), delta.xy);
    if (dot(edges, vec2(1.0)) == 0.0) discard; // the target was cleared to no edge
    float Lright = luma_at(offset1.xy);
    float Lbottom = luma_at(offset1.zw);
    delta.zw = abs(L - vec2(Lright, Lbottom));
    vec2 maxDelta = max(delta.xy, delta.zw);
    float Lleftleft = luma_at(offset2.xy);
    float Ltoptop = luma_at(offset2.zw);
    delta.zw = abs(vec2(Lleft, Ltop) - vec2(Lleftleft, Ltoptop));
    maxDelta = max(maxDelta.xy, delta.zw);
    float finalDelta = max(maxDelta.x, maxDelta.y);
    // local contrast adaptation
    edges *= step(vec2(finalDelta), LOCAL_CONTRAST_ADAPTATION_FACTOR * delta.xy);
    o = edges;
}

#elif defined(SMAA_PASS_WEIGHTS)
// ---------------------------------------------------------------------------------------------------------------
// pass 2: the blending weights of each pixel (right, left, bottom, top) from the shapes of the edges around it
layout(set = 0, binding = 0) uniform sampler2D edgesTex;
layout(set = 0, binding = 1) uniform sampler2D areaTex;
layout(set = 0, binding = 2) uniform sampler2D searchTex;
layout(location = 0) out vec4 o;

const int AREATEX_MAX_DISTANCE = 16;
const int AREATEX_MAX_DISTANCE_DIAG = 20;
const vec2 AREATEX_PIXEL_SIZE = 1.0 / vec2(160.0, 560.0);
const float AREATEX_SUBTEX_SIZE = 1.0 / 7.0;
const vec2 SEARCHTEX_SIZE = vec2(66.0, 33.0);
const vec2 SEARCHTEX_PACKED_SIZE = vec2(64.0, 16.0);

#define SAMPLE_LZ(tex, coord) textureLod(tex, coord, 0.0)
#define SAMPLE_LZ_OFFSET(tex, coord, off) textureLodOffset(tex, coord, 0.0, off)

vec2 decode_diag_bilinear(vec2 e) {
    e.r = e.r * abs(5.0 * e.r - 5.0 * 0.75);
    return roundEven(e);
}
vec4 decode_diag_bilinear(vec4 e) {
    e.rb = e.rb * abs(5.0 * e.rb - 5.0 * 0.75);
    return roundEven(e);
}

// the diagonal search for the "\" direction
vec2 search_diag1(vec2 texcoord, vec2 dir, out vec2 e) {
    vec4 coord = vec4(texcoord, -1.0, 1.0);
    vec3 t = vec3(RT_METRICS.xy, 1.0);
    e = vec2(0.0);
    while (coord.z < float(MAX_SEARCH_STEPS_DIAG - 1) && coord.w > 0.9) {
        coord.xyz = t * vec3(dir, 1.0) + coord.xyz;
        e = SAMPLE_LZ(edgesTex, coord.xy).rg;
        coord.w = dot(e, vec2(0.5));
    }
    return coord.zw;
}
// the "/" direction: the bilinear fetch at the corner needs the decoding, and a quarter-texel shift of the start
vec2 search_diag2(vec2 texcoord, vec2 dir, out vec2 e) {
    vec4 coord = vec4(texcoord, -1.0, 1.0);
    coord.x += 0.25 * RT_METRICS.x;
    vec3 t = vec3(RT_METRICS.xy, 1.0);
    e = vec2(0.0);
    while (coord.z < float(MAX_SEARCH_STEPS_DIAG - 1) && coord.w > 0.9) {
        coord.xyz = t * vec3(dir, 1.0) + coord.xyz;
        e = SAMPLE_LZ(edgesTex, coord.xy).rg;
        e = decode_diag_bilinear(e);
        coord.w = dot(e, vec2(0.5));
    }
    return coord.zw;
}

vec2 area_diag(vec2 dist, vec2 e, float offset) {
    vec2 texcoord = float(AREATEX_MAX_DISTANCE_DIAG) * e + dist;
    texcoord = AREATEX_PIXEL_SIZE * texcoord + 0.5 * AREATEX_PIXEL_SIZE;
    texcoord.x += 0.5; // the diagonal patterns are in the right half of the texture
    texcoord.y += AREATEX_SUBTEX_SIZE * offset;
    return SAMPLE_LZ(areaTex, texcoord).rg;
}

vec2 calculate_diag_weights(vec2 texcoord, vec2 e) {
    vec2 weights = vec2(0.0);
    vec4 d;
    vec2 end;
    if (e.r > 0.0) {
        d.xz = search_diag1(texcoord, vec2(-1.0, 1.0), end);
        d.x += float(end.y > 0.9);
    } else
        d.xz = vec2(0.0);
    d.yw = search_diag1(texcoord, vec2(1.0, -1.0), end);
    if (d.x + d.y > 2.0) { // d.x + d.y + 1 > 3
        vec4 coords = vec4(-d.x + 0.25, d.x, d.y, -d.y - 0.25) * RT_METRICS.xyxy + texcoord.xyxy;
        vec4 c;
        c.xy = SAMPLE_LZ_OFFSET(edgesTex, coords.xy, ivec2(-1, 0)).rg;
        c.zw = SAMPLE_LZ_OFFSET(edgesTex, coords.zw, ivec2(1, 0)).rg;
        c.yxwz = decode_diag_bilinear(c.xyzw);
        vec2 cc = vec2(2.0) * c.xz + c.yw;
        if (d.z >= 0.9) cc.x = 0.0; // no edge at the end of the line
        if (d.w >= 0.9) cc.y = 0.0;
        weights += area_diag(d.xy, cc, 0.0);
    }
    d.xz = search_diag2(texcoord, vec2(-1.0, -1.0), end);
    if (SAMPLE_LZ_OFFSET(edgesTex, texcoord, ivec2(1, 0)).r > 0.0) {
        d.yw = search_diag2(texcoord, vec2(1.0, 1.0), end);
        d.y += float(end.y > 0.9);
    } else
        d.yw = vec2(0.0);
    if (d.x + d.y > 2.0) { // d.x + d.y + 1 > 3
        vec4 coords = vec4(-d.x, -d.x, d.y, d.y) * RT_METRICS.xyxy + texcoord.xyxy;
        vec4 c;
        c.x = SAMPLE_LZ_OFFSET(edgesTex, coords.xy, ivec2(-1, 0)).g;
        c.y = SAMPLE_LZ_OFFSET(edgesTex, coords.xy, ivec2(0, -1)).r;
        c.zw = SAMPLE_LZ_OFFSET(edgesTex, coords.zw, ivec2(1, 0)).gr;
        vec2 cc = vec2(2.0) * c.xz + c.yw;
        if (d.z >= 0.9) cc.x = 0.0;
        if (d.w >= 0.9) cc.y = 0.0;
        weights += area_diag(d.xy, cc, 0.0).gr;
    }
    return weights;
}

// the length of a search that ended on the edge pattern e, from the search texture
float search_length(vec2 e, float offset) {
    vec2 scale = SEARCHTEX_SIZE * vec2(0.5, -1.0);
    vec2 bias = SEARCHTEX_SIZE * vec2(offset, 1.0);
    scale += vec2(-1.0, 1.0); // a texel's center
    bias += vec2(0.5, -0.5);
    scale *= 1.0 / SEARCHTEX_PACKED_SIZE;
    bias *= 1.0 / SEARCHTEX_PACKED_SIZE;
    return SAMPLE_LZ(searchTex, scale * e + bias).r;
}
float search_x_left(vec2 texcoord, float end) {
    vec2 e = vec2(0.0, 1.0);
    while (texcoord.x > end && e.g > 0.8281 && e.r == 0.0) {
        e = SAMPLE_LZ(edgesTex, texcoord).rg;
        texcoord = -vec2(2.0, 0.0) * RT_METRICS.xy + texcoord;
    }
    float offset = -(255.0 / 127.0) * search_length(e, 0.0) + 3.25;
    return RT_METRICS.x * offset + texcoord.x;
}
float search_x_right(vec2 texcoord, float end) {
    vec2 e = vec2(0.0, 1.0);
    while (texcoord.x < end && e.g > 0.8281 && e.r == 0.0) {
        e = SAMPLE_LZ(edgesTex, texcoord).rg;
        texcoord = vec2(2.0, 0.0) * RT_METRICS.xy + texcoord;
    }
    float offset = -(255.0 / 127.0) * search_length(e, 0.5) + 3.25;
    return -RT_METRICS.x * offset + texcoord.x;
}
float search_y_up(vec2 texcoord, float end) {
    vec2 e = vec2(1.0, 0.0);
    while (texcoord.y > end && e.r > 0.8281 && e.g == 0.0) {
        e = SAMPLE_LZ(edgesTex, texcoord).rg;
        texcoord = -vec2(0.0, 2.0) * RT_METRICS.xy + texcoord;
    }
    float offset = -(255.0 / 127.0) * search_length(e.gr, 0.0) + 3.25;
    return RT_METRICS.y * offset + texcoord.y;
}
float search_y_down(vec2 texcoord, float end) {
    vec2 e = vec2(1.0, 0.0);
    while (texcoord.y < end && e.r > 0.8281 && e.g == 0.0) {
        e = SAMPLE_LZ(edgesTex, texcoord).rg;
        texcoord = vec2(0.0, 2.0) * RT_METRICS.xy + texcoord;
    }
    float offset = -(255.0 / 127.0) * search_length(e.gr, 0.5) + 3.25;
    return -RT_METRICS.y * offset + texcoord.y;
}

// the area under the edge's line for the distances to its ends and the crossing edges at them
vec2 area(vec2 dist, float e1, float e2) {
    vec2 texcoord = float(AREATEX_MAX_DISTANCE) * roundEven(4.0 * vec2(e1, e2)) + dist;
    texcoord = AREATEX_PIXEL_SIZE * texcoord + 0.5 * AREATEX_PIXEL_SIZE;
    return SAMPLE_LZ(areaTex, texcoord).rg;
}

void detect_horizontal_corner_pattern(inout vec2 weights, vec4 texcoord, vec2 d) {
    vec2 leftRight = step(d.xy, d.yx);
    vec2 rounding = (1.0 - CORNER_ROUNDING_NORM) * leftRight;
    rounding /= leftRight.x + leftRight.y; // less blending for pixels in the center of a line
    vec2 factor = vec2(1.0);
    factor.x -= rounding.x * SAMPLE_LZ_OFFSET(edgesTex, texcoord.xy, ivec2(0, 1)).r;
    factor.x -= rounding.y * SAMPLE_LZ_OFFSET(edgesTex, texcoord.zw, ivec2(1, 1)).r;
    factor.y -= rounding.x * SAMPLE_LZ_OFFSET(edgesTex, texcoord.xy, ivec2(0, -2)).r;
    factor.y -= rounding.y * SAMPLE_LZ_OFFSET(edgesTex, texcoord.zw, ivec2(1, -2)).r;
    weights *= clamp(factor, 0.0, 1.0);
}
void detect_vertical_corner_pattern(inout vec2 weights, vec4 texcoord, vec2 d) {
    vec2 leftRight = step(d.xy, d.yx);
    vec2 rounding = (1.0 - CORNER_ROUNDING_NORM) * leftRight;
    rounding /= leftRight.x + leftRight.y;
    vec2 factor = vec2(1.0);
    factor.x -= rounding.x * SAMPLE_LZ_OFFSET(edgesTex, texcoord.xy, ivec2(1, 0)).g;
    factor.x -= rounding.y * SAMPLE_LZ_OFFSET(edgesTex, texcoord.zw, ivec2(1, 1)).g;
    factor.y -= rounding.x * SAMPLE_LZ_OFFSET(edgesTex, texcoord.xy, ivec2(-2, 0)).g;
    factor.y -= rounding.y * SAMPLE_LZ_OFFSET(edgesTex, texcoord.zw, ivec2(-2, 1)).g;
    weights *= clamp(factor, 0.0, 1.0);
}

void main() {
    vec2 texcoord = v_uv;
    vec2 pixcoord = texcoord * RT_METRICS.zw;
    vec4 offset0 = RT_METRICS.xyxy * vec4(-0.25, -0.125, 1.25, -0.125) + texcoord.xyxy;
    vec4 offset1 = RT_METRICS.xyxy * vec4(-0.125, -0.25, -0.125, 1.25) + texcoord.xyxy;
    vec4 offset2 = RT_METRICS.xxyy * (vec4(-2.0, 2.0, -2.0, 2.0) * float(MAX_SEARCH_STEPS)) +
                   vec4(offset0.xz, offset1.yw);
    vec4 weights = vec4(0.0);
    vec2 e = texture(edgesTex, texcoord).rg;
    if (e.g > 0.0) { // an edge at the north
        weights.rg = calculate_diag_weights(texcoord, e);
        if (weights.r == -weights.g) { // no diagonal pattern: the horizontal search
            vec2 d;
            vec3 coords;
            coords.x = search_x_left(offset0.xy, offset2.x);
            coords.y = offset1.y; // a quarter of a texel above the center, where the crossing edges are read
            d.x = coords.x;
            float e1 = SAMPLE_LZ(edgesTex, coords.xy).r;
            coords.z = search_x_right(offset0.zw, offset2.y);
            d.y = coords.z;
            d = abs(roundEven(RT_METRICS.zz * d - pixcoord.xx));
            vec2 sqrt_d = sqrt(d);
            float e2 = SAMPLE_LZ_OFFSET(edgesTex, coords.zy, ivec2(1, 0)).r;
            weights.rg = area(sqrt_d, e1, e2);
            coords.y = texcoord.y;
            detect_horizontal_corner_pattern(weights.rg, coords.xyzy, d);
        } else
            e.r = 0.0; // the diagonal covers it: no vertical processing
    }
    if (e.r > 0.0) { // an edge at the west
        vec2 d;
        vec3 coords;
        coords.y = search_y_up(offset1.xy, offset2.z);
        coords.x = offset0.x;
        d.x = coords.y;
        float e1 = SAMPLE_LZ(edgesTex, coords.xy).g;
        coords.z = search_y_down(offset1.zw, offset2.w);
        d.y = coords.z;
        d = abs(roundEven(RT_METRICS.ww * d - pixcoord.yy));
        vec2 sqrt_d = sqrt(d);
        float e2 = SAMPLE_LZ_OFFSET(edgesTex, coords.xz, ivec2(0, 1)).g;
        weights.ba = area(sqrt_d, e1, e2);
        coords.x = texcoord.x;
        detect_vertical_corner_pattern(weights.ba, coords.xyxz, d);
    }
    o = weights;
}

#elif defined(SMAA_PASS_BLEND)
// ---------------------------------------------------------------------------------------------------------------
// pass 3: each pixel blended with its neighbour by the weights, in one bilinear fetch of the frame
layout(set = 0, binding = 0) uniform sampler2D colorTex;
layout(set = 0, binding = 1) uniform sampler2D blendTex;
layout(location = 0) out vec4 o;

void main() {
    vec2 texcoord = v_uv;
    vec4 offset = RT_METRICS.xyxy * vec4(1.0, 0.0, 0.0, 1.0) + texcoord.xyxy;
    vec4 a;
    a.x = textureLod(blendTex, offset.xy, 0.0).a; // right
    a.y = textureLod(blendTex, offset.zw, 0.0).g; // top
    a.wz = textureLod(blendTex, texcoord, 0.0).xz; // bottom, left
    if (dot(a, vec4(1.0)) < 1e-5) {
        o = textureLod(colorTex, texcoord, 0.0);
        return;
    }
    bool h = max(a.x, a.z) > max(a.y, a.w); // the larger blend is horizontal
    vec4 blendingOffset = vec4(0.0, a.y, 0.0, a.w);
    vec2 blendingWeight = a.yw;
    if (h) {
        blendingOffset = vec4(a.x, 0.0, a.z, 0.0);
        blendingWeight = a.xz;
    }
    blendingWeight /= dot(blendingWeight, vec2(1.0));
    vec4 blendingCoord = blendingOffset * vec4(RT_METRICS.xy, -RT_METRICS.xy) + texcoord.xyxy;
    o = blendingWeight.x * textureLod(colorTex, blendingCoord.xy, 0.0);
    o += blendingWeight.y * textureLod(colorTex, blendingCoord.zw, 0.0);
}
#else
#error choose a pass: SMAA_PASS_EDGES, SMAA_PASS_WEIGHTS or SMAA_PASS_BLEND
#endif
