<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the NXAevora contributors -->

# NVN on Vulkan

This document tells how NXAevora Engine runs the game's NVN graphics on the host. It covers the two paths, the hook that replaces the game's NVN library, the host objects, the command lists, the frame job, the replay, the fences and the shader translator. It is for contributors who change the GPU front end or the native NVN layer.

## How it works

### The two paths

| Path | Selection | What runs |
| --- | --- | --- |
| Old path | `nvn_legacy=1` | The game's own NVN library runs as guest A64 code. Its GPFIFO submissions go through `engine/src/nx/hle/services/nvdrv.c` into `gpu_submit` in `engine/src/nx/gpu/gpu.c`. The GPU thread decodes the Maxwell stream in `engine/src/nx/gpu/maxwell/` and the Vulkan renderer in `engine/src/nx/gpu/vk/` draws it. |
| Native path | `nvn_legacy=0`, the default | The host answers the NVN calls. The game's library is not run. The host records the objects and the command lists, and the GPU thread replays them into Maxwell methods. |

The setting is `nvn_legacy` in `engine/src/nx/runtime/settings.txt`: an int, 0 or 1, default 0, read at startup, with the words `Off|On`. `nvn_native_init` returns at once when it is set.

A setting named `nvn_native` does not exist in the code. Every match in `engine/src` is an identifier prefix such as `nvn_native_init`, a handler name or a comment. `engine/docs/DEPRECATIONS.md` records the removal. Use `nvn_legacy`.

### How the native path takes the calls

| Step | Function | What it does |
| --- | --- | --- |
| 1 | `nvn_native_init` | Called from `snx_main` before any guest thread runs. It finds `nvnBootstrapLoader` with `kernel_find_symbol_exact`, checks that the entry is inside the code range and aligned, checks that its dispatch slot is not empty, and puts `bootstrap_native` in the slot. It sets `active = 1` only after the hook succeeds. |
| 2 | `bootstrap_native` | Reads the requested name from the guest pointer with `nvn_guest_name_read`, answers it from the host name table through `host_lookup`, and installs a handler with `install`. For `nvnDeviceGetProcAddress` it puts `getproc_native` in that function's slot once. |
| 3 | `getproc_native` | Answers every later `nvnDeviceGetProcAddress` call from the host name table, and installs a handler for every other name. |
| 4 | `proc_table_build` | Finds the `sdk` module, compares its build id with `known_library.build_id`, reads the NVN name offset table, the name strings and the address table from the module's rodata and data, and checks their spans and the addresses. It runs at the first lookup. |
| 5 | `install` | Checks that the resolved address is inside the code range and aligned, records the name and handler in `slots`, and puts `native_call` in `g_disp[(addr-g_code_lo) / 4]`. |
| 6 | `native_call` | Runs the handler and then sets `c->pc = x30`. Without this step the caller ran its last block again. |

The device argument of `nvnDeviceGetProcAddress` is not read. The host answers from the name table, so the library lookup does not run.

When the bootstrap symbol, its code range or its dispatch slot is missing, `nvn_native_init` logs one warning and returns with the native path inactive. The game's own library then runs, exactly as with `nvn_legacy=1`. The native layer does not fake a success.

### The host objects

`NvnObjTab` in `engine/src/nx/gpu/nvn/nvn_objtab.h` keeps one record for each NVN object, keyed by the guest address. The game allocates the object memory. The host never writes it. The record holds the address, the kind, a generation and zero-filled host data. `nvn_objtab_put` makes or replaces a record and increases the generation, `nvn_objtab_remove` removes a live record, and `nvn_objtab_valid` checks a generation. The table has no lock. Its owner serializes the calls.

| Kind | Owner | End of life |
| --- | --- | --- |
| Device | `h_device_initialize` | Kept to the process end. `nvnDeviceFinalize` is not supported and logs that the records stay. |
| Queue | `h_queue_initialize` | Kept to the process end. `nvnQueueFinalize` is not supported and logs that the record stays. |
| Window | `h_window_initialize` | `h_window_finalize` removes the record. The images belong to the window. |
| Memory pool | `h_memory_pool_initialize` | `h_memory_pool_finalize` removes the record, unmaps the GPU ranges and frees the GPU address. A failed initialize removes its record. |
| Buffer | `h_buffer_initialize` | A failed initialize removes the half-made record. `h_buffer_finalize` is a no-op, so the record stays, as the library leaves the object readable. |
| Texture | `h_texture_initialize` | A failed initialize removes the half-made record. `h_texture_finalize` is a no-op, so the record stays. Imports are refused with a log line. |
| Sampler | `h_sampler_initialize` | `h_sampler_finalize` is a no-op, so the record stays. |
| Command buffer | `h_command_buffer_initialize` | `h_command_buffer_finalize` releases its owned list references and removes the record. |
| Sync | `h_sync_initialize` | `h_sync_finalize` removes the record. |
| State and builder records | Their builders and setters | Kept in the same table. The builders (`DeviceBuilderRec`, `QueueRec`, `WindowRec` and the rest) hold the values until the object is created. |

The pure layout and descriptor encoders are in `engine/src/nx/gpu/nvn/nvn_params.c`. They make the storage sizes, the alignments, the descriptor words and the buffer and texture checks without guest memory.

### Command buffers

A recording appends host ops to an `NvnOpList` in `engine/src/nx/gpu/nvn/nvn_cmdlist.h`. The op codes cover pools, render targets, viewports, scissors, clears, barriers, calls, binds, draws, dispatches, copies, counters, events, syncs and uniforms. `nvn_oplist_append` pads each payload to the alignment and refuses a payload above `NVN_OP_MAX_PAYLOAD`.

`h_command_buffer_end_recording` calls `nvn_list_publish` in `engine/src/nx/gpu/nvn/nvn_cmdlist.c`. The list moves into an `NvnListStore` slot and the call returns a handle. The handle names the slot and a generation, is never 0 and has bit 0 clear. The list is immutable after that.

The store keeps a reference count for each live list. `nvn_list_find` returns the list of a live handle, `nvn_list_ref` adds a reference, and `nvn_list_unref` removes one. The last reference frees the list and gives up the references of its CALL ops. A CALL op takes a new reference before the copy, so a copied list does not depend on its source.

A queue keeps a pending frame in `engine/src/nx/gpu/nvn/nvn_frame.h`. The frame holds one reference of each submitted list and the tokens that `nvnQueueFenceSync` named. The limits are `NVN_FRAME_LISTS_MAX` 65536 lists and `NVN_FRAME_TOKENS_MAX` 256 tokens. A frame that does not fit logs a line and does not add the new handles.

### The frame job and the replay

`nvnQueueSubmitCommands` adds the lists to the pending frame of the queue. `nvnQueueFlush`, `nvnQueueFinish`, `nvnQueuePresentTexture`, a fence with the flush-for-CPU flag and a submit with such a fence call `queue_post_frame`. It promises one syncpoint increment and sets the fence values. The caller then queues `frame_run` with `gpu_call`.

The GPU thread runs `frame_run`:

1. `frame_begin` starts the frame.
2. `frame_read_parts` reads the window or the texture target. A flush job skips this step.
3. `frame_replay_init` replays the queue init macro words, in the first job of a queue only.
4. `frame_set_target` binds the window or the texture target. A flush job skips this step.
5. `frame_replay_lists` calls `nvn_replay_list` for each submitted list.
6. `frame_end` ends the stream and increments the queue syncpoint.
7. `frame_show` presents a window image through `g_renderer->present_buffer`. A flush job skips this step.
8. `frame_finish` frees the image state and the list references.

`nvn_replay_list` in `engine/src/nx/gpu/nvn/nvn_replay.c` walks the ops and turns each one into Maxwell 3D methods through `NvnReplayEnv`. The module has no lock, no global state and no allocation, so a host test can run it. A bad op calls `fault` and is skipped, then the replay goes on. `nvn_native_replay_env` in `nvn_native.c` fills the environment: methods go to `maxwell3d_method` on a private channel from `gpu_channel_open`, and the lists, descriptor pools and render target textures are read from the host records.

The render targets are in `engine/src/nx/gpu/nvn/nvn_rt.h`. `nvn_rt_emit` writes `COLOR_TARGET_CONTROL`, up to 8 target blocks, the disable words, the null depth words, `SURFACE_CLIP` and the sample mode. A refused request writes nothing and calls `fault` once. The same module holds the word builders for `ClearTexture`, `CopyTextureToTexture`, the buffer and texture copies and the downsample.

### Present and fences

`h_queue_present_texture` marks the image PRESENTED and posts a `NVN_FRAME_PRESENT` job. `frame_show` presents through `g_renderer->present_buffer`. `frame_finish` frees the image and the list references and signals `frame_done_cv`.

A window acquire paces outside the object lock with `present_pace`, picks a FREE image, and waits on `frame_done_cv` only while an image is PRESENTED. With no free and no presented image it fails with `no free window image` and writes `NVN_WINDOW_INVALID_INDEX`. A successful acquire signals the sync. `h_sync_wait` waits on the queue's syncpoint value with the timeout and compares across the 32-bit wrap. `h_queue_finish` waits on the same value. A cross-queue wait on a pending fence is not supported. It returns 0 with a log line.

### Threads and locks

Guest game threads run the host handlers on their guest cores through `native_call`. The GPU thread runs the frame jobs through `gpu_thread` and `gpu_call` in `engine/src/nx/gpu/gpu.c`.

| Lock | Mode | What it protects |
| --- | --- | --- |
| `objs_lock` | Exclusive | The object records and their generations, for a handler that creates, changes or removes a record. |
| `objs_lock` | Shared | The command buffer handlers, which change only their own command buffer, so several threads can record at the same time. The buffer handlers `h_buffer_map`, `h_buffer_get_address` and `h_buffer_get_size` also take it, because they read the records and write none. |
| `lists_lock` | Exclusive | The published list store. |
| `slots_lock` | Exclusive | The handler slots and the NVN name table build. |

A command buffer has an owner thread id. `command_hold` takes `objs_lock` shared, claims the command buffer with one compare-and-swap on its owner id, and falls back to the exclusive lock when another thread owns it. The record changes only under its owner.

The lock order is `objs_lock`, then `lists_lock`, then the GPU queue lock. Never take `objs_lock` with `lists_lock` or the GPU queue lock held. `frame_done_cv` wakes the image waiters.

### An unhandled function

`handler_for` looks a name up in `native_procs`. A name that is not there gets `missing_handler`. On the first call of that function, `missing_handler` either stops the game with a fatal line that names the function and the first four arguments, or logs one line and returns 0. The setting is `nvn_native_missing`: an int, 0 or 1, default 1, read at startup, with the words `Log|Stop`.

A lookup of a name that the NVN library does not have also stops the game, unless `nvn_native_missing=0`. In that case it logs the name, at most 16 names.

At a clean exit, `nvn_native_report` lists the resolved addresses, the number without a handler and the missing functions that the game called. `snx_save_before_exit` calls it.

### The shader translator

`engine/src/nx/gpu/shader/shader.h` is the translator. `shader_to_spirv` translates a Maxwell graphics program and `shader_compute_to_spirv` translates a compute program. Both return allocated SPIR-V words. The module holds `decode.c`, `shader.c`, `structure.c`, `spirv_translate.c`, `spirv_compute.c`, `corpus.c` and `shipped.c`.

The translator is shared by both NVN paths. The renderer makes pipelines from the game's shader code in `engine/src/nx/gpu/vk/`. The translated shaders and the pipelines are cached in `shader_cache/`. The runtime change `0002-global-red-add-u64` adds `RED.E.ADD.64` to the translator and declares `Int64Atomics`, so each global buffer gets a 64-bit aliased view. It also needs the device feature `shaderBufferInt64Atomics`. A device without the feature refuses the program.

### Coverage, diagnostics and tests

`tools/nvn_coverage.py` writes `docs/records/graphics/nvn-coverage.md`. It reads the `nvn:` and `nvn-used:` lines of a trace log, or the `sdk` module name table and `nvn-calls.md`, and the `native_procs` table. `--check` writes nothing and exits 1 when the file would change.

The checked-in coverage record has 523 functions that the game resolves. All 523 are `implemented`, 0 are `stub-log`, and 242 were called. 281 of them were never called in a recorded run (group G10 of the coverage record). `native_procs` has 522 entries. The 523rd lookup, `nvnDeviceGetProcAddress`, is served by `getproc_native`. A later change of the table can make these counts stale, so run the tool after a handler change.

The diagnostics are in `engine/src/nx/gpu/nvn/diag/`:

| File | Setting | What it logs or writes |
| --- | --- | --- |
| `nvn_trace.c` | `nvn_trace=1` | Each resolved function and the GPFIFO submissions of the queue calls. |
| `nvn_enum_trace.c` | `nvn_trace=1` | The device queries and their values. |
| `draw_record.c`, `draw_record_sig.c` | `nvn_drawrec=1` | The draw, clear, dispatch, copy and present calls to `<game>.drawrec<N>.frames.txt` and `.sigs.txt`. |
| `nvn_measure.c`, `nvn_measure_calc.c` | `nvn_measure=1` | Guest and GPU time per frame, handler times and lock waits. |

The NVN settings and their defaults are in `engine/src/nx/runtime/settings.txt`: `nvn_trace` 0, `nvn_legacy` 0, `nvn_drawrec` 0, `nvn_measure` 0, `nvn_native_missing` 1, `nvn_selftest` 0, `nvn_selftest_mutate` empty and `nvn_exectest` 0.

The in-game tests are in `engine/src/nx/gpu/nvn/test/`. `nvn_selftest.c` needs `nvn_trace=1` and `nvn_legacy=1`. `nvn_exectest.c` needs `nvn_trace=1` and `nvn_legacy=0`. Both are diagnostics and draw nothing. The host tests are in `engine/tests/nvn/`: `nvn_objtab`, `nvn_cmdlist`, `nvn_frame`, `nvn_replay`, `nvn_rt`, `draw_record`, `nvn_measure`, `nvn_params`, `nvn_exectest`, `nvn_enum_trace`, `nvn_command_gap` and `nvn_control`, with the selftest parts `nvn_selftest_cmp`, `nvn_selftest_mem`, `nvn_selftest_rec`, `nvn_selftest_dec` and `nvn_selftest_regdiff`. `engine/tests/CMakeLists.txt` adds them to `ctest`.

## Current state

### Complete and used in a normal run

- The native path is the default. `nvn_legacy=0` and `nvn_native_init` hooks the loader.
- The bootstrap and proc-address lookups are answered by the host name table.
- Every resolved function gets a handler slot. All 523 names have a handler.
- The object records are made and removed by the handlers.
- Memory pools map and unmap their GPU ranges.
- Textures, samplers, buffers and pools have records and pure encoders.
- Command buffers record host ops and publish lists at `nvnCommandBufferEndRecording`.
- `nvnQueueFlush`, `nvnQueueFinish`, `nvnQueuePresentTexture` and `nvnQueueFenceSync` with the flush-for-CPU flag post frame jobs. `nvnQueueSubmitCommands` adds lists to the pending frame and posts only when one of them has that fence.
- The GPU thread replays each list into Maxwell methods.
- The frame presents the window image through the renderer.
- Fences follow the queue syncpoint. `nvnSyncWait`, `nvnQueueWaitSync` and `nvnQueueFinish` wait on it.
- The read-write `objs_lock` split is in use for the command buffer handlers and the three buffer getters.

### Present but not used or not complete

- `nvnQueueFinalize` is not supported. The record stays to the process end.
- The queue usage getters refuse with 0.
- `nvnDeviceFinalize` is not supported. The records stay.
- Imported memory pools fail with a log line.
- Texture imports fail with a log line.
- `nvnBufferFinalize`, `nvnTextureFinalize` and `nvnSamplerFinalize` are no-ops, so their records stay to the process end.
- A cross-queue wait on a pending fence is not supported. It returns 0 with a log line.
- The name table is read for one library build, the `sdk` module of update 1.1.1. Another build stops the game at the first lookup.
- `nvn_selftest`, `nvn_exectest`, `nvn_drawrec`, `nvn_measure`, `nvn_trace` and `nvn_enum_trace` are diagnostics and are off by default.
- `nvn_selftest` does not run on the native path. It needs `nvn_legacy=1`.
- The old path stays in the code as a comparison target.

The render-target clip limit is `NVN_REPLAY_CLIP_LIMIT` 0x4000. It is a provisional constant. Its source, the device word at `+0x18`, is not known. The code uses the same value for the render targets and the clears.

For the history of the NVN work, see [nvn-design.md](../../records/graphics/nvn-design.md) and [nvn-coverage.md](../../records/graphics/nvn-coverage.md).
