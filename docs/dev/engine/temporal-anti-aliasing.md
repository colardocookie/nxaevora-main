<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the NXAevora contributors -->

# Temporal anti-aliasing

## Purpose

`SNX_ARCEUS_TEMPORAL=taa` adds temporal anti-aliasing to Pokemon Legends: Arceus in the Vulkan renderer. Each frame, it moves the
scene camera by a sub-pixel offset. It then resolves the scene with a history at the size of the scene. This is the `taa` pass of
[motion-vector.md](motion-vector.md), which provides the vectors, the rectangle and the projection that the pass uses. The options
are environment switches only, and there is no `.ini` setting for them. The `.ini` settings are described in
[arceus-timing.md](arceus-timing.md).

## Switches

Each option has its own switch, which is off unless it is set. This lets you compare one option at a time. The options change only
the `taa` resolve.

| Switch | Values | Default | Effect |
| --- | --- | --- | --- |
| `SNX_ARCEUS_TEMPORAL` | `taa` | unset | `taa` runs the stage and the resolve. Any other value turns the anti-aliasing off. `probe` runs the stage alone. |
| `SNX_ARCEUS_TEMPORAL_NEAREST=1` | `1` | off | Each pixel takes the vector of the nearest surface in its 3 x 3 neighbourhood. An edge and its background then follow the object in front. |
| `SNX_ARCEUS_TEMPORAL_HISTORY=catmull` | `catmull` | linear | The history colour is read with a Catmull-Rom filter, which uses five linear fetches and has no corners. The default reads it with one linear fetch. The history depth is still read texel by texel. |
| `SNX_ARCEUS_TEMPORAL_CLAMP=variance` | `variance` | min and max | The history is clipped to the frame's 3 x 3 mean, plus or minus 1.25 standard deviations, along the line to the frame colour. The default clips to the minimum and maximum instead. |
| `SNX_ARCEUS_TEMPORAL_BLEND=adaptive` | `adaptive` | fixed 0.1 | The frame's share of the blend is 0.05 when the camera stands still. It is 0.1 from two pixels of motion per frame. It rises to 0.2 where the clip changed the brightness of the history. |
| `SNX_ARCEUS_TEMPORAL_LUMA=1` | `1` | off | The history and the frame are weighted by 1 / (1 + brightness). A bright spark then neither flickers in nor smears. |
| `SNX_ARCEUS_TEMPORAL_SHARPEN=<0 to 1>` | a number | off | A light sharpen after the resolve. It reads the resolved picture from the history image and writes only the scene colour, so the history stays unsharpened. The amount falls where the neighbourhood has contrast. The result stays within the 3 x 3 minimum and maximum. The value is clamped to the range 0 to 1, and a value that is not above 0 turns the sharpen off. |
| `SNX_ARCEUS_TEMPORAL_MIPBIAS=<0 to -2>` | a number | unset | A level shift for the scene's material textures. These are mipmapped, linearly filtered textures, not render targets. The shift is added to the game's bias. One value applies to a run, and it is clamped to the range 0 to -2. A valid number, `MIPBIAS=0` included, is a set value. It replaces the automatic bias of the rectangle frame. A value that is not a number leaves the shift unset. |
| `SNX_ARCEUS_TEMPORAL_VIEW=3` | `3`, `4` | `0` | View 3 shows the frame's weight in the blend, with white where the history was dropped. View 4 shows the jittered frame without the resolve. Views 1 and 2 belong to the probe. |

The off forms are `NEAREST=0`, `HISTORY=linear`, `CLAMP=minmax`, `BLEND=fixed`, `LUMA=0`, `SHARPEN=0`,
`SNX_ARCEUS_TEMPORAL_VECTORS=camera`, `SNX_ARCEUS_TEMPORAL_VIEW=0` and `SNX_ARCEUS_TEMPORAL=off`. The first
`arceus-temporal` lines state every option. The automatic texture bias is used only when `SNX_ARCEUS_TEMPORAL_RECT=1` is set.
That switch belongs to the gate described in [motion-vector.md](motion-vector.md).

## How it works

### Jitter

Each frame uses one of 8 sub-pixel offsets from a Halton sequence. Each offset lies within half a pixel in each direction. Game
memory is not changed. A draw that binds the scene camera block (constant bank 4, at one of two addresses) gets a staging copy, in
which the view-projection matrix and the projection matrix are shifted, and the inverse view-projection matrix is shifted back. The
depth pre-pass, the G-buffer, and the lighting and forward passes see the jittered camera. Shadow maps, probes and the UI see no
jitter. The vectors and the history use the unjittered game matrices, and so does the neighbouring block that holds last frame's
matrices.

### Resolve

At the place that the probe chooses, before the first read of the scene colour, the colour is computed in linear HDR. It is the
frame's colour at one tenth, plus the history at nine tenths.

The history is fetched through the camera vector and clamped to the frame's 3 x 3 neighbourhood in YCoCg. It is dropped in these
cases: the previous depth lies outside the 3 x 3 depths of the history, with the range widened by a tenth; the fetch lands off the
screen; it is the first frame; it comes after a gap; it comes after a change of size; or it comes after a camera cut.

The game's bloom, exposure and tone mapping then run on the resolved picture.

### Numbers

The `arceus-temporal-stats` line gives the numbers for each depth band. Its fields are these.

- `dropped` is the share of pixels where the history was dropped.
- `far_from_frame` is the share of pixels where the resolved colour differs from the frame by more than a quarter of its
  brightness. Trails show up in this number.
- `frame_to_frame` is the share of pixels that differ from the previous drawn frame by more than a quarter. With a still camera,
  this number shows the jitter, because the edges change. Without jitter, it is near zero.
- `shown_frame_to_frame` compares the resolved picture with the previous resolved picture.
- `above_0.05` and `above_0.02` are the shares of pixels above those two thresholds. A difference of a quarter is rarely reached,
  so these finer thresholds show the shimmer.
- `sharpness` is the gradient length of the shown picture after the sharpen, compared with the drawn frame. It is summed over the
  sampled pixels whose brightness is under 1. The resolve and the blur lower it. Compare the options on one scene only.

### The viewport gate

The resolve and the jitter run only while the scene viewport equals the whole target and equals the previous frame's viewport. The
gate decides once per frame, at the first draw that binds the scene camera block and draws into the scene depth (the depth pass), from
that draw's viewport. Otherwise there is no jitter, no resolve and no sharpen, and the next frame has no history. The same applies to a
G-buffer viewport that is not the decided one, and the object vectors are also given up in that case.

A block bound before the frame's first depth draw cannot be tied to the scene. Also, no two draws of one frame may differ in their
jitter. A frame of either kind has no jitter and is left as drawn. The gate counts and logs these cases with the messages
`no jitter and no history this frame`, `frame left as drawn`, `is not the one the jitter was decided on`, and
`binds the scene camera's block before`.

A scene that is smaller than its target, as the dynamic scale makes it, is left as drawn. `SNX_ARCEUS_TEMPORAL_RECT=1` takes such a
frame.

Camera vectors use unjittered values at both ends. The first end is the pixel's place as drawn, minus the frame's jitter. The second
end is that same point under the previous camera.

## Log lines

- The `arceus-temporal` line is the options line. It gives the history, clamp, weight, vector, blend, sharpen and texture level bias
  options.
- The `arceus-temporal-stats` line gives the numbers described above, once a second.
- The other lines, for the stage, the readers, the rectangle, the projection and the GPU time, are described in
  [motion-vector.md](motion-vector.md).

## Limits

Without `SNX_ARCEUS_TEMPORAL_VECTORS=object`, the vectors are the camera's only. A moving character or Pokemon then drags a trail until
the history is clamped, and wind-blown grass blurs. Thin bright details can flicker at the clamp.

The resolve runs once per game frame, also outside the field. It is skipped when the composite is not found. The formulas are in
`temporal_math.h`. The GLSL shader is `shaders/temporal_resolve.frag`. Both are under `engine/src/nx/gpu/vk/`.

## Tests

- The host test `temporal_math`, in `engine/tests/temporal_math.c`, covers the resolve, jitter and stage rules.
- The shaders are `temporal_resolve.frag`, `temporal_sharpen.frag`, `temporal_stats.frag` and `temporal_view.frag`. They are in
  `engine/src/nx/gpu/vk/shaders/`.

## Current state

- The resolve is experimental and not part of a normal run. `SNX_ARCEUS_TEMPORAL` is unset by default, and so are its options.
- No run has used the resolve options. The runs described in [motion-vector.md](motion-vector.md) used the defaults.
