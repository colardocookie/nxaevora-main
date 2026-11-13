// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// Temporal anti-aliasing at the scene's size, in linear HDR (gpu/vk/temporal.c). The frame was drawn with a sub-pixel
// jitter. Its colour is blended with our history, fetched through the camera motion vector, clamped to the frame's
// 3x3 neighbourhood in YCoCg and dropped where the depth this pixel's point had a frame ago is outside the depths the
// history held around that place.
// Output 0 is the scene colour, output 1 the new history with the pixel's depth in alpha (negative when dropped).
#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(location = 1) out vec4 o_history;
layout(set = 0, binding = 0) uniform sampler2D u_motion;
layout(set = 0, binding = 1) uniform sampler2D u_now;
layout(set = 0, binding = 2) uniform sampler2D u_history;
layout(set = 0, binding = 3) uniform sampler2D u_depth;
layout(push_constant) uniform Constants {
    mat4 reproject; // clip of this frame to clip of the frame before, without jitter
    vec4 viewport;  // x, y, width, height of the scene's viewport in image pixels
    vec4 depth;     // depth translate, depth scale, unused, 1 when there is no usable history
    vec4 size;      // image width, height, view, weight of the frame in the blend
} pc;
vec3 to_ycocg(vec3 c) {
    return vec3(dot(c, vec3(0.25, 0.5, 0.25)), dot(c, vec3(0.5, 0.0, -0.5)), dot(c, vec3(-0.25, 0.5, -0.25)));
}
vec3 from_ycocg(vec3 c) { return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z); }
void main() {
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    ivec2 last = ivec2(pc.size.xy) - 1;
    vec2 uv = gl_FragCoord.xy / pc.size.xy;
    vec3 now = texelFetch(u_now, pixel, 0).rgb;
    float z = max((texelFetch(u_depth, pixel, 0).r - pc.depth.x) / pc.depth.y, 0.0);
    vec2 from = uv - texelFetch(u_motion, pixel, 0).rg;
    // the depth this pixel's point had in the frame before
    vec2 ndc = (gl_FragCoord.xy - pc.viewport.xy) / pc.viewport.zw * 2.0 - 1.0;
    vec4 before = pc.reproject * vec4(ndc, z, 1.0);
    bool dropped = pc.depth.w > 0.5 || before.w <= 0.0 || from.x < 0.0 || from.y < 0.0 || from.x > 1.0 || from.y > 1.0;
    vec4 history = textureLod(u_history, from, 0.0);
    if (!dropped) {
        // The history's depths around the place, each read whole: the sign of alpha is a mark, and an edge moves by
        // the jitter, so the depth must only lie within what the 3x3 texels there held, a tenth wider.
        float expected = max(before.z / before.w, 0.0), nearest = 1e9, farthest = 0.0;
        ivec2 place = clamp(ivec2(from * pc.size.xy), ivec2(0), last);
        for (int y = -1; y <= 1; y++)
            for (int x = -1; x <= 1; x++) {
                float had = abs(texelFetch(u_history, clamp(place + ivec2(x, y), ivec2(0), last), 0).a);
                nearest = min(nearest, had);
                farthest = max(farthest, had);
            }
        dropped = expected < nearest * 0.9 - 0.00001 || expected > farthest * 1.1 + 0.00001;
    }
    // the frame's neighbourhood bounds the history
    vec3 low = to_ycocg(now), high = low;
    for (int y = -1; y <= 1; y++)
        for (int x = -1; x <= 1; x++) {
            if (x == 0 && y == 0) continue;
            vec3 c = to_ycocg(texelFetch(u_now, clamp(pixel + ivec2(x, y), ivec2(0), last), 0).rgb);
            low = min(low, c);
            high = max(high, c);
        }
    vec3 kept = from_ycocg(clamp(to_ycocg(history.rgb), low, high));
    float weight = dropped ? 1.0 : pc.size.w;
    vec3 result = max(mix(kept, now, weight), vec3(0.0));
    o_history = vec4(result, dropped ? -max(z, 0.0000001) : z);
    o_color = pc.size.z > 2.5 && pc.size.z < 3.5 ? vec4(vec3(weight), 1.0) : vec4(result, 1.0);
}
