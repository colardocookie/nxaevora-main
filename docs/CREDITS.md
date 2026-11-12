<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the NXAevora contributors -->

# Credits

NXAevora is GPL-3.0-or-later. These projects and works are used in it.

| Project | Use | License |
| --- | --- | --- |
| ScarletNX | The recompiler and the Switch runtime (vendored in `third_party/ScarletNX`, from upstream commit 8efbf9c4) | GPL-3.0-or-later |
| Dear ImGui (Omar Cornut and contributors) | The in-game windows, version 1.93.0, fetched at configure time by patch `0013` | MIT |
| Roboto (The Roboto Project Authors) | The Latin font of the in-game windows (`assets/fonts`) | SIL OFL 1.1 |
| Noto Sans JP (Google and the Noto project authors) | The Japanese font of the in-game windows (`assets/fonts`) | SIL OFL 1.1 |
| SMAA (Jorge Jimenez, Jose I. Echevarria, Belen Masia, Fernando Navarro, Diego Gutierrez) | The anti-aliasing mode SMAA: the shader port and the two lookup textures (`third_party/ScarletNX/dist/smaa`, license text in `LICENSE.txt` there) | MIT |
| Ryujinx (the Ryujinx team and contributors) | The resolution scale of texel fetches in fragment shaders: the formula of `Helper_TexelFetchScale` (the texel times the scale plus the fragment position modulo the scale) and its aspect ratio rule from `TextureBindingsManager`, in `spirv_translate.c` (`scaled_coords`) and `scale.h` (`vk_scale_same_aspect`). Thanks to the Ryujinx team for this method. | MIT |

The credits of the libraries that ScarletNX uses are in its own `docs/CREDITS.md`.
