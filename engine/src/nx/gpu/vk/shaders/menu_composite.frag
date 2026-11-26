// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// The engine menu's panel over the picture (gpu/vk/imgui.c). The offscreen image holds the ImGui draw over a
// transparent clear: its blend leaves premultiplied color, so the panel alpha premultiplies it again and the
// pipeline blends ONE / ONE_MINUS_SRC_ALPHA. The image has the swapchain's format, so an sRGB pair needs no
// conversion here: the hardware decodes the sample and encodes the write.
#version 450
layout(set = 0, binding = 0) uniform sampler2D panel;
layout(push_constant) uniform Params {
    float alpha;
} pc;
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o;
void main() { o = texture(panel, v_uv) * pc.alpha; }
