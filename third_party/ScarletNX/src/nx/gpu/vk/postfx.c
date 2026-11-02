/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* What the frame goes through on its way to the window. The anti-aliasing pass reads the game's last render target and
   writes an image of its own (same format and size): FXAA is one pass, SMAA three (edges, blending weights, blending
   through two images of its own and two lookup tables). The scaling pass draws the frame, or that image, into the window's
   image over the letterboxed rectangle, where a plain blit would stretch it with the hardware's bilinear filter. The
   game's render targets stay as it drew them, and the on-screen controls and the windows are drawn after both */
#include <string.h>
#include "nx/runtime/platform/log.h"
#include "nx/gpu/vk/vk.h"
#include "nx/gpu/vk/post_spv.h"

#define PIPE_FORMATS 4 /* the formats seen: one pipeline each, kept to the end of the run */

typedef struct {
    VkFormat format;
    VkPipeline pipe;
} FormatPipe;

typedef struct {
    FormatPipe e[PIPE_FORMATS];
} PipeCache;

/* the anti-aliasing pass's push constants */
typedef struct {
    float texel[2];
    u32 srgb, pad;
} AaConstants;

/* the scaling pass's: the rectangle, the part of the image, the image's size, the sharpness, the sRGB flag */
typedef struct {
    float dst[4];
    float uv[4];
    float size[2];
    float sharp;
    u32 srgb;
} ScaleConstants;

/* the SMAA passes' push constants: the frame's metrics (1 / width, 1 / height, width, height), the sRGB flag */
typedef struct {
    float metrics[4];
    u32 srgb, pad[3];
} SmaaConstants;

/* an image of the passes with its view; created for a format and a size, kept while they stay */
typedef struct {
    VkImage img;
    VmaAllocation mem;
    VkImageView view;
    VkFormat format;
    u32 w, h;
} Target;

#define SMAA_SETS_BINDINGS 3 /* the most that a pass reads: the edges, the area table and the search table */
#define SMAA_EDGES_FORMAT VK_FORMAT_R8G8_UNORM
#define SMAA_WEIGHTS_FORMAT VK_FORMAT_R8G8B8A8_UNORM
#define SMAA_AREA_W 160u /* the tables of cmake/smaa_tables.cmake */
#define SMAA_AREA_H 560u
#define SMAA_SEARCH_W 64u
#define SMAA_SEARCH_H 16u

static VkDescriptorSetLayout g_dsl, g_smaa_dsl;
static VkPipelineLayout g_aa_layout, g_scale_layout, g_smaa_layout;
static VkSampler g_sampler, g_point;
static PipeCache g_aa_pipes, g_bicubic_pipes, g_edges_pipes, g_weights_pipes, g_blend_pipes;
static Target g_out, g_edges, g_weights, g_area, g_search;
static int g_tables_failed; /* the lookup tables could not be made: SMAA stays off */

static int layouts_make(void) {
    VkDescriptorSetLayoutBinding b = {.binding = 0,
                                      .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                      .descriptorCount = 1,
                                      .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT};
    VkDescriptorSetLayoutCreateInfo dci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 1, .pBindings = &b};
    VkPushConstantRange aa_pc = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(AaConstants)};
    VkPushConstantRange scale_pc = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ScaleConstants)};
    VkPipelineLayoutCreateInfo lci = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                      .setLayoutCount = 1,
                                      .pSetLayouts = &g_dsl,
                                      .pushConstantRangeCount = 1,
                                      .pPushConstantRanges = &aa_pc};
    VkSamplerCreateInfo sci = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                               .magFilter = VK_FILTER_LINEAR,
                               .minFilter = VK_FILTER_LINEAR,
                               .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                               .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                               .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                               .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
    if (g_scale_layout) return 1;
    if (!VK_OK(vkCreateDescriptorSetLayout(g_vk.dev, &dci, NULL, &g_dsl))) return 0;
    if (!VK_OK(vkCreatePipelineLayout(g_vk.dev, &lci, NULL, &g_aa_layout))) return 0;
    lci.pPushConstantRanges = &scale_pc;
    if (!VK_OK(vkCreatePipelineLayout(g_vk.dev, &lci, NULL, &g_scale_layout))) return 0;
    if (!VK_OK(vkCreateSampler(g_vk.dev, &sci, NULL, &g_sampler))) return 0;
    return 1;
}

/* the pipeline of one fragment shader (with post.vert) for a color format, made once and kept in cache */
static VkPipeline pipeline_for(PipeCache* cache, VkFormat format, const u32* fs_words, u64 fs_bytes,
                               VkPipelineLayout layout, const char* what) {
    VkShaderModule vs, fs;
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
                                        .layout = layout};
    VkPipeline pipe = VK_NULL_HANDLE;
    int i;
    for (i = 0; i < PIPE_FORMATS && cache->e[i].pipe; ++i)
        if (cache->e[i].format == format) return cache->e[i].pipe;
    if (i == PIPE_FORMATS) {
        LOG_LIMIT(1, "vulkan: %s: more than %d formats, the pass is skipped\n", what, PIPE_FORMATS);
        return VK_NULL_HANDLE;
    }
    vs = vk_module_spv(post_vs_spv, (size_t)(post_vs_spv_size / 4));
    fs = vk_module_spv(fs_words, (size_t)(fs_bytes / 4));
    st[0].module = vs;
    st[1].module = fs;
    if (vs && fs && !VK_OK(vkCreateGraphicsPipelines(g_vk.dev, vk_pipeline_cache(), 1, &gci, NULL, &pipe)))
        pipe = VK_NULL_HANDLE;
    if (vs) vkDestroyShaderModule(g_vk.dev, vs, NULL);
    if (fs) vkDestroyShaderModule(g_vk.dev, fs, NULL);
    if (!pipe) {
        LOG_LIMIT(1, "vulkan: %s: the pipeline for format %d could not be made\n", what, (int)format);
        return VK_NULL_HANDLE;
    }
    cache->e[i] = (FormatPipe){format, pipe};
    return pipe;
}

/* the layouts, the point sampler and the pipeline layout of the SMAA passes, made once */
static int smaa_layouts_make(void) {
    VkDescriptorSetLayoutBinding b[SMAA_SETS_BINDINGS];
    VkDescriptorSetLayoutCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                           .bindingCount = SMAA_SETS_BINDINGS,
                                           .pBindings = b};
    VkPushConstantRange pc = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SmaaConstants)};
    VkPipelineLayoutCreateInfo lci = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                      .setLayoutCount = 1,
                                      .pSetLayouts = &g_smaa_dsl,
                                      .pushConstantRangeCount = 1,
                                      .pPushConstantRanges = &pc};
    VkSamplerCreateInfo sci = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                               .magFilter = VK_FILTER_NEAREST,
                               .minFilter = VK_FILTER_NEAREST,
                               .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                               .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                               .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                               .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
    if (g_smaa_layout) return 1;
    if (!layouts_make()) return 0; /* the linear sampler */
    for (u32 i = 0; i < SMAA_SETS_BINDINGS; i++)
        b[i] = (VkDescriptorSetLayoutBinding){.binding = i,
                                              .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                              .descriptorCount = 1,
                                              .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT};
    if (!VK_OK(vkCreateDescriptorSetLayout(g_vk.dev, &dci, NULL, &g_smaa_dsl))) return 0;
    if (!VK_OK(vkCreateSampler(g_vk.dev, &sci, NULL, &g_point))) return 0;
    if (!VK_OK(vkCreatePipelineLayout(g_vk.dev, &lci, NULL, &g_smaa_layout))) return 0;
    return 1;
}

/* t as an image of this format and size for the passes: the last one when it fits, else a new one (the old one goes when
   the frames in flight are done with it). what names the image in the log */
static int target_ensure(Target* t, VkFormat format, u32 w, u32 h, const char* what) {
    VkImageCreateInfo ii = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                            .imageType = VK_IMAGE_TYPE_2D,
                            .format = format,
                            .extent = {w, h, 1},
                            .mipLevels = 1,
                            .arrayLayers = 1,
                            .samples = VK_SAMPLE_COUNT_1_BIT,
                            .tiling = VK_IMAGE_TILING_OPTIMAL,
                            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                     VK_IMAGE_USAGE_SAMPLED_BIT,
                            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    VkImageViewCreateInfo vci = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                 .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                 .format = format,
                                 .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    if (t->img && t->format == format && t->w == w && t->h == h) return 1;
    if (t->img) vk_defer_image(t->img, t->mem, t->view, VK_NULL_HANDLE);
    *t = (Target){0};
    if (!vk_image_create(&ii, &t->img, &t->mem)) {
        *t = (Target){0};
        LOG_LIMIT(1, "vulkan: anti-aliasing: no memory for the %ux%u %s image of the pass\n", w, h, what);
        return 0;
    }
    vci.image = t->img;
    if (!VK_OK(vkCreateImageView(g_vk.dev, &vci, NULL, &t->view))) {
        vk_defer_image(t->img, t->mem, VK_NULL_HANDLE, VK_NULL_HANDLE);
        *t = (Target){0};
        return 0;
    }
    t->format = format;
    t->w = w;
    t->h = h;
    return 1;
}

/* a descriptor set of this frame that holds the image to sample */
static VkDescriptorSet sampled_set(VkImageView view) {
    VkDescriptorSet set = vk_desc_alloc(g_dsl);
    VkDescriptorImageInfo di = {g_sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet wr = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                               .dstSet = set,
                               .descriptorCount = 1,
                               .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                               .pImageInfo = &di};
    if (!set) return VK_NULL_HANDLE;
    vkUpdateDescriptorSets(g_vk.dev, 1, &wr, 0, NULL);
    return set;
}

/* a descriptor set of this frame for an SMAA pass: the three views with their samplers (a view the pass does not read
   is any valid one) */
static VkDescriptorSet smaa_set(const VkImageView views[SMAA_SETS_BINDINGS],
                                const VkSampler samplers[SMAA_SETS_BINDINGS]) {
    VkDescriptorSet set = vk_desc_alloc(g_smaa_dsl);
    VkDescriptorImageInfo di[SMAA_SETS_BINDINGS];
    VkWriteDescriptorSet wr[SMAA_SETS_BINDINGS];
    if (!set) return VK_NULL_HANDLE;
    for (u32 i = 0; i < SMAA_SETS_BINDINGS; i++) {
        di[i] = (VkDescriptorImageInfo){samplers[i], views[i], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        wr[i] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                       .dstSet = set,
                                       .dstBinding = i,
                                       .descriptorCount = 1,
                                       .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                       .pImageInfo = &di[i]};
    }
    vkUpdateDescriptorSets(g_vk.dev, SMAA_SETS_BINDINGS, wr, 0, NULL);
    return set;
}

/* one table to the GPU: the image, filled by a copy from a staging buffer, left in SHADER_READ_ONLY_OPTIMAL */
static int table_make(VkCommandBuffer cb, Target* t, VkFormat format, u32 w, u32 h, const u8* px, u64 bytes,
                      const char* what) {
    VkBuffer buf;
    VmaAllocation mem;
    VmaAllocationInfo info;
    VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                              .size = bytes,
                              .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                              .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    VmaAllocationCreateInfo bai = {.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                                            VMA_ALLOCATION_CREATE_MAPPED_BIT,
                                   .usage = VMA_MEMORY_USAGE_AUTO};
    VkImageCreateInfo ii = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                            .imageType = VK_IMAGE_TYPE_2D,
                            .format = format,
                            .extent = {w, h, 1},
                            .mipLevels = 1,
                            .arrayLayers = 1,
                            .samples = VK_SAMPLE_COUNT_1_BIT,
                            .tiling = VK_IMAGE_TILING_OPTIMAL,
                            .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                            .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    VkImageViewCreateInfo vci = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                 .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                 .format = format,
                                 .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    VkBufferImageCopy copy = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {w, h, 1}};
    if (!VK_OK(vmaCreateBuffer(g_vk.vma, &bci, &bai, &buf, &mem, &info))) {
        LOG("vulkan: SMAA: no memory for the %s table's staging buffer\n", what);
        return 0;
    }
    vk_mem_track(mem);
    memcpy(info.pMappedData, px, (size_t)bytes);
    vmaFlushAllocation(g_vk.vma, mem, 0, VK_WHOLE_SIZE);
    vk_defer_buffer(buf, mem);
    if (!vk_image_create(&ii, &t->img, &t->mem)) {
        *t = (Target){0};
        LOG("vulkan: SMAA: no memory for the %s table\n", what);
        return 0;
    }
    vci.image = t->img;
    if (!VK_OK(vkCreateImageView(g_vk.dev, &vci, NULL, &t->view))) {
        vk_defer_image(t->img, t->mem, VK_NULL_HANDLE, VK_NULL_HANDLE);
        *t = (Target){0};
        return 0;
    }
    vk_barrier(cb, t->img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_COPY_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED,
               VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    rec_CmdCopyBufferToImage(cb, buf, t->img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    vk_barrier(cb, t->img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
               VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    t->format = format;
    t->w = w;
    t->h = h;
    return 1;
}

/* the area and search tables, made the first time SMAA runs (outside a render pass). A failure is final: SMAA stays
   off for the run and the frame goes through unsmoothed */
static int smaa_tables_ready(VkCommandBuffer cb) {
    if (g_area.img && g_search.img) return 1;
    if (g_tables_failed) return 0;
    if (smaa_area_tex_size != (u64)SMAA_AREA_W * SMAA_AREA_H * 2 || smaa_search_tex_size != (u64)SMAA_SEARCH_W * SMAA_SEARCH_H ||
        !table_make(cb, &g_area, VK_FORMAT_R8G8_UNORM, SMAA_AREA_W, SMAA_AREA_H, smaa_area_tex, smaa_area_tex_size, "area") ||
        !table_make(cb, &g_search, VK_FORMAT_R8_UNORM, SMAA_SEARCH_W, SMAA_SEARCH_H, smaa_search_tex,
                    smaa_search_tex_size, "search")) {
        g_tables_failed = 1;
        LOG("vulkan: SMAA: the lookup tables could not be made, the frame is shown without anti-aliasing\n");
        return 0;
    }
    return 1;
}

/* an image of the passes to be written: the last frame read it in a blit or a draw, which finishes first, and its old
   content is not needed */
static void to_attachment(VkCommandBuffer cb, const Target* t) {
    vk_barrier(cb, t->img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_BLIT_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
               0, VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
               VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
}

/* an image of the passes that a pass wrote, to be sampled by the next */
static void to_sampled(VkCommandBuffer cb, const Target* t) {
    vk_barrier(cb, t->img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
               VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
               VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

/* one fullscreen triangle into the target (w x h), with the set and the push constants. load: CLEAR for a pass that
   discards pixels (the target then holds zero there), else DONT_CARE for a pass that writes them all */
static void fullscreen_pass(VkCommandBuffer cb, const Target* t, VkAttachmentLoadOp load, VkPipeline pipe,
                            VkPipelineLayout layout, VkDescriptorSet set, const void* pc, u32 pc_size) {
    VkRenderingAttachmentInfo at = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                                    .imageView = t->view,
                                    .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                    .loadOp = load,
                                    .storeOp = VK_ATTACHMENT_STORE_OP_STORE};
    VkRenderingInfo ri = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                          .renderArea = {{0, 0}, {t->w, t->h}},
                          .layerCount = 1,
                          .colorAttachmentCount = 1,
                          .pColorAttachments = &at};
    VkViewport vp = {0, 0, (float)t->w, (float)t->h, 0, 1};
    VkRect2D sc = {{0, 0}, {t->w, t->h}};
    rec_CmdBeginRendering(cb, &ri);
    vk_render_forget_state();
    rec_CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    rec_CmdSetViewport(cb, 0, 1, &vp);
    rec_CmdSetScissor(cb, 0, 1, &sc);
    rec_CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, NULL);
    rec_CmdPushConstants(cb, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, pc_size, pc);
    rec_CmdDraw(cb, 3, 1, 0, 0);
    rec_CmdEndRendering(cb);
}

/* the anti-aliasing image, ready for what comes after: sampled by the scaling pass, or the source of a blit */
static VkImage out_finish(VkCommandBuffer cb, int for_sampling, VkImageView* view) {
    if (for_sampling)
        to_sampled(cb, &g_out);
    else
        vk_barrier(cb, g_out.img, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                   VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    *view = g_out.view;
    return g_out.img;
}

static VkImage fxaa_apply(VkCommandBuffer cb, Surf* src, int for_sampling, VkImageView* view) {
    VkPipeline pipe =
        pipeline_for(&g_aa_pipes, src->f.vk, fxaa_fs_spv, fxaa_fs_spv_size, g_aa_layout, "anti-aliasing");
    VkDescriptorSet set;
    AaConstants pc;
    if (!pipe || !target_ensure(&g_out, src->f.vk, src->sw, src->sh, "FXAA")) return VK_NULL_HANDLE;
    set = sampled_set(src->att);
    if (!set) return VK_NULL_HANDLE;
    vk_render_end();
    vk_surf_use(cb, src, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    to_attachment(cb, &g_out);
    pc.texel[0] = 1.0f / (float)src->sw;
    pc.texel[1] = 1.0f / (float)src->sh;
    pc.srgb = vk_srgb_as(src->f.vk, 0) != src->f.vk;
    pc.pad = 0;
    fullscreen_pass(cb, &g_out, VK_ATTACHMENT_LOAD_OP_DONT_CARE, pipe, g_aa_layout, set, &pc, sizeof pc);
    return out_finish(cb, for_sampling, view);
}

/* SMAA 1x in three passes (shaders/smaa.frag): the edges of the frame, the blending weights from the shapes of the edges
   (with the area and search tables), and the frame blended by the weights into the anti-aliasing image */
static VkImage smaa_apply(VkCommandBuffer cb, Surf* src, int for_sampling, VkImageView* view) {
    VkPipeline edges_pipe, weights_pipe, blend_pipe;
    VkDescriptorSet edges_set, weights_set, blend_set;
    SmaaConstants pc = {.metrics = {1.0f / (float)src->sw, 1.0f / (float)src->sh, (float)src->sw, (float)src->sh}};
    if (g_tables_failed || !smaa_layouts_make()) return VK_NULL_HANDLE;
    edges_pipe = pipeline_for(&g_edges_pipes, SMAA_EDGES_FORMAT, smaa_edges_fs_spv, smaa_edges_fs_spv_size,
                              g_smaa_layout, "SMAA edges");
    weights_pipe = pipeline_for(&g_weights_pipes, SMAA_WEIGHTS_FORMAT, smaa_weights_fs_spv, smaa_weights_fs_spv_size,
                                g_smaa_layout, "SMAA weights");
    blend_pipe = pipeline_for(&g_blend_pipes, src->f.vk, smaa_blend_fs_spv, smaa_blend_fs_spv_size, g_smaa_layout,
                              "SMAA blending");
    if (!edges_pipe || !weights_pipe || !blend_pipe) return VK_NULL_HANDLE;
    if (!target_ensure(&g_edges, SMAA_EDGES_FORMAT, src->sw, src->sh, "SMAA edges") ||
        !target_ensure(&g_weights, SMAA_WEIGHTS_FORMAT, src->sw, src->sh, "SMAA weights") ||
        !target_ensure(&g_out, src->f.vk, src->sw, src->sh, "SMAA"))
        return VK_NULL_HANDLE;
    vk_render_end();
    if (!smaa_tables_ready(cb)) return VK_NULL_HANDLE;
    {
        /* the sets: the source by the point sampler for the edges; the edges, area and search tables for the weights;
           the source and the weights for the blending. The bindings a pass does not read hold the source */
        const VkImageView e_v[3] = {src->att, src->att, src->att};
        const VkSampler e_s[3] = {g_point, g_point, g_point};
        const VkImageView w_v[3] = {g_edges.view, g_area.view, g_search.view};
        const VkSampler w_s[3] = {g_sampler, g_sampler, g_sampler};
        const VkImageView b_v[3] = {src->att, g_weights.view, src->att};
        const VkSampler b_s[3] = {g_sampler, g_sampler, g_sampler};
        edges_set = smaa_set(e_v, e_s);
        weights_set = smaa_set(w_v, w_s);
        blend_set = smaa_set(b_v, b_s);
    }
    if (!edges_set || !weights_set || !blend_set) return VK_NULL_HANDLE;
    vk_surf_use(cb, src, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    pc.srgb = vk_srgb_as(src->f.vk, 0) != src->f.vk;
    to_attachment(cb, &g_edges);
    fullscreen_pass(cb, &g_edges, VK_ATTACHMENT_LOAD_OP_CLEAR, edges_pipe, g_smaa_layout, edges_set, &pc, sizeof pc);
    to_sampled(cb, &g_edges);
    to_attachment(cb, &g_weights);
    fullscreen_pass(cb, &g_weights, VK_ATTACHMENT_LOAD_OP_DONT_CARE, weights_pipe, g_smaa_layout, weights_set, &pc,
                    sizeof pc);
    to_sampled(cb, &g_weights);
    to_attachment(cb, &g_out);
    fullscreen_pass(cb, &g_out, VK_ATTACHMENT_LOAD_OP_DONT_CARE, blend_pipe, g_smaa_layout, blend_set, &pc, sizeof pc);
    return out_finish(cb, for_sampling, view);
}

VkImage vk_postfx_apply(VkCommandBuffer cb, Surf* src, int mode, int for_sampling, VkImageView* view) {
    if ((mode != VK_AA_FXAA && mode != VK_AA_SMAA) || !src || src->layers != 1 || src->f.depth || !layouts_make())
        return VK_NULL_HANDLE;
    return mode == VK_AA_SMAA ? smaa_apply(cb, src, for_sampling, view) : fxaa_apply(cb, src, for_sampling, view);
}

int vk_postfx_scale_ready(VkFormat sc_format, int mode) {
    if (mode != VK_SCALE_BICUBIC || !layouts_make()) return 0;
    return pipeline_for(&g_bicubic_pipes, sc_format, bicubic_fs_spv, bicubic_fs_spv_size, g_scale_layout, "scaling") !=
           VK_NULL_HANDLE;
}

void vk_postfx_scale(VkCommandBuffer cb, VkImage sc, VkImageView sc_view, VkImageView from, u32 sw, u32 sh, int srgb,
                     const float uv[4], const int dst[4], int mode, float sharpness) {
    VkPipeline pipe = pipeline_for(&g_bicubic_pipes, g_vk.sc_format, bicubic_fs_spv, bicubic_fs_spv_size,
                                   g_scale_layout, "scaling");
    VkDescriptorSet set = sampled_set(from);
    ScaleConstants pc = {.dst = {(float)dst[0], (float)dst[1], (float)dst[2], (float)dst[3]},
                         .uv = {uv[0], uv[1], uv[2], uv[3]},
                         .size = {(float)sw, (float)sh},
                         .sharp = sharpness,
                         .srgb = (u32)srgb};
    (void)mode;
    if (!pipe || !set || !sc_view) {
        LOG_LIMIT(1, "vulkan: scaling: the pass could not run, the window shows a frame without the game's picture\n");
        return;
    }
    vk_render_end();
    vk_barrier(cb, sc, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
               VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    {
        VkRenderingAttachmentInfo at = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                                        .imageView = sc_view,
                                        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD, /* the black bars stay */
                                        .storeOp = VK_ATTACHMENT_STORE_OP_STORE};
        VkRenderingInfo ri = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                              .renderArea = {{0, 0}, g_vk.sc_extent},
                              .layerCount = 1,
                              .colorAttachmentCount = 1,
                              .pColorAttachments = &at};
        VkViewport vp = {(float)dst[0], (float)dst[1], (float)dst[2], (float)dst[3], 0, 1};
        VkRect2D scissor = {{dst[0], dst[1]}, {(u32)dst[2], (u32)dst[3]}};
        rec_CmdBeginRendering(cb, &ri);
        vk_render_forget_state();
        rec_CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
        rec_CmdSetViewport(cb, 0, 1, &vp);
        rec_CmdSetScissor(cb, 0, 1, &scissor);
        rec_CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_scale_layout, 0, 1, &set, 0, NULL);
        rec_CmdPushConstants(cb, g_scale_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof pc, &pc);
        rec_CmdDraw(cb, 3, 1, 0, 0);
        rec_CmdEndRendering(cb);
    }
    vk_barrier(cb, sc, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
               VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
               VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
}
