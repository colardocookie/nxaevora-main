<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the NXAevora contributors -->

# Documents

The notes of NXAevora, by subject. Each line names the file and its state. Until November 2026 all files were in
`docs/` itself, so older commit messages give paths without the subfolder.

[CREDITS.md](CREDITS.md): the projects and works that NXAevora uses, and their licenses.

## build

- [SCARLETNX_BUILD-NOTES.md](build/SCARLETNX_BUILD-NOTES.md): how ScarletNX is built on Windows in MSYS2 CLANG64.
- [runtime-changes.md](build/runtime-changes.md): the list of the changes in the vendored ScarletNX. Kept up to date.

## graphics

- [nvn-plan.md](graphics/nvn-plan.md): the five stages of phase 6, NVN on the host drawn with Vulkan.
- [nvn-design.md](graphics/nvn-design.md): the design of the host NVN layer and its decisions. Active.
- [nvn-unobserved.md](graphics/nvn-unobserved.md): the plan for the NVN functions and values that no scene called
  (coverage, selftest, execution test, steps 4-u1 to 4-u4). Active.
- [nvn-coverage.md](graphics/nvn-coverage.md): one row for each NVN function the game resolves. Made by
  `tools/nvn_coverage.py`. Active.
- [nvn-calls.md](graphics/nvn-calls.md): the NVN functions that one run called, with the call counts.
- [nvn-survey.md](graphics/nvn-survey.md): the survey of stage 1, what ScarletNX had for NVN.
- [shader-analysis.md](graphics/shader-analysis.md): the shader programs that ScarletNX did not translate.
- [present-alpha.md](graphics/present-alpha.md): the HUD ghosting with Mailbox and Immediate. Fixed.

## input

- [input-analysis.md](input/input-analysis.md): how the keyboard and mouse reach the game, and the native input plan.
- [camera-analysis.md](input/camera-analysis.md): the path of the right stick to the camera. Paused.

## ui

- [engine-ui-plan.md](ui/engine-ui-plan.md): phase 7.5, the move to the NXAevora Engine, the CLI and the new in-game
  UI that replaces Dear ImGui and the old UIs. Active.
- [imgui-plan.md](ui/imgui-plan.md): the old plan of the in-game Dear ImGui windows. Replaced by `engine-ui-plan.md`.

## analysis

- [jit-analysis.md](analysis/jit-analysis.md): why the game reached code that the AOT module lacked.
- [stutter-analysis.md](analysis/stutter-analysis.md): the short stops of the game. Stopped by the user.
- [crash-analysis.md](analysis/crash-analysis.md): the crash of 2026-10-31. The cause is not found.

## archive

Finished reviews. Their findings are fixed, or listed as open tasks in the work plan.

- [imgui-review.md](archive/imgui-review.md): the review of the ImGui patches 0013 to 0019.
- [controller-keyboard-review.md](archive/controller-keyboard-review.md): the review of patch 0016.
