// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// The Debug page of the engine menu (engine_menu_debug.h). The table of its
// settings rows is pure and the labels are the only names the page shows; the
// cards read the rows through imgui_settings_row_key and write them through
// imgui_settings_set, as the other pages do.

#include "nx/gpu/vk/engine_menu_debug.h"

#include <cstring>

namespace {

const char* const kCardNames[ENGINE_MENU_DEBUG_CARD_COUNT] = {
    "Overlays",
    "Hardware",
    "Frame",
    "Display",
    "Logs and IPC",
    "Textures",
    "Recompiler and memory",
    "Vulkan",
    "NVN diagnostics",
    "Macros",
    "Profilers and captures",
};

// The settings rows of the page in page order. The Overlays keys live in the
// Display section of settings.txt, so the page reads them by key.
const EngineMenuDebugEntry kEntries[] = {
    {"stats", ENGINE_MENU_DEBUG_OVERLAYS, "Performance overlay"},
    {"stats_corner", ENGINE_MENU_DEBUG_OVERLAYS, "Performance overlay corner"},
    {"showcase", ENGINE_MENU_DEBUG_OVERLAYS, "Input display"},
    {"showcase_corner", ENGINE_MENU_DEBUG_OVERLAYS, "Input display corner"},

    {"log", ENGINE_MENU_DEBUG_LOGS, "Log level"},
    {"log_max", ENGINE_MENU_DEBUG_LOGS, "Log size limit"},
    {"auto_stub_ipc", ENGINE_MENU_DEBUG_LOGS, "Answer unknown commands"},

    {"native_astc", ENGINE_MENU_DEBUG_TEXTURES, "Native ASTC"},
    {"native_bc", ENGINE_MENU_DEBUG_TEXTURES, "Native BCn"},
    {"native_etc2", ENGINE_MENU_DEBUG_TEXTURES, "Native ETC2"},

    {"slp_vectorizer", ENGINE_MENU_DEBUG_RECOMPILER, "SLP vectorizer"},
    {"fpsr_flags", ENGINE_MENU_DEBUG_RECOMPILER, "FPSR flags"},
    {"volatile_memory", ENGINE_MENU_DEBUG_RECOMPILER, "Volatile load/stores"},
    {"gdb_jit", ENGINE_MENU_DEBUG_RECOMPILER, "GDB JIT symbols"},
    {"alias_pool", ENGINE_MENU_DEBUG_RECOMPILER, "Alias pool"},

    {"diag", ENGINE_MENU_DEBUG_VULKAN, "Shader captures"},
    {"vk_validation", ENGINE_MENU_DEBUG_VULKAN, "Vulkan validation"},
    {"vk_gpu_profile", ENGINE_MENU_DEBUG_VULKAN, "GPU profile"},
    {"vk_buffer_stats", ENGINE_MENU_DEBUG_VULKAN, "Buffer copy stats"},
    {"vk_record", ENGINE_MENU_DEBUG_VULKAN, "Vulkan recording thread"},
    {"compute", ENGINE_MENU_DEBUG_VULKAN, "Compute dispatches"},
    {"vk_max_allocation_mb", ENGINE_MENU_DEBUG_VULKAN, "Vulkan allocation cap"},
    {"vk_budget_mb", ENGINE_MENU_DEBUG_VULKAN, "Vulkan memory budget"},

    {"nvn_trace", ENGINE_MENU_DEBUG_NVN, "NVN trace"},
    {"nvn_legacy", ENGINE_MENU_DEBUG_NVN, "NVN legacy"},
    {"nvn_drawrec", ENGINE_MENU_DEBUG_NVN, "NVN draw record"},
    {"nvn_measure", ENGINE_MENU_DEBUG_NVN, "NVN measure"},
    {"nvn_native_missing", ENGINE_MENU_DEBUG_NVN, "NVN native missing"},
    {"nvn_selftest", ENGINE_MENU_DEBUG_NVN, "NVN selftest"},
    {"nvn_selftest_mutate", ENGINE_MENU_DEBUG_NVN, "NVN selftest mutation"},
    {"nvn_exectest", ENGINE_MENU_DEBUG_NVN, "NVN exectest"},
    {"shader_corpus", ENGINE_MENU_DEBUG_NVN, "Shader corpus"},
    {"gpu_trace_target", ENGINE_MENU_DEBUG_NVN, "GPU trace target"},

    {"mme_hle", ENGINE_MENU_DEBUG_MACROS, "Native macros"},
    {"mme_jit", ENGINE_MENU_DEBUG_MACROS, "Compiled macros"},
    {"mme_verify", ENGINE_MENU_DEBUG_MACROS, "Verify macros"},

    {"profile", ENGINE_MENU_DEBUG_PROFILERS, "Profiler"},
    {"renderdoc", ENGINE_MENU_DEBUG_PROFILERS, "RenderDoc captures"},
    {"renderdoc_frames", ENGINE_MENU_DEBUG_PROFILERS, "RenderDoc frames"},
    {"tracy_profiler", ENGINE_MENU_DEBUG_PROFILERS, "Tracy profiler", true},
    {"tracy_sampling", ENGINE_MENU_DEBUG_PROFILERS, "Tracy sampling", true},
    {"tracy_port", ENGINE_MENU_DEBUG_PROFILERS, "Tracy port", true},
    {"tracy_capture_tool", ENGINE_MENU_DEBUG_PROFILERS, "Tracy capture tool", true},
    {"tracy_open_trace", ENGINE_MENU_DEBUG_PROFILERS, "Tracy open trace", true},
};

constexpr int kEntryCount = static_cast<int>(sizeof kEntries / sizeof kEntries[0]);

} // namespace

const EngineMenuDebugEntry* EngineMenuDebugEntries(int* count) {
    if (count != nullptr) *count = kEntryCount;
    return kEntries;
}

const EngineMenuDebugEntry* EngineMenuDebugEntryOfKey(const char* key) {
    if (key == nullptr) return nullptr;
    for (int i = 0; i < kEntryCount; ++i)
        if (strcmp(kEntries[i].key, key) == 0) return &kEntries[i];
    return nullptr;
}

const EngineMenuDebugEntry* EngineMenuDebugCardEntries(int card, int* count) {
    if (count != nullptr) *count = 0;
    if (card < 0 || card >= ENGINE_MENU_DEBUG_CARD_COUNT) return nullptr;
    int first = -1;
    for (int i = 0; i < kEntryCount; ++i) {
        if (kEntries[i].card != card) continue;
        if (first < 0) first = i;
        if (count != nullptr) *count += 1;
    }
    return first < 0 ? nullptr : &kEntries[first];
}

const char* EngineMenuDebugCardName(int card) {
    if (card < 0 || card >= ENGINE_MENU_DEBUG_CARD_COUNT) return "";
    return kCardNames[card];
}

#ifndef ENGINE_MENU_PURE_TEST

#include <cstdio>
#include <cstdlib>

#include "imgui.h"
#include "nx/gpu/vk/debug_stats.h"
#include "nx/gpu/vk/md3/md3_theme.h"
#include "nx/gpu/vk/md3/md3_widgets.h"
// log.h holds C functions and has no extern "C" guard: a C++ unit needs the block.
extern "C" {
#include "nx/runtime/platform/log.h"
}

namespace {

// GiB in one MiB, and the frame target of the game's pace (the mockup's 30 FPS).
constexpr float kMiBPerGiB = 1024.0f;
constexpr float kGameTargetMs = 1000.0f / 30.0f;
constexpr float kMillisecondsPerSecond = 1000.0f;
constexpr int kValueTextMax = 32;
constexpr int kLineTextMax = 192;
constexpr int kCardIdMax = 48;
constexpr int kMissingRowLogMax = 1;
const char* const kUnknownValue = "\xE2\x80\x94"; // em dash

void MissingRow(const char* key) {
    LOG_LIMIT(kMissingRowLogMax, "engine menu: the Debug page has no row for %s\n", key);
}

void DashedInt(char* out, size_t n, int value, const char* format) {
    if (value < 0)
        std::snprintf(out, n, "%s", kUnknownValue);
    else
        std::snprintf(out, n, format, value);
}

void DashedTenths(char* out, size_t n, float value) {
    if (value < 0.0f)
        std::snprintf(out, n, "%s", kUnknownValue);
    else
        std::snprintf(out, n, "%.1f", static_cast<double>(value));
}

bool ReadDebugRow(const EngineMenuDebugEntry& entry, ImguiSettingRow* row) {
    if (!imgui_settings_row_key(entry.key, row)) {
        if (!entry.build_optional) MissingRow(entry.key);
        return false;
    }
    row->name = entry.label;
    return true;
}

void DrawEntry(const EngineMenuDebugEntry& entry) {
    ImguiSettingRow row{};
    if (!ReadDebugRow(entry, &row)) return;
    EngineMenuDrawDebugSettingRow(row);
}

void DrawRows(int card) {
    int count = 0;
    const EngineMenuDebugEntry* entries = EngineMenuDebugCardEntries(card, &count);
    if (entries == nullptr) return;
    for (int i = 0; i < count; ++i) DrawEntry(entries[i]);
}

void DrawOverlaysCard() {
    md3::CardBegin("##debug_overlays_card");
    md3::SectionTitle(EngineMenuDebugCardName(ENGINE_MENU_DEBUG_OVERLAYS));
    DrawRows(ENGINE_MENU_DEBUG_OVERLAYS);
    md3::CardEnd();
}

void GpuLine(const DebugStats& stats, char* line, size_t n) {
    char load[kValueTextMax], temp[kValueTextMax], power[kValueTextMax];
    DashedInt(load, sizeof load, stats.gpu_load, "%d %%");
    DashedInt(temp, sizeof temp, stats.gpu_temp, "%d \xC2\xB0" "C");
    DashedInt(power, sizeof power, stats.gpu_power, "%d W");
    std::snprintf(line, n, "GPU  %s, %s, %s", load, temp, power);
}

void VramLine(const DebugStats& stats, char* line, size_t n) {
    if (stats.vram_used_mb < 0 || stats.vram_total_mb < 0) {
        std::snprintf(line, n, "VRAM  %s / %s", kUnknownValue, kUnknownValue);
        return;
    }
    std::snprintf(line, n, "VRAM  %.1f / %.1f GB", static_cast<double>(stats.vram_used_mb) / kMiBPerGiB,
                  static_cast<double>(stats.vram_total_mb) / kMiBPerGiB);
}

void CpuLine(const DebugStats& stats, char* line, size_t n) {
    char load[kValueTextMax], temp[kValueTextMax], power[kValueTextMax];
    DashedInt(load, sizeof load, stats.cpu_load, "%d %%");
    DashedInt(temp, sizeof temp, stats.cpu_temp, "%d \xC2\xB0" "C");
    DashedInt(power, sizeof power, stats.cpu_power, "%d W");
    std::snprintf(line, n, "CPU  %s, %s, %s", load, temp, power);
}

void RamLine(const DebugStats& stats, char* line, size_t n) {
    if (stats.ram_used_mb < 0 || stats.ram_total_mb < 0 || stats.game_ram_mb < 0) {
        std::snprintf(line, n, "RAM  %s / %s, game %s", kUnknownValue, kUnknownValue, kUnknownValue);
        return;
    }
    std::snprintf(line, n, "RAM  %.1f / %.1f GB, game %.1f GB", static_cast<double>(stats.ram_used_mb) / kMiBPerGiB,
                  static_cast<double>(stats.ram_total_mb) / kMiBPerGiB,
                  static_cast<double>(stats.game_ram_mb) / kMiBPerGiB);
}

void DrawHardwareCard(const DebugStats& stats) {
    char line[kLineTextMax];
    md3::CardBegin("##debug_hardware_card");
    md3::SectionTitle(EngineMenuDebugCardName(ENGINE_MENU_DEBUG_HARDWARE));
    GpuLine(stats, line, sizeof line);
    md3::BodyText(line);
    VramLine(stats, line, sizeof line);
    md3::BodyText(line);
    CpuLine(stats, line, sizeof line);
    md3::BodyText(line);
    RamLine(stats, line, sizeof line);
    md3::BodyText(line);
    md3::CardEnd();
}

// The frame target of fps_limit's text: the game's pace, a number of frames a
// second, or no line at 0 and Unlimited.
float FrameTargetMs(const char* fps_limit) {
    if (fps_limit == nullptr) return 0.0f;
    if (std::strcmp(fps_limit, "game") == 0) return kGameTargetMs;
    const int fps = std::atoi(fps_limit);
    return fps > 0 ? kMillisecondsPerSecond / static_cast<float>(fps) : 0.0f;
}

void FrameLine(const DebugStats& stats, char* line, size_t n) {
    char fps[kValueTextMax], last[kValueTextMax], max[kValueTextMax];
    DashedTenths(fps, sizeof fps, stats.fps);
    DashedTenths(last, sizeof last, stats.frame_last_ms);
    DashedTenths(max, sizeof max, stats.frame_max_ms);
    std::snprintf(line, n, "%s FPS, last frame %s ms, max %s ms", fps, last, max);
}

void CostLines(const DebugStats& stats, char* prep, size_t prep_n, char* wait, size_t wait_n) {
    char prepared[kValueTextMax], busy[kValueTextMax], waits[kValueTextMax];
    DashedTenths(prepared, sizeof prepared, stats.prep_ms);
    DashedTenths(busy, sizeof busy, stats.cpu_ms);
    DashedTenths(waits, sizeof waits, stats.pipeline_wait_ms);
    std::snprintf(prep, prep_n, "CPU frame prep %s ms, CPU busy %s ms", prepared, busy);
    if (stats.texture_uploads < 0.0f)
        std::snprintf(wait, wait_n, "Pipeline waits %s ms, texture uploads %s", waits, kUnknownValue);
    else
        std::snprintf(wait, wait_n, "Pipeline waits %s ms, texture uploads %d", waits,
                      static_cast<int>(stats.texture_uploads + 0.5f));
}

void DrawFrameCard(const DebugStats& stats, const ImguiFrame* frame) {
    const md3::Theme& theme = md3::CurrentTheme();
    const float s = theme.scale;
    char line[kLineTextMax], prep[kLineTextMax], wait[kLineTextMax];
    md3::CardBegin("##debug_frame_card");
    md3::SectionTitle(EngineMenuDebugCardName(ENGINE_MENU_DEBUG_FRAME));
    FrameLine(stats, line, sizeof line);
    md3::BodyText(line);
    md3::FrameTimeGraph("##debug_frame_graph", stats.frame_ms, stats.frame_count,
                        FrameTargetMs(frame != nullptr ? frame->fps_limit : nullptr),
                        ImVec2(ImGui::GetContentRegionAvail().x, md3::kFrameGraphHeightPx * s));
    CostLines(stats, prep, sizeof prep, wait, sizeof wait);
    md3::BodyText(prep);
    md3::BodyText(wait);
    md3::CardEnd();
}

void GameResolutionLine(const ImguiFrame& frame, char* line, size_t n) {
    if (frame.game_w > 0 && frame.game_h > 0)
        std::snprintf(line, n, "Game resolution %d x %d", frame.game_w, frame.game_h);
    else
        std::snprintf(line, n, "Game resolution %s", kUnknownValue);
}

void DrawDisplayCard(const ImguiFrame& frame) {
    char line[kLineTextMax];
    md3::CardBegin("##debug_display_card");
    md3::SectionTitle(EngineMenuDebugCardName(ENGINE_MENU_DEBUG_DISPLAY));
    std::snprintf(line, sizeof line, "Window %d x %d", frame.window_w, frame.window_h);
    md3::BodyText(line);
    GameResolutionLine(frame, line, sizeof line);
    md3::BodyText(line);
    std::snprintf(line, sizeof line, "Scaling %s", frame.scaling_filter == 1 ? "Bicubic" : "Bilinear");
    md3::BodyText(line);
    const char* anti_aliasing = frame.anti_aliasing == 2 ? "SMAA" : frame.anti_aliasing == 1 ? "FXAA" : "Off";
    std::snprintf(line, sizeof line, "Anti-aliasing %s", anti_aliasing);
    md3::BodyText(line);
    std::snprintf(line, sizeof line, "Frame limit %s", frame.fps_limit != nullptr ? frame.fps_limit : kUnknownValue);
    md3::BodyText(line);
    const char* vsync = frame.vsync == 2 ? "On (Mailbox)" : frame.vsync ? "On (FIFO)" : "Off";
    std::snprintf(line, sizeof line, "V-sync %s", vsync);
    md3::BodyText(line);
    md3::CardEnd();
}

void DrawReadOnlyCards(const ImguiFrame* frame) {
    DebugStats stats{};
    debug_stats_get(&stats);
    ImGui::Spacing();
    DrawHardwareCard(stats);
    ImGui::Spacing();
    DrawFrameCard(stats, frame);
    if (frame == nullptr) return;
    ImGui::Spacing();
    DrawDisplayCard(*frame);
}

void DrawDeveloperCards() {
    static bool open = false;
    md3::ExpandHeader("##debug_developer", "Developer", &open);
    if (!open) return;
    for (int card = ENGINE_MENU_DEBUG_RECOMPILER; card <= ENGINE_MENU_DEBUG_PROFILERS; ++card) {
        char id[kCardIdMax];
        std::snprintf(id, sizeof id, "##debug_group_%d", card);
        ImGui::Spacing();
        md3::CardBegin(id);
        md3::SectionTitle(EngineMenuDebugCardName(card));
        DrawRows(card);
        md3::CardEnd();
    }
}

} // namespace

void EngineMenuDrawDebugPage(const ImguiFrame* frame) {
    DrawOverlaysCard();
    DrawReadOnlyCards(frame);
    ImGui::Spacing();
    md3::CardBegin("##debug_logs_card");
    md3::SectionTitle(EngineMenuDebugCardName(ENGINE_MENU_DEBUG_LOGS));
    DrawRows(ENGINE_MENU_DEBUG_LOGS);
    md3::CardEnd();
    ImGui::Spacing();
    md3::CardBegin("##debug_textures_card");
    md3::SectionTitle(EngineMenuDebugCardName(ENGINE_MENU_DEBUG_TEXTURES));
    DrawRows(ENGINE_MENU_DEBUG_TEXTURES);
    md3::CardEnd();
    ImGui::Spacing();
    DrawDeveloperCards();
}

#endif // ENGINE_MENU_PURE_TEST
