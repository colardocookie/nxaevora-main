// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// Camera motion vectors (gpu/vk/temporal.c): a pixel's depth through this frame's inverse view-projection and last
// frame's view-projection, both folded into one matrix. The result is this pixel's uv minus the uv the same world
// point had in the frame before, zero behind the previous camera. The far plane moves as the camera turns.
#version 450
layout(location = 0) in vec2 v_uv;
// Red and green: the vector. Alpha 1 where it is an object's, and blue then the depth its point had a frame ago.
layout(location = 0) out vec4 o_motion;
layout(set = 0, binding = 0) uniform sampler2D u_depth;
layout(set = 0, binding = 1) uniform sampler2D u_objects; // the vectors of objects that moved, their depth in blue
layout(set = 0, binding = 2) uniform sampler2D u_unused2;
layout(push_constant) uniform Constants {
    mat4 reproject; // clip of this frame to clip of the frame before
    vec4 viewport;  // x, y, width, height of the scene's viewport in image pixels (height may be negative)
    vec4 depth;     // the viewport's depth translate and scale (stored depth = translate + scale x z), unused,
                    // 1 when u_objects was drawn this frame
    vec4 size;      // image width, height, view, gain
    vec4 jitter;    // this frame's jitter in clip units over w
} pc;
void main() {
    float stored = texelFetch(u_depth, ivec2(gl_FragCoord.xy), 0).r;
    float z = max((stored - pc.depth.x) / pc.depth.y, 0.0); // reversed depth: 0 is the far plane, a point at infinity
    o_motion = vec4(0.0);
    if (pc.depth.w > 0.5) {
        // an object's vector counts where the scene's depth is still the one it was drawn at (half float steps)
        vec4 object = texelFetch(u_objects, ivec2(gl_FragCoord.xy), 0);
        if (object.b >= 0.0 && abs(object.b - stored) <= 0.002 * stored + 0.0000002) {
            o_motion = vec4(object.rg, object.a, 1.0);
            return;
        }
    }
    // The pixel was drawn with the jitter. Its point's place without it, now and a frame ago: both ends of the
    // vector are without jitter, as an object's are (temporal_camera_points in temporal_math.h).
    vec2 ndc = (gl_FragCoord.xy - pc.viewport.xy) / pc.viewport.zw * 2.0 - 1.0 - pc.jitter.xy;
    vec4 before = pc.reproject * vec4(ndc, z, 1.0);
    if (before.w <= 0.0) return;
    vec2 is = (ndc * 0.5 + 0.5) * pc.viewport.zw + pc.viewport.xy;
    vec2 was = (before.xy / before.w * 0.5 + 0.5) * pc.viewport.zw + pc.viewport.xy;
    o_motion = vec4((is - was) / pc.size.xy, 0.0, 0.0);
}
