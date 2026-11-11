# Work plan: what to do now

Read `CLAUDE.md`, then this file. Read `OLD-WORK_PLAN.md` only when you need a fact (it is long). Move a finished task to
`OLD-WORK_PLAN.md`. One task, one session: start a new session at each stop (see "Why the sessions use much of the limit" there).


## State

- PUSHED: `c5fe2db` and older. Commits after `c5fe2db` are not pushed. History:
  `OLD-WORK_PLAN.md`, last sections.
- `CLAUDE.md` and `.claude/` are in git (`1c5cfea`). Local paths in these files become public at the push.
- The exe in `game/` is the build of the d1 fix commit (md5 4750291d), no diagnostic. The diagnostic code of task B is saved as `patches/diag-taskB.patch`
  (lines `diag-present`, `diag-rt`, `diag-last`, `diag-skip`).
  `ctest` passes except `tracked_material` (known; `driver_workflow` can fail in a parallel run). The `.ini` of the game has
  `nvn_trace=1`, `nvn_native=0`, `nvn_selftest=1` (the selftest run of 2026-11-10). After each build: `tasklist` shows no game, then copy the exe.
- GitHub PRs #2 to #6 (opt-in 60/120 fps clocks, colardocookie) and issue #7 (a contributor offer): ON HOLD, frame rate is later
  work. The user replies to them. Facts: `OLD-WORK_PLAN.md`, section "PR #1 NVN part taken".
- Scratchpad: `C:/Users/Azunel/AppData/Roaming/orca/chat-scratch/f41e32fc08995f90ab471cdfd0b7bdb8` (`analysis/` holds disassembly
  text files and `read_sdk.py` (`dis`, `words`, `callers`, run it in the MSYS2 CLANG64 shell with `SNX_SCARLETNX` set; the files
  `write_texels.txt`, `blwriter.txt`, `register_image.txt` are the disassembly of group 7), `reports/` the agent reports and briefs
  (`brief-6a.md`, `brief-6b.md`, `brief-5-writer.md` are good templates), `patches/` the writer patches, `edit_*.py` edit scripts, `design3c.md`).
  Rule (lesson of 2026-11-04): after each commit run `git show --stat` and compare it with the files of the change.
- Commit messages: PowerShell 5.1 does not pass a here-string to `git commit -F -`. Write the message to a file in the scratchpad
  and use `git commit -F <file>`.
- Setup: Claude Code subagents are denied (`.claude/settings.json`). Use the Orca agents of the skill `orca-agents`: code
  reviewer (`codex`, Sol, high). Game explorer, library analyst, handler writer and code writer are `opencode` with
  `opencode-go/deepseek-v4.1-flash`, started WITHOUT `--effort`. Its output is weaker: check at least 3 rows of every table, read every
  patch fully, and run a mutant of each new test. Launch from the main folder (a scratchpad worktree is not managed by Orca), name the
  work folder by absolute path. `terminal wait` does not work for opencode: use a background loop that waits for the report and patch
  files. Close the terminal of a finished agent (`orca terminal close`). The `Co-Authored-By` line of the agents: `OpenCode (DeepSeek V4.1 Flash)
  <noreply@opencode.ai>` for the explorer, analyst and writers (the real model, user 2026-11-07), `GPT-6.1-Sol (Codex)` for the reviewer.
- Answer the user in Japanese, in short and exact sentences. Push only when the user asks. The user wants to delegate the code writing
  to the code writer: the caller writes the brief (decisions, lock order, existing functions), reads the whole patch, builds and tests.
- Rule of the user (2026-11-03): the code writer writes all code except very light edits (a constant, a few lines of glue). The caller
  writes the brief (decisions, lock order, existing functions, the formulas checked in the disassembly), reads the whole patch, builds,
  tests and checks the host (lock, lifetime, thread). Look in each patch for a zero divisor, a NULL pointer and a missing refusal.
- The full playthrough comes last (decision of the user). Do not ask for it before the other work is done.
- Rule of the user for the window and sync work (2026-11-03): the host owns the objects, no compatibility words go into the game's
  memory, and an unknown part stops with a log line. No faked success and no sync completed without a cause.

## Next

The native plan (decision of the user, 2026-11-02, `ROADMAP.md` phases 6 to 10), in this order: 1. graphics, NVN on the host to
Vulkan (no GPU command stream), 2. input and camera, 3. host replacement of SDK functions, 4. shaders and pipelines, 5. extracted
RomFS and a file layer for mods.

Phase 6 (graphics) follows `docs/nvn-plan.md` (five stages) and `docs/nvn-design.md`. Stages 1 to 3 are done except the open steps
below. Design of 3c: `docs/nvn-design.md`, "3c Host design decisions", decisions 6 to 13.

NEXT: task (u), the unobserved functions (user, 2026-11-09). Plan: `docs/nvn-design.md`, "Unobserved functions", steps 4-u2 to
  4-u4. `docs/nvn-coverage.md`: 1 row of G10 is `stub-log` (`nvnDeviceGetProcAddress`, by decision) after U5 (`11978be`).
  Current step: 4-u3-3 step (d), group d2 (d1 history: `OLD-WORK_PLAN.md`, last section "4-u3-3 step (c) and d1").
    Groups of (d): d1 fixed-function state (29 rows, committed `writer-u3-3d1`), d2 binds and resources, d3 draws,
    clears, copies, dispatch, d4 CPU side (queries, callbacks, debug markers). Then 4-u3-4 (exectest). The `unit`
    scope is not in the checks column yet.
    d1 DONE (2026-11-11, exe 4750291d): 286 cases, 0 failed, 8 expected no-op, 0 skipped. History: `OLD-WORK_PLAN.md`,
    section "d1 faults fixed". Next: group d2 (binds and resources), first a library analyst table of the d2 rows.
    The `.ini` has `nvn_selftest_mutate=` (empty) now.
  - Open for 4-u4: the design asks for no `stub-log` row, but `nvnDeviceGetProcAddress` stays `stub-log` by decision (the
    game resolves names through d832940, which the hook covers). Decide the row (a handler that calls the resolver, or an
    explicit exception in the design) before 4-u4.
  - The enum trace values (`analysis/enum-run1.txt`, `enum-run2.txt`) are the observed scope of each function.
- Open: 0xD23/0xD27 get 0x230/0x430 (offsets of driver programs that the host does not have). queue+0x16C0 stays zero (a command
  buffer overflow in the init builders would crash).
- Diagnostic code: `patches/diag-taskB.patch`. Apply it only for a diagnostic run, and remove it before a commit.
- Known edges: a vertex buffer bind of size 0 is not sent (decision 7). Open for compute: gpr and sass bytes are 0, the rest of
  cbuf 0 is zero, cbuf 2 textures are refused (no scene needed it yet).

A writer patch can come as UTF-16 (PowerShell redirect): regenerate it with `git diff` in the worktree and `git apply --check` it.
Read the patch, then build and run `ctest`, before you read the tests: the writers get float constants and keep masks wrong.
The writers also put their own faults into the test values (patch 2: the 2D start not in 32.32, header counts). Check each
expected word against the register layout, not against the code.

Notes:
- Method of the agent work (explorer or analyst, brief with decisions, writer in a detached worktree, read the whole patch, build,
  `ctest`): `OLD-WORK_PLAN.md`, section "NVN stage 3, 3d steps 3 to 5" (line ~1008). Remove the worktree after the patch.
- Edit CRLF files with the Edit tool. `sed -i` in Git Bash changes CRLF to LF (restore with `unix2dos`, check with `file`).
- Wait for an agent with a background loop on the report or patch file (stable for 60 seconds). Read every patch fully.
- `gui_sdl_events` can time out in the parallel `ctest` run. Run it alone before you call it a fault.
- `tracked_material` fails on two SMAA `.dds` files (not caused by this work). Tell the user; the fix is a list entry or a moved
  file, and the user decides.
- After each fix of the runtime, ask the user for one run (`nvn_native=1`, update program, wait 10 seconds, close), then read the log
  with `nx-log-read`: the lines `nvn-native: not implemented` and the crash block.

## Open tasks

- Debug window: CPU temperature and power, AMD (ADL) and Intel GPUs (they show as ?).
- Not in the windows: the player maps (`gamepad_pN_map`) and the players' rows. Only the old menu has the save states and the edit mode
  of the on-screen controls. The stats corner as a window.
- Review of `0016` not fixed: finding 5 (the first frame after the pointer arrives gives an arrow key to the game) and finding 8 (a
  design choice).
- Remove the test window (`imgui_test`, its Debug menu entry) at the end of the window work.
- Not done yet: make `reports/brief-template.md` (the fixed rules, line ends and patch steps of the briefs) and a code map
  `.claude/CODE_MAP.md` (function names and line numbers of `nvn_native.c`). The user checks `/context` for the 25k base and may
  switch off the skills and MCP servers that the project does not use.

## Phase 7 input (the native keyboard and mouse input is done in the field, scenes left to test)

Facts: `docs/input-analysis.md`, `docs/camera-analysis.md`.

- Not tested: the native mouse look in cutscenes, dialogs, menus, battles and the other camera modes. The mod adds the mouse to the
  setter whenever it runs, so a scene whose camera the player does not control may move. If it does, gate the stub on a game state (a
  flag in the camera object).
- Not done: the launcher must write `mouse_native_address` and keep the mod in the mods folder (the mod is for the update 1.1.1 `main`,
  its build ID is in the file). The mouse buttons are fixed (left ZR, right ZL): no wheel, no remap.
- The stick based mouse look (`mouse_sensitivity`, `mouse_curve`, `mouse_dead_zone_offset`) stays for `mouse_native_address=0`.

## Later (from `ROADMAP.md`)

- Test the scenes that are not tested: video (movies), battles against wild Pokemon of the later areas. Read the logs of each run for
  misses (`nx-log-read`).
- Game enhancements such as the frame rate are later work (the user's decision). Do not start them now. Facts: the game asks for the
  swap interval 2 (`docs/stutter-analysis.md`), the GPU uses about 6 ms of 33 ms.
- Not explained: the pcs 0x8cbe780 (main) and 0xd92f310 (sdk), the cause of the PLT-stub misses (none seen in the last runs), and the
  slow frames of 50 to 85 ms (stopped by the user, `docs/stutter-analysis.md`).
- Optional: the patch that turns the JIT off, to check that no miss is hidden.
- Phase 11 (launcher, Tauri) and phase 12 (release): not started.

## How to work

- Keep the context small (the measure is in `OLD-WORK_PLAN.md`; the base context is 25k). Rules:
  - One session for one step: (a) brief and launch, (b) review, build and commit of the patch, (c) log reading and analysis. Update
    "State" and "Next" at the end of each step, then tell the user to start a new session.
  - Read a patch with `git apply --stat` first, then `git diff -U0` or the added lines. Read a source file with `Grep` first, then
    `Read` with `offset` and `limit`. Do not read a file twice.
  - Read a log with `grep -v "host handler for"`, `head` or `tail`. Write long disassembly to a file and read the part that you need.
  - Work of more than 3 disassembly calls goes to the game explorer. Read its short report only.
  - Answers to the user are short: no table unless the user asks for it.
- One request, one step. When a step is finished (an analysis, a patch, a build, a run), check the result, prepare the next step (brief, worktree) and stop. Do not start the next step until the user says so. Do not run several steps in one request: the user wants short turns.
- Run `ctest` and every other CMake command only in the MSYS2 CLANG64 shell (the script below). In Git Bash, the test `runtime_architecture`
  configures `build/default` again with the wrong compiler, and the CMake cache is lost (no `-mcx16`, other LLVM tools, 1481 steps to
  build again). Repair: delete `build/default/CMakeCache.txt` and `build/default/CMakeFiles/4.*`, then run the configure command of
  `docs/SCARLETNX_BUILD-NOTES.md` section 3 with `-DCMAKE_C_COMPILER=C:/msys64/clang64/bin/clang.exe` and `-DCMAKE_CXX_COMPILER=C:/msys64/clang64/bin/clang++.exe`
  added. FFmpeg and OpenSSL are not built again. The configure takes 2 minutes and the build about 10.
- Build: in the MSYS2 CLANG64 shell (`docs/SCARLETNX_BUILD-NOTES.md`). Write a script to `C:\msys64\tmp\x.sh` and run it from Git Bash
  with `MSYSTEM=CLANG64 CHERE_INVOKING=1 /c/msys64/usr/bin/bash.exe -l /tmp/x.sh`. The script does `cd
  /d/.Developments/nxaevora/project-main/third_party/ScarletNX`, `cmake --build --preset default`, and `ctest --test-dir build/default -j4`
  with `export APPDATA=$(cygpath -w /tmp/snx-appdata)` set first (or two tests fail). A full ctest run takes about 1 minute.
- After a build: check that the game does not run (`tasklist`), then copy `build/default/scarletnx.exe` over the three copies
  (`game/scarletnx/bin/scarletnx.exe` and the two programs in `game/recompiled/01001F5010DFA000/`).
- Edit files with a script that you write with the Write tool and run by its Windows path. Do not put a Python script in a Bash
  heredoc (it loses backslashes). Many source files are CRLF: make a replace work for both line ends and check with `file`.
- A new window or setting row: `PAGES` and `PRESETS` in `imgui_settings.c`, the drawing in `imgui_ui.cpp`, `settings.txt` for the
  settings. Make one commit for one window. Add a test for each pure part (`tests/imgui_settings.c` shows how).
- More tools and traps: `OLD-WORK_PLAN.md`, section "Tools and traps moved from the work plan".

## Reminders

- Do not start the game. Tell the user which scene to run and read the files after.
- Before a recompile: `Get-Process scarletnx`. Use the skill `nx-recompile`. Make the hints with `tools/jump_table_scan.py` first.
- Use the skills `nx-log-read` and `nx-jit-triage`. Check the layout of the program before any analysis.
- Documents for users (`README.md`, `ROADMAP.md`, commit messages): plain prose, no em dash, no semicolon between sentences, no spaced
  hyphen as a dash.
- Say only what you verified. A build or a test is not a run of the game.
