<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

<!-- Copyright 2026 the NXAevora contributors -->

<p align="center">
<img alt="nxaevora-banner" src="assets/nxaevora-banner.png">
</p>

<p align="center">
<a href="https://github.com/nxaevora/nxaevora-main/actions/workflows/ci.yml"><img src="https://github.com/nxaevora/nxaevora-main/actions/workflows/ci.yml/badge.svg?branch=main" alt="Build NXAevora (Windows)"></a>
<a href="https://github.com/nxaevora/nxaevora-main/actions/workflows/repo-checks.yml"><img src="https://github.com/nxaevora/nxaevora-main/actions/workflows/repo-checks.yml/badge.svg?branch=main" alt="Repository checks"></a>
<br>
<img src="https://img.shields.io/badge/Platform-Windows%20x64%20%7C%20Linux%20x64%20(Planned)-blue?style=flat" alt="Badge">
<img src="https://img.shields.io/badge/Upscaler-DLSS%20%7C%20FSR3.1%20%7C%20XeSS-green?style=flat" alt="Badge">
<img src="https://img.shields.io/badge/Status-work%20in%20progress%20(alpha%20stage)-orange?style=flat" alt="Status: work in progress (alpha stage)">
<img src="https://img.shields.io/badge/License-GPL--3.0%20(v3)-lightgrey?style=flat" alt="License: GPL-3.0 (v3)">
</p>

<p align="center">
<a href="ROADMAP.md"><strong>ROADMAP</strong></a> · <a href="docs/dev/engine/overview.md"><strong>Engine Specifications</strong></a> · <a href="#recompile-and-run"><strong>How to Recompile (EXPERIMENTAL)</strong></a>
</p>

NXAevora is a project aimed at running Pokémon Legends: Arceus fully natively.<br>
The game's code is compiled using <a href="https://github.com/scarletnx/ScarletNX">ScarletNX</a>, a static compiler, while NXAevora provides a wide range of modifications, including the framework for native execution, runtime modifications, patches to the game's code, and a dedicated launcher.


> [!IMPORTANT]
> While AI is used as the primary means of development, human maintainers are responsible for determining the project's development direction, verifying functionality, reviewing and approving changes, and managing the project.

> [!NOTE]
> Users will be required to provide the game files and update data in an already decrypted format (**RomFS and ExeFS**), rather than as NSP files.<br>
> <strong>We do not condone the use of illegally distributed copies with this project under any circumstances.</strong> You must use copies obtained and prepared from your own legally acquired game.

## Reporting Issues and Contributing

Have you found a bug or issue in NXAevora that the developers are not aware of? Or do you have a suggestion about the code?

We certainly welcome both. If you find a bug or issue, please open an [Issue](https://github.com/nxaevora/nxaevora-main/issues/new/choose). If you have a suggestion, please submit a [Pull Request](https://github.com/nxaevora/nxaevora-main/pulls)!

If you would like to contribute, please read the [CONTRIBUTING.md](CONTRIBUTING.md) first. It will help you.

## Status

NXAevora is currently in alpha and is not yet suitable for general use.

Azunel, the project owner and one of its main developers, is currently developing the native path, creating the launcher, and refactoring the code, while colardocookie is implementing patches related to improving game quality (such as accurate dynamic FPS and research into motion vectors).

Windows x64 is currently the primary development target, with Linux x64 development planned for the beta stage or later. MacOS, Android, and iOS are not being considered and will not be included in the future roadmap.

## Features

### Implemented features
・Native NVN processing that bypasses the command stream built by the library (complete, now in the maintenance phase with bug fixes)

・FXAA and SMAA anti-aliasing

・Bilinear and bicubic scaling

・Native mouse camera controls (currently in testing, with fixes planned under roadmap item 9)

・An accurate dynamic FPS patch (now in the maintenance phase, with changes limited to bug fixes)

・Temporal anti-aliasing (TAA) (EXPERIMENTAL)

・DLSS, FSR3.1, and XeSS AI upscaling technologies (EXPERIMENTAL)

### Planned features
・Intel Xe Ground Truth Ambient Occlusion (XeGTAO) support

・HDR10 support



・Support for adjusting various LoD distances and draw distances

・Support for increasing Pokémon spawn distance

・Support for adjusting shadow resolution, filtering cascades, sharpness, draw distance, and other settings

・Improvements to screen-space reflections (SSR)

・Enhanced bloom

<br>The list of the changes in the runtime is in [docs/build/runtime-changes.md](docs/build/runtime-changes.md). 

Also, the plan for native implementation is also documented in [ROADMAP.md](ROADMAP.md).

## Requirements

- Windows 11 x64
- A GPU with Vulkan 1.3 support
- Decrypted game sources (RomFS, ExeFS, control.nacp), including the base game and the v1.1.1 update, dumped from your own copy<br>(NXAevora contains no game files. Do not ask for them and do not share them.)

## Build

Clone the repository. ScarletNX is part of it (`engine`, copied from upstream commit 8efbf9c4):

```sh
git clone <this repository>
```

The build steps for Windows are in [docs/build/engine-build.md](docs/build/engine-build.md). They use MSYS2
and the CLANG64 environment.

## Recompile and run

Build NXAevora Engine first, as [docs/build/engine-build.md](docs/build/engine-build.md) describes. The build makes `engine/build/default/nxaevora-engine.exe`. A portable copy, made with step 7 of that guide, works too. Its program is `nxaevora-portable/bin/nxaevora-engine.exe` in your MSYS2 home folder.

### The game folder

The input is a folder that you make from your own game. It holds:

- `exefs/` (or `ExeFS/`), with the program files `main` and `main.npdm`. Use the ExeFS of the v1.1.1 update.
- `romfs/` (or `RomFS/`), with the RomFS of the game and the v1.1.1 update applied.
- `control.nacp`.

No keys are needed. The recompiler refuses NSP and XCI files, so decrypt your copy to a folder first.

### Optional: make the jump-table hints

The recompiler does not read the jump tables of the game. The hints file lists the targets of these tables, so the recompiled code has a start point at each target. The hints are valid for one game version only. Make them again for another version.

The scan does not create folders. Make the game's output folder first, then write the hints into it:

```sh
mkdir -p game/recompiled/01001F5010DFA000
export SNX_SCARLETNX="$(cygpath -m "$PWD/engine/build/default/nxaevora-engine.exe")"
python tools/jump_table_scan.py "C:/path/to/game folder" --hints game/recompiled/01001F5010DFA000/01001F5010DFA000.hints --merge
```

With `--merge`, the targets in an existing hints file stay, and the old file is copied to `<file>.bak`. The recompile reads the hints file by itself. It must have the name `<name>.hints` in the game's folder.

### Recompile

```sh
engine/build/default/nxaevora-engine.exe recompile "C:/path/to/game folder" --outdir game/recompiled
```

The game takes its title ID, `01001F5010DFA000`, as its name. The recompile writes its folder, `game/recompiled/01001F5010DFA000/`, with:

- `01001F5010DFA000.exe` and `01001F5010DFA000_code.dll`, the runtime and the compiled game code.
- `01001F5010DFA000.ini`, the settings. Its `[Game]` section has `romfs=`, the path of your RomFS folder. The runtime reads the RomFS from that folder, with no copy. Moving or deleting the folder stops the run.
- `01001F5010DFA000_data/`, the saves and `control.nacp`.

### Run

```sh
engine/build/default/nxaevora-engine.exe run game/recompiled/01001F5010DFA000
```

The `run` command takes the folder of a recompiled game.

### Settings

The `settings` command reads and writes the game's settings. `settings list` prints them, `settings get KEY --game=PATH` prints one value, `settings set KEY VALUE --game=PATH` writes one value, and `settings check KEY VALUE` checks one value with no file. The full list of commands and options is in [engine/docs/USAGE.md](engine/docs/USAGE.md).

## Legal

NXAevora is an independent project. It is not affiliated with or endorsed by Nintendo, The Pokemon Company or Game
Freak. Pokemon and Pokemon Legends Arceus are trademarks of their owners.

You must own the game to use NXAevora. The files that you make with it, such as the recompiled code, come from the
game and are for your own use. Do not distribute them.

## License

NXAevora is free software under the GNU General Public License, version 3 or later. See [LICENSE](LICENSE).
ScarletNX is also GPL-3.0. Its license and the list of the projects that it builds on are in
`engine/`. The credits of NXAevora are in [docs/CREDITS.md](docs/CREDITS.md).
