// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the ScarletNX contributors
// The Vita's YUV420 texture shader's body (src/vita/gpu/yuv.glsl) run on the CPU through tests/glsl/glsl.hpp against
// the conversion written out per pixel: random YUV420P2 and P3 textures in both chroma orders, the BT.601 and BT.709
// matrices, strides past the width, the planes at offsets in the source.
#include <cstdio>
#include <cstring>
#include <vector>
#include "glsl.hpp"

namespace vita_yuv_shader {
using namespace glsl;
struct Converter : Invocation {
    uint pixels, width, height, y_at, c_at, stride, packed, first_v, rv, gu, gv, bu;
    const leuint* src;
    leuint* dst;
#include "vita/gpu/yuv.glsl"
};
} // namespace vita_yuv_shader
using vita_yuv_shader::Converter;

static uint32_t rng = 0x1b873593;
static uint32_t random_word() {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}
static int clamp_byte(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }
static unsigned failures;
static void one(uint32_t width, uint32_t height, uint32_t stride, bool packed, bool first_v, bool bt709) {
    const int m[2][4] = {{409, 100, 208, 516}, {459, 55, 136, 541}}; // rv, gu, gv, bu
    const int* k = m[bt709];
    uint32_t y_at = random_word() % 9, ch = height / 2, c_at = y_at + stride * height;
    std::vector<uint8_t> bytes(c_at + stride * ch + 8);
    for (auto& b : bytes) b = (uint8_t)random_word();
    std::vector<glsl::leuint> src((bytes.size() + 3) / 4);
    std::memcpy(src.data(), bytes.data(), bytes.size());
    std::vector<glsl::leuint> dst(width * height);
    glsl::dispatch<Converter>(glsl::uvec3((width * height + 63) / 64, 1, 1), glsl::uvec3(64, 1, 1), [&](Converter& c) {
        c.pixels = width * height, c.width = width, c.height = height, c.y_at = y_at, c.c_at = c_at, c.stride = stride;
        c.packed = packed, c.first_v = first_v, c.rv = k[0], c.gu = k[1], c.gv = k[2], c.bu = k[3];
        c.src = src.data(), c.dst = dst.data();
    });
    for (uint32_t y = 0; y < height; y++)
        for (uint32_t x = 0; x < width; x++) {
            int u, v;
            if (packed) {
                const uint8_t* pair = &bytes[c_at + (y / 2) * stride + (x / 2) * 2];
                u = pair[first_v ? 1 : 0], v = pair[first_v ? 0 : 1];
            } else {
                uint32_t cs = stride / 2, plane = cs * ch, at = c_at + (y / 2) * cs + x / 2;
                u = bytes[at + (first_v ? plane : 0)], v = bytes[at + (first_v ? 0 : plane)];
            }
            int c = 298 * (bytes[y_at + y * stride + x] - 16), d = u - 128, e = v - 128;
            uint32_t want = (uint32_t)clamp_byte((c + k[0] * e + 128) >> 8) |
                            (uint32_t)clamp_byte((c - k[1] * d - k[2] * e + 128) >> 8) << 8 |
                            (uint32_t)clamp_byte((c + k[3] * d + 128) >> 8) << 16 | 0xff000000u;
            uint32_t got = dst[y * width + x];
            if (got != want && failures++ < 10)
                std::printf("%ux%u %s %s %s: (%u, %u) is %08x, not %08x\n", width, height, packed ? "P2" : "P3",
                            first_v ? "VU" : "UV", bt709 ? "709" : "601", x, y, got, want);
        }
}

int main() {
    for (int i = 0; i < 300; i++) {
        uint32_t width = 2 * (1 + random_word() % 64), height = 2 * (1 + random_word() % 48);
        uint32_t stride = (width + 7) / 8 * 8 + 8 * (random_word() % 3);
        one(width, height, stride, random_word() % 2, random_word() % 2, random_word() % 2);
    }
    if (failures) {
        std::printf("vita_yuv_glsl: %u failures\n", failures);
        return 1;
    }
    std::puts("vita_yuv_glsl: the shader converts every pixel as the fixed-point matrix does");
    return 0;
}
