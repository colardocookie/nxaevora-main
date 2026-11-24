<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the NXAevora contributors -->

# The AOT recompiler

This document tells how NXAevora Engine turns a decrypted game folder into an AOT module, how the runtime finds code for a guest pc, and what happens when no code exists. It is for contributors who change the recompiler, the loader or the dispatch table.

## How it works

### The input

`snx_load_program` in `engine/src/nx/loader/loader.c` takes one of these inputs:

| Input | Layout |
| --- | --- |
| Extracted program folder | `exefs/main`, `ExeFS/main` or `main` inside the folder. |
| NRO file | One `.nro` with its optional embedded RomFS. |

The decrypted folder holds `exefs/` with `main`, `main.npdm`, `rtld`, `sdk` and `subsdk*`, a `romfs/` folder, and `control.nacp`. `SnxProgram` holds the modules in load order, the NPDM, the ExeFS and RomFS paths, and the `control.nacp` bytes.

No key is read. An encrypted dump is refused with `SNX_EXIT_GAME_FILES` by `snx_refuse_encrypted_dump` in `engine/src/common/utilities/common.c`. The modules are read by `snx_load_nso` and `snx_load_nro`, and each is laid out at its guest base.

### The stages

| Stage | What it does | Source |
| --- | --- | --- |
| Program load | `snx_load_program` reads the ExeFS modules, the NPDM, the RomFS path and the `control.nacp`. | `engine/src/nx/loader/loader.c` |
| Function discovery | `snx_codegen_analyze` seeds the walk with the FDE starts, the `.init_array` pointers, each module's function list, the relocated pointers, the module text start and the hints. A discovery walk adds branch and call targets in up to 8 rounds. | `engine/src/common/cpu/recompiler/codegen.c`, called by `snx_nx_codegen_analyze` in `engine/src/nx/loader/codegen_view.c` |
| Hints | `read_hints` reads `<base>.hints`, one hexadecimal pc per line. A64 needs word alignment. A32 refuses `value & 3 == 2` and values above 32 bits. | `engine/src/nx/runtime/driver/recompile.c` |
| LLVM IR | `snx_ir_emit` emits one LLVM function for each discovered function. One unit is one chunk of whole functions, 90000 instructions by default. | `engine/src/common/cpu/recompiler/llvm/ir_codegen.c`, `engine/src/nx/loader/module_llvm.c` |
| Unit cache | A unit found in `<cache>/units/` is reused instead of compiled. | `engine/src/nx/loader/module_llvm.c` |
| Descriptor | The module emits `FNS`, `FN_LO`, `FN_LEN`, `snx_stale`, `ENT_OFF`, `ENT_FN`, `ENT_ADDR`, `ENT_PTR`, `MOD` and `snx_module()`. | `engine/src/nx/loader/module_llvm.c` |
| Variant table | `snx_variants()` is built from the JIT cache records, one table per record. It is used only with `--jit-cache`. | `engine/src/nx/loader/module_variants.c` |
| Link | `snx_link` links the runtime library, the descriptor, the units and the cached units into one shared object or PE DLL with no imports. | `engine/src/common/cpu/recompiler/llvm/link.c` |
| Deploy | `snx_deploy_runtime` writes `<NAME>.exe` or `<NAME>`, and the code is `<NAME>_code.dll` on Windows and `<NAME>_code.so` elsewhere. | `engine/src/nx/runtime/driver/recompile.c` |
| Stamp | The `.abi` stamp is removed before the build and written last. `snx_game_stamp_for` builds its text, and the last line is `made opt=2 aot=1 found=... code=...`. | `engine/src/common/utilities/mods.c` |

A 32-bit program takes `snx_build_module_arm` and `snx_module_arm()` instead. The ARM blocks are in `engine/src/common/cpu/arm/`.

### The output

| Path | Contents |
| --- | --- |
| `recompiled/<id>/<id>[.exe]` | The runtime program. |
| `recompiled/<id>/<id>_code.so` or `<id>_code.dll` | The AOT module. |
| `recompiled/<id>/<id>_data/` | The staged data, `control.nacp` and the saves. |
| `recompiled/<id>/<id>.ini` | The settings. |
| `recompiled/<id>/<id>.abi` | The ABI stamp and the made line. |
| `recompiled/<id>/<id>.hints` | The pcs that the runtime missed, beside the program. |
| `recompiled/<id>/<id>_build/` | The generated IR, only with `--keep-ir`. |
| `recompiled/<id>/shader_cache/` | The Vulkan pipelines and the translated shaders. |

The RomFS is not copied. `snx_record_romfs` writes its absolute path as `romfs=` under `[Game]` of the game's `.ini`, and the runtime reads that folder in place.

### How the runtime finds code for a guest pc

`dispatch_init` in `engine/src/nx/runtime/main.c` makes `g_disp`, one `GuestFn` for each 4 bytes of `[g_code_lo, g_code_hi)`. It fills the slots from `SnxModule.entry_addr` and `entry_fn`.

`snx_lookup` in `engine/src/common/cpu/aarch64/cpu.h` reads `c->disp[(pc-code_lo) >> 2]`. An unaligned or out-of-range pc, or a NULL slot, calls `c->rt->miss`.

`rt_miss` in `engine/src/nx/runtime/rtapi.c` calls `jit_resolve_or_add`, then `jit_wait`. If neither gives code, the game ends with a fatal message.

`jit_resolve` in `engine/src/nx/runtime/guest_jit.c` tries three sources in this order:

1. The dispatch slot.
2. The AOT variant table through `variant_resolve`.
3. The JIT service through `snx_jit_resolve`.

The variant table exists only in a module built with `--jit-cache`. `jit_init` reads `g_variants` and builds the dense leader index. When the module has no variant table, `jit_init` warms the hints that earlier runs recorded.

The JIT service is in `engine/src/common/cpu/recompiler/llvm/jit_service.c`. Its limits are in `jit_service.h`:

| Constant | Value | Meaning |
| --- | --- | --- |
| `SNX_JIT_BUDGET` | 20000 | Instructions that a batch discovers beyond the functions at its pcs. |
| `SNX_JIT_BATCH` | 256 | Pcs in one batch. |
| `SNX_JIT_THREADS` | 4 | Compile threads. |
| `SNX_JIT_IMAGES` | 64 | Guest-mapped images that the service tracks. |

A miss goes to a compile queue. A batch discovers the functions with `snx_codegen_discover`, emits IR with `snx_ir_emit`, loads the object into the code arena, and publishes every leader into the dispatch table. JIT code is never freed. The arena lives as long as the service.

### When no code exists

There is no interpreter. `engine/src/nx/runtime/guest_jit.h` states that a pc which neither the AOT module nor the JIT can run ends the game. The module's `interp` field stays NULL because there is no interpreter.

`jit_failure` gives one of three reasons. With the JIT started, it says that the AOT pack has no code for the pc and the JIT could not compile it. With a variant table but no JIT, it says that the AOT pack has no code for the pc and the JIT is not available. With neither, it gives the text of `g_jit_error` when one is set, else `no AOT pack and no JIT`. `rt_miss` then ends the game with this message:

`The game reached code at <pc> that the recompiler missed, and <reason>`

### How a miss reaches the `.hints` file

After code is found, `rt_miss` calls `hint_add(pc)` when the pc is inside the code range. `hint_add` adds one pc and sets the dirty flag. The hint is not recorded for code that the game mapped, because the pack never holds such code and it lands elsewhere next time.

`save_hints` writes one lowercase hexadecimal pc per line beside the file, then replaces the file. `snx_save_before_exit` calls `save_hints`, and the fatal paths save too. `load_hints` reads the file at start.

`tools/jump_table_scan.py` finds the jump tables of the game's AArch64 modules and lists the targets of their `BR` instructions. It finds two compiler patterns: a byte, half or word table added to a base address, and a signed table relative to its own address.

| Option | Effect |
| --- | --- |
| `--hints FILE` | Writes one hexadecimal pc per line, the `.hints` format. |
| `--merge` | Keeps the pcs that the file already holds, which are the misses that the runtime recorded. |
| `--check HEXPC` | Says which table holds the pc. |

The tool needs `SNX_SCARLETNX` and the MSYS2 tools on `PATH`, as the upstream tools do.

### Patches and the recompile settings

`recomp/patches/` holds game code patches in IPSwitch text: `native-mouse-look.pchtxt`, `camera-unclamp.pchtxt` and `dynamic-resolution-off.pchtxt`. A patch goes in a mod's `exefs/` folder and applies at the next recompile. `patch_modules` applies the mods' patches after the ExeFS is loaded and before any code is made from it. The `diagnostics/` patches are analysis tools and are not applied in a normal build. `recomp/patches/README.md` tells each one.

`recomp/config/` holds only `.gitkeep`. No code reads a file there. `git grep "recomp/config"` matches only `ROADMAP.md` and a record.

The recompile settings come from `engine/src/nx/runtime/settings.txt`. `snx_recompile_setting` looks in `g_recompile_ini`, `g_llvm_ini` and `g_recompile_game_ini`. The recompile keys are:

| Key | Default | Meaning |
| --- | --- | --- |
| `opt` | `2` | The optimization level, one of `0`, `1`, `2`, `3`, `s` or `z`. |
| `aot_threads` | `0` | Compile jobs. 0 uses the host CPU count. |
| `unit_instructions` | `90000` | Instructions in one unit. |
| `unit_cache_gb` | `0` | The cache limit. 0 means no limit. |

`g_llvm_ini` gets every pass of the build's LLVM pipelines and each LLVM option that the C API sets.

## Current state

- The recompiler compiles A64 code and the 32-bit ARM blocks. The 64-bit path uses `snx_build_module_llvm` and the 32-bit path uses `snx_build_module_arm`.
- There is no interpreter. A pc that the AOT module and the JIT both miss ends the game. A miss is a fatal, not a fallback to a slower path. For the history, see [jit-analysis.md](../../records/analysis/jit-analysis.md).
- The AOT variant table is built only with `--jit-cache`, so a normal build depends on the JIT for a pc outside the pack.
- The `.hints` file collects the misses of each run and is read at the next recompile. The jump table scan writes the same format, and `--merge` keeps the recorded misses.
- `recomp/config/` is empty and unused. The settings of a game are the generated `settings.txt` tables, the `--KEY=VALUE` arguments and the game's `.ini`.
- A patch applies only at a recompile. It does not change a game that is already built.
