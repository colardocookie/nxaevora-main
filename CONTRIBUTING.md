<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the NXAevora contributors -->
# Contributing

Thank you for your interest. This file tells what a change must follow. For the state of the project, see
[ROADMAP.md](ROADMAP.md).

## Game data stays out

Nothing of the game goes into git, an issue, a pull request or a comment. This includes keys, dumps (NSP, XCI, NCA),
extracted files, the code that the recompiler makes, saves, logs that hold game data, and shader or texture dumps.

You can quote addresses, symbol names and short disassembly in commit messages and docs. You cannot attach extracted
files. The folder `game/` is in `.gitignore` for this reason. Keep your dumps and your build output there or outside
the repository.

## License

NXAevora is GPL-3.0-or-later. A new file starts with this line, in the comment syntax of its language, and the
copyright line of the project:

```
SPDX-License-Identifier: GPL-3.0-or-later
Copyright 2026 the NXAevora contributors
```

By contributing, you agree that your work is released under this license.

### Code from other projects

You can port code that is under GPL-3.0, GPL-2.0-or-later (for example yuzu, suyu, sudachi, Citron and Eden), LGPL,
MIT, BSD, ISC, zlib or Apache 2.0. Keep its copyright lines, say where it came from, and list it in the credits.

You cannot port code that is GPL-2.0-only. Atmosphere is GPL-2.0-only. You can read it to learn how Horizon behaves,
but do not port it. The main references are Switchbrew and the behavior of the SDK, which you can read from the
disassembly of the game.

## Changes to ScarletNX

`third_party/ScarletNX` is a copy of the upstream project (commit 8efbf9c4), kept in this repository. Edit it like
any other source. It does not follow the upstream project, so a fix that helps every game can also be sent upstream.
The changes of NXAevora are listed in `docs/build/runtime-changes.md`.

## Code

- Give each function one job. Extract a helper when it makes the flow clear or when the rule is reusable. Do not
  build speculative frameworks.
- Write ownership, lifetime, error handling and fallback behavior explicitly. On an error, keep the context, so that
  you can find the stage that failed.
- Use a named constant for each threshold or setting that has a meaning. Document why a workaround exists and when it
  applies.
- Prefer early checks and simple control flow. Avoid deeply nested branches, unrelated refactors and wide format
  changes.
- Comments tell the intent, the assumptions and the limits. A comment has 4 lines at most. Do not comment what the
  code, a name or a type already says.
- Code identifiers, comments and project documents are in English.

## Find the mechanism

A fix explains why the game does what it does. It tells what the game or SDK code expects, and what the runtime gave
it instead. Do not tune delays, add retries or add special cases to hide a problem.

## Testing

A build is not a test. To verify a change to the runtime, run the game through the scene that the change affects.
The pull request tells what you ran. If a change fixes a crash, run the game many times. One good run does not prove
the fix of an intermittent crash.

### Automatic checks

Each pull request to `main`, each push to `main` and each manual run starts the workflow `CI`
(`.github/workflows/ci.yml`). Its check `Windows build and test` builds `third_party/ScarletNX` in MSYS2 CLANG64 with
the steps of `docs/build/SCARLETNX_BUILD-NOTES.md`, starts `scarletnx.exe --help` and runs `ctest`. It uses no game
data and no secrets, so it also runs for a pull request from a fork. The result is on the Checks tab of the pull
request. When a test fails, its output is in the log of the step `Run ctest`, and the file `LastTest.log` is
attached to the run.

The check does not run the game and has no GPU. It does not show that a scene works. The test `tracked_material` is
left out because it fails on two SMAA `.dds` files under `src/nx/gpu/vk/shaders/smaa/`. Put it back when that is
fixed. `qemu_ref_selftest` runs only on a Linux build with Clang, `ld.lld` and `qemu-aarch64`, and `gdb_jit` does not
run on Windows, so both skip on the runner and on a local Windows build. The runner has no Vulkan driver, so
`3ds_vk_transfer`, `3ds_vk_y2r`, `3ds_vk_draw` and `3ds_gsp_gpu` skip there too. The list of skipped tests is at the
end of the step `Run ctest`.

## Commits and documents

- A commit has a short subject in the imperative. The body says why, and how you verified the change.
- Commit messages, the README, the roadmap and other documents for users are plain prose. They have no em dash
  between clauses, no semicolon between sentences and no spaced hyphen used as a dash.

## Pull requests

Keep a pull request small and about one subject. Describe what it changes, why, and what you ran to check it.
Do not include game data (see above).
