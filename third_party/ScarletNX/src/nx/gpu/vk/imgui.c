/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* Dear ImGui's windows over the presented image (docs/imgui-plan.md). ImGui's backend calls Vulkan directly, so the
   recording ring is drained first (vk_rec_sync, under vk_lock), and ImGui records into the frame's command buffer in
   order with the commands queued after it. A frame is drawn only while a window shows. */
#include <stdio.h>
#include <string.h>
#include "nx/gpu/vk/vk.h"
#include "nx/gpu/vk/imgui_glue.h"
#include "nx/runtime/main.h"
#include "nx/runtime/options.h"
#include "nx/runtime/platform/log.h"
#include "nx/runtime/platform/overlay.h"
#include "nx/runtime/platform/ui.h"
#include "nx/gpu/vk/imgui_input.h"
#include "nx/runtime/platform/plat.h"

enum { FONT_PX = 18, FONT_PATH_MAX = 1100 };
static const char LATIN_FONT[] = "fonts/Roboto-Variable.ttf";
static const char JAPANESE_FONT[] = "fonts/NotoSansJP-Regular.otf";

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
    char latin[FONT_PATH_MAX], japanese[FONT_PATH_MAX];
    int have_latin, have_jp;
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
    in = (ImguiGlueInit){.instance = g_vk.instance,
                         .phys = g_vk.phys,
                         .dev = g_vk.dev,
                         .family = g_vk.gfx_family,
                         .queue = g_vk.gfx,
                         .image_count = g_vk.sc_count > VK_FRAMES ? g_vk.sc_count : VK_FRAMES,
                         .format = g_vk.sc_format,
                         .latin_font = have_latin ? latin : NULL,
                         .japanese_font = have_jp ? japanese : NULL,
                         .font_px = FONT_PX};
    if (!have_latin) LOG("imgui: %s is missing in the data folder, the built-in font is used\n", LATIN_FONT);
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

void vk_imgui_swapchain_gone(void) {
    ImguiWants none = {0};
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
    frame.open_test = __atomic_load_n(&g_settings.imgui_test, __ATOMIC_RELAXED);
    frame.window_w = g_vk.sc_win_w;
    frame.window_h = g_vk.sc_win_h;
    frame.game_w = (int)game_w;
    frame.game_h = (int)game_h;
    frame.vsync = __atomic_load_n(&g_settings.vsync, __ATOMIC_RELAXED);
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
