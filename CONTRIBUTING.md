<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the NXAevora contributors -->
# Contributing

Thank you for your interest. This file tells what a change must follow.

## Before you start

For the state of the project, see [ROADMAP.md](ROADMAP.md). For a bug or a suggestion, open an issue. Keep one pull
request for one subject.

## Project layout

| Folder | Contents |
| --- | --- |
| `engine/` | The vendored recompiler and Switch runtime, from upstream ScarletNX. |
| `recomp/` | The game recompile settings and patches. |
| `tools/` | Helper scripts. |
| `docs/` | The guides for contributors. |
| `docs/dev/` | The specifications. |
| `docs/records/` | The records of investigations, plans and reviews, as history. |
| `launcher/` | The Tauri launcher. |

`engine/docs/` comes from upstream ScarletNX. The NXAevora documents are in `docs/`, `README.md` and `ROADMAP.md`.

## Game data stays out

Nothing of the game goes into git, an issue, a pull request or a comment. This includes keys, dumps (NSP, XCI, NCA),
extracted files, the code that the recompiler makes, saves, logs that hold game data, and shader or texture dumps.

You can quote addresses, symbol names and short disassembly in commit messages and docs. You cannot attach extracted
files. The folder `game/` is in `.gitignore` for this reason. Keep your dumps and your build output there or outside
the repository.

## Licenses

NXAevora is GPL-3.0-or-later. A new file starts with this line, in the comment syntax of its language, and the
copyright line of the project:

```
SPDX-License-Identifier: GPL-3.0-or-later
Copyright 2026 the NXAevora contributors
```

By contributing, you agree that your work is released under this license.

You can port code that is under GPL-3.0, GPL-2.0-or-later (for example yuzu, suyu, sudachi, Citron and Eden), LGPL,
MIT, BSD, ISC, zlib or Apache 2.0. Keep its copyright lines, say where it came from, and list it in the credits.

You cannot port code that is GPL-2.0-only. Atmosphere is GPL-2.0-only. You can read it to learn how Horizon behaves,
but do not port it. The main references are Switchbrew and the behavior of the SDK, which you can read from the
disassembly of the game.

## Changing the engine

`engine` is a copy of the upstream project (commit 8efbf9c4), kept in this repository. Edit it like any other source.
It does not follow the upstream project, so a fix that helps every game can also be sent upstream. List each change in
`docs/build/runtime-changes.md`, and update the matching specification in `docs/dev/engine/`.

A fix explains why the game does what it does. It tells what the game or SDK code expects, and what the runtime gave
it instead. Do not tune delays, add retries or add special cases to hide a problem.

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

## Testing

A build is not a test. To verify a change to the runtime, run the game through the scene that the change affects. The
pull request tells what you ran. If a change fixes a crash, run the game many times. One good run does not prove the
fix of an intermittent crash.

Each pull request to `main`, each push to `main` and each manual run starts the workflow `CI`
(`.github/workflows/ci.yml`). Its check `Windows build and test` builds `engine` in MSYS2 CLANG64 with the steps of
`docs/build/ENGINE_BUILD-NOTES.md`, starts `nxaevora-engine.exe --help` and runs `ctest`. It uses no game data and no
secrets, so it also runs for a pull request from a fork. The result is on the Checks tab of the pull request. When a
test fails, its output is in the log of the step `Run ctest`, and the file `LastTest.log` is attached to the run.

The check does not run the game and has no GPU. It does not show that a scene works. `qemu_ref_selftest` runs only on a
Linux build with Clang, `ld.lld` and `qemu-aarch64`, and `gdb_jit` does not run on Windows, so both skip on the runner
and on a local Windows build. The runner has no Vulkan driver, so `3ds_vk_transfer`, `3ds_vk_y2r`, `3ds_vk_draw` and
`3ds_gsp_gpu` skip there too. The list of skipped tests is at the end of the step `Run ctest`.

## Commits

A commit has a short subject in the imperative. The body says why, and how you verified the change. Commit messages,
the README, the roadmap and other documents for users are plain prose. They have no em dash between clauses, no
semicolon between sentences and no spaced hyphen used as a dash.

A commit title starts with one tag that tells the area of the change. Use the tag of the main change, so code with its
tests and docs takes the tag of the code:

- `[Docs]`: docs, `README.md`, `ROADMAP.md` only.
- `[Graphics]`: NVN, GPU, renderer, shaders.
- `[FS]`: file system, RomFS, saves.
- `[Input]`: input and camera.
- `[Runtime]`: other HLE services, kernel, memory, threads.
- `[Recomp]`: recompiler, recompile driver, `recomp/`.
- `[Game]`: fixes for the game only (Arceus timing layer, patches).
- `[UI]`: in-game UI.
- `[Launcher]`: launcher.
- `[Tests]`: tests only.
- `[Build]`: CMake, CI, build files.
- `[Tools]`: `tools/`.

Each commit message ends with a `Co-Authored-By` line for each AI tool that did work for the change, and names that
work in one line of the body. No such work, no line.

## Pull requests

Keep a pull request small and about one subject. Describe what it changes, why, and what you ran to check it. Do not
include game data (see above).

A pull request body follows `.github/pull_request_template.md`. Keep all sections and fill each field. In "AI tools",
keep only the line that applies and name each tool and model. The branch must be up to date with `main`, and all
conversations must be resolved. CodeRabbit checks the description.

## AI tools

AI is the primary means of development. Human maintainers determine the project's development direction, verify
functionality, review and approve changes, and manage the project. Name each AI tool and model that you use, in the
commit message and in the pull request.
