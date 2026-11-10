<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

<!-- Copyright 2026 the NXAevora contributors -->

# Arceus 120 FPS timing work

Everything around the Pokémon Legends: Arceus frame-rate work that is not runtime code. The runtime code is
`runtime/gfx/world-pt/overlay/src/nx/runtime/arceus_clock.c` in this branch (`experiment/arceus-elapsed-gravity`).
Every fix there is opt-in through an `SNX_ARCEUS_*` environment variable and needs the 120 FPS elapsed-time clocks.

Start with `notes/HANDOFF-2026-11-09.md`, then the last sections and "Next steps" of
`notes/ARCEUS-TIMING-HANDOFF.md`, which hold the current state, the open issues and the standing test rules.

| Path | Contents |
| --- | --- |
| `notes/` | Handoffs, the research passes (engine, physics, AI, dialogue, timers), audits and the controller launch policy |
| `tools/` | Log reports (`gravity_report.py`, `fps_runs.py`, `cadence.py` and the like), static helpers that read the main module dump, the build scripts and `pin_launcher_hash.py` |
| `launchers/` | Sources of the native controller launchers (`physics-controller.c` is the main 120 FPS test, `baseline30-controller.c` the 30 FPS baseline, `physics-nomove-controller.c` the one without the movement fix), the older PowerShell capture launcher and its `.cmd` presets |

The launchers pin the SHA256 of the staged runtime and refuse to start another build. After a rebuild, run
`tools/pin_launcher_hash.py HASH physics-controller.c baseline30-controller.c physics-nomove-controller.c` in the
launcher folder, then rebuild each one with
`x86_64-w64-mingw32-clang -O2 -municode -mwindows -Wall -o X.exe X.c -lbcrypt` and check it with `--verify-only`.

Paths in the scripts and notes are the Windows machine the work was done on. Game data, saves, module dumps,
disassembly, run logs and builds are not here and stay local.
