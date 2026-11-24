// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// A light sharpen after the temporal resolve (SNX_ARCEUS_TEMPORAL_SHARPEN). It reads the resolved picture from the
// history image and writes the scene colour only, so the sharpening does not build up from frame to frame. It falls
// where the neighbourhood already has contrast and stays within each channel's 3x3 least and greatest.
// temporal_sharpen_rgb in temporal_math.h is the whole of it.
#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = 2) uniform sampler2D u_resolved;
layout(push_constant) uniform Constants {
    mat4 reproject;
    vec4 viewport;
    vec4 depth;
    vec4 size; // image width, height, unused, the amount (0 to 1)
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
// HDR to 0..1 and back, so that the contrast measure means the same in dark and bright places
vec3 squeeze(vec3 c) { return c / (1.0 + max(c.r, max(c.g, c.b))); }
vec3 widen(vec3 c) { return c / max(1.0 - max(c.r, max(c.g, c.b)), 0.0001); }
void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    ivec2 first = first_pixel(), last = last_pixel();
    vec3 centre = max(texelFetch(u_resolved, pixel, 0).rgb, vec3(0.0));
    vec3 c = squeeze(centre);
    vec3 low = c, high = c, cross = vec3(0.0), least = centre, greatest = centre;
    for (int y = -1; y <= 1; y++)
        for (int x = -1; x <= 1; x++) {
            if (x == 0 && y == 0) continue;
            vec3 linear = max(texelFetch(u_resolved, clamp(pixel + ivec2(x, y), first, last), 0).rgb, vec3(0.0));
            vec3 n = squeeze(linear);
            least = min(least, linear);
            greatest = max(greatest, linear);
            low = min(low, n);
            high = max(high, n);
            if (x == 0 || y == 0) cross += n;
        }
    // room left to the ends of the range, against the brightest: little room, little sharpening
    vec3 room = clamp(min(low, 1.0 - high) / max(high, vec3(0.0001)), 0.0, 1.0);
    vec3 w = -sqrt(room) * 0.2 * pc.size.w;
    vec3 sharp = clamp((c + cross * w) / (1.0 + 4.0 * w), low, high);
    // The squeeze ties the channels together, so its bounds are not the picture's: the result is held to those too.
    o_color = vec4(clamp(widen(sharp), least, greatest), 1.0);
}
