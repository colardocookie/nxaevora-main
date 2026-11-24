// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// SNX_ARCEUS_UPSCALER=spatial (D3): the private resolved picture's rectangle, w x h at the origin, scaled to the
// target's W x H with Catmull-Rom into the picture part of the larger texture. The sixteen taps are held to the
// rectangle, so nothing outside it is read. temporal_spatial_catmull_rom in temporal_spatial.h is the whole of it.
#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = 0) uniform sampler2D u_picture;
layout(push_constant) uniform Constants {
    mat4 reproject;
    vec4 viewport;
    vec4 depth;
    vec4 size; // the rectangle's w and h, the picture part's W and H
    vec4 jitter;
} pc;
// the weights of the four taps at -1, 0, 1, 2 from the tap below the point, t its distance from it
vec4 weights(float t) {
    float t2 = t * t, t3 = t2 * t;
    return vec4(-0.5 * t3 + t2 - 0.5 * t, 1.5 * t3 - 2.5 * t2 + 1.0, -1.5 * t3 + 2.0 * t2 + 0.5 * t,
                0.5 * t3 - 0.5 * t2);
}
void main() {
    vec2 at = gl_FragCoord.xy * pc.size.xy / pc.size.zw - 0.5; // in the rectangle's pixels, centres at whole numbers
    vec2 base = floor(at);
    vec4 wx = weights(at.x - base.x), wy = weights(at.y - base.y);
    ivec2 last = ivec2(pc.size.xy) - 1;
    vec4 sum = vec4(0.0);
    for (int j = 0; j < 4; j++) {
        vec4 row = vec4(0.0);
        for (int i = 0; i < 4; i++)
            row += wx[i] * texelFetch(u_picture, clamp(ivec2(base) + ivec2(i - 1, j - 1), ivec2(0), last), 0);
        sum += wy[j] * row;
    }
    // the negative lobes can undershoot below 0 at a hard edge: no negative light
    o_color = max(sum, vec4(0.0));
}
