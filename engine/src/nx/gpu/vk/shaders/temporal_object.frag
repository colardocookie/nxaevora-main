// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// The motion vector of an object that moved (gpu/vk/temporal.c). A G-buffer draw is drawn once more with the game's
// vertex program and this fragment program, onto the pixels the object owns. The pixel and its depth give the
// point, and one matrix per draw takes it to where it was a frame ago. Blue keeps the depth the vector belongs to:
// the camera vector pass takes the vector only where the scene's final depth is still that one. Alpha keeps the
// depth the point had a frame ago, which the resolve expects of the history there. The probe has no resolve and
// keeps the draw's number there, so its numbers can name the draw of a wrong vector.
#version 450
layout(location = 0) out vec4 o_vector;
layout(set = 1, binding = 0) uniform Block {
    mat4 before;   // this frame's clip, as drawn, to the frame before's clip without jitter
    mat4 now;      // this frame's clip, as drawn, to this frame's clip without jitter
    vec4 viewport; // x, y, width, height of the scene's viewport in image pixels
    vec4 depth;    // depth translate, depth scale
    vec4 size;     // image width, height, the draw's number in the probe (0 otherwise)
} u;
void main() {
    float z = (gl_FragCoord.z - u.depth.x) / u.depth.y;
    vec2 ndc = (gl_FragCoord.xy - u.viewport.xy) / u.viewport.zw * 2.0 - 1.0;
    vec4 before = u.before * vec4(ndc, z, 1.0);
    vec4 now = u.now * vec4(ndc, z, 1.0);
    o_vector = vec4(0.0, 0.0, -1.0, 0.0);
    if (before.w <= 0.0 || now.w <= 0.0) return;
    vec2 was = (before.xy / before.w * 0.5 + 0.5) * u.viewport.zw + u.viewport.xy;
    vec2 is = (now.xy / now.w * 0.5 + 0.5) * u.viewport.zw + u.viewport.xy;
    o_vector = vec4((is - was) / u.size.xy, gl_FragCoord.z,
                    u.size.z > 0.5 ? u.size.z : max(before.z / before.w, 0.0));
}
