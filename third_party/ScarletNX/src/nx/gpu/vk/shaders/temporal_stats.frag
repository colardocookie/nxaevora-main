// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// The temporal probe's numbers (gpu/vk/temporal.c): a small image whose texels each sample one pixel of the scene.
// Red: the relative colour error against last frame fetched through the motion vector. Green: the same without the
// vector. Blue: the vector's length in pixels. Alpha: the pixel's depth as the game's projection gives it (1 near,
// 0 far), -1 for the far plane, -2 where last frame's pixel is off screen.
// With anti-aliasing on (depth.z is 1) u_before is the resolved frame: red is 1 where the history was dropped, green
// how far the resolved colour is from the frame's own.
// The image is two such halves side by side. The right half holds, for the same pixels: with anti-aliasing on, red
// the gradient length of the frame as drawn and green that of the picture as shown (their ratio is how sharp the
// shown picture is). In the probe with object vectors, red is the number of the draw that left the pixel's vector.
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
    vec4 jitter; // this frame's jitter in clip units over w, then 1 when the scene is a rectangle of the image
} pc;
layout(set = 0, binding = 3) uniform sampler2D u_depth;
layout(set = 0, binding = 4) uniform sampler2D u_last; // the frame before, as it was drawn
layout(set = 0, binding = 5) uniform sampler2D u_shown; // the resolved picture of the frame before
layout(set = 0, binding = 6) uniform sampler2D u_final; // the picture as shown this frame, after the sharpen
const ivec2 SAMPLES = ivec2(96, 54); // one half of the image: STATS_W x STATS_H of temporal.c
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
float relative(vec3 a, vec3 b) {
    vec3 e = abs(a - b) / (max(a, b) + 0.05);
    return max(e.r, max(e.g, e.b));
}
// brightness brought under 1, so a few very bright pixels do not carry the sum
float tone(sampler2D s, ivec2 pixel) {
    float l = dot(texelFetch(s, clamp(pixel, first_pixel(), last_pixel()), 0).rgb, vec3(0.2126, 0.7152, 0.0722));
    return l / (1.0 + l);
}
float gradient(sampler2D s, ivec2 pixel) {
    return 0.5 * length(vec2(tone(s, pixel + ivec2(1, 0)) - tone(s, pixel - ivec2(1, 0)),
                             tone(s, pixel + ivec2(0, 1)) - tone(s, pixel - ivec2(0, 1))));
}
void main() {
    // Whole numbers only, so both halves sample the very same pixel: the sample's index in its half, then the
    // pixel at the middle of its cell of the scene, which with a rectangle is that alone (temporal_rect_sample).
    ivec2 texel = ivec2(gl_FragCoord.xy);
    ivec2 sample_at = ivec2(texel.x % SAMPLES.x, texel.y);
    ivec2 pixel = ((2 * sample_at + 1) * ivec2(pc.size.xy)) / (2 * SAMPLES);
    if (in_rectangle())
        pixel = first_pixel() + ((2 * sample_at + 1) * (last_pixel() - first_pixel() + 1)) / (2 * SAMPLES);
    if (texel.x >= SAMPLES.x) {
        o_stats = vec4(0.0);
        if (pc.depth.z > 0.5) o_stats.rg = vec2(gradient(u_now, pixel), gradient(u_final, pixel));
        else if (pc.depth.w > 0.5) {
            vec4 object = texelFetch(u_shown, pixel, 0);
            float stored = texelFetch(u_depth, pixel, 0).r;
            if (object.b >= 0.0 && abs(object.b - stored) <= 0.002 * stored + 0.0000002) o_stats.r = object.a;
        }
        // A rejected object pixel (alpha 2 of the motion image): blue is how far its vector is from the camera's
        // in pixels, as temporal_motion.frag held it against the bound, 60000 without a finite one.
        vec4 vector = texelFetch(u_motion, pixel, 0);
        if (vector.a > 1.5) {
            float z = max((texelFetch(u_depth, pixel, 0).r - pc.depth.x) / pc.depth.y, 0.0);
            vec2 ndc = (vec2(pixel) + 0.5 - pc.viewport.xy) / pc.viewport.zw * 2.0 - 1.0 - pc.jitter.xy;
            vec4 before = pc.reproject * vec4(ndc, z, 1.0);
            o_stats.b = 60000.0;
            if (before.w > 0.0) {
                vec2 camera = (ndc - before.xy / before.w) * 0.5 * pc.viewport.zw / pc.size.xy;
                float apart = length((vector.rg - camera) * pc.size.xy);
                if (!isnan(apart) && !isinf(apart)) o_stats.b = clamp(apart, 1.0, 59000.0);
            }
        }
        return;
    }
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
        // 4000 more where a skinned character's extra draw missed the scene depth by a little (blue -2)
        if (object.b < -1.5) o_stats.b += 4000.0;
        if (object.b >= 0.0 && abs(object.b - stored) <= 0.002 * stored + 0.0000002) {
            vec2 ndc = (vec2(pixel) + 0.5 - pc.viewport.xy) / pc.viewport.zw * 2.0 - 1.0;
            vec4 before = pc.reproject * vec4(ndc, max(z, 0.0), 1.0);
            float camera = 1.0;
            if (before.w > 0.0) {
                vec2 was = (before.xy / before.w * 0.5 + 0.5) * pc.viewport.zw + pc.viewport.xy;
                vec2 was_at = in_rectangle() ? tap(was) / pc.size.xy : was / pc.size.xy;
                camera = relative(now, textureLod(u_before, was_at, 0.0).rgb);
            }
            o_stats.b += 1000.0 + (camera > 0.25 ? 2000.0 : 0.0);
        }
    }
    if (z <= 0.0) {
        o_stats.a = -1.0;
        return;
    }
    if (outside(from)) {
        o_stats.a = -2.0;
        return;
    }
    vec2 from_at = in_rectangle() ? tap(from * pc.size.xy) / pc.size.xy : from;
    o_stats.r = relative(now, textureLod(u_before, from_at, 0.0).rgb);
}
