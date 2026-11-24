// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// DL2 point 3: the vectors in two channels for an adapter that takes them so (DLSS). Red and green of the motion
// image, the same pixel by integer texel addressing: no filter, no flip, no change of the jitter. The motion image
// itself is left as it is.
#version 450
layout(location = 0) out vec2 o_motion;
layout(set = 0, binding = 0) uniform sampler2D u_motion;
void main() { o_motion = texelFetch(u_motion, ivec2(gl_FragCoord.xy), 0).rg; }
