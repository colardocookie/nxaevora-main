// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// The MD3 widgets of the mockup. Each widget is one ImGui item: ItemAdd and
// ButtonBehavior give it an id, so keyboard and gamepad navigation reach it.

#include "nx/gpu/vk/md3/md3_widgets.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

#include "imgui_internal.h"
#include "nx/gpu/vk/md3/md3_motion.h"
#include "nx/gpu/vk/md3/md3_theme.h"

namespace md3 {
namespace {

constexpr int kRowDepth = 8;
ImVec2 g_row_after[kRowDepth];
int g_row_depth = 0;

// Number of open cards. A row inside a card has no horizontal padding of its
// own, so that its text and its right control align on the card padding.
int g_card_depth = 0;

NavAnchor g_nav_anchor{};
bool g_capture_nav_anchor = false;

// Tween keys of the widget motion. Each key is one item id with one salt, so
// each item and each motion of an item gets its own tween.
constexpr TweenId kStateLayerSalt = 0x85EBCA6Bu;
constexpr TweenId kSwitchSalt = 0xC2B2AE35u;
constexpr TweenId kRailIndicatorSalt = 0x27D4EB2Fu;
constexpr TweenId kComboMenuSalt = 0x165667B1u;
constexpr TweenId kComboAlphaSalt = 0x6C8E9CF5u;
constexpr TweenId kSliderLabelSalt = 0xD3A2646Cu;
constexpr TweenId kRowHoverSalt = 0xFD7046C5u;
constexpr TweenId kDialogAlphaSalt = 0x51ED270Bu;

// The dialog keeps a small visible alpha at 0, so that BeginPopupModal still
// opens the window and the fade can close it from inside. A zero style alpha
// makes Begin skip the window (imgui.cpp:8382).
constexpr float kDialogMinAlpha = 1.0e-4f;
constexpr int kDialogFadeCount = 4;

// The dropdown state survives the frames of the close fade. Only one popup is
// open at a time, so the field id is enough.
ImGuiID g_combo_open_id = 0;
ImGuiID g_combo_closing_id = 0;

// The fade of each dialog: the popup key and the close flag.
struct DialogFade {
    ImGuiID id;
    bool closing;
};
DialogFade g_dialog_fades[kDialogFadeCount]{};

// The key and state of the DialogBegin/DialogEnd pair in progress.
ImGuiID g_dialog_pair_id = 0;
float g_dialog_pair_alpha = 1.0f;
bool g_dialog_pair_closing = false;

DialogFade* FindDialogFade(ImGuiID id) {
    for (DialogFade& fade : g_dialog_fades) {
        if (fade.id == id) return &fade;
    }
    for (DialogFade& fade : g_dialog_fades) {
        if (fade.id == 0) {
            fade.id = id;
            fade.closing = false;
            return &fade;
        }
    }
    return nullptr;
}

// ImGui 1.93 exposes the nav cursor state as ImGuiContext::NavCursorVisible
// (imgui_internal.h:2448). A mouse click clears it, so no focus ring follows.
bool NavCursorVisible() {
    ImGuiContext* context = ImGui::GetCurrentContext();
    return context != nullptr && context->NavCursorVisible;
}

int SnapToStep(int value, int min, int max, int step) {
    const int clamped = std::clamp(value, min, max);
    if (step <= 1) return clamped;
    const double offset = static_cast<double>(clamped - min) / static_cast<double>(step);
    const int snapped = min + static_cast<int>(std::lround(offset)) * step;
    return std::clamp(snapped, min, max);
}

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

ImVec2 TextSize(const char* text, float size) {
    ImFont* font = ImGui::GetFont();
    if (font == nullptr || text == nullptr) return ImVec2(0.0f, size);
    return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
}

void DrawTextAt(const char* text, ImVec2 pos, float size, ImU32 color, float alpha_mul = 1.0f) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImFont* font = ImGui::GetFont();
    if (draw == nullptr || font == nullptr || text == nullptr) return;
    draw->AddText(font, size, pos, FadeColor(color, alpha_mul), text);
}

void DrawTextCentered(const char* text, const ImRect& box, float size, ImU32 color,
                      float alpha_mul = 1.0f) {
    const ImVec2 text_size = TextSize(text, size);
    DrawTextAt(text, ImVec2(box.Min.x + (box.GetWidth() - text_size.x) * 0.5f,
                            box.Min.y + (box.GetHeight() - text_size.y) * 0.5f),
               size, color, alpha_mul);
}

void DrawIcon(uint32_t codepoint, const ImRect& box, float size, ImU32 color,
              float alpha_mul = 1.0f) {
    char utf8[5];
    EncodeUtf8(codepoint, utf8);
    DrawTextCentered(utf8, box, size, color, alpha_mul);
}

void DrawFocusRing(const ImRect& box, float rounding, const Theme& theme, bool focus_visible) {
    if (!focus_visible) return;
    ImGui::GetWindowDrawList()->AddRect(box.Min, box.Max, FadeColor(ToImU32(theme.scheme.secondary)),
                                        rounding, kFocusRingPx * theme.scale, 0);
}

struct ItemState {
    bool hovered;
    bool held;
    bool pressed;
    bool focused;
    bool focus_visible;
};

ItemState RunItem(const ImRect& box, ImGuiID id) {
    ImGui::ItemSize(box.GetSize());
    ImGui::ItemAdd(box, id);
    if (g_capture_nav_anchor) {
        g_nav_anchor.id = id;
        g_nav_anchor.min = box.Min;
        g_nav_anchor.max = box.Max;
        g_nav_anchor.window = ImGui::GetCurrentWindow();
        g_capture_nav_anchor = false;
    }
    ItemState state{};
    state.pressed = ImGui::ButtonBehavior(box, id, &state.hovered, &state.held);
    state.focused = ImGui::IsItemFocused();
    state.focus_visible = state.focused && NavCursorVisible();
    return state;
}

float TweenLayerAlpha(TweenId id, const ItemState& state) {
    const float target = state.held            ? kStatePress
                         : state.focus_visible ? kStateFocus
                         : state.hovered       ? kStateHover
                                               : 0.0f;
    return Tween(id ^ kStateLayerSalt, target, kDurationShortMs, kCurveStandard);
}

ImU32 LayerForState(TweenId id, ImU32 container, ImU32 on_color, const ItemState& state) {
    return StateLayer(container, on_color, TweenLayerAlpha(id, state));
}

// The visible area of the page window: the outermost child window above the
// widget, so the page itself for a row of a card of the page.
ImRect PageVisibleRect() {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    while ((window->Flags & ImGuiWindowFlags_ChildWindow) != 0 && window->ParentWindow != nullptr &&
           (window->ParentWindow->Flags & ImGuiWindowFlags_ChildWindow) != 0) {
        window = window->ParentWindow;
    }
    return window->InnerRect;
}

} // namespace

bool Button(const char* id, const char* label, ButtonKind kind) {
    const Theme& theme = CurrentTheme();
    const float s = theme.scale;
    const float height = kButtonHeightPx * s;
    const ImVec2 text_size = TextSize(label, theme.label_large);
    const float width = std::max(kButtonMinWidthPx * s, text_size.x + 2.0f * kButtonPaddingPx * s);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImRect box(origin, ImVec2(origin.x + width, origin.y + height));
    const ImGuiID item_id = ImGui::GetID(id);
    const ItemState state = RunItem(box, item_id);
    const float rounding = height * 0.5f;

    const ImU32 primary = ToImU32(theme.scheme.primary);
    const ImU32 on_primary = ToImU32(theme.scheme.on_primary);
    const ImU32 secondary_container = ToImU32(theme.scheme.secondary_container);
    const ImU32 on_secondary_container = ToImU32(theme.scheme.on_secondary_container);
    ImDrawList* draw = ImGui::GetWindowDrawList();

    ImU32 text_color = primary;
    if (kind == ButtonKind::Filled) {
        draw->AddRectFilled(box.Min, box.Max,
                            FadeColor(LayerForState(item_id, primary, on_primary, state)), rounding);
        text_color = on_primary;
    } else if (kind == ButtonKind::Tonal) {
        draw->AddRectFilled(
            box.Min, box.Max,
            FadeColor(LayerForState(item_id, secondary_container, on_secondary_container, state)),
            rounding);
        text_color = on_secondary_container;
    } else {
        const bool error_kind = kind == ButtonKind::ErrorOutlined;
        const ImU32 accent = error_kind ? ToImU32(theme.scheme.error) : primary;
        const float alpha = TweenLayerAlpha(item_id, state);
        if (alpha > 0.0f) {
            draw->AddRectFilled(box.Min, box.Max, FadeColor(StateLayerOver(accent, alpha)),
                                rounding);
        }
        if (kind == ButtonKind::Outlined || error_kind) {
            draw->AddRect(box.Min, box.Max,
                          FadeColor(error_kind ? ToImU32(theme.scheme.error)
                                               : ToImU32(theme.scheme.outline)),
                          rounding, 1.0f * s);
        }
        text_color = accent;
    }

    DrawTextCentered(label, box, theme.label_large, text_color);
    DrawFocusRing(box, rounding, theme, state.focus_visible);
    return state.pressed;
}

bool IconButton(const char* id, uint32_t icon_code) {
    const Theme& theme = CurrentTheme();
    const float s = theme.scale;
    const float size = kButtonHeightPx * s;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImRect box(origin, ImVec2(origin.x + size, origin.y + size));
    const ImGuiID item_id = ImGui::GetID(id);
    const ItemState state = RunItem(box, item_id);
    const float rounding = size * 0.5f;

    const float alpha = TweenLayerAlpha(item_id, state);
    if (alpha > 0.0f) {
        ImGui::GetWindowDrawList()->AddRectFilled(
            box.Min, box.Max,
            FadeColor(StateLayerOver(ToImU32(theme.scheme.on_surface), alpha)), rounding);
    }
    DrawIcon(icon_code, box, kIconSizePx * s, ToImU32(theme.scheme.on_surface_variant));
    DrawFocusRing(box, rounding, theme, state.focus_visible);
    return state.pressed;
}

bool Switch(const char* id, bool* value) {
    if (value == nullptr) return false;
    const Theme& theme = CurrentTheme();
    const float s = theme.scale;
    const float width = kSwitchWidthPx * s;
    const float height = kSwitchHeightPx * s;
    const float handle_on = kSwitchHandlePx * s;
    const float handle_off = kSwitchHandleUnselectedPx * s;
    const float margin = kSwitchHandleMarginPx * s;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImRect box(origin, ImVec2(origin.x + width, origin.y + height));
    const ImGuiID item_id = ImGui::GetID(id);
    const ItemState state = RunItem(box, item_id);
    if (state.pressed) {
        *value = !*value;
    }

    const float on_t = Tween(item_id ^ kSwitchSalt, *value ? 1.0f : 0.0f, kDurationShortLongMs,
                             kCurveStandard);
    const ImU32 outline = ToImU32(theme.scheme.outline);
    const ImU32 on_primary = ToImU32(theme.scheme.on_primary);
    ImU32 track = StateLayer(ToImU32(theme.scheme.surface_container_highest),
                             ToImU32(theme.scheme.primary), on_t);
    track = LayerForState(item_id, track, *value ? on_primary : ToImU32(theme.scheme.on_surface),
                          state);

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float rounding = height * 0.5f;
    draw->AddRectFilled(box.Min, box.Max, FadeColor(track), rounding);
    if (on_t < 1.0f) {
        draw->AddRect(box.Min, box.Max, FadeColor(StateLayerOver(outline, 1.0f - on_t)), rounding,
                      2.0f * s);
    }

    const float handle_diameter = handle_off + (handle_on - handle_off) * on_t;
    const float handle_radius = handle_diameter * 0.5f;
    const float off_center = box.Min.x + margin + handle_off * 0.5f;
    const float on_center = box.Max.x - margin - handle_on * 0.5f;
    const float handle_x = off_center + (on_center - off_center) * on_t;
    draw->AddCircleFilled(ImVec2(handle_x, box.Min.y + height * 0.5f), handle_radius,
                          FadeColor(StateLayer(outline, on_primary, on_t)), 0);
    DrawFocusRing(box, rounding, theme, state.focus_visible);
    return state.pressed;
}

bool NavItem(const char* id, uint32_t icon_code, const char* label, bool selected) {
    const Theme& theme = CurrentTheme();
    const float s = theme.scale;
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = kRailItemHeightPx * s;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImRect box(origin, ImVec2(origin.x + width, origin.y + height));
    const ImGuiID item_id = ImGui::GetID(id);
    const ItemState state = RunItem(box, item_id);

    const float select_t = Tween(item_id ^ kRailIndicatorSalt, selected ? 1.0f : 0.0f,
                                 kDurationShortLongMs, kCurveStandard);
    const float indicator_width = kRailIndicatorWidthPx * s;
    const float indicator_height = kRailIndicatorHeightPx * s;
    const ImRect indicator(
        ImVec2(box.Min.x + (width - indicator_width) * 0.5f, box.Min.y + kRailIndicatorTopPx * s),
        ImVec2(box.Min.x + (width + indicator_width) * 0.5f,
               box.Min.y + kRailIndicatorTopPx * s + indicator_height));

    const ImU32 on_secondary_container = ToImU32(theme.scheme.on_secondary_container);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (select_t > 0.0f) {
        const float active_width = indicator_width * select_t;
        const float center_x = (indicator.Min.x + indicator.Max.x) * 0.5f;
        draw->AddRectFilled(ImVec2(center_x - active_width * 0.5f, indicator.Min.y),
                            ImVec2(center_x + active_width * 0.5f, indicator.Max.y),
                            FadeColor(ToImU32(theme.scheme.secondary_container)),
                            indicator_height * 0.5f);
    }
    if (!selected) {
        const float alpha = TweenLayerAlpha(item_id, state);
        if (alpha > 0.0f) {
            draw->AddRectFilled(indicator.Min, indicator.Max,
                                FadeColor(StateLayerOver(ToImU32(theme.scheme.on_surface), alpha)),
                                indicator_height * 0.5f);
        }
    }

    const ImU32 icon_color =
        selected ? on_secondary_container : ToImU32(theme.scheme.on_surface_variant);
    DrawIcon(icon_code, indicator, kIconSizePx * s, icon_color);

    const ImU32 label_color = selected ? ToImU32(theme.scheme.on_surface) : icon_color;
    const ImRect label_box(ImVec2(box.Min.x, indicator.Max.y + kRailLabelGapPx * s),
                           ImVec2(box.Max.x, box.Max.y));
    const ImVec2 label_size = TextSize(label, kRailLabelPx * s);
    DrawTextAt(label, ImVec2(box.Min.x + (width - label_size.x) * 0.5f,
                             label_box.Min.y + (label_box.GetHeight() - label_size.y) * 0.5f),
               kRailLabelPx * s, label_color);
    DrawFocusRing(box, theme.corner_small, theme, state.focus_visible);
    return state.pressed;
}

void CardBegin(const char* id) {
    const Theme& theme = CurrentTheme();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ToImU32(theme.scheme.surface_container_low));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, theme.corner_medium);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(kCardPaddingPx * theme.scale, kCardPaddingPx * theme.scale));
    // A borderless child gets zero padding unless this flag is set
    // (imgui.cpp:7860 in 1.93.0).
    ImGui::BeginChild(id, ImVec2(0.0f, 0.0f),
                      ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_NavFlattened |
                          ImGuiChildFlags_AlwaysUseWindowPadding);
    g_card_depth += 1;
}

void CardEnd() {
    if (g_card_depth > 0) g_card_depth -= 1;
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

void SectionTitle(const char* text) {
    const Theme& theme = CurrentTheme();
    PushFontSize(kTitleMediumPx);
    ImGui::PushStyleColor(ImGuiCol_Text, ToImU32(theme.scheme.on_surface));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void BodyText(const char* text) {
    const Theme& theme = CurrentTheme();
    PushFontSize(kBodyMediumPx);
    ImGui::PushStyleColor(ImGuiCol_Text, ToImU32(theme.scheme.on_surface_variant));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

RowLayout RowBegin(const char* id, const char* label, const char* supporting, bool restart,
                   float right_width, float right_height) {
    const ImGuiID row_id = ImGui::GetID(id);
    const Theme& theme = CurrentTheme();
    const float s = theme.scale;
    const bool two_line = supporting != nullptr && supporting[0] != '\0';
    const float height = (two_line ? kRowHeightTwoLinePx : kRowHeightPx) * s;
    const float width = ImGui::GetContentRegionAvail().x;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImRect box(origin, ImVec2(origin.x + width, origin.y + height));

    ImGui::ItemSize(box.GetSize());
    const ImVec2 after = ImGui::GetCursorScreenPos();

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const bool row_hovered =
        ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(box.Min, box.Max);
    const float hover_alpha =
        Tween(row_id ^ kRowHoverSalt, row_hovered ? 1.0f : 0.0f, kDurationShortMs, kCurveStandard);
    if (hover_alpha > 0.0f) {
        draw->AddRectFilled(
            box.Min, box.Max,
            FadeColor(StateLayerOver(ToImU32(theme.scheme.on_surface), 0.04f * hover_alpha)),
            theme.corner_small);
    }

    const bool inside_card = g_card_depth > 0;
    const float pad = inside_card ? 0.0f : kRowPaddingPx * s;
    float text_x = box.Min.x + pad;
    const ImU32 on_surface = ToImU32(theme.scheme.on_surface);
    if (restart) {
        const float icon_size = kSmallIconSizePx * s;
        const ImRect icon_box(ImVec2(text_x, box.Min.y + (height - icon_size) * 0.5f),
                              ImVec2(text_x + icon_size, box.Min.y + (height + icon_size) * 0.5f));
        DrawIcon(icon::kRestart, icon_box, icon_size, ToImU32(theme.scheme.on_surface_variant));
        text_x += icon_size + kRowIconGapPx * s;
    }

    if (two_line) {
        // Center the two lines as one block, so a larger font cannot leave the row.
        const float block = theme.body_large + kTwoLineGapPx * s + theme.body_medium;
        const float top = box.Min.y + (height - block) * 0.5f;
        DrawTextAt(label, ImVec2(text_x, top), theme.body_large, on_surface);
        DrawTextAt(supporting, ImVec2(text_x, top + theme.body_large + kTwoLineGapPx * s),
                   theme.body_medium, ToImU32(theme.scheme.on_surface_variant));
    } else {
        const ImVec2 label_size = TextSize(label, theme.body_large);
        DrawTextAt(label, ImVec2(text_x, box.Min.y + (height - label_size.y) * 0.5f),
                   theme.body_large, on_surface);
    }

    RowLayout layout{};
    layout.right_width = right_width;
    layout.right_origin =
        ImVec2(box.Max.x - pad - right_width, box.Min.y + (height - right_height) * 0.5f);
    ImGui::SetCursorScreenPos(layout.right_origin);

    if (g_row_depth < kRowDepth) {
        g_row_after[g_row_depth] = after;
        g_row_depth += 1;
    }
    return layout;
}

void RowEnd() {
    if (g_row_depth <= 0) return;
    g_row_depth -= 1;
    // ImGui 1.93 asserts if a window ends on a cursor move that grows the contents.
    // Submit an empty item one ItemSpacing above the stored position: ItemSize adds the
    // spacing back, so the next row starts at the stored position and IsSetPos is cleared.
    const ImVec2 after = g_row_after[g_row_depth];
    ImGui::SetCursorScreenPos(ImVec2(after.x, after.y - ImGui::GetStyle().ItemSpacing.y));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

bool Combo(const char* id, int* index, const char* const* items, int count) {
    if (id == nullptr || index == nullptr || items == nullptr || count <= 0) return false;
    const Theme& theme = CurrentTheme();
    const float s = theme.scale;
    const int current = (*index >= 0 && *index < count) ? *index : 0;

    ImGui::PushID(id);
    const ImGuiID field_id = ImGui::GetID("##field");

    const float width = ImGui::GetContentRegionAvail().x;
    const float height = kDropdownHeightPx * s;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImRect box(origin, ImVec2(origin.x + width, origin.y + height));
    const ItemState state = RunItem(box, field_id);

    const bool was_open = g_combo_open_id == field_id;
    bool closing = g_combo_closing_id == field_id;
    bool open = ImGui::IsPopupOpen("##menu");
    const bool cancel_pressed = ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
                                ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false);
    const bool outside_click = ImGui::IsMouseClicked(ImGuiMouseButton_Left);

    // ImGui closes the popup in NewFrame on Escape, B or a click outside
    // (imgui.cpp:14603, 5556). Reopen it for the fade; a later popup that takes
    // the level instead abandons the fade.
    if (!closing && was_open && !open && (cancel_pressed || outside_click)) {
        closing = true;
        g_combo_closing_id = field_id;
        ResetTween(field_id ^ kComboAlphaSalt, 1.0f);
        ImGui::OpenPopup("##menu");
        open = true;
    }
    if (!closing && open && cancel_pressed) {
        closing = true;
        g_combo_closing_id = field_id;
        ResetTween(field_id ^ kComboAlphaSalt, 1.0f);
    }
    if (closing && !open) {
        closing = false;
        g_combo_closing_id = 0;
    }
    if (state.pressed && !closing && !was_open && !open) {
        ImGui::OpenPopup("##menu");
        ResetTween(field_id ^ kComboMenuSalt, 0.0f);
        ResetTween(field_id ^ kComboAlphaSalt, 0.0f);
        open = true;
    }
    const bool active = open || state.focus_visible;

    const ImU32 surface = ToImU32(theme.scheme.surface_container_highest);
    const ImU32 on_surface = ToImU32(theme.scheme.on_surface);
    const ImU32 on_surface_variant = ToImU32(theme.scheme.on_surface_variant);
    const ImU32 primary = ToImU32(theme.scheme.primary);
    const float corner = kDropdownCornerPx * s;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float field_layer =
        Tween(field_id ^ kStateLayerSalt, (state.hovered && !active) ? kStateHover : 0.0f,
              kDurationShortMs, kCurveStandard);
    const ImU32 background =
        field_layer > 0.0f ? StateLayer(surface, on_surface, field_layer) : surface;
    draw->AddRectFilled(box.Min, box.Max, FadeColor(background), corner, ImDrawFlags_RoundCornersTop);

    const ImVec2 text_size = TextSize(items[current], theme.body_large);
    DrawTextAt(items[current],
               ImVec2(box.Min.x + kDropdownPaddingPx * s,
                      box.Min.y + (height - text_size.y) * 0.5f),
               theme.body_large, on_surface);

    const float icon_px = kDropdownIconPx * s;
    const ImRect icon_box(
        ImVec2(box.Max.x - kDropdownPaddingPx * s - icon_px, box.Min.y + (height - icon_px) * 0.5f),
        ImVec2(box.Max.x - kDropdownPaddingPx * s, box.Min.y + (height + icon_px) * 0.5f));
    DrawIcon(open ? icon::kArrowDropUp : icon::kArrowDropDown, icon_box, icon_px,
             on_surface_variant);

    const float indicator = (active ? kDropdownIndicatorActivePx : kDropdownIndicatorPx) * s;
    draw->AddRectFilled(ImVec2(box.Min.x, box.Max.y - indicator), box.Max,
                        FadeColor(active ? primary : on_surface_variant), 0.0f);

    bool changed = false;
    const float item_height = kMenuItemHeightPx * s;
    const float menu_spacing = ImGui::GetStyle().ItemSpacing.y;
    const float full_height = 2.0f * kMenuVerticalPaddingPx * s +
                              static_cast<float>(count) * item_height +
                              static_cast<float>(count - 1) * menu_spacing;
    float menu_progress = 1.0f;
    float menu_alpha = 0.0f;
    if (open) {
        menu_progress = Tween(field_id ^ kComboMenuSalt, 1.0f, kDurationMediumMs,
                              kCurveEmphasizedDecelerate);
        menu_alpha = Tween(field_id ^ kComboAlphaSalt, closing ? 0.0f : 1.0f,
                           closing ? kDurationShortMs : kDurationMediumMs,
                           closing ? kCurveStandard : kCurveEmphasizedDecelerate);
    }

    // Decision 1: the menu keeps the left edge and the width of the field and
    // stays in the visible area of the page, kMenuEdgeMarginPx from its edge.
    // It opens below the field, else above it, else on the larger side with a
    // limited height. It never covers the field.
    const ImRect area = PageVisibleRect();
    const float margin = kMenuEdgeMarginPx * s;
    const float below_space = area.Max.y - margin - box.Max.y;
    const float above_space = box.Min.y - (area.Min.y + margin);
    bool open_up = false;
    float menu_height = full_height;
    if (below_space < full_height) {
        if (above_space >= full_height) {
            open_up = true;
        } else if (above_space > below_space) {
            open_up = true;
            menu_height = std::max(above_space, item_height);
        } else {
            menu_height = std::max(below_space, item_height);
        }
    }
    // ImGui truncates a window position, so round the top up: the menu must
    // not cover the field by a subpixel.
    const ImVec2 menu_pos(box.Min.x, open_up ? box.Min.y - menu_height : ImCeil(box.Max.y));

    ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, kMenuCornerPx * s);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(0.0f, kMenuVerticalPaddingPx * s));
    // A position from the API makes Begin skip FindBestWindowPosForPopup
    // (imgui.cpp:7983, 13059) and ClampWindowPos (imgui.cpp:7998), and the size
    // replaces the forced AlwaysAutoResize (imgui.cpp:7918-7923).
    ImGui::SetNextWindowPos(menu_pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, menu_height), ImGuiCond_Always);
    const bool close_now = closing && menu_alpha <= 0.0f;
    bool kept_open = false;
    ImGuiWindowFlags menu_flags = ImGuiWindowFlags_NoScrollbar;
    if (closing) menu_flags |= ImGuiWindowFlags_NoInputs;
    if (ImGui::BeginPopup("##menu", menu_flags)) {
        kept_open = true;
        // Decision 1: a limited menu opens with the selected item in view. The
        // target applies at the next Begin, before the menu is drawn.
        if (!closing && ImGui::IsWindowAppearing() && menu_height < full_height) {
            const float item_top = kMenuVerticalPaddingPx * s +
                                   static_cast<float>(current) * (item_height + menu_spacing);
            const float item_bottom = item_top + item_height;
            if (item_bottom > menu_height) ImGui::SetScrollY(item_bottom - menu_height);
        }
        if (close_now) {
            ImGui::CloseCurrentPopup();
        } else {
            const ImVec2 window_pos = ImGui::GetWindowPos();
            // Decision 2: the menu grows from the field, downward or upward.
            const float shown_height = menu_height * menu_progress;
            const float reveal_top =
                open_up ? window_pos.y + menu_height - shown_height : window_pos.y;
            ImDrawList* popup_draw = ImGui::GetWindowDrawList();
            popup_draw->AddRectFilled(
                ImVec2(window_pos.x, reveal_top),
                ImVec2(window_pos.x + width, reveal_top + shown_height),
                FadeColor(ToImU32(theme.scheme.surface_container), menu_alpha), kMenuCornerPx * s);
            popup_draw->PushClipRect(ImVec2(window_pos.x, reveal_top),
                                     ImVec2(window_pos.x + width, reveal_top + shown_height), true);
            const float item_width = ImGui::GetContentRegionAvail().x;
            const ImU32 selected_bg = ToImU32(theme.scheme.secondary_container);
            const ImU32 selected_text = ToImU32(theme.scheme.on_secondary_container);
            for (int i = 0; i < count; ++i) {
                ImGui::PushID(i);
                const ImVec2 item_origin = ImGui::GetCursorScreenPos();
                const ImRect item_box(
                    item_origin, ImVec2(item_origin.x + item_width, item_origin.y + item_height));
                const ImGuiID item_id = ImGui::GetID("##item");
                ItemState item_state = RunItem(item_box, item_id);
                if (closing) item_state = ItemState{};
                const bool selected = i == current;
                const float layer = TweenLayerAlpha(item_id, item_state);
                ImDrawList* menu_draw = ImGui::GetWindowDrawList();
                if (selected) {
                    menu_draw->AddRectFilled(
                        item_box.Min, item_box.Max,
                        FadeColor(layer > 0.0f ? StateLayer(selected_bg, on_surface, layer)
                                               : selected_bg,
                                  menu_alpha));
                } else if (layer > 0.0f) {
                    menu_draw->AddRectFilled(item_box.Min, item_box.Max,
                                             FadeColor(StateLayerOver(on_surface, layer), menu_alpha));
                }
                const ImVec2 item_text_size = TextSize(items[i], theme.body_large);
                DrawTextAt(items[i],
                           ImVec2(item_box.Min.x + kMenuItemPaddingPx * s,
                                  item_box.Min.y + (item_height - item_text_size.y) * 0.5f),
                           theme.body_large, selected ? selected_text : on_surface, menu_alpha);
                if (item_state.pressed) {
                    if (!selected) {
                        *index = i;
                        changed = true;
                    }
                    closing = true;
                    g_combo_closing_id = field_id;
                    ResetTween(field_id ^ kComboAlphaSalt, menu_alpha);
                }
                if (selected && !closing) ImGui::SetItemDefaultFocus();
                ImGui::PopID();
            }
            popup_draw->PopClipRect();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();
    ImGui::PopID();

    if (kept_open && !close_now) {
        g_combo_open_id = field_id;
    } else if (g_combo_open_id == field_id) {
        g_combo_open_id = 0;
    }
    if (close_now) g_combo_closing_id = 0;
    return changed;
}

bool SliderInt(const char* id, int* value, int min, int max, int step, const char* format) {
    if (id == nullptr || value == nullptr || min > max) return false;
    const Theme& theme = CurrentTheme();
    const float s = theme.scale;
    char slider_label[160];
    std::snprintf(slider_label, sizeof(slider_label), "%s##slider", id);
    const ImGuiID slider_id = ImGui::GetID(slider_label);
    const char* fmt = (format != nullptr && format[0] != '\0') ? format : "%d";

    const float width = ImGui::GetContentRegionAvail().x;
    const float height = kSliderItemHeightPx * s;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImRect box(origin, ImVec2(origin.x + width, origin.y + height));
    const ItemState state = RunItem(box, slider_id);

    char text_min[32];
    char text_max[32];
    std::snprintf(text_min, sizeof(text_min), fmt, min);
    std::snprintf(text_max, sizeof(text_max), fmt, max);
    const float value_width = std::max(TextSize(text_min, theme.label_large).x,
                                       TextSize(text_max, theme.label_large).x);
    const float value_area = value_width + kSliderTextGapPx * s;
    ImRect track_box(box.Min, ImVec2(box.Max.x - value_area, box.Max.y));
    const float handle_radius = kSliderHandleRadiusPx * s;
    const float usable = std::max(0.0f, track_box.GetWidth() - 2.0f * handle_radius);
    const float track_y = box.Min.y + height * 0.5f;

    bool changed = false;
    ImGuiContext* context = ImGui::GetCurrentContext();
    const bool mouse_dragging =
        state.held && context != nullptr && context->ActiveIdSource == ImGuiInputSource_Mouse;
    if (mouse_dragging && usable > 0.0f) {
        const float t = std::clamp(
            (ImGui::GetIO().MousePos.x - (track_box.Min.x + handle_radius)) / usable, 0.0f, 1.0f);
        const int candidate = SnapToStep(
            min + static_cast<int>(std::lround(t * static_cast<float>(max - min))), min, max, step);
        if (candidate != *value) {
            *value = candidate;
            changed = true;
        }
    }

    const int increment = step > 1 ? step : 1;
    const bool left_down = ImGui::IsKeyDown(ImGuiKey_GamepadDpadLeft) ||
                           ImGui::IsKeyDown(ImGuiKey_LeftArrow);
    const bool right_down = ImGui::IsKeyDown(ImGuiKey_GamepadDpadRight) ||
                            ImGui::IsKeyDown(ImGuiKey_RightArrow);
    const bool nav_right =
        state.focus_visible && (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight, true) ||
                                ImGui::IsKeyPressed(ImGuiKey_RightArrow, true));
    const bool nav_left =
        state.focus_visible && (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft, true) ||
                                ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true));
    if (nav_right || nav_left) {
        const int next = SnapToStep(*value + (nav_right ? increment : -increment), min, max, step);
        if (next != *value) {
            *value = next;
            changed = true;
        }
        // The left/right keys adjust the value here, so they do not move the nav.
        ImGui::NavMoveRequestCancel();
    }

    const float fraction = (max > min) ? static_cast<float>(*value - min) / static_cast<float>(max - min)
                                       : 0.0f;
    const float handle_x = track_box.Min.x + handle_radius + std::clamp(fraction, 0.0f, 1.0f) * usable;
    const float half_track = kSliderTrackPx * s * 0.5f;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(ImVec2(track_box.Min.x, track_y - half_track),
                        ImVec2(handle_x, track_y + half_track),
                        FadeColor(ToImU32(theme.scheme.primary)), half_track);
    draw->AddRectFilled(ImVec2(handle_x, track_y - half_track),
                        ImVec2(track_box.Max.x, track_y + half_track),
                        FadeColor(ToImU32(theme.scheme.secondary_container)), half_track);

    const float layer_alpha = TweenLayerAlpha(slider_id, state);
    if (layer_alpha > 0.0f) {
        draw->AddCircleFilled(
            ImVec2(handle_x, track_y), kSliderStateRadiusPx * s,
            FadeColor(StateLayerOver(ToImU32(theme.scheme.primary), layer_alpha)), 0);
    }
    draw->AddCircleFilled(ImVec2(handle_x, track_y), handle_radius,
                          FadeColor(ToImU32(theme.scheme.primary)), 0);

    char value_text[32];
    std::snprintf(value_text, sizeof(value_text), fmt, *value);
    const ImVec2 value_size = TextSize(value_text, theme.label_large);
    DrawTextAt(value_text,
               ImVec2(box.Max.x - value_size.x, box.Min.y + (height - value_size.y) * 0.5f),
               theme.label_large, ToImU32(theme.scheme.on_surface_variant));

    const bool value_shown =
        mouse_dragging || (state.focus_visible && (left_down || right_down));
    const float label_alpha =
        Tween(slider_id ^ kSliderLabelSalt, value_shown ? 1.0f : 0.0f, kDurationShortMs,
              kCurveStandard);
    if (label_alpha > 0.0f) {
        const float pill_height = kSliderValueHeightPx * s;
        const float pill_width = value_size.x + 2.0f * kSliderValuePaddingPx * s;
        const float half_pill = pill_width * 0.5f;
        const float max_center = std::max(box.Min.x + half_pill, box.Max.x - half_pill);
        const float pill_center = std::clamp(handle_x, box.Min.x + half_pill, max_center);
        const float pill_bottom = track_y - kSliderStateRadiusPx * s - kSliderValueGapPx * s;
        const ImRect pill(ImVec2(pill_center - half_pill, pill_bottom - pill_height),
                          ImVec2(pill_center + half_pill, pill_bottom));
        draw->AddRectFilled(pill.Min, pill.Max,
                            FadeColor(StateLayerOver(ToImU32(theme.scheme.primary), label_alpha)),
                            pill_height * 0.5f);
        DrawTextCentered(value_text, pill, theme.label_large,
                         StateLayerOver(ToImU32(theme.scheme.on_primary), label_alpha));
    }
    return changed;
}

void Chip(const char* text) {
    const Theme& theme = CurrentTheme();
    const float s = theme.scale;
    const ImVec2 text_size = TextSize(text, theme.body_medium);
    const float padding_x = 10.0f * s;
    const float padding_y = 5.0f * s;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImRect box(origin, ImVec2(origin.x + text_size.x + 2.0f * padding_x,
                                    origin.y + text_size.y + 2.0f * padding_y));
    ImGui::ItemSize(box.GetSize());
    ImGui::GetWindowDrawList()->AddRectFilled(
        box.Min, box.Max, FadeColor(ToImU32(theme.scheme.surface_container_highest)),
        theme.corner_small);
    DrawTextCentered(text, box, theme.body_medium, ToImU32(theme.scheme.on_surface_variant));
}

void DialogOpen(const char* id) {
    if (id == nullptr) return;
    ImGui::OpenPopup(id);
    const ImGuiID key = ImGui::GetID(id);
    if (DialogFade* fade = FindDialogFade(key)) fade->closing = false;
    ResetTween(key ^ kDialogAlphaSalt, 0.0f);
}

void DialogClose() {
    if (g_dialog_pair_id == 0) return;
    if (DialogFade* fade = FindDialogFade(g_dialog_pair_id)) fade->closing = true;
}

bool DialogBegin(const char* id, const char* header) {
    const Theme& theme = CurrentTheme();
    const ImGuiID key = ImGui::GetID(id);
    DialogFade* fade = FindDialogFade(key);
    const bool was_open = ImGui::IsPopupOpen(id, ImGuiPopupFlags_None);
    const bool closing = fade != nullptr && fade->closing;

    float alpha = 1.0f;
    if (was_open) {
        alpha = Tween(key ^ kDialogAlphaSalt, closing ? 0.0f : 1.0f,
                      closing ? kDurationShortLongMs : kDurationMediumMs,
                      closing ? kCurveEmphasizedAccelerate : kCurveEmphasizedDecelerate);
    }
    const float shown_alpha = alpha > 0.0f ? alpha : kDialogMinAlpha;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 center(viewport->Pos.x + viewport->Size.x * 0.5f,
                        viewport->Pos.y + viewport->Size.y * 0.5f);
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    // The scrim color is captured at Begin (imgui.cpp:8277), so scale its alpha
    // here to fade the dim background with the dialog.
    const ImVec4 dim = ImGui::GetStyleColorVec4(ImGuiCol_ModalWindowDimBg);
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg,
                          ImVec4(dim.x, dim.y, dim.z, dim.w * shown_alpha));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, shown_alpha);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ToImU32(theme.scheme.surface_container_high));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, theme.corner_large);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(24.0f * theme.scale, 20.0f * theme.scale));
    ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove;
    if (closing) flags |= ImGuiWindowFlags_NoInputs;
    const bool open = ImGui::BeginPopupModal(id, nullptr, flags);
    g_dialog_pair_id = key;
    g_dialog_pair_alpha = alpha;
    g_dialog_pair_closing = closing;
    if (open) {
        // A modal cannot be closed by its user, so the mockup starts the fade.
        if (!closing && (ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
                         ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false))) {
            if (fade != nullptr) fade->closing = true;
        }
        PushFontSize(kTitleMediumPx);
        ImGui::TextUnformatted(header);
        ImGui::PopFont();
        ImGui::Spacing();
    }
    return open;
}

void DialogEnd(bool open) {
    if (open && g_dialog_pair_closing && g_dialog_pair_alpha <= 0.0f) {
        ImGui::CloseCurrentPopup();
        if (DialogFade* fade = FindDialogFade(g_dialog_pair_id)) fade->closing = false;
    }
    if (open) ImGui::EndPopup();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
    g_dialog_pair_id = 0;
}

bool AnyPopupOpen() {
    return ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
}

void BeginNavAnchorCapture() {
    g_nav_anchor = NavAnchor{};
    g_capture_nav_anchor = true;
}

NavAnchor EndNavAnchorCapture() {
    g_capture_nav_anchor = false;
    return g_nav_anchor;
}

} // namespace md3
