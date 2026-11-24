<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the NXAevora contributors -->

# Credits

NXAevora is GPL-3.0-or-later. These projects and works are used in it.

| Project | Use | License |
| --- | --- | --- |
| ScarletNX | The recompiler and the Switch runtime: the Engine in `engine/` is derived from it (from upstream commit 8efbf9c4) | GPL-3.0-or-later |
| Dear ImGui (Omar Cornut and contributors) | The in-game windows, version 1.93.0, fetched at configure time by patch `0013` | MIT |
| Roboto (The Roboto Project Authors) | The Latin font of the in-game windows (`assets/fonts`) | SIL OFL 1.1 |
| Noto Sans JP (Google and the Noto project authors) | The Japanese font of the in-game windows (`assets/fonts`) | SIL OFL 1.1 |
| SMAA (Jorge Jimenez, Jose I. Echevarria, Belen Masia, Fernando Navarro, Diego Gutierrez) | The anti-aliasing mode SMAA: the shader port and the two lookup textures (`engine/dist/smaa`, license text in `LICENSE.txt` there) | MIT |
| Ryujinx (the Ryujinx team and contributors) | The resolution scale of texel fetches in fragment shaders: the formula of `Helper_TexelFetchScale` (the texel times the scale plus the fragment position modulo the scale) and its aspect ratio rule from `TextureBindingsManager`, in `spirv_translate.c` (`scaled_coords`) and `scale.h` (`vk_scale_same_aspect`). Thanks to the Ryujinx team for this method. | MIT |
| FidelityFX SDK API headers (Advanced Micro Devices, Inc.) | The C API headers of the FSR 3.1 adapter, from the SDK tag `v2.4.0` (commit `7539ae8`), in `engine/third_party/FidelityFX-SDK` (license text in `LICENSE.txt` there) | MIT |
| XeSS SDK headers (Intel Corporation) | The C headers of the XeSS Super Resolution adapter, from the XeSS SDK commit `de0fb9c`, in `engine/third_party/XeSS` (license text in `LICENSE.txt` there) | MIT |

The credits of the libraries that ScarletNX uses are in its own `docs/CREDITS.md`.
