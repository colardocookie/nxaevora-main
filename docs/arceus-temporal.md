<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the NXAevora contributors -->

# Temporal probe

`SNX_ARCEUS_TEMPORAL=probe` is the first step toward temporal anti-aliasing and upscaling in Pokémon Legends: Arceus.
It changes nothing in the picture unless a view is chosen. Unset, none of it runs.

## What it does

1. It learns the scene from the frame itself. The composite is the draw with one colour target that is not a float
   target, no depth target, and among its textures a `B10G11R11` float one and a depth one of the target's size. Those
   two are the scene colour and the scene depth. No guest address is built in.
2. At the first draw of a frame into three or more colour targets with the scene depth (the G-buffer) it reads the
   camera block, constant bank 4: the view-projection at `+0x00` and its inverse at `+0xC0`, row vectors, reversed
   depth. The view-projection of the frame before is kept. The game writes this block to two slots in turn, so the
   neighbouring slot should hold the same matrix: the log says whether it does.
3. It notes the fragment program of the first draw that samples the scene colour after it was written. From the next
   frame on the probe runs just before that draw, which is after the last write to the scene colour and before its
   first read.
4. There a pass writes camera motion vectors to a two-channel float image of the scene's size: for each pixel its uv
   minus the uv the same world point had in the frame before, from depth and the two matrices. Zero at the far plane.

## Views

`SNX_ARCEUS_TEMPORAL_VIEW=1` draws the vectors over the scene colour: red for motion across, green for motion down,
with a little blue where either is negative. `SNX_ARCEUS_TEMPORAL_VIEW=2` draws the difference between this frame's
colour and last frame's colour fetched through the vectors, relative to their brightness. With a still camera both are black. While the camera pans
over terrain, view 2 stays near black on static geometry and lights up on what moves by itself: characters, Pokémon,
particles and water. The game's own bloom, exposure and tone mapping then run on the view, so bright spots glow.

## Numbers

So that nobody has to judge the views by eye, the probe measures itself. Every 60 runs it samples 96 x 54 pixels of
the scene and compares each with last frame's colour, once fetched through the motion vector and once at the same
place. A pixel counts as wrong when the colours differ by more than a quarter of their brightness. One
`arceus-temporal-stats` line a second gives the mean and longest vector in pixels and, for three depth bands (nearer
than 10, to 100, beyond), the share of wrong pixels with the vectors and without them. With a still camera both
shares are near zero. While the camera pans over static ground the vectors are long, the share without vectors is
high and the share with them must stay low. If it does not, the depth or screen convention is wrong.

Depth is taken as the viewport stores it: stored depth = translate + scale x the projection's z, which holds for both
depth modes of the guest GPU. The first runs log the two values and the mode.

## Log lines

`arceus-temporal` lines say which scene colour and depth were chosen, which program reads the scene colour first,
where the camera block is and whether the neighbouring slot holds last frame's view-projection, and the first runs
with the viewport and depth range used. Skipped frames are counted with the reason.

## Limits

The vectors describe the camera only. A moving object has the vector of the background behind its depth. The depth
convention and the direction of the screen's y axis come from the scene's viewport, and view 2 is the check that they
are right. Cutscenes and menus with another frame layout are skipped when the composite is not found.
