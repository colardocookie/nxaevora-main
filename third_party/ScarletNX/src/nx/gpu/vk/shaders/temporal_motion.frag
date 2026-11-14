// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// Camera motion vectors (gpu/vk/temporal.c): a pixel's depth through this frame's inverse view-projection and last
// frame's view-projection, both folded into one matrix. The result is this pixel's uv minus the uv the same world
// point had in the frame before, zero behind the previous camera. The far plane moves as the camera turns.
#version 450
layout(location = 0) in vec2 v_uv;
// Red and green: the vector. Alpha is its class (temporal_math.h): 0 the camera's, 1 an object's, and blue then the
// depth its point had a frame ago, 2 an object's that is further from the camera's vector than the bound: rejected.
layout(location = 0) out vec4 o_motion;
layout(set = 0, binding = 0) uniform sampler2D u_depth;
layout(set = 0, binding = 1) uniform sampler2D u_objects; // the vectors of objects that moved, their depth in blue
layout(set = 0, binding = 2) uniform sampler2D u_unused2;
layout(push_constant) uniform Constants {
    mat4 reproject; // clip of this frame to clip of the frame before
    vec4 viewport;  // x, y, width, height of the scene's viewport in image pixels (height may be negative)
    vec4 depth;     // the viewport's depth translate and scale (stored depth = translate + scale x z), the bound
                    // of an object's vector against the camera's in pixels (0 for none), 1 when u_objects was
                    // drawn this frame
    vec4 size;      // image width, height, view, gain
    vec4 jitter;    // this frame's jitter in clip units over w
} pc;
const float CLASS_OBJECT = 1.0, CLASS_REJECTED = 2.0; // TEMPORAL_CLASS_* of temporal_math.h
void main() {
    float stored = texelFetch(u_depth, ivec2(gl_FragCoord.xy), 0).r;
    float z = max((stored - pc.depth.x) / pc.depth.y, 0.0); // reversed depth: 0 is the far plane, a point at infinity
    o_motion = vec4(0.0);
    // The pixel was drawn with the jitter. Its point's place without it, now and a frame ago: both ends of the
    // vector are without jitter, as an object's are (temporal_camera_points in temporal_math.h).
    vec2 ndc = (gl_FragCoord.xy - pc.viewport.xy) / pc.viewport.zw * 2.0 - 1.0 - pc.jitter.xy;
    vec4 before = pc.reproject * vec4(ndc, z, 1.0);
    bool camera_valid = before.w > 0.0;
    vec2 camera = vec2(0.0);
    if (camera_valid) {
        vec2 is = (ndc * 0.5 + 0.5) * pc.viewport.zw + pc.viewport.xy;
        vec2 was = (before.xy / before.w * 0.5 + 0.5) * pc.viewport.zw + pc.viewport.xy;
        camera = (is - was) / pc.size.xy;
    }
    if (pc.depth.w > 0.5) {
        // an object's vector counts where the scene's depth is still the one it was drawn at (half float steps)
        vec4 object = texelFetch(u_objects, ivec2(gl_FragCoord.xy), 0);
        if (object.b >= 0.0 && abs(object.b - stored) <= 0.002 * stored + 0.0000002) {
            // what the object adds to the camera's vector, in pixels, against the bound (temporal_vector_class)
            vec2 own = (object.rg - camera) * pc.size.xy;
            bool rejected = pc.depth.z > 0.0 && (!camera_valid || dot(own, own) > pc.depth.z * pc.depth.z);
            o_motion = vec4(object.rg, object.a, rejected ? CLASS_REJECTED : CLASS_OBJECT);
            return;
        }
    }
    if (camera_valid) o_motion = vec4(camera, 0.0, 0.0);
}
