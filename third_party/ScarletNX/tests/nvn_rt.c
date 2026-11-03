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

static NvnTexture make_texture(uint32_t format, uint32_t width, uint32_t height, uint64_t address) {
    NvnTexture t;
    memset(&t, 0, sizeof t);
    nvn_texture_builder_defaults(&t.builder);
    t.builder.format = format;
    t.builder.width = width;
    t.builder.height = height;
    t.builder.depth = 1;
    if (!nvn_texture_layout(&t.builder, &nvn_texture_device_words, &t.layout)) {
        printf("FAIL texture layout: format %08x %ux%u\n", format, width, height);
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
    CHECK(nvn_rt_emit(&env, color, 1, 0, 0x4000) == 1);
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
    CHECK(nvn_rt_emit(&env, color, 2, 0, 0x4000) == 1);
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
    CHECK(nvn_rt_emit(&env, NULL, 0, 0, 0x4000) == 1);
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
    CHECK(nvn_rt_emit(&env, color, 1, 0, 0) == 1);
    expect(&k, 18, 0x3FD, 0u);
    expect(&k, 19, 0x3FE, 0u);
}

static void test_width_align_up(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    NvnTexture t = make_texture(0x25, 100, 16, 0x1000);
    NvnRtColor color[1] = {{&t, NULL}};
    CHECK(nvn_rt_emit(&env, color, 1, 0, 0x4000) == 1);
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

static void test_refusals(void) {
    Mock k;
    NvnRtEnv env;
    NvnTexture good = make_texture(0x25, 1280, 720, 0x1000);
    NvnRtColor one[1] = {{&good, NULL}};

    memset(&k, 0, sizeof k);
    env = make_env(&k);
    NvnRtColor many[NVN_RT_COLOR_MAX + 1u] = {0};
    CHECK(nvn_rt_emit(&env, many, NVN_RT_COLOR_MAX + 1u, 0, 0x4000) == 0);
    CHECK(k.write_count == 0 && k.fault_count == 1 && k.last_fault_value == 9u);

    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, one, 1, 1, 0x4000) == 0);
    CHECK(k.write_count == 0 && k.fault_count == 1);

    NvnTextureView view;
    memset(&view, 0, sizeof view);
    view.flags = NVN_VIEW_LEVELS;
    NvnRtColor with_view[1] = {{&good, &view}};
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, with_view, 1, 0, 0x4000) == 0);
    CHECK(k.write_count == 0 && k.fault_count == 1 && k.last_fault_value == NVN_VIEW_LEVELS);

    NvnTexture not_2d = make_texture(0x25, 64, 64, 0x1000);
    not_2d.builder.target = NVN_TEXTURE_TARGET_2D_MS;
    NvnRtColor bad_target[1] = {{&not_2d, NULL}};
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, bad_target, 1, 0, 0x4000) == 0);
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
    CHECK(nvn_rt_emit(&env, bad_levels, 1, 0, 0x4000) == 0);
    CHECK(k.write_count == 0 && k.fault_count == 1 && k.last_fault_value == 2u);

    NvnTexture linear;
    memset(&linear, 0, sizeof linear);
    nvn_texture_builder_defaults(&linear.builder);
    linear.builder.format = 0x25;
    linear.builder.width = 64;
    linear.builder.height = 64;
    linear.builder.depth = 1;
    linear.builder.flags = NVN_TEXTURE_FLAG_LINEAR;
    linear.address = 0x1000;
    CHECK(nvn_texture_layout(&linear.builder, &nvn_texture_device_words, &linear.layout));
    NvnRtColor bad_linear[1] = {{&linear, NULL}};
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, bad_linear, 1, 0, 0x4000) == 0);
    CHECK(k.write_count == 0 && k.fault_count == 1 && k.last_fault_value == NVN_TEXTURE_FLAG_LINEAR);

    NvnTexture bad_format = make_texture(0x39, 64, 64, 0x1000);
    NvnRtColor bad_format_color[1] = {{&bad_format, NULL}};
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, bad_format_color, 1, 0, 0x4000) == 0);
    CHECK(k.write_count == 0 && k.fault_count == 1 && k.last_fault_value == 0x39u);

    NvnTexture compressed = make_texture(0x41, 16, 16, 0x1000);
    NvnRtColor bad_block[1] = {{&compressed, NULL}};
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, bad_block, 1, 0, 0x4000) == 0);
    CHECK(k.write_count == 0 && k.fault_count == 1 && k.last_fault_value == 0x41u);

    NvnTexture small = make_texture(0x25, 64, 4, 0x1000);
    NvnRtColor bad_tile[1] = {{&small, NULL}};
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, bad_tile, 1, 0, 0x4000) == 0);
    CHECK(k.write_count == 0 && k.fault_count == 1 && k.last_fault_value == 4u);
}

static void test_bad_input(void) {
    Mock k = {0};
    NvnRtEnv env = make_env(&k);
    CHECK(nvn_rt_emit(&env, NULL, 1, 0, 0x4000) == 0);
    CHECK(k.write_count == 0 && k.fault_count == 1);

    NvnTexture t = make_texture(0x25, 1280, 720, 0x1000);
    t.layout.bytes_per_block = 0;
    NvnRtColor color[1] = {{&t, NULL}};
    memset(&k, 0, sizeof k);
    CHECK(nvn_rt_emit(&env, color, 1, 0, 0x4000) == 0);
    CHECK(k.write_count == 0 && k.fault_count == 1);
}

static void test_null_env_fields(void) {
    Mock k = {0};
    NvnTexture t = make_texture(0x25, 1280, 720, 0x1000);
    NvnRtColor color[1] = {{&t, NULL}};
    CHECK(nvn_rt_emit(NULL, color, 1, 0, 0x4000) == 0);

    NvnRtEnv env = make_env(&k);
    env.method = NULL;
    CHECK(nvn_rt_emit(&env, color, 1, 0, 0x4000) == 0);
    CHECK(k.write_count == 0 && k.fault_count == 0);

    env = make_env(&k);
    env.fault = NULL;
    CHECK(nvn_rt_emit(&env, color, 1, 0, 0x4000) == 0);
    CHECK(k.write_count == 0 && k.fault_count == 0);
}

int main(void) {
    test_one_target();
    test_null_slot_zero();
    test_no_target();
    test_clip_limit_zero();
    test_width_align_up();
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
