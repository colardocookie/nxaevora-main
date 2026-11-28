// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 the NXAevora contributors
// The MD3 widgets of the mockup. Each widget is one ImGui item: ItemAdd and
// ButtonBehavior give it an id, so keyboard and gamepad navigation move to it.
// The focus ring and the focus state layer use ImGuiContext::NavCursorVisible,
// so a mouse click leaves no focus ring.

#pragma once

#include <stddef.h>

#include <cstdint>

#include "imgui.h"
#include "nx/gpu/vk/md3/md3_layout.h"

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
inline constexpr uint32_t kExpandMore = 0xE5CF;       // expand_more
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
// Gap between the text column and the right part of a settings row (the MD3
// list item gap), 16 px at 1080p.
inline constexpr float kRowTextGapPx = 16.0f;

// Exposed dropdown menu (decision 5 of the UI brief).
inline constexpr float kDropdownHeightPx = 44.0f;
inline constexpr float kDropdownCornerPx = 4.0f;
inline constexpr float kDropdownPaddingPx = 12.0f;
inline constexpr float kDropdownIconPx = 20.0f;
inline constexpr float kDropdownIndicatorPx = 1.0f;
inline constexpr float kDropdownIndicatorActivePx = 2.0f;
// Outline and caret of the number field, 1px at 1080p.
inline constexpr float kTextFieldOutlinePx = 1.0f;
inline constexpr float kTextFieldCaretPx = 1.0f;
inline constexpr float kMenuCornerPx = 4.0f;
inline constexpr float kMenuItemHeightPx = 40.0f;
inline constexpr float kMenuItemPaddingPx = 12.0f;
inline constexpr float kMenuVerticalPaddingPx = 8.0f;
// Distance of the menu to the edge of the visible area of the page.
inline constexpr float kMenuEdgeMarginPx = 8.0f;

// Frame time graph: the line over the last samples, the dashed target line.
inline constexpr int kFrameGraphSamples = 120;
inline constexpr float kFrameGraphHeightPx = 48.0f;
inline constexpr float kFrameGraphLinePx = 1.5f;
inline constexpr float kFrameGraphDashPx = 4.0f;
inline constexpr float kFrameGraphDashGapPx = 4.0f;
inline constexpr float kFrameGraphLabelGapPx = 2.0f;
// A sample above this factor of the target is drawn in the error color.
inline constexpr float kFrameGraphSpikeFactor = 1.5f;

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

// The height that the last card grew by in this frame, above the size that
// ImGui gave it at Begin from the last frame's content. The page scroll adds it
// so the clamp uses this frame's content (decision 6).
float LastCardGrowth();

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

// A full-width row that opens and closes a section. The expand_more icon at
// the right turns 180 degrees with the open state. Returns true on a toggle.
bool ExpandHeader(const char* id, const char* label, bool* open);

// Draws the last count samples of ms in a primary line, a dashed reference
// line at target_ms with its value at the right end, and each sample above
// 1.5 x target_ms in the error color. It takes no input.
void FrameTimeGraph(const char* id, const float* ms, int count, float target_ms, ImVec2 size);

// An MD3 outlined text field of the dropdown height, the same look as the
// number field. A click, Enter or A starts the edit. Enter or a loss of focus
// commits into value (size of the line, as snprintf takes it), Esc cancels. A
// page change commits the edit and a menu close cancels it. Returns true on a
// commit whose text differs from value. A size above kTextFieldMax draws
// nothing and returns false.
bool TextField(const char* id, char* value, size_t size);

// An MD3 outlined number field of the dropdown height. A click, Enter or A
// starts the edit. Enter or a loss of focus commits, Esc cancels, and only
// digits go in. A page change commits the edit and a menu close cancels it.
// Returns true on each valid commit and each Left/Right step that changes it.
bool NumberField(const char* id, int* value, int lo, int hi, int step);

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

// True while a number or text field of this frame or the frame before takes the text keys.
bool TextEditing();

// Cancels every number and text field edit without a commit. Call it when the menu closes.
void CancelTextEditing();

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

// The row of a settings page that holds the nav item the keyboard or the gamepad
// moved to in the frame just drawn. top and bottom are in the page's content
// coordinates; index and count are among the rows with a right part.
struct FocusedRow {
    bool valid;
    ImGuiID nav_id;
    float top;
    float bottom;
    int index;
    int count;
};

FocusedRow LastFocusedRow();

} // namespace md3
