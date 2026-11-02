/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* What the frame goes through on its way to the window. The anti-aliasing pass reads the game's last render target and
   writes an image of its own (same format and size). The scaling pass draws the frame, or that image, into the window's
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

static VkDescriptorSetLayout g_dsl;
static VkPipelineLayout g_aa_layout, g_scale_layout;
static VkSampler g_sampler;
static PipeCache g_aa_pipes, g_bicubic_pipes;
static VkImage g_out;
static VmaAllocation g_out_mem;
static VkImageView g_out_view;
static VkFormat g_out_format;
static u32 g_out_w, g_out_h;

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

/* the anti-aliasing pass's image for a frame of this size and format: the last one when it fits, else a new one (the
   old one goes when the frames in flight are done with it) */
static int out_ensure(VkFormat format, u32 w, u32 h) {
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
    if (g_out && g_out_format == format && g_out_w == w && g_out_h == h) return 1;
    if (g_out) vk_defer_image(g_out, g_out_mem, g_out_view, VK_NULL_HANDLE);
    g_out = VK_NULL_HANDLE;
    g_out_view = VK_NULL_HANDLE;
    if (!vk_image_create(&ii, &g_out, &g_out_mem)) {
        g_out = VK_NULL_HANDLE;
        LOG_LIMIT(1, "vulkan: anti-aliasing: no memory for the %ux%u image of the pass\n", w, h);
        return 0;
    }
    vci.image = g_out;
    if (!VK_OK(vkCreateImageView(g_vk.dev, &vci, NULL, &g_out_view))) {
        vk_defer_image(g_out, g_out_mem, VK_NULL_HANDLE, VK_NULL_HANDLE);
        g_out = VK_NULL_HANDLE;
        return 0;
    }
    g_out_format = format;
    g_out_w = w;
    g_out_h = h;
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

VkImage vk_postfx_apply(VkCommandBuffer cb, Surf* src, int mode, int for_sampling, VkImageView* view) {
    VkPipeline pipe;
    VkDescriptorSet set;
    AaConstants pc;
    if (mode != VK_AA_FXAA || !src || src->layers != 1 || src->f.depth || !layouts_make()) return VK_NULL_HANDLE;
    pipe = pipeline_for(&g_aa_pipes, src->f.vk, fxaa_fs_spv, fxaa_fs_spv_size, g_aa_layout, "anti-aliasing");
    if (!pipe || !out_ensure(src->f.vk, src->sw, src->sh)) return VK_NULL_HANDLE;
    set = sampled_set(src->att);
    if (!set) return VK_NULL_HANDLE;
    vk_render_end();
    vk_surf_use(cb, src, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    /* the last frame read the image, in a blit or a draw: that finishes before this pass writes it, whose old content
       is not needed */
    vk_barrier(cb, g_out, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_BLIT_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
               0, VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
               VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    pc.texel[0] = 1.0f / (float)src->sw;
    pc.texel[1] = 1.0f / (float)src->sh;
    pc.srgb = vk_srgb_as(src->f.vk, 0) != src->f.vk;
    pc.pad = 0;
    {
        VkRenderingAttachmentInfo at = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                                        .imageView = g_out_view,
                                        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                        .storeOp = VK_ATTACHMENT_STORE_OP_STORE};
        VkRenderingInfo ri = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                              .renderArea = {{0, 0}, {src->sw, src->sh}},
                              .layerCount = 1,
                              .colorAttachmentCount = 1,
                              .pColorAttachments = &at};
        VkViewport vp = {0, 0, (float)src->sw, (float)src->sh, 0, 1};
        VkRect2D sc = {{0, 0}, {src->sw, src->sh}};
        rec_CmdBeginRendering(cb, &ri);
        vk_render_forget_state();
        rec_CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
        rec_CmdSetViewport(cb, 0, 1, &vp);
        rec_CmdSetScissor(cb, 0, 1, &sc);
        rec_CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_aa_layout, 0, 1, &set, 0, NULL);
        rec_CmdPushConstants(cb, g_aa_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof pc, &pc);
        rec_CmdDraw(cb, 3, 1, 0, 0);
        rec_CmdEndRendering(cb);
    }
    if (for_sampling)
        vk_barrier(cb, g_out, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                   VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    else
        vk_barrier(cb, g_out, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                   VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    *view = g_out_view;
    return g_out;
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
