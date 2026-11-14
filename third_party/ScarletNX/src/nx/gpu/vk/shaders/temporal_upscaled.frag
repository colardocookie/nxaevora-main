// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// The write-back after an upscaler (SNX_ARCEUS_UPSCALER). The library writes its picture into a private 16 bit float
// image of the scene's size. This pass writes that picture into the scene colour itself, in the scene colour's own
// format, texel for texel: one fetch, no filter, no neighbour. A negative value and one that is no number become 0,
// since the scene colour's format has no sign. temporal_upscaled in temporal_math.h is the whole of it.
#version 450
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = 2) uniform sampler2D u_upscaled;
float kept(float c) { return isnan(c) ? 0.0 : max(c, 0.0); }
void main() {
    vec3 c = texelFetch(u_upscaled, ivec2(gl_FragCoord.xy), 0).rgb;
    o_color = vec4(kept(c.r), kept(c.g), kept(c.b), 1.0);
}
