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
#define NVN_RT_DEPTH_ENABLE         1u
#define NVN_RT_DEPTH_BIND_BASE      0x3F8u
#define NVN_RT_DEPTH_SIZE_BASE      0x48Au
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
#define NVN_RT_SHIFT_MASK           31u
#define NVN_RT_DEPTH_WORD_MASK      0xFFFFu
#define NVN_RT_VIEW_FLAGS           (NVN_VIEW_LEVELS | NVN_VIEW_LAYERS | NVN_VIEW_FORMAT | \
                                     NVN_VIEW_SWIZZLE | NVN_VIEW_DEPTH_STENCIL_MODE | NVN_VIEW_TARGET)
#define NVN_RT_TILE_3D_BIT          0x10000u
#define NVN_RT_VIEW_TARGET_LAST     NVN_TEXTURE_TARGET_BUFFER

/* The fixed words and headers of nvnCommandBufferClearTexture (d857610, verify-s4-4b.md). */
#define NVN_RT_HEADER_INCREASING    0x20000000u /* mode 1: a run of count data words follows */
#define NVN_RT_HEADER_INLINE        0x80000000u /* mode 4: the value is in the count field */
#define NVN_RT_HEADER_INCREASE_ONCE 0xA0000000u /* mode 5: a macro of count data words follows */
#define NVN_RT_HEADER_COUNT_SHIFT   16u
#define NVN_RT_HEADER_VALUE_MAX     0x1FFFu
#define NVN_RT_CLEAR_OPEN           0x91080002u
#define NVN_RT_CLEAR_CLOSE          0x91000002u
#define NVN_RT_CLEAR_COLOR_METHOD   0x360u
#define NVN_RT_CLEAR_IMMD_METHOD    0x049u
#define NVN_RT_CLEAR_MACRO_METHOD   0xE1Eu
#define NVN_RT_CLEAR_MASK_METHOD    0x674u
#define NVN_RT_CLEAR_COLOR_WORDS    4u
#define NVN_RT_CLEAR_MACRO_WORDS    4u
#define NVN_RT_CLEAR_MASK_NIBBLE    0xFu
#define NVN_RT_CLEAR_MASK_SHIFT     2u
#define NVN_RT_CLEAR_FORMAT_SHIFT   24u
#define NVN_RT_CLEAR_LAYER_LIMIT    0xFFFFu

#define NVN_RT_CLEAR_RUN(method, count) \
    (NVN_RT_HEADER_INCREASING | ((count) << NVN_RT_HEADER_COUNT_SHIFT) | (method))
#define NVN_RT_CLEAR_VALUE(method, value) \
    (NVN_RT_HEADER_INLINE | (((value) & NVN_RT_HEADER_VALUE_MAX) << NVN_RT_HEADER_COUNT_SHIFT) | (method))
#define NVN_RT_CLEAR_MACRO(method, count) \
    (NVN_RT_HEADER_INCREASE_ONCE | ((count) << NVN_RT_HEADER_COUNT_SHIFT) | (method))

/* The fixed words and methods of nvnCommandBufferCopyTextureToTexture (d82bfd0, d82c5d0, verify-s4-4b.md). */
#define NVN_RT_COPY_OPEN             0x91030002u
#define NVN_RT_COPY_CLOSE            0x91000002u
#define NVN_RT_COPY_SRC_METHOD       0x608Cu
#define NVN_RT_COPY_SRC_SIZE_METHOD  0x6092u
#define NVN_RT_COPY_ADDRESS_METHOD   0x6094u
#define NVN_RT_COPY_DST_METHOD       0x6080u
#define NVN_RT_COPY_DST_SIZE_METHOD  0x6086u
#define NVN_RT_COPY_DST_ADDR_METHOD  0x6088u
#define NVN_RT_COPY_IMMD_METHOD      0x60B5u
#define NVN_RT_COPY_FLAGS_METHOD     0x6223u
#define NVN_RT_COPY_RECT_METHOD      0x622Cu
#define NVN_RT_COPY_SCALE_METHOD     0x6230u
#define NVN_RT_COPY_OFFSET_METHOD    0x6234u
#define NVN_RT_COPY_SRC_WORDS        4u
#define NVN_RT_COPY_DST_WORDS        5u
#define NVN_RT_COPY_PAIR_WORDS       2u
#define NVN_RT_COPY_RECT_WORDS       4u
#define NVN_RT_COPY_FLAGS_WORDS      1u
#define NVN_RT_COPY_IMMD_VALUE       1u
#define NVN_RT_COPY_FLAGS_MASK       0x1u   /* only bit0 is in this step */
#define NVN_RT_COPY_FLAGS_VALUE      0x10u
#define NVN_RT_COPY_FLAGS_BASE       1u
#define NVN_RT_COPY_RATIO_UNIT       0x8000
#define NVN_RT_COPY_RATIO_SHIFT      15u
#define NVN_RT_COPY_FRACTION_SHIFT   17u    /* 32 - NVN_RT_COPY_RATIO_SHIFT */
#define NVN_RT_COPY_RATIO_INT_SHIFT  12u
#define NVN_RT_COPY_ORIGIN_SHIFT     4u
#define NVN_RT_COPY_OFFSET_SHIFT     28u
#define NVN_RT_COPY_OFFSET_RIGHT     4u
#define NVN_RT_COPY_LAYER_LIMIT      0xFFFFu
#define NVN_RT_COPY_FORMAT_0X37      0x37u
#define NVN_RT_COPY_FORMAT_0X38      0x38u
#define NVN_RT_COPY_FORMAT_0X6D      0x6Du
#define NVN_RT_COPY_FORMAT_0X25      0x25u

#define NVN_RT_COPY_RUN(method, count) \
    (NVN_RT_HEADER_INCREASING | ((count) << NVN_RT_HEADER_COUNT_SHIFT) | (method))
#define NVN_RT_COPY_VALUE(method, value) \
    (NVN_RT_HEADER_INLINE | (((value) & NVN_RT_HEADER_VALUE_MAX) << NVN_RT_HEADER_COUNT_SHIFT) | (method))

typedef struct {
    uint64_t address;
    uint32_t width, height, depth, format, tile, layer_stride_words;
} NvnRtDepth;

typedef struct {
    uint64_t address;
    uint32_t depth_word;
    int target_3d;
} NvnRtViewResult;

typedef struct {
    uint32_t aligned_width, height, tile, depth_word;
} NvnRtCopySurface;

static uint32_t rt_align_up(uint32_t value, uint32_t multiple) {
    return ((value + multiple - 1u) / multiple) * multiple;
}

static uint32_t rt_clip_word(uint32_t minimum, uint32_t limit) {
    if (minimum < 1u || limit < 1u) return 0u;
    return (minimum < limit ? minimum : limit) << NVN_RT_CLIP_SHIFT;
}

/* One ClearTexture clip word: the origin in the low 16 bits and the span above it. The library compares the wrapped
   int32 values, so a negative origin moves the span start back. origin and span are already sample scaled. */
static uint32_t rt_clear_clip_word(uint32_t origin, uint32_t span, uint32_t limit) {
    int32_t p = (int32_t)origin;
    uint32_t low = p < 0 ? (uint32_t)p : 0u;          /* min(origin, 0) */
    uint32_t start = p < 0 ? 0u : origin;             /* max(origin, 0) */
    int32_t reached = (int32_t)(span + low);
    if (reached < 1 || (int32_t)(limit - start) <= 0) return 0u;
    int32_t clipped = (int32_t)(span + low + start) < (int32_t)limit ? reached : (int32_t)(limit - start);
    return start | ((uint32_t)clipped << NVN_RT_CLIP_SHIFT);
}

static uint32_t rt_sample_mode(uint32_t code) {
    static const uint8_t modes[8] = {5u, 2u, 0u, 4u, 0u, 0u, 0u, 6u};
    uint32_t index = ((code >> 1) | (code << 31)) - 1u;
    return index <= NVN_RT_SAMPLE_INDEX_MAX ? modes[index] : 0u;
}

/* d869d10: the tile row count of a block linear layout. A target below the tile size gets 1. */
static uint32_t rt_tile_factor(const NvnTextureLayout* layout, uint32_t width, uint32_t height, uint32_t depth) {
    uint32_t gob_blocks = NVN_TEXTURE_GOB_WIDTH / layout->bytes_per_block;
    uint32_t tile_x = layout->tile_x & NVN_RT_SHIFT_MASK;
    uint32_t tile_y = layout->tile_y & NVN_RT_SHIFT_MASK;
    uint32_t tile_z = layout->tile_z & NVN_RT_SHIFT_MASK;
    uint32_t factor = 1u << tile_x;
    if ((uint64_t)width < ((uint64_t)gob_blocks << tile_x)) return 1u;
    if ((uint64_t)height < ((uint64_t)NVN_TEXTURE_GOB_HEIGHT << tile_y)) return 1u;
    if ((depth >> tile_z) == 0u) return 1u;
    return factor;
}

static uint32_t rt_tile_word(const NvnTextureLayout* layout) {
    return ((uint32_t)layout->tile_x & NVN_RT_TILE_NIBBLE) |
           (((uint32_t)layout->tile_y & NVN_RT_TILE_NIBBLE) << NVN_RT_TILE_Y_SHIFT) |
           (((uint32_t)layout->tile_z & NVN_RT_TILE_NIBBLE) << NVN_RT_TILE_Z_SHIFT);
}

/* The checks that a color and a depth target share. *format_word is set when 1 is returned. */
static int rt_texture_checks(const NvnRtEnv* env, const NvnTexture* texture, const char* linear_fault,
                             uint32_t* format_word) {
    const NvnTextureLayout* layout = &texture->layout;
    if (texture->builder.target != NVN_TEXTURE_TARGET_2D && texture->builder.target != NVN_TEXTURE_TARGET_2D_ARRAY) {
        env->fault(env->ctx, "rt target", texture->builder.target);
        return 0;
    }
    if (texture->builder.levels > 1u) {
        env->fault(env->ctx, "rt levels", texture->builder.levels);
        return 0;
    }
    if (texture->builder.flags & NVN_TEXTURE_LINEAR_FLAGS) {
        env->fault(env->ctx, linear_fault, texture->builder.flags);
        return 0;
    }
    uint32_t word = nvn_rt_format_word(texture->builder.format);
    if (word == 0u) {
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
    *format_word = word;
    return 1;
}

/* The address, the depth word and the 3D bit of a view, shared by the color and the depth target.
   The library reads LEVELS, LAYERS, FORMAT and TARGET. It does not read level_count, and it wraps
   when the base layer and the count pass the texture depth; the host refuses that case. A refusal
   writes no part of *out. */
static int rt_view_apply(const NvnRtEnv* env, const NvnTexture* texture, const NvnTextureView* view,
                         NvnRtViewResult* out) {
    const NvnTextureLayout* layout = &texture->layout;
    uint64_t address = texture->address;
    uint32_t depth_word = layout->depth;
    uint32_t target = texture->builder.target;
    if (!view || view->flags == 0u) {
        out->address = address;
        out->depth_word = depth_word;
        out->target_3d = target == NVN_TEXTURE_TARGET_3D;
        return 1;
    }
    if (view->flags & (uint8_t)~NVN_RT_VIEW_FLAGS) {
        env->fault(env->ctx, "rt view flags", view->flags);
        return 0;
    }
    if ((view->flags & NVN_VIEW_LEVELS) && view->base_level != 0u) {
        env->fault(env->ctx, "rt view levels", view->base_level);
        return 0;
    }
    if (view->flags & NVN_VIEW_LAYERS) {
        if (view->layer_count != 0u) {
            uint64_t layers = (uint64_t)view->base_layer + view->layer_count;
            if (layers > layout->depth) {
                env->fault(env->ctx, "rt view layers", layers);
                return 0;
            }
            depth_word = view->layer_count;
        } else if (view->base_layer != 0u) {
            if (view->base_layer > layout->depth) {
                env->fault(env->ctx, "rt view layers", view->base_layer);
                return 0;
            }
            depth_word = layout->depth - view->base_layer;
        }
        uint64_t offset = (uint64_t)view->base_layer * layout->layer_stride;
        if (offset > UINT64_MAX - address) {
            env->fault(env->ctx, "rt view address", address);
            return 0;
        }
        address += offset;
    }
    if ((view->flags & NVN_VIEW_FORMAT) && view->format != texture->builder.format) {
        env->fault(env->ctx, "rt view format", view->format);
        return 0;
    }
    if (view->flags & NVN_VIEW_TARGET) target = view->target;
    out->address = address;
    out->depth_word = depth_word;
    out->target_3d = target == NVN_TEXTURE_TARGET_3D;
    return 1;
}

/* One target: check it and compute its eight words. The fault callback reports the first bad part. */
static int rt_target_words(const NvnRtEnv* env, const NvnRtColor* color, uint32_t word[NVN_RT_WORDS_PER_TARGET]) {
    const NvnTexture* texture = color->texture;
    const NvnTextureLayout* layout = &texture->layout;
    uint32_t format_word = 0;
    if (!rt_texture_checks(env, texture, "rt linear", &format_word)) return 0;
    if (color->view && (color->view->flags & NVN_VIEW_TARGET) &&
        color->view->target > NVN_RT_VIEW_TARGET_LAST) {
        env->fault(env->ctx, "rt view target", color->view->target);
        return 0;
    }
    NvnRtViewResult view;
    if (!rt_view_apply(env, texture, color->view, &view)) return 0;
    uint32_t width = layout->width * layout->sample_x;
    uint32_t height = layout->height * layout->sample_y;
    uint32_t gob_blocks = NVN_TEXTURE_GOB_WIDTH / layout->bytes_per_block;
    uint32_t factor = rt_tile_factor(layout, width, height, layout->depth);
    word[0] = (uint32_t)(view.address >> 32);
    word[1] = (uint32_t)view.address;
    word[2] = rt_align_up(width, factor * gob_blocks);
    word[3] = height;
    word[4] = format_word;
    word[5] = rt_tile_word(layout) | (view.target_3d ? NVN_RT_TILE_3D_BIT : 0u);
    word[6] = view.depth_word;
    word[7] = (uint32_t)(layout->layer_stride >> NVN_RT_LAYER_STRIDE_SHIFT);
    return 1;
}

/* The depth surface: check it and compute its words. The view rules are the color rules; the depth
   word is the low 16 bits and the 3D tile bit has no depth part. */
static int rt_depth_words(const NvnRtEnv* env, const NvnRtColor* depth, NvnRtDepth* out) {
    const NvnTexture* texture = depth->texture;
    const NvnTextureLayout* layout = &texture->layout;
    uint32_t format_word = 0;
    if (!rt_texture_checks(env, texture, "rt depth linear", &format_word)) return 0;
    NvnRtViewResult view;
    if (!rt_view_apply(env, texture, depth->view, &view)) return 0;
    if (view.depth_word > NVN_RT_DEPTH_WORD_MASK) {
        env->fault(env->ctx, "rt depth layers", view.depth_word);
        return 0;
    }
    uint32_t width = layout->width * layout->sample_x;
    uint32_t height = layout->height * layout->sample_y;
    uint32_t gob_blocks = NVN_TEXTURE_GOB_WIDTH / layout->bytes_per_block;
    uint32_t factor = rt_tile_factor(layout, width, height, layout->depth);
    out->address = view.address;
    out->width = rt_align_up(width, factor * gob_blocks);
    out->height = height;
    out->depth = view.depth_word & NVN_RT_DEPTH_WORD_MASK;
    out->format = format_word;
    out->tile = rt_tile_word(layout);
    out->layer_stride_words = (uint32_t)(layout->layer_stride >> NVN_RT_LAYER_STRIDE_SHIFT);
    return 1;
}

/* The depth bind words, the 0/1 pair, then the depth size words (order of d855da0). The library writes 0 only when
   there is no depth target. The depth enable meaning is an inference from that call order. */
static void rt_depth_emit(const NvnRtEnv* env, const NvnRtDepth* depth) {
    env->method(env->ctx, NVN_RT_DEPTH_BIND_BASE + 0u, (uint32_t)(depth->address >> 32));
    env->method(env->ctx, NVN_RT_DEPTH_BIND_BASE + 1u, (uint32_t)depth->address);
    env->method(env->ctx, NVN_RT_DEPTH_BIND_BASE + 2u, depth->format);
    env->method(env->ctx, NVN_RT_DEPTH_BIND_BASE + 3u, depth->tile);
    env->method(env->ctx, NVN_RT_DEPTH_BIND_BASE + 4u, depth->layer_stride_words);
    env->method(env->ctx, NVN_RT_DEPTH_A, 0u);
    env->method(env->ctx, NVN_RT_DEPTH_A, NVN_RT_DEPTH_ENABLE);
    env->method(env->ctx, NVN_RT_DEPTH_SIZE_BASE + 0u, depth->width);
    env->method(env->ctx, NVN_RT_DEPTH_SIZE_BASE + 1u, depth->height);
    env->method(env->ctx, NVN_RT_DEPTH_SIZE_BASE + 2u, depth->depth);
}

int nvn_rt_emit(const NvnRtEnv* env, const NvnRtColor* color, uint32_t count, const NvnRtColor* depth,
                uint32_t clip_limit) {
    if (!env || !env->method || !env->fault) return 0;
    if (count > NVN_RT_COLOR_MAX) {
        env->fault(env->ctx, "rt count", count);
        return 0;
    }
    if (count > 0u && !color) {
        env->fault(env->ctx, "rt no color array", count);
        return 0;
    }
    uint32_t words[NVN_RT_COLOR_MAX][NVN_RT_WORDS_PER_TARGET];
    uint8_t present[NVN_RT_COLOR_MAX] = {0};
    uint32_t min_width = NVN_RT_CLIP_MAX;
    uint32_t min_height = NVN_RT_CLIP_MAX;
    uint32_t samples = 0;
    int has_color = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (!color[i].texture) continue;
        if (!rt_target_words(env, &color[i], words[i])) return 0;
        present[i] = 1;
        has_color = 1;
        const NvnTextureLayout* layout = &color[i].texture->layout;
        if (layout->width < min_width) min_width = layout->width;
        if (layout->height < min_height) min_height = layout->height;
        samples = layout->samples;
    }
    NvnRtDepth depth_words;
    int has_depth = depth && depth->texture;
    if (has_depth) {
        if (!rt_depth_words(env, depth, &depth_words)) return 0;
        const NvnTextureLayout* layout = &depth->texture->layout;
        if (layout->width < min_width) min_width = layout->width;
        if (layout->height < min_height) min_height = layout->height;
        if (!has_color) samples = layout->samples;
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
    /* The dependent block (0x1F8, 0x572) is not written: its words depend on compression and zcull,
       which the host does not model, and only a device mode accepts no block (explorer section 4).
       replay_host_render_targets logs the omission once. */
    if (has_depth) {
        rt_depth_emit(env, &depth_words);
    } else {
        env->method(env->ctx, NVN_RT_DEPTH_A, 0u);
        env->method(env->ctx, NVN_RT_DEPTH_B, 0u);
    }
    env->method(env->ctx, NVN_RT_CLIP_X, clip_x);
    env->method(env->ctx, NVN_RT_CLIP_Y, clip_y);
    env->method(env->ctx, NVN_RT_SAMPLE_MODE, mode);
    return 1;
}

uint32_t nvn_rt_clear_texture_words(const NvnRtEnv* env, const NvnTexture* texture,
                                    const NvnTextureView* view, const uint32_t region[6],
                                    const uint32_t color[4], uint32_t mask, uint32_t clip_limit,
                                    int* nothing, uint32_t* out) {
    if (nothing) *nothing = 0;
    if (!env || !env->fault || !out || !nothing || !texture || !region || !color) return 0;
    uint32_t format_word = 0;
    if (!rt_texture_checks(env, texture, "rt clear linear", &format_word)) return 0;
    uint32_t layers = region[5];
    uint32_t width_count = region[3];
    uint32_t rows = region[4];
    if (layers == 0u || width_count == 0u || rows == 0u) {
        *nothing = 1;
        return 0;
    }
    if (layers > NVN_RT_CLEAR_LAYERS_MAX) {
        env->fault(env->ctx, "clear layers", layers);
        return 0;
    }

    const NvnTextureLayout* layout = &texture->layout;
    uint32_t view_base = (view && (view->flags & NVN_VIEW_LAYERS)) ? view->base_layer : 0u;
    uint32_t base_level = (view && (view->flags & NVN_VIEW_LEVELS)) ? view->base_level : 0u;
    NvnTextureView layer_view = {0};
    layer_view.flags = NVN_VIEW_LEVELS | NVN_VIEW_LAYERS;
    layer_view.base_level = (uint8_t)base_level;
    layer_view.level_count = 1;
    layer_view.layer_count = 1;

    /* The addresses are made first, so that a late refusal writes no part of out. */
    uint32_t layer_arg1[NVN_RT_CLEAR_LAYERS_MAX];
    for (uint32_t i = 0; i < layers; i++) {
        uint64_t layer = (uint64_t)region[2] + view_base + i;
        if (layer > NVN_RT_CLEAR_LAYER_LIMIT) {
            env->fault(env->ctx, "clear layers", layer);
            return 0;
        }
        layer_view.base_layer = (uint16_t)layer;
        NvnRtViewResult result;
        if (!rt_view_apply(env, texture, &layer_view, &result)) return 0;
        layer_arg1[i] = (uint32_t)(result.address >> 8);
    }

    uint32_t width = layout->width * layout->sample_x;
    uint32_t height = layout->height * layout->sample_y;
    uint32_t gob_blocks = NVN_TEXTURE_GOB_WIDTH / layout->bytes_per_block;
    uint32_t factor = rt_tile_factor(layout, width, height, layout->depth);
    uint32_t aligned_width = rt_align_up(width, factor * gob_blocks);
    uint32_t surface = rt_tile_word(layout) | ((format_word & 0xFFu) << NVN_RT_CLEAR_FORMAT_SHIFT);
    uint32_t clip_x = rt_clear_clip_word(layout->sample_x * region[0], layout->sample_x * width_count,
                                         clip_limit);
    uint32_t clip_y = rt_clear_clip_word(layout->sample_y * region[1], layout->sample_y * rows, clip_limit);
    uint32_t mask_word = (mask & NVN_RT_CLEAR_MASK_NIBBLE) << NVN_RT_CLEAR_MASK_SHIFT;

    uint32_t at = 0;
    out[at++] = NVN_RT_CLEAR_OPEN;
    out[at++] = NVN_RT_CLEAR_RUN(NVN_RT_CLEAR_COLOR_METHOD, NVN_RT_CLEAR_COLOR_WORDS);
    for (uint32_t i = 0; i < NVN_RT_CLEAR_COLOR_WORDS; i++) out[at++] = color[i];
    out[at++] = NVN_RT_CLEAR_VALUE(NVN_RT_CLEAR_IMMD_METHOD, 2u);
    for (uint32_t i = 0; i < layers; i++) {
        out[at++] = NVN_RT_CLEAR_MACRO(NVN_RT_CLEAR_MACRO_METHOD, NVN_RT_CLEAR_MACRO_WORDS);
        out[at++] = layer_arg1[i];
        out[at++] = aligned_width;
        out[at++] = height;
        out[at++] = surface;
        out[at++] = NVN_RT_CLEAR_RUN(NVN_RT_CLIP_X, 2u);
        out[at++] = clip_x;
        out[at++] = clip_y;
        out[at++] = NVN_RT_CLEAR_RUN(NVN_RT_CLEAR_MASK_METHOD, 1u);
        out[at++] = mask_word;
    }
    out[at++] = NVN_RT_CLEAR_VALUE(NVN_RT_CLEAR_IMMD_METHOD, 3u);
    out[at++] = NVN_RT_CLEAR_MACRO(NVN_RT_CLEAR_MACRO_METHOD, NVN_RT_CLEAR_MACRO_WORDS);
    out[at++] = layer_arg1[layers - 1u];
    out[at++] = aligned_width;
    out[at++] = height;
    out[at++] = surface;
    out[at++] = NVN_RT_CLEAR_VALUE(NVN_RT_CLEAR_IMMD_METHOD, 1u);
    out[at++] = NVN_RT_CLEAR_CLOSE;
    return at;
}

/* The checks that the two copy surfaces share. The format word is read after the copy format change, so it is not
   part of these checks: rt_texture_checks reads the render target column +0x1C and the copy takes +0x18. */
static int rt_copy_checks(const NvnRtCopyEnv* env, const NvnTexture* texture) {
    const NvnTextureLayout* layout = &texture->layout;
    if (texture->builder.target != NVN_TEXTURE_TARGET_2D &&
        texture->builder.target != NVN_TEXTURE_TARGET_2D_ARRAY) {
        env->base.fault(env->base.ctx, "copy target", texture->builder.target);
        return 0;
    }
    if (texture->builder.levels > 1u) {
        env->base.fault(env->base.ctx, "copy levels", texture->builder.levels);
        return 0;
    }
    if (texture->builder.flags & NVN_TEXTURE_LINEAR_FLAGS) {
        env->base.fault(env->base.ctx, "copy linear", texture->builder.flags);
        return 0;
    }
    if (layout->block_width != 1u || layout->block_height != 1u) {
        env->base.fault(env->base.ctx, "copy block", texture->builder.format);
        return 0;
    }
    if (layout->bytes_per_block == 0u || layout->bytes_per_block > NVN_TEXTURE_GOB_WIDTH) {
        env->base.fault(env->base.ctx, "copy bytes per block", layout->bytes_per_block);
        return 0;
    }
    return 1;
}

/* The size and tile words of one copy surface. The width and height use the sample scale as in ClearTexture. */
static void rt_copy_surface(const NvnTexture* texture, NvnRtCopySurface* out) {
    const NvnTextureLayout* layout = &texture->layout;
    uint32_t width = layout->width * layout->sample_x;
    uint32_t height = layout->height * layout->sample_y;
    uint32_t gob_blocks = NVN_TEXTURE_GOB_WIDTH / layout->bytes_per_block;
    uint32_t factor = rt_tile_factor(layout, width, height, layout->depth);
    uint32_t tile_depth = 1u << (layout->tile_z & NVN_RT_SHIFT_MASK);
    out->aligned_width = rt_align_up(width, factor * gob_blocks);
    out->height = height;
    out->tile = rt_tile_word(layout);
    out->depth_word = layout->depth < tile_depth ? layout->depth : tile_depth;
}

/* (int32_t)(extent << 15) / divisor (SDIV). The INT32_MIN / -1 case is undefined in C and is refused here. */
static int rt_copy_ratio(const NvnRtCopyEnv* env, uint32_t extent, uint32_t divisor, int32_t* out) {
    int32_t value = (int32_t)(extent << NVN_RT_COPY_RATIO_SHIFT);
    int32_t div = (int32_t)divisor;
    if (value == INT32_MIN && div == -1) {
        env->base.fault(env->base.ctx, "copy ratio", divisor);
        return 0;
    }
    *out = value / div;
    return 1;
}

uint32_t nvn_rt_copy_texture_words(const NvnRtCopyEnv* env, const NvnTexture* src,
                                   const NvnTextureView* src_view, const NvnTexture* dst,
                                   const NvnTextureView* dst_view, const uint32_t src_region[6],
                                   const uint32_t dst_region[6], uint32_t flags, int* nothing,
                                   uint32_t* out) {
    if (nothing) *nothing = 0;
    if (!env || !env->base.fault || !env->format_word || !out || !nothing || !src || !dst ||
        !src_region || !dst_region) return 0;
    uint32_t layers = dst_region[5];
    /* The library returns before the start word when the destination region is empty (explorer section 7). */
    if (dst_region[3] == 0u || dst_region[4] == 0u || layers == 0u) {
        *nothing = 1;
        return 0;
    }
    if (!rt_copy_checks(env, src) || !rt_copy_checks(env, dst)) return 0;
    if (flags & ~NVN_RT_COPY_FLAGS_MASK) {
        env->base.fault(env->base.ctx, "copy flags", flags);
        return 0;
    }
    if (layers > NVN_RT_COPY_LAYERS_MAX) {
        env->base.fault(env->base.ctx, "copy layers", layers);
        return 0;
    }
    int32_t ratio_x = 0, ratio_y = 0;
    if (!rt_copy_ratio(env, src_region[3], dst_region[3], &ratio_x) ||
        !rt_copy_ratio(env, src_region[4], dst_region[4], &ratio_y)) return 0;

    /* The format change of item 5, then the +0x18 word of each index (explorer sections 2 and 5).
       The caller reads the format from the view when the view has one, else from the texture. */
    uint32_t src_format = (src_view && (src_view->flags & NVN_VIEW_FORMAT)) ? src_view->format
                                                                           : src->builder.format;
    uint32_t dst_format = (dst_view && (dst_view->flags & NVN_VIEW_FORMAT)) ? dst_view->format
                                                                           : dst->builder.format;
    if (src_format == dst_format &&
        (!(flags & NVN_RT_COPY_FLAGS_MASK) ||
         (ratio_x == NVN_RT_COPY_RATIO_UNIT && ratio_y == NVN_RT_COPY_RATIO_UNIT))) {
        if (src_format == NVN_RT_COPY_FORMAT_0X38) {
            src_format = NVN_RT_COPY_FORMAT_0X25;
            dst_format = NVN_RT_COPY_FORMAT_0X25;
        } else if (src_format == NVN_RT_COPY_FORMAT_0X37) {
            src_format = NVN_RT_COPY_FORMAT_0X6D;
            dst_format = NVN_RT_COPY_FORMAT_0X6D;
        }
    }
    uint32_t src_format_word = env->format_word(env->base.ctx, src_format);
    if (src_format_word == 0u) {
        env->base.fault(env->base.ctx, "copy format", src_format);
        return 0;
    }
    uint32_t dst_format_word = env->format_word(env->base.ctx, dst_format);
    if (dst_format_word == 0u) {
        env->base.fault(env->base.ctx, "copy format", dst_format);
        return 0;
    }

    /* The layer addresses are made first, so that a late refusal writes no part of out. */
    uint32_t src_high[NVN_RT_COPY_LAYERS_MAX], src_low[NVN_RT_COPY_LAYERS_MAX];
    uint32_t dst_high[NVN_RT_COPY_LAYERS_MAX], dst_low[NVN_RT_COPY_LAYERS_MAX];
    uint32_t src_view_base = (src_view && (src_view->flags & NVN_VIEW_LAYERS)) ? src_view->base_layer : 0u;
    uint32_t dst_view_base = (dst_view && (dst_view->flags & NVN_VIEW_LAYERS)) ? dst_view->base_layer : 0u;
    int32_t step = (int32_t)src_region[5] / (int32_t)layers;
    NvnTextureView src_layer = {0};
    NvnTextureView dst_layer = {0};
    src_layer.flags = dst_layer.flags = NVN_VIEW_LEVELS | NVN_VIEW_LAYERS;
    src_layer.level_count = dst_layer.level_count = 1u;
    src_layer.layer_count = dst_layer.layer_count = 1u;
    src_layer.base_level =
        (uint8_t)((src_view && (src_view->flags & NVN_VIEW_LEVELS)) ? src_view->base_level : 0u);
    dst_layer.base_level =
        (uint8_t)((dst_view && (dst_view->flags & NVN_VIEW_LEVELS)) ? dst_view->base_level : 0u);
    for (uint32_t i = 0; i < layers; i++) {
        uint64_t src_position =
            (uint64_t)src_region[2] + src_view_base + (uint64_t)((int64_t)step * (int64_t)i);
        uint64_t dst_position = (uint64_t)dst_region[2] + dst_view_base + i;
        if (src_position > NVN_RT_COPY_LAYER_LIMIT || dst_position > NVN_RT_COPY_LAYER_LIMIT) {
            env->base.fault(env->base.ctx, "copy layers",
                            src_position > NVN_RT_COPY_LAYER_LIMIT ? src_position : dst_position);
            return 0;
        }
        src_layer.base_layer = (uint16_t)src_position;
        dst_layer.base_layer = (uint16_t)dst_position;
        NvnRtViewResult src_result, dst_result;
        if (!rt_view_apply(&env->base, src, &src_layer, &src_result)) return 0;
        if (!rt_view_apply(&env->base, dst, &dst_layer, &dst_result)) return 0;
        src_high[i] = (uint32_t)(src_result.address >> 32);
        src_low[i] = (uint32_t)src_result.address;
        dst_high[i] = (uint32_t)(dst_result.address >> 32);
        dst_low[i] = (uint32_t)dst_result.address;
    }

    NvnRtCopySurface src_surface, dst_surface;
    rt_copy_surface(src, &src_surface);
    rt_copy_surface(dst, &dst_surface);
    uint32_t w5 = (uint32_t)(ratio_x >> NVN_RT_COPY_RATIO_INT_SHIFT) + (src_region[0] << NVN_RT_COPY_ORIGIN_SHIFT);
    uint32_t w6 = (uint32_t)(ratio_y >> NVN_RT_COPY_RATIO_INT_SHIFT) + (src_region[1] << NVN_RT_COPY_ORIGIN_SHIFT);

    uint32_t at = 0;
    out[at++] = NVN_RT_COPY_OPEN;
    for (uint32_t i = 0; i < layers; i++) {
        if (i == 0u) {
            out[at++] = NVN_RT_COPY_RUN(NVN_RT_COPY_SRC_METHOD, NVN_RT_COPY_SRC_WORDS);
            out[at++] = src_format_word;
            out[at++] = 0u;
            out[at++] = src_surface.tile;
            out[at++] = src_surface.depth_word;
            out[at++] = NVN_RT_COPY_RUN(NVN_RT_COPY_SRC_SIZE_METHOD, NVN_RT_COPY_PAIR_WORDS);
            out[at++] = src_surface.aligned_width;
            out[at++] = src_surface.height;
            out[at++] = NVN_RT_COPY_RUN(NVN_RT_COPY_ADDRESS_METHOD, NVN_RT_COPY_PAIR_WORDS);
            out[at++] = src_high[i];
            out[at++] = src_low[i];
            out[at++] = NVN_RT_COPY_RUN(NVN_RT_COPY_DST_METHOD, NVN_RT_COPY_DST_WORDS);
            out[at++] = dst_format_word;
            out[at++] = 0u;
            out[at++] = dst_surface.tile;
            out[at++] = dst_surface.depth_word;
            out[at++] = 0u;
            out[at++] = NVN_RT_COPY_RUN(NVN_RT_COPY_DST_SIZE_METHOD, NVN_RT_COPY_PAIR_WORDS);
            out[at++] = dst_surface.aligned_width;
            out[at++] = dst_surface.height;
            out[at++] = NVN_RT_COPY_RUN(NVN_RT_COPY_DST_ADDR_METHOD, NVN_RT_COPY_PAIR_WORDS);
            out[at++] = dst_high[i];
            out[at++] = dst_low[i];
            out[at++] = NVN_RT_COPY_VALUE(NVN_RT_COPY_IMMD_METHOD, NVN_RT_COPY_IMMD_VALUE);
        } else {
            out[at++] = NVN_RT_COPY_RUN(NVN_RT_COPY_ADDRESS_METHOD, NVN_RT_COPY_PAIR_WORDS);
            out[at++] = src_high[i];
            out[at++] = src_low[i];
            out[at++] = NVN_RT_COPY_RUN(NVN_RT_COPY_DST_ADDR_METHOD, NVN_RT_COPY_PAIR_WORDS);
            out[at++] = dst_high[i];
            out[at++] = dst_low[i];
        }
        out[at++] = NVN_RT_COPY_RUN(NVN_RT_COPY_FLAGS_METHOD, NVN_RT_COPY_FLAGS_WORDS);
        out[at++] = ((flags & NVN_RT_COPY_FLAGS_MASK) ? NVN_RT_COPY_FLAGS_VALUE : 0u) |
                    NVN_RT_COPY_FLAGS_BASE;
        out[at++] = NVN_RT_COPY_RUN(NVN_RT_COPY_RECT_METHOD, NVN_RT_COPY_RECT_WORDS);
        out[at++] = dst_region[0];
        out[at++] = dst_region[1];
        out[at++] = dst_region[3];
        out[at++] = dst_region[4];
        out[at++] = NVN_RT_COPY_RUN(NVN_RT_COPY_SCALE_METHOD, NVN_RT_COPY_RECT_WORDS);
        out[at++] = (uint32_t)ratio_x << NVN_RT_COPY_FRACTION_SHIFT;
        out[at++] = (uint32_t)(ratio_x >> NVN_RT_COPY_RATIO_SHIFT);
        out[at++] = (uint32_t)ratio_y << NVN_RT_COPY_FRACTION_SHIFT;
        out[at++] = (uint32_t)(ratio_y >> NVN_RT_COPY_RATIO_SHIFT);
        out[at++] = NVN_RT_COPY_RUN(NVN_RT_COPY_OFFSET_METHOD, NVN_RT_COPY_RECT_WORDS);
        out[at++] = w5 << NVN_RT_COPY_OFFSET_SHIFT;
        out[at++] = (uint32_t)((int32_t)w5 >> NVN_RT_COPY_OFFSET_RIGHT);
        out[at++] = w6 << NVN_RT_COPY_OFFSET_SHIFT;
        out[at++] = (uint32_t)((int32_t)w6 >> NVN_RT_COPY_OFFSET_RIGHT);
    }
    out[at++] = NVN_RT_COPY_CLOSE;
    return at;
}
