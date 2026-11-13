/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The temporal probe for Pokemon Legends: Arceus (SNX_ARCEUS_TEMPORAL=probe), the first step toward temporal
   anti-aliasing and upscaling. It finds the scene colour and depth of a frame, keeps the camera's matrices, and at the
   moment between the last write to the scene colour and its first read makes camera motion vectors from depth. With
   SNX_ARCEUS_TEMPORAL_VIEW it draws them, or the reprojection error, over the scene. Unset, nothing here runs. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nx/gpu/gpu.h"
#include "nx/gpu/vk/vk.h"
#include "nx/gpu/vk/post_spv.h"
#include "nx/gpu/vk/temporal_spv.h"
#include "nx/runtime/platform/log.h"

#define CAMERA_BANK 4       /* the constant bank of the camera block */
#define CAMERA_BYTES 0x140  /* view-projection at +0x00, its inverse at +0xC0, row vectors */
#define CAMERA_INVERSE 0xc0
#define MOTION_FORMAT VK_FORMAT_R16G16_SFLOAT
#define SETS_BINDINGS 4
#define STATS_FORMAT VK_FORMAT_R16G16B16A16_SFLOAT
#define STATS_W 96u /* the numbers' image: one texel a sampled pixel */
#define STATS_H 54u
#define STATS_EVERY 60u    /* runs between two readings */
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
    int mode; /* -1 not read yet, 0 off, 1 probe */
    int view; /* 0 none, 1 the vectors, 2 the reprojection error */
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
} g_t = {.mode = -1};

static int temporal_on(void) {
    if (g_t.mode < 0) {
        const char* mode = getenv("SNX_ARCEUS_TEMPORAL");
        const char* view = getenv("SNX_ARCEUS_TEMPORAL_VIEW");
        g_t.mode = mode && !strcmp(mode, "probe");
        g_t.view = view && (!strcmp(view, "1") || !strcmp(view, "2")) ? view[0] - '0' : 0;
        if (g_t.mode)
            LOG("arceus-temporal: probe on, view %d (0 none, 1 motion vectors, 2 reprojection error)\n", g_t.view);
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
static VkPipeline pipeline_make(VkFormat format, const u32* fs_words, u64 fs_bytes, const char* what) {
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
    VkPipelineColorBlendAttachmentState ba = {.colorWriteMask = 0xF};
    VkPipelineColorBlendStateCreateInfo cb = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &ba};
    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = 2, .pDynamicStates = dyn};
    VkPipelineRenderingCreateInfo ri = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
                                        .colorAttachmentCount = 1,
                                        .pColorAttachmentFormats = &format};
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
    if (!pipe) LOG_LIMIT(1, "arceus-temporal: the %s pipeline for format %d could not be made\n", what, (int)format);
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
static void fullscreen(VkCommandBuffer cb, VkImageView view, u32 w, u32 h, VkPipeline pipe, VkDescriptorSet set,
                       const Constants* pc) {
    VkRenderingAttachmentInfo at = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                                    .imageView = view,
                                    .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                    .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                    .storeOp = VK_ATTACHMENT_STORE_OP_STORE};
    VkRenderingInfo ri = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                          .renderArea = {{0, 0}, {w, h}},
                          .layerCount = 1,
                          .colorAttachmentCount = 1,
                          .pColorAttachments = &at};
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
    g_t.have_before = had;
    if (had) {
        int equal = 0;
        memcpy(g_t.before, g_t.kept, sizeof g_t.before);
        for (int side = 0; side < 2 && !equal; side++) {
            const u8* other = gmem_host(side ? address + size : address - size, 64);
            if (other && !memcmp(other, g_t.kept, 64)) equal = 1 + side;
        }
        if (equal) g_t.pair_equal++;
        else g_t.pair_differ++;
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

static float half_float(u16 h) {
    const int e = h >> 10 & 31, m = h & 1023;
    const float v = e == 0 ? ldexpf((float)m, -24) : e == 31 ? (m ? NAN : INFINITY) : ldexpf((float)(m + 1024), e - 25);
    return h & 0x8000 ? -v : v;
}

/* The numbers of the run the buffer holds, a second old by now: for the pixels sampled, the mean and longest camera
   vector, and by depth band how many differ from last frame by more than STATS_ERROR with the vector and without. */
static void stats_log(void) {
    static const char* const names[3] = {"near", "mid", "far"};
    unsigned n[3] = {0, 0, 0}, wrong[3] = {0, 0, 0}, wrong_still[3] = {0, 0, 0}, sky = 0, off = 0;
    double length = 0, longest = 0;
    for (u32 i = 0; i < STATS_W * STATS_H; i++) {
        const float moved = half_float(g_t.stats_words[4 * i]), still = half_float(g_t.stats_words[4 * i + 1]);
        const float vector = half_float(g_t.stats_words[4 * i + 2]), z = half_float(g_t.stats_words[4 * i + 3]);
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
        wrong[band] += moved > STATS_ERROR;
        wrong_still[band] += still > STATS_ERROR;
        length += vector;
        if (vector > longest) longest = vector;
    }
    const unsigned all = n[0] + n[1] + n[2];
    char bands[240];
    int at = 0;
    for (int b = 0; b < 3; b++)
        at += snprintf(bands + at, sizeof bands - (size_t)at, " %s n=%u wrong=%.1f%% without_vectors=%.1f%%", names[b], n[b],
                       n[b] ? 100.0 * wrong[b] / n[b] : 0.0, n[b] ? 100.0 * wrong_still[b] / n[b] : 0.0);
    LOG("arceus-temporal-stats: run=%llu vector_mean=%.2f px vector_max=%.2f px sky=%u off_screen=%u%s\n",
        (unsigned long long)g_t.stats_run, all ? length / all : 0.0, longest, sky, off, bands);
}

static int stats_ready(void) {
    VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                              .size = (u64)STATS_W * STATS_H * 8,
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
    if (g_t.view && (!g_t.view_pipe || g_t.view_format != scene->f.vk)) {
        g_t.view_pipe = pipeline_make(scene->f.vk, temporal_view_fs_spv, temporal_view_fs_spv_size, "view");
        g_t.view_format = scene->f.vk;
    }
    const VkImageUsageFlags target = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    const VkImageUsageFlags copy = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    if (!g_t.motion_pipe || (g_t.view && !g_t.view_pipe) ||
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
    /* the vectors: depth sampled, the motion image written */
    vk_surf_use(cb, depth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    vk_barrier(cb, g_t.motion.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0,
               VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
               VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    {
        const VkImageView views[SETS_BINDINGS] = {depth_view, depth_view, depth_view, depth_view};
        const VkSampler samplers[SETS_BINDINGS] = {g_t.point, g_t.point, g_t.point, g_t.point};
        const VkDescriptorSet set = set_make(views, samplers);
        if (!set) return;
        fullscreen(cb, g_t.motion.view, g_t.motion.w, g_t.motion.h, g_t.motion_pipe, set, &pc);
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
    /* without a frame before (the first one, or after a gap) the frame is compared with itself */
    const VkImageView views[SETS_BINDINGS] = {g_t.motion.view, now->view, history ? before->view : now->view,
                                              depth_view};
    const VkSampler samplers[SETS_BINDINGS] = {g_t.point, g_t.point, g_t.linear, g_t.point};
    const VkDescriptorSet set = set_make(views, samplers);
    if (!set) return;

    /* the numbers: every STATS_EVERY runs the small image is drawn and copied to memory, and the copy made that
       many runs ago, long finished, is read and logged */
    if (history && g_t.runs % STATS_EVERY == 0 && g_t.stats_pipe && stats_ready() &&
        image_ensure(&g_t.stats, STATS_FORMAT, STATS_W, STATS_H,
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, "numbers")) {
        const VkBufferImageCopy out = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                       .imageExtent = {STATS_W, STATS_H, 1}};
        if (g_t.stats_run) stats_log();
        vk_barrier(cb, g_t.stats.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_COPY_BIT, 0,
                   VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        fullscreen(cb, g_t.stats.view, STATS_W, STATS_H, g_t.stats_pipe, set, &pc);
        vk_barrier(cb, g_t.stats.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                   VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        rec_CmdCopyImageToBuffer(cb, g_t.stats.img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g_t.stats_buffer, 1, &out);
        g_t.stats_run = g_t.runs;
    }
    if (!g_t.view) return;
    /* a view, drawn over the scene */
    vk_surf_use(cb, scene, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    fullscreen(cb, scene->att, scene->sw, scene->sh, g_t.view_pipe, set, &pc);
    vk_surf_wrote(scene);
    vk_rt_drawn(scene);
}
