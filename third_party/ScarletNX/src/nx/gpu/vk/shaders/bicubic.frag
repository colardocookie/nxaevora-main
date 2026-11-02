// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the ScarletNX contributors
// The frame scaled to the window with a Catmull-Rom filter over 4 x 4 texels. It filters in gamma space (the bytes'
// values), where its negative lobes ring least, and clamps the result.
#version 450
layout(set = 0, binding = 0) uniform sampler2D img;
layout(push_constant) uniform Pass {
    vec4 dst;   // the window's rectangle that the frame covers: x, y, width, height in pixels
    vec4 uv;    // the part of the image shown: its corner at the rectangle's top left, and at its bottom right
    vec2 size;  // the image's size in texels
    float sharp;
    uint srgb;  // the image decodes sRGB on sampling
} pc;
layout(location = 0) out vec4 o;

vec3 to_gamma(vec3 c) {
    if (pc.srgb == 0u) return c;
    return mix(c * 12.92, 1.055 * pow(max(c, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}
vec3 from_gamma(vec3 c) {
    if (pc.srgb == 0u) return c;
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}
vec3 fetch(ivec2 p) {
    p = clamp(p, ivec2(0), ivec2(pc.size) - 1);
    return to_gamma(texelFetch(img, p, 0).rgb);
}
// the four weights of the texels at -1, 0, 1 and 2 for a position x texels after the second one
vec4 weights(float x) {
    float x2 = x * x, x3 = x2 * x;
    return vec4(-0.5 * x3 + x2 - 0.5 * x, 1.5 * x3 - 2.5 * x2 + 1.0, -1.5 * x3 + 2.0 * x2 + 0.5 * x,
                0.5 * x3 - 0.5 * x2);
}

void main() {
    vec2 t = (gl_FragCoord.xy - pc.dst.xy) / pc.dst.zw;
    vec2 pos = mix(pc.uv.xy, pc.uv.zw, t) * pc.size - 0.5;
    ivec2 i = ivec2(floor(pos));
    vec2 f = pos - vec2(i);
    vec4 wx = weights(f.x), wy = weights(f.y);
    vec3 acc = vec3(0.0);
    for (int j = 0; j < 4; j++) {
        vec3 row = vec3(0.0);
        for (int k = 0; k < 4; k++) row += wx[k] * fetch(i + ivec2(k - 1, j - 1));
        acc += wy[j] * row;
    }
    o = vec4(from_gamma(clamp(acc, 0.0, 1.0)), 1.0);
}
