// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// The temporal probe's numbers (gpu/vk/temporal.c): a small image whose texels each sample one pixel of the scene.
// Red: the relative colour error against last frame fetched through the motion vector. Green: the same without the
// vector. Blue: the vector's length in pixels. Alpha: the pixel's depth as the game's projection gives it (1 near,
// 0 far), -1 for the far plane, -2 where last frame's pixel is off screen.
// With anti-aliasing on (depth.z is 1) u_before is the resolved frame: red is 1 where the history was dropped, green
// how far the resolved colour is from the frame's own.
#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_stats;
layout(set = 0, binding = 0) uniform sampler2D u_motion;
layout(set = 0, binding = 1) uniform sampler2D u_now;
layout(set = 0, binding = 2) uniform sampler2D u_before;
layout(push_constant) uniform Constants {
    mat4 reproject;
    vec4 viewport;
    vec4 depth; // depth translate, depth scale, 1 with anti-aliasing on, 1 when u_shown holds object vectors
    vec4 size;  // image width, height, view, gain
} pc;
layout(set = 0, binding = 3) uniform sampler2D u_depth;
layout(set = 0, binding = 4) uniform sampler2D u_last; // the frame before, as it was drawn
layout(set = 0, binding = 5) uniform sampler2D u_shown; // the resolved picture of the frame before
float relative(vec3 a, vec3 b) {
    vec3 e = abs(a - b) / (max(a, b) + 0.05);
    return max(e.r, max(e.g, e.b));
}
void main() {
    ivec2 pixel = ivec2(v_uv * pc.size.xy);
    vec2 uv = (vec2(pixel) + 0.5) / pc.size.xy;
    float z = (texelFetch(u_depth, pixel, 0).r - pc.depth.x) / pc.depth.y;
    vec2 motion = texelFetch(u_motion, pixel, 0).rg;
    vec3 now = texelFetch(u_now, pixel, 0).rgb;
    vec2 from = uv - motion;
    // the length stays under 1000: the thousands are marks (below)
    o_stats = vec4(0.0, relative(now, textureLod(u_before, uv, 0.0).rgb), min(length(motion * pc.size.xy), 990.0), z);
    if (pc.depth.z > 0.5) {
        // three marks in one number: 1 the history was dropped, 2 the frame as drawn changed since the one before,
        // 4 the resolved picture changed since the one before
        vec4 resolved = texelFetch(u_before, pixel, 0);
        // The blend moves a pixel by a tenth of its distance a frame, so the resolved picture is also counted at
        // two finer steps: 8 above 0.05, 16 above 0.02.
        float shown = relative(resolved.rgb, texelFetch(u_shown, pixel, 0).rgb);
        o_stats.r = (resolved.a < 0.0 ? 1.0 : 0.0) +
                    (relative(now, texelFetch(u_last, pixel, 0).rgb) > 0.25 ? 2.0 : 0.0) + (shown > 0.25 ? 4.0 : 0.0) +
                    (shown > 0.05 ? 8.0 : 0.0) + (shown > 0.02 ? 16.0 : 0.0);
        if (z <= 0.0) o_stats.a = -1.0;
        return;
    }
    if (pc.depth.w > 0.5) {
        // The probe with object vectors: u_shown is the objects' target. On a pixel that took an object's vector,
        // blue gains 1000, and 2000 more when the camera vector alone would have been wrong there.
        vec4 object = texelFetch(u_shown, pixel, 0);
        float stored = texelFetch(u_depth, pixel, 0).r;
        if (object.b >= 0.0 && abs(object.b - stored) <= 0.002 * stored + 0.0000002) {
            vec2 ndc = (vec2(pixel) + 0.5 - pc.viewport.xy) / pc.viewport.zw * 2.0 - 1.0;
            vec4 before = pc.reproject * vec4(ndc, max(z, 0.0), 1.0);
            float camera = 1.0;
            if (before.w > 0.0) {
                vec2 was = (before.xy / before.w * 0.5 + 0.5) * pc.viewport.zw + pc.viewport.xy;
                camera = relative(now, textureLod(u_before, was / pc.size.xy, 0.0).rgb);
            }
            o_stats.b += 1000.0 + (camera > 0.25 ? 2000.0 : 0.0);
        }
    }
    if (z <= 0.0) {
        o_stats.a = -1.0;
        return;
    }
    if (from.x < 0.0 || from.y < 0.0 || from.x > 1.0 || from.y > 1.0) {
        o_stats.a = -2.0;
        return;
    }
    o_stats.r = relative(now, textureLod(u_before, from, 0.0).rgb);
}
