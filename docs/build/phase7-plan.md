<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the NXAevora contributors -->

# Phase 7 plan: decrypted game files and a file layer for mods

Goal and exit condition: `ROADMAP.md`, section "Phase 7". This file holds the inventory of the code that phase 7
removes or changes, the input layout of the decrypted files, the mod layer, and the PR units. Paths are relative to
`engine/` unless they start with the repository root. Line numbers are from `main` at `92f1705`.

## 1. How the game is read now

- Recompile: the driver takes an NSP or XCI (also DXCI and DNSP), loads `prod.keys` and `title.keys`, and reads the
  ExeFS and `control.nacp` in place (`src/nx/runtime/driver/recompile.c` `load_dump`). The update is a second container
  (`--update`), and the driver makes a second program for it (`01001F5010DFA000_01001F5010DFA800v262144.exe`).
- Run: `<name>.ini` names the dump (`source=`) and the update (`addons=`). `fs_init` (`src/nx/hle/filesystem/fs.c`,
  `dump_open`) opens the dump with the keys and reads the RomFS in place. The update RomFS is a BKTR patch RomFS over the
  base (`snx_dump_fs`, `snx_hac.c` `patches_romfs`). DLCs come from dumps too (`fs.c` `g_hle.fs.aocs`).
- A path with no keys exists already, but only for the recompile: `-r` on an ExeFS folder (`main`, `main.npdm`, `rtld`,
  `sdk`, `subsdk*`) with a sibling `romfs/` folder (`src/nx/loader/loader.c` `find_exefs`, `load_exefs_program`). The
  driver cannot play from such a folder (`driver.c` `play_dump_at` needs a prepared game, an NRO or a container).
- `romfs_init` (`fs.c:923`) already reads, in this order: `romfs.bin` in the data folder, the update over the base dump,
  a `romfs/` folder in the data folder, the base dump. The mods' `romfs/` folders go over the result.

## 2. Inventory

### 2.1 Remove

- `src/nx/runtime/driver/hactool/` (55 files): AES, RSA, SHA, key files (`extkeys`), NCA, NCA0, PFS0, HFS0, XCI, NAX0,
  KIP, packages, save, BKTR, IVFC, the LibHac storages (`storage.c`), NSO and NPDM parsers that the loader does not use,
  `snx_hac.c` (the ScarletNX face). Exception: `cJSON.c` and `cJSON.h` move out (see 2.2).
- `src/nx/runtime/driver/container.c`, `container.h`: keys, containers, content meta, update and DLC selection.
- `src/nx/runtime/driver/extract.c`, `extract.h`: `-x` and the title and icon read from a dump.
- `src/common/utilities/setup.c:237-359`, `setup.h:62-81`: the keys API (`snx_keys_text`, `snx_keys_found`,
  `snx_keys_dir`, `snx_keys_put`, `snx_keys_install`). `RAND_bytes` for the user ID stays.
- `src/nx/runtime/driver/setup.c:248-276`, `:387`: the keys step of the terminal setup.
- Options: `--keys`, `--title-keys` (driver and runtime, `driver.c:67-68`, `main.c:785-823`), `-x` and its options
  (`extract.c:30-34`), `--update` of a container, `--extract-container` and `tmpfs`/`extract` settings for the NX target
  (`cli.h:17-19`, `settings.txt:1161-1169`, `src/gui/settings.c:383`).
- Runtime file system: `dump_open`, `dump_map`, the dump fields of `fs.h` (`dfs`, `hfs`, `aocs`), the BKTR branch and
  the dump branches of `romfs_init`, DLC from dumps (`fs.c:225-252`).
- GUI (`src/gui/`): `GUI_KEYS`, `GUI_LOAD_KEYS`, the Install Keys button, help and messages, the drop and paste of
  `prod.keys`, the setup page rows, the `.xci/.nsp/.dxci/.dnsp` file filters. The rest of the GUI goes in phase 7.5.
- Build: `cmake/hactool.cmake`, its include (`cmake/dependencies.cmake:325-327`), the `hactool_lib` link
  (`src/nx/runtime/driver/CMakeLists.txt:18`), the license install (`CMakeLists.txt:248,281-288`), the test targets
  `hactool_decrypted` and `hactool_layers` (`CMakeLists.txt:221-230`), `scripts/check-package.sh:164-170`.
- Tests that test only removed code: `tests/hactool_decrypted.c`, `tests/hactool_layers.c`, `tests/hac_synth.h`,
  `tests/extract.c`, the keys cases of `tests/setup.c` and `tests/gui_sdl.c`.

### 2.2 Keep and change

- cJSON: `mcp.c`, `mcp_gdb.c` and `mcp_edenprobe.c` use it. Move it to its own folder (MIT, keep the credit).
- OpenSSL stays: the loader hash, `unit_cache.c`, `ldr.c`, `time.c`, `ldn.c`, TLS (`network/ssl.c`), the 3DS target.
  LZ4 stays (NSO decompression is not encryption).
- `snx_dump_name` (`src/common/utilities/common.c:1313`) and `--extract-container` of the 3DS target
  (`src/3ds/runtime/main.c`): not Switch decryption, out of scope. Keep them.
- Tests with a container as input: `tests/filesystem_service.c` (the `dump*`, `addon_overlay`, `mods*` suites),
  `tests/driver_workflow.c`, `tests/data_dir.c`, `tests/setup_terminal.c`, `tests/gui_model.c`, `tests/gui_setup.c`,
  `tests/gui_settings.c`, `tests/uigen.cmake`, `tests/xdg_layout.py`. Change them to folder input or remove the cases.
- Docs: `docs/USAGE.md`, `docs/GUI.md`, `docs/ARCHITECTURE.md`, `docs/BUILD.md`,
  `docs/CREDITS.md`, `docs/ANDROID.md`, `docs/APPLE.md`, `dist/android` comments; `po/` strings (regenerate);
  repository root `docs/build/runtime-changes.md` (the removal list), `docs/analysis/stutter-analysis.md` (old path).
- Local skills: `nx-recompile` passes `<base>.nsp --update <update>.nsp`. Change it to the decrypted folder.

## 3. Input layout of the decrypted files (proposal)

The user makes one folder from their console:

```text
<game folder>/
  exefs/          main, main.npdm, rtld, sdk, subsdk0, subsdk1 (the ExeFS of the update)
  romfs/          the RomFS of the game with the update applied
  control.nacp    from the Control NCA of the update (name, version, save size)
  icon.jpg        optional
```

- This is the layout that `find_exefs` and `load_exefs_program` read already (`exefs/` with a sibling `romfs/`).
- The recompile makes one program for this folder. The base program and the separate update program go away.
- The run reads the RomFS from the folder of the user with no copy. The runtime gets the path from `<name>.ini`
  (`romfs=`, in place of `source=`) and lays it as the `romfs/` branch of `romfs_init`. A `romfs=` folder that is
  not there, or a `romfs/` or `romfs.bin` in the data folder beside it, stops the run with a log line. An
  add-on's RomFS (an update's data folder) replaces it, as the overlays do, and the run logs that `romfs=` is not used.
- The folder names can have capitals (`ExeFS/`, `RomFS/`, as Ryujinx writes them): `find_exefs` and
  `load_exefs_program` take both forms.
- `romfs.bin` (one RomFS image file) stays a valid input, as now.

## 4. Mod layer

- Base: the existing mods (`src/common/utilities/mods.c`, `fs.c` `mods_open`). A mod is
  `<game>_data/mods/<mod>/` with `romfs/` and `exefs/`.
- Order (written in ScarletNX `docs/USAGE.md`, Mods, and tested by `filesystem_mods` and `filesystem_mods_setting`):
  game RomFS first (also a `romfs=` folder), then each played mod in name order of its folder, so that a later mod
  wins. `mods_off=` leaves a mod out. The order of the mods is changed by the folder name (for example a number
  prefix). The names are compared byte by byte (`strcmp`), so `10` comes before `2`.
- RomFS files: replaced at run time, no change to the folder of the user.
- ExeFS: a mod gives `.pchtxt` or `.ips` patches or a replaced module. These are code, so they need a new recompile
  (AOT). This stays and is written in the docs.
- Test: a host test with a game RomFS folder and two mods that replace the same file (the later mod wins) and one mod in
  `mods_off=`. User test: a mod that replaces one game file, with a visible effect.

## 5. PR units

1. Input from the decrypted folder: `-r <game folder>` recompiles with no keys and no copy of the RomFS. It records the
   RomFS folder in `<name>.ini` (`romfs=`), and the runtime reads the RomFS from there. Container input stays in this
   PR. Play through the driver (`scarletnx <game folder>`) is not part of it: the player starts the program in
   `recompiled/<id>/`, and the launcher (phase 11) can add it. User test: recompile and run with the keys moved away.
2. Removal: hactool, containers, keys, extraction, BKTR, DLC from dumps, the options, the GUI parts, the tests, the
   build lines; cJSON moves; `docs/build/runtime-changes.md`. User test: the same run as PR 1.
3. Mod layer: the order in the docs, the host test, the user test mod.
4. Docs and translations: ScarletNX docs, `po/`, `README.md` status, `ROADMAP.md` phase 7 done.

## 6. Open decisions

Decided by the user on 2026-11-15:

- Input: one game folder with `exefs/`, `romfs/` and `control.nacp` (section 3), and one recompiled program. The
  separate base and update programs go away.
- RomFS at run time: read from the folder of the user by its path in `<name>.ini`, with no copy.
- DLC: the DLC read from dumps is removed with the decryption. A DLC read from a folder can come later if needed.
- Mod order: the name order of the mod folders (section 4), fixed in the docs and in a test. No order list setting.

- The decrypted files of the user stay outside the repository, in a folder that the user chooses. `romfs=` holds its
  absolute path.
- PR 1 recompiles in place in `recompiled/<id>/`: the saves, the mods, the settings and the shader cache stay. The old
  base program and its `.jitcache` are moved away first, and the hints of the update program are merged into
  `<id>.hints`.
