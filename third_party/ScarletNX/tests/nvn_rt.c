/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The render target words of gpu/nvn_rt.c: every word of a run, by hand from the rules of the task brief. */
#include "nx/gpu/nvn_rt.h"
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
            failures++;                                                      \
        }                                                                    \
    } while (0)

enum { MOCK_MAX_WRITES = 512 };

typedef struct { uint32_t method, value; } MockWrite;

typedef struct {
    MockWrite writes[MOCK_MAX_WRITES];
    uint32_t write_count;
    uint32_t fault_count;
    const char* last_fault;
    uint64_t last_fault_value;
} Mock;

static void mock_method(void* ctx, uint32_t method, uint32_t value) {
    Mock* k = ctx;
    if (k->write_count < MOCK_MAX_WRITES)
        k->writes[k->write_count++] = (MockWrite){method, value};
}

static void mock_fault(void* ctx, const char* what, uint64_t value) {
    Mock* k = ctx;
    k->fault_count++;
    k->last_fault = what;
    k->last_fault_value = value;
}

static NvnRtEnv make_env(Mock* k) {
    NvnRtEnv env;
    memset(&env, 0, sizeof env);
    env.ctx = k;
    env.method = mock_method;
    env.fault = mock_fault;
    return env;
}

static void expect(const Mock* k, uint32_t index, uint32_t method, uint32_t value) {
    if (index >= k->write_count) {
        printf("FAIL write %u: missing\n", index);
        failures++;
        return;
    }
    if (k->writes[index].method != method || k->writes[index].value != value) {
        printf("FAIL write %u: actual %08x/%08x expected %08x/%08x\n", index,
               k->writes[index].method, k->writes[index].value, method, value);
        failures++;
    }
}

static int same_writes(const Mock* a, const Mock* b) {
    if (a->write_count != b->write_count) return 0;
    for (uint32_t i = 0; i < a->write_count; i++)
        if (a->writes[i].method != b->writes[i].method || a->writes[i].value != b->writes[i].value)
            return 0;
    return 1;
}

static int fault_is(const Mock* k, const char* what) {
    return k->fault_count == 1 && k->last_fault && strcmp(k->last_fault, what) == 0;
}

static NvnTexture make_texture_with(uint32_t format, uint32_t width, uint32_t height, uint64_t address,
                                    uint16_t tile, uint32_t samples) {
    NvnTexture t;
    memset(&t, 0, sizeof t);
    nvn_texture_builder_defaults(&t.builder);
    t.builder.format = format;
    t.builder.width = width;
    t.builder.height = height;
    t.builder.depth = 1;
    t.builder.tile = tile;
    t.builder.samples = samples;
    if (!nvn_texture_layout(&t.builder, &nvn_texture_device_words, &t.layout)) {
        printf("FAIL texture layout: format %08x %ux%u\n", format, width, height);
        failures++;
    }
    t.address = address;
    return t;
}

static NvnTexture make_texture(uint32_t format, uint32_t width, uint32_t height, uint64_t address) {
    return make_texture_with(format, width, height, address, 0u, 0u);
}

static NvnTexture make_linear_texture(uint32_t format, uint32_t width, uint32_t height, uint64_t address) {
    NvnTexture t;
    memset(&t, 0, sizeof t);
    nvn_texture_builder_defaults(&t.builder);
    t.builder.format = format;
    t.builder.width = width;
    t.builder.height = height;
    t.builder.depth = 1;
    t.builder.flags = NVN_TEXTURE_FLAG_LINEAR;
    if (!nvn_texture_layout(&t.builder, &nvn_texture_device_words, &t.layout)) {
        printf("FAIL linear texture layout: format %08x %ux%u\n", format, width, height);
        failures++;
    }
    t.address = address;
    return t;
}

static void test_one_target(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture t = make_texture(0x25, 1280, 720, 0x0000000412345600ull);
    NvnRtColor color[1] = {{&t, NULL}};
    CHECK(nvn_rt_emit(&env, color, 1, NULL, 0x4000) == 1);
    CHECK(k.fault_count == 0 && k.write_count == 21);
    expect(&k, 0, 0x487, 0x0FAC6881u);
    expect(&k, 1, 0x200, 0x00000004u);
    expect(&k, 2, 0x201, 0x12345600u);
    expect(&k, 3, 0x202, 1280u);
    expect(&k, 4, 0x203, 720u);
    expect(&k, 5, 0x204, 0x000000D5u);
    expect(&k, 6, 0x205, 0x00000040u);
    expect(&k, 7, 0x206, 0x00000001u);
    expect(&k, 8, 0x207, 0x000F0000u);
    expect(&k, 9, 0x214, 0u);
    expect(&k, 10, 0x224, 0u);
    expect(&k, 11, 0x234, 0u);
    expect(&k, 12, 0x244, 0u);
    expect(&k, 13, 0x254, 0u);
    expect(&k, 14, 0x264, 0u);
    expect(&k, 15, 0x274, 0u);
    expect(&k, 16, 0x54E, 0u);
    expect(&k, 17, 0xD28, 0u);
    expect(&k, 18, 0x3FD, 0x05000000u);
    expect(&k, 19, 0x3FE, 0x02D00000u);
    expect(&k, 20, 0x574, 0u);
}

static void test_null_slot_zero(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture t = make_texture(0x25, 1280, 720, 0x0000000123456789ull);
    NvnRtColor color[2] = {{NULL, NULL}, {&t, NULL}};
    CHECK(nvn_rt_emit(&env, color, 2, NULL, 0x4000) == 1);
    CHECK(k.fault_count == 0 && k.write_count == 21);
    expect(&k, 0, 0x487, 0x0FAC6882u);
    expect(&k, 1, 0x204, 0u);
    expect(&k, 2, 0x210, 0x00000001u);
    expect(&k, 3, 0x211, 0x23456789u);
    expect(&k, 4, 0x212, 1280u);
    expect(&k, 5, 0x213, 720u);
    expect(&k, 6, 0x214, 0x000000D5u);
    expect(&k, 7, 0x215, 0x00000040u);
    expect(&k, 8, 0x216, 0x00000001u);
    expect(&k, 9, 0x217, 0x000F0000u);
    expect(&k, 10, 0x224, 0u);
    expect(&k, 11, 0x234, 0u);
    expect(&k, 12, 0x244, 0u);
    expect(&k, 13, 0x254, 0u);
    expect(&k, 14, 0x264, 0u);
    expect(&k, 15, 0x274, 0u);
    expect(&k, 16, 0x54E, 0u);
    expect(&k, 17, 0xD28, 0u);
    expect(&k, 18, 0x3FD, 0x05000000u);
    expect(&k, 19, 0x3FE, 0x02D00000u);
    expect(&k, 20, 0x574, 0u);
}

static void test_no_target(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    CHECK(nvn_rt_emit(&env, NULL, 0, NULL, 0x4000) == 1);
    CHECK(k.fault_count == 0 && k.write_count == 14);
    expect(&k, 0, 0x487, 0x0FAC6880u);
    expect(&k, 1, 0x204, 0u);
    expect(&k, 2, 0x214, 0u);
    expect(&k, 3, 0x224, 0u);
    expect(&k, 4, 0x234, 0u);
    expect(&k, 5, 0x244, 0u);
    expect(&k, 6, 0x254, 0u);
    expect(&k, 7, 0x264, 0u);
    expect(&k, 8, 0x274, 0u);
    expect(&k, 9, 0x54E, 0u);
    expect(&k, 10, 0xD28, 0u);
    expect(&k, 11, 0x3FD, 0x40000000u);
    expect(&k, 12, 0x3FE, 0x40000000u);
    expect(&k, 13, 0x574, 0u);
}

static void test_clip_limit_zero(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture t = make_texture(0x25, 1280, 720, 0x1000);
    NvnRtColor color[1] = {{&t, NULL}};
    CHECK(nvn_rt_emit(&env, color, 1, NULL, 0) == 1);
    expect(&k, 18, 0x3FD, 0u);
    expect(&k, 19, 0x3FE, 0u);
}

static void test_width_align_up(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture t = make_texture(0x25, 100, 16, 0x1000);
    NvnRtColor color[1] = {{&t, NULL}};
    CHECK(nvn_rt_emit(&env, color, 1, NULL, 0x4000) == 1);
    CHECK(k.fault_count == 0 && k.write_count == 21);
    expect(&k, 1, 0x200, 0x00000000u);
    expect(&k, 2, 0x201, 0x00001000u);
    expect(&k, 3, 0x202, 112u);
    expect(&k, 4, 0x203, 16u);
    expect(&k, 5, 0x204, 0x000000D5u);
    expect(&k, 6, 0x205, 0x00000010u);
    expect(&k, 7, 0x206, 0x00000001u);
    expect(&k, 8, 0x207, 0x00000700u);
}

static void test_depth_only_d32f(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture d = make_texture(0x34, 1280, 720, 0x0000000212345600ull);
    NvnRtColor depth = {&d, NULL};
    CHECK(nvn_rt_emit(&env, NULL, 0, &depth, 0x4000) == 1);
    CHECK(k.fault_count == 0 && k.write_count == 22);
    expect(&k, 0, 0x487, 0x0FAC6880u);
    for (uint32_t j = 0; j < NVN_RT_COLOR_MAX; j++)
        expect(&k, 1u + j, 0x204u + 0x10u * j, 0u);
    expect(&k, 9, 0x3F8, 0x00000002u);
    expect(&k, 10, 0x3F9, 0x12345600u);
    expect(&k, 11, 0x3FA, 0x0000000Au);
    expect(&k, 12, 0x3FB, 0x00000040u);
    expect(&k, 13, 0x3FC, 0x000F0000u);
    expect(&k, 16, 0x48A, 1280u);
    expect(&k, 17, 0x48B, 720u);
    expect(&k, 18, 0x48C, 1u);
    expect(&k, 14, 0x54E, 0u);
    expect(&k, 15, 0x54E, 1u);
    expect(&k, 19, 0x3FD, 0x05000000u);
    expect(&k, 20, 0x3FE, 0x02D00000u);
    expect(&k, 21, 0x574, 0u);
}

static void test_color_plus_depth(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture c = make_texture(0x25, 1280, 720, 0x0000000412345600ull);
    NvnTexture d = make_texture(0x34, 1280, 720, 0x0000000212345600ull);
    NvnRtColor color[1] = {{&c, NULL}};
    NvnRtColor depth = {&d, NULL};
    CHECK(nvn_rt_emit(&env, color, 1, &depth, 0x4000) == 1);
    CHECK(k.fault_count == 0 && k.write_count == 29);
    expect(&k, 0, 0x487, 0x0FAC6881u);
    expect(&k, 8, 0x207, 0x000F0000u);
    expect(&k, 9, 0x214, 0u);
    expect(&k, 15, 0x274, 0u);
    expect(&k, 16, 0x3F8, 0x00000002u);
    expect(&k, 17, 0x3F9, 0x12345600u);
    expect(&k, 18, 0x3FA, 0x0000000Au);
    expect(&k, 19, 0x3FB, 0x00000040u);
    expect(&k, 20, 0x3FC, 0x000F0000u);
    expect(&k, 23, 0x48A, 1280u);
    expect(&k, 24, 0x48B, 720u);
    expect(&k, 25, 0x48C, 1u);
    expect(&k, 21, 0x54E, 0u);
    expect(&k, 22, 0x54E, 1u);
    expect(&k, 26, 0x3FD, 0x05000000u);
    expect(&k, 27, 0x3FE, 0x02D00000u);
    expect(&k, 28, 0x574, 0u);
}

static void test_small_target_factor(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture c = make_texture(0x25, 64, 4, 0x1000);
    NvnRtColor color[1] = {{&c, NULL}};
    CHECK(nvn_rt_emit(&env, color, 1, NULL, 0x4000) == 1);
    CHECK(k.fault_count == 0);
    expect(&k, 3, 0x202, 64u);
    expect(&k, 4, 0x203, 4u);
    expect(&k, 6, 0x205, 0u);

    NvnTexture d = make_texture(0x34, 64, 4, 0x2000);
    NvnRtColor depth = {&d, NULL};
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, NULL, 0, &depth, 0x4000) == 1);
    CHECK(k.fault_count == 0);
    expect(&k, 16, 0x48A, 64u);
    expect(&k, 17, 0x48B, 4u);
    expect(&k, 12, 0x3FB, 0u);
}

static void test_tile_x_nibble(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture c = make_texture_with(0x25, 1280, 720, 0x1000, 0x1001u, 0);
    NvnRtColor color[1] = {{&c, NULL}};
    CHECK(nvn_rt_emit(&env, color, 1, NULL, 0x4000) == 1);
    CHECK(k.fault_count == 0);
    expect(&k, 3, 0x202, 1280u);
    expect(&k, 6, 0x205, 0x00000001u);

    NvnTexture d = make_texture_with(0x34, 1280, 720, 0x2000, 0x1001u, 0);
    NvnRtColor depth = {&d, NULL};
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, NULL, 0, &depth, 0x4000) == 1);
    CHECK(k.fault_count == 0);
    expect(&k, 12, 0x3FB, 0x00000001u);
    expect(&k, 16, 0x48A, 1280u);
}

static void test_clip_includes_depth(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture c = make_texture(0x25, 1280, 720, 0x1000);
    NvnTexture d = make_texture(0x34, 64, 32, 0x2000);
    NvnRtColor color[1] = {{&c, NULL}};
    NvnRtColor depth = {&d, NULL};
    CHECK(nvn_rt_emit(&env, color, 1, &depth, 0x4000) == 1);
    CHECK(k.fault_count == 0 && k.write_count == 29);
    expect(&k, 26, 0x3FD, 0x00400000u);
    expect(&k, 27, 0x3FE, 0x00200000u);
}

static void test_sample_mode(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture d = make_texture_with(0x34, 1280, 720, 0x2000, 0u, 4u);
    NvnRtColor depth = {&d, NULL};
    CHECK(nvn_rt_emit(&env, NULL, 0, &depth, 0x4000) == 1);
    CHECK(k.fault_count == 0);
    expect(&k, 21, 0x574, 2u);

    NvnTexture c = make_texture_with(0x25, 1280, 720, 0x1000, 0u, 2u);
    NvnRtColor color[1] = {{&c, NULL}};
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, color, 1, &depth, 0x4000) == 1);
    CHECK(k.fault_count == 0);
    expect(&k, 28, 0x574, 5u);
}

static void test_depth_view_layers(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture d = make_texture(0x34, 1280, 720, 0x0000000212345600ull);
    NvnTextureView view;
    memset(&view, 0, sizeof view);
    view.flags = NVN_VIEW_LAYERS;
    view.base_layer = 3;
    view.layer_count = 5;
    NvnRtColor depth = {&d, &view};
    CHECK(nvn_rt_emit(&env, NULL, 0, &depth, 0x4000) == 1);
    CHECK(k.fault_count == 0);
    expect(&k, 9, 0x3F8, 0x00000002u);
    expect(&k, 10, 0x3F9, 0x12E85600u);
    expect(&k, 18, 0x48C, 5u);
}

static void test_depth_view_checks(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture d = make_texture(0x34, 1280, 720, 0x1000);
    NvnTextureView view;

    memset(&view, 0, sizeof view);
    view.flags = NVN_VIEW_LEVELS;
    view.base_level = 1;
    view.level_count = 1;
    NvnRtColor levels = {&d, &view};
    CHECK(nvn_rt_emit(&env, NULL, 0, &levels, 0x4000) == 0);
    CHECK(k.write_count == 0 && fault_is(&k, "rt view levels") && k.last_fault_value == 1u);

    memset(&k, 0, sizeof k);
    view.base_level = 0;
    view.level_count = 2;
    CHECK(nvn_rt_emit(&env, NULL, 0, &levels, 0x4000) == 1);
    CHECK(k.fault_count == 0);
    expect(&k, 18, 0x48C, 1u);

    memset(&k, 0, sizeof k);
    view.base_level = 0;
    view.level_count = 1;
    CHECK(nvn_rt_emit(&env, NULL, 0, &levels, 0x4000) == 1);
    CHECK(k.fault_count == 0);

    memset(&k, 0, sizeof k);
    memset(&view, 0, sizeof view);
    view.flags = NVN_VIEW_FORMAT;
    view.format = 0x33;
    NvnRtColor format = {&d, &view};
    CHECK(nvn_rt_emit(&env, NULL, 0, &format, 0x4000) == 0);
    CHECK(k.write_count == 0 && fault_is(&k, "rt view format") && k.last_fault_value == 0x33u);

    memset(&k, 0, sizeof k);
    view.format = 0x34;
    CHECK(nvn_rt_emit(&env, NULL, 0, &format, 0x4000) == 1);
    CHECK(k.fault_count == 0);

    memset(&k, 0, sizeof k);
    memset(&view, 0, sizeof view);
    view.flags = NVN_VIEW_SWIZZLE | NVN_VIEW_DEPTH_STENCIL_MODE | NVN_VIEW_TARGET;
    view.base_layer = 7;
    view.layer_count = 3;
    NvnRtColor ignored = {&d, &view};
    CHECK(nvn_rt_emit(&env, NULL, 0, &ignored, 0x4000) == 1);
    CHECK(k.fault_count == 0);
    expect(&k, 18, 0x48C, 1u);
}

static void test_depth_linear(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture d = make_linear_texture(0x34, 64, 64, 0x1000);
    NvnRtColor depth = {&d, NULL};
    CHECK(nvn_rt_emit(&env, NULL, 0, &depth, 0x4000) == 0);
    CHECK(k.write_count == 0 && k.fault_count == 1 && k.last_fault_value == NVN_TEXTURE_FLAG_LINEAR);
}

static void test_2d_array_target(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture array = make_texture(0x25, 64, 64, 0x1000);
    array.builder.target = NVN_TEXTURE_TARGET_2D_ARRAY;
    array.layout.depth = 4;
    NvnRtColor color[1] = {{&array, NULL}};
    CHECK(nvn_rt_emit(&env, color, 1, NULL, 0x4000) == 1);
    CHECK(k.fault_count == 0);
    expect(&k, 7, 0x206, 4u);
}

static void test_refusals(void) {
    Mock k;
    NvnRtEnv env;
    NvnTexture good = make_texture(0x25, 1280, 720, 0x1000);

    memset(&k, 0, sizeof k);
    env = make_env(&k);
    NvnRtColor many[NVN_RT_COLOR_MAX + 1u] = {0};
    CHECK(nvn_rt_emit(&env, many, NVN_RT_COLOR_MAX + 1u, NULL, 0x4000) == 0);
    CHECK(k.write_count == 0 && k.fault_count == 1 && k.last_fault_value == 9u);

    NvnTextureView view;
    memset(&view, 0, sizeof view);
    view.flags = NVN_VIEW_LEVELS;
    view.base_level = 1;
    NvnRtColor with_view[1] = {{&good, &view}};
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, with_view, 1, NULL, 0x4000) == 0);
    CHECK(k.write_count == 0 && fault_is(&k, "rt view levels") && k.last_fault_value == 1u);

    NvnTexture not_2d = make_texture(0x25, 64, 64, 0x1000);
    not_2d.builder.target = NVN_TEXTURE_TARGET_2D_MS;
    NvnRtColor bad_target[1] = {{&not_2d, NULL}};
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, bad_target, 1, NULL, 0x4000) == 0);
    CHECK(k.write_count == 0 && k.fault_count == 1 && k.last_fault_value == NVN_TEXTURE_TARGET_2D_MS);

    NvnTexture levels;
    memset(&levels, 0, sizeof levels);
    nvn_texture_builder_defaults(&levels.builder);
    levels.builder.format = 0x25;
    levels.builder.width = 64;
    levels.builder.height = 64;
    levels.builder.depth = 1;
    levels.builder.levels = 2;
    levels.address = 0x1000;
    CHECK(nvn_texture_layout(&levels.builder, &nvn_texture_device_words, &levels.layout));
    NvnRtColor bad_levels[1] = {{&levels, NULL}};
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, bad_levels, 1, NULL, 0x4000) == 0);
    CHECK(k.write_count == 0 && k.fault_count == 1 && k.last_fault_value == 2u);

    NvnTexture linear = make_linear_texture(0x25, 64, 64, 0x1000);
    NvnRtColor bad_linear[1] = {{&linear, NULL}};
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, bad_linear, 1, NULL, 0x4000) == 0);
    CHECK(k.write_count == 0 && k.fault_count == 1 && k.last_fault_value == NVN_TEXTURE_FLAG_LINEAR);

    NvnTexture bad_format = make_texture(0x39, 64, 64, 0x1000);
    NvnRtColor bad_format_color[1] = {{&bad_format, NULL}};
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, bad_format_color, 1, NULL, 0x4000) == 0);
    CHECK(k.write_count == 0 && k.fault_count == 1 && k.last_fault_value == 0x39u);

    NvnTexture compressed = make_texture(0x41, 16, 16, 0x1000);
    NvnRtColor bad_block[1] = {{&compressed, NULL}};
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, bad_block, 1, NULL, 0x4000) == 0);
    CHECK(k.write_count == 0 && k.fault_count == 1 && k.last_fault_value == 0x41u);

    NvnTexture bad_depth = make_texture(0x34, 64, 64, 0x1000);
    bad_depth.layout.bytes_per_block = 0;
    NvnRtColor depth = {&bad_depth, NULL};
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, NULL, 0, &depth, 0x4000) == 0);
    CHECK(k.write_count == 0 && k.fault_count == 1 && k.last_fault_value == 0u);
}

static void test_bad_input(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    CHECK(nvn_rt_emit(&env, NULL, 1, NULL, 0x4000) == 0);
    CHECK(k.write_count == 0 && k.fault_count == 1);

    NvnTexture t = make_texture(0x25, 1280, 720, 0x1000);
    t.layout.bytes_per_block = 0;
    NvnRtColor color[1] = {{&t, NULL}};
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, color, 1, NULL, 0x4000) == 0);
    CHECK(k.write_count == 0 && k.fault_count == 1);
}

static void test_null_env_fields(void) {
    Mock k = {0};
    NvnTexture t = make_texture(0x25, 1280, 720, 0x1000);
    NvnRtColor color[1] = {{&t, NULL}};
    CHECK(nvn_rt_emit(NULL, color, 1, NULL, 0x4000) == 0);

    NvnRtEnv env = make_env(&k);
    env.method = NULL;
    CHECK(nvn_rt_emit(&env, color, 1, NULL, 0x4000) == 0);
    CHECK(k.write_count == 0 && k.fault_count == 0);

    env = make_env(&k);
    env.fault = NULL;
    CHECK(nvn_rt_emit(&env, color, 1, NULL, 0x4000) == 0);
    CHECK(k.write_count == 0 && k.fault_count == 0);
}

static void test_color_view_no_change(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture t = make_texture(0x25, 1280, 720, 0x0000000412345600ull);
    NvnRtColor plain[1] = {{&t, NULL}};
    CHECK(nvn_rt_emit(&env, plain, 1, NULL, 0x4000) == 1);
    Mock base = k;

    NvnTextureView view;
    memset(&view, 0, sizeof view);
    view.level_count = 1;
    view.layer_count = 1;
    view.format = 0x25;
    view.target = NVN_TEXTURE_TARGET_2D;
    NvnRtColor seen[1] = {{&t, &view}};

    view.flags = 0x3fu;
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, seen, 1, NULL, 0x4000) == 1);
    CHECK(k.fault_count == 0 && same_writes(&base, &k));

    view.flags = 0x27u;
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, seen, 1, NULL, 0x4000) == 1);
    CHECK(k.fault_count == 0 && same_writes(&base, &k));
}

static void test_color_view_layers(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture t = make_texture(0x25, 1280, 720, 0x0000000412345600ull);
    NvnTextureView view;
    memset(&view, 0, sizeof view);
    view.flags = NVN_VIEW_LAYERS;
    view.base_layer = 3;
    view.layer_count = 5;
    NvnRtColor color[1] = {{&t, &view}};
    CHECK(nvn_rt_emit(&env, color, 1, NULL, 0x4000) == 1);
    CHECK(k.fault_count == 0);
    expect(&k, 1, 0x200, 0x00000004u);
    expect(&k, 2, 0x201, 0x12E85600u);
    expect(&k, 7, 0x206, 5u);
    expect(&k, 8, 0x207, 0x000F0000u);

    view.base_layer = 0;
    view.layer_count = 0;
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, color, 1, NULL, 0x4000) == 1);
    CHECK(k.fault_count == 0);
    expect(&k, 2, 0x201, 0x12345600u);
    expect(&k, 7, 0x206, 1u);

    view.base_layer = 3;
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, color, 1, NULL, 0x4000) == 0);
    CHECK(k.write_count == 0 && fault_is(&k, "rt view layers") && k.last_fault_value == 3u);
}

static void test_color_view_target_3d(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture t = make_texture(0x25, 1280, 720, 0x1000);
    NvnTextureView view;
    memset(&view, 0, sizeof view);
    view.target = NVN_TEXTURE_TARGET_3D;
    NvnRtColor color[1] = {{&t, &view}};

    view.flags = NVN_VIEW_TARGET;
    CHECK(nvn_rt_emit(&env, color, 1, NULL, 0x4000) == 1);
    CHECK(k.fault_count == 0);
    expect(&k, 6, 0x205, 0x00010040u);

    view.flags = NVN_VIEW_SWIZZLE;
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, color, 1, NULL, 0x4000) == 1);
    CHECK(k.fault_count == 0);
    expect(&k, 6, 0x205, 0x00000040u);
}

static void test_color_view_checks(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture t = make_texture(0x25, 1280, 720, 0x1000);
    NvnTextureView view;
    memset(&view, 0, sizeof view);
    NvnRtColor color[1] = {{&t, &view}};

    view.flags = NVN_VIEW_LEVELS;
    view.base_level = 1;
    CHECK(nvn_rt_emit(&env, color, 1, NULL, 0x4000) == 0);
    CHECK(k.write_count == 0 && fault_is(&k, "rt view levels") && k.last_fault_value == 1u);

    memset(&k, 0, sizeof k);
    view.flags = NVN_VIEW_FORMAT;
    view.format = 0x26;
    CHECK(nvn_rt_emit(&env, color, 1, NULL, 0x4000) == 0);
    CHECK(k.write_count == 0 && fault_is(&k, "rt view format") && k.last_fault_value == 0x26u);

    memset(&k, 0, sizeof k);
    view.flags = 0x40u;
    CHECK(nvn_rt_emit(&env, color, 1, NULL, 0x4000) == 0);
    CHECK(k.write_count == 0 && fault_is(&k, "rt view flags") && k.last_fault_value == 0x40u);
}

int main(void) {
    test_one_target();
    test_null_slot_zero();
    test_no_target();
    test_clip_limit_zero();
    test_width_align_up();
    test_depth_only_d32f();
    test_color_plus_depth();
    test_small_target_factor();
    test_tile_x_nibble();
    test_clip_includes_depth();
    test_sample_mode();
    test_depth_view_layers();
    test_depth_view_checks();
    test_color_view_no_change();
    test_color_view_layers();
    test_color_view_target_3d();
    test_color_view_checks();
    test_depth_linear();
    test_2d_array_target();
    test_refusals();
    test_bad_input();
    test_null_env_fields();
    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("nvn_rt: ok\n");
    return 0;
}
