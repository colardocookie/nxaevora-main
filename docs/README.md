<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the NXAevora contributors -->

# Documents

The notes of NXAevora, by subject.

## Guides for contributors

- [ENGINE_BUILD-NOTES.md](build/ENGINE_BUILD-NOTES.md): how ScarletNX is built on Windows in MSYS2 CLANG64.
- [runtime-changes.md](build/runtime-changes.md): the list of the changes in the vendored ScarletNX. Kept up to date.
- [CREDITS.md](CREDITS.md): the projects and works that NXAevora uses, and their licenses.

## Specifications

These documents tell how the engine works now. They come from the code. The state of a part changes over time, so each document keeps it in a "Current state" section.

### engine

- [overview.md](dev/engine/overview.md): what NXAevora Engine is, the program flow, the commands, the settings, the folder map and the build.
- [aot-recompiler.md](dev/engine/aot-recompiler.md): the input, the stages, the output, the dispatch table, the JIT fallback and the hints.
- [nvn-vulkan.md](dev/engine/nvn-vulkan.md): the two NVN paths, the host objects, the command lists, the frame job, the replay and the shaders.
- [arceus-timing.md](dev/engine/arceus-timing.md): the elapsed-time clocks of the game and the hook groups that follow the frame step.
- [arceus-temporal.md](dev/engine/arceus-temporal.md): the temporal passes (`probe` and `taa`), the object vectors, the scene rectangle and the FSR, XeSS and DLSS adapters.
- [arceus-pass-census.md](dev/engine/arceus-pass-census.md): the read-only frame pass census and its log lines.

### ui

- [engine-menu.md](dev/ui/engine-menu.md): the design of the engine menu (the in-game UI base, the frame, the theme, the components, the motion and the migration).

The launcher specification comes later.

## Records

These files are the notes of investigations, analysis results, plans and reviews made during the development, mostly by
AI agents. They are kept as history. They are not a specification and can be out of date.

### analysis

- [jit-analysis.md](records/analysis/jit-analysis.md): why the game reached code that the AOT module lacked.
- [stutter-analysis.md](records/analysis/stutter-analysis.md): the short stops of the game. Stopped by the user.
- [crash-analysis.md](records/analysis/crash-analysis.md): the crash of 2026-10-31. The cause is not found.

### archive

- [imgui-review.md](records/archive/imgui-review.md): the review of the ImGui patches 0013 to 0019.
- [controller-keyboard-review.md](records/archive/controller-keyboard-review.md): the review of patch 0016.

### fs

- [phase7-plan.md](records/fs/phase7-plan.md): the plan of phase 7 (decrypted game files and a file layer for mods), with
  the inventory, the input layout and the PR units. In progress.

### game

- [arceus-timing.md](records/game/arceus-timing.md): the change of the game's fixed 1/30 second clocks to the elapsed
  time of the frame above 30 FPS, with Discord presence, the play set (`SNX_ARCEUS_PERF_*`) and the scene scale.

### graphics

- [nvn-plan.md](records/graphics/nvn-plan.md): the five stages of phase 6, NVN on the host drawn with Vulkan.
- [nvn-design.md](records/graphics/nvn-design.md): the design of the host NVN layer and its decisions. Active.
- [nvn-unobserved.md](records/graphics/nvn-unobserved.md): the plan for the NVN functions and values that no scene called
  (coverage, selftest, execution test, steps 4-u1 to 4-u4). Active.
- [nvn-coverage.md](records/graphics/nvn-coverage.md): one row for each NVN function the game resolves. Made by
  `tools/nvn_coverage.py`. Active.
- [nvn-calls.md](records/graphics/nvn-calls.md): the NVN functions that one run called, with the call counts.
- [nvn-survey.md](records/graphics/nvn-survey.md): the survey of stage 1, what ScarletNX had for NVN.
- [shader-analysis.md](records/graphics/shader-analysis.md): the shader programs that ScarletNX did not translate.
- [present-alpha.md](records/graphics/present-alpha.md): the HUD ghosting with Mailbox and Immediate. Fixed.

### input

- [input-analysis.md](records/input/input-analysis.md): how the keyboard and mouse reach the game, and the native input plan.
- [camera-analysis.md](records/input/camera-analysis.md): the path of the right stick to the camera. Paused.

### ui

- [engine-ui-plan.md](records/ui/engine-ui-plan.md): phase 7.5, the move to the NXAevora Engine, the CLI and the new in-game
  UI that replaces Dear ImGui and the old UIs. Active.
- [imgui-plan.md](records/ui/imgui-plan.md): the old plan of the in-game Dear ImGui windows. Replaced by `engine-ui-plan.md`.
