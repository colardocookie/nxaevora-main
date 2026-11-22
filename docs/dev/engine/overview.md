<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the NXAevora contributors -->

# NXAevora Engine overview

This document is the entry point to the NXAevora Engine specification. It tells what the engine is, how a decrypted game folder becomes a running game, where the code lives, and how to build and test it. It is for contributors who change the engine or read its code.

## How it works

### What NXAevora Engine is

NXAevora Engine is a vendored copy of ScarletNX at upstream commit `8efbf9c4`. It plays Pokemon Legends Arceus, a Nintendo Switch game, as a native program on Windows. The host code keeps the upstream Linux support. The game is recompiled ahead of time (AOT) into a shared library that the engine loads.

The copy removes three groups of upstream code:

| Area | What is removed |
| --- | --- |
| Guest drivers | The GBA, 3DS, Vita and Linux AArch64 ELF drivers, with their build options. The Switch driver and the 32-bit ARM path stay. |
| Front ends | The Android, iOS and macOS front ends, their build support and the Mach-O linker. The Windows and Linux host builds stay. |
| Dump input | hactool, extraction, keys, BKTR and the dump options. `fs_init` stops a data folder with no `exefs/`. |

The full list is in [runtime-changes.md](../../build/runtime-changes.md).

### The program flow

The build makes one program. `engine/src/multicall/main.c` holds the only `main`. It runs the Switch driver when the program name is `nxaevora-engine-switch` or the first argument is that name. Any other name falls through to the Switch driver.

`snx_switch_main` in `engine/src/nx/runtime/entry.c` runs these steps in order:

1. The setup options of `snx_setup_options`.
2. The CLI translation of `snx_command_translate`.
3. The `--mcp` mode, when the first argument is `--mcp`.
4. `snx_driver` in `engine/src/nx/runtime/driver/driver.c`.
5. `snx_main` in `engine/src/nx/runtime/main.c`, which runs one game.

The path from a decrypted folder to a running game:

1. The input is a decrypted folder with `exefs/`, `romfs/` and `control.nacp`. An encrypted dump is refused.
2. `snx_driver` finds the game. A game that is absent or not stamped runs `-r` as a child stage named `Recompilation` through `snx_run_stage`.
3. `snx_recompile_game` loads the input with `snx_load_program`, applies the mod patches with `patch_modules`, reads the hints with `read_hints`, stages the data and writes the game's `.ini` and its `romfs=`, builds and links the module with `snx_build_module_llvm` (which calls `snx_link`), deploys the runtime with `snx_deploy_runtime`, and writes the `.abi` stamp last.
4. The driver starts the game program with its folder, `recompiled/<id>/`, as the working directory. The recompile ran as a separate process.
5. `snx_main` locates the game, loads the AOT module, opens the `.ini`, sets up the file system, memory, kernel and dispatch table, and loads the hints and the guest executables.
6. `snx_main` installs the NVN native path and the NVN trace, then the Arceus layer, then the JIT. It sets up the SVCs, the services, the RomFS and the GPU, starts the window, waits for the shaders, and runs the main guest thread and the frontend loop.

The recompiler has its own document: [aot-recompiler.md](aot-recompiler.md).

### Commands

`snx_command_translate` in `engine/src/nx/runtime/command.c` maps the first word to the old option form:

| Command | Translation |
| --- | --- |
| `recompile ...` | `-r ...` |
| `run PROGRAM ...` | Removes the word `run`. Keeps the program and its settings. |
| `settings list` | `--settings` |
| `settings list --scope=recompile` | `--settings=recompile` |
| `settings schema`, `settings get`, `settings set`, `settings check` | Run by `engine/src/nx/runtime/settings_cli.c`. |
| `logs`, `cache` | Stop with `SNX_EXIT_USAGE` until the command exists. |

`--help` must come alone. Any other argument gives exit code 2.

`recompile` and `run` also take `--progress=jsonl` (`common/utilities/progress.c`). Then stdout carries only JSON Lines and the human text goes to stderr.

### Exit codes

`engine/src/common/utilities/exit_codes.h` defines the codes. A child stage uses the same table, and the driver passes its code on unchanged.

| Code | Name | Meaning |
| --- | --- | --- |
| 0 | `SNX_EXIT_OK` | Success, a normal game end or a window close. |
| 1 | `SNX_EXIT_INTERNAL` | An internal error, a bug or out of memory. |
| 2 | `SNX_EXIT_USAGE` | Wrong usage, an unknown option or command, or a bad argument count. |
| 3 | `SNX_EXIT_GAME_FILES` | Game files missing or wrong: the input, `exefs/main`, the NPDM or the RomFS. |
| 4 | `SNX_EXIT_RECOMPILE` | The recompile failed: compile, link, staging or build. |
| 5 | `SNX_EXIT_START` | The start failed before the window. |
| 6 | `SNX_EXIT_CRASH` | A crash of the game: a fatal, a crash report or an unhandled exception. |
| 7 | `SNX_EXIT_SETTING` | A setting value was refused by `settings set` or `settings check`. |
| 8 | `SNX_EXIT_GAME_CODE` | The game ended with a nonzero code. |
| 130 | `SNX_EXIT_INTERRUPT` | Stopped by the user with Ctrl+C. |

On POSIX, a driver stage that cannot be prepared exits 126 and one that cannot be started exits 127. These two are stage conventions, not values in `exit_codes.h`.

### The settings file

The game's settings file is `<g_confdir><g_exename>.ini`. `snx_main` opens it with `ini_open`. `g_confdir` is beside the game, or the config folder of an installed build (`engine/src/nx/runtime/main.h`).

`engine/src/nx/runtime/settings.txt` is the one schema for every setting of the runtime, the launcher and the recompiler. The build reads it and generates the `Settings` struct, `g_settings` and the `IniOpt` tables. `engine/cmake/settings.cmake` runs that step. A row with scope `recompile` also enters `g_recompile_game_ini`.

A `--KEY=VALUE` argument overrides a value for one run. `ini_poll` reloads a changed file while the game runs. When the file is absent, the recompile makes it with a header and `[Display] language=1`.

The CLI commands use `snx_settings_load`, `snx_settings_value` and `snx_settings_save` in `engine/src/nx/runtime/options.c`. `settings schema` prints one JSON document, `{"v":1,"settings":[...]}`, made at run time from the same tables.

The game's `.ini` holds the settings of the runtime only. The Arceus timing switches are environment variables and are not in this file. See [arceus-timing.md](arceus-timing.md).

### Folder map of `engine/src/`

| Folder | Contents |
| --- | --- |
| `common/cpu/aarch64/` | A64 CPU state and decoder, `cpu.h` and `a64_decoder.c`. |
| `common/cpu/arm/` | The 32-bit A32 and Thumb blocks, `a32.c`, `thumb_decode.c`, `exec.c` and `llvm.c`. |
| `common/cpu/recompiler/` | Function discovery and the LLVM backend, `codegen.c` and `llvm/`. |
| `common/filesystem/` | Host file and lock primitives, `host.h` and `tree.h`. |
| `common/gpu/vk/` | Vulkan helpers shared by front ends, `swapchain.h`. |
| `common/network/` | The shared network stack, `net.h`. |
| `common/platform/` | Windows resource and manifest files, `runtime.rc` and `runtime.manifest`. |
| `common/runtime/` | `RtAPI`, the table that generated code calls, `interface.h`. |
| `common/utilities/` | Cross-cutting helpers, `common.h`, `mods.h`, `patches.h`, `exit_codes.h`, `logscan.h` and `demangle.h`. |
| `nx/gpu/` | The GPU front end `gpu.h` and the back ends `maxwell/`, `vk/`, `shader/`, `texture/`, `video/` and `nvn/`. |
| `nx/hle/` | The Horizon kernel and services: `hle/`, `kernel/`, `services/` and `svc/`. |
| `nx/loader/` | Decoding and module build, `loader.h`, `module_llvm.c` and `module_variants.c`. |
| `nx/memory/` | The guest address space, `mem.h`. |
| `nx/runtime/` | Startup, CLI, settings, driver, platform layer, the Arceus layer and save states. |
| `gui/` | The in-engine launcher menu only. It is not in this set of documents. |
| `multicall/` | The one `main` of the program. |

### The NXAevora changes by area

`docs/build/runtime-changes.md` has 106 rows. The areas do not overlap:

| Area | Rows | Main items |
| --- | --- | --- |
| Arceus timing | 36 | Elapsed-time clocks, physics and navigation steps, world timers, movement, battle and script ticks. |
| NVN | 38 | The native NVN layer, object records, command lists, replay, fences and the selftest. |
| Vulkan renderer and game graphics | 6 | FXAA, scaling, SMAA and scaled texture fetches. |
| In-game UI | 8 | The Dear ImGui windows and the display settings. |
| Input | 3 | Native mouse input and the keyboard map. |
| Recompiler and core | 2 | The 64-bit global add and the 256-key `.ini` store. |
| Removals, naming, docs and CLI | 13 | The three removals above, the engine names, the CLI subcommands and the decrypted-folder docs. |

### Build and test

The Windows build uses MSYS2 CLANG64. The steps, the packages and the presets are in [ENGINE_BUILD-NOTES.md](../../build/ENGINE_BUILD-NOTES.md). The output is `engine/build/default/` with `nxaevora-engine.exe` and `snx-shaderc.exe`.

The host tests are `add_test` entries under `engine/tests/`, run by `ctest`. `ctest` needs `APPDATA` set on Windows. The test options are `SNX_TEST_SANITIZERS`, `SNX_TEST_VULKAN`, `SNX_TEST_ARM_AOT` and `SNX_TEST_VP9_VECTORS` in `engine/tests/CMakeLists.txt`. The NVN tests are `engine/tests/nvn/`. The Arceus tests are `engine/tests/arceus_clock.c` and `engine/tests/arceus_timing.c`.

Each engine change gets a row in [runtime-changes.md](../../build/runtime-changes.md).

## Current state

- The engine is built and tested on Windows. The Linux host code stays, but no CI job builds it. The Switch driver is the only guest driver, and the 32-bit ARM path stays.
- The native NVN path is the default. The old path remains as a comparison target and is selected by `nvn_legacy=1`.
- The Arceus timing layer supports only the `main` module of build 1.1.1. Another build is refused with one log line and installs nothing.
- The input is a decrypted folder. An encrypted dump is refused.
- The recompiler has no interpreter. A guest pc that neither the AOT module nor the JIT can run ends the game.
- The in-engine GUI is a launcher menu. It is not in this set of documents. The launcher specification comes later.

## The four engine documents

- [overview.md](overview.md): this document.
- [aot-recompiler.md](aot-recompiler.md): the input, the stages, the output, the dispatch table, the JIT fallback and the hints.
- [nvn-vulkan.md](nvn-vulkan.md): the two NVN paths, the host objects, the command lists, the frame job, the replay and the shaders.
- [arceus-timing.md](arceus-timing.md): the elapsed-time clocks of the game and the hook groups that follow the frame step.
