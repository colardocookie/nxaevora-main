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

/* the host's monotonic clock in nanoseconds (runtime/platform/plat.h, whose Windows headers define near and far,
   which are names here) */
u64 plat_ns(void);

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
#define OBJECT_SIGNATURES 8 /* draws of one block in a frame that are kept one by one */
#define OBJECT_TOUCHED 4096 /* blocks bound in a frame that are judged at its end */
/* why a frame's record of blocks is not complete (the links): record full, table full, a draw before the camera */
enum { LINK_RECORD, LINK_TABLE, LINK_EARLY };
#define MODE_PROBE 1
#define MODE_TAA 2
#define SETS_BINDINGS 7
#define STATS_FORMAT VK_FORMAT_R16G16B16A16_SFLOAT
#define STATS_W 96u /* the numbers' image: one texel a sampled pixel, in each of two halves side by side */
#define STATS_H 54u
/* Runs between two readings. Not a multiple of the jitter's period, so the readings walk through all its offsets:
   every 60 runs they saw two of the eight. */
#define STATS_EVERY 61u
/* Runs between two copies into the numbers' buffer at least. The host reads a copy at the next one, without waiting
   for it, so the copy must be done by then: the spacing stays well above the frames in flight. */
#define STATS_SPACING 8u
_Static_assert(STATS_SPACING >= 4 * VK_FRAMES && 2 * STATS_SPACING < STATS_EVERY, "numbers runs too close");
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
    float jitter[4]; /* this frame's jitter in clip units over w (x, y), which the camera's vectors take off */
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
    /* draws without a matrix of a frame ago, [0] rigid and [1] reading skinned positions: all of them, those whose
       matrix differs from what the address last held, those never bound before. Then the skinned reads' fates. */
    u64 uncovered[2][3], uncovered_blind[2], skin_plain, skin_left, skin_rigid;
    /* SNX_ARCEUS_TEMPORAL_LINKS: a block's other slot found by its draws. link_frame: the frame being recorded.
       Then the draws a link gave a matrix and the ones it did not by reason (TEMPORAL_LINK_*), the links that
       reached their fourth frame, the frames not complete by cause and the frames given up for a link. */
    int links;
    u32 link_frame, link_unknown_at[3], link_logged_at;
    /* link_left: draws left without a matrix, [rigid or skinned][not asked, then TEMPORAL_LINK_*][all, moving] */
    u64 link_left[2][7][2];
    u64 link_why[6], links_made, link_unknown_frames[3], link_early_draws, link_broke_frames;
    int reject; /* SNX_ARCEUS_TEMPORAL_REJECT: no history where an object's vector is far from the camera's */
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
    u64 skin_kept, skin_drawn, skin_first, skin_twice, skin_changed, skin_short, skin_unowned, skin_shared,
        skin_uncopied, skin_unkept;
    /* the learned pairs: why draws got no matrix of a frame ago, what the end of each frame judged, and the frames
       whose object vectors were given up because a pair broke after its draws */
    u64 object_learning, object_other, object_rest, object_full, object_unjudged;
    u64 pairs_learned, stays_learned, pairs_broken, rest_broken, broke_frames;
    void* touched[OBJECT_TOUCHED]; /* the table entries bound in touched_frame, for the judging at its end */
    u32 touched_n, touched_frame, broke_logged_at;
    /* the draw render.c is at (vk_temporal_object_note): its block and what the table gave it */
    struct {
        u64 address;
        void *slot, *other;
        const float* was;
        int source;
        u64 pair, signature; /* the pair's lower slot (the slot itself when it takes no turns), the draw */
        u64 pair_other;      /* a link's higher slot, 0 for a pair: the two tag the skinned copies */
        int chosen;          /* what the pairs said of the draw, before the guards */
        u64 link;            /* the linked slot whose matrix it took, 0 for none */
        int link_why;        /* what the links said of the draw (TEMPORAL_LINK_*), -1 when they were not asked */
        int unskinned;       /* it reads skinned positions and was not drawn again with last frame's */
        float world[16];
    } current;
    /* each extra draw of this frame by its number, and the same for the frame the numbers' image was drawn in:
       the probe writes the number to the vector target, so a wrong vector names its draw */
    struct Drawn {
        u64 block, feedback, signature, link;
        float shift; /* the origin's move on screen in pixels */
        u8 skinned, source, far; /* far: a long move with the camera nearly still */
        void *slot, *other; /* the table entries its matrix of a frame ago came from */
        const TemporalStreamCopy* stream; /* a skinned draw: this frame's copy at its capture address */
    } drawn[OBJECT_DRAWS + 1], stats_drawn[OBJECT_DRAWS + 1];
    /* stats_draws and stats_height: the extra draws and the scene's height of the frame the numbers' image was
       drawn in */
    u32 long_logged_at, stats_draws, stats_height;
    /* the jitter of the frame: decided at its first draw with the scene camera's block (jitter_decide) from that
       draw's depth target and registers, which vk_temporal_draw notes for every draw */
    const Surf* draw_zt;
    const u32* draw_r3d;
    /* SNX_ARCEUS_TEMPORAL_RECT: the scene may be a rectangle of its targets, and the frame goes through the
       rectangle's gate (temporal_math.h) in place of the whole-target one. The draw's channel and programs are
       kept for it, as are this frame's count of offsets and the making of the images a history is tied to. */
    int rect;
    const GpuChannel* draw_ch;
    const VkSh *draw_vs, *draw_fs;
    TemporalRectGate rect_gate;
    u32 offsets, rect_early_said, rect_fraction_said;
    u32 camera_new_at; /* the frame whose G-buffer draw showed a camera block address not known before */
    /* The draw noted: whether it goes into the scene (its depth target is the scene's depth, or a colour target is
       the scene colour) and its target's size. rect_kept: a scene draw of an undecided frame (rect_note). */
    int draw_scene, rect_stop_said;
    u32 draw_w, draw_h;
    struct {
        int have, shaped;
        u32 frame;
        TemporalRectState geometry;
        TemporalSceneBlock vertex, fragment;
    } rect_kept;
    /* the frame's copy of the scene as drawn, for the numbers and the views: what it was made from, and when */
    TemporalRectStamp rect_raw;
    u32 rect_raw_at;
    int mip_bias_set; /* SNX_ARCEUS_TEMPORAL_MIPBIAS holds a number, 0 included: no automatic bias */
    int rect_extras_off; /* the numbers and the views could not be made once: none for the rest of the session */
    u32 object_failed_at; /* the frame an object pass could not get what it needs (vk_temporal_object_failed) */
    /* SNX_ARCEUS_TEMPORAL_GPUTIME: -1 not looked at yet, 0 off or given up, 1 measuring. The pool has two queries
       a pair of the ring. time_bits and time_period are the graphics queue's clock. */
    int gputime;
    VkQueryPool time_pool;
    u32 time_bits;
    /* The window the kept times belong to: what our passes were configured as when it began (whole numbers
       only, so two can be held against each other), and the present it began at. A line is written, and a new
       window begun, every 600 presents and whenever the configuration is another. */
    TemporalGpuWindow time_window;
    double time_period;
    /* the brackets of the current run of our passes that are begun and not yet ended: -1 for none */
    TemporalGpuHandle time_hook, time_numbers;
    u64 rect_generation, rect_gated, rect_seen, rect_steady, rect_spoilt, rect_left;
    VkImage rect_images[5];
    TemporalGate gate;
    float jitter_ndc[2], jitter_made[2];
    u64 gated, mismatched, early, unsteady;
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
} g_t = {.mode = -1, .gputime = -1, .time_hook = -1, .time_numbers = -1};
static TemporalGpuRing g_time_ring; /* the brackets under way and the times kept since the last line */

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
        char* mip_end = NULL;
        g_t.mip_bias = g_t.mode == MODE_TAA && mip_bias ? strtof(mip_bias, &mip_end) : 0.0f;
        /* set: the line holds one number and nothing else. Without one the rectangle's gate gives its own bias. */
        g_t.mip_bias_set = g_t.mode == MODE_TAA && mip_bias && mip_end != mip_bias && !*mip_end &&
                           isfinite(g_t.mip_bias);
        if (!(g_t.mip_bias < 0.0f)) g_t.mip_bias = 0.0f;
        if (g_t.mip_bias < -2.0f) g_t.mip_bias = -2.0f;
        const char* vectors = getenv("SNX_ARCEUS_TEMPORAL_VECTORS");
        g_t.objects = g_t.mode && vectors && !strcmp(vectors, "object");
        const char* links = getenv("SNX_ARCEUS_TEMPORAL_LINKS");
        g_t.links = g_t.objects && links && !strcmp(links, "1");
        const char* reject = getenv("SNX_ARCEUS_TEMPORAL_REJECT");
        g_t.reject = g_t.objects && reject && !strcmp(reject, "1");
        const char* rect = getenv("SNX_ARCEUS_TEMPORAL_RECT");
        g_t.rect = g_t.mode && rect && !strcmp(rect, "1");
        g_t.offsets = JITTERS;
        /* Every option's state, on or off, in each run's log. A value other than the one named leaves an option
           off: NEAREST and LUMA take 1, HISTORY catmull, CLAMP variance, BLEND adaptive, VECTORS object, SHARPEN
           a number above 0, MIPBIAS one below 0. */
        if (g_t.mode)
            LOG("arceus-temporal: vectors %s\n", g_t.objects ? "of the camera and of objects that moved (object)"
                                                             : "of the camera only");
        if (g_t.mode)
            LOG("arceus-temporal: a block's other slot where it is not the neighbouring one: %s\n",
                g_t.links ? "looked for by its draws (links)" : "not looked for");
        if (g_t.mode)
            LOG("arceus-temporal: an object's vector further than an eighth of the scene's height from the "
                "camera's: %s\n", g_t.reject ? "rejected, the pixel takes no history (reject)" : "kept");
        if (g_t.mode)
            LOG("arceus-temporal: a scene inside a rectangle of its targets: %s\n",
                g_t.rect ? "taken when the scene block confirms it (rect)" : "refused, the whole target only");
        char bias[48] = "none";
        if (g_t.mip_bias_set) snprintf(bias, sizeof bias, "set to %.2f", (double)g_t.mip_bias);
        else if (g_t.rect && g_t.mode == MODE_TAA) snprintf(bias, sizeof bias, "automatic (log2 of the scene's scale)");
        if (g_t.mode == MODE_TAA)
            LOG("arceus-temporal: resolve options: history %s, clamp %s, weight %s, vector %s, blend %s, sharpen "
                "%.2f, texture level bias %s\n",
                g_t.options & 1 ? "Catmull-Rom (catmull)" : "linear", g_t.options & 2 ? "variance clip" : "min and max",
                g_t.options & 4 ? "by motion and clip (adaptive)" : "fixed",
                g_t.options & 8 ? "of the nearest depth" : "own", g_t.options & 16 ? "by brightness (luma)" : "plain",
                (double)g_t.sharpen, bias);
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
/* One pass over an image of w by h. area: the scene's rectangle when the pass may write that alone, NULL for the
   whole image. The viewport stays the image's, so a pixel keeps its place. */
static void fullscreen2(VkCommandBuffer cb, VkImageView view, VkImageView second, u32 w, u32 h, VkPipeline pipe,
                        VkDescriptorSet set, const Constants* pc, const TemporalRect* area) {
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
    VkRect2D sc = {{0, 0}, {w, h}};
    if (area) sc = (VkRect2D){{area->x, area->y}, {(u32)area->w, (u32)area->h}};
    VkRenderingInfo ri = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                          .renderArea = sc,
                          .layerCount = 1,
                          .colorAttachmentCount = second ? 2 : 1,
                          .pColorAttachments = at};
    VkViewport vp = {0, 0, (float)w, (float)h, 0, 1};
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
                       const Constants* pc, const TemporalRect* area) {
    fullscreen2(cb, view, VK_NULL_HANDLE, w, h, pipe, set, pc, area);
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
        if (address != g_t.camera_address[1]) g_t.camera_new_at = frame;
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
    /* The jitter was decided on another viewport than the scene's: the frame is left as drawn, so the next has no
       history, and this frame's object vectors are given up. */
    const TemporalViewport scene_view = {g_t.viewport.x, g_t.viewport.y, g_t.viewport.width, g_t.viewport.height};
    if (g_t.mode == MODE_TAA && !g_t.rect && temporal_gate_scene(&g_t.gate, frame, &scene_view)) {
        if (g_t.mismatched++ < 4 || g_t.mismatched % 600 == 0)
            LOG("arceus-temporal: the scene's viewport %g,%g %gx%g is not the one the jitter was decided on (%g,%g "
                "%gx%g), %llu frames so far\n",
                (double)g_t.viewport.x, (double)g_t.viewport.y, (double)g_t.viewport.width,
                (double)g_t.viewport.height, (double)g_t.gate.decided.x, (double)g_t.gate.decided.y,
                (double)g_t.gate.decided.width, (double)g_t.gate.decided.height,
                (unsigned long long)g_t.mismatched);
    }
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

/* The scene block of one stage at the draw vk_temporal_draw noted: whether the stage's program reads it as far as
   the fraction, and what is bound there (temporal_scene_block). */
static TemporalSceneBlock rect_block(int stage, const VkSh* program) {
    const GpuChannel* ch = g_t.draw_ch;
    const u64 address = ch->cbind[stage][TEMPORAL_SCENE_BANK];
    const u32 size = ch->cbind_size[stage][TEMPORAL_SCENE_BANK];
    const u8* bytes = address && size >= TEMPORAL_SCENE_BYTES ? gmem_host(address + TEMPORAL_SCENE_FRACTION, 8) : NULL;
    float words[2] = {0.0f, 0.0f};
    if (bytes) memcpy(words, bytes, sizeof words);
    return temporal_scene_block(program ? program->si.cbuf_mask : 0,
                                program ? program->si.cbuf_max[TEMPORAL_SCENE_BANK] : 0, address, size,
                                bytes ? words : NULL);
}
/* the geometry of the draw noted, from its own viewport in its target: 1 when it is a scene's */
static int rect_geometry(TemporalRectState* out) {
    const VkViewport v = vk_render_viewport(g_t.draw_r3d);
    const TemporalViewport view = {v.x, v.y, v.width, v.height};
    return temporal_rect_geometry(&view, (int)g_t.draw_w, (int)g_t.draw_h, TEMPORAL_CORNER_TOP_LEFT, out) ==
           TEMPORAL_RECT_OK;
}
static int rect_line(u64 n) { return n <= 4 || n % 600 == 0; }
/* 1 when the rectangle's gate let this frame be drawn with jitter */
static int rect_accepted(u32 frame) { return g_t.rect_gate.decided_at == frame && g_t.rect_gate.accepted; }

/* Our passes could not be made or run, or a draw could not be given its jitter, in a frame that was accepted,
   confirmed yet or not: its picture is shown as drawn, jittered in whole or in part. That must not repeat, so
   the gate accepts no frame any more this session (temporal_rect_failed). One line names what failed. */
static void rect_fail(u32 frame, const char* what) {
    TemporalRectGate* rg = &g_t.rect_gate;
    if (!g_t.rect || g_t.mode != MODE_TAA || rg->stopped || !rect_accepted(frame)) return;
    temporal_rect_failed(rg, frame);
    g_t.rect_stop_said = 1;
    LOG("arceus-temporal-rect: frame %u was drawn with jitter and the anti-aliasing passes failed (%s): no frame "
        "is drawn with jitter any more in this session\n", frame, what);
}

/* Under the rectangle's gate the passes after the resolve do not fail in silence, and do not come and go from
   frame to frame. The numbers and the views: none from here on, and a view that stood in for the picture gives
   way to the resolved picture. The frame and its history are not touched by it. */
static void rect_extras_off(const char* what) {
    if (!g_t.rect || g_t.rect_extras_off) return;
    g_t.rect_extras_off = 1;
    g_t.view = 0;
    LOG("arceus-temporal-rect: the numbers and the views are off for the rest of the session (%s could not be "
        "made)\n", what);
}

/* A scene draw of a decided frame, before our passes: the one vk_temporal_draw noted. The first whose vertex or
   fragment program reads the scene block as far as the fraction confirms the frame from what is bound there
   (temporal_rect_confirm), and it alone. Every one is held to the frame's viewport, and after the confirmation
   to its fraction in each stage that reads the block (temporal_rect_late). */
static void rect_watch(u32 frame) {
    TemporalRectGate* rg = &g_t.rect_gate;
    if (rg->decided_at != frame || g_t.ran_at == frame || !g_t.draw_scene) return;
    if (rg->state == TEMPORAL_FRAME_NONE) return;
    const TemporalSceneBlock vertex = rect_block(ST_VS, g_t.draw_vs), fragment = rect_block(ST_FS, g_t.draw_fs);
    const VkViewport v = vk_render_viewport(g_t.draw_r3d);
    TemporalRectState geometry;
    const int shaped = rect_geometry(&geometry);
    if (temporal_rect_late(rg, frame, shaped ? &geometry : NULL, &vertex, &fragment)) {
        if (rect_line(++g_t.rect_spoilt))
            LOG("arceus-temporal-rect: a later scene draw (fragment program %llx) disagrees with the frame: "
                "viewport %g,%g %gx%g in %ux%u against %g,%g %gx%g, scene block vertex %d/%d %g by %g, fragment "
                "%d/%d %g by %g, confirmed %g by %g. The frame is left as drawn and the next starts again, %llu "
                "frames so far\n",
                (unsigned long long)(g_t.draw_fs ? g_t.draw_fs->hash : 0), (double)v.x, (double)v.y,
                (double)v.width, (double)v.height, g_t.draw_w, g_t.draw_h, (double)rg->now.viewport.x,
                (double)rg->now.viewport.y, (double)rg->now.viewport.width, (double)rg->now.viewport.height,
                vertex.reads, vertex.bound, (double)vertex.fx, (double)vertex.fy, fragment.reads, fragment.bound,
                (double)fragment.fx, (double)fragment.fy, (double)rg->now.fx, (double)rg->now.fy,
                (unsigned long long)g_t.rect_spoilt);
        return;
    }
    if (rg->state != TEMPORAL_FRAME_DECIDED || (!vertex.reads && !fragment.reads)) return;
    const int was_accepted = rg->accepted;
    const int state = temporal_rect_confirm(rg, frame, &vertex, &fragment);
    if (state == TEMPORAL_FRAME_NONE) {
        if (rect_line(++g_t.rect_spoilt))
            LOG("arceus-temporal-rect: the first scene draw that reads the scene block (fragment program %llx) "
                "does not confirm the frame (vertex reads %d bound %d %g by %g, fragment reads %d bound %d %g by "
                "%g, rectangle %dx%d in %dx%d): the frame is left as drawn and the next starts again, %llu "
                "frames so far\n",
                (unsigned long long)(g_t.draw_fs ? g_t.draw_fs->hash : 0), vertex.reads, vertex.bound,
                (double)vertex.fx, (double)vertex.fy, fragment.reads, fragment.bound, (double)fragment.fx,
                (double)fragment.fy, rg->now.rect.w, rg->now.rect.h, rg->now.target_w, rg->now.target_h,
                (unsigned long long)g_t.rect_spoilt);
    } else if (state == TEMPORAL_FRAME_SEEN) {
        if (rect_line(++g_t.rect_seen))
            LOG("arceus-temporal-rect: fraction %g by %g confirmed for a frame that is not resolved (%s): it is the "
                "next frame's candidate, %llu frames so far\n",
                (double)rg->now.fx, (double)rg->now.fy,
                was_accepted ? "the fraction is new for this viewport" : "not accepted at its first draw",
                (unsigned long long)g_t.rect_seen);
    } else if (rect_line(++g_t.rect_steady))
        LOG("arceus-temporal-rect: frame confirmed and steady: rectangle %d,%d %dx%d of %dx%d, fraction %g by %g, "
            "%u offsets, %llu frames so far\n",
            rg->now.rect.x, rg->now.rect.y, rg->now.rect.w, rg->now.rect.h, rg->now.target_w, rg->now.target_h,
            (double)rg->now.fx, (double)rg->now.fy, g_t.offsets, (unsigned long long)g_t.rect_steady);
}

/* Every draw, from vk_temporal_draw, which comes before the draw's own constant blocks are bound. In a decided
   frame the draw is watched. In an undecided one a scene draw is kept: when the next draw comes and the frame is
   still undecided, the kept one was completed before the decision and goes to the gate as such
   (temporal_rect_before). The draw that turns out to decide the frame is not an earlier draw of its own frame:
   rect_decide drops it from here and watches it after its decision. */
static void rect_note(u32 frame) {
    TemporalRectGate* rg = &g_t.rect_gate;
    if (!g_t.rect) return;
    if (g_t.rect_kept.have && g_t.rect_kept.frame == frame && rg->decided_at != frame)
        temporal_rect_before(rg, frame, g_t.rect_kept.shaped ? &g_t.rect_kept.geometry : NULL, &g_t.rect_kept.vertex,
                             &g_t.rect_kept.fragment);
    g_t.rect_kept.have = 0;
    if (g_t.ran_at == frame || !g_t.draw_scene) return;
    if (rg->decided_at == frame) {
        rect_watch(frame);
        return;
    }
    g_t.rect_kept.have = 1;
    g_t.rect_kept.frame = frame;
    g_t.rect_kept.shaped = rect_geometry(&g_t.rect_kept.geometry);
    g_t.rect_kept.vertex = rect_block(ST_VS, g_t.draw_vs);
    g_t.rect_kept.fragment = rect_block(ST_FS, g_t.draw_fs);
}

/* The rectangle's gate at a draw that binds the scene camera's block while the frame is undecided. The frame is
   decided here whatever the draw is: one that is not into the scene's depth cannot be tied to the scene and
   leaves the frame without a geometry. Else the draw's own viewport decides (temporal_rect_decide), after a scene
   block that happens to be bound at this draw had its say (temporal_rect_early: it can only refuse). */
static void rect_decide(u32 frame) {
    TemporalRectGate* rg = &g_t.rect_gate;
    const int depth_draw = g_t.draw_zt && g_t.depth && g_t.draw_zt->gva == g_t.depth && g_t.draw_r3d && g_t.draw_ch;
    g_t.jitter_ndc[0] = g_t.jitter_ndc[1] = 0;
    g_t.rect_kept.have = 0; /* this draw decides: it is not a draw before the decision */
    /* The block's address was learned in this very frame, at its G-buffer: draws before that one may have bound
       it without being known, so this is not the frame's first such draw and the frame has no geometry. */
    if (g_t.camera_new_at == frame || !depth_draw) {
        temporal_rect_decide(rg, frame, NULL, 0);
        if (g_t.camera_new_at == frame) {
            if (rect_line(++g_t.rect_gated))
                LOG("arceus-temporal-rect: frame gated (the scene camera's block was at an address first seen in "
                    "this frame, so its first draw is not known), %llu frames so far\n",
                    (unsigned long long)g_t.rect_gated);
        } else if (rect_line(++g_t.early))
            LOG("arceus-temporal: a draw with depth target %llx binds the scene camera's block before the frame's "
                "first draw into the scene depth: no jitter this frame, %llu frames so far\n",
                (unsigned long long)(g_t.draw_zt ? g_t.draw_zt->gva : 0), (unsigned long long)g_t.early);
    } else {
        const VkViewport v = vk_render_viewport(g_t.draw_r3d);
        TemporalRectState geometry;
        const int shaped = rect_geometry(&geometry);
        /* A viewport whose only fault is that it is not on whole pixels: said once, in words, with the scales
           that would be for this very target. The frame is gated like any other without a geometry. */
        const TemporalViewport asked = {v.x, v.y, v.width, v.height};
        if (!shaped && temporal_viewport_fractional(&asked, (int)g_t.draw_w, (int)g_t.draw_h) &&
            !g_t.rect_fraction_said++) {
            char scales[96];
            const int found = temporal_whole_scales((int)g_t.draw_w, (int)g_t.draw_h, scales, sizeof scales);
            LOG("arceus-temporal-rect: the scene's viewport %g,%g %gx%g is not on whole pixels of its %ux%u target, "
                "so no frame of it is taken: the game draws the target's size times SNX_ARCEUS_SCENE_SCALE as it "
                "comes. Scales p/q with q up to 8 that give whole pixels in both axes of this target: %s. That is "
                "about whole pixels only, and no promise that a frame is taken\n",
                (double)v.x, (double)v.y, (double)v.width, (double)v.height, g_t.draw_w, g_t.draw_h,
                found ? scales : "none");
        }
        /* the scene block as bound at this draw, whether or not its programs read it: vertex stage first */
        TemporalSceneBlock early = rect_block(ST_VS, NULL);
        int stage = ST_VS;
        if (!early.bound) {
            early = rect_block(ST_FS, NULL);
            stage = ST_FS;
        }
        if (!g_t.rect_early_said++)
            LOG("arceus-temporal-rect: at the frame's first draw with the scene camera's block, a scene block of "
                "0x%x bytes or more is %s%s\n",
                TEMPORAL_SCENE_BYTES, early.bound ? "bound in the " : "not bound",
                !early.bound ? "" : stage == ST_VS ? "vertex stage" : "fragment stage");
        const int refused = shaped && temporal_rect_early(&geometry, &early);
        const int came_early = rg->early_at == frame;
        const int on = temporal_rect_decide(rg, frame, shaped ? &geometry : NULL, refused);
        if (shaped) g_t.offsets = temporal_rect_offsets(&geometry.rect, geometry.target_w, geometry.target_h);
        if (on && g_t.mode == MODE_TAA) {
            float pixels[2];
            temporal_jitter(g_vk_present % g_t.offsets, g_t.offsets, pixels);
            g_t.jitter_ndc[0] = 2.0f * pixels[0] / fabsf(v.width);
            g_t.jitter_ndc[1] = 2.0f * pixels[1] / fabsf(v.height);
        } else if (!on && rect_line(++g_t.rect_gated))
            LOG("arceus-temporal-rect: frame gated at its first draw with the scene camera's block (viewport %g,%g "
                "%gx%g in a target of %ux%u: %s), %llu frames so far\n",
                (double)v.x, (double)v.y, (double)v.width, (double)v.height, g_t.draw_w, g_t.draw_h,
                !shaped       ? "not the whole target and no rectangle of a scene at its corner"
                : rg->stopped ? "the gate has stopped for this session"
                : came_early  ? "scene draws came before it"
                : refused     ? "the scene block bound at this draw has another fraction"
                              : "not the viewport of a confirmed frame before",
                (unsigned long long)g_t.rect_gated);
    }
    if (rg->stopped && !g_t.rect_stop_said++)
        LOG("arceus-temporal-rect: %d frames in a row were drawn with jitter and the anti-aliasing passes were "
            "not reached in them: no frame is drawn with jitter any more in this session\n", TEMPORAL_RECT_UNHOOKED);
    /* this draw is a scene draw of the frame like the ones after it */
    rect_watch(frame);
}

/* Whether this frame is drawn with jitter (the gate of temporal_math.h), asked at a draw that binds the scene
   camera's block while the frame is undecided. A draw into the scene's depth decides it from its own viewport.
   Any other draw cannot be tied to the scene and gates the whole frame, so no two draws of a frame differ in
   jitter. 1 when the frame is decided now. */
static int jitter_decide(u32 frame) {
    const Surf* zt = g_t.draw_zt;
    if (!zt || !g_t.depth || zt->gva != g_t.depth || !g_t.draw_r3d) {
        const int first = g_t.gate.early_at != frame;
        if (temporal_gate_early(&g_t.gate, frame) && first && (g_t.early++ < 4 || g_t.early % 600 == 0))
            LOG("arceus-temporal: a draw with depth target %llx binds the scene camera's block before the frame's "
                "first draw into the scene depth: no jitter this frame, %llu frames so far\n",
                (unsigned long long)(zt ? zt->gva : 0), (unsigned long long)g_t.early);
        return 0;
    }
    const VkViewport v = vk_render_viewport(g_t.draw_r3d);
    const float w = (float)zt->sw, h = (float)zt->sh;
    const TemporalViewport view = {v.x, v.y, v.width, v.height};
    const int on = temporal_gate_decide(&g_t.gate, frame, &view, w, h);
    g_t.jitter_ndc[0] = g_t.jitter_ndc[1] = 0;
    if (on) {
        float pixels[2];
        temporal_jitter(g_vk_present % JITTERS, JITTERS, pixels);
        g_t.jitter_ndc[0] = 2.0f * pixels[0] / fabsf(v.width);
        g_t.jitter_ndc[1] = 2.0f * pixels[1] / fabsf(v.height);
    } else if (g_t.gated++ < 4 || g_t.gated % 600 == 0)
        LOG("arceus-temporal: no jitter and no history this frame (viewport %g,%g %gx%g in a target of %gx%g, not "
            "the whole target or not last frame's), %llu frames so far\n",
            (double)v.x, (double)v.y, (double)v.width, (double)v.height, (double)w, (double)h,
            (unsigned long long)g_t.gated);
    return 1;
}

/* this frame's jitter as the draws got it, in clip units over w: 0 when the frame has none */
static void frame_jitter(u32 frame, double* jx, double* jy) {
    const int on = g_t.mode == MODE_TAA &&
                   (g_t.rect ? rect_accepted(frame) : temporal_gate_jitter(&g_t.gate, frame));
    *jx = on ? (double)g_t.jitter_ndc[0] : 0.0;
    *jy = on ? (double)g_t.jitter_ndc[1] : 0.0;
}

/* With the rectangle's gate: a stage of the draw about to be recorded reads bank 4 and this is bound there. When
   it is the scene camera's block the frame is decided now, before the draw's texture level bias is asked for and
   its samplers are chosen, so that the deciding draw has the frame's bias like every draw after it. */
void vk_temporal_camera_seen(u64 address, u32 bound, u32 size) {
    if (!g_t.rect || !address || (address != g_t.camera_address[0] && address != g_t.camera_address[1])) return;
    if (bound > 0x1000 || bound < CAMERA_BYTES || size > bound) return;
    if (g_t.rect_gate.decided_at != g_vk_present + 1) rect_decide(g_vk_present + 1);
}

/* A draw binds constant bank 4: when it is the scene camera's block and anti-aliasing is on, the draw gets a copy
   whose projection is shifted by this frame's jitter. Guest memory is not written, so the game and our own matrices
   keep the block without jitter. The copy is made once a submission in the frame's staging. With row vectors a shift
   of clip x and y by jitter x w is the matrix times J, which has the jitter in its fourth row: view-projection x J at
   +0x00, projection x J at +0x80, and J's inverse x the inverse view-projection at +0xC0. */
int vk_temporal_camera_block(u64 address, u32 bound, u32 size, VkBuffer* buffer, u64* offset) {
    /* Without the rectangle's gate no draw is jittered before our passes ran once. With it the gate's own start
       (a confirmed frame before) stands in for that, and the frame must be decided before our passes can run. */
    if ((g_t.mode != MODE_TAA && !g_t.rect) || !address || (!g_t.rect && !g_t.motion.w) ||
        (address != g_t.camera_address[0] && address != g_t.camera_address[1]))
        return 0;
    if (bound > 0x1000 || bound < CAMERA_BYTES || size > bound) return 0;
    /* With the rectangle's gate the frame is decided here in the probe too, which then draws without jitter. The
       renderer asks before the draw's samplers are chosen (vk_temporal_camera_seen), so this is a second look. */
    if (g_t.rect && g_t.rect_gate.decided_at != g_vk_present + 1) rect_decide(g_vk_present + 1);
    if (g_t.mode != MODE_TAA) return 0;
    /* only +0x00, +0x80 and +0xC0 are shifted: a program that reads further is worth knowing about */
    if (size > g_t.camera_read) {
        g_t.camera_read = size;
        LOG("arceus-temporal: a program reads %u of the camera block's %u bytes\n", size, bound);
    }
    const u32 offsets = g_t.rect ? g_t.offsets : JITTERS, index = g_vk_present % offsets;
    if (g_t.rect) {
        if (!rect_accepted(g_vk_present + 1)) return 0;
    } else {
        if (g_t.gate.decided_at != g_vk_present + 1 && !jitter_decide(g_vk_present + 1)) return 0;
        if (!temporal_gate_jitter(&g_t.gate, g_vk_present + 1)) return 0;
    }
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
        g_t.jitter_size != bound || g_t.jitter_made[0] != g_t.jitter_ndc[0] ||
        g_t.jitter_made[1] != g_t.jitter_ndc[1] || memcmp(source, g_t.jitter_source, bound)) {
        u8* bytes = vk_stage(bound, vk_stage_align(), &g_t.jitter_buffer, &g_t.jitter_offset);
        float pixels[2], m[16];
        if (!bytes) {
            rect_fail(g_vk_present + 1, "no room for the jittered camera block");
            return 0;
        }
        memcpy(bytes, source, bound);
        memcpy(g_t.jitter_source, source, bound);
        temporal_jitter(index, offsets, pixels);
        /* in the viewport's pixels the frame's jitter was decided on, which are the target's */
        const float jx = g_t.jitter_ndc[0], jy = g_t.jitter_ndc[1];
        g_t.jitter_made[0] = jx;
        g_t.jitter_made[1] = jy;
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
            LOG("arceus-temporal: camera block %llx drawn with jitter %g,%g pixels (offset %u of %u)\n",
                (unsigned long long)address, (double)pixels[0], (double)pixels[1], index, offsets);
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
    /* per extra draw: its sampled pixels, and those wrong with its vector, with the camera's, with none */
    /* and those rejected, and of them the ones without a finite distance from the camera's vector */
    static u16 per_draw[OBJECT_DRAWS + 1][6];
    static float per_draw_far[OBJECT_DRAWS + 1], per_draw_long[OBJECT_DRAWS + 1];
    memset(per_draw_long, 0, sizeof per_draw_long);
    unsigned rejected = 0, rejected_blind = 0, rejected_all = 0;
    float rejected_far = 0.0f;
    memset(per_draw_far, 0, sizeof per_draw_far);
    memset(capped, 0, sizeof capped);
    memset(per_draw, 0, sizeof per_draw);
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
        /* A rejected object pixel: how far its vector is from the camera's, 60000 for no finite distance
           (temporal_stats.frag). Sky and samples whose vector leaves the picture are not among the counted ones. */
        const float residual = half_float(more[2]);
        const int blind = residual > 59500.0f;
        if (residual > 0.0f) {
            rejected_all++;
            rejected_blind += blind;
            if (!blind && residual > rejected_far) rejected_far = residual;
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
        rejected += residual > 0.0f;
        skin_near += object_marks >> 2 & 1;
        if (object_marks & 1) {
            const float number = half_float(more[0]);
            const u32 d = number >= 1.0f && number <= (float)OBJECT_DRAWS ? (u32)(number + 0.5f) : 0;
            u16* tally = per_draw[d];
            tally[0]++;
            tally[1] += moved > STATS_ERROR;
            tally[2] += object_marks >> 1 & 1;
            tally[3] += still > STATS_ERROR;
            if (vector > per_draw_long[d]) per_draw_long[d] = vector;
            if (residual > 0.0f) {
                tally[4]++;
                tally[5] += blind;
                if (!blind && residual > per_draw_far[d]) per_draw_far[d] = residual;
            }
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
    /* rejected: counted in this sampled frame only, one frame of the numbers, not every frame */
    /* two counts: of every sample, and of the eligible ones the shares above are taken of (not sky, the vector
       inside the picture). The distance is the largest finite one, which the shader caps at 59000. */
    char refused[260] = "";
    if (g_t.reject)
        snprintf(refused, sizeof refused, " rejected_sampled of_all n=%u of %u, of_eligible n=%u of %u, "
                 "without_finite_camera_distance n=%u, largest_finite_distance=%.0f px (a half float, capped at 59000)",
                 rejected_all, STATS_W * STATS_H, rejected, all, rejected_blind, (double)rejected_far);
    LOG("arceus-temporal-stats: run=%llu vector_mean=%.2f px vector_max=%.2f px sky=%u off_screen=%u%s%s\n",
        (unsigned long long)g_t.stats_run, all ? length / all : 0.0, longest, sky, off, bands, refused);
    /* The probe names the draws that left a sampled vector longer than an eighth of the scene's height in that
       frame, four at most. The length has the camera's part in it, so a fast pan names draws too and nothing here
       says a pixel would be rejected. It is a half float, cut at 990 px by the numbers' shader. */
    int long_named = 0;
    for (u32 d = 1; !taa && g_t.stats_height && d <= g_t.stats_draws && d <= OBJECT_DRAWS && long_named < 4; d++) {
        const struct Drawn* w = &g_t.stats_drawn[d];
        if (!(per_draw_long[d] > (float)g_t.stats_height / 8.0f)) continue;
        long_named++;
        LOG("arceus-temporal-longest: run=%llu draw %u (%s, block %llx, draw %llx, matrix before from %s, origin "
            "moved %.0f px): longest sampled vector about %.0f px with the camera's part (990 at most), sampled pixels "
            "n=%u wrong=%u "
            "with_camera_vectors=%u\n",
            (unsigned long long)g_t.stats_run, d, w->skinned ? "skinned" : "rigid", (unsigned long long)w->block,
            (unsigned long long)w->signature,
            w->source == TEMPORAL_BEFORE_OWN    ? "its own address"
            : w->source == TEMPORAL_BEFORE_LINK ? "its linked slot"
                                                : "the other slot",
            (double)w->shift, (double)per_draw_long[d], per_draw[d][0], per_draw[d][1], per_draw[d][2]);
    }
    /* the probe names the draws of the rejected samples, four at most */
    int named = 0;
    for (u32 d = 0; !taa && d <= OBJECT_DRAWS && named < 4; d++) {
        const struct Drawn* w = &g_t.stats_drawn[d];
        if (!per_draw[d][4]) continue;
        named++;
        if (!d || d > g_t.stats_draws) {
            LOG("arceus-temporal-rejected: run=%llu %u sampled pixels rejected name no draw\n",
                (unsigned long long)g_t.stats_run, per_draw[d][4]);
            continue;
        }
        LOG("arceus-temporal-rejected: run=%llu draw %u (%s, block %llx, draw %llx): %u of its %u eligible sampled "
            "pixels rejected (%u without a finite camera distance, the largest finite one about %.0f px, a half "
            "float capped at 59000), wrong with its vector %u\n",
            (unsigned long long)g_t.stats_run, d, w->skinned ? "skinned" : "rigid", (unsigned long long)w->block,
            (unsigned long long)w->signature, per_draw[d][4], per_draw[d][0], per_draw[d][5],
            (double)per_draw_far[d], per_draw[d][1]);
    }
    /* the draws whose origin moved far on screen: what the picture says of their vectors, four at most */
    int moves = 0;
    for (u32 d = 1; !taa && d <= g_t.stats_draws && d <= OBJECT_DRAWS && moves < 4; d++) {
        const struct Drawn* w = &g_t.stats_drawn[d];
        if (!w->far) continue;
        moves++;
        LOG("arceus-temporal-moved: run=%llu draw %u (%s, block %llx, origin moved %.0f px): sampled pixels n=%u "
            "wrong=%u with_camera_vectors=%u without_vectors=%u\n",
            (unsigned long long)g_t.stats_run, d, w->skinned ? "skinned" : "rigid", (unsigned long long)w->block,
            (double)w->shift, per_draw[d][0], per_draw[d][1], per_draw[d][2], per_draw[d][3]);
    }
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
            w->source == TEMPORAL_BEFORE_OWN       ? "its own address"
            : w->source == TEMPORAL_BEFORE_PARTNER ? "the other slot"
            : w->source == TEMPORAL_BEFORE_LINK    ? "its linked slot"
                                                   : "none (this frame's)",
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
void vk_temporal_draw(GpuChannel* ch, const VkSh* vs, const VkSh* fs0, Surf* const* sampled, int nt,
                      Surf* const* rt, u32 n, Surf* zt) {
    const u32 frame = g_vk_present + 1;
    g_t.draw_zt = zt;
    g_t.draw_r3d = ch->r3d;
    g_t.draw_ch = ch;
    g_t.draw_vs = vs;
    g_t.draw_fs = fs0;
    if (!fs0) nt = 0;
    u32 colours = 0;
    int writes_scene = 0;
    const Surf* scene_colour = NULL;
    for (u32 i = 0; i < n; i++) {
        if (!rt[i]) continue;
        colours++;
        if (g_t.scene && rt[i]->gva == g_t.scene) {
            g_t.scene_rt = rt[i];
            g_t.written_at = frame;
            writes_scene = 1;
            scene_colour = rt[i];
        }
    }
    /* for the rectangle's gate: a scene draw has the scene's depth as its depth target or the scene colour among
       its colour targets, and its viewport is held against that target's size */
    const int depth_draw = zt && g_t.depth && zt->gva == g_t.depth;
    g_t.draw_scene = depth_draw || scene_colour;
    g_t.draw_w = depth_draw ? zt->sw : scene_colour ? scene_colour->sw : 0;
    g_t.draw_h = depth_draw ? zt->sh : scene_colour ? scene_colour->sh : 0;
    rect_note(frame);
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

/* The making of the images a frame's pictures are tied to under the rectangle's gate: a number that changes when
   the scene's image, a copy of the frame or a history image is another than the last time. */
static u64 rect_making(const Surf* scene) {
    const VkImage now[5] = {scene->img, g_t.colour[0].img, g_t.colour[1].img, g_t.history[0].img, g_t.history[1].img};
    if (memcmp(now, g_t.rect_images, sizeof now)) {
        memcpy(g_t.rect_images, now, sizeof now);
        g_t.rect_generation++;
    }
    return g_t.rect_generation;
}

/* SNX_ARCEUS_TEMPORAL_GPUTIME=1, read only: the approximate time between two completions on the GPU's clock,
   around our passes (temporal_math.h has the rules). Off, nothing here adds a command or a query. A failure
   switches the measure off for the session with one line and touches nothing else. The pool is never
   destroyed in a session: marks recorded with it may still be on their way. */
static void gputime_off(const char* why) {
    temporal_gpu_stop(&g_time_ring);
    g_t.gputime = 0;
    LOG("arceus-temporal-gputime: off for the rest of the session (%s)\n", why);
}
static int gputime_on(void) {
    if (g_t.gputime >= 0) return g_t.gputime;
    const char* asked = getenv("SNX_ARCEUS_TEMPORAL_GPUTIME");
    g_t.gputime = 0;
    if (!asked || strcmp(asked, "1")) return 0;
    VkQueueFamilyProperties families[32];
    u32 n = 32;
    vkGetPhysicalDeviceQueueFamilyProperties(g_vk.phys, &n, families);
    g_t.time_bits = g_vk.gfx_family < n ? families[g_vk.gfx_family].timestampValidBits : 0;
    g_t.time_period = (double)g_vk.props.limits.timestampPeriod;
    if (!temporal_gpu_usable(g_vk.props.limits.timestampComputeAndGraphics, g_t.time_bits, g_t.time_period)) {
        LOG("arceus-temporal-gputime: not supported here (timestamps on the queue %d, %u valid bits, a period of %g "
            "ns): nothing is added\n",
            (int)g_vk.props.limits.timestampComputeAndGraphics, g_t.time_bits, g_t.time_period);
        return 0;
    }
    const VkQueryPoolCreateInfo info = {.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                                        .queryType = VK_QUERY_TYPE_TIMESTAMP,
                                        .queryCount = 2 * TEMPORAL_GPU_PAIRS};
    if (!VK_OK(vkCreateQueryPool(g_vk.dev, &info, NULL, &g_t.time_pool))) {
        g_t.time_pool = VK_NULL_HANDLE;
        LOG("arceus-temporal-gputime: the query pool could not be made: nothing is added\n");
        return 0;
    }
    g_t.gputime = 1;
    LOG("arceus-temporal-gputime: on. Two timestamps at the pipeline's last stage bracket a stretch of our "
        "commands, and their difference is the approximate time between the two completions: no exclusive time "
        "of the stretch and no bound on it. The clock has a period of %g ns and %u valid bits\n",
        g_t.time_period, g_t.time_bits);
    return 1;
}
/* A bracket's first mark: on a free pair only, whose two queries are reset in this same command buffer just
   before. The bracket's handle, -1 for none. */
static TemporalGpuHandle gputime_begin(VkCommandBuffer cb, int kind) {
    if (!gputime_on()) return -1;
    /* after a failed submission no pair is taken again: one could be reused whose reset never ran */
    if (vk_rec_submit_failed()) {
        gputime_off("a command buffer could not be begun or ended, a submission was refused or a command pool "
                    "could not be reset");
        return -1;
    }
    const TemporalGpuHandle h = temporal_gpu_begin(&g_time_ring, kind, vk_submit_number(), plat_ns());
    if (h < 0) return -1;
    const u32 first = 2 * (u32)temporal_gpu_index(h);
    rec_CmdResetQueryPool(cb, g_t.time_pool, first, 2);
    rec_CmdWriteTimestamp2(cb, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, g_t.time_pool, first);
    return h;
}
/* its second mark, recorded only while the submission is still the first mark's. The handle is then used up. */
static void gputime_end(VkCommandBuffer cb, TemporalGpuHandle* h) {
    if (*h >= 0 && gputime_on() && temporal_gpu_end(&g_time_ring, *h, vk_submit_number()))
        rec_CmdWriteTimestamp2(cb, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, g_t.time_pool,
                               2 * (u32)temporal_gpu_index(*h) + 1);
    *h = -1;
}
/* a bracket of an earlier run of our passes that never got its second mark (the passes returned between the
   two) is given up, so that its pair can leave the ring */
static void gputime_leftover(TemporalGpuHandle* h) {
    if (*h >= 0 && gputime_on()) temporal_gpu_abandon(&g_time_ring, *h);
    *h = -1;
}
/* Where the renderer has completed a slot's submissions and recycles it (cmd.c): every pair whose submission
   is known to have completed gives its time or is counted, and is free again. The completed number is read
   from the timeline as it stands and the results are asked for with their availability: nothing waits. */
void vk_temporal_gputime_poll(void) {
    if (g_t.gputime != 1) return;
    /* "Completed" is read from the timeline, and it means "ran" only while no submission has failed: the recorder
       signals a failed one's number itself. It marks the failure before that signal, so the mark is looked at
       after the timeline is read. With either wrong the measure stops and reads nothing more. */
    u64 done = 0;
    if (!vk_submit_completed(&done)) {
        gputime_off("the timeline's count of completed submissions could not be read");
        return;
    }
    if (vk_rec_submit_failed()) {
        gputime_off("a command buffer could not be begun or ended, a submission was refused or a command pool "
                    "could not be reset, so a completed submission need not have run");
        return;
    }
    const u64 now = plat_ns();
    for (int i = 0; i < TEMPORAL_GPU_PAIRS; i++) {
        const int state = g_time_ring.pair[i].state;
        /* a pair that is begun stays until its own end or until it is given up */
        if (state == TEMPORAL_GPU_FREE || state == TEMPORAL_GPU_BEGUN || done < g_time_ring.pair[i].submit) continue;
        u64 got[4] = {0, 0, 0, 0}; /* a tick and whether it is there, twice */
        unsigned long long ticks[2] = {0, 0};
        int there[2] = {0, 0}, fetched = 0;
        if (temporal_gpu_fetch(&g_time_ring, i, done)) {
            const VkResult rc =
                vkGetQueryPoolResults(g_vk.dev, g_t.time_pool, 2 * (u32)i, 2, sizeof got, got, 2 * sizeof(u64),
                                      VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
            if (rc != VK_SUCCESS && rc != VK_NOT_READY) {
                gputime_off("the results could not be asked for");
                return;
            }
            ticks[0] = got[0];
            ticks[1] = got[2];
            there[0] = got[1] != 0;
            there[1] = got[3] != 0;
            fetched = 1;
        }
        temporal_gpu_settle(&g_time_ring, i, done, now, fetched ? ticks : NULL, fetched ? there : NULL, g_t.time_bits,
                            g_t.time_period);
    }
}
/* A window's lines: under which configuration its times were taken, and for each kind of bracket how many were
   asked for, measured, still under way, and lost by each reason, with the median, the mean and the 95th
   percentile of the measured ones. Then the window is closed: the next begins empty, and brackets still under
   way are retired when they complete without giving it a time. */
static void gputime_window(const char* ended, const u32* c, u32 presents) {
    static const char* const names[TEMPORAL_GPU_KINDS] = {
        "our passes before the scene's first reader (vectors, the frame's copy, resolve, sharpen)",
        "the numbers and the view, only in the frames that have them",
        "an empty bracket, a baseline that is subtracted from nothing"};
    float sharpen;
    memcpy(&sharpen, &c[13], sizeof sharpen);
    LOG("arceus-temporal-gputime: approximate time between two completions, a window of %u presents %s. Taken "
        "with: %s, view %u, resolve options 0x%x, sharpen %.2f, object vectors %u, links %u, rejection %u, "
        "rectangle gate %u, images %ux%u, scene %ux%u at %u,%u. Ends that came late for their bracket and were "
        "ignored: %llu\n",
        presents, ended, c[0] == MODE_TAA ? "taa" : "probe", c[1], c[2], (double)sharpen, c[3], c[4], c[5], c[6],
        c[7], c[8], c[11], c[12], c[9], c[10], (unsigned long long)g_time_ring.late);
    for (int k = 0; k < TEMPORAL_GPU_KINDS; k++) {
        double median, mean, high;
        const unsigned n = g_time_ring.n[k], pending = temporal_gpu_pending(&g_time_ring, k);
        const unsigned long long* lost = g_time_ring.lost[k];
        temporal_gpu_summary(g_time_ring.ms[k], n, &median, &mean, &high);
        LOG("arceus-temporal-gputime: %s: asked %llu, measured %u, still under way and not carried over %u, split "
            "between submissions %llu, begun and not ended %llu, a result not there %llu, too old to rule out a "
            "turn of the clock %llu, longer than the host saw pass %llu, no free pair %llu. Retired from an "
            "earlier window, in no count above: %llu. Median %.4f ms, mean %.4f ms, 95th percentile %.4f ms\n",
            names[k], (unsigned long long)g_time_ring.asked[k], n, pending, lost[TEMPORAL_GPU_CUT],
            lost[TEMPORAL_GPU_PARTIAL], lost[TEMPORAL_GPU_UNAVAILABLE], lost[TEMPORAL_GPU_OLD],
            lost[TEMPORAL_GPU_ABSURD], lost[TEMPORAL_GPU_EXHAUSTED], lost[TEMPORAL_GPU_OUTDATED], median, mean, high);
    }
    temporal_gpu_restart(&g_time_ring);
}
/* Before a run of our passes records its first bracket: the configuration the run is taken under
   (temporal_gpu_configure). When it is another than the window's, the window is written and closed here, under
   its own configuration, so that this run's brackets are the new window's first and not the old one's last. */
static void gputime_configure(const Surf* scene, const TemporalRect* area) {
    u32 now[TEMPORAL_GPU_CONFIG] = {(u32)g_t.mode,
                                    (u32)g_t.view,
                                    (u32)g_t.options,
                                    (u32)g_t.objects,
                                    (u32)g_t.links,
                                    (u32)g_t.reject,
                                    (u32)g_t.rect,
                                    scene->sw,
                                    scene->sh,
                                    area ? (u32)area->x : 0,
                                    area ? (u32)area->y : 0,
                                    area ? (u32)area->w : scene->sw,
                                    area ? (u32)area->h : scene->sh,
                                    0,
                                    0,
                                    0};
    u32 before[TEMPORAL_GPU_CONFIG], presents;
    memcpy(&now[13], &g_t.sharpen, sizeof g_t.sharpen);
    if (temporal_gpu_configure(&g_t.time_window, now, g_vk_present, before, &presents))
        gputime_window("ended by a change of configuration", before, presents);
}
/* The window's clock is the count of presents. Called by the renderer right after it has raised that count,
   once a present and nowhere else, so that a window is written after 600 presents whether or not our passes
   ran in them and whichever way the present was reached (temporal_gpu_tick). Results are not read here. */
void vk_temporal_gputime_present(void) {
    u32 presents;
    if (g_t.gputime != 1) return;
    if (temporal_gpu_tick(&g_t.time_window, g_vk_present, &presents))
        gputime_window("", g_t.time_window.config, presents);
}

/* Before the draw whose fragment program is the scene colour's first reader: the probe's passes. Once a frame. */
int vk_temporal_enabled(void) { return temporal_on(); }

/* Object vectors. The world matrix of each object block that a G-buffer draw binds is kept in a table by its
   address, with the draws that bound it. The game writes many blocks to two slots in turn, one bound size apart, in
   step with the camera's block. Which slots are such a pair is learned from complete frames (temporal_math.h has
   the rules), and only an established pair, or a slot that takes no turns, gives a matrix of a frame ago. */
typedef struct {
    u64 key, partner;        /* partner: the other slot's address in the frame at, 0 for none */
    u32 at, before_at;       /* the frames world and before were taken in */
    u32 judged_at;           /* the frame turns and stays were judged for */
    u16 turns, stays;        /* temporal_pair_judge */
    u16 n, before_n;         /* the draws that bound the block in the frames at and before_at */
    u16 misses, said;        /* draws in a row without a matrix of a frame ago, and whether a line named the block */
    u64 sum, before_sum;     /* their signatures added up */
    u64 draws[OBJECT_SIGNATURES], before_draws[OBJECT_SIGNATURES];
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

/* Under the rectangle's gate: an object pass could not get a thing it needs (a program, the target, room for its
   block, a pipeline or a set). That is not a draw without a matrix, which falls back to the camera's vector by
   itself. This frame's object target is given up whole, as when a pair breaks, and object vectors are off for
   the rest of the session, with one line. The frame, its resolve and its history are not touched. */
void vk_temporal_object_failed(const char* what) {
    if (!g_t.rect || !g_t.objects) return;
    g_t.object_failed_at = g_vk_present + 1;
    g_t.objects = 0;
    LOG("arceus-temporal-rect: object vectors are off for the rest of the session (%s could not be made): the "
        "camera's vectors alone from here on\n", what);
}

/* The texture level shift for a draw into the scene (its depth target is the scene's), 0 for any other draw */
float vk_temporal_mip_bias(const Surf* zt) {
    if (!temporal_on() || !zt) return 0.0f;
    /* No value set and the rectangle's gate in use: every draw into the scene's depth of a frame drawn with
       jitter gets log2 of the scale. The frame is decided before its first such draw asks (render.c), so one
       frame has one bias. A value that was set, 0 included, is used as it always was. */
    if (g_t.rect && g_t.mode == MODE_TAA && !g_t.mip_bias_set) {
        if (!g_t.depth || zt->gva != g_t.depth || !rect_accepted(g_vk_present + 1)) return 0.0f;
        return temporal_rect_bias(0, 0.0f, &g_t.rect_gate.now.rect, g_t.rect_gate.now.target_w);
    }
    return g_t.mip_bias < 0.0f && zt == g_t.depth_rt ? g_t.mip_bias : 0.0f;
}

static void pairs_judge(void);
static void objects_log(u32 frame) {
    if (g_t.object_frame == frame) return;
    pairs_judge();
    g_t.touched_frame = frame;
    g_t.object_frame = frame;
    g_t.object_draws = 0;
    if (++g_t.object_frames % 600) return;
    LOG("arceus-temporal-objects: in 600 frames, G-buffer draws with an object block %llu. Without a matrix of a "
        "frame ago: nothing bound a frame ago %llu (never seen %llu, seen two frames ago or more %llu), pair still "
        "being learned %llu, another draw than the pair's %llu, at rest alone %llu, jumped too far %llu, table full "
        "%llu, not judged %llu, not an affine matrix %llu. With one: from its own address %llu, moved %llu, drawn "
        "again %llu, over the cap of %d a frame %llu, pipelines made %llu. Judged: pairs learned %llu, slots "
        "without turns learned %llu, pairs broken %llu (at rest alone %llu), frames whose object vectors were "
        "given up %llu. Skinned: reading a feedback buffer %llu, buffers kept %llu (a second time in a frame "
        "%llu), drawn again %llu, without last frame's buffer %llu, with a capture of another length %llu, "
        "reading past last frame's copy %llu, last frame's copy not this draw's or not known %llu, address "
        "captured more than once %llu, captures not copied %llu, captures without a slot %llu\n",
        (unsigned long long)g_t.object_seen, (unsigned long long)g_t.object_new,
        (unsigned long long)g_t.object_never, (unsigned long long)g_t.object_older,
        (unsigned long long)g_t.object_learning, (unsigned long long)g_t.object_other,
        (unsigned long long)g_t.object_rest, (unsigned long long)g_t.object_far,
        (unsigned long long)g_t.object_full, (unsigned long long)g_t.object_unjudged,
        (unsigned long long)g_t.object_odd, (unsigned long long)g_t.object_same,
        (unsigned long long)g_t.object_moved, (unsigned long long)g_t.object_drawn, OBJECT_DRAWS,
        (unsigned long long)g_t.object_capped, (unsigned long long)g_t.object_pipelines,
        (unsigned long long)g_t.pairs_learned, (unsigned long long)g_t.stays_learned,
        (unsigned long long)g_t.pairs_broken, (unsigned long long)g_t.rest_broken,
        (unsigned long long)g_t.broke_frames, (unsigned long long)g_t.object_skinned,
        (unsigned long long)g_t.skin_kept, (unsigned long long)g_t.skin_twice, (unsigned long long)g_t.skin_drawn,
        (unsigned long long)g_t.skin_first, (unsigned long long)g_t.skin_changed,
        (unsigned long long)g_t.skin_short, (unsigned long long)g_t.skin_unowned,
        (unsigned long long)g_t.skin_shared, (unsigned long long)g_t.skin_uncopied,
        (unsigned long long)g_t.skin_unkept);
    g_t.skin_kept = g_t.skin_drawn = g_t.skin_first = g_t.skin_twice = g_t.skin_changed = g_t.skin_short = 0;
    g_t.skin_unowned = g_t.skin_shared = g_t.skin_uncopied = g_t.skin_unkept = 0;
    g_t.object_seen = g_t.object_new = g_t.object_moved = g_t.object_drawn = g_t.object_capped = g_t.object_odd = 0;
    g_t.object_far = g_t.object_same = g_t.object_never = g_t.object_older = g_t.object_skinned = 0;
    g_t.object_learning = g_t.object_other = g_t.object_rest = g_t.object_full = g_t.object_unjudged = 0;
    g_t.pairs_learned = g_t.stays_learned = g_t.pairs_broken = g_t.rest_broken = g_t.broke_frames = 0;
    LOG("arceus-temporal-uncovered: in 600 frames, plain G-buffer draws without a matrix of a frame ago: rigid %llu "
        "(matrix not what the address last held %llu, address never bound before %llu), reading skinned positions "
        "%llu (%llu, %llu), block not looked at: rigid %llu, reading skinned positions %llu. Plain G-buffer draws "
        "reading skinned positions %llu, each counted once: with a matrix but not drawn again with last frame's "
        "positions %llu (the copy's refusals above, the stream's layout, the cap), and of those tried as a rigid "
        "object because the matrix moved %llu (tried, not known to be drawn)\n",
        (unsigned long long)g_t.uncovered[0][0], (unsigned long long)g_t.uncovered[0][1],
        (unsigned long long)g_t.uncovered[0][2], (unsigned long long)g_t.uncovered[1][0],
        (unsigned long long)g_t.uncovered[1][1], (unsigned long long)g_t.uncovered[1][2],
        (unsigned long long)g_t.uncovered_blind[0], (unsigned long long)g_t.uncovered_blind[1],
        (unsigned long long)g_t.skin_plain,
        (unsigned long long)g_t.skin_left, (unsigned long long)g_t.skin_rigid);
    if (g_t.links)
        LOG("arceus-temporal-links: in 600 frames, draws the pairs gave no matrix: a link gave one %llu, no link "
            "%llu, link still being learned %llu, another draw than the linked slot's at that place or past the "
            "eighth %llu, at rest alone %llu, records not complete or not of the frames before %llu. Links that "
            "reached their fourth frame %llu. Frames whose record is not complete: record full %llu, block table "
            "full or block not judged %llu, a draw before the camera's block or into another surface of the scene "
            "depth %llu (%llu such draws). Frames whose "
            "object vectors were given up for a link %llu\n",
            (unsigned long long)g_t.link_why[TEMPORAL_LINK_USED], (unsigned long long)g_t.link_why[TEMPORAL_LINK_NONE],
            (unsigned long long)g_t.link_why[TEMPORAL_LINK_LEARNING],
            (unsigned long long)g_t.link_why[TEMPORAL_LINK_OTHER], (unsigned long long)g_t.link_why[TEMPORAL_LINK_REST],
            (unsigned long long)g_t.link_why[TEMPORAL_LINK_UNKNOWN], (unsigned long long)g_t.links_made,
            (unsigned long long)g_t.link_unknown_frames[LINK_RECORD],
            (unsigned long long)g_t.link_unknown_frames[LINK_TABLE],
            (unsigned long long)g_t.link_unknown_frames[LINK_EARLY], (unsigned long long)g_t.link_early_draws,
            (unsigned long long)g_t.link_broke_frames);
    /* the draws still without a matrix with the links on, by what the links said, all and (moving) */
    for (int kind = 0; g_t.links && kind < 2; kind++) {
        const u64(*left)[2] = g_t.link_left[kind];
        LOG("arceus-temporal-links-left: in 600 frames, %s draws without a matrix: links not asked (the pairs' own "
            "refusal) %llu (%llu), a link's matrix refused as a jump %llu (%llu), records not complete %llu (%llu), "
            "no link %llu (%llu), still being learned %llu (%llu), another draw %llu (%llu), at rest alone %llu "
            "(%llu). In brackets: matrix not what the address last held\n",
            kind ? "skinned-reading" : "rigid", (unsigned long long)left[0][0], (unsigned long long)left[0][1],
            (unsigned long long)left[1][0], (unsigned long long)left[1][1], (unsigned long long)left[2][0],
            (unsigned long long)left[2][1], (unsigned long long)left[3][0], (unsigned long long)left[3][1],
            (unsigned long long)left[4][0], (unsigned long long)left[4][1], (unsigned long long)left[5][0],
            (unsigned long long)left[5][1], (unsigned long long)left[6][0], (unsigned long long)left[6][1]);
    }
    memset(g_t.link_left, 0, sizeof g_t.link_left);
    memset(g_t.link_why, 0, sizeof g_t.link_why);
    memset(g_t.link_unknown_frames, 0, sizeof g_t.link_unknown_frames);
    g_t.links_made = g_t.link_early_draws = g_t.link_broke_frames = 0;
    memset(g_t.uncovered, 0, sizeof g_t.uncovered);
    g_t.uncovered_blind[0] = g_t.uncovered_blind[1] = g_t.skin_plain = g_t.skin_left = g_t.skin_rigid = 0;
}

static int object_has_draw(const u64* draws, u32 n, u64 signature) {
    for (u32 i = 0; i < n && i < OBJECT_SIGNATURES; i++)
        if (draws[i] == signature) return 1;
    return 0;
}

/* The table's entry of the block at address with this frame's matrix and this draw in it. Several draws may bind
   one block: the first of a frame moves the matrix and the draws on. */
static Object* object_take(u64 address, u64 partner, const float world[16], u64 signature, u32 frame) {
    Object* o = object_slot(address, frame, 1);
    if (!o) {
        g_t.object_full++;
        return NULL;
    }
    if (o->at != frame) {
        memcpy(o->before, o->world, sizeof o->before);
        memcpy(o->before_draws, o->draws, sizeof o->before_draws);
        o->before_at = o->at;
        o->before_n = o->n;
        o->before_sum = o->sum;
        memcpy(o->world, world, sizeof o->world);
        o->at = frame;
        o->n = 0;
        o->sum = 0;
        o->partner = partner;
        /* an entry that does not fit the list is not judged, and what is not judged gives nothing */
        if (g_t.touched_n < OBJECT_TOUCHED) g_t.touched[g_t.touched_n++] = o;
        else g_t.object_unjudged++;
    }
    if (o->n < OBJECT_SIGNATURES) o->draws[o->n] = signature;
    if (o->n < 0xFFFF) o->n++;
    o->sum += signature;
    return o;
}

/* The end of a frame: every block bound in it is judged from what the complete frame shows (temporal_pair_judge).
   Run at the first object draw of a later frame, before anything of that frame is in the table. */
static void pairs_judge(void) {
    const u32 frame = g_t.touched_frame;
    for (u32 i = 0; i < g_t.touched_n; i++) {
        Object* o = g_t.touched[i];
        if (o->at != frame) continue;
        const Object* q = o->partner ? object_slot(o->partner, frame, 0) : NULL;
        TemporalSlotFrame f = {0};
        f.bound_before = o->before_at && o->before_at + 1 == frame;
        f.partner_now = q && q->at == frame;
        f.partner_before = q && q->at && (q->at + 1 == frame || (f.partner_now && q->before_at + 1 == frame));
        f.same_as_partner = q && q->at + 1 == frame && q->n == o->n && q->sum == o->sum;
        f.same_as_own = f.bound_before && o->before_n == o->n && o->before_sum == o->sum;
        f.at_rest_alone = q && q->at + 1 == frame &&
                          temporal_at_rest_alone(o->world, o->before_at + 2 == frame ? o->before : NULL, q->world);
        f.partner_turns = q && q->at + 1 == frame && q->judged_at == q->at ? q->turns : 0;
        f.own_stays = o->judged_at && o->judged_at + 1 == frame ? o->stays : 0;
        unsigned turns, stays;
        temporal_pair_judge(&f, &turns, &stays);
        if (turns + 1 == TEMPORAL_PAIR_FRAMES) g_t.pairs_learned++;
        if (stays + 1 == TEMPORAL_PAIR_FRAMES) g_t.stays_learned++;
        if (!turns && f.partner_turns + 1 >= TEMPORAL_PAIR_FRAMES) {
            g_t.pairs_broken++;
            if (f.at_rest_alone) g_t.rest_broken++;
        }
        o->turns = (u16)(turns < 0xFFFF ? turns : 0xFFFF);
        o->stays = (u16)(stays < 0xFFFF ? stays : 0xFFFF);
        o->judged_at = frame;
    }
    g_t.touched_n = 0;
}

/* One line for a block whose draws stay without a matrix of a frame ago: what the table holds of it, of both
   neighbouring slots, and of the blocks bound a frame ago that the same draws bound or that stood at the same place. */
static void object_missed(Object* o, u32 size, u64 signature, u32 frame, int source) {
    static u32 lines;
    if (source == TEMPORAL_BEFORE_OWN || source == TEMPORAL_BEFORE_PARTNER || source == TEMPORAL_BEFORE_LINK) {
        o->misses = 0;
        return;
    }
    if (o->misses < 0xFFFF) o->misses++;
    if (o->misses != 240 || o->said || lines >= 64) return;
    o->said = 1;
    lines++;
    char sides[400] = "", others[300] = "";
    int at = 0;
    for (int side = -1; side <= 1; side += 2) {
        const u64 key = side < 0 ? o->key - size : o->key + size;
        const Object* q = object_slot(key, frame, 0);
        const char* name = side < 0 ? "below" : "above";
        if (!q || !q->at)
            at += snprintf(sides + at, sizeof sides - (size_t)at, " %s never bound.", name);
        else
            at += snprintf(sides + at, sizeof sides - (size_t)at,
                           " %s%s bound %u and %u frames ago by %u draws (%s), turns %u stays %u, matrix %s.", name,
                           key == o->partner ? " (the partner)" : "", frame - q->at, frame - q->before_at, q->n,
                           object_has_draw(q->draws, q->n, signature) ? "this one among them" : "others", q->turns,
                           q->stays, memcmp(q->world, o->world, sizeof q->world) ? "another" : "equal");
        if (at >= (int)sizeof sides) break;
    }
    at = 0;
    for (u32 i = 0, found = 0; i < OBJECTS && found < 3 && at < (int)sizeof others; i++) {
        const Object* q = &g_objects[i];
        if (!q->key || q == o || q->at + 1 != frame) continue;
        const float dx = q->world[12] - o->world[12], dy = q->world[13] - o->world[13];
        const float dz = q->world[14] - o->world[14];
        const int same = object_has_draw(q->draws, q->n, signature);
        const int near = dx * dx + dy * dy + dz * dz < 0.0025f;
        if (!same && !near) continue;
        found++;
        at += snprintf(others + at, sizeof others - (size_t)at, " %llx (%+lld bytes, %s%s%u draws)",
                       (unsigned long long)q->key, (long long)(q->key - o->key), same ? "this draw, " : "",
                       near ? "this place, " : "", q->n);
    }
    LOG("arceus-temporal-objects: block %llx size %x stays without a matrix of a frame ago (%s): draw %llx, bound "
        "before %u frames ago by %u draws, judged %u frames ago, turns %u stays %u, matrix %s its last, origin %.2f "
        "%.2f %.2f.%s Bound a frame ago with this draw or at this place:%s\n",
        (unsigned long long)o->key, size, source == TEMPORAL_BEFORE_LEARNING ? "still being learned"
                                          : source == TEMPORAL_BEFORE_OTHER  ? "another draw" : "nothing bound",
        (unsigned long long)signature, frame - o->before_at, o->before_n, frame - o->judged_at, o->turns, o->stays,
        memcmp(o->world, o->before, sizeof o->world) ? "not" : "equal to", (double)o->world[12],
        (double)o->world[13], (double)o->world[14], sides, others[0] ? others : " none");
}

/* The matrix the draw's block had a frame ago, NULL for none (counted by reason), and in source and other where it
   came from. A draw gets one only from an established pair or from its own slot when that takes no turns. */
static const float* object_before(const Object* o, u64 signature, u32 frame, int* source, const Object** other) {
    const Object* q = o->partner ? object_slot(o->partner, frame, 0) : NULL;
    const int partner_now = q && q->at == frame;
    const int partner_last = q && q->at && (q->at + 1 == frame || (partner_now && q->before_at + 1 == frame));
    const int partner_then = q && q->at + 1 == frame; /* its entry still holds the frame before */
    *other = q;
    *source = temporal_before_choice(o->before_at && o->before_at + 1 == frame,
                                     o->judged_at && o->judged_at + 1 == frame ? o->stays : 0,
                                     object_has_draw(o->before_draws, o->before_n, signature), partner_last,
                                     partner_now, partner_then && q->judged_at == q->at ? q->turns : 0,
                                     partner_then && object_has_draw(q->draws, q->n, signature));
    g_t.current.chosen = *source;
    if (*source == TEMPORAL_BEFORE_PARTNER &&
        temporal_at_rest_alone(o->world, o->before_at + 2 == frame ? o->before : NULL, q->world)) {
        g_t.object_rest++;
        *source = TEMPORAL_BEFORE_NONE;
        return NULL;
    }
    if (*source == TEMPORAL_BEFORE_OWN) {
        g_t.object_same++;
        return o->before;
    }
    if (*source == TEMPORAL_BEFORE_PARTNER) return q->world;
    if (*source == TEMPORAL_BEFORE_LEARNING) g_t.object_learning++;
    else if (*source == TEMPORAL_BEFORE_OTHER) g_t.object_other++;
    else {
        g_t.object_new++;
        if (!o->before_at) g_t.object_never++;
        else g_t.object_older++;
    }
    return NULL;
}

/* The links (temporal_math.h). Four records in turn, one a frame: the one being filled, the two before it that a
   draw reads and are not changed any more, and one to spare. */
static TemporalFrame g_link_frames[4];
static TemporalLinkScratch g_link_scratch;

static TemporalFrame* link_record(u32 frame) {
    TemporalFrame* f = &g_link_frames[frame & 3];
    return frame && f->frame == frame ? f : NULL;
}

/* judges the frame's record as it now is against the two before it */
static void links_judge(u32 frame) {
    TemporalFrame* now = link_record(frame);
    if (now) temporal_links_judge(now, link_record(frame - 1), link_record(frame - 2), &g_link_scratch);
}

/* the first draw of a frame closes the record of the frame before it, complete now, and opens its own */
static void links_roll(u32 frame) {
    if (!g_t.links || g_t.link_frame == frame) return;
    if (g_t.link_frame) {
        const TemporalFrame* last = link_record(g_t.link_frame);
        links_judge(g_t.link_frame);
        for (u32 i = 0; last && i < last->n; i++) g_t.links_made += last->blocks[i].turns + 1 == TEMPORAL_PAIR_FRAMES;
    }
    temporal_frame_begin(&g_link_frames[frame & 3], frame);
    g_t.link_frame = frame;
}

/* something of the frame was not seen: its record proves nothing, counted once a frame by cause */
static void links_unknown(u32 frame, int cause) {
    if (!g_t.links) return;
    links_roll(frame);
    TemporalFrame* now = link_record(frame);
    if (now) now->complete = 0;
    if (g_t.link_unknown_at[cause] == frame) return;
    g_t.link_unknown_at[cause] = frame;
    g_t.link_unknown_frames[cause]++;
}

/* A block bound by a draw goes into the frame's record, which is there whichever way the draw came. Without room
   the frame is not complete. Off, nothing. */
static TemporalBlock* link_bind(u32 frame, u64 address, u32 size, const float world[16], int affine, u64 signature) {
    if (!g_t.links) return NULL;
    links_roll(frame);
    TemporalBlock* b = temporal_frame_bind(link_record(frame), address, size, world, affine, signature);
    if (!b) links_unknown(frame, LINK_RECORD);
    return b;
}

/* The draw vk_temporal_object_note is at goes into the frame's record. Where the pairs know nothing of its block,
   a link may give the matrix of a frame ago: from the record of the frame before, which nothing changes. */
static void link_try(u32 frame, u64 address, u32 size, u64 signature) {
    if (!g_t.links) return;
    links_roll(frame);
    TemporalFrame* now = link_record(frame);
    const int chosen = g_t.current.chosen;
    const int served =
        chosen == TEMPORAL_BEFORE_OWN || chosen == TEMPORAL_BEFORE_PARTNER || chosen == TEMPORAL_BEFORE_OTHER;
    if (!now) return;
    const TemporalBlock* mine = temporal_frame_find(now, address);
    const TemporalBlock* q = NULL;
    int why = TEMPORAL_LINK_NONE;
    if (!served)
        q = temporal_link_before(now, link_record(frame - 1), link_record(frame - 2), address, size,
                                 mine ? mine->count : 0, signature, g_t.current.world, &why);
    TemporalBlock* b = link_bind(frame, address, size, g_t.current.world, 1, signature);
    if (!b) {
        /* no room in the record: the links were asked and nothing is known */
        if (!served) g_t.current.link_why = TEMPORAL_LINK_UNKNOWN;
        return;
    }
    if (served) {
        b->served = 1;
        return;
    }
    g_t.link_why[why]++;
    g_t.current.link_why = why;
    if (!q) return;
    g_t.current.was = q->world;
    g_t.current.source = TEMPORAL_BEFORE_LINK;
    g_t.current.link = q->address;
    g_t.current.pair = address < q->address ? address : q->address;
    g_t.current.pair_other = address < q->address ? q->address : address;
    g_t.current.other = NULL;
}

/* After the G-buffer, before the vectors are read: 1 when every pair this frame's extra draws relied on still
   holds now that the frame's draws are known. The other slot was not bound too, and both ends have the same draws.
   Else the frame's object vectors are given up and the camera's stand. */
static int pairs_hold(u32 frame) {
    int judged = 0;
    for (u32 d = 1; d <= g_t.object_draws && d <= OBJECT_DRAWS; d++) {
        const struct Drawn* w = &g_t.drawn[d];
        const Object *o = w->slot, *q = w->other;
        int broke = 0;
        if (w->source == TEMPORAL_BEFORE_LINK) {
            /* the links are judged again on the frame's G-buffer as it now is: the draw's must still stand */
            const TemporalFrame* now = link_record(frame);
            if (!judged++) links_judge(frame);
            broke = !now || !temporal_link_holds(now, w->block, w->link);
            if (broke && g_t.link_logged_at != frame) {
                g_t.link_logged_at = frame;
                g_t.link_broke_frames++;
            }
        } else if (w->source == TEMPORAL_BEFORE_PARTNER)
            broke = !o || !q || q->at + 1 != frame || o->n != q->n || o->sum != q->sum;
        else if (w->source == TEMPORAL_BEFORE_OWN)
            broke = !o || o->before_at + 1 != frame || o->n != o->before_n || o->sum != o->before_sum;
        /* a skinned draw took last frame's copy, and its address was captured again later in this frame */
        const int again = w->stream && temporal_stream_conflict(w->stream, frame);
        if (!broke && !again) continue;
        g_t.broke_frames++;
        if (g_t.broke_logged_at + 60 <= frame) { /* one example a second */
            g_t.broke_logged_at = frame;
            LOG("arceus-temporal-objects: the pair of block %llx broke after its draw (%s), the frame's object "
                "vectors are given up, %llu frames so far\n",
                (unsigned long long)w->block,
                again                              ? "its positions' address was captured again"
                : w->source == TEMPORAL_BEFORE_LINK ? "its link did not hold"
                : w->source == TEMPORAL_BEFORE_OWN  ? "its own draws changed"
                : q && q->at == frame            ? "both slots bound"
                                                 : "the draws changed",
                (unsigned long long)g_t.broke_frames);
        }
        return 0;
    }
    return 1;
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
    d->slot = g_t.current.slot;
    d->other = g_t.current.other;
    d->stream = NULL;
    d->block = block;
    d->feedback = feedback;
    d->signature = g_t.current.signature;
    d->link = source == TEMPORAL_BEFORE_LINK ? g_t.current.link : 0;
    d->skinned = (u8)skinned;
    d->source = (u8)source;
    /* the scene's size on screen: the target's, or under the rectangle's gate the viewport's */
    const double scene_w = g_t.rect ? fabs((double)g_t.viewport.width) : (double)zt->sw;
    const double scene_h = g_t.rect ? fabs((double)g_t.viewport.height) : (double)zt->sh;
    d->shift = (float)temporal_origin_shift(g_t.now, world, g_t.before, was, scene_w, scene_h);
    const float camera = (float)temporal_origin_shift(g_t.now, world, g_t.before, world, scene_w, scene_h);
    d->far = d->shift > 100.0f && camera >= 0.0f && camera < 20.0f;
    if (d->far && g_t.long_logged_at + 60 <= frame) {
        g_t.long_logged_at = frame;
        LOG("arceus-temporal-objects: long move of %.0f px (the camera alone %.0f px) at block %llx (%s, before from "
            "%s): now %.2f %.2f %.2f, a frame ago %.2f %.2f %.2f\n",
            (double)d->shift, (double)camera, (unsigned long long)block, skinned ? "skinned" : "rigid",
            source == TEMPORAL_BEFORE_OWN    ? "its own address"
            : source == TEMPORAL_BEFORE_LINK ? "its linked slot"
                                             : "the other slot",
            (double)world[12],
            (double)world[13], (double)world[14], (double)was[12], (double)was[13], (double)was[14]);
    }
    return number;
}

/* Every plain G-buffer draw is shown here once, before its extra draw is tried: its block's matrix and the draw's
   signature (vertex and index buffer addresses, index count, first index, vertex program) go into the table.
   1 when the block has a matrix of a frame ago. vk_temporal_skin and vk_temporal_object then work on this draw. */
int vk_temporal_object_note(const GpuChannel* ch, const Surf* zt, u64 vertex, u64 index, u32 count, u32 first,
                            u64 program) {
    const u32 frame = g_vk_present + 1;
    const u64 address = ch->cbind[ST_VS][OBJECT_BANK];
    const u32 size = ch->cbind_size[ST_VS][OBJECT_BANK];
    g_t.current.was = NULL;
    g_t.current.slot = g_t.current.other = NULL;
    g_t.current.address = 0;
    g_t.current.link = 0;
    g_t.current.link_why = -1;
    if (!g_t.objects || !zt) return 0;
    const u64 signature = temporal_draw_signature(vertex, index, count, first, program);
    /* The links' record must see every draw with an object block into the scene depth, by its address or by the
       surface the pairs go by (temporal_scene_gate). Where the two do not agree, or the camera's block is not
       known yet, the draw is noted as not seen. A block of a size the pairs leave alone is there, not usable. */
    const int seen = temporal_scene_gate(g_t.links, address != 0, g_t.depth && zt->gva == g_t.depth,
                                         zt == g_t.depth_rt, g_t.camera_at == frame);
    if (seen == TEMPORAL_SCENE_UNSEEN) {
        g_t.link_early_draws++;
        links_unknown(frame, LINK_EARLY);
    } else if (seen == TEMPORAL_SCENE_SEEN && (size < 64 || size > 0x10000)) {
        static const float none[16];
        link_bind(frame, address, size, none, 0, signature);
    }
    if (zt != g_t.depth_rt || !address || size < 64 || size > 0x10000 || g_t.camera_at != frame) return 0;
    objects_log(frame);
    g_t.object_seen++;
    block_read(address, g_t.current.world);
    const float* world = g_t.current.world;
    if (world[3] != 0.0f || world[7] != 0.0f || world[11] != 0.0f || world[15] != 1.0f) {
        g_t.object_odd++;
        /* such a block is in the record all the same: it counts among the blocks with the same draws */
        link_bind(frame, address, size, world, 0, signature);
        return 0;
    }
    /* the slot this block would have had a frame ago: the camera's block went the same way */
    u64 partner = 0;
    if (g_t.camera_address[0] && g_t.camera_address[1])
        partner = g_t.camera_address[0] > g_t.camera_address[1] ? address - size : address + size;
    const u64 unjudged = g_t.object_unjudged;
    Object* o = object_take(address, partner, world, signature, frame);
    /* a block the table could not take or will not judge: what the pairs make of it is not known */
    if (g_t.links && (!o || g_t.object_unjudged != unjudged)) links_unknown(frame, LINK_TABLE);
    if (!o) {
        link_bind(frame, address, size, world, 1, signature);
        return 0;
    }
    const Object* other = NULL;
    g_t.current.address = address;
    g_t.current.slot = o;
    g_t.current.was = object_before(o, signature, frame, &g_t.current.source, &other);
    g_t.current.signature = signature;
    g_t.current.pair = g_t.current.source == TEMPORAL_BEFORE_PARTNER && partner < address ? partner : address;
    g_t.current.pair_other = 0;
    g_t.current.other = (void*)other;
    link_try(frame, address, size, signature);
    object_missed(o, size, signature, frame, g_t.current.source);
    if (g_t.current.was && object_jumped(address, world, g_t.current.was, frame)) g_t.current.was = NULL;
    return g_t.current.was != NULL;
}

/* For the numbers: what became of the plain G-buffer draw just shown to vk_temporal_object_note. known is its
   answer, reads 1 when the draw reads skinned positions, again 1 when it was drawn again with last frame's. Called
   before the rigid try, which may still draw it again by its matrix alone (the try is counted there). */
void vk_temporal_object_fate(int known, int reads, int again) {
    g_t.current.unskinned = 0;
    if (!g_t.objects || g_t.object_frame != g_vk_present + 1) return;
    g_t.current.unskinned = known && reads && !again;
    const Object* o = g_t.current.slot;
    g_t.skin_plain += reads != 0;
    if (known) {
        g_t.skin_left += reads && !again;
        return;
    }
    if (!o) {
        g_t.uncovered_blind[reads != 0]++;
        return;
    }
    u64* count = g_t.uncovered[reads != 0];
    const int moving = o->before_at && memcmp(o->world, o->before, sizeof o->world);
    count[0]++;
    if (!o->before_at) count[2]++;
    else if (moving) count[1]++;
    if (g_t.links) {
        u64* left = g_t.link_left[reads != 0][g_t.current.link_why + 1];
        left[0]++;
        left[1] += moving != 0;
    }
}

int vk_temporal_object(const GpuChannel* ch, const Surf* zt, VkTemporalObject* out) {
    const u32 frame = g_vk_present + 1;
    const u64 address = g_t.current.address;
    const float *world = g_t.current.world, *was_world = g_t.current.was;
    const int source = g_t.current.source;
    (void)ch;
    if (!address || !was_world || !memcmp(was_world, world, sizeof g_t.current.world) || !g_t.have_before) return 0;
    g_t.object_moved++;
    g_t.skin_rigid += g_t.current.unskinned != 0;
    if (g_t.object_draws >= OBJECT_DRAWS) {
        g_t.object_capped++;
        return 0;
    }
    /* this frame's clip as drawn -> without jitter -> world -> the object -> its world before -> the clip before,
       composed in double (temporal_object_matrices) */
    double jx, jy;
    frame_jitter(frame, &jx, &jy);
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
            vk_temporal_object_failed("the object pass's program");
            return 0;
        }
    }
    if (!image_ensure(&g_t.object, OBJECT_FORMAT, zt->sw, zt->sh,
                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, "object vector")) {
        vk_temporal_object_failed("the object vectors' target");
        return 0;
    }
    u8* bytes = vk_stage(sizeof block, vk_stage_align(), &out->block, &out->offset);
    if (!bytes) {
        vk_temporal_object_failed("room for an object draw's block");
        return 0;
    }
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
    TemporalStreamCopy copy[2];   /* whose each copy is (temporal_math.h) */
    u32 generation;               /* of the address: every capture into it takes a new one */
    VkBuffer buffer[2];
    VmaAllocation memory[2];
} Skin;
static Skin g_skins[SKINS];

/* The slot of an address among its eight candidates (temporal_slot_pick): a slot's age is its newest capture's,
   copied or not, so one a draw of this frame relied on is never given away. NULL: the address has no record. */
static Skin* skin_slot(u64 address, u32 frame, int make) {
    unsigned long long addresses[8];
    unsigned newest[8];
    Skin* slots[8];
    for (u32 i = 0; i < 8; i++) {
        slots[i] = &g_skins[((address >> 2) * 0x9E3779B1u + i) % SKINS];
        addresses[i] = slots[i]->address;
        newest[i] = temporal_stream_newest(slots[i]->copy);
    }
    const int pick = temporal_slot_pick(addresses, newest, 8, address, frame, make);
    if (pick < 0) return NULL;
    Skin* s = slots[pick];
    if (s->address == address) return s;
    for (int k = 0; k < 2; k++)
        if (s->buffer[k]) vk_defer_buffer(s->buffer[k], s->memory[k]);
    memset(s, 0, sizeof *s);
    s->address = address;
    return s;
}

/* After a transform feedback draw, outside a pass: output 0 (size bytes at address, in source at offset) is copied
   to this frame's buffer of its address. */
void vk_temporal_skin_keep(VkCommandBuffer cb, u64 address, u32 size, VkBuffer source, u64 offset) {
    const u32 frame = g_vk_present + 1;
    if (!g_t.objects || !address) return;
    Skin* s = skin_slot(address, frame, 1);
    if (!s) { /* no slot to be had: the address has no record, so nothing can take its content for known */
        g_t.skin_unkept++;
        return;
    }
    const int turn = (int)(frame & 1);
    if (s->copy[turn].at == frame) g_t.skin_twice++; /* a second capture to this address in one frame */
    /* The capture is counted before anything below can fail: from here the copy of this turn is not known to hold
       this capture until its bytes are copied. */
    temporal_stream_captured(&s->copy[turn], &s->generation, frame);
    if (!size || size > SKIN_BYTES || !source) {
        g_t.skin_uncopied++;
        return;
    }
    if (size > s->capacity) {
        VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                  .size = size,
                                  .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                  .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
        VmaAllocationCreateInfo bai = {.usage = VMA_MEMORY_USAGE_AUTO};
        /* both new buffers first: when one cannot be made the slot keeps what it had */
        VkBuffer made[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
        VmaAllocation made_memory[2] = {NULL, NULL};
        for (int k = 0; k < 2; k++)
            if (!VK_OK(vmaCreateBuffer(g_vk.vma, &bci, &bai, &made[k], &made_memory[k], NULL)))
                made[k] = VK_NULL_HANDLE;
        if (!made[0] || !made[1]) {
            for (int k = 0; k < 2; k++)
                if (made[k]) vmaDestroyBuffer(g_vk.vma, made[k], made_memory[k]);
            g_t.skin_uncopied++;
            /* memory that could not be had is not a capture too large or without a slot: under the rectangle's
               gate it ends the object vectors (the capture stays counted, the slot keeps what it had) */
            vk_temporal_object_failed("memory for a skinned character's positions");
            return;
        }
        for (int k = 0; k < 2; k++) {
            if (s->buffer[k]) vk_defer_buffer(s->buffer[k], s->memory[k]);
            s->buffer[k] = made[k];
            s->memory[k] = made_memory[k];
            s->at[k] = 0;
            temporal_stream_poison(&s->copy[k]); /* the bytes go with the buffer, the counts stay */
        }
        s->capacity = size;
    }
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
    temporal_stream_copied(&s->copy[turn]);
    g_t.skin_kept++;
}

/* A G-buffer draw whose position stream starts offset bytes into the transform feedback output at address: 1 with
   what its vector draw needs when last frame's copy of that output is there. The draw is then made with our vertex
   and fragment programs: the position now from the game's binding, the position before from out->before. */
int vk_temporal_skin(const GpuChannel* ch, Surf* zt, u64 address, u64 offset, u64 last_vertex,
                     VkTemporalSkin* out) {
    const u32 frame = g_vk_present + 1;
    const u64 block_at = g_t.current.address;
    const float *world = g_t.current.world, *was = g_t.current.was;
    const int source = g_t.current.source;
    (void)ch;
    /* the copy of a frame ago is the same character's only as far as its block's pair is known */
    if (!block_at || !was || !g_t.have_before) return 0;
    Skin* s = skin_slot(address, frame, 0);
    const int last = (int)((frame - 1) & 1);
    /* this draw reads this frame's capture: the copy is tagged as its own for the frame after */
    if (s && s->capacity)
        temporal_stream_used(&s->copy[!last], frame, g_t.current.pair, g_t.current.pair_other,
                             g_t.current.signature);
    if (!s || !s->capacity || s->at[last] + 1 != frame || offset >= s->size[last]) {
        g_t.skin_first++;
        return 0;
    }
    /* another mesh may have taken the address: this frame's capture must be there and as long as last frame's */
    if (s->at[!last] != frame || s->size[!last] != s->size[last]) {
        g_t.skin_changed++;
        return 0;
    }
    /* last frame's copy must be this draw's own, and the address captured once in each frame */
    {
        const int own =
            temporal_stream_before(&s->copy[!last], &s->copy[last], frame, g_t.current.pair, g_t.current.pair_other,
                                   g_t.current.signature);
        if (own <= 0) {
            if (own < 0) g_t.skin_shared++;
            else g_t.skin_unowned++;
            return 0;
        }
    }
    /* every vertex the draw reads must be in last frame's copy, 12 bytes each from offset on */
    if (last_vertex == ~0ull || (last_vertex + 1) * 12 > (u64)s->size[last] - offset) {
        g_t.skin_short++;
        return 0;
    }
    if (g_t.object_draws >= OBJECT_DRAWS) {
        g_t.object_capped++;
        return 0;
    }
    double jx, jy;
    frame_jitter(frame, &jx, &jy);
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
    g_t.drawn[number <= OBJECT_DRAWS ? number : 0].stream = &s->copy[!last];
    block.size[2] = g_t.mode == MODE_PROBE ? (float)number : 0.0f;
    block.size[3] = 0;
    if (!(block.viewport[2] != 0.0f) || !(block.viewport[3] != 0.0f)) return 0;
    if (!layouts_make()) {
        vk_temporal_object_failed("the passes' layouts");
        return 0;
    }
    if (!g_t.skin_vs) g_t.skin_vs = vk_module_spv(temporal_skin_vs_spv, (size_t)(temporal_skin_vs_spv_size / 4));
    if (!g_t.skin_fs) g_t.skin_fs = vk_module_spv(temporal_skin_fs_spv, (size_t)(temporal_skin_fs_spv_size / 4));
    if (!g_t.skin_vs || !g_t.skin_fs) {
        vk_temporal_object_failed("the skinned pass's programs");
        return 0;
    }
    if (!image_ensure(&g_t.object, OBJECT_FORMAT, zt->sw, zt->sh,
                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, "object vector")) {
        vk_temporal_object_failed("the object vectors' target");
        return 0;
    }
    const VkComponentMapping identity = {0};
    out->depth = vk_tex_view(zt, VK_IMAGE_VIEW_TYPE_2D, 0, &identity, 0);
    if (!out->depth) {
        vk_temporal_object_failed("the scene depth's view for a skinned draw");
        return 0;
    }
    u8* bytes = vk_stage(sizeof block, vk_stage_align(), &out->block, &out->offset);
    if (!bytes) {
        vk_temporal_object_failed("room for a skinned draw's block");
        return 0;
    }
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
        /* after last frame's reads by the motion pass, and after its writes when that pass did not read the
           target (a frame whose object vectors were given up) */
        vk_barrier(cb, g_t.object.img, VK_IMAGE_ASPECT_COLOR_BIT,
                   VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
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
    /* our passes are reached in this frame, whatever comes of them (the rectangle's gate counts the frames drawn
       with jitter in which they are not) */
    if (g_t.rect) temporal_rect_hooked(&g_t.rect_gate, frame);
    Surf *scene = g_t.scene_rt, *depth = g_t.depth_rt;
    if (g_t.camera_at != frame || g_t.written_at != frame || !scene || !depth || !g_t.have_before ||
        scene->layers != 1 || scene->group || depth->group || scene->sw != depth->sw || scene->sh != depth->sh) {
        if (g_t.skipped++ < 4 || g_t.skipped % 1800 == 0)
            LOG("arceus-temporal: skipped (camera %d, scene written %d, previous matrix %d), %llu so far\n",
                g_t.camera_at == frame, g_t.written_at == frame, g_t.have_before, (unsigned long long)g_t.skipped);
        rect_fail(frame, "the scene, its camera or the camera of the frame before is not known");
        return;
    }
    g_t.ran_at = frame;
    /* Under the rectangle's gate nothing of ours runs unless the frame was accepted at its first draw and
       confirmed by the scene block since, with the G-buffer's viewport the one decided on: no vectors, no
       resolve, no sharpen, no numbers, no view, and no history for the frame after. */
    const TemporalRectGate* rg = &g_t.rect_gate;
    const TemporalViewport hook_view = {g_t.viewport.x, g_t.viewport.y, g_t.viewport.width, g_t.viewport.height};
    const int rect_ok = g_t.rect && temporal_rect_steady(rg, frame) && rg->now.target_w == (int)scene->sw &&
                        rg->now.target_h == (int)scene->sh && temporal_viewport_same(&hook_view, &rg->now.viewport);
    if (g_t.rect && !rect_ok) {
        if (rect_line(++g_t.rect_left))
            LOG("arceus-temporal-rect: frame left as drawn (%s), %llu so far\n",
                rg->decided_at != frame                 ? "not decided"
                : !rg->accepted                         ? "gated at its first draw"
                : rg->state == TEMPORAL_FRAME_DECIDED   ? "no draw confirmed it before our passes"
                : rg->state == TEMPORAL_FRAME_SEEN      ? "confirmed with a new fraction"
                : rg->state == TEMPORAL_FRAME_NONE      ? "found wrong after its decision"
                                                        : "the G-buffer's viewport or the target is another",
                (unsigned long long)g_t.rect_left);
        /* A confirmed frame refused here for its viewport or target was drawn with jitter and is not resolved.
           That is a disagreement found late: the start begins again, and only a row of such frames stops the
           gate (temporal_rect_refused). */
        if (temporal_rect_steady(rg, frame)) temporal_rect_refused(&g_t.rect_gate, frame);
        return;
    }
    const TemporalRect* area = rect_ok ? &rg->now.rect : NULL;
    if (!layouts_make()) {
        rect_fail(frame, "the passes' layouts");
        return;
    }
    if (!g_t.motion_pipe)
        g_t.motion_pipe = pipeline_make(MOTION_FORMAT, temporal_motion_fs_spv, temporal_motion_fs_spv_size, "motion");
    const int probe_view = g_t.mode == MODE_PROBE && g_t.view;
    if (probe_view && (!g_t.view_pipe || g_t.view_format != scene->f.vk)) {
        g_t.view_pipe = pipeline_make(scene->f.vk, temporal_view_fs_spv, temporal_view_fs_spv_size, "view");
        g_t.view_format = scene->f.vk;
    }
    const VkImageUsageFlags target = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    const VkImageUsageFlags copy = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    /* under the rectangle's gate a view that cannot be made is off for the session, and the rest goes on */
    if (g_t.rect && probe_view && !g_t.view_pipe) rect_extras_off("the view's pipeline");
    if (!g_t.motion_pipe || (!g_t.rect && probe_view && !g_t.view_pipe) ||
        !image_ensure(&g_t.motion, MOTION_FORMAT, scene->sw, scene->sh, target, "motion")) {
        rect_fail(frame, "the vector pass's pipeline or image");
        return;
    }
    if (!g_t.stats_pipe)
        g_t.stats_pipe = pipeline_make(STATS_FORMAT, temporal_stats_fs_spv, temporal_stats_fs_spv_size, "numbers");
    if (!image_ensure(&g_t.colour[0], scene->f.vk, scene->sw, scene->sh, copy, "colour") ||
        !image_ensure(&g_t.colour[1], scene->f.vk, scene->sw, scene->sh, copy, "colour")) {
        rect_fail(frame, "the frame's copies");
        return;
    }
    const VkComponentMapping identity = {0};
    const VkImageView depth_view = vk_tex_view(depth, VK_IMAGE_VIEW_TYPE_2D, 0, &identity, 0);
    if (!depth_view) {
        rect_fail(frame, "the depth's view");
        return;
    }

    Constants pc = {.viewport = {g_t.viewport.x, g_t.viewport.y, g_t.viewport.width, g_t.viewport.height},
                    .depth = {g_t.depth_translate, g_t.depth_scale, 0, 0},
                    .size = {(float)scene->sw, (float)scene->sh, (float)g_t.view, 40.0f}};
    vk_temporal_multiply(g_t.inverse, g_t.before, pc.reproject);
    {
        double jx, jy;
        frame_jitter(frame, &jx, &jy);
        pc.jitter[0] = (float)jx;
        pc.jitter[1] = (float)jy;
        pc.jitter[2] = rect_ok ? 1.0f : 0.0f; /* the passes keep to the viewport's rectangle */
    }
    if (!(pc.depth[1] != 0.0f) || !(pc.viewport[2] != 0.0f) || !(pc.viewport[3] != 0.0f)) {
        rect_fail(frame, "a viewport or depth range without a size");
        return;
    }
    /* Under the rectangle's gate, what this frame's pictures are made from: the images, the sizes, the rectangle
       and the fraction. The frame's copy and the history are those of the frame before only if theirs is the
       same (temporal_rect_stamp_same). */
    TemporalRectStamp stamp = {0};
    if (rect_ok)
        stamp = (TemporalRectStamp){scene->gva,     rect_making(scene), (int)scene->sw, (int)scene->sh,
                                    (int)scene->sw, (int)scene->sh,     rg->now.rect,   rg->now.fx,
                                    rg->now.fy};

    vk_render_end();
    VkCommandBuffer cb = vk_cmd();
    /* with SNX_ARCEUS_TEMPORAL_GPUTIME: the first mark of the bracket around our passes here, -1 without */
    gputime_leftover(&g_t.time_hook);
    gputime_leftover(&g_t.time_numbers);
    if (gputime_on()) gputime_configure(scene, area);
    g_t.time_hook = gputime_begin(cb, TEMPORAL_GPU_HOOK);
    /* objects that moved were drawn into their own target this frame: the vector pass takes their vectors from it */
    const int objects = g_t.objects && g_t.object_at == frame && g_t.object.view && g_t.object.w == scene->sw &&
                        g_t.object.h == scene->sh && (g_t.rect || g_t.gate.mismatch_at != frame) &&
                        g_t.object_failed_at != frame && pairs_hold(frame);
    /* the vectors: depth sampled, the motion image written */
    vk_surf_use(cb, depth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    vk_barrier(cb, g_t.motion.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
               VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
               VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    {
        Constants mc = pc;
        /* the bound: an eighth of the scene's height, which under the rectangle's gate is the rectangle's */
        mc.depth[2] = area ? (float)temporal_rect_reject_bound(g_t.reject, area)
                           : (float)temporal_reject_bound(g_t.reject, (double)scene->sh);
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
        if (!set) {
            rect_fail(frame, "the vector pass's set");
            return;
        }
        fullscreen(cb, g_t.motion.view, g_t.motion.w, g_t.motion.h, g_t.motion_pipe, set, &mc, area);
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
    /* the copy of the frame before, for the numbers and the error view: under the rectangle's gate only one made
       from the same as this frame's */
    const int same_making = g_t.rect_raw_at + 1 == frame && temporal_rect_stamp_same(&g_t.rect_raw, &stamp);
    const int history = g_t.history_at + 1 == frame && (!g_t.rect || same_making);
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
        g_t.rect_raw = stamp;
        g_t.rect_raw_at = frame;
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
    /* A frame without jitter (jitter_decide) or whose G-buffer viewport is not the one decided on is left as drawn:
       no resolve, no sharpen, no numbers, and no history for the frame after it. */
    const int steady = g_t.rect ? rect_ok : temporal_gate_steady(&g_t.gate, frame);
    if (taa && !steady) {
        if (g_t.unsteady++ < 4 || g_t.unsteady % 600 == 0)
            LOG("arceus-temporal: frame left as drawn (the viewport), %llu so far\n",
                (unsigned long long)g_t.unsteady);
        numbers = 0;
    } else if (taa && g_t.view != 4) {
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
            !image_ensure(&g_t.history[1], HISTORY_FORMAT, scene->sw, scene->sh, target, "history")) {
            rect_fail(frame, "the resolve's pipeline or the history's images");
            return;
        }
        /* history images made anew are another making */
        if (rect_ok) stamp.generation = rect_making(scene);
        Image* kept = &g_t.history[g_t.history_newest];
        Image* made = &g_t.history[!g_t.history_newest];
        /* a cut: the screen's middle at a depth of ten moved by more than a quarter of the screen */
        const float* m = pc.reproject;
        const float w = 0.01f * m[11] + m[15], x = 0.01f * m[8] + m[12], y = 0.01f * m[9] + m[13];
        const int cut = !(w > 0.0f) || fabsf(x) > 0.5f * w || fabsf(y) > 0.5f * w;
        /* under the rectangle's gate the history is the frame before's only with the same stamp */
        const int had = g_t.rect ? temporal_rect_history(rg, frame, &stamp) : g_t.resolved_at + 1 == frame;
        const int usable = had && !cut;
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
        if (!set) {
            rect_fail(frame, "the resolve's set");
            return;
        }
        vk_barrier(cb, made->img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
                   VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        vk_surf_use(cb, scene, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        fullscreen2(cb, scene->att, made->view, scene->sw, scene->sh, g_t.resolve_pipe, set, &rc, area);
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
            if (!sharpen_set && g_t.rect) {
                /* a sharpen that was asked for and cannot be made is off for the session, said once: the
                   resolved picture and its history, written before it, stand */
                LOG("arceus-temporal-rect: the sharpen is off for the rest of the session (its %s could not be "
                    "made)\n", g_t.sharpen_pipe ? "set" : "pipeline");
                g_t.sharpen = 0.0f;
            }
            if (sharpen_set) {
                Constants sc = pc;
                sc.size[3] = g_t.sharpen;
                /* a second write of the scene colour after the resolve's: ordered behind it */
                vk_surf_use(cb, scene, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
                fullscreen(cb, scene->att, scene->sw, scene->sh, g_t.sharpen_pipe, sharpen_set, &sc, area);
                sharpened = 1;
            }
        }
        vk_surf_wrote(scene);
        vk_rt_drawn(scene);
        g_t.history_newest = !g_t.history_newest;
        g_t.resolved_at = frame;
        if (rect_ok) temporal_rect_wrote(&g_t.rect_gate, frame, &stamp);
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

    /* the bracket around our passes ends here, and an empty one follows it as a baseline */
    gputime_end(cb, &g_t.time_hook);
    TemporalGpuHandle empty = gputime_begin(cb, TEMPORAL_GPU_EMPTY);
    gputime_end(cb, &empty);
    /* without a frame before (the first one, or after a gap) the frame is compared with itself */
    /* the probe's numbers also look at the pixels that took an object's vector */
    if (!taa && objects) {
        shown_before = g_t.object.view;
        pc.depth[3] = 1.0f;
    }
    /* A frame with a long move line has its numbers taken too, when no regular run is within STATS_SPACING runs: the
       buffer is read one run of the numbers later and the copy must be finished by then. */
    const u64 phase = g_t.runs % STATS_EVERY;
    const int moved_far = !taa && objects && g_t.long_logged_at == g_vk_present + 1 && phase >= STATS_SPACING &&
                          phase + STATS_SPACING <= STATS_EVERY;
    int stats_due = numbers && !g_t.rect_extras_off && (phase == 0 || moved_far);
    /* the picture as shown, for its gradient: the resolve's, or the scene colour itself after a sharpen */
    VkImageView final_view = numbers_before;
    if (sharpened && stats_due) {
        const VkImageView scene_view = vk_tex_view(scene, VK_IMAGE_VIEW_TYPE_2D, 0, &identity, 0);
        if (scene_view) {
            vk_surf_use(cb, scene, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
            final_view = scene_view;
        } else if (g_t.rect) {
            /* the sharpened picture cannot be read: no numbers of another picture in its place */
            rect_extras_off("the sharpened picture's view");
            stats_due = 0;
        }
    }
    const VkImageView views[SETS_BINDINGS] = {
        g_t.motion.view, now->view, numbers_before, depth_view, last_frame,
        shown_before ? shown_before : numbers_before, final_view};
    const VkDescriptorSet set = set_make(views, samplers);
    if (!set) {
        rect_extras_off("their set");
        return;
    }

    /* the numbers: every STATS_EVERY runs the small image is drawn and copied to memory, and the copy made that
       many runs ago, long finished, is read and logged */
    const int stats_made = stats_due && g_t.stats_pipe && stats_ready() &&
                           image_ensure(&g_t.stats, STATS_FORMAT, 2 * STATS_W, STATS_H,
                                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                        "numbers");
    if (stats_due && !stats_made) rect_extras_off("the numbers' pipeline, buffer or image");
    /* The memory may not be coherent: what the GPU wrote is fetched before it is read. A fetch that fails logs
       nothing. Under the rectangle's gate it also ends the numbers, and none are drawn or kept in this frame. */
    int stats_write = stats_made;
    if (stats_made && g_t.stats_run) {
        if (VK_OK(vmaInvalidateAllocation(g_vk.vma, g_t.stats_memory, 0, VK_WHOLE_SIZE))) stats_log();
        else if (g_t.rect) {
            rect_extras_off("the read back of the numbers");
            stats_write = 0;
        }
    }
    /* the numbers and the view, where a frame has them, in a bracket of their own */
    const int view_due = !taa && g_t.view && !(g_t.rect && g_t.view == 2 && !history);
    g_t.time_numbers = stats_write || view_due ? gputime_begin(cb, TEMPORAL_GPU_NUMBERS) : -1;
    if (stats_write) {
        const VkBufferImageCopy out = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                       .imageExtent = {2 * STATS_W, STATS_H, 1}};
        vk_barrier(cb, g_t.stats.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_COPY_BIT, 0,
                   VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        fullscreen(cb, g_t.stats.view, 2 * STATS_W, STATS_H, g_t.stats_pipe, set, &pc, NULL);
        vk_barrier(cb, g_t.stats.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                   VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        rec_CmdCopyImageToBuffer(cb, g_t.stats.img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g_t.stats_buffer, 1, &out);
        /* the copy's writes made available to the host, which reads the buffer a run of the numbers later */
        const VkBufferMemoryBarrier2 read = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                                             .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
                                             .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                             .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
                                             .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT,
                                             .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                             .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                             .buffer = g_t.stats_buffer,
                                             .size = VK_WHOLE_SIZE};
        const VkDependencyInfo host = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .bufferMemoryBarrierCount = 1, .pBufferMemoryBarriers = &read};
        rec_CmdPipelineBarrier2(cb, &host);
        g_t.stats_run = g_t.runs;
        memcpy(g_t.stats_drawn, g_t.drawn, sizeof g_t.stats_drawn);
        g_t.stats_draws = g_t.object_draws;
        g_t.stats_height = area ? (u32)area->h : scene->sh;
    }
    if (!view_due) {
        /* no view: in taa, with none asked for, or under the rectangle's gate the error view without a frame
           before made from the same */
        gputime_end(cb, &g_t.time_numbers);
        return;
    }
    /* a view of the probe, drawn over the scene */
    vk_surf_use(cb, scene, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    fullscreen(cb, scene->att, scene->sw, scene->sh, g_t.view_pipe, set, &pc, area);
    vk_surf_wrote(scene);
    vk_rt_drawn(scene);
    gputime_end(cb, &g_t.time_numbers);
}
