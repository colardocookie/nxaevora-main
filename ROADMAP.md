# NXAevora roadmap

NXAevora aims to run Pokemon Legends Arceus natively on the PC. The code of the game is recompiled ahead of time with
ScarletNX, and the result runs with no emulation of the CPU. This file shows where the project is and what comes next.
It has no dates. A phase ends when its exit condition is true.

The word "miss" in this file means a pc that the recompiled code does not have. When the game reaches a miss, the
ScarletNX runtime compiles the code at that moment (the JIT). A fully static build has no misses.

## Phase 0: Foundation (done)

- The repository, the vendored ScarletNX and the folder layout are in place.
- ScarletNX builds on Windows with MSYS2 CLANG64. The steps are in `docs/build/SCARLETNX_BUILD-NOTES.md`.
- A portable copy of the recompiler runs without MSYS2.
- The base game and update 1.1.1 recompile in a few minutes.
- The recompiled game boots to the language selection screen and runs without a crash for 45 seconds.

## Phase 1: Understand the misses (closed for now)

The update program had 17 different misses in a run of 84 seconds, and the base program had 19 in the boot. Most
are inside functions. A few are PLT stubs.

- Done: read how ScarletNX decides where a miss can happen (`docs/analysis/jit-analysis.md`).
- Done: verify one miss as a jump-table target. The recompiler does not read the entries of a jump table.
- Done: scan the jump tables of all modules. 49 of the 51 pcs that are not PLT stubs are targets of a table.
- Open: 2 pcs are not explained (0x8cbe780 in main and 0xd92f310 in sdk), and the cause of the PLT-stub misses is not
  known. None of them missed again in the runs with the hints.

This phase is closed for now, because the runs so far found no new miss. Open it again when a miss appears that the
scan does not explain.

Exit condition: every miss of the update program in a boot run has a known cause.

## Phase 2: Remove the misses of the boot (closed for now)

Result: with the targets of all jump tables as hints (45728 pcs from `tools/jump_table_scan.py`), the opening scene
at sea ran for 252 seconds with 0 misses. Before, the same scene had 114 misses.

- Feed the recorded pcs to the next recompile and check that the boot run has no miss. `scarletnx -r` reads
  `<outdir>/<ID>/<name>.hints` by itself. The runtime names the file after the program that started. The base
  program runs the base game code and the update program runs the update code. The modules have other addresses in
  each, so a hints file is valid only for the program that wrote it. For the update, the user starts the update
  program.
- Done: the scan of the jump tables is part of the recompile procedure (the skill `nx-recompile`). It makes the
  hints from the game files, so the list is not in git. Other game-specific settings and patches go to
  `recomp/config`.
- Open: the cause is a rule of the recompiler (it does not read the entries of a jump table). The hints are a
  workaround that needs no change in ScarletNX. Decide whether to fix `explore()` in ScarletNX and report it to the
  upstream project.

Exit condition: the boot run to the language selection screen ends with 0 misses.

## Phase 3: Cover the game (closed for now)

The user plays a scene. Claude reads the log and `<id>.hints` of the run. Each round adds the new pcs to the hints
and recompiles again.

This phase is closed for now, because every run after the jump-table hints had 0 misses. Open it again when a miss
appears in a scene that is not tested yet.

- Scenes to cover: the title screen, the start of the story, the village, the wild areas, battles, menus, and the save
  and load flow.
- Covered so far, with 0 misses: the opening scene at sea, the first menus with the software keyboard, the write of
  a save, the load of that save, a play of 7 minutes to the entry of the village with a capture, and a play of
  13 minutes to the point before the Obsidian Fieldlands with a battle against an NPC, and a play of 4 minutes in
  the Obsidian Fieldlands. Not tested yet: long play in the open areas, the other wild areas, and battles against wild
  Pokemon of the later areas.
- The number of misses for each scene is the measure. It is written down after each round.
- Findings about the game (crashes, wrong rendering, missing services) go to `docs/`.

Exit condition: a long play session in each scene ends with 0 misses.

## Phase 4: The JIT stays as a safety net

Decision: the released build keeps the JIT. A miss in a scene that was not tested is then compiled and run, and the
game goes on. A build without the JIT would end the game at the first such miss. Because the JIT stays, LLVM stays
in the runtime and `libLLVM-23.dll` is shipped.

A build with the JIT turned off is still useful as a check, and it is optional.

What the source of ScarletNX shows (`guest_jit.c`, `rtapi.c`, `jit_service.c`):

- No setting turns the JIT off. `jit_init()` always starts it. `jit_threads` can only be 0 (automatic) to 4.
- A miss is already a hard error when the JIT is not running. `rt_miss` then ends the game with the message "The game
  reached code at <pc> that the recompiler missed". The runtime has this path for a JIT that cannot start.
- The smallest change is a setting or a switch that skips `snx_jit_service_new` in `jit_init()`. It is a few lines.
  It would be a normal commit in `third_party/ScarletNX`.
- The game program is the same program as `scarletnx.exe`, with the recompiler and the JIT in it, and it imports
  `libLLVM-23.dll` (154 MB).

The line `jit: ... N misses` of the log shows exactly how often the JIT was needed, so 0 misses in a run means that
the run did not need it.

Optional step: make the patch that turns the JIT off, and let the user play the covered scenes with it. A miss ends
the game and names the pc.

Exit condition: the decision above is recorded. The optional check can be done at any time.

## Phase 5: Quality of the port

- Rendering: look for wrong or missing effects in the Maxwell to Vulkan path.
- Speed: the game limits the rate, not the runtime. In every run the game sets a swap interval of 2 (`vi: swap interval
  2`, one change at about 2 s). The setting `fps_limit=game` makes the runtime hold each frame for 2 refreshes of 60 Hz,
  which is 30 frames each second. The GPU needs about 6 ms of the 33 ms, so the hardware is not the limit. A higher
  rate is a game enhancement (later work) and needs the game's own timing to change with it.
- Shaders: by hash, 11 of the 1379 programs that the game ships fail (see `docs/graphics/shader-analysis.md`). The old figure of
  80 counted the same program once for each archive. Six wait for their first draw, because their translation reads a
  constant buffer. Four hit the data of a jump table that follows the program (checked in the files). The last one has the instruction `RED`, and the
  count of skipped draws grew by one in each frame. Patch `0002` adds the form, and a run of 157 s then showed 0
  skipped draws. The one texture of
  format 0 is an empty header that the game binds to the slot of a compute program. The slot reads zeros through a
  null view and no draw is dropped. The user sees no visible fault from these in the tested scenes. A run of 681 s with the bag, the box, the clothes,
  the battle introduction and a large part of the open areas had no shader that failed at its first draw, no skipped
  draw and 0 JIT misses.
- Stutter: the only visible problem so far is a stutter when a pipeline is made for the first time. A run of 420 s
  made 100 pipelines (3.8 s making, 2.5 s of waiting). The runtime keeps its pipelines in `shader_cache/` and makes
  them again in the background at the next start. A run of 794 s made 67 new pipelines (2.1 s of waiting). The
  analysis is in `docs/analysis/stutter-analysis.md`: ScarletNX already prepares the shaders ahead of time, and the pipelines
  for new draw states cannot be made before the game uses them. Most slow frames are not from pipelines. The setting
  `async_pipelines=1` removed the wait for pipelines (0 ms in 375 s, with an empty cache), and the user reports clearly
  less stutter. The stop of about 1.2 s to 1.4 s at the start is the game's own loading loop: its main thread starts 39
  pooled objects one after the other and waits for each with a sleep of 16 ms, with no other thread busy and no slow file
  read (see `docs/analysis/stutter-analysis.md`, the hitch runs). The runtime's sleeps are exact, so no fix is planned. The cause of
  the other slow frames is not known.
- Audio and save data.
- Services of the console that Arceus needs and ScarletNX does not have yet (`runtime/hle`).

Exit condition: the covered scenes play at the speed and the quality that the user accepts.

## Native plan

The CPU code of the game is native after the recompile. The other layers of the ScarletNX runtime still re-create the
console. The user decided to replace them in the order below. The earlier rule "replace a layer only after a measured
problem" no longer applies to these steps. Each step still needs a test in the game, and the 30 frames each second
and the 0 misses are the baseline that no step may make worse. Every step moves away from the upstream project, so a
patch must be kept up to date or be proposed upstream.

Phase 7 is not a native step. It changes how NXAevora gets the game files, and it comes right after phase 6 because
the decryption code is a legal risk.

Not in the plan: audio, memory and video. They stay as they are. Open them again only with a measured problem.

## Phase 6: Graphics, NVN to Vulkan

Now: the game calls its own NVN library, which writes a GPU command stream. ScarletNX decodes that stream (the methods
and macros of the console GPU) and draws with Vulkan. The chain is: game, NVN library inside the game, GPU command
stream, interpretation by ScarletNX, Vulkan.

Goal: game, NVN implementation on the host, Vulkan. The NVN calls of the game go to code on the PC that calls Vulkan
directly, and the command stream is gone.

The plan is in `docs/graphics/nvn-plan.md`. It has five stages:
1. Survey the paths of Arceus (the NVN API, inline work, extra queue commands). Exit: the paths that the hook does not catch are known.
2. Choose the way: replace NVN completely, or replace the existing Maxwell layer in part. Exit: the range and the remaining work are explained.
3. Smallest drawing path: initialization, clear, one simple draw and present. Exit: the result matches the old path.
4. Extend to the real game: textures, compute, synchronization and memory sharing. Exit: the target scenes match the old path.
5. Measure CPU time, frame time and dropped draws. Exit: the effect and the limits are confirmed.

The translation of the shader binaries stays. Drawbacks to plan for: the API is large, the game uses it in its own ways,
and a new path can add visual faults.

Exit condition: the covered scenes draw the same as before with no command stream decode, at 30 frames each second.

## Phase 7: Decrypted game files and a file layer for mods

Now: ScarletNX reads NSP and XCI files and decrypts them with the keys of the console, at the recompile and at each
start of the game. The code is a copy of hactool in `third_party/ScarletNX/src/nx/runtime/driver/hactool/`. Code that
decrypts the game can conflict with laws against the circumvention of copy protection, such as the DMCA.

Goal: NXAevora reads only game files that are already decrypted. The user makes them from their own console: the
ExeFS of the update and one RomFS of the game with the update applied. ScarletNX has no decryption code and needs no keys.

- The recompile and the runtime read normal files. The LZ4 compression of the NSO files is not encryption and stays.
- Remove the hactool code, the reading of the keys, the NSP, XCI and NCA readers, and the settings, tests and parts of
  the GUI that use them. List the removal in `docs/build/runtime-changes.md`.
- Add a file layer for mods that replaces files of the game without a change to the decrypted files. Define the order
  in which a mod overrides a game file.
- The decrypted files are for the own use of the user only and must never be shared. They need 6 GB or more of disk
  space, and they must be made again after an update.

Exit condition: the game recompiles and runs from the decrypted files with no keys, ScarletNX has no decryption code,
and a test mod replaces one file without a change to the decrypted files.

## Phase 8: Shaders and pipelines

Use the existing preparation (shaders made ahead of time, `shader_cache/`, `async_pipelines`, see
`docs/analysis/stutter-analysis.md`) and build it into the new graphics layer of phase 6. Pipelines for new draw states cannot be
made before the game uses them, so the aim is to prepare as many as possible before play and to hide the rest.

Exit condition: the stutter at the first use of a pipeline is gone in the covered scenes, and the shaders that fail
now (11 of 1379, see `docs/graphics/shader-analysis.md`) are handled.

## Phase 9: Input and camera

Facts are in `docs/input/input-analysis.md` and `docs/input/camera-analysis.md`.

- Finish the direct mouse look (the mod `native-mouse-look`). Test it in cutscenes, dialogs, menus, battles and the
  other camera modes. If a scene whose camera the player does not control moves, gate the stub on a game state.
- Arrange the state of the controls and the remap: the mouse buttons (now fixed), the wheel, rebinding and profiles
  for the keyboard, the mouse and the pads.
- The launcher writes the setting `mouse_native_address` and keeps the mod in the mods folder.

Exit condition: the whole game is playable with the keyboard and the mouse, and the user can rebind every control.

## Phase 10: Host replacement of SDK functions

Replace SDK functions of the game with code on the host. Use the preparation that exists (the recompile patches in
`recomp/patches/`, the analysis of the SDK code from the disassembly, and the HLE services in `runtime/hle`). Make the
new graphics layer of phase 6 part of it, so that the SDK calls that touch graphics go to the host too.

Exit condition: the replaced functions are listed in `docs/`, each has a test run, and the game runs with 0 misses.

## Phase 11: Launcher

A launcher made with Tauri (`launcher/`).

- The user chooses the folders of the decrypted ExeFS and RomFS (phase 7). The launcher never copies or uploads them.
- It checks the game files and the mod folder.
- It starts the recompile, shows its progress and starts the game.
- It lists the settings that matter, for example the language, the resolution and the input. It manages the mods of
  phase 7.
- It ships with the runtime DLLs, so that no other tool must be installed.

Exit condition: a user with the decrypted game files goes from a fresh install to a running game without a terminal.

## Phase 12: Release

- Choose and write down the license (GPL-3.0-or-later is the plan, because ScarletNX is GPL-3.0).
- Add the credits for the projects whose code or research NXAevora uses.
- Build the release in CI from a pinned ScarletNX commit.
- Write the README for users, with the legal notes: the game files are not part of NXAevora, the user must dump
  them from their own console, and NXAevora does not decrypt them.

Game enhancements such as a higher frame rate are later work and are not part of this list. The full playthrough comes
after the phases above.

## Open questions

- Which Arceus versions are supported? The work so far uses 1.1.1.
- Measure: the stop of about 1.3 s at the start (a wait, not work). Find the lock or the read that it waits on.
