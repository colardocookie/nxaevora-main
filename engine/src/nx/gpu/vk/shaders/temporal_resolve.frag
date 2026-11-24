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
    vec4 depth;     // depth translate, depth scale, options, 1 when there is no usable history
                    // options: 1 Catmull-Rom history, 2 variance clip, 4 weight by motion and clip,
                    // 8 the vector of the nearest depth around, 16 blend by brightness
    vec4 size;      // image width, height, view, weight of the frame in the blend
    vec4 jitter;    // this frame's jitter in clip units over w, then 1 when the scene is a rectangle of the image
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
const float CLASS_REJECTED = 2.0; // TEMPORAL_CLASS_REJECTED of temporal_math.h, in the motion image's alpha
vec3 to_ycocg(vec3 c) {
    return vec3(dot(c, vec3(0.25, 0.5, 0.25)), dot(c, vec3(0.5, 0.0, -0.5)), dot(c, vec3(-0.25, 0.5, -0.25)));
}
vec3 from_ycocg(vec3 c) { return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z); }
// The history through a Catmull-Rom filter in five linear fetches (the four corners left out), which keeps it
// sharper under motion than one linear fetch. The weights are temporal_catmull_weights in temporal_math.h.
vec3 history_sharp(vec2 uv) {
    vec2 position = uv * pc.size.xy;
    vec2 centre = floor(position - 0.5) + 0.5;
    vec2 f = position - centre, f2 = f * f, f3 = f2 * f;
    vec2 w0 = -0.5 * f3 + f2 - 0.5 * f;
    vec2 w1 = 1.5 * f3 - 2.5 * f2 + 1.0;
    vec2 w2 = -1.5 * f3 + 2.0 * f2 + 0.5 * f;
    vec2 w3 = 0.5 * f3 - 0.5 * f2;
    vec2 w12 = w1 + w2;
    vec2 t0 = tap(centre - 1.0) / pc.size.xy, t3 = tap(centre + 2.0) / pc.size.xy;
    vec2 t12 = tap(centre + w2 / w12) / pc.size.xy;
    vec4 sum = vec4(textureLod(u_history, vec2(t12.x, t0.y), 0.0).rgb, 1.0) * (w12.x * w0.y) +
               vec4(textureLod(u_history, vec2(t0.x, t12.y), 0.0).rgb, 1.0) * (w0.x * w12.y) +
               vec4(textureLod(u_history, t12, 0.0).rgb, 1.0) * (w12.x * w12.y) +
               vec4(textureLod(u_history, vec2(t3.x, t12.y), 0.0).rgb, 1.0) * (w3.x * w12.y) +
               vec4(textureLod(u_history, vec2(t12.x, t3.y), 0.0).rgb, 1.0) * (w12.x * w3.y);
    return max(sum.rgb / sum.a, vec3(0.0));
}
// A colour outside the box moved along the line toward the frame's colour until it is on the box
// (temporal_clip_toward in temporal_math.h)
vec3 clip_toward(vec3 colour, vec3 frame, vec3 low, vec3 high) {
    vec3 inside = clamp(frame, low, high), away = colour - inside;
    float t = 1.0;
    for (int i = 0; i < 3; i++) {
        if (away[i] > 0.000001) t = min(t, (high[i] - inside[i]) / away[i]);
        if (away[i] < -0.000001) t = min(t, (low[i] - inside[i]) / away[i]);
    }
    return inside + away * t;
}
void main() {
    int options = int(pc.depth.z + 0.5);
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    ivec2 first = first_pixel(), last = last_pixel();
    vec2 uv = gl_FragCoord.xy / pc.size.xy;
    vec3 now = texelFetch(u_now, pixel, 0).rgb;
    float z = max((texelFetch(u_depth, pixel, 0).r - pc.depth.x) / pc.depth.y, 0.0);
    vec4 vector = texelFetch(u_motion, pixel, 0);
    bool rejected = vector.a > CLASS_REJECTED - 0.5;
    vec2 motion = vector.rg;
    // the surface the vector belongs to: this pixel's, or with option 8 the nearest one's around it
    vec2 surface = gl_FragCoord.xy;
    float surface_z = z;
    if ((options & 8) != 0) {
        // the vector of the nearest surface in the 3x3 (reversed depth: the largest), so an edge's own pixels and
        // the background pixels beside it follow the thing in front
        float nearest = texelFetch(u_depth, pixel, 0).r;
        for (int y = -1; y <= 1; y++)
            for (int x = -1; x <= 1; x++) {
                ivec2 at = clamp(pixel + ivec2(x, y), first, last);
                float d = texelFetch(u_depth, at, 0).r;
                if (d > nearest) {
                    nearest = d;
                    vector = texelFetch(u_motion, at, 0);
                    motion = vector.rg;
                    surface = vec2(at) + 0.5;
                    surface_z = max((d - pc.depth.x) / pc.depth.y, 0.0);
                }
            }
    }
    // A rejected object pixel, this one or the one whose vector it would take: the frame as drawn, before any
    // history is fetched, and marked as dropped in the new history (temporal_current_only in temporal_math.h)
    if (rejected || vector.a > CLASS_REJECTED - 0.5) {
        o_history = vec4(max(now, vec3(0.0)), -max(z, 0.0000001));
        o_color = pc.size.z > 2.5 && pc.size.z < 3.5 ? vec4(1.0) : vec4(max(now, vec3(0.0)), 1.0);
        return;
    }
    vec2 from = uv - motion;
    // the depth that surface's point had in the frame before: what the history must hold at the place fetched
    vec2 ndc = (surface - pc.viewport.xy) / pc.viewport.zw * 2.0 - 1.0 - pc.jitter.xy; // drawn with the jitter
    vec4 before = pc.reproject * vec4(ndc, surface_z, 1.0);
    // The history's centre is tested before anything is fetched. With a rectangle a centre outside it fetches
    // nothing at all, and the pixel shows the frame.
    bool centre_out = outside(from);
    bool dropped = pc.depth.w > 0.5 || before.w <= 0.0 || centre_out;
    vec2 linear_at = in_rectangle() ? tap(from * pc.size.xy) / pc.size.xy : from;
    vec3 history = now;
    if (!in_rectangle() || !centre_out)
        history = (options & 1) != 0 ? history_sharp(from) : textureLod(u_history, linear_at, 0.0).rgb;
    if (!dropped) {
        // The history's depths around the place, each read whole: the sign of alpha is a mark, and an edge moves by
        // the jitter, so the depth must only lie within what the 3x3 texels there held, a tenth wider.
        // an object's vector comes with the depth its point had a frame ago (the camera's reprojection does not
        // know the object moved)
        float expected = vector.a > 0.5 ? vector.b : max(before.z / before.w, 0.0), nearest = 1e9, farthest = 0.0;
        ivec2 place = clamp(ivec2(from * pc.size.xy), first, last);
        for (int y = -1; y <= 1; y++)
            for (int x = -1; x <= 1; x++) {
                float had = abs(texelFetch(u_history, clamp(place + ivec2(x, y), first, last), 0).a);
                nearest = min(nearest, had);
                farthest = max(farthest, had);
            }
        dropped = expected < nearest * 0.9 - 0.00001 || expected > farthest * 1.1 + 0.00001;
    }
    // the frame's neighbourhood bounds the history
    vec3 low = to_ycocg(now), high = low, sum = low, squares = low * low;
    for (int y = -1; y <= 1; y++)
        for (int x = -1; x <= 1; x++) {
            if (x == 0 && y == 0) continue;
            vec3 c = to_ycocg(texelFetch(u_now, clamp(pixel + ivec2(x, y), first, last), 0).rgb);
            low = min(low, c);
            high = max(high, c);
            sum += c;
            squares += c * c;
        }
    vec3 kept;
    if ((options & 2) != 0) {
        // the box of the neighbourhood's mean and 1.25 standard deviations, the history clipped toward its middle
        vec3 mean = sum / 9.0, deviation = sqrt(max(squares / 9.0 - mean * mean, vec3(0.0)));
        kept = from_ycocg(
            clip_toward(to_ycocg(history), to_ycocg(now), mean - 1.25 * deviation, mean + 1.25 * deviation));
    } else
        kept = from_ycocg(clamp(to_ycocg(history), low, high));
    kept = max(kept, vec3(0.0));
    float weight = pc.size.w;
    if ((options & 4) != 0) {
        // 0.05 standing, 0.1 moving, up to 0.2 where the clip changed the history's brightness
        // (temporal_frame_weight in temporal_math.h)
        float before = dot(history, vec3(0.25, 0.5, 0.25)), after = dot(kept, vec3(0.25, 0.5, 0.25));
        float clipped = abs(before - after) / (max(before, after) + 0.05);
        float base = 0.05 + 0.05 * clamp(length(motion * pc.size.xy) / 2.0, 0.0, 1.0);
        weight = base + (0.2 - base) * clamp(clipped * 2.0, 0.0, 1.0);
    }
    if (dropped) weight = 1.0;
    vec3 result;
    if ((options & 16) != 0) {
        // each colour counts by 1 / (1 + its brightness), so a bright spark weighs no more than its neighbours
        // (temporal_blend_by_brightness in temporal_math.h)
        float wh = (1.0 - weight) / (1.0 + dot(kept, vec3(0.25, 0.5, 0.25)));
        float wf = weight / (1.0 + dot(now, vec3(0.25, 0.5, 0.25)));
        result = (kept * wh + now * wf) / (wh + wf);
    } else
        result = mix(kept, now, weight);
    result = max(result, vec3(0.0));
    o_history = vec4(result, dropped ? -max(z, 0.0000001) : z);
    o_color = pc.size.z > 2.5 && pc.size.z < 3.5 ? vec4(vec3(weight), 1.0) : vec4(result, 1.0);
}
