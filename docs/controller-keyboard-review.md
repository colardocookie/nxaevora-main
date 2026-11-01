<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the NXAevora contributors -->

# Review of patch 0016

Paths below are relative to `third_party/ScarletNX`. This is a source review of the patch and the applied files. No source was changed. No build, test suite, or game was run. Small byte-count and held-bit examples were checked in memory.

## 1. [P1] Mouse changes race with the main thread

**File and line:** `src/nx/gpu/vk/imgui_settings.c:83`, `src/nx/runtime/options.c:290`, `src/nx/runtime/platform/backend/sdl3.c:1340-1342,1360-1361,1382,1397-1414`.

**Fault:** The renderer applies mouse settings through `options_change`. The stores in `ini_read` are atomic, but the main thread reads these fields without atomic loads or `ui_lock`. A lock held only by the writer does not prevent this data race. The native address is also read once for the nonzero check and again for the pointer.

**Input or state:** Change a mouse slider while `video_poll` handles motion. If the native address changes to zero between its check and its use, motion can access `g_mem` at address zero. A concurrent file reload can also apply an older value after the window applies a new one, while the new value remains queued for saving.

**Fix:** Queue mouse changes for the main thread, as for key bindings. Apply and save them in order with reloads. Read one address snapshot for both the check and the memory access. If cross-thread writes remain, use atomic reads everywhere and define how concurrent changes are ordered.

## 2. [P1] The live address field accepts an unsafe memory target

**File and line:** `src/nx/gpu/vk/imgui_ui.cpp:254-267`, `src/nx/runtime/options.c:286-290`; consumer at `src/nx/runtime/platform/backend/sdl3.c:1340-1351`.

**Fault:** The new address field accepts every integer from zero through `INT_MAX`. The numeric check does not enforce the documented eight-byte alignment or check that the target is mapped. The motion path then uses the address for a 64-bit atomic access.

**Input or state:** Enter `1` for Mouse native address, press Enter, close the window, and move the captured mouse. The address passes validation but points into the unmapped low guest range. The field's `+` button can also turn a valid aligned address into a misaligned address.

**Fix:** Validate nonzero addresses before use. Require eight-byte alignment and a mapped target that can be used by the native mouse protocol. Reject an unsafe value with its address and cause. Use an aligned step for the field. Do not run this failure case in the game before the guard exists.

## 3. [P2] A failed request corrupts an earlier pending request

**File and line:** `src/nx/runtime/platform/backend/sdl3.c:681-689`.

**Fault:** `keybind_edit` writes directly into `g_keybind_next` before it knows that the result fits. On failure, `g_keybind_pending` can still be set from an earlier request. The main thread then applies the partial result even though the log says that the change was not made.

**Input or state:** Use 62 copies of `A:F`, separated by spaces. A request for `B:Q` makes a valid pending text of 251 bytes. Before the main thread takes it, request `R3:Left_Shift`. This needs 265 bytes. The 256-byte buffer is overwritten with text that ends in ` R3:`, and the earlier pending flag stays set.

**Fix:** Edit into a local temporary. Check the full length and grammar. Copy it into `g_keybind_next` and set the pending flag only after success. On failure, keep the earlier pending text intact.

## 4. [P2] A remap can leave a game button held

**File and line:** `src/nx/runtime/platform/backend/sdl3.c:597-602,692-700,1694-1697`.

**Fault:** Replacing the map does not reconcile `keys_held`. Key-up uses the new map, so it can no longer clear the button that the old map set.

**Input or state:** Hold default key F so that game button A is down. Open Settings and choose None for A. Let the main thread apply the change, then release F. Its new map is zero. The A bit stays set until another reset occurs.

**Fix:** Track game-held state per scancode, including the mapping used for its press. Reconcile it when the map changes. If the policy clears all held keys, require their physical release before they can press again.

## 5. [P2] The first arrow press can reach both the slider and the game

**File and line:** `src/nx/gpu/vk/imgui_ui.cpp:214-217`; gateway at `src/nx/runtime/platform/backend/imgui_sdl.c:89-94`.

**Fault:** `SetNextFrameWantCaptureKeyboard` changes the next ImGui frame. The gateway uses the previously published capture flag. An arrow can reach the game before the hover request takes effect. Nothing clears that game-held bit when capture starts.

**Input or state:** With no active widget and keyboard capture false, move over a mouse slider and press Right before the next capture flag is published. The game receives D-pad Right. ImGui also receives the press and changes the slider. Holding the key can keep D-pad Right down during the adjustment.

**Fix:** Decide input ownership before a press is sent to the game. Share the required hover/interaction state with the main-thread gateway, or give keyboard ownership to the settings interaction earlier. Keep press and release ownership per scancode. A frame delay does not establish ownership.

## 6. [P2] A captured key-up can release a different held key's button

**File and line:** `src/nx/runtime/platform/backend/sdl3.c:719-733,1641,1696-1697`.

**Fault:** Capture swallows only key-down. Its key-up still clears game button bits, even when the game never received that press. `keys_held` records buttons rather than the individual keys that hold them.

**Input or state:** Set `keyboard_map=A:F A:H`. Hold F, start Bind for another button, then press and release H before the renderer takes the capture result. H-down is swallowed, but H-up clears A although F is still held. The same release problem can affect an arrow taken by the window when another key maps to that game button.

**Fix:** Record which consumer received each scancode's press. Process its release for that consumer. Preserve releases for keys that were already down before capture. Derive each game button from all game-owned keys that still hold it.

## 7. [P2] Capture remains active after focus is lost

**File and line:** `src/nx/runtime/platform/backend/sdl3.c:719-720,1699-1708`; UI at `src/nx/gpu/vk/imgui_ui.cpp:366-370,412`.

**Fault:** Focus loss clears game input but does not cancel capture. The Keyboard page remains drawn, so the UI's hidden-page check does not end it.

**Input or state:** Click Bind, switch to another application, return to the game, then press an ordinary game key. It is taken as a binding without another Bind action. If no key follows, capture has no end condition from focus loss.

**Fix:** Publish a cancelled capture result on focus loss. Let the UI consume that result and clear its waiting row. Also test window close, tab change, collapse, disable, and swapchain recreation.

## 8. [P2] Reserved runtime keys are swallowed during capture

**File and line:** `src/nx/runtime/platform/backend/sdl3.c:719-733`.

**Fault:** A reserved key does not become a binding, but the function still returns 1. This stops it before ImGui, the old menu, and the runtime hotkey code. This does not match the backend contract that the runtime's own keys are not taken.

**Input or state:** Start Bind and press F1, F11, or Pause. The capture keeps waiting, and the requested runtime action is not reached. Alt is physically tracked first, but its down event is still withheld from ImGui.

**Fix:** Handle Escape as cancellation. Return the normal event path for the other reserved keys, subject to the usual UI ownership policy. Test reserved-key repeats as well.

## 9. [P2] Capture can bind a different scancode from the one pressed

**File and line:** `src/nx/runtime/platform/backend/sdl3.c:724-731`; parser at `src/nx/runtime/platform/backend/sdl3.c:561-562`.

**Fault:** Capture assumes that a scancode name uniquely identifies its scancode. The bundled SDL table has duplicate names. It also returns an empty string for unnamed scancodes. The result is published without a round-trip check.

**Input or state:** Capture `SDL_SCANCODE_RETURN2` (158). SDL names it `Return`, but `SDL_GetScancodeFromName("Return")` returns the first Return scancode (40). The saved binding responds to a different key. An unnamed scancode instead produces an incomplete `Button:` word.

**Fix:** Before publishing a capture, require a nonempty name that parses back to the same scancode and fits the format. Keep waiting with a clear error for an unsupported key, or extend the binding format to store its exact scancode.

## 10. [P2] A rejected map is reported as applied and is saved

**File and line:** `src/nx/runtime/options.c:281-292`; callback at `src/nx/runtime/platform/backend/sdl3.c:597-600`.

**Fault:** `ini_check` checks only the length of a text setting. `ini_read` copies the text before its void callback runs. If `keybind_compile` rejects it, `options_change` still saves it and returns `OPTIONS_OK`. The text and the active map then disagree.

**Input or state:** Pass `keyboard_map=A:NoSuchKey` to `options_change`, or reach it through the partial pending text in finding 3. The previous compiled map remains active, but the invalid text is stored and queued for the file.

**Fix:** Validate and compile the complete map before changing any state. Return the parser failure to `options_change`. Commit the text, compiled map, and save request only after success. Include the failed word and cause in the result.

## 11. [P2] The view substitutes defaults for an active custom map

**File and line:** `src/nx/runtime/platform/backend/sdl3.c:631-638`.

**Fault:** On a parse error, the view compiles the defaults. The setter instead keeps the previous map. Thus the displayed keys need not be the keys that control the game. The cause of the parse error does not justify this default-map fallback.

**Input or state:** Apply `A:H`, then reload the file with `A:NoSuchKey`. The active map still has A on H. Opening Keyboard shows A on F. The view marks A as custom from the invalid text while showing default keys.

**Fix:** Publish a main-thread snapshot of the last accepted compiled map and its accepted text. Show that snapshot on an error, and show the pending/rejected state separately. Do not read `g_keybind` directly from the renderer.

## 12. [P2] Focused tests and boundary scenes are missing

**File and line:** `src/nx/runtime/platform/backend/sdl3.c:658-679`, `src/nx/CMakeLists.txt:107`, `tests/CMakeLists.txt:756-759`; project `docs/imgui-plan.md`, Verification and Progress.

**Fault:** Patch 0016 adds no tests. The existing `imgui_input` test checks the event queue, not map editing, capture ownership, or renderer-side setting changes. The recorded general window run does not establish these boundary results.

**Input or state:** The existing tests can pass while findings 1-11 remain.

**Fix:** Move the pure `keybind_edit` helper to a shared internal header and test it from `tests/`. Cover mixed-case button names, repeated words, None, Default, reset-all, 255-byte success, and overflow with the old pending text intact. Add tests for parser failure and press/release ownership.

After fixes, the user must run these small scenes:

- In a field, hold a game key, open Settings, remap or clear it, then release it. Repeat with two keys on one button. Check that no button remains held or releases early.
- Move onto a slider and press Left/Right at once. Repeat while holding the arrow, with Shift/Ctrl, and while moving off the slider. The game must receive no slider arrow press.
- Start Bind, cancel with Escape, then type a normal key. Test reserved keys, a tab change, close/collapse, focus loss, disable/re-enable, and resize/fullscreen. No stale capture or held key must remain.
- Change mouse values in the field, in battle, and in a game menu. Check persistence after exit and restart. Check startup-only and command-line-held settings with host tests where these pages have no such row.
- Start with `--keyboard_map=A:H` and `--mouse_native_pitch=700`. Check the locked controls and the saved file. Test a nearly full map and an invalid map through host tests, not an unsafe game run.

Record each scene and inspect its logs and saved settings. A build does not replace these checks.

## 13. [P3] The key-binding controls ignore the command-line lock

**File and line:** `src/nx/gpu/vk/imgui_settings.c:22-23,64-65`, `src/nx/gpu/vk/imgui_ui.cpp:335-381`.

**Fault:** The generic row carries `IMGUI_SETTING_LOCKED`, but `keyboard_map` is removed from those rows. Bind, None, Default, and Reset all have no equivalent check. They accept input and show a pending preview before the main thread rejects it with a generic log message.

**Input or state:** Start with `--keyboard_map=A:H`, then choose None or Bind for A. The control appears usable, but the change is rejected and the old binding returns.

**Fix:** Pass the map's lock state to the binding view. Disable all map-edit actions and capture when it is held by the command line. Show the same explanation as the generic locked row.

## 14. [P3] New locals are declared before their first value

**File and line:** `src/nx/gpu/vk/imgui_settings.c:15,27-30,36-39`; `src/nx/runtime/platform/backend/sdl3.c:632-638,660-668,683-686,694-696`; `src/nx/gpu/vk/imgui_ui.cpp:327-329`.

**Fault:** The new code declares loop variables and result variables before their first assignment. This violates ScarletNX's explicit rule to declare locals where they first get a value.

**Input or state:** Every call through these helpers uses the submitted declarations.

**Fix:** Declare loop indices in the loop. Initialize `failed`, `taken`, `named`, and `result` at their first assignment. Declare the row pointer in the inner loop where it first gets its table value.

## 15. [P3] The fast input step has an unnamed setting

**File and line:** `src/nx/gpu/vk/imgui_ui.cpp:265`.

**Fault:** `r.step * 10` sets the fast input-button step with a literal. The Code rule requires a named constant for a meaningful setting. The nearby slider code already names its ten-step amount.

**Input or state:** Use Ctrl with a step button in Mouse native address. This literal selects the fast step.

**Fix:** Use a named fast-step factor, shared with the other ten-step behavior if that is the intended policy.

Checks with no separate finding: reverse patch validation passed. Both Git indexes are empty. The patch contains only runtime/build source paths and no game files. Both new files have SPDX and copyright lines. The inspected Begin/End, table, tab, combo, and ID paths are balanced. `options_change` is called after the pending-map code releases `ui_lock`. The map text and pending flag use that lock. The current Mouse/Keyboard ranges do not overflow the new slider or choice arithmetic. The wide address range uses InputInt, whose bundled step operations saturate at the integer limits. The default keys match the new HELP binding list. The other old scancode maps found in AGB, 3DS, and Vita belong to different runtimes. No new delay or retry without a cause was found.

## Check of the findings (Claude, against the code)

The reviewer is the code reviewer agent (GPT-6.1-Sol). Each finding was read against the applied files.

| # | Result | Note |
| --- | --- | --- |
| 1 | Confirmed | `mouse_native_motion` reads `mouse_native_address` twice (`sdl3.c`, the check and the pointer). Before the window, only the main thread wrote it (a reload). Now the renderer thread writes it too. |
| 2 | Confirmed | The setting takes any int from 0 to INT_MAX. This was true for the `.ini` too, but the window makes a wrong value easy. |
| 3 | Confirmed | `keybind_edit` writes into `g_keybind_next` while `g_keybind_pending` can already be 1. |
| 4 | Confirmed | `keys_held` is a button mask. A key-up uses the new map. The same hazard existed for a reload of the `.ini`. |
| 5 | Known | Said to the user: the first frame after the pointer arrives still gives the arrow key to the game. |
| 6 | Confirmed | Needs two keys on one button (`A:F A:H`). |
| 7 | Confirmed | Focus loss does not end the capture. |
| 8 | Design | The capture swallows the reserved keys on purpose, so that F2 or Pause do not act while a key is awaited. The wording in `backend.h` should say so. |
| 9 | Confirmed | `SDL_keymap.c` names scancodes 40 and 158 both "Return". Only Return2 and unnamed scancodes are affected. |
| 10 | Confirmed by reading | Reachable only through 3 or 9. |
| 11 | Confirmed | The view shows the defaults when the text cannot be read, the map in use is the older one. |
| 12 | Known | No test for the new code. `keybind_edit` is static. |
| 13 | Confirmed | The key binding buttons ignore a `--keyboard_map` on the command line. |
| 14 | Confirmed | Locals declared before their first value, against the rule of `third_party/ScarletNX/CLAUDE.md`. |
| 15 | Confirmed | `r.step * 10` in `setting_number` has no name (`ARROW_COARSE` exists). |

Proposed fixes, by group:

1. Settings changes from the window go to a queue that the main thread applies in `video_poll` (as the key bindings do), and the
   row `mouse_native_address` is not shown in the window (findings 1 and 2).
2. `keybind_edit` writes into a local buffer and the pending text is replaced only on success. A capture needs a name that
   parses back to the same scancode. `keyboard_map_set` returns the failure to `options_change` (3, 9, 10).
3. A capture ends on focus loss. The main thread keeps one snapshot of the map in use for the view. The key binding buttons
   follow the command line lock (7, 11, 13).
4. The map change releases the buttons that `keys_held` holds for keys no longer mapped (4, 6).
5. The rules: locals where they get a value, a named factor for `r.step * 10` (14, 15). Tests for `keybind_edit` need it in a
   header (12).

## Fixes (patch 0016, same day)

All findings are fixed in `0016-settings-controller-keyboard.patch` but 5, 8 and 12.

| # | Fix |
| --- | --- |
| 1 | The window queues each change (`backend_setting_request`, 16 entries, a later value of a key replaces the earlier). The main thread applies them in `video_poll` (`window_changes_apply`). |
| 2 | The row `mouse_native_address` is not shown in the window (`HIDDEN` in `imgui_settings.c`). |
| 3 | `backend_keybind_request` edits into a local buffer under `ui_lock` and replaces the pending text only on success. |
| 4, 6 | `g_key_bits[scancode]` holds the buttons that a press set. A release clears those and keeps the buttons of the other keys that are down. A menu or a focus loss clears all of it. |
| 7 | A loss of the focus cancels the capture (`keycap_cancel`). The window takes the result and ends the wait. |
| 9 | A key is taken only when its name gives the same scancode back and has no `_`. Else the capture ends with a note in the window. |
| 10 | `keybind_apply` compiles the text before `options_change`, and drops one that cannot be read. |
| 11 | The view compiles the text of the map in use (`g_keybind_accepted`, set by `keyboard_map_set`) or the text that waits. |
| 13 | The bindings carry `locked` (`options_overridden`). The window disables them, and the request does nothing. |
| 14, 15 | Locals are declared where they get a value. `ARROW_COARSE` replaces the literal. |

Not changed: 5 (the first frame after the pointer arrives, a limit that the user knows), 8 (a design choice: the reserved keys
do nothing while a key is awaited, and the comment in `backend.h` says so), 12 (no test yet: `keybind_edit` and
`keybind_compile` are static in `sdl3.c`, and a test needs them in a header).
