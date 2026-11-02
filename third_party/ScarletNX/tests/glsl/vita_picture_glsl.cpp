// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the ScarletNX contributors
// The Vita's decoded picture layout shader's body (src/vita/gpu/picture.glsl) run on the CPU through tests/glsl/glsl.hpp
// against a byte by byte reference of what sceAvcdecDecode writes: Y rows at the pitch, then the chroma interleaved V
// first or U first, or as U's plane then V's at half the pitch, the bytes past the picture in a row zero. Random
// pictures of even sizes, pitches of any 16-byte multiple at least the width.
#include <cstdio>
#include <cstring>
#include <vector>
#include "glsl.hpp"

namespace vita_picture_shader {
using namespace glsl;
struct Layout : Invocation {
    uint words, luma, pitch, width, height, u_at, v_at, packed, vu;
    const leuint* src;
    leuint* dst;
#include "vita/gpu/picture.glsl"
};
} // namespace vita_picture_shader
using vita_picture_shader::Layout;

static uint32_t rng = 0x6c078965;
static uint32_t random_word() {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}
static unsigned failures;
static void one(uint32_t width, uint32_t height, uint32_t pitch, uint32_t packed, uint32_t vu) {
    uint32_t half = width / 2, ch = height / 2, y_bytes = width * height, c_bytes = half * ch;
    std::vector<uint8_t> planes(y_bytes + 2 * c_bytes + 4);
    for (auto& b : planes) b = (uint8_t)random_word();
    std::vector<glsl::leuint> src((planes.size() + 3) / 4);
    std::memcpy(src.data(), planes.data(), planes.size());
    uint32_t total = pitch * height * 3 / 2, words = total / 4;
    std::vector<glsl::leuint> dst(words + 1);
    for (auto& w : dst) w = random_word();
    uint32_t guard = dst[words];
    glsl::dispatch<Layout>(glsl::uvec3((words + 63) / 64, 1, 1), glsl::uvec3(64, 1, 1), [&](Layout& l) {
        l.words = words, l.luma = pitch * height, l.pitch = pitch, l.width = width, l.height = height;
        l.u_at = y_bytes, l.v_at = y_bytes + c_bytes, l.packed = packed, l.vu = vu;
        l.src = src.data(), l.dst = dst.data();
    });
    std::vector<uint8_t> want(total, 0);
    const uint8_t *Y = planes.data(), *U = Y + y_bytes, *V = U + c_bytes;
    for (uint32_t y = 0; y < height; y++)
        for (uint32_t x = 0; x < width; x++) want[y * pitch + x] = Y[y * width + x];
    uint8_t* c = want.data() + pitch * height;
    for (uint32_t y = 0; y < ch; y++)
        for (uint32_t x = 0; x < half; x++) {
            if (packed) {
                c[y * pitch + 2 * x] = vu ? V[y * half + x] : U[y * half + x];
                c[y * pitch + 2 * x + 1] = vu ? U[y * half + x] : V[y * half + x];
            } else {
                c[y * (pitch / 2) + x] = U[y * half + x];
                c[(pitch / 2) * ch + y * (pitch / 2) + x] = V[y * half + x];
            }
        }
    const uint8_t* got = (const uint8_t*)dst.data();
    for (uint32_t i = 0; i < total; i++)
        if (got[i] != want[i] && failures++ < 10)
            std::printf("%ux%u pitch %u %s: byte %u is %02x, not %02x\n", width, height, pitch,
                        packed ? (vu ? "VU" : "UV") : "planes", i, got[i], want[i]);
    if ((uint32_t)dst[words] != guard && failures++ < 10) std::printf("%ux%u: the word past it was written\n", width, height);
}

int main() {
    for (int i = 0; i < 300; i++) {
        uint32_t width = 2 * (1 + random_word() % 100), height = 2 * (1 + random_word() % 60);
        uint32_t pitch = (width + 15) / 16 * 16 + 16 * (random_word() % 3);
        one(width, height, pitch, random_word() % 2, random_word() % 2);
    }
    if (failures) {
        std::printf("vita_picture_glsl: %u failures\n", failures);
        return 1;
    }
    std::puts("vita_picture_glsl: the shader lays out every picture byte where the decoder's layout has it");
    return 0;
}
