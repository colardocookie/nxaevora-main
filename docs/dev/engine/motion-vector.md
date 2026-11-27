<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the NXAevora contributors -->

# Motion vectors

The motion-vector stage makes the camera and object vectors for Pokemon Legends: Arceus in the Vulkan renderer. It is the
base stage. [temporal-anti-aliasing.md](temporal-anti-aliasing.md) and [ai-upscaling.md](ai-upscaling.md) use its vectors,
its rectangle and its camera projection.

## Purpose

`SNX_ARCEUS_TEMPORAL=probe` adds the stage. It finds the scene, reads the camera matrices, makes the camera vectors and
measures them. The picture does not change unless a view is chosen. `SNX_ARCEUS_TEMPORAL=taa` adds the stage and also the
resolve described in [temporal-anti-aliasing.md](temporal-anti-aliasing.md). The stage is for Pokemon Legends: Arceus only.
Every switch is an environment variable, not an `.ini` setting (see [arceus-timing.md](arceus-timing.md)). When the switch is
unset, none of this code runs.

## Switches

| Switch | Values | Default | Effect |
| --- | --- | --- | --- |
| `SNX_ARCEUS_TEMPORAL` | `probe`, `taa` | unset | `probe` runs the vectors, the numbers and the views. `taa` also runs the resolve. Any other value turns the stage off. |
| `SNX_ARCEUS_TEMPORAL_VIEW=1` or `SNX_ARCEUS_TEMPORAL_VIEW=2` | `1`, `2` | `0` | `1` draws the vectors over the scene colour. `2` draws the reprojection error. Only `probe` uses these views. The anti-aliasing uses `3` and `4`. A view that does not fit the current mode counts as `0`, which is off. |
| `SNX_ARCEUS_TEMPORAL_VECTORS=object` | `object` | camera only | Adds a vector to moving objects. Only with `probe` or `taa`. |
| `SNX_ARCEUS_TEMPORAL_REJECT=1` | `1` | off | Refuses history beyond the object-motion bound. Only with object vectors. |
| `SNX_ARCEUS_TEMPORAL_LINKS=1` | `1` | off | Finds a block's other slot by its draws. Only with object vectors. |
| `SNX_ARCEUS_TEMPORAL_RECT=1` | `1` | off | Takes a scene that is drawn into a rectangle of its targets. Only with `probe` or `taa`. |
| `SNX_ARCEUS_TEMPORAL_GPUTIME=1` | `1` | off | Read only. Measures the GPU time of our passes. |

The switch `SNX_ARCEUS_TEMPORAL_MIPBIAS` sets a texture level shift for the anti-aliasing. See
[temporal-anti-aliasing.md](temporal-anti-aliasing.md).

## How it works

### The probe

The probe looks for the composite. The composite is the draw that has one colour target that is not a float target and no
depth target, and whose textures include a `B10G11R11` float texture and a depth texture of the size of the target. These are
the scene colour and the scene depth.

At the first draw of a frame into three or more colour targets with the scene depth (the G-buffer), the probe reads the camera
block in constant bank 4. The block holds the view-projection matrix at `+0x00`, the inverse matrix at `+0xC0`, the row vectors
and the reversed depth. The probe keeps the previous view-projection matrix. The game writes the camera block to two slots in
turn, and the log says whether the neighbouring slot holds the same matrix.

From the next frame on, the probe runs just before the first draw that reads the scene colour, after the last write to it.

The vector pass writes a four-channel float image (`VK_FORMAT_R16G16B16A16_SFLOAT`) at the size of the scene. Each pixel holds
its uv coordinate minus the uv coordinate of the same world point one frame earlier. The red and green channels hold the vector,
and the blue channel holds the previous depth. The alpha channel is the class of the pixel: 0 for a camera vector and for a pixel
without a vector, 1 for an object vector, and 2 for a rejected object vector. Class 2 is written only when the rejection is on
(`temporal_motion.frag:27`, `:49`, `:50`, `:54`). An upscaler that takes only two channels gets a copy of the vector in
`VK_FORMAT_R16G16_SFLOAT`.

### Views 1 and 2

View 1 (`SNX_ARCEUS_TEMPORAL_VIEW=1`) draws the vectors over the scene colour. Red shows the horizontal part of a vector, green
the vertical part, and a little blue appears where either part is negative.

View 2 (`SNX_ARCEUS_TEMPORAL_VIEW=2`) draws this frame's colour against last frame's colour, fetched through the vectors, relative
to brightness. With a still camera, both views are black. On a pan over terrain, view 2 stays near black on static geometry and
lights up on characters, Pokemon, particles and water. The game's bloom, exposure and tone mapping then run on the view.

### Numbers

Every 61 frames, a count that is not a multiple of the eight jitter offsets, the probe samples 96 x 54 pixels. Each sample is
compared with last frame's colour twice: through the vector and at the same place. A sample is wrong when the two colours differ
by more than a quarter of their brightness. The `arceus-temporal-stats` line, written once a second, gives the mean and the longest
vector in pixels, and the wrong share in three depth bands (nearer than 10, up to 100, and beyond 100), each with and without the
vectors.

With a still camera, both shares are near zero. On a pan over static ground, the vectors are long and the share without vectors is
high, while the share with the vectors must stay low. If it does not, the depth or screen convention is wrong. The stored depth is a
translate plus a scale times the z value of the projection, as the viewport stores it. This holds for both depth modes of the guest
GPU, and the first runs log the two values and the mode.

### Object vectors (`SNX_ARCEUS_TEMPORAL_VECTORS=object`)

This option works in `probe` or `taa` mode. Camera vectors suit still things, so a moving object gets a vector of its own.

- Each G-buffer draw binds the object block (constant bank 6, world matrix at `+0`). A table, indexed by address, keeps the matrix
  and the draws that use it. A draw is described by its vertex and index buffer addresses, its index count, its first index and its
  vertex program. The game uses two slots that lie one bound size apart, and it switches between them in step with the camera.
- A pair is learned when exactly one of the two slots is bound in each of 4 frames in a row, the slots take turns, and the same draws
  are made in each frame. The judgement is made at the end of each frame. A slot is `at rest alone` when its matrix is bit for bit its
  own matrix from two frames ago and not the other slot's (`temporal_math.h:292-297`). A pair is given up at once, and learned again
  later, when both slots are bound in one frame, when the draws change, when a turn is missed, or when a slot is `at rest alone`. A slot
  bound by the same draws for 4 frames in a row takes no turns, and it is its own pair.
- Only a learned pair gives a matrix from a frame earlier. Every other draw keeps the camera vector, and the reason is counted, so an
  object has no vector of its own during its first 4 frames.
- When the matrix differs from the pair's previous matrix by less than 5 units of position, the draw is made once more, right after the
  game's own draw. It uses the game's vertex program and state, one of our fragment programs, the vector target as its only colour
  target, and the scene depth tested for equality without writing. It therefore covers the pixels that the object owns.
- After the G-buffer pass, the pairs that the extra draws relied on are checked again. A broken pair gives up the object vectors of that
  frame, and the camera vectors are used instead. The give-up is counted.
- Our fragment program takes the pixel, its depth and one matrix per draw. The reprojection matrix is composed on the host in double
  precision (`temporal.c:598-603`, used at `temporal.c:4068`). The chain runs from this frame's clip position, as drawn and without
  jitter, through the world and the object to the object's world matrix of a frame ago, and then to last frame's clip. The blue channel
  keeps the depth at which the point was drawn.
- The camera vector pass takes an object's vector at each pixel where the final scene depth equals that object's depth. Elsewhere it
  takes the camera vector, and a later draw in front keeps its own vector.

Skinned meshes need a separate treatment. The game writes the vertex positions with transform feedback into an object-space buffer
that keeps the same address in each frame. After each such write, we copy the positions into our own buffer, with two copies per
address, used in turn. A draw whose position stream comes from such output (3 floats, 12 bytes per vertex) is drawn again with our
vertex program. The current pass reads the game's binding, and the previous pass reads our copy. Each pass uses its own world and
view-projection matrix. The depth is not bit-exact, so a pixel matches when it is within 0.3 % of the scene depth. The world matrix is
used instead on the first frame, when there is no previous buffer, when the capture is shorter than the capture of last frame, and
when the draw reads past the last copy. Without a pair, the extra draw is not made.

Each copy carries a tag made from the G-buffer draws that read it, by pair and by signature. The last copy is taken only when its tag
matches and the address was captured exactly once in both frames. More than one capture gives the draw no skinned vector, and the case
is counted. A second capture after a copy was taken gives up the object vectors of that frame. A copy counts only while it holds the
bytes of its frame's last capture. When a copy is too large, or its buffers are not made, the content is unknown in this frame and the
next. Table slots can share an address, and the age of a slot is the age of its newest capture, copied or not. After 600 frames without
a capture, the slot may move to another address. When no address is free, no record is made and the case is counted. When the buffers
cannot grow, the slot stays unchanged.

The previous depth is written with the vector: the class is in alpha and the depth is in blue. The resolve expects the previous depth
in the history, and camera reprojection does not know that the object moved.

Some draws get no extra draw: still objects, new objects, instanced draws, draws with a tessellation or geometry stage, predicated draws,
draws without a depth write unless they use an equal depth test, and draws beyond 256 per frame. Each new combination of state needs one
pipeline, which is compiled when it is first needed.

In the probe, the extra draws write their frame number where the resolve expects the previous depth, and the numbers image carries it.
When sampled pixels have an object vector at the 990 px cap, the `arceus-temporal-long` line names the draw that left them there. It
gives the block, whether the draw is rigid or skinned, the feedback output, the source of the previous matrix and the screen origin move.

### Rejected object vectors (`SNX_ARCEUS_TEMPORAL_REJECT=1`)

This option works only with object vectors, and it is off unless it is set to `1`. History is refused where an object vector differs
from the camera vector of the same pixel by more than an eighth of the scene height, in pixels. The pixel then shows the frame as drawn,
without history, so the valid history of a fast object is lost. The camera part of the motion is removed first, so a fast pan on its own
rejects nothing. An object vector needs a camera vector and finite numbers. Any other vector is rejected, including a vector with NaN.

The resolve reads the class of the pixel, and the object target does not change. When `SNX_ARCEUS_TEMPORAL_NEAREST=1` is set, the resolve
also reads the class of the pixel whose vector it would take. If either class is 2, the resolve uses the frame colour instead of the
history, and the new history is dropped. A good vector from a neighbour never restores a rejected pixel. The rule is in
`temporal_vector_class` and `temporal_current_only` in `temporal_math.h`, and the host test covers it.

Rejected pixels are counted only in the sampled frame. The `arceus-temporal-stats` line gives `rejected_sampled` in two forms: `of_all`
counts every sample, and `of_eligible` counts the samples that are not sky and whose vector lies inside the picture. Samples without a
finite camera distance are counted apart and never enter `largest_finite_distance`. The shader caps the camera distance at 59000 px, and
the value is read back as a half float, so the figure is approximate.

In the probe, nothing is resolved, but the class is still written, and the wrong share on object pixels still uses the object vector. The
`arceus-temporal-rejected` line gives the draw, the block, the signature, the rejected pixels and the wrong pixels measured with the
object's vector. The `long move` line is separate: it is written once a second for a long move of a draw's origin on the screen while
the camera is still. The origin move is known every frame, so it is not a record of a rejection (`temporal.c:2295-2318`). The cost of
rejection is not measured.

### Links (`SNX_ARCEUS_TEMPORAL_LINKS=1`)

This option works only with object vectors, and it is off unless it is set to `1`. Some blocks take turns between two slots that lie
farther apart than one bound size. The bound sizes seen were 0x300, 0x400, 0x600 and 0x1b00, and a distance of about 1 MB was seen as
well. Pairs cannot find the other slot of such a block, so the block keeps the camera vectors. A link finds the other slot by the draws.
Pairs are applied first and are not changed, and a link is tried only for a draw that the pairs know nothing about.

Each frame, the record keeps every object block that a plain G-buffer draw binds, with its address, its bound size, its matrix and a
digest of its draws. The digest holds the number of draws, the sum of their signatures and the signatures of the first eight draws in
order.

A block P that is bound in this frame and was not bound in the frame before is linked to a block Q, which was bound in the frame before and
is not bound now. The link is made only when all of these hold. Q is the only such block whose size and digest match P, and P is the only
such block whose size and digest match Q. Both blocks have an affine matrix and one bound size. Neither block has a matrix from a pair. P
is not `at rest alone`. Every block of the record counts as a candidate, including a block that a pair already serves and a block whose
matrix is not affine. All counts are made over the whole record before any link is made.

After four frames in a row in which the same two slots took turns in this way, a draw in the fifth frame takes the other slot's matrix from
the record of the frame before, which is no longer changed. The draw must be the one that the other slot had at the same place in the order,
among the first eight draws.

A draw cannot wait for the end of its own frame. The frames before are judged in full when the draw asks for it, and the draw's own frame is
judged at the end of the G-buffer pass, before the vectors are read. If the link does not stand at that point, the object vectors of the frame
are given up, as for a pair. A link does not stand when a second candidate exists on either side, when the other slot is bound too, when the
digest or the size differs, or when the record is not complete. Draws into the scene after the G-buffer pass are seen at the end of the frame,
and they end the link in the next frame.

A record that is not complete proves nothing about whether a block is the only one of its kind. A record is incomplete when it is full, with
4096 blocks, when the table of blocks is full or leaves a block unjudged, when an object draw comes before the frame's camera block is known,
or when a draw goes into another surface at the address of the scene depth. A block whose size the pairs exclude, under 64 bytes or over
64 KB, is in the record as present but cannot be used. Such a frame ends every link, makes no new link, and restarts the turns. A full table
can keep the record in this state for up to 600 frames. The pairs are not affected.

Skinned copies of a linked block carry a tag with both link addresses, the lower one and the higher one. The copies of a pair carry the lower
address of the pair and 0, so a link's tag never matches another link's tag or a pair's tag. A copy made before the first use of a link
carries no tag, and that first use is refused.

When the option is off, no record code runs. Every entry point returns at once, and `temporal_scene_gate` takes no draw.

### The scene inside a rectangle (`SNX_ARCEUS_TEMPORAL_RECT=1`)

When `SNX_ARCEUS_SCENE_SCALE` is below 1 (see `docs/records/game/arceus-timing.md`, "Scene scale"), the game draws the scene into the top
left rectangle of its targets, which keep their full size. Without this switch, the temporal code takes only frames that fill the whole
target and leaves the other frames as drawn. With the switch set, in `probe` or `taa` mode, the frame goes through the rectangle gate.
The rules are pure functions in `temporal_math.h`, and the wiring is in `gpu/vk/temporal.c`. The game's corner is passed to the rules as a
parameter.

- The gate decides at the first draw that binds the scene camera block, from that draw's viewport alone (`rect_decide`). Jitter is applied
  only when that viewport is the previous frame's viewport and that frame was confirmed. A block bound without a draw into the scene depth,
  or at a new address, can only refuse.
- The frame is confirmed at the first draw into the scene depth whose vertex or fragment program reads the fraction at `+0x628` in constant
  bank 3. The translator record tells which program reads it (`rect_watch`). The fraction must belong to the viewport and be the previous
  frame's fraction. When a reading stage has no block bound, when two stages read different values, or when the fraction is wrong, the frame
  stays as drawn, and it is not repaired later.
- Every later scene draw before our passes must use the frame's viewport and the confirmed fraction in each stage that reads it. A scene draw
  is a draw into the scene depth or the scene colour target. A disagreement is logged with the fragment program and both viewports. A scene
  draw that completes before the decision counts in the same way (`rect_note`). Such a frame never jitters, and it only makes the next frame
  a candidate.
- Our passes run only for a frame that the gate accepted and confirmed (`vk_temporal_before`). Any other frame stays as drawn, and the next
  frame gets no history.
- A jittered frame must not stay unresolved. When the passes do not run, or there is no jittered block, the gate accepts no frame again, and
  one line names the failure (`rect_fail`). After 8 such frames, the gate stops as well. Only view 4 shows a jittered frame unresolved.
- After the resolve, no failure is silent. When the sharpen is not made, it is off for the session, and the numbers and the views are off
  too, with one line each. A view that stands in for the resolve gives way: it is shown once more in the failing frame, and the resolved
  picture is shown in the next frame. When the object pass lacks a program, a target, room in the block, a pipeline or a set, the object
  vectors of the frame are given up, and they are off for the session, with one line logged. A draw without a previous matrix falls back to
  the camera vector.
- The passes write only the rectangle and read nothing outside it. When the history centre lies outside the rectangle, the fetch reads nothing,
  for the resolve and for the numbers. The history, and the copy of the frame that the numbers and the error view use, are used only when they
  come from the same images, sizes, rectangle and fraction.
- The rejection bound is one eighth of the rectangle height. The lattice of the numbers covers the rectangle. The number of jitter offsets is
  `ceil(8 x W x H / (w x h))`, where W and H are the target size and w and h are the rectangle size. For a scale f, it is `ceil(8 / f^2)`. At the
  scales 3/5, 3/4 and 4/5, the count is one more than the truncated count in the guidance of AMD and NVIDIA. The SDKs do not take this count as
  an input.

Texture level bias. When `SNX_ARCEUS_TEMPORAL_MIPBIAS` is not set, the scene draws of a jittered frame get the automatic bias log2(w / W), the
same for all draws. Here W is the target width and w is the rectangle width (`temporal_math.h:1069`). The frame is decided before the samplers
of the deciding draw are made. A number, 0 included, replaces the automatic bias, so `MIPBIAS=0` is a set value. The options line says whether
the bias is automatic, set or none. A set value is used only in a frame that the gate accepted at its first draw. Without
`SNX_ARCEUS_TEMPORAL_RECT=1`, a set value also applies to the frames that the whole-target gate rejects, and the runs so far show a few such
load frames, with no known bound on their number.

One bias per frame is not promised. A set value is chosen per depth target surface, and the automatic bias per address. A surface made again at
the same address can give two biases. A frame of the upscaler that is not jittered gets no bias from our code (`gpu/vk/temporal.c:1890`).

The game sets the viewport to the target size times the scale, with no rounding. A scale that does not give whole pixels ends inside a pixel.
An example is `0.6667` on a 3840 by 2160 target, and no frame is taken for such a scale. The log says so once, and it lists the scales `p/q`,
with q up to 8, that give whole pixels in both axes of that target. `SNX_ARCEUS_SCENE_SCALE` takes such a fraction, for example `2/3`.

After a viewport change, two frames run without anti-aliasing: one is gated, and the other seeds the history. After a change of the fraction
only, one more frame runs, which is jittered but not resolved. The lines start with `arceus-temporal-rect:`. An unattended run at scale 0.75
produced the reports 20261114-104624-report-5ed4ae and 20261114-105726-report-d751de.

### The camera block's projection, read only

While the temporal code is on, the `arceus-temporal-projection:` lines show the two matrices at `+0x40` and `+0x80`, as the game wrote them.
Each line also shows the viewport, the block address, the stage, whether the block is present, and what `temporal_projection_camera` derives
from each matrix: the near plane, the far plane, whether the far plane is infinite, the vertical field of view, or the first reason for a refusal.
The lines are written when the block is first seen and again when `+0x80` changes. There are at most eight such events, so at most sixteen
lines. Nothing uses these lines yet, and an upscaler may be given these values only after they are checked in the logs.

Two cameras of one place showed this layout. `+0x40` holds the view matrix, and `+0x80` holds the projection matrix alone. In the projection
matrix, the x scale is in word 0, the y scale is in word 5, A is in word 10, the value -1 is in word 11, and B is in word 14. All other words
are zero. The stored depth is (A z + B) / (-z), where z is the view depth and is negative in front of the camera. The depth is reversed: it is
1 at the near plane and 0 at the far plane. The far plane is finite. One camera had a near plane of 0.1 and a far plane of 5000 at 50 degrees.
In the first scene, a camera had a near plane of 0.01 and a far plane of 120 at 30 degrees.

The decoder accepts only this form, with no tolerance. It refuses, and gives the reason, when the scale is not positive, when word 11 is not
exactly -1 (which also refuses the same matrix multiplied by a factor), when a word that must be zero is not zero, or when a number cannot be
held by the floats that a library receives. It is to be widened only on the evidence of another camera.

### GPU time of our passes (`SNX_ARCEUS_TEMPORAL_GPUTIME=1`)

This option is read only and off by default. When it is off, it adds no command and no query. Two timestamps at the last stage of the pipeline
bracket our commands. Every 600 presents, counted whether or not our passes ran, and at each configuration change, the log gives the window
that has ended. The configuration is read before the first bracket of a frame, and it covers the mode, the view, the options, the object
vectors, the links, the rejection, the rectangle gate and the image and scene sizes.

Each window line gives the configuration first. For each kind of stretch, it then gives the number of brackets that were asked, measured,
still under way and lost, with the reason for each loss, and the median, the mean and the 95th percentile in milliseconds. The next window
starts empty. A bracket still under way keeps its pair until the submission completes, and is then retired without a time. The lines start
with `arceus-temporal-gputime:`.

Each number is an approximate GPU-clock time between two completions. A mark is taken when the prior commands have passed the pipeline, and
stretch work that ran while earlier work was finishing is excluded. Other work and stalls between marks are included. The number is not
exclusive time and not a bound, and it holds only for this GPU and this size. Extra object draws are game draws, so they belong to no stretch,
and their cost is not measured. The stretches are these. The first is our passes before the scene's first reader: the vectors, the frame copy,
the resolve and the sharpen. The second is the numbers and the view, which appear only in the frames that have them. The third is the empty
bracket, two marks with nothing between them, which is the baseline. Nothing is subtracted from it.

The rules are in `temporal_math.h`, which the host tests cover, and in the functions `gputime_begin`, `gputime_end` and `vk_temporal_gputime_poll`
in `gpu/vk/temporal.c`. There are 64 query pairs. A bracket starts only on a free pair, and the pair is reset in the same buffer before its first
mark. Both marks must be in one submission, or the bracket is split and no time is given. Results are asked for only where a completed slot is
recycled, with an availability check, without waiting, and only for a completed submission. The tick difference is taken modulo the valid bits,
and it is used only when the host clock saw less than half a turn since the first mark and the difference is no longer than that. Each bracket
has its own handle, so a late end cannot close a later bracket. When there is no second mark, the caller gives up at the next run of our passes.
A pair becomes free after its end or its giving up, and after the submission completes. Each case without a time is counted by its reason.

On a failure, the GPU time is off for the session, one line is logged, and the frame is not changed. The pool is kept until the program ends. A
completed submission is one that the renderer timeline reports as completed, and the timeline also signals for a refused submission. The recorder
mark is set before that signal, and it covers a buffer that was not begun or not ended, a refused submission, and a pool that was not reset. The
measure reads the timeline first, then the mark, before any result is read or any new pair is used. If the mark is set, or the timeline cannot be
read, the measure is off for the session.

The measure ran in the unattended runs of 2026-11-14, which include the report 20261114-103803-report-bdd4f2 and the two rectangle runs described above.

## Log lines

The `arceus-temporal` line gives the chosen scene colour and depth, the first reader of the scene colour, the place of the camera block, whether
the neighbouring slot holds last frame's view-projection matrix, and the first viewport and depth range. Skipped frames are counted by reason.

With `probe` or `taa`, and read only, three readers are logged in every 600th frame: the scene colour copy, the blur preparation and the composite,
identified by fragment program and by its whole 64 bit hash, as the census prints it. The line describes the first recorded draw of that frame.
Setup can repeat or be skipped, so the line is written once, at the record. A setup that was not recorded gives one line. A replaced description
before the record gives a line and a count, and then the same draw or the program's next draw is unknown. The lines start with
`arceus-temporal-readers:`, and they name the frame and the program. Each reader line gives these details:

- The draw: the vertex program, the game's viewport and scissor registers (whether the test is on or off, and the rectangle in game pixels), the
  number of colour targets, and the address and size of the first target.
- For each sampled texture, up to 16 textures: the address and size, and the Vulkan sampler as it was made. The sampler line gives the filters, the
  address modes on both axes, the level range and bias, the normalised coordinates, the compare mode and the anisotropy. The renderer limits the
  levels when there is no filter between levels, swaps a reversed range, maps two address modes depending on the device, and can force nearest
  filtering, so the line does not show the game's header as such. If no sampler is bound at setup, the line says "not known". The line also gives
  the game's sampler header as numbers and words, and words 4 and 5 of its texture header.
- For the fragment stage's scene block: thirteen words, and the used fraction at `+628` and `+62C`. If the block is not bound or too short, the
  line says so with "not bound" or "too short".
- For the vertex stage's scene block: the values at `+628` and `+62C`, and the first sixteen words of vertex bank 1.
- The recorded viewport and scissor, after the render scale and the window origin are applied, in image pixels, with the game's registers and the
  effective values side by side.

These lines change no draw.

| Prefix | What it says |
| --- | --- |
| `arceus-temporal-stats` | Once a second: the mean and the longest vector, and the wrong shares by depth band. |
| `arceus-temporal-objects` | Every 600 frames: the object draws without a previous matrix, by reason (nothing bound before, pair learning, another draw, at rest alone, jumped too far, table full, not judged). Draws with a previous matrix, by moved, drawn again, over the cap, or pipelines made. The frame-end judgements and the skinned buffer counts. One example a second: a jump, a pair broken after a draw, or a screen move above 100 px while the camera moved less than 20 px. |
| `arceus-temporal-long` | The 990 px object-vector cap: the draw that left the pixels at the cap. |
| `arceus-temporal-rejected` | The draw, the block, the signature, the rejected pixels, and the wrong share measured with its vector. |
| `arceus-temporal-links` | Every 600 frames: the draws that a link gave a matrix, and those it did not, by reason (no link, learning, another draw, at rest alone, incomplete record). Also the links in the fourth frame, the incomplete records by cause, and the frames given up for a link. |
| `arceus-temporal-rect` | Decisions and refusals of the rectangle gate. |
| `arceus-temporal-gputime` | GPU time windows. |

The first lines state every option. The viewport gate logs these messages: `no jitter and no history this frame`, `frame left as drawn`,
`is not the one the jitter was decided on`, and `binds the scene camera's block before`. The counts for the gate are in
[temporal-anti-aliasing.md](temporal-anti-aliasing.md).

## Limits

Without `SNX_ARCEUS_TEMPORAL_VECTORS=object`, the vectors are the camera's only, so a moving object has the background's vector. The depth
convention and the screen y direction come from the viewport, and view 2 checks them. Cutscenes and menus with another layout are skipped when
the composite is not found.

A pair is a guess made from 4 frames. The renderer does not see which game object wrote a block. Two things that share one mesh and swap
visibility every frame, and that move, are taken for one thing. One object that moves there and back in two frames is taken for two things at
rest, and it keeps the camera vector. Instanced draws have no vectors of their own.

A link is the same kind of guess, and it does not remove that limit. Equal digests do not mean equal draws, and two sets that share the number,
the sum and the first eight signatures pass the test. Two moving things of one mesh, seen in turns and each at one address, meet every rule and
receive each other's matrix. The host test records this case as a known limit. Two things of one mesh that take turns in step get no link. A block
bound only by other passes, such as a shadow pass, is not seen. A mesh whose vertex buffer takes turns never repeats a digest, so it gets no link.

## Tests

- The host test is `tests/temporal_math.c`, under `engine/`.
- The renderer is `gpu/vk/temporal.c`. Its pure rules are in `gpu/vk/temporal_math.h`. Both are under `engine/src/nx/`.
- The shaders are `temporal_motion.frag`, `temporal_motion2.frag`, `temporal_object.frag`, `temporal_skin.vert`, `temporal_skin.frag`,
  `temporal_view.frag` and `temporal_stats.frag`. They are under `engine/src/nx/gpu/vk/shaders/`.

## Current state

- The probe, the object vectors, the links, the rejection and the rectangle gate are experimental. They are not part of a normal run. The runs
  are the reports named above.
- `SNX_ARCEUS_TEMPORAL` is unset by default, and so are all of its options.
