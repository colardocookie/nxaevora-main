# Old work plan: what was done

This file records the work that is finished. Add new finished work at the end of the section "Log". The current
tasks are in `WORK_PLAN.md`. The rules of the project are in `CLAUDE.md`.

## How to work with the user

- The user writes Japanese. Answer in Japanese. Files of the project are in English.
- The user runs and tests the game. Claude never starts the game. The user does not read logs. Claude finds the
  files of a run, reads them and reports the numbers.
- The user makes the commits with GitHub Desktop. Claude gives the summary and the description of a commit.
  Claude never pushes.
- Ask the user only about real decisions. Say what you could not verify.
- Note: a log folder `game/recompiled/<id>/logs/` keeps one log for each run. Read ALL logs of a period, not only the
  newest `<name>.log`. A crash was missed once because only the newest log was read.

## Facts

- Game: Pokemon Legends Arceus, title ID 01001F5010DFA000, version 1.1.1.
  The update has the title ID 01001F5010DFA800 and the version number 262144.
- Dumps (outside the repository): base `D:\.sw_roms\Pokemon Legends Arceus.nsp`, update
  `D:\.sw_update + dlc\Pokemon Legends Arceus_UPDv111.nsp`. Keys are in `%USERPROFILE%\.switch`.
- Portable ScarletNX: `game/scarletnx/bin/scarletnx.exe` (the MSYS2 DLLs are next to it).
- Output of the recompile: `game/recompiled/01001F5010DFA000/`. The program of the base game is
  `01001F5010DFA000.exe`. The program of the update is `01001F5010DFA000_01001F5010DFA800v262144.exe`.
- The base program and the update program have different module addresses (sdk and subsdk are 0x45000 higher in the
  update). A hints file is valid only for the program that wrote it. The user must start the UPDATE program.
- Update layout bases: rtld 0x8000000, main 0x8005000, subsdk0 0xc357000, subsdk1 0xc9fe000, sdk 0xd3c7000.
- Extracted ExeFS with the update layout: `game/work/Arceus` (the RomFS in it, 6 GB, is not used).
- The hints file of the update has 45734 pcs (45728 jump-table targets from `tools/jump_table_scan.py` and the pcs
  that the runtime recorded).
- The extracted ExeFS and RomFS (update layout) are in `game/work/Arceus` (the RomFS is 6 GB). The runtime reads the dump in
  place, not these files. They serve the analysis (shader containers, module dumps for `tools/disasm.py`).
- The machine of the user: Windows 11, Intel i5-12400F (12 threads), 16 GB RAM, NVIDIA GeForce RTX 2060 SUPER.
- Settings in `01001F5010DFA000.ini`: `async_pipelines=1` (set on purpose), `prepare_shaders=2`,
  `pipeline_threads=0`.

## Log

### 1. Setup (done)

- Made the repository, the `.gitignore` (`.claude/`, `CLAUDE.md`, `AGENTS.md`, `game/`) and the folder layout.
- Added ScarletNX as a submodule (upstream URL, pinned commit 8efbf9c). Not changed.
- Built ScarletNX on Windows with MSYS2 CLANG64. Needed `-DCMAKE_C_FLAGS=-mcx16` (two test targets failed without it).
  Notes: `docs/SCARLETNX_BUILD-NOTES.md`.
- Made a portable copy of the program (`game/scarletnx/`) with the 7 DLLs.

### 2. First recompile and run (done)

- Recompiled the base game and the update with `scarletnx -r`.
- The game started and ran without crash to the language selection screen.
- Mistake: started the recompile twice (a log was empty and was taken as a failure). Two recompiles wrote the same
  files. Stopped both, deleted the output and started again. Lesson: check `Get-Process scarletnx` before a start. An
  empty log is normal. The exit code in a task notification can be the code of another command.

### 3. JIT miss analysis (done, closed for now)

- A miss is a pc that the recompiled code does not have. The runtime JIT compiles it and records it in `.hints`.
- Mistake: the first run used the BASE program, not the update program. The first analysis disassembled base pcs
  against the update modules, and the findings were wrong. They were removed. Lesson: check that the program, the log
  and the extracted ExeFS have the same layout (`Loaded <module> at` lines).
- Finding from the source of ScarletNX: the recompiler marks leaders (function starts, branch targets, return sites)
  and fills `CPU.disp`. `explore()` stops at `br` and does not read jump-table entries. So jump-table targets are
  missed.
- Verified one pc (0xae40adc) as the entry 0 of a jump table.
- Wrote `tools/jump_table_scan.py`. It found 45728 targets in main, subsdk0, subsdk1 and sdk. 49 of the 51 recorded
  misses (not counting 4 PLT stubs) are targets. Not explained: 0x8cbe780 (main) and 0xd92f310 (sdk).
- Fed the targets as hints (`--merge`) and recompiled the update (666 s, code +2.1 MB).
- Result: 0 misses in all later runs: the opening scene at sea (252 s, before: 114 misses), name entry with the
  software keyboard, saving, loading, a capture, the village entry (420 s), a battle against an NPC up to the point
  before the Obsidian Fieldlands (794 s), and the Obsidian Fieldlands (252 s). All at 30 fps.
- Details: `docs/jit-analysis.md`.

### 4. Decisions (done)

- The JIT stays in the released build as a safety net. LLVM stays, so `libLLVM-23.dll` is shipped.
- ScarletNX has no setting that turns the JIT off. A miss is a hard error only when the JIT cannot start. A patch of a
  few lines in `jit_init()` (`guest_jit.c`) would do it. It is optional.
- Roadmap phases 1, 2 and 3 are closed for now. Open them again if a new miss appears.
- Copyright line stays `Copyright 2026 the NXAevora contributors`.

### 5. Documents and tools (done, committed)

- `README.md` (the user edited the badges and the logo), `CONTRIBUTING.md`, `LICENSE` (GPL-3.0), `ROADMAP.md`,
  `docs/SCARLETNX_BUILD-NOTES.md`, `docs/jit-analysis.md`, `docs/stutter-analysis.md`.
- `tools/classify_hints.py` (sorts hints by module), `tools/jump_table_scan.py`.
- Logo in `assets/` (normal and dark variant).
- Skills in `.claude/skills/`: `nx-recompile`, `nx-log-read`, `nx-jit-triage`.
- The roadmap has a section "Candidates for native replacement" (graphics, audio, input, video, memory, files) with
  benefits and drawbacks. Suggested order, not decided: pipelines, files, video, input, audio and NVN, memory last.

### 6. Stutter analysis (done in part)

- ScarletNX already prepares the shaders ahead of time (`prepare_shaders=2`) and makes the pipelines of earlier runs
  again at the start. A pipeline for a new draw state cannot be made before the game uses it.
- Most slow frames are not from pipelines. In one run of 252 s, 21 frames took 50 ms or more, and only 1 made
  pipelines.
- The user deleted the shader cache and set `async_pipelines=1`. Result: 336 pipelines, 0 ms of waiting, 1001 draws
  skipped in 375 s, and the user sees clearly less stutter. Use `async_pipelines=1` as the default of the launcher.
  The user did not say yet whether late appearing objects were seen.
- Not explained: a stop of 1.2 to 1.4 s that every run has 11 to 18 s after the start (GPU idle, no pipeline, no
  upload, so it is on the CPU side), and other slow frames in groups (for example 176 s to 177.7 s in one run).
- Details: `docs/stutter-analysis.md`.

### 7. Found at the end of this session

- The folder `game/recompiled/01001F5010DFA000/logs/` keeps the log of each run, and `scarletnx-error.log` holds a
  crash line: 2026-10-31 00:50:22, "The game crashed (invalid memory access)". This crash was not noticed before. It
  is the first item of `WORK_PLAN.md`.

## Git

The tree was clean after 9674bbd ("Record the shader and stop analysis and add the patch files"), except the submodule, which
shows as changed because patches 0001 and 0002 are in its working tree (never commit that). Earlier commits: d333c8b (README),
e587d40, 750a34a, 1bdb641, 4f0a181, 69a17d7 and older.

## Notes about temporary files

- The scratchpad of a session is deleted with the session. Backups of the hints and of the ini that were put there may
  be gone. The hints can be made again with `tools/jump_table_scan.py --hints FILE --merge`.

### Crash of 2026-10-31 00:50:22 (closed by the user)

- 1 of 5 runs crashed at the start (invalid memory access in a97a858, release of a handle-table entry). The cause is
  not found. The user said that this rare crash at start can be ignored. Notes: `docs/crash-analysis.md`.
- Open it again only if the crash comes back.

### Profile run (done)

- Run L (90 s, `profile=1`, `vk_gpu_profile=1`): 95% of the CPU samples are waits, the GPU uses 6 ms of 33 ms, the user
  saw no stutter while walking. Details in `docs/stutter-analysis.md`. The settings are back to 0.

### Native replacement plan (done)

- Decision: replace a layer only with a measured problem. Video is out (the movies are in-engine cutscenes). Files are
  out for now (run M). Graphics, memory and audio are not started. Input is the one layer to replace, with the
  keyboard and mouse as native input (the user asked for it because the mouse look is poor). `ROADMAP.md` has the
  new "Decision" section. Details: `docs/input-analysis.md`.
- Run M made a second game folder in `%APPDATA%\scarletnx
ecompiled` (12.3 GB). The user allowed the deletion and it
  was deleted. A start through `scarletnx.exe <dump>` uses that folder, not the project folder.

### Native mouse input and camera search (paused)

- Built the stick-based mouse look (patch 0001) and a stick tracer. The tracer found the path of the stick in the game
  (manager 0x8c9f9b4, distributors 0xa50d75c and 0x96f41a8, many copies), but not the camera code. A stick value past the
  range and an unclamp patch of the manager did not speed up the camera. The user paused the search.
- Mistakes to remember: a search for calls to SDK functions finds nothing, because the game calls them through PLT stubs
  (resolve the stub names from the relocation table). The first scan can fill the word table with a table of copies (limit the
  matches for each page). Page protection of hot pages slows the game to a crawl. A build of a patch with string literals
  made by Python scripts lost the backslash of `
` more than once: use the Edit tool for such lines.

### Keyboard remap (done, 2026-10-31)

- Patch `0009` adds the setting `keyboard_map` (`Button:Key` words, `docs/input-analysis.md`). The user tested it
  (`keyboard_map=ZL:Q A:F`) and the keys changed as set.
- The first test lost the line. The store of the `.ini` (`STORE_MAX` in `options.c`) held 128 keys and the file had 129 (the
  log said `more than 128 keys, renderdoc_frames and those after it are left out`). A key after the 128th is dropped at the
  read and erased at the next write. Patch `0010` raises it to 256. Look for this line in the log when a key of the `.ini`
  does not stay.
- The runtime rewrites the `.ini` at each start: a new key moves to its section with its help text. This is normal.
- Tool mistake: the Bash tool loses the backslash of `\r\n` in a Python script. Build such characters with `chr()`.

### Native mouse look (done, 2026-10-31)

- The user found the stick based mouse look slow to answer: a big move turned the camera late, because the game turns the
  stick into a speed with a top speed. The camera search was opened again and ended in four runs of the angle search
  (patch `0011`, after `0003`): run 1 and 2 gave copies and no proof, run 3 with an x only and a y only hold named the
  yaw (-3.742 rad, 2.1 rad a second at 0.61 of the stick) and its writer 0x99437c0. Details in `docs/camera-analysis.md`.
- The camera controller's angle setter 0x99437c0 takes the angle in `s0`, `this` in `x0`, and keeps the yaw at `+0x360` and
  `+0xdd80` and the pitch as a double `t` (0 to 1, the position on the camera's arc) at `+0x700`.
- The mod `native-mouse-look.pchtxt` adds the mouse to both at the start of the setter. The runtime (patch `0012`) adds
  the motion to a 64 bit word at guest 0x9943980 (`mouse_native_address=160708992`). The user tested it in the field: it
  answers at once, and the direction is right after `-dx` for the yaw. Defaults: `mouse_native_yaw=1400`,
  `mouse_native_pitch=1000`, `mouse_invert_x`, `mouse_invert_y`.
- Mistakes to remember: (1) code that a mod adds must lie inside `text_size` of the NSO header (the recompiler and the JIT
  take nothing else, and the JIT refuses an image inside the main dispatch table). A zero padding after the text is not
  code. The text had no hole, so the space came from the end of a path that copied the end of another path. (2) The
  Edit tool and `git diff` change line endings: the files of the submodule have CRLF and the patches have LF; build the
  patch from LF copies with `diff -u` (the home folder is a git repository, so `git apply` in a temp folder is skipped).
  (3) A `.ini` of 129 keys hit the limit of 128 keys of the store (patch `0010`). (4) The member of `g_settings` takes the
  name of the setting, not its key. (5) `python - <<EOF </dev/null` hangs: use a script file.

### Phase 5: frame rate limit and shader failures (done)

- The 30 frames each second come from the game: it sets a swap interval of 2. The native replacement review found no
  layer with a measured problem (`ROADMAP.md`, "Decision"). The stop of 1.3 s at the start stays open. The user said it
  is not needed now.
- The 80 failed shaders were 11 by hash (`docs/shader-analysis.md`). 6 vertex programs wait for their first draw, 4 hit
  data that looks like a jump table (not verified), and 1 fragment program had `RED.E.ADD.64`.
- Patch `0002-global-red-add-u64.patch` adds `RED.E.ADD.64` (Int64Atomics, a 64-bit view of each global buffer, the
  feature `shaderBufferInt64Atomics`). The run of 157 s showed 0 skipped draws (600 in each 20 s before), no crash, and
  the user saw no change on the screen. The patch is applied to the working tree of the submodule and is not committed.
- The stick trace was split out of patch 0001 into `0003-stick-trace.patch` (not applied). The `trace_stick` keys are
  removed from the game `.ini`.
- README: the logo uses `<picture>` and the ScarletNX link is an HTML anchor (not committed).
- Also in Phase 5: the texture of format 0 is an empty header in the slot of one compute program (read as zeros, no
  change). The 4 programs with "unknown opcode" are jump table data after the code (the same form in `main.data`, which the module
  dump showed). `tools/maxwell_label.py` was written for it.
- Crashes at 13:49:56 and 13:57:43 on 2026-10-31 (invalid memory access) are in `scarletnx-error.log`. Their logs are
  gone, so they are not analyzed. Open `docs/crash-analysis.md` again if a crash comes back.
- The 4 shader programs with "unknown opcode" were jump table data (the table follows the code, and the length scan of
  the pass before the start runs into it). A run of 681 s with the bag, the box, the clothes, the battle introduction
  and a large part of the open areas had no failed shader at a first draw, no skipped draw, no crash, and 0 JIT misses
  at 30 fps. The user thinks that no JIT miss is left. The check of the hints (`tools/jump_table_scan.py`) holds for
  all the tested scenes.

### The stop of 1.3 s at the start (explained, no fix)

- Found with the diagnostic patches 0004 to 0008 (`hitch_dump`: the threads, the RomFS read time, the GPU thread, the sleeps
  and the pointers of a stop). The stop is a loop of the game's main thread (guest `a7678f0`): 39 pooled objects, each started
  by its first poll (the done-check function `8c3d488` is a state machine), and a `svcSleepThread` of 16 ms between polls.
  The sleeps took 15.7 to 16.5 ms each, no other guest thread was busy, no RomFS read took 50 ms, the GPU thread was idle.
  About 185 ms of each 400 ms was the main thread's own work (about 8 ms for each entry). Details: `docs/stutter-analysis.md`.
- Mistakes to remember: an estimate of one sleep from the time between two dumps was wrong (38 ms), because the dump takes time
  and the main thread works between sleeps. The per-call time (requested and taken) from the dump was the right measure.
  A search of the files with `rm` of the hitch files before they were copied lost the data of the last run: copy first.
- The patches 0004 to 0008 are optional diagnostics (not applied). The runtime of the project is built with 0001 and 0002.
- Phase 5 has no open item now. What is left in `ROADMAP.md` is the other slow frames (cause not known) and the tests of scenes
  that are not tested yet.


## In-game ImGui windows, steps 1 to 9 (done, details in `docs/imgui-plan.md`)

- Patches `0013` to `0020` in `recomp/patches/`. ImGui 1.93.0 (MIT) is a pinned CPM package. Roboto and Noto Sans JP are
  in `assets/fonts` (SIL OFL 1.1) and are copied to `fonts/` in each game data folder (the runtime reads them there; the
  built-in font is the fallback). `docs/CREDITS.md` lists them.
- The renderer draws ImGui after `vk_ui_draw` (`gpu/vk/imgui.c`): `vk_rec_sync` drains the recording ring, then ImGui
  records straight into the frame's command buffer in its own dynamic rendering scope. ImGui is touched only under
  `vk_lock`. The window's events go through a queue (`imgui_input.c`) and the frame publishes what the windows want
  (mouse, keyboard, text, cursor release, the input method's place). ImGui's own SDL3 backend is not used.
- Settings: `imgui` (default 1), `imgui_test`, `legacy_menu` (default 0). The menu bar shows when the cursor is within 24 px
  of the top (Alt frees the cursor from the mouse look). The Debug window reads NVML (loaded at run time) and the
  Windows system times and memory, once a second, only while it is open.
- The name input: `overlay.c` keeps the state and the answer, the window shows a copy (`overlay_keyboard_view`) and answers
  with the ticket (`overlay_keyboard_apply`). The old box returns when the windows do not run.
- Facts of ImGui 1.93.0 that cost time: the character filter gets no line (use `CallbackEdit` for a limit), `ImWchar` is
  16 bit unless `IMGUI_USE_WCHAR32`, `Style.Alpha` (not `WindowBg`) makes the whole window translucent.
- Tests that pass after the patches: the shader and settings tests (18). Three others fail and were not compared with a
  build without them: `gui_sdl_events`, `native_recompiler`, `llvm_module`.

### ImGui code review fixes and test cleanup (done, 2026-11-01)

- The 23 findings of the review (`docs/imgui-review.md`) are fixed and run by the user: the name input (group 1), the input
  queue, Alt, the Vulkan result check (group 2), the NVML loader, the Debug window and the code rules (group 3), the refresh
  of the windows while the game presents nothing (15) and the fallback to the old menu by the backend's state (8).
- The three failing tests had two causes. `native_recompiler` and `llvm_module` need `APPDATA` in the MSYS2 shell. `gui_sdl_events`
  needed group icons for the sections Mouse and Keyboard (patch `0014`). With `APPDATA` set all 284 tests pass (2 skipped).
  The queue has a host test (patch `0015`).
- The ten ImGui patches were merged into `0013-imgui-windows.patch`. The diagnostic patches moved to `recomp/patches/diagnostics/`.

### Slow frames of 50 to 85 ms (checked, stopped by the user, 2026-11-01)

- About 10 a minute. Three runs (`imgui=1` with the test window, `imgui=1`, `imgui=0`) gave 8.7, 12.2 and 10.4 a minute: not ImGui.
  A sampling profile that kept the slow frames apart showed all threads in an OS wait in them. A log of the frontend's
  vsync ticks and the pace sleeps showed them on time (worst 2.2 ms late, longest gap 18.3 ms). The display runs at 179 Hz.
  The frames come in multiples of 16.7 ms: the game misses a vsync event. Not found: which guest wait. The diagnostics
  (profile, `pace:` line, `hitch_dump` with 40 files) were removed again. Details: `docs/stutter-analysis.md`.
- Mistakes to remember: `Set-Content -Encoding utf8` on the `.ini` in PowerShell 5.1 adds a BOM (use `sed`); a `git apply`
  of a patch can turn a CRLF file into LF (make the diff from copies with the line ends made equal).

### Controller&Keyboard window (done, 2026-11-01, commit 641d1ed)

- Patch `0016-settings-controller-keyboard.patch` (it was `0016` to `0019`, four steps). The menu Settings opens the window
  "Controller&Keyboard". Its pages Mouse and Keyboard come from `g_ini_sections` through `imgui_settings.c` (plain C, so that
  `imgui_ui.cpp` has no runtime header). A change is a request that the main thread applies in `video_poll` with the new
  `options_change` (`ini_check`, the apply of a reload, `ini_set`). The rows `keyboard_map` and `mouse_native_address` are hidden.
- Key bindings: 24 buttons, Bind (a capture of the next key in `video_poll`, by scancode, before the windows and the game),
  None, Default and Reset all. The window leaves the new text of `keyboard_map` for the main thread. `keyboard_map_set` uses
  `keybind_compile`. `g_key_bits[scancode]` keeps the buttons each key's press set. A capture ends on Esc, on a loss of the
  focus and on a key without a name that gives the same scancode back.
- Arrow keys: while the pointer is over a slider or a choice, Left and Right move it by its step (Shift ten steps, Ctrl one
  unit). The window asks for the keyboard with `SetNextFrameWantCaptureKeyboard`.
- New defaults: A F, B Left Shift, X R, Y Space, L3 Left Ctrl, L Q, R E, Plus G, Minus M, arrow keys for the D-pad, W A S D for
  the left stick. `mouse_native_pitch` 500. F1's help text follows.
- Review by the code reviewer agent (GPT-6.1-Sol): 15 findings, 12 fixed (`docs/controller-keyboard-review.md`).
- Two faults found in the user's runs: (1) a combo closed at once, because `bar_wanted` brought the menu bar up for any popup
  and the bar took the focus (`g_bar_shown` was cleared before `bar_wanted`, so "the cursor is on the bar" never held). The
  bar now stays only if it showed in the last frame. (2) The bar showed at start with the cursor outside the window, because
  `SDL_GetMouseState` gives a place without a mouse focus. `imgui_sdl.c` gives a place only with the focus and clears it on
  `SDL_EVENT_WINDOW_MOUSE_LEAVE`.
- Mistake to remember: the patch grew as four numbered patches and had to be merged. Make one patch for one window from the start.

### Controllers page and the button map (done, 2026-11-01, commit c915c66)

- Part of patch `0016`. The page Controllers shows the rows that every player shares (swap A/B, dead zone, vibration,
  background input, Joy-Con pairing). The rows `gamepad_pN_*` and the text row `gamepad_map` are hidden. The button map is a
  choice of the console button for each of the 16 controls. A request (`backend_padmap_request`) swaps the button with the control that
  sent it (as the old menu's press does), the main thread applies it with `options_change("gamepad_map", text)`. The map is kept without
  the A/B swap, which goes over it. A player's own `gamepad_pN_map` is not edited. The user ran it: it works.

### Display window and the alpha fix (done, 2026-11-01 and 2026-11-02, commit e58a53f)

- Patch `0017-display-window.patch`. The window Display (menu Settings) has the tabs Display, Graphics and Performance. Each page has
  its window (`IMGUI_WINDOW_INPUT`, `IMGUI_WINDOW_DISPLAY`), and `settings_window` draws both windows.
- A setting from a short list is a combo box (`PRESETS` in `imgui_settings.c`): resolution 0.5x to 4x (1x is 1920 x 1080: the log of
  resolution 16 had targets of 30720 x 17280) and anisotropy (the game's, 2x to 16x). A value that is not in the list shows as custom.
- A startup setting that a window changed shows the saved value and "Saved. Start the game again to apply it."
  (`options_restart_value`, kept in `options_change`). Before this the window showed the running value, so a change looked lost.
- `vsync` has three values: 0 off (immediate, else mailbox, else FIFO), 1 on (FIFO, default), 2 on (mailbox, else FIFO). Before,
  0 chose mailbox first. The Debug window shows the size the game renders at (the scaled size) and the V-sync mode.
- The HUD ghosting with mailbox and immediate: the game's final target keeps alpha below 1 where its HUD passes blend, the blit copies
  it, and the display used it. The fix draws one quad over the picture with a pipeline that writes only alpha (`g_pipe_alpha` in `ui.c`).
  Found with the game's frame saved by `--shot` (clean) against a Windows screenshot (ghosts). Not the cause: ImGui (`imgui=0`),
  G-Sync, the GPU running ahead (a `vk_finish` each present), a swapchain rebuild with the same mode. See `docs/present-alpha.md`.
- The freeze at the start with `fullscreen=1` was `resolution=16`: `VK_ERROR_OUT_OF_DEVICE_MEMORY` 94 times and 0 frames a second. The
  user's `.ini` was reset to the defaults (a backup was in the scratchpad, which does not last).
- Mistakes to remember: I read the user's "prepare the agent's short replies" as "start the review" and launched agents. Do what
  the user asks and stop. A wait on `tui-idle` can return before the agent writes its report.

### ScarletNX vendored (done, 2026-11-02, commits bb1c55f, 9c4af4f, c45f2c9, 543f001)

- The submodule is gone. `third_party/ScarletNX` is part of the repository, copied from upstream commit 8efbf9c4 (decision of the user:
  no follow of the upstream). The upstream `CLAUDE.md`, `.claude/`, `.github/` and `.mcp.json` are not in the copy. The test
  `claude_ste100` was removed from `tests/CMakeLists.txt` (it checked the upstream Claude files).
- The patches `0001`, `0002`, `0009`, `0010`, `0012` to `0017` are in the source and their files are deleted. Their text is in
  `docs/runtime-changes.md`. `recomp/patches/` keeps `native-mouse-look.pchtxt`, `camera-unclamp.pchtxt` and `diagnostics/`
  (made against the old source, not applied).
- Order that works: copy the work tree away, take the pinned files from `git archive` BEFORE the removal of `.git/modules`, then
  remove the submodule. Mistake of this session: the removal of `.git/modules` first made `git archive` fail. The pinned files came
  back from a clone of upstream.
- The README, CONTRIBUTING, ROADMAP, CREDITS and the build notes say "vendored" now.

### Display window follow-ups (done, 2026-11-02, commits db82425, 7acbd49, dee56dc)

- The rows Showcase, Showcase corner, Stats and Stats corner are not in the window (`HIDDEN` in `imgui_settings.c`). The settings stay
  (F6, F12, old menu).
- Resolution scale is a combo box with the labels "scale (docked size / handheld size)", for example `1x (1920 x 1080 / 1280 x 720)`.
  The handheld size is 2/3 of the docked size. A preset 1.33x (`1.333333`) gives 2560 x 1440 docked. Cause of the first failure: `setting_row`
  tested FLOAT before the presets, so the float `resolution` always got a slider. The presets are tested first now.
- FPS limit is a combo box (Game's pace, 30, 60, 120, 144, 240, Unlimited, Custom) with a number field (10 to 1000) under it for Custom or a
  value that is not a preset. The row is an INT row with the flag `IMGUI_SETTING_CUSTOM` (`fps_limit_row`). A preset has a text value and
  a number (`imgui_settings_preset` gives both).
- A TEXT row with `value=label` words (`jit_opt`) is a combo box (`imgui_settings_text_word`). A TEXT row with a whole number of MB
  (`heap_max_mb`, list `NUMBER_TEXT`) is a number field. Both read the text that the game read (`o->var`) and show the saved value with the
  restart note (`text_row`).

### Resolution against the GPU memory (done as a warning, 2026-11-02, commit c7e97f2)

- Measure from the logs: heap 0 "ours" was 520 MB at 1.33x, 639 MB at 1.5x, 924 MB at 2x. The estimate is 272 MB + 163 MB x scale squared
  (`vk_scale_memory_mb`, `memory.c`; the header is `gpu/vk/scale.h`). A scale fits when the estimate is at most 90 percent of the budget.
- The user chose: warn only, run with the value as set, never change the `.ini`. The log has a `WARNING` line at the start, and the
  Display window adds " - too much GPU memory" to a preset that does not fit (and "custom, too much GPU memory" for another value).
- Test run with `resolution=7` (estimate 8259 MB, budget 7238 MB): the warning came at 0.78 s, 6101 of 6585 MB were in use at 2.06 s, and
  the device was lost at 3.04 s (`FATAL: vulkan: the GPU device was lost`). So a value over the budget still ends the game. The `.ini`
  was put back to `resolution=1.333333`. Not measured: where between 4x and 7x the real limit is (the formula says about 6.2x).
- The user's runs at 1.33x, 1.5x and 2x on 2026-11-02: 30 frames a second, 0 JIT misses, no crash.

### Tests of the static functions (done, 2026-11-02, commit 69cc878)

- Pure parts moved out and tested: `snx_pad_map_assign` (`pad_map.c`, test `pad_map`), `imgui_text_utf16_units` and `imgui_text_fit_units`
  (`imgui_text.c`, test `imgui_text`), `imgui_input_push_text` (`imgui_input.c`, test `imgui_input`), the keyboard map in
  `runtime/platform/backend/keybind.c` (test `keybind`, links SDL3), and `imgui_settings.c` on small tables of its own (test
  `imgui_settings`, with stubs for the runtime). 287 tests passed. The new tests were broken on purpose in five places and all five failed.
- The review finding 12 of `0016` (the tests) is closed. Findings 5 (the first frame after the pointer arrives gives an arrow key to the
  game) and 8 (a design choice) are open.
- The change moved about 200 lines out of `sdl3.c`. The behavior is meant to be the same. It was not run in the game yet at the time of
  this note (key binding, the button map swap, the name input).

### Why the sessions use much of the limit (measured, 2026-11-02)

- A session of 157 model calls read 22 million tokens of context: every call reads the whole history. The context grew from 27k to 280k
  tokens. Calls with more than 100k of context were 85 percent of all the reading. The same work at 40k would be 28 percent.
- The growth comes from long outputs and from the contents of files that the session wrote (tests, scripts), not from the tool results.
  Redo work (a heredoc that lost backslashes, a wrong claim) adds calls at the large size.
- Rule: one task, one session. Start a new session at each stop. Read `WORK_PLAN.md` only at the start of a session, and `OLD-WORK_PLAN.md`
  only when a fact is needed. Keep the answers short.

### Tools and traps moved from the work plan

- Callers of a guest address: `SNX_SCARLETNX=<abs path of scarletnx.exe> python tools/find_callers.py ADDRESS...` from the repository root in
  MSYS2. It scans the text segment only. The path must be absolute (a relative one fails in `CreateProcess`).
- The disassembler: in MSYS2, from `third_party/ScarletNX`, `SNX_SCARLETNX=build/default/scarletnx.exe python tools/disasm.py
  ../../game/work/Arceus START END` (guest addresses in hex). `scarletnx --dump-modules game/work/Arceus DIR` writes the module images.
  `tools/maxwell_label.py` names the words of a shader program and finds a word in the extracted RomFS.
- A diagnostic that needs a run: build it, copy it, tell the user which scene to run, and copy the files that it writes to `game/work/`
  before anything deletes them.
- Screenshots of the frame before the blit: run the program of `scarletnx.exe` with `--shot=<path stem> --shot_every=<presents>` on the
  command line (the `.ini` keys did not take). The files are `.ppm` (1920 x 1080 RGB, no alpha): convert them with PIL (`python -I`).
- Agents (skill `orca-agents`): count only agents with state `working`. The wait returns `timeout` after 10 minutes: run it again. The
  report is a file in the scratchpad: read it, check the claims, copy what you need into `docs/`. `orca agent launch --prompt` splits at
  quotes: put the brief in a file. `orca terminal wait --for tui-idle` can return before the agent starts.
- Traps of the tools: (1) `grep -c` with a carriage return pattern matches every line: use `file` or `od -c`. (2) `sed -i` in Git Bash can
  change the line ends: check with `file`. Many files of the source are CRLF (`sdl3.c`, `backend.h`, `device.c`, `vk.h`, `renderer.c`,
  `ui.c`, `settings.txt`, `imgui_settings.c`, `memory.c`), `imgui_ui.cpp` and `imgui_settings.h` are mixed. Make a replace work for both
  line ends. (3) A Python script inside a Bash heredoc loses backslashes: write the script to a file with the Write tool, use `chr(92)`,
  and run it with a Windows path. (4) A command with a single quote inside a heredoc breaks the Bash tool: write the script to a file.
  (5) Python opens a file as cp932 by default: pass `encoding='utf-8'`. (6) An MSYS2 script goes to `C:\msys64\tmp\x.sh` and runs with
  `MSYSTEM=CLANG64 CHERE_INVOKING=1 /c/msys64/usr/bin/bash.exe -l /tmp/x.sh`. (7) The ctest run next to a build can fail one time: run it
  again before you look for a cause. (8) An estimate from a time between two dumps can be wrong: measure the thing itself.
- Unexplained from the alpha fix: why the driver or Windows used the alpha with mailbox and immediate and not with FIFO. The fix does not
  depend on it.
- The slow frames of 50 to 85 ms (stopped): if it is opened again, use `diagnostics/0004-hitch-dump.patch` (and `0006`) with `hitch_dump=40`,
  with the limit of files raised from 6, to see which guest wait misses a vsync. These patches were made for the old source.

## Jaggies at resolution over 1 (ScarletNX against Eden), stopped by the user on 2026-11-02

ctest pass (287). The runtime is copied over the three programs. Ask the user how to split the commits.

Facts (read from screenshots and logs):
- At 1x, ScarletNX and Eden look the same. The difference shows only at resolution 2x. Eden has soft grass edges even with AA None. ScarletNX has
  1 pixel steps on grass edges, with FXAA on or off. FXAA helps on the clothes, the hair and the leaves, not on the grass.
- The frame shown is 3840x2160 at 2x (log line `the frame shown is`). No render target falls back to 1x for the main picture. Only four 1024x1024
  targets (2048x2048 image, format f6) are copied down to 1024x1024 with NEAREST for a texture each frame (`texture.c` `copy_layers`).
- The game uses alpha to coverage (log: `register 0x54f = 1`). Eden sets `alphaToCoverageEnable` from it (`vk_graphics_pipeline.cpp`, only when
  `supports_alpha_output`). ScarletNX did not, now it does. The first screenshot with it on still shows steps on the grass. The user's `anti_aliasing`
  value in that screenshot is not known.
- Eden reference code: GitHub `luneflu/eden-lun` (a fork of Eden, GPL-3.0). Read it with `gh api`. Do not port GPL-2.0-only code.
- Compared and equal to Eden: the sampler bits, the mip range, the Catmull-Rom bicubic, the order AA then scaling then windows.
- The dynamic resolution of the game: the sizes do not change in the run (1920x1080 and 960x540 only). Not proved in handheld mode.

Next steps:
1. Ask the user for `anti_aliasing` of the last screenshot, and for a new screenshot at 2x with AA Off.
2. Check that the fragment shader of the grass writes the alpha of color output 0 (Eden: `supports_alpha_output`). If not, alpha to coverage has no effect.
3. Compare the alpha test and the alpha output format with Eden. Check the grass draw (blend state, depth write, the alpha cut value in the shader).
4. Optional: draw the 4 shadow targets at the scaled size (no NEAREST copy down).

Result of the last runs:
- The game sets register 0x54f = 1 (alpha to coverage) and the MSAA mode 0x574 = 0. The targets have 1 sample, as in Eden, so alpha to
  coverage does not soften the grass in either program.
- Eden enables alpha to coverage when the fragment shader has a color 0 output. ScarletNX does when a fragment shader exists. Both do the
  alpha test as a discard in the shader.
- No difference was found in the draw. Not checked: the window size and the scaling filter of the Eden screenshots. If the Eden window is
  smaller than the frame, the downscale averages 2x2 pixels and hides the steps. The Area filter would show whether this is the cause.

## SMAA (done 2026-11-02, not committed at the end of the session)

- Setting `anti_aliasing`: 0 Off, 1 FXAA, 2 SMAA. Row 0020 in `docs/runtime-changes.md`, credits in `docs/CREDITS.md`.
- `shaders/smaa.frag` is a GLSL port of SMAA 1x of SMAA.hlsl (iryoku/smaa, MIT), preset HIGH (threshold 0.1, 16 steps, 8 diagonal steps,
  corner rounding 25), luma edges, no predication and no temporal parts. One file, three builds by the macros `SMAA_PASS_EDGES`,
  `SMAA_PASS_WEIGHTS` and `SMAA_PASS_BLEND`. The edge pass takes luma in gamma space (pow 1/2.2) when the frame decodes sRGB.
- The tables are `AreaTexDX9.dds` (160x560, RG8) and `SearchTex.dds` (64x16, R8) of the SMAA repository, each after 128 bytes of header.
  `cmake/smaa_tables.cmake` embeds them. They go to the GPU the first time SMAA runs (`smaa_tables_ready`). A failure is final for the run.
- `postfx.c`: images are `Target` (edges RG8, weights RGBA8, output). `fullscreen_pass` draws one pass. The edge pass clears its target
  (it discards pixels without an edge). The point sampler is for the edge pass, the linear one for the others.
- Test by the user (2x, grass scene): no problem seen. Log: `AA 2`, 30.0 frames a second, no pipeline or table failure, 0 JIT misses.
- The Debug window row "Anti-aliasing" shows the setting (Off, FXAA, SMAA).
- Build trap met in this task: see "How to work" in `WORK_PLAN.md` (ctest in Git Bash destroys the CMake cache).



## Open points of the jaggies work (moved from WORK_PLAN on 2026-11-02)



## NVN survey, stage 1 of phase 6 (done 2026-11-02, commit 526d0e0)

- Files: `docs/nvn-survey.md`, `docs/nvn-calls.md`, `docs/nvn-plan.md`. Runtime change 0021 (`nvn_trace=1` logs the first 40 GPFIFO
  submissions as `gpu-submit:` lines).
- Run of 302 s (boot, title, village, wild area, battle, menus): 522 addresses resolved, 242 functions called, 8919 frames, 30 frames
  a second, 0 JIT misses. A frame is acquire, submit 77 command buffers, fence, present. The submit sends 0 GPFIFO entries. The present
  sends one submission of 168 to 172 entries.
- No direct caller of the hot NVN functions in the game (`tools/find_callers.py`). The slots of the pointers are filled by a function in
  `main` (aa93e30 and 877e440). The call sites lie between a9b82e0 and ac8714c.
- Lower bounds of the object sizes from the `Initialize` functions are in the survey (table).
- Cleanup of the game folder (the user agreed): `game/work/Arceus/romfs`, `game/backup`, the hitch and profile files and old logs were
  deleted. About 1.9 GB are left in `game/`.

## NVN design, stage 2 steps 2-1 to 2-5 (done 2026-11-02, text in `docs/nvn-design.md`, not committed at the end of the session)

Moved from WORK_PLAN on 2026-11-02. The decisions of the user: way A (replace NVN completely), choice 1 (the host handlers write
the engine methods on a private channel and call the existing `Renderer`), choice 2 (a native draw interface) is a candidate for a
later clean-up and is judged after stage 4. `.gitattributes` makes GitHub count ScarletNX in the language statistics (committed in
7d6d383).

Facts found (details and addresses are in `docs/nvn-design.md`):

- 2-1: the game wraps NVN in one layer of small functions (about 440 call sites, aa81d54 to aac7de4, grouped by object type, the
  shape of `nn::gfx`). The wrapper allocates the NVN object itself and passes the pointer. In the `Initialize` wrappers read, the game
  does not load or store a field of the object. Buffer 0x30 bytes, texture 0xC0, command buffer inside a 0x158 byte game object (NVN
  part at most 0xE0), sampler likely 0x60. Memory pool, window, queue and sync sizes not found. Design: a side table keyed by address.
- 2-2: `nvnDeviceGetTextureHandle` has one call site (aaa1fcc). Its result goes at once to `nvnCommandBufferBindTexture`. The
  buffer address from `nvnBufferGetAddress` is stored in the wrapper (+0x40) and passed back to `BindUniformBuffer` and
  `BindStorageBuffer`. `nvnMemoryPoolMap` and `nvnMemoryPoolGetBufferAddress` have no call site in `main` (callers not found).
- 2-3: the host layer replaces `g_disp` slots as `nvn_trace.c` does. The 522 resolved addresses are all different (no folded
  functions). The handle format is `sign_extend(texture | sampler << 20) | 1 << 32` (d82d0d0). The resolver stubs (d82d090 and
  d82d110) are trampolines (`adrp`, `ldr`, `ldr`, `br`) to pointers that are filled at load. The survey row for the device
  `Initialize` (d82d110, +0x19d8) is wrong: +0x19d8 belongs to the getter at d82d100.
- 2-4: the `Renderer` interface is Maxwell register based (`draw(ch, ...)` reads `ch->r3d[]` and `ch->cbind`). The rules for the
  register path (side effects, notifications, lifetime, synchronization, taps for TAA) are in the section "2-4 Decision".
  `0xB2` in the library entries is `SYNC_INFO` (a syncpoint request).
- 2-5: record host calls (not registers) in lists, replay on the GPU thread with `gpu_call`, one job a frame, fences as tokens,
  `present_buffer` with the texture storage address (no new renderer call). The state objects are copied at record time.

Tools and traps of this work:

- The scripts are in the scratch folder `C:\msys64\tmp\snxhome` only (never in git): `slots.py` (the 523 slots of the loader at
  aa93e30; `python -I slots.py aa80000 aaa2000 out.json`), `sites.py` (the `adrp` + `ldr` sites of the slots in `main`, writes
  `sites.json`), `find2.py` (sites of given slot addresses with a window of 600 words), `byfunc.py`, `allocs.py` (allocation
  sizes before an `Initialize` site), `dis.py` (disassembly of a range of `main`; for the `sdk` module set `dis.m` and `dis.BASE`).
  The module dump they read is the cache `C:\msys64\tmp\snxhome\~\.cache\scarletnx\module-dumps\755dec237db4c3df` (main base 8005000,
  sdk d3c7000, subsdk0 c357000, subsdk1 c9fe000). `tools/disasm.py` could not make a new dump from Git Bash; use the cache.
- `main` has no symbols. The slot table ends near page c2f6000: the names of the last slots (the Event functions) are not reliable
  in the scan (14309 false hits).
- A Python script in a Bash heredoc breaks when the text has a single quote: write the text to a file with the Write tool and
  read it from the script.
- Docs in `docs/` and `.claude/OLD-WORK_PLAN.md` are CRLF. A script that appends must keep the line ends.

## Earlier state notes (moved from WORK_PLAN on 2026-11-02)

- The runtime built from the working tree (with the `gpu-submit:` trace, 0021) is copied over the three program copies.
- The game's `.ini` has `nvn_trace=1` (the game runs slower with it) and `mouse_native_address=160708992`. The user may have changed
  the graphics settings after the last run and said that they need not be set back. The recompiled game has the mod `native-mouse-look`.
- Last runs read (2026-11-02): 302 s with `nvn_trace=1`, 30 frames a second, 0 JIT misses, no crash. A second short run (to the
  title) gave the `gpu-submit:` lines.
- `tools/find_callers.py` and `tools/disasm.py` write a cache in a folder named `~` in the current directory (it holds game code,
  so it must never reach git). Run them from a scratch folder with absolute paths.

## NVN stage 2 decisions, step 3a and part 3b-1 (done 2026-11-02, moved from WORK_PLAN on 2026-11-02)

- Decided by the user (2026-11-02): way A, choice 1 (reuse `Renderer` and the engine state update code), the old path as a
  comparison target selected for each launch, and all 523 resolved functions as the goal (the 242 are not the limit; plan in
  `docs/nvn-design.md` "Unobserved functions").
- The user adopted `nvn_selftest`: comparison of return values, memory writes, state at draw, clear and compute, and the order of
  sync operations. It has its own objects and memory. The fence and visibility checks are in a separate `nvn_exectest`. It starts
  in stage 3 with a few functions and a mutation check. `selftest`, `exec` and `seen` are recorded apart.
- The user accepted step 2-6 (`docs/nvn-design.md` section 2-6: groups G1 to G10, files, steps 3a to 3d, test plan). Stage 2 is
  finished. Steps 2-1 to 2-6 are in `docs/nvn-design.md`.
- The Orca "code reviewer" (skill `orca-agents`) reviews the code after 3b and 3c (and after 3d if it is large); see
  `docs/nvn-plan.md` stage 3. For 3b it reviews 3b-1 to 3b-7 together, after 3b-7.
- Facts to keep: object memory is not used by the host (a side table keyed by address); the texture handle is `texture | sampler
  << 20 | 1 << 32`; the rules for side effects, notifications, lifetime and the taps for TAA are in section 2-4. Choice 2 (a
  native draw interface) is a candidate for a later clean-up, judged after stage 4.
- Step 3a done: the `gpu-submit:` and `nvn-size:` diagnostics (runtime change 0022) and the run of the user. The result is in
  `docs/nvn-design.md`, section "3a Result". The shadow RAM assumption of 2-5 (b) was wrong (the library moves the mode 2, 3, 1 in
  the tail of each frame) and is corrected there. The log of that run is replaced by later runs.
- Part 3b-1 written and built: the setting `nvn_native`, `gpu/nvn_native.c` (loader hooks below the trace hooks, the
  missing-function handler, the exit report), `gpu/nvn_objtab.c` (test `nvn_objtab`), and the `nvn-value:` trace (runtime changes
  0023 and 0024). The full `ctest` run: all pass except `tracked_material`, which fails on the two SMAA `.dds` files of an
  earlier commit (not caused by 3b-1; not fixed). Not run in the game with `nvn_native=1`.
- Staging done for the user: the stage 2 text (`docs/nvn-design.md` without the sections "3a Result" and "3b Progress", and
  `docs/nvn-plan.md` as it was then) was added to the index for a commit. The user commits.

## NVN stage 3, parts 3b-2 and 3b-3 (2026-11-02, moved from WORK_PLAN on 2026-11-02)

Done:

- 3b-2: one run to the title (`nvn_trace=1`, `nvn_native=0`). The values are in `docs/nvn-design.md`, "3b-2 Result". Committed (c088362).
- 3b-3: the host handlers of G1 (device builder, device, queue builder, queue: 30 functions in `native_procs` of `gpu/nvn_native.c`)
  and the pure parts in `gpu/nvn_params.c` and `gpu/nvn_params.h` (the `GetInteger` table, the timestamp ratio 625 / 384, the
  queue memory size). Test `nvn_params` passes. `ctest` has one failure, `tracked_material` (two SMAA `.dds` files, not caused by
  this work). Runtime changes 0025 to 0027 are in `docs/runtime-changes.md`.
- Facts from the library (update 1.1.1, `sdk` module): `nvnDeviceGetTimestampInNanoseconds` is at d82d0e0 and uses the device
  words +0x58 = 0x271 and +0x5c = 0x180. `nvnQueueBuilderGetQueueMemorySize` is at d8383f0. It reads six device words (+0x8c = 0x80,
  +0x94 = 2, +0x98 = 1, +0xe0 = 1, +0xe4 = 0x1000, +0x1af4 = 0). The port gives 0x8a000 for the game's builder, the same as the
  library. `nvnDeviceGetInteger` is at d82d060 and calls the switch d831f40.
- The game wrapper that sets up the device is at aa912a8 (`main`): `SetDefaults`, `SetFlags`, then `DeviceInitialize`. The
  function slots of the game are in the page c2f5000 (and c2f6000).

Tools and traps learned:

- `tools/disasm.py` does not find NVN functions by name (the symbols are not those names). Take the address from the log line
  `nvn: <name> -> <address>` and give an address range.
- In a Python heredoc inside Bash, `\n` inside a C string literal becomes a real line break. Use the Edit tool for such a change.
- A build that fails can still end the pipe with success (`grep | head`). Check that the build printed `Linking CXX executable
  scarletnx.exe` before you copy the program, and check the time of the file.
- Check the program with `tasklist` before the copy to the three places.

State at the end of the session: the first run with `nvn_native=1` hangs at the first call of `nvnDeviceBuilderSetDefaults`
(see "3b-3 First run" in `docs/nvn-design.md`). The diagnostic of change 0027 is built and copied. The user has not yet run it.
## NVN stage 3, part 3b-3 finished (2026-11-02, moved from WORK_PLAN on 2026-11-02)

- Cause of the hang of the first run with `nvn_native=1`: the host handlers did not set `c->pc`. A guest function ends with RET, which
  sets `c->pc` to x30, and the generated code of the caller compares `c->pc` with its return address (`check_return` in
  `llvm/ir_codegen.c`). The caller ran its last block again, so `nvnDeviceBuilderSetDefaults` was called more than 10^9 times. The
  hypothesis about the trace wrapper was wrong. Fix: `native_call` in `gpu/nvn_native.c` (runtime change 0028). It saves x30 and
  sets `c->pc` after each handler, including the missing-function handler. A handler must not change x30.
- Second run (17:14, update program, `nvn_native=1`): the loop is gone. The game called the 30 handlers, then 37 functions without a
  handler (memory pool builder, `MemoryPoolInitialize`, texture and sampler pools, sampler builder, `SamplerInitialize`, texture
  builder, `TextureBuilderGetStorageAlignment`, `GetStorageSize`, `TextureInitialize`). All returned 0. The game then crashed in
  aa926ec (`main`): a null pointer plus 0x30, the result of the pool allocator aa969bc. It follows from the missing handlers.
  Values of the first calls: `MemoryPoolBuilderSetStorage` x1=9270eb000 x2=12c00000, `TexturePoolInitialize` x2=12bc0000 x3=2000,
  `SamplerPoolInitialize` x2=12bbc000 x3=200.
- The text is in `docs/nvn-design.md` ("3b-3 First run", "3b-3 Second run") and `docs/runtime-changes.md` (row 0028).
- The commit message of 3b-3 was given to the user. The user commits.
- Setup of the session: `.claude/settings.json` (not in git) denies the unused skills and the tool `Agent` (Claude Code subagents are
  not used). `orca-native-chat:orca-chat-visuals` is allowed. `.claude/skills/orca-agents/SKILL.md` has four agents now: code
  reviewer (high), game explorer (medium), library analyst (high) and handler writer (medium), all `gpt-6.1-sol`. The two new
  agents are not run yet, so the briefs may need a change after the first use.

## NVN stage 3, part 3b-4 finished (2026-11-02)

- Done: 13 host handlers for the memory pool and descriptor pool functions (`gpu/nvn_native.c`, pure parts in `gpu/nvn_params.c`,
  test `nvn_params`, runtime change 0029). The library analyst gave the table (13 functions, sdk addresses d833b80 to d83d930).
  I checked five rows against the disassembly (SetDefaults, SetStorage, GetFlags, GetBufferAddress, TexturePoolInitialize): all
  matched. The handler writer gave the patch. `ctest`: 288 of 289 pass (`tracked_material` fails on the SMAA files, as before).
- Decisions: virtual pools (builder flag bit 9, only the `nn::gfx` path at d85ecc4 with flags 0x221 uses them, then
  `MemoryPoolMapVirtual`, which the game never called) and imports (bit 29) return 0 with a log line. `SamplerPoolInitialize` only
  stores the link (the encoder d858210 is a pure function, about 0x320 bytes, and the library writes two 32 byte descriptors at
  the pool offset). The pool GPU address is a host bump allocator from 0x400000000, page aligned, no reuse. The GPU page table
  mapping is not connected.
- Library facts: the pool flags N = (F and 0xffffffc0) or C or G (C: bit 1 gives 2, else bit 2 gives 4, else bit 0 gives 1, else
  2; bit 9 forces 1. G: bit 5 gives 32, else bit 4 gives 16, else bit 3 gives 8, else 32; bit 8 forces 8). `GetFlags` returns N
  and 0x01ffffff. `Map` returns 0 when flag bit 0 is set, else the storage pointer (+0x70). `GetBufferAddress` returns +0x40. A
  texture or sampler pool object is (memory pool, offset, count) at +0x00, +0x08, +0x10, and `GetSize` returns the count.
- Run of 19:52 (update program, `nvn_native=1`): the 13 handlers ran, no failure line. Next missing: the sampler builder and
  `SamplerInitialize`, `SamplerPoolRegisterSampler`, the texture builder, `TextureInitialize`. Crash in aa926ec (null plus 0x30)
  after `GetStorageSize` returned 0.
- Setup lessons: the writer brief needs the decisions of the caller listed by number, and the writer cannot build (`snx_settings.h`
  is generated), so the caller builds. The commit message of 3b-4 was given to the user.

## NVN stage 3, part 3b-5 finished (2026-11-02)

- 3b-5 (row 0030): 29 handlers for the sampler builder, `SamplerInitialize`, `SamplerPoolRegisterSampler`, the texture builder, the
  storage size and alignment functions and `TextureInitialize`. Pure code in `gpu/nvn_params.c` (sampler encoder, LOD snap formula
  `T[i]=(8-d)/(2*d)`, `d=(8+i%8)*2^(i/8)`, texture layout), test `nvn_params`. 40 of 40 `nvn-size:` queries of the archived log
  `logs/2026-11-02_16-50-49.log` agree (`snx-test-nvn-params.exe <log>`). The caller changed one thing in the patch: the wrap enum
  is masked with 7. Analyst table: scratchpad `reports/analyst-3b5-1.md`.
- Run of 2026-11-02 20:44 (update program, `nvn_native=1`): all 29 handlers ran without a failure line. The texture 1920x1080,
  format 0x25, flags 9 has size 0x870000. The game then crashed at a9f5198 (null store) because `nvnBufferMap` returned 0 (no buffer
  handlers). Missing functions in the log (32): window and sync (8), buffer (7), program (2), and 15 texture functions
  (`TextureBuilderSet{Width,Height,Depth,PackagedTextureLayout}`, `TextureViewSet*`, `TextureGet{Width,Height,Stride}`,
  `TexturePoolRegisterTexture`).
- Device words of 3b-5 were provisional (0x1a80, 0x1a84 = 0). The explorer found the real values for 3b-5b (see "Next").
- Lessons: the writer reports can be missing from the terminal (read the patch and the files instead). Wait for the report file,
  not for `tui-idle`. One request with several steps took more than one hour: the user wants one step for each request.

## NVN stage 3, part 3b-5b finished (2026-11-02, commit 1793594, pushed)

- 3b-5b (row 0031): 15 handlers: `TextureBuilderSet{Width,Height,Depth,PackagedTextureLayout}`, 7 `TextureViewSet*`,
  `TextureGet{Width,Height,Stride}` and `TexturePoolRegisterTexture`. Pure code in `gpu/nvn_params.c` (`nvn_texture_descriptor`,
  `nvn_texture_packaged_tile`), tests in `nvn_params`. The format record comes from the guest pointer at `0xe03fd40` (48 bytes each,
  seed at byte 0x24). Device words now verified: +0x08=0x4000, 0x1a48=2, 0x1a4c=1, 0x1a50=0, 0x1a54=0xffffffff, 0x1a80=0, 0x1a84=0x111,
  0x1a88=2. Only +0x1b1c=1 stays provisional (the 40 archived size queries need it, device init clears it).
- Check of the encoder against d858c70 and d8593f0: 8 rule groups agree (sRGB bit, depth/stencil bases, quality codes, tile and
  device-row fields, mips, address, dimensions, registration bits). One difference: the real code does not check the row index
  (+0x1a88), the patch clamps it to 5 (the real value is 2). NOT checked: the bytes of the jump tables (de87944, de87948, de87950) and
  the data tables (de879e8 target codes, de87970 device rows). The sector map 0>0, 1>2, 2>1, 3>3 comes from the analyst report.
- Build and ctest: 288 of 289 pass (`tracked_material` fails, known). Run of 2026-11-02 21:31 (update program): no
  `texture registration` error line, 7 texture initializations, then the same crash as after 3b-5 (`nvnBufferMap` returns 0, crash in
  a9f5198). The game did not call `TexturePoolRegisterTexture` before the crash, so no real descriptor was written. The encoder is
  proved only by the unit tests and the disassembly check until 3c.
- Missing functions in the log now (17): window and sync (8), buffer (7), program (2).
- Setup changes (2026-11-03): game explorer, library analyst and handler writer use `gpt-6-luna` (high, high, medium). The code reviewer
  stays `gpt-6.1-sol` high. The `Co-Authored-By` line follows the model (rule in `CLAUDE.md` and in the skill `orca-agents`).
- The worktree `hw-3b5b` is removed.

## NVN stage 3, parts 3b-6a and 3b-6b finished (2026-11-03, commits f702755 and 23ab714)

- Decision of the user (2026-11-03): the window and the sync object belong to the host. No compatibility words go into the game's
  window, builder or sync memory. A part that is not known stops with a log line. No success is faked and no sync is completed
  without a cause. (The first plan kept `WindowInitialize` and `WindowAcquireTexture` in the library. It does not work: the library
  would read a builder that the host never wrote.)
- 3b-6a (row 0032, f702755): 9 handlers: `WindowBuilder{SetDefaults,SetDevice,SetNativeWindow,SetTextures,SetPresentInterval}`,
  `WindowInitialize`, `WindowSetPresentInterval`, `WindowSetCrop`, `SyncInitialize`. Records `NVN_OBJ_WINDOW_BUILDER`, `NVN_OBJ_WINDOW`,
  `NVN_OBJ_SYNC`. `WindowInitialize` reads the texture array at call time and needs 1 to 4 known texture records, else one log line
  "window initialize failed: <reason>" and result 0. The native window pointer is stored and never used (it points to the 0x3c byte
  vi parcel of `vi.c:vi_native_window`). The builder interval is stored as given (the library stores it minus 1 and adds 1 later).
- 3b-6b (row 0033, 23ab714): `WindowAcquireTexture` and `SyncWait`. Image states `NVN_IMAGE_FREE` and `NVN_IMAGE_ACQUIRED` in the window
  record. `Acquire` calls `present_pace(interval)` outside `objs_lock`, takes the next FREE image after `last_index`, signals the sync
  (a FREE image means the frame job before is finished), writes the index, returns 0. No FREE image: one log line "no free window
  image", `*x2 = -1`, result 1. This is the intended stop: only the frame job of 3c (`QueuePresentTexture`) will set an image back
  to FREE. `SyncWait` returns 0 for a signaled sync and 3 for a sync never fenced. The timeout is ignored until 3c adds waiting states.
- `SyncWait` facts (library `d83bc00` to `d83be68`): results 0 already signaled, 1 satisfied after a wait, 2 timeout, 3 failed; timeout -1
  is no limit. The type word at sync+8 selects the path by the byte table at `de860b3` = 81 16 00 08 10 56: type 0 gives 3, type 1 gives
  0, types 2 to 4 compare a counter, type 5 waits for a fence handle list. `d9510a0` is only a GOT veneer for the native window wrapper.
- Corrections to the analyst table (3b-6): `WindowSetPresentInterval` stores only when the wrapper callback exists (the host always
  stores). The calls to `+0x50` and `d94ec20` are in `d83e794`, not in `WindowInitialize`. Writer errors found in the review: a double
  interval offset (default 2 instead of 1) and a null texture in `SetCrop`. Both fixed before the commit.
- Runs (update program, `nvn_native=1`): 3b-6a at 12:28 had no failure line and the first missing function was `WindowAcquireTexture`.
  3b-6b at 13:12 had no failure line and `Acquire` was not missing any more. Both runs ended in the known crash (`nvnBufferMap` returns 0,
  a9f5198). Not visible: the number of `Acquire` calls, the index, the `present_pace` wait, and any `SyncWait` call (the success path
  logs nothing and the trace summary does not print after a crash). Build and ctest: 288 of 289 (`tracked_material` fails, known).
- Open: the game's vi layer still exists for its own vi calls. That no frame is paced twice is not checked (the host `Acquire` paces, the
  vi dequeue paces only for queued layers). The result 1 as failure comes from the analyst table, not from the NVN header.
- Method notes: `tui-idle` returns early. Wait until the patch file is stable for 45 seconds and the agent is idle. The writer cannot
  build `nvn_native.c` (generated headers): read its patch twice. A filtered `ctest` in Git Bash that runs `runtime_architecture` left the
  CMake cache intact this time (checked), but use the MSYS2 shell.
- The worktrees `hw-3b6` and `hw-3b6b` are removed. The reports are in the scratchpad: `reports/analyst-3b6-1.md`, `explorer-3b6-1.md`,
  `brief-*-3b6*.md`.

## NVN stage 3, part 3b-7 finished (2026-11-03, commit fd2bea5)

- Done: the selftest harness and its first cases of the kinds R and M (row 0034 of `docs/runtime-changes.md`, design table of
  `docs/nvn-design.md` row 3b-7). The new setting is `nvn_selftest`. It needs `nvn_trace=1` and `nvn_native=0`.
- Files (all in `third_party/ScarletNX`): `src/nx/gpu/nvn_selftest_cmp.c` (the comparer: R, M, S and Y, the first difference of each
  kind, an ignore mask for M, a full table is a fail), `nvn_selftest_mem.c` (the arena over one address range), `nvn_selftest_rec.c`
  (the recording `Renderer` `nvn_st_renderer`, with a snapshot hook that 3c fills), `nvn_selftest.c` (the cases and the run).
  Host tests: `nvn_selftest_cmp`, `nvn_selftest_mem`, `nvn_selftest_rec`.
- How a run works: `nvn_trace.c` calls `nvn_selftest_note_device` before the original `nvnDeviceInitialize` and `nvn_selftest_run`
  before the original `nvnQueueInitialize`, on that guest thread. The library function comes from `nvn_trace_library(name)` (the
  `original` of the trace slot). The host function comes from `nvn_native_handler(name)`. The run calls both with the same x0 to x4 and
  restores the whole `CPU` after each call. The objects are in a 64 KiB block from `kmem_alloc` (state `MS_STATIC`), never freed.
- Run at 13:35 (update program, `nvn_native=0`, `nvn_trace=1`, `nvn_selftest=1`, title screen): 51 cases, 0 failed, 0 skipped. Cases:
  `nvnDeviceGetInteger` for the 27 values of `nvn_params.c` (M, the written word), and the texture builder storage size, alignment,
  width and height for 24 sets (flags 0 and 9, formats 0x25 and 0x26, three sizes, levels 1 and 4; R). No crash, 30 fps. The 40 archived
  size queries and these 24 sets agree with the provisional `NVN_TEXTURE_DEVICE_COMPRESSION_ENABLE`.
- Limits: the log shows no values, only ok or FAIL, so a pass of two equal zeros is not excluded (the `nvn-size` line of the same run
  shows an alignment of 0x10000, so that call is not zero). The selftest did not yet show that it sees a wrong handler on a live run:
  the mutation setting `nvn_selftest_mutate` is part of 3c. The recorder takes no lock (one private channel). The values of
  `DeviceGetInteger` outside the 27 are not tested. The cases of `BufferGetAddress`, `VertexAttribState*` and `BlendState*` of the
  design table need functions that the host does not have yet (3c and 3d).
- State of the program copies after the run: the three copies hold the build with 0022 to 0034. The game `.ini` has `nvn_trace=1`,
  `nvn_native=0` and `nvn_selftest=1` now. Set `nvn_native=1` and `nvn_selftest=0` again for a run of the native path.
- Traps found: a long Bash command with several heredocs failed with "unexpected EOF" (nothing ran): write the files with the Write tool
  and run a script. The tool does not set the working directory after a `cd` in a script: use absolute paths.

## NVN stage 3, review of 3b-1 to 3b-7 (2026-11-03, commits eb7f721 and e0567f8, not pushed)

The Orca code reviewer (Sol) made 17 findings. The report was `reports/review-nvn-3b.md` in the scratchpad. The commits fix
findings 1 to 14. Findings 15 to 17 (P3) are open (see WORK_PLAN). Rows 0035 and 0036 of `docs/runtime-changes.md` list the changes.

- P1 (eb7f721). Finding 1: with `nvn_native=1` and `nvn_trace=1` the two resolver hooks of `nvnDeviceGetProcAddress` called each
  other after the second bootstrap lookup. Each hook now installs once (it tests its own `getproc_original`). It has no host test:
  only a game run shows it. Finding 2: the descriptor registration had no range check. New pure function `nvn_descriptor_offset`
  and the helper `native_guest_writable` (`native_guest_access` with a permission).
- P2 (e0567f8). Fixed: failed `MemoryPoolInitialize` and `TextureInitialize` remove their record. A texture must fit its memory
  pool (`nvn_pool_range_fits`). Texture and buffer sizes use 64-bit math. A sampler wrap above 7 and a texture swizzle above 5 fail at
  registration with a log line (`nvn_sampler_descriptor` returns int, new result `NVN_TEXTURE_DESCRIPTOR_BAD_SWIZZLE`).
  `nvnDeviceGetInteger` answers 0 only for `what` above `NVN_DEVICE_INTEGER_LIMIT` (0x62, the default of the library switch at
  d831f68), and writes nothing for an unknown `what` below it. `WindowAcquireTexture` checks write permission of the index pointer.
  An unsignaled `SyncWait` logs. The trace keys use the result for the object-only queries (`VALUE_RESULT`). The recorder keeps the
  QMD fields. The selftest checks guard bytes around the `DeviceGetInteger` output word.
- Two findings were wrong, and the reason is a fact. Finding 3 (linear pitch): the library function d858c70, emulated on 1800 random
  cases (`analysis/check_descriptor.py`, `analysis/emulate.py`), also clears word 3 bits 10 to 12 for linear textures. The host copies
  this on purpose (comment in `nvn_params.c`). Finding 7 (multisample array): the behavior was right, the names were wrong. The
  library numbers are 5 multisample, 6 multisample array, 7 rectangle. `nvn_params.h` now uses them.
- The library maps a swizzle above 5 to component 0 and the host now refuses it. The host result differs from the library for this
  unsupported input, by decision (rule of `docs/nvn-design.md`).
- The compression switch (device +0x1b1c, `NVN_TEXTURE_DEVICE_COMPRESSION_ENABLE`) stays 1 and unverified. The 40 archived size
  queries need it non-zero. The only store found for the range is `str wzr, [x19, #0x1b18]` at d831c0c (4 bytes, does not reach
  +0x1b1c), so the old claim "device initialization clears it" was wrong. The selftest now logs `selftest: device texture words
  +1b18=... +1b1c=...` at its start. A run with `nvn_selftest=1` and `nvn_native=0` shows the real byte of the game device.
- State after the commits: `ctest` 291 of 292 pass (`tracked_material` fails, known). The program copies still hold the build with
  0022 to 0034. They do not have 0035 and 0036. The game was not run for these commits.

## NVN stage 3, P3 findings of the 3b review and the preparation of 3c (2026-11-03, commits 7d7532e and 4f25979)

- P3 findings 15 to 17 fixed (7d7532e): the tile thresholds of `texture_choose_tile` are named constants
  (`NVN_TEXTURE_AUTO_TILE_DEPTH_*` and `_ROWS_*` in `nvn_params.h`), the first comment of `nvn_native.c` has 4 lines (the text is in
  `docs/nvn-design.md` 2-3), and the 3b row of the selftest table lists the real cases. Build ok, `ctest` ok except `tracked_material`.
- Selftest run of 2026-11-03 16:54 (update program, `nvn_native=0`, `nvn_trace=1`, `nvn_selftest=1`, title scene, about 41 s, 30 fps,
  no crash): 51 cases, 0 failed, 0 skipped (27 `DeviceGetInteger` M cases, 24 texture builder R cases), arena 560 of 65536 bytes.
  The game device has `+1b18=0` and `+1b1c=1`. `NVN_TEXTURE_DEVICE_COMPRESSION_ENABLE` stays 1 (4f25979). The store of the byte is
  not found: the value comes from one run. 522 resolved addresses, 231 called.
- Agents: the game explorer, the library analyst and the handler writer now run as `opencode` with `opencode-go/deepseek-v4.1-flash`.
  Orca refuses `--effort low`, `high` and `max` for this model ("does not support effort"), so no effort is passed. The default
  variant of the model is `high` (the screen shows it). Co-Authored-By line: `OpenCode <noreply@opencode.ai>`. The code reviewer is
  still `codex` / `gpt-6.1-sol` / `high`. `CLAUDE.md` section "Git" still names Luna for the three agents: it is out of date.
  A long brief goes in a file and the prompt says "Read the file <path> and do the task in it".
- Reports of 3c (scratchpad `reports/`): `analyst-3c-A-1.md` (Buffer, BufferBuilder, Program), `analyst-3c-B-1.md` (CommandBuffer,
  Queue), `analyst-3c-C-1.md` (pool, render target, viewport, scissor, clear, barrier setters), `explorer-3c-1.md` (the library
  entries of a submission), with the briefs `brief-analyst-3c-A/B/C.md` and `brief-explorer-3c.md`.
  Checked against the disassembly: BufferInitialize, BufferMap (pool method d83f56c), BufferGetAddress, BufferBuilderGetSize,
  CommandBufferInitialize, the QueueFenceSync thunk (a branch to d83bb20), EndRecording (start), SetTexturePool. Not checked:
  ProgramSetShadersExt, the tables of the explorer, the algorithm sections.
- Facts from the reports: `BufferMap` is pool CPU pointer (pool+0x70) plus offset, 0 only when pool flag 0x1 (CPU no access) is set.
  The library command buffer is a record list. `SetTexturePool` and `SetSamplerPool` store a 16 byte control record (ops 0x13 and 0x14)
  and the interpreter d839d20 expands them at submission. The other setters write methods at call time. The `EndRecording` handle is a
  control memory address with a 16 bit tag. `QueueSubmitCommands` makes no GPFIFO entry. `QueuePresentTexture` flushes the queue
  (d83915c) and builds the entries. The explorer decoded the four entries (caches, empty, pools, fence and window image). `ClearColor`
  runs macro 1 (0xE02) with `(target << 6) | (mask << 2)`. The macro body is not read. The format word of a render target comes from
  the table at `[0xe03fd40] + format*0x30 + 0x1c`. `nvnDeviceFinalizeCommandHandle` is not in the call list.
- Design decisions of 3c: `docs/nvn-design.md`, section "3c Host design decisions" (13 decisions and the work order).

## NVN stage 3, 3c steps 1 to 4a finished (2026-11-03, commits 5b6455c, 7163d63 and 77ec6f7, not pushed)

Scope: the design section 3c, the pool mapping, the Buffer handlers, the two analysis tasks and the command buffer recorder. No game
run was made for any of it. Host tests: `nvdrv_service`, `nvn_params`, `nvn_cmdlist`. Rows 0037 to 0039 of `docs/runtime-changes.md`.

- Pool mapping (0037): `nvn_gpu_map_range(gva, cpu, size)` and `nvn_gpu_unmap_range(gva, cpu, size)` in `nvdrv`. Map needs page
  aligned arguments inside the 40 bit space and no mapped page in the range (else 0). Unmap clears only the pages that still show the
  pool storage (the `own` mode of `gmmu_unmap`). `MemoryPoolInitialize` maps and `Finalize` unmaps. Open: a pool initialized twice
  without `Finalize` keeps its first mapping (game fault, not handled).
- Buffers (0038): ten handlers. A record holds pool address, pool generation, offset and size. `nvn_buffer_check` (pure) refuses a
  physical pool and a range outside the pool. A buffer whose pool ended returns 0 with a log line (first 8 times). `BufferMap` is
  `nvn_memory_pool_map` plus offset.
- Format table (analyst task D, 9 rows checked against the module bytes): the static table is at 0xe029028 (sdk), 161 rows (index
  0x00 to 0xa0), stride 0x30. The global 0xe03fd40 points to it through an R_AARCH64_RELATIVE relocation, no code stores it. The NVN
  format enum is the same as the row index. The word at row +0x1c is the Maxwell RT format word. Values: 0x01 f3, 0x02 f4, 0x03 f6,
  0x04 f5, 0x05 f2, 0x06 ee, 0x07 ef, 0x08 f1, 0x09 f0, 0x0a e5, 0x0b e4, 0x0c e3, 0x0d ea, 0x0e eb, 0x0f ed, 0x10 ec, 0x11 de,
  0x12 da, 0x13 db, 0x14 dd, 0x15 dc, 0x16 cb, 0x17 cd, 0x18 cc, 0x25 d5, 0x26 d7, 0x27 d9, 0x28 d8, 0x29 ca, 0x2a c6, 0x2b c7,
  0x2c c9, 0x2d c8, 0x2e c0, 0x2f c2, 0x30 c1, 0x31 17, 0x32 13, 0x33 14, 0x34 0a, 0x35 14, 0x36 19, 0x37 fa, 0x38 d6, 0x3d d1,
  0x3e d2, 0x3f e0, 0x6d f9, 0x6e d7, 0x6f d9, 0x70 d8, 0x71 ce, 0x72 c6, 0x73 c7, 0x74 c9, 0x75 c8, 0x76 c3, 0x77 c5, 0x78 c4,
  0x95 e8, 0x96 f8, 0x97 e9, 0x99 e6, 0x9a cf, 0x9b e7, 0x9c d0. All other rows (0x19 to 0x24, 0x39 to 0x3c, 0x40 to 0x6c, 0x79 to
  0x94, 0x98, 0x9d to 0xa0) hold 0. The depth block of `SetRenderTargets` (0x3F8) uses the same table (0x31 to 0x36 are the depth
  and depth stencil rows). Other fields of a row: +0x00 bit 0 valid, +0x08/+0x09 block width and height, +0x0a to +0x0d channel bits,
  +0x0e depth bits, +0x0f stencil bits, +0x10 bytes per block, +0x14 vertex attribute word, +0x18 copy texture word, +0x24 TIC seed,
  +0x2c depth stencil code. Open: the result for an index above 0xa0 (not checked), the format source of the depth compression
  block that follows the 0x3F8 and 0x48A blocks, and the fields +0x04, +0x13, +0x20. The values above come from the report, and 9
  rows were compared with the module bytes (0x01, 0x02, 0x25, 0x29, 0x34, 0x35, 0x6d, 0x95, 0xa0): read the module again for a row
  that a scene needs and that is not in this list. Full report: scratchpad `reports/analyst-3c-D-1.md`.
- Macro 1 (explorer task, 0xE02 called by `ClearColor`): 10 words at 0xde86704 (sdk), uploaded by `nvnQueueInitialize` (d84f800, on
  subchannel 0, start entry index 1, start word 0x10). The log line of the run: `mme: macro 1 at word 10, 10 words uploaded
  together, hash ead26c3e2109b06b`. The macro reads the layer count at engine register `0x206 + 16 * target` and writes
  CLEAR_SURFACE (0x674) once for each layer with `arg + 0x400 * layer` (arg = (target & 0xF) << 6 | (mask & 0xF) << 2). It writes
  no other method. `ClearColor` also writes 0x360 to 0x363 (the color bits) and writes 0x3ED = 0 around the call and restores it (net
  change zero). ScarletNX already runs this macro (`mme_hle.c` `multi_layer_clear`, `engines.c` `macro_call`) and `vk_render_clear`
  honors the scissor when CLEAR_FLAGS 0x43E bit 8 and scissor enable 0x380 bit 0 are set. A masked clear before the renderer runs
  writes nothing (`clear_in_memory` takes mask 15 only). Open: the meaning of 0x3ED, and the 16 word macro form (device +0xB0 bit
  4, registers 0x3D8 and 0x3E0). The report had one typo: the word at index 4 is ffffd211. Full report: `reports/explorer-3c-2.md`.
- Command buffer recorder (0039): `gpu/nvn_cmdlist.c` and `.h` (pure) hold the op list and the list store. Ops: SET_TEXTURE_POOL,
  SET_SAMPLER_POOL, SET_RENDER_TARGETS (count 8 at most, views copied), SET_VIEWPORT and SET_SCISSOR (one op for each rectangle,
  16 at most), CLEAR_COLOR (float bits as given), BARRIER, CALL. Payload at most 512 bytes. A handle has the generation in the
  upper 32 bits, (index + 1) in bits 1 to 31, bit 0 clear. A list has a reference count: the command buffer owns one, `CALL` ops
  hold their targets, and a freed list gives its CALL targets back without recursion. The command buffer drops the owner
  reference at `Finalize` and when the game gives control memory that overlaps the memory the list had at `EndRecording`. Lists
  of other command buffers are not seen. `AddControlMemory` is called 1.47 million times, `BeginRecording` 1.33 million times: the
  reuse pattern is not known. The handlers are in `nvn_native.c` (G6). An op outside a recording or with an unknown object is
  dropped with a log line (first 16). The other CommandBuffer functions are still missing functions.
- Tools of this work: the scratchpad has `edit_*.py` scripts (they edit files and keep CRLF), and `read_sdk.py words <addr> <bytes>`
  reads module words (run it in the MSYS2 CLANG64 shell with `SNX_SCARLETNX` set). `orca terminal wait --for tui-idle` returned at
  once for the opencode agents (`blockedReason: agent-interactive-prompt` although the agent worked). A background loop that waits
  until both report files are stable for 45 seconds worked. A multi-line Python heredoc in the Bash tool failed once: write the
  script with the Write tool and run it.

## NVN stage 3, 3c step 1, 2a and 2b finished (2026-11-03, not committed)

- RT format table: `nvn_rt_format_word(format)` in `gpu/nvn_params.c` and `.h`. 66 rows from the section "Format table", all other
  rows 0, an index above 0xa0 gives 0 (0 means a fault of the caller). Test: `test_rt_format_word` in `tests/nvn_params.c`.
- 2a: `gpu_channel_open(id)` in `gpu.c` and `gpu.h` (row 0040 of `docs/runtime-changes.md`). It makes the `GpuChannel` of a syncpoint
  id with the reset state and no submission. Take the id from `syncpt_alloc()`: the renderer uses `ch->id` for the query counters
  (`vk_query_*`) and for `g_draw.ch_id`, so two channels must not share an id. `gpu_call(fn, arg, NULL, 0)` runs `fn` on the GPU thread in
  order with the other calls. The save state code walks `g_gpu.chan`: a channel without jobs is fine for it (not tested).
- 2b: the replay module `gpu/nvn_replay.c` and `.h`, test `tests/nvn_replay.c` (CMake entries added in `src/nx/CMakeLists.txt` and
  `tests/CMakeLists.txt`). Written by the code writer (brief `reports/brief-writer-3c-2b.md`, report `reports/writer-3c-2b-1.md`). I
  read the `.c` and `.h` fully. I did not read the 519 lines of the test fully (expected words are by hand, ASan and UBSan runs were
  clean, `ctest` passes). The module is pure (no lock, no allocation, no log). `NvnReplayEnv` has the callbacks `method`,
  `read_register`, `find_list`, `descriptor_pool`, `render_targets`, `fault` and the flag `window_origin_flip`. `nvn_replay_list(env, list,
  stats)` never stops early: a fault skips the op and adds to `stats.faults`. Rules: pools write 0x55D to 0x55F (texture) and 0x557 to
  0x559 (sampler) with limit count-1; viewport writes 0x280/0x281, 0x283/0x284, 0x300/0x301 (stride 8, 8, 4; Y scale negative with
  the flip flag); scissor writes 0x381/0x382 (stride 4), both words 0 when an axis is empty; ClearColor writes 0x360 to 0x363, then
  0x674 once for each layer (layer count = register 0x206 + 16*index, at most 4096); Barrier writes no method and counts the bits 0 to
  8; CALL replays the target one level deeper (a CALL at depth 8 is a fault, a dead handle is a fault); SET_RENDER_TARGETS goes to the
  callback. Notes: the color words are written before the layer limit is checked; the size check of a payload uses the padded size
  (the op list pads to 8). Nothing calls the module yet. The viewport and scissor formulas come from the unverified analyst report
  (`analyst-3c-C-1.md`) and are checked later by the selftest case S.
- Explorer task 3c-3 (`reports/explorer-3c-3.md`, `d83d01c`, the layout record that `SetRenderTargets` reads). Checked against the
  disassembly: +0x00 address (`T+0x80`), +0x0c/+0x10/+0x14 size (`T+0x88/0x8c/0x90`, no view), +0x18 kind (1 for a buffer target or
  `flags & 0x50`, else 2), +0x20 and +0x24 = width and height times the sample factors (`T+0x66`, `T+0x67`), +0x40 layer stride
  (`T+0x58`), +0x48 bytes per block (`T+0x64`), +0x4c sample code (`T+0x68`), +0x50 format (`T+0x38`, or the view word). Pitch linear
  returns at d83d170, so +0x20 and up stay 0 and +0x08 = `T+0x60`. The sample switch (d83cb68, table 0xde860fa, key = builder samples
  - 1): samples 0 gives factors 1 and 1 and code 0; samples 1 and 2 give 2 and 1 and code 2; samples 4, 8, 16 give 2 and 2, ... with
  code = samples. The host `sample_x` for samples 1 is 1, so it differs (open: does the game call SetSamples(1)?). Not checked: the
  tile rows +0x2c to +0x38 (`d869e10`, `d869d10`). From the report: +0x2c is 0, +0x30 and +0x34 are the tile exponents `T+0x95` and
  `T+0x96` (lowered when the level is below one tile), +0x38 is `1 << T+0x94` when width_blocks >= (64 / bytes_per_block) << tile_x
  and height_blocks >= 8 << tile_y and (depth >> tile_z) != 0, else 1. The RT block (from `analyst-3c-C-1.md`, step 4 to 6): width word =
  align_up(+0x20, +0x38 * (64 / +0x48)), height = +0x24, tile word = (+0x2c & 0xF) | ((+0x30 & 0xF) << 4) | ((+0x34 & 0xF) << 8)
  (| 0x10000 for a 3D target), array pitch = +0x40 >> 2, third dimension = +0x14, block header 0x20080000 | (0x200 + 0x10 * i).
  Pitch linear block: address, +0x08 (stride), +0x10 (height), format, 0x1000, 1, 0. The view rules are in the report (base level,
  base layer, layer count, format remap by block ratio); the target field of a view is not read by d83d01c.
- Agents: the `code writer` (skill `orca-agents`, `opencode`, `opencode-go/deepseek-v4.1-flash`) is defined. Facts of the first use:
  `orca agent launch` fails with `selector_not_found` in a worktree that Orca does not manage (a worktree in the scratchpad): launch it
  from the main folder and name the work folder in the brief by absolute path. `orca terminal wait --for tui-idle` returns at once for
  opencode: wait with a background loop until the report and patch files exist, then 45 seconds more. The writer made its patch with `git
  diff` in a worktree that held my uncommitted changes, so the patch had them too: take only the new files and the CMake diff.
  `CLAUDE.md` still names GPT-6-Luna for the explorer, analyst and handler writer (they run on DeepSeek, the skill says `OpenCode`).
- `ctest`: `driver_workflow` failed once in a parallel run (the agents ran meanwhile) and passed alone in 11 seconds. `tracked_material`
  is the known failure.

## NVN stage 3, 3c steps 2b-2 and 2b-3 finished (2026-11-03, commits caed276, 4260dd8, c31d4bd, f9141a2 and 5ad2370, not pushed)

- Commits: the RT format table (caed276), `gpu_channel_open` row 0040 (4260dd8), the replay module row 0041 (c31d4bd), the RT words
  module row 0042 (f9141a2), the replay host callbacks row 0043 (5ad2370). The replay commit and the RT commit have the line
  `Co-Authored-By: OpenCode <noreply@opencode.ai>` (the code writer). `CLAUDE.md` names GPT-6-Luna for the writers: it is old, the
  writers run on DeepSeek through OpenCode.
- 2b-2, the RT words (`gpu/nvn_rt.c/.h`, test `tests/nvn_rt.c`, code writer, brief `reports/brief-writer-3c-2b2.md`, report
  `reports/writer-3c-2b2-1.md`). `nvn_rt_emit(env, color[], count, has_depth, clip_limit)` checks all targets first and writes
  nothing when it refuses (one `fault` call). Words in order: 0x487 = 0x0FAC6880 | count, per color target eight words at 0x200 +
  0x10*i (address high, low, width word, height, RT format word, tile word, depth, layer stride >> 2), a NULL slot gives 0x204 +
  0x10*i = 0, the slots from count to 7 give the same, then 0x54E = 0 and 0xD28 = 0 (null depth), 0x3FD and 0x3FE (min(width, limit)
  << 16 and the same for the height, 0 when the width or the limit is below 1, start value 0xFFFF), then 0x574 = sample mode. The
  sample mode: code = `layout.samples` of the LAST non-NULL color target (no target: 0), index = rotate_right(code, 1) - 1, an
  index above 7 gives 0, else {5, 2, 0, 4, 0, 0, 0, 6}[index] (table 0xde10b10, it matches `texture_sample_codes` in `nvn_params.c`).
  The host layout maps `SetSamples(1)` to code 2, so the open question about `SetSamples(1)` is closed. Width word = align_up(W,
  factor * (64 / bytes_per_block)), W = width * sample_x; factor = 1 << tile_x, or 1 when W is below (64 / bpb) << tile_x (from
  `d869d10`; the read of the caller of `d869d10` is not done: selftest case S checks it). The tile word = (tile_y << 4) | (tile_z <<
  8). Refused: count above 8, a depth texture, a view with flags, a target other than 2D, levels above 1, linear flags, an RT format
  word of 0, a compressed block, a target smaller than one tile (the library lowers the tile exponents then, not checked), a bytes per
  block of 0 or above 64, a NULL color array. I added the last two guards after the review. The disassembly of `d855da0` matched the
  analyst report, except `SURFACE_CLIP`: it is 0 when the width or the limit is below 1.
- 2b-3, the replay host (`nvn_native.c`, `nvn_native.h`). `nvn_native_replay_env(NvnNativeReplayHost* host, int
  window_origin_flip, NvnReplayEnv* env)`. `NvnNativeReplayHost` has `channel` (a `GpuChannel*`, GPU thread only) and `clip_limit`.
  Callbacks: `method` = `maxwell3d_method(ch, m, v, 1)`, `read_register` = `ch->r3d[m]`, `find_list` = `nvn_list_find` under
  `objs_lock`, `descriptor_pool` = memory pool `gpu_address` + descriptor pool offset (the generation of the pool and the kind are
  checked), `render_targets` copies the textures under the lock and calls `nvn_rt_emit` outside it, and refuses with a log line while
  `clip_limit` is 0. Faults are logged with `LOG_LIMIT(16)`. The list slots are separate allocations now (`NvnListStore.slot` is
  `NvnListSlot**`): a pointer from `nvn_list_find` stays valid while the list has a reference, also when the game publishes another
  list (test `test_find_pointer_stable`). The replay holds a pointer for the whole walk, so the frame job must hold one reference of
  each list until the replay ends. Not done: the private channel (made in the frame job).
- Selftest run of 2026-11-03 23:05 (update program, `nvn_native=0`, `nvn_selftest=1`): 51 cases, 0 failed, 0 skipped, 30 frames a
  second, a clean exit. New log line "selftest: device render target words +18=00004000 +19d8=00000001 +1a04=00000000". So the
  library device has `+0x18` = 0x4000 (clip limit; the store is not found, one run), `+0x19d8` = 1 (window origin mode 1: the
  viewport Y scale is negative, `window_origin_flip` = 1) and `+0x1a04` = 0 (the dependent depth block of `SetRenderTargets` is not
  skipped; no effect while no depth target is supported).
- The code writer worked well when the brief had the formulas (with the disassembly checked), the exact interface, the refusal list and
  the test cases. It left two holes (a zero divisor, a NULL array): look for those in each patch. The writer worktree
  (`wt-2b2`) was removed after the commit. `driver_workflow` fails in a parallel `ctest` and passes alone (about 11 s).

## NVN stage 3, 3d step 1: the Program records (2026-11-04, commit e1d52ab, not pushed)

Crash of the first native run (2026-11-03) and its cause (checked by the caller where noted):
The crash (log of 2026-11-03 23:45, update program, main at 0x8005000, sdk at 0xd3c7000):
- Host exception at game address 0x68, in the game function `8156090` (main), at 8156140 (`mov w8,#0x68; ldr x0,[x8]`). It is the
  branch of a lookup that found 0 three times: `x2 = [x1 + offset]` for three offsets from a table in the GOT (`c277850`). The compiler
  took the zero as impossible. `x1` is `[x23+0x20]` where `x23` is the first block of a BNSH file (`nn::util::BinaryFileHeader::
  GetFirstBlock`, sdk d77bf34). The 0xc0 bytes of the object at `1455410c0` are 0. The BNSH header (`BNSH`, `grsc` block at +0x60,
  size 0x21fa0) reads well.
- Call chain: ab4ec6c (main) calls 8132b6c, then 8134740, then 8134814, then 8156090. The caller at ab4ec68 passes size 0x400 and a
  base from `[[x19+0x68]+0x58]+0x3010`.
- Before the crash the log shows `not implemented` for `nvnProgramInitialize` and `nvnProgramSetShadersExt` (2.27 s, planned for 3d),
  and later `nvnTexturePoolRegisterImage`, `nvnTextureViewGetLevels`, `nvnTextureBuilderSetSize3D`, `nvnBlendStateGet*`, the
  `nvnVertexAttribState*` group. Not known: which one (if any) feeds the zero. Not known: whether `nvn_native=0` passes this point (earlier
  selftest runs did reach the title area, but no log of that point was kept).
- Explorer report (2026-11-04, `reports/explorer-bnsh-crash-1.md`; the control flow of 8156090 was checked by the caller, the rest
  is not checked): `x1` is the program table at copy+0xc0 of a heap copy of a static BNSH template (`memcpy` from main:0xc02d000 at
  8132bdc, then the SDK relocator sdk d77c128). It is not a file read and not a host Buffer. The table has three columns (+0x10, +8, +0).
  The log pc 81560e4 is the return of the helper call at 81560e0, so column +0x10 was non-null, the helper `8781cc4` returned
  non-zero, and columns +8 and +0 are zero: the code then goes to the load at 0x68. The game has no error path for it.
  The explorer says that `nvnProgramInitialize` and `nvnProgramSetShadersExt` (they return 0 and write nothing) cause the helper failure.
  This is a guess: no load from the program object is shown in the helper. The log shows only 16 bytes of the table.
- CAUSE FOUND (2026-11-04, `reports/explorer-helper-8781cc4-1.md`; the path 8781ee4 -> 87820f8 -> 8782100 -> 8782120 was checked by
  the caller). The helper `8781cc4` builds an NVN program from a "grsc" record: `nvnProgramInitialize` (8781d10), then
  `nvnProgramSetShadersExt` (87820f8, the record flag bit 1 is set). `tst w0,#0xff; b.eq 8782120` sets the result 3 when it returns 0.
  The host "not implemented" handler returns 0, so the helper fails, the columns +8 and +0 are null, and 8156090 loads address 0x68.
  So the crash is the missing 3d work: real handlers for `nvnProgramInitialize` (sdk d8362a0: returns 1, device at program+0,
  link into the device list at device+0x940/+0x948) and `nvnProgramSetShadersExt` (sdk d836da0 -> d836770: returns 1 at d836cb0).
  Not checked: the other "not implemented" functions of 2.33 may fail the same way later.
- Step 3d (Program handlers), analyst table done (2026-11-04, `reports/analyst-3d-1.md`, brief `brief-analyst-3d.md`). The caller
  checked the rows of Initialize (d8362a0), Finalize (d836330) and SetShadersExt (d836da0) in the disassembly: they are right. Not
  checked: the rows of helper d836770, SetSubroutineLinkage, BindProgram. Facts: Initialize(prog, device) returns 1 and writes
  prog+0..0x8f (device at +0, device list links at +0x80/+0x88, lock device+0x950). Finalize returns void. SetShadersExt(prog, count,
  entries 0x18 bytes: +0 nonzero, +8 shader header pointer, +0x10 control pointer) copies to d836770 and returns 1 or 0 (count<0 -> 0,
  count 0 -> 1). No `nvnProgram*` getter was found in the resolved list. The game's program block is at least 0xc8 bytes.
- Next: decisions of the caller for the brief of the handler writer (host side table record for the program: device, shader headers,
  stage, control; no write of the library words into the game memory by the rule of the user; what `BindProgram` of the host recorder
  needs), then the handler writer. Ask the user before the start.

Result of 3d step 1 (commit e1d52ab):
- The host has `NVN_OBJ_PROGRAM` records (`ProgramRec`: device, generation, 6 stage records with header pointer, entry word, copy of the
  0x18 control block). Handlers: `nvnProgramInitialize`, `nvnProgramSetShadersExt`, `nvnProgramFinalize` in `nvn_native.c`. The pure function
  `nvn_program_header_stage` is in `nvn_params.c`. Not done: `SetShaders`, `SetSubroutineLinkage`, `SetSampleShading`, `SetDebugLabel`,
  `CommandBufferBindProgram`, the shader compile.
- MISTAKE found by the first run (2026-11-04 14:10): the caller took `header+8` (5..16) as the stage. It is the header LAYOUT version
  (9 classes for the 12 values, jump table de8605c, d8363c0). The stage index is the u32 at `header+0x714` (sdk d837228, vtable+0x70):
  0 vertex, 1 fragment, 2..5 accepted by d836e90 but unused by the game. With the wrong stage, every program with two entries was refused
  ("stage is repeated") and the old crash at 0x68 stayed. Lesson: the analyst table said "header+8 in 5..16" and the caller did not
  check the row of d836770/d8363c0; check such rows before a brief.
- Run of 2026-11-04 14:26 after the fix: no crash, 64 programs set (8 with stage mask 1, 56 with 3), 0 refused. See "State" in `WORK_PLAN.md`.
- Not verified: that the checks header+0xc == 0x120 and header+0x10 == 0xb hold for all headers (they held for all 64 sets of the run);
  the stage names 2..5; the size constant at vptr+8 of the nine classes (0x7c0..0x878) is not shown to be the header size.
- Agents: the code writer (patch `writer-3d-prog-1.patch`, read fully, correct to the brief) and the game explorer (`explorer-stagemap-1.md`).

## NVN stage 3, 3d step 2: the refusal counters and the analysis of the depth part (2026-11-04, commit 895de30, not pushed)

- Moved from "State" and "Next" of `WORK_PLAN.md`. Run of 2026-11-04 14:26 (update program, `nvn_native=1`, build of e1d52ab): no crash, 31.7 s,
  30 frames a second, 0 JIT misses, 201 `frame N` lines with `shown=1` and `dropped=0`. Every frame had `faults=25` and `missing` of about
  600 (6751 in frame 1). 16 lines `replay render targets refused` (15 `rt depth (1)`, 1 `rt view flags (3f)`), because the log limit is 16.
  What the screen showed: not known.
- Done and committed in 3c before this step: the pool mapping, the Buffer handlers, the command buffer recorder, the RT format table,
  `gpu_channel_open`, the replay module, the RT words module, the replay host callbacks, 2c (`7ef6809`), 2d (`d0bb6b9`) and the
  Program records (`e1d52ab`).
- Candidates that were listed for 3d: (a) why `faults=25`, (b) `rt depth`, (c) the "not implemented" groups (State objects, Texture and
  TextureView getters, `TexturePoolRegisterImage`, `TextureBuilderSetSize3D`, `Sampler*`, then `CommandBufferBindProgram` and the draws).
  (a) and (b) are one cause (see below). (c) is still open.
- Commit 895de30: `replay_cause_count` and `replay_causes_take` in `nvn_native.c`. Each logged frame has a line
  `nvn-native: frame N refusals: <cause>=<count>; ...` (16 causes at most for a frame, the rest is `other`).
  `render targets` counts every refused `SetRenderTargets`, so the sum is more than `faults`.
- Run of 2026-11-04 14:35 (build of 895de30): no crash, 17 s, 452 frames. Each frame from frame 2: `rt view flags=1; render targets=25;
  rt depth=22; rt small target=2` (frame 1: flags=5, targets=29). So all 25 `faults` are refused `SetRenderTargets`: 22 with a depth
  texture, 2 `rt small target`, 1 color view with flags. `missing` is still 563 to 893 for a frame; the most called missing function is
  `nvnDeviceGetTextureHandle` (53310 calls in the run).
- Game explorer report `reports/explorer-3d-depth.md` (scratchpad). Checked by the caller in the disassembly: the depth block words
  (`d856118..d856170`), the small target function `d869d10` and the tile word (`d856150..d856164`, `d855ecc..d855eec`).
  - Depth part of `d855da0` with depth texture x4 != 0: `d83d01c(x4, layout, x5)`, then `0x200503F8` (5 words: address high, address
    low, format word P, tile word T, array pitch R = layout+0x40 >> 2), inline `0x54E` 0, inline `0x54E` 1, `0x2003048A` (3 words:
    W, H, depth & 0xFFFF; W uses the same align_up formula as a color target), then the dependent block, then the clip words `0x3FD`,
    `0x3FE` and the sample mode `0x574`. With x4 == 0: inline `0x54E` 0 and inline `0xD28` 0.
  - Format word: the same table as the color targets (global `0xE03FD40` to `0xE029028`, stride 0x30, field +0x1C). Depth rows 0x31 to
    0x36: S8 0x17, D16 0x13, X8D24 0x14, D32F 0x0A, D24S8 0x14, D32FS8 0x19. All six match `nvn_rt_format_word` (explorer, not checked
    by the caller).
  - Tile word T = nibbles of layout+0x2C (x), +0x30 (y), +0x34 (z) at bits 0, 4, 8. The color word adds 0x10000 for a 3D view target;
    the depth word does not. The host color word has no x nibble (equal only while tile_x is 0). The depth part has no linear branch:
    a linear depth texture gives W = H = T = R = 0 (d83d01c returns before it writes them).
  - `d869d10` (tile factor) never refuses: if width, height or depth is too small for the tile, it returns 1, else `1 << tile_x`. The host
    `nvn_rt.c:80` refuses ("rt small target"). That is the cause of the 2 refusals.
  - Clip words: the minimum of width and height covers the color targets AND the depth target (explorer, not checked by the caller).
  - Dependent block (methods 0x1F8, 0x572, 0x1F0, 0x57F, 0x580, 0x1F3, 0xBA, 0xE06, 0xD28, branch B 0xD29, 0xBA, 0xBE.., 0xDC, 0xBF):
    from `d83ce30`, only written when `device+0x1A04 != 0x08C8B771`. Compression and zcull; not needed for a working depth target
    (explorer's guess). The value of `device+0x1A04` on the game's device is not known (option `1a298e9f`, `d831634`).
  - Depth view (x5): flags 0x1 (levels), 0x2 (layers), 0x4 (format) change the words; 0x8, 0x10 and 0x20 do not.
- Moved from `WORK_PLAN.md`: the optional check for 3b-5b (not done): read the bytes of the tables de87944, de87948, de87950 (jump tables),
  de879e8 (target codes) and de87970 (device rows) from the module and compare them with `nvn_params.c`.

## NVN stage 3, 3d steps 3 to 5: depth target, color view, 2D array target (2026-11-04, commits 4690c52, a4d649f, 15648e5, not pushed)

Moved from `WORK_PLAN.md` when the steps were finished. Run of `15648e5`: no crash, `faults=0`, black screen (expected).

Task now: the run of `15648e5` (title scene, `nvn_native=1`, update program, wait 10 s, close). Then read `frame N refusals:` (expect no `render targets` refusal left, `faults=0`) and the crash block. The reviewer step for the depth work was not done. Steps 3 to 5 below are DONE (code writer patch, caller fixed the write order 0x3F8, 0x54E pair, 0x48A). Step 5 (reviewer) was not done. Old text of the task: depth render targets in `nvn_rt.c` (steps 3 to 6 of the plan below). Steps 1 and 2 are done
(895de30 and the explorer report `reports/explorer-3d-depth.md`; its facts are in `OLD-WORK_PLAN.md`, "3d step 2"). Decisions of the user:
1. `nvn_rt_emit` gets a depth target (a texture and a view, like `NvnRtColor`) and the blanket "rt depth" refusal goes. The host callback
   `replay_host_targets` (`nvn_native.c`) copies the depth texture under `objs_lock` too (a gone depth texture: fault "texture gone").
2. "rt small target" stops refusing: if width, height or depth is too small for the tile, the factor is 1 (as `d869d10`). Fix it for
   color and depth. Check the width formula of word[2] with it.
3. The tile word gets the x nibble (layout tile_x at bit 0, y at bit 4, z at bit 8). Color adds 0x10000 for a 3D view target only if the
   color path needs it (the host refuses view flags, so no view target is seen now).
4. The clip words (0x3FD, 0x3FE) take the minimum over the color targets AND the depth target (explorer, check `d8563c0..d85644c`).
   The sample mode uses the last present color target, or the depth target when there is no color.
5. The dependent block (`d83ce30`: 0x1F8, 0x572, 0x1F0, ..., 0xBE, 0xDC) is NOT written. The log says so once (a counted line). The value of
   `device+0x1A04` is unknown: add it to the first frame log line if it is easy.
6. A depth view: flags 0x1, 0x2, 0x4 are used (levels, layers, format); 0x8, 0x10, 0x20 are ignored. The color view refusal
   (`rt view flags`) stays as it is. A linear depth texture is refused with a log line (the library gives zero words).
The depth words: `0x200503F8` (A high, A low, P, T, R = layout+0x40 >> 2), inline `0x54E` 0, inline `0x54E` 1, `0x2003048A` (W, H,
depth & 0xFFFF), then the clip words. With no depth: inline `0x54E` 0 and inline `0xD28` 0. P is the same table as color
(`nvn_rt_format_word`). The `0x54E` meaning (depth enable) is an inference: say so in the comment.
Plan: 3. the caller writes the brief for the code writer (files: `nvn_rt.c`, `nvn_rt.h`, `nvn_native.c` (`replay_host_*` only), `tests/nvn_rt.c`;
detached worktree, the existing functions `nvn_rt_emit`, `rt_target_words`, `replay_host_targets`) and launches it. 4. read the whole patch, `git apply
--check`, build, `ctest`. 5. optional code reviewer. 6. commit, copy the exe, tell the user the scene to run (title scene, `nvn_native=1`,
update program) and read `frame N refusals:` (expect `rt depth` and `rt small target` gone, only `rt view flags=1` left). Open after
this: the 1 color view refusal (a view with flags 0x3f on a color slot: find which call), then the "not implemented" groups, the first
being `nvnDeviceGetTextureHandle`. The analyst/writer method of 3b applies. Ask the user which one comes after the depth work.

## NVN stage 3, 3d texture handle (2026-11-04, commit 2b33574, not pushed)

- Run of `15648e5` (update program, 29 s): no crash, 30.0 fps, 200 of 200 logged frames `faults=0`, no JIT miss, black screen.
- Done in 3d before it: program records (`e1d52ab`), refusal counters (`895de30`), depth render target (`4690c52`), color view words
  (`a4d649f`), 2D array render targets (`15648e5`).
- Analyst table `reports/analyst-3d-texhandle-1.md` (14 functions; 3 rows checked in the disassembly: d82d0d0, d83c8b0, d8483b0).
  Handle = `(u64)(s64)(s32)(tex | smp << 20) | 1<<32`. Image handle = `(s64)(s32)idx`, no tag. `BindTexture` (d8483b0) writes into
  the command stream: macro 7 (method 0xE0E | map<<16, map = table de864dc[stage]), optional 0x68B, then 0x8E3 (cb pos slot*8+0x20)
  and the handle; stage 5 writes a 16-byte record. `TextureFinalize`, `SamplerFinalize`, `SamplerSetDebugLabel` are empty (`ret`).
- Code writer (patch UTF-16, regenerated by `git diff`): pure `nvn_texture_handle` and `nvn_image_handle` in `nvn_params.c`, the op
  `NVN_OP_BIND_TEXTURE` (stage 0 to 5, slot below 128, host rule), the env callback `bind_texture`, the table
  `texture_handle[6][128]` in `NvnNativeReplayHost` (per frame job, on the stack of `frame_replay_lists`, not cleared between lists).
  No Maxwell method is written. Tests added in `nvn_params`, `nvn_cmdlist`, `nvn_replay`. Build and ctest pass.
- Run of `2b33574` (update program, 34 s): no crash, 30.0 fps, `faults=0`, no JIT miss, 105 missing functions. `docs/nvn-design.md`
  2-3 was corrected (the image handle has no tag).

## Group 1 and the lost code (2026-11-04, commits ab78933 and 70b9a42, not pushed)

Task: remove the "missing" functions by call count (decision of the user, 2026-11-04). Group 1 = the raster state setters of the
command buffer. Method of 3b-4 (analyst table, brief with decisions, code writer patch, read, build, ctest, commit, run).

- Analyst table: `reports/analyst-g1-raster-1.md` (10 rows). The five setters and `SetDepthClamp` were checked in the disassembly (all
  match). Rows not checked: `SetDepthBounds`, `SetLineWidth`, `SetPolygonOffsetClamp`, `SetRasterizerDiscard`.
- Facts: every setter writes words to the command memory only (no lock, no counter). Words: `I(m,v) = 0x80000000 | (v & 0x1FFF) << 16 | m`,
  `H(m,n) = 0x20000000 | n << 16 | m`. Face bit 0 = front, bit 1 = back, other bits ignored. Stencil value is cut to 8 bits.
  `SetStencilRef` 0x4E5 / 0x3D5, `SetStencilValueMask` 0x4E6 / 0x3D7, `SetStencilMask` 0x4E7 / 0x3D6 (front / back). `SetBlendColor` is
  `H(0x4C7,4)` with the raw words of the 16 bytes at x1. `SetAlphaRef` is `H(0x4C4,1)` with the float of s0 (v0, not x1).
  Other rows: `SetDepthBounds` 0x66F inline plus `H(0x3E7,2)`, `SetLineWidth` `H(0x4EC,2)` (same float twice), `SetPolygonOffsetClamp`
  0x55B / 0x56F (2 * units) / 0x61F, `SetDepthClamp` 0x64F (0x101A when arg != 0, else 0x181D), `SetRasterizerDiscard` 0xDF (1 when arg == 0).
  The space check calls sdk `0xd8243d0` (update address) when the command memory is short.
- Commit `ab78933`: ops `NVN_OP_SET_STENCIL`, `NVN_OP_SET_BLEND_COLOR`, `NVN_OP_SET_ALPHA_REF`, handlers in `nvn_native.c` (macro
  `COMMAND_STENCIL_HANDLER`), replay in `nvn_replay.c`, tests in `tests/nvn_replay.c`. The brief is `reports/brief-writer-g1-raster.md`.
- Run of `ab78933` (18:42): 992 frames, 30 fps, no crash, `faults=0`, 0 JIT misses. The five setters left the missing list.
- Problem found in that run: commit `2b33574` held only a change of `docs/nvn-design.md`. The code of the texture handles and
  `NVN_OP_BIND_TEXTURE` had never been committed (the exe of that time had it). The writer patch `patches/writer-3d-texhandle-1b.patch`
  was merged by hand (`git apply --3way`, two "keep both" conflicts in `nvn_cmdlist.h` and `nvn_replay.c`) and committed as `70b9a42`.
  Rule: after each commit run `git show --stat` and compare it with the files of the change.
- `70b9a42`: `nvn_texture_handle` / `nvn_image_handle` in `nvn_params.c` (library form: sign extension, tag 1 << 32 for texture handles),
  `h_command_buffer_bind_texture` (refuses stage above 5 or slot from 128 with a log line), `replay_host_bind_texture` (bind table in
  `NvnNativeReplayHost`, not cleared between the command lists of a frame job), `env->bind_texture` in `nvn_replay.h`.
- Run of `70b9a42` (18:48, update program): 42 s, 1187 frames, 30.0 fps, no crash, `faults=0` in all 200 logged frames, 0 JIT misses,
  `GetTextureHandle` and `BindTexture` left the missing list. ops per frame 1113 (885 before). 100 missing functions. Top by calls:
  `BindUniformBuffer` 57548, `BindColorState` 56410, `BindDepthStencilState` 49034, `BindBlendState` 28105, `BindPolygonState` 22250,
  `BindMultisampleState` 20288, `BindChannelMaskState` 19375, `BindVertexBuffer` 8743.
- Not checked: the replay of the new ops is only seen in the totals of the frame line (no count by op code in the log).

## Group 2: the buffer binds (2026-11-04, commit ca3bdd7, not pushed)

- Analyst table: `reports/analyst-g2-bind-1.md` (accepted; the rows UniformBuffer, VertexBuffer, TransformFeedbackBuffer and the two
  stage tables checked in the disassembly). Brief of the writer: `reports/brief-writer-g2-bind.md`, patch `patches/writer-g2-bind-1.patch`.
- Library facts (sdk): all 8 functions write packets on subchannel 0. Uniform: `0x200308E0` (size rounded up to 256, addr hi, addr lo)
  and an inline header with the bind-group method `M[stage]` = 0x904 (vertex), 0x924 (fragment), 0x91C (geometry), 0x90C (tess
  control), 0x914 (tess eval), data `((slot+3)<<4)|1`, or `|0` when size is 0. Storage: macro 0xE20 with `P[stage]+slot` (0x11, 0x51,
  0x41, 0x21, 0x31) then `0x8E4..0x8E6`. Vertex: `0x701+4i` (addr hi, lo) and `0x7C0+2i` (limit hi, lo), size 0 gives addr 0x1000 and
  limit 0xFFF. Transform feedback: `0xE0+8i`, enable 1, addr hi, lo, size; addr 0 gives an inline disable. Stage 5 (compute) writes a
  24-byte control record (0x0C uniform, 0x0D storage); its consumer is not traced. The library checks no stage, slot or size.
- Host design: no GPU methods are written. One op `NVN_OP_BIND_BUFFER` (`NvnOpBindBuffer`, kinds `NvnBindKind`), limits
  `NVN_UNIFORM_SLOT_MAX` 32, `NVN_STORAGE_SLOT_MAX` 128, `NVN_VERTEX_SLOT_MAX` 16, `NVN_XFB_SLOT_MAX` 4, stage up to 5. The replay stores
  addr, size and `bound` (`nvn_bind_buffer_active`) in `NvnNativeReplayHost` (`uniform`, `storage`, `vertex`, `xfb`). The shader and
  pipeline stage (stage 4) reads these tables. A plural bind refuses the whole call on a bad range or an unreadable array.
- Known edge: in a plural bind a failed `command_record` does not stop the loop (only out of memory causes it).
- Writer error found by the caller: a test used a 32-byte payload, equal to `sizeof(NvnOpBindBuffer)`, as the "too big" case.
- Run of `ca3bdd7` (19:29, update program): 43 s, 1143 frames, 29.9 fps, no crash, `faults=0` in all 200 logged frames, `dropped=0`,
  0 JIT misses. 97 missing functions called (100 before). `BindUniformBuffer`, `BindStorageBuffer` and `BindVertexBuffer` left the
  list. Top by calls now: `BindColorState` 53796, `BindDepthStencilState` 46917, `BindBlendState` 26959, `BindPolygonState` 21719,
  `BindMultisampleState` 19962, `BindChannelMaskState` 18671, `DrawElementsBaseVertex` 7095, `SetDepthBounds` 6568, `BindProgram` 6255,
  `SetRasterizerDiscard`, `SetPolygonOffsetClamp`, `SetDepthClamp` 5586 each, `BindVertexStreamState` and `BindVertexAttribState` 5187.
  Screen: black (expected until stage 4).


## Group 3: the state objects (2026-11-04, commits 57b070b, not pushed) and group 4: the draw path (2026-11-04, commit a58387b, not pushed)

Group 3 step (a) DONE (2026-11-04): the analyst tables are accepted (7 rows checked in the disassembly, all correct):
`reports/analyst-g3-A-1.md` (Color, DepthStencil, Blend, ChannelMask) and `reports/analyst-g3-B-1.md` (Polygon, Multisample). They give the
word layouts, the defaults, the Set/Get formulas and the method packets of the six Bind functions. Facts to use: the Set functions do
not mask their arguments (copy the spill); Blend advanced path (d845cd0) is not decoded (the game never sets it: stop with a log line);
the Bind methods are not known to `nvn_replay.c` (0xE10, 0x671/0x672, 0x4BB, 0x4C5, 0x4B3, 0x4BA, 0x4C3, 0x4E0, 0xE1A, 0xE12-0xE15,
0xD35, 0x374, 0x4B9, 0x781+8t.., 0x646-0x648, 0x36B/0x36C, 0x370-0x372, 0x54D, 0x54F, 0x4B8, 0x478, 0xE20, 0x8E4, 0x3ED, 0x3EE,
0x3F5, 0x47E). Open: multisample sample-location tables 0xe015658/0xe015698 are zero in the file.
Group 3 step (b) DONE (2026-11-04, `57b070b`): 57 state functions, `NVN_OP_BIND_STATE`, host table `state[]` in the replay host. ctest passes
except `tracked_material`. The caller fixed three wrong clear masks of the writer (it passed keep masks to `nvn_state_insert`): check this in
the next patches. RUN DONE (2026-11-04 20:17, update program, `57b070b`): 55 s, 1445 frames, 30 fps, no crash, `faults=0`, `dropped=0`, 0 JIT misses, no
`blend advanced` line. Missing functions: 97 -> 45 (the 57 are gone). Not in the 57 but called: `ColorStateGetLogicOp` 465,
`DepthStencilStateGetStencilTestEnable` 381, `GetStencilFunc` 375, `PolygonStateGetPolygonMode` 43, `BlendStateGetBlendEquation` 10 (the
analyst tables have their formulas: add them with group 4 or alone). Next: group 4 (draw path, item 4 above): analyst brief for
`DrawElementsBaseVertex`, `SetDepthBounds`, `BindProgram`, `SetRasterizerDiscard`, `SetPolygonOffsetClamp`, `SetDepthClamp`,
`DrawElementsInstanced`, `DispatchCompute`, `DrawElements`, `DrawArrays`. The black screen is expected.
Group 4 step (a) DONE (2026-11-04): analyst table accepted: `reports/analyst-g4-draw-1.md` (17 functions; 3 rows checked in the
disassembly: DrawArrays, DrawArraysInstanced, SetRasterizerDiscard, all correct). The log addresses of the brief differ from the
disassembly addresses by +0x45000 (example: log d804670, code d849670). Facts: the draw functions emit macro methods (0xE32, 0xE36, 0xE38,
0xE3C, 0xE44, 0xE46) with the topology and the index type copied unchanged; they write no BEGIN or END word. DispatchCompute writes a
16-byte control record (0x0E, x, y, z), no Maxwell method. BindProgram copies the program words (`program+0x10`, count `+0x20`) and writes
control records 0x07 and 0x12. The program record of `e1d52ab` does not keep `+0x10/+0x20/+0x24/+0x28`: the host BindProgram must keep them.
Group 4 step (b) DONE (2026-11-04, `a58387b`): 17 functions, ops `NVN_OP_DRAW`, `NVN_OP_DISPATCH`, `NVN_OP_BIND_PROGRAM`, `NVN_OP_SET_RASTER`. Draws and dispatches are recorded and counted in the replay host, not executed. The frame line has `draws=` and `dispatches=`. ctest passes except `tracked_material`. The caller fixed one test value (StencilFunc face 2). RUN DONE (2026-11-04 23:11, update program, `a58387b`): 38 s, about 1150 frames, 30 fps, no crash, `faults=0` and `dropped=0` in all 200 logged frames, 0 JIT misses. `draw recorded` at 5.72 s, `dispatch recorded` at 12.10 s. A frame has 17 to 43 draws (1 dispatch in the later frames). Missing functions: 45 -> 31 (list in the end lines of the log). Black screen is expected. Missing, by count: `BindVertexStreamState` and `BindVertexAttribState` 4253, `SetDepthRanges` 3802, `SetDepthRange` 2535, `SamplerCompare` 2415, `ReportCounter` 1968, `ClearDepthStencil` 1678, `DepthStencilStateGetStencilOp` 375, `VertexAttribStateSetDefaults/SetStreamIndex/SetFormat` 80/78/78, `VertexStreamStateSetStride/SetDivisor/SetDefaults` 59 each, `TextureViewGetLevels` 47, `PolygonStateGetPolygonOffsetEnables` 12, `CopyTextureToTexture` 10, and 12 small texture and label functions (1 to 5 calls).
Next after the run: group 5 (item 5 above: vertex state), then item 6.
Lessons: the code writer passed keep masks to `nvn_state_insert` three times (caller fixed them); check the clear masks and the test values in each
patch. The log addresses of `nvn: <name> -> <address>` are the base game build; the update program code is at +0x45000.
Reports of the groups (scratchpad `reports/`): `analyst-g3-A-1.md`, `analyst-g3-B-1.md`, `analyst-g4-draw-1.md`, briefs `brief-*-g3-*`, `brief-*-g4-draw.md`.

Group 5 DONE (2026-11-04, `72cf639`): 13 functions (vertex attrib and stream state objects, their two Bind functions, SetDepthRange and SetDepthRanges, GetStencilOp, GetPolygonOffsetEnables). Ops `NVN_OP_BIND_VERTEX_ATTRIB`, `NVN_OP_BIND_VERTEX_STREAM`, `NVN_OP_SET_DEPTH_RANGE`. The replay host keeps raw attrib words and stream stride and divisor (16 entries each, entries above count reset). The library format table (guest `[0xe03fd40]`, entry 0x30, +0x14) is NOT used yet: stage 4 must convert the format. The stream bind sets the enable bit 12 for i < count. Depth range methods: 0x282, 0x285 (+8i), 0x302, 0x303 (+4i); `depth_mode` is read at record time from `DeviceRec`. The host default is 0: the only store of device+0x19DC in the sdk is SetDepthMode (d82d170), so no init default of 1 is shown (the analyst claim of 1 is not verified).
Lessons: the analyst row of BindVertexStream missed the enable bit (checked in the disassembly); the writer renamed a helper that already existed (duplicate definition) and wrote three wrong test floats (0.75 is 0x3F400000). Run the build before you read the tests.
Reports: `analyst-g5-vertex-1.md`, `writer-g5-vertex-1.md`, briefs `brief-*-g5-vertex.md`.

Open steps moved from the work plan on 2026-11-04 (old State and Next text; the facts of groups 1 to 5 are in the sections above):

- The last commit is `72cf639` (group 5 step (a): vertex state, depth ranges; before it `a58387b` group 4, `57b070b` group 3, `ca3bdd7`,
  `70b9a42`, `ab78933`). Check `git log` and `git status` first. The user pushed up
  to 7d7532e. The commits after it are not pushed. Nothing else is uncommitted (`tools/__pycache__/` is untracked on purpose).
  `ctest` passes except `tracked_material` (known; `driver_workflow` can fail in a parallel run, it passes alone).
- `third_party/ScarletNX/build/default/scarletnx.exe` is the build of `72cf639` (md5 785b36a7, the same as the three program copies:
  `game/scarletnx/bin/scarletnx.exe` and the two programs in `game/recompiled/01001F5010DFA000/`). The `.ini` of the game has
  `nvn_trace=1`, `nvn_native=1` and `nvn_selftest=0` (for the selftest set `nvn_native=0` and `nvn_selftest=1`). After each build:
  `tasklist` shows no game, then copy the exe.
- Last read run: 2026-11-04 23:11 (build of `a58387b`, update program): 38 s, about 1150 frames, 30 fps, no crash, `faults=0` and `dropped=0` in all
  200 logged frames, 0 JIT misses, 31 missing functions (list in the end lines of the log), `draw recorded` and `dispatch recorded` lines
  seen. The screen is BLACK (user): expected until stage 4. Facts of older work: `OLD-WORK_PLAN.md`, the last sections.


The native plan (decision of the user, 2026-11-02, `ROADMAP.md` phases 6 to 10), in this order: 1. graphics, NVN on the host to
Vulkan (no GPU command stream), 2. input and camera, 3. host replacement of SDK functions, 4. shaders and pipelines, 5. extracted
RomFS and a file layer for mods.

Phase 6 (graphics) follows `docs/nvn-plan.md` (five stages) and `docs/nvn-design.md`. Stages 1 and 2 are done. Stage 3 has the steps
3a and 3b-1 to 3b-7 (done), 3c and 3d. The review of 3b is done. The selftest run of 2026-11-03 passed (51 cases, twice).

Task now (decision of the user, 2026-11-04): remove the "missing" functions in the order of the call count, largest first. Groups, each one
step with the method of 3b-4: analyst table, caller decisions in the brief, code writer patch, read, build, ctest, commit, run.
Groups 1 (stencil, blend color, alpha ref, `ab78933`), 2 (buffer binds, `ca3bdd7`), 3 (state objects, `57b070b`) and 4 (draw, dispatch,
program bind, raster setters, `a58387b`) are DONE: facts in `OLD-WORK_PLAN.md`, the last sections. The black screen is expected.
5. DONE (`72cf639`, not run by the user yet: ask for one run, then read the frame line `vbinds=` and the missing list). Facts in
   `OLD-WORK_PLAN.md`, last section. Old text of the item: vertex state. `BindVertexStreamState` and `BindVertexAttribState` (4253 calls each) with the Set/Get functions of
   `VertexAttribState` (`SetDefaults` 80, `SetStreamIndex` 78, `SetFormat` 78) and `VertexStreamState` (`SetStride`, `SetDivisor`,
   `SetDefaults` 59 each, `GetStride` 1); `SetDepthRanges` 3802, `SetDepthRange` 2535. Also the small Get functions that are called but
   missing: `DepthStencilStateGetStencilOp` 375, `PolygonStateGetPolygonOffsetEnables` 12 (their formulas are in `reports/analyst-g3-A-1.md`
   and `analyst-g3-B-1.md`). The Bind needs a host record of the object (word layout from the analyst, as in group 3). The vertex methods
   are not known to `nvn_replay.c`; the replay stores the state in a host table for stage 4 (as `NVN_OP_BIND_STATE`).
6. NEXT (after the run of 5): the rest of the texture group (table `reports/analyst-3d-texhandle-1.md`): `SamplerCompare` 2415, the texture and view getters
   (`TextureViewGetLevels` 47, `TextureGetTarget`, `TextureViewGetTarget`, `TextureGetDepth`, `TextureViewGetLayers`),
   `TextureFinalize`, `TexturePoolRegisterImage`, `TextureBuilderSetSize3D`, `TextureWriteTexels`, `TextureFlushTexels`; then
   `ReportCounter` 1968, `ClearDepthStencil` 1678, `CopyTextureToTexture` 10, `ClearTexture`, `BindImage`, the debug labels
   (`SamplerSetDebugLabel`, `MemoryPoolSetDebugLabel`).
Last run (2026-11-04 23:11, `a58387b`): 31 missing functions, listed above. After group 6 the list is empty: then plan stage 4 (shaders,
pipelines, the draw execution) with the user.
Open: the reviewer step of the depth and color view work (optional); method 0x68B and macro 7 (stage 4).

Black screen: expected (no vertex binding, no shader and pipeline, stage 4). Do not search a bug in the present path for it.
Done in 3d: `OLD-WORK_PLAN.md`, the last sections. Open facts: method 0x68B (device+0x1a00 bit 0) and macro 7 (stage 4). A writer
patch can come as UTF-16 (PowerShell redirect): regenerate it with `git diff` in the worktree and `git apply --check` it.

Design of 3c: `docs/nvn-design.md`, "3c Host design decisions", decisions 6 to 13.

The crash is solved. Open steps (one step for each request, the code writer writes the code):
1. Not checked yet: the assumption `NVN_REPLAY_CLIP_LIMIT 0x4000` and the present arguments (`fmt` 1, `layout` 3, `bhl` from
   `tile_y`: used only when no render target is found). The run of `15648e5` had no crash and `faults=0`.
2. Known edges of 2d: a sync that is fenced again while an older token is still pending (the older token only lowers `fences`); a
   sync that is initialized again keeps its record generation; `QueuePresentTexture` does not test `q->channel` for NULL.
3. 2e: `SyncWait` with the waiting states (a condition variable, outside `objs_lock`, never on the GPU thread).
4. The selftest cases S and Y, the mutation `nvn_selftest_mutate` (it must show a fail on a live run) and the first execution test
   (decision 13). S compares the replayed words with the words the library writes (it checks the unverified viewport and scissor
   formulas, the tile factor of the RT width and the clip limit).

3c must also: set a window image back to FREE in the frame job (until then the third `Acquire` stops with "no free window image"),
write the first real texture descriptor (compare it with `nvn_params.c`). The functions that no scene called (`WindowFinalize`,
`SyncFinalize`, the window getters, `SetNumActiveTextures`, `DeviceFinalizeCommandHandle`) follow the rule for unobserved functions.
Also test the `DeviceGetInteger` values outside the 27 known ones, when a safe list exists. The crash after 3b-5b and 3b-6 (a9f5198)
comes from `nvnBufferMap` returning 0, not from a host fault.

Not checked in the reports of 3c: `ProgramSetShadersExt`, the tables of the explorer, the algorithm sections (the viewport and scissor
formulas are in this group). Open in the reports: the range records of `Barrier` bit 3 (types 1, 3, 7), the effect of method 0x3E0,
the store that sets `device+0x18` (value 0x4000 seen in one run) and `device+0x19d8`, and whether the host needs the cache
invalidations of the library entry 0.


## Group 6: the rest of the missing functions (2026-11-05, commits 69b5136 and ae57d56, not pushed)

Method: two analysts (`reports/analyst-g6-A-1.md` and `analyst-g6-B-1.md`), two code writers in two worktrees
(`patches/writer-g6a-1.patch`, `writer-g6b-1.patch`, applied one after the other), the caller read both patches, built and ran
`ctest`. Checked by the caller in the disassembly: `SamplerCompare` (first words), `TextureViewGetLevels`, `ViewGetLayers`,
`TextureGetTarget`, `GetDepth`, `BuilderSetSize3D` (all right) and the three view getters of `ae57d56` (flag bits 2, 3, 4).
Not checked: `ReportCounter`, the clears, the copy, `BindImage`, `WriteTexels`, `RegisterImage`.

What the commits hold:
- `69b5136`: five new ops `NVN_OP_CLEAR_DEPTH_STENCIL`, `CLEAR_TEXTURE`, `COPY_TEXTURE`, `REPORT_COUNTER`, `BIND_IMAGE`
  (payloads in `nvn_cmdlist.h`, `NVN_REPORT_TYPE_MAX 16`, `NVN_IMAGE_BINDS_MAX 16`). The replay does NOT emit methods for them
  (the macro bodies 0xE1E, 0xE04, 0xE26, 0xE0E are unknown): it calls one callback `gpu_op` of `NvnReplayEnv`. The host counts
  in `NvnNativeReplayHost.gpu_ops[]` and keeps the last report and the last image bind. The frame line has `gops=%u`. Stage 4
  must execute these ops. Also: `TextureViewGetLevels/Layers/Target`, `TextureGetTarget/Depth`, `TextureBuilderSetSize3D`,
  `TextureFinalize` (the host drops its record), `TextureFlushTexels` (check only), `SamplerCompare` (pure
  `nvn_sampler_equal` in `nvn_params.c`), and one no-op handler for 11 `SetDebugLabel` names.
- `ae57d56`: `TextureViewGetFormat`, `GetSwizzle`, `GetDepthStencilMode` (the run showed `ViewGetFormat` 6 times).

Facts of the analysts (module sdk, base 0xd3c7000):
- `SamplerCompare` (d83aae0) compares +0x08 (u64), +0x10 to +0x20 (u32), floats +0x24 to +0x2C, +0x30, +0x34, +0x38, +0x40, +0x48,
  +0x4C. It returns 1 for equal. The caller (aa9e550) keeps a cache entry when it is 1.
- `ReportCounter` (d856900): types 0 to 14 write methods 0x6C0 to 0x6C3 with the query word of the table at de87778; type 15 writes
  four short reports; type 16 uses methods 0x004 to 0x007 (query 0x00100002, bit 20: timestamp is a guess). The report write of
  the engine: a non-short report is 16 bytes (64-bit value, 64-bit GPU timestamp), a short report 4 bytes. The read back by the
  game is not traced (the caller is aab179c). The host can write the report itself.
- `ClearDepthStencil` (d84aae0): methods 0x36F (depth clear mode), 0x364 (depth), 0x368 and 0x4E7 (stencil), macros 0xE04 and 0xE26.
  It reads no render target. `ClearTexture`, `ClearTexturei`, `ClearTextureui` have the same code: methods 0x360 to 0x363,
  macro 0xE1E (surface set), 0x3FD (clip), 0x674 (clear), a loop over the layers.
- `CopyTextureToTexture` (d82bfd0): methods 0x6080 to 0x6094, 0x60B5, 0x6223 to 0x6234, shared builder d82c5d0. CORRECTION by the
  caller: x1 to x3 are the SOURCE (the analyst had dst and src swapped; in the Fermi 2D class the low registers are the
  destination). Not checked in the disassembly: check it when stage 4 executes the op.
- `BindImage` (d8491c0) and `BindImages` (d849430): macro 0xE0E with the stage table de864dc, optional 0x68B (when
  device+0x1A00 bit 0), method 0x8E3 (handles at slot*8+0x120). Stage 5 writes a control record (byte 0x0B) instead.
- `SetDebugLabel` d82ccc0 to d82cd70 are one `ret`. `TextureFinalize` is a `ret`. `FlushTexels` only calls the pool cache
  callback (vtable +0x18).
- `TexturePoolRegisterImage` (d83dae0) uses the same descriptor encoder (d858c70) as `RegisterTexture`, with mode 1 and word 2
  bit 24 set. `nvn_texture_descriptor` has no image mode argument yet. It also calls the pool cache callback when the pool
  flag bit 2 is set.
- `TextureWriteTexels` (d83e020 -> d83ddd0): a block linear (GOB) copy, one 2D copy for each depth slice. Row unit
  `8 << [x7+4]` bytes, GOB `512 << (sum of three tile exponents)` bytes, rounding of x with 0xfffffff8 and of y with 0x3f. The
  address comes from d83d01c, the block offset from d822300 and d8228f0. The swizzle index formula is not decoded.
  A view of null becomes a local view with the layers flag.
- Run of 2026-11-05 00:56 (build 2b4af13c, update program): about 900 frames, 30 fps, no crash, `faults=0`, `dropped=0`, 0 JIT
  misses, `gops=47` to 51, no refusal line. Missing at the end: `TextureWriteTexels` 1, `TexturePoolRegisterImage` 1,
  `TextureViewGetFormat` 6 (done in `ae57d56`, not run yet). The screen is black (expected until stage 4).
- Copied analyst items for the old groups: the call counts of the list of 2026-11-04 (SamplerCompare 2415, ReportCounter 1860,
  ClearDepthStencil 1572) are from the build of `72cf639`.

## Group 7: RegisterImage and WriteTexels (2026-11-05, commits 6b8aa06 and bcbbf89, not pushed)

- `TexturePoolRegisterImage` (d83dae0) is `RegisterTexture` (d83d950) with the image mode. The encoder d858c70 gets w4 = 1. Three places
  depend on it (the mode bit is `tst w23, #1`): for a block linear CUBE or CUBE_ARRAY the target code in word 4 is 5 (d858e0c); a
  CUBE_ARRAY depth is not divided by 6 (d858fe8); a CUBE depth is not set to 1 (d8590a8). The end of the function sets word 2 bit 24
  (d83dbc0) where `RegisterTexture` clears it. Host: `NvnTextureDescriptorMode` is the new argument of `nvn_texture_descriptor`,
  handler `h_texture_pool_register_image` shares `texture_pool_register(c, mode)`. The pool cache callback is the same as before.
- `TextureWriteTexels` (d83e020 -> d83ddd0, arguments x0 texture, x1 view or NULL, x2 region, x3 source, x4 and x5 stride overrides
  that d83e020 sets to 0). The Strided variant calls the same function with x4 and x5 (not done, never called).
  - Region u32 fields at +0x00 x, +0x04 y, +0x08 z, +0x0c width, +0x10 height, +0x14 depth.
  - Level is `view->base_level` when the view has the levels flag, else 0. Base layer is `view->base_layer` with the layers flag.
  - Special case: with a view, `view+0x20 == 3` (raw field, no flag test); without a view, the texture target == 3 (1D array). Then
    the first layer is region.y, the layer count is region.height, the row start is 0 and the height is 1 row.
  - `x_start = ceil(x / bw) * bpb`, `src_row_bytes = ceil(width / bw) * bpb`, `row_start = ceil(y / bh)`, `rows = ceil(height / bh)`,
    source layer stride `src_row_bytes * rows`. The source holds only the region. The column index in the source is `x - x_start`
    (d8232a0 sets the pointer to `src + (x_start & ~63) - x_start`, d8233ec to d8233f0).
  - The destination is `[tex+0x10]->vtable[0x28]() + [tex+0x18]` plus `layer * layer_stride` plus the level offset. There is no level
    offset table: the level offset is the sum of the sizes of the earlier levels (the loop of `texture_footprint`). No pool cache call.
  - The writer d8232a0 gets x0 destination, x1 source, x2 and x3 first and last byte of x, w4 and w5 first and last row, x6 source
    row stride, x7 the tile record, and on the stack the GOBs across (rounded to the tile multiple). It places the bytes in the
    standard Maxwell GOB layout (x offsets 0, 16, 32, 48 go to 0x000, 0x020, 0x100, 0x120). The library has NO range check.
  - The block stride in the library uses three exponents (`[x7] + [x7+4] + [x7+8]`). `[x7]` is 0 in this path, so `bl_offset` is equal.
- Host: `nvn_texture_level` (offset, size, GOBs across, block rows, trimmed block height exponent of one level) and
  `nvn_texture_write_rows` in `nvn_params.c`, `h_texture_write_texels` in `nvn_native.c`. `texture_footprint` now calls the helper
  `texture_level_parts`. Refused with one log line: linear, BUFFER, 3D, multisample, a view with the format flag, a level or layer
  outside the texture, a region outside the level, an unreadable source, an unwritable destination.
- The 3D case needs the slab math of d83d2cc to d83d308 and the trimmed z exponent (`ez` of `texture_level_parts`, the third tile
  exponent). The first run (below) shows that the one call of the game is a 3D texture.
- Lessons: the code writer made one error in the patch of WriteTexels (source column index `x` instead of `x - x_start`, and the
  readable range of the source) and the test copied the same error, so the test passed. Always run a mutant: change the test
  expectation back to the wrong form and see that the test fails. The explorer report was good in the facts and weak in what it did
  not trace: read the pointer set-up of the helper yourself. The line ends: the blobs in git are LF, the files in the working tree
  are CRLF through `.gitattributes`, so `file` shows CRLF and `grep -c` shows 0. `git apply` and `sed -i` work.
- Run of 2026-11-05 10:22 (build `bcbbf89`, md5 acd321e1, update program): 990 frames, 30 fps, no crash, `faults=0`, `dropped=0`,
  `missing=0`, `gops=47`, 0 JIT misses, 256 NVN functions without a handler and 0 of them called. `RegisterImage` ran once, no log
  line. `WriteTexels` ran once and was refused: `texture=c294288 is 3D` (t=2.35 s). The screen is black (user), as expected.

## Notes moved from the work plan on 2026-11-05

- `tui-idle` can return before the agent starts and before its last edit: wait until the patch file is stable for 45 seconds and the
  agent is idle. The writer cannot build `nvn_native.c` alone (generated headers), and its patch had errors in 3b-6a: read it fully.
- Open: that no frame is paced twice (host `Acquire` paces, the vi dequeue paces queued layers) is not checked. The success path of
  `Acquire` logs nothing: add a counted log line in 3c if the number of calls is needed.
- Check the two `nvnBufferBuilderGetSize` calls whose x3 looks like a stack address (see "3b-2 Result"): read the disassembly.
- Stage 4 step 1 (2026-11-05): the explorer report `explorer-prog-bind-1.md` and the analyst report `analyst-prog-builder-1.md` are in
  the scratchpad `reports/` folder. Their facts are in `WORK_PLAN.md`, "Next", item 2.


## Stage 4 step 1: program bind and word builder (2026-11-05, commits 72254f8 and 8c2c1df, not pushed)

Moved from `WORK_PLAN.md` on 2026-11-05. The 0x2004 branch and stage 5 were read later: explorer reports `explorer-prog-2a.md`
and `explorer-prog-2b.md` (checked in the disassembly: d854ac4 to d854f30 for the branch, d8543c0..d854444 and d836f10..d837168 for
the compute block). The compute block is 18 data words, first in the array. Word 17 and the upper flag bits are 0 (the library
leaves stack content). Compute with graphics is refused (the library crashes: d854414 sets x0 = 0).

   Step 1 is explored (report `reports/explorer-prog-bind-1.md`, 3 claims checked: BindProgram d8474d0 copies the word array
   `[prog+0x10]` x `[prog+0x20]` into command memory and writes no method itself; SetShaders stores them at d855124/d85510c; p2 =
   `entry_word - low32([[device+0x1af8]+0x18])` at d836bd4). The array holds the macro 0xE18 bind (methods 0x801/0x811..0x851, const
   buffer 0x8E0..0x8E2, 0x904+8g), the fragment config (0x084, 0x3C7, 0x3D0, 0x489, 0x65B, 0x1D5, 0xD8/0xD9, 0x2004) and transform
   feedback (0x1C0..). BindProgram adds the stage-disable words (d855610). The region base 0x582/0x583 comes from d85f0d0 (no direct
   caller: guess, at queue submission). Open decisions for step 1: (a) how the host builds the word array (port the builder
   d854460..d854f10 from the header, needs an analyst for the blob fields; or call the original SetShaders on a scratch object),
   (b) the host value of the region base 0x582/0x583 (the library subtracts only the low 32 bits, so a base with low32 = 0 inside
   the 0x400000000 window works if all shader pools lie in one 4 GiB window), (c) that `ProgramRec` keeps the array.
   DECIDED (user, 2026-11-05): (a) port the builder. Analyst report `reports/analyst-prog-builder-1.md` (builder d8543a0..d85532c;
   game case: vertex 8 words, vertex+fragment 25 words, no VertexA, no 0x2004, no transform feedback, all three are guesses).
   Checked against the disassembly: the vertex p2/p4, the fragment 0x084/0x3C7/0x3D0/0x65B/0x489 words, the 0xD8 block, the TF size
   formula, the two 0x489 tables (full header words 80020489 800a0489 80010489 80090489 and 80010489 80090489 80010489 80090489).
   ERRORS of the report: the header words with 7 hex digits lack a zero (the base is w8 = 0x80000489, d854468): the true words are
   0x80000853, 0x80000084/0x80010084, 0x800003c7, 0x800003d0/0x800103d0, 0x8000065b, 0x80000813. NOT RESOLVED: the 0x1D5 word
   (`csel w8, w9, w8` at d85509c: the w9 value and the real payload; 0x803001d5 has the count field 0x30, not 3).
   RESOLVED (2026-11-05, disassembly d854930, d855094..d85509c): w9 = 0x800101d5 (set at d854930, no write on the path to d85509c).
   The word at +0x1c is `[prog.cfg+0xac] == 0 ? 0x800101d5 : 0x803001d5`. Mode 4 (bits 29..31 = 100) is immediate data: bits
   16..28 are the VALUE, not a count. So the words are method 0x1D5 with the value 1 or 0x30, and no payload word follows. The
   same holds for all the 0x8000xxxx words of the builder. Next session: brief for the code writer (ProgramRec keeps the array,
   a pure builder in `nvn_params.c`, tests), with this note.
   BLOCKER RESOLVED (2026-11-05, report `reports/explorer-hdrgetters-1.md`, 5 slots checked: 0x148, 0x188, 0x1a0, 0x58, 0x190).
   The host `header` pointer is the object `h` of the getters (the host already reads the stage at h+0x714). Getter -> header field:
   0x38 h+0x6f8, 0x40 h+0x6fc, 0x48 h+0x700, 0x50 h+0x704, 0x58 h+0x708, 0x60 h+0x70c, 0x68 h+0x710, 0x70 h+0x714 (all u32);
   0x148..0x170 u8 at h+0x718..0x71d; 0x178 u8 h+0x748; 0x180 u8 h+0x749; 0x188 u8 h+0x74a; 0x190 u32 h+0x720; 0x198 u32 h+0x734;
   0x1a0 = ADDRESS h+0x724 (16 bytes, no load); vertex stage (0): 0x2b8..0x2e0 u32 at h+0x748, 0x74c, 0x750, 0x754, 0x758, 0x75c.
   The same slots hold in all nine vtables. The u8 getters return the raw byte (the caller tests nonzero). Unresolved and not needed
   for the game case: stages 2..5 getters (0x1a8..0x378). The record fields rN+8/+0xc/+0x18 = h+0x708/0x700/0x70c (+delta).
   DEVICE WORDS MEASURED (2026-11-05, run with `nvn_native=0`, device 1448bf010; the host has no device object, so they become
   constants) [DONE 2026-11-05: commit "Build the Maxwell word array of a program in nvnProgramSetShadersExt": pure builder
   `nvn_program_build_words`, `ProgramRec.words`, region base 0x400000000; stages 2..5, VertexA, the 0x2004 branch (g158 != 0 and
   g160 == 0) and transform feedback are refused; build `ed01bdcf` copied; the user has not run it. NEXT: step 1c, the replay
   of `ProgramRec.words` at BindProgram (stage-disable words d855610, region base 0x582/0x583 d85f0d0), needs a look at how the
   Maxwell layer takes methods]. RUN 2026-11-05 11:55 (build ed01bdcf): 56 programs words=25, 8 programs words=8, 3 refused for the
   0x2004 branch (144996a90, 1449d2f68, 913a8d6b0), then a COMPUTE program (stage 5, 14846d240) refused at 4.26 s and the game
   crashed at 4.27 s (null + 0x28, game function 87828bc, because SetShaders returned 0). So the game DOES use the 0x2004 branch
   and stage 5; both guesses were wrong.
   DONE 2026-11-05: commit 8c2c1df builds both (reports `explorer-prog-2a.md`, `explorer-prog-2b.md`, checked in the disassembly;
   compute = 18 data words first, word 17 and the upper flag bits are 0, compute + graphics is refused because the library crashes).
   Build c5fdf63b copied; the user has not run it. NEXT: ask the user for one run (update program, wait 10 s, close), read the
   `words=` lines and the refusals. Then step 1c, the replay of `ProgramRec.words` at BindProgram (stage-disable words d855610,
   region base 0x582/0x583 d85f0d0). Still refused: stages 2..4, VertexA, transform feedback. The compute bind uses a control
   record 7, not the array (`explorer-prog-bind-1.md`).
   Device words: +0x8c=0x80, +0x90=0x503, +0x94=2, +0x98=1, +0xa4=0, +0xe0=1, +0xe4=0x1000, +0xe8=1, +0x1a38=0x087f6080 (matches the
   DeviceInitialize rule: [+0x90]==0x503), +0x1a3c=0, +0x1a40=0, +0x1a48=2, +0x1a68=0x20, +0x1a70=1, +0x1a74=0x20806080, +0x1af4=0.
   Note: with `nvn_native=1` the trace reads the queue builder +8 and gets a code address (the host builder has no device): use
   `nvn_native=0` for device words. The trace list in `nvn_trace.c` is extended (uncommitted). NEXT: write the brief.

## Stage 4 step 1c: replay of the program words (2026-11-05, commit 4c279e4, not pushed)

Moved from `WORK_PLAN.md` on 2026-11-05. Last read run before 1c (build c5fdf63b, 14:44):
- Last read run: 2026-11-05 14:44 (build c5fdf63b, update program): 31 s, 30 fps, clean `Exit.`, no crash, 0 JIT misses, 0 NVN
  functions without a handler called, no refusal line. The 0x2004 program 144996a90 now builds (`words=30`). The earlier crash at
  4.27 s (compute program 14846d240) did not happen. BUT the log has exactly 64 `nvn-native: program` lines: they hit
  `PROGRAM_LOG_LIMIT` (64, nvn_native.c:734), and the success lines count in it. So it is NOT proven that the compute program was
  set. The screen is BLACK (expected until stage 4).

Step 1 history (1a, 1b, 1c, the design, the explorer answer on d855610, the run of 1c):
   Step 1 (program bind): 1a DONE (the builder, commits 72254f8 and 8c2c1df; facts in `OLD-WORK_PLAN.md`, section "Stage 4 step 1:
   program bind and word builder"). 1b NEXT: log summary (below). 1c: replay of `ProgramRec.words` at BindProgram (stage-disable
   words d855610, region base 0x582/0x583 from d85f0d0). The compute bind uses a control record 7, not the array. Still refused:
   stages 2..4, VertexA, transform feedback.
Step 1c CODED (commit after 686affd, build md5 fb2437e8, copied to the three program copies; ctest passes except `tracked_material`; a
mutant of the decoder and of the disable table fails the tests). RUN READ (2026-11-05 17:37, build fb2437e8): no crash, `refused=0`, no replay fault, 0 JIT misses, clean Exit. BUT only
3 frames in 28 s (0.2 fps, before: 30 fps). Cause (log + source): the first macro call (`mme: macro 12 at word 0, 0 words uploaded
together, hash 0`) runs EMPTY macro code. `gpu: macro on channel 20 ran 5000000 instructions and is cut off` (WARN_LIMIT 4, so more
follow) and each call burns 5M steps; the frame job is slow, the presented images are not freed, `nvnWindowAcquireTexture failed: no
free window image (states=2,2,0,0)` 778 times. Mechanism: the game library uploads the macro code to the MME instruction RAM
(methods LOAD_MME_INSTRUCTION_PTR/LOAD_MME_INSTRUCTION, `mme_upload` mme.c:185) at device or queue setup, and native mode
(`nvn_native=1`) skips it, so `ch->mme_code` is empty. 1c exposed it, 1c itself is not wrong. NEXT SESSION (decide with the explorer):
find who uploads macro 12 (0xE18 bind macro, and macros 0xE1E, 0xE04, 0xE26, 0xE0E, 7 of group 6), where the code lives in the library
(data table in the sdk module), and upload it from the host at channel setup. Idea: the host needs the macro code of the library anyway
(the Maxwell layer has native forms by hash, `mme_native_find`). Do not tune the step limit. Facts: mode 5 is INCREASE_ONCE (first word to
the header method, the rest to method+1, as `pb_decode`); the caller's first brief said "non-incrementing" and was wrong, fixed.
The 0x821 value is sent as 0 (`NVN_PROGRAM_DISABLE_TCS_OFFSET`). Not checked by the user run alone: that the stream is accepted by
the Maxwell layer as the library would send it. NEXT after the run: step 2 (state to methods).
Step 1b DONE (commit 686affd, run 2026-11-05 15:xx, build 7e768b74): 3840 sets, 0 refusals, masks 1:8 and 3:3832, then one set
stages=20 words=18 (the compute program). 50 s, 30 fps, no crash, clean Exit. The summary comes every 128 sets (30 lines at start,
no exit hook, so the later masks are not counted in a summary). NEXT SESSION: step 1c. Design (read 2026-11-05, not coded):
(1) the graphics words are a pure pushbuffer stream (headers mode 1/4/5, see `pb_decode` gpu.c:442); the compute block is plain data, so
a compute bind (stage mask bit 5, control record 7) does NOT replay the array. (2) The replay thread already calls `maxwell3d_method`
(`replay_host_method`, nvn_native.c:2783) but with last=1 for each word: a macro call (0xE18, 5 words) needs last=1 only on the LAST
word, so add a decoder in `nvn_params.c` (pure, tested) that gives (method, value, last). (3) Copy the words INTO the op at record
time (`h_command_buffer_bind_program`, as the library copies them into command memory at d8474d0): `NvnOpBindProgram` gets `words[]`,
`word_count` and the program stage mask (max payload 512 bytes, 25 words fit). This avoids the lifetime problem of a program that is
finalized before the list is replayed. (4) In `replay_host_bind_program`: send the words, then the stage-disable words for each
requested bit not in the program mask (0x850=0x50; 0x840=0x40 and 0x47C=0; 0x820=0x20, 0x821=<device address>, 0xD1E=0; 0x830=0x30).
ANSWERED (explorer report `reports/explorer-1c-1.md`, d855610 checked by the caller 2026-11-05): d855610(x0 = command pointer, x1 = device,
w2 = bits) writes in ORDER bit3, bit4, bit2, bit1 (bit0 vertex: none). Words (pushbuffer, header then data): bit3 = 0x20020820 (mode 1,
2 words), 0x20, then 0x821 value, then 0x80000D1E (imm, data 0); bit4 = 0x80300830; bit2 = 0x80400840 and 0x8000047C; bit1 = 0x80500850.
The 0x821 value = u32 [device+0x994] + u32 [[device+0x1b00]+0x120] (a device field, the host reads it from the guest device object).
BindProgram order: array copy first, then the 0x12 record if [prog+0x28], then d855610 for requested bits not in [prog+0x24]. Compute
(bit 5) = control record 7 only, no words. There is no "current program" field in the command buffer. d85f0d0 (region base
0x582/0x583 = u64 [[device+0x1af8]+0x18], then 0xD27, 0xD23) has no direct caller (guess: runs at flush), so send it once at the
replay start. The explorer's claim that the order is not ascending is verified.
(5) Region base 0x582/0x583 = 0x400000000 (`NVN_SHADER_REGION_BASE`): send it once at the first bind or at the replay start (the
library does it at d85f0d0, caller unknown). Then ask the user for a run: no crash, `refused` stays 0, no Maxwell warning.

## Notes moved from the work plan on 2026-11-05 (end of the session of task A step 1)

- The task "remove the missing functions" is DONE (groups 1 to 7, 2026-11-05). The run shows `missing=0`.
- DECIDED (user, 2026-11-05): the 3D `WriteTexels` goes to stage 4 (step 5). The renderer stays the existing Maxwell layer
  (shaders and pipelines move in phase 9, `docs/nvn-plan.md`).
- Stage 4 step 1 (program bind) is DONE (1a builder, 1b summary, 1c replay, commit `4c279e4`; history in section "Stage 4 step 1c").
  Still refused: stages 2..4, VertexA, transform feedback.
- Task A step 1 (explorer, 2026-11-05): the builder sdk 0xD84F800 (called by nvnQueueInitialize 0xD839360) writes the initial command
  buffer of the queue (queue+0x16D8, subchannel 0) with 38 macro slots (slot n = method 0xE00+2n; order per macro: 0x47 slot, 0x48
  start, 0x45 pointer (mode 5), words by 0x46; one batch per macro). Slot 12 (0xE18) = 28 words, hash eb29b2a09aa06d38 (mme_hle
  "program bind"). Slot 15 (0xE1E) = 78 words, hash bcfd89165d030f2f, no native form. Words are in sdk tables (0xDE06xxx to
  0xDE08xxx, 0xDE86xxx, 0xDE87xxx) and, for slots 0, 4 to 9, 13 to 21, partly built from immediates in the builder. Device byte +0xB0
  bits 4 and 0 select longer forms of slots 1, 2, 10, 11, 16 (setter not found; bit 4 = 0 proven by the hash). Report:
  `reports/explorer-taskA-1.md`.
- Cancelled by the user: the Area filter and FSR1 (`fsr_sharpness`). The work on the jaggies stopped.

### Task A: MME macro code (done 2026-11-05, commit bebc670)
- Cause: native mode skipped the library builder sdk 0xD84F800 (called by nvnQueueInitialize), so `ch->mme_code` was empty and each macro call ran 5M steps.
- Fix: `h_queue_initialize` runs the builder on a scratch queue/device image below the guest stack (x0 = queue, w1 = 0, device+0xB0 = 0), keeps the 810 words in QueueRec, and the first frame job replays them with `maxwell3d_method` (decoder `nvn_pushbuf_decode`). Run: 736 methods replayed, macro 12 hash eb29b2a09aa06d38 (program bind), 30 fps, no cut off. Other macros (0xE1E, 0xE04, 0xE26, 0xE0E, 7) were not called yet in this run; bit 0 of device+0xB0 is still unproven. Reports: `reports/explorer-taskA-1.md`, `explorer-taskA-2.md`.

## Stage 4 step 2a: the methods of the Bind functions (2026-11-05, analysis only, no commit)

Moved from `WORK_PLAN.md` on 2026-11-05. Brief `reports/brief-analyst-s2-A.md`, analyst table `reports/analyst-s2-A-1.md` (12 functions:
BindColorState d8472e0, ChannelMaskState d847340, DepthStencilState d8473a0, PolygonState d847400, MultisampleState d847460, BlendState
d858670, VertexAttribState d855340, VertexStreamState d8554b0, VertexBuffer d8479d0, UniformBuffer d847b70, StorageBuffer d847f70, Texture
d8483b0). The log addresses match the sdk image (no +0x45000 shift). Checked in the disassembly by the caller: DepthStencil (d8585e0),
ChannelMask (d858850), UniformBuffer (d847b70): all correct.
Facts: no row reads device+0x1A00. None of the methods is sent by `nvn_replay.c` today. VertexBuffer writes the start (0x701+4i) and the limit
(0x7C0+2i) and no stride (the stride comes from the stream word 0x700+4i). Uniform, Storage and Texture write a control record (types 12, 13,
8) and no 3D method for stage 5 (compute). Stage to group map {0,4,3,1,2}. Uniform: H(0x8E0,3) size rounded to 256, addr hi, addr lo, then
I(0x904+8g, valid | ((slot+3)&0x1F)<<4). Texture: I(0xE0E, map[stage]), [cb+0x70 bit4] I(0x68B,0), H(0x8E3,2) slot*8+0x20, low32(handle).
Not decoded: BlendState with advanced mode (d845cd0), the MultisampleState sample-location tables (0xe015658, 0xe015698 are zero in the file),
macros 7 (0xE0E) and 16 (0xE20).
Older State text of the work plan (before this session): last read run 2026-11-05 19:12 (build 5b2ab5be, update program): no crash, 0 JIT
misses, `faults=0`, no `cut off`, 30 fps, clean `Exit.`. Task A is DONE (see the section above). The screen is BLACK (expected until stage 4).

## Stage 4 step 2b: pure builders of the bind packets (2026-11-05, commit 2ce700f, not pushed)

Moved from `WORK_PLAN.md` on 2026-11-05. Brief `reports/brief-2b.md`, report `reports/2b-1.md`, patch `patches/2b-1.patch` (the worktree is removed).
Decisions: the host sends the methods at draw time from its tables (2c). The pure functions `nvn_bind_<kind>_words` in `nvn_params.c` build the
SAME pushbuffer words as the library (headers and data, library order), and 2c feeds them through `nvn_word_stream_next` and `maxwell3d_method`.
Done: color, channel mask, depth stencil, polygon, blend (advanced mode 0 only), vertex attrib (the host gives the format words and uses
`nvn_vertex_attrib_format_index`), vertex stream, vertex buffer, uniform, storage and texture buffers (stage 0 to 4). Refused (return 0, the host
logs the cause): advanced blend, polygon mode 3, stage 5 (compute), uniform slot >= 32, storage and texture slot >= 128, vertex index or count > 16,
an address sum that wraps. Not written: Multisample (sample locations not decoded). Texture takes `descriptor_flag` from the caller.
Checked: the caller read the whole patch, `ctest` passes except `tracked_material`, three mutants failed 6 test checks. The exe is not rebuilt
for the game (no host code uses the builders). Open for 2c: the source of the descriptor flag (`device+0x1A00` bit0 per group 6, or `cb+0x70`
bit4 per 2a), the guest format table `[0xe03fd40]` (this image `0xe029028`, entry 0x30, +0x14), the texture constant `0x20` (BindImage uses `0x120`).

## Stage 4 step 2c: analysis part (2026-11-05, moved from WORK_PLAN.md)

Descriptor flag found (explorer `reports/explorer-2c-flag-1.md`, the caller checked two points in the disassembly): `device+0x1A00` is the
constant 1 (16 bytes at 0xde06680 = {0,0,1,0}, stored by d8310A0 at device init; the option callback d833320 returns null). d823CF0 copies it
to `cb+0x70` bit 4 (`bfi` at d823d28), no writer clears it. So `descriptor_flag` = 1. Method 0x68B is PipeNop (Switchbrew), no host action.
Macro 7 (method 0xE0E) is `cb_select` (hash 0xD7333D26E0A93EDE, `mme_hle.c`): it selects the constant buffer of the stage, then 0x8E3/0x8E4
write the handle at slot*8+0x20 (texture) or slot*8+0x120 (image). Open (not needed now): the writer of shadows 0xD2A/0xD2F, the reason of
the conditional PipeNop, the option 0x9abdc5. The brief of the code writer is `reports/brief-2c.md`.

## State text before the session of 2026-11-05 (moved from WORK_PLAN.md)

- Commits not pushed after 7d7532e: 2ce700f (2b), bebc670, 4c279e4, 686affd. The exe is the build of bebc670 (md5 5b2ab5be, equal to the three
  program copies). The `.ini` of the game has `nvn_trace=1`, `nvn_native=1`, `nvn_selftest=0`.
- Last read run: 2026-11-05 19:12 (build 5b2ab5be): clean, 30 fps, black screen (expected until stage 4).
- Stage 4 steps list: (1) program bind (done), (2) state to methods (2a, 2b done, 2c next), (3) execute draw and dispatch, (4) the five ops
  of group 6 (macros 0xE1E, 0xE04, 0xE26, 0xE0E, CopyTextureToTexture source order), (5) renderer notice for the CPU writes of WriteTexels plus
  the 3D case, (6) open method 0x68B and macro 7 (0x68B and macro 7 are explained by 2c, see above).

## Stage 4 step 2c: code part (2026-11-05, commits after 2ce700f, moved from WORK_PLAN.md)

Code writer patch `2c-1` (dirty flags per group, `replay_bind_send_*` at `replay_host_draw`), read in full, ctest passed, mutant of
`nvn_highest_enabled_index` killed. Run 21:13: 41 methods, 0 graphics refusals, 46 stage 5 refusals (the draw sent compute binds), 808
multisample lines. Fix `e6be052`: draw covers stages below `NVN_BIND_STAGE_COMPUTE`, `LOG_LIMIT` for multisample. Run 21:18: clean.

Old text of the State and Next blocks:

- Last commit: see `git log` (2c code, send of bind state at draw). The exe is the build of that commit (md5 bc0ad3ac, equal to the three program copies in
  `game/scarletnx/bin/` and `game/recompiled/01001F5010DFA000/`). Check `git log` and `git status` first. The commits after 7d7532e
  are not pushed. `ctest` passes except `tracked_material` (known; `driver_workflow` can fail in a parallel run). The `.ini` of the
  game has `nvn_trace=1`, `nvn_native=1`, `nvn_selftest=0`. After each build: `tasklist` shows no game, then copy the exe.
- Last read run: 2026-11-05 19:12: clean, 30 fps, black screen (expected until stage 4). Older state text: `OLD-WORK_PLAN.md`.

2. Stage 4 steps, one per session, a user run after each: (1) program bind (done), (2) state to methods, (3) execute `draw` and
   `dispatch` (first picture), (4) the five ops of group 6 (macros 0xE1E, 0xE04, 0xE26, 0xE0E, `CopyTextureToTexture` source order),
   (5) renderer notice for the CPU writes of `WriteTexels` plus the 3D case. (Step 6, method 0x68B and macro 7, is explained: no host work.)
   Step 2: 2a (analysis), 2b (pure builders, `2ce700f`) and the flag analysis of 2c are DONE (facts: `OLD-WORK_PLAN.md`, sections "Stage 4
   step 2a", "2b", "2c"). The descriptor flag is the constant 1.
   2c code part is DONE (commit after `2ce700f`, ctest passes except `tracked_material`, mutant checked). Run 21:13 read: clean, 30 fps, 41 methods, 0 graphics refusals, 46 stage 5 refusals and 808 multisample lines (both fixed in e6be052). NEXT: ask the user for one run
   (any scene that draws, 10 seconds), read the log with `nx-log-read`: the line `draw recorded, not executed ... bind methods=N
   refusals=M` and the `bind ... refused` lines. Known edge: a vertex buffer bind of size 0 is not sent (decision 7).

## Stage 4 step 3: draw words (2026-11-05, commit 9a32410, not pushed, moved from WORK_PLAN.md)

   Step 3 analysis (done 2026-11-05, from `reports/analyst-g4-draw-1.md` and `mme_hle.c`): the macro code is already on the
   private channel (commit bebc670), so `replay_host_draw` can send the SAME macro method and parameter words as the library:
   DrawArrays `D(0xE32,3) mode,first,count`; DrawArraysInstanced `D(0xE36,5) mode,count,instances,first,baseInstance`
   (`0xE44` and a pre-word `I(0xE20,0x190)` when baseInstance != 0); DrawElements `D(0xE38,5) mode,addrHi,addrLo,type,count`;
   DrawElementsBaseVertex as the report row (base vertex 0: `H(0x446,1),0; H(0x50D,1),0` before and `I(0x50D,0); I(0x446,0)`
   after); DrawElementsInstanced `I(0x5F6,type); H(0x5F2,2) hi,lo; D(0xE3C,6) mode,count,instances,0,baseVertex,baseInstance`
   (`0xE46` with the `0xE20` pre-word when baseVertex|baseInstance != 0). `mode` and `type` go unchanged (no enum conversion).
   Dispatch: no Maxwell method; the library writes a control record only. Compute needs a new path (QMD, subchannel 1), not
   yet designed. Open points for the brief: (a) what `I(0xE20,0x190)` does (the explorer must read it), (b) the MME scratch
   0xD1B masks the instance count: if it is 0 after the queue init, instanced draws draw nothing (check it first), (c) the
   `0x8E3` constant buffer writes of the `_cb` macros need the bound constant buffer, (d) compute stays log-and-skip in step 3.
   Explorer answers (`reports/explorer-3e-1.md`, not yet checked by the caller): (a) `I(0xE20,0x190)` is macro 16
   `cb_select_offset` (in `mme_hle.c`): it selects the constant buffer of scratch 0xD18 and sets the write offset to 0x190*4 =
   0x640. The `_cb` macros then write base vertex and base instance at 0x640/0x644. (b) The queue builder writes 0xD1B =
   0xFFFFFFFF, and the first frame job replays it (log: 736 methods), so the mask is not 0. (c) Nobody writes 0xD18 (the
   explorer found no writer in sdk or main): if it stays 0, the `_cb` macros write to GPU address 0x640. Check in the brief:
   where 0xD18 is set (the driver constants buffer of the device?), and send the same words so that the macro runs as in the game.
   Check of (c) done: the queue builder words (`analysis/qrec_00.bin`) hold only 0xD19, 0xD1A, 0xD1B. `nvn_native.c` and the
   host never write 0xD18. A scan of the sdk image for the immediate 0xD18 gave nothing, but the scanner (`analysis/scan_*.py`)
   found nothing for 0xD1B either, so it is not trusted. Decision: do not search more. The brief makes `replay_host_draw` log
   the value of r3d[0xD18] at the first draw that uses a `_cb` macro (0xE44, 0xE46) and refuse that draw with a log line when it
   is 0 (no write to GPU address 0x640). The user run then shows the real value.
   Step 3 committed as `9a32410` (draw words sent to the macros; ctest passes except `tracked_material`; mutant caught; worktree
   removed). NEXT: ask the user for one run (title scene, `nvn_native=1`, update program, wait 10 seconds, close). Read the log with
   `nx-log-read`: the line `first draw sent ... r3d[0xD18]=`, `bind draw ... refused` lines, the crash block, `mme:` cut off lines.
   Then the next step: dispatch (compute, QMD, new path), then steps 4 and 5. Known edges of step 2: a vertex buffer bind of size 0
   is not sent (decision 7).
   Run of step 3 (2026-11-05 22:11): clean, 30 fps, no crash, 0 JIT misses, `first draw sent, bind methods=44 refusals=0
   r3d[0xD18]=00000000`, no refusal line, no `not implemented`. No `_cb` draw was refused, so the game did not use a form with
   base vertex or base instance in this scene. 0xD18 stays 0 (unexplained writer). Black screen (expected).

## Stage 4 step 3b analysis: dispatch (2026-11-05, moved from WORK_PLAN.md)

   Step 3b, `dispatch` (compute). The library writes only a control record `{0x0E, x, y, z}` (no Maxwell method), so
   the host needs a new path: QMD on subchannel 1, the compute stage 5 binds that stay dirty (`replay_bind_send_all` skips
   them), and `replay_host_dispatch` (now log and skip). Analysis DONE (2026-11-05, `reports/explorer-3b-1.md`; claims 1 and 2
   checked in the disassembly: record 0x0E written by `d84da90`, table entry `d839df0` -> `d84f010`). Facts: the guest library
   turns the record into methods at submit time (`d84f010` fills the QMD image at `cb+0x76c` from the bound program and the
   stage 5 binds, `d84e520` writes the uploads `I(0x44)`, `I(0x87)`, `H(0x60,4)`, `I(0x6C)`, `H(0x6D,n)`, then `H(0xAD,1)`
   QMD address>>8 and `I(0xAF,3)` launch). CODE_LOC (0x582/0x583) is not in that path. The host path bypasses `d84f010`.
   The host runs compute methods with `compute_method(ch, m, v, 1)`; `vk_dispatch` is real and reads grid, block, shared,
   program start and the const buffers 0 to 7 of the QMD. Design decided by the user (2026-11-05): (A) the host builds the QMD
   itself. Second analysis DONE (`reports/explorer-3b-2.md`, 3 rows checked: QMD words 0x8, 0xC, 0x11, 0x12, 0x1E): all QMD
   words, cbuf 0 (driver block: uniform table 0x210, storage table 0x310), cbuf 1 (program data), uniform slot N = cbuf N+3,
   CODE_LOC written by `d85f0d0` (not by the dispatch). `d839d20` is the submit interpreter; `cb+0x11c` is always 0.
   Brief for the code writer is WRITTEN: `reports/brief-3b-writer.md` (placeholder `<WORKTREE>`). Decisions in it: pure QMD
   builder in `nvn_params.c`, scratch arena from `kmem_alloc` mapped with `nvn_gpu_map_range`, compute program words carried
   in the bind op (18 words), refusal of stage 5 textures in v1, gpr/sass bytes 0 (open). NEXT: make the worktree
   (`git worktree add --detach` in the scratchpad), replace `<WORKTREE>` in the brief, launch the code writer (opencode), wait,
   read the whole patch, `git apply --check`, build, `ctest`, mutants. Then the user run needs a scene with a compute dispatch
   (unknown which one: the log line `first dispatch sent` or the refusal count tells). Then steps 4 and 5.
   Reports: `reports/explorer-3b-1.md`, `reports/explorer-3b-2.md`. Brief: `reports/brief-3b-writer.md`.
   QMD words (base `cb+0x76c`, checked: 0x8, 0xC, 0x11, 0x12, 0x1E): 0x8 code_offset; 0xC x; 0xD y|z<<16; 0x11 shared
   `value & 0x3FF00`; 0x12 block x<<16; 0x13 block y|z<<16; 0x14 enable mask; 0x1D+2i const buffer i; 0x2D to 0x2F local
   allocs. Uniform slot N uses const buffer N+3 (N 0 to 4). Const buffer 0 = driver block (size 0x500, uniform table 0x210,
   storage table 0x310, 16 byte entries), const buffer 1 = program data at region base + cb_offset, const buffer 2 = texture
   handle block. CODE_LOC is sent by `d85f0d0` (queue callback), not by the dispatch. `d839d20` is the submit interpreter.
   `cb+0x11c` has no setter, so `d84e520` is the path. Open: gpr and sass bytes (`x21+0x823`, `x21+0x82b`), the rest of
   the 0x230 byte block, the cbuf 2 texture block (v1 refuses stage 5 textures), the `d84eab0` launch words.


## Stage 4 step 3b done: dispatch (2026-11-06, moved from WORK_PLAN.md)

   Commits: `3f4399e` (host builds the QMD and the cbuf 0 block, writes both into one mapped arena page, sends 5 compute methods),
   the refusal log (reason code, printed again only when a value changes), `59c7e17` (slot rules). User run 2026-11-06: clean, 30 fps,
   `first compute dispatch sent x=1 y=1 z=1 qmd=42a15f000 program=29dc7000`, `dispatches=1 dispatch_refusals=0` each frame.
   First run: every dispatch refused, uniform slots 5, 6, 7, 8, 12 bound (mask 0x11e0, slot 5 size 0x140). Cause (disassembly of
   `d84dec0`, the compute BindUniformBuffer): the library writes every bind to the cbuf 0 table at `0x210 + N*16` (no limit of N), and
   updates the QMD cbuf only when N+3 <= 7 (`d84dee4`: `cmp w10,#7; b.hi`). It enables the QMD cbuf when size+0xFF has a bit above 0xFF
   (size >= 1; `d84df00`) and stores the size rounded up to 0x100. The tables have 16 entries (uniform 0x210, storage 0x310).
   The first analysis said "N 0 to 4 only, size >= 0x100": that was a wrong reading. Open: gpr and sass bytes (0), the rest of cbuf 0
   (0), cbuf 2 textures (refused). One QMD arena slot is enough: the compute method runs on the same GPU thread (`compute_method` call).
   Files: `nvn_params.c` (`nvn_compute_qmd_check`, `_build`, `nvn_compute_cbuf0_build`, `nvn_program_compute_from_words`),
   `nvn_native.c` (`replay_host_dispatch`, arena `dispatch_scratch_get`).

## Stage 4 step 4a done and state text of 2026-11-06 (moved from WORK_PLAN.md on 2026-11-07)

Step 4a (commit `0ea245e`): `ReportCounter`, `BindImage` and `ClearDepthStencil` send their methods. Pure builders in `nvn_params.c`
(`nvn_report_counter_words`, `nvn_bind_image_words`, `nvn_clear_depth_stencil_words`, `nvn_clear_depth_stencil_nothing`), host
functions in `nvn_native.c` (`replay_host_report_counter`, `_bind_image`, `_clear_depth_stencil`, called by `replay_host_gpu_op`).
Method: analyst `reports/analyst-s4-4.md` (OpenCode, DeepSeek V4.1 Flash), brief `reports/brief-s4-4.md` and `brief-s4-4a.md`, patch
`patches/s4-4a-1.patch`. The caller checked in the disassembly: the 15 report queries (de87778), the stage table {0,4,3,1,2}
(de864dc), ReportCounter types 0 to 14 and 16, ClearDepthStencil rows 1 to 11 (constant de13288 = 0x80030049, 0x800004e7), and
`d830fc0` (it only reads `device+0x1A08`). The mutant `depth < 0.5f` to `<=` was killed. `ctest` passed except `tracked_material`.
Run 2026-11-07 09:27 (exe md5 f8d20979): clean, 42 s, no `refused` line of the new groups, `faults=0`, `gops=47`, no crash.
Renderer: `0 draws, 22606 skipped`: the first draw gets `vulkan: shader 5d7fc8b3d07ff5a3 (stage 0) not translated: missing exit`
and all later draws are skipped. The user decided that this is part of the shader work of stage 4 (no separate task). Check it
when the shader work starts: the shader address of the program bind, and the skip rule of the renderer.
Decisions of 4a: `BindImage` writes one INCR 3 packet at 0x8E3 for each handle (no ONCE packet). Stage 5 (compute) is not sent.
`+0x1A04` = 0 (zcull word is sent). `+0x1A08` is unknown, assumed 0 (macro 0xE26 is not sent). Not checked in the disassembly:
`ClearTexture` clip and layout formulas, `CopyTextureToTexture` byte values of rows 1 to 6 and 9 to 11 (analyst text, section 2 and 3).
The analyst reports with these formulas are in `reports/analyst-s4-4.md`.

Old State text (2026-11-06):
- Last commit `59c7e17` (step 3b). The exe was the build of `59c7e17` (md5 6c50f646). Last read run 2026-11-06 00:10: clean, 30 fps,
  `first draw sent`, `first compute dispatch sent`, `dispatch_refusals=0`, no crash, black screen.
- Old rule: `CLAUDE.md` named GPT-6-Luna (Codex) for the explorer, analyst and writers. The real model of these agents is OpenCode with
  DeepSeek V4.1 Flash (user, 2026-11-07). Commit line used: `Co-Authored-By: OpenCode (DeepSeek V4.1 Flash) <noreply@opencode.ai>`.
- Old NEXT text: "first task: find why the screen is black. Read the log and `docs/nvn-design.md`, tell if the Vulkan side builds a shader
  and a pipeline from the program words." Answer: the translator exists and runs, the first shader fails (see above).

## Stage 4 step 4b ClearTexture done and state text of 2026-11-07 (moved from WORK_PLAN.md on 2026-11-07)

Commit 847971d (not pushed). Facts: `reports/verify-s4-4b.md` (caller check of the disassembly; the analyst report had 4 errors).
Code: `nvn_rt_clear_texture_words` (`nvn_rt.c`), `replay_host_clear_texture` (`nvn_native.c`), tests in `tests/nvn_rt.c`. Only 2D and 2D array
targets are sent; 1D array, 3D, linear, levels above 1 and more than 64 layers are refused with a log line. Run 2026-11-07 09:56 (update
program, 36 s): no crash, faults=0, 0 JIT misses, 30 fps, no refusal. ClearTexture was called only 2 times, so its effect is not seen.
Black screen as before (shader failure, stage 4 shader work). The exe of that commit: md5 eccbbb49. The mutant "min(origin,0) removed"
is killed by the clip test with a wide limit. Used calls in that run: ClearDepthStencil 1676, ClearColor 925, ReportCounter 1950,
BindImage 2, CopyTextureToTexture 42.
Copy builder `d82c5d0` is verified (see verify-s4-4b.md). The copy caller `d82bfd0` is NOT decoded.

Old steps text:
2. Stage 4 steps, one per session, a user run after each. DONE: (1) program bind, (2) state to methods, (3) `draw`, (3b) `dispatch`,
   (4a) report counter, image bind and depth stencil clear (facts in `OLD-WORK_PLAN.md`, sections "Stage 4 step 3", "3b done", "step 4a
   done"). LEFT: (4b) `ClearTexture` (macro 0xE1E, 0x3FD clip, 0x674) and `CopyTextureToTexture` (0x6080 to 0x6094, 0x6223 to 0x6234),
   (5) renderer notice for the CPU writes of `WriteTexels` plus the 3D case. (Step 6 is explained: no host work.) Then the shader work.
   CHECKED 2026-11-07 (`reports/verify-s4-4b.md`, read it, not the analyst report): ClearTexture is verified (4 corrections to the
   analyst: e, the sample scale in the clip, the linear width and height, the missing word 0x80020049). The copy builder `d82c5d0` is
   verified (format row +0x18, corrected field list). The copy CALLER `d82bfd0` is NOT decoded (ratios, block scaling, the builder
   stack arguments of words 9 to 11): give it to the game explorer, one task. Do the clear first if the copy is not needed.
   NEXT (new session), step 4b: write `brief-s4-4b.md` from the template `brief-s4-4a.md` (clear first), make a
   worktree, launch the code writer, read the patch, build, `ctest`, mutant, copy the exe, ask for one run. Macro 0xE1E has no handler
   in `mme_hle.c` (hash 0xBCFD89165D030F2F, 78 words, interpreted): check that the private channel runs it.
   Known edges: a vertex buffer bind of size 0 is not sent (decision 7). Open: the writer of MME scratch 0xD18 is unknown
   (stays 0; `_cb` draws are refused with a log line). Open for compute: gpr and sass bytes are 0, the rest of cbuf 0 is zero, cbuf 2
   textures are refused (no scene needed it yet).

Old state text:
- Last commit `0ea245e` (step 4a: report counter, image bind, depth stencil clear). The exe is its build (md5 f8d20979, equal to the
  three program copies in `game/scarletnx/bin/` and `game/recompiled/01001F5010DFA000/`). Check `git log` and `git status` first. The
  commits after 7d7532e are not pushed. `ctest` passes except `tracked_material` (known; `driver_workflow` can fail in a parallel run).
  The `.ini` of the game has `nvn_trace=1`, `nvn_native=1`, `nvn_selftest=0`. After each build: `tasklist` shows no game, then copy the exe.
- Last read run: 2026-11-07 09:27: clean, 42 s, no refusal, `faults=0`, no crash. Black screen: the renderer skips all draws after one
  shader failed (`missing exit`). It is part of the shader work of stage 4. Older state text: `OLD-WORK_PLAN.md`.

## Work plan text moved on 2026-11-07 (start of step 4c)

Done steps of stage 4 (before 4c): (1) program bind, (2) state to methods, (3) `draw`, (3b) `dispatch`, (4a) report counter, image bind,
depth stencil clear, (4b) `ClearTexture`. Facts are in the sections "Stage 4 step 3", "3b done", "step 4a done", "step 4b ClearTexture done".
Step 6 is explained: no host work.

Method of 3b-4 to 3b-6b (it worked): library analyst (10 to 30 functions in one brief, check at least 3 rows with the
disassembly), then decisions of the caller in the brief of the handler writer, then read the whole patch, `git apply --check`,
build, `ctest`. Tools: `disasm.py` and `find_callers.py` need the MSYS2 CLANG64 shell, `SNX_SCARLETNX=<abs path of
build/default/scarletnx.exe>`, and a scratch folder as current folder (a script in `C:\msys64\tmp`). The writer works in a detached
worktree in the scratchpad (`git worktree add --detach`); remove it after the patch. Get the addresses from the log lines
`nvn: <name> -> <address>`. The `Co-Authored-By` lines are in `CLAUDE.md`, section "Git".
Step 4c explorer report and brief (2026-11-07): `reports/explorer-s4-4c.md` (checked), `reports/brief-s4-4c.md`.

## Stage 4 step 4c CopyTextureToTexture done (2026-11-07, commit 4736e3f, moved from WORK_PLAN.md)

Code: `nvn_rt_copy_texture_words` (`nvn_rt.c`), `copy_format_word_cb` (table column +0x18) and `replay_host_copy_texture` (`nvn_native.c`). Scope: 2D and 2D array, block linear, no block format, flag bit0, at most 64 layers. Refused with a named fault: kind 3, block scaling, kind 5/6 multiply, flag bits 2 to 4, clip limit. Empty destination region: nothing sent, no fault. Run 2026-11-07 16:33: 42 copies, no refusal, faults=0. Open: `verify-s4-4b.md` says 0x800160B5 and 18 words for later passes, the disassembly says 6 words (the patch follows the disassembly). Worktree `wt-s4c` removed.

Text moved from the state and next lines:

- Last read run: 2026-11-07 16:33 (4c): clean, 30 s, 42 copies, no copy refusal, 600 frames, 30 fps. Run before: 09:56: clean, 36 s, no refusal, `faults=0`, no crash, 30 fps. Black screen: the renderer skips all draws after
  one shader failed (`missing exit`). It is part of the shader work of stage 4. Older state text: `OLD-WORK_PLAN.md`.

2. Stage 4 steps, one per session, a user run after each. DONE: steps 1 to 4b (`OLD-WORK_PLAN.md`, last section). LEFT: (5) renderer notice for the CPU writes of `WriteTexels` plus the 3D case. (Step 6 is explained: no host work.) Then the shader work.
   DONE 4c (commit 4736e3f; move its facts to `OLD-WORK_PLAN.md`). NEXT: step 5 (renderer notice for the CPU writes of `WriteTexels` plus the 3D case).
   4c later work only if a log shows refusals: kind 3, block scaling, kind 5/6 multiply, flag bits 2 to 4, clip limit. Open: `verify-s4-4b.md` says
   0x800160B5 and 18 words for later passes, the disassembly says 6 words (the patch follows the disassembly). Macro 0xE1E: no host handler.

## Stage 4 step 5 WriteTexels 3D done (2026-11-07, commit e571093, moved from WORK_PLAN.md)

- Renderer notice: no code. `frame_run` calls `frame_begin`, which starts a write epoch (`mem_may_have_changed`), and the OS write
  watch (`plat_ww_scan`) sees the host writes of `WriteTexels` like CPU writes of the game. `gpu_mem_written_cpu` is only for bytes that
  the host publishes without a CPU write (`vk_own_publish`). Not proven by a screen: the screen was not read (ask the user).
- 3D case (explorer report `reports/explorer-5-1.md`, the slab formula checked by the caller at d83d2cc to d83d308): the slice z is at
  `((nx*R*(z>>ez)) << (ey+ez) + ((z & (2^ez-1)) << ey)) << 9` bytes from the level base. It equals `bl_x/bl_yz` of `gpu.c` with bh = ey
  and bd = ez. A 3D level has no layer stride. The source of slice k is `src + k*src_row_bytes*rows`. The library has no range check:
  the host refuses z + count above the LEVEL depth (`mip_dimension(depth, level)`).
- Code: `NvnTextureLevel` has `depth` and `depth_log2`; `texture_level_offset` and `nvn_texture_write_rows` have a z argument;
  `texture_write_check` and `texture_write_slice` (`nvn_native.c`) handle dim 3. Test: `test_texture_write_rows_3d`.
- Lessons: (1) the first mutant (`<< bh` to `<< 0` in the z term) PASSED, because the writer's test used bh = 0 only. A mutant of every
  new formula term is needed: the test must use every exponent above 0 at the same time (bh = 1 and bd = 1 now). (2) The writer
  added a `dim != 3` guard to `one_d_array` that no cause needs: the caller removed it. (3) The opencode writer took about 35 minutes
  and wrote the patch and report only at the end: wait with a loop that needs both files (patch and report) stable for 60 s, in
  rounds of 9 minutes (`timeout` 595000), and check `git diff --stat` in its worktree to see progress.
- Run of 2026-11-07 20:48 (build e571093, md5 0e9781a6, update program): 31.8 s, `faults=0`, `dropped=0`, `missing=0`, no crash, 0 JIT
  misses, 30 fps after the first 5 s (14 fps while loading). `WriteTexels` ran once, no `is 3D` line. Seen: `nvnSyncWait sync=14482da50
  state=2 timeout=ffffffffffffffff is not signaled, fails` 21 times, `nvnWindowAcquireTexture failed: no free window image
  (states=2,2,0,0)` 18 times and `nvnQueuePresentTexture failed: index outside the window textures (index=-1)` 18 times, all between
  3.90 s and 4.50 s, then no more. The design (`docs/nvn-design.md` line 860) calls the acquire stop "intended until" the frame job frees
  the image. Not known: whether the three lines were in the runs of 4c (those logs were not kept). The cause of the SyncWait failure is
  not found. 80 shaders are `not translated` (a jump table in a constant buffer, unknown opcode, `missing exit`).
- Open from the reports: a 3D write through a view with the layers flag (`base_layer` is added, not checked in the disassembly);
  compressed 3D levels (the block row count `ceil(rows, 8<<ey)` follows the size formula, not checked in the disassembly).

## Stage 4 task A SyncWait and acquire wait for the frame job done (2026-11-07, commit a042a79, moved from WORK_PLAN.md)

Cause: the first frame job needs about 0.5 s on the GPU thread (new render targets, 80 shaders not translated). The game waits
on its fence with `timeout=ffffffffffffffff` meanwhile. `h_sync_wait` ignored the timeout and failed at once on a FENCED sync
(state=2), and `h_window_acquire_texture` found no FREE image (`states=2,2,0,0`) and wrote index -1. Run before the fix: 21 SyncWait
failures, 18 `no free window image`, 18 `index=-1`, 3.90 to 4.50 s, first frame line at 4.51 s.

Fix: `frame_done_cv` (PCond, with `objs_lock`), broadcast in `frame_finish`. `SyncWait` on FENCED waits with `kthread_wait` until
SIGNALED (0) or the timeout (2). SIGNALED gives 0 at once. UNSIGNALED and unknown sync give 3 with a log line (no limit wait
would never end). Acquire waits the same way only when no image is FREE and one is PRESENTED. Pure helpers in `nvn_params.c`:
`nvn_sync_wait_action`, `nvn_wait_deadline`, `nvn_wait_remaining_ns`, `nvn_window_has_presented`; test `test_sync_wait_limit`
(a mutant without the overflow clamp fails it). A negative `timeout_ns` is no limit in `cond_wait` (`plat_win32.c`). The code
writer made the patch (brief `reports/brief-a-syncwait.md`), the caller read it whole. ctest: only `tracked_material` fails.

Run after the fix (2026-11-07 21:35, 58 s): `faults=0`, no crash, 0 JIT misses, 30 fps, 0 `no free window image`, 1 SyncWait
failure (3.57 s, sync=14482da50, state=0), first frame line at 4.02 s, 80 shaders not translated. Log copy: scratchpad
`runs/run-syncwait-fix.log`. Docs: `docs/nvn-design.md` near line 863.

## Stage 4 task A2 SyncWait failure at 3.57 s (2026-11-07, closed by the user)

The log line has `caller lr=aac7d20`: the `blr` of the GOT slot `c2f5fc0` (`nvnSyncWait`) at `aac7d1c`, in the worker task `aac7460` (no direct caller).
The task waits for the syncs at renderer `+0x108` (flag `+0x100`) and `+0x118` (flag `+0x110`) with timeout -1. The sync at `+0x118` failed.
The game does not read `x0` after either `blr` (`aac7cfc`, `aac7d1c`). A never fenced sync (type 0) gives 3 on the console too (library `d83bc00`).
So the host result equals the console result and the game ignores it. Not checked: the writer of the flag `+0x110` and the first fence of the sync `+0x118`.
Decision of the user: close with no fix. Disassembly: `analysis/syncwait_caller.txt` in the scratchpad.

## Items moved from the work plan on 2026-11-07 (before task B)

- Last read run: 2026-11-07 21:35, 58 s: `faults=0`, no crash, 0 JIT misses, 30 fps, 0 `no free window image`, 80 shaders not
  translated, 1 SyncWait failure (explained by A2, harmless). The user was asked what the screen showed: no answer yet.
Older open steps (moved to `OLD-WORK_PLAN.md`, section "Open steps moved from the work plan on 2026-11-04"): the clip limit and the
present arguments, the known edges of 2d, `SyncWait`, the selftest cases S and Y, the points that 3c must also do, and the
points of the reports of 3c that were not checked.
- Cleaned on 2026-11-02 (the user agreed): `game/work/Arceus/romfs`, `game/backup`, the hitch and profile files, old logs. Phase 10 extracts the RomFS again if needed.
- Resolution: the warning is done. A forced cap was not wanted (the user chose "warn only"). The real limit between 4x and 7x is not
  measured.

## Task B step 1: the program id word (2026-11-08, not committed, moved from WORK_PLAN.md)

Task B state (2026-11-08, NOT committed, the exe is its build, md5 081f754f): the cause of `missing exit` for shader 5d7fc8b3 was the program
id word of the 0xE18 macro. The host sent 0, `bind_program` (mme_hle.c:222) skips an id equal to the slot's id (starts 0), so the code
offset was never written and every draw read zeros at 0x400000000. Fix: a counter in `DeviceRec.program_ids` (library: u32 at device+0x1b30,
`d8368a0`), id = ++counter, never 0 (`nvn_program_build_words` refuses 0). ctest passes except `tracked_material`. After the fix: no crash,
`missing exit` gone, 25 shaders in the corpus. The screen is STILL BLACK (user, 2026-11-08). The user said: do not look for the next cause yet.
The log still has 79 `not translated` (47 unknown opcode = jump table read as code, 32 jump table in a cbuf): see the explorer report
`reports/explorer-shader-1.md` (pipeline.c:449 drops a shipped result with failed cbuf reads, shader_hash scan includes the table).
Two DIAGNOSTIC log edits are still in the tree and must go before the commit: `vk/pipeline.c` (WARN_LIMIT "code at gpu", plus the nvdrv.h
include) and `nvn_native.c` (the loop "program %llx stage %u entry_word=" after the program set log). `shader_corpus` in the .ini is empty again
(with it on, the game runs at 0.5 fps for 30 s). The corpus folder is in the scratchpad (`corpus/`, 25 files).

- Run facts before the fix: 80 `not translated` (47 unknown opcode, 32 jump table in a cbuf, 1 missing exit). Distinct shaders: 11
  (4 unknown opcode, 6 jump table, 1 missing exit). The corpus file of 5d7fc8b3 held 88 bytes, all zero. gva was 0x400000000 for
  every draw (macro never wrote the offset). The `nvnProgramSetShadersExt` inputs were right (entry_word 0x424e00000, h+708 0x30).
- Library mechanism (explorer `reports/explorer-progid-1.md`, checked at `d8368a0`..`d8368b0` and `d8543d8`): sdk `d836770`
  increments the u32 at `device+0x1b30` atomically, stores the result at `[prog+8]`, the builder `d8543a0` loads it as w25 and
  writes it as the id word of every 0xE18 run of that program. One id for each SetShaders call.
- Run after the fix: gva values like 0x4254c1030, `not translated` 0 (with shader_corpus) and 79 (without: the shipped path), 25 corpus
  files, `faults=0`, no crash, 26 fps at 32 s. With `shader_corpus` set every shader is translated at once: 0.5 fps for 30 s.
  Screen: black.
- Explorer report of the reject classes (`reports/explorer-shader-1.md`): the unknown opcodes are the jump table read as code
  (`shader_hash` stops only at a zero word); the jump tables are dropped by `pipeline.c:449` (a shipped result with `s->cbr.n > 0`);
  the 9 rejected shaders that a run drew translated at their first draw. Its claims were checked in the source: pipeline.c:449,
  `has_exit` at spirv_translate.c:4415, `shader_hash` in shader.c.
- Tools used: `shader_corpus=<dir>` in the .ini (file format in `shader/corpus.c`: 24-byte header, ShaderInfo of 2096 bytes, code).
- A heredoc of Python in Bash can change `\\n`: write edit scripts with the Write tool (`edit_progid.py` shows the CRLF-safe pattern).
- Last run before this task (2026-11-07, A2): `faults=0`, no crash, 0 JIT misses, 30 fps, 80 shaders not translated. The user did
  not say what the screen showed.

## Task B session 2026-11-08: step 1 committed, subchannel bug found, black screen not solved

Commit `1a76532` (program id word): done, `ctest` passes except `tracked_material`. The two old diagnostic edits are removed.

Method: after the commit, the log showed RT registers with wrong values. A ring of the writes to the RT registers (methods 0x200 to 0x27F and 0x487)
with the replayed NVN op code showed the cause. Op 24 is `NVN_OP_COPY_TEXTURE`. Its words (`nvn_rt_copy_texture_words`) use headers with
subchannel 3 (`0x608C`, `0x6223`, `0x622C` ...). The word decoder `nvn_word_stream_next` dropped bits 15:13, and `replay_bind_send`
sent every method to `maxwell3d_method`. So the 2D engine methods (Fermi2D PIXELS_FROM_MEMORY: 0x223, 0x22C to 0x237) wrote the RT slots 2 and 3
of the 3D engine. Result: RT with width 0xCCCCCC00, address 0x3332000000000003, format 0x90000000, images of 19 to 26 GiB,
`VK_ERROR_OUT_OF_DEVICE_MEMORY`, frames of 2 to 5 s. The copy itself did not run on the 2D engine. Subchannel 3 is bound to Fermi2D at
channel reset (`channel_reset` in gpu.c).

FIX (in the tree, NOT committed):
- `nvn_params.h/.c`: `NvnWordStream` has the field `subchannel` (bits 15:13 of the header of each call).
- `gpu.c/.h`: `gpu_channel_method(ch, sub, m, v, last)` calls `method_call` (the engine bound to the subchannel).
- `nvn_native.c` `replay_bind_send`: a subchannel other than 0 goes to `gpu_channel_method`, subchannel 0 stays `maxwell3d_method`.
  The second user of `nvn_word_stream_next` (`nvn_native.c` near line 3430, program words) is not changed (subchannel 0 only).
- `tests/nvn_params.c` `test_word_stream`: new checks of the subchannel. `ctest` passes except `tracked_material`.

Run after the fix (user, 2026-11-08): `diag-rt` 0, no `OUT_OF_DEVICE_MEMORY`, 664 frames in 34 s, 20 to 30 fps, up to 363 draws each frame,
`faults=0`, no crash. The screen is STILL BLACK. 79 `not translated` stay (the shipped path).

Black screen facts (runs `runs/run-after-subch-fix.log`, `runs/run-diag-present.log` in the scratchpad):
- The present is right: the window textures are gpu 421d90000 and 421520000 (cpu 92687b000 and 92600b000, 1920x1080, layout 3), and
  `vk_rt_for_cpu` finds them as render targets. The CPU copy is not shown.
- `render_draw` records the draw in 87 percent (2674 of 3000). The other 13 percent leave at one exit: `!vset || (fs && !fset) || !bset_ok`
  (`vk/render.c`, descriptor set not made, `g_vk_skipped++`).
- So the picture is black although draws reach Vulkan. Candidates (not proved): the zero texture descriptor (`unsupported texture
  format 0, header word 0 00000000 (not drawn)`, once at 1.93 s) and `nvn-native: bind draw constants refused stage=-1 slot=-1 word=00000004`
  (16 lines).
- After frame 360 (about 16 s): `missing=` is above 0. NVN functions without a host handler are called: `nvnVertexAttribStateGetStreamIndex`
  79682802 times in the run, `GetFormat` 79682737, `VertexStreamStateGetDivisor` 13571, `SetPointSize` 357, `Begin`/`EndTransformFeedback` 357,
  `ResetCounter` 123, `SamplerFinalize` 1. The first 300 frames have `missing=0` and are black too, so this is not the cause.
- Frame ops: 14 draws each frame in the first 60 frames, 47 draws at frame 300, 363 at frame 663 (title demo).

DIAGNOSTIC code in the tree (remove before the commit, the fix does not need it):
- new untracked file `src/nx/gpu/diag_rt.h` (ring of RT register writes, weak globals).
- `maxwell/engines.c` (include and `diag_rt_note` in `maxwell3d_method`), `maxwell/mme.c` (include and `g_diag_in_macro` in `mme_out`),
  `nvn_replay.c` (include, `g_diag_op`, `g_diag_op_seq` in `replay_op`, reset in `nvn_replay_list`).
- `vk/render.c`: `diag_bad_target` in `read_targets`; `g_diag_exit`, `diag_draw_done`, `g_diag_recorded` and the `{ g_diag_exit = __LINE__; return; }`
  forms in `render_draw` (24 returns).
- `vk/renderer.c`: the `diag-present` block in `vkr_present_buffer`. `nvn_native.c`: the `diag-present` LOG_LIMIT in `frame_show`.
  The files `vk/render.c`, `vk/renderer.c`, `maxwell/engines.c`, `maxwell/mme.c`, `nvn_replay.c` hold only diagnostic changes: use `git checkout`.
  Edit scripts: scratchpad `.../25a34053-.../scratchpad/edit_diag_rt.py`, `edit_subch.py`, `edit_diag_present.py`, `edit_diag_draw.py`.
Build state: exe md5 108776b9 (the diagnostic build) in `game/scarletnx/bin` and in the two programs of `game/recompiled/01001F5010DFA000/`.
`shader_corpus` in the `.ini` is empty. Saved logs: `runs/run-before-diag-rt.log`, `run-diag-rt-found.log`, `run-after-subch-fix.log`, `run-diag-present.log`.

## Task B step 0 done, black screen diagnostics (2026-11-08, second session, moved from WORK_PLAN.md)

Step 0 DONE: commit `3d4d3e9` (subchannel fix: `nvn_params.c/.h`, `gpu.c/.h`, `nvn_native.c` `replay_bind_send`, `tests/nvn_params.c`).
All old diagnostic code was removed first (the `diag_rt.h` file, the `diag-present` block, the five files restored with `git checkout`).
The exe was built before the commit, so the log says `build: main 1a76532-dirty`. Its content is the tree of `3d4d3e9`.

Run after the commit (user, 63 s): 30 fps stable, no crash, no `invalid`, no `refused`, no `diag-rt`, no giant image. `faults=0 dropped=0
missing=0` in all frames. Draws each frame: 14 (first 60 frames), 47 (later). One `unsupported texture format 0, header word 0` at 1.25 s.
`bind draw constants refused` did NOT appear in this run, so candidate (ii) is not seen now. The screen is still black.
Setting `nvn_trace=1` is not needed for `nvn_native=1` (only `nvn_selftest=1` needs it). It logs every call and slows the game.

DIAGNOSTIC 2 (in the tree, NOT committed, remove before any commit): `vk/vk.h` (declaration `vk_diag_draw`), `vk/render.c` (one loop
`vk_diag_draw(g_rp.rt[di])` after `pred_end` in the draw record code), `vk/renderer.c` (block `DIAGNOSTIC (task B)` before
`vkr_present_buffer`: `vk_diag_draw`, `diag_present`, a forward declaration of `surf_read`, and the call in `vkr_present_buffer`).
All three files hold only diagnostic changes: `git checkout` them. Edit scripts in the scratchpad: `edit_diag.py`, `fix_diag.py`.
The exe in `game/` is this diagnostic build (md5 030b32c4, equal in the three copies).

Result of the first diagnostic run (two runs, same result, log saved as `C:/Users/Azunel/AppData/Local/Temp/claude/a4804efbbfb6aaab/439fcab0-1a16-405a-8dce-e2dc973a40b4/scratchpad/run-diag-prev.log`):
- The present image is found as a render target (`RT`, 1920x1080, layout 6, size equal to the asked size). The CPU copy is NOT used.
- The read back of this image is all black in all 960 and more presents: `nonblack=0 maxa=0 sum=0`. Alpha is 0 too.
- Draws to the window image: 0 or 1 in the first 120 frames, then 0 for every frame from frame 240 on, while the frame has 62 to 66 draws
  to at least 16 other targets (the table had 16 entries, full). So the game's last pass does not write the window image, or the replay
  sends it to another target, or another operation writes it (candidates: `NVN_OP_COPY_TEXTURE`, the 1 dispatch each frame,
  `draw_texture`, `BIND_IMAGE`).
- The extended diagnostic (table of 64 targets, lines `diag-rt #N: gva=... WxH vkfmt=... draws=... <- WINDOW` at presents 3, 240, 600)
  is built and copied, NOT run yet.

## Task B step 1: black screen history and cause found (2026-11-08 to 2026-11-09, moved from WORK_PLAN.md)

   (B) The black screen and the shader work. Rule of the user (2026-11-08): order 1, then 2. If step 1 finds the cause of the black
   screen, fix it and let the user run it first. Make step 2 a separate change after that.
   Step 1 (black screen, the cause is NOT found). Facts (`OLD-WORK_PLAN.md`, last section): the present image is the window render target
   (found, right size) and it reads back all black (`nonblack=0`, alpha 0). From frame 240 on, NO draw writes it, while 62 to 66 draws
   go to other targets. The old candidates (zero texture descriptor, `bind draw constants refused`) are probably not the cause.
   RESULT of the run with exe 030b32c4 (2026-11-08 15:55; 2026-11-09 the exe 2ee62268 adds per-target readback and skip lines diag-skip, waiting for the run): the window RT (gva 421520000, 1920x1080, vkfmt 37) gets 1 draw each
   present and still reads all black. The old fact "no draw writes it" is wrong (the old log came from the build before). 42 targets
   at present 240 (one draw each to most 1920x1080 targets: 420a50000 f43, 4201e0000 f37, 41f970000 f37, 41b2f0000 f43 8 draws).
   1200 of 20779 draws skipped. NEXT: read back the upstream targets too (nonblack per target) to find the first pass that gives
   black, and log why a draw is skipped.
   RESULT exe 2ee62268 (2026-11-09 09:41): only 6 64x64 targets (f76), 4201e0000 (f37, all pixels) and 41d2e3c00 (960x540) have
   content. The scene target 41b2f0000 (f43, 8 draws), all HDR targets (f97, f122) and the window are black. Skips: only 2 per frame
   (render.c stage set failed). Conclusion: the passes run, but 66 draws a frame is post-processing only; the scene geometry draws do
   not reach the renderer, or draw nothing. RESULT 2026-11-09 (from the `nvn-used` lines of the same log, no new run): the guest
   calls all Draw* functions 14093 times in 65 s (1889 presents): ElementsBaseVertex 12268, ElementsInstanced 1729, Arrays 94,
   Elements 2. No Indirect, MultiDraw or DrawTexture call (the trace counts every bootstrap proc). CallCommands 251740, EndRecording
   180304. The renderer counted 20779 draws (more, because of the replay of command handles). So NO draw is lost between the guest
   and the renderer: the GAME does not send scene draws. NEXT: find why the game does not draw the scene (it waits for something
   that the runtime does not give: a query or counter result (ReportCounter 3778, 2 a frame), a SyncWait result, a resource or
   shader load, or a loading state). Ask the user what the screen shows at 10 to 60 s (logo, title, loading). Give the question to
   the game explorer: what makes the renderer of the game skip the scene passes.
   RESULT explorer (`reports/explorer-noscene-1.md`, 2026-11-09): no runtime value blocks the scene. ReportCounter is the frame
   profiler (no ResetCounter, no occlusion). The run of 09:41 stayed in the title or logo state (47 to 66 draws). The run of
   2026-11-08 (`runs/run-after-subch-fix.log`, verified) reached 371 to 379 draws a frame at frames 653 to 664 (32.7 s), then exited.
   Engine gates (not checked by the caller): pass gate 0x8188e94, object gate 0x816b370. So the black screen is in the title
   frames: their draws reach Vulkan and still give black. User (2026-11-09): the screen was all black; the user will press a
   button past the title in the next run (exe 2ee62268). After that run: read diag-present, diag-rt and frame lines past 33 s.
   RESULT run 2026-11-09 11:29 (exe 2ee62268, user pressed past the title): the scene runs, 500 to 530 draws a frame (1681 at
   present 600). Content (nonblack) at present 600: scene target 41b2f0000 (f43, 49 draws, all pixels), 41e890000 (f122, all
   pixels), 4201e0000 (f37, 856441 px), 41f970000 (f37, 76880 px), 41d2e3c00 (960x540, all). Black: the window 421520000 (f37,
   1 draw, maxa=0), 420a50000 (f43, maxa 142), the bloom chain (f97 and f9, mip sizes). The 1 window draw is NOT skipped (it is
   counted after rec_CmdDraw, render.c:3231) and writes 0 with alpha 0. Skips stay 2 a frame (render.c:2776). One warning:
   `unsupported texture format 0, header word 0 (not drawn)` (format.c:354). NEXT STEP (one light diagnostic): for a draw whose
   target is the window, log the color mask, blend, viewport, scissor, the bound textures (gva, format, and nonblack of their
   image if it is a known RT) and the shader hash. Then find why it writes 0. Compare: the window input is probably 4201e0000
   or 420a50000 (black, maxa 142: check its format readback, f43 is SRGB).
   DONE 2026-11-09: exe 4c7758a9 (the three copies) adds `diag-last #N:` lines after each `diag-rt` line (presents 3, 240, 600):
   the state of the last draw to each target (writes, color mask, sph18, blend0, viewport, scissor regs, vs/fs hash, count, the fs
   textures with gva, size, format, is_rt). ctest not run (diagnostic only). WAITING for the user run (press past the title,
   stay to about 40 s). Then read the `diag-last` line of the WINDOW target, and the nonblack of its input textures in `diag-rt`.
   RESULT run 2026-11-09 11:35 (exe 4c7758a9): the window draw is a full screen triangle (cnt 3, blend off, mask f, writes 1,
   viewport ok). Its one fs texture has have=1, t=NULL, tic0=0: it samples zeros, so the window is black. ALL 77 logged fs textures
   (every target, presents 3/240/600) have tic0=0 and no surface. So NO texture resolves: tex_resolve (render.c:752) reads a zero
   header. NEXT: log in tex_resolve the handle, the texture pool base and size (TIC address) and the address it reads, and compare
   with what the host nvnTexturePool / RegisterTexture writes (nvn_native). Find where the header is (or is not) written.
   Path read 2026-11-09: BindTexture (412067 calls) -> replay_host_bind_texture -> replay_bind_send_textures -> nvn_bind_texture_words
   (nvn_params.c:1727, MME methods 0xE0E/0x68B/0x8E3: the MME writes the handle into the driver cbuf). The shader reads the handle
   from cbuf r[0x982] (render.c:715). Hypothesis: the MME writes to the driver constants buffer of 3D reg 0xD18 (r[0x346]), which
   stays 0 (known open item), so the cbuf holds handle 0 -> TIC entry 0 -> zero header. exe ea33b705 adds to `diag-last`: tcb, cb
   (cbind[4][tcb]), tic base, lim, d18 (r[0x346]) and hd= (the handle) per texture. WAITING for the user run.
   RESULT run 2026-11-09 11:40 (exe ea33b705): CONFIRMED. All 27 diag-last lines: tcb=r[0x982]=0, cbind[4][0]=0, d18=0,
   tic base 412bc0000 lim 1fff (pool OK). All 123 texture handles read 0 (hd=0) -> TIC entry 0 -> zero header -> black.
   Cause: the driver setup of the texture handle cbuf is missing on the host: the real NVN queue init sets 0x982 (the cbuf
   index for bound textures), binds the driver constants buffer at that index for each stage and sets MME scratch 0xD18 (its
   address). The bind words (0xE0E param, 0x8E3 + handle) go to a buffer that nothing points at.
   NEXT (one step): game explorer brief: from the NVN library disassembly, what the queue init (nvnQueueInitialize and the
   first command memory it writes) and the MME macros 0xE0E/0x8E3 write: 0x982, 0xD18, the cbuf binds (0x8E0..0x8E2, 0x904+)
   of the driver buffer, its size and the slot layout (slot*8+0x20 per stage). Then a brief for the code writer: the host
   owns that buffer (no compatibility words in game memory, rule of 2026-11-03) and sends the setup words at queue init.
   OLD NEXT: (1) ask the user what the screen shows and to press a button past
   the title in the next run; (2) check the data path of the title draws (texture descriptors, vertex data from guest memory,
   the 2 skips a frame at render.c:2776, `GPU-owned guest memory landed for the CPU 0 times`). OLD TEXT: ask the user for the log of the run with exe 030b32c4 (or read the log in the game folder if the user ran it): read the
   `diag-rt` lines (`grep diag-rt`): the list of targets (gva, size, format, draws) at presents 3, 240, 600 and the line with
   `<- WINDOW`. Then find the writer of the window image: (a) a draw to a target of the same size 1920x1080 at another gva (alias or
   a wrong address in `NVN_OP_RENDER_TARGETS`, view, `frame_set_target`), (b) `NVN_OP_COPY_TEXTURE`, `draw_texture` or the 1 dispatch
   each frame that targets the window texture, (c) an RT that is cleared or discarded after the last draw. Compare the window texture
   address of the frame job with the gva of the final pass targets. Fix on the confirmed difference only. Do not commit the diagnostic.

## Black screen: driver constants buffer (step a)

Step 1 (black screen). CAUSE FOUND (run 2026-11-09 11:40, exe ea33b705; history in `OLD-WORK_PLAN.md`, last section): every
fragment texture handle reads 0 (123 of 123), so every draw samples TIC entry 0 (zero header) and the window gets 0, alpha 0.
The pool is good (tic base 412bc0000, lim 1fff). The texture handle cbuf is not set up: 3D reg 0x982 (cbuf index of bound
textures) = 0, cbind[4][0] = 0, MME scratch 0xD18 (r[0x346]) = 0. Path: BindTexture (412067 calls) -> replay_host_bind_texture
-> replay_bind_send_textures (nvn_native.c) -> nvn_bind_texture_words (nvn_params.c:1727: methods 0xE0E stage param, 0x68B,
0x8E3 run of slot*8+0x20 and the handle). The shader reads the handle with tex_handle (render.c:715) from cbuf r[0x982].
The real driver sets up the driver constants buffer at queue init: 0x982, the cbuf bind of that buffer for each stage, 0xD18.
The host does not. The same cause explains the old open item "the writer of MME scratch 0xD18 is unknown".
Step a is DONE (2026-11-09): `reports/explorer-7a.md` (brief `reports/brief-7a.md`). The caller checked d851bd4 (inline 0x982=2),
d852074..d852090 (0xD18 = (queue memory base + queue[0x1680]) >> 8) and the call chain d838a64/d838a6c, and the host defines.
Result: nvnQueueInitialize runs d84f630 (8 words) and d851880 (calls d84f800, then 204 words, then d8511e0 with 684 words).
The host runs only d84f800 (810 words) with a 0x1000-byte buffer (the full stream needs 0x1AA8). The missing 896 words set
0x982=2, 0xD18, the cbuf 0 of 0x7000 bytes at the queue memory, and for each stage 0..4 a block of 0x200 bytes at
base+0x7000+stage*0x200 bound as cbuf 2 (0x904+8*stage = 0x21), filled with words 0..7 then 0xFFFFFFFF. Sent once only.
The bound texture handles go into this block at 0x20+slot*8. Open: the object at queue+0x1568 (its [obj+0x38] gives the
queue memory GPU base) does not exist in the host scratch image. The block size is 0x700 when device+0x1AF5 bit 0 is set.
NEXT (step b, new session): decide between (1) the host runs d851880 with a host-owned memory object and a 0x1AA8 buffer, or
(2) the host sends the listed words itself from a host-owned GPU buffer. Then the brief for the code writer.
Old step a text: brief for the game explorer (template `reports/brief-6a.md`): from the NVN library disassembly,
what nvnQueueInitialize (and the first command memory it writes) and the MME macros at 0xE0E and 0x8E3 write: 0x982, 0xD18,
the cbuf binds (0x8E0..0x8E2, 0x904 + 8 stage) of the driver buffer, its size, the slot layout per stage, and what else
reads that buffer (0xE20 storage params, `_cb` draws that read 0xD18). Then (step b) a brief for the code writer: the host owns
the buffer (no compatibility words in game memory, rule of 2026-11-03) and sends the setup words at queue init. A refusal
keeps a log line.

## Black screen step b: design of the full queue init stream (2026-11-09, moved from WORK_PLAN.md)

Short cause (from step a): the host ran only d84f800 (810 words). The missing words of d851880 and d8511e0 set 0x982=2, 0xD18,
cbuf 0 (0x7000 bytes) and per stage 0..4 a 0x200 block bound as cbuf 2 (texture handles at 0x20+slot*8). Report `reports/explorer-7a.md`.
DECIDED (user, 2026-11-09): option (1), the host runs d851880 (it calls d84f800) with a host-owned memory object.
Facts of the caller (2026-11-09): queue+0x1568 is an NVNmemoryPool (d838c54 builds a pool builder, calls d833ce0 = pool init,
flags 0x21). The calls at d852074 and d851300 do `blr [[obj]+0x38]` with x0 = obj. The library method there is d840b60
(`ldr x0,[x0,#0x40]; ret`, checked), so the host writes the GPU base at obj+0x40. d838f70 is a leaf: it reads device +0x8C,
+0x94, +0x98, +0xE0, +0xE4, +0x1AF4/5 and builder +0x10 (flags), +0x15 (has compute), +0x1C (compute size), and writes the pool
layout at obj+0x114..+0x154 (obj+0x114 = pool size = queue+0x167C, obj+0x118 = queue+0x1680 = 0, obj+0x120 = queue+0x1688 = 0x7000).
d84f630 is NOT run: it writes the 5 BindObject words from queue+0xEC..+0xFC, and the host channel binds the classes (gpu.c:409).
Explorer 7b (`reports/explorer-7b.md`, brief `reports/brief-7b.md`): d851880 reads about 30 device words. The library device init
(d831ef0 -> d8310a0, d831d30) gives: +0x90 = 0x503, +0x99C = 0x230, +0x9A4 = 0x430, +0x1A84 = 0x111, +0x1AAC = 0x3F,
+0x1AB0 = 0x00110011, +0x1AC0 = 5, +0x19D8/+0x19DC = the window origin and depth mode setters, +0x1AF8 -> +0x18 = shader region
base, +0x1B00 -> +0x120 = program offset 0; the other words are 0. The caller checked d8312fc, d831328, d83138c, d831398, d840b60.
The explorer's class-id guess for +0xF0/+0xF8/+0xFC was wrong (the brief uses gpu.c:409). The full stream is 2154 words: the
command buffer needs 0x2400 bytes (0x2000 reaches the overflow call d8243d0). Builder stack use 0x120 bytes.
Also found: `frame_replay_init` sent every init word to the 3D engine; the new stream has methods on other subchannels.
Code writer 7c (brief `reports/brief-7c.md`, worktree `wt-7c`) wrote `patches/7c-1.patch` (4 files, +348 -100, UTF-8,
`git apply --check` OK on the main tree).

## Task B step 1 done: the full queue init stream, black screen fixed (2026-11-09, moved from WORK_PLAN.md)

- Patch 7c-1 (code writer, brief `reports/brief-7c.md`) was reviewed, applied, built and committed as `7becf0c`. The host runs
  d838f70 and then d851880 on a host arena (kmem_alloc) with a host-owned and mapped queue pool. The device image words come from
  `nvn_queue_init_device_words` (`nvn_params.c`, test `nvn_params`). `frame_replay_init` sends each word to the subchannel of its
  header. Two light edits by the caller: a comment and the "ends inside a run" log line.
- ctest: only `tracked_material` fails (known). A mutant of the new test (value of +0x1AB0) made `nvn_params` fail.
- Review facts: the class order at queue+0xEC..0xFC matches d84f630 (it binds sub0, 1, 4, 2, 3 from these words) and
  `channel_reset` (gpu.c:409). d838f70 is a leaf that writes only obj+0x114..0x154 (it keeps the vtable at +0 and the GPU base at
  +0x40). The mode 3 headers (0x6000206D, compute inline data) are only in d84e010, d84e3c0 and d84eab0. The d851880 chain does not
  call them (`analysis/qinit_chain.txt`, `analysis/pool_layout.txt`). kmem_alloc never returns 0 (it stops with fatal) and gives
  new committed pages.
- Risk kept: queue+0x16C0 (the overflow handler object) stays zero. A command buffer overflow (d8243d0) would crash. Margin in
  the run: 0x204 bytes.
- User run (update program past the title): the screen shows the game. Log: `queue builder words=2175`, `queue init stream
  replayed methods=1851`, no refusal, 3D 0x982 = 2 (tcb=2), texture handles not zero, window nonblack=2073600, JIT misses 0.
- The diagnostic code of task B was removed from the tree and saved as `patches/diag-taskB.patch`. The exe in `game/` still has it.
- Lesson: `sed -i` in Git Bash changed CRLF files to LF. Edit CRLF files with the Edit tool, or restore with `unix2dos`.

## PR #1 NVN part taken, task (c) done (2026-11-09, commit 5c1dd1c, pushed, moved from WORK_PLAN.md)

- GitHub (nxaevora/nxaevora-main): issue #7 (a contributor offer, 60 fps work) and PRs #1 to #6 by colardocookie. PR #1 had
  1020 files: about 900 research files (`research/arceus-complete/`, a copy of the author's local folders), a Vulkan path tracer
  (`runtime/gfx/world-pt`), DLSS settings, README changes, and about 400 lines of NVN code. PRs #2 to #6: opt-in elapsed-time
  clocks for 60/120 fps (`arceus_clock.c`, env switches `SNX_ARCEUS_*`), tested only in the author's fork. Decision of the user:
  take only the NVN part of PR #1, close PR #1 with a comment. PRs #2 to #6 and issue #7: on hold (frame rate is later work).
- Method: `git fetch origin pull/1/head:refs/remotes/pr/1`, base f0f12b3. `git diff f0f12b3 pr/1 --` the NVN files (gpu/*,
  tests nvn_params.c and nvn_replay.c, tools/nvn_coverage.py, docs nvn-coverage.md, nvn-design.md, runtime-changes.md). It
  applied clean on 7becf0c. Build OK, ctest OK except `tracked_material`.
- Content: vertex attrib getters (format bits 21..30, offset bits 7..20 as 64 bits, stream index bits 0..4), stream divisor word
  +4, SamplerFinalize (library is `ret`), SetPointSize (0x546 float, then 0x68B = 0, always, meaning unknown), ResetCounter (0x54C,
  table sdk 0xde877bc, 17 words), transform feedback bind (0xE0+8*slot, run of 4 or inline 0, sent at call time as the library
  does), Begin (macro 0xE00, 2 words), End (0x1D1 = 0, then 4 report runs at 0x6C0, query words 0x1D005002 + 0x20*i at sdk
  0xde13dc0, 0xde135c8, 0xde13e50, 0xde13f78). The compute QMD cbuf size check uses the 17-bit field. Coverage tool and file.
- Check: game explorer (`reports/explorer-pr1-verify.md`): all match. The caller checked the ResetCounter table, the End query
  words, d84a600 and d856b40 again. Differences kept: the host refuses counter type >= 17 and xfb slot >= 4 with a log line (the
  library has no check).
- Commit 5c1dd1c, author colardocookie, OpenCode line for the explorer. Exe copied (md5 7f7cd018, no diag code).
- User run 2026-11-09 14:36 (update program, 68 s, clean exit): 30 fps steady from 15 s (the fall to 3.6 fps is gone, the user
  saw no stutter), "248 without a handler, 0 of them called", no refusal lines, JIT misses 0. Characters still not shown. The
  user: in some conditions they show with broken vertices. All `not translated` lines are vertex shaders, the same 11 as task (a):
  unknown opcode 0x3b8/0x380 (adb94885 37x, a3c1291e 8x, 45a7cf5b, 9ff45b16: the BRX table scanned as code) and cbuf jump tables
  (fdac039a, 9bac6c0b, 7fdd43b7, fe3faabb, a0a97109, 80033a4b). Guess (not verified): these are the character (skinning) shaders.

## Session of 2026-11-09 (afternoon): README, task (a) checked, task (b) cause found (moved from WORK_PLAN.md)

- Commit `6a3dfc5` (README status: the native NVN path draws at 30 fps with nvn_native=1, characters missing, old path is the
  default). Not pushed. The `Co-Authored-By` line of Claude changed from Sonnet 5.5 to Opus 5.5 in `CLAUDE.md` (user).
- Open, not done: `settings.txt:764` help of `nvn_native` still says "with 1 the game does not draw yet" (outdated).
- Task (a), shaders. The plan was: (1) `pipeline.c:449` drops a shipped result whose cbuf reads all failed (6 jump table shaders
  fdac039a, 9bac6c0b, 7fdd43b7, fe3faabb, a0a97109, 80033a4b). (2) `shader_hash` (shader.c:324) scans the BRX table as code because
  it stops only at a zero word (4 shaders adb94885, a3c1291e, 45a7cf5b, 9ff45b16): stop at the always run EXIT. A separate change for
  each (rule of the user). Source: `reports/explorer-shader-1.md`.
  FACT (log of the 14:36 run, nvn_native=1): all 79 `not translated` lines come from the shipped pass (ship_make, pipeline.c:1716,
  3.96 to 4.00 s, before the device init). No draw time failure (`shader ... (stage N) not translated`, pipeline.c:606). Only BRX
  reads a cbuf (shader.c:295). So (a) changes the prebuilt cache only, not the picture, and (a) is not the cause of (b). The pass
  length (to the zero word) differs from the draw length (to the EXIT), so a shipped entry of these programs is never hit at draw
  unless (2) is done first. User: (b) first, (a) later.
- Task (b), characters. Log of the 14:36 run: no refusal, no fault, no `not implemented`, 0 draws skipped, 3 compute dispatches a
  frame in the field, MME macro "transform feedback setup" first at 25.37 s, "multisample state bound, not sent" once.
  User: ALL characters (player, NPCs, Pokemon) are missing. Broken vertices seen when the camera turns away from a near model. With
  nvn_native=0 all are correct.
  Game explorer (brief `reports/brief-explorer-chars-1.md`, report `reports/explorer-chars-1.md`): skinning VS `24ba3b3c367d0b35`
  reads 4 bone matrices from a uniform buffer (bank 13, indexed 64-bit LDC) and writes the skinned vertices by transform feedback
  (PF_SRC_XFB, 3 buffers, stride 12/4/4). The bind (uniform, storage, vertex, XFB), Begin/End XFB, vertex state and draw word
  builders match the library. The game callers of the XFB functions were not found (called through pointers).
  ONE difference: `replay_barrier` (nvn_replay.c:189) only counts the bits (3c decision 6, nvn-design.md:1051). The library
  `nvnCommandBufferBarrier` (sdk d856590) writes methods. Explorer list (not verified yet): first `0x910E0002`; bit 0 `0x80000044`,
  bit 1 `0x80000378`, bit 2 `0x800003DF`, bit 3 `0x80000014` plus a 0x30 byte block, bit 5 `0x90110369`, bit 6
  `0x8000050A/0x80000509`, bit 7 `0x801904B2`, bit 8 a call to `d859710`. Verified by the caller: method 0x44 WAIT_FOR_IDLE goes
  engines.c:554 -> barrier(4) -> vkr_method_barrier (renderer.c:164), which ends the render pass and records vkCmdPipelineBarrier2
  (kinds 1 and 3 return, kind 5 increments shader_generation). So on the native path the XFB write and the later vertex read have
  no Vulkan barrier. User 2026-11-09: change decision 6, send the library words of each bit.
- Task (b) step 1, DONE 2026-11-09 (not committed with the `settings.txt:764` help fix of `nvn_native`). The words of each bit are
  in `docs/nvn-design.md` decision 6, "Barrier". Sources: `analysis/d_barrier.txt` (d856590) and `analysis/d859710.txt`. The
  explorer list was right, but it did not have: bit 4 (0x3DD after bit 1 or 2, else 0x4A2), the priority of bits 3, 0, 1, 2 (only
  the first set bit is sent), the 0x80000040 after the type 1 record of bit 3, and the words of d859710 (cb, 1, 1): 0x8000C014,
  then 0x2001C00B 0x80000000 when the device byte +0x60 is not 0, then 0x2001C00B 0x70000000. These are puller writes on
  subchannel 6 (`REF_CNT`, `MEM_OP_B` operations 16 and 14) that `puller_method` only stores. The first and last words are puller
  NOPs (0x910E0002, 0x91000002). `replay_host_method` (nvn_native.c:3223) and `replay_bind_send` (nvn_native.c:3319) send
  subchannel 0 to `maxwell3d_method`, so a method below 0x40 must go to `gpu_channel_method` (gpu.c:572) instead. The replay env
  (`NvnReplayEnv`, nvn_native.c:3986) has only the 3D `method` callback now.

## Task (b) done: barrier words in the replay, characters shown (2026-11-09, commits 2de86ee and 5c3f76d, moved from WORK_PLAN.md)

   Task (b) characters missing (cause found, user agreed 2026-11-09): change 3c decision 6. `replay_barrier`
   (nvn_replay.c:189) only counts the bits, so no WAIT_FOR_IDLE reaches the renderer and the transform feedback write (skinning
   VS 24ba3b3c) has no Vulkan barrier before the vertex read. Steps, one per session:
   (1) DONE: the words are in `docs/nvn-design.md` decision 6, "Barrier". Facts: `OLD-WORK_PLAN.md`, last section.
   (2) DONE: `nvn_barrier_words` (nvn_params.c, pure, max 14 words) and `replay_barrier` sends the words: method < 0x40 to the new env callback `puller_method` (`gpu_channel_method`), else `replay_method`. Build OK, ctest OK except `tracked_material`, two mutants caught (`nvn_params`, `nvn_replay`). `wt-7b` removed. The brief said 13 words: wrong, 14 is right.
   (3) DONE: the user ran into the field, the characters are shown. Log 15:46: no replay fault, no crash. Task (b) is finished:
       move it to `OLD-WORK_PLAN.md` at the start of the next session. Next: task (a) or the open items below (ask the user).
- Step 2 facts: the device byte +0x60 is always 1. Device init calls d835630 at d831164: it fills a stack record (d8358c4 writes 1
  at +0x60 at d835980, no branch) and copies record +0x0 to +0x6F to the device (`stp q3, q2, [x19, #0x50]` at d8311b8). Scan
  tool: `analysis/scan_strb60.py`. Brief `reports/brief-7b-barrier.md` (its "13 words" was wrong, 14 is right), report
  `reports/writer-7b-1.md`, patch `patches/7b-barrier-1.patch` (LF in the patch, `git apply` kept CRLF). `puller_method` of gpu.c
  ignores the methods 0x02, 0x14 and 0x0B (default case). Two mutants caught. The exe md5 d4021300.

## State and history moved from WORK_PLAN.md (2026-11-09, evening)

- Stage 4 history: steps 1 to 5, task A, A2, task B steps 0 and 1, task (c) (5c1dd1c) and task (b) (5c3f76d) are done. The black
  screen is fixed (7becf0c). The game runs at a steady 30 fps (run 2026-11-09 14:36). The characters are shown (5c3f76d, run
  2026-11-09 15:46).
- The exe in `game/` has the barrier patch (md5 d4021300, equal in the three copies, no diagnostic code).
- PR #1 is closed by the user. Its NVN part is 5c1dd1c (pushed).
- The worktree `wt-7b` was removed. No worktree exists.
- Measure of 2026-11-03: 58 calls read 8 million tokens. 91 percent of it was in calls with more than 100k of context. The base
  context is 25k.

## Session of 2026-11-09 (evening): Claude files in git, issue and PR templates

- `1c5cfea`: `.gitignore` keeps only `game/`. `CLAUDE.md`, `.claude/` (work plans, skills, `settings.json`) are in git now.
  `CLAUDE.md` and `WORK_PLAN.md` do not say "not in git" for these files any more.
- `4e6b015`: `.github/ISSUE_TEMPLATE/` (forms `bug_report.yml`, `graphics_issue.yml`, `feature_request.yml`, and `config.yml`)
  and `.github/pull_request_template.md`. Decisions of the user: English, game screenshots are allowed, Discussions are on,
  the PR template has a required "AI tools" field (if used, name the tools and models). Blank issues are off. Questions go to
  Discussions. The YAML was parsed with PyYAML (scratch venv).
- On GitHub: Discussions are enabled, the label `graphics` is made (`#1D76DB`). The templates show on GitHub only after a push.

## Review of the depth and color view work (2026-11-09, evening, commits a897e21 and 7e48157, moved from WORK_PLAN.md)

- Code reviewer (Codex): brief `reports/brief-review-rt-views.md`, report `reports/review-rt-views-1.md`. Range `4690c52`,
  `a4d649f`, `15648e5`, checked at HEAD. 5 findings, the 4 P2 findings verified in the code by the caller.
- Code writer (OpenCode): brief `reports/brief-writer-rtfix.md`, report `reports/writer-rtfix-1.md`, patch
  `patches/writer-rtfix-1.patch`. The worktree `wt-rtfix` was removed.
- Fixes in `a897e21`: `rt_view_apply` refuses base_layer + layer_count > layout depth ("rt view layers", also for the clear and
  copy paths). `rt_depth_words` refuses a depth word above 0xFFFF ("rt depth layers"). `rt_target_words` refuses a color view
  target above NVN_TEXTURE_TARGET_BUFFER ("rt view target"); depth ignores TARGET. `replay_host_render_targets` logs the
  omitted dependent block once (`LOG_LIMIT(1)`). The old comment "device+0x1A04 is unknown" is gone (it is 0).
- Tests: 6 new tests, 3 old tests changed to real array textures. Build OK, `ctest` OK except `tracked_material`. Two mutants
  caught: no factor fallback (4 checks fail), and no new refusals (all three fail). Exe md5 96ab1617 in the three copies.
- Run 16:38 had `nvn_native=0` (the user had changed the ini), so it did not test the change. Run 16:40 with `nvn_native=1`:
  no refusal, `depth target bound` once, no crash, 30 fps. The characters were missing again: see task (d) in WORK_PLAN.md.

## Task (d) cause found: no transform feedback state on the native path (2026-11-09, evening, moved from WORK_PLAN.md)

NEXT, task (d): the characters are missing again (REGRESSION, run 2026-11-09 16:40, exe `a897e21`, `nvn_native=1`).
- User: the player, NPCs and Pokemon are not shown, and the broken vertices are back. Same symptom as before `5c3f76d`.
- Log 16:40: no crash, 30 fps, 63 frame lines all `faults=0 dropped=0 missing=0`, about 600 draws a frame, no refusal of any
  kind, `depth target bound` once, MME "transform feedback setup" at 18.72 s.
- `a897e21` is NOT the cause: the only source change from the good exe `5c3f76d` is `a897e21`, and it changes output only
  when it refuses. No refusal was logged, so the words are the same.
- Most probable cause (not verified): the barrier fix `5c3f76d` is not complete, and the fault depends on timing. The fix was
  confirmed by one run only (15:46). Mechanism of the bug: the skinning VS `24ba3b3c` writes the vertices by transform
  feedback, the draw reads them, and without an ordering barrier the read can see old data.
- Other differences (probably not the cause): the shader caches were made again at 16:37 by an `nvn_native=0` run (the build
  is part of the key; the xfb state is part of the shader key, `pipeline.c:514`). The ini now has `vsync=2`, `fps_limit=30`.
- Step 1 DONE (runs 16:44, 16:47, 16:48, 16:49, logs in `logs/`): all 4 runs show the bug. The fault is NOT timing (it is
  constant). The run 16:44 made all caches again (1380 shaders translated new, `nvn_native=1`) and shows the bug, so the old
  shader cache is NOT the cause. No `rt view`/`rt depth` fault line, so `a897e21` is not the cause (diff read again).
  Left: the ini (`vsync=2`, `fps_limit=30`, set after 15:46), or the good run 15:46 was wrong. No copy of the `5c3f76d` exe.
  The user thinks that the ini did not change after 15:46 (not sure). So the most probable fact: the good run 15:46 was wrong
  or had another cause, and `5c3f76d` does not fix the characters.
  Step 2 BUILT (2026-11-09, not committed): diagnostic `patches/diag-taskD.patch` (render.c, renderer.c), exe md5 361c8c3e in
  the game. Fact found: the Vulkan renderer itself puts a barrier before a draw that reads a pending XFB result (`gfx_gpu_src`,
  buffer.c:946, `copy_barrier`), so the NVN barrier words are probably not needed for this order. Lines: `diag-xfb` (each
  capture: vs hash, address, size, ctl 0x1C0, layout 0xA00), `diag-read` (a vertex stream over a captured range: same_buf=1
  means it reads the XFB result on the GPU), `diag-sum` (each 30th frame: xfb draws, reads, method_barrier kinds 0 to 5).
  Asked the user for 2 runs to the field: `nvn_native=1`, then `nvn_native=0` (good picture). Compare the two logs (`logs/`).
  Remove the diagnostic before a commit.
  Step 2 RESULT (runs 16:57 native = `logs/2026-11-09_16-57-56.log`, 16:59 non-native = the main log; the user named them the
  other way round): native has 0 `diag-xfb` and 0 `diag-read` lines, so NO transform feedback capture happens at all.
  Non-native: 60 `diag-xfb` from frame 393, vs `24ba3b3c`, 3 buffers, ctl (0x1C0..0x1C2) = 0,3,0xC / 0,1,4 / 0,1,4,
  layout 0xA00 = 0x00222120 / 0x24 / 0x28, r1D1=1. CAUSE: the host program builder (`nvn_program_build_words`,
  nvn_params.c:1247) does not write the transform feedback block of the library builder: `reports/analyst-prog-builder-1.md`
  section 5 (the analyst guessed "0 of 64 programs use it": wrong). The renderer skips a buffer when 0x1C1 and 0x1C2 are 0
  (render.c, the xbound loop), so the skinning output is never written and the draws read old memory.
  The barrier fix `5c3f76d` is not the cause and not the fix (the renderer orders XFB reads itself: `gfx_gpu_src`).
  NEXT (task (d) step 3): verify the TF block in the disassembly (blob+0x600..0x610, the packing loop d854c08..d854d90, the
  header getter that fills the blob), write the brief, the code writer adds the block to the host builder (with a test from the
  values above), build, ctest, then one native run with the diagnostic: `diag-xfb` lines must match the non-native ones.
  Old step 2 text: a diagnostic log that shows if each Barrier op of the replay reaches `vkr_method_barrier`
  (renderer.c:164) and records `vkCmdPipelineBarrier2` between the XFB draw and the draw that reads it.

## Task (d) step 1 done: XFB block verified, brief written (2026-11-09, night, commit 7c0b05a, moved from WORK_PLAN.md)

NEXT, task (d) step 3: the characters are missing because NO transform feedback capture happens on the native path.
- Cause (found 2026-11-09 with a diagnostic, history in `OLD-WORK_PLAN.md`, last section): the host program builder
  (`nvn_program_build_words`, nvn_params.c:1247) does not write the transform feedback block of the library builder
  (`reports/analyst-prog-builder-1.md` section 5; the analyst guessed "0 of 64 programs use it": wrong). The renderer skips an
  XFB buffer when 0x1C1 and 0x1C2 are 0 (render.c, the xbound loop), so the skinning VS `24ba3b3c` writes nothing and the
  draws read old memory. The barrier fix `5c3f76d` is not the cause and not the fix (the renderer orders XFB reads itself).
- The good values (non-native run, `diag-xfb`): 3 buffers, 0x1C0+4i..+2 = {0,3,0xC} / {0,1,4} / {0,1,4}, layout 0xA00+0x20i
  first word = 0x00222120 / 0x00000024 / 0x00000028, then 0. The analyst: header `0x20030000|(0x1C0+4i)` with
  `{blob[0x608+i], n_i>>2, n_i}`, `n_i = u16 blob[0x600+2i]`, and if `n_i != 0` the layout run at 0xA00+0x20i of ceil(w19/4)
  words, from the packing loop d854c08..d854d90 (`blob[0x80i]`, `blob[0x80i+0x200]`, `blob[0x80i+0x400]`, components
  `u8 blob[0x60c+i]`). Condition `blob+0x610 != 0`. The blob is the 0x612 byte header blob at descriptor+0x14 (d836b30).
- Step (1) DONE (2026-11-09): the brief `reports/brief-writer-xfb-prog.md` has the verified facts. The blob is raw header bytes
  `header+0xDC..+0x6EE` (getters d8372dc..d837370 are plain loads, source stage = vertex). Layout byte = `(u8)(TABLE[id]+k)`, TABLE =
  sdk 0xde87370 (64 u16, low byte), id -6..-3 gives 0xFF. Decisions: NVN_PROGRAM_WORDS_MAX 32 to 178, NVN_OP_MAX_PAYLOAD 512 to 768,
  no layout header when the layout is empty, refusals for bad ids, comps > 0x80, layout above 32 words or the library allocation.
- Steps (one per session): (2) the code writer
  adds the block (brief above, worktree `wt-xfb`, patch `patches/xfb-prog-1.patch`), the caller reads the patch, builds, runs
  ctest; (3) one native run to the
  field: the `diag-xfb` lines must match the non-native ones and the characters must show; then remove the diagnostic and commit.

## Facts moved from WORK_PLAN.md (2026-11-09, night, session change before task (d) step 2)

- GitHub: Discussions are on, the label `graphics` exists. The templates show only after the push.
- Stage 4: the black screen is fixed and the game runs at 30 fps (history: the sections above).
- The GPU page table mapping of a pool is connected (0037). The GPU addresses are a bump allocator from 0x400000000. Virtual pools and
  imports fail with a log line. The texture kind is not ported (stored 0).

## Task (d) step 2 done: XFB block in the host program builder (2026-11-09, night, moved from WORK_PLAN.md)

- The code writer (opencode) wrote the patch from `reports/brief-writer-xfb-prog.md` in the worktree `wt-xfb` (removed after).
  Report: `reports/writer-xfb-prog-1.md`. Writer patch: `patches/xfb-prog-1.patch`. Final patch with the test fixes:
  `patches/xfb-prog-2.patch` (nvn_cmdlist.h, nvn_native.c, nvn_params.c/.h, tests/nvn_params.c, tests/nvn_cmdlist.c).
- New: `nvn_program_xfb_words` (pure, nvn_params.c) and `program_append_xfb_words` (nvn_native.c, called in
  `h_program_set_shaders_ext` after the graphics words when the vertex stage exists). A refusal logs
  `program %llx refused: transform feedback: ...` and sets `ok = 0`. `NVN_PROGRAM_WORDS_MAX` = 178, `NVN_OP_MAX_PAYLOAD` = 768.
- Two tests of the writer were wrong: test (c) count 6 must be 18 (streams 1 to 3 with stride 0 add 4 words each), and the
  BindProgram size in tests/nvn_cmdlist.c:127 is 736, not 152. After the fix, ctest passes except `tracked_material`.
  A mutant (table step 5) fails the test.
- The exe in `game/` (md5 f0c83b01) has the patch and the diagnostic `patches/diag-taskD.patch`. Not run yet.

## Task (d) step 3 done: native run verified, XFB fix committed (2026-11-09, evening, moved from WORK_PLAN.md)

- Native run to the field (update program, exe md5 f0c83b01): the characters show (user). The `diag-xfb` lines of the 3 buffers
  match the non-native values (ctl {0,3,0xC} / {0,1,4} / {0,1,4}, layout 0x00222120 / 0x24 / 0x28). 4736 program sets, 0
  refused, 0 `not implemented`, no crash, normal exit at 69.8 s.
- The diagnostic (render.c, renderer.c) is removed (saved as `patches/diag-taskD.patch`). The XFB fix is commit `8c6a511`.
  ctest passes except `tracked_material`. The exe in `game/` is `8c6a511` (md5 3c38c171), not run yet.

## Session change after task (d) (2026-11-09, moved from WORK_PLAN.md)

- Task (d) (missing characters, XFB block) is DONE: commit `8c6a511`. The sections above hold the history.
- The deny of `Artifact`, `ScheduleWakeup`, `ListAgents` in `.claude/settings.json` is committed. `/context` after it: System tools 6k
  (was 20.1k), deferred system tools 18.8k. The claude.ai skill sync (about 2.7k) and the Claude Docs connector were still on.

## Task (a) step 1 explorer result (2026-11-09, moved from WORK_PLAN.md)

- Plan agreed by the user: step 1 `shader_hash` end rule (4 shaders), step 2 `pipeline.c:449` keeps a shipped result when no cbuf
  read succeeded (6 + 4). One commit for each step.
- Explorer (`reports/explorer-shader-2.md`, brief `brief-shader-2.md`): the rule length (EXIT + 1) * 8 equals the draw length for
  adb94885 2992, a3c1291e 2976, 45a7cf5b 3528, 9ff45b16 2480. Every BRX target is before the EXIT. Over 1439 programs: all end with
  an always-run EXIT and a self-BRA, no target after the self-BRA, the rule never changes a resolved scan. 117 programs have inline
  data after the self-BRA (floats, tables). The 6 jump-table programs get 8 to 64 bytes shorter (draw length not verified, no record).

## Task (a) step 1 done (2026-11-09, moved from WORK_PLAN.md)

- Commit `9036dc8`: `shader_hash` ends at the last always-run EXIT before an always-run self-BRA when a BRX table is not read
  (`resolved == 0`, `i >= furthest`, `last_exit >= furthest`). Other cases do not change.
- The caller verified a3c1291e (`main.bin@0x4041c30`): BRX 111, EXIT 371, self-BRA 372, len 2976 = draw len.
- Code writer (opencode): brief `reports/brief-a1-writer.md`, patch `patches/a1-hash-1.patch`, report `reports/a1-writer-1.md`.
  The patch was correct at the first try. 4 tests in `tests/shader_brx.c`. Mutant (`t == i + 1`) failed 2 tests. `ctest`: only
  `tracked_material` fails (known). Exe md5 a63e12b7 copied to the 3 places. Worktree `wt-a1` removed. Not run in the game.

## Task (a) step 2 done, task (a) closed (2026-11-09, moved from WORK_PLAN.md)

- Task (a) is done. Run 21:34 (`nvn_native=1`): the shipped file has 1390 shaders (1380 before, +10 as expected), made
  by a run at 21:31 whose log was overwritten. The 10 old hashes are absent because the new lengths change the hash. 0
  `not translated`, 0 `not implemented`, normal `Exit.` at 74.5 s. The user saw no change on the screen.
  Step 2: `sh_spirv` keeps a shipped result whose reads all failed and drops its reads (`cbr.n = 0`). Code writer patch
  `patches/a2-ship-1.patch` (correct at the first try), no test (no test links `pipeline.c`). `ctest`: only `tracked_material`
  fails. Exe md5 d050d20e copied to the 3 places. Worktree `wt-a2` removed. Step 1: `9036dc8`.
- Task (a), plan agreed by the user: step 1 `shader_hash` end rule (4 shaders), step 2 `pipeline.c:449` keeps a shipped result
  when no cbuf read succeeded (6 + 4). Step 2 changes the draw: a shipped entry with no read matches every draw, so the runtime
  PC switch replaces the draw-time translation. Ask the user for a run after step 2. One commit for each step.
- Commit of step 2: "Keep a shipped shader when none of its constant buffer reads succeeded". User: task (a) closed.

## Session of 2026-11-09 (night): task (u) planned (moved from WORK_PLAN.md)

- Tasks (a) and (b) are closed (sections "Task (a) step 2 done, task (a) closed" and "Task (b) done" above).
- The State of WORK_PLAN.md said "NOT PUSHED: 9aa4635 and later". Wrong: 19b6d35 and older were pushed. Corrected.
- User chose task (u), the 249 unobserved functions. Plan U1 to U5 in WORK_PLAN.md, "Next".

## Task (u) U1 step (a) done (2026-11-10, moved from WORK_PLAN.md)

- Nine library analyst runs (opencode, DeepSeek V4.1 Flash), 2 at a time: briefs `reports/brief-analyst-u1-common.md` and
  `brief-analyst-u1-{A..I}.md`, tables `reports/analyst-u1-{A..I}-1.md`, 118 functions (A SamplerBuilder, B Sampler and
  SamplerPool, C and D TextureBuilder, E and F Texture, TexturePool, Buffer, G MemoryPool and BlendState advanced, H Window,
  I Event builder and MultisampleState sample locations).
- 3 to 6 rows of each table checked in the disassembly, all equal. One error: D gives host `tile` at 0x60 (0x5c).
- The enum argument trace (missing part of 4-u1) comes after U1 (user, 2026-11-09). Commit `Record the U1 library tables`.

## Task (u) U1 step (b1) done (2026-11-10, moved from WORK_PLAN.md)

- Decisions of the user (2026-11-10): (1) the 4 storage class and ZCull getters wait for the device words (step b2). (2) Buffer
  and Texture Finalize keep the host record (the library Finalize is one `ret`), as the sampler does. (3) EventGetMemoryPool
  and EventGetMemoryOffset move to U2. The 5 EventBuilder functions stay in U1.
- Brief `reports/brief-u1-b1.md`, handler writer (opencode, DeepSeek V4.1 Flash) in worktree `wt-u1` (removed), patch
  `patches/u1-b1-1.patch`, report `reports/writer-u1-b1-1.md`. Facts of the brief: getters read the host record under
  `objs_lock`, never guest memory; new host setters (TextureBuilder SetSamples, SetGLTextureName, SetPackagedTextureData,
  SetSize1D, SamplerBuilder SetBorderColori/ui, Window and WindowBuilder SetNumActiveTextures); the library stores
  interval-1 (d83e5b0), the host stores the interval; table D gives `tile` at 0x60, it is 0x5c.
- Caller fixes: the writer's fixed event builder side table (128, never freed) became objtab kind `NVN_OBJ_EVENT_BUILDER`; the
  sample location NaN takes 0.9375 (fminnm, d83b734, checked in d83b530..d83b768); a uint8_t test constant.
- Only the pure parts have unit tests (no test links `nvn_native.c`). 9 mutants, all killed. `ctest`: only `tracked_material`
  fails (known). Commit `2c2b25e`: 105 handlers, coverage 379 implemented, 144 stub-log. Exe md5 6a1fd3c9. No game run.

## Task (u) U1 step (b2) analysis done (2026-11-10, moved from WORK_PLAN.md)

- Brief `reports/brief-explorer-devwords2.md`, game explorer (opencode, DeepSeek V4.1 Flash), report
  `reports/explorer-devwords2-1.md`. Commit `Record the U1 step (b2) device word analysis`.
- Checked in the disassembly: d830fc0 is `ldr w8,[x0,#0x1a08]; cmp; cset` (not a device query). d8358c4 (d835a44..d835a9c)
  copies the ZCULL_GET_INFO reply (+0x238..+0x25c) to device +0xb8..+0xdc and the ZCULL ctx size (+0x230) to +0xe0. Our
  values: `gm20b_zcull_info` (`nvdrv.c:1366`). Device +0x1a08 is a value record from de08020 (1, 0x22555555, 0xff222222, ~0).
- Not checked: the kind table of d83563c (part b, from a unicorn run) and the block formula of d869610 (part c).

## Task (u) U1 step (b2) checks and decisions (2026-11-10, moved from WORK_PLAN.md)

- Block formula `d869610` and caller `d83cf88..d83cff0` checked: the switch index w3 is 1 or 2 (`cinc` at `d83cebc`), table
  `de8833a` entries 1 and 2 keep the count. Size = `+0xc8 * blocks + +0xcc + +0xd0`.
- Kind table `de7f0b1`: kinds 2b, 08, 52, 87 have bit 0 set and second byte 2a, 01, 46, 7b (the w3=0 kinds). cc/e0/cd give fe.
- Decisions of the user: flags bit 7 set gives a log line and a refusal (no info path). Device +0x1b1c: analyze again.

## Task (u) U1 step (b2) device +0x1b1c found (2026-11-10, moved from WORK_PLAN.md)

- Brief `reports/brief-explorer-devwords3.md`, game explorer (opencode), report `reports/explorer-devwords3-1.md`. Commit
  `Record the U1 step (b2) device +0x1b1c store`.
- Checked: the device constructor `d8310a0` writes 1 at `d831cb4` (`strb w8,[x20,#0x124]`, x20 = device+0x19f8 from
  `d831150`) after the display context is made. `d831cc8` writes 0 only if device+0x1a00 has bit 1 clear and bit 2 set.
- Not checked: device+0x1a00 comes from the NvOs config value "0x9abdc5" (`d835784` at `d831608`). The selftest read 1, so
  the value is most likely 0. The selftest reads guest memory (`nvn_selftest.c:209`).

## Task (u) U1 step (b2) done (2026-11-10, moved from WORK_PLAN.md)

- Brief `reports/brief-u1-b2.md`, handler writer (opencode, DeepSeek V4.1 Flash), patch `patches/u1-b2-1.patch`, report
  `reports/writer-u1-b2-1.md`. Commit `c5fe2db`.
- Handlers: TextureBuilder and Texture GetStorageClass, GetZCullStorageSize. Pure helpers `nvn_texture_storage_class` (kind
  tables of d83563c for `w3=1`, bit 8 and bit 12 corrections, `w3=0` kinds) and `nvn_texture_zcull_size` (normal path).
  New `NvnZcullDeviceWords` (must stay equal to `gm20b_zcull_info`). Flags bit 7 (info path): log line and 0.
- The no-surface rule comes before the bit 7 refusal: a color format with bit 7 gives 0 with no log.
- Caller added the samples 1 and 2 tests. 14 mutants, all killed. `ctest`: only `tracked_material` fails (known).
  Coverage 383 implemented, 140 stub-log. Exe md5 10160cb0. No game run (the 4 functions have call count 0).

## Task (u) U1 step (b3) analysis done (2026-11-10, moved from WORK_PLAN.md)

- Brief `reports/brief-u1-b3a.md`, library analyst (opencode, DeepSeek V4.1 Flash), table `reports/analyst-u1-b3-1.md`, accepted.
  Commit `f4fa50a`. Disassembly `analysis/b3_check.txt`, `analysis/d83d01c.txt`.
- Checked: d83dd40 calls d822300 and d822538 (not d869e10). d83d810 returns `record.address - T[+0x80]`. d83d01c slice
  offset: 3D gives a 32-bit term `<< 9`, other targets give `T[+0x58] * base_layer`.
- Sparse output, 16 bytes: level count, tile width, height, depth. Same as `texture_sparse_tile` (`nvn_params.c:916`) except
  levels == 0 (the library writes zeros, the host refuses).
- View offset of an array target uses `layout.layer_stride` (as `nvn_rt.c:236`), not `texture_level_offset`. The library
  checks no level or layer range.

## Task (u) U1 step (b3) writer patch done (2026-11-10, moved from WORK_PLAN.md)

- Brief `reports/brief-u1-b3.md`. The caller checked the decisions in `analysis/d822538.txt` and `analysis/d83d01c.txt`:
  - Sparse layout (d822538): the level count is the first level that is smaller than one tile. The sizes are the raw builder
    values. Count 0 (also levels == 0) gives all 4 words 0. This is not a refusal.
  - View offset (d83d01c): the base level is used only with NVN_VIEW_LEVELS, the base layer only with NVN_VIEW_LAYERS.
    Linear and BUFFER textures give 0. 3D layer term is u32, then `<< 9`. Other targets use `layer_stride * layer`.
    A base level >= 32 is a refusal (the shift count wraps).
- Handler writer (opencode, DeepSeek V4.1 Flash): patch `patches/u1-b3-1.patch` (4 files, +241/-2), report
  `reports/writer-u1-b3-1.md`. The caller read the whole patch and found no fault. The patch also moves the sample divisors
  of `nvn_texture_layout` to `texture_sample_x` and `texture_sample_y`. Worktree removed. Commit `01c2ecd`.

## Task (u) U1 step (b3) done (2026-11-10, moved from WORK_PLAN.md)

- Commit `c590af9`: the handlers of nvnTextureBuilderGetSparseTileLayout, nvnTextureGetSparseTileLayout and nvnTextureGetViewOffset
  (patch `patches/u1-b3-1.patch`). Build OK, `ctest` passes except `tracked_material` (known). Mutants: sparse 3D z exponent 4->3
  and view 3D layer shift 9->8, both caught by `nvn_params`. `nvn-coverage.md`: G10 144 implemented, 137 stub-log. Exe md5 76d641aa.
  No game run (no selftest or exectest case).

## Task (u) U1 closed (2026-11-10, moved from WORK_PLAN.md)

- No getter or builder of Sampler, Texture, Buffer, MemoryPool, Window, the pools, BlendState or MultisampleState is still
  stub-log in `nvn-coverage.md`. The remaining rows of these objects are texel IO, mapped ranges, MapVirtual (U3), finalizers and
  GL sync (U5). No step b4 was necessary.

## Enum argument trace (4-u1) step (a) done (2026-11-10, moved from WORK_PLAN.md)

- Library analyst table `reports/analyst-enum-1.md` (commit `08a2ef9`): Table 1 has about 90 register arguments (enum and
  flags) of the 242 observed functions, Table 2 the enum fields read from structs. Checked in the disassembly: BlendFunc
  d83ac30, DepthFunc d83b060, TextureBuilderSetFormat d83c2f0, ChannelMask d83ade0. Open: QueueFenceSync argument order
  (w3 & 1 only), ClearColor word meaning, SetShadersExt stage offset (guess 0).

## Enum argument trace (4-u1) step (b) done (2026-11-10, moved from WORK_PLAN.md)

- Decision of the user (2026-11-09): the enum trace (the missing part of 4-u1) comes after U1. U1 is closed (sections
  "Task (u) U1").
- Decisions of the brief `reports/brief-enum-b.md`: a pure module `nvn_enum_trace.c/.h` (table of the w register masks of
  Table 1, enum and flags, not the bool enables `tst #0xff`; a seen set of 64 keys (register, value) for each function).
  `proc_trace` calls it before the size, value and queue branches, and logs `nvn-enum: <name> wN=<value>` once. Table 2
  (struct fields) is not traced: the setters of those records are traced. `nvnDeviceGetInteger` stays `VALUE_GET_INTEGER`.
- Code writer patch `patches/enum-b-1.patch` (63 rows, 6 files). `nvnPolygonStateSetPolygonOffsetEnables` keeps mask 0x02 (a
  bit field of three enables, not a bool). Not built yet.

## Enum argument trace (4-u1) step (c) and first run (2026-11-10, moved from WORK_PLAN.md)

- Commit `b9d35bb`: patch read fully (rows CopyTextureToTexture w7, ClearTexture w5, BindProgram w2 checked against the table),
  `ctest` passes except `tracked_material`, 3 mutants caught (a table mask, the full limit, the register in the key). Exe md5
  95cfea4d in the 3 places.
- Run 1 (user, update program to the field, `nvn_trace=1`, 72 s, no crash): 371 `nvn-enum:` lines, 62 of 63 functions logged.
  Lines saved in `analysis/enum-run1.txt` of the scratchpad.
- Error of the analyst table: DrawElements, DrawElementsInstanced and DrawElementsBaseVertex have mode w1, index type w2, count
  w3 (log values 6, 0x21c, 0x204; host handler `nvn_native.c:3426` reads type x[2], count x[3]). The mask 0x0a logs the count
  and fills the seen set of DrawElements (one "seen set full" line). The index type is not logged.

## Enum argument trace (4-u1) fix and second run (2026-11-10, moved from WORK_PLAN.md)

- Commit `6f98b42`: DrawElements, DrawElementsInstanced and DrawElementsBaseVertex masks 0x0a to 0x06, test row
  `nvnCommandBufferDrawElements` 0x06. `ctest` passes except `tracked_material`, the mutant (0x0a) is caught. Exe md5 25d58048.
- Run 2 (user, update program, `nvn_trace=1`, no crash line): 277 distinct values, all 63 functions. Same set as run 1 except
  DrawElements: mode 4 and index type 1 only. Saved in `analysis/enum-run2.txt`. Result in `docs/nvn-design.md` 4-u1 (commit `6f09358`).

## U2 events and sync, step (a) and task 1 (2026-11-10, moved from WORK_PLAN.md)

- Step (a), commit `e69f838`: library analyst tables `reports/analyst-u2-A-1.md` (events, 11) and `analyst-u2-B-1.md` (sync,
  queue, finalizers, 12). Rows checked in the disassembly: EventGetValue, GetMemoryPool, GetMemoryOffset, Signal (mode 1 adds,
  other modes write), SyncFinalize, QueueWaitSync (tail call to d83beb0).
- Facts of the tables: the host engines (`gpu.c`, `maxwell/engines.c`) do not do semaphore reductions, they only write.
  WaitEvent parks the GPU stream with no timeout. CB FenceSync and WaitSync append library records 5 and 6 (no GPU method).
  QueueWaitSync appends GPU waits and does not block. Sync kind 4 is a GL sync.
- Task 1, commit `94754a7`: 18 CPU side handlers by the code writer (brief `reports/brief-writer-u2-1.md`, report
  `writer-u2-1.md`). New `NVN_OBJ_EVENT`, `EventRec`, `event_value_address`, `nvn_event_signal_value` (unit test),
  `frames_in_flight` (+1 at the present post, -1 in `frame_finish`). Decisions: `docs/nvn-design.md` 4-u2.
- Checks: the whole patch read, `ctest` passes except `tracked_material` (`driver_workflow` passes alone), the mutant of the
  signal rule is caught. `nvn-coverage.md`: G10 has 119 `stub-log` rows. Exe md5 878c4351 in the 3 places. No game run.

## U2 events and sync, task 2 (2026-11-10, moved from WORK_PLAN.md)

- Commit `1b05ba0`: CommandBuffer SignalEvent, WaitEvent, ReportValue, FenceSync, WaitSync by the code writer (brief
  `reports/brief-writer-u2-2.md`, decisions 1 to 8, report `writer-u2-2.md`). New ops `NVN_OP_SIGNAL_EVENT`, `WAIT_EVENT`,
  `REPORT_VALUE`, `FENCE_SYNC`, `WAIT_SYNC`.
- Mechanism: the host Maxwell model does not emulate the report reduction (ADD) and parks on an ACQUIRE, so Signal/Wait
  Event write or check the CPU word at replay (no GPU words). A not met wait logs and counts `unmet_event_waits` in the frame
  line, the replay goes on (one GPU thread, no library timeout). ReportValue sends 0x6C0 words with query 0xF002, not the
  0x800003E0 word (no action in the host model). FenceSync/WaitSync act at submit (`queue_scan_list`, follows CALL to
  `NVN_CALL_DEPTH_MAX`): a fence is a frame token, a wait logs an unsignaled or removed sync.
- The writer took the SignalEvent arguments wrong. The library (d832ef0) reads mode w2, location w3, flags w4, value w5
  (`analysis/u2-sig2.txt`). The caller fixed it.
- Checks: the whole patch read, `ctest` passes except `tracked_material`, 4 mutants caught (wait compare, replay dispatch,
  report value word). `nvn-coverage.md`: G10 has 114 `stub-log` rows. Exe md5 f0d4863f in the 3 places. No game run.

## U3 copies, clears, texel IO, step (a) (2026-11-10, moved from WORK_PLAN.md)

- Briefs `reports/brief-analyst-u3-common.md`, `-A.md`, `-B.md`. Tables `reports/analyst-u3-A-1.md` (15 command buffer
  copies and clears) and `analyst-u3-B-1.md` (11 copy strides, mapped ranges, texel IO). DeviceRegisterFastClear* stay in U5.
- Checked in the disassembly: copy strides (str/ldr cb+0x88 and +0x8C), ResolveDepthBuffer (one word 0x8001047F),
  ReadTexels (x4=x5=0, tail to d83e030), ClearBuffer (size 0 returns, first word 0x91070002, size>>2 at +0x2C and +0x38).
- Easy rows: ClearColori/ui have the body of ClearColor (register the existing handler). Invalidate (buffer, pool, texels)
  is the flush path with pool slot 0x20 (reuse the no-op flush handlers). WriteTexelsStrided reuses the write helpers with
  x4/x5. ReadTexels(Strided) needs a read helper. Get/SetCopy*Stride need two fields in `CommandBufferRec`.
- Open decisions (caller proposal: refuse with a log line unless a design exists): MapVirtual (no sparse pools, return
  false and log), B2T/T2B flip bits and bit1, TiledDownsample (device+0x9AC..+0x9B8 not decoded), ResolveDepthBuffer (no
  Vulkan depth resolve), Discard gate device+0xA8 (writer not found), UpdateUniformBuffer (payload over 768 B), CopyCommands
  (deep copy, or NVN_OP_CALL with a fence assumption).

## U3 open items, step (b1) (2026-11-10, moved from WORK_PLAN.md)

- Decision of the user: no refusal for the 7 open items (MapVirtual, B2T/T2B flags, TiledDownsample, ResolveDepthBuffer,
  Discard gate, UpdateUniformBuffer over 768 B, CopyCommands). Make a sure design for each one.
- Two game explorers: briefs `reports/brief-explorer-u3-common.md`, `-A.md`, `-B.md`. Reports `explorer-u3-A-1.md`,
  `explorer-u3-B-1.md`. Checked in the disassembly: B2T route bit1 (d82b53c), MIRROR_Y (d82b7e8), device+0xA8 = 0
  (writer d835a2c, gate d8564d0), cb+0x70 bit4 = device+0x1A00 (d823d24), PipeNop 0x68B (d84ca10).
- No `main` caller for any of the 7. Only CallCommands is called (aab1a74).
- Facts for the design:
  - Copy flags: 0x1 LINEAR_FILTER (not used here), 0x2 ENGINE_2D (B2T only, 2D engine, format change), 0x4 MIRROR_X (B2T 2D
    route only), 0x8 MIRROR_Y and 0x10 MIRROR_Z (both routes, negative row or image stride). Strides cb+0x88/+0x8C replace
    the tight strides when not 0.
  - TiledDownsample: device +0x9AC/+0x9B0 = program 1 offset/register count, +0x9B4/+0x9B8 = program 5. No writer in sdk
    (value 0). Driver program pass with macro 0xE1E. Box 2x2 filter is a guess.
  - ResolveDepthBuffer: method 0x47F ResolveDepthValues, an in-place depth compression resolve, not an MSAA resolve.
  - MapVirtual: the game makes no pool with flag 0x200. The library returns false for a non-virtual pool.
  - Discard: method 0x3DE DiscardRenderTarget, contents become undefined. Gate device+0xA8 is always 0.
  - UpdateUniformBuffer: data copied into the stream at record time, ordered with the draws. Bytes 1..3 of a tail drop.
  - CopyCommands: copies the word blocks that the source owns (record byte+0x14 bit0), the rest stays a reference.
- Trap: a `for` loop with a failed `grep` launched both agents, then a third. Parse the
  launch JSON with spaces after the colon (`"handle": "..."`).

## U3 design and patch 1 (2026-11-10, moved from WORK_PLAN.md)

- Step (b2): the design of the 7 open items is in `docs/nvn-design.md`, "U3 design of the open items" (`acec5d0`, approved
  by the user). MIRROR_Y/Z: one DMA launch for each row or layer, because `dma.c` keeps the pitch as u32. TiledDownsample:
  `vkCmdBlitImage` linear. MapVirtual: virtual pool and `as_remap` of `nvdrv.c`. Discard, ResolveDepthBuffer: logged no-ops.
- Patch 1 (`c4f60c5`): 19 rows. Brief `reports/brief-writer-u3-1.md`, patch `patches/writer-u3-1.patch`, report
  `reports/writer-u3-1.md`. New ops `NVN_OP_COPY_BUFFER`, `NVN_OP_CLEAR_BUFFER`, `NVN_OP_UPDATE_UNIFORM`. New pure functions
  `nvn_copy_buffer_words`, `nvn_clear_buffer_words`, `nvn_update_uniform_words`, `nvn_texture_read_rows`, `nvn_oplist_copy`.
- Facts: `replay_bind_send` reaches the DMA engine on subchannel 4 (`gpu.c` binds the DMA class there). Method 0x68B only
  stores a register in `maxwell3d.c`. `command_record` returns 1 or 0. `texture_write_check` takes the operation name,
  the direction and the strides. Copy strides are `CommandBufferRec.copy_row_stride`/`copy_image_stride`.
- Caller fixes of the writer patch: a payload size check before `op->words` in `nvn_replay.c`, a test that kept the
  owner reference of a list, a joined test line. 6 mutants failed. `ctest` passes except `tracked_material`.
- Trap: `git worktree remove` needs `--force` when the writer left edits (the patch file has them). The coverage file is LF;
  its checks cells have two spaces when empty (`|  |`).

## U3 patch 2 (2026-11-10, moved from WORK_PLAN.md)

- Brief `reports/brief-writer-u3-2.md`, patch `patches/writer-u3-2.patch` (writer), `patches/writer-u3-2-fixed.patch`
  (with the caller fixes), report `reports/writer-u3-2.md`. 6 rows: CopyBufferToTexture, CopyTextureToBuffer, Downsample,
  DownsampleTextureView, TiledDownsample, TiledDownsampleTextureView. New ops `NVN_OP_COPY_BUFFER_TEXTURE`, `NVN_OP_DOWNSAMPLE`.
  New pure functions `nvn_rt_copy_dma_words`, `nvn_rt_copy_dma_check`, `nvn_rt_copy_buffer_texture_2d_words`, `nvn_rt_downsample_words`.
- Downsample and TiledDownsample use the 2D block (fermi2d, then the renderer blit). The tiled form sets SAMPLE_MODE bit 4
  (bilinear). The renderer blit is linear only for a live render target source with other sizes, else `f2d_blit_memory`
  point samples. A negative step goes to the memory path (`vk_blit2d` returns 0).
- Downsample factors: index ror32(samples, 1) - 1 gives 2x1, 2x2, 4x2, 4x4 for 2, 4, 8, 16 samples (writer's inference from
  d83d3c0: record +0x68 holds the sample pattern).
- Caller fixes: the 2D source start of a mirror was not 32.32 fixed point, a DMA origin over 16 bits was cut silently (now a
  refusal), Downsample lacked the close marker 0x91000002, the buffer side depth word was 0, two test headers had count 4
  for 6 words, a missing include in `tests/nvn_replay.c`. 7 mutants failed. `ctest` passes except `tracked_material`.
- Not verified in the game: no `main` caller of the 6 functions.
