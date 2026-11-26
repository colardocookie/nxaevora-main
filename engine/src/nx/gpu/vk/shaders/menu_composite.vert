// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// The full-screen triangle of the engine menu's composite (gpu/vk/imgui.c). It has no vertex buffer: gl_VertexIndex
// makes the three corners, and the image's top row is the top of the picture.
#version 450
layout(location = 0) out vec2 v_uv;
void main() {
    vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    // Vulkan's image row 0 is the top: the screen's top gets v = 0.
    v_uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
