<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the NXAevora contributors -->

# Frame pass census

`SNX_PASS_CENSUS=1` makes the Vulkan renderer log what a frame is made of. It only reads. Every 1200 presents from
the 900th, up to 10 times, the passes of two frames in a row are written in order as `arceus-pass` lines. A pass is a run of
draws on one set of targets, so the game returning to a target starts a new pass.

The header line gives the present number, the number of passes, how many were dropped (the limit is 384), the guest
address of the surface shown last frame and the renderer's resolution scale. Then one line per pass:

    arceus-pass: present=N pass=K kind=draw draws=D c0=ADDR:WxH(SWxSH)fF z=ADDR:WxH(SWxSH)fF vp=WxH vs=HASH fs=HASH+N samples=... marks=...

| Field | Meaning |
| --- | --- |
| `kind` | `draw`, or `drawtexture` for the guest's texture blit |
| `c0` to `c7` | colour targets: guest address, guest size, image size, Vulkan format number. A trailing `-` means bound and not written |
| `z` | depth target, the same. `ro` means read-only, `-` bound and not written |
| `vp` | the first draw's viewport in image pixels. Against the target's image size it shows the game's 3D buffer scale |
| `vs`, `fs` | program hashes of the first draw, and how many later draws used another fragment program |
| `samples` | textures the fragment programs sampled that are or were render targets: address, guest size, format. `D` a depth format, `H` a target of last frame not yet written in this one, `T` a target written earlier in this frame |

Marks: `PRESENT` on the first pass that writes the surface shown last frame (`present` on later ones), `HISTORY` when a
sample carries `H`, `VELOCITY?` when a two-channel colour target is written with a depth target bound, `DEPTHREAD` when
a sampled depth format was a target earlier in the frame.

Clears are not listed. Textures that never were a render target are left out. A pass keeps at most 12 samples.

## Vertex constant blocks

For a draw into a geometry pass (two or more colour targets written, with a depth target) the vertex stage's constant
blocks are logged too, for draws 0, 1, 2 and every 16th, up to eight a pass:

    arceus-pass-blocks: present=N pass=K draw=D vs=HASH b0=ADDR:SIZE b1=ADDR:SIZE ...
    arceus-pass-block: present=N pass=K draw=D b1 words=<up to 0x140 bytes as 32-bit words in hex>

The first line lists every bound block with its guest address and size in bytes. The second gives the first 0x140
bytes of up to four blocks of at least 0x40 bytes. The two frames in a row let a block that keeps last frame's world
matrix beside this frame's be seen: the same draw's words in the second frame repeat a matrix of the first.
