/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The replay of the host's NVN command lists (gpu/nvn_replay.c): the method word of each op, the faults, and the
   statistics. Each expected word below is computed by hand from analyst-3c-C-1.md. */
#include "nx/gpu/nvn_replay.h"
#include <math.h>
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

enum {
    MOCK_MAX_WRITES = 512,
    MOCK_MAX_LISTS = 16,
    MOCK_MAX_POOLS = 8
};

typedef struct { uint32_t method, value; } MockWrite;
typedef struct { uint64_t handle; const NvnOpList* list; } MockList;
typedef struct { NvnOpCode op; uint64_t pool, base; uint32_t count; } MockPool;

typedef struct {
    MockWrite writes[MOCK_MAX_WRITES];
    uint32_t write_count;
    uint32_t reg;
    int render_targets_ok;
    MockList lists[MOCK_MAX_LISTS];
    uint32_t list_count;
    MockPool pools[MOCK_MAX_POOLS];
    uint32_t pool_count;
    uint32_t render_target_calls;
    int bind_texture_ok;
    uint32_t bind_texture_calls;
    uint32_t bind_stage, bind_slot;
    uint64_t bind_handle;
    int bind_buffer_ok;
    uint32_t bind_buffer_calls;
    NvnOpBindBuffer bind_buffer_op;
    int bind_state_ok;
    uint32_t bind_state_calls;
    NvnOpBindState bind_state_op;
    uint32_t fault_count;
    const char* last_fault;
    uint64_t last_fault_value;
} Mock;

static void mock_method(void* ctx, uint32_t method, uint32_t value) {
    Mock* k = ctx;
    if (k->write_count < MOCK_MAX_WRITES)
        k->writes[k->write_count++] = (MockWrite){method, value};
}

static uint32_t mock_read_register(void* ctx, uint32_t method) {
    (void)method;
    return ((Mock*)ctx)->reg;
}

static const NvnOpList* mock_find_list(void* ctx, uint64_t handle) {
    Mock* k = ctx;
    for (uint32_t i = 0; i < k->list_count; i++)
        if (k->lists[i].handle == handle) return k->lists[i].list;
    return NULL;
}

static int mock_descriptor_pool(void* ctx, NvnOpCode op, const NvnOpPool* pool, uint64_t* gpu_base,
                                uint32_t* count) {
    Mock* k = ctx;
    for (uint32_t i = 0; i < k->pool_count; i++) {
        if (k->pools[i].op == op && k->pools[i].pool == pool->pool) {
            *gpu_base = k->pools[i].base;
            *count = k->pools[i].count;
            return 1;
        }
    }
    return 0;
}

static int mock_render_targets(void* ctx, const NvnOpRenderTargets* op) {
    (void)op;
    Mock* k = ctx;
    k->render_target_calls++;
    return k->render_targets_ok;
}

static int mock_bind_texture(void* ctx, uint32_t stage, uint32_t slot, uint64_t handle) {
    Mock* k = ctx;
    k->bind_texture_calls++;
    k->bind_stage = stage;
    k->bind_slot = slot;
    k->bind_handle = handle;
    return k->bind_texture_ok;
}

static int mock_bind_buffer(void* ctx, const NvnOpBindBuffer* op) {
    Mock* k = ctx;
    k->bind_buffer_calls++;
    k->bind_buffer_op = *op;
    return k->bind_buffer_ok;
}

static int mock_bind_state(void* ctx, const NvnOpBindState* op) {
    Mock* k = ctx;
    k->bind_state_calls++;
    k->bind_state_op = *op;
    return k->bind_state_ok;
}

static void mock_fault(void* ctx, const char* what, uint64_t value) {
    Mock* k = ctx;
    k->fault_count++;
    k->last_fault = what;
    k->last_fault_value = value;
}

static NvnReplayEnv make_env(Mock* k) {
    NvnReplayEnv env;
    memset(&env, 0, sizeof env);
    env.ctx = k;
    env.method = mock_method;
    env.read_register = mock_read_register;
    env.find_list = mock_find_list;
    env.descriptor_pool = mock_descriptor_pool;
    env.bind_texture = mock_bind_texture;
    env.bind_buffer = mock_bind_buffer;
    env.bind_state = mock_bind_state;
    env.render_targets = mock_render_targets;
    env.fault = mock_fault;
    return env;
}

static void mock_add_list(Mock* k, uint64_t handle, const NvnOpList* list) {
    k->lists[k->list_count++] = (MockList){handle, list};
}

static void mock_add_pool(Mock* k, NvnOpCode op, uint64_t pool, uint64_t base, uint32_t count) {
    k->pools[k->pool_count++] = (MockPool){op, pool, base, count};
}

static int has_method(const Mock* k, uint32_t method) {
    for (uint32_t i = 0; i < k->write_count; i++)
        if (k->writes[i].method == method) return 1;
    return 0;
}

static int has_write(const Mock* k, uint32_t method, uint32_t value) {
    for (uint32_t i = 0; i < k->write_count; i++)
        if (k->writes[i].method == method && k->writes[i].value == value) return 1;
    return 0;
}

static void test_viewport(void) {
    Mock k = {0};
    NvnReplayEnv env = make_env(&k);
    NvnOpList l = {0};
    NvnReplayStats st = {0};
    NvnOpViewport vp = {0, 0.0f, 0.0f, 640.0f, 480.0f};
    CHECK(nvn_oplist_append(&l, NVN_OP_SET_VIEWPORT, &vp, sizeof vp));
    nvn_replay_list(&env, &l, &st);
    CHECK(has_write(&k, 0x280, 0x43A00000u)); /* 640/2 = 320 */
    CHECK(has_write(&k, 0x281, 0x43700000u)); /* 480/2 = 240 */
    CHECK(has_write(&k, 0x283, 0x43A00000u)); /* 0 + 320 */
    CHECK(has_write(&k, 0x284, 0x43700000u)); /* 0 + 240 */
    CHECK(has_write(&k, 0x300, 0x02800000u)); /* 0..640 */
    CHECK(has_write(&k, 0x301, 0x01E00000u)); /* 0..480 */
    CHECK(!has_method(&k, 0x282) && !has_method(&k, 0x285));
    CHECK(st.replayed[NVN_OP_SET_VIEWPORT] == 1 && st.methods == 6 && st.faults == 0);
    nvn_oplist_free(&l);
}

static void test_viewport_flip(void) {
    Mock k = {0};
    NvnReplayEnv env = make_env(&k);
    env.window_origin_flip = 1;
    NvnOpList l = {0};
    NvnReplayStats st = {0};
    NvnOpViewport vp = {1, 0.0f, 0.0f, 800.0f, 600.0f};
    CHECK(nvn_oplist_append(&l, NVN_OP_SET_VIEWPORT, &vp, sizeof vp));
    nvn_replay_list(&env, &l, &st);
    CHECK(has_write(&k, 0x288, 0x43C80000u)); /* 800/2 = 400 */
    CHECK(has_write(&k, 0x289, 0xC3960000u)); /* -(600/2) = -300 */
    CHECK(has_write(&k, 0x28B, 0x43C80000u)); /* 0 + 400 */
    CHECK(has_write(&k, 0x28C, 0x43960000u)); /* 0 + 300 */
    CHECK(has_write(&k, 0x304, 0x03200000u)); /* 0..800 */
    CHECK(has_write(&k, 0x305, 0x02580000u)); /* 0..600 */
    CHECK(st.replayed[NVN_OP_SET_VIEWPORT] == 1 && st.methods == 6);
    nvn_oplist_free(&l);
}

static void test_viewport_fractional(void) {
    Mock k = {0};
    NvnReplayEnv env = make_env(&k);
    NvnOpList l = {0};
    NvnReplayStats st = {0};
    NvnOpViewport vp = {2, 1.5f, 2.25f, 3.0f, 4.0f};
    CHECK(nvn_oplist_append(&l, NVN_OP_SET_VIEWPORT, &vp, sizeof vp));
    nvn_replay_list(&env, &l, &st);
    CHECK(has_write(&k, 0x290, 0x3FC00000u)); /* 3/2 = 1.5 */
    CHECK(has_write(&k, 0x291, 0x40000000u)); /* 4/2 = 2 */
    CHECK(has_write(&k, 0x293, 0x40400000u)); /* 1.5 + 1.5 = 3 */
    CHECK(has_write(&k, 0x294, 0x40880000u)); /* 2.25 + 2 = 4.25 */
    CHECK(has_write(&k, 0x308, 0x00040001u)); /* floor(1.5)=1, ceil(4.5)-1=4 */
    CHECK(has_write(&k, 0x309, 0x00050002u)); /* floor(2.25)=2, ceil(6.25)-2=5 */
    CHECK(st.methods == 6 && st.faults == 0);
    nvn_oplist_free(&l);
}

static void test_scissor(void) {
    Mock k = {0};
    NvnReplayEnv env = make_env(&k);
    NvnOpList l = {0};
    NvnReplayStats st = {0};
    NvnOpScissor negative = {0, -5, -10, 20, 30};
    NvnOpScissor empty = {1, 0, 0, 0, 10};
    CHECK(nvn_oplist_append(&l, NVN_OP_SET_SCISSOR, &negative, sizeof negative));
    CHECK(nvn_oplist_append(&l, NVN_OP_SET_SCISSOR, &empty, sizeof empty));
    nvn_replay_list(&env, &l, &st);
    CHECK(has_write(&k, 0x381, 0x000F0000u)); /* max(-5,0)=0, min(-5+20, 0xFFFF)=15 */
    CHECK(has_write(&k, 0x382, 0x00140000u)); /* max(-10,0)=0, min(-10+30, 0xFFFF)=20 */
    CHECK(has_write(&k, 0x385, 0u));          /* width is 0: the rectangle is empty */
    CHECK(has_write(&k, 0x386, 0u));
    CHECK(!has_method(&k, 0x380));
    CHECK(st.replayed[NVN_OP_SET_SCISSOR] == 2 && st.methods == 4 && st.faults == 0);
    nvn_oplist_free(&l);
}

static void test_scissor_clamp(void) {
    Mock k = {0};
    NvnReplayEnv env = make_env(&k);
    NvnOpList l = {0};
    NvnReplayStats st = {0};
    NvnOpScissor clamp = {2, 65000, 0, 1000, 1};
    CHECK(nvn_oplist_append(&l, NVN_OP_SET_SCISSOR, &clamp, sizeof clamp));
    nvn_replay_list(&env, &l, &st);
    CHECK(has_write(&k, 0x389, 0xFFFFFDE8u)); /* 65000, end clamped to 0xFFFF */
    CHECK(has_write(&k, 0x38A, 0x00010000u)); /* 0..1 */
    CHECK(st.methods == 2 && st.faults == 0);
    nvn_oplist_free(&l);
}

static void test_clear_layers(void) {
    NvnOpClearColor op = {0, 0xFu, {0x3F800000u, 0x40000000u, 0x40400000u, 0x40800000u}};
    /* no layer: the four color words only */
    Mock k = {0};
    NvnReplayEnv env = make_env(&k);
    NvnOpList l = {0};
    NvnReplayStats st = {0};
    k.reg = 0;
    CHECK(nvn_oplist_append(&l, NVN_OP_CLEAR_COLOR, &op, sizeof op));
    nvn_replay_list(&env, &l, &st);
    CHECK(has_write(&k, 0x360, 0x3F800000u) && has_write(&k, 0x363, 0x40800000u));
    CHECK(!has_method(&k, 0x674));
    CHECK(st.replayed[NVN_OP_CLEAR_COLOR] == 1 && st.methods == 4 && st.faults == 0);
    nvn_oplist_free(&l);

    /* one layer: arg = 1<<6 | 5<<2 = 0x54 */
    Mock k1 = {0};
    NvnReplayEnv env1 = make_env(&k1);
    NvnOpClearColor op1 = {1, 0x5u, {1u, 2u, 3u, 4u}};
    NvnReplayStats st1 = {0};
    NvnOpList l1 = {0};
    k1.reg = 1;
    CHECK(nvn_oplist_append(&l1, NVN_OP_CLEAR_COLOR, &op1, sizeof op1));
    nvn_replay_list(&env1, &l1, &st1);
    CHECK(has_write(&k1, 0x674, 0x54u));
    CHECK(st1.methods == 5 && st1.faults == 0);
    nvn_oplist_free(&l1);

    /* three layers: arg = 2<<6 | 0xA<<2 = 0xA8, plus 0x400 for each layer */
    Mock k3 = {0};
    NvnReplayEnv env3 = make_env(&k3);
    NvnOpClearColor op3 = {2, 0xAu, {5u, 6u, 7u, 8u}};
    NvnReplayStats st3 = {0};
    NvnOpList l3 = {0};
    k3.reg = 3;
    CHECK(nvn_oplist_append(&l3, NVN_OP_CLEAR_COLOR, &op3, sizeof op3));
    nvn_replay_list(&env3, &l3, &st3);
    CHECK(has_write(&k3, 0x674, 0xA8u));
    CHECK(has_write(&k3, 0x674, 0x4A8u));
    CHECK(has_write(&k3, 0x674, 0x8A8u));
    CHECK(st3.methods == 7 && st3.faults == 0);
    nvn_oplist_free(&l3);
}

static void test_clear_layer_limit(void) {
    Mock k = {0};
    NvnReplayEnv env = make_env(&k);
    NvnOpList l = {0};
    NvnReplayStats st = {0};
    NvnOpClearColor op = {0, 0xFu, {1u, 2u, 3u, 4u}};
    k.reg = NVN_REPLAY_LAYER_MAX + 1u;
    CHECK(nvn_oplist_append(&l, NVN_OP_CLEAR_COLOR, &op, sizeof op));
    nvn_replay_list(&env, &l, &st);
    CHECK(st.faults == 1 && k.fault_count == 1);
    CHECK(st.replayed[NVN_OP_CLEAR_COLOR] == 0);
    CHECK(st.methods == 4 && !has_method(&k, 0x674));
    CHECK(k.last_fault_value == NVN_REPLAY_LAYER_MAX + 1u);
    nvn_oplist_free(&l);
}

static void test_pool(void) {
    /* the texture pool is found: base 0x123456789A, 5 descriptors */
    Mock k = {0};
    NvnReplayEnv env = make_env(&k);
    NvnOpList l = {0};
    NvnReplayStats st = {0};
    NvnOpPool pool = {0x1000u, 1u, 0u};
    mock_add_pool(&k, NVN_OP_SET_TEXTURE_POOL, 0x1000u, 0x123456789Aull, 5u);
    CHECK(nvn_oplist_append(&l, NVN_OP_SET_TEXTURE_POOL, &pool, sizeof pool));
    nvn_replay_list(&env, &l, &st);
    CHECK(has_write(&k, 0x55D, 0x12u));
    CHECK(has_write(&k, 0x55E, 0x3456789Au));
    CHECK(has_write(&k, 0x55F, 4u));
    CHECK(st.replayed[NVN_OP_SET_TEXTURE_POOL] == 1 && st.methods == 3 && st.faults == 0);
    nvn_oplist_free(&l);

    /* the sampler pool is found: base 0xABCDEF0123, 1 descriptor */
    Mock ks = {0};
    NvnReplayEnv envs = make_env(&ks);
    NvnOpList ls = {0};
    NvnReplayStats sts = {0};
    NvnOpPool sampler = {0x2000u, 2u, 0u};
    mock_add_pool(&ks, NVN_OP_SET_SAMPLER_POOL, 0x2000u, 0xABCDEF0123ull, 1u);
    CHECK(nvn_oplist_append(&ls, NVN_OP_SET_SAMPLER_POOL, &sampler, sizeof sampler));
    nvn_replay_list(&envs, &ls, &sts);
    CHECK(has_write(&ks, 0x557, 0xABu));
    CHECK(has_write(&ks, 0x558, 0xCDEF0123u));
    CHECK(has_write(&ks, 0x559, 0u));
    CHECK(sts.methods == 3 && sts.faults == 0);
    nvn_oplist_free(&ls);

    /* the pool object is gone */
    Mock kg = {0};
    NvnReplayEnv envg = make_env(&kg);
    NvnOpList lg = {0};
    NvnReplayStats stg = {0};
    NvnOpPool gone = {0x3000u, 0u, 0u};
    CHECK(nvn_oplist_append(&lg, NVN_OP_SET_TEXTURE_POOL, &gone, sizeof gone));
    nvn_replay_list(&envg, &lg, &stg);
    CHECK(stg.faults == 1 && stg.methods == 0 && stg.replayed[NVN_OP_SET_TEXTURE_POOL] == 0);
    CHECK(kg.last_fault_value == 0x3000u);
    nvn_oplist_free(&lg);

    /* the descriptor count is 0 */
    Mock ke = {0};
    NvnReplayEnv enve = make_env(&ke);
    NvnOpList le = {0};
    NvnReplayStats ste = {0};
    NvnOpPool empty = {0x4000u, 0u, 0u};
    mock_add_pool(&ke, NVN_OP_SET_TEXTURE_POOL, 0x4000u, 0x1000u, 0u);
    CHECK(nvn_oplist_append(&le, NVN_OP_SET_TEXTURE_POOL, &empty, sizeof empty));
    nvn_replay_list(&enve, &le, &ste);
    CHECK(ste.faults == 1 && ste.methods == 0);
    nvn_oplist_free(&le);
}

static void test_render_targets(void) {
    NvnOpRenderTargets rt;
    memset(&rt, 0, sizeof rt);
    rt.count = 1;

    Mock k = {0};
    NvnReplayEnv env = make_env(&k);
    NvnOpList l = {0};
    NvnReplayStats st = {0};
    k.render_targets_ok = 1;
    CHECK(nvn_oplist_append(&l, NVN_OP_SET_RENDER_TARGETS, &rt, sizeof rt));
    nvn_replay_list(&env, &l, &st);
    CHECK(k.render_target_calls == 1 && st.replayed[NVN_OP_SET_RENDER_TARGETS] == 1);
    CHECK(st.methods == 0 && st.faults == 0);
    nvn_oplist_free(&l);

    Mock kf = {0};
    NvnReplayEnv envf = make_env(&kf);
    NvnOpList lf = {0};
    NvnReplayStats stf = {0};
    kf.render_targets_ok = 0;
    CHECK(nvn_oplist_append(&lf, NVN_OP_SET_RENDER_TARGETS, &rt, sizeof rt));
    nvn_replay_list(&envf, &lf, &stf);
    CHECK(kf.render_target_calls == 1 && stf.faults == 1);
    CHECK(stf.replayed[NVN_OP_SET_RENDER_TARGETS] == 0 && stf.methods == 0);
    nvn_oplist_free(&lf);
}

static void test_bind_texture(void) {
    NvnOpBindTexture bind = {3, 11, 0x110000147ull};
    Mock k = {0};
    NvnReplayEnv env = make_env(&k);
    NvnOpList l = {0};
    NvnReplayStats st = {0};
    k.bind_texture_ok = 1;
    CHECK(nvn_oplist_append(&l, NVN_OP_BIND_TEXTURE, &bind, sizeof bind));
    nvn_replay_list(&env, &l, &st);
    CHECK(k.bind_texture_calls == 1 && k.bind_stage == 3 && k.bind_slot == 11);
    CHECK(k.bind_handle == 0x110000147ull);
    CHECK(st.replayed[NVN_OP_BIND_TEXTURE] == 1 && st.faults == 0 && st.methods == 0);
    nvn_oplist_free(&l);

    Mock kn = {0};
    NvnReplayEnv envn = make_env(&kn);
    NvnOpList ln = {0};
    NvnReplayStats stn = {0};
    envn.bind_texture = NULL;
    CHECK(nvn_oplist_append(&ln, NVN_OP_BIND_TEXTURE, &bind, sizeof bind));
    nvn_replay_list(&envn, &ln, &stn);
    CHECK(stn.faults == 1 && kn.fault_count == 1 && stn.replayed[NVN_OP_BIND_TEXTURE] == 0);
    nvn_oplist_free(&ln);

    Mock kr = {0};
    NvnReplayEnv envr = make_env(&kr);
    NvnOpList lr = {0};
    NvnReplayStats str = {0};
    kr.bind_texture_ok = 0;
    CHECK(nvn_oplist_append(&lr, NVN_OP_BIND_TEXTURE, &bind, sizeof bind));
    nvn_replay_list(&envr, &lr, &str);
    CHECK(str.faults == 1 && kr.bind_texture_calls == 1 && str.replayed[NVN_OP_BIND_TEXTURE] == 0);
    nvn_oplist_free(&lr);

    Mock ks = {0};
    NvnReplayEnv envs = make_env(&ks);
    NvnOpList ls = {0};
    NvnReplayStats sts = {0};
    uint8_t too_big[24] = {0};
    CHECK(nvn_oplist_append(&ls, NVN_OP_BIND_TEXTURE, too_big, sizeof too_big));
    nvn_replay_list(&envs, &ls, &sts);
    CHECK(sts.faults == 1 && ks.last_fault && !strcmp(ks.last_fault, "op size"));
    CHECK(ks.last_fault_value == NVN_OP_BIND_TEXTURE && sts.replayed[NVN_OP_BIND_TEXTURE] == 0);
    nvn_oplist_free(&ls);
}

static void test_bind_buffer(void) {
    NvnOpBindBuffer bind = {NVN_BIND_UNIFORM, 2, 5, 0, 0x400000000ull, 32};
    Mock k = {0};
    NvnReplayEnv env = make_env(&k);
    NvnOpList l = {0};
    NvnReplayStats st = {0};
    k.bind_buffer_ok = 1;
    CHECK(nvn_oplist_append(&l, NVN_OP_BIND_BUFFER, &bind, sizeof bind));
    nvn_replay_list(&env, &l, &st);
    CHECK(k.bind_buffer_calls == 1 && k.bind_buffer_op.kind == NVN_BIND_UNIFORM);
    CHECK(k.bind_buffer_op.stage == 2 && k.bind_buffer_op.slot == 5);
    CHECK(k.bind_buffer_op.addr == 0x400000000ull && k.bind_buffer_op.size == 32);
    CHECK(st.replayed[NVN_OP_BIND_BUFFER] == 1 && st.faults == 0 && st.methods == 0);
    nvn_oplist_free(&l);

    Mock kn = {0};
    NvnReplayEnv envn = make_env(&kn);
    NvnOpList ln = {0};
    NvnReplayStats stn = {0};
    envn.bind_buffer = NULL;
    CHECK(nvn_oplist_append(&ln, NVN_OP_BIND_BUFFER, &bind, sizeof bind));
    nvn_replay_list(&envn, &ln, &stn);
    CHECK(stn.faults == 1 && kn.fault_count == 1 && stn.replayed[NVN_OP_BIND_BUFFER] == 0);
    nvn_oplist_free(&ln);

    Mock kr = {0};
    NvnReplayEnv envr = make_env(&kr);
    NvnOpList lr = {0};
    NvnReplayStats str = {0};
    kr.bind_buffer_ok = 0;
    CHECK(nvn_oplist_append(&lr, NVN_OP_BIND_BUFFER, &bind, sizeof bind));
    nvn_replay_list(&envr, &lr, &str);
    CHECK(str.faults == 1 && kr.bind_buffer_calls == 1 && str.replayed[NVN_OP_BIND_BUFFER] == 0);
    nvn_oplist_free(&lr);

    Mock ks = {0};
    NvnReplayEnv envs = make_env(&ks);
    NvnOpList ls = {0};
    NvnReplayStats sts = {0};
    uint8_t too_big[40] = {0};
    CHECK(nvn_oplist_append(&ls, NVN_OP_BIND_BUFFER, too_big, sizeof too_big));
    nvn_replay_list(&envs, &ls, &sts);
    CHECK(sts.faults == 1 && ks.last_fault && !strcmp(ks.last_fault, "op size"));
    CHECK(ks.last_fault_value == NVN_OP_BIND_BUFFER && sts.replayed[NVN_OP_BIND_BUFFER] == 0);
    nvn_oplist_free(&ls);
}

static void test_bind_state(void) {
    NvnOpBindState bind = {NVN_STATE_DEPTH_STENCIL, 2, {0x00000022u, 0x11180118u, 0, 0, 0}};
    Mock k = {0};
    NvnReplayEnv env = make_env(&k);
    NvnOpList l = {0};
    NvnReplayStats st = {0};
    k.bind_state_ok = 1;
    CHECK(nvn_oplist_append(&l, NVN_OP_BIND_STATE, &bind, sizeof bind));
    nvn_replay_list(&env, &l, &st);
    CHECK(k.bind_state_calls == 1 && k.bind_state_op.kind == NVN_STATE_DEPTH_STENCIL);
    CHECK(k.bind_state_op.words == 2 && k.bind_state_op.w[0] == 0x00000022u);
    CHECK(k.bind_state_op.w[1] == 0x11180118u);
    CHECK(st.replayed[NVN_OP_BIND_STATE] == 1 && st.faults == 0 && st.methods == 0);
    nvn_oplist_free(&l);

    Mock kn = {0};
    NvnReplayEnv envn = make_env(&kn);
    NvnOpList ln = {0};
    NvnReplayStats stn = {0};
    envn.bind_state = NULL;
    CHECK(nvn_oplist_append(&ln, NVN_OP_BIND_STATE, &bind, sizeof bind));
    nvn_replay_list(&envn, &ln, &stn);
    CHECK(stn.faults == 1 && kn.fault_count == 1 && stn.replayed[NVN_OP_BIND_STATE] == 0);
    nvn_oplist_free(&ln);

    Mock kr = {0};
    NvnReplayEnv envr = make_env(&kr);
    NvnOpList lr = {0};
    NvnReplayStats str = {0};
    kr.bind_state_ok = 0;
    CHECK(nvn_oplist_append(&lr, NVN_OP_BIND_STATE, &bind, sizeof bind));
    nvn_replay_list(&envr, &lr, &str);
    CHECK(str.faults == 1 && kr.bind_state_calls == 1 && str.replayed[NVN_OP_BIND_STATE] == 0);
    nvn_oplist_free(&lr);

    Mock ks = {0};
    NvnReplayEnv envs = make_env(&ks);
    NvnOpList ls = {0};
    NvnReplayStats sts = {0};
    uint8_t too_big[40] = {0};
    CHECK(nvn_oplist_append(&ls, NVN_OP_BIND_STATE, too_big, sizeof too_big));
    nvn_replay_list(&envs, &ls, &sts);
    CHECK(sts.faults == 1 && ks.last_fault && !strcmp(ks.last_fault, "op size"));
    CHECK(ks.last_fault_value == NVN_OP_BIND_STATE && sts.replayed[NVN_OP_BIND_STATE] == 0);
    nvn_oplist_free(&ls);
}

static void test_barrier(void) {
    Mock k = {0};
    NvnReplayEnv env = make_env(&k);
    NvnOpList l = {0};
    NvnReplayStats st = {0};
    NvnOpBarrier op = {0x309u, 0u}; /* bits 0, 3, 8 and 9 */
    CHECK(nvn_oplist_append(&l, NVN_OP_BARRIER, &op, sizeof op));
    nvn_replay_list(&env, &l, &st);
    CHECK(st.barrier_bits[0] == 1 && st.barrier_bits[3] == 1 && st.barrier_bits[8] == 1);
    CHECK(st.barrier_bits[1] == 0 && st.barrier_bits[7] == 0);
    CHECK(st.replayed[NVN_OP_BARRIER] == 1 && st.methods == 0 && st.faults == 0);
    nvn_oplist_free(&l);
}

static void test_stencil_ref(void) {
    Mock k = {0};
    NvnReplayEnv env = make_env(&k);
    NvnOpList l = {0};
    NvnReplayStats st = {0};
    NvnOpStencil front = {NVN_STENCIL_REF, 1u, 0x1ABu, 0u};
    NvnOpStencil back = {NVN_STENCIL_REF, 2u, 0x2CDu, 0u};
    NvnOpStencil both = {NVN_STENCIL_REF, 3u, 0x3EFu, 0u};
    NvnOpStencil none = {NVN_STENCIL_REF, 0u, 0x55u, 0u};
    CHECK(nvn_oplist_append(&l, NVN_OP_SET_STENCIL, &front, sizeof front));
    CHECK(nvn_oplist_append(&l, NVN_OP_SET_STENCIL, &back, sizeof back));
    CHECK(nvn_oplist_append(&l, NVN_OP_SET_STENCIL, &both, sizeof both));
    CHECK(nvn_oplist_append(&l, NVN_OP_SET_STENCIL, &none, sizeof none));
    nvn_replay_list(&env, &l, &st);
    CHECK(k.write_count == 4);
    CHECK(k.writes[0].method == 0x4E5 && k.writes[0].value == 0xABu);
    CHECK(k.writes[1].method == 0x3D5 && k.writes[1].value == 0xCDu);
    CHECK(k.writes[2].method == 0x4E5 && k.writes[2].value == 0xEFu);
    CHECK(k.writes[3].method == 0x3D5 && k.writes[3].value == 0xEFu);
    CHECK(st.replayed[NVN_OP_SET_STENCIL] == 4 && st.methods == 4 && st.faults == 0);
    nvn_oplist_free(&l);
}

static void test_stencil_face_and_value_mask(void) {
    Mock k = {0};
    NvnReplayEnv env = make_env(&k);
    NvnOpList l = {0};
    NvnReplayStats st = {0};
    NvnOpStencil high = {NVN_STENCIL_VALUE_MASK, 1u, 0xFFFFFFFFu, 0u};
    NvnOpStencil ignored = {NVN_STENCIL_MASK, 0xFCu, 0x77u, 0u};
    NvnOpStencil all = {NVN_STENCIL_MASK, 0xFFu, 0x77u, 0u};
    CHECK(nvn_oplist_append(&l, NVN_OP_SET_STENCIL, &high, sizeof high));
    CHECK(nvn_oplist_append(&l, NVN_OP_SET_STENCIL, &ignored, sizeof ignored));
    CHECK(nvn_oplist_append(&l, NVN_OP_SET_STENCIL, &all, sizeof all));
    nvn_replay_list(&env, &l, &st);
    CHECK(k.write_count == 3);
    CHECK(k.writes[0].method == 0x4E6 && k.writes[0].value == 0xFFu);
    CHECK(k.writes[1].method == 0x4E7 && k.writes[1].value == 0x77u);
    CHECK(k.writes[2].method == 0x3D6 && k.writes[2].value == 0x77u);
    CHECK(st.replayed[NVN_OP_SET_STENCIL] == 3 && st.methods == 3 && st.faults == 0);
    nvn_oplist_free(&l);
}

static void test_stencil_kinds(void) {
    Mock k = {0};
    NvnReplayEnv env = make_env(&k);
    NvnOpList l = {0};
    NvnReplayStats st = {0};
    NvnOpStencil ref = {NVN_STENCIL_REF, 3u, 1u, 0u};
    NvnOpStencil value = {NVN_STENCIL_VALUE_MASK, 3u, 2u, 0u};
    NvnOpStencil mask = {NVN_STENCIL_MASK, 3u, 3u, 0u};
    CHECK(nvn_oplist_append(&l, NVN_OP_SET_STENCIL, &ref, sizeof ref));
    CHECK(nvn_oplist_append(&l, NVN_OP_SET_STENCIL, &value, sizeof value));
    CHECK(nvn_oplist_append(&l, NVN_OP_SET_STENCIL, &mask, sizeof mask));
    nvn_replay_list(&env, &l, &st);
    CHECK(k.write_count == 6);
    CHECK(k.writes[0].method == 0x4E5 && k.writes[0].value == 1u);
    CHECK(k.writes[1].method == 0x3D5 && k.writes[1].value == 1u);
    CHECK(k.writes[2].method == 0x4E6 && k.writes[2].value == 2u);
    CHECK(k.writes[3].method == 0x3D7 && k.writes[3].value == 2u);
    CHECK(k.writes[4].method == 0x4E7 && k.writes[4].value == 3u);
    CHECK(k.writes[5].method == 0x3D6 && k.writes[5].value == 3u);
    CHECK(st.methods == 6 && st.faults == 0);
    nvn_oplist_free(&l);
}

static void test_stencil_unknown_kind(void) {
    Mock k = {0};
    NvnReplayEnv env = make_env(&k);
    NvnOpList l = {0};
    NvnReplayStats st = {0};
    NvnOpStencil bad = {3u, 3u, 1u, 0u};
    CHECK(nvn_oplist_append(&l, NVN_OP_SET_STENCIL, &bad, sizeof bad));
    nvn_replay_list(&env, &l, &st);
    CHECK(st.faults == 1 && k.fault_count == 1 && k.last_fault_value == 3u);
    CHECK(st.replayed[NVN_OP_SET_STENCIL] == 0 && st.methods == 0);
    nvn_oplist_free(&l);
}

static void test_blend_color(void) {
    Mock k = {0};
    NvnReplayEnv env = make_env(&k);
    NvnOpList l = {0};
    NvnReplayStats st = {0};
    NvnOpBlendColor op = {{0x7FC00001u, 0xFFFFFFFFu, 0x3F800000u, 0x80000000u}};
    CHECK(nvn_oplist_append(&l, NVN_OP_SET_BLEND_COLOR, &op, sizeof op));
    nvn_replay_list(&env, &l, &st);
    CHECK(k.write_count == 4);
    CHECK(k.writes[0].method == 0x4C7 && k.writes[0].value == 0x7FC00001u);
    CHECK(k.writes[1].method == 0x4C8 && k.writes[1].value == 0xFFFFFFFFu);
    CHECK(k.writes[2].method == 0x4C9 && k.writes[2].value == 0x3F800000u);
    CHECK(k.writes[3].method == 0x4CA && k.writes[3].value == 0x80000000u);
    CHECK(st.replayed[NVN_OP_SET_BLEND_COLOR] == 1 && st.methods == 4 && st.faults == 0);
    nvn_oplist_free(&l);
}

static void test_alpha_ref(void) {
    Mock k = {0};
    NvnReplayEnv env = make_env(&k);
    NvnOpList l = {0};
    NvnReplayStats st = {0};
    NvnOpAlphaRef op = {0x3F000000u, 0u};
    CHECK(nvn_oplist_append(&l, NVN_OP_SET_ALPHA_REF, &op, sizeof op));
    nvn_replay_list(&env, &l, &st);
    CHECK(k.write_count == 1 && k.writes[0].method == 0x4C4 && k.writes[0].value == 0x3F000000u);
    CHECK(st.replayed[NVN_OP_SET_ALPHA_REF] == 1 && st.methods == 1 && st.faults == 0);
    nvn_oplist_free(&l);
}

static void test_raster_bad_payload(void) {
    Mock k = {0};
    NvnReplayEnv env = make_env(&k);
    NvnOpList l = {0};
    NvnReplayStats st = {0};
    uint8_t small[8] = {0};
    uint8_t big[16] = {0};
    CHECK(nvn_oplist_append(&l, NVN_OP_SET_STENCIL, small, sizeof small));
    CHECK(nvn_oplist_append(&l, NVN_OP_SET_BLEND_COLOR, small, sizeof small));
    CHECK(nvn_oplist_append(&l, NVN_OP_SET_ALPHA_REF, big, sizeof big));
    nvn_replay_list(&env, &l, &st);
    CHECK(st.faults == 3 && k.fault_count == 3 && k.last_fault_value == NVN_OP_SET_ALPHA_REF);
    CHECK(st.replayed[NVN_OP_SET_STENCIL] == 0 && st.replayed[NVN_OP_SET_BLEND_COLOR] == 0);
    CHECK(st.replayed[NVN_OP_SET_ALPHA_REF] == 0 && st.methods == 0);
    nvn_oplist_free(&l);
}

static void test_call_live(void) {
    Mock k = {0};
    NvnReplayEnv env = make_env(&k);
    NvnOpList target = {0};
    NvnOpList top = {0};
    NvnReplayStats st = {0};
    NvnOpBarrier barrier = {0x1u, 0u};
    NvnOpCall call = {0x100u};
    CHECK(nvn_oplist_append(&target, NVN_OP_BARRIER, &barrier, sizeof barrier));
    CHECK(nvn_oplist_append(&top, NVN_OP_CALL, &call, sizeof call));
    mock_add_list(&k, 0x100u, &target);
    nvn_replay_list(&env, &top, &st);
    CHECK(st.replayed[NVN_OP_CALL] == 1 && st.replayed[NVN_OP_BARRIER] == 1);
    CHECK(st.faults == 0 && st.methods == 0);
    nvn_oplist_free(&top);
    nvn_oplist_free(&target);
}

static void test_call_dead(void) {
    Mock k = {0};
    NvnReplayEnv env = make_env(&k);
    NvnOpList l = {0};
    NvnReplayStats st = {0};
    NvnOpCall call = {0xDEADu};
    CHECK(nvn_oplist_append(&l, NVN_OP_CALL, &call, sizeof call));
    nvn_replay_list(&env, &l, &st);
    CHECK(st.faults == 1 && k.fault_count == 1 && k.last_fault_value == 0xDEADu);
    CHECK(st.replayed[NVN_OP_CALL] == 0);
    nvn_oplist_free(&l);
}

static void test_call_depth(void) {
    enum { CHAIN = 10 };
    Mock k = {0};
    NvnReplayEnv env = make_env(&k);
    NvnOpList chain[CHAIN] = {0};
    uint64_t handles[CHAIN];
    NvnReplayStats st = {0};
    NvnOpBarrier barrier = {0u, 0u};
    for (int i = 0; i < CHAIN; i++) handles[i] = 0xA0u + (uint64_t)i;
    CHECK(nvn_oplist_append(&chain[CHAIN - 1], NVN_OP_BARRIER, &barrier, sizeof barrier));
    for (int i = CHAIN - 2; i >= 0; i--) {
        NvnOpCall call = {handles[i + 1]};
        CHECK(nvn_oplist_append(&chain[i], NVN_OP_CALL, &call, sizeof call));
    }
    for (int i = 0; i < CHAIN; i++) mock_add_list(&k, handles[i], &chain[i]);
    nvn_replay_list(&env, &chain[0], &st);
    CHECK(st.replayed[NVN_OP_CALL] == 8); /* the calls at depth 0 to 7 */
    CHECK(st.faults == 1 && st.replayed[NVN_OP_BARRIER] == 0);
    CHECK(k.last_fault_value == handles[CHAIN - 1]); /* the call at depth 8 */
    for (int i = 0; i < CHAIN; i++) nvn_oplist_free(&chain[i]);
}

static void test_bad_payload(void) {
    Mock k = {0};
    NvnReplayEnv env = make_env(&k);
    NvnOpList l = {0};
    NvnReplayStats st = {0};
    uint8_t too_big[16] = {0};
    CHECK(nvn_oplist_append(&l, NVN_OP_BARRIER, too_big, sizeof too_big));
    nvn_replay_list(&env, &l, &st);
    CHECK(st.faults == 1 && k.last_fault_value == NVN_OP_BARRIER);
    CHECK(st.replayed[NVN_OP_BARRIER] == 0 && st.methods == 0);
    nvn_oplist_free(&l);
}

static void test_unknown_op(void) {
    Mock k = {0};
    NvnReplayEnv env = make_env(&k);
    NvnReplayStats st = {0};
    uint8_t raw[sizeof(NvnOpHeader)] = {0};
    NvnOpHeader header = {0xFFu, 0u};
    memcpy(raw, &header, sizeof header);
    NvnOpList l = {raw, sizeof raw, sizeof raw, 1};
    nvn_replay_list(&env, &l, &st);
    CHECK(st.faults == 1 && k.last_fault_value == 0xFFu);
    CHECK(st.methods == 0 && st.replayed[NVN_OP_NONE] == 0);
}

static void test_index_range(void) {
    Mock k = {0};
    NvnReplayEnv env = make_env(&k);
    NvnOpList l = {0};
    NvnReplayStats st = {0};
    NvnOpViewport vp = {NVN_VIEWPORTS_MAX, 0.0f, 0.0f, 1.0f, 1.0f};
    NvnOpScissor sc = {NVN_VIEWPORTS_MAX, 0, 0, 1, 1};
    NvnOpClearColor cc = {NVN_RENDER_TARGETS_MAX, 0xFu, {1u, 2u, 3u, 4u}};
    CHECK(nvn_oplist_append(&l, NVN_OP_SET_VIEWPORT, &vp, sizeof vp));
    CHECK(nvn_oplist_append(&l, NVN_OP_SET_SCISSOR, &sc, sizeof sc));
    CHECK(nvn_oplist_append(&l, NVN_OP_CLEAR_COLOR, &cc, sizeof cc));
    nvn_replay_list(&env, &l, &st);
    CHECK(st.faults == 3 && k.fault_count == 3 && st.methods == 0);
    CHECK(st.replayed[NVN_OP_SET_VIEWPORT] == 0 && st.replayed[NVN_OP_SET_SCISSOR] == 0);
    CHECK(st.replayed[NVN_OP_CLEAR_COLOR] == 0);
    nvn_oplist_free(&l);
}

static void test_viewport_non_finite(void) {
    Mock k = {0};
    NvnReplayEnv env = make_env(&k);
    NvnOpList l = {0};
    NvnReplayStats st = {0};
    NvnOpViewport nan_vp = {0, 0.0f, 0.0f, NAN, 1.0f};
    NvnOpViewport inf_vp = {1, 0.0f, 0.0f, 1.0f, INFINITY};
    CHECK(nvn_oplist_append(&l, NVN_OP_SET_VIEWPORT, &nan_vp, sizeof nan_vp));
    CHECK(nvn_oplist_append(&l, NVN_OP_SET_VIEWPORT, &inf_vp, sizeof inf_vp));
    nvn_replay_list(&env, &l, &st);
    CHECK(st.faults == 2 && k.fault_count == 2 && st.methods == 0);
    CHECK(st.replayed[NVN_OP_SET_VIEWPORT] == 0);
    nvn_oplist_free(&l);
}

static void test_stats_accumulate(void) {
    Mock k = {0};
    NvnReplayEnv env = make_env(&k);
    NvnOpList l = {0};
    NvnReplayStats st = {0};
    NvnOpBarrier barrier = {0x1u, 0u};
    CHECK(nvn_oplist_append(&l, NVN_OP_BARRIER, &barrier, sizeof barrier));
    nvn_replay_list(&env, &l, &st);
    nvn_replay_list(&env, &l, &st); /* stats is added to, not cleared */
    CHECK(st.replayed[NVN_OP_BARRIER] == 2 && st.barrier_bits[0] == 2 && st.faults == 0);
    nvn_oplist_free(&l);
}

int main(void) {
    test_viewport();
    test_viewport_flip();
    test_viewport_fractional();
    test_scissor();
    test_scissor_clamp();
    test_clear_layers();
    test_clear_layer_limit();
    test_pool();
    test_render_targets();
    test_bind_texture();
    test_bind_buffer();
    test_bind_state();
    test_barrier();
    test_stencil_ref();
    test_stencil_face_and_value_mask();
    test_stencil_kinds();
    test_stencil_unknown_kind();
    test_blend_color();
    test_alpha_ref();
    test_raster_bad_payload();
    test_call_live();
    test_call_dead();
    test_call_depth();
    test_bad_payload();
    test_unknown_op();
    test_index_range();
    test_viewport_non_finite();
    test_stats_accumulate();
    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("nvn_replay: ok\n");
    return 0;
}
