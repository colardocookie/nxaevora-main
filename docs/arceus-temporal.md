<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the NXAevora contributors -->

# Temporal passes

`SNX_ARCEUS_TEMPORAL` adds temporal passes to Pokémon Legends: Arceus in the Vulkan renderer. Unset, none of it runs.

| Value | What it does |
| --- | --- |
| `probe` | Finds the scene, makes camera motion vectors and measures them. The picture is unchanged unless a view is chosen |
| `taa` | Also jitters the scene's camera by a sub-pixel offset each frame and resolves the scene with a history: temporal anti-aliasing at the scene's size |

## Probe

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

## Anti-aliasing (`taa`)

Jitter. Each frame the scene is drawn with one of 8 sub-pixel offsets (a Halton sequence, within half a pixel each
way). The game's memory is not changed. A draw that binds the scene camera's block (constant bank 4 at one of the two
addresses the block takes in turn) gets a copy from the frame's staging memory in which the view-projection and the
projection are shifted and the inverse view-projection is shifted back. So the depth pre-pass, the G-buffer, the
lighting and the forward passes all see the same jittered camera, while the shadow maps, the probes and the UI, which
bind other blocks, see none. The motion vectors and the history use the matrices read from the game's memory, which
have no jitter, and so does the neighbouring slot with last frame's block.

Resolve. At the probe's place, before the scene colour is first read, the scene colour is replaced by a blend of the
frame (a tenth) and our history (nine tenths) in linear HDR. The history is fetched through the camera motion vector,
clamped to the frame's 3 x 3 neighbourhood in YCoCg, and dropped where the depth this pixel's point had a frame ago
lies outside the depths the history held in the 3 x 3 texels around that place, widened by a tenth,
where the point was off screen, on the first frame,
after a gap, after a change of size and after a camera cut. The game's bloom, exposure and tone mapping then work on
the resolved picture.

Views. `SNX_ARCEUS_TEMPORAL_VIEW=3` shows the frame's weight in the blend (white where the history was dropped) and
`4` the jittered frame without the resolve. Views 1 and 2 belong to the probe.

Numbers. The `arceus-temporal-stats` line then gives, by depth band, `dropped`, the share of pixels whose history
was dropped, and `far_from_frame`, the share whose resolved colour differs from the frame's own by more than a
quarter of its brightness. Trails behind things that move show in the second. `frame_to_frame` is the share of
pixels that differ by more than a quarter from the frame before as it was drawn, before the resolve. With a still
camera it shows the jitter at work: edges change from frame to frame. Without jitter it is near zero.
`shown_frame_to_frame` is the same share for the resolved picture against the resolved picture of the frame before.
The blend moves a pixel by a tenth of its distance each frame, so a quarter is rarely reached there: `above_0.05`
and `above_0.02` give the same share at those finer steps. With a still camera, shimmer from the jitter shows in them.

Limits. The vectors are the camera's only, so a moving character or Pokémon drags a trail until the history is
clamped away, and grass in the wind blurs. Thin bright details can flicker where the clamp cuts them. The resolve
runs once per frame of the game, also where the scene is not the field (it is skipped when the composite is not
found).

## Resolve options

Each is its own switch, off unless set, so they can be compared one at a time. They change the resolve of `taa` only.

| Switch | What it does |
| --- | --- |
| `SNX_ARCEUS_TEMPORAL_NEAREST=1` | A pixel takes the motion vector of the nearest surface in its 3 x 3 neighbourhood, so an edge and the background pixels beside it follow the thing in front |
| `SNX_ARCEUS_TEMPORAL_HISTORY=catmull` | The history's colour is fetched through a Catmull-Rom filter (five linear fetches, the corners left out) in place of one linear fetch: less softening under motion. The depth kept with the history is still read texel by texel |
| `SNX_ARCEUS_TEMPORAL_CLAMP=variance` | The history is clipped to the mean of the frame's 3 x 3 neighbourhood plus and minus 1.25 standard deviations, along the line toward the frame's colour, in place of the clamp to its minimum and maximum |
| `SNX_ARCEUS_TEMPORAL_BLEND=adaptive` | The frame's share is 0.05 standing, 0.1 from two pixels of motion a frame on, and up to 0.2 where the clip changed the history's brightness, in place of a fixed 0.1 |
| `SNX_ARCEUS_TEMPORAL_LUMA=1` | History and frame are blended with each weighted by 1 / (1 + its brightness), so a bright spark neither flickers in nor smears |
| `SNX_ARCEUS_TEMPORAL_SHARPEN=<0 to 1>` | A light sharpen in a pass of its own after the resolve. It reads the resolved picture from the history image and writes the scene colour only, so the history stays unsharpened. The amount falls where the neighbourhood already has contrast and the result stays within the 3 x 3 neighbourhood's least and greatest |
| `SNX_ARCEUS_TEMPORAL_MIPBIAS=<0 to -2>` | A texture level shift for the material textures (mipmapped, linearly filtered, not render targets) of draws into the scene, added to the game's own bias. Sharper textures under jitter. One value for the run |

The formulas are in `gpu/vk/temporal_math.h` with a host test (`tests/temporal_math.c`), and again in GLSL in
`shaders/temporal_resolve.frag`. None of the options has been played.

## Object vectors (`SNX_ARCEUS_TEMPORAL_VECTORS=object`)

With `probe` or `taa`. The camera vectors are right for what stands still. An object that moves gets its own:

- Each G-buffer draw binds the object's constant block (bank 6, its world matrix at +0). The matrix is kept in a
  table by the block's address. The game writes a block to two slots in turn, one bound size apart, and all blocks
  take the same turn as the camera's block. Last frame's matrix is the entry of the other slot.
- When the matrix differs from that one, and by less than 5 units of position, the draw is made once more right after the game's: the
  game's vertex program and state, a fragment program of ours, the vector target as its only colour target and the
  scene depth tested for equal and not written. So it covers the pixels the object owns.
- The fragment program takes the pixel and its depth and one matrix per draw, composed in double: this frame's clip
  as drawn, without the jitter, to the world, to the object, to its world a frame ago, to last frame's clip. The
  vector is the unjittered position now minus the position before. Blue keeps the depth it was drawn at.
- The camera vector pass takes an object's vector where the scene's final depth still is that depth, and the camera
  vector elsewhere. So something drawn in front later keeps its own.

Not drawn again: objects that did not move, new objects, instanced draws, draws with tessellation or a geometry
stage, draws under a predicate, draws without depth write unless they test for equal, and more than 256 a frame.
A skinned character gets the motion of its world matrix, not of its limbs. Each new combination of state makes one
pipeline, compiled when first needed.

`arceus-temporal-objects` every 600 frames: G-buffer draws with an object block, without a block a frame ago,
jumped too far, moved, drawn again, over the cap, not an affine matrix, pipelines made.

