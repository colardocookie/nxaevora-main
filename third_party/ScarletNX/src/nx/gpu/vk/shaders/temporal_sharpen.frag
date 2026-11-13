// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// A light sharpen after the temporal resolve (gpu/vk/temporal.c, SNX_ARCEUS_TEMPORAL_SHARPEN). It reads the resolved
// picture from the history image and writes the scene colour only: the history stays unsharpened, so the sharpening
// does not build up from frame to frame. The amount falls where the neighbourhood already has contrast, and the
// result stays within the 3x3 neighbourhood's least and greatest. The formula is temporal_sharpen in temporal_math.h.
#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = 2) uniform sampler2D u_resolved;
layout(push_constant) uniform Constants {
    mat4 reproject;
    vec4 viewport;
    vec4 depth;
    vec4 size; // image width, height, unused, the amount (0 to 1)
} pc;
// HDR to 0..1 and back, so that the contrast measure means the same in dark and bright places
vec3 squeeze(vec3 c) { return c / (1.0 + max(c.r, max(c.g, c.b))); }
vec3 widen(vec3 c) { return c / max(1.0 - max(c.r, max(c.g, c.b)), 0.0001); }
void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    ivec2 last = ivec2(pc.size.xy) - 1;
    vec3 c = squeeze(max(texelFetch(u_resolved, pixel, 0).rgb, vec3(0.0)));
    vec3 low = c, high = c, cross = vec3(0.0);
    for (int y = -1; y <= 1; y++)
        for (int x = -1; x <= 1; x++) {
            if (x == 0 && y == 0) continue;
            vec3 n = squeeze(max(texelFetch(u_resolved, clamp(pixel + ivec2(x, y), ivec2(0), last), 0).rgb, vec3(0.0)));
            low = min(low, n);
            high = max(high, n);
            if (x == 0 || y == 0) cross += n;
        }
    // room left to the ends of the range, against the brightest: little room, little sharpening
    vec3 room = clamp(min(low, 1.0 - high) / max(high, vec3(0.0001)), 0.0, 1.0);
    vec3 w = -sqrt(room) * 0.2 * pc.size.w;
    vec3 sharp = clamp((c + cross * w) / (1.0 + 4.0 * w), low, high);
    o_color = vec4(widen(sharp), 1.0);
}
