<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the NXAevora contributors -->

# Phase 7.5: NXAevora Engine, the CLI and the new UI

Status: plan only. No code is changed, no file is moved and no dependency is added yet. The roadmap entry is
`ROADMAP.md`, "Phase 7.5". This file holds the details. It replaces the plan of the Dear ImGui windows
(`imgui-plan.md`), which is now the old plan and stays as history.

The facts below come from a reading of the source at `1ec1b69`. Work unit 2 checks each row again before a removal.

## Decisions

These decisions are fixed. A change to them needs the user.

| Item | Decision |
|---|---|
| Name | The base derived from ScarletNX is the NXAevora Engine |
| Location | `third_party/ScarletNX/` moves to `engine/` |
| Entry point | The Engine starts from its CLI. The CLI recompiles the game and runs it. The game window stays |
| Window and input | SDL stays the base of the game window and the input |
| Dear ImGui | Replaced completely. It does not stay, also not for debug |
| Old UI of ScarletNX | The start GUI (`src/gui/`), the old in-game menu and the old UI drawing base are removed, after their needed functions move |
| In-game UI | One new UI base. Its look follows Material Design 3 (MD3) |
| External launcher | A separate component that calls the Engine CLI. Phase 11 finishes it |
| Internal names | A bulk rename of internal identifiers (`snx_`, `SNX_`, `scarletnx` in code) is not required |
| Origin | The copyright lines, the license and the upstream commit stay recorded. The new name never removes them |

The old UI and the new UI can both exist while the work goes on. At the end of phase 7.5 the old UI is gone.

## Work units

Each unit is one reviewable pull request, or a short series of them. Each unit builds, passes `ctest` and the check
`Windows build and test`, and has a game run by the user where the unit changes the runtime.

1. **Move to the Engine** (layout). Section "Unit 1".
2. **Inventory of the UI and its functions**. Section "Unit 2". It is a document change, and it gates units 4 and 5.
3. **CLI** (arguments, settings file, exit codes, progress). Section "Unit 3".
4. **New UI base and the move of the functions**. Section "Unit 4". This unit has several pull requests: the
   selection prototype, the base, then one pull request for each group of functions.
5. **Removal of the old UI and Dear ImGui**. Section "Unit 5".

Unit 2 can start before unit 1 ends. Unit 3 starts after unit 1. Unit 4 needs the decision of the UI base and the
settings definition of unit 3. Unit 5 comes last.

## Unit 1: Move to the Engine

### Steps

1. Move `third_party/ScarletNX/` to `engine/` with `git mv` in one commit that changes no content, so that git
   follows each file. The next commits of the same pull request update the references.
2. Update the references to the old path:
   - `.github/workflows/ci.yml` (the working directory, the cache keys, the paths of the test logs). The cache keys
     change, so the first CI run builds the dependencies again.
   - `.gitattributes` (the `linguist-vendored` line: decide if the Engine still counts as vendored).
   - `.github/pull_request_template.md`, `CONTRIBUTING.md`, `README.md`, `ROADMAP.md`, `recomp/patches/README.md`.
   - `docs/` (build notes, `runtime-changes.md`, `CREDITS.md`, and the notes that give source paths). The archived
     reviews in `docs/archive/` keep their old paths and get one line that names the move.
   - `tools/` (the scripts that name the source or the build folder).
   - The build folder `build/default` moves with the source. The build notes give the new path.
3. Change the shown name: the window title, the message boxes (`SDL_ShowSimpleMessageBox` and `MessageBoxW` in
   `backend/sdl3.c` show "ScarletNX"), the `--help` text and the log header.
4. Decide the program name (`nxaevora.exe` is the proposal) and update its callers: the CMake target or an output
   name, the CI step that runs `--help`, the skills `nx-recompile` and `nx-log-read`, and the copies in
   `game/scarletnx/bin/` and `game/recompiled/<id>/`.
5. Record the origin:
   - `engine/LICENSE` stays as it is. The source files keep the line "Copyright 2026 the ScarletNX contributors".
   - A short file in `engine/` names the upstream project, its URL and the base commit `8efbf9c4`, and says that
     NXAevora changes are listed in `docs/build/runtime-changes.md`.
   - `docs/CREDITS.md` names the Engine as derived from ScarletNX.
   - The upstream `README.md`, `ROADMAP.md` and `docs/` inside the copy stay, or move to a subfolder that says they
     are the upstream documents. They are not deleted for the rename.
6. Clean the local Claude files (`CLAUDE.md`, `.claude/WORK_PLAN.md`, `.claude/OLD-WORK_PLAN.md`, the skills and
   `.claude/settings.json`). They are local only and are not in the pull request. Replace the old path and the
   program name. Keep each valid note about the implementation (locks, threads, CRLF files, build traps). Remove
   the notes about the vendored copy that the move makes wrong.

### Limits

- Internal identifiers keep their names. A rename of an identifier is done only where it shows to the user.
- The other drivers of ScarletNX (3DS, Vita, GBA, Linux programs in `src/3ds`, `src/vita`, `src/agb`,
  `src/gnuabi`) are not part of this unit. Their removal is an open question (see the end of this file).

### Exit check

The Engine builds in `engine/`, `ctest` passes, CI passes, and the recompile and the start of the update program
work with the new path and the new program name. The user runs the title screen once.

## Unit 2: Inventory of the UI and its functions

Each function of the old UI goes to one target, or to more than one:

- **CLI**: a command or an option of the Engine, with a text or a machine-readable output.
- **Launcher**: the external launcher (phase 11), through the CLI and the settings definition.
- **New UI**: the new in-game UI.
- **Delete**: not needed for NXAevora.

The rule: a removal of the old UI never removes the work under it. The processing, the save of the settings and
the diagnostic data stay. Code that the UI and the game share is separated first and kept.

### First inventory

This table is the first result of a source reading. Unit 2 checks each row, adds the missing ones and records the
decision of the user where a row says "decide".

**Start GUI of ScarletNX** (`src/gui/`, the menu that `entry.c` starts with no argument through `snx_gui_main`)

| Function | Code | Target |
|---|---|---|
| Game library, browser, recent games, the systems Switch, Linux, 3DS, Vita and GBA | `browser.c`, `recent.c`, `model.c` | Delete. NXAevora runs one game. The launcher chooses the game folders (phase 11) |
| Keys screen, the read of `prod.keys` and `title.keys` | `GUI_KEYS`, driver `--keys` | Delete. Phase 7 removes the keys |
| Recompile queue and its progress | `queue.c`, `aot.c`, `SCREEN_COMPILING` | CLI (progress lines) and launcher (display) |
| Settings of the game and of the launcher | `settings.c`, `editor.c` | Launcher, through the shared settings definition (unit 3) |
| Folders (data, recompiled, work) | `GUI_FOLDERS`, `place.c` | CLI options and the settings file. Launcher for the choice |
| Logs in one file for a bug report | `logzip.c`, `log.c` | CLI command. Launcher button |
| Clean the shader cache and the code cache | `clean.c`, `GUI_ASK_CACHE` | CLI command. Launcher button |
| Setup: language, theme, UI scale, folders, keys, first user | `steps.c`, driver `--setup` | CLI without dialogs (defaults and options). The keys part is deleted. The first user is made by the Engine without a dialog |
| Move old files of older versions | `--migrate`, `snx_gui_migrate` | Delete (no older NXAevora install exists). Confirm in unit 2 |
| Themes (default, YoRHa), icons, fonts of the menu | `theme.c`, `themes/`, `icons.h` | Delete |
| Questions and error boxes of the menu | `GUI_ASK_*` | CLI messages and exit codes. Launcher dialogs |

**Old in-game menu** (`platform/overlay.c`, drawn with the quad list of `platform/ui.c`)

| Function | Code | Target |
|---|---|---|
| On-screen controls and the touch screen | `PAGE_CONTROLS`, `PAGE_BUTTONS`, `vpad.c`, `touch.c` | Decide. They are for touch devices. If kept, settings only, not a page |
| Gamepads: player, swap, rumble | `PAGE_PADS` | New UI (input settings) |
| Gamepad button map | `PAGE_MAP`, `gamepad.c` | New UI. The map storage stays in the runtime |
| Save states: slots, save, load, delete | `PAGE_STATES`, `PAGE_SLOT`, `PAGE_ASK` | Decide (new UI or delete). The game's own save data is not affected |
| Stats corner | `PAGE_MAIN` item Stats, `stats.c` | New UI (performance view) and the log |
| Software keyboard box (player name) | `overlay_keyboard_*` | New UI. The state, the ticket and the AM service path stay in the runtime |

**Dear ImGui windows** (`gpu/vk/imgui*.c*`, `platform/backend/imgui_sdl.c`)

| Function | Code | Target |
|---|---|---|
| Menu bar (About, Settings, Mods, Cheats, Debug) | `imgui_ui.cpp` `menu_bar` | New UI (one entry point) |
| Input settings: mouse, keyboard, controllers | `imgui_settings.c` pages | New UI. The page and preset tables become UI-neutral data of the settings definition |
| Display settings: display, graphics, performance | `imgui_settings.c` pages | New UI, same rule |
| Debug window: FPS, frame graph, frame timings, NVML GPU values, CPU and memory | `imgui_ui.cpp`, `debug_stats.c` | New UI (performance view), the log (a periodic stats line), CLI (an option for it). The counters and the hardware poll stay |
| Test window | `imgui_ui.cpp` `test_window`, setting `imgui_test` | Delete |
| Name input with the Japanese IME | `imgui_ui.cpp` `keyboard_window`, `imgui_text.c` | New UI. The rules of the game's request (length, number-only, Enter, Escape) stay |
| Mods and Cheats menus | not available yet | Mods: launcher (the file layer of phase 7). Cheats: decide (launcher, or a toggle in the new UI). `cheats.c` and `mods.c` stay |
| Input gate (who gets mouse, keys, text) | `imgui_input.c`, `imgui_events.cpp` | New UI base. The rules stay: the game gets no event that the UI took, a key-up is never taken, the mouse look stops over the UI |

**Start and failure screens**

| Function | Code | Target |
|---|---|---|
| Crash panel | `fatal_screen.c` | stderr, the log and the exit code always. New UI display when the window works |
| Shader preparation screen before the game | `prepare_screen.c` | Progress lines (CLI) and a new UI display |
| Debug warning line | `hud.c` | Log. New UI where the user must see it |
| Error and help message boxes | `backend/sdl3.c` | stderr, the log and the exit code. No dialog in a normal run |
| Old UI drawing base | `platform/ui.c`, `gpu/vk/ui.c`, `ui_draw_sdl` | Delete after the last user moves |

Kept without a change of target (not UI): `stats.c`, `debug_stats.c`, `keybind.c`, `gamepad.c`, `cheats.c`,
`mods.c`, `settings.txt` and `ini.c`, `unattended.c` (CI tests), `tracy.c`.

### Exit check

Every row has a target and a short reason. Each row with "decide" has the decision of the user. The shared code of
each row is named, so that unit 5 can remove the drawing and keep the work.

## Unit 3: CLI

### What the CLI defines

- **Commands.** At least: recompile the game, run the game, list and check the settings, export the logs, clean
  the caches. The current options (`-r`, `--game=DIR`, `--settings`, `--KEY=VALUE`, `--data`, `--outdir`) are the
  base. A command for each job is better than a mode that the argument count selects.
- **Inputs.** The decrypted ExeFS and RomFS of phase 7, the data folder, the output folder and the settings file.
  Each one has an option and a default. The working directory has no effect on the result, or the CLI says which
  path it uses.
- **Settings file.** One file format and one place (now `<id>.ini` beside the program and `%APPDATA%\scarletnx`).
  The CLI can read, write and check a setting without a start of the game.
- **Exit codes.** A table: success, wrong usage, missing or wrong game files, recompile failure, start failure
  before the window, crash of the game, stop by the user. The launcher acts on the code only.
- **Progress and errors for the launcher.** A machine-readable stream (the proposal is JSON Lines on stdout with an
  option, and the human text on stderr). Each line has a stage, a value and, on an error, the stage that failed and
  the cause. `entry.c` already sets line buffering for a launcher on a pipe. The format has a version number.
- **No dialog.** A normal recompile or run needs no GUI and no question. The setup asks nothing when the options and
  the settings file give the values. A missing value is an error with a clear message and an exit code.
- **Failure before the window.** An error before the renderer starts goes to stderr and to the log, with the stage
  and the cause. It does not depend on a message box.

### Settings definition

`settings.txt` stays the one source of the settings. It generates the C tables now. Unit 3 adds an output of the
definition that the launcher and the new UI read (for example a JSON file made at build time, or a CLI command that
prints it): key, type, range, default, words, page, label, help and scope. The read, the write and the check of the
values go to code that has no UI. The launcher and the new UI never keep their own copy of the definition.

### Exit check

From a terminal, without the old GUI: the recompile, the run and the setting changes work, each failure gives its
exit code and a message on stderr and in the log, and a test reads the progress stream of a recompile.

## Unit 4: New UI base and the move of the functions

### Selection between RmlUi and Flutter

Make the same small prototype with each candidate, inside the game window, on a branch that is not merged:

- a settings panel with five rows generated from the settings definition (a switch, a slider, a choice list, a
  number, a text),
- a Japanese text field for the name input.

Check in the game:

- the composition with the game picture on the swapchain (SDL and Vulkan), and the switch between window and full
  screen,
- input leaks: no click, key or text that the UI takes reaches the game, and no game input reaches the UI,
- the capture of the mouse (mouse look) and the return of the focus to the game when the UI closes,
- the Japanese IME: composition text, the candidate window position, commit and cancel,
- the gamepad: move between the controls and change a value without a mouse,
- DPI scaling on a display with a scale above 100 percent,
- the CPU time and the GPU time for each frame with the panel open and closed.

Compare also, from the documentation and the source:

- the MD3 parts that must be written by hand (buttons, switches, sliders, lists, dialogs, text fields, the state
  layers of hover, focus, press and disabled),
- the work to connect it to the existing SDL and Vulkan code and to the render thread and its locks
  (`docs/ui/imgui-plan.md`, "Findings of step 1", has the lock order and the draw point),
- accessibility (screen readers, focus order, contrast),
- the files and the size that the release must ship, the license, and the build in MSYS2 CLANG64 and in CI,
- the work to keep it up to date.

Write the result and the choice in this file. The user decides.

### Shared design values

One set of design tokens for the color scheme, the type scale, the spacing, the corner radius, the icons and the
states (hover, focus, pressed, disabled). The tokens follow MD3. Roboto and Noto Sans JP are in `assets/fonts`
already. The icon set and its license are chosen with the UI base. The launcher can read the same token file, but
this phase does not require it.

### Order of the move

1. The UI base: draw, input gate, focus, IME, gamepad navigation, DPI.
2. The name input (it is needed to start a new game).
3. The settings pages, generated from the settings definition.
4. The performance view (the data of the Debug window and the stats corner).
5. The start and failure displays (the shader preparation progress, the crash panel).
6. The rows of the inventory that the user kept (save states, cheats).

The old UI stays usable until the new UI has its function. Each step is one pull request with a game run.

## Unit 5: Removal of the old UI and Dear ImGui

Remove, after each function has moved:

- Dear ImGui: the sources, the pin in `cmake/dependencies.cmake`, the SDL and Vulkan glue (`imgui*.c*`,
  `imgui_sdl.c`), its settings (`imgui_test`, `legacy_menu` and the others), its tests, and the entry in
  `docs/CREDITS.md`.
- The start GUI (`src/gui/`, `SNX_BUILD_GUI`, `SNX_MULTICALL_GUI`, the call of `snx_gui_main` in `entry.c`,
  `cmake/gui_icons.cmake`, the menu translations in `po/` that only the GUI uses, and the tests of the GUI).
- The old in-game menu (`overlay.c` drawing and pages) and the old drawing base (`platform/ui.c`, `gpu/vk/ui.c`,
  `ui_draw_sdl`), after their last user moves.
- The message boxes of the normal path.

List each removal in `docs/build/runtime-changes.md`. Check that the settings files of a user still load: a key
that is gone is ignored with a log line, not an error.

## Exit condition of phase 7.5

- From the new layout, the CLI recompiles the game and runs it.
- The Engine does not depend on the old start GUI.
- The needed in-game UI works on the new base.
- Dear ImGui and the old UI code and dependencies are gone.
- The inventory shows no function that was lost. The tests, CI and the runs of the target scenes pass.
- The Japanese input, the switch of the input between the UI and the game, and the load of the new UI are checked
  in game runs.

Target scenes for the runs: the title screen, a new game with the player name in Japanese, the field with the
settings panel opened and closed, a battle, and a switch between window and full screen.

## Effect on the other phases

- **Phase 7** removes the keys and the decryption. The keys screen and `--keys` of the inventory go with it. If
  phase 7.5 starts first, unit 5 leaves the keys code to phase 7.
- **Phase 9** keeps the work on the input and the camera themselves: the mouse look in all scenes, the mouse buttons,
  the wheel, rebinding and profiles. Phase 7.5 moves only the screens that exist now. The new screens of phase 9 use
  the new UI base and the settings definition.
- **Phase 11** builds the launcher on the CLI, the exit codes, the progress stream and the settings definition of
  phase 7.5. The launcher plan (Tauri) is separate from the choice of the in-game UI base. Phase 7.5 does not choose
  the launcher technology.
- **Phase 12** builds the release from `engine/` and names the upstream base commit.

## Risks

- The move to `engine/` touches every path in the build notes, CI and the skills. An old path breaks the build or
  the recompile procedure. Unit 1 searches every reference.
- A second UI base on the render thread can break the lock order, as with Dear ImGui.
- Flutter needs its own engine library and runtime in the release. RmlUi needs more MD3 parts made by hand.
- The Japanese IME was only partly done with Dear ImGui (no composition text). The new base must do better, or at
  least the same.
- The start GUI holds functions that the launcher does not have yet. Until phase 11, the CLI is the only way to do
  them.

## Open questions

- Keep or remove the drivers for the other systems (3DS, Vita, GBA, Linux programs) in the Engine.
- The program name (`nxaevora.exe` is the proposal) and the folder and file names of the data folder
  (`%APPDATA%\scarletnx` now).
- Save states: keep them in the new UI or remove them.
- Cheats: chosen in the launcher, in the new UI, or both.
- The on-screen controls and the touch screen: keep the settings or remove them.
