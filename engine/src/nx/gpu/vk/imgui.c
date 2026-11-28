/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* Dear ImGui's windows over the presented image (docs/records/ui/imgui-plan.md). ImGui's backend calls Vulkan directly, so the
   recording ring is drained first (vk_rec_sync, under vk_lock), and ImGui records into the frame's command buffer in
   order with the commands queued after it. A frame is drawn only while a window shows. */
#include <stdio.h>
#include <string.h>
#include "nx/gpu/vk/vk.h"
#include "nx/gpu/vk/engine_menu.h"
#include "nx/gpu/vk/imgui_glue.h"
#include "nx/runtime/main.h"
#include "nx/runtime/options.h"
#include "nx/runtime/platform/log.h"
#include "nx/runtime/platform/overlay.h"
#include "nx/runtime/platform/ui.h"
#include "nx/gpu/vk/imgui_input.h"
#include "nx/gpu/vk/menu_spv.h"
#include "nx/runtime/platform/plat.h"

enum { FONT_PX = 18, FONT_PATH_MAX = 1100 };
static const char LATIN_FONT[] = "fonts/Roboto-Variable.ttf";
static const char JAPANESE_FONT[] = "fonts/NotoSansJP-Regular.otf";
static const char ICON_FONT[] = "fonts/MaterialSymbolsRounded-Subset.ttf";

static u64 g_last_ns;
/* the box's texts are the sizes that ImguiKeyboard holds */
_Static_assert(IMGUI_KB_TEXT == OVERLAY_KB_TEXT, "the keyboard's line");
_Static_assert(sizeof(((ImguiKeyboard*)0)->header) == sizeof(((OverlayKeyboardView*)0)->header), "the keyboard's header");
_Static_assert(sizeof(((ImguiKeyboard*)0)->sub) == sizeof(((OverlayKeyboardView*)0)->sub), "the keyboard's sub text");
_Static_assert(sizeof(((ImguiKeyboard*)0)->guide) == sizeof(((OverlayKeyboardView*)0)->guide), "the keyboard's guide");
_Static_assert(sizeof(((ImguiKeyboard*)0)->ok) == sizeof(((OverlayKeyboardView*)0)->ok), "the keyboard's OK");
static int g_failed, g_hook_set; /* g_failed: the backend did not start or failed, not tried again until the swapchain is made again */
static int windows_running(void) { return imgui_input_running(); }

static int font_path(char* out, const char* name) {
    FILE* f;
    snprintf(out, FONT_PATH_MAX, "%s%s", g_datadir, name);
    f = fopen(out, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

/* the rendering device's UUID, asked once: NVML names the same GPU with it */
static const unsigned char* gpu_uuid(void) {
    static unsigned char uuid[VK_UUID_SIZE];
    static int have;
    if (!have) {
        VkPhysicalDeviceIDProperties id = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
        VkPhysicalDeviceProperties2 p2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &id};
        vkGetPhysicalDeviceProperties2(g_vk.phys, &p2);
        memcpy(uuid, id.deviceUUID, sizeof uuid);
        have = 1;
    }
    return uuid;
}

static int ready(void) {
    char latin[FONT_PATH_MAX], japanese[FONT_PATH_MAX], icons[FONT_PATH_MAX];
    int have_latin, have_jp, have_icons;
    ImguiGlueInit in;
    if (!g_hook_set) { /* the hook is the same for the whole run: set once, before the first window frame */
        overlay_keyboard_external_set(windows_running);
        g_hook_set = 1;
    }
    if (g_failed) return 0;
    if (imgui_glue_ready()) {
        imgui_input_running_set(1);
        return 1;
    }
    have_latin = font_path(latin, LATIN_FONT);
    have_jp = have_latin && font_path(japanese, JAPANESE_FONT);
    have_icons = have_latin && font_path(icons, ICON_FONT);
    in = (ImguiGlueInit){.instance = g_vk.instance,
                         .phys = g_vk.phys,
                         .dev = g_vk.dev,
                         .family = g_vk.gfx_family,
                         .queue = g_vk.gfx,
                         .image_count = g_vk.sc_count > VK_FRAMES ? g_vk.sc_count : VK_FRAMES,
                         .format = g_vk.sc_format,
                         .latin_font = have_latin ? latin : NULL,
                         .japanese_font = have_jp ? japanese : NULL,
                         .icon_font = have_icons ? icons : NULL,
                         .font_px = FONT_PX};
    if (!have_latin) LOG("imgui: %s is missing in the data folder, the built-in font is used\n", LATIN_FONT);
    else if (!have_icons) LOG("imgui: %s is missing in the data folder, the icons use the text font\n", ICON_FONT);
    g_failed = !imgui_glue_init(&in);
    if (g_failed) LOG("imgui: the Vulkan backend did not start\n");
    imgui_input_running_set(!g_failed);
    return !g_failed;
}

/* a barrier straight into cb, in order with the commands that the drain has recorded */
static void image_barrier(VkCommandBuffer cb, VkImage img, VkPipelineStageFlags2 src_stage, VkAccessFlags2 src_access,
                          VkImageLayout from, VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access,
                          VkImageLayout to) {
    VkImageMemoryBarrier2 b = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                               .srcStageMask = src_stage,
                               .srcAccessMask = src_access,
                               .dstStageMask = dst_stage,
                               .dstAccessMask = dst_access,
                               .oldLayout = from,
                               .newLayout = to,
                               .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                               .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                               .image = img,
                               .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    VkDependencyInfo d = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1,
                          .pImageMemoryBarriers = &b};
    vkCmdPipelineBarrier2(cb, &d);
}

/* the game's keyboard box into the frame: its state, under ui_lock (vk_lock then ui_lock, as vk_ui_draw takes them) */
static void keyboard_view(ImguiKeyboard* k) {
    OverlayKeyboardView v;
    ui_lock();
    k->shown = overlay_keyboard_view(&v);
    ui_unlock();
    if (!k->shown) return;
    k->ticket = v.ticket;
    k->units = v.units;
    k->min = v.min;
    k->max = v.max;
    k->password = v.password;
    k->numbers = v.numbers;
    memcpy(k->header, v.header, sizeof k->header);
    memcpy(k->sub, v.sub, sizeof k->sub);
    memcpy(k->guide, v.guide, sizeof k->guide);
    memcpy(k->ok, v.ok, sizeof k->ok);
    memcpy(k->text, v.text, sizeof k->text);
    k->pad_answer = v.pad_answer == OVERLAY_KEY_ENTER ? IMGUI_KB_ENTER : v.pad_answer == OVERLAY_KEY_CANCEL ? IMGUI_KB_CANCEL : IMGUI_KB_NONE;
}

/* what the window answered, for the request that it showed: the line, then the key. The box takes it only while that
   request is still its own (overlay_keyboard_apply checks the ticket under ui_lock) */
static void keyboard_answer(const ImguiKeyboard* k) {
    int key = k->answer == IMGUI_KB_ENTER ? OVERLAY_KEY_ENTER : k->answer == IMGUI_KB_CANCEL ? OVERLAY_KEY_CANCEL : -1;
    if (!k->shown || (!k->text_changed && key < 0)) return;
    ui_lock();
    overlay_keyboard_apply(k->ticket, k->text_changed ? k->text : NULL, key);
    ui_unlock();
}

/* ---- the engine menu's offscreen layer: the panel fades as one picture (docs/dev/ui/engine-menu.md) ---- */
static VkImage g_menu_img;
static VmaAllocation g_menu_mem;
static VkImageView g_menu_view;
static VkExtent2D g_menu_extent;
static VkDescriptorSetLayout g_menu_dsl;
static VkPipelineLayout g_menu_layout;
static VkPipeline g_menu_pipe;
static VkFormat g_menu_pipe_format;
static VkSampler g_menu_sampler;

static void menu_image_gone(void) {
    if (g_menu_img) vk_defer_image(g_menu_img, g_menu_mem, g_menu_view, VK_NULL_HANDLE);
    g_menu_img = VK_NULL_HANDLE;
    g_menu_mem = VK_NULL_HANDLE;
    g_menu_view = VK_NULL_HANDLE;
    g_menu_extent = (VkExtent2D){0, 0};
}

/* one color image of the swapchain's format and size, made again after an extent change */
static int menu_image_ready(void) {
    VkImageCreateInfo ii = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                            .imageType = VK_IMAGE_TYPE_2D,
                            .format = g_vk.sc_format,
                            .extent = {g_vk.sc_extent.width, g_vk.sc_extent.height, 1},
                            .mipLevels = 1,
                            .arrayLayers = 1,
                            .samples = VK_SAMPLE_COUNT_1_BIT,
                            .tiling = VK_IMAGE_TILING_OPTIMAL,
                            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                            .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    VkImageViewCreateInfo vci = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                 .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                 .format = g_vk.sc_format,
                                 .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    if (g_menu_img && g_menu_extent.width == g_vk.sc_extent.width &&
        g_menu_extent.height == g_vk.sc_extent.height)
        return 1;
    menu_image_gone();
    if (!vk_image_create(&ii, &g_menu_img, &g_menu_mem)) {
        g_menu_img = VK_NULL_HANDLE;
        return 0;
    }
    vci.image = g_menu_img;
    if (!VK_OK(vkCreateImageView(g_vk.dev, &vci, NULL, &g_menu_view))) {
        vk_image_destroy(g_menu_img, g_menu_mem);
        g_menu_img = VK_NULL_HANDLE;
        return 0;
    }
    g_menu_extent = g_vk.sc_extent;
    return 1;
}

static int menu_objects_ready(void) {
    VkDescriptorSetLayoutBinding binding = {.binding = 0,
                                            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                            .descriptorCount = 1,
                                            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT};
    VkDescriptorSetLayoutCreateInfo dci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 1, .pBindings = &binding};
    VkPushConstantRange push = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float)};
    VkPipelineLayoutCreateInfo lci = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                      .setLayoutCount = 1,
                                      .pSetLayouts = &g_menu_dsl,
                                      .pushConstantRangeCount = 1,
                                      .pPushConstantRanges = &push};
    VkSamplerCreateInfo sci = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                               .magFilter = VK_FILTER_NEAREST,
                               .minFilter = VK_FILTER_NEAREST,
                               .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                               .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                               .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                               .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
    if (g_menu_layout) return 1;
    if (!g_menu_dsl && !VK_OK(vkCreateDescriptorSetLayout(g_vk.dev, &dci, NULL, &g_menu_dsl))) return 0;
    if (!VK_OK(vkCreatePipelineLayout(g_vk.dev, &lci, NULL, &g_menu_layout))) return 0;
    if (!g_menu_sampler && !VK_OK(vkCreateSampler(g_vk.dev, &sci, NULL, &g_menu_sampler))) return 0;
    return 1;
}

static int menu_pipeline_ready(VkFormat format) {
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
    /* the image holds premultiplied color (ImGui's blend), so the panel alpha premultiplies it too */
    VkPipelineColorBlendAttachmentState ba = {.blendEnable = VK_TRUE,
                                              .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
                                              .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                                              .colorBlendOp = VK_BLEND_OP_ADD,
                                              .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
                                              .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                                              .alphaBlendOp = VK_BLEND_OP_ADD,
                                              .colorWriteMask = 0xF};
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
                                        .pDynamicState = &ds};
    if (g_menu_pipe && g_menu_pipe_format == format) return 1;
    if (!menu_objects_ready()) return 0;
    gci.layout = g_menu_layout; /* made by menu_objects_ready on the first call */
    if (g_menu_pipe) vkDestroyPipeline(g_vk.dev, g_menu_pipe, NULL); /* the swapchain changed format and waited */
    g_menu_pipe = VK_NULL_HANDLE;
    vs = vk_module_spv(menu_composite_vs_spv, (size_t)(menu_composite_vs_spv_size / 4));
    fs = vk_module_spv(menu_composite_fs_spv, (size_t)(menu_composite_fs_spv_size / 4));
    st[0].module = vs;
    st[1].module = fs;
    if (vs && fs && VK_OK(vkCreateGraphicsPipelines(g_vk.dev, vk_pipeline_cache(), 1, &gci, NULL, &g_menu_pipe)))
        g_menu_pipe_format = format;
    else
        g_menu_pipe = VK_NULL_HANDLE;
    if (vs) vkDestroyShaderModule(g_vk.dev, vs, NULL);
    if (fs) vkDestroyShaderModule(g_vk.dev, fs, NULL);
    return g_menu_pipe != VK_NULL_HANDLE;
}

/* the ImGui draw into the offscreen image, then the image over the swapchain image with the panel alpha */
static void menu_record(VkCommandBuffer cb, VkImage sc, VkImageView view) {
    const float alpha = engine_menu_panel_alpha();
    VkDescriptorSet set;
    if (!menu_pipeline_ready(g_vk.sc_format)) return; /* the frame stays as it was drawn */
    /* the previous frame sampled the image: wait for that, and discard its contents (they are cleared) */
    image_barrier(cb, g_menu_img, VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                  VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    {
        VkRenderingAttachmentInfo at = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                                        .imageView = g_menu_view,
                                        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                        .clearValue = {.color = {.float32 = {0.0f, 0.0f, 0.0f, 0.0f}}}};
        VkRenderingInfo ri = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                              .renderArea = {{0, 0}, g_vk.sc_extent},
                              .layerCount = 1,
                              .colorAttachmentCount = 1,
                              .pColorAttachments = &at};
        vkCmdBeginRendering(cb, &ri);
        imgui_glue_record(cb);
        vkCmdEndRendering(cb);
    }
    vk_render_forget_state();
    image_barrier(cb, g_menu_img, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    set = vk_desc_alloc(g_menu_dsl);
    if (!set) return;
    {
        VkDescriptorImageInfo di = {g_menu_sampler, g_menu_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet wr = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                   .dstSet = set,
                                   .descriptorCount = 1,
                                   .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                   .pImageInfo = &di};
        vkUpdateDescriptorSets(g_vk.dev, 1, &wr, 0, NULL);
    }
    image_barrier(cb, sc, VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    {
        VkRenderingAttachmentInfo at = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                                        .imageView = view,
                                        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                                        .storeOp = VK_ATTACHMENT_STORE_OP_STORE};
        VkRenderingInfo ri = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                              .renderArea = {{0, 0}, g_vk.sc_extent},
                              .layerCount = 1,
                              .colorAttachmentCount = 1,
                              .pColorAttachments = &at};
        VkViewport vport = {0, 0, (float)g_vk.sc_extent.width, (float)g_vk.sc_extent.height, 0, 1};
        VkRect2D sci = {{0, 0}, g_vk.sc_extent};
        vkCmdBeginRendering(cb, &ri);
        vk_render_forget_state();
        rec_CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_menu_pipe);
        rec_CmdSetViewport(cb, 0, 1, &vport);
        rec_CmdSetScissor(cb, 0, 1, &sci);
        rec_CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g_menu_layout, 0, 1, &set, 0, NULL);
        rec_CmdPushConstants(cb, g_menu_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof alpha, &alpha);
        rec_CmdDraw(cb, 3, 1, 0, 0);
        vkCmdEndRendering(cb);
    }
    vk_render_forget_state();
    image_barrier(cb, sc, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_BLIT_BIT,
                  VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
}

void vk_imgui_swapchain_gone(void) {
    ImguiWants none = {0};
    menu_image_gone();
    imgui_input_running_set(0);
    imgui_input_publish(&none);
    imgui_glue_shutdown();
    g_failed = 0;
}

void vk_imgui_draw(VkCommandBuffer cb, VkImage sc, u32 game_w, u32 game_h) {
    u64 now = plat_ns();
    float dt = g_last_ns ? (float)((double)(now - g_last_ns) * 1e-9) : 0;
    VkImageView view;
    static ImguiFrame frame; /* the keyboard's texts make it big */
    char limit[16];
    int drawn;
    if (!__atomic_load_n(&g_settings.imgui, __ATOMIC_RELAXED)) {
        g_last_ns = 0;
        imgui_input_running_set(0);
        if (imgui_glue_ready()) imgui_glue_unwanted();
        return;
    }
    g_last_ns = now;
    if (!ready() || !(view = vk_ui_swapchain_view(sc))) return;
    fps_limit_text(g_opt_fps, limit, sizeof limit);
    frame = (ImguiFrame){0};
    keyboard_view(&frame.kb);
    frame.w = (float)g_vk.sc_extent.width;
    frame.h = (float)g_vk.sc_extent.height;
    frame.dt = dt;
    frame.window_w = g_vk.sc_win_w;
    frame.window_h = g_vk.sc_win_h;
    frame.display_scale = (float)overlay_display_percent() / 100.0f;
    frame.game_w = (int)game_w;
    frame.game_h = (int)game_h;
    frame.vsync = __atomic_load_n(&g_settings.vsync, __ATOMIC_RELAXED);
    frame.anti_aliasing = __atomic_load_n(&g_settings.anti_aliasing, __ATOMIC_RELAXED);
    frame.scaling_filter = __atomic_load_n(&g_settings.scaling_filter, __ATOMIC_RELAXED);
    frame.fps_limit = limit;
    frame.gpu_uuid = gpu_uuid();
    drawn = imgui_glue_frame(&frame);
    if (imgui_glue_vk_failed()) { /* the backend stays until the swapchain goes: frames may still use it */
        g_failed = 1;
        imgui_input_running_set(0);
        LOG("imgui: the Vulkan backend reported an error, the windows are off\n");
        imgui_glue_unwanted();
        return;
    }
    keyboard_answer(&frame.kb);
    if (!drawn) return; /* nothing shows: no drain, no pass */
    vk_rec_sync();
    if (engine_menu_active() && menu_image_ready()) { /* the panel fades as one picture through the offscreen layer */
        menu_record(cb, sc, view);
        return;
    }
    image_barrier(cb, sc, VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                  VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    {
        VkRenderingAttachmentInfo at = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                                        .imageView = view,
                                        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                        .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                                        .storeOp = VK_ATTACHMENT_STORE_OP_STORE};
        VkRenderingInfo ri = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                              .renderArea = {{0, 0}, g_vk.sc_extent},
                              .layerCount = 1,
                              .colorAttachmentCount = 1,
                              .pColorAttachments = &at};
        vkCmdBeginRendering(cb, &ri);
        imgui_glue_record(cb);
        vkCmdEndRendering(cb);
    }
    vk_render_forget_state(); /* ImGui bound its own pipeline and buffers */
    image_barrier(cb, sc, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                  VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_BLIT_BIT,
                  VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
}
