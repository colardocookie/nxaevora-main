// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// The engine menu shell (docs/dev/ui/engine-menu.md). The open state, the
// choice of the menu of a run, the gamepad mapping and the finger pointer
// have no ImGui state of their own, so the pure test links this file with
// ENGINE_MENU_PURE_TEST.

#include "nx/gpu/vk/engine_menu.h"

#include <SDL3/SDL_gamepad.h>
#include <cmath>
#include <cstring>

#include "imgui.h"

namespace {

constexpr float kStickDeadzone = 0.5f;
constexpr float kTriggerDown = 0.5f;

float AxisMagnitude(int value) {
    const float magnitude = std::fabs(static_cast<float>(value)) / 32767.0f;
    return magnitude > 1.0f ? 1.0f : magnitude;
}

// Fills the two events of a stick axis: out[0] toward the positive side, out[1] the other.
int Stick(int positive, float magnitude, int positive_key, int negative_key, EngineMenuPadEvent out[2]) {
    out[0] = {positive_key, 0, 0.0f};
    out[1] = {negative_key, 0, 0.0f};
    if (magnitude < kStickDeadzone) return 2;
    EngineMenuPadEvent& active = positive ? out[0] : out[1];
    active.down = 1;
    active.value = magnitude;
    return 2;
}

// The physical face positions, so gamepad_swap_ab does not apply in the menu.
int PadButton(int control, EngineMenuPadEvent out[2]) {
    int key = -1;
    switch (control) {
        case SDL_GAMEPAD_BUTTON_SOUTH: key = ImGuiKey_GamepadFaceDown; break;
        case SDL_GAMEPAD_BUTTON_EAST: key = ImGuiKey_GamepadFaceRight; break;
        case SDL_GAMEPAD_BUTTON_WEST: key = ImGuiKey_GamepadFaceLeft; break;
        case SDL_GAMEPAD_BUTTON_NORTH: key = ImGuiKey_GamepadFaceUp; break;
        case SDL_GAMEPAD_BUTTON_BACK: key = ImGuiKey_GamepadBack; break;
        case SDL_GAMEPAD_BUTTON_START: key = ImGuiKey_GamepadStart; break;
        case SDL_GAMEPAD_BUTTON_LEFT_STICK: key = ImGuiKey_GamepadL3; break;
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK: key = ImGuiKey_GamepadR3; break;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: key = ImGuiKey_GamepadL1; break;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: key = ImGuiKey_GamepadR1; break;
        case SDL_GAMEPAD_BUTTON_DPAD_UP: key = ImGuiKey_GamepadDpadUp; break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: key = ImGuiKey_GamepadDpadDown; break;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: key = ImGuiKey_GamepadDpadLeft; break;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: key = ImGuiKey_GamepadDpadRight; break;
        default: return 0;
    }
    out[0] = {key, 1, 0.0f};
    return 1;
}

int PadAxis(int control, int value, EngineMenuPadEvent out[2]) {
    const float magnitude = AxisMagnitude(value);
    switch (control) {
        case SDL_GAMEPAD_AXIS_LEFTX:
            return Stick(value > 0, magnitude, ImGuiKey_GamepadLStickRight, ImGuiKey_GamepadLStickLeft, out);
        case SDL_GAMEPAD_AXIS_LEFTY:
            return Stick(value > 0, magnitude, ImGuiKey_GamepadLStickDown, ImGuiKey_GamepadLStickUp, out);
        case SDL_GAMEPAD_AXIS_RIGHTX:
            return Stick(value > 0, magnitude, ImGuiKey_GamepadRStickRight, ImGuiKey_GamepadRStickLeft, out);
        case SDL_GAMEPAD_AXIS_RIGHTY:
            return Stick(value > 0, magnitude, ImGuiKey_GamepadRStickDown, ImGuiKey_GamepadRStickUp, out);
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
            out[0] = {ImGuiKey_GamepadL2, magnitude >= kTriggerDown, magnitude};
            return 1;
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
            out[0] = {ImGuiKey_GamepadR2, magnitude >= kTriggerDown, magnitude};
            return 1;
        default: return 0;
    }
}

} // namespace

extern "C" int engine_menu_choice(int imgui, int legacy_menu, int running) {
    if (!imgui || legacy_menu) return ENGINE_MENU_LEGACY;
    return running ? ENGINE_MENU_ENGINE : ENGINE_MENU_NONE;
}

extern "C" int engine_menu_pad_events(int kind, int control, int value, EngineMenuPadEvent out[2]) {
    out[0] = {-1, 0, 0.0f};
    out[1] = {-1, 0, 0.0f};
    if (kind == ENGINE_MENU_PAD_BUTTON) {
        const int count = PadButton(control, out);
        if (count) out[0].down = value != 0;
        return count;
    }
    if (kind == ENGINE_MENU_PAD_AXIS) return PadAxis(control, value, out);
    return 0;
}

extern "C" EngineMenuPointer engine_menu_pointer(int phase, uint64_t id, float fx, float fy, int window_w,
                                                 int window_h, uint64_t* first) {
    EngineMenuPointer pointer = {ENGINE_MENU_POINTER_IGNORED, 0.0f, 0.0f};
    int action = ENGINE_MENU_POINTER_IGNORED;
    if (phase == ENGINE_MENU_FINGER_DOWN) {
        if (*first == 0) {
            *first = id;
            action = ENGINE_MENU_POINTER_PRESS;
        }
    } else if (*first == id) {
        if (phase == ENGINE_MENU_FINGER_MOTION) {
            action = ENGINE_MENU_POINTER_MOVE;
        } else if (phase == ENGINE_MENU_FINGER_UP || phase == ENGINE_MENU_FINGER_CANCELED) {
            *first = 0;
            action = ENGINE_MENU_POINTER_RELEASE;
        }
    }
    if (action == ENGINE_MENU_POINTER_IGNORED) return pointer;
    pointer.action = action;
    pointer.x = fx * (float)window_w;
    pointer.y = fy * (float)window_h;
    return pointer;
}

#ifndef ENGINE_MENU_PURE_TEST

#include <atomic>
#include <cfloat>
#include <cstdio>

#include "imgui_internal.h"
#include "nx/gpu/vk/imgui_events.h"
#include "nx/gpu/vk/imgui_input.h"
#include "nx/gpu/vk/imgui_settings.h"
#include "nx/gpu/vk/md3/md3_layout.h"
#include "nx/gpu/vk/md3/md3_motion.h"
#include "nx/gpu/vk/md3/md3_palette.h"
#include "nx/gpu/vk/md3/md3_scale.h"
#include "nx/gpu/vk/md3/md3_theme.h"
#include "nx/gpu/vk/md3/md3_widgets.h"
#include "nx/gpu/vk/menu_settings.h"

namespace {

constexpr int kPageCount = 10;
constexpr int kPageGame = 0;
constexpr int kPageDisplay = 1;
constexpr int kPageGraphics = 2;
constexpr int kPagePerformance = 3;

constexpr float kTopBarHeightPx = 64.0f;
constexpr float kRailWidthPx = 112.0f;
constexpr float kBottomBarHeightPx = 52.0f;
constexpr float kPanelPaddingPx = 24.0f;
// The panel content slides this far down at the start of the open and at the end of the close.
constexpr float kPanelSlidePx = 16.0f;
// The page content fades in and moves up by this distance on a page change.
constexpr float kPageSlidePx = 16.0f;
constexpr float kTitleIconGapPx = 8.0f;

// Bottom bar glyphs, pixels at 1080p.
constexpr float kHintGlyphHeightPx = 24.0f;
constexpr float kHintGlyphPaddingPx = 8.0f;
constexpr float kHintGlyphGapPx = 8.0f;
constexpr float kHintGroupGapPx = 24.0f;

constexpr md3::TweenId kPanelAlphaTweenId = 0x4D440001u;
constexpr md3::TweenId kPanelOffsetTweenId = 0x4D440002u;
constexpr md3::TweenId kPageFadeTweenId = 0x4D440003u;
constexpr md3::TweenId kPageOffsetTweenId = 0x4D440004u;
constexpr md3::TweenId kPageScrollTweenId = 0x4D440005u;
constexpr md3::TweenId kHintFadeInTweenId = 0x4D440006u;
constexpr md3::TweenId kHintFadeOutTweenId = 0x4D440007u;

constexpr uint32_t kSeed = 0xF97316u;

// U+F8CD (Material Symbols Rounded, build) in UTF-8, for the title.
const char* const kBuildMark = "\xEF\xA3\x8D";
// U+E5D5 (Material Symbols Rounded, refresh) in UTF-8, for a restart hint.
const char* const kRestartMark = "\xEE\x97\x95";

// The settings model's pages that this menu shows, and the lines of a row.
constexpr const char* kDisplayPageName = "Display";
constexpr const char* kGraphicsPageName = "Graphics";
constexpr const char* kPerformancePageName = "Performance";
constexpr const char* kRestartSupporting = "Saved. Applies after a restart.";
constexpr const char* kTextOnlySupporting = "Edit it in the .ini file";
constexpr double kPresetEpsilon = 1e-6;

// The row limits and right part sizes of the settings pages. The widths are
// the mockup's, the row count is above every page of the model, and a combo
// holds at most 40 values.
constexpr int kMenuRowMax = 64;
constexpr int kMenuComboMax = 40;
constexpr int kMenuValueMax = 96;
constexpr float kRightWidthComboPx = 240.0f;
constexpr float kRightWidthNumberPx = 240.0f;
constexpr float kRightWidthSliderPx = 280.0f;

// The preset row whose Custom entry was picked: its number field stays below the
// dropdown, as in the old window, even when the value is one of the presets.
const char* g_custom_key = nullptr;

enum class InputDevice { Keyboard, Gamepad, Mouse };

InputDevice DeviceFromEvent(int device) {
    if (device == IMGUI_DEVICE_GAMEPAD) return InputDevice::Gamepad;
    if (device == IMGUI_DEVICE_MOUSE) return InputDevice::Mouse;
    return InputDevice::Keyboard;
}

const uint32_t kRailIcons[kPageCount] = {
    md3::icon::kGame,   md3::icon::kDisplay,       md3::icon::kGraphics, md3::icon::kPerformance,
    md3::icon::kMouse,  md3::icon::kKeyboard,      md3::icon::kControllers, md3::icon::kOnScreenControls,
    md3::icon::kDebug,  md3::icon::kAbout,
};

const char* const kRailLabels[kPageCount] = {
    "Game", "Display", "Graphics", "Performance", "Mouse",
    "Keyboard", "Controllers", "On-screen controls", "Debug", "About",
};

enum { MENU_CLOSED, MENU_OPEN, MENU_CLOSING };

struct MenuState {
    float panel_alpha;
    float panel_offset;
    int page;
    int pending_page = -1;
    int anim_page;
    bool reduce_motion;
    bool was_visible;
    bool focus_panel;
    InputDevice last_device;
    InputDevice hint_device;
    InputDevice hint_old_device;
    bool hint_fading;
    md3::NavAnchor page_anchor;
    int page_anchor_page;
    bool request_page_top;
    ImGuiWindow* page_window;
    int page_window_frame;
    float page_scroll_target;
    bool page_scroll_animating;
    int rail_focus;
};

std::atomic<int> g_state{MENU_CLOSED};

// The frame thread moves the state only from the state it saw: a toggle of the main thread between its read and
// its write wins, so a reopen during the close is not lost.
bool StateMove(int from, int to) { return g_state.compare_exchange_strong(from, to, std::memory_order_acq_rel); }
float g_panel_alpha; /* the compositor's alpha of the last frame, on the GPU thread */
MenuState g_ui{};

constexpr const char* PAGE_PLACEHOLDER = "This page moves here in a later change.";

ImVec2 HintTextSize(const char* text, float px) {
    ImFont* font = ImGui::GetFont();
    if (font == nullptr || text == nullptr) return ImVec2(0.0f, px);
    return font->CalcTextSizeA(px, FLT_MAX, 0.0f, text);
}

void DrawHintText(ImDrawList* draw, const char* text, ImVec2 pos, float px, ImU32 color, float alpha) {
    ImFont* font = ImGui::GetFont();
    if (draw == nullptr || font == nullptr || text == nullptr) return;
    draw->AddText(font, px, pos, md3::FadeColor(color, alpha), text);
}

float DrawRoundKey(ImDrawList* draw, const char* letter, float x, float center_y, const md3::Theme& theme,
                   float alpha) {
    const float diameter = kHintGlyphHeightPx * theme.scale;
    const ImU32 fill = md3::ToImU32(theme.scheme.on_surface_variant);
    const ImVec2 center(x + diameter * 0.5f, center_y);
    draw->AddCircleFilled(center, diameter * 0.5f, md3::FadeColor(fill, alpha), 0);
    const ImVec2 size = HintTextSize(letter, theme.label_large);
    DrawHintText(draw, letter, ImVec2(center.x - size.x * 0.5f, center.y - size.y * 0.5f), theme.label_large,
                 md3::ToImU32(theme.scheme.surface), alpha);
    return x + diameter;
}

float DrawPillKey(ImDrawList* draw, const char* text, float x, float center_y, const md3::Theme& theme,
                  bool outlined, float alpha) {
    const float height = kHintGlyphHeightPx * theme.scale;
    const float padding = kHintGlyphPaddingPx * theme.scale;
    const ImVec2 size = HintTextSize(text, theme.label_large);
    const float text_width = size.x + 2.0f * padding;
    const float width = text_width > height ? text_width : height;
    const ImRect box(ImVec2(x, center_y - height * 0.5f), ImVec2(x + width, center_y + height * 0.5f));
    const float rounding = height * 0.5f;
    if (outlined) {
        draw->AddRect(box.Min, box.Max, md3::FadeColor(md3::ToImU32(theme.scheme.outline), alpha), rounding, 0,
                      theme.scale);
    } else {
        draw->AddRectFilled(box.Min, box.Max, md3::FadeColor(md3::ToImU32(theme.scheme.on_surface_variant), alpha),
                            rounding);
    }
    DrawHintText(draw, text, ImVec2(box.Min.x + (width - size.x) * 0.5f, box.Min.y + (height - size.y) * 0.5f),
                 theme.label_large,
                 outlined ? md3::ToImU32(theme.scheme.on_surface_variant) : md3::ToImU32(theme.scheme.surface),
                 alpha);
    return x + width;
}

enum class HintKind { Round, Pill, Outline };

float DrawHintGroup(ImDrawList* draw, float x, float center_y, const char* glyph, const char* label, HintKind kind,
                    const md3::Theme& theme, float alpha) {
    const float gap = kHintGlyphGapPx * theme.scale;
    if (kind == HintKind::Round) {
        x = DrawRoundKey(draw, glyph, x, center_y, theme, alpha);
    } else {
        x = DrawPillKey(draw, glyph, x, center_y, theme, kind == HintKind::Outline, alpha);
    }
    if (label == nullptr) return x;
    const ImVec2 size = HintTextSize(label, theme.label_large);
    DrawHintText(draw, label, ImVec2(x + gap, center_y - size.y * 0.5f), theme.label_large,
                 md3::ToImU32(theme.scheme.on_surface_variant), alpha);
    return x + gap + size.x;
}

void DrawHintsForDevice(ImDrawList* draw, InputDevice device, float x, float center_y, float alpha,
                        const md3::Theme& theme) {
    if (device == InputDevice::Mouse) return;
    const float s = theme.scale;
    const float group_gap = kHintGroupGapPx * s;
    if (device == InputDevice::Gamepad) {
        x = DrawHintGroup(draw, x, center_y, "A", "Select", HintKind::Round, theme, alpha) + group_gap;
        x = DrawHintGroup(draw, x, center_y, "B", "Back", HintKind::Round, theme, alpha) + group_gap;
        x = DrawHintGroup(draw, x, center_y, "LB", nullptr, HintKind::Pill, theme, alpha) + kHintGlyphGapPx * s;
        DrawHintGroup(draw, x, center_y, "RB", "Page", HintKind::Pill, theme, alpha);
    } else {
        x = DrawHintGroup(draw, x, center_y, "Enter", "Select", HintKind::Outline, theme, alpha) + group_gap;
        x = DrawHintGroup(draw, x, center_y, "Esc", "Back", HintKind::Outline, theme, alpha) + group_gap;
        DrawHintGroup(draw, x, center_y, "Ctrl+Tab", "Page", HintKind::Outline, theme, alpha);
    }
}

void FocusNavAnchor(const md3::NavAnchor& anchor) {
    if (anchor.id == 0 || anchor.window == nullptr) return;
    ImGuiWindow* window = anchor.window;
    ImGui::SetNavWindow(window);
    const ImRect rel = ImGui::WindowRectAbsToRel(window, ImRect(anchor.min, anchor.max));
    ImGui::SetNavID(anchor.id, ImGuiNavLayer_Main, window->NavRootFocusScopeId, rel);
    ImGui::NavMoveRequestCancel();
    ImGui::SetNavCursorVisible(true);
}

bool NavRightPressed() {
    return ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight, false) ||
           ImGui::IsKeyPressed(ImGuiKey_RightArrow, false);
}

// The largest scroll of the window's content as it is this frame. ImGui writes
// ScrollMax at the next Begin from the last frame's content, so the engine
// computes the current maximum itself (decision 6, fault C3). content_growth is
// the part that a card added above its last frame's size.
float PageScrollMax(ImGuiWindow* window, float content_growth) {
    if (window == nullptr) return 0.0f;
    const float content_height = window->DC.CursorMaxPos.y - window->DC.CursorStartPos.y;
    const float max_scroll =
        content_height + content_growth + window->WindowPadding.y * 2.0f - window->InnerRect.GetHeight();
    return max_scroll > 0.0f ? max_scroll : 0.0f;
}

void DrawTopBar() {
    const md3::Theme& theme = md3::CurrentTheme();
    const float s = theme.scale;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("##top_bar", ImVec2(0.0f, kTopBarHeightPx * s), ImGuiChildFlags_NavFlattened,
                      ImGuiWindowFlags_NoScrollbar);
    const ImVec2 origin = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const float padding = kPanelPaddingPx * s;

    md3::PushFontSize(md3::kTitleLargePx);
    ImGui::PushStyleColor(ImGuiCol_Text, md3::ToImU32(theme.scheme.on_surface));
    ImGui::SetCursorScreenPos(ImVec2(origin.x + padding, origin.y + (size.y - ImGui::GetTextLineHeight()) * 0.5f));
    ImGui::TextUnformatted(kBuildMark);
    ImGui::SameLine(0.0f, kTitleIconGapPx * s);
    ImGui::TextUnformatted("Engine menu");
    ImGui::PopStyleColor();
    ImGui::PopFont();

    const float button = md3::kButtonHeightPx * s;
    ImGui::SetCursorScreenPos(
        ImVec2(origin.x + size.x - padding - button, origin.y + (size.y - button) * 0.5f));
    if (md3::IconButton("##close_panel", md3::icon::kClose)) StateMove(MENU_OPEN, MENU_CLOSING);
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void DrawBottomBar() {
    const md3::Theme& theme = md3::CurrentTheme();
    const float s = theme.scale;
    const float padding = kPanelPaddingPx * s;
    const float height = kBottomBarHeightPx * s;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("##bottom_bar", ImVec2(0.0f, height), ImGuiChildFlags_NavFlattened,
                      ImGuiWindowFlags_NoScrollbar);
    const ImVec2 origin = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const float center_y = origin.y + height * 0.5f;

    if (g_ui.last_device != g_ui.hint_device) {
        g_ui.hint_old_device = g_ui.hint_device;
        g_ui.hint_device = g_ui.last_device;
        g_ui.hint_fading = true;
        md3::ResetTween(kHintFadeInTweenId, 0.0f);
        md3::ResetTween(kHintFadeOutTweenId, 1.0f);
    }
    const float hint_in = md3::Tween(kHintFadeInTweenId, 1.0f, md3::kDurationShortMs, md3::kCurveStandard);
    float hint_out = 0.0f;
    if (g_ui.hint_fading) {
        hint_out = md3::Tween(kHintFadeOutTweenId, 0.0f, md3::kDurationShortMs, md3::kCurveStandard);
        if (hint_out <= 0.0f) g_ui.hint_fading = false;
    }
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float hint_x = origin.x + padding;
    if (g_ui.hint_fading && hint_out > 0.0f) {
        DrawHintsForDevice(draw, g_ui.hint_old_device, hint_x, center_y, hint_out, theme);
    }
    if (hint_in > 0.0f) DrawHintsForDevice(draw, g_ui.hint_device, hint_x, center_y, hint_in, theme);

    md3::PushFontSize(md3::kLabelLargePx);
    ImGui::PushStyleColor(ImGuiCol_Text, md3::ToImU32(theme.scheme.on_surface_variant));
    const float restart_width = ImGui::CalcTextSize(kRestartMark).x + ImGui::CalcTextSize(" = applies after a restart").x;
    const float line_y = (size.y - ImGui::GetTextLineHeight()) * 0.5f;
    ImGui::SetCursorPos(ImVec2(size.x - padding - restart_width, line_y));
    ImGui::Text("%s = applies after a restart", kRestartMark);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

void DrawRail(float body_height) {
    const md3::Theme& theme = md3::CurrentTheme();
    ImGui::BeginChild("##rail", ImVec2(kRailWidthPx * theme.scale, body_height), ImGuiChildFlags_NavFlattened,
                      ImGuiWindowFlags_NoScrollbar);
    const bool right_pressed = NavRightPressed();
    int focused_page = -1;
    ImRect focused_box;
    for (int page = 0; page < kPageCount; ++page) {
        char id[32];
        std::snprintf(id, sizeof(id), "##rail_%d", page);
        if (md3::NavItem(id, kRailIcons[page], kRailLabels[page], g_ui.page == page) &&
            page != g_ui.page) {
            if (md3::TextEditing())
                g_ui.pending_page = page;
            else
                g_ui.page = page;
        }
        // The old page is drawn once more, so the field loses the focus and commits.
        if (page == g_ui.pending_page) {
            ImGuiWindow* rail = ImGui::GetCurrentWindow();
            const ImRect box(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            ImGui::SetNavWindow(rail);
            ImGui::SetNavID(ImGui::GetItemID(), ImGuiNavLayer_Main, rail->NavRootFocusScopeId,
                            ImGui::WindowRectAbsToRel(rail, box));
            ImGui::SetNavCursorVisible(g_ui.last_device != InputDevice::Mouse);
        }
        if (ImGui::IsItemFocused()) {
            focused_page = page;
            focused_box = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        }
        if (right_pressed && focused_page == page && g_ui.page_anchor_page == g_ui.page &&
            g_ui.page_anchor.id != 0) {
            FocusNavAnchor(g_ui.page_anchor);
            g_ui.request_page_top = true;
        }
    }
    // Decision 8: the rail scrolls with no visible scrollbar when the items are
    // taller than the body, and a new focused item is kept fully visible.
    if (focused_page >= 0 && focused_page != g_ui.rail_focus) {
        g_ui.rail_focus = focused_page;
        ImGuiWindow* rail = ImGui::GetCurrentWindow();
        const float top = focused_box.Min.y - rail->DC.CursorStartPos.y;
        const float bottom = focused_box.Max.y - rail->DC.CursorStartPos.y;
        const float target = md3::PageScrollTarget(
            {top, bottom, rail->InnerRect.GetHeight(), ImGui::GetScrollY(), PageScrollMax(rail, 0.0f),
             focused_page == 0, focused_page == kPageCount - 1});
        ImGui::SetScrollY(rail, target);
    }
    ImGui::EndChild();
}

int SettingsPageByName(const char* name) {
    for (int page = 0; page < imgui_settings_pages(); ++page)
        if (std::strcmp(imgui_settings_page_name(page), name) == 0) return page;
    return -1;
}

int ReadSettingsRows(int page, ImguiSettingRow* rows, int max_rows) {
    int count = 0;
    while (count < max_rows && imgui_settings_row(page, count, &rows[count])) ++count;
    return count;
}

void SetSettingInt(const ImguiSettingRow& row, int value) {
    char text[16];
    std::snprintf(text, sizeof text, "%d", value);
    imgui_settings_set(row.key, text);
}

void DrawSwitchRow(const ImguiSettingRow& row) {
    bool on = row.ivalue != 0;
    if (md3::Switch("##value", &on)) imgui_settings_set(row.key, on ? "1" : "0");
}

void DrawSliderRow(const ImguiSettingRow& row) {
    int value = row.ivalue;
    const int lo = static_cast<int>(row.lo);
    const int hi = static_cast<int>(row.hi);
    if (md3::SliderInt("##value", &value, lo, hi, row.step, "%d") && value != row.ivalue) SetSettingInt(row, value);
}

void DrawNumberRow(const ImguiSettingRow& row) {
    int value = row.ivalue;
    const int step = row.step > 0 ? row.step : 1;
    if (md3::NumberField("##value", &value, static_cast<int>(row.lo), static_cast<int>(row.hi), step) &&
        value != row.ivalue)
        SetSettingInt(row, value);
}

void DrawIntWordsRow(const ImguiSettingRow& row) {
    const int step = row.step > 0 ? row.step : 1;
    const int lo = static_cast<int>(row.lo);
    int count = static_cast<int>((row.hi - row.lo) / step) + 1;
    if (count < 1) count = 1;
    if (count > kMenuComboMax) count = kMenuComboMax;
    char labels[kMenuComboMax][kMenuValueMax];
    const char* items[kMenuComboMax];
    int index = 0;
    for (int i = 0; i < count; ++i) {
        const int value = lo + i * step;
        imgui_settings_word(row.key, value, labels[i], kMenuValueMax);
        items[i] = labels[i];
        if (value == row.ivalue) index = i;
    }
    int chosen = index;
    if (!md3::Combo("##value", &chosen, items, count) || chosen == index) return;
    SetSettingInt(row, lo + chosen * step);
}

void DrawTextWordsRow(const ImguiSettingRow& row) {
    char values[kMenuComboMax][kMenuValueMax];
    char labels[kMenuComboMax][kMenuValueMax];
    const char* items[kMenuComboMax];
    int count = 0;
    int index = -1;
    while (count < kMenuComboMax &&
           imgui_settings_text_word(row.key, count, values[count], kMenuValueMax, labels[count], kMenuValueMax)) {
        items[count] = labels[count];
        if (std::strcmp(values[count], row.text) == 0) index = count;
        ++count;
    }
    if (count == 0) return;
    const int word_count = count;
    if (index < 0 && count < kMenuComboMax) {
        std::snprintf(labels[count], kMenuValueMax, "%s (custom)", row.text);
        items[count] = labels[count];
        index = count;
        ++count;
    }
    if (index < 0) index = 0;
    int chosen = index;
    if (!md3::Combo("##value", &chosen, items, count) || chosen == index) return;
    if (chosen < word_count) imgui_settings_set(row.key, values[chosen]);
}

bool PresetShowsCustom(const ImguiSettingRow& row) {
    if (row.presets <= 0 || (row.flags & IMGUI_SETTING_CUSTOM) == 0) return false;
    if (g_custom_key != nullptr && std::strcmp(g_custom_key, row.key) == 0) return true;
    const double now = row.type == IMGUI_SETTING_FLOAT ? static_cast<double>(row.fvalue) : static_cast<double>(row.ivalue);
    for (int i = 0; i < row.presets; ++i) {
        char value[kMenuValueMax];
        char label[kMenuValueMax];
        double number = 0.0;
        if (!imgui_settings_preset(row.key, i, value, sizeof value, label, sizeof label, &number)) return false;
        if (std::fabs(number - now) < kPresetEpsilon) return false;
    }
    return true;
}

void DrawPresetRow(const ImguiSettingRow& row) {
    char values[kMenuComboMax][kMenuValueMax];
    char labels[kMenuComboMax][kMenuValueMax];
    const char* items[kMenuComboMax];
    int count = row.presets;
    if (count < 0) count = 0;
    if (count > kMenuComboMax - 1) count = kMenuComboMax - 1;
    const double now = row.type == IMGUI_SETTING_FLOAT ? static_cast<double>(row.fvalue) : static_cast<double>(row.ivalue);
    int at = -1;
    for (int i = 0; i < count; ++i) {
        double number = 0.0;
        if (!imgui_settings_preset(row.key, i, values[i], kMenuValueMax, labels[i], kMenuValueMax, &number)) {
            count = i;
            break;
        }
        items[i] = labels[i];
        if (std::fabs(number - now) < kPresetEpsilon) at = i;
    }
    const bool custom_row = (row.flags & IMGUI_SETTING_CUSTOM) != 0;
    const int custom_index = count;
    if (custom_row && count < kMenuComboMax) {
        items[count] = "Custom";
        ++count;
    }
    const bool custom_on = custom_row && PresetShowsCustom(row);
    const int index = custom_on ? custom_index : (at >= 0 ? at : 0);
    int chosen = index;
    if (md3::Combo("##value", &chosen, items, count) && chosen != index) {
        if (custom_row && chosen == custom_index) {
            g_custom_key = row.key;
        } else if (chosen < custom_index) {
            g_custom_key = nullptr;
            imgui_settings_set(row.key, values[chosen]);
        }
    }
    if (!custom_on) return;
    int value = row.ivalue;
    if (value < static_cast<int>(row.lo)) value = static_cast<int>(row.lo);
    if (value > static_cast<int>(row.hi)) value = static_cast<int>(row.hi);
    const int step = row.step > 0 ? row.step : 1;
    if (md3::NumberField("##custom", &value, static_cast<int>(row.lo), static_cast<int>(row.hi), step) &&
        value != row.ivalue)
        SetSettingInt(row, value);
}

void DrawSettingRow(const ImguiSettingRow& row) {
    const int widget = menu_settings_widget(&row);
    if (widget == MENU_WIDGET_UNSUPPORTED) return; // no FLOAT row of these pages today
    const md3::Theme& theme = md3::CurrentTheme();
    const float s = theme.scale;
    const bool locked = (row.flags & IMGUI_SETTING_LOCKED) != 0;
    const bool restart = (row.flags & IMGUI_SETTING_STARTUP) != 0;
    const bool custom_field = widget == MENU_WIDGET_COMBO && PresetShowsCustom(row);
    float right_width = 0.0f;
    float right_height = 0.0f;
    switch (widget) {
        case MENU_WIDGET_COMBO:
            right_width = kRightWidthComboPx * s;
            right_height = md3::kDropdownHeightPx * s;
            break;
        case MENU_WIDGET_NUMBER:
            right_width = kRightWidthNumberPx * s;
            right_height = md3::kDropdownHeightPx * s;
            break;
        case MENU_WIDGET_SLIDER:
            right_width = kRightWidthSliderPx * s;
            right_height = md3::kSliderItemHeightPx * s;
            break;
        case MENU_WIDGET_SWITCH:
            right_width = md3::kSwitchWidthPx * s;
            right_height = md3::kSwitchHeightPx * s;
            break;
        default:
            break;
    }
    if (custom_field) right_height = right_height * 2.0f + ImGui::GetStyle().ItemSpacing.y;
    const char* supporting = row.help;
    if (row.flags & IMGUI_SETTING_RESTART)
        supporting = kRestartSupporting;
    else if (widget == MENU_WIDGET_TEXT_ONLY)
        supporting = kTextOnlySupporting;
    char id[96];
    std::snprintf(id, sizeof id, "##setting_%s", row.key);
    ImGui::BeginDisabled(locked);
    md3::RowBegin(id, row.name != nullptr ? row.name : row.key, supporting, restart, right_width, right_height);
    ImGui::PushID(row.key);
    switch (widget) {
        case MENU_WIDGET_COMBO:
            if (row.presets > 0)
                DrawPresetRow(row);
            else if (row.type == IMGUI_SETTING_TEXT)
                DrawTextWordsRow(row);
            else
                DrawIntWordsRow(row);
            break;
        case MENU_WIDGET_NUMBER:
            DrawNumberRow(row);
            break;
        case MENU_WIDGET_SLIDER:
            DrawSliderRow(row);
            break;
        case MENU_WIDGET_SWITCH:
            DrawSwitchRow(row);
            break;
        default:
            break;
    }
    ImGui::PopID();
    md3::RowEnd();
    ImGui::EndDisabled();
}

// The rows of a settings model page, in model order, with no section title.
void DrawSettingsPage(const char* page_name) {
    const int page = SettingsPageByName(page_name);
    if (page < 0) return;
    ImguiSettingRow rows[kMenuRowMax];
    const int count = ReadSettingsRows(page, rows, kMenuRowMax);
    for (int i = 0; i < count; ++i) DrawSettingRow(rows[i]);
}

void DrawPerformanceSettings() {
    const int page = SettingsPageByName(kPerformancePageName);
    if (page < 0) return;
    ImguiSettingRow rows[kMenuRowMax];
    const int count = ReadSettingsRows(page, rows, kMenuRowMax);
    int main_index[MENU_PERFORMANCE_MAIN_COUNT];
    int advanced[kMenuRowMax];
    int main_count = 0;
    int advanced_count = 0;
    menu_performance_groups(rows, count, main_index, &main_count, advanced, &advanced_count);
    for (int i = 0; i < main_count; ++i) DrawSettingRow(rows[main_index[i]]);
    md3::SectionTitle("Advanced");
    for (int i = 0; i < advanced_count; ++i) DrawSettingRow(rows[advanced[i]]);
}

void DrawPage(int page) {
    md3::CardBegin("##page_card");
    md3::SectionTitle(kRailLabels[page]);
    if (page == kPageDisplay) {
        DrawSettingsPage(kDisplayPageName);
        md3::RowBegin("##reduce_motion", "Reduce motion", "Turns off the animations", false,
                      md3::kSwitchWidthPx * md3::CurrentTheme().scale,
                      md3::kSwitchHeightPx * md3::CurrentTheme().scale);
        md3::Switch("##reduce_motion", &g_ui.reduce_motion);
        md3::RowEnd();
    } else if (page == kPageGraphics) {
        DrawSettingsPage(kGraphicsPageName);
    } else if (page == kPagePerformance) {
        DrawPerformanceSettings();
    } else {
        md3::BodyText(PAGE_PLACEHOLDER);
    }
    md3::CardEnd();
}

void DrawPanel() {
    const md3::Theme& theme = md3::CurrentTheme();
    const float s = theme.scale;
    ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    if (g_ui.focus_panel) {
        ImGui::SetNextWindowFocus();
        g_ui.focus_panel = false;
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, md3::ToImU32(theme.scheme.surface));
    ImGui::Begin("##panel", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

    if (g_ui.panel_offset > 0.0f) ImGui::SetCursorPosY(ImGui::GetCursorPosY() + g_ui.panel_offset * s);

    DrawTopBar();

    float body_height = ImGui::GetContentRegionAvail().y - kBottomBarHeightPx * s - ImGui::GetStyle().ItemSpacing.y;
    if (body_height < 1.0f) body_height = 1.0f;

    DrawRail(body_height);
    ImGui::SameLine();

    const bool page_changed = g_ui.page != g_ui.anim_page;
    if (page_changed) {
        g_ui.anim_page = g_ui.page;
        md3::ResetTween(kPageFadeTweenId, 0.0f);
        md3::ResetTween(kPageOffsetTweenId, kPageSlidePx);
    }
    const float page_fade =
        md3::Tween(kPageFadeTweenId, 1.0f, md3::kDurationMediumMs, md3::kCurveEmphasizedDecelerate);
    const float page_offset =
        md3::Tween(kPageOffsetTweenId, 0.0f, md3::kDurationMediumMs, md3::kCurveEmphasizedDecelerate);

    const bool page_known = g_ui.page_window != nullptr && g_ui.page_window_frame >= ImGui::GetFrameCount() - 1;
    const bool wheel = ImGui::GetIO().MouseWheel != 0.0f;
    if (page_changed || g_ui.request_page_top) {
        g_ui.request_page_top = false;
        g_ui.page_scroll_target = 0.0f;
        g_ui.page_scroll_animating = false;
        md3::ResetTween(kPageScrollTweenId, 0.0f);
        if (page_known) ImGui::SetScrollY(g_ui.page_window, 0.0f);
    } else if (page_known) {
        // The wheel keeps its own target. A nav move uses the row rule after the
        // page is drawn, so its keep-visible target is dropped here.
        const float pending = g_ui.page_window->ScrollTarget.y;
        if (pending < FLT_MAX && wheel) {
            const float ratio = g_ui.page_window->ScrollTargetCenterRatio.y;
            float wanted = pending - ratio * g_ui.page_window->SizeFull.y;
            wanted = g_ui.page_scroll_target + (wanted - g_ui.page_window->Scroll.y);
            md3::ResetTween(kPageScrollTweenId, g_ui.page_window->Scroll.y);
            g_ui.page_scroll_target = wanted;
        }
        g_ui.page_window->ScrollTarget.y = FLT_MAX;
    }
    if (page_known) {
        const float scroll =
            md3::Tween(kPageScrollTweenId, g_ui.page_scroll_target, md3::kDurationShortLongMs, md3::kCurveStandard);
        const bool running = scroll != g_ui.page_scroll_target;
        if (running || g_ui.page_scroll_animating) ImGui::SetScrollY(g_ui.page_window, scroll);
        g_ui.page_scroll_animating = running;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, page_fade);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kPanelPaddingPx * s, kPanelPaddingPx * s));
    ImGui::BeginChild("##page", ImVec2(0.0f, body_height),
                      ImGuiChildFlags_NavFlattened | ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_None);
    if (page_offset > 0.0f) ImGui::SetCursorPosY(ImGui::GetCursorPosY() + page_offset * s);
    ImGuiWindow* page_window = ImGui::GetCurrentWindow();
    g_ui.page_window = page_window;
    g_ui.page_window_frame = ImGui::GetFrameCount();
    if (!page_known) {
        md3::ResetTween(kPageScrollTweenId, ImGui::GetScrollY());
        g_ui.page_scroll_target = ImGui::GetScrollY();
    }
    if (ImGui::GetCurrentContext()->ActiveId == ImGui::GetWindowScrollbarID(page_window, ImGuiAxis_Y)) {
        md3::ResetTween(kPageScrollTweenId, ImGui::GetScrollY());
        g_ui.page_scroll_target = ImGui::GetScrollY();
        g_ui.page_scroll_animating = false;
    }
    md3::BeginNavAnchorCapture();
    DrawPage(g_ui.page);
    g_ui.page_anchor = md3::EndNavAnchorCapture();
    g_ui.page_anchor_page = g_ui.page;

    // Decision 5: a nav move scrolls the page by the whole focused row box.
    // Decision 6: the clamp uses this frame's content maximum.
    const float max_scroll = PageScrollMax(page_window, md3::LastCardGrowth());
    const md3::FocusedRow focused = md3::LastFocusedRow();
    ImGuiContext* context = ImGui::GetCurrentContext();
    const ImGuiID nav_moved = context != nullptr ? context->NavJustMovedToId : 0;
    if (focused.valid && focused.count > 0 && nav_moved != 0 && nav_moved == focused.nav_id) {
        const float scroll = ImGui::GetScrollY();
        md3::ResetTween(kPageScrollTweenId, scroll);
        g_ui.page_scroll_target = md3::PageScrollTarget(
            {focused.top, focused.bottom, page_window->InnerRect.GetHeight(), scroll, max_scroll,
             focused.index == 0, focused.index == focused.count - 1});
        g_ui.page_scroll_animating = true;
    } else {
        g_ui.page_scroll_target = ImClamp(g_ui.page_scroll_target, 0.0f, max_scroll);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);

    DrawBottomBar();
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

bool HandlePageKeys() {
    if (md3::AnyPopupOpen()) return false;
    ImGuiIO& io = ImGui::GetIO();
    const int previous_page = g_ui.page;
    int target_page = previous_page;
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
        if (io.KeyShift)
            target_page = (target_page + kPageCount - 1) % kPageCount;
        else
            target_page = (target_page + 1) % kPageCount;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false))
        target_page = (target_page + 1) % kPageCount;
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false))
        target_page = (target_page + kPageCount - 1) % kPageCount;
    if (target_page == previous_page) return false;
    if (md3::TextEditing())
        g_ui.pending_page = target_page;
    else
        g_ui.page = target_page;
    return g_ui.page != previous_page;
}

void ResetUiState() {
    md3::CancelTextEditing();
    g_ui.panel_alpha = 0.0f;
    g_ui.panel_offset = 0.0f;
    g_ui.page = kPageGame;
    g_ui.pending_page = -1;
    g_ui.anim_page = kPageGame;
    g_ui.was_visible = false;
    g_ui.focus_panel = false;
    g_ui.hint_fading = false;
    g_ui.hint_device = InputDevice::Keyboard;
    g_ui.hint_old_device = InputDevice::Keyboard;
    g_ui.last_device = InputDevice::Keyboard;
    g_ui.page_anchor = md3::NavAnchor{};
    g_ui.page_anchor_page = -1;
    g_ui.request_page_top = false;
    g_ui.page_window = nullptr;
    g_ui.page_window_frame = 0;
    g_ui.page_scroll_target = 0.0f;
    g_ui.page_scroll_animating = false;
    g_ui.rail_focus = -1;
    g_custom_key = nullptr;
}

} // namespace

extern "C" void engine_menu_toggle(void) {
    const int state = g_state.load(std::memory_order_acquire);
    g_state.store(state == MENU_OPEN ? MENU_CLOSING : MENU_OPEN, std::memory_order_release);
}

extern "C" int engine_menu_active(void) { return g_state.load(std::memory_order_acquire) != MENU_CLOSED; }

extern "C" float engine_menu_panel_alpha(void) { return g_panel_alpha; }

extern "C" void engine_menu_reset(void) {
    g_state.store(MENU_CLOSED, std::memory_order_release);
    g_panel_alpha = 0.0f;
    ResetUiState();
}

extern "C" void engine_menu_gamepad_button(int sdl_button, int down) {
    EngineMenuPadEvent events[2];
    if (!engine_menu_active() || !engine_menu_pad_events(ENGINE_MENU_PAD_BUTTON, sdl_button, down, events)) return;
    for (int i = 0; i < 2 && events[i].key >= 0; ++i) {
        ImguiEvent e = {.type = IMGUI_EV_GAMEPAD, .down = events[i].down, .key = events[i].key};
        imgui_input_push(&e);
    }
}

extern "C" void engine_menu_gamepad_axis(int sdl_axis, int value) {
    EngineMenuPadEvent events[2];
    if (!engine_menu_active() || !engine_menu_pad_events(ENGINE_MENU_PAD_AXIS, sdl_axis, value, events)) return;
    for (int i = 0; i < 2 && events[i].key >= 0; ++i) {
        ImguiEvent e = {.type = IMGUI_EV_GAMEPAD_AXIS, .down = events[i].down, .key = events[i].key, .value = events[i].value};
        imgui_input_push(&e);
    }
}

bool engine_menu_visible(void) { return g_state.load(std::memory_order_acquire) != MENU_CLOSED; }

bool engine_menu_draw(ImguiFrame* f) {
    int state = g_state.load(std::memory_order_acquire);
    if (state == MENU_CLOSED) return false;

    const float scale = md3::UiScale(f->window_h, f->display_scale);
    const md3::Scheme scheme = md3::SchemeFromSeed(kSeed);
    ImGuiStyle saved = ImGui::GetStyle();
    md3::ApplyTheme(scheme, scale);
    md3::SetReduceMotion(g_ui.reduce_motion);
    g_ui.last_device = DeviceFromEvent(imgui_events_last_device());

    if (!g_ui.was_visible) { /* the first frame after closed: the panel starts down and transparent */
        g_ui.was_visible = true;
        g_ui.focus_panel = true;
        md3::ResetTween(kPanelAlphaTweenId, 0.0f);
        md3::ResetTween(kPanelOffsetTweenId, kPanelSlidePx);
    }

    const bool open = state == MENU_OPEN;
    if (open) {
        if (g_ui.pending_page >= 0) {
            g_ui.page = g_ui.pending_page;
            g_ui.pending_page = -1;
        }
        HandlePageKeys();
        const bool close_pressed = ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
                                   ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false);
        if (close_pressed && !md3::AnyPopupOpen() && !md3::TextEditing()) {
            if (StateMove(MENU_OPEN, MENU_CLOSING)) state = MENU_CLOSING;
        }
    } else {
        md3::CancelTextEditing();
        g_ui.pending_page = -1;
    }

    const bool closing = state != MENU_OPEN;
    const int duration = closing ? md3::kDurationShortLongMs : md3::kDurationMediumMs;
    const md3::MotionCurve& curve = closing ? md3::kCurveEmphasizedAccelerate : md3::kCurveEmphasizedDecelerate;
    g_ui.panel_alpha = md3::Tween(kPanelAlphaTweenId, closing ? 0.0f : 1.0f, duration, curve);
    g_ui.panel_offset = md3::Tween(kPanelOffsetTweenId, closing ? kPanelSlidePx : 0.0f, duration, curve);

    const bool show_panel = g_ui.panel_alpha > 0.0f;
    if (show_panel) DrawPanel();
    g_panel_alpha = g_ui.panel_alpha;
    if (closing && g_ui.panel_alpha <= 0.0f && StateMove(MENU_CLOSING, MENU_CLOSED)) g_ui.was_visible = false;

    ImGui::GetStyle() = saved;
    return show_panel;
}

#endif // ENGINE_MENU_PURE_TEST
