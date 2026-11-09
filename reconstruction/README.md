<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

<!-- Copyright 2026 the NXAevora contributors -->

# Source reconstruction

This directory recovers the game's code as readable C, one function at a time. It sits beside the static
recompilation and does not change it. The recompiled game keeps running as it does. A reconstructed function is a
second, native implementation of a game function, and it only counts once it is proven to behave like the original.

## How a function is verified

`harness/verify.c` (built as `recon-verify`) checks each reconstruction against the game's own code:

1. It maps the main module dump (`main.bin`) at the address the runtime uses (`0x8005000`) and applies the module's
   relative relocations, so data and pointers hold the values the game sees.
2. It compiles the original function from those bytes with ScarletNX's LLVM backend, the code generator the
   recompiled game runs on.
3. A fixture builds the inputs: the registers and the objects in scratch memory (for the Lua API, `lua_State`s with
   a `CallInfo`, a stack of typed values, a `global_State`, a C closure and a pool of tables, userdata, closures and
   threads the values point to). Every other byte and register is random.
4. The original and the reconstruction run from the same state. They must agree on the result register, X19 to X29,
   SP, the return address and every byte of the 1 MB scratch area except the 32 KB just below SP, where a function
   keeps its own frame (dead once it returns). The module must be unchanged at the end.

### Functions that call others

The compiled original reaches its callees through `snx_lookup`, as the game's code does, and the harness answers
those calls (`RtAPI.miss`, since the dispatch table is empty). A reconstruction calls through the same path
(`include/recon/call.h`), with the original's frame size so the pointers it passes match. Each call is answered in one
of three ways:

- **Compiled.** By default, every main function the original reaches by a direct branch is compiled from the dump
  too, transitively, with sizes from the function inventory (`--functions`). Both sides run the game's own callee.
- **Imported.** A branch to one of main's PLT stubs is named from the module's jump slot relocations and runs a model
  of that C library function on guest memory (`harness/imports.c`: strlen, strtod, memcpy and the like). The model is
  the same for both sides and fails the run on unmapped memory instead of crashing.
- **Stubbed.** A case may replace a callee whose inputs the fixtures do not build, such as the collector or a table's
  hash part (`harness/callees_lua53.c`). A stub records the arguments it is given and returns a value drawn for the
  run. Its optional effect leaves what the caller relies on, such as `luaO_tostring`'s string. The original and the
  reconstruction must make the same stubbed calls, in the same order, with the same arguments.

A call to anything else (an indirect call to an unknown address, say) fails the run and names the target. The
results record each case's callee count and a hash over the callees' bytes.

A function is **verified** when it passes at least 10000 randomized runs on the exact bytes recorded in the manifest.
`recon-verify --control` runs every original against a deliberately wrong variant of its reconstruction
(`harness/mutants_lua53.c`), and every one must fail. That shows the fixtures and the comparison catch such mistakes.

Verification is behavioural, not byte matching: it shows the reconstruction does what the original does for the
inputs the fixtures cover. It does not show that a compiler would produce the original's instructions.

## Layout

| Path | Contents |
| --- | --- |
| `include/recon/guest.h` | Guest memory accessors used by every reconstruction |
| `include/recon/registry.h` | The reconstructions as runtime `GuestFn`s, with the original each one replaces |
| `include/recon/call.h` | Guest calls and stack frames, for reconstructions that call other game functions |
| `src/lua53/` | The game's Lua 5.3 VM: `lua53_layout.h` (the structures as the game lays them out), `lapi.c` |
| `harness/` | The verifier, its fixtures, the control mutants, the C library models and the callee stubs |
| `manifest/functions.csv` | Every reconstructed function: offset, size, a hash of the original's bytes, status |
| `tools/build_harness.py` | Builds `recon-verify` from a ScarletNX build with its tests configured |
| `tools/progress.py` | Progress against the function inventory, checking each claim |
| `tools/candidates.py` | The functions the harness can verify as they are, ranked by callers |

Game data stays local. `main.bin`, results that quote game bytes and builds are never committed.

## Running it

A ScarletNX build with `SNX_BUILD_TESTING=ON`, configured from `third_party/ScarletNX`, with the target
`snx_codegen_test` built, provides the backend and its link line. Then:

```
python tools/build_harness.py --build SCARLETNX_BUILD --ninja NINJA --out BUILD_DIR
BUILD_DIR/recon-verify MAIN.BIN --functions functions.tsv --runs 20000 --results results.csv
BUILD_DIR/recon-verify MAIN.BIN --functions functions.tsv --runs 2000 --control
python tools/progress.py --functions functions.tsv --main MAIN.BIN --results results.csv
```

`MAIN.BIN` is ScarletNX's dump of the main module (`scarletnx --dump-modules`, cached under `module-dumps/`).
`functions.tsv` is the function inventory (address and size per line). Without `--functions`, the cases whose
originals call other functions report that they need it.

## Status, 2026-11-09

| | Count | Bytes |
| --- | --- | --- |
| Functions in the inventory (all modules) | 227067 | 72851324 |
| Functions in main | 161208 | 57424060 |
| Verified reconstructions | 30 | 4872 |

All 30 are the game's Lua 5.3 C API (`lapi.c`), identified from their layouts and constants and confirmed by the
"Lua 5.3" string and `lua_version`'s 503.0. Each passed 20000 runs, and all 30 mutants failed the control. Among
them are `lua_touserdata` (11744 direct callers), `lua_settop` (11108), `lua_pushnil` (8031) and `lua_type` (3864).
Every leaf function of `lapi.c` (main+4E3B0 to main+53000) is reconstructed. Five that call into the VM are too:
`lua_isnumber`, `lua_tonumberx` and `lua_tointegerx` run the game's own `luaV_tonumber_` or `luaV_tointeger` and
`luaO_str2num`, with fixture strings that convert. `lua_rawlen` and `lua_tolstring` stub `luaH_getn`, `luaO_tostring`
and `luaC_step`.

## Adding a function

1. Pick one from `tools/candidates.py` (`--max-size` raises its 200-byte default), or a function whose library is
   known.
2. Write it in `src/<library>/` against `guest.h`, with field offsets as named constants.
3. Add its `GuestFn` adapter and registry entry, a fixture that builds valid inputs, and a mutant. A function that
   calls others keeps the entry X30, since its calls set it. Stub a callee in `harness/callees_lua53.c` only when the
   fixtures cannot give it valid inputs.
4. Run the verifier and the control, then add the manifest line with the hash the verifier prints.
5. `tools/progress.py` must report no problem.

## Not done yet

- The rest of `lapi.c` and then `lauxlib.c`. Most of what is left reaches the collector or the table code, so it
  needs fixtures with real tables and strings, or stubs.
- Indirect calls. A call through a function pointer works only when it lands on a compiled callee or an import.
- Use in the game. The registry entries are already `GuestFn`s, but the recompiled code calls most functions
  directly, not through the dispatch table. Using a reconstruction in game needs the runtime to reroute those direct
  calls, and a test in the game.
- Structure recovery beyond what each function needs, and naming beyond libraries with public sources.
- Byte matching. It would need Nintendo's compiler at the version and settings the game was built with.
