// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// The MD3 widgets of the mockup. Each widget is one ImGui item: ItemAdd and
// ButtonBehavior give it an id, so keyboard and gamepad navigation move to it.
// The focus ring and the focus state layer use ImGuiContext::NavCursorVisible,
// so a mouse click leaves no focus ring.

#pragma once

#include <cstdint>

#include "imgui.h"

struct ImGuiWindow;

namespace md3 {

// The Material Symbols Rounded code points of the icons that the mockup uses.
// Source: google/material-design-icons, variablefont/
// MaterialSymbolsRounded[FILL,GRAD,opsz,wght].codepoints (Apache-2.0), read on
// 2026-11-25.
namespace icon {
inline constexpr uint32_t kGame = 0xEA28;             // sports_esports
inline constexpr uint32_t kDisplay = 0xE30C;          // desktop_windows
inline constexpr uint32_t kGraphics = 0xE40A;         // palette
inline constexpr uint32_t kPerformance = 0xE9E4;      // speed
inline constexpr uint32_t kMouse = 0xE323;            // mouse
inline constexpr uint32_t kKeyboard = 0xE312;         // keyboard
inline constexpr uint32_t kControllers = 0xE30F;      // gamepad
inline constexpr uint32_t kOnScreenControls = 0xE913; // touch_app
inline constexpr uint32_t kDebug = 0xE868;            // bug_report
inline constexpr uint32_t kAbout = 0xE88E;            // info
inline constexpr uint32_t kClose = 0xE5CD;            // close
inline constexpr uint32_t kBuild = 0xF8CD;            // build (wrench)
inline constexpr uint32_t kRestart = 0xE5D5;          // refresh
inline constexpr uint32_t kArrowDropDown = 0xE5C5;    // arrow_drop_down
inline constexpr uint32_t kArrowDropUp = 0xE5C7;      // arrow_drop_up
} // namespace icon

// Sizes in pixels at 1080p. Each one is multiplied by the theme scale.
inline constexpr float kSwitchWidthPx = 44.0f;
inline constexpr float kSwitchHeightPx = 26.0f;
inline constexpr float kSwitchHandlePx = 20.0f;
// The handle grows from this diameter to kSwitchHandlePx when the switch is on.
inline constexpr float kSwitchHandleUnselectedPx = 16.0f;
inline constexpr float kSwitchHandleMarginPx = 3.0f;
inline constexpr float kButtonHeightPx = 36.0f;
inline constexpr float kButtonMinWidthPx = 64.0f;
inline constexpr float kButtonPaddingPx = 20.0f;
inline constexpr float kRowHeightPx = 48.0f;
inline constexpr float kRowHeightTwoLinePx = 64.0f;
inline constexpr float kRailItemHeightPx = 52.0f;
inline constexpr float kRailIndicatorWidthPx = 48.0f;
inline constexpr float kRailIndicatorHeightPx = 28.0f;
inline constexpr float kRailIndicatorTopPx = 4.0f;
inline constexpr float kRailLabelPx = 12.0f;
inline constexpr float kRailLabelGapPx = 2.0f;
inline constexpr float kIconSizePx = 20.0f;
inline constexpr float kSmallIconSizePx = 16.0f;
inline constexpr float kFocusRingPx = 2.0f;
inline constexpr float kStateHover = 0.08f;
inline constexpr float kStateFocus = 0.10f;
inline constexpr float kStatePress = 0.10f;
inline constexpr float kRowPaddingPx = 16.0f;
// Inner padding of a card on all 4 sides. A row inside a card drops its own
// horizontal padding, so every item of the card starts at the card padding.
inline constexpr float kCardPaddingPx = 16.0f;
inline constexpr float kRowIconGapPx = 6.0f;
inline constexpr float kTwoLineGapPx = 2.0f;

// Exposed dropdown menu (decision 5 of the UI brief).
inline constexpr float kDropdownHeightPx = 44.0f;
inline constexpr float kDropdownCornerPx = 4.0f;
inline constexpr float kDropdownPaddingPx = 12.0f;
inline constexpr float kDropdownIconPx = 20.0f;
inline constexpr float kDropdownIndicatorPx = 1.0f;
inline constexpr float kDropdownIndicatorActivePx = 2.0f;
inline constexpr float kMenuCornerPx = 4.0f;
inline constexpr float kMenuItemHeightPx = 40.0f;
inline constexpr float kMenuItemPaddingPx = 12.0f;
inline constexpr float kMenuVerticalPaddingPx = 8.0f;
// Distance of the menu to the edge of the visible area of the page.
inline constexpr float kMenuEdgeMarginPx = 8.0f;

// Slider (decision 6 of the UI brief).
inline constexpr float kSliderItemHeightPx = 32.0f;
inline constexpr float kSliderTrackPx = 4.0f;
inline constexpr float kSliderHandleRadiusPx = 8.0f;
inline constexpr float kSliderStateRadiusPx = 16.0f;
inline constexpr float kSliderValueHeightPx = 24.0f;
inline constexpr float kSliderValuePaddingPx = 10.0f;
inline constexpr float kSliderValueGapPx = 2.0f;
inline constexpr float kSliderTextGapPx = 8.0f;

enum class ButtonKind { Filled, Tonal, Outlined, Text, ErrorOutlined };

// Draws a button of the given MD3 kind. Returns true when it is pressed.
bool Button(const char* id, const char* label, ButtonKind kind);

// Draws a round icon button of 36x36 at 1080p.
bool IconButton(const char* id, uint32_t icon_code);

// Draws a switch of 44x26 at 1080p and toggles the value when it is pressed.
bool Switch(const char* id, bool* value);

// Draws a navigation rail item: the icon over the label, with the active
// indicator pill behind the icon when selected.
bool NavItem(const char* id, uint32_t icon_code, const char* label, bool selected);

// A card: a rounded surfaceContainerLow child that sizes itself to its content.
void CardBegin(const char* id);
void CardEnd();

// A title medium line at the top of a page section.
void SectionTitle(const char* text);

// A body text line wrapped to the available width.
void BodyText(const char* text);

struct RowLayout {
    ImVec2 right_origin;
    float right_width;
    bool focused;
};

// Draws the row background, the label, the optional supporting text and the
// restart mark, advances the row, and moves the cursor to the right part. The
// caller draws one right part of right_width x right_height pixels, then calls
// RowEnd().
RowLayout RowBegin(const char* id, const char* label, const char* supporting, bool restart,
                   float right_width, float right_height);
void RowEnd();

// A dropdown field. The items are shown in a popup. Returns true on a change.
bool Combo(const char* id, int* index, const char* const* items, int count);

// A slider with an optional step. Returns true on a change.
bool SliderInt(const char* id, int* value, int min, int max, int step, const char* format);

// A read-only chip.
void Chip(const char* text);

// Opens the dialog of this id. BeginPopupModal with the MD3 dialog look.
void DialogOpen(const char* id);
// Starts the close fade of the dialog that DialogBegin began. Call it in place
// of ImGui::CloseCurrentPopup().
void DialogClose();
bool DialogBegin(const char* id, const char* header);
// Must be called once for each DialogBegin; it pops only what BeginPopup pushed.
void DialogEnd(bool open);

// True when any ImGui popup or dialog is open (a dropdown or a dialog).
bool AnyPopupOpen();

// The id and screen box of the first nav item drawn inside the page. The rail
// uses it to send D-pad right to the first page control.
struct NavAnchor {
    ImGuiID id;
    ImVec2 min;
    ImVec2 max;
    ImGuiWindow* window;
};

void BeginNavAnchorCapture();
NavAnchor EndNavAnchorCapture();

} // namespace md3
