// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// The write-back after an upscaler (SNX_ARCEUS_UPSCALER): the library's picture, in a private 16 bit float image, is
// written into the scene colour in its own format, texel for texel, with one fetch. A negative value and one that
// is no number become 0, and each channel is held to the largest finite value the scene colour's format holds:
// nothing of a library that is not finite gets in. Later passes of the game are promised nothing (temporal_math.h).
#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = 2) uniform sampler2D u_upscaled;
layout(push_constant) uniform Constants {
    mat4 reproject;
    vec4 viewport;
    vec4 depth; // here: the largest finite value of the scene colour's red, green and blue
    vec4 size;
    vec4 jitter;
} pc;
float kept(float c, float largest) { return isnan(c) ? 0.0 : min(max(c, 0.0), largest); }
void main() {
    vec3 c = texelFetch(u_upscaled, ivec2(gl_FragCoord.xy), 0).rgb;
    o_color = vec4(kept(c.r, pc.depth.r), kept(c.g, pc.depth.g), kept(c.b, pc.depth.b), 1.0);
}
