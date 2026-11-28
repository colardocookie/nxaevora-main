/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The ImGui context, its fonts and its Vulkan backend (imgui_glue.h) */
#define IMGUI_IMPL_VULKAN_USE_VOLK
#include "nx/gpu/vk/imgui_glue.h"
#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include "nx/gpu/vk/engine_menu.h"
#include "nx/gpu/vk/imgui_events.h"
#include "nx/gpu/vk/imgui_input.h"
#include "nx/gpu/vk/imgui_style.h"
#include "nx/gpu/vk/imgui_ui.h"

namespace {
constexpr uint32_t TEXTURE_SLOTS = 16; /* sampler descriptors: the font atlas and a few more images */
/* Two draw parts are recorded in a frame (the overlays and the rest), so each swapchain image needs two buffers: the
   backend moves to the next buffer at each call, and a buffer that a frame in flight still reads must not be written. */
constexpr uint32_t BUFFERS_PER_IMAGE = 2;
bool g_ready;
bool g_vk_failed; /* the backend got an error result: set on the frame's thread, under vk_lock */
ImGuiContext* g_ctx;
debug_overlay::OverlayDrawLists g_overlays{};

void check_vk_result(VkResult r) {
    if (r < 0) g_vk_failed = true;
}

bool IsOverlayList(const ImDrawList* list) {
    for (int i = 0; i < g_overlays.count; ++i)
        if (g_overlays.lists[i] == list) return true;
    return false;
}

/* The draw data of one part: the overlay lists, or every other list. */
void BuildPart(ImDrawData* out, bool overlays) {
    const ImDrawData* all = ImGui::GetDrawData();
    out->DisplayPos = all->DisplayPos;
    out->DisplaySize = all->DisplaySize;
    out->FramebufferScale = all->FramebufferScale;
    out->OwnerViewport = all->OwnerViewport;
    out->Valid = true;
    if (overlays) {
        for (int i = 0; i < g_overlays.count; ++i) out->AddDrawList(g_overlays.lists[i]);
        return;
    }
    for (ImDrawList* list : all->CmdLists)
        if (!IsOverlayList(list)) out->AddDrawList(list);
}

bool add_fonts(const ImguiGlueInit* in) {
    ImGuiIO& io = ImGui::GetIO();
    if (!in->latin_font) return true;
    if (!io.Fonts->AddFontFromFileTTF(in->latin_font, in->font_px)) return false;
    ImFontConfig merge;
    merge.MergeMode = true;
    if (in->japanese_font && !io.Fonts->AddFontFromFileTTF(in->japanese_font, in->font_px, &merge)) return false;
    if (in->icon_font) {
        ImFontConfig icons;
        icons.MergeMode = true;
        icons.GlyphOffset = ImVec2(0.0f, 1.0f);
        if (!io.Fonts->AddFontFromFileTTF(in->icon_font, in->font_px, &icons)) return false;
    }
    return true;
}
} // namespace

extern "C" int imgui_glue_init(const ImguiGlueInit* in) {
    if (g_ready) return 1;
    IMGUI_CHECKVERSION();
    g_ctx = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    imgui_style_apply();
    imgui_events_init();
    if (!add_fonts(in)) { /* a font that cannot be read leaves the built-in one */
        ImGui::DestroyContext(g_ctx);
        g_ctx = ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::GetIO().LogFilename = nullptr;
        imgui_style_apply();
        imgui_events_init();
    }
    ImGui_ImplVulkan_InitInfo vi = {};
    vi.ApiVersion = VK_API_VERSION_1_3;
    vi.Instance = in->instance;
    vi.PhysicalDevice = in->phys;
    vi.Device = in->dev;
    vi.QueueFamily = in->family;
    vi.Queue = in->queue;
    vi.DescriptorPoolSize = TEXTURE_SLOTS;
    vi.MinImageCount = 2;
    vi.ImageCount = BUFFERS_PER_IMAGE * (in->image_count < 2 ? 2 : in->image_count);
    vi.UseDynamicRendering = true;
    vi.CheckVkResultFn = check_vk_result;
    vi.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    vi.PipelineInfoMain.PipelineRenderingCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR};
    vi.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    vi.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &in->format;
    if (!ImGui_ImplVulkan_Init(&vi)) {
        ImGui::DestroyContext(g_ctx);
        g_ctx = nullptr;
        return 0;
    }
    g_vk_failed = false;
    g_ready = true;
    return 1;
}

extern "C" void imgui_glue_shutdown(void) {
    if (!g_ready) return;
    imgui_ui_reset();
    ImGui_ImplVulkan_Shutdown();
    ImGui::DestroyContext(g_ctx);
    g_ctx = nullptr;
    g_ready = false;
}

extern "C" int imgui_glue_ready(void) { return g_ready; }

extern "C" int imgui_glue_vk_failed(void) { return g_vk_failed; }

extern "C" int imgui_glue_frame(ImguiFrame* f) {
    if (!g_ready) return 0;
    ImGuiIO& io = ImGui::GetIO();
    /* the menu's navigation is on only while its panel shows, so the windows keep their behavior */
    const bool menu_shows = engine_menu_visible();
    if (menu_shows) {
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
        io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    } else {
        io.ConfigFlags &= ~(ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad);
        io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
    }
    io.DisplaySize = ImVec2(f->w, f->h);
    io.DeltaTime = f->dt > 0 ? f->dt : 1.0f / 60;
    imgui_events_feed();
    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();
    const ImguiUiDraw ui = imgui_ui_draw(f);
    g_overlays = ui.overlays;
    ImGui::Render(); /* the end of the frame: the IME callback has run */
    imgui_events_publish(ui.menu_shown);
    return ImGui::GetDrawData()->TotalVtxCount > 0;
}

extern "C" void imgui_glue_unwanted(void) {
    imgui_ui_reset();
    g_overlays = debug_overlay::OverlayDrawLists{};
    ImguiWants none = {};
    imgui_input_publish(&none);
}

extern "C" int imgui_glue_overlay_count(void) { return g_overlays.count; }

extern "C" void imgui_glue_record_overlays(VkCommandBuffer cb) {
    if (!g_ready || g_overlays.count <= 0) return;
    ImDrawData part;
    BuildPart(&part, true);
    /* This part always carries the texture updates, so it draws at 0 vertices too: a 0-vertex
       overlay list (a fade at alpha 0) must not lose them. */
    part.Textures = ImGui::GetDrawData()->Textures;
    ImGui_ImplVulkan_RenderDrawData(&part, cb);
}

extern "C" void imgui_glue_record_rest(VkCommandBuffer cb) {
    if (!g_ready) return;
    ImDrawData part;
    BuildPart(&part, false);
    /* The overlays went first, so they carry the texture updates: a second list would repeat them. */
    part.Textures = g_overlays.count > 0 ? nullptr : ImGui::GetDrawData()->Textures;
    /* Only a part without Textures may skip at 0 vertices; the part that carries them always draws. */
    if (part.Textures != nullptr || part.TotalVtxCount > 0) ImGui_ImplVulkan_RenderDrawData(&part, cb);
}
