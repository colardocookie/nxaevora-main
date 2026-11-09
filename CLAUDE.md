# NXAevora

NXAevora is a native port of Pokemon Legends Arceus (Nintendo Switch) for the PC. It uses static recompilation.
The recompiler is ScarletNX (`third_party/ScarletNX`, vendored). NXAevora adds the game-specific
configuration, patches and runtime parts. It also has a launcher that is made with the Tauri framework.

This file holds the rules that sessions get wrong, and the steps to build, recompile and read a run.

## Rules

- **Claude files and agent messages use ASD-STE100.** CLAUDE.md, the Markdown files in `.claude/` and the memory
  files use Simplified Technical English. The prompts to subagents and the final reports of subagents also use it.
- **Nothing of the game goes into git.** Keys, dumps (NSP, XCI, NCA), code, data, saves, logs, and shader or
  texture dumps stay local. Commit messages and docs can quote addresses, symbol names and short disassembly, but
  never extracted files.
- **The user runs and tests the game.** Do not start the game, and do not run it with scripted input. Build,
  recompile and analyze only. When a change needs a game run, tell the user which scene to run. The user does not
  read logs. After the run, find the logs and files yourself (the game folder, `<id>.log`, `<id>.hints`), read them
  and report the result. Ask the user only for what you cannot read yourself, for example what the screen showed.
- **A build is not a test.** To verify a runtime change, the user runs the game through the scene that the change
  affects. The commit tells what the user ran. If a change fixes a crash, the user runs the game many times to show
  that the game does not crash again. One good run does not prove the fix of an intermittent crash.
- **Find the mechanism.** A fix explains why the game does what it does. It tells what the game or SDK code
  expects, and what the runtime gave it instead. Do not tune delays, add retries or add special cases.
- **Licenses.** NXAevora is GPL-3.0-or-later. A new file starts with the line
  `SPDX-License-Identifier: GPL-3.0-or-later` and the copyright line of the project.

  You can port code under GPL-3.0, GPL-2.0-or-later (yuzu, suyu, sudachi, Citron, Eden), LGPL, MIT, BSD, ISC, zlib
  or Apache 2.0. Keep its copyright lines. Name where it came from (Ryujinx is MIT). List it in the credits.
  You cannot port GPL-2.0-only code.

  The primary references are Switchbrew and the behavior of the SDK, which you read from the disassembly.
  Atmosphere (the kernel of Mesosphere, the services of Stratosphere) is the reference for how Horizon behaves. It
  is GPL-2.0-only, so read it, but never port it.
- Ask the user only about real decisions. When the next step is obvious, do it.

### Code

- Give each function one job. Extract a helper when it makes the flow clear or when the rule is reusable. Do not
  build speculative frameworks.
- Write ownership, lifetime, error handling and fallback behavior explicitly. On an error, keep the context, so
  that you can find the stage that failed.
- Use a named constant for each threshold or setting that has a meaning. Document why a workaround exists and when
  it applies.
- Prefer early checks and simple control flow. Do not use deeply nested branches, unrelated refactors or wide
  format changes.
- Comments tell the intent, the assumptions and the limits. Write code identifiers, comments and project
  documents in English.
- Keep a code comment to 4 lines at most. Do not comment what the code, a name or a type already says. Delete
  such a comment when you find it or fix the code near it. Put longer explanations in the docs.

## Environment

- Platform: Windows 11 x64.
- ScarletNX builds in the MSYS2 CLANG64 shell. See `docs/SCARLETNX_BUILD-NOTES.md`.
- Put temporary files in the session scratchpad, never in the repository root.

## Build and run

- Build ScarletNX: `docs/SCARLETNX_BUILD-NOTES.md`.
- Recompile the game: the skill `nx-recompile`. Before the recompile of the update, make the hints with
  `tools/jump_table_scan.py --hints FILE --merge`. The recompile takes about 11 minutes with these hints.
- The user starts the update program from `game/recompiled/<id>/`. Read the result with the skill `nx-log-read`.
- The launcher is not written yet.

## Folder structure

```
project-main/
  .gitignore
  .gitmodules
  README.md                 overview, status, build, legal notes
  CONTRIBUTING.md           rules for changes
  LICENSE                   GPL-3.0 (the text of the license)
  ROADMAP.md                phases and exit conditions
  CLAUDE.md                 rules for Claude sessions
  .claude/                  work plan, skills and session settings
  assets/                   logo for the README and the launcher (aevora-logo.png, aevora-logo-dark.png)
  third_party/ScarletNX/    vendored, the recompiler and the Switch runtime
  recomp/
    config/                 game-specific recompile settings
    patches/                patches for instructions and functions
  runtime/
    hle/                    HOS services that ScarletNX does not have
    gfx/                    graphics bridge and shader translation
  launcher/                 Tauri launcher
  tools/                    helper scripts (classify_hints.py, jump_table_scan.py)
  docs/                     notes (SCARLETNX_BUILD-NOTES.md, jit-analysis.md, stutter-analysis.md)
  .github/workflows/        CI
  game/                     not in git: dumps, generated code, build output
```

## Work plan

At the start of a session, read `.claude/WORK_PLAN.md` (the tasks to do now). Read `.claude/OLD-WORK_PLAN.md` (the
facts and the finished work) only when you need a fact: it is long and each read makes every later call more costly.
When a task is finished, move it from `WORK_PLAN.md` to `OLD-WORK_PLAN.md`. Both files are in git. Use one session
for one task.

## Skills and agents

The skills are in `.claude/skills/`.

- `nx-recompile`: recompile the base game and the update with the portable ScarletNX, and check the result.
- `nx-log-read`: find and read the log and result files of a run that the user made.
- `nx-jit-triage`: find why the recompiled game reached code that the AOT module lacks.

The upstream skills of ScarletNX are not in the copy. They were written for Linux and for runs by the agent.

## Git

- `third_party/ScarletNX` is part of this repository (vendored from upstream commit 8efbf9c4, no submodule, no
  patch files). Edit it like other source. The changes are listed in `docs/runtime-changes.md`. The upstream
  `CLAUDE.md`, `.claude/`, `.github/` and `.mcp.json` are not in the copy.
- Push only when the user asks. Never force-push a pushed branch.
- Stage files by name. Check the staged diff before a commit.
- Do not commit files of the game, keys or dumps. `game/` is in `.gitignore`.
- Write the body of a commit message short and easy to understand. Say what changed and why, in a few lines. Do not list
  every detail. The details belong in the docs.
- Each commit message ends with the line `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. When the
  agent of the skill `orca-agents` did work for the change (a review or a research that the change uses), add the
  line of each model that did work below it: `Co-Authored-By: GPT-6.1-Sol (Codex) <noreply@openai.com>` for the code
  reviewer, and `Co-Authored-By: OpenCode (DeepSeek V4.1 Flash) <noreply@opencode.ai>` for the game explorer, the
  library analyst, the handler writer and the code writer. If both models did work, add both lines. If no such agent did work, do not add a line.
  Name the work of the agent in one line of the message body.

## Text for users

README, ROADMAP, commit messages, and the messages that the runtime prints for players are plain prose. They have
no em dash between clauses, no semicolon between sentences, and no spaced hyphen as a dash. They have no
rule-of-three flourishes. This rule does not apply to code comments. The Claude files follow ASD-STE100 (the first
rule above).
