// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// Camera motion vectors (gpu/vk/temporal.c): a pixel's depth through this frame's inverse view-projection and last
// frame's view-projection, both folded into one matrix. The result is this pixel's uv minus the uv the same world
// point had in the frame before, zero behind the previous camera. The far plane moves as the camera turns.
#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec2 o_motion;
layout(set = 0, binding = 0) uniform sampler2D u_depth;
layout(set = 0, binding = 1) uniform sampler2D u_unused1;
layout(set = 0, binding = 2) uniform sampler2D u_unused2;
layout(push_constant) uniform Constants {
    mat4 reproject; // clip of this frame to clip of the frame before
    vec4 viewport;  // x, y, width, height of the scene's viewport in image pixels (height may be negative)
    vec4 depth;     // the viewport's depth translate and scale: stored depth = translate + scale x z, both depth modes
    vec4 size;      // image width, height, view, gain
} pc;
void main() {
    float stored = texelFetch(u_depth, ivec2(gl_FragCoord.xy), 0).r;
    float z = max((stored - pc.depth.x) / pc.depth.y, 0.0); // reversed depth: 0 is the far plane, a point at infinity
    o_motion = vec2(0.0);
    vec2 ndc = (gl_FragCoord.xy - pc.viewport.xy) / pc.viewport.zw * 2.0 - 1.0;
    vec4 before = pc.reproject * vec4(ndc, z, 1.0);
    if (before.w <= 0.0) return;
    vec2 pixel = (before.xy / before.w * 0.5 + 0.5) * pc.viewport.zw + pc.viewport.xy;
    o_motion = (gl_FragCoord.xy - pixel) / pc.size.xy;
}
