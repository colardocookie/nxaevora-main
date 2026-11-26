<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the NXAevora contributors -->

# Phase 7.5: NXAevora Engine, the CLI and the new UI

Status (2026-11-26): the UI base is Dear ImGui 1.93.0 with its own MD3-style widgets. RmlUi was dropped, and Dear ImGui is not replaced. The current design is in [engine-menu.md](../../dev/ui/engine-menu.md).

Status: plan only. No code is changed, no file is moved and no dependency is added yet. The roadmap entry is
`ROADMAP.md`, "Phase 7.5". This file holds the details. It replaces the plan of the Dear ImGui windows
(`imgui-plan.md`), which is now the old plan and stays as history.

The inventory of unit 2 is checked (state of `main` at `7283f33`, 2026-11-16). No code is changed yet.

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
   - Unit 1.5, **Removal of the other drivers and platforms**, comes after unit 1. Section "Unit 1.5".
2. **Inventory of the UI and its functions**. Section "Unit 2". It is a document change, and it gates units 4 and 5.
3. **CLI** (arguments, settings file, exit codes, progress). Section "Unit 3".
4. **New UI base and the move of the functions**. Section "Unit 4". This unit has several pull requests: the
   selection prototype, the base, then one pull request for each group of functions.
5. **Removal of the old UI and Dear ImGui**. Section "Unit 5".

Unit 2 can start before unit 1 ends. Unit 1.5 starts after unit 1 and ends before unit 3. Unit 3 starts after unit 1.
Unit 4 needs the decision of the UI base and the settings definition of unit 3. Unit 5 comes last.

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
     reviews in `docs/records/archive/` keep their old paths and get one line that names the move.
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
   Changed 2026-11-18: the files `engine/UPSTREAM.md`, `engine/README.md`, `engine/ROADMAP.md`,
   `engine/CONTRIBUTING.md` and `engine/docs/ROADMAP.md` are deleted, and the origin is recorded in the root files
   (`README.md`, `CONTRIBUTING.md`, `docs/CREDITS.md` and `docs/build/runtime-changes.md`).
6. Clean the local Claude files (`CLAUDE.md`, `.claude/WORK_PLAN.md`, `.claude/OLD-WORK_PLAN.md`, the skills and
   `.claude/settings.json`). They are local only and are not in the pull request. Replace the old path and the
   program name. Keep each valid note about the implementation (locks, threads, CRLF files, build traps). Remove
   the notes about the vendored copy that the move makes wrong.

### Limits

- Internal identifiers keep their names. A rename of an identifier is done only where it shows to the user.
- The other drivers of ScarletNX (3DS, Vita, GBA, Linux programs in `src/3ds`, `src/vita`, `src/agb`,
  `src/gnuabi`) are not part of this unit. They are removed in unit 1.5.

### Exit check

The Engine builds in `engine/`, `ctest` passes, CI passes, and the recompile and the start of the update program
work with the new path and the new program name. The user runs the title screen once.

## Unit 1.5: Removal of the other drivers and platforms

Decision of the user (2026-11-16): the Engine keeps only the Switch driver. The Linux host build stays as a later
target.

### Steps

1. PR A removes the guest drivers GBA (`engine/src/agb`), 3DS (`engine/src/3ds`), Vita (`engine/src/vita`) and the
   Linux AArch64 ELF guest driver (`engine/src/gnuabi`), with their CMake targets, tests, CI steps, tools, GUI
   entries and docs (`engine/docs/AGB.md`, `3DS.md`, `3DS-SUPERTUX.md`, `CTR-IPC.md`, `VITA.md`, `GNUABI.md`).
   The old program name check at `engine/src/agb/runtime/main.c:103` goes with it.
2. PR B removes the Android, iOS and macOS frontends and their build support, with `engine/docs/ANDROID.md` and
   `engine/docs/APPLE.md`.
3. Before each PR, an inventory lists every file, CMake target, test, CI step and document that the removal touches,
   and the shared code that the Switch driver or the Linux host still uses. Shared code stays.
4. A docs PR corrects the upstream documents of `engine/docs/` and deletes `TESTING.md` and
   `shader-audit/fixes-001.md` (decision of the user, 2026-11-20).

### Limits

- The Switch driver and the Linux host build do not change. No unrelated refactor.
- The other recompiler targets (`engine/docs/PPC64.md`, `RISCV-LOONGARCH.md`) stay in the Engine and are
  corrected, and their code stays (decision of the user, 2026-11-20).
- CI is Windows only, so Linux-only code and tests can break and nobody sees it. A Linux CI task is proposed when
  the Linux work starts.

### Exit check

The Engine builds, `ctest` passes, CI passes, `--help` and the GUI name no removed driver or platform, and the user
runs the title screen once after each PR.

## Unit 2: Inventory of the UI and its functions

Each function of the old UI goes to one target, or to more than one:

- **CLI**: a command or an option of the Engine, with a text or a machine-readable output.
- **Launcher**: the external launcher (phase 11), through the CLI and the settings definition.
- **New UI**: the new in-game UI.
- **Delete**: not needed for NXAevora.

The rule: a removal of the old UI never removes the work under it. The processing, the save of the settings and
the diagnostic data stay. Code that the UI and the game share is separated first and kept.

### Checked inventory

This inventory is the result of a reading of the source at `7283f33` (2026-11-16). It holds the current code
locations, the shared code to keep, the missing rows and the decisions of the user. A cell that says "check in
unit 4" is a point that this inventory could not decide from the source.

**Start GUI of ScarletNX** (`src/gui/`, the menu that `src/nx/runtime/entry.c` starts with no argument through `snx_gui_main`)

| Function | Code (current `path:line`) | Target | Shared code to keep |
|---|---|---|---|
| Game library, browser, recent games, the systems Switch, Linux, 3DS, Vita and GBA | `src/gui/browser.c:33` (`gui_is_dump`), `:524` (`gui_games_group`), `src/gui/recent.c:22` (`gui_recent_add`), `src/gui/model.c:39` (`gui_menu_key`) | Delete. NXAevora runs one game. The launcher chooses the game folders (phase 11) | `src/common/utilities/game_ini.c` (`snx_game_system`), `src/nx/runtime/driver/driver.c` (`--game`) |
| Recompile queue and its progress | `src/gui/queue.c` (`gui_queue_add`, `queue.h:45`), `src/gui/aot.c` (`gui_aot_cache_info`, `aot.h:22`), `GUI_SCREEN_COMPILING` (`theme.h:47`, `main.c:998`) | CLI (progress lines) and launcher (display) | `src/nx/runtime/driver/recompile.c`, the driver stdout that `gui_steps_feed` parses (`steps.h:37`) |
| Settings of the game and of the launcher | `src/gui/settings.c:43,54` (`gui_settings_read`, `gui_settings_save`), `src/gui/editor.c` | Launcher, through the shared settings definition (unit 3) | `src/nx/runtime/settings.txt`, `settingsgen`, `src/nx/runtime/ini.c`, `src/nx/runtime/options.c` |
| Folders (data, recompiled, work) | `GUI_FOLDERS` (`src/gui/model.h:20`, `src/gui/main.c:139-159`), `src/gui/place.c` (`place.h:21`) | CLI options and the settings file. Launcher for the choice | `snx_app_dir`, `snx_menu_dir` and `snx_user_path` (`src/common/utilities/common.c:1114-1216`), `app.ini [folders]` (`:1137-1170`) |
| Logs in one file for a bug report | `src/gui/logzip.c` (`gui_logs_zip`, `logzip.h:17`), `src/gui/log.c` (`gui_log`, `log.h:24`) | CLI command. Launcher button | None in the runtime. `miniz` stays GUI only. The driver has a log summarizer at `driver.c:477-479` (`--scan-log`), not a zip |
| Clean the shader cache and the code cache | `src/gui/main.c` (`GUI_ASK_CACHE`, about `:3590-3648`) | CLI command. Launcher button | None. The runtime makes the caches again |
| Setup: language, theme, UI scale, folders, keys, first user | `src/gui/steps.c` (`gui_steps_*`), driver `--setup` | CLI without dialogs (defaults and options). The first user is made by the Engine without a dialog | The terminal setup (`src/nx/runtime/driver/setup.c:321`, `src/common/utilities/setup.c`) and the first user (`driver/setup.c:248-305`, `snx_users_start`) |
| Move old files of older versions | `--migrate` (`src/nx/runtime/entry.c:57-62`), `snx_gui_migrate` (`src/gui/main.c:14452`), `src/gui/clean.c` (`gui_move_entries`, `gui_clean_inis`) | Delete (confirmed in unit 2: no older NXAevora install exists). The test `gui_clean` goes with it | None |
| Themes (default, YoRHa), icons, fonts of the menu | `src/gui/theme.c` (`gui_theme_builtin`, `theme.h:140`), `src/gui/themes/default/theme.ini`, `src/gui/themes/yorha/theme.ini`, `src/gui/icons.h`, `src/gui/icons.hex` | Delete | Mixed with the drawing: separate first. `cmake/gui_themes.cmake:9-136` colours feed the runtime old UI (`overlay.c`, `prepare_screen.c`, `hud.c`, `stats.c`, `ui.c`). `icons.h` is used by `overlay.c` and `ui.c` |
| Questions and error boxes of the menu | `GUI_ASK_*` (`src/gui/model.h:42-60`, `src/gui/main.c:1955-1975`) | CLI messages and exit codes. Launcher dialogs | None |

The keys screen was removed by phase 7. `GUI_KEYS` and `--keys` do not exist. The stale comments
`src/gui/themes/default/theme.ini:103` and `src/gui/themes/yorha/theme.ini:103` go with the themes.

The cache clean is `src/gui/main.c` (`GUI_ASK_CACHE`, about `:3590-3648`), not `clean.c`. `clean.c` cleans old
`.ini` keys and belongs to the migrate row.

**Old in-game menu** (`src/nx/runtime/platform/overlay.c`, drawn with the quad list of `src/nx/runtime/platform/ui.c`)

| Function | Code (current `path:line`) | Target | Shared code to keep |
|---|---|---|---|
| On-screen controls and the touch screen | `PAGE_CONTROLS`, `PAGE_BUTTONS` (`overlay.c:19,24-52`), `src/nx/runtime/platform/vpad.c` (`vpad_draw` `:457`, edit mode `vpad_edit` `:381`, showcase `vpad_showcase_draw` `:547`) | Delete (decision of the user, 2026-11-16). `touch.c` and the mouse as the touch screen (`touch=2`) stay as the game's touch service. The settings `controls`, `controls_*`, `showcase` and `showcase_corner` go with it | Mixed with the drawing: separate first. `stats.c:87` calls `vpad_showcase_box`. `touch.c` stays (`src/nx/runtime/platform/touch.c`, `sdl3.c:1288-1309,1652-1655`) |
| Gamepads: player, swap, rumble | `PAGE_PADS` (`overlay.c:53-59,164-171,301-303`) | New UI (input settings) | `src/nx/runtime/platform/backend/gamepad.c` (`gamepad_store`, `gamepad_ini`) |
| Gamepad button map | `PAGE_MAP` (`overlay.c:175-277`), `gamepad.c` | New UI. The map storage stays in the runtime | `src/common/utilities/pad_screen.c` (`snx_pad_screen_*`), the `gamepad_map` and `gamepad_pN_map` storage |
| Save states: slots, save, load, delete | `PAGE_STATES`, `PAGE_SLOT`, `PAGE_ASK` (`overlay.c:19,61-65,143-163,312-333`) | New UI (decision of the user): the slots page, the ask box and the toast. F7, F8 and F9, the MCP `state_info` (`driver/mcp.c:869,993`) and the unattended settings stay | Mixed with the drawing: separate first. `state.c` includes `overlay.h` (`:31`) and calls `overlay_slots_set`, `overlay_toast`, `overlay_states_shown` and `overlay_states_stale` (`:1481-1490,1506-1526,1591-1604`). `state_file.c` and `state.h` stay |
| Stats corner | `PAGE_MAIN` item Stats (`overlay.c:107,289-291,305-309`), `stats.c` (`stats_draw` `:102`) | New UI (performance view) and the log | Mixed with the drawing: separate first. `stats.c` draws with `ui_*` and the theme colours (`stats.c:14-17`). Unit 4 moves its view and keeps its counters |
| Software keyboard box (player name) | `overlay_keyboard_*` (`src/nx/runtime/platform/overlay.h:106-169`, `overlay.c:460-786`) | New UI. The state, the ticket and the AM service path stay in the runtime | Mixed with the drawing: separate first. The keyboard state is in `overlay.c`. The AM request is `src/nx/hle/services/am.c:1085,1239,1344` through `backend_keyboard_show` (`sdl3.c:305`). `src/nx/runtime/main.c:602` (`frontend_loop`) pauses while `overlay_keyboard_shown` is true |
| User badge in the head of the old menu | `user_draw` (`overlay.c:439`), nickname and picture of the user | New UI (optional) | Check in unit 4 |

**Dear ImGui windows** (`src/nx/gpu/vk/imgui*.c*`, `src/nx/runtime/platform/backend/imgui_sdl.c`)

| Function | Code (current `path:line`) | Target | Shared code to keep |
|---|---|---|---|
| Menu bar (About, Settings, Mods, Cheats, Debug) | `src/nx/gpu/vk/imgui_ui.cpp:49-72` (`menu_bar`). About shows `NXAevora` (`:51`) | New UI (one entry point) | None |
| Input settings: mouse, keyboard, controllers | `src/nx/gpu/vk/imgui_settings.c:26-28` (`PAGES`), `imgui_ui.cpp:567-575` | New UI. The page and preset tables become UI-neutral data of the settings definition | `settings.txt`, `ini.c` and `options.c` |
| Display settings: display, graphics, performance | `imgui_settings.c:26-28`, `imgui_ui.cpp:552-566,700` | New UI, same rule | `settings.txt`, `ini.c` and `options.c` |
| Debug window: FPS, frame graph, frame timings, NVML GPU values, CPU and memory | `imgui_ui.cpp:177-195` (`debug_window`), `src/nx/gpu/vk/debug_stats.c:77` (`debug_stats_get`) | New UI (performance view), the log (a periodic stats line), CLI (an option for it). The counters and the hardware poll stay | `debug_stats.c`. The poll starts only from the window (`:302-311`) |
| Test window | `imgui_ui.cpp:74-89`, setting `imgui_test` (`settings.txt:911`, `imgui.c:167`) | Delete | None |
| Name input with the Japanese IME | `imgui_ui.cpp:605-668` (`keyboard_window`), `imgui_text.c`, `imgui_events.cpp:16-19,65` | New UI. The rules of the game's request (length, number-only, Enter, Escape) stay | The game's limits and the ticket (`overlay.h:113-169`, `am.c`) |
| Mods and Cheats menus | `imgui_ui.cpp:59-66` (`unavailable("Not available yet")`) | Mods: launcher (the file layer of phase 7). Cheats: launcher and New UI (decision of the user). The launcher chooses the cheats and the new UI switches them on and off in the game | `cheats.c`, `src/common/utilities/cheat_list.c` and `src/common/utilities/mods.c` stay. `cheats.c` runs from `present.c:27`, `mods.c` from `fs.c:148-174` |
| Input gate (who gets mouse, keys, text) | `src/nx/gpu/vk/imgui_input.c`, `imgui_events.cpp:67-101`, `src/nx/runtime/platform/backend/imgui_sdl.c:87-113` | New UI base. The rules stay: the game gets no event that the UI took, a key-up is never taken, the mouse look stops over the UI | None |

**Start and failure screens**

| Function | Code (current `path:line`) | Target | Shared code to keep |
|---|---|---|---|
| Crash panel | `src/nx/runtime/platform/backend/sdl3.c:813` (`fatal_screen`, loop `:813-988`) | stderr, the log and the exit code always. New UI display when the window works | `src/nx/runtime/platform/fatal_screen.c` holds text helpers only (`fatal_share_log` `:10`, `fatal_log_url` `:29`, `fatal_wrap` `:86`, `fatal_cut` `:115`, `fatal_cp437` `:133`). `overlay.c` and `prepare_screen.c:70` use them |
| Shader preparation screen before the game | `prepare_screen.c:38,115` (`prepare_screen_set`, `prepare_screen_draw`) | Progress lines (CLI) and a new UI display | Drawn by `vk_ui_draw` (`src/nx/gpu/vk/ui.c:353`). No other user |
| Debug warning line | `hud.c:19,46` (`hud_debug_set`, `hud_draw`) | Log. New UI where the user must see it | Drawn by `vk_ui_draw` (`src/nx/gpu/vk/ui.c:364`) |
| Error and help message boxes | `backend/sdl3.c:1027,1048,1050,1071,1623`, `src/gui/main.c:14484,14486` | stderr, the log and the exit code. No dialog in a normal run | None |
| Old UI drawing base | `src/nx/runtime/platform/ui.c` (`ui_fill` `:78`, `ui_text` `:82`, `ui_icon` `:92`, `ui_image` `:98`), `src/nx/gpu/vk/ui.c` (`vk_ui_draw` `:333`), `ui_draw_sdl` (`backend/sdl3.c:741`, used `:981`) | Delete after the last user moves | It has the users of the on-screen controls, stats, keyboard, crash panel, prepare screen and debug warning rows |

**Missing rows found in unit 2**

| Function | Code (current `path:line`) | Target | Shared code to keep |
|---|---|---|---|
| Hotkeys of the old UI | `hotkey` (`src/nx/runtime/platform/backend/sdl3.c:1066-1128`) | F1 help box (`:1068-1073`): Delete, the text goes to `--help` and the log. F6 (`:1097-1105`): Delete with the on-screen controls. F7, F8 and F9 (`:1107-1109`): stay (save states). F12 stats (`:1110-1119`): New UI (performance view). Pause (`:1124-1127`) and F11 (`:1123`): stay (not UI) | The save-state, pause and full-screen paths stay |
| Toast notifications | `overlay.c:373-377,977-987`, the log-full toast at `src/nx/runtime/platform/backend/sdl3.c:1897-1899` | New UI (shared: the text and the time of each toast) | Mixed with the drawing: separate first. Used by save states (`state.c:1481-1490`), screenshots (`sdl3.c:1924`) and the overlay |
| Overlay navigation and placement | `overlay.h:35-39` (`overlay_select`, `_move`, `_scroll`, `_side`, `_back`), `overlay.h:30-34` (`overlay_place`, `_cols`, `_set_display_scale`) | New UI base (focus and gamepad navigation). Delete the old code | None |
| Start-menu gamepad navigation | `src/gui/pad_menu.c` (`gui_pads_*`, `gui_pad_key`) | Delete | None |
| Child-process runner | `src/gui/process_win32.c`, `src/gui/process_posix.c`, `src/gui/gui_process.h` | Delete. The CLI (unit 3) and the launcher read the progress stream | None |
| NRO game discovery through `source=` | `job_play` (`src/gui/main.c:11629-11652`), `snx_record_source` (no NX caller), `tests/gui_sdl.c:583` writes the `source=` itself | Delete with the start GUI. The launcher finds the game through the CLI | None. The NX recompile writes only `romfs=` (`recompile.c:520`) |
| Start-menu translations | `src/gui/i18n.c`, `po/`, `scripts/build-languages.py` | Delete with the start GUI. The language support of the new UI is decided with the UI base (unit 4) | None |
| Start-menu glyphs | `src/gui/latin.c`, `src/gui/wide.c`, `src/gui/unifont-*.hex`, `scripts/build-glyphs.py` | Delete with the start GUI | None |
| Start-menu boot splash | `src/gui/splash.cpp`, `cmake/splash.cmake`, `dist/wimsy.svg`, `dist/splash/` | Delete with the start GUI | Keep lunasvg and plutovg: `src/nx/hle/services/acc.c` uses them |
| Android and iOS frontends | `src/gui/android.c`, `src/gui/apple.m`, `dist/apple/Frontend.swift` | Delete with the start GUI | None |

Kept without a change of target (not UI): `debug_stats.c`, `keybind.c`, `gamepad.c`, `cheats.c`, `mods.c`,
`settings.txt` and `ini.c`, `unattended.c` (CI tests), `tracy.c`. `stats.c` is not kept as it is: it draws with
`ui.c` and the theme colours, so unit 4 moves its view and keeps its counters.

**Settings that only the old UI uses**

- `imgui` (`settings.txt:897`), `legacy_menu` (`:904`) and `imgui_test` (`:911`).
- The `On-screen controls` section and its `Buttons` page (`:1144`), with `controls` and `controls_*` (`:329-386`)
  and `showcase` and `showcase_corner` (`:132-159`). `stats` and `stats_corner` stay.
- The `Player` page (`:1147`) and `gamepad_p<N>_*` (`:539-580`). They move to the new UI input settings.
- Unit 5 must load an old settings file with a gone key. The key is ignored with a log line.

**Tests of the old UI**

- Unit 5 removes the `gui_*` tests and `uigen` (`src/gui/CMakeLists.txt:143-274`), the splash tests
  (`cmake/splash.cmake:99-113`) and the tests `overlay`, `vpad`, `hud`, `fatal_screen`, `imgui_input`,
  `imgui_text` and `imgui_settings` (`tests/CMakeLists.txt:744-769,858-871,902-905`).
- `stats`, `gamepad`, `keybind` and `settingsgen` stay or move with their code.

### Open points of the old windows

- The Debug window shows "?" for the CPU temperature and power and for the AMD and Intel GPU values.
  `system_poll` fills only `cpu_load` and `ram_*` (`debug_stats.c:246-260`), and the GPU values need NVML
  (`:114-239`). The hardware poll runs only while the window is open (`:302-311`). Row: Debug window.
- The input gate: an arrow key on the frame the pointer arrives reaches the game (`backend/imgui_sdl.c:73,88`,
  `imgui_ui.cpp:224`, `docs/records/archive/controller-keyboard-review.md:50-56`). The new UI base must not repeat it.
  Row: Input gate.
- The test window goes in unit 5. Row: Test window.
- The comments and strings that still name a dump go with the start GUI: `src/common/utilities/game_ini.h:17` and
  `src/gui/main.c:3544,3645,6861,6862,10688`. `src/common/filesystem/tree.h:56` is now stale wording.
  `game_ini.h:17` is fixed in unit 1 or 3.

### Exit check

Every row has a target and a short reason. The rows with a decision of the user name it. The shared code of
each row is named, so that unit 5 can remove the drawing and keep the work.

## Unit 3: CLI

### What the CLI defines

- **Commands.** At least: recompile the game, run the game, list and check the settings, export the logs, clean
  the caches. The current options (`-r`, `--game=DIR`, `--settings`, `--KEY=VALUE`, `--data`, `--outdir`) are the
  base. A command for each job is better than a mode that the argument count selects.
- **Inputs.** The decrypted ExeFS and RomFS of phase 7, the data folder, the output folder and the settings file.
  Each one has an option and a default. The working directory has no effect on the result, or the CLI says which
  path it uses.
- **Settings file.** One file format and one place (now `<config>/<id>.ini` under `%APPDATA%\nxaevora`,
  `engine/src/common/utilities/common.h:113`). The CLI can read, write and check a setting without a start
  of the game.
- **Exit codes.** A table: success, wrong usage, missing or wrong game files, recompile failure, start failure
  before the renderer runs, crash of the game, stop by the user. The launcher acts on the code only.
- **Progress and errors for the launcher.** A machine-readable stream (the proposal is JSON Lines on stdout with an
  option, and the human text on stderr). Each line has a stage, a value and, on an error, the stage that failed and
  the cause. `entry.c` already sets line buffering for a launcher on a pipe. The format has a version number.
- **No dialog.** A normal recompile or run needs no GUI and no question. The setup asks nothing when the options and
  the settings file give the values. A missing value is an error with a clear message and an exit code.
- **Failure before the renderer.** An error before the renderer starts goes to stderr and to the log, with the stage
  and the cause. It does not depend on a message box.

### Settings definition

`settings.txt` stays the one source of the settings. It generates the C tables now. Unit 3 adds an output of the
definition that the launcher and the new UI read (the command `settings schema`, see "Commands"): key, type,
range, default, words, page, label, help and scope. The read, the write and the check of the values go to code
that has no UI. The launcher and the new UI never keep their own copy of the definition.

### Commands

The program is `nxaevora-engine`. The first argument selects the command. A command name is only `argv[1]`. A
relative path argument uses the current directory. Every other path does not depend on it.

| Command | Job | Old form |
|---|---|---|
| `recompile <game folder> [options] [--KEY=VALUE]` | Recompile the game. All current recompile options stay | `-r` |
| `run <game program> [--KEY=VALUE] [-- game arguments]` | Run the game | Play mode (`nxaevora-engine --game=PATH`, no command) |
| `settings list [--scope=game\|recompile]` | Print the nine-field settings text, with the current format | `--settings`, `--settings=recompile` |
| `settings schema` | Print the settings definition as one JSON document on stdout | None |
| `settings get KEY --game=PATH` | Read one value of the game's `<config>/<id>.ini` | None |
| `settings set KEY VALUE --game=PATH` | Write one value of that file | None |
| `settings check KEY VALUE` | Check one value against the settings definition. It reads and writes no file | None |
| `logs export --game=PATH [--out FILE.zip]` | Write one zip with the diagnostic files | None |
| `cache clean --game=PATH [--shaders] [--code] [--units]` | Delete the game caches | None |

`settings list` refuses other arguments with exit 2. It does not ignore them.

`settings get` and `settings set` are exit 1 when the `.ini` cannot be read, or when it has a key that the store
cannot keep; `set` is exit 1 when the `.ini` cannot be written. They are exit 2 for a bad argument or a key that is not
a setting, and exit 3 when there is no game or no `.ini` at `PATH`.

`settings schema` comes from the `settings.txt` tables at run time. It is not a file made at build time. The
top level is `{"v":1,"settings":[...]}`. Each setting has these fields:

| Field | Content |
|---|---|
| `key`, `section`, `page` | The names in `settings.txt` |
| `type` | `int`, `float` or `text` |
| `min`, `max` | The range |
| `default` | The default value |
| `words` | The choice words |
| `label`, `help` | The label and the help text |
| `scope` | `startup`, `in-game`, `recompile` or `launcher` |

In-game rows are in the schema, unlike `list`.

`settings get` and `settings set` use the same file that `engine/src/nx/runtime/main.c:770` opens.
`settings check` reads and writes no file and needs no `--game`. It uses the same check as `ini_check`
(`engine/src/nx/runtime/ini.c:68-84`) against the value and the settings definition only. The three setting
commands need no window and no game start. The code has no UI dependency.

`logs export` writes one zip. It holds the game log, the kept older logs (`<state>/logs/`), `threads.txt`, the
`.ini` and the early error file. miniz moves from the GUI-only dependency to the runtime. The default output is
`<state>/<id>-logs-<date>_<time>.zip`.

`cache clean` with no flag deletes the shader cache and the `<id>.jitcache` of the game. `--units` also deletes
the recompile unit cache (`<cache>/units`). The command stops while the game runs only if a check exists that
needs no guess. Without such a check, the rule stays a rule of use and the command does not test it.

These developer tools stay as they are: `--help`, `<command> --help`, `--scan-log`, `--dump-modules`, `--mcp`,
`--setup` and `--no-setup`.

The old flags are aliases:

| Old form | Command |
|---|---|
| `-r` | `recompile` |
| Play mode (`nxaevora-engine --game=PATH`, no command) | `run` |
| `--settings` | `settings list` |
| `--settings=recompile` | `settings list --scope=recompile` |
| No argument | The old start GUI, until unit 5 |

### Exit codes

One header holds the named constants. The proposal is `engine/src/common/utilities/exit_codes.h`.

| Code | Meaning |
|---|---|
| 0 | Success. Also a normal game end and a window close by the user |
| 1 | Internal error: a bug, out of memory, or a settings file the store cannot keep |
| 2 | Wrong usage: unknown command or option, bad argument count |
| 3 | Game files missing or wrong: the input folder, `exefs/main`, the RomFS or `romfs=` |
| 4 | Recompile failed: compile, link or build |
| 5 | Start failed before the renderer runs: the settings file is not readable, or Vulkan or the renderer did not start |
| 6 | Crash of the game: `fatal`, a crash report or an unhandled exception |
| 7 | Setting value refused: `settings set` and `settings check` |
| 8 | The game ended with a nonzero code. The code goes to the log and to `game_code` of the `run` stage's error line |
| 126 | POSIX only: a driver stage program could not be prepared (process group, terminal or working directory) |
| 127 | POSIX only: a driver stage program could not be started (exec failed) |
| 130 | Stopped by the user (Ctrl+C). This value does not change |

A child stage uses the same table, so the driver passes its code on. A child code above 255 stays 1. On POSIX a
stage that cannot be prepared exits 126 and one that cannot be started exits 127; the driver passes both on. On
Windows a start failure is 1.

### Progress stream

`--progress=jsonl` works for `recompile` and `run`. Then stdout carries only JSON Lines and the human text goes
to stderr. Each line is one object with `"v":1`, `"stage"` and `"event"`, and the optional fields of the event.
The stages are `input`, `recompile`, `build`, `prepare` (shader preparation), `start` (window) and `run`. `extract`
is not written: the command path loads the folder with `snx_load_program` and has no extraction step. `link` is not
written: the link is inside the module build. With `--quiet` the stream has no `progress` lines.

The outermost command process writes the exit line. A raw folder recompile runs in the driver and writes `input`
(the target check and `snx_load_program`), `recompile` (the module build) and `build` (the runtime build, the
deploy and `Built`). For a prepared game and for `run`, the driver writes `input` for its own checks, gives
`--progress=jsonl-stage` to the recompile child and the game child, passes their codes on, and writes the exit
line. A child in `jsonl-stage` mode writes stage and error lines and never the exit line.

| Event | Fields |
|---|---|
| `start` | None |
| `progress` | `"value"`, `"total"`, for example the compiled blocks |
| `done` | None |
| `error` | `"message"` with the cause, `"code"` with the exit code |

The last line is the exit line, `{"v":1,"event":"exit","code":N}`. It has no stage. When the game ends with a
nonzero code, `process_exit` writes the `run` stage's error line with `"code":8` and one more field,
`"game_code":N`, the game's own code, and the exit line has `code` 8. An error after a stage has closed gets that
stage's name, when the process noted an error after the close. `v` changes when a field changes its meaning. A test
reads the stream of a recompile (PR 3d).

### Failure before the renderer runs

Only a `fatal` before the renderer starts goes to stderr, the log and the early error file `scarletnx-error.log`. A
usage error (exit 2) before the log file exists goes to stderr only. The internal name stays, per the "Internal names"
decision. `fatal_code` also writes the error line of the open stage of the progress stream, with the fatal text and
the code the process ends with.

No dialog shows before the renderer runs. After the renderer runs, the crash box stays until the new UI of unit 4
replaces it. It does not show with `--headless` or in an unattended run.

### Pull requests

| PR | Content |
|---|---|
| 3a | The exit-code table, the failure before the renderer, and this design text |
| 3b | The subcommand dispatch with the old-flag aliases |
| 3c | `settings list\|schema\|get\|set\|check` |
| 3d | The JSON Lines progress stream with its test |
| 3e | `logs export` (zip) and `cache clean` |

Each PR builds, passes `ctest`, and updates `engine/docs/USAGE.md` for its commands.

### Exit check

From a terminal, without the old GUI: the recompile, the run and the setting changes work, each failure gives its
exit code and a message on stderr and in the log, and a test reads the progress stream of a recompile.

## Unit 4: New UI base and the move of the functions

### RmlUi prototype (dropped)

The UI base was RmlUi (user decision 2026-11-25). Flutter was checked and dropped. The Flutter embedder that Flutter
publishes for Windows x64 is a debug (JIT) build only, so a release build needs an engine build from source, which
Flutter does not support for custom embedders. Flutter also needs the Dart toolchain and its own engine library
(about 38 MB) in the release.

Dropped: make a small prototype with RmlUi, inside the game window, on a branch that is not merged:

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

Record for RmlUi (dropped), from the documentation and the source:

- the MD3 parts that must be written by hand (buttons, switches, sliders, lists, dialogs, text fields, the state
  layers of hover, focus, press and disabled),
- the work to connect it to the existing SDL and Vulkan code and to the render thread and its locks
  (`docs/records/ui/imgui-plan.md`, "Findings of step 1", has the lock order and the draw point),
- accessibility (screen readers, focus order, contrast),
- the files and the size that the release must ship, the license, and the build in MSYS2 CLANG64 and in CI,
- the work to keep it up to date.

Write the result in this file. If RmlUi fails a check, the user decides the next step.

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
- The old in-game menu (`overlay.c` drawing and pages) and the old drawing base (`src/nx/runtime/platform/ui.c`,
  `src/nx/gpu/vk/ui.c`, `ui_draw_sdl`), after their last user moves.
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

- **Phase 7** removes the keys and the decryption. The keys screen and `--keys` are already gone.
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
- A removal of unit 1.5 can delete code that the Switch driver or the Linux host shares. The inventory of each PR
  lists the shared code first.
- A second UI base on the render thread can break the lock order, as with Dear ImGui.
- RmlUi has no MD3 controls, so each control and its states (hover, focus, press, disabled) are made by hand. RmlUi was dropped as the UI base on 2026-11-26.
- The Japanese IME was only partly done with Dear ImGui (no composition text). The new base must do better, or at
  least the same.
- The start GUI holds functions that the launcher does not have yet. Until phase 11, the CLI is the only way to do
  them.

## Open questions

- The drivers for the other systems (3DS, Vita, GBA, Linux AArch64 programs) and the Android, iOS and macOS
  frontends are removed in unit 1.5 (decision of the user, 2026-11-16).
- The other recompiler targets (`engine/docs/PPC64.md`, `engine/docs/RISCV-LOONGARCH.md`) stay in the Engine and
  are corrected, and their code stays (decision of the user, 2026-11-20).
- The program name and the data folder are decided by PR #58: the program is `nxaevora-engine.exe` and the data
  folder is `%APPDATA%\nxaevora`.
