---
name: nx-log-read
description: Find and read the log and result files of a game run that the user made (game/recompiled/<id>/). Use it after the user says that they ran the game, or when you must judge a run from its files.
---

# Read the result of a game run

The user runs the game. You find the files and read them. Do not ask the user to read a log or to report numbers.

## Files

The folder is `game/recompiled/<id>/`. The program name decides the file names. A run of the update program writes
files with the update name.

| File | Holds |
| --- | --- |
| `<name>.log` | The log of the last run. The next run replaces it. |
| `<name>.hints` | The pcs that the recompiled code did not have (JIT misses). |
| `<name>.jitcache` | The code that the JIT compiled. |
| `threads.txt` | The state of each guest thread. It is written again every 15 seconds. Read a hang from it. |

Check the modification time first. A file that is older than the run is not the result of that run.

Check which program ran. The log tells it in its first lines (`game:` and `fs:` lines) and in the `Loaded <module>
at` lines. The base program runs the base game code. The update program runs the update code, and its modules are at
other addresses. Do not compare pcs of two runs unless the module bases are the same.

## Read the log

The log grows to megabytes. Never print it whole. Use `Select-String` with a pattern, `-First`, and
`Get-Content -Tail`.

| Look for | Pattern | Meaning |
| --- | --- | --- |
| Crash | `Crash`, `svcBreak`, `FATAL` | The game stopped. Read the lines around it. |
| JIT misses | `jit:` | The number of misses and the functions that the JIT compiled. |
| Frame rate | `perf:` | Frames each second. |
| Shaders | `vulkan:` with `failed` | Shader programs that did not translate. |
| End of run | `frames=` | Frames shown and the hash of the last frame. |

The first lines of the log tell which program ran, which modules loaded and at which addresses
(`Loaded <module> at 0x...`).

## Report

Give the user the numbers and your reading of them: crash or no crash, number of misses, frame rate, failed
shaders. Compare with the earlier run when you have one. Keep a copy of a file in the scratchpad before the next
run replaces it, if you need to compare.

Ask the user only for what a file cannot tell, for example what the screen showed.

## Next step

For the pcs in a hints file, use the skill `nx-jit-triage`.
