<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# NVN survey (phase 6, stage 1)

Status: started on 2026-11-02. This file grows with the substeps of `docs/nvn-plan.md`. A line marked "verified" was read
in the source of `third_party/ScarletNX`. A line marked "open" is not checked yet.

## What the runtime does now (verified from the headers)

- The game's NVN library runs as recompiled guest code. It writes pushbuffers, macros and descriptors into guest
  memory. It submits GPFIFO entries through the nvhost-gpu ioctls (`hle/services/nvdrv.c`). The ioctl only queues them
  with `gpu_submit` (`gpu/gpu.h`).
- The GPU thread decodes the entries (`gpu/gpu.c`, `pushbuffer.h`). Each channel keeps the registers of the engines
  (3D, compute, 2D, copy, inline-to-memory), the macro state and the constant buffer bindings (`GpuChannel`).
- The engines (`gpu/maxwell/`) turn methods into calls of the renderer interface `Renderer` (`gpu/renderer.h`). The only
  implementation is `vk_renderer` (`gpu/vk/`).
- The renderer interface is the seam where a new path can start. It has these groups of calls: clear and draw, barriers,
  DRAW_TEXTURE, 2D blit, copy engine, compute dispatch, semaphores and syncpoint releases, samples-passed queries, render
  predicates, VIC and NVDEC, present, save state.
- `nvn_trace.c` replaces the dispatch table entries of the NVN functions. The original function still runs, so the hook
  only counts. It sees calls through function pointers. It does not see direct branches unless the game was recompiled
  with `--hook-direct-calls`.

## What follows for the replacement (open, to check in 1-3 and 1-4)

- NVN structures live in guest memory and the game code may read them (open: which fields).
- The game gives GPU virtual addresses to the library. The runtime keeps the GPU address space in `nvdrv.c` (`as_*`
  ioctls and nvmap). A host NVN must keep the same addresses or give new ones that the game accepts (open).
- The synchronization is by syncpoints and semaphores in guest memory. The game polls memory that the GPU writes
  (`gpu_release_write`). A host NVN must write the same results (open).

## 1-1 List of functions

Run of 2026-11-02 (`nvn_trace=1`, update program, 302.75 s, 8900 frames, 30 frames a second, 0 JIT misses, no crash).
The full list is in `docs/nvn-calls.md`. 522 addresses were resolved and 242 functions were called.

Calls by group (functions called): command buffer 57, texture and texture view or builder 80 in all, multisample state 19,
sampler 18 or more, queue 13, depth-stencil 13, memory pool 10, buffer 10 or more, vertex state 10, polygon state 9,
blend and color state 14, window 9.

Facts that matter for the replacement:

- The frame cycle is the same in all 8919 frames: `nvnWindowAcquireTexture`, `nvnQueueSubmitCommands`,
  `nvnQueueFenceSync`, `nvnQueuePresentTexture`. A submit does 0 GPFIFO submissions. The present does 1. So the queue
  gathers the work and sends it with the present. `nvnQueueFlush` is not in the list.
- Command buffers are recorded and replayed. `nvnCommandBufferBeginRecording` ran 1.33 million times, but
  `nvnCommandBufferCallCommands` ran 4.34 million times. The hook counts the draws at the time of recording
  (1.46 million of the draw functions), and the renderer counted 4.32 million draws at 285 s. The replay explains the
  difference: one recorded draw runs about three times. A host NVN must keep a recorded command buffer as an object that
  can be called again and again.
- Compute matches: 41793 `nvnCommandBufferDispatchCompute` calls against 41259 dispatches in the renderer at 285 s.
- The game uses `nvnCommandBufferAddCommandMemory` and `AddControlMemory` 1.77 and 1.47 million times: the command memory is
  given to the library in pieces. A host NVN does not need this memory, but the game still passes it.
- Queries and sync: `nvnCommandBufferReportCounter` 37762, `ResetCounter` 9962, `nvnSyncWait` 17838, `nvnSyncInitialize` 2.
- Copy: only `nvnCommandBufferCopyTextureToTexture` (150). No buffer copies were called.
- The state objects (`Bind...State` and the `...State` getters and setters) are called very often (millions). They are
  small structures in guest memory that the game reads back (`VertexAttribStateGetFormat` 4.2 million calls).
- Not called in this run: `nvnQueueFlush` and every function that the scenes of the run did not reach.

Scenes of the run (the user's report): all the scenes that were asked for (boot, title, village, wild area, battle and menus). The setting values were resolution 1 and anti-aliasing 0 in the file after the run, and
`async_pipelines` was 1 (1 draw skipped at 17.8 s, 0 later in the counts that were read).

## 1-2 What the hook misses

Method: use the log of the run of 1-1 and compare the hook with the other counts that the log has. The game was not run again.

Result (2026-11-02): no path that the hook misses was found, and a recompile with `--hook-direct-calls` is not needed for
the work of phase 6.

- Every function that was called is also in the list of resolved functions: 523 names were resolved through
  `nvnBootstrapLoader` and `nvnDeviceGetProcAddress`, 242 were called, and 281 were not called in this run. The game
  therefore reaches the NVN API through the pointers that it got from these two functions, which is the path that the hook
  wraps.
- The pairs match. `nvnProgramInitialize` and `nvnProgramSetShadersExt` were both called 5179 times. Every kind of object has its
  `Initialize` call (device, queue, window, memory pool, buffer, texture, sampler, program, command buffer, sync). A call that
  passed by a direct branch would show as an object with no `Initialize`.
- Each frame submits one list of 77 command buffers (76 in 311 of 8919 frames), and the list alternates between two arrays in
  guest memory. The queue is one, the same in all frames.
- The queue calls log their GPFIFO submissions: 0 in `nvnQueueSubmitCommands` and `nvnQueueFenceSync`, 1 in
  `nvnQueuePresentTexture`, in all 8919 frames. No other GPFIFO submission was seen during a queue call.
- The count of draws cannot be checked exactly from the log: the hook counts when a command buffer is recorded, and the
  renderer counts when it runs (see 1-1). The two agree only by size (4.34 million `CallCommands`, 4.32 million draws at
  285 s).

What stays open: the calls inside the NVN library itself (one library function that calls another) are direct branches and
are not seen. They are not part of the surface that phase 6 replaces, because the host NVN replaces the entry points that
the game calls. If a function is found that the game calls but the hook never shows, this step opens again.

Resolved and not called in this run (so not shown by the scenes that were played): the transform feedback calls, the
tessellation levels, the conservative raster calls, the Event object (`nvnEvent*`, `nvnCommandBufferSignalEvent`,
`WaitEvent`), `nvnCommandBufferFenceSync` and `WaitSync`, `nvnQueueFlush`, `nvnQueueWaitSync`, the buffer copies
(`CopyBufferToBuffer`, `CopyBufferToTexture`), `BindImages`, and the mapped range flushes. The full list is the difference
between the resolved names in the log and `docs/nvn-calls.md`. The host NVN must still answer them. A call that nothing
used so far can be a stub that stops with a clear message when it is reached.

## Direct callers in the code (check of 2026-11-02)

`tools/find_callers.py` searched the extracted code for direct `bl` and `b` to the NVN functions that the game calls most:
`nvnCommandBufferDrawArrays` (d849670), `nvnQueueSubmitCommands` (d839ff0), `nvnDeviceGetTextureHandle` (d82d0d0) and
`nvnVertexAttribStateGetFormat` (d83af80) have no direct caller in any module. `nvnBufferMap` (d823b80) and
`nvnBufferGetAddress` (d823bc0) have four, all in the `sdk` module at d862368 to d862444, next to the NVN code. So the
game's own code reaches these functions only through the pointers that it got from the loader, and the hook sees every call.
This agrees with step 1-2. The NVN code is in the `sdk` module (the functions are at d82xxxx to d85xxxx).

## 1-3 State of the objects

Method: the API list of 1-1 (`docs/nvn-calls.md`). This part is read from the names and the counts. The disassembly of the
game's own reads of an object is not done.

Result so far:

- Every object type is used only through calls: builders (`...BuilderSet...`), `Initialize` and `Finalize`, `Set...` and
  `Get...`. The game has the memory of each object (it passes a pointer to it), and the library fills it. The calls give
  the fields that a host NVN must keep for each type:
  - Device: `GetInteger` (127349 calls), `GetTimestampInNanoseconds`, `SetDepthMode`, `SetWindowOriginMode`,
    `GetTextureHandle`, `GetImageHandle` (2).
  - Memory pool: storage address and size, flags (`GetFlags`), `Map`, `GetBufferAddress`.
  - Buffer: storage (pool and offset), size (`GetSize` 1.06 million), `Map` (2.29 million), `GetAddress` (316689).
  - Texture and view: target, format, size, levels, layers, swizzle, depth-stencil mode, storage, stride, and the packaged
    layout (`SetPackagedTextureLayout` 5584). The getters are `GetWidth`, `GetHeight`, `GetDepth`, `GetStride`, `GetTarget`.
  - Sampler: filters, wrap, LOD, border color, compare, reduction, anisotropy.
  - Pools: one texture pool and one sampler pool, with `RegisterTexture` 6443 and `RegisterSampler` 82.
  - State objects (blend, color, depth-stencil, polygon, multisample, vertex attribute and stream, channel mask): pure data
    with a `Set` and a `Get` for each field. The getters run very often in the hot path (the vertex getters 4.2 million times).
  - Program: `Initialize` and `SetShadersExt` 5179 each, `Finalize` 384.
  - Queue, window and sync: one queue and one window, 2 syncs.
- A state getter is a pure read of data that the setter stored, so a host NVN can answer it from its own copy.

Open (needs the disassembly of the game, or a test in stage 3):

- Does the game code read a field of an object directly, with no call? The hook cannot see this. If it does, the host NVN
  must keep that field at the same offset.
- The exact sizes of the object memory that the game gives (they come from the NVN header of the SDK, which is not in the
  repo). The lower bounds are in the table "Object memory that Initialize writes" below. The exact size can be found at the
  places where the game reserves the memory of each object.
- Which GPU address `nvnBufferGetAddress` and `nvnMemoryPoolGetBufferAddress` must give: the game writes these into its own
  data, for example into the buffer tables that shaders read.

### Object memory that Initialize writes (check of 2026-11-02)

Method: `tools/disasm.py` on the `Initialize` function of each type in the `sdk` module, reading the stores through `x0`
(the object). The size of the object is at least the last byte that the function writes. A function that writes less than
the object (it zeroes only a part) shows a lower bound only.

| Type | Function | Bytes written, from the start | Notes |
| --- | --- | --- | --- |
| Buffer | d823b20 | 0x30 (zeroed), fields at +0x8 to +0x20 copied from the builder | |
| Sampler | d83a570 | 0x60 (zeroed) | |
| Texture | d83d3c0 | 0xA8 (zeroed) | at least 0xA8 |
| Memory pool | d833ce0 | 0x100 (zeroed) | |
| Window | d83e7d0 | 0x148 (zeroed), fields at +0x30, +0x38, +0x40 | at least 0x148 |
| Program | d8362a0 | at least 0x90, the device pointer at +0 | |
| Command buffer | d823cf0 | at least 0x90, the device pointer at +0, fields at +0x8, +0x10, +0x70 | |
| Texture pool and sampler pool | d83d930, d83a700 | 0x14 (the memory, the size and a word at +0x10) | |
| Sync | d83b800 | 0x20 (zeroed), the device pointer at +0 | |
| Device | d82d110 | writes at +0x19d8 and +0x19dc | at least 0x19e0 |
| Queue | d839360 | a byte at +0x8 and a word at a computed offset | not read yet |

The library stores the pointer to its device in the first word of an object (program, command buffer, sync). This is a
fact that the game code may depend on only when it reads that word itself, which is the open question above.

### The game's own use of the NVN objects (check of 2026-11-02)

Method: the function that loads the NVN pointers is in `main` (around aa93e30 and 877e440, two copies). For each name it
calls `nvnDeviceGetProcAddress` and stores the result in a global slot (page c2f5000 in one copy). 522 slots were read from
this code with a script that decodes `adrp`, `add` and `blr`. The code that loads a slot and calls it was then searched in
`main`. The script is not in the repo (scratch only).

- Slots: 522, equal to the 522 resolved addresses in the log.
- The call sites of the hot functions are few. `nvnCommandBufferDrawArraysInstanced` has one site (aaaf970),
  `nvnQueueSubmitCommands` one (aac7d78), `nvnQueuePresentTexture` one (aa83738), `nvnDeviceGetTextureHandle` one (aaa1fcc),
  `nvnVertexAttribStateGetFormat` one (aa859fc), `nvnBufferMap` 33. The sites lie between a9b82e0 and ac8714c, in a layer of
  the game that wraps NVN. So a host NVN is called from a small number of places.
- The search is not complete. A load of a slot that the compiler placed far from its `adrp`, or through another register,
  is not found: 31 of the 242 called functions had no site by this method. The counts of sites are therefore lower bounds.
- One wrapper was read: the check of a vertex state cache at aa859fc. The game keeps its own copy of the vertex state, and
  it asks the NVN state object through `nvnVertexAttribStateGetFormat` and `GetStreamIndex` (results in out parameters), then
  compares them with its own fields. It does not read the bytes of the NVN object. This is the source of the 4.2 million
  getter calls: a cache check for each state change. A host NVN answers them from its own data.

This does not prove that no code of the game reads an NVN object directly. It shows that the state objects, which are
the most called ones, are read through calls. The reading of a raw field stays an open question for the buffer, texture
and command buffer objects. Stage 3 (the smallest drawing path) shows it at once: a field that the game reads and the
host NVN does not provide gives a wrong value in the first scene.

## 1-4 Inline work and extra queue commands

Method: read from the API list, so far. Not yet checked in the code.

- Data that the CPU writes for the GPU without a command: `nvnBufferMap` (2.29 million) and `nvnMemoryPoolMap` (3485) give a
  CPU pointer into the memory of the GPU, and the game writes uniform data and vertex data there. Texture data is written the
  same way (storage in a memory pool). The runtime already handles this with write tracking (`gpu_mem_written`, memory
  `gmem_*`), and the host NVN must keep this model: the guest memory is the truth, and the renderer reads it when a draw
  needs it.
- Descriptors: `TexturePoolRegisterTexture` (6443) and `SamplerPoolRegisterSampler` (82) write texture and sampler descriptors
  into the pool memory. `DeviceGetTextureHandle` (2.24 million) builds the 32-bit handle that the game writes into its
  uniform buffers, and the shader reads it. Open: the handle format, and whether the game ever reads the descriptor memory.
- Commands that the library adds by itself (run of 2026-11-02 with the new `gpu-submit:` lines, 40 submissions logged,
  all on channel 20, none with a syncpoint increment or a fence wait):
  - `nvnQueuePresentTexture` sends the whole frame in one submission of 168 to 172 GPFIFO entries (17 submissions had 168,
    20 had 171). `nvnQueueSubmitCommands` sends nothing. So the 77 command buffers of the game reach the GPU with the present.
  - Two kinds of entry are in it. The entries that the library makes lie in the queue's control memory (gpu 5596324xx),
    are small and carry the SYNC bit: three at the start (19, 1 and 20 words) and the last one (56 words). The entries of the
    game's command buffers lie in the command memory (gpu 556e27400 and others), have no SYNC bit and are about 54 words
    in the first ones that were listed.
  - The first 20 words of the last entry (56 words) decode as: a 3D method 0x451 (inline, data 0), a write to 0xB2, a
    second 0x451, a report semaphore write (3D 0x6C0 to 0x6C3, four words), an inline 0x3E0, `SET_MME_SHADOW_RAM_CONTROL`
    (3D 0x49, value 2), and the setup of color target 0 (3D 0x200): address 5_53dc0000, 1920 x 1080, format 0xD5.
    So the library restores the engine state and binds the swap chain image for the work that follows. Words 20 to 55 and the
    contents of the other library entries are not logged yet.
- Events and `QueueFlush` are not used in the scenes that were played.

## 1-5 Summary and input for stage 2

Facts that hold (from the runs and the code of 2026-11-02):

1. The game reaches NVN only through pointers. 523 functions are resolved, 242 were called in a run through the boot, title,
   village, wild area, battle and menus, and the call sites in the game are few (a layer around a9b82e0 to ac8714c). The
   hook sees all calls. A recompile with `--hook-direct-calls` is not needed.
2. One queue and one window. A frame is: acquire, submit 77 command buffers, fence, present. The submit does not reach the
   GPU. The present sends the frame as one submission of about 170 GPFIFO entries.
3. Command buffers are recorded once and replayed many times (`CallCommands` 4.34 million against 1.33 million recordings).
   The host NVN needs a recorded command buffer that can be called again.
4. The library adds its own pieces to every submission (a prologue and an epilogue that reset the engine state, write a
   report semaphore and bind the window image as color target 0). A host NVN has to do the equivalent work by itself.
5. State objects are read through getters (the vertex state cache is the main user, 4.2 million calls). The host answers from
   its own copy. The object memory has a minimum size per type (the table above).
6. The CPU writes buffer and texture data into memory that the GPU reads (`BufferMap`, `MemoryPoolMap`). The runtime keeps the
   guest memory as the truth and tracks writes. A host NVN keeps this.
7. 281 resolved functions were not called: transform feedback, tessellation, events, buffer copies, `QueueFlush`, fences
   inside command buffers. They can be stubs that stop with a clear message.

Open points that stage 2 and 3 settle:

- A raw read of an object field by the game (not found for the state objects, not excluded for the others).
- The values that `nvnBufferGetAddress` and `nvnMemoryPoolGetBufferAddress` give, and the 32-bit texture handle format. The
  game writes them into its own data, and the shaders read them.
- The exact sizes of the objects (only minimum sizes are known).
- The contents of words 20 to 55 of the last entry, and of the other library entries.

What this means for the choice of way (stage 2):

- Way A (replace NVN completely) needs about 242 working functions and the frame handling of point 4, plus the recording
  object of point 3. Its risk is point 4 and the open points above, because the game and the shaders read what the library
  produces.
- Way B (keep the NVN library, replace the Maxwell layer in part) keeps points 2 to 4 as they are, because the library still
  writes the stream. It removes only the decode of the stream for chosen commands.
- The difference between the two ways is therefore mainly the work of points 3 and 4 and the open points.
