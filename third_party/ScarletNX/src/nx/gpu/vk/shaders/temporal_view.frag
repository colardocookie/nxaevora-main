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
} pc;
void main() {
    vec2 uv = gl_FragCoord.xy / pc.size.xy;
    vec2 motion = texelFetch(u_motion, ivec2(gl_FragCoord.xy), 0).rg;
    if (pc.size.z < 1.5) {
        o_color = vec4(abs(motion) * pc.size.w, (motion.x < 0.0 || motion.y < 0.0) ? 0.25 : 0.0, 1.0);
        return;
    }
    vec2 from = uv - motion;
    if (from.x < 0.0 || from.y < 0.0 || from.x > 1.0 || from.y > 1.0) {
        o_color = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    o_color = vec4(abs(texelFetch(u_now, ivec2(gl_FragCoord.xy), 0).rgb - textureLod(u_before, from, 0.0).rgb), 1.0);
}
