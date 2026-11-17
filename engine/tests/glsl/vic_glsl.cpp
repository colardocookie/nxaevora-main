// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the ScarletNX contributors
// The VIC compute shader's body (src/nx/gpu/video/vic_compose.glsl) run on the CPU through tests/glsl/glsl.hpp, both
// builds of it: tests/glsl/vic_jobs.c makes the jobs and compares what this composes with vic.c's compose_cpu.
#include <cstdint>
#include <cstring>
#include "glsl.hpp"

namespace vic_shader {
using namespace glsl;
#include "nx/gpu/video/vic_params.glsl"
static_assert(sizeof(VicGpuPlane) == 32 && sizeof(VicGpuSlot) == 176 && sizeof(VicGpuJob) == 128,
              "vic_params.glsl's structs are not std140's");
// what vic.comp declares, under its names
struct Composer : Invocation {
    VicGpuJob job;
    const VicGpuSlot* slots;
    const leuint* inputs;
    leuint* outputs;
#include "nx/gpu/video/vic_compose.glsl"
};
#define VIC_UNALIGNED
struct ComposerUnaligned : Invocation {
    VicGpuJob job;
    const VicGpuSlot* slots;
    const leuint* inputs;
    leuint* outputs;
#include "nx/gpu/video/vic_compose.glsl"
};
#undef VIC_UNALIGNED

// the uniform buffer as the device reads it: 32-bit words, all of its members (vic_params.glsl), little-endian
template <class T> T uniform(const char* at) {
    T v;
    uint words[sizeof v / 4];
    std::memcpy(words, at, sizeof words);
    for (uint& w : words) w = le_order(w);
    std::memcpy(&v, words, sizeof v);
    return v;
}
template <class Shader> void run(const void* params, const uint* inputs, uint* outputs) {
    const char* at = static_cast<const char*>(params);
    const VicGpuJob job = uniform<VicGpuJob>(at);
    VicGpuSlot slots[8];
    for (int i = 0; i < 8; i++) slots[i] = uniform<VicGpuSlot>(at + sizeof job + i * sizeof *slots);
    // the dispatch gpu/vk/vic.c makes: the target rectangle's tiles
    uvec3 groups((job.tx1 - job.gx0 + VIC_TILE_W - 1) / VIC_TILE_W,
                 (job.ty0 + job.th - job.gy0 + VIC_TILE_H - 1) / VIC_TILE_H, 1u);
    dispatch<Shader>(groups, uvec3(VIC_GROUP_W, VIC_GROUP_H, 1u), [&](Shader& s) {
        s.job = job;
        s.slots = slots;
        s.inputs = reinterpret_cast<const leuint*>(inputs);
        s.outputs = reinterpret_cast<leuint*>(outputs);
    });
}
} // namespace vic_shader

// params: a VicGpuParams as gpu/vk/vic.c uploads it, little-endian; inputs and outputs: the two storage buffers, as
// gpu/vk/vic.c binds them
extern "C" void vic_glsl_run(const void* params, const uint32_t* inputs, uint32_t* outputs, int unaligned) {
    if (unaligned)
        vic_shader::run<vic_shader::ComposerUnaligned>(params, inputs, outputs);
    else
        vic_shader::run<vic_shader::Composer>(params, inputs, outputs);
}
