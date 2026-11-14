/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* Temporal passes for Pokemon Legends: Arceus (SNX_ARCEUS_TEMPORAL). "probe" finds the scene colour and depth of a
   frame, keeps the camera's matrices and makes camera motion vectors between the scene colour's last write and first
   read. "taa" also gives the scene's camera a sub-pixel jitter and resolves the scene there with a history of its
   own. SNX_ARCEUS_TEMPORAL_VIEW draws what they see (docs/arceus-temporal.md). Unset, nothing here runs. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nx/gpu/gpu.h"
#include "nx/gpu/vk/vk.h"
#include "nx/gpu/vk/post_spv.h"
#include "nx/gpu/vk/temporal_spv.h"
#include "nx/gpu/vk/temporal_math.h"
#include "nx/runtime/platform/log.h"

#define CAMERA_BANK 4       /* the constant bank of the camera block */
#define CAMERA_BYTES 0x140  /* view-projection at +0x00, its inverse at +0xC0, row vectors */
#define CAMERA_INVERSE 0xc0
/* red and green the vector, alpha 1 where it is an object's and blue then the depth its point had a frame ago */
#define MOTION_FORMAT VK_FORMAT_R16G16B16A16_SFLOAT
#define SKINS 512          /* slots of the table of skinned characters' position buffers */
#define SKIN_BYTES (16u << 20) /* the largest position buffer kept */
#define HISTORY_FORMAT VK_FORMAT_R16G16B16A16_SFLOAT
#define JITTERS 8          /* the offsets of the jitter, a Halton (2, 3) sequence */
#define FRAME_WEIGHT 0.1f  /* the frame's share in the blend with the history */
#define OBJECT_FORMAT VK_FORMAT_R16G16B16A16_SFLOAT
#define OBJECT_BANK 6      /* the object's constant block: its world matrix at +0, row vectors */
#define OBJECTS 8192       /* slots of the table of world matrices */
#define OBJECT_DRAWS 256   /* extra draws a frame at most */
#define MODE_PROBE 1
#define MODE_TAA 2
#define SETS_BINDINGS 7
#define STATS_FORMAT VK_FORMAT_R16G16B16A16_SFLOAT
#define STATS_W 96u /* the numbers' image: one texel a sampled pixel, in each of two halves side by side */
#define STATS_H 54u
/* Runs between two readings. Not a multiple of the jitter's period, so the readings walk through all its offsets:
   every 60 runs they saw two of the eight. */
#define STATS_EVERY 61u
#define STATS_ERROR 0.25f  /* a relative colour error above this counts as wrong */

typedef struct {
    VkImage img;
    VmaAllocation mem;
    VkImageView view;
    VkFormat format;
    u32 w, h;
} Image;
/* the passes' push constants (shaders/temporal_motion.frag) */
typedef struct {
    float reproject[16];
    float viewport[4];
    float depth[4];
    float size[4];
} Constants;

static struct {
    int mode; /* -1 not read yet, 0 off, MODE_PROBE, MODE_TAA */
    int view; /* 0 none, 1 the vectors, 2 the reprojection error, 3 the history weight, 4 the jittered frame alone */
    /* what a frame taught: the scene colour and depth targets' guest addresses, and the fragment program of the first
       draw that samples the scene colour */
    u64 scene, depth, reader;
    /* this frame, by g_vk_present + 1 (0 is never) */
    u32 camera_at, written_at, ran_at, reader_at, history_at;
    Surf *scene_rt, *depth_rt;
    VkViewport viewport;
    float depth_translate, depth_scale; /* stored depth = translate + scale x the projection's z */
    int depth_zero_to_one;
    float now[16], inverse[16], before[16], kept[16];
    int have_before;
    u64 pair_equal, pair_differ, runs, skipped;
    VkDescriptorSetLayout dsl;
    VkPipelineLayout layout;
    VkSampler linear, point;
    VkPipeline motion_pipe, view_pipe, stats_pipe;
    Image stats;
    VkBuffer stats_buffer;
    VmaAllocation stats_memory;
    const u16* stats_words;
    u64 stats_run; /* the run whose numbers the buffer holds, 0 for none */
    VkFormat view_format;
    Image motion, colour[2];
    int newest; /* which of colour[] holds the newer frame */
    /* anti-aliasing: the two addresses the scene camera's block takes in turn, its jittered copy for this
       submission, and our history */
    u64 camera_address[2], jitter_address, jitter_submit, jittered;
    /* a miss of the paired slot: what the two neighbours held, to compare with the next frame's block */
    u8 miss_bytes[2][64];
    u32 miss_at;
    u64 miss_ahead, miss_other;
    u8 jitter_source[0x1000]; /* the guest's bytes the copy was made from */
    /* object vectors: the target, the frame it was last drawn in, and the counts of the stats line */
    int objects;
    float sharpen;  /* 0 off, to 1: the sharpen after the resolve */
    float mip_bias; /* 0 off, negative: added to the level bias of material textures on scene draws */
    VkPipeline sharpen_pipe;
    VkFormat sharpen_format;
    /* the resolve's: 1 Catmull-Rom history, 2 variance clip, 4 weight by motion and clip, 8 the nearest depth's
       vector, 16 blend by brightness */
    int options;
    Image object;
    VkShaderModule object_fs;
    u32 object_at, object_frame, object_draws;
    u64 object_seen, object_moved, object_drawn, object_capped, object_new, object_odd, object_pipelines,
        object_frames, object_far, object_same, object_never, object_older, object_skinned, skinned_lines;
    /* skinned characters: our vertex and fragment programs, and the counts of the stats line */
    VkShaderModule skin_vs, skin_fs;
    u64 skin_kept, skin_drawn, skin_first, skin_twice, skin_changed, object_unpaired;
    /* each extra draw of this frame by its number, and the same for the frame the numbers' image was drawn in:
       the probe writes the number to the vector target, so a wrong vector names its draw */
    struct Drawn {
        u64 block, feedback;
        float shift; /* the origin's move on screen in pixels */
        u8 skinned, source;
    } drawn[OBJECT_DRAWS + 1], stats_drawn[OBJECT_DRAWS + 1];
    u32 unpaired_logged_at, long_logged_at;
    /* the transform feedback outputs of this frame, for finding the draws that read them */
    struct {
        u64 address;
        u32 size, index;
    } feedback[256];
    u32 feedback_n, feedback_frame, far_logged_at;
    u64 other_cameras;
    u32 jitter_index, jitter_size, camera_read; /* camera_read: the most bytes a program read of the block */
    VkBuffer jitter_buffer;
    u64 jitter_offset;
    VkPipeline resolve_pipe;
    VkFormat resolve_format;
    Image history[2];
    int history_newest;
    u32 resolved_at;
    u64 resets;
} g_t = {.mode = -1};

static int temporal_on(void) {
    if (g_t.mode < 0) {
        const char* mode = getenv("SNX_ARCEUS_TEMPORAL");
        const char* view = getenv("SNX_ARCEUS_TEMPORAL_VIEW");
        g_t.mode = !mode ? 0 : !strcmp(mode, "probe") ? MODE_PROBE : !strcmp(mode, "taa") ? MODE_TAA : 0;
        g_t.view = view && view[0] >= '1' && view[0] <= '4' && !view[1] ? view[0] - '0' : 0;
        /* the probe has views 1 and 2, the anti-aliasing 3 and 4 */
        if ((g_t.mode == MODE_PROBE && g_t.view > 2) || (g_t.mode == MODE_TAA && g_t.view < 3)) g_t.view = 0;
        const char* history = getenv("SNX_ARCEUS_TEMPORAL_HISTORY");
        const char* clamp = getenv("SNX_ARCEUS_TEMPORAL_CLAMP");
        const char* blend = getenv("SNX_ARCEUS_TEMPORAL_BLEND");
        const char* nearest = getenv("SNX_ARCEUS_TEMPORAL_NEAREST");
        const char* luma = getenv("SNX_ARCEUS_TEMPORAL_LUMA");
        g_t.options = (history && !strcmp(history, "catmull") ? 1 : 0) |
                      (clamp && !strcmp(clamp, "variance") ? 2 : 0) | (blend && !strcmp(blend, "adaptive") ? 4 : 0) |
                      (nearest && !strcmp(nearest, "1") ? 8 : 0) | (luma && !strcmp(luma, "1") ? 16 : 0);
        const char* sharpen = getenv("SNX_ARCEUS_TEMPORAL_SHARPEN");
        const char* mip_bias = getenv("SNX_ARCEUS_TEMPORAL_MIPBIAS");
        g_t.sharpen = g_t.mode == MODE_TAA && sharpen ? strtof(sharpen, NULL) : 0.0f;
        if (!(g_t.sharpen > 0.0f)) g_t.sharpen = 0.0f;
        if (g_t.sharpen > 1.0f) g_t.sharpen = 1.0f;
        g_t.mip_bias = g_t.mode == MODE_TAA && mip_bias ? strtof(mip_bias, NULL) : 0.0f;
        if (!(g_t.mip_bias < 0.0f)) g_t.mip_bias = 0.0f;
        if (g_t.mip_bias < -2.0f) g_t.mip_bias = -2.0f;
        const char* vectors = getenv("SNX_ARCEUS_TEMPORAL_VECTORS");
        g_t.objects = g_t.mode && vectors && !strcmp(vectors, "object");
        /* Every option's state, on or off, in each run's log. A value other than the one named leaves an option
           off: NEAREST and LUMA take 1, HISTORY catmull, CLAMP variance, BLEND adaptive, VECTORS object, SHARPEN
           a number above 0, MIPBIAS one below 0. */
        if (g_t.mode)
            LOG("arceus-temporal: vectors %s\n", g_t.objects ? "of the camera and of objects that moved (object)"
                                                             : "of the camera only");
        if (g_t.mode == MODE_TAA)
            LOG("arceus-temporal: resolve options: history %s, clamp %s, weight %s, vector %s, blend %s, sharpen "
                "%.2f, texture level bias %.2f\n",
                g_t.options & 1 ? "Catmull-Rom (catmull)" : "linear", g_t.options & 2 ? "variance clip" : "min and max",
                g_t.options & 4 ? "by motion and clip (adaptive)" : "fixed",
                g_t.options & 8 ? "of the nearest depth" : "own", g_t.options & 16 ? "by brightness (luma)" : "plain",
                (double)g_t.sharpen, (double)g_t.mip_bias);
        if (g_t.mode)
            LOG("arceus-temporal: %s on, view %d (0 none, 1 motion vectors, 2 reprojection error, 3 history weight, "
                "4 jittered frame alone)\n",
                g_t.mode == MODE_TAA ? "anti-aliasing" : "probe", g_t.view);
    }
    return g_t.mode;
}

static int image_ensure(Image* t, VkFormat format, u32 w, u32 h, VkImageUsageFlags usage, const char* what) {
    VkImageCreateInfo ii = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                            .imageType = VK_IMAGE_TYPE_2D,
                            .format = format,
                            .extent = {w, h, 1},
                            .mipLevels = 1,
                            .arrayLayers = 1,
                            .samples = VK_SAMPLE_COUNT_1_BIT,
                            .tiling = VK_IMAGE_TILING_OPTIMAL,
                            .usage = usage,
                            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    VkImageViewCreateInfo vci = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                 .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                 .format = format,
                                 .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    if (t->img && t->format == format && t->w == w && t->h == h) return 1;
    if (t->img) vk_defer_image(t->img, t->mem, t->view, VK_NULL_HANDLE);
    *t = (Image){0};
    if (!vk_image_create(&ii, &t->img, &t->mem)) {
        *t = (Image){0};
        LOG_LIMIT(1, "arceus-temporal: no memory for the %ux%u %s image\n", w, h, what);
        return 0;
    }
    vci.image = t->img;
    if (!VK_OK(vkCreateImageView(g_vk.dev, &vci, NULL, &t->view))) {
        vk_defer_image(t->img, t->mem, VK_NULL_HANDLE, VK_NULL_HANDLE);
        *t = (Image){0};
        return 0;
    }
    t->format = format;
    t->w = w;
    t->h = h;
    return 1;
}

static int layouts_make(void) {
    VkDescriptorSetLayoutBinding b[SETS_BINDINGS];
    VkDescriptorSetLayoutCreateInfo dci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = SETS_BINDINGS, .pBindings = b};
    VkPushConstantRange pc = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Constants)};
    VkPipelineLayoutCreateInfo lci = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                      .setLayoutCount = 1,
                                      .pSetLayouts = &g_t.dsl,
                                      .pushConstantRangeCount = 1,
                                      .pPushConstantRanges = &pc};
    VkSamplerCreateInfo sci = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                               .magFilter = VK_FILTER_LINEAR,
                               .minFilter = VK_FILTER_LINEAR,
                               .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                               .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                               .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                               .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
    if (g_t.layout) return 1;
    for (u32 i = 0; i < SETS_BINDINGS; i++)
        b[i] = (VkDescriptorSetLayoutBinding){.binding = i,
                                              .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                              .descriptorCount = 1,
                                              .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT};
    if (!VK_OK(vkCreateDescriptorSetLayout(g_vk.dev, &dci, NULL, &g_t.dsl))) return 0;
    if (!VK_OK(vkCreateSampler(g_vk.dev, &sci, NULL, &g_t.linear))) return 0;
    sci.magFilter = sci.minFilter = VK_FILTER_NEAREST;
    if (!VK_OK(vkCreateSampler(g_vk.dev, &sci, NULL, &g_t.point))) return 0;
    if (!VK_OK(vkCreatePipelineLayout(g_vk.dev, &lci, NULL, &g_t.layout))) {
        g_t.layout = VK_NULL_HANDLE;
        return 0;
    }
    return 1;
}

/* a pipeline of one fragment shader with the post passes' fullscreen triangle, for one colour format */
static VkPipeline pipeline_make_n(const VkFormat* formats, u32 count, const u32* fs_words, u64 fs_bytes,
                                  const char* what);
static VkPipeline pipeline_make(VkFormat format, const u32* fs_words, u64 fs_bytes, const char* what) {
    return pipeline_make_n(&format, 1, fs_words, fs_bytes, what);
}
static VkPipeline pipeline_make_n(const VkFormat* formats, u32 count, const u32* fs_words, u64 fs_bytes,
                                  const char* what) {
    VkPipelineShaderStageCreateInfo st[2] = {{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                              .stage = VK_SHADER_STAGE_VERTEX_BIT,
                                              .pName = "main"},
                                             {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                              .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                                              .pName = "main"}};
    VkPipelineVertexInputStateCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                                                 .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkPipelineViewportStateCreateInfo vp = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1};
    VkPipelineRasterizationStateCreateInfo rs = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                                                 .polygonMode = VK_POLYGON_MODE_FILL,
                                                 .cullMode = VK_CULL_MODE_NONE,
                                                 .lineWidth = 1};
    VkPipelineMultisampleStateCreateInfo ms = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                                               .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState ba[2] = {{.colorWriteMask = 0xF}, {.colorWriteMask = 0xF}};
    VkPipelineColorBlendStateCreateInfo cb = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                                              .attachmentCount = count,
                                              .pAttachments = ba};
    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = 2, .pDynamicStates = dyn};
    VkPipelineRenderingCreateInfo ri = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
                                        .colorAttachmentCount = count,
                                        .pColorAttachmentFormats = formats};
    VkGraphicsPipelineCreateInfo gci = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                                        .pNext = &ri,
                                        .stageCount = 2,
                                        .pStages = st,
                                        .pVertexInputState = &vi,
                                        .pInputAssemblyState = &ia,
                                        .pViewportState = &vp,
                                        .pRasterizationState = &rs,
                                        .pMultisampleState = &ms,
                                        .pColorBlendState = &cb,
                                        .pDynamicState = &ds,
                                        .layout = g_t.layout};
    VkPipeline pipe = VK_NULL_HANDLE;
    VkShaderModule vs = vk_module_spv(post_vs_spv, (size_t)(post_vs_spv_size / 4));
    VkShaderModule fs = vk_module_spv(fs_words, (size_t)(fs_bytes / 4));
    st[0].module = vs;
    st[1].module = fs;
    if (vs && fs && !VK_OK(vkCreateGraphicsPipelines(g_vk.dev, vk_pipeline_cache(), 1, &gci, NULL, &pipe)))
        pipe = VK_NULL_HANDLE;
    if (vs) vkDestroyShaderModule(g_vk.dev, vs, NULL);
    if (fs) vkDestroyShaderModule(g_vk.dev, fs, NULL);
    if (!pipe)
        LOG_LIMIT(1, "arceus-temporal: the %s pipeline for format %d could not be made\n", what, (int)formats[0]);
    return pipe;
}

static VkDescriptorSet set_make(const VkImageView views[SETS_BINDINGS], const VkSampler samplers[SETS_BINDINGS]) {
    VkDescriptorSet set = vk_desc_alloc(g_t.dsl);
    VkDescriptorImageInfo di[SETS_BINDINGS];
    VkWriteDescriptorSet wr[SETS_BINDINGS];
    if (!set) return VK_NULL_HANDLE;
    for (u32 i = 0; i < SETS_BINDINGS; i++) {
        di[i] = (VkDescriptorImageInfo){samplers[i], views[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        wr[i] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                       .dstSet = set,
                                       .dstBinding = i,
                                       .descriptorCount = 1,
                                       .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                       .pImageInfo = &di[i]};
    }
    vkUpdateDescriptorSets(g_vk.dev, SETS_BINDINGS, wr, 0, NULL);
    return set;
}

/* one fullscreen triangle into view (w x h, in COLOR_ATTACHMENT_OPTIMAL), every pixel written */
static void fullscreen2(VkCommandBuffer cb, VkImageView view, VkImageView second, u32 w, u32 h, VkPipeline pipe,
                        VkDescriptorSet set, const Constants* pc) {
    VkRenderingAttachmentInfo at[2] = {{.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                                        .imageView = view,
                                        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                        .storeOp = VK_ATTACHMENT_STORE_OP_STORE},
                                       {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                                        .imageView = second,
                                        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                        .storeOp = VK_ATTACHMENT_STORE_OP_STORE}};
    VkRenderingInfo ri = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                          .renderArea = {{0, 0}, {w, h}},
                          .layerCount = 1,
                          .colorAttachmentCount = second ? 2 : 1,
                          .pColorAttachments = at};
    VkViewport vp = {0, 0, (float)w, (float)h, 0, 1};
    VkRect2D sc = {{0, 0}, {w, h}};
    rec_CmdBeginRendering(cb, &ri);
    vk_render_forget_state();
    rec_CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    rec_CmdSetViewport(cb, 0, 1, &vp);
    rec_CmdSetScissor(cb, 0, 1, &sc);
    rec_CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_t.layout, 0, 1, &set, 0, NULL);
    rec_CmdPushConstants(cb, g_t.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof *pc, pc);
    rec_CmdDraw(cb, 3, 1, 0, 0);
    rec_CmdEndRendering(cb);
}
static void fullscreen(VkCommandBuffer cb, VkImageView view, u32 w, u32 h, VkPipeline pipe, VkDescriptorSet set,
                       const Constants* pc) {
    fullscreen2(cb, view, VK_NULL_HANDLE, w, h, pipe, set, pc);
}

/* out = a x b for 4 x 4 matrices in memory order. With the game's row vectors, a then b */
void vk_temporal_multiply(const float a[16], const float b[16], float out[16]) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            double sum = 0;
            for (int k = 0; k < 4; k++) sum += (double)a[4 * r + k] * (double)b[4 * k + c];
            out[4 * r + c] = (float)sum;
        }
}

static void block_read(u64 address, float out[16]) {
    for (u32 w = 0; w < 16; w++) {
        const u32 bits = gmem_r32(address + 4 * w);
        memcpy(&out[w], &bits, 4);
    }
}

/* The camera of this frame, read at the first G-buffer draw: the view-projection and its inverse. Last frame's
   view-projection is the one in the block's neighbouring slot (the game writes the block to two slots in turn) when
   that equals the copy kept here, else the copy. */
static void camera_take(const GpuChannel* ch, Surf* zt) {
    const u32 frame = g_vk_present + 1;
    u64 address = ch->cbind[ST_FS][CAMERA_BANK];
    u32 size = ch->cbind_size[ST_FS][CAMERA_BANK];
    if (!address || size < CAMERA_BYTES) {
        address = ch->cbind[ST_VS][CAMERA_BANK];
        size = ch->cbind_size[ST_VS][CAMERA_BANK];
    }
    if (!address || size < CAMERA_BYTES) {
        LOG_LIMIT(2, "arceus-temporal: no camera block at the G-buffer draw (bank %d size %x)\n", CAMERA_BANK, size);
        return;
    }
    const int had = g_t.camera_at && g_t.camera_at + 1 == frame;
    if (had) memcpy(g_t.kept, g_t.now, sizeof g_t.kept);
    block_read(address, g_t.now);
    block_read(address + CAMERA_INVERSE, g_t.inverse);
    if (address != g_t.camera_address[0]) {
        g_t.camera_address[1] = g_t.camera_address[0];
        g_t.camera_address[0] = address;
    }
    g_t.have_before = had;
    /* a neighbour that did not hold last frame's matrix: did it already hold this frame's? */
    if (g_t.miss_at && g_t.miss_at + 1 == frame) {
        if (!memcmp(g_t.miss_bytes[0], g_t.now, 64) || !memcmp(g_t.miss_bytes[1], g_t.now, 64)) g_t.miss_ahead++;
        else g_t.miss_other++;
        if (g_t.miss_ahead + g_t.miss_other <= 4 || (g_t.miss_ahead + g_t.miss_other) % 300 == 0)
            LOG("arceus-temporal: when the neighbouring slot missed, it held the next frame's matrix %llu times and "
                "something else %llu times\n",
                (unsigned long long)g_t.miss_ahead, (unsigned long long)g_t.miss_other);
    }
    g_t.miss_at = 0;
    if (had) {
        int equal = 0;
        memcpy(g_t.before, g_t.kept, sizeof g_t.before);
        for (int side = 0; side < 2 && !equal; side++) {
            const u8* other = gmem_host(side ? address + size : address - size, 64);
            if (other && !memcmp(other, g_t.kept, 64)) equal = 1 + side;
        }
        if (equal) g_t.pair_equal++;
        else {
            g_t.pair_differ++;
            for (int side = 0; side < 2; side++) {
                const u8* other = gmem_host(side ? address + size : address - size, 64);
                memset(g_t.miss_bytes[side], 0, 64);
                if (other) memcpy(g_t.miss_bytes[side], other, 64);
            }
            g_t.miss_at = frame;
        }
        if (g_t.pair_equal + g_t.pair_differ <= 4 || (g_t.pair_equal + g_t.pair_differ) % 1800 == 0)
            LOG("arceus-temporal: camera block %llx size %x, last frame's view-projection %s (equal %llu, not %llu)\n",
                (unsigned long long)address, size,
                equal == 1 ? "is in the slot below" : equal == 2 ? "is in the slot above" : "is in neither slot",
                (unsigned long long)g_t.pair_equal, (unsigned long long)g_t.pair_differ);
    }
    g_t.viewport = vk_render_viewport(ch->r3d);
    /* depth as vk_render_viewport makes its range: both depth modes store translate + scale x z */
    {
        const u32* r = ch->r3d;
        float vt[6], zn, zf;
        memcpy(vt, &r[0x280], sizeof vt);
        memcpy(&zn, &r[0x302], 4);
        memcpy(&zf, &r[0x303], 4);
        g_t.depth_zero_to_one = (int)(r[0x35F] & 1);
        if (vt[2] != 0.0f || vt[5] != 0.0f) {
            g_t.depth_translate = vt[5];
            g_t.depth_scale = vt[2];
        } else if (g_t.depth_zero_to_one) {
            g_t.depth_translate = zn;
            g_t.depth_scale = zf - zn;
        } else {
            g_t.depth_translate = (zn + zf) * 0.5f;
            g_t.depth_scale = (zf - zn) * 0.5f;
        }
    }
    g_t.depth_rt = zt;
    g_t.camera_at = frame;
}

/* the size of a jittered pixel: the scene viewport's, the motion image's until a viewport is known */
static float jitter_width(void) {
    const float w = fabsf(g_t.viewport.width);
    return w >= 1.0f ? w : (float)g_t.motion.w;
}
static float jitter_height(void) {
    const float h = fabsf(g_t.viewport.height);
    return h >= 1.0f ? h : (float)g_t.motion.h;
}

/* A draw binds constant bank 4: when it is the scene camera's block and anti-aliasing is on, the draw gets a copy
   whose projection is shifted by this frame's jitter. Guest memory is not written, so the game and our own matrices
   keep the block without jitter. The copy is made once a submission in the frame's staging. With row vectors a shift
   of clip x and y by jitter x w is the matrix times J, which has the jitter in its fourth row: view-projection x J at
   +0x00, projection x J at +0x80, and J's inverse x the inverse view-projection at +0xC0. */
int vk_temporal_camera_block(u64 address, u32 bound, u32 size, VkBuffer* buffer, u64* offset) {
    if (g_t.mode != MODE_TAA || !address || !g_t.motion.w ||
        (address != g_t.camera_address[0] && address != g_t.camera_address[1]))
        return 0;
    if (bound > 0x1000 || bound < CAMERA_BYTES || size > bound) return 0;
    /* only +0x00, +0x80 and +0xC0 are shifted: a program that reads further is worth knowing about */
    if (size > g_t.camera_read) {
        g_t.camera_read = size;
        LOG("arceus-temporal: a program reads %u of the camera block's %u bytes\n", size, bound);
    }
    const u32 index = g_vk_present % JITTERS;
    /* the guest's bytes now: the copy is remade when they differ from the ones it was made from */
    u8 source[0x1000];
    gmem_read(address, source, bound);
    /* once this frame's scene camera is known, a block with another view-projection is another camera in the slot.
       Before that (the depth pre-pass) the address alone decides. */
    if (g_t.camera_at == g_vk_present + 1 && memcmp(source, g_t.now, sizeof g_t.now)) {
        if (g_t.other_cameras++ < 4 || g_t.other_cameras % 3600 == 0)
            LOG("arceus-temporal: another camera at %llx is drawn without jitter, %llu draws so far\n",
                (unsigned long long)address, (unsigned long long)g_t.other_cameras);
        return 0;
    }
    if (g_t.jitter_address != address || g_t.jitter_submit != vk_submit_number() || g_t.jitter_index != index ||
        g_t.jitter_size != bound || memcmp(source, g_t.jitter_source, bound)) {
        u8* bytes = vk_stage(bound, vk_stage_align(), &g_t.jitter_buffer, &g_t.jitter_offset);
        float pixels[2], m[16];
        if (!bytes) return 0;
        memcpy(bytes, source, bound);
        memcpy(g_t.jitter_source, source, bound);
        temporal_jitter(index, JITTERS, pixels);
        /* in the scene viewport's pixels, which is the target's size only while the game draws the 3D scene at
           full size */
        const float jx = 2.0f * pixels[0] / jitter_width(), jy = 2.0f * pixels[1] / jitter_height();
        /* the view-projection and the projection x J, and J's inverse x the inverse view-projection */
        static const u32 forward[2] = {0x00, 0x80};
        for (int k = 0; k < 2; k++) {
            memcpy(m, source + forward[k], sizeof m);
            temporal_jitter_forward(m, jx, jy);
            memcpy(bytes + forward[k], m, sizeof m);
        }
        memcpy(m, source + CAMERA_INVERSE, sizeof m);
        temporal_jitter_inverse(m, jx, jy);
        memcpy(bytes + CAMERA_INVERSE, m, sizeof m);
        g_t.jitter_address = address;
        g_t.jitter_submit = vk_submit_number();
        g_t.jitter_index = index;
        g_t.jitter_size = bound;
        if (!g_t.jittered && bound >= CAMERA_BYTES + 16) {
            /* once, the words after the matrices we know, as the game wrote them */
            char line[1024];
            int at = 0;
            for (u32 o = CAMERA_BYTES; o + 4 <= bound && at < (int)sizeof line - 12; o += 4) {
                u32 word;
                memcpy(&word, source + o, 4);
                at += snprintf(line + at, sizeof line - (size_t)at, " %08x", word);
            }
            LOG("arceus-temporal: camera block from +0x%x:%s\n", CAMERA_BYTES, line);
        }
        if (g_t.jittered++ < 3)
            LOG("arceus-temporal: camera block %llx drawn with jitter %g,%g pixels (offset %u of %d)\n",
                (unsigned long long)address, (double)pixels[0], (double)pixels[1], index, JITTERS);
    }
    *buffer = g_t.jitter_buffer;
    *offset = g_t.jitter_offset;
    return 1;
}

static float half_float(u16 h) {
    const int e = h >> 10 & 31, m = h & 1023;
    const float v = e == 0 ? ldexpf((float)m, -24) : e == 31 ? (m ? NAN : INFINITY) : ldexpf((float)(m + 1024), e - 25);
    return h & 0x8000 ? -v : v;
}

/* The numbers of the run the buffer holds, a second old by now: for the pixels sampled, the mean and longest camera
   vector, and by depth band how many differ from last frame by more than STATS_ERROR with the vector and without. */
static void stats_log(void) {
    static const char* const names[3] = {"near", "mid", "far"};
    unsigned n[3] = {0, 0, 0}, wrong[3] = {0, 0, 0}, wrong_still[3] = {0, 0, 0}, sky = 0, off = 0, changed = 0;
    unsigned shown = 0, shown_fine[2] = {0, 0}, still_fine[2] = {0, 0};
    unsigned object_n = 0, object_wrong = 0, object_camera = 0, object_still = 0, skin_near = 0;
    const int taa = g_t.mode == MODE_TAA;
    double length = 0, longest = 0;
    double drawn_gradient = 0, shown_gradient = 0;
    u16 capped[OBJECT_DRAWS + 1];
    memset(capped, 0, sizeof capped);
    for (u32 i = 0; i < STATS_W * STATS_H; i++) {
        /* the left half's texel of this sample, then the right half's */
        const u16* words = &g_t.stats_words[4 * (i / STATS_W * 2 * STATS_W + i % STATS_W)];
        const u16* more = words + 4 * STATS_W;
        const float moved = half_float(words[0]), still = half_float(words[1]);
        float vector = half_float(words[2]);
        const float z = half_float(words[3]);
        if (taa) {
            drawn_gradient += (double)half_float(more[0]);
            shown_gradient += (double)half_float(more[1]);
        }
        /* the probe marks a pixel that took an object's vector in the thousands of the length (temporal_stats.frag) */
        const int object_marks = vector >= 1000.0f ? (int)(vector / 1000.0f) : 0;
        vector -= 1000.0f * (float)object_marks;
        /* an object's vector at the cap: counted for the draw that left it */
        if ((object_marks & 1) && vector >= 989.5f) {
            const float number = half_float(more[0]);
            capped[number >= 1.0f && number <= (float)OBJECT_DRAWS ? (u32)(number + 0.5f) : 0]++;
        }
        if (z == -1.0f) {
            sky++;
            continue;
        }
        if (z == -2.0f) {
            off++;
            continue;
        }
        /* the projection gives z = 0.1 / distance: nearer than 10, to 100, beyond */
        const int band = z > 0.01f ? 0 : z > 0.001f ? 1 : 2;
        n[band]++;
        /* with anti-aliasing on the first number is three marks: 1 history dropped, 2 the frame as drawn changed
           since the last one, 4 the resolved picture changed since the last one */
        const int marks = taa ? (int)(moved + 0.5f) : 0;
        wrong[band] += taa ? marks & 1 : moved > STATS_ERROR;
        changed += marks >> 1 & 1;
        shown += marks >> 2 & 1;
        shown_fine[0] += marks >> 3 & 1;
        shown_fine[1] += marks >> 4 & 1;
        wrong_still[band] += still > STATS_ERROR;
        skin_near += object_marks >> 2 & 1;
        if (object_marks & 1) {
            object_n++;
            object_wrong += moved > STATS_ERROR;
            object_camera += object_marks >> 1 & 1;
            object_still += still > STATS_ERROR;
        }
        still_fine[0] += still > 0.05f;
        still_fine[1] += still > 0.02f;
        length += vector;
        if (vector > longest) longest = vector;
    }
    const unsigned all = n[0] + n[1] + n[2];
    char bands[600];
    int at = 0;
    /* with anti-aliasing on, the first number is where the history was dropped and the second where the resolved
       colour is far from the frame's own: trails behind what moves show there. frame_to_frame is the share of
       pixels that differ from the frame before as drawn: with a still camera, the jitter at work on the edges.
       shown_frame_to_frame is the same for the resolved picture, where the jitter should be gone. */
    if (taa && g_t.view == 4) {
        /* the jittered frame alone: nothing is resolved, so only the frame as drawn has a number */
        LOG("arceus-temporal-stats: run=%llu vector_mean=%.2f px frame_to_frame=%.1f%% (view 4: no resolve, no "
            "numbers for the shown picture)\n",
            (unsigned long long)g_t.stats_run, all ? length / all : 0.0, all ? 100.0 * changed / all : 0.0);
        return;
    }
    /* sharpness: the gradient length of the picture shown over that of the frame as drawn, summed over every
       sample. An edge the resolve smooths lowers it, and so does a blur of the whole picture. */
    if (taa)
        at += snprintf(bands, sizeof bands,
                       " frame_to_frame=%.1f%% shown_frame_to_frame=%.1f%% above_0.05=%.1f%% above_0.02=%.1f%% "
                       "sharpness=%.3f (gradient drawn %.5f, shown %.5f)",
                       all ? 100.0 * changed / all : 0.0, all ? 100.0 * shown / all : 0.0,
                       all ? 100.0 * shown_fine[0] / all : 0.0, all ? 100.0 * shown_fine[1] / all : 0.0,
                       drawn_gradient > 0.0 ? shown_gradient / drawn_gradient : 0.0,
                       drawn_gradient / (STATS_W * STATS_H), shown_gradient / (STATS_W * STATS_H));
    for (int b = 0; b < 3; b++)
        at += snprintf(bands + at, sizeof bands - (size_t)at, taa ? " %s n=%u dropped=%.1f%% far_from_frame=%.1f%%"
                                                               : " %s n=%u wrong=%.1f%% without_vectors=%.1f%%",
                       names[b], n[b], n[b] ? 100.0 * wrong[b] / n[b] : 0.0,
                       n[b] ? 100.0 * wrong_still[b] / n[b] : 0.0);
    /* the probe: the frame as drawn against the one before at the finer steps too, the base for the same shares
       of the resolved picture */
    if (!taa && at < (int)sizeof bands)
        at += snprintf(bands + at, sizeof bands - (size_t)at, " frame_to_frame_above_0.05=%.1f%% above_0.02=%.1f%%",
                       all ? 100.0 * still_fine[0] / all : 0.0, all ? 100.0 * still_fine[1] / all : 0.0);
    /* the pixels that took an object's vector: wrong with it, with the camera's vector, with none */
    if (!taa && g_t.objects && at < (int)sizeof bands)
        snprintf(bands + at, sizeof bands - (size_t)at,
                 " object_pixels n=%u wrong=%.1f%% with_camera_vectors=%.1f%% without_vectors=%.1f%% "
                 "skinned_depth_near_miss n=%u",
                 object_n, object_n ? 100.0 * object_wrong / object_n : 0.0,
                 object_n ? 100.0 * object_camera / object_n : 0.0, object_n ? 100.0 * object_still / object_n : 0.0,
                 skin_near);
    LOG("arceus-temporal-stats: run=%llu vector_mean=%.2f px vector_max=%.2f px sky=%u off_screen=%u%s\n",
        (unsigned long long)g_t.stats_run, all ? length / all : 0.0, longest, sky, off, bands);
    /* the draws that left vectors at the cap, four at most */
    int lines = 0;
    for (u32 d = 0; d <= OBJECT_DRAWS && lines < 4; d++) {
        const struct Drawn* w = &g_t.stats_drawn[d];
        if (!capped[d]) continue;
        lines++;
        if (!d) {
            LOG("arceus-temporal-long: run=%llu %u sampled pixels with a vector at the cap name no draw\n",
                (unsigned long long)g_t.stats_run, capped[d]);
            continue;
        }
        LOG("arceus-temporal-long: run=%llu %u sampled pixels with a vector at the cap from draw %u: %s, block %llx, "
            "feedback output %llx, matrix before from %s, origin moved %.1f px\n",
            (unsigned long long)g_t.stats_run, capped[d], d, w->skinned ? "skinned" : "rigid",
            (unsigned long long)w->block, (unsigned long long)w->feedback,
            w->source == TEMPORAL_BEFORE_OWN ? "its own address" : w->source == TEMPORAL_BEFORE_PARTNER
                                                                      ? "the other slot" : "none (this frame's)",
            (double)w->shift);
    }
}

static int stats_ready(void) {
    VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                              .size = (u64)2 * STATS_W * STATS_H * 8,
                              .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                              .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    VmaAllocationCreateInfo bai = {.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                                            VMA_ALLOCATION_CREATE_MAPPED_BIT,
                                   .usage = VMA_MEMORY_USAGE_AUTO};
    VmaAllocationInfo info;
    if (g_t.stats_buffer) return 1;
    if (!VK_OK(vmaCreateBuffer(g_vk.vma, &bci, &bai, &g_t.stats_buffer, &g_t.stats_memory, &info))) {
        g_t.stats_buffer = VK_NULL_HANDLE;
        return 0;
    }
    g_t.stats_words = info.pMappedData;
    return g_t.stats_words != NULL;
}

/* What a draw tells before it is recorded: the composite names the scene colour and depth, the first G-buffer draw
   of a frame has the camera, a write to the scene colour gives its surface, and the first draw that samples the
   scene colour names the program before which the probe runs from the next frame on. */
void vk_temporal_draw(GpuChannel* ch, const VkSh* fs0, Surf* const* sampled, int nt, Surf* const* rt, u32 n,
                      Surf* zt) {
    const u32 frame = g_vk_present + 1;
    if (!fs0) nt = 0;
    u32 colours = 0;
    int writes_scene = 0;
    for (u32 i = 0; i < n; i++) {
        if (!rt[i]) continue;
        colours++;
        if (g_t.scene && rt[i]->gva == g_t.scene) {
            g_t.scene_rt = rt[i];
            g_t.written_at = frame;
            writes_scene = 1;
        }
    }
    /* the composite: one colour target that is not the scene's float format, no depth target, and among its textures
       a float one and a depth one of the target's size */
    if (colours == 1 && n == 1 && !zt && rt[0]->f.vk != VK_FORMAT_B10G11R11_UFLOAT_PACK32 && !rt[0]->f.depth) {
        const Surf *colour = NULL, *depth = NULL;
        for (int i = 0; i < nt; i++) {
            const Surf* t = sampled[i];
            if (!t || t->w != rt[0]->w || t->h != rt[0]->h) continue;
            if (t->f.depth) depth = t;
            else if (t->f.vk == VK_FORMAT_B10G11R11_UFLOAT_PACK32) colour = t;
        }
        if (colour && depth && (colour->gva != g_t.scene || depth->gva != g_t.depth)) {
            g_t.scene = colour->gva;
            g_t.depth = depth->gva;
            g_t.reader = 0;
            LOG("arceus-temporal: scene colour %llx (%ux%u, format %d) and depth %llx (format %d) from the composite "
                "(fragment program %llx into %llx)\n",
                (unsigned long long)colour->gva, colour->w, colour->h, (int)colour->f.vk,
                (unsigned long long)depth->gva, (int)depth->f.vk, (unsigned long long)fs0->hash,
                (unsigned long long)rt[0]->gva);
        }
    }
    if (g_t.depth && zt && zt->gva == g_t.depth && colours >= 3 && g_t.camera_at != frame) camera_take(ch, zt);
    if (g_t.scene && !writes_scene && g_t.written_at == frame && g_t.reader_at != frame)
        for (int i = 0; i < nt; i++)
            if (sampled[i] && sampled[i]->gva == g_t.scene) {
                g_t.reader_at = frame;
                if (g_t.reader != fs0->hash) {
                    g_t.reader = fs0->hash;
                    LOG("arceus-temporal: the scene colour is first read by fragment program %llx\n",
                        (unsigned long long)fs0->hash);
                }
                break;
            }
}

/* Before the draw whose fragment program is the scene colour's first reader: the probe's passes. Once a frame. */
int vk_temporal_enabled(void) { return temporal_on(); }

/* Object vectors. The world matrix of each object block that a G-buffer draw binds is kept in a table by its
   address. The game writes a block to two slots in turn, one bound size apart, and every block of a frame takes the
   same turn as the camera's. So last frame's matrix is the table's entry for the other slot, the one the camera's
   step points back to. The slot itself is not read: the game may have filled it for the next frame already. */
typedef struct {
    u64 key;
    u32 at, before_at; /* the frames world and before were taken in */
    float world[16], before[16]; /* before: what this same address held the last time it was bound */
} Object;
static Object g_objects[OBJECTS];

static Object* object_slot(u64 key, u32 frame, int make) {
    Object* oldest = NULL;
    for (u32 i = 0; i < 8; i++) {
        Object* o = &g_objects[((key >> 8) * 0x9E3779B1u + i) % OBJECTS];
        if (o->key == key) return o;
        if (!oldest || o->at < oldest->at) oldest = o;
    }
    /* a slot not used for ten seconds may go */
    if (!make || (oldest->key && oldest->at + 600 > frame)) return NULL;
    memset(oldest, 0, sizeof *oldest);
    oldest->key = key;
    return oldest;
}

int vk_temporal_objects(void) { return temporal_on() && g_t.objects; }

/* The texture level shift for a draw into the scene (its depth target is the scene's), 0 for any other draw */
float vk_temporal_mip_bias(const Surf* zt) {
    return temporal_on() && g_t.mip_bias < 0.0f && zt && zt == g_t.depth_rt ? g_t.mip_bias : 0.0f;
}

static void objects_log(u32 frame) {
    if (g_t.object_frame == frame) return;
    g_t.object_frame = frame;
    g_t.object_draws = 0;
    if (++g_t.object_frames % 600) return;
    LOG("arceus-temporal-objects: in 600 frames, G-buffer draws with an object block %llu, without a block a frame "
        "ago %llu (never seen %llu, seen two frames ago or more %llu), found at the same address %llu, reading a "
        "feedback buffer %llu, jumped too far %llu, moved %llu, drawn again %llu, over the cap of %d a frame %llu, not "
        "an affine matrix %llu, pipelines made %llu, skinned: buffers kept %llu (a second time in a frame %llu), "
        "drawn again %llu, without last frame's buffer %llu, with a capture of another length %llu, blocks that "
        "are not their other slot's pair %llu\n",
        (unsigned long long)g_t.object_seen, (unsigned long long)g_t.object_new,
        (unsigned long long)g_t.object_never, (unsigned long long)g_t.object_older,
        (unsigned long long)g_t.object_same, (unsigned long long)g_t.object_skinned,
        (unsigned long long)g_t.object_far,
        (unsigned long long)g_t.object_moved, (unsigned long long)g_t.object_drawn, OBJECT_DRAWS,
        (unsigned long long)g_t.object_capped, (unsigned long long)g_t.object_odd,
        (unsigned long long)g_t.object_pipelines, (unsigned long long)g_t.skin_kept,
        (unsigned long long)g_t.skin_twice, (unsigned long long)g_t.skin_drawn, (unsigned long long)g_t.skin_first,
        (unsigned long long)g_t.skin_changed, (unsigned long long)g_t.object_unpaired);
    g_t.skin_kept = g_t.skin_drawn = g_t.skin_first = g_t.skin_twice = g_t.skin_changed = g_t.object_unpaired = 0;
    g_t.object_seen = g_t.object_new = g_t.object_moved = g_t.object_drawn = g_t.object_capped = g_t.object_odd = 0;
    g_t.object_far = g_t.object_same = g_t.object_never = g_t.object_older = g_t.object_skinned = 0;
}

/* The table's entry of the block at address with this frame's matrix in it. Several draws may bind one block: the
   first of a frame moves the matrix on. */
static Object* object_take(u64 address, const float world[16], u32 frame) {
    Object* o = object_slot(address, frame, 1);
    if (o && o->at != frame) {
        memcpy(o->before, o->world, sizeof o->before);
        o->before_at = o->at;
        memcpy(o->world, world, sizeof o->world);
        o->at = frame;
    }
    return o;
}

/* The matrix the block had a frame ago, NULL for none, and in source where it came from. The other slot of its
   pair is the one the camera's step points back to (temporal_before_choice has the rules). */
static const float* object_before(const Object* o, u64 address, u32 size, u32 frame, int skinned, int* source) {
    const Object* partner = NULL;
    if (g_t.camera_address[0] && g_t.camera_address[1]) {
        partner =
            object_slot(g_t.camera_address[0] > g_t.camera_address[1] ? address - size : address + size, frame, 0);
        if (partner && partner->at + 1 != frame) partner = NULL;
    }
    *source = temporal_before_choice(o->world, partner ? partner->world : NULL, o->before_at ? o->before : NULL,
                                     o->before_at ? frame - o->before_at : 0);
    if (*source == TEMPORAL_BEFORE_OWN) {
        g_t.object_same++;
        return o->before;
    }
    if (*source == TEMPORAL_BEFORE_PARTNER) return partner->world;
    if (*source == TEMPORAL_BEFORE_UNPAIRED) {
        g_t.object_unpaired++;
        if (g_t.unpaired_logged_at + 60 <= frame) { /* one example a second */
            g_t.unpaired_logged_at = frame;
            LOG("arceus-temporal-objects: block %llx (%s) is not the pair of its other slot: now %.2f %.2f %.2f, "
                "two frames ago here %.2f %.2f %.2f, a frame ago there %.2f %.2f %.2f\n",
                (unsigned long long)address, skinned ? "skinned" : "rigid", (double)o->world[12],
                (double)o->world[13], (double)o->world[14], (double)o->before[12], (double)o->before[13],
                (double)o->before[14], (double)partner->world[12], (double)partner->world[13],
                (double)partner->world[14]);
        }
        return NULL;
    }
    g_t.object_new++;
    if (!o->before_at) g_t.object_never++;
    else g_t.object_older++;
    return NULL;
}

/* Another object may have taken the slot: a jump of more than 5 units in a frame is not a move. 1 for a jump. */
static int object_jumped(u64 address, const float world[16], const float was[16], u32 frame) {
    const float dx = world[12] - was[12], dy = world[13] - was[13], dz = world[14] - was[14];
    const float distance = dx * dx + dy * dy + dz * dz;
    if (distance < 25.0f) return 0;
    g_t.object_far++;
    if (g_t.far_logged_at + 60 <= frame) { /* one example a second */
        g_t.far_logged_at = frame;
        LOG("arceus-temporal-objects: jump of %.2f units at block %llx: now %.2f %.2f %.2f, a frame ago "
            "%.2f %.2f %.2f\n",
            sqrt((double)distance), (unsigned long long)address, (double)world[12], (double)world[13],
            (double)world[14], (double)was[12], (double)was[13], (double)was[14]);
    }
    return 1;
}

/* Notes the extra draw that comes next, for the numbers: its block, where its matrix of a frame ago came from and
   how far its origin moved on screen. A long move with a still camera has a line, one a second. Returns the draw's
   number. */
static u32 drawn_note(u64 block, u64 feedback, int skinned, int source, const float world[16], const float* was,
                      const Surf* zt, u32 frame) {
    const u32 number = g_t.object_draws + 1;
    struct Drawn* d = &g_t.drawn[number <= OBJECT_DRAWS ? number : 0];
    d->block = block;
    d->feedback = feedback;
    d->skinned = (u8)skinned;
    d->source = (u8)source;
    d->shift = (float)temporal_origin_shift(g_t.now, world, g_t.before, was, (double)zt->sw, (double)zt->sh);
    const float camera =
        (float)temporal_origin_shift(g_t.now, world, g_t.before, world, (double)zt->sw, (double)zt->sh);
    if (d->shift > 100.0f && camera >= 0.0f && camera < 20.0f && g_t.long_logged_at + 60 <= frame) {
        g_t.long_logged_at = frame;
        LOG("arceus-temporal-objects: long move of %.0f px (the camera alone %.0f px) at block %llx (%s, before from "
            "%s): now %.2f %.2f %.2f, a frame ago %.2f %.2f %.2f\n",
            (double)d->shift, (double)camera, (unsigned long long)block, skinned ? "skinned" : "rigid",
            source == TEMPORAL_BEFORE_OWN ? "its own address" : "the other slot", (double)world[12],
            (double)world[13], (double)world[14], (double)was[12], (double)was[13], (double)was[14]);
    }
    return number;
}

int vk_temporal_object(const GpuChannel* ch, const Surf* zt, VkTemporalObject* out) {
    const u32 frame = g_vk_present + 1;
    const u64 address = ch->cbind[ST_VS][OBJECT_BANK];
    const u32 size = ch->cbind_size[ST_VS][OBJECT_BANK];
    if (!g_t.objects || g_t.camera_at != frame || zt != g_t.depth_rt || !address || size < 64 || size > 0x10000)
        return 0;
    objects_log(frame);
    g_t.object_seen++;
    float world[16];
    block_read(address, world);
    if (world[3] != 0.0f || world[7] != 0.0f || world[11] != 0.0f || world[15] != 1.0f) {
        g_t.object_odd++;
        return 0;
    }
    Object* o = object_take(address, world, frame);
    if (!o) return 0;
    int source;
    const float* was_world = object_before(o, address, size, frame, 0, &source);
    if (!was_world || !memcmp(was_world, world, sizeof world) || !g_t.have_before) return 0;
    if (object_jumped(address, world, was_world, frame)) return 0;
    g_t.object_moved++;
    if (g_t.object_draws >= OBJECT_DRAWS) {
        g_t.object_capped++;
        return 0;
    }
    /* this frame's clip as drawn -> without jitter -> world -> the object -> its world before -> the clip before,
       composed in double (temporal_object_matrices) */
    double jx = 0, jy = 0;
    if (g_t.mode == MODE_TAA && zt->sw && zt->sh) {
        float pixels[2];
        temporal_jitter(g_vk_present % JITTERS, JITTERS, pixels);
        jx = 2.0 * (double)pixels[0] / (double)jitter_width();
        jy = 2.0 * (double)pixels[1] / (double)jitter_height();
    }
    struct {
        float before[16], now[16], viewport[4], depth[4], size[4];
    } block;
    if (!temporal_object_matrices(g_t.inverse, world, was_world, g_t.before, jx, jy, block.now, block.before)) {
        g_t.object_odd++;
        return 0;
    }
    block.viewport[0] = g_t.viewport.x;
    block.viewport[1] = g_t.viewport.y;
    block.viewport[2] = g_t.viewport.width;
    block.viewport[3] = g_t.viewport.height;
    block.depth[0] = g_t.depth_translate;
    block.depth[1] = g_t.depth_scale;
    block.depth[2] = block.depth[3] = 0;
    block.size[0] = (float)zt->sw;
    block.size[1] = (float)zt->sh;
    /* in the probe the draw's number takes the place of the depth before, which only the resolve reads */
    const u32 number = drawn_note(address, 0, 0, source, world, was_world, zt, frame);
    block.size[2] = g_t.mode == MODE_PROBE ? (float)number : 0.0f;
    block.size[3] = 0;
    if (!(block.depth[1] != 0.0f) || !(block.viewport[2] != 0.0f) || !(block.viewport[3] != 0.0f)) return 0;
    if (!g_t.object_fs) {
        VkShaderModuleCreateInfo mi = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                       .codeSize = temporal_object_fs_spv_size,
                                       .pCode = temporal_object_fs_spv};
        if (!VK_OK(vkCreateShaderModule(g_vk.dev, &mi, NULL, &g_t.object_fs))) {
            g_t.object_fs = VK_NULL_HANDLE;
            return 0;
        }
    }
    if (!image_ensure(&g_t.object, OBJECT_FORMAT, zt->sw, zt->sh,
                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, "object vector"))
        return 0;
    u8* bytes = vk_stage(sizeof block, vk_stage_align(), &out->block, &out->offset);
    if (!bytes) return 0;
    memcpy(bytes, &block, sizeof block);
    out->size = sizeof block;
    out->fs = g_t.object_fs;
    out->format = OBJECT_FORMAT;
    return 1;
}

/* Skinned characters. The game skins a character with transform feedback into a buffer of positions in the
   object's own space, at the same address every frame. After each such draw the positions are copied into a buffer
   of ours, two per address in turn, so the frame after has both this frame's and last frame's. */
typedef struct {
    u64 address;
    u32 capacity, size[2], at[2]; /* at: the frame each of the two copies was made in */
    VkBuffer buffer[2];
    VmaAllocation memory[2];
} Skin;
static Skin g_skins[SKINS];

static Skin* skin_slot(u64 address, u32 frame, int make) {
    Skin* oldest = NULL;
    for (u32 i = 0; i < 8; i++) {
        Skin* s = &g_skins[((address >> 2) * 0x9E3779B1u + i) % SKINS];
        if (s->address == address) return s;
        const u32 used = s->at[0] > s->at[1] ? s->at[0] : s->at[1];
        if (!oldest || used < (oldest->at[0] > oldest->at[1] ? oldest->at[0] : oldest->at[1])) oldest = s;
    }
    const u32 used = oldest->at[0] > oldest->at[1] ? oldest->at[0] : oldest->at[1];
    if (!make || (oldest->address && used + 600 > frame)) return NULL;
    for (int k = 0; k < 2; k++)
        if (oldest->buffer[k]) vk_defer_buffer(oldest->buffer[k], oldest->memory[k]);
    memset(oldest, 0, sizeof *oldest);
    oldest->address = address;
    return oldest;
}

/* After a transform feedback draw, outside a pass: output 0 (size bytes at address, in source at offset) is copied
   to this frame's buffer of its address. */
void vk_temporal_skin_keep(VkCommandBuffer cb, u64 address, u32 size, VkBuffer source, u64 offset) {
    const u32 frame = g_vk_present + 1;
    if (!g_t.objects || !address || !size || size > SKIN_BYTES || !source) return;
    Skin* s = skin_slot(address, frame, 1);
    if (!s) return;
    if (size > s->capacity) {
        VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                  .size = size,
                                  .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                  .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
        VmaAllocationCreateInfo bai = {.usage = VMA_MEMORY_USAGE_AUTO};
        for (int k = 0; k < 2; k++) {
            if (s->buffer[k]) vk_defer_buffer(s->buffer[k], s->memory[k]);
            s->buffer[k] = VK_NULL_HANDLE;
            s->at[k] = 0;
            if (!VK_OK(vmaCreateBuffer(g_vk.vma, &bci, &bai, &s->buffer[k], &s->memory[k], NULL)))
                s->buffer[k] = VK_NULL_HANDLE;
        }
        s->capacity = s->buffer[0] && s->buffer[1] ? size : 0;
        if (!s->capacity) return;
    }
    const int turn = (int)(frame & 1);
    if (s->at[turn] == frame) g_t.skin_twice++; /* a second capture to this address in one frame */
    /* the capture's writes before the copy reads them, the copy's before a draw reads the vertices */
    VkMemoryBarrier2 before = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                               .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFORM_FEEDBACK_BIT_EXT,
                               .srcAccessMask = VK_ACCESS_2_TRANSFORM_FEEDBACK_WRITE_BIT_EXT,
                               .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
                               .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT};
    VkMemoryBarrier2 after = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                              .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
                              .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                              .dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT,
                              .dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT};
    VkDependencyInfo di = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .memoryBarrierCount = 1, .pMemoryBarriers = &before};
    const VkBufferCopy region = {.srcOffset = offset, .dstOffset = 0, .size = size};
    rec_CmdPipelineBarrier2(cb, &di);
    rec_CmdCopyBuffer(cb, source, s->buffer[turn], 1, &region);
    di.pMemoryBarriers = &after;
    rec_CmdPipelineBarrier2(cb, &di);
    s->size[turn] = size;
    s->at[turn] = frame;
    g_t.skin_kept++;
}

/* A G-buffer draw whose position stream starts offset bytes into the transform feedback output at address: 1 with
   what its vector draw needs when last frame's copy of that output is there. The draw is then made with our vertex
   and fragment programs: the position now from the game's binding, the position before from out->before. */
int vk_temporal_skin(const GpuChannel* ch, Surf* zt, u64 address, u64 offset, VkTemporalSkin* out) {
    const u32 frame = g_vk_present + 1;
    const u64 block_at = ch->cbind[ST_VS][OBJECT_BANK];
    const u32 block_size = ch->cbind_size[ST_VS][OBJECT_BANK];
    if (!g_t.objects || g_t.camera_at != frame || zt != g_t.depth_rt || !g_t.have_before || !block_at ||
        block_size < 64 || block_size > 0x10000)
        return 0;
    objects_log(frame);
    const Skin* s = skin_slot(address, frame, 0);
    const int last = (int)((frame - 1) & 1);
    if (!s || !s->capacity || s->at[last] + 1 != frame || offset >= s->size[last]) {
        g_t.skin_first++;
        return 0;
    }
    /* another mesh may have taken the address: this frame's capture must be there and as long as last frame's */
    if (s->at[!last] != frame || s->size[!last] != s->size[last]) {
        g_t.skin_changed++;
        return 0;
    }
    if (g_t.object_draws >= OBJECT_DRAWS) {
        g_t.object_capped++;
        return 0;
    }
    float world[16];
    block_read(block_at, world);
    if (world[3] != 0.0f || world[7] != 0.0f || world[11] != 0.0f || world[15] != 1.0f) {
        g_t.object_odd++;
        return 0;
    }
    /* the object's matrix a frame ago, or this frame's without one: the limbs' own motion stays */
    int source = TEMPORAL_BEFORE_NONE;
    const Object* o = object_take(block_at, world, frame);
    g_t.object_seen++;
    const float* was = o ? object_before(o, block_at, block_size, frame, 1, &source) : NULL;
    if (!was || object_jumped(block_at, world, was, frame)) {
        was = world;
        source = TEMPORAL_BEFORE_NONE;
    }
    double jx = 0, jy = 0;
    if (g_t.mode == MODE_TAA) {
        float pixels[2];
        temporal_jitter(g_vk_present % JITTERS, JITTERS, pixels);
        jx = 2.0 * (double)pixels[0] / (double)jitter_width();
        jy = 2.0 * (double)pixels[1] / (double)jitter_height();
    }
    struct {
        float drawn[16], now[16], before[16], viewport[4], depth[4], size[4];
    } block;
    temporal_skin_matrices(g_t.now, world, was, g_t.before, jx, jy, block.drawn, block.now, block.before);
    block.viewport[0] = g_t.viewport.x;
    block.viewport[1] = g_t.viewport.y;
    block.viewport[2] = g_t.viewport.width;
    block.viewport[3] = g_t.viewport.height;
    block.depth[0] = g_t.depth_translate;
    block.depth[1] = g_t.depth_scale;
    block.depth[2] = g_t.depth_zero_to_one ? 0.0f : 1.0f;
    /* near misses of the depth tolerance are marked for the numbers in the probe only: a mark takes the place of
       a vector another draw may have left on that pixel */
    block.depth[3] = g_t.mode == MODE_PROBE ? 1.0f : 0.0f;
    block.size[0] = (float)zt->sw;
    block.size[1] = (float)zt->sh;
    const u32 number = drawn_note(block_at, address, 1, source, world, was, zt, frame);
    block.size[2] = g_t.mode == MODE_PROBE ? (float)number : 0.0f;
    block.size[3] = 0;
    if (!(block.viewport[2] != 0.0f) || !(block.viewport[3] != 0.0f) || !layouts_make()) return 0;
    if (!g_t.skin_vs) g_t.skin_vs = vk_module_spv(temporal_skin_vs_spv, (size_t)(temporal_skin_vs_spv_size / 4));
    if (!g_t.skin_fs) g_t.skin_fs = vk_module_spv(temporal_skin_fs_spv, (size_t)(temporal_skin_fs_spv_size / 4));
    if (!g_t.skin_vs || !g_t.skin_fs) return 0;
    if (!image_ensure(&g_t.object, OBJECT_FORMAT, zt->sw, zt->sh,
                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, "object vector"))
        return 0;
    const VkComponentMapping identity = {0};
    out->depth = vk_tex_view(zt, VK_IMAGE_VIEW_TYPE_2D, 0, &identity, 0);
    if (!out->depth) return 0;
    u8* bytes = vk_stage(sizeof block, vk_stage_align(), &out->block, &out->offset);
    if (!bytes) return 0;
    memcpy(bytes, &block, sizeof block);
    out->size = sizeof block;
    out->vs = g_t.skin_vs;
    out->fs = g_t.skin_fs;
    out->format = OBJECT_FORMAT;
    out->sampler = g_t.point;
    out->before = s->buffer[last];
    out->before_offset = offset;
    out->before_size = s->size[last] - offset;
    g_t.skin_drawn++;
    return 1;
}

/* Begins the pass of one object's extra draw: the vector target, cleared at the frame's first, and the scene depth
   as the caller attaches it. The caller ends it. */
int vk_temporal_object_begin(VkCommandBuffer cb, const VkRenderingAttachmentInfo* depth, int stencil) {
    const u32 frame = g_vk_present + 1;
    const int first = g_t.object_at != frame;
    VkRenderingAttachmentInfo at = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                                    .imageView = g_t.object.view,
                                    .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                    .loadOp = first ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
                                    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                    .clearValue = {.color = {.float32 = {0.0f, 0.0f, -1.0f, 0.0f}}}};
    VkRenderingInfo ri = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                          .renderArea = {{0, 0}, {g_t.object.w, g_t.object.h}},
                          .layerCount = 1,
                          .colorAttachmentCount = 1,
                          .pColorAttachments = &at,
                          .pDepthAttachment = depth,
                          .pStencilAttachment = stencil ? depth : NULL};
    if (!g_t.object.view) return 0;
    if (first)
        vk_barrier(cb, g_t.object.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                   VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    else
        /* an earlier extra draw wrote the target in a pass of its own: this one loads and writes after it */
        vk_barrier(cb, g_t.object.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                   VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    g_t.object_at = frame;
    g_t.object_draws++;
    g_t.object_drawn++;
    rec_CmdBeginRendering(cb, &ri);
    return 1;
}

void vk_temporal_object_pipeline(void) { g_t.object_pipelines++; }

/* Read-only, for the skinned characters. A transform feedback draw's output is noted, and a G-buffer draw whose
   vertex stream starts inside one of this frame's outputs is counted, the first ones with a line. */
void vk_temporal_feedback(u64 address, u32 size, u32 index) {
    const u32 frame = g_vk_present + 1;
    if (g_t.feedback_frame != frame) {
        g_t.feedback_frame = frame;
        g_t.feedback_n = 0;
    }
    if (g_t.feedback_n < 256) {
        g_t.feedback[g_t.feedback_n].address = address;
        g_t.feedback[g_t.feedback_n].size = size;
        g_t.feedback[g_t.feedback_n++].index = index;
    }
}
int vk_temporal_feedback_find(u64 address, u32* index, u64* offset, u32* size) {
    if (g_t.feedback_frame != g_vk_present + 1) return 0;
    for (u32 i = 0; i < g_t.feedback_n; i++)
        if (address >= g_t.feedback[i].address && address < g_t.feedback[i].address + g_t.feedback[i].size) {
            *index = g_t.feedback[i].index;
            *offset = address - g_t.feedback[i].address;
            *size = g_t.feedback[i].size;
            return 1;
        }
    return 0;
}
int vk_temporal_skinned_line(void) {
    g_t.object_skinned++;
    return g_t.skinned_lines++ < 24;
}

void vk_temporal_before(u64 fs0_hash) {
    if (!temporal_on() || !g_t.reader || fs0_hash != g_t.reader) return;
    const u32 frame = g_vk_present + 1;
    if (g_t.ran_at == frame) return;
    Surf *scene = g_t.scene_rt, *depth = g_t.depth_rt;
    if (g_t.camera_at != frame || g_t.written_at != frame || !scene || !depth || !g_t.have_before ||
        scene->layers != 1 || scene->group || depth->group || scene->sw != depth->sw || scene->sh != depth->sh) {
        if (g_t.skipped++ < 4 || g_t.skipped % 1800 == 0)
            LOG("arceus-temporal: skipped (camera %d, scene written %d, previous matrix %d), %llu so far\n",
                g_t.camera_at == frame, g_t.written_at == frame, g_t.have_before, (unsigned long long)g_t.skipped);
        return;
    }
    g_t.ran_at = frame;
    if (!layouts_make()) return;
    if (!g_t.motion_pipe)
        g_t.motion_pipe = pipeline_make(MOTION_FORMAT, temporal_motion_fs_spv, temporal_motion_fs_spv_size, "motion");
    const int probe_view = g_t.mode == MODE_PROBE && g_t.view;
    if (probe_view && (!g_t.view_pipe || g_t.view_format != scene->f.vk)) {
        g_t.view_pipe = pipeline_make(scene->f.vk, temporal_view_fs_spv, temporal_view_fs_spv_size, "view");
        g_t.view_format = scene->f.vk;
    }
    const VkImageUsageFlags target = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    const VkImageUsageFlags copy = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (!g_t.motion_pipe || (probe_view && !g_t.view_pipe) ||
        !image_ensure(&g_t.motion, MOTION_FORMAT, scene->sw, scene->sh, target, "motion"))
        return;
    if (!g_t.stats_pipe)
        g_t.stats_pipe = pipeline_make(STATS_FORMAT, temporal_stats_fs_spv, temporal_stats_fs_spv_size, "numbers");
    if (!image_ensure(&g_t.colour[0], scene->f.vk, scene->sw, scene->sh, copy, "colour") ||
        !image_ensure(&g_t.colour[1], scene->f.vk, scene->sw, scene->sh, copy, "colour"))
        return;
    const VkComponentMapping identity = {0};
    const VkImageView depth_view = vk_tex_view(depth, VK_IMAGE_VIEW_TYPE_2D, 0, &identity, 0);
    if (!depth_view) return;

    Constants pc = {.viewport = {g_t.viewport.x, g_t.viewport.y, g_t.viewport.width, g_t.viewport.height},
                    .depth = {g_t.depth_translate, g_t.depth_scale, 0, 0},
                    .size = {(float)scene->sw, (float)scene->sh, (float)g_t.view, 40.0f}};
    vk_temporal_multiply(g_t.inverse, g_t.before, pc.reproject);
    if (!(pc.depth[1] != 0.0f) || !(pc.viewport[2] != 0.0f) || !(pc.viewport[3] != 0.0f)) return;

    vk_render_end();
    VkCommandBuffer cb = vk_cmd();
    /* objects that moved were drawn into their own target this frame: the vector pass takes their vectors from it */
    const int objects = g_t.objects && g_t.object_at == frame && g_t.object.view && g_t.object.w == scene->sw &&
                        g_t.object.h == scene->sh;
    /* the vectors: depth sampled, the motion image written */
    vk_surf_use(cb, depth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    vk_barrier(cb, g_t.motion.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
               VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
               VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    {
        Constants mc = pc;
        mc.depth[3] = objects ? 1.0f : 0.0f;
        if (objects)
            vk_barrier(cb, g_t.object.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                       VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                       VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        const VkImageView views[SETS_BINDINGS] = {depth_view, objects ? g_t.object.view : depth_view, depth_view,
                                                  depth_view, depth_view,                             depth_view,
                                                  depth_view};
        const VkSampler samplers[SETS_BINDINGS] = {g_t.point, g_t.point, g_t.point, g_t.point,
                                                   g_t.point, g_t.point, g_t.point};
        const VkDescriptorSet set = set_make(views, samplers);
        if (!set) return;
        fullscreen(cb, g_t.motion.view, g_t.motion.w, g_t.motion.h, g_t.motion_pipe, set, &mc);
    }
    vk_barrier(cb, g_t.motion.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
               VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
               VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    g_t.runs++;
    if (g_t.runs <= 2 || g_t.runs % 1800 == 0)
        LOG("arceus-temporal: run %llu at present %u, scene %ux%u, viewport %g,%g %gx%g, depth = %g + %g x z (%s), "
            "view %d\n",
            (unsigned long long)g_t.runs, g_vk_present, scene->sw, scene->sh, (double)g_t.viewport.x,
            (double)g_t.viewport.y, (double)g_t.viewport.width, (double)g_t.viewport.height,
            (double)g_t.depth_translate, (double)g_t.depth_scale,
            g_t.depth_zero_to_one ? "zero to one" : "minus one to one", g_t.view);

    /* this frame's colour is copied aside: the numbers and the error view compare it with last frame's */
    Image* now = &g_t.colour[!g_t.newest];
    Image* before = &g_t.colour[g_t.newest];
    const int history = g_t.history_at + 1 == frame;
    {
        const VkImageCopy region = {.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, scene->base_layer, 1},
                                    .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                    .extent = {scene->sw, scene->sh, 1}};
        vk_surf_use(cb, scene, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                    VK_ACCESS_2_TRANSFER_READ_BIT);
        vk_barrier(cb, now->img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                   VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        rec_CmdCopyImage(cb, scene->img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, now->img,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        vk_barrier(cb, now->img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
                   VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        g_t.newest = !g_t.newest;
        g_t.history_at = frame;
    }
    const int taa = g_t.mode == MODE_TAA;
    const VkSampler samplers[SETS_BINDINGS] = {g_t.point, g_t.point, g_t.linear, g_t.point,
                                               g_t.point, g_t.point, g_t.point};
    /* the frame before as it was drawn, for the numbers: with jitter its edges differ from this frame's */
    const VkImageView last_frame = history ? before->view : now->view;
    VkImageView numbers_before = last_frame;
    /* the picture shown a frame ago, for the numbers: the resolved one with anti-aliasing on */
    VkImageView shown_before = VK_NULL_HANDLE;
    int numbers = history, sharpened = 0;
    if (taa && g_t.view != 4) {
        /* the resolve: the scene colour and the new history are written, the old history and this frame's copy
           read. No history is used on the first frame, after a gap, after a change of size or a camera cut. */
        const VkFormat formats[2] = {scene->f.vk, HISTORY_FORMAT};
        if (!g_t.resolve_pipe || g_t.resolve_format != scene->f.vk) {
            g_t.resolve_pipe =
                pipeline_make_n(formats, 2, temporal_resolve_fs_spv, temporal_resolve_fs_spv_size, "resolve");
            g_t.resolve_format = scene->f.vk;
        }
        if (g_t.history[0].w != scene->sw || g_t.history[0].h != scene->sh) g_t.resolved_at = 0;
        if (!g_t.resolve_pipe ||
            !image_ensure(&g_t.history[0], HISTORY_FORMAT, scene->sw, scene->sh, target, "history") ||
            !image_ensure(&g_t.history[1], HISTORY_FORMAT, scene->sw, scene->sh, target, "history"))
            return;
        Image* kept = &g_t.history[g_t.history_newest];
        Image* made = &g_t.history[!g_t.history_newest];
        /* a cut: the screen's middle at a depth of ten moved by more than a quarter of the screen */
        const float* m = pc.reproject;
        const float w = 0.01f * m[11] + m[15], x = 0.01f * m[8] + m[12], y = 0.01f * m[9] + m[13];
        const int cut = !(w > 0.0f) || fabsf(x) > 0.5f * w || fabsf(y) > 0.5f * w;
        const int usable = g_t.resolved_at + 1 == frame && !cut;
        if (!usable && (g_t.resets++ < 4 || g_t.resets % 600 == 0))
            LOG("arceus-temporal: history not used (%s), %llu times so far\n", cut ? "camera cut" : "no frame before",
                (unsigned long long)g_t.resets);
        Constants rc = pc;
        rc.depth[2] = (float)g_t.options;
        rc.depth[3] = usable ? 0.0f : 1.0f;
        rc.size[3] = FRAME_WEIGHT;
        const VkImageView views[SETS_BINDINGS] = {g_t.motion.view, now->view, usable ? kept->view : now->view,
                                                  depth_view, last_frame, last_frame, last_frame};
        const VkDescriptorSet set = set_make(views, samplers);
        if (!set) return;
        vk_barrier(cb, made->img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                   VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        vk_surf_use(cb, scene, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        fullscreen2(cb, scene->att, made->view, scene->sw, scene->sh, g_t.resolve_pipe, set, &rc);
        vk_barrier(cb, made->img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                   VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        /* the sharpen: the resolved picture read from the new history, the scene colour written again. Not with
           the weight view, which is no picture. */
        if (g_t.sharpen > 0.0f && g_t.view != 3) {
            if (!g_t.sharpen_pipe || g_t.sharpen_format != scene->f.vk) {
                g_t.sharpen_pipe =
                    pipeline_make(scene->f.vk, temporal_sharpen_fs_spv, temporal_sharpen_fs_spv_size, "sharpen");
                g_t.sharpen_format = scene->f.vk;
            }
            const VkImageView sharpen_views[SETS_BINDINGS] = {made->view, made->view, made->view, made->view,
                                                              made->view, made->view, made->view};
            const VkDescriptorSet sharpen_set = g_t.sharpen_pipe ? set_make(sharpen_views, samplers) : VK_NULL_HANDLE;
            if (sharpen_set) {
                Constants sc = pc;
                sc.size[3] = g_t.sharpen;
                /* a second write of the scene colour after the resolve's: ordered behind it */
                vk_surf_use(cb, scene, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
                fullscreen(cb, scene->att, scene->sw, scene->sh, g_t.sharpen_pipe, sharpen_set, &sc);
                sharpened = 1;
            }
        }
        vk_surf_wrote(scene);
        vk_rt_drawn(scene);
        g_t.history_newest = !g_t.history_newest;
        g_t.resolved_at = frame;
        /* the numbers then compare the frame with what the resolve made of it */
        numbers_before = made->view;
        shown_before = usable ? kept->view : made->view;
        numbers = 1;
        pc.depth[2] = 1.0f;
    } else if (taa) {
        /* the jittered frame alone: no history to measure, the frame to frame number stays */
        numbers_before = now->view;
        pc.depth[2] = 1.0f;
    }

    /* without a frame before (the first one, or after a gap) the frame is compared with itself */
    /* the probe's numbers also look at the pixels that took an object's vector */
    if (!taa && objects) {
        shown_before = g_t.object.view;
        pc.depth[3] = 1.0f;
    }
    const int stats_due = numbers && g_t.runs % STATS_EVERY == 0;
    /* the picture as shown, for its gradient: the resolve's, or the scene colour itself after a sharpen */
    VkImageView final_view = numbers_before;
    if (sharpened && stats_due) {
        const VkImageView scene_view = vk_tex_view(scene, VK_IMAGE_VIEW_TYPE_2D, 0, &identity, 0);
        if (scene_view) {
            vk_surf_use(cb, scene, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
            final_view = scene_view;
        }
    }
    const VkImageView views[SETS_BINDINGS] = {
        g_t.motion.view, now->view, numbers_before, depth_view, last_frame,
        shown_before ? shown_before : numbers_before, final_view};
    const VkDescriptorSet set = set_make(views, samplers);
    if (!set) return;

    /* the numbers: every STATS_EVERY runs the small image is drawn and copied to memory, and the copy made that
       many runs ago, long finished, is read and logged */
    if (stats_due && g_t.stats_pipe && stats_ready() &&
        image_ensure(&g_t.stats, STATS_FORMAT, 2 * STATS_W, STATS_H,
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, "numbers")) {
        const VkBufferImageCopy out = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                       .imageExtent = {2 * STATS_W, STATS_H, 1}};
        /* the memory may not be coherent: what the GPU wrote is fetched before it is read */
        if (g_t.stats_run && VK_OK(vmaInvalidateAllocation(g_vk.vma, g_t.stats_memory, 0, VK_WHOLE_SIZE))) stats_log();
        vk_barrier(cb, g_t.stats.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_COPY_BIT, 0,
                   VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        fullscreen(cb, g_t.stats.view, 2 * STATS_W, STATS_H, g_t.stats_pipe, set, &pc);
        vk_barrier(cb, g_t.stats.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                   VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        rec_CmdCopyImageToBuffer(cb, g_t.stats.img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g_t.stats_buffer, 1, &out);
        g_t.stats_run = g_t.runs;
        memcpy(g_t.stats_drawn, g_t.drawn, sizeof g_t.stats_drawn);
    }
    if (taa || !g_t.view) return;
    /* a view of the probe, drawn over the scene */
    vk_surf_use(cb, scene, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    fullscreen(cb, scene->att, scene->sw, scene->sh, g_t.view_pipe, set, &pc);
    vk_surf_wrote(scene);
    vk_rt_drawn(scene);
}
