/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
#ifndef SNX_NVN_RT_H
#define SNX_NVN_RT_H
#include "nx/gpu/nvn_params.h"
#include <stdint.h>

#define NVN_RT_COLOR_MAX 8u

typedef struct {
    const NvnTexture* texture;     /* NULL: the slot has no target */
    const NvnTextureView* view;    /* NULL: no view. A view with flags 0 is the same as no view. */
} NvnRtColor;

typedef struct NvnRtEnv {
    void* ctx;
    void (*method)(void* ctx, uint32_t m, uint32_t v);          /* one method write */
    void (*fault)(void* ctx, const char* what, uint64_t value);  /* one call for a refused request */
} NvnRtEnv;

/* 1 when all words are written. 0 when the request is refused: then NOTHING is written and fault was called once.
   depth is NULL or one target with a texture and its view. clip_limit is the device word at +0x18. */
int nvn_rt_emit(const NvnRtEnv* env, const NvnRtColor* color, uint32_t count, const NvnRtColor* depth,
                uint32_t clip_limit);

#define NVN_RT_CLEAR_LAYERS_MAX 64u
#define NVN_RT_CLEAR_WORDS_MAX  (15u + 10u * NVN_RT_CLEAR_LAYERS_MAX)

/* The words of nvnCommandBufferClearTexture (d857610). Returns the word count. A refusal returns 0, calls fault once and
   writes no part of out. An empty region writes no word in the library: then 0 is returned with *nothing set to 1 and
   fault is not called. view is the view of the call, NULL when there is none. */
uint32_t nvn_rt_clear_texture_words(const NvnRtEnv* env, const NvnTexture* texture,
                                    const NvnTextureView* view, const uint32_t region[6],
                                    const uint32_t color[4], uint32_t mask, uint32_t clip_limit,
                                    int* nothing, uint32_t* out);

/* 43 words for one layer (start, first pass, tail, end) and 23 more for each later layer. */
#define NVN_RT_COPY_LAYERS_MAX 64u
#define NVN_RT_COPY_WORDS_MAX  (43u + 23u * (NVN_RT_COPY_LAYERS_MAX - 1u))

/* The copy environment adds the game's format table column +0x18 to the render target callbacks. */
typedef struct {
    NvnRtEnv base;
    uint32_t (*format_word)(void* ctx, uint32_t format); /* 0: unreadable or invalid format */
} NvnRtCopyEnv;

/* The words of nvnCommandBufferCopyTextureToTexture (d82bfd0). Returns the word count. A refusal returns 0, calls fault
   once and writes no part of out. An empty destination region writes no word in the library: then 0 is returned with
   *nothing set to 1 and fault is not called. src_view and dst_view are the views of the call, NULL when there is none. */
uint32_t nvn_rt_copy_texture_words(const NvnRtCopyEnv* env, const NvnTexture* src,
                                   const NvnTextureView* src_view, const NvnTexture* dst,
                                   const NvnTextureView* dst_view, const uint32_t src_region[6],
                                   const uint32_t dst_region[6], uint32_t flags, int* nothing,
                                   uint32_t* out);

#endif
