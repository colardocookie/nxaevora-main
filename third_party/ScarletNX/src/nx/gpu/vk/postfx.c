/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
/* Anti-aliasing of the frame that is shown. The game's last render target goes through one fragment pass into an
   image of its own (same format and size), and the present blits from that image. The game's render targets stay as
   it drew them: the pass sees the finished frame, the on-screen controls and the menus are drawn after it */
#include <string.h>
#include "nx/runtime/platform/log.h"
#include "nx/gpu/vk/vk.h"
#include "nx/gpu/vk/post_spv.h"

#define PIPE_FORMATS 4 /* the frame's formats seen: one pipeline each, kept to the end of the run */

typedef struct {
    VkFormat format;
    VkPipeline pipe;
} FormatPipe;

typedef struct {
    float texel[2];
    u32 srgb, pad;
} PassConstants;

static VkDescriptorSetLayout g_dsl;
static VkPipelineLayout g_layout;
static VkSampler g_sampler;
static FormatPipe g_pipes[PIPE_FORMATS];
static VkImage g_out;
static VmaAllocation g_out_mem;
static VkImageView g_out_view;
static VkFormat g_out_format;
static u32 g_out_w, g_out_h;

static int layout_make(void) {
    VkDescriptorSetLayoutBinding b = {.binding = 0,
                                      .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                      .descriptorCount = 1,
                                      .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT};
    VkDescriptorSetLayoutCreateInfo dci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 1, .pBindings = &b};
    VkPushConstantRange pc = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PassConstants)};
    VkPipelineLayoutCreateInfo lci = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                      .setLayoutCount = 1,
                                      .pSetLayouts = &g_dsl,
                                      .pushConstantRangeCount = 1,
                                      .pPushConstantRanges = &pc};
    VkSamplerCreateInfo sci = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                               .magFilter = VK_FILTER_LINEAR,
                               .minFilter = VK_FILTER_LINEAR,
                               .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                               .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                               .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                               .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
    if (g_layout) return 1;
    if (!VK_OK(vkCreateDescriptorSetLayout(g_vk.dev, &dci, NULL, &g_dsl))) return 0;
    if (!VK_OK(vkCreatePipelineLayout(g_vk.dev, &lci, NULL, &g_layout))) return 0;
    if (!VK_OK(vkCreateSampler(g_vk.dev, &sci, NULL, &g_sampler))) return 0;
    return 1;
}

/* the FXAA pipeline for a color format, made once */
static VkPipeline pipeline_for(VkFormat format) {
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
                                        .layout = g_layout};
    VkPipeline pipe = VK_NULL_HANDLE;
    int i;
    for (i = 0; i < PIPE_FORMATS && g_pipes[i].pipe; ++i)
        if (g_pipes[i].format == format) return g_pipes[i].pipe;
    if (i == PIPE_FORMATS) {
        LOG_LIMIT(1, "vulkan: anti-aliasing: the frame has more than %d formats, the pass is skipped\n", PIPE_FORMATS);
        return VK_NULL_HANDLE;
    }
    vs = vk_module_spv(post_vs_spv, (size_t)(post_vs_spv_size / 4));
    fs = vk_module_spv(fxaa_fs_spv, (size_t)(fxaa_fs_spv_size / 4));
    st[0].module = vs;
    st[1].module = fs;
    if (vs && fs && !VK_OK(vkCreateGraphicsPipelines(g_vk.dev, vk_pipeline_cache(), 1, &gci, NULL, &pipe)))
        pipe = VK_NULL_HANDLE;
    if (vs) vkDestroyShaderModule(g_vk.dev, vs, NULL);
    if (fs) vkDestroyShaderModule(g_vk.dev, fs, NULL);
    if (!pipe) {
        LOG_LIMIT(1, "vulkan: anti-aliasing: the pipeline for format %d could not be made\n", (int)format);
        return VK_NULL_HANDLE;
    }
    g_pipes[i] = (FormatPipe){format, pipe};
    return pipe;
}

/* the pass's image for a frame of this size and format: the last one when it fits, else a new one (the old one goes
   when the frames in flight are done with it) */
static int out_ensure(VkFormat format, u32 w, u32 h) {
    VkImageCreateInfo ii = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                            .imageType = VK_IMAGE_TYPE_2D,
                            .format = format,
                            .extent = {w, h, 1},
                            .mipLevels = 1,
                            .arrayLayers = 1,
                            .samples = VK_SAMPLE_COUNT_1_BIT,
                            .tiling = VK_IMAGE_TILING_OPTIMAL,
                            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
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

VkImage vk_postfx_apply(VkCommandBuffer cb, Surf* src, int mode) {
    VkPipeline pipe;
    VkDescriptorSet set;
    PassConstants pc;
    if (mode != VK_AA_FXAA || !src || src->layers != 1 || src->f.depth || !layout_make()) return VK_NULL_HANDLE;
    pipe = pipeline_for(src->f.vk);
    if (!pipe || !out_ensure(src->f.vk, src->sw, src->sh)) return VK_NULL_HANDLE;
    set = vk_desc_alloc(g_dsl);
    if (!set) return VK_NULL_HANDLE;
    vk_render_end();
    {
        VkDescriptorImageInfo di = {g_sampler, src->att, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet wr = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                   .dstSet = set,
                                   .descriptorCount = 1,
                                   .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                   .pImageInfo = &di};
        vkUpdateDescriptorSets(g_vk.dev, 1, &wr, 0, NULL);
    }
    vk_surf_use(cb, src, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    /* the last frame's blit read the image: it finishes before this pass writes it, whose old content is not needed */
    vk_barrier(cb, g_out, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_BLIT_BIT, 0, VK_IMAGE_LAYOUT_UNDEFINED,
               VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
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
        rec_CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_layout, 0, 1, &set, 0, NULL);
        rec_CmdPushConstants(cb, g_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof pc, &pc);
        rec_CmdDraw(cb, 3, 1, 0, 0);
        rec_CmdEndRendering(cb);
    }
    vk_barrier(cb, g_out, VK_IMAGE_ASPECT_COLOR_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
               VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
               VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    return g_out;
}
