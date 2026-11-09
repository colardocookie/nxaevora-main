---
name: nx-recompile
description: Recompile Pokemon Legends Arceus (base game and update) with the portable ScarletNX in game/scarletnx. Use it when you recompile the game, apply new hints, or check that a recompile finished. It does not start the game.
---

# Recompile the game

## Before you start

1. Look for a running recompile: `Get-Process scarletnx -ErrorAction SilentlyContinue`. If one runs, wait. Never
   start a second recompile. Two recompiles write the same files and can damage them.
2. The portable program is `game/scarletnx/bin/scarletnx.exe`. If ScarletNX was built again, make the portable
   copy again (`docs/SCARLETNX_BUILD-NOTES.md`).
3. The dumps are outside the repository. Ask the user for the paths if you do not know them.

## Command

Run it in the background. The output goes to a log file in the scratchpad. Use `cmd /c` for the redirect, because
the PowerShell redirect gives an empty log and no exit code.

```powershell
$root = 'D:\.Developments\nxaevora\project-main'
$env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
cmd /c "`"$root\game\scarletnx\bin\scarletnx.exe`" -r --outdir `"$root\game\recompiled`" `"<base>.nsp`" --update `"<update>.nsp`" > `"<scratchpad>\recompileN.log`" 2>&1"
"exit=$LASTEXITCODE" | Add-Content "<scratchpad>\recompileN.log"
```

The `-r` option recompiles and does not start the game. A base game and its update take about 3 to 4 minutes
without hints. With about 45000 hints they take about 11 minutes.

## Check the result

- An empty log is normal. The program prints almost nothing. Do not take it as a failure.
- The `exit=` line at the end of your log file is the exit code. The exit code in the task notification can be
  the exit code of another command. Do not use it.
- Look in `game/recompiled/<id>/`. A finished recompile has `<id>_code.dll`, the code of the update
  (`<id>_<update id>v<version>_code.dll`) and the `.abi` stamps.
- To know that an update recompile used new hints, compare the SHA-256 and the size of its `_code.dll` before and
  after.

## Jump-table hints (do this before the recompile of the update)

Almost all misses are targets of jump tables, which the recompiler does not read. `tools/jump_table_scan.py` finds
the targets and writes them into the hints file, so the recompile marks them as leaders.

1. The extracted ExeFS must have the layout of the program: `game/work/Arceus` is extracted with the update. Only
   its `exefs/` is needed. If the game version changes, extract again and make the hints again. Old hints are not valid.
2. Run the scan. It takes a few seconds and needs no MSYS2:

   ```powershell
   $env:SNX_SCARLETNX = "$root\game\scarletnx\bin\scarletnx.exe"
   python -I "$root\tools\jump_table_scan.py" "$root\game\work\Arceus" `
     --hints "$root\game\recompiled\<id>\<update program name>.hints" --merge
   ```

3. `--merge` keeps the pcs that the runtime recorded and copies the old file to `<file>.bak`.
4. Recompile (see Command). The hints file is not in git. Make it again from the scan when you need it.

## Hints

- `scarletnx -r` reads `<id>/<name>.hints` by itself. It marks each pc as a leader in the new code.
- The runtime names the hints file after the program that started. The base program (`<id>.exe`) runs the BASE
  game code, even when the update is checked. The update has its own program
  (`<id>_<update id>v<version>.exe`) with its own hints file.
- A hints file is valid only for the program that wrote it. The modules of the update are laid out at other
  addresses than the modules of the base game (sdk and subsdk are 0x45000 higher). Never copy hints from the base
  program to the update program. Only pcs in rtld have the same address in both.
- Make the user start the program that you want hints for. For the update, that is the update program.
- Keep a copy of a hints file before you change it.

## Rules

- Do not start the game. The user runs the game.
- Do not delete `<id>_data/` (saves) without the user's answer.
