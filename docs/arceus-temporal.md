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
enter `largest_finite_distance`, which the shader caps at 59000 px and which is read back as a half float, so
it is approximate.
In the probe nothing is resolved, the class is still written, and the wrong share on object pixels is still taken
with the object's own vector, so rejecting cannot make that number look better. The probe also names the draws:
`arceus-temporal-rejected` gives the draw, its block and signature, how many of its sampled pixels were rejected
and how many were wrong with its vector. The `long move` line is something else: the move of a draw's origin,
known every frame, and not a record of what was rejected.

What it costs is not measured: an object that truly moves faster than the bound loses its history for those
frames and shows the jittered frame there.

## Links (`SNX_ARCEUS_TEMPORAL_LINKS=1`)

Only with object vectors, off unless set to `1`. The learned pairs look for a block's other slot one bound size
away. In play some blocks take turns between two slots that are further apart (0x300, 0x400, 0x600, 0x1b00 and
about 1 MB were seen). The pairs give those no matrix of a frame ago, so they keep the camera's vectors. A link
looks for the other slot by the draws. The pairs come first and are not changed: a link is tried only for a draw
the pairs know nothing of.

Every object block that a plain G-buffer draw binds in a frame goes into a record of that frame: its address, bound
size, matrix, and a digest of its draws, which is their number, the sum of their signatures and the first eight in
order. A block P that is bound in a frame and was not bound the frame before is linked to a block Q that was bound
the frame before and is not bound now when all of this holds: Q is the only such block with P's size and digest, P
is the only such block with Q's, both have an affine matrix and one size, neither has a matrix from the pairs, and
P is not at rest alone. Every block of the record counts as a candidate, also one the pairs serve and one whose
matrix is not affine. The counts are made over the whole record before any link is made. After four frames in a
row in which the same two slots took turns this way, a draw of the fifth frame takes the other slot's matrix from
the record of the frame before, which nothing changes any more. Its draw must be the one the other slot had at the
same place in its order, among the first eight.

A draw cannot wait for the end of its own frame. So the frames before are judged in full when the draw asks, and
the draw's own frame is judged at the end of the G-buffer, before the vectors are read. If the draw's link does
not stand then (a second candidate on either side, the other slot bound too, another digest or size, the record
not complete), the frame's object vectors are given up and the camera's stand, as for a pair. Draws into the scene
after that are seen at the frame's end and end the link from the next frame.

A record that is not complete proves nothing about a block being the only one. That is so when it runs full (4096
blocks), when the table of blocks is full or leaves a block unjudged, and when a draw with an object block comes
before the frame's camera block is known or goes into another surface at the scene depth's address. A block bound
with a size the pairs leave alone (under 64 bytes or over 64 KB) is in the record as present and not usable. Such a frame ends every link, makes none, and the turns start again.
A full table can keep it that way for up to 600 frames. The pairs are not touched by this.

Skinned copies of a linked block are tagged with both of the link's addresses, the lower and the higher, where a
pair's copies carry the pair's lower address and 0. So a link's tag is no other link's and no pair's. The copy of
the frame before a link's first use carries no tag, so that first use is refused and the next is owned.

Limits. This is a guess of the same kind as the pairs and does not close the limit named above. Equal digests are
not equal draws: two different sets of draws with the same number, sum and first eight pass. Equal draws and strict
turns are not one object: two moving things of one mesh that are seen in turns, each at one address, fulfil every
rule, and each gets the other's matrix. The host test has that case as the known limit. Two things of one mesh that
take turns in step are ambiguous and get no link. A block bound only by other passes (a shadow pass) is not seen.
A mesh whose vertex buffer itself takes turns never has the same digest twice in a row and gets no link.

Off, none of the record's code runs: every entry to it returns at once, and the gate that lets a draw into the
record (`temporal_scene_gate`) takes none.

The first lines of a run say whether it is on. `arceus-temporal-links`, every 600 frames: the draws the pairs gave
no matrix that a link gave one, and those it did not by reason (no link, still being learned, another draw, at
rest alone, records not complete), the links that reached their fourth frame, the frames whose record was not
complete by cause, and the frames whose object vectors were given up for a link.

## The scene inside a rectangle (`SNX_ARCEUS_TEMPORAL_RECT=1`)

With `SNX_ARCEUS_SCENE_SCALE` below 1 (`docs/arceus-timing.md`) the game draws the scene into the top left
rectangle of its targets, which keep their size. Without this switch the temporal code takes the whole target only
and leaves every such frame as drawn. With it, in `probe` and in `taa`, a frame goes through the rectangle's gate
(the rules are pure functions in `temporal_math.h`, the wiring is in `gpu/vk/temporal.c`):

- The frame is decided at its first draw that binds the scene camera's block, from that draw's viewport alone
  (`rect_decide`). It is drawn with jitter only when that viewport is the one of the frame before and that frame
  was confirmed. A draw that binds the block and does not draw into the scene's depth, or a block address first
  seen in this frame, leaves the frame without a decision in its favour. A scene block bound at that draw is
  read too, vertex stage first: it can only refuse the frame.
- The frame is confirmed at the first draw into the scene's depth whose vertex or fragment program reads the
  scene block (bank 3) as far as the used fraction at +0x628, by what the translator recorded of the program
  (`rect_watch`). The fraction bound there must belong to the viewport and be the one of the frame before. A
  stage that reads the block and has none bound, two stages that differ, or a fraction that does not fit leave
  the frame as drawn, and a later draw does not mend it.
- Every later scene draw until our passes is held to the frame's viewport and, in each stage that reads the
  block, to the confirmed fraction. A scene draw is one whose depth target is the scene's depth or whose colour
  targets include the scene colour. A draw that disagrees is logged with its fragment program and both viewports.
- A scene draw that is completed before the frame's decision counts in the same way (`rect_note`): the first
  that reads the scene block is the confirming draw, and every one must have the decision's viewport. Such a
  frame is never drawn with jitter. It can only make the next frame's candidate.
- Our passes run only for a frame that was accepted and confirmed (`vk_temporal_before`): vectors, resolve,
  sharpen, numbers and views. Any other frame is left as drawn and gives the next one no history.
- A frame drawn with jitter must not stay unresolved frame after frame. If our passes cannot be made or run in
  such a frame, or a draw cannot be given its jittered block, the gate accepts no frame any more in the session
  and one line names what failed (`rect_fail`). If our passes are not reached at all in such a frame, the start
  begins again, and after 8 such frames in a row the gate stops as well. Only view 4 shows a jittered frame
  unresolved on purpose.
- What comes after the resolve does not touch the frame or its history and does not fail in silence: a sharpen
  that cannot be made is off for the rest of the session, and so are the numbers and the views, each with one
  line. A view that stood in for the picture then gives way to the resolved picture: the view asked for is shown
  once more in the failing frame, the resolved picture from the next. An
  object pass that cannot get a program, its target, room for a block, a pipeline or a set gives up the frame's
  object vectors whole and switches them off for the rest of the session, with one line: the camera's vectors
  alone remain. A draw that merely has no matrix of a frame ago is not that, and falls back by itself.
- The passes write the rectangle only and read nothing outside it. A history centre outside it fetches
  nothing, in the resolve and in the numbers. The history, and the copy of the frame before that the numbers
  and the error view use, are used only when they were made from the same images, sizes, rectangle and fraction.
  Without such a copy a frame has no numbers and no error view.
- The rejection bound is an eighth of the rectangle's height, the numbers' lattice spans the rectangle, the
  jitter has ceil(8 x W x H / (w x h)) offsets, and without `SNX_ARCEUS_TEMPORAL_MIPBIAS` the scene draws of a
  jittered frame get a level bias of log2(w / W), the same for every draw of the frame: the renderer has the
  frame decided before the deciding draw's samplers are chosen. A number in that switch, 0 included, turns the
  automatic bias off. The options line says which: automatic, set to a value, or none. A value that was set is
  given, like the automatic one, only in a frame the gate accepted at its first draw (the first acceptance,
  not the later confirmation or the resolve): a frame without jitter has no use for sharper texture levels.
  That is so with this switch only. Without it a set value is also given in the frames the whole-target gate
  does not accept, since that gate decides a frame only after its first draw's bias was asked for: a few
  frames at load in the runs so far, which is what was seen and no bound. And no promise of one bias for a
  whole frame follows from this: a set value goes by the depth target's surface, the automatic one by its
  address, so a surface made anew at the same address in mid-frame can still give two biases in a frame.

The game draws a viewport of the target's size times the scale as it comes, without rounding. A scale that does
not give whole pixels, `0.6667` in a target of 3840 by 2160 for one, ends inside a pixel, and no frame of it is
taken. The log says so once and lists the scales `p/q` with q up to 8 that give whole pixels in both axes of that
very target (`SNX_ARCEUS_SCENE_SCALE` takes `2/3`). The list is about whole pixels only.

A change of the viewport costs two frames without anti-aliasing: one gated, one that seeds the history. A change
of the fraction alone costs one more, drawn with jitter and not resolved. Lines start with
`arceus-temporal-rect:`. Not played yet.

## The camera block's projection, read only

Whenever the temporal code is on, the log has lines that start with `arceus-temporal-projection:`: the two
matrices of the scene camera's block at +0x40 and +0x80 as the game wrote them, with the frame's viewport, the
block's address and stage and the present, and what `temporal_projection_camera` makes of each (near plane, far
plane and whether it is at infinity, vertical field of view) or the first rule by which it refuses it. They are written when the
block is first seen and when the matrix at +0x80 changes: eight such events at most, each printing both
matrices, so sixteen lines at most. Nothing uses the numbers: an
upscaler has to be told them, and they are to be seen before they are relied on.

What a run showed (two cameras of one place): the matrix at +0x40 is the view, the one at +0x80 the projection
alone, with the x scale in word 0, the y scale in word 5, A in word 10, exactly -1 in word 11, B in word 14 and
exact zeros elsewhere. The stored depth is (A z + B) / (-z) for a view depth z that is negative in front of the
camera: reversed, 1 at the near plane, 0 at the far plane. The far plane is finite: near 0.1 and far 5000 at 50
degrees in the field, near 0.01 and far 120 at 30 degrees in a first scene. The decoder takes that form and no
other, without a tolerance: a scale that is not positive, a word 11 that is not exactly -1 (the same matrix
times a factor too), a word that must be zero and is not, and numbers that the floats handed to a library
cannot hold are each refused with their reason. It is to be widened only on the evidence of another camera.

## GPU time of our passes (`SNX_ARCEUS_TEMPORAL_GPUTIME=1`)

Read only, off by default, and off it adds no command and no query. With it two timestamps are written at the
pipeline's last stage around a stretch of our commands. Every 600 presents, counted at the present whether or
not our passes ran, and whenever what our passes are configured as changes, which is looked at before a frame's
first bracket begins (mode, view, options, object vectors, links, rejection, the rectangle's gate, the images'
and the scene's size), the log gives the window that ended: under which configuration it was taken and, for each
kind of stretch, how many brackets were asked for, measured, still under way, and lost by each reason, with the
median, the mean and the 95th percentile in milliseconds. The next window begins empty. A bracket still under way
at that point keeps its pair until its own submission has completed and is then retired without giving the new
window a time. Lines start with `arceus-temporal-gputime:`.

What the number is: the approximate time between two completions on the GPU's clock. Each mark is taken when
the commands before it have passed through the pipeline. Work of the stretch that ran while earlier work was
still finishing is not in it, and whatever else ran or stalled between the marks is. So it is no exclusive time
of our commands and no bound on it either way, and it holds for this GPU at this size only. The extra draws for
object vectors are among the game's own draws and are not in any stretch: their cost is not measured.

The stretches: our passes before the scene's first reader (vectors, the frame's copy, resolve, sharpen). The
numbers and the view, only in the frames that have them, so their count is small. And an empty bracket, two marks
with nothing between, printed as a baseline and subtracted from nothing.

Rules (`temporal_math.h`, host tested, and `gputime_begin`, `gputime_end`, `vk_temporal_gputime_poll` in
`gpu/vk/temporal.c`): a ring of 64 pairs of queries of its own. A bracket begins only on a free pair, which is
reset in the same command buffer just before its first mark. Both marks must be recorded in one submission,
else the pair is split and gives no time. Results are asked for where the renderer recycles a completed slot,
with their availability and without waiting, and only for a pair whose submission is known to have completed.
The difference of two ticks is taken modulo the clock's valid bits, and is taken only when the host's own clock
saw less than half a turn of that counter pass since the first mark was recorded and the difference is no
longer than that. A bracket has a handle of its own, so an end that comes late cannot close a later bracket
on the same pair, and a bracket that gets no second mark is given up by its caller at the next run of our
passes. A pair is free again only after its own end or that giving up, and when its submission has completed. What gave no time is counted by
its reason in the line. A failure switches the measure off for the session with one line and changes no frame,
and the query pool is kept until the program ends. "Completed" is read from the renderer's timeline, which the
recorder also signals for a submission that was refused. So the recorder keeps a mark, set before that signal,
for a command buffer that could not be begun or ended, a refused submission and a command pool that could not be
reset. The measure reads the timeline, then the mark, before it asks for any result or takes a pair again, and
with the mark set, or a timeline that cannot be read, it is off for the session. Not run yet.

## For the upscaler step: rules and an interface that nothing uses yet

Two things are in the sources ahead of their full use.

The rules for a scene inside a rectangle of its targets (`SNX_ARCEUS_SCENE_SCALE`, `docs/arceus-timing.md`) are pure
functions in `temporal_math.h` with host tests: the active rectangle of a signed viewport, a frame's geometry
from the viewport, the fraction that belongs to a geometry, the gate with its candidate and history stamp, the
refusal of a history fetch whose centre is outside the rectangle the history was made in, the rejection bound from
the rectangle's height, the numbers' lattice, the jitter's offsets and the texture level shift. The game's corner
of the target is a parameter. The renderer uses them with `SNX_ARCEUS_TEMPORAL_RECT=1` (above). Without it the
whole-target gate stays, so under a scale below 1 it refuses every frame.

The gate works in two steps, because the frame's first draw (the depth pre-pass) has no program that reads the
scene block. At that draw the frame is decided from its viewport alone: it is jittered when the geometry is the
one of the frame before and that frame was confirmed. A scene block that happens to be bound there can refuse the
frame and nothing else. The frame is confirmed at its first scene draw whose program reads the scene block: the
used fraction must belong to the geometry and be the candidate's. Only a frame that was accepted and confirmed
is resolved and uses or writes history. A fraction that fits and is new makes the frame the next candidate
without resolving it. A bad binding at the confirming draw, a later draw with another viewport or fraction, or no
confirming draw leave the picture as drawn and start the sequence again. A frame gated at its first draw stays
gated whatever follows.

`gpu/vk/upscaler.h` is the contract for an upscaler's adapter (FSR, XeSS, DLSS), which turns its steps into one
SDK's calls. For FSR and XeSS, whose headers are MIT, the adapter can be part of the runtime and load the SDK's own
library by name. For DLSS it is a separate module. The depth it is given is the game's own where the SDK takes
that format, and a copy in a 32 bit float image only where it does not.

Its steps: `instance_extensions` without any handle, `device_extensions` with the instance and the physical
device, `create` of a context for one configuration (sizes, formats, the inputs' properties, the device) without
work for the GPU, `prepare`, which records what the context must run once into a command buffer that the runtime
submits, `evaluate` and `destroy`. A context may be used only when its preparation's submission has completed. A
change of anything in the configuration is a new context, and until that one is ready the frames of the new kind
are left as drawn. If the instance or the device cannot be made with an adapter's extensions it is made again
without them and the upscaler is off. No adapter exists yet, nothing is loaded, nothing calls the interface, and
no SDK's library is in this repository.

The numbers a frame would carry are made by `temporal_upscaler_frame`:
- the vector is where the pixel's point was a frame ago less where it is now, in pixels, x to the right and y down
  the image, without jitter. A motion texel times minus the whole target's size gives it, also for a rectangle.
- the jitter is the shift of the drawn image against the unjittered one in pixels of the rectangle, taken through
  the viewport's signed size, so a negative height turns its y.
- reset is set when no history of the frame before may be used or the camera cut. A frame that is not steady is
  not given to the upscaler at all. Eight failures in a row turn it off for the session.
The host tests work the places out by hand for a moved camera, an object moving against a still and a moving
camera, jitter over a negative height, a rectangle, a resize and a reset. They cannot show that a library reads
the signs the same way: that needs a test on a device with the library.

## Handing a library the frame's command buffer (`vk_rec_direct`), used by nothing yet

The frame's commands go through a ring to a recording thread, and an upscaler's library makes its Vulkan calls
at once. `vk_rec_direct` in `gpu/vk/record.c` is the one way such a library gets the frame's command buffer, for
its prepare and its evaluate alike. It is one stretch under the renderer's lock, so that no other producer adds a
command inside it:
1. the caller's `before` queues what must come first (the end of the runtime's rendering, the transitions of the
   images handed over),
2. the ring is drained to its end, so the recording thread is at rest,
3. the recorder's failure mark is looked at again: after a failed begin, end or submission the library is not
   called,
4. the library records at once, on the frame's command buffer, which must be begun and not submitted,
5. every bound state counts as lost, so the runtime binds all of it anew.
It answers whether the library recorded, refused, was not called for the failure mark, or was not called because
the command buffer is not the frame's unsubmitted one. Only the first two mean the library was called. In the
other two `before` may have run all the same (always for the failure mark, and for the command buffer when it
stopped being the frame's while `before` ran): what it queued is recorded, and may be submitted, so whatever
that work uses stays in use. No answer means "nothing was recorded".

`tests/vk_record/run.sh` is its fixture, for a host with gcc and pthreads (it is not part of the Windows test
run). It compiles the recorder's own file as it is, with its ring, its thread, its drain and its mark, against
stand-in headers for Vulkan and the renderer, with the host's own mutex, and runs nine scenarios with and
without the recording thread, once under the address sanitizer and once under the thread sanitizer: the order
of the commands, two other producers that try to get in during the stretch, a failed begin, a failed end and a
failed submission of an earlier frame, a library that refuses and one that succeeds after it, command buffers
that are not the frame's, sixty frames that wrap the ring, and barriers with each combination of their three
arrays present or empty. Each scenario asserts the list of calls in
order and who made each, so a clean schedule under a sanitizer is not what passes it. It says nothing about a
real driver or a real library.

## An upscaler's library in the game (`SNX_ARCEUS_UPSCALER=fsr`)

EXPERIMENTAL. The self-test's result for FSR is FAIL: the edge case's budgets and case 4's comparison with a
plainly scaled frame are not met. Nothing about quality is claimed. It is off without its switch, and it is not
to be switched on in a run before its conditions hold (the conformance of the jitter, the native self-test suite
and case 7 with the validation layers installed, the passing cases as they are, and the fixture of
`vk_rec_direct`). Without the switch none of what follows runs.

At full size only: the library does the temporal resolve in place of ours, in frames whose scene is the whole
target. It needs `SNX_ARCEUS_TEMPORAL=taa` and is refused with `SNX_ARCEUS_TEMPORAL_REJECT=1`, each with one line.
An adapter that is not there, that names needs of the instance or the device, or that cannot make a context on
the game's device before any was in use, is "not available": one line, and the run goes on as without the
switch. The FSR adapter names needs of the device since a run of the self-test showed them (below), and the
game's device is not made with an adapter's needs in this piece. So with FSR the switch says "not available"
in the game until a piece of its own adds an adapter's needs to the game's device. With it on, our own resolve, sharpen, history, numbers and views never run, and the options that steer
them are without effect.

A frame, in order:
1. At the frame's first draw with the scene camera's block the rectangle's gate decides as it always does
   (whatever `SNX_ARCEUS_TEMPORAL_RECT` says). Then the upscaler decides whether the frame is drawn with jitter:
   only when the gate accepts it, its scene is the whole target at full size, its configuration is known and a
   context is ready for exactly that configuration. So no frame is jittered without an evaluate that can follow.
   The configuration is the sizes, the formats, the layer counts and the create flags, with the far plane's
   decision from the projection decoded at that draw. The depth target and the projection are this draw's. The
   colour target is the one thing taken from the frame before, which must have reached our passes, since the
   frame's own is not seen yet in the depth pre-pass. It is kept by value. One that is layered, not at its
   image's first layer or a member of a shared array is kept as one the upscaler does not take, so the decision
   after the frame it was seen in refuses jitter. The recorder's failure mark is read at every decision and at
   every hook, context on its way or not, before anything is published or counts as ready.
2. A frame left without jitter only because no context is ready gets one made at our passes' place: created,
   and its prepare recorded through `vk_rec_direct`. A frame's recycle notes when that submission has completed,
   and the context comes into use at the next gate's draw or hook, held against the configuration that frame
   is confirmed to have: at the gate's draw the one decided there, at the hook the one made from the frame's
   own colour target, the projection its draws took and its whole-target state. One made for frames that have
   moved on goes unused and the one in use stays. Where a frame has no confirmed configuration nothing is
   published, and only the recorder's mark is read. Nothing else of ours runs in such a frame.
3. In a frame drawn with jitter the vector pass runs as always. What the hook sees (this frame's own colour
   target, the depth, the projection, eight offsets, a stored depth that is the projection's own) must be what
   the frame was decided with, and every number must pass its check. The projection is the one the frame's
   draws were given: it is kept by value from the unjittered bytes each jittered copy was made from, all draws
   must have had the same one, and it must be the decision's bit for bit. Nothing is read again from guest
   memory at the hook. And until our passes run, every draw into the scene's targets whose program reads
   constant bank 4, however little of it, must have the frame's decided camera block bound there: any other
   block, at a slot the camera is known to use or anywhere else, leaves the frame unevaluated, with a line
   naming the program and the address (eight times at most). Nothing is inferred from where a block is. Then the scene colour, the depth and the
   motion image are handed to the library through `vk_rec_direct`, it writes a private 16 bit float image, and
   a pass of ours (`temporal_upscaled.frag`) writes that picture into the scene colour before its first reader:
   texel for texel, negative values and values that are no number raised to 0, each channel held to the largest
   finite value of the scene colour's format. Once the library was entered, whatever it answers, every image
   it was handed counts as last used by all commands, reading and writing, for whatever uses it next.
4. The reset flag is set by any of: no history of the frame before or a camera cut (the gate's rules), a frame
   before that was not evaluated (the frame time then is the nominal one of `SNX_ARCEUS_TARGET_FPS`, or a
   sixtieth), a context that came into use since the last evaluate.

What can go wrong, and what then happens. A frame drawn with jitter whose evaluate is skipped (the hook saw
another configuration, a number failed its check, the gate did not confirm the frame) or fails (the library
refused) is left as drawn, once, and the next evaluate has the reset. Eight such frames in a row, with frames
without jitter between them not counting either way, turn the upscaler off for the session. It is turned off
at once when a pass of ours that the hand-over needs cannot be made, when a submission did not run as
recorded, when a context's prepare fails, and when the timeline cannot be read eight recycles in a row. Off is
for good: no frame is drawn with jitter any more and every frame is left as the game draws it. It does not
fall back to our own resolve. Off means no publication, no jitter and no evaluate. Retirement goes on: a
context that was in use when it stopped, and one on its way once its submission has completed, are released
after the frames in flight. Only once a submission did not run as recorded, or a wait for the timeline failed
(that sets the same mark), is nothing destroyed any more, to the end of the session. A frame has one
fate in the counts, the first reported for it, and is counted in one window with its decision.

Limits of this piece: FSR only, the formats the self-test ran (a B10G11R11 scene colour, a D24S8 depth), one
layer each, the game's form of projection as two cameras of one place showed it. Any other frame is left
without jitter. The first context costs at least two frames without jitter and a create in the middle of a
frame. A change of the colour target between two frames costs one frame drawn with jitter and left as drawn.

Lines: `arceus-upscaler:` at the start (what was asked, experimental, what is without effect), for each of the
first contexts, for the first frames left as drawn and every 600th, when it turns off, and every 600 decided
frames the counts (evaluated, drawn with jitter and skipped or failed, and not jittered by reason: the gate, not
the whole target, a part of the configuration not known, no ready context, off).

`tests/upscaler_selftest_callbacks/run.sh` runs two host regressions of the validation callbacks, written by the
reviewer, for a host with python3, clang, gcc and pthreads (not part of the Windows test run). Each takes the
callback, its counts and the self-test's last lines out of `upscaler_selftest.c` as they are and puts a test
body around them: the ids, the caps, the marks for every byte value and their reading back, a character of
several bytes at a piece's edge, and the 64 combinations of case 7's last lines (the wait, a count before, a
callback after) under the address and undefined-behaviour sanitizers, and a callback thread against the counts
under the thread sanitizer.

## A run of the game under the validation layer

The runtime has its own option for the validation layer (the setting `vulkan_validation_layer`). With it on, the
game's instance now also asks for the layer's synchronisation checks, through the extension the layer lists for
its features, and the log says in lines of their own what the run really has: that the layer is enabled, whether
the synchronisation checks were asked for and the instance is made with them (and if not, which of these it
was: the layer's list does not hold the extension, the list stayed incomplete, a call failed, or there was no
memory for the list), and whether the message callback is ready or could not be made, with the result. That
no "not installed" warning is printed says nothing about a
callback that works: a run counts as made under the layer only with those lines. What the layer then does with
the checks cannot be read back. Without the option nothing changes and no line is printed.

## What the FSR adapter needs of a device

A conservative policy for the one provider that was seen, and not a complete list. In a run of the self-test
with the validation layer, the library's shader modules declared `SPV_KHR_compute_shader_derivatives` with
linear derivative groups, and the device had been made without them. As far as the SDK's published source
shows, the library picks its shaders by what the physical device has and not by what the device was made
with. So the adapter names `VK_KHR_compute_shader_derivatives` with its feature for linear groups as needs of
the device, and it is "not available" on a physical device that lacks either: a conservative refusal of a
configuration that was not seen or audited, and no claim of what the library would do there. A list of extensions or a
query of features that fails, or a list that stays incomplete, also means "not available": nothing is taken from a
part of a list. 16 bit floats in shaders are a core feature that whoever makes the device turns on where the physical
device has it. The self-test's device has every feature of the physical device on, that one among them as a bit of the
Vulkan 1.2 structure, and the adapter's own structure for the derivatives goes behind it in the chain and lives in the
adapter. This is for the self-test's device only. It closes no condition: the library's own storage image messages
remain.

## The upscaler's self-test (`SNX_ARCEUS_UPSCALER_SELFTEST=<adapter>`)

With this set to an adapter's name (`fsr`), the program does not load the game. `0` and `off` leave the mode off,
like no value. The switch is the first thing the
start looks at: before its options and their key files, the check for dumps, the menu, the guest's address space
and the game's code. The mode's own path takes the program's folder and the run's name from the command line,
names the folders, and makes the log the file a report of the run reads. It opens no window and no file of the
game. Without the switch the start is that one look and otherwise as before. A run whose device state becomes
unknown (a submission whose end cannot be waited for) stops using the device at once: the cases left are
reported as not run and the result is a failure. The mode makes a Vulkan instance and device of its
own without a window, through the steps the runtime will take with an adapter (`gpu/vk/upscaler_host.c`), runs
the adapter on a generated scene, writes its lines and ends the process with 0 for a pass. Every line begins with
`arceus-upscaler-selftest`. The adapter's own lines begin with `arceus-upscaler`.

The FSR adapter (`gpu/vk/upscaler_fsr.cpp`, Windows only) needs AMD's `amd_fidelityfx_loader_vk.dll` and
`amd_fidelityfx_upscaler_vk.dll` beside the program. They are the user's to put there and are never in this
repository. The API headers they are called through are in `third_party/FidelityFX-SDK`. The adapter takes the
newest version of the 3.1 line the library holds and logs what was asked for and what runs.

The scene is a function of position (`gpu/vk/upscaler_selftest.h`): a far background with a checker, slanted
edges and a fine grid, and a nearer patch with its own pattern. Its true picture at the output's size is that
function averaged over 8 x 8 points of a pixel. A frame's input is what a rasteriser draws: at a pixel's centre,
the scene's point at the centre's place less the frame's perturbation, in the game's formats (B10G11R11 colour,
D24S8 depth). That file shares no arithmetic with the conversion in `temporal_math.h` or with an adapter, so a
mistake in those cannot repeat itself in the test. The inputs of a run are made once. A variant of a run changes
only the numbers handed to the adapter.

The cases, each run with a viewport of negative and of positive height (render 320 x 180, output 480 x 270):
1. Jitter alone over a still pattern, twice the offsets. The four combinations of the jitter's signs: the
   configured one must have strictly the lowest error. The jitter's margin is the edge case's (below), since
   this pattern does not tell the jitter's signs apart by much. The error must be 1.25 times lower than that of
   one frame scaled plainly. The change between frames over the last cycle of offsets must be at most half of
   that over the first.
2. A translation that differs in both axes. All sixteen combinations of the jitter's and the vector's signs:
   the configured one strictly the lowest, and the three that turn vector signs only at least 1.25 times it.
3. A patch moving another way over a still background. The four combinations of the vector's signs, judged
   inside the patch. In the band the patch left, taken over every evaluated frame after the first cycle of
   offsets, the configured run must beat the run with the depth convention turned, by the margin. The band's
   pixels are counted and logged, and under 500 the case fails as a test.
4. The images' size changes in mid-run: the frames until the new context is ready are skipped. Then the still
   pattern is measured again as in case 1: the error against one frame scaled plainly, and the settling. And
   the picture after the change is held against the picture of a fresh context given the same frames at the new
   sizes: at most 0.005 apart, a budget fixed before any such data. Two fresh contexts against each other give
   the noise. A control, a fresh context whose images are one offset ahead of the jitter reported for them, must
   be more than 0.005 and more than the noise away from the fresh one, or the case has decided nothing.
5. A new pattern with the reset flag: the output of that frame against a context that never saw the old pattern,
   at most 0.01 apart. Without the flag it must be at least 0.05 apart, or the control saw nothing.
6. The content at 40, 30 of larger images of other proportions, with padding that is not the scene's, against the
   same content at the first texel of images of its own size: at most 0.01 apart.
7. No message of the validation layer, with its synchronisation checks on, in any case. Without the layer
   installed the case is not run, which is not a pass. The self-test sees only the loader's list of layers, and
   says how it read it: whole and the layer is not in it, in part (asked three times, still incomplete, so
   whether the layer is installed is not known), or not at all (a call failed, with its result, or the list
   was not asked for since the count failed or there was no memory for it, and then no result is printed for a
   call that was not made). A fourth
   state is its own: the layer was listed and asked for, and its message callback could not be made, said
   with that call's result. The device's line and case 7's line say which of the four it is. Then each layer
   listed is printed on a line of its own, 64 at most with a line for the rest, and the values of
   `VK_LAYER_PATH`, `VK_ADD_LAYER_PATH` and `VULKAN_SDK` as the process sees them. Case 7 is not run in all
   four.
   With the layer on, every callback at warning or above is counted and printed whole: its number, its
   message id's name and number, and its text in pieces of a log line's length, for the first 256 callbacks and
   up to 4096 characters of a text, each cap said when it is reached. At the end one line gives the callbacks
   received, printed, cut, not printed and without an id's name, one line a message id gives its count (64 ids,
   and a line when that table is full), and a last line says whether the counts add up to the callbacks
   received. A message id is its whole name and its number: a name of 128 characters or more is not
   classified, is counted in a line of its own and is printed cut, said as cut, with its length. Only
   printable ASCII is printed as it is. A line break, another control character and every byte outside ASCII
   (so each byte of a character of several bytes) is written out as a visible mark (a backslash with n, r or
   t, or with x and two hexadecimal digits), a backslash of the text itself is doubled, and nothing is
   dropped: a printed line reads back to exactly one text, and no piece can cut a character in two. So every piece
   is one log line with the self-test's prefix, and the callback's first line says how many marks the text and the
   name have. The counts at the end are taken at one moment, under the lock the callbacks use, after a wait for the
   device that is made whatever the cases before came to. The lines of the counts, case 7's line and the RESULT all
   go by that one copy. When the wait could not be made or failed, the copy is taken all the same, case 7's line
   says that it was taken without the device known to be idle, and the case fails. A callback that arrives after the
   copy is said in a line of its own and fails case 7 as well. The messenger is not destroyed in this mode, so a
   callback that arrives later still is not counted. The layer stops reporting an id after its own limit of repeats,
   so a count is what it reported.
   The self-test asks the layer to lift that limit (its setting `enable_message_limit`, false, through the layer
   settings at the instance's creation, where the layer offers them). The setting cannot be read back. Three
   facts are printed apart: that it was asked for (or which way it was not: the layer's list of extensions
   without its settings, incomplete, a failed call, no memory, or no room in the self-test's own list), the most
   callbacks one message id had, and how many callbacks hold the layer's own sentence of stopping an id, matched
   as a run printed it and not by one word of it. A count over ten shows only that ten was passed
   in this run.
   A table lists the names of variables found in the raw text of every callback received, printed or not, by the
   pattern the layer writes (the word variable and a quoted name): whole names, at most 64 of at most 63
   characters, each with the callbacks it occurs in. It is taken from texts by a pattern and is no identity of
   a resource. Beside it: the callbacks with no match, and marks for a text not looked through to its end, a
   quoted name that does not close, a name too long and a full table. When a name is not in the table, what may
   be said is "no match for this exact pattern in the callbacks received", and only when no mark is set.
   Nothing follows about the resource: a callback worded another way is not found.
The scene's depth is made from two distances (5 for the background, 1/6 for the patch) through a projection in
the game's form, depth = (A z + B) / (-z) with A = n / (f - n) and B = n f / (f - n). The cases above use a near
plane of 0.1 and a far plane at infinity, with the create flag for that: their 24 bit depths are the 0.02 and
0.6 they always had.

The native suite, for both heights of the viewport: the render size is the output's size (480 x 270), so 8
offsets, as the game at full size has it. It runs in two forms. The main one is the game's far plane as a run
showed it: near 0.1, far 5000, the create flag for a far plane at infinity off, the far plane handed as a
number. The second is the far plane at infinity with the flag on. Every change is held by one rule: the picture
after the change against a fresh context's of the destination on the same frames, at most 0.005 apart, with the
noise of two fresh contexts beside it and a control that is wrong on purpose and must be over the budget and
over the noise, or nothing is decided. In each form:
- a reset (a new pattern with the flag, against a context that saw that frame alone, the control without the
  flag),
- a change of a create property with the sizes kept (a context made without the flag for linear colours over 1,
  then one with it, the control one offset ahead of the reported jitter),
- a change of size (320 x 180 to the native size, the same control).
Between the forms, in both directions and at the same sizes: the first frames with one far plane, then a fresh
run's frames with the other, held against the destination form's fresh context with its noise and control.
In the game's form, a case with two depths: the nearer patch moving over the still background, judged as case
3 is, by the vector's signs inside the patch and by the band the patch left against the run with the depth
convention turned, by 1.25. The patch's and the background's errors are printed.
Each of these, and each form's fresh context, also prints what its session showed and fails when it is not what
the case expects: how many contexts came into use, each one's create flags (the far-plane flag on or off), the
far plane it was handed in every frame, that its first evaluate had the reset, and that the checksum of the last
depth image handed over is that of the image generated again for the expected far plane. The error against the
true picture is logged and has no margin.

The edge case, for the jitter's signs, on a picture of its own: grey, with one near-vertical and one
near-horizontal edge from 0.25 to 0.75 at a slope of 1 in 10, each in a region of 96 by 96 output pixels. Its
images are made, and their checksums logged, before any adapter is looked for, and the case runs only on images
with those checksums. Seven pictures of the same frozen images: the configured signs twice, each other
combination of the jitter's signs, the jitter reported as zero while the images are jittered ("told nothing"),
and one frame scaled plainly. An edge's profile is the mean of the pixels at each distance from it, in 32 bins a
quarter of a pixel wide, in fixed places: nothing is fitted or shifted. Each picture is held against the true
picture by its profiles and by the difference over the same pixels, channel by channel. To pass, for either
height of the viewport: on the edge across an axis the errors are in the order configured, told nothing, that
axis's sign turned. The turned sign is worse than the configured run by 0.010 at least and told nothing by
0.003, on both measures. Both signs turned is worse by 0.010 on each edge. Each of these margins is at least
five times the noise of its measure, which is the difference between the two configured runs' profiles and
pictures. The configured run beats the plainly scaled frame on both edges, and its output has stopped changing.
If told nothing is not worse, the noise is too large or a profile has a bin without pixels, the case has
decided nothing and fails as a test. The budgets were fixed before any run of the case.

A case that cannot tell its variants apart fails as a test. Every number is in the log, pass or fail. For the
log alone, each variant's error is also given by region (background, patch, the band the patch left) and by
channel, and each picture of the edge case by channel on each edge: on the first device run nearly all of the
background's error was in one channel, which the single mean did not show. The last line is `RESULT: PASS` or
`RESULT: FAIL`.

For the log alone as well: in the edge case's two configured runs every frame's inputs are printed as they are
handed to the adapter, and the adapter prints the dispatch from the SDK's own structure after it is filled,
field by field, every resource with the description and state the SDK gets and an unset one named as such (a
`trace` flag in the frame of `gpu/vk/upscaler.h`). A further line, marked as ours, has the caller's record. What a library is given is to
be known from the log before anything is concluded about what it does with it.

Two more parts are printed after the cases and judge nothing. The matched pair: the edge images with y down, and
with y up and the clip offset's y negated, are the same bytes, so the two signs of the viewport hand the
adapter identical inputs. The images are compared and what each frame handed over is compared. Only when both
runs succeeded, each handed over every frame the gate lets through, and those are the same, the two outputs'
difference is printed beside a repeated run's. Otherwise the line says which of these failed. The response table: for one axis at a time the reported
jitter is multiplied by -1, -0.5, 0, 0.5, 9/16, 0.75, 1, 1.25, 1.5, 16/9 and 2 with the images unchanged, and each
run's errors on both edges are printed. It is for looking: a lowest error among those factors on one picture
does not name the factor a library wants.

Limits. Cases 1 to 7 have run once on a device, with FSR 3.1.6, in an earlier state of the test, and failed
on the jitter's margin of cases 1 and 2, on case 4's error and on the missing validation layer. The edge case
has been built and has not run. Errors are
means over a region, so a fault in very few pixels can pass. The scene has one depth step and no transparency. A
pass says the signs and conventions agree with the SDK on this scene. It says nothing about the game's picture.
