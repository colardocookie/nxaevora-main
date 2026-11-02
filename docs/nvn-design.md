<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# NVN design: the choice of the way (phase 6, stage 2)

This file compares the two ways of `docs/nvn-plan.md` and gives a recommendation. The input is `docs/nvn-survey.md`
(section 1-5). The choice is the user's. Nothing here is built yet.

## The two ways

- **Way A, replace NVN completely.** The host implements the NVN functions that the game calls. The game's NVN library
  (the `sdk` module, d82xxxx to d85xxxx) is not run for those functions. No pushbuffer, no GPFIFO entry and no Maxwell
  method exist for the replaced work. The host records Vulkan work directly.
- **Way B, keep the library, replace the Maxwell layer in part.** The library still builds the pushbuffers and the
  GPFIFO entries. The runtime decodes them as now, but turns chosen command groups (for example draws and clears) into
  Vulkan calls at a higher level than register writes.

## What is replaced and what stays

| | Way A | Way B |
| --- | --- | --- |
| Entry points | About 242 called functions are host code. The 281 not called functions are stubs that stop with a message. | None. The table stays as it is. |
| Library code in `sdk` | Not run for replaced functions. | Runs in full. |
| Command stream (pushbuffer, GPFIFO, macros) | Gone for the replaced work. | Stays. It is decoded, and a part of it is grouped. |
| Recorded command buffers | New host object that can be called again (4.34 million calls against 1.33 million recordings). | The library does it. |
| Frame prologue and epilogue (engine reset, report semaphore, window image as color target 0) | The host does the equivalent by itself. | The library writes them. |
| Object memory in the guest | The host fills it. The game may read fields (open). | The library fills it. |
| GPU addresses (`BufferGetAddress`, texture handle) | The host chooses and must match what the game and the shaders expect (open). | Unchanged. |
| Shaders, pipeline cache, `gmem_*` write tracking | Stay. | Stay. |

## Size of the work

- **A.** The functions fall in few groups: state objects (pure data, a Set and a Get for each field, about 100 functions),
  texture, sampler and pool objects, buffer and memory pool, the queue and window (the frame cycle), and the command buffer
  (57 called functions, the hardest part because of replay). The game calls NVN from few places (a layer around
  a9b82e0 to ac8714c), so the surface is known. The survey gives a list, so the size is bounded. It is the larger work.
- **B.** The grouping needs a model of the engine state that the library leaves in registers, and a rule for each group
  when the state is complete. The work is smaller at first, but it grows with each kind of command, and the decode of
  macros and library-made entries stays.

## Risk of visual faults

- **A.** The risk is in what the library produces that the game or the shaders read back: raw field reads of objects,
  the GPU address values, the texture handle format, the exact object sizes. A mistake shows at once and in a clear way
  (wrong value in the first scene), as stage 3 expects. A missing library side effect (a state reset, a fence) shows as a
  fault that depends on the scene, which is harder to find.
- **B.** The risk is lower for the objects, because the library keeps them. It is higher for the grouping: a group closed
  at the wrong time gives a missing or wrong draw, and these faults depend on the order of the stream.

## Patch size against upstream

Both ways are code in `third_party/ScarletNX`, which is vendored, so upstream distance is not a hard limit.

- **A.** Mostly new files (a host NVN layer next to `nvn_trace.c`, which already wraps the table), plus a switch in the
  place that loads the table. Small changes to old files. The old path stays and a setting selects it.
- **B.** Changes inside `gpu/maxwell/` and the renderer interface, which are old files that upstream also changes.

## What each way gives

- **A** removes the command stream for the work it covers. This is the goal of the native plan (no GPU command stream).
- **B** does not remove it. It changes what the decoder emits. The gain in CPU time is not measured: the GPU uses about
  6 ms of 33 ms, and the CPU time of the decode is first measured in stage 5. B has no clear goal that A does not reach
  better.

## Recommendation

Way A, built in this order so that the old path stays:

1. The host layer replaces entries of the dispatch table (the same place where `nvn_trace.c` wraps them). A setting
   (for example `nvn_native`) selects the old path or the new one, as stage 3 requires.
2. Start with the objects that have no stream effect: device, state objects, memory pool, buffer, texture, sampler and
   pools. Object memory is the open point, so stage 3 begins with the raw-read question. The first scene shows a
   wrong field at once.
3. Then the queue, the window and the command buffer with replay, with the frame prologue and epilogue done by the host.
4. The 281 functions that the played scenes did not reach become stubs that stop with a clear message.

Mixing the two paths inside one frame (some objects native, some from the library) is not possible for command buffers,
because a library command buffer holds a Maxwell stream and a host command buffer holds Vulkan work. The switch is
therefore per run, not per function.

Why not B: it keeps the library, the macros and the GPFIFO decode, so it ends the phase with the same command stream. It
is the smaller step only at the start.

## Decisions for the user

1. Way A or way B. The recommendation is A.
2. If A: the switch is per run (`nvn_native` setting, old path kept until stage 5 decides).
3. The place of the code (`runtime/gfx` or inside ScarletNX) is decided after this choice. The first files go into
   `third_party/ScarletNX/src/nx/gpu/` next to `nvn_trace.c`.

## Open points that this choice does not close

- Raw reads of object fields by the game (state objects: not found; buffer, texture and command buffer: open).
- The values of `nvnBufferGetAddress` and `nvnMemoryPoolGetBufferAddress`, and the 32-bit texture handle format.
- The exact object sizes.
- Words 20 to 55 of the last library entry, and the contents of the other library entries.

Stage 3 answers the first three by running one scene. The fourth is a read of the log with more words logged.
