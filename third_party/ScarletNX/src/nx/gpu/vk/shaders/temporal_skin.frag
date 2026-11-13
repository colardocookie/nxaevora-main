// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// The fragment side of temporal_skin.vert. Our vertex program is not the game's, so its depth is not bit for bit
// the game's: a fragment counts when its depth is within 0.3 % of the scene's depth there, which is sampled, not
// tested. The output is the object vector target's: the vector, the scene's depth in blue, the point's depth a
// frame ago in alpha.
#version 450
layout(location = 0) in vec4 v_now;
layout(location = 1) in vec4 v_before;
layout(location = 0) out vec4 o_vector;
layout(set = 1, binding = 0) uniform Block {
    mat4 drawn;
    mat4 now;
    mat4 before;
    vec4 viewport;
    vec4 depth;
    vec4 size;
} u;
layout(set = 1, binding = 34) uniform sampler2D u_depth; // the fragment set's first texture binding (SNX_VKB_TEX)
void main() {
    float stored = texelFetch(u_depth, ivec2(gl_FragCoord.xy), 0).r;
    if (abs(gl_FragCoord.z - stored) > 0.003 * stored + 0.0000002) discard;
    if (v_now.w <= 0.0 || v_before.w <= 0.0) discard;
    vec2 is = (v_now.xy / v_now.w * 0.5 + 0.5) * u.viewport.zw + u.viewport.xy;
    vec2 was = (v_before.xy / v_before.w * 0.5 + 0.5) * u.viewport.zw + u.viewport.xy;
    o_vector = vec4((is - was) / u.size.xy, stored, max(v_before.z / v_before.w, 0.0));
}
