<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

<!-- Copyright 2026 the NXAevora contributors -->

<center>
<img alt="aevora-icon" src="assets/nxaevora-banner.png" width="1000">
<br><a href="https://github.com/nxaevora/nxaevora-main/actions/workflows/ci.yml"><img src="https://github.com/nxaevora/nxaevora-main/actions/workflows/ci.yml/badge.svg?branch=main" alt="Build NXAevora (Windows)"></a>
<img src="https://img.shields.io/badge/Platform-Windows%20x64%20%7C%20Linux%20x64%20(Planned)-blue?style=flat" alt="Badge">
<img src="https://img.shields.io/badge/Status-work%20in%20progress%20(alpha%20stage)-orange?style=flat" alt="Status: work in progress (alpha stage)">
<img src="https://img.shields.io/badge/License-GPL--3.0%20(v3)-lightgrey?style=flat" alt="License: GPL-3.0 (v3)">

<br>NXAevora is a project aimed at running **Pokémon Legends: Arceus** fully natively.<br>
The game's code is compiled using <a href="https://github.com/scarletnx/ScarletNX">ScarletNX</a>, a static compiler, while NXAevora provides a wide range of modifications, including the framework for native execution, runtime modifications, patches to the game's code, and a dedicated launcher.

<br>
</center>

> [!IMPORTANT]
> While AI is used as the primary means of development, human maintainers are responsible for determining the project's development direction, verifying functionality, reviewing and approving changes, and managing the project.

> [!NOTE]
> Users will be required to provide the game files and update data in an already decrypted format (**RomFS and ExeFS**), rather than as NSP files.<br>
> <strong>We do not condone the use of illegally distributed copies with this project under any circumstances.</strong> You must use copies obtained and prepared from your own legally acquired game.

## Status

NXAevora is currently in alpha and is not yet suitable for general use.

Azunel, the project owner and one of its main developers, is currently developing the native path, creating the launcher, and refactoring the code, while colardocookie is implementing patches related to improving game quality (such as accurate dynamic FPS and research into motion vectors).

Windows x64 is currently the primary development target, with Linux x64 development planned for the beta stage or later. MacOS, Android, and iOS are not being considered and will not be included in the future roadmap.

## Reporting Issues and Contributing

Have you found a bug or issue in NXAevora that the developers are not aware of? Or do you have a suggestion about the code?

We certainly welcome both. If you find a bug or issue, please open an [Issue](https://github.com/nxaevora/nxaevora-main/issues/new/choose). If you have a suggestion, please submit a [Pull Request](https://github.com/nxaevora/nxaevora-main/pulls)!

If you would like to contribute, please read the [CONTRIBUTING.md](CONTRIBUTING.md) first. It will help you.

## Features

### Implemented features
・Native NVN processing that bypasses the command stream built by the library

・FXAA and SMAA anti-aliasing

・Bilinear and bicubic scaling

・Native mouse camera controls (currently in testing, with fixes planned under roadmap item 9)

・An accurate dynamic FPS patch (now in the maintenance phase, with changes limited to bug fixes)

### Planned features
・Intel Xe Ground Truth Ambient Occlusion (XeGTAO) support

・HDR10 support

・Temporal anti-aliasing (TAA)

・DLSS, FSR3.1, and XeSS AI upscaling technologies

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
- Decrypted game sources (RomFS, ExeFS), including the base game and the v1.1.1 update, dumped from your own copy<br>(NXAevora contains no game files. Do not ask for them and do not share them.)

## Build

Clone the repository. ScarletNX is part of it (`third_party/ScarletNX`, copied from upstream commit 8efbf9c4):

```sh
git clone <this repository>
```

The build steps for Windows are in [docs/build/SCARLETNX_BUILD-NOTES.md](docs/build/SCARLETNX_BUILD-NOTES.md). They use MSYS2
and the CLANG64 environment.

## Legal

NXAevora is an independent project. It is not affiliated with or endorsed by Nintendo, The Pokemon Company or Game
Freak. Pokemon and Pokemon Legends Arceus are trademarks of their owners.

You must own the game to use NXAevora. The files that you make with it, such as the recompiled code, come from the
game and are for your own use. Do not distribute them.

## License

NXAevora is free software under the GNU General Public License, version 3 or later. See [LICENSE](LICENSE).
ScarletNX is also GPL-3.0. Its license and the list of the projects that it builds on are in
`third_party/ScarletNX/`. The credits of NXAevora are in [docs/CREDITS.md](docs/CREDITS.md).
