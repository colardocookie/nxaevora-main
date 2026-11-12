<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the NXAevora contributors -->

# Game patches and diagnostic patches

The runtime changes of ScarletNX are in the source of `third_party/ScarletNX` (see `docs/build/runtime-changes.md`). This
folder holds the patches of the game code and the diagnostic patches.

## Game code patches

| Patch | What it does |
| --- | --- |
| `native-mouse-look.pchtxt` | A patch of the game code (IPSwitch text, update 1.1.1). The camera's angle setter 0x99437c0 takes the mouse word at 0x9943980 and adds it to the yaw and to the pitch arc position. The code and the word sit in the words that the setter's mode 1 path frees (it jumps into the end of the mode 0 path). Put it in `game/recompiled/<id>/<id>_data/mods/native-mouse-look/exefs/` and recompile. Set `mouse_native_address=160708992` in the game's `.ini`. |
| `camera-unclamp.pchtxt` | A patch of the game code (IPSwitch text). It sets the clamp limits of the input manager to 8.0 and -8.0. A test showed that the camera does not use that clamp, so it has no effect on the camera. It stays as a record. Remove the mod when the camera patch exists. |
| `dynamic-resolution-off.pchtxt` | A patch of the game code (IPSwitch text, update 1.1.1). It turns off the dynamic resolution of the 3D image. The per-frame update 0x8cc85a4 returns at once, so the measured frame time no longer lowers the level, and the mode selector 0x8cc8888 always takes its default block, so every mode uses the scale 1.0 by 1.0 (the shader parameters `3DBufferWidthScale` and `3DBufferHeightScale`). A PC does not need the lower sizes. Put it in `game/recompiled/<id>/<id>_data/mods/dynamic-resolution-off/exefs/` and recompile. A run at 1440p and 2160p showed no crash. It does not change the jaggies of a render resolution other than 1080p. |

To use `camera-unclamp.pchtxt`, put it in `game/recompiled/<id>/<id>_data/mods/camera-unclamp/exefs/` and recompile.

## Diagnostic patches

The patches in `diagnostics/` are tools for an analysis. They are not applied in the normal build. They were made
against the source before the vendoring: a patch may need a manual fix to apply to the current files.
`0003` is the stick trace and `0011` is the angle search (both for the camera analysis, apply `0003` after `0001`
and `0011` after `0003`). `0004` to `0008` find the wait of a stop of the game (`hitch_dump`, apply them in the order
of their numbers, `0004` first). The `0001` that `0003` needs is now in the source. Use the path `diagnostics/` in
the command, from `third_party/ScarletNX`, for example `git apply ../../recomp/patches/diagnostics/0004-hitch-dump.patch`.

| Patch | What it does |
| --- | --- |
| `0003-stick-trace.patch` | The stick trace, a diagnostic for the camera analysis. Apply it after `0001`. With `trace_stick=1` in the `.ini`, the right stick is held at a known value after `trace_stick_start` seconds, the guest memory is searched for the floats that the game makes of it, and the code that reads or writes them is logged for `trace_stick_seconds`, with the call chain of the first reads (Windows only). It is not applied now, because the camera search is paused. See `docs/input/camera-analysis.md`. |
| `0004-hitch-dump.patch` | A diagnostic for the stop of about 1.3 s at the start. The new setting `hitch_dump` (milliseconds, 0 = off) writes the game's threads, each with its state, what it waits for and its call chain, to `hitch-N.txt` when the frame count stands still for that time, and again at twice the time (six files at most in a run). Two snapshots of one stop show the wait that holds the game. The file goes to the game's data folder, next to `threads.txt`. See `docs/analysis/stutter-analysis.md`. |
| `0005-slow-romfs-read.patch` | With `hitch_dump` on, a RomFS read through the storage service that takes 50 ms or more is logged (`fs: RomFS read of N bytes at OFFSET took M ms`). The offset is the place in the RomFS image, which names the file. Apply it after `0004`. The test `snx-test-fs-service` gets a stub of `plat_ns`. See `docs/analysis/stutter-analysis.md`. |
| `0006-stop-dump-sleep-and-gpu.patch` | Makes the `hitch-N.txt` of `0004` fuller. A thread that slept at least once since the last dump gets the registers of its last `svcSleepThread` and what they point to (a dump takes this only from 100 sleeps otherwise), and the file ends with the GPU thread's state and the host threads that wait for a lock. Apply it after `0004`. See `docs/analysis/stutter-analysis.md`. |
| `0007-stop-dump-object-chain.patch` | In the stop's dump of `0006`, the callee-saved registers of the sleeping thread also show what they point at, then what its first pointer points at, then the third level with the names of its twelve first functions (a pointer to an object, the object, its virtual function table). Apply it after `0006`. See `docs/analysis/stutter-analysis.md`. |
| `0008-sleep-time-in-dump.patch` | The dump of a thread that sleeps (`0006`) adds how much `svcSleepThread` time the thread asked for since the last dump and how long its sleeps took, in all and for one call. A sleep that takes much longer than asked shows here. Apply it after `0006`. See `docs/analysis/stutter-analysis.md`. |
| `0011-angle-search.patch` | The angle search, a diagnostic for the camera analysis. Apply it after `0003`. With `trace_stick=2` the right stick is held in steps after `trace_stick_start` seconds (rest, hold, hold, rest, opposite hold). The floats of the writable guest memory that change in the hold at a steady rate, do not change at rest and change the other way in the opposite hold are the candidates for the camera angle. Their words are then watched as in `0003`, and the game functions that write them are logged. It uses about 1 GB of memory for a short time. It is not applied in the normal build. See `docs/input/camera-analysis.md`. |

