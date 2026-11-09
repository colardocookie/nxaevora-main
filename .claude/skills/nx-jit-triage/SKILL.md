---
name: nx-jit-triage
description: Find why the recompiled game reached code that the AOT module lacks (JIT misses). Use it with a .hints file, when you classify the missed pcs, name them, disassemble them, or write the result in docs/jit-analysis.md.
---

# Triage of JIT misses

## What a miss is

- Generated code finds an indirect target in `CPU.disp`, one slot for each 4-byte pc. An empty slot calls the
  runtime JIT. That call is a miss.
- A slot is filled for each leader: the first instruction of a function, the instruction after B, BR, RET or BRK,
  the target of B or a conditional branch, and the instruction after BL or BLR (the return site).
- The exploration of a function stops at BR. The recompiler detects the base of a jump table, but it does not read
  the entries of the table.

## Check the layout first

The pcs, the log and the extracted ExeFS must be of the same program. The base game and the update have different
module bases. A disassembly of pcs from the base program against the update modules shows the wrong code.

- Read the `Loaded <module> at` lines of the log of the run.
- Print the bases of the extracted ExeFS (`moduledump.load_program` in `tools/moduledump.py` of ScarletNX). They
  must be equal. `game/work/Arceus` is extracted with the update, so it has the update layout.
- For the base program, extract the base game alone (no `--update`) into another folder.

## Steps

1. Sort the pcs by module:
   `python -I tools/classify_hints.py game/recompiled/<id> [hints-name]`
2. Extract the dump, if `game/work/Arceus` does not exist. It writes about 6 GB, so tell the user first.
   `scarletnx.exe -x <base>.nsp --update <update>.nsp --out game/work/Arceus`
3. Name and disassemble the pcs with the ScarletNX tools. They need the MSYS2 tools on `PATH`. Run them in the
   Bash tool, in `third_party/ScarletNX`:

   ```bash
   export PATH="/c/msys64/clang64/bin:$PATH"
   export SNX_SCARLETNX="D:/.Developments/nxaevora/project-main/game/scarletnx/bin/scarletnx.exe"
   python -I tools/symbols.py <work dir> <hex pc>...
   python -I tools/disasm.py <work dir> <start> <end>
   ```

   Start 8 to 12 bytes before the pc, so that you see the instruction before it.
4. For each pc, write what the instruction before it is and what kind of place it is (see the table below).
5. Write the result in `docs/jit-analysis.md`.

## Jump-table scan

`tools/jump_table_scan.py <extracted game folder>` lists the targets of all jump tables. Use the same environment as
for `symbols.py` (`PATH` and `SNX_SCARLETNX`). The extracted ExeFS must have the layout of the program that you
analyze. Options: `--hints FILE` writes the targets as a hints file, `--check <hex pc>...` says which table holds a
pc. Run it first. A pc that is a target of a table is explained, and the next steps are only for the other pcs.

## Check each pc

- The return site of a BL is the address of the BL plus 4. A pc that is two or more instructions later is not a
  return site. This was wrong once in `docs/jit-analysis.md`.
- A symbol name of a pc is not reliable when `symbols.py` says `past MAX_DISTANCE`. Such a pc is in code without
  symbols.
- A return site should be a leader. A miss at a return site means that the caller function was not in the AOT module.

| Kind | What you see |
| --- | --- |
| Stub | `adrp`, `ldr x17`, `br x17` (PLT), or code after a `br x16` |
| Return site | The first instruction after a `bl` or `blr` |
| After exclusive store | `cbnz` right after `stlxr` or `stxp` |
| Case block | A block after an unconditional `b`, with no direct way in |
| In function | Anything else inside a function |

## Rules

- Do not start the game. For more data, ask the user to run a scene, then read the files with `nx-log-read`.
- A hypothesis is not a finding. Write "not verified" until you check it in the data.
