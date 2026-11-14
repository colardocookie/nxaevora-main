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

So that nobody has to judge the views by eye, the probe measures itself. Every 61 runs (not a multiple of the eight jitter offsets, so the readings see all of them) it samples 96 x 54 pixels of
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
`sharpness` is the gradient length of the picture as shown (after the sharpen, when on) over that of the frame as
drawn, summed over the sampled pixels on brightness brought under 1. The resolve smooths edges, which lowers it, and
so does a blur of the whole picture. It compares options on the same scene, not runs of different scenes.

The viewport gate. The resolve and the jitter hold only while the scene's viewport is the whole target and the
same as the frame before. This is decided once a frame, at the first draw that binds the scene camera's block and
draws into the scene's depth (the depth pass), from that draw's own viewport. Otherwise the whole frame is drawn
without jitter and left as drawn: no resolve, no sharpen, and the frame after it has no history. The same goes for
a frame whose G-buffer viewport is not the one decided on, whose object vectors are given up too. A draw that binds
the block before the frame's first draw into the scene depth cannot be tied to the scene, and no two draws of a
frame may differ in jitter: such a frame has no jitter at all and is left as drawn. All of these are counted and logged
(`no jitter and no history this frame`, `frame left as drawn`, `is not the one the jitter was decided on`, `binds
the scene camera's block before`). A scene drawn smaller than its target, as the game's own dynamic scale does, is
not supported yet.

The camera's vectors have both ends without jitter, as the objects' have: the pixel's place as drawn minus the
frame's jitter, and that point under the camera of a frame ago.

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

Any other value leaves an option off, so a run can name each one either way: `NEAREST=0`, `HISTORY=linear`,
`CLAMP=minmax`, `BLEND=fixed`, `LUMA=0`, `SHARPEN=0`, `MIPBIAS=0`, and `SNX_ARCEUS_TEMPORAL_VECTORS=camera`,
`SNX_ARCEUS_TEMPORAL_VIEW=0`, `SNX_ARCEUS_TEMPORAL=off`. The first `arceus-temporal` lines of a run state every
option as it runs.

The formulas are in `gpu/vk/temporal_math.h` with a host test (`tests/temporal_math.c`), and again in GLSL in
`shaders/temporal_resolve.frag`. None of the options has been played.

## Object vectors (`SNX_ARCEUS_TEMPORAL_VECTORS=object`)

With `probe` or `taa`. The camera vectors are right for what stands still. An object that moves gets its own:

- Each G-buffer draw binds the object's constant block (bank 6, its world matrix at +0). The matrix is kept in a
  table by the block's address, with the draws that bound it (vertex and index buffer addresses, index count, first
  index, vertex program). The game writes many blocks to two slots in turn, one bound size apart, in step with the
  camera's block.
- Which slots are such a pair is learned. Two slots are a pair once exactly one of them was bound in each of 4
  frames in a row, in turns, by the same draws. This is judged at the end of each frame from the complete frame. A
  pair is given up at once, and learned again, when both slots are bound in one frame, when the draws change, when
  a turn is missed, or when a slot's matrix is bit for bit its own of two frames ago and not the other slot's (two
  things at rest). A slot bound by the same draws 4 frames in a row takes no turns and is its own pair.
- Only an established pair gives a matrix of a frame ago. Every other draw keeps the camera's vector, counted by
  reason. So an object has no vector of its own for its first 4 frames.
- When the matrix differs from that one, and by less than 5 units of position, the draw is made once more right
  after the game's: the game's vertex program and state, a fragment program of ours, the vector target as its only
  colour target and the scene depth tested for equal and not written. So it covers the pixels the object owns.
- After the G-buffer the pairs the frame's extra draws relied on are checked again against the frame's draws. When
  one broke, the frame's object vectors are given up and the camera's stand (counted).
- The fragment program takes the pixel and its depth and one matrix per draw, composed in double: this frame's clip
  as drawn, without the jitter, to the world, to the object, to its world a frame ago, to last frame's clip. The
  vector is the unjittered position now minus the position before. Blue keeps the depth it was drawn at.
- The camera vector pass takes an object's vector where the scene's final depth still is that depth, and the camera
  vector elsewhere. So something drawn in front later keeps its own.

Skinned characters. The game skins a character's vertices with transform feedback into a buffer of positions in
the object's own space, at the same address every frame, and the character's G-buffer draw reads that buffer. After
each such draw its positions are copied into a buffer of ours, two per address in turn. A G-buffer draw whose position
stream is such an output (three floats, 12 bytes a vertex) is then drawn again with a vertex program of ours: the
position now from the game's binding, the position a frame ago from our copy, each through its own world and
view-projection matrix. That program is not the game's, so its depth is not bit for bit the game's: the scene depth
is sampled and a fragment counts when it is within 0.3 % of it. A character whose first frame it is, or whose buffer
was not written the frame before, falls back to the motion of its world matrix. So does one whose capture of this
frame is not as long as last frame's (another mesh may have taken the address), and one whose draw reads a vertex
past last frame's copy. A character whose block has no established pair is not drawn again at all.

The address does not say whose positions a copy holds: another character may capture into it. So each copy is
tagged with the G-buffer draws that read it, by their block's pair and signature, and a draw takes last frame's copy
only when that copy carries its tag and the address was captured exactly once in both frames. An address captured
more than once gives no skinned vector (counted). A second capture that arrives after a draw already took the copy
gives up the frame's object vectors.
Every capture into an address is counted before anything can fail, and a copy counts only while it holds the bytes
of its frame's last capture: a capture that was too large to keep, or whose buffers could not be made, leaves the
content unknown for that frame and the next.
The copies live in a table whose slots addresses share. A slot's age is its newest capture's, copied or not, and
only a slot without a capture for 600 frames is given to another address. When none is free the new address has
no record (counted). Buffers that cannot grow leave their slot as it was.

Limit. The renderer does not see which game object wrote a block, so a pair is a guess that 4 frames of evidence
support. Two different things with the same mesh that swap visibility every frame and move would be taken for one
object. One object that truly moves there and back in two frames is taken for two things at rest and keeps the
camera's vector. Instanced draws have no vectors of their own.

The depth an object's point had a frame ago goes with its vector (the object target's alpha, the vector image's
blue), and the resolve expects that depth of the history there. The camera's reprojection does not know the object
moved.

Not drawn again: objects that did not move, new objects, instanced draws, draws with tessellation or a geometry
stage, draws under a predicate, draws without depth write unless they test for equal, and more than 256 a frame.
Each new combination of state makes one
pipeline, compiled when first needed.

`arceus-temporal-objects` every 600 frames: the G-buffer draws with an object block, those without a matrix of a
frame ago by reason (nothing bound a frame ago, pair still being learned, another draw than the pair's, at rest
alone, jumped too far, table full, not judged), those with one, moved, drawn again, over the cap, pipelines made,
what the end of the frames judged (pairs learned, slots without turns learned, pairs broken, of those at rest
alone, frames whose object vectors were given up), and for the skinned characters: buffers kept, drawn again,
without last frame's buffer, with a capture of another length, reading past last frame's copy. One example a
second has its own line: a jump, a pair that broke after its draw, and a move of more than 100 px on screen while
the camera's own is under 20 px.

In the probe the extra draws write their number of the frame where the resolve would expect the depth before, and
the numbers' image carries it. `arceus-temporal-long` then names, for sampled pixels whose object vector is at the
990 px cap, the draw that left it: rigid or skinned, its block, its feedback output, where its matrix of a frame
ago came from and how far its origin moved on screen.

## Rejected object vectors (`SNX_ARCEUS_TEMPORAL_REJECT=1`)

Only with object vectors, off unless set to `1`. An object's vector says where the object's point was a frame ago,
and the renderer guesses which matrix that was (see the limit above). History is refused where the object's own
motion exceeds a bound, since identity cannot be verified there: where an object's vector differs from the
camera's vector of the same pixel by more than an eighth of the scene's height in pixels, the pixel is rejected.
It shows the frame as drawn and takes no history. Valid history of fast objects is lost too. The camera's part is
taken off first, so a fast pan alone rejects nothing. An object's vector is kept only with a camera vector to hold
it against and finite numbers throughout: anything else, a NaN included, is rejected.

The vector pass decides it and writes a class into the motion image's alpha: 0 the camera's vector, 1 an object's,
2 rejected. The objects' target is not changed. The resolve reads the class of the pixel and, with `NEAREST=1`, of
the pixel whose vector it would take. If either is 2 it returns the frame's colour before any history is fetched
and marks the new history as dropped there. A neighbour's good vector never restores a rejected pixel's history.
The rule is `temporal_vector_class` and `temporal_current_only` in `temporal_math.h`, host-tested.

The first lines of a run say whether it is on. The numbers count rejected pixels only in the sampled frame of the
numbers, not every frame. The `arceus-temporal-stats` line gives two counts after `rejected_sampled`: `of_all`,
of every sample, and `of_eligible`, of the samples the line's shares are taken of (not sky, the vector inside the
picture). Samples rejected without a finite distance from the camera's vector are counted on their own and never
enter `largest_finite_distance`, which the shader caps at 59000 px.
In the probe nothing is resolved, the class is still written, and the wrong share on object pixels is still taken
with the object's own vector, so rejecting cannot make that number look better. The probe also names the draws:
`arceus-temporal-rejected` gives the draw, its block and signature, how many of its sampled pixels were rejected
and how many were wrong with its vector. The `long move` line is something else: the move of a draw's origin,
known every frame, and not a record of what was rejected.

What it costs is not measured: an object that truly moves faster than the bound loses its history for those
frames and shows the jittered frame there.

