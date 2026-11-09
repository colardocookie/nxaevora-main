<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the NXAevora contributors -->
<!-- Read CONTRIBUTING.md before you open a pull request. Keep it small and about one subject. -->

## What and why

<!-- What this changes. For a fix, tell what the game or SDK code expects and what the runtime gave it instead. -->

## What you ran

<!-- A build is not a test. Name the scene that you ran, the number of runs, and the log lines that you checked.
     For a crash fix, run the game many times and give the count. For a change without a runtime effect, say so. -->

- Game version:
- Scene:
- Runs:
- Tests (`ctest`):

## AI tools

<!-- Required. Keep one line and delete the other. If you used AI tools, name each tool and model. -->

- [ ] I did not use AI tools for this change.
- [ ] I used AI tools for this change. Tools and models:

## Checklist

- [ ] This pull request has no keys, dumps, extracted game files, saves, logs with game data, or shader and texture dumps.
- [ ] Each new file starts with the `SPDX-License-Identifier: GPL-3.0-or-later` line and the copyright line of the project.
- [ ] Ported code keeps its copyright lines, names its source and is listed in the credits. It is not GPL-2.0-only (for example Atmosphere).
- [ ] Changes to `third_party/ScarletNX` are listed in `docs/runtime-changes.md`.
- [ ] New or changed behavior has tests where it can be tested.
