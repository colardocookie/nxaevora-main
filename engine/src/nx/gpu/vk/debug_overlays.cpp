// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// The three debug overlays over the game (debug_overlays.h): the performance
// overlay, the input display and the HUD. They draw only while the engine menu
// is closed and take no input.

#include "nx/gpu/vk/debug_overlays.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "nx/gpu/vk/debug_overlays_layout.h"
#include "nx/gpu/vk/engine_menu.h"
#include "nx/gpu/vk/md3/md3_motion.h"
#include "nx/gpu/vk/md3/md3_theme.h"
#include "nx/gpu/vk/md3/md3_widgets.h"
// The runtime's C headers have no extern "C" guard: a C++ unit needs the block.
extern "C" {
#include "nx/runtime/platform/hud.h"
#include "nx/runtime/platform/stats.h"
#include "nx/runtime/platform/vpad.h"
}

namespace debug_overlay {
namespace {

// Overlay sizes, alphas, durations and counts in pixels at 1080p (or their own
// unit). Each pixel value is multiplied by the theme scale.
constexpr float kOverlayMarginPx = 16.0f;
constexpr float kOverlayGapPx = 8.0f;
constexpr float kOverlayCardAlpha = 0.85f;
constexpr float kOverlayCardPaddingPx = 12.0f;
constexpr float kOverlayCardCornerPx = 12.0f;
constexpr int kOverlayFadeMs = 150;

constexpr float kPerfMinimalWidthPx = 200.0f;
constexpr float kPerfStandardWidthPx = 240.0f;
constexpr float kPerfDetailedWidthPx = 300.0f;
constexpr float kPerfValueColumnPx = 76.0f;
constexpr float kPerfRowGapPx = 3.0f;
constexpr float kPerfGraphGapPx = 4.0f;

constexpr float kShowcaseWidthPx = 220.0f;
constexpr float kShowcaseHeightPx = 120.0f;
constexpr float kShowcaseRingPx = 15.0f;
constexpr float kShowcaseDotPx = 6.0f;
constexpr float kShowcaseTravelPx = 10.0f;
constexpr float kStickLeftXPx = 34.0f;
constexpr float kStickRightXPx = 186.0f;
constexpr float kStickYPx = 92.0f;
constexpr float kDpadCenterXPx = 34.0f;
constexpr float kDpadCenterYPx = 46.0f;
constexpr float kDpadArmPx = 14.0f;
constexpr float kDpadThickPx = 10.0f;
constexpr float kDpadRoundPx = 2.0f;
constexpr float kFaceCenterXPx = 186.0f;
constexpr float kFaceCenterYPx = 46.0f;
constexpr float kFaceRadiusPx = 8.0f;
constexpr float kFaceOffsetPx = 14.0f;
constexpr float kFaceLetterPx = 11.0f;
constexpr float kPillWidthPx = 38.0f;
constexpr float kPillHeightPx = 11.0f;
constexpr float kPillLetterPx = 9.0f;
constexpr float kPillLeftXPx = 8.0f;
constexpr float kPillRightXPx = 174.0f;
constexpr float kPillTopYPx = 2.0f;
constexpr float kPillRowGapPx = 13.0f;
constexpr float kSmallButtonCenterXPx = 98.0f;
constexpr float kSmallButtonCenterYPx = 10.0f;
constexpr float kSmallButtonGapPx = 24.0f;
constexpr float kSmallButtonRadiusPx = 6.0f;
constexpr float kSmallButtonGlyphPx = 11.0f;
constexpr float kShowcaseOutlinePx = 2.0f;

constexpr float kHudMaxWidthRatio = 0.6f;
constexpr float kHudLineGapPx = 2.0f;
constexpr int kHudTextMax = 1024;
constexpr int kHudLineMax = 768;

constexpr int kValueTextMax = 32;
constexpr int kLineTextMax = 192;
constexpr float kMiBPerGiB = 1024.0f;
constexpr float kGameTargetMs = 1000.0f / 30.0f;
constexpr float kMillisecondsPerSecond = 1000.0f;
const char* const kUnknownValue = "\xE2\x80\x94"; // em dash

// The HidNpadButton bits of the controls that the input display shows.
constexpr unsigned kButtonA = 1u << 0;
constexpr unsigned kButtonB = 1u << 1;
constexpr unsigned kButtonX = 1u << 2;
constexpr unsigned kButtonY = 1u << 3;
constexpr unsigned kButtonL = 1u << 6;
constexpr unsigned kButtonR = 1u << 7;
constexpr unsigned kButtonZL = 1u << 8;
constexpr unsigned kButtonZR = 1u << 9;
constexpr unsigned kButtonPlus = 1u << 10;
constexpr unsigned kButtonMinus = 1u << 11;
constexpr unsigned kButtonLeft = 1u << 12;
constexpr unsigned kButtonUp = 1u << 13;
constexpr unsigned kButtonRight = 1u << 14;
constexpr unsigned kButtonDown = 1u << 15;

// The tween keys of the overlay fades. The high range keeps them away from the
// widget keys, which mix a widget id with a salt.
constexpr md3::TweenId kPerfFadeTweenId = 0x4D4F0001u;
constexpr md3::TweenId kInputFadeTweenId = 0x4D4F0002u;
constexpr md3::TweenId kHudFadeTweenId = 0x4D4F0003u;

struct InputState {
    bool a, b, x, y, l, r, zl, zr, minus, plus, up, down, left, right;
    float lx, ly, rx, ry; // right and down positive
};

float g_perf_alpha;
float g_input_alpha;
float g_hud_alpha;
int g_perf_last_level;
bool g_fades_started;
char g_hud_text[kHudTextMax];
int g_hud_lines;

void EncodeUtf8(uint32_t codepoint, char out[5]) {
    if (codepoint < 0x80u) {
        out[0] = static_cast<char>(codepoint);
        out[1] = 0;
    } else if (codepoint < 0x800u) {
        out[0] = static_cast<char>(0xC0u | (codepoint >> 6));
        out[1] = static_cast<char>(0x80u | (codepoint & 0x3Fu));
        out[2] = 0;
    } else if (codepoint < 0x10000u) {
        out[0] = static_cast<char>(0xE0u | (codepoint >> 12));
        out[1] = static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu));
        out[2] = static_cast<char>(0x80u | (codepoint & 0x3Fu));
        out[3] = 0;
    } else {
        out[0] = static_cast<char>(0xF0u | (codepoint >> 18));
        out[1] = static_cast<char>(0x80u | ((codepoint >> 12) & 0x3Fu));
        out[2] = static_cast<char>(0x80u | ((codepoint >> 6) & 0x3Fu));
        out[3] = static_cast<char>(0x80u | (codepoint & 0x3Fu));
        out[4] = 0;
    }
}

float TextWidth(const char* text, float px) {
    ImFont* font = ImGui::GetFont();
    if (font == nullptr || text == nullptr) return 0.0f;
    return font->CalcTextSizeA(px, FLT_MAX, 0.0f, text).x;
}

// The width function of the HUD wrap: the text at the font size in ctx.
float HudTextWidth(const char* text, void* ctx) { return TextWidth(text, *static_cast<const float*>(ctx)); }

ImVec2 TextSize(const char* text, float px) {
    ImFont* font = ImGui::GetFont();
    if (font == nullptr || text == nullptr) return ImVec2(0.0f, px);
    return font->CalcTextSizeA(px, FLT_MAX, 0.0f, text);
}

void TextAt(ImDrawList* draw, const char* text, ImVec2 pos, float px, ImU32 color, float alpha) {
    ImFont* font = ImGui::GetFont();
    if (draw == nullptr || font == nullptr || text == nullptr) return;
    draw->AddText(font, px, pos, md3::FadeColor(color, alpha), text);
}

void TextRightAt(ImDrawList* draw, const char* text, float right, float y, float px, ImU32 color, float alpha) {
    TextAt(draw, text, ImVec2(right - TextWidth(text, px), y), px, color, alpha);
}

void TextCentered(ImDrawList* draw, const char* text, const ImVec2& center, float px, ImU32 color, float alpha) {
    const ImVec2 size = TextSize(text, px);
    TextAt(draw, text, ImVec2(center.x - size.x * 0.5f, center.y - size.y * 0.5f), px, color, alpha);
}

// Calls visit for each line of a wrapped HUD text, without the "\n".
template <typename Visit>
void ForEachHudLine(const char* text, Visit visit) {
    const char* cursor = text;
    while (cursor != nullptr && *cursor != '\0') {
        const char* end = std::strchr(cursor, '\n');
        const size_t length = end != nullptr ? static_cast<size_t>(end - cursor) : std::strlen(cursor);
        char buffer[kHudLineMax];
        const size_t copy = length < sizeof(buffer) - 1 ? length : sizeof(buffer) - 1;
        std::memcpy(buffer, cursor, copy);
        buffer[copy] = 0;
        visit(buffer);
        cursor = end != nullptr ? end + 1 : cursor + length;
    }
}

void DrawOverlayCard(ImDrawList* draw, const ImVec2& pos, const ImVec2& size, float alpha, uint32_t color) {
    const float corner = kOverlayCardCornerPx * md3::CurrentTheme().scale;
    draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                        md3::FadeColor(md3::ToImU32(color), kOverlayCardAlpha * alpha), corner);
}

void BeginOverlayWindow(const char* id, const ImVec2& pos, const ImVec2& size) {
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
    ImGui::Begin(id, nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground);
}

void EndOverlayWindow() {
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

void DrawPerfRow(ImDrawList* draw, const ImVec2& card_pos, const ImVec2& card_size, float y, float row_height,
                 float scale, float alpha, const char* label, const char* first, const char* second) {
    const md3::Theme& theme = md3::CurrentTheme();
    const float pad = kOverlayCardPaddingPx * scale;
    const float right2 = card_pos.x + card_size.x - pad;
    const float right1 = right2 - kPerfValueColumnPx * scale;
    const ImVec2 label_size = TextSize(label, theme.body_medium);
    TextAt(draw, label, ImVec2(card_pos.x + pad, y + (row_height - label_size.y) * 0.5f), theme.body_medium,
           md3::ToImU32(theme.scheme.on_surface_variant), alpha);
    const float value_y = y + (row_height - theme.label_large) * 0.5f;
    if (first != nullptr)
        TextRightAt(draw, first, right1, value_y, theme.label_large, md3::ToImU32(theme.scheme.on_surface), alpha);
    if (second != nullptr)
        TextRightAt(draw, second, right2, value_y, theme.label_large, md3::ToImU32(theme.scheme.on_surface), alpha);
}

void PerfCardSize(int level, float scale, float* width, float* height) {
    const float pad = kOverlayCardPaddingPx * scale;
    const float row_height = md3::CurrentTheme().label_large;
    const int rows = PerfRowCount(level);
    const float base_width = level == kLevelMinimal   ? kPerfMinimalWidthPx
                             : level == kLevelStandard ? kPerfStandardWidthPx
                                                       : kPerfDetailedWidthPx;
    if (width != nullptr) *width = base_width * scale;
    if (height == nullptr) return;
    float value = 2.0f * pad + static_cast<float>(rows) * row_height;
    if (rows > 1) value += static_cast<float>(rows - 1) * kPerfRowGapPx * scale;
    if (level >= kLevelStandard) value += md3::kFrameGraphHeightPx * scale + kPerfGraphGapPx * scale;
    *height = value;
}

void ShowcaseCardSize(float scale, float* width, float* height) {
    const float pad = kOverlayCardPaddingPx * scale;
    if (width != nullptr) *width = kShowcaseWidthPx * scale + 2.0f * pad;
    if (height != nullptr) *height = kShowcaseHeightPx * scale + 2.0f * pad;
}

void GbTenths(char* out, size_t size, int megabytes) {
    DashedTenths(out, size, megabytes < 0 ? -1.0f : static_cast<float>(megabytes) / kMiBPerGiB);
}

void FrameTimes(char* out, size_t size, const DebugStats& stats) {
    char last[kValueTextMax], max[kValueTextMax];
    DashedTenths(last, sizeof last, stats.frame_last_ms);
    DashedTenths(max, sizeof max, stats.frame_max_ms);
    std::snprintf(out, size, "%s/%s ms", last, max);
}

void Uploads(char* out, size_t size, float uploads) {
    if (uploads < 0.0f)
        std::snprintf(out, size, "%s", kUnknownValue);
    else
        std::snprintf(out, size, "%d", static_cast<int>(uploads + 0.5f));
}

void ResolutionLine(char* out, size_t size, const ImguiFrame& frame) {
    if (frame.game_w > 0 && frame.game_h > 0)
        std::snprintf(out, size, "%d x %d", frame.game_w, frame.game_h);
    else
        std::snprintf(out, size, "%s", kUnknownValue);
}

ImDrawList* DrawPerformanceWindow(const OverlayRect& rect, const ImVec2& viewport_pos, float alpha,
                                  const ImguiFrame& frame) {
    const md3::Theme& theme = md3::CurrentTheme();
    const float s = theme.scale;
    const int level = g_perf_last_level;
    DebugStats stats{};
    debug_stats_get(&stats);
    const ImVec2 pos(viewport_pos.x + rect.x, viewport_pos.y + rect.y);
    const ImVec2 size(rect.w, rect.h);
    BeginOverlayWindow("##performance_overlay", pos, size);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (!ImGui::IsWindowCollapsed()) {
        const ImVec2 origin = ImGui::GetWindowPos();
        DrawOverlayCard(draw, origin, size, alpha, theme.scheme.surface_container);

        const float pad = kOverlayCardPaddingPx * s;
        const float row_height = theme.label_large;
        float y = origin.y + pad;
        char first[kLineTextMax], second[kLineTextMax];

        char fps[kValueTextMax], last[kValueTextMax], times[kLineTextMax], values[kLineTextMax];
        DashedTenths(fps, sizeof fps, stats.fps);
        DashedTenths(last, sizeof last, stats.frame_last_ms);
        if (level >= kLevelStandard) {
            /* the standard levels show the last and the max frame time in one right column */
            FrameTimes(times, sizeof times, stats);
            std::snprintf(values, sizeof values, "%s  %s", fps, times);
            DrawPerfRow(draw, origin, size, y, row_height, s, alpha, "FPS", nullptr, values);
        } else {
            std::snprintf(values, sizeof values, "%s ms", last);
            DrawPerfRow(draw, origin, size, y, row_height, s, alpha, "FPS", fps, values);
        }
        y += row_height + kPerfGraphGapPx * s;

        if (level >= kLevelStandard) {
            const float graph_width = size.x - 2.0f * pad;
            const ImVec2 graph_pos(origin.x + pad, y);
            ImGui::SetCursorScreenPos(graph_pos);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
            md3::FrameTimeGraph("##overlay_frame_graph", stats.frame_ms, stats.frame_count,
                                FrameTargetMs(frame.fps_limit),
                                ImVec2(graph_width, md3::kFrameGraphHeightPx * s));
            ImGui::PopStyleVar();
            y += md3::kFrameGraphHeightPx * s + kPerfRowGapPx * s;
        }

        if (level >= kLevelStandard) {
            char cpu_load[kValueTextMax], gpu_load[kValueTextMax];
            DashedInt(cpu_load, sizeof cpu_load, stats.cpu_load, "%d %%");
            DashedInt(gpu_load, sizeof gpu_load, stats.gpu_load, "%d %%");
            std::snprintf(first, sizeof first, "CPU %s", cpu_load);
            std::snprintf(second, sizeof second, "GPU %s", gpu_load);
            DrawPerfRow(draw, origin, size, y, row_height, s, alpha, "Load", first, second);
            y += row_height + kPerfRowGapPx * s;

            char ram[kValueTextMax], vram[kValueTextMax];
            GbTenths(ram, sizeof ram, stats.ram_used_mb);
            GbTenths(vram, sizeof vram, stats.vram_used_mb);
            std::snprintf(first, sizeof first, "RAM %s GB", ram);
            std::snprintf(second, sizeof second, "VRAM %s GB", vram);
            DrawPerfRow(draw, origin, size, y, row_height, s, alpha, "Memory", first, second);
            y += row_height + kPerfRowGapPx * s;
        }

        if (level == kLevelDetailed) {
            char gpu_temp[kValueTextMax], cpu_temp[kValueTextMax];
            DashedInt(gpu_temp, sizeof gpu_temp, stats.gpu_temp, "%d \xC2\xB0" "C");
            DashedInt(cpu_temp, sizeof cpu_temp, stats.cpu_temp, "%d \xC2\xB0" "C");
            std::snprintf(first, sizeof first, "GPU %s", gpu_temp);
            std::snprintf(second, sizeof second, "CPU %s", cpu_temp);
            DrawPerfRow(draw, origin, size, y, row_height, s, alpha, "Temperature", first, second);
            y += row_height + kPerfRowGapPx * s;

            char gpu_power[kValueTextMax], cpu_power[kValueTextMax];
            DashedInt(gpu_power, sizeof gpu_power, stats.gpu_power, "%d W");
            DashedInt(cpu_power, sizeof cpu_power, stats.cpu_power, "%d W");
            std::snprintf(first, sizeof first, "GPU %s", gpu_power);
            std::snprintf(second, sizeof second, "CPU %s", cpu_power);
            DrawPerfRow(draw, origin, size, y, row_height, s, alpha, "Power", first, second);
            y += row_height + kPerfRowGapPx * s;

            char prep[kValueTextMax], busy[kValueTextMax];
            DashedTenths(prep, sizeof prep, stats.prep_ms);
            DashedTenths(busy, sizeof busy, stats.cpu_ms);
            std::snprintf(first, sizeof first, "%s ms", prep);
            std::snprintf(second, sizeof second, "%s ms", busy);
            DrawPerfRow(draw, origin, size, y, row_height, s, alpha, "Prep / busy", first, second);
            y += row_height + kPerfRowGapPx * s;

            char waits[kValueTextMax], uploads[kValueTextMax];
            DashedTenths(waits, sizeof waits, stats.pipeline_wait_ms);
            Uploads(uploads, sizeof uploads, stats.texture_uploads);
            std::snprintf(first, sizeof first, "%s ms", waits);
            std::snprintf(second, sizeof second, "%s", uploads);
            DrawPerfRow(draw, origin, size, y, row_height, s, alpha, "Waits / uploads", first, second);
            y += row_height + kPerfRowGapPx * s;

            ResolutionLine(second, sizeof second, frame);
            DrawPerfRow(draw, origin, size, y, row_height, s, alpha, "Render", nullptr, second);
            y += row_height + kPerfRowGapPx * s;

            DrawPerfRow(draw, origin, size, y, row_height, s, alpha, "Scaling", nullptr,
                        frame.scaling_filter == 1 ? "Bicubic" : "Bilinear");
            y += row_height + kPerfRowGapPx * s;

            const char* anti_aliasing =
                frame.anti_aliasing == 2 ? "SMAA" : frame.anti_aliasing == 1 ? "FXAA" : "Off";
            DrawPerfRow(draw, origin, size, y, row_height, s, alpha, "Anti-aliasing", nullptr, anti_aliasing);
            y += row_height + kPerfRowGapPx * s;

            DrawPerfRow(draw, origin, size, y, row_height, s, alpha, "Frame limit", nullptr,
                        frame.fps_limit != nullptr ? frame.fps_limit : kUnknownValue);
            y += row_height + kPerfRowGapPx * s;

            const char* vsync = frame.vsync == 2 ? "On (Mailbox)" : frame.vsync ? "On (FIFO)" : "Off";
            DrawPerfRow(draw, origin, size, y, row_height, s, alpha, "V-sync", nullptr, vsync);
        }
    }
    EndOverlayWindow();
    return draw;
}

void DrawPartBox(ImDrawList* draw, const ImVec2& min, const ImVec2& max, float rounding, bool pressed, float alpha,
                 float scale, const char* letter, float letter_px) {
    const md3::Theme& theme = md3::CurrentTheme();
    const ImU32 primary = md3::ToImU32(theme.scheme.primary);
    const ImU32 outline = md3::ToImU32(theme.scheme.outline_variant);
    if (pressed) {
        draw->AddRectFilled(min, max, md3::FadeColor(primary, alpha), rounding);
    } else {
        draw->AddRect(min, max, md3::FadeColor(outline, alpha), rounding, 0, kShowcaseOutlinePx * scale);
    }
    if (letter != nullptr) {
        const ImU32 color =
            pressed ? md3::ToImU32(theme.scheme.on_primary) : md3::ToImU32(theme.scheme.on_surface_variant);
        TextCentered(draw, letter, ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f), letter_px, color, alpha);
    }
}

void DrawPartCircle(ImDrawList* draw, const ImVec2& center, float radius, bool pressed, float alpha, float scale,
                    const char* letter, float letter_px) {
    const md3::Theme& theme = md3::CurrentTheme();
    const ImU32 primary = md3::ToImU32(theme.scheme.primary);
    const ImU32 outline = md3::ToImU32(theme.scheme.outline_variant);
    if (pressed) {
        draw->AddCircleFilled(center, radius, md3::FadeColor(primary, alpha), 0);
    } else {
        draw->AddCircle(center, radius, md3::FadeColor(outline, alpha), 0, kShowcaseOutlinePx * scale);
    }
    if (letter != nullptr) {
        const ImU32 color =
            pressed ? md3::ToImU32(theme.scheme.on_primary) : md3::ToImU32(theme.scheme.on_surface_variant);
        TextCentered(draw, letter, center, letter_px, color, alpha);
    }
}

void DrawInputContent(ImDrawList* draw, const ImVec2& origin, float scale, const InputState& in, float alpha) {
    const md3::Theme& theme = md3::CurrentTheme();
    const ImU32 outline = md3::ToImU32(theme.scheme.outline_variant);
    const auto at = [&](float x, float y) { return ImVec2(origin.x + x * scale, origin.y + y * scale); };

    const ImVec2 left_center = at(kStickLeftXPx, kStickYPx);
    const ImVec2 right_center = at(kStickRightXPx, kStickYPx);
    const float ring = kShowcaseRingPx * scale;
    const float dot = kShowcaseDotPx * scale;
    const float travel = kShowcaseTravelPx * scale;
    draw->AddCircle(left_center, ring, md3::FadeColor(outline, alpha), 0, kShowcaseOutlinePx * scale);
    draw->AddCircle(right_center, ring, md3::FadeColor(outline, alpha), 0, kShowcaseOutlinePx * scale);
    const ImVec2 left_dot(left_center.x + in.lx * travel, left_center.y + in.ly * travel);
    const ImVec2 right_dot(right_center.x + in.rx * travel, right_center.y + in.ry * travel);
    DrawPartCircle(draw, left_dot, dot, in.lx * in.lx + in.ly * in.ly > 0.09f, alpha, scale, nullptr, 0.0f);
    DrawPartCircle(draw, right_dot, dot, in.rx * in.rx + in.ry * in.ry > 0.09f, alpha, scale, nullptr, 0.0f);

    const float dpad_x = kDpadCenterXPx;
    const float dpad_y = kDpadCenterYPx;
    const float arm = kDpadArmPx;
    const float thick = kDpadThickPx;
    const float half = thick * 0.5f;
    const float rounding = kDpadRoundPx * scale;
    DrawPartBox(draw, at(dpad_x - half, dpad_y - arm), at(dpad_x + half, dpad_y - 2.0f), rounding, in.up, alpha,
                scale, nullptr, 0.0f);
    DrawPartBox(draw, at(dpad_x - half, dpad_y + 2.0f), at(dpad_x + half, dpad_y + arm), rounding, in.down, alpha,
                scale, nullptr, 0.0f);
    DrawPartBox(draw, at(dpad_x - arm, dpad_y - half), at(dpad_x - 2.0f, dpad_y + half), rounding, in.left, alpha,
                scale, nullptr, 0.0f);
    DrawPartBox(draw, at(dpad_x + 2.0f, dpad_y - half), at(dpad_x + arm, dpad_y + half), rounding, in.right, alpha,
                scale, nullptr, 0.0f);

    const float face = kFaceOffsetPx;
    const float face_r = kFaceRadiusPx * scale;
    DrawPartCircle(draw, at(kFaceCenterXPx, kFaceCenterYPx - face), face_r, in.x, alpha, scale, "X",
                   kFaceLetterPx * scale);
    DrawPartCircle(draw, at(kFaceCenterXPx - face, kFaceCenterYPx), face_r, in.y, alpha, scale, "Y",
                   kFaceLetterPx * scale);
    DrawPartCircle(draw, at(kFaceCenterXPx + face, kFaceCenterYPx), face_r, in.a, alpha, scale, "A",
                   kFaceLetterPx * scale);
    DrawPartCircle(draw, at(kFaceCenterXPx, kFaceCenterYPx + face), face_r, in.b, alpha, scale, "B",
                   kFaceLetterPx * scale);

    const float pill_height = kPillHeightPx * scale;
    const float pill_round = pill_height * 0.5f;
    const float letter_px = kPillLetterPx * scale;
    DrawPartBox(draw, at(kPillLeftXPx, kPillTopYPx), at(kPillLeftXPx + kPillWidthPx, kPillTopYPx + kPillHeightPx),
                pill_round, in.zl, alpha, scale, "ZL", letter_px);
    DrawPartBox(draw, at(kPillLeftXPx, kPillTopYPx + kPillRowGapPx),
                at(kPillLeftXPx + kPillWidthPx, kPillTopYPx + kPillRowGapPx + kPillHeightPx), pill_round, in.l, alpha,
                scale, "L", letter_px);
    DrawPartBox(draw, at(kPillRightXPx, kPillTopYPx), at(kPillRightXPx + kPillWidthPx, kPillTopYPx + kPillHeightPx),
                pill_round, in.zr, alpha, scale, "ZR", letter_px);
    DrawPartBox(draw, at(kPillRightXPx, kPillTopYPx + kPillRowGapPx),
                at(kPillRightXPx + kPillWidthPx, kPillTopYPx + kPillRowGapPx + kPillHeightPx), pill_round, in.r, alpha,
                scale, "R", letter_px);

    const float small_r = kSmallButtonRadiusPx * scale;
    DrawPartCircle(draw, at(kSmallButtonCenterXPx, kSmallButtonCenterYPx), small_r, in.minus, alpha, scale, "-",
                   kSmallButtonGlyphPx * scale);
    DrawPartCircle(draw, at(kSmallButtonCenterXPx + kSmallButtonGapPx, kSmallButtonCenterYPx), small_r, in.plus, alpha,
                   scale, "+", kSmallButtonGlyphPx * scale);
}

InputState ReadInput() {
    SnxVpadShowcaseInput state{};
    vpad_showcase_state(&state);
    InputState in{};
    in.a = (state.buttons & kButtonA) != 0;
    in.b = (state.buttons & kButtonB) != 0;
    in.x = (state.buttons & kButtonX) != 0;
    in.y = (state.buttons & kButtonY) != 0;
    in.l = (state.buttons & kButtonL) != 0;
    in.r = (state.buttons & kButtonR) != 0;
    in.zl = (state.buttons & kButtonZL) != 0;
    in.zr = (state.buttons & kButtonZR) != 0;
    in.minus = (state.buttons & kButtonMinus) != 0;
    in.plus = (state.buttons & kButtonPlus) != 0;
    in.up = (state.buttons & kButtonUp) != 0;
    in.down = (state.buttons & kButtonDown) != 0;
    in.left = (state.buttons & kButtonLeft) != 0;
    in.right = (state.buttons & kButtonRight) != 0;
    in.lx = state.lx;
    in.ly = state.ly;
    in.rx = state.rx;
    in.ry = state.ry;
    return in;
}

ImDrawList* DrawInputWindow(const OverlayRect& rect, const ImVec2& viewport_pos, float alpha) {
    const md3::Theme& theme = md3::CurrentTheme();
    const float s = theme.scale;
    const ImVec2 pos(viewport_pos.x + rect.x, viewport_pos.y + rect.y);
    const ImVec2 size(rect.w, rect.h);
    BeginOverlayWindow("##input_overlay", pos, size);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (!ImGui::IsWindowCollapsed()) {
        const ImVec2 origin = ImGui::GetWindowPos();
        DrawOverlayCard(draw, origin, size, alpha, theme.scheme.surface_container);
        const float pad = kOverlayCardPaddingPx * s;
        const InputState input = ReadInput();
        DrawInputContent(draw, ImVec2(origin.x + pad, origin.y + pad), s, input, alpha);
    }
    EndOverlayWindow();
    return draw;
}

void HudSize(float scale, int lines, float text_width, float* width, float* height) {
    const md3::Theme& theme = md3::CurrentTheme();
    const float pad = kOverlayCardPaddingPx * scale;
    const float icon = md3::kIconSizePx * scale;
    const float line_height = std::max(theme.label_large, icon) + kHudLineGapPx * scale;
    if (width != nullptr) *width = 2.0f * pad + icon + kOverlayGapPx * scale + text_width;
    if (height != nullptr) *height = 2.0f * pad + static_cast<float>(lines) * line_height;
}

ImDrawList* DrawHudWindow(const OverlayRect& rect, const ImVec2& viewport_pos, float alpha) {
    const md3::Theme& theme = md3::CurrentTheme();
    const float s = theme.scale;
    const ImVec2 pos(viewport_pos.x + rect.x, viewport_pos.y + rect.y);
    const ImVec2 size(rect.w, rect.h);
    BeginOverlayWindow("##debug_hud", pos, size);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (!ImGui::IsWindowCollapsed()) {
        const ImVec2 origin = ImGui::GetWindowPos();
        DrawOverlayCard(draw, origin, size, alpha, theme.scheme.tertiary_container);

        const float pad = kOverlayCardPaddingPx * s;
        const float icon = md3::kIconSizePx * s;
        const float line_height = std::max(theme.label_large, icon) + kHudLineGapPx * s;
        char glyph[5];
        EncodeUtf8(md3::icon::kDebug, glyph);
        const ImU32 color = md3::ToImU32(theme.scheme.on_tertiary_container);
        TextAt(draw, glyph, ImVec2(origin.x + pad, origin.y + pad + (line_height - icon) * 0.5f), icon, color, alpha);

        const float text_x = origin.x + pad + icon + kOverlayGapPx * s;
        float line_y = origin.y + pad;
        ForEachHudLine(g_hud_text, [&](const char* line) {
            TextAt(draw, line, ImVec2(text_x, line_y), theme.label_large, color, alpha);
            line_y += line_height;
        });
    }
    EndOverlayWindow();
    return draw;
}

void UpdateFades(bool panel_closed) {
    if (!g_fades_started) {
        md3::ResetTween(kPerfFadeTweenId, 0.0f);
        md3::ResetTween(kInputFadeTweenId, 0.0f);
        md3::ResetTween(kHudFadeTweenId, 0.0f);
        g_fades_started = true;
    }
    const int level = stats_level();
    if (level != kLevelOff) g_perf_last_level = level;
    char list[HUD_DEBUG_MAX];
    hud_debug_get(list, sizeof list);
    const bool perf_on = level != kLevelOff;
    const bool input_on = __atomic_load_n(&g_settings.showcase, __ATOMIC_RELAXED) != 0;
    const bool hud_on = list[0] != '\0';
    const float perf_target = (perf_on && panel_closed) ? 1.0f : 0.0f;
    const float input_target = (input_on && panel_closed) ? 1.0f : 0.0f;
    const float hud_target = (hud_on && panel_closed) ? 1.0f : 0.0f;
    g_perf_alpha = md3::Tween(kPerfFadeTweenId, perf_target, kOverlayFadeMs, md3::kCurveStandard);
    g_input_alpha = md3::Tween(kInputFadeTweenId, input_target, kOverlayFadeMs, md3::kCurveStandard);
    g_hud_alpha = md3::Tween(kHudFadeTweenId, hud_target, kOverlayFadeMs, md3::kCurveStandard);
}

} // namespace

void Reset() {
    g_perf_alpha = 0.0f;
    g_input_alpha = 0.0f;
    g_hud_alpha = 0.0f;
    g_perf_last_level = kLevelMinimal;
    g_fades_started = false;
    g_hud_text[0] = 0;
    g_hud_lines = 0;
}

OverlayDrawLists Draw(const ImguiFrame* frame, bool panel_closed) {
    OverlayDrawLists out{};
    if (frame == nullptr) return out;

    const ImGuiStyle saved = ImGui::GetStyle();
    md3::ApplyTheme(md3::SchemeFromSeed(engine_menu_scheme_seed()),
                    md3::UiScale(frame->window_h, frame->display_scale));
    md3::SetReduceMotion(engine_menu_reduce_motion());

    UpdateFades(panel_closed);

    const bool perf_shown = g_perf_alpha > 0.0f;
    const bool input_shown = g_input_alpha > 0.0f;
    const bool hud_shown = g_hud_alpha > 0.0f;
    if (!perf_shown && !input_shown && !hud_shown) {
        ImGui::GetStyle() = saved;
        return out;
    }

    const md3::Theme& theme = md3::CurrentTheme();
    const float s = theme.scale;
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 viewport_pos = viewport->Pos;
    const ImVec2 viewport_size = viewport->Size;
    const float margin = kOverlayMarginPx * s;
    const float gap = kOverlayGapPx * s;

    OverlayPlacementInput in{};
    in.margin = margin;
    in.gap = gap;
    in.screen_w = viewport_size.x;
    in.screen_h = viewport_size.y;
    in.perf_shown = perf_shown;
    in.input_shown = input_shown;
    in.hud_shown = hud_shown;
    in.perf_corner = stats_corner();
    in.input_corner = vpad_showcase_corner();
    if (perf_shown) PerfCardSize(g_perf_last_level, s, &in.perf_w, &in.perf_h);
    if (input_shown) ShowcaseCardSize(s, &in.input_w, &in.input_h);
    if (hud_shown) {
        char list[HUD_DEBUG_MAX];
        hud_debug_get(list, sizeof list);
        if (list[0] != '\0') {
            const float max_width = kHudMaxWidthRatio * viewport_size.x;
            float text_px = theme.label_large;
            g_hud_lines = HudWrapText(list, max_width, HudTextWidth, &text_px, g_hud_text, sizeof g_hud_text);
        }
        float widest = 0.0f;
        ForEachHudLine(g_hud_text, [&](const char* line) {
            const float width = TextWidth(line, theme.label_large);
            if (width > widest) widest = width;
        });
        HudSize(s, g_hud_lines, widest, &in.hud_w, &in.hud_h);
        const float limit = viewport_size.x - 2.0f * margin;
        if (in.hud_w > limit) in.hud_w = limit;
    }

    const OverlayPlacement placement = PlaceOverlays(in);
    out.count = 0;
    if (perf_shown)
        out.lists[out.count++] = DrawPerformanceWindow(placement.perf, viewport_pos, g_perf_alpha, *frame);
    if (input_shown) out.lists[out.count++] = DrawInputWindow(placement.input, viewport_pos, g_input_alpha);
    if (hud_shown) out.lists[out.count++] = DrawHudWindow(placement.hud, viewport_pos, g_hud_alpha);

    ImGui::GetStyle() = saved;
    return out;
}

void DashedInt(char* out, size_t size, int value, const char* format) {
    if (value < 0)
        std::snprintf(out, size, "%s", kUnknownValue);
    else
        std::snprintf(out, size, format, value);
}

void DashedTenths(char* out, size_t size, float value) {
    if (value < 0.0f)
        std::snprintf(out, size, "%s", kUnknownValue);
    else
        std::snprintf(out, size, "%.1f", static_cast<double>(value));
}

void GpuLine(const DebugStats& stats, char* line, size_t size) {
    char load[kValueTextMax], temp[kValueTextMax], power[kValueTextMax];
    DashedInt(load, sizeof load, stats.gpu_load, "%d %%");
    DashedInt(temp, sizeof temp, stats.gpu_temp, "%d \xC2\xB0" "C");
    DashedInt(power, sizeof power, stats.gpu_power, "%d W");
    std::snprintf(line, size, "GPU  %s, %s, %s", load, temp, power);
}

void VramLine(const DebugStats& stats, char* line, size_t size) {
    if (stats.vram_used_mb < 0 || stats.vram_total_mb < 0) {
        std::snprintf(line, size, "VRAM  %s / %s", kUnknownValue, kUnknownValue);
        return;
    }
    std::snprintf(line, size, "VRAM  %.1f / %.1f GB", static_cast<double>(stats.vram_used_mb) / kMiBPerGiB,
                  static_cast<double>(stats.vram_total_mb) / kMiBPerGiB);
}

void CpuLine(const DebugStats& stats, char* line, size_t size) {
    char load[kValueTextMax], temp[kValueTextMax], power[kValueTextMax];
    DashedInt(load, sizeof load, stats.cpu_load, "%d %%");
    DashedInt(temp, sizeof temp, stats.cpu_temp, "%d \xC2\xB0" "C");
    DashedInt(power, sizeof power, stats.cpu_power, "%d W");
    std::snprintf(line, size, "CPU  %s, %s, %s", load, temp, power);
}

void RamLine(const DebugStats& stats, char* line, size_t size) {
    if (stats.ram_used_mb < 0 || stats.ram_total_mb < 0 || stats.game_ram_mb < 0) {
        std::snprintf(line, size, "RAM  %s / %s, game %s", kUnknownValue, kUnknownValue, kUnknownValue);
        return;
    }
    std::snprintf(line, size, "RAM  %.1f / %.1f GB, game %.1f GB", static_cast<double>(stats.ram_used_mb) / kMiBPerGiB,
                  static_cast<double>(stats.ram_total_mb) / kMiBPerGiB,
                  static_cast<double>(stats.game_ram_mb) / kMiBPerGiB);
}

void FrameLine(const DebugStats& stats, char* line, size_t size) {
    char fps[kValueTextMax], last[kValueTextMax], max[kValueTextMax];
    DashedTenths(fps, sizeof fps, stats.fps);
    DashedTenths(last, sizeof last, stats.frame_last_ms);
    DashedTenths(max, sizeof max, stats.frame_max_ms);
    std::snprintf(line, size, "%s FPS, last frame %s ms, max %s ms", fps, last, max);
}

float FrameTargetMs(const char* fps_limit) {
    if (fps_limit == nullptr) return 0.0f;
    if (std::strcmp(fps_limit, "game") == 0) return kGameTargetMs;
    const int fps = std::atoi(fps_limit);
    return fps > 0 ? kMillisecondsPerSecond / static_cast<float>(fps) : 0.0f;
}

} // namespace debug_overlay
