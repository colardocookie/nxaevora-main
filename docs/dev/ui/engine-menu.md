<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the NXAevora contributors -->

# Engine menu design

This document is the design of the engine menu, the in-game user interface of NXAevora Engine. It tells the scope, the frame, the theme, the components, the motion, the fades, the scrolling, the ImGui rules and the migration. It is for contributors who build the menu or change its look.

The base of the menu is Dear ImGui 1.93.0 with its own Material Design 3 (MD3) widgets. RmlUi was dropped as the UI base. An accepted standalone mockup is the source of the design. All sizes in this document are pixels at 1080p, unless a rule says otherwise. The theme scale multiplies each one (section 5).

## 1. Scope

The engine menu holds all current ImGui screens and the functions of the legacy overlay menu:
- the settings of the game, the display, the graphics, the performance, the mouse and the keyboard,
- the controller map,
- the on-screen controls setup,
- the save states,
- the stats and the showcase on and off,
- the exit.

The legacy overlay menu is deleted. The menu has one shell for all these functions.

These parts stay as they are for now, and the user decides them later:
- the toasts,
- the shader prepare screen,
- the fatal screen,
- the stats and showcase drawing,
- the HUD,
- the on-screen controls drawing.

The start GUI in `engine/src/gui/` is out of scope. The menu has no UI sounds.

## 2. Open and close

The menu opens and closes with these inputs:
- the **F2** key,
- the **Back** button of a gamepad,
- the **Guide** button of a gamepad.

The menu has full navigation with a gamepad and with a keyboard. An open popup or dialog blocks the open and close input, so the panel never closes under a popup.

## 3. Frame

The menu is one full-screen panel with square corners. The panel has four parts:
- a top bar with the title,
- a navigation rail at the left,
- the page area,
- a bottom bar with the input hints.

The title is the Material Symbols Rounded `build` icon (U+F8CD), then the text "Engine menu".

The bottom bar shows the input hints of the last input device. The hints change when the device changes. A restart mark (Material Symbols Rounded `refresh`, U+E5D5) stands before a setting that applies after a restart.

The frame sizes:

| Part | Size |
|---|---|
| Top bar height | `kTopBarHeightPx` = 64 |
| Navigation rail width | `kRailWidthPx` = 112 |
| Bottom bar height | `kBottomBarHeightPx` = 52 |
| Panel padding | `kPanelPaddingPx` = 24 |
| Gap between the title icon and the title text | `kTitleIconGapPx` = 8 |
| Panel content slide on open and close | `kPanelSlidePx` = 16 |
| Page content slide on a page change | `kPageSlidePx` = 16 |

The top bar holds the close button at the right. The close button is an icon button of `kButtonHeightPx` = 36 square.

The bottom bar hint groups use these sizes:

| Part | Size |
|---|---|
| Hint glyph height | `kHintGlyphHeightPx` = 24 |
| Hint glyph padding | `kHintGlyphPaddingPx` = 8 |
| Hint glyph gap | `kHintGlyphGapPx` = 8 |
| Hint group gap | `kHintGroupGapPx` = 24 |

## 4. Rail pages

The navigation rail has these pages, in this order:
1. Game
2. Display
3. Graphics
4. Performance
5. Mouse
6. Keyboard
7. Controllers
8. On-screen controls
9. Debug
10. About

Each rail item has an icon and a label. A selected item has a pill indicator behind the icon.

The game software keyboard is its own centered dialog. It is not a rail page.

### Display and Performance pages

The Display and Performance pages show the rows of the settings model. The model is generated from `engine/src/nx/runtime/settings.txt`, and the old window and the command line read the same model. The Display page shows the rows of its table in their order. The Performance page shows the rows of its table, in the groups that this part describes below. A page reads at most 64 rows of its table.

Each row takes its widget from the model alone. The first rule that matches picks the widget:

1. A row with presets is a dropdown.
2. An int or a text row with words is a dropdown. A word list with `#` (a numeric entry) does not count as words, so `map_limit_mb` and `map_limit_files`, with the words `No limit|#`, get a number field.
3. An int from 0 to 1 without words is a switch.
4. An int whose range has at most 64 steps (`kSliderMaxSteps`) is a slider.
5. Any other int is a number field.
6. A text row without words shows only its supporting text, "Edit it in the .ini file".
7. A float row without presets is not drawn. No row of these two pages is a float row.

The model gives `heap_max_mb`, a text setting, as an int row, so it gets a number field (`NUMBER_TEXT` in `imgui_settings.c`).

A row whose setting applies at the next start shows the restart mark before its label. A row whose saved value differs from the running game shows "Saved. Applies after a restart." in place of its help text. A row that the command line holds is disabled, and a change of its value is not taken.

The Display page ends with the Reduce motion switch. The menu keeps the value of this switch in memory only, so the value is not saved.

The Performance page shows five main rows first, in this order: `fps_limit`, `memory_gb`, `pipeline_threads`, `async_pipelines` and `prepare_shaders`. A key that the model does not have is skipped. The section title "Advanced" follows the main rows. Under it, the page shows all other rows of the table, in the order of the table.

The `fps_limit` row is a dropdown of these presets: "Game's pace", 30, 60, 120, 144, 240 and "Unlimited". Its Custom entry takes any value from 10 to 1000. When the value is not one of the presets, the row shows Custom and a number field below the dropdown. The field also shows when the user picks Custom, even if the value is a preset. Picking a preset hides the field and sets the value.

The right part is 240 wide (`kRightWidthComboPx`) for a dropdown, 240 wide (`kRightWidthNumberPx`) for a number field and 280 wide (`kRightWidthSliderPx`) for a slider. The right part of a row with a Custom field is as high as two dropdowns and the item spacing between them.

A number field edits as follows:

- A click, Enter or A on a gamepad starts an edit. The field then holds the current value. Only digits go in, and Backspace removes the last digit.
- Enter, or a loss of focus, commits the edit. A whole number in the range of the row becomes the value. Any other text keeps the old value, and the outline and the text turn to the error color until the next edit starts.
- Esc or the B button of a gamepad cancels the edit. While an edit is open, these keys do not close the menu.
- A page change ends an open edit the same way as a loss of focus.
- A menu close cancels an open edit.
- With the nav focus on the field and no edit open, the arrow keys or the D-pad change the value by the step of the row, within the range, at once.

## 5. Theme

The theme uses one seed color, `0xF97316`. The theme has only the dark scheme. The palette comes from an HCT port of material-color-utilities (Apache 2.0, commit `0f07b687`).

The fonts:
- **Roboto** for Latin text,
- **Noto Sans JP** for Japanese text, merged into the same font,
- **Material Symbols Rounded** for the icons, merged into the same font.

The type scale:

| Role | Size |
|---|---|
| Title large | `kTitleLargePx` = 22 |
| Title medium | `kTitleMediumPx` = 16 |
| Body large | `kBodyLargePx` = 16 |
| Body medium | `kBodyMediumPx` = 14 |
| Label large | `kLabelLargePx` = 14 |

The UI scale is `1.25 x max(display scale, window pixel height / 1080)`. The constants are `kUiSizeFactor` = 1.25, `kReferenceHeightPx` = 1080, `kMinUiScale` = 1.25 and `kMaxUiScale` = 3.75. The scale is clamped to 1.25 to 3.75 and read each frame. A display scale that is not positive counts as 1.0. The menu is never smaller than its 1080p size.

The shape corners:

| Corner | Size |
|---|---|
| Small | `kCornerSmallPx` = 8 |
| Medium | `kCornerMediumPx` = 12 |
| Large | `kCornerLargePx` = 16 |

A pill uses half of its height.

## 6. Components

These sizes set the components. Each state layer uses `kStateHover` = 0.08, `kStateFocus` = 0.10 or `kStatePress` = 0.10, as its name says.

| Widget | Size | States |
|---|---|---|
| Switch | `kSwitchWidthPx` = 44 by `kSwitchHeightPx` = 26. Handle `kSwitchHandlePx` = 20 when on and `kSwitchHandleUnselectedPx` = 16 when off. Margin `kSwitchHandleMarginPx` = 3. Pill rounding | Off and on. The track and the handle grow between the two. Hover, focus and press layers |
| Button | Height `kButtonHeightPx` = 36. Minimum width `kButtonMinWidthPx` = 64. Horizontal padding `kButtonPaddingPx` = 20 each side. Pill rounding | Filled, tonal, outlined, text and error outlined. Hover, focus and press layers. A 1 px outline for the outlined kinds |
| Icon button | `kButtonHeightPx` = 36 square. Icon `kIconSizePx` = 20. Round | Hover, focus and press layers |
| Rail item | Height `kRailItemHeightPx` = 52. Indicator `kRailIndicatorWidthPx` = 48 by `kRailIndicatorHeightPx` = 28, at `kRailIndicatorTopPx` = 4. Label `kRailLabelPx` = 12 at `kRailLabelGapPx` = 2 below the indicator | Selected indicator (secondary container). Unselected hover, focus and press layers |
| List row | Minimum height `kRowHeightPx` = 48, or `kRowHeightTwoLinePx` = 64 with a supporting text. The row grows to fit its wrapped text. Horizontal padding `kRowPaddingPx` = 16. Icon gap `kRowIconGapPx` = 6. Two-line gap `kTwoLineGapPx` = 2. Text gap `kRowTextGapPx` = 16 | A hover layer of the on-surface color at 0.04. An optional restart mark of `kSmallIconSizePx` = 16 |
| Card | Padding `kCardPaddingPx` = 16 on all four sides. Medium corner. Height follows its content | No state. The background is `surface_container_low` |
| Dialog | Centered. Large corner. Window padding 24 horizontal by 20 vertical. Modal with a scrim | Open and close fade. A close input starts the fade |
| Exposed dropdown | Height `kDropdownHeightPx` = 44. Corner `kDropdownCornerPx` = 4. Padding `kDropdownPaddingPx` = 12. Icon `kDropdownIconPx` = 20. Indicator `kDropdownIndicatorPx` = 1, or `kDropdownIndicatorActivePx` = 2 when active. Menu corner `kMenuCornerPx` = 4, item height `kMenuItemHeightPx` = 40, item padding `kMenuItemPaddingPx` = 12, vertical padding `kMenuVerticalPaddingPx` = 8, edge margin `kMenuEdgeMarginPx` = 8 | Closed, open and closing. Hover and focus. The selected item has a secondary container background |
| Slider | Item height `kSliderItemHeightPx` = 32. Track `kSliderTrackPx` = 4. Handle radius `kSliderHandleRadiusPx` = 8. State radius `kSliderStateRadiusPx` = 16. Value label height `kSliderValueHeightPx` = 24, padding `kSliderValuePaddingPx` = 10, gap `kSliderValueGapPx` = 2, text gap `kSliderTextGapPx` = 8 | Hover, focus and press layers. A value label on drag and on a key change |
| Number field | Height `kDropdownHeightPx` = 44, the same as the exposed dropdown. Corner `kDropdownCornerPx` = 4. Padding `kDropdownPaddingPx` = 12. Outline `kTextFieldOutlinePx` = 1. Caret `kTextFieldCaretPx` = 1. Background `surface_container_highest`. Text at the body large size | Hover, focus and press layers. The outline is the outline color, or the error color after a rejected commit. A caret while an edit is open. The edit rules are in [Display and Performance pages](#display-and-performance-pages) |

The focus ring is `kFocusRingPx` = 2 thick and uses the secondary color. It shows only when the ImGui nav cursor is visible. A mouse click hides the nav cursor, so no focus ring follows the mouse.

Cards and rows follow one padding rule. A card has `kCardPaddingPx` (16) on all four sides. A row inside a card drops its own horizontal padding, so each item of the card starts at the card padding. A row outside a card has `kRowPaddingPx` (16) horizontal padding.

The text of a list row wraps before its right part. The text column is the row width less the horizontal padding on each side, the icon slot, the right part and the text gap. Every list row reserves the icon slot, which holds the restart mark and its gap, so the labels of the rows start at the same x. The row height is the largest of the minimum height, the height of the right part, and the wrapped text with `kRowPaddingPx` above and below it.

## 7. Motion

The curves are CSS cubic-bezier control pairs:

| Token | Control points |
|---|---|
| Standard | `kCurveStandard` = (0.2, 0.0, 0.0, 1.0) |
| Emphasized decelerate | `kCurveEmphasizedDecelerate` = (0.05, 0.7, 0.1, 1.0) |
| Emphasized accelerate | `kCurveEmphasizedAccelerate` = (0.3, 0.0, 0.8, 0.15) |

The durations are `kDurationShortMs` = 100, `kDurationShortLongMs` = 200, `kDurationMediumMs` = 300 and `kDurationMediumLongMs` = 400. The 400 ms token has no use in the mockup.

| Use | Curve | Duration (ms) |
|---|---|---|
| Panel open | Emphasized decelerate | 300 |
| Panel close | Emphasized accelerate | 200 |
| Page change (fade and offset) | Emphasized decelerate | 300 |
| Rail indicator | Standard | 200 |
| Switch | Standard | 200 |
| State layers (hover, focus, press) | Standard | 100 |
| Dropdown open | Emphasized decelerate | 300 |
| Dropdown close | Standard | 100 |
| Slider value label | Standard | 100 |
| Dialog and scrim open | Emphasized decelerate | 300 |
| Dialog and scrim close | Emphasized accelerate | 200 |
| Bottom bar hint crossfade | Standard | 100 |
| Page scroll | Standard | 200 |

The "Reduce motion" setting on the Display page makes every transition instant. A tween returns its target at once when reduce motion is on.

## 8. Fades

The panel fades as one picture. The menu draws the panel opaque into an offscreen target, then composites the target with the panel alpha. The mockup uses an SDL target texture. The engine needs the same offscreen layer in its Vulkan overlay path.

All draw-list colors follow `style.Alpha`. A page fade and a component fade use `ImGui::PushStyleVar(ImGuiStyleVar_Alpha, value)`, so every color of the page or the component follows the value. The text, the icons and the focus ring use `ImGui::GetColorU32(color, alpha_mul)` for the same result.

## 9. Dropdown placement

The dropdown menu has the left edge and the width of the field. It opens below the field. When the space below is too small, it opens above the field. When neither side has the full height, it opens on the larger side with a limited height, and it scrolls to show the selected item. The menu never covers the field, and it grows away from the field. It keeps `kMenuEdgeMarginPx` (8) from the edge of the visible page area.

## 10. Scrolling

The mouse wheel and the navigation scroll are eased. The wheel step adds to the target and not to the eased value, so quick notches keep the full distance. A scrollbar drag is direct and cancels the eased motion. Reduce motion makes the scroll direct. The scroll tween uses the standard curve and 200 ms.

The nav scroll keeps the focused row of a page visible. The whole row box counts, not only the right part. A row above the view scrolls to its top, and a row below the view scrolls to its bottom. The first nav row of a page scrolls to 0, and the last nav row scrolls to the maximum. A nav row is a row with a right part that is not disabled. The maximum uses the content of this frame, including the growth of the page card in this frame, and not the value that ImGui wrote in the last frame.

The rail scrolls in the same way. The rail has no visible scrollbar. It scrolls only when its items are taller than its body, and it keeps the focused item fully visible. The first item scrolls the rail to 0, and the last item scrolls it to the maximum.

## 11. ImGui 1.93.0 notes

The mockup depends on these ImGui rules. A note gives the flag or the function name.

- `ImGui::PushFont(nullptr, size)` pushes an unscaled pixel size. The style fields `FontSizeBase`, `FontScaleMain` and `FontScaleDpi` scale it. `ImGui::StyleColorsDark` is the base, and `ScaleAllSizes` scales the sizes.
- A custom widget is one ImGui item. `ImGui::ItemSize`, `ImGui::ItemAdd` and `ImGui::ButtonBehavior` give it an id, so keyboard and gamepad navigation reach it.
- The focus ring reads `ImGuiContext::NavCursorVisible` with `ImGui::IsItemFocused`. A mouse click clears the nav cursor.
- `ImGuiChildFlags_NavFlattened` on the top bar, the bottom bar, the rail, the page and each card lets the navigation cross the child windows.
- `ImGuiChildFlags_AlwaysUseWindowPadding` on a borderless child keeps the pushed `WindowPadding`. Without it the child ignores the padding.
- `ImGuiChildFlags_AutoResizeY` lets a card take the height of its content.
- `ImGui::SetNextWindowPos(pos, ImGuiCond_Always)` on a popup skips `FindBestWindowPosForPopup` and `ClampWindowPos`. `ImGui::SetNextWindowSize(size, ImGuiCond_Always)` replaces the forced `AlwaysAutoResize`. `ImGui::BeginPopup` then uses the position and the size as given.
- The dropdown popup has a transparent `ImGuiCol_PopupBg` and draws its own background. `ImGuiStyleVar_PopupRounding` and `PopupBorderSize` set its shape.
- A closing fade uses `ImGuiWindowFlags_NoInputs`.
- The eased page scroll reads `window->ScrollTarget` and `window->ScrollTargetCenterRatio` after `ImGui::NewFrame` wrote the wheel and the nav scroll. `ImGui::GetIO().MouseWheel` gives the wheel step. `ImGui::SetScrollY(window, value)` writes the eased value. A scrollbar drag sets `window->Scroll` directly and shows as `context->ActiveId == ImGui::GetWindowScrollbarID(window, ImGuiAxis_Y)`.
- The rail sends the D-pad right to the first page control with `ImGui::SetNavWindow`, `ImGui::SetNavID`, `ImGui::NavMoveRequestCancel` and `ImGui::SetNavCursorVisible(true)`.
- The slider reads `ImGuiKey_GamepadDpadLeft` and `ImGuiKey_GamepadDpadRight` (or the arrow keys) with `IsKeyPressed(key, true)`, then calls `ImGui::NavMoveRequestCancel` so the key does not also move the nav.
- A dialog uses `ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f))` and `ImGui::BeginPopupModal` with `AlwaysAutoResize`, `NoTitleBar`, `NoSavedSettings` and `NoMove`. The scrim color `ImGuiCol_ModalWindowDimBg` is read before `Begin` and pushed with a scaled alpha. `ImGuiStyleVar_Alpha` fades the content. At zero alpha `ImGui::CloseCurrentPopup` closes the modal.
- `ImGuiConfigFlags_NavEnableKeyboard` and `ImGuiConfigFlags_NavEnableGamepad` turn on the navigation. `ImGuiWindowFlags_NoBringToFrontOnFocus` keeps the panel in order.
- `ImGui::IsWindowAppearing` marks the first frame of a limited dropdown, so the menu can scroll to the selected item before it is drawn.

## 12. Overlaps with the ScarletNX SDL overlay

These parts of the old overlay share data or input with the engine menu. Check each one before a delete. The names are file and function names.

- The keyboard box: `overlay_keyboard_*` in `overlay.c`.
- The open rule of the old menu: `overlay_menu_opens` in `overlay.c` and `overlay.h`, with the `legacy_menu` setting in `settings.txt`.
- `gamepad_map`: the old GUI and the ImGui settings both edit it (`gamepad.c`, `imgui_settings.c`, `sdl3.c`, `settings.txt`).
- `stats` and `showcase`: the F12 and F6 hotkeys in `sdl3.c` and the legacy menu set them.
- The input gate `owned`: `imgui_sdl_event` in `imgui_sdl.c` and `imgui_sdl.h`.
- The `imgui` setting in `settings.txt`: no menu is left with `imgui=0`. This is an open decision.

## 13. Migration

The base and the move of the functions are units 4 to 6 of phase 7.5 in `ROADMAP.md`.

1. Unit 4, the base in the engine: the theme, the fonts, the widgets, the shell and the host tests.
2. Unit 5, the move of the functions: one screen or one group per pull request. The old screen is deleted in the same pull request. The game never switches between the old and the new UI.
3. Unit 6, the removal of the rest of the old UI, and the finish of the docs.

The first move of unit 5 moves the Display and Performance pages to the engine menu. These two pages leave the old Display window in the same pull request, so the old window keeps only its Graphics tab.

The third-party parts go into `docs/CREDITS.md` in unit 4. These parts are the HCT port and the fonts.
