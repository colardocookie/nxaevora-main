/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#include "nx/gpu/nvn_rt.h"

/* Maxwell 3D methods and values of nvnCommandBufferSetRenderTargets (d855da0). */
#define NVN_RT_CONTROL_METHOD       0x487u
#define NVN_RT_CONTROL_BASE         0x0FAC6880u
#define NVN_RT_CONTROL_COUNT_MASK   0xFu
#define NVN_RT_COLOR_BLOCK_BASE     0x200u
#define NVN_RT_COLOR_DISABLE_BASE   0x204u
#define NVN_RT_COLOR_STRIDE         0x10u
#define NVN_RT_DEPTH_A              0x54Eu
#define NVN_RT_DEPTH_B              0xD28u
#define NVN_RT_CLIP_X               0x3FDu
#define NVN_RT_CLIP_Y               0x3FEu
#define NVN_RT_SAMPLE_MODE          0x574u

#define NVN_RT_WORDS_PER_TARGET     8u
#define NVN_RT_CLIP_MAX             0xFFFFu
#define NVN_RT_CLIP_SHIFT           16u
#define NVN_RT_TILE_NIBBLE          0xFu
#define NVN_RT_TILE_Y_SHIFT         4u
#define NVN_RT_TILE_Z_SHIFT         8u
#define NVN_RT_LAYER_STRIDE_SHIFT   2u
#define NVN_RT_SAMPLE_INDEX_MAX     7u

static uint32_t rt_align_up(uint32_t value, uint32_t multiple) {
    return ((value + multiple - 1u) / multiple) * multiple;
}

static uint32_t rt_clip_word(uint32_t minimum, uint32_t limit) {
    if (minimum < 1u || limit < 1u) return 0u;
    return (minimum < limit ? minimum : limit) << NVN_RT_CLIP_SHIFT;
}

static uint32_t rt_sample_mode(uint32_t code) {
    static const uint8_t modes[8] = {5u, 2u, 0u, 4u, 0u, 0u, 0u, 6u};
    uint32_t index = ((code >> 1) | (code << 31)) - 1u;
    return index <= NVN_RT_SAMPLE_INDEX_MAX ? modes[index] : 0u;
}

/* One target: check it and compute its eight words. The fault callback reports the first bad part. */
static int rt_target_words(const NvnRtEnv* env, const NvnRtColor* color, uint32_t word[NVN_RT_WORDS_PER_TARGET]) {
    const NvnTexture* texture = color->texture;
    const NvnTextureView* view = color->view;
    const NvnTextureLayout* layout = &texture->layout;
    if (view && view->flags != 0u) {
        env->fault(env->ctx, "rt view flags", view->flags);
        return 0;
    }
    if (texture->builder.target != NVN_TEXTURE_TARGET_2D) {
        env->fault(env->ctx, "rt target", texture->builder.target);
        return 0;
    }
    if (texture->builder.levels > 1u) {
        env->fault(env->ctx, "rt levels", texture->builder.levels);
        return 0;
    }
    if (texture->builder.flags & NVN_TEXTURE_LINEAR_FLAGS) {
        env->fault(env->ctx, "rt linear", texture->builder.flags);
        return 0;
    }
    uint32_t format_word = nvn_rt_format_word(texture->builder.format);
    if (format_word == 0u) {
        env->fault(env->ctx, "rt format", texture->builder.format);
        return 0;
    }
    if (layout->block_width != 1u || layout->block_height != 1u) {
        env->fault(env->ctx, "rt block", texture->builder.format);
        return 0;
    }
    if (layout->bytes_per_block == 0u || layout->bytes_per_block > NVN_TEXTURE_GOB_WIDTH) {
        env->fault(env->ctx, "rt bytes per block", layout->bytes_per_block);
        return 0;
    }
    uint32_t width = layout->width * layout->sample_x;
    uint32_t height = layout->height * layout->sample_y;
    uint32_t depth = layout->depth;
    uint32_t tile_height = NVN_TEXTURE_GOB_HEIGHT << (layout->tile_y & 31u);
    if (height < tile_height || (depth >> (layout->tile_z & 31u)) == 0u) {
        env->fault(env->ctx, "rt small target", height);
        return 0;
    }
    uint32_t gob_blocks = NVN_TEXTURE_GOB_WIDTH / layout->bytes_per_block;
    uint32_t factor = width < (gob_blocks << (layout->tile_x & 31u)) ? 1u : (1u << (layout->tile_x & 31u));
    word[0] = (uint32_t)(texture->address >> 32);
    word[1] = (uint32_t)texture->address;
    word[2] = rt_align_up(width, factor * gob_blocks);
    word[3] = height;
    word[4] = format_word;
    word[5] = ((uint32_t)(layout->tile_y & NVN_RT_TILE_NIBBLE) << NVN_RT_TILE_Y_SHIFT) |
              ((uint32_t)(layout->tile_z & NVN_RT_TILE_NIBBLE) << NVN_RT_TILE_Z_SHIFT);
    word[6] = depth;
    word[7] = (uint32_t)(layout->layer_stride >> NVN_RT_LAYER_STRIDE_SHIFT);
    return 1;
}

int nvn_rt_emit(const NvnRtEnv* env, const NvnRtColor* color, uint32_t count, int has_depth, uint32_t clip_limit) {
    if (!env || !env->method || !env->fault) return 0;
    if (count > NVN_RT_COLOR_MAX) {
        env->fault(env->ctx, "rt count", count);
        return 0;
    }
    if (count > 0u && !color) {
        env->fault(env->ctx, "rt no color array", count);
        return 0;
    }
    if (has_depth) {
        env->fault(env->ctx, "rt depth", (uint64_t)has_depth);
        return 0;
    }
    uint32_t words[NVN_RT_COLOR_MAX][NVN_RT_WORDS_PER_TARGET];
    uint8_t present[NVN_RT_COLOR_MAX] = {0};
    uint32_t min_width = NVN_RT_CLIP_MAX;
    uint32_t min_height = NVN_RT_CLIP_MAX;
    uint32_t samples = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (!color[i].texture) continue;
        if (!rt_target_words(env, &color[i], words[i])) return 0;
        present[i] = 1;
        const NvnTextureLayout* layout = &color[i].texture->layout;
        if (layout->width < min_width) min_width = layout->width;
        if (layout->height < min_height) min_height = layout->height;
        samples = layout->samples;
    }
    uint32_t clip_x = rt_clip_word(min_width, clip_limit);
    uint32_t clip_y = rt_clip_word(min_height, clip_limit);
    uint32_t mode = rt_sample_mode(samples);
    uint32_t control = NVN_RT_CONTROL_BASE | (count & NVN_RT_CONTROL_COUNT_MASK);
    env->method(env->ctx, NVN_RT_CONTROL_METHOD, control);
    for (uint32_t i = 0; i < count; i++) {
        if (!present[i]) {
            env->method(env->ctx, NVN_RT_COLOR_DISABLE_BASE + NVN_RT_COLOR_STRIDE * i, 0u);
            continue;
        }
        for (uint32_t k = 0; k < NVN_RT_WORDS_PER_TARGET; k++)
            env->method(env->ctx, NVN_RT_COLOR_BLOCK_BASE + NVN_RT_COLOR_STRIDE * i + k, words[i][k]);
    }
    for (uint32_t j = count; j < NVN_RT_COLOR_MAX; j++)
        env->method(env->ctx, NVN_RT_COLOR_DISABLE_BASE + NVN_RT_COLOR_STRIDE * j, 0u);
    env->method(env->ctx, NVN_RT_DEPTH_A, 0u);
    env->method(env->ctx, NVN_RT_DEPTH_B, 0u);
    env->method(env->ctx, NVN_RT_CLIP_X, clip_x);
    env->method(env->ctx, NVN_RT_CLIP_Y, clip_y);
    env->method(env->ctx, NVN_RT_SAMPLE_MODE, mode);
    return 1;
}
