/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the NXAevora contributors */
/* The ImGui context, its fonts and its Vulkan backend (imgui_glue.h) */
#define IMGUI_IMPL_VULKAN_USE_VOLK
#include "nx/gpu/vk/imgui_glue.h"
#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include "nx/gpu/vk/imgui_events.h"
#include "nx/gpu/vk/imgui_input.h"
#include "nx/gpu/vk/imgui_style.h"
#include "nx/gpu/vk/imgui_ui.h"

namespace {
constexpr uint32_t TEXTURE_SLOTS = 16; /* sampler descriptors: the font atlas and a few more images */
bool g_ready;
bool g_vk_failed; /* the backend got an error result: set on the frame's thread, under vk_lock */
ImGuiContext* g_ctx;

void check_vk_result(VkResult r) {
    if (r < 0) g_vk_failed = true;
}

bool add_fonts(const ImguiGlueInit* in) {
    ImGuiIO& io = ImGui::GetIO();
    if (!in->latin_font) return true;
    if (!io.Fonts->AddFontFromFileTTF(in->latin_font, in->font_px)) return false;
    if (in->japanese_font) {
        ImFontConfig merge;
        merge.MergeMode = true;
        if (!io.Fonts->AddFontFromFileTTF(in->japanese_font, in->font_px, &merge)) return false;
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
    vi.ImageCount = in->image_count < 2 ? 2 : in->image_count;
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
    io.DisplaySize = ImVec2(f->w, f->h);
    io.DeltaTime = f->dt > 0 ? f->dt : 1.0f / 60;
    imgui_events_feed();
    ImGui_ImplVulkan_NewFrame();
    ImGui::NewFrame();
    bool menu = imgui_ui_draw(f);
    ImGui::Render(); /* the end of the frame: the IME callback has run */
    imgui_events_publish(menu);
    return ImGui::GetDrawData()->TotalVtxCount > 0;
}

extern "C" void imgui_glue_unwanted(void) {
    imgui_ui_reset();
    ImguiWants none = {};
    imgui_input_publish(&none);
}

extern "C" void imgui_glue_record(VkCommandBuffer cb) {
    if (g_ready) ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cb);
}
