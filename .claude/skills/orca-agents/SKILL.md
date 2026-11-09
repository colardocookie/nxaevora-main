---
name: orca-agents
description: Start a second AI agent in Orca for a review or a research task. Five agents are defined, "code reviewer" (review of generated code), "game explorer" (what a game or SDK function does and what it expects from the runtime), "library analyst" (a table of the fields and the results of many NVN library functions), "handler writer" (the host handlers and tests for such a table, as a patch) and "code writer" (host code of one task of a written design, as a patch). Use it when the user asks for one of these agents, or asks for a second opinion on a change. The agent has no heartbeat. The caller waits until the agent is done. At most 2 agents run at the same time.
---

# Orca agents

Use the `orca` CLI (skill `orca-cli`). Do not use `orca orchestration`: it needs heartbeats and `worker_done`, and
these agents do not send them.

## Agents

| Name | Agent id | Model | Effort | Job |
|---|---|---|---|---|
| code reviewer | `codex` | `gpt-6.1-sol` | `high` | Review the code that was generated. Report the faults. |
| game explorer | `opencode` | `opencode-go/deepseek-v4.1-flash` | none | Find what a function of the game or of the SDK does, and what it expects from the runtime. |
| library analyst | `opencode` | `opencode-go/deepseek-v4.1-flash` | none | Read the disassembly of 10 to 15 NVN library functions and write one table row for each. A wrong row goes into a handler, so check at least 3 rows. |
| handler writer | `opencode` | `opencode-go/deepseek-v4.1-flash` | none | Write the host handlers and the unit tests for the rows of an accepted table. Give the work as a patch file. |
| code writer | `opencode` | `opencode-go/deepseek-v4.1-flash` | none | Write the host code and the host tests of one task of a written design (for example the replay or the frame job of 3c). Give the work as a patch file. |

Do not pass `--effort` to the `opencode` agents: Orca refuses `low`, `high` and `max` for this model (checked 2026-11-03).
The agent runs with the default variant of the model, which is `high` (shown in its screen). The id `opencode` and the model
id are accepted.

## Rules

- **Maximum 2 agents at the same time.** Before a start, count the running agents: `orca worktree ps --json`. If 2
  agents run, wait for one of them (see "Wait"). Do not start a third.
  The list also shows the session of the caller (agent type `claude`) and finished sessions. Count only the entries
  with agent type `codex` or `opencode` and state `working`.
- **No heartbeat.** The agent does not send messages. Do not poll its state on a timer, and do not ask it for status.
- **Wait until it is done.** After the start, wait with one command (see "Wait"). Do not do other work that depends on
  the result before the command returns.
- **Read-only.** The agent does not edit files in the repository, does not commit, does not push and does not start
  the game. It writes its report to one file in the scratchpad (outside the repository). The handler writer and the code writer have one
  exception: they work in a copy of the files (see "Brief: handler writer" and "Brief: code writer") and give a patch
  file, never an edit of the repository.
- **Nothing of the game goes into git.** The agent must not copy keys, dumps or extracted game files into the
  report. It can quote addresses, symbol names and short disassembly.
- The brief for the agent uses ASD-STE100 (project rule). Answer the user in Japanese.
- **Short reports.** Put this rule at the top of each brief, before the task: the report is short and exact, with no
  introduction, no summary, no praise and no repeated text. Each finding or item has at most 5 lines. Report only what
  the agent verified in the code or the disassembly. Write "no findings" when there are none. This saves the tokens
  that the caller spends to read the report.

## Start

Work in the current worktree (`agent launch` uses the worktree of the current folder). Choose a report path in the
scratchpad, for example `<scratchpad>/reports/<name>-<n>.md`.

```text
orca agent launch --agent codex --model gpt-6.1-sol --effort high --surface terminal --prompt "<brief>" --json
orca agent launch --agent opencode --model opencode-go/deepseek-v4.1-flash --surface terminal --prompt "<brief>" --json
orca agent launch --agent opencode --model opencode-go/deepseek-v4.1-flash --surface terminal --prompt "<brief>" --json
```

The first line is the code reviewer. The second and third lines are for the game explorer, the library analyst, the
handler writer and the code writer, with their briefs. A long brief goes in a file: the prompt says "Read the file <path> and do the task in it." Use `--surface terminal`, so that
`terminal wait` works. Keep `outcome.handle` from the result: it is the only handle for this agent. Check
`prompt.outcome` (`journaled` or `handed-to-terminal`). If the reply is lost, repeat the same command with
`--retry-launch <operationId>`. Never start a second agent for the same brief.

## Wait

```text
orca terminal wait --terminal <handle> --for tui-idle --timeout-ms 1800000 --json
```

`tui-idle` means that the agent has finished its turn. The wait can return before the agent starts. Check that the report file exists before you close the terminal. Use a long timeout (the code reviewer at high effort can take
many minutes). If the wait times out, run the same wait again. Do not send text to the agent while it works.

When it returns, read the report file. If the file is missing, read the screen with
`orca terminal read --terminal <handle> --screen --json`. Then close the terminal:
`orca terminal close --terminal <handle> --json`. A closed terminal frees one of the 2 places.

## Brief: code reviewer

The brief must contain these items:

1. The goal of the change, in 1 to 3 sentences, and the files or the commit range to review
   (for example `git diff HEAD~1` or the names of the files).
2. The project rules to check: `CLAUDE.md` section "Code" (one job for each function, named constants, errors keep
   their context, comments of 4 lines at most, English), the license line in a new file, and that no game file is staged.
3. The mechanism rule: a fix must explain why the game does what it does. Report a delay, a retry or a special case
   that has no cause.
4. The task: find faults in correctness (wrong behavior, crash, race, leak, wrong error handling), in the rules, and
   in the missing tests or scenes for the user to run. Do not report style that the rules do not cover.
5. The format of the report: one entry for each finding with the file and line, the fault, a concrete input or state
   that causes it, and a suggested fix. Put the most severe first. Write "no findings" if there are none.
6. The limits: read-only, do not edit, do not start the game, write the report to `<report path>`.

## Brief: game explorer

The brief must contain these items:

1. The question: the guest addresses or the symbol names of the functions, and which module (main, sdk, or the
   other modules of the extracted game in `game/work/Arceus`).
2. The tools to use: `tools/disasm.py`, `tools/find_callers.py`, `tools/maxwell_label.py` and the notes in `docs/`
   (see "How to work" in `.claude/WORK_PLAN.md`). Check the layout of the program before the analysis.
3. The task: tell what each function does (inputs, outputs, state it changes), who calls it, and what it expects from
   the runtime (a service reply, a kernel call, a memory layout, a timing). Separate what the disassembly shows from
   what is a guess.
4. The references: Switchbrew and the disassembly. Atmosphere is GPL-2.0-only: it can be read as a reference of
   Horizon behavior, but no code is copied from it.
5. The format of the report: for each function, a short summary, the evidence (addresses, short disassembly), the
   expectation of the runtime, and the open questions.
6. The limits: read-only, do not edit, do not start the game, do not copy extracted game files into the report, write
   the report to `<report path>`.

## Brief: library analyst

Use it for the NVN functions that a part of stage 3 (for example 3b-4) must implement. Give 10 to 15 functions in one
brief. The brief must contain these items:

1. The function names, and the module of the NVN library (`main` or `sdk`). The agent finds the addresses with the
   resolved addresses in the log (`nvn: <name> -> <address>`) or with `tools/symbols.py`.
2. The tools and notes: `tools/disasm.py`, `tools/find_callers.py`, and `docs/nvn-design.md` (sections 2-6 and "3a
   Result"). The agent checks the layout of the program before the analysis.
3. The task: for each function, read the library code and tell which words of the object it reads and writes (offset and
   size), what it does with the other arguments, what it returns, which state outside the object it changes (a lock, a
   counter, a call to a kernel or service function), and what it does when an argument is zero or out of range.
4. The format of the report: one table row for each function with the columns name, address, reads, writes, result, side
   effects, checks. Below the table, the open questions, at most 3 lines for each. Separate what the disassembly shows
   from a guess. Do not write prose outside the table and the questions.
5. The limits: read-only, do not edit, do not start the game, do not copy extracted game files into the report (short
   disassembly and addresses are allowed), write the report to `<report path>`.

## Brief: handler writer

Use it only after the table of the library analyst is accepted. The agent writes code, so it must not touch the
repository. The brief must contain these items:

1. The accepted table (the path of its file) and the names of the functions to implement.
2. The place to work: a copy of the files `gpu/nvn_native.c`, `gpu/nvn_params.c`, `gpu/nvn_params.h` and the test
   `tests/nvn_params.c` in the scratchpad, or a new worktree that the caller makes. The agent does not edit the files of
   the working folder.
3. The rules: `CLAUDE.md` section "Code", the pattern of the handlers in `gpu/nvn_native.c` (the lock, the record in the
   side table, the entry in `native_procs`), no change of `c->pc` or x30 in a handler (`native_call` does it), a named
   constant for each value from the table, and a pure function in `gpu/nvn_params.c` with a test for each part that has no
   side effect. A handler follows its row. If a row is unclear, the agent does not guess: it writes the question in the
   report and leaves the handler out.
4. The task: write the handlers, the table entries and the tests. Build with the commands of `.claude/WORK_PLAN.md`
   ("How to work") only if the caller allows it in the brief. Otherwise the agent only writes the patch.
5. The result: one patch file `<scratchpad>/patches/<name>-<n>.patch` that `git apply` accepts in the working folder,
   and a report of at most 15 lines: the functions done, the functions left out and why, and the tests added.
6. The limits: do not edit the repository, do not commit, do not push, do not start the game, do not copy extracted game
   files into the patch or the report.

## Brief: code writer

Use it for one task of a design that is written and decided (for example one step of the 3c work order in
`docs/nvn-design.md`). The agent writes code, so it must not touch the repository. Give one task for each brief. A task
is one module or one handler group with its tests. The brief must contain these items:

1. The task in 1 to 3 sentences, and the design text to follow (the file and the numbers of the decisions). The caller
   writes the decisions that the design leaves open, for example the lock order, the thread that runs the code, who owns
   each object and which function ends its life. The agent does not decide them.
2. The place to work: a detached worktree that the caller makes in the scratchpad (`git worktree add --detach`). The
   agent does not edit the files of the working folder. The caller names the files that the agent can change and the
   files that it must not change. If the task needs another file, the agent does not change it: it writes the reason in
   the report.
3. The rules: `CLAUDE.md` section "Code" (one job for each function, named constants, errors keep their context,
   comments of 4 lines at most, English), the SPDX line and the copyright line in a new file, the line ends of the file
   (many files are CRLF: keep them), and the rule of the user: the host owns the objects, no compatibility word goes into
   the memory of the game, and an unknown part stops with a log line. No faked success. No delay, no retry and no special
   case that has no cause.
4. The existing code to use: the names of the functions, the types and the handlers that the new code calls or follows
   (with the file and the line). The agent reads them before it writes. It does not invent a function that does not exist:
   it searches for it, and if it is missing it writes the question in the report.
5. The tests: a host test for each part that has no side effect, in the style of the test files of the project. The
   agent builds and runs `ctest` only if the caller allows it in the brief, with the commands of `.claude/WORK_PLAN.md`
   ("How to work"; the MSYS2 CLANG64 shell only). Otherwise it only writes the patch and says that nothing was built.
6. The result: one patch file `<scratchpad>/patches/<name>-<n>.patch` that `git apply` accepts in the working folder,
   and a report of at most 20 lines: what is done, what is left out and why, the open questions, the tests added, and
   what was built or run (nothing, when nothing was).
7. The limits: do not edit the repository, do not commit, do not push, do not start the game, do not copy extracted game
   files into the patch or the report.

## After the report

Check the claims of the agent against the code or the disassembly before you use them. The report is an input, not a
fact. Then tell the user the result in Japanese.

For the library analyst, check at least 3 rows against the disassembly before you accept the table. For the handler
writer and the code writer, read the whole patch, run `git apply --check`, build and run `ctest` in the MSYS2 shell, and only then apply it.
A commit that uses the work of an agent gets the line of its model. Add both lines when both models did work.

- Code reviewer: `Co-Authored-By: GPT-6.1-Sol (Codex) <noreply@openai.com>`
- Game explorer, library analyst, handler writer, code writer: `Co-Authored-By: OpenCode <noreply@opencode.ai>`
