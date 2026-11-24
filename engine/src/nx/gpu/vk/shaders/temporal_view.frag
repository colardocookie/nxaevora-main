// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// The debug views of the temporal probe (gpu/vk/temporal.c), drawn over the scene colour. View 1: the motion vectors
// as colour (red across, green down, blue where either is negative). View 2: the difference between this frame's
// colour and last frame's colour fetched through the vectors, black where the reprojection is right.
#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = 0) uniform sampler2D u_motion;
layout(set = 0, binding = 1) uniform sampler2D u_now;
layout(set = 0, binding = 2) uniform sampler2D u_before;
layout(push_constant) uniform Constants {
    mat4 reproject;
    vec4 viewport;
    vec4 depth;
    vec4 size; // image width, height, view, gain
    vec4 jitter; // unused, unused, 1 when the scene is a rectangle of the image
} pc;
// The pixels our passes may read: the whole image, or with jitter.z set the scene's rectangle of it, which is the
// viewport's. Outside the rectangle the images hold what an earlier frame left (temporal_rect_tap in temporal_math.h).
bool in_rectangle() { return pc.jitter.z > 0.5; }
ivec2 first_pixel() {
    if (!in_rectangle()) return ivec2(0);
    return ivec2(round(vec2(pc.viewport.x, min(pc.viewport.y, pc.viewport.y + pc.viewport.w))));
}
ivec2 last_pixel() {
    if (!in_rectangle()) return ivec2(pc.size.xy) - 1;
    return first_pixel() + ivec2(round(abs(pc.viewport.zw))) - 1;
}
// a place for a linear fetch, in pixels, kept where the fetch reads no texel outside the rectangle
vec2 tap(vec2 pixels) {
    if (!in_rectangle()) return pixels;
    return clamp(pixels, vec2(first_pixel()) + 0.5, vec2(last_pixel()) + 0.5);
}
// 1 when a centre to fetch at, as a place of the image from 0 to 1, lies outside the picture: outside the image,
// or with a rectangle outside it, whose right and bottom edges are not part of it (temporal_rect_holds)
bool outside(vec2 uv) {
    if (!in_rectangle()) return uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0;
    vec2 at = uv * pc.size.xy, low = vec2(first_pixel()), high = vec2(last_pixel()) + 1.0;
    return !(at.x >= low.x && at.y >= low.y && at.x < high.x && at.y < high.y);
}
void main() {
    vec2 uv = gl_FragCoord.xy / pc.size.xy;
    vec2 motion = texelFetch(u_motion, ivec2(gl_FragCoord.xy), 0).rg;
    if (pc.size.z < 1.5) {
        o_color = vec4(abs(motion) * pc.size.w, (motion.x < 0.0 || motion.y < 0.0) ? 0.25 : 0.0, 1.0);
        return;
    }
    vec2 from = uv - motion;
    if (outside(from)) {
        o_color = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    // relative to the brightness, so that a bright edge does not light up on a sub-pixel error
    vec3 now = texelFetch(u_now, ivec2(gl_FragCoord.xy), 0).rgb;
    vec3 before = textureLod(u_before, in_rectangle() ? tap(from * pc.size.xy) / pc.size.xy : from, 0.0).rgb;
    o_color = vec4(abs(now - before) / (max(now, before) + 0.05), 1.0);
}
