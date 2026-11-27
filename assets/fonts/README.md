<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the NXAevora contributors -->

# Fonts of the in-game user interface

These fonts are for the in-game user interface. They are not data of the game. Roboto and Noto Sans JP are under
the SIL Open Font License 1.1, and their license text is in this folder. Material Symbols Rounded is under the Apache
License 2.0, and its license text is in this folder. The text fonts are not changed.

| File | Font | Source | SHA-256 |
| --- | --- | --- | --- |
| `Roboto-Variable.ttf` | Roboto, variable (`wdth`, `wght`) | `ofl/roboto/Roboto[wdth,wght].ttf` in `google/fonts` at commit `6c1dbe27dcf7d817d4b62cec033ba77fa8e2a5ec` | `d7598e12c5dbef095ff8272cfc55da0250bd07fbdecbac8a530b9b277872a134` |
| `NotoSansJP-Regular.otf` | Noto Sans JP, Regular, the subset OTF | `Sans/SubsetOTF/JP/NotoSansJP-Regular.otf` in `notofonts/noto-cjk` at commit `f8d157532fbfaeda587e826d4cd5b21a49186f7c` | `dff723ba59d57d136764a04b9b2d03205544f7cd785a711442d6d2d085ac5073` |
| `Roboto-OFL.txt` | License of Roboto | same commit as the font | `061402327a96aadb0bfb694a960ed289ecd38d383e396243831ab81feb109c41` |
| `NotoSansJP-OFL.txt` | License of Noto Sans JP | `Sans/LICENSE`, same commit as the font | `6a73f9541c2de74158c0e7cf6b0a58ef774f5a780bf191f2d7ec9cc53efe2bf2` |
| `MaterialSymbols-LICENSE.txt` | License of Material Symbols (Apache License 2.0) | `LICENSE`, same commit as the icon font | `58d1e17ffe5109a7ae296caafcadfdbe6a7d176f0bc4ab01e12a689b0499d8bd` |
| `MaterialSymbolsRounded-Subset.ttf` | Material Symbols Rounded, the static instance FILL 0, GRAD 0, opsz 24, wght 400, with the 15 icons of the engine menu | `variablefont/MaterialSymbolsRounded[FILL,GRAD,opsz,wght].ttf` in `google/material-design-icons` at commit `10ecc14790612ca8fd164e3f4ffe9c16fc50d5b7` | `f086a36357fb4d8f8020ffd43e9221ac18815d54b81d308c46dab23e026b535e` |

Roboto is a variable font. The text renderer of Dear ImGui (stb_truetype) draws its default instance, which is the
Regular weight. Noto Sans JP is the subset OTF of the Japanese set, with the glyphs of the Japanese character sets.
It is about 4.5 MB.

Material Symbols Rounded is the icon font of the engine menu (`docs/dev/ui/engine-menu.md`). The license text is
`MaterialSymbols-LICENSE.txt`. Make `MaterialSymbolsRounded-Subset.ttf` again
with `python tools/subset_icon_font.py assets/fonts/MaterialSymbolsRounded-Subset.ttf --cache <folder>`: the tool
downloads the pinned commit, fixes the axes to FILL 0, GRAD 0, opsz 24 and wght 400, and keeps only the code points
of the 15 icons of the menu.

The runtime reads the three fonts from `fonts/` in the game's data folder (the folder beside the program that holds
the game's files). No build step or copy step puts them there, so copy `Roboto-Variable.ttf`,
`NotoSansJP-Regular.otf` and `MaterialSymbolsRounded-Subset.ttf` into `<data folder>/fonts/` by hand until the
launcher writes them. The engine menu logs the path of a missing icon font once and uses the text font for the icons.

