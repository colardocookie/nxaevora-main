// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// The motion vector of a skinned character (gpu/vk/temporal.c). The game skins a character's vertices with transform
// feedback into a buffer of positions in the object's own space, and its G-buffer draw reads that buffer. That draw
// is drawn once more with this vertex program: the position now from the game's buffer, the position a frame ago
// from a copy kept of last frame's buffer, each through its own world and view-projection matrix.
#version 450
layout(location = 0) in vec3 a_now;
layout(location = 1) in vec3 a_before;
layout(location = 0) out vec4 v_now;
layout(location = 1) out vec4 v_before;
layout(set = 0, binding = 0) uniform Block {
    mat4 drawn;    // position to this frame's clip as the game draws it (with the jitter)
    mat4 now;      // position to this frame's clip without jitter
    mat4 before;   // last frame's position to last frame's clip without jitter
    vec4 viewport; // x, y, width, height of the scene's viewport in image pixels
    vec4 depth;    // depth translate, depth scale, 1 when clip z runs from -w to w, 1 to mark near misses
    vec4 size;     // image width, height
} u;
void main() {
    vec4 p = u.drawn * vec4(a_now, 1.0);
    if (u.depth.z > 0.5) p.z = (p.z + p.w) * 0.5; // as the translated programs do for that depth mode
    gl_Position = p;
    v_now = u.now * vec4(a_now, 1.0);
    v_before = u.before * vec4(a_before, 1.0);
}
