<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Phase 6 plan: NVN on the host, drawn with Vulkan

This is the plan for phase 6 of `ROADMAP.md`. It has five stages. A stage ends when its exit condition is true. The next
stage does not start before that. The plan is a proposal until the user accepts it.

## Where we start

- The game calls NVN functions through a dispatch table. The NVN library is inside the game. It writes methods into
  push buffers, and the runtime reads them from the GPFIFO (`src/nx/gpu/maxwell/`, about the console GPU engines and
  the macros in `mme*.c`). The Vulkan side is in `src/nx/gpu/vk/` (`render.c`, `pipeline.c`, `record.c`, `surface.c`).
- `nvn_trace=1` (`src/nx/gpu/nvn_trace.c`) already lists the NVN functions that the game resolves and counts the calls.
  It wraps the dispatch table entries. It does not see direct branches, so its counts are lower bounds. Calls that the
  game makes without the table are not seen.
- The translation of shader binaries (`src/nx/gpu/shader/`) and the pipeline cache are independent of the command
  stream. They stay.

## Stage 1: Survey the paths of Arceus

Content: the NVN API that Arceus uses, the inline work (data written into memory by the CPU and read by the GPU, for
example uniform and texture descriptors), and the extra commands that the queue adds by itself (fences, flushes,
present, the commands of the library that the game never calls by name).

- Run `nvn_trace=1` in the scenes of the roadmap and write the list of functions with the call counts in `docs/`.
- Find the paths that the hook does not catch. Compare the GPFIFO submissions and the draws that the runtime sees with
  the calls that the hook counts. A gap shows a path that does not pass through the table. Recompile with
  `--hook-direct-calls` if a gap needs it.
- For each object type (device, queue, command buffer, memory pool, texture, sampler, program, sync, window), write which
  state lives in guest memory and which state the game reads back.

Substeps:

- 1-1 List of functions: run `nvn_trace=1` in the boot, title, village, wild area, battle and menu scenes. Write the functions
  and the call counts in a table. The user runs the game, the agent reads the logs.
- 1-2 What the hook misses: compare the GPFIFO submissions and the draws with the hook counts. Decide if a recompile with
  `--hook-direct-calls` is needed (it takes about 11 minutes).
- 1-3 State of the objects: for each object type, where its state lives in guest memory and what the game reads back.
  This is read from the code and the disassembly. The game does not run.
- 1-4 Inline work and extra queue commands: the descriptors that the CPU writes for the GPU, and the commands that the
  library adds by itself (fences, flushes, present). Also read from the code.
- 1-5 Summary: `docs/nvn-survey.md`, with the parts that are hard to replace, as input for stage 2.

Exit condition: all drawing paths that the hook does not catch are known.

## Stage 2: Choose the way of replacement

Content: compare two ways, and decide.

- A: replace NVN completely. The host implements every NVN function the game calls and the library of the game is not
  used.
- B: replace the existing Maxwell layer in part. The library of the game stays. The runtime turns the commands of some
  parts (for example draws and clears) into Vulkan calls at a higher level than the register writes.

For each way, write what is replaced, what stays, the size of the work, the risk of visual faults and the patch size
against upstream.

Exit condition: the range that is replaced and the work that remains are explained.

## Stage 3: Smallest drawing path

Content: connect initialization, a screen clear, one simple draw and the presentation on the new path.

- Pick the first scene from the survey (the title screen or the first clear and present of the boot).
- Keep the old path in the runtime, switched by a setting, so both can run on the same scene.
- Compare the result with the old path by frame dumps of the screen (local only, never in git).

Exit condition: the new path and the old path give the same drawing result for this scene.

## Stage 4: Extend to the real game

Content: textures, compute, synchronization (fences and events) and memory sharing between the CPU and the GPU
(memory pools, write-back, pending pages).

- Add the features in the order that the survey gives, one scene each. After each one, run the scenes of
  `ROADMAP.md` phase 3 and read the logs (`nx-log-read`).
- Shaders and pipelines keep the existing path until phase 9 moves them.

Exit condition: in the target scenes, the drawing and the behavior match the old path in a comparison.

## Stage 5: Measure performance and quality

Content: CPU time, frame time and dropped draws, compared with the old path.

- Use the debug window and the profile output that exist. Measure the CPU time of the command decode that the new path
  removes.
- Check for missing draws, wrong effects and new crashes. Run each fixed crash many times (one good run is no proof).

Exit condition: the effect and the limits of the replacement are confirmed. Then decide if the old path is removed.

## Rules for all stages

- The user runs and tests the game. After a run, the agent reads the files and reports.
- A fix explains the mechanism. No tuning of delays, no retries, no special cases.
- Nothing of the game goes into git (dumps, frame dumps, shader dumps, logs).
- The 30 frames each second and 0 JIT misses are the baseline. A stage must not make them worse.
- A change in `third_party/ScarletNX` goes to `docs/runtime-changes.md`.
