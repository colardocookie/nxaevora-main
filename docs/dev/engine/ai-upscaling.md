<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the NXAevora contributors -->

# AI upscaling

## Purpose

`SNX_ARCEUS_UPSCALER` gives the frame to a library (FSR, XeSS or DLSS), or to our spatial hand-over, in place of the temporal
resolve. The base of this feature is [motion-vector.md](motion-vector.md), which provides the vectors, the rectangle and the
projection. The option needs `SNX_ARCEUS_TEMPORAL=taa`, and it is refused when `SNX_ARCEUS_TEMPORAL_REJECT=1` is set. The feature
is experimental and works for Pokemon Legends: Arceus only. Like the other temporal options, it uses environment switches only.
See [arceus-timing.md](arceus-timing.md).

## Switches

| Switch | Values | Default | Effect |
| --- | --- | --- | --- |
| `SNX_ARCEUS_UPSCALER=fsr` | `fsr`, `xess`, `dlss`, `spatial` | off | Selects a named library, or our spatial hand-over. `0`, `off` and unset all mean off. The option needs `taa`, and it is refused with `SNX_ARCEUS_TEMPORAL_REJECT=1`. |
| `SNX_ARCEUS_UPSCALER_SELFTEST=<adapter>` | `fsr`, `dlss`, `xess` | off | Runs the adapter self-test instead of the game. `0`, `off` and unset all mean off. |
| `SNX_ARCEUS_FIXED_RESOLUTION` | `1`, other | on | Needed below native size. It is on unless it is set to another value (`gpu/vk/temporal.c:333`). |
| `SNX_ARCEUS_SCENE_SCALE` | `1/2`, `3/5`, `2/3`, `3/4`, `4/5` | unset | Sets a scene scale below native size. The rectangle is the render size. See `docs/records/game/arceus-timing.md`, "Scene scale". |
| `SNX_ARCEUS_TARGET_FPS` | a number | unset | Sets the nominal frame time. It is 1 divided by the number, when that is at most 0.25 second. Otherwise, and when the switch is unset, it is a sixtieth of a second (`temporal.c:3929`, `temporal_math.h:1449`). |
| `SNX_ARCEUS_XESS_OUTPUT=b10g11r11` | `b10g11r11` | four 32 bit floats | A test override for the XeSS output format. No run uses it. |
| `SNX_ARCEUS_DLSS_JITTER_SIGN` | `--`, `-+`, `+-`, `++`, `mm`, `mp`, `pm`, `pp` | protocol (+1, +1) | Forces the DLSS jitter sign. This is an experiment. |

## How it works

### The contract

`gpu/vk/upscaler.h` defines the adapter contract for FSR, XeSS and DLSS. Each step of the contract becomes one call of the SDK. The
FSR and XeSS adapters use MIT headers. Their adapter code is in the runtime, and each loads its SDK library by name. The DLSS adapter is
a separate module. For depth, the adapter uses the game's own depth buffer if the SDK accepts it. Otherwise, it uses a 32 bit float copy
of the depth. No SDK library is in this repository.

The steps are these. `instance_extensions` is called without a handle. `device_extensions` is called with the instance and the physical
device. `create` makes one configuration from the sizes, the formats, the input properties and the device, and it does no GPU work.
`prepare` records the work that needs to run only once, into a buffer that the runtime submits. `evaluate` runs one frame. `destroy` frees
the context. A context can be used once the submission of its prepare work has completed. When the configuration changes, a new context is
made, and new frames are left as drawn until it is ready. When an instance or device is made without the adapter's extensions, it is made
again without them, and the upscaler is turned off.

`temporal_upscaler_frame` builds the numbers that a frame carries.

- The vector is the previous point minus the current point, in pixels. The x axis points right and the y axis points down. The vector has
  no jitter. The motion texel is multiplied by minus the whole target's size, and the same holds for a rectangle.
- The jitter is the shift of the drawn image minus the shift of the unjittered image, in pixels of the rectangle, through the signed
  viewport size. A negative height turns the y axis.
- The reset flag is set when no previous history may be used, or when the camera was cut. A frame that is not steady is not given to the
  upscaler at all. After eight failures in a row, the upscaler is turned off for the session.

The host tests cover a moved camera, an object that moves against a still background, a moving camera, a jitter over a negative height, a
rectangle, a resize and a reset. They cannot show that a library reads the signs in the same way. That needs a test on a device with the
library.

### The rectangle gate

The gate is the one described in [motion-vector.md](motion-vector.md). It must accept the frame at its first draw, and it must confirm the
frame at the first scene draw that reads the scene camera block. The decision is made at the depth pre-pass, which does not read the block,
so a block bound there can only refuse. A new fraction that fits makes the next frame a candidate, without a resolve. A bad confirming
binding, a later draw with another viewport or fraction, or the lack of any confirming draw leaves the picture as drawn, and the sequence
starts again. A frame that the gate refused at its first draw stays refused.

### Handing a library the frame's command buffer (`vk_rec_direct`)

Frame commands go to a recording thread through a ring. A library's Vulkan calls must go at once, so they cannot wait in the ring.
`vk_rec_direct` in `gpu/vk/record.c` is the one path for both prepare and evaluate. It runs one stretch under the renderer lock, in these
steps.

1. The caller runs `before`, which queues the first work: the end of the runtime's rendering, and the image transitions.
2. The ring is drained, and the recording thread rests.
3. The failure mark is read again. After a failed begin, a failed end or a failed submission, the library is not called.
4. The library records at once into the frame buffer, which has begun and is not submitted.
5. All bound state is lost, and the runtime binds its state anew.

The answers are these. The library was recorded, or it was refused. It was not called because of the mark. It was not called because the
buffer is not the frame's unsubmitted buffer. For the first two answers, the library was called. For the other two, `before` may have run.
It always runs for the mark case, and it also runs when the buffer has stopped being the frame's. So the queued work is recorded and may be
submitted, and its images stay in use. Without an answer, nothing is recorded.

`tests/vk_record/run.sh` is a fixture that runs on Linux with gcc and pthreads, not on Windows. It uses the recorder file as it is: the ring,
the thread, the drain and the mark. It adds stand-in Vulkan and renderer headers, and a host mutex. It has nine scenarios, each run with and
without the recording thread, under the address and thread sanitizers. The scenarios cover the call order; two other producers during the
stretch; a failed begin, a failed end or an earlier failed submission; a library that refuses and then succeeds; buffers that are not the
frame's; sixty frames that wrap the ring; and barriers with every combination of three arrays, each present or empty. Each scenario asserts
the call order and the caller. A clean schedule under a sanitizer is not what passes it. The fixture says nothing about a real driver or a
real library.

### The game's device and an adapter's needs

These rules apply only when `SNX_ARCEUS_UPSCALER` names an adapter that is built into the program. Without the switch, the game's device is
made from the same list of extensions and the same chain of features as before. Nothing is asked, and no line is printed. With the switch
set, the host asks the adapter what it needs before the device is made, and then checks the answer itself. The rules are in
`upscaler_needs.h`, which is host tested, and `upscaler_host.c` applies them.

- Each extension that the adapter names is looked up in the physical device's own list, which must have been read whole. If it is
  incomplete, the host asks again, up to three times in all. A name that is not in the list, or a list that was not read whole, is a
  refusal. The adapter may name at most eight extensions, each of at most 63 characters. The host keeps its own copies of these names.
- Two feature structures are accepted from an adapter. The first is the compute shader derivatives feature, accepted by its type and its
  two bits. The second, from version 5 of the contract, is the mutable descriptor type feature, which XeSS uses, accepted by its one bit. Any
  other type is a refusal. This includes a core feature structure, a promoted alias such as the 16 bit float features, and anything else. A
  refusal also applies when an accepted type appears twice, when the chain does not end within eight structures, and when a bit is asked for
  without `VK_KHR_compute_shader_derivatives` among the names. The NV extension of the same structure is not accepted in its place, because
  nobody has examined it. The bound of eight guards the walk against a chain that does not end.
- Each bit that is asked for is checked against the host's own question to the physical device. A bit that the device does not have is a
  refusal. Bits that are not asked for stay off.
- A need for a core feature bit is not a structure. The adapter's answer has a plain field for such bits, since version 3 of the contract. An
  adapter of another version is refused before anything of it is called. The first defined bit, which this section describes, is separate
  layouts for the depth and stencil aspects, a bit of the 1.2 features. The other bits in `upscaler.h` came with DLSS and XeSS, and XeSS's
  section below describes them. A bit that the host does not know is a refusal. The known bit is checked against the question that the game's
  device creation asks the physical device anyway. A physical device without it means the adapter is not available. The bit is turned on in
  the game's own 1.2 structure for a creation with the additions, and it is put back to the game's own value before a creation without them.
  No 1.2 structure of an adapter is ever taken or chained.
- The adapter's storage is read only, as bytes. Each field is copied out at its offset into a value of the host's own, and the host never uses
  a structure type of its own for it. What goes into the device is a structure of the host's own, placed in front of the game's chain. The
  host's own copies of the names go behind the game's list. The game's count and chain head are not changed in place.

If anything is refused, or the instance would need something, the device is made as it would be without the switch, and one line says why.
For a name that no adapter of this program has, the device creation says nothing, and the upscaler's start prints one line for that case. If
the device cannot be made with the additions, it is made once more from the game's own list and chain head, with one line, and the adapter is
not available. A record is written only after a device has been made with the additions. It holds the adapter's name, every extension it
named (also one that the game's list already had), the bits that were turned on, and the core bits. Its line reads `arceus-upscaler: the
game's device is made with <names> ... for <adapter>`. The upscaler's start asks the adapter again and compares the answer with that record.
The adapter must be the same, every name must be in the record, and every bit that was asked for must be on in it. Anything else makes the
adapter not available, with the reason given. So an adapter never starts on a device that was made without what it names.

Limits. One run of the game with the switch had the device made with the extension and the derivatives feature, before the core bit existed
(report 20261114-204846-report-1d541e). The refusals, the fallback after a failed creation and the core bit are read from the code, not seen
in a run. Turning on the extension and the feature allows a shader capability, and according to the specification, it changes no existing call.

Why the core bit. In that run, the validation layer reported barriers of the old barrier call on a `D24_UNORM_S8_UINT` image, with the depth
aspect alone. The rule is `VUID-VkImageMemoryBarrier-image-03320`. No barrier of the runtime, and no barrier of ours, uses that call or names
one aspect of such an image alone. The SDK's published Vulkan backend does both for a depth image that is handed to it. That is the normal path
of the published source. It is not proof of what the library that ran did. The line that gives the handed images' handles ties the message's
image to the depth we hand over. With separate depth and stencil layouts on, the rule allows a depth aspect alone. The self-test's device has
had that feature on all along, because it turns on every 1.2 feature, and it never showed the rule. The proof is a run of the game under the
layer in which rule 03320 no longer occurs and no new rule appears at the barriers. After the library returns, both aspects must be in the
layout that the runtime's tracker holds. By the published source, the library puts the depth aspect back to the layout it was handed, and it
never names the stencil aspect. Our barrier after the library names both aspects, with that layout. A run with the switch is to be made under
the validation layer, as described in [nvn-vulkan.md](nvn-vulkan.md), "A run under the validation layer". The adapter's name is its own (`name`
in the contract), or the word of the switch when the adapter gives none.

### The FSR adapter in the game (`SNX_ARCEUS_UPSCALER=fsr`)

This adapter is EXPERIMENTAL. The FSR self-test FAILED. Its edge-case budgets were not met, and neither was the comparison of case 4 with a
plainly scaled frame. The record [upscaler-selftest-runs.md](../../records/graphics/upscaler-selftest-runs.md) has one open point. Another line
in that record says that the edge case was built and did not run. The code cannot decide between these two statements. The FSR path makes no
quality claim. It is off without the switch. Do not use it before these conditions are met: jitter conformance; the native self-test suite and
case 7 with the validation layer; the passing cases; and the `vk_rec_direct` fixture. Without the switch, none of this code runs.

At full size, the library does the temporal resolve, and the scene is the whole target. Below native size, the scene rectangle is the render
size. The option needs `SNX_ARCEUS_TEMPORAL=taa`, and it is refused with `SNX_ARCEUS_TEMPORAL_REJECT=1`. Each refusal gives one line. The feature
is not available, with one line each, and the run continues, in these cases: the adapter is absent; the instance needs an extension that is not
named; the device lacks the private image format as storage and sampled; the device needs are not all in the record; or there is no context on
the game device before use. When the upscaler is on, our resolve, sharpen, history, numbers and views never run, and their options have no effect.

One log-only line shows the handed images: `arceus-upscaler: handed images at frame N: colour 0x.., depth 0x.., vectors 0x.., output 0x..`. It
gives the four handles in the form the layer prints an image. It is written right before the evaluate, and nowhere earlier. It is written at the
first hand-over after a context is ready, and again whenever one of the four handles changes. At most sixteen lines are written, and the last of
them says so. A message from the layer about an image can be matched to this line. The format question at the start also gives the largest width
and height. A larger scene stops the upscaler, and the line names both sizes. The driver is not asked about the image.

The frame order is as follows.

1. At the first draw with the scene camera block, the rectangle gate decides as always, whatever `SNX_ARCEUS_TEMPORAL_RECT` is set to. The upscaler
   then decides whether to jitter. The frame is jittered only when the gate accepts it, the scene is the whole target at full size, the
   configuration is known, and a context is ready for exactly that configuration. There is no jitter without an evaluate. The configuration
   covers the sizes, the formats, the layer counts, the create flags, and the far-plane decision, which comes from the projection at that draw.
   The depth target and the projection are those of this draw. The colour target is the previous frame's colour target, which reached our
   passes, because this frame's colour target was not seen in the depth pre-pass. The colour target is kept by value. A layered target that is
   not its first layer, or a shared array, is kept as a target that the upscaler does not take, and the next decision then refuses jitter. The
   failure mark is read at every decision and every hook, before publication or before the context is ready.
2. A frame without jitter happens only when no context is ready. In that case, the context is made at the place of our passes, created, and
   prepared through `vk_rec_direct`. The recycle step notes the completion of the submission. A context is in use at the next gate draw or hook,
   and it is checked against the confirmed configuration. The gate's decided configuration is used for the gate. The hook's configuration is
   taken from the frame's colour target, the projection of the draws, and the state of the whole target. A context that a frame which has moved on
   no longer uses is released, and a context that is in use stays. When no configuration is confirmed, nothing is published, and only the mark is
   read. Nothing else of ours runs.
3. A jittered frame runs the vector pass. The hook sees this frame's colour target, its depth, its projection, the eight offsets, and the stored
   depth, which is the projection's own. These must match the decision, and every number must pass. The projection is kept by value, taken from
   the unjittered bytes of each jittered copy. All draws use the same projection, and it must be bit for bit the decided one. The hook reads no
   guest memory. Until our passes run, every draw into the scene target whose program reads constant bank 4 must use the decided camera block,
   however little it reads. A draw that uses another block, at a known slot or somewhere else, is not evaluated. Its line names the program and
   the address, for up to eight such lines. There is no inference from the place of the block. Then the colour, the depth and the motion go to
   the library through `vk_rec_direct`. The library writes into a private image of four 32 bit floats. The format is the one that the layer
   reports for the output variable in the self-test. Our shader `temporal_upscaled.frag` copies that image into the scene colour before the first
   reader, texel for texel. Negative values, and values that are not numbers, become 0. Each channel is held to the largest finite value of the
   format. Once a handed image has been entered, whatever the answer, every handed image counts as last used by all commands. This covers reading
   and writing, for whatever uses the image next.
4. A reset happens when there is no previous history or a camera cut (the rules of the gate apply), when the previous frame was not evaluated, or
   when a context has been in use since the last evaluate. The frame time passed on is the nominal frame time from `SNX_ARCEUS_TARGET_FPS`, or a
   sixtieth of a second (see the switch table).

Failures are handled as follows. A jittered frame whose evaluate is skipped, because of another configuration, a failed number or a missing gate
confirmation, or whose library refused, is left as drawn, and one line is logged. The next evaluate resets. Eight such failures in a row turn the
upscaler off for the session. Non-jittered frames between them are not counted. Some failures turn the upscaler off at once. These are: a pass that
the hand-over needs cannot be made; a submission did not run as recorded; a context's prepare fails; and the timeline cannot be read eight times in
a row. When the upscaler is off for good, no frame is drawn with jitter, and every frame is left as the game draws it. There is no fallback to our
own resolve. Off means no publication, no jitter and no evaluate.

Retirement continues after the upscaler is off. A context that was in use when the upscaler stopped, and a context that is on its way out once its
submission has completed, are released after the frames in flight. Once a submission has not run as recorded, or a wait for the timeline has failed
(which sets the same mark), nothing is destroyed until the end of the session. Each frame has one fate in the counts: the first fate reported for it.
A frame is counted in one window, together with its decision.

The adapter `gpu/vk/upscaler_fsr.cpp` is for Windows only. It needs `amd_fidelityfx_loader_vk.dll` and `amd_fidelityfx_upscaler_vk.dll` beside the
program. The user supplies both files, and they are never in this repository. The API headers are in `third_party/FidelityFX-SDK`. The adapter takes
the newest 3.1 line that the library holds, and it logs what it asked for and what it runs.

### Below native size

With `SNX_ARCEUS_UPSCALER=fsr`, `dlss` or `xess`, fixed resolution on, and a scale of 1/2, 3/5, 2/3, 3/4 or 4/5, the library's render size is the
scene rectangle. The output size is the target. The picture goes to the game's readers in a larger texture, as it does in the spatial hand-over.

For DLSS and XeSS, the mode is the one whose optimal render size the library gives as the rectangle. If there is none, the mode is the nearest one
whose range holds that size. The user's look for XeSS is that 1/2 is blurrier and 2/3 is sharper. Both are softer than DLSS at 1/2, on NVIDIA
hardware, where XeSS uses its generic path. The scale itself is the user's choice.

Our TAA below native size has an automatic bias of log2(scale), which is -1 at 1/2. A frame of the upscaler that is not jittered by our code gets no
bias from us (`gpu/vk/temporal.c:1890`). For DLSS at 1/2, the user's look is that a bias of -1 is much sharper than no bias. A bias of -2 is sharper
still, with slight shimmer, and it is available as a set value.

Limits. The paths cover FSR, DLSS and XeSS. The self-test uses the formats B10G11R11 for colour and D24S8 for depth, and it uses one layer. The
projection form comes from two cameras of one place. Other frames get no jitter. The first context needs at least two unjittered frames and a create
in the middle of a frame. A change of colour target leaves one jittered frame as drawn.

### What the FSR adapter needs of a device

This is a conservative policy for the one provider that was seen, and it is not complete. The self-test ran under the layer. The library modules
declared `SPV_KHR_compute_shader_derivatives` with linear groups, and the device was made without them. The published source picks shaders by the
physical device's features, not by the device creation. The adapter names `VK_KHR_compute_shader_derivatives` with the linear-groups feature. If
either is missing, the adapter is not available. This is a conservative refusal, and it makes no claim beyond that. A failed or incomplete list, or
a failed feature query, makes the adapter not available. Nothing is taken from part of a list.

16 bit floats in shaders are a core feature, and the device maker turns them on where they are available. The self-test device has every physical
feature on. That feature is turned on as a 1.2 bit, and the adapter's derivatives structure sits behind it, inside the adapter. The adapter also names
separate depth and stencil layouts, for the reason given above. The self-test checks for that feature, and it refuses a device without it. The device
line says that the device was made with it for the adapter, so that the configurations of both makers can be compared. The game device has the same
needs, checked through its own path. This does not close any open condition. The library's messages about storage images remain.

### The XeSS adapter (`SNX_ARCEUS_UPSCALER=xess`)

This adapter is EXPERIMENTAL. `gpu/vk/upscaler_xess.cpp` is for Windows only. It uses Intel XeSS Super Resolution through its Vulkan API. The MIT
headers are in `third_party/XeSS`. The user supplies `libxess.dll`, which is never in this repository. The run that was seen used SDK 3.0.2, and the
reports show 2.0.2.

The adapter needs answers from the instance (no extension, and Vulkan 1.3 or later) and from the device. Its feature answer comes from a chain that the
adapter makes. Storage writes without a format, 8 bit integers, scalar block layouts and integer dot products are core bits. The mutable descriptor type
is a structure with an extension. Any other field or structure makes the adapter not available, and a line names it first.

Prepare submits work (`submits_at_init`). The runtime handles this between two whole-device idle waits. The rule is in `gpu/vk/upscaler_protocol.h`,
which is host tested. The GPU thread is used, the lock is taken, and the ring is drained. A failed wait before the prepare means that no call is made,
and the upscaler is off. A failed prepare after a good wait turns the upscaler off, and the context is destroyed, as for any failed prepare. A failed
wait after the prepare is fatal, and nothing more is called, freed or submitted (`temporal.c:3079-3080`). XeSS uses its own Vulkan loader, so its
submissions cannot be counted. The self-test shows the layer's messages for each prepare, and it runs case 7 for the evaluates.

Every call comes from the thread of the first context. A call from another thread gives a line. A destroy from another thread makes no XeSS call, and
the context stays until the process ends. The game uses the GPU thread and never shuts down. The self-test uses one thread.

The conventions that come from the self-test are these. Our jitter scale is 1, and the sign protocol is (+1, +1) at 1/2. Our vectors are multiplied by
minus the image size. Case 2's sixteen sign variants put that convention lowest. The vector scale is set at create, and it is set again only for a frame
that brings another scale. The output is four 32 bit floats. `SNX_ARCEUS_XESS_OUTPUT=b10g11r11` gives B10G11R11 instead. That is a test override, and it
needs no run.

The `xess` self-test uses Vulkan 1.3. It runs the DLSS integration matrix, which covers the native suites, 1/2 and 2/3, and the sign protocol. It also runs
case 2's sign variants, and it logs all of them. The result was PASS, and case 7 was silent. The report is 20261116-040400-report-605f2d, and
[upscaler-selftest-runs.md](../../records/graphics/upscaler-selftest-runs.md) describes it.

### DLSS

The module is `snx_upscaler_dlss.dll` in `engine/msvc/upscaler_dlss/`. It is built with the Microsoft compiler, against the DLSS SDK that the builder
supplies. The SDK is never in this repository, and its folder is named by `SNX_DLSS_SDK_DIR`. The build fails on the import of `vulkan-1.dll`. The lifetimes
and frame numbers are in `dlss_core.h`, `dlss_eval.h` and `dlss_record.h`. The test in `engine/tests/upscaler_boundary` runs them against a stand-in NGX.
The NGX data folder must be writable, and the module goes in a folder below the program.

In the game, DLSS runs DLAA at native size, and its vectors have two channels. The jitter sign is (+1, +1), following the user's look. `SNX_ARCEUS_DLSS_JITTER_SIGN`
forces one of eight values. Below native size, the mode is NGX's optimal mode.

The self-test runs the integration matrix at native size, at 1/2 and at 2/3. It makes no quality claim. The result is FAIL, with one known deviation of the
library: a reset at 1/2 in performance mode (`gpu/vk/upscaler_selftest.c:2151`, `:2911`; `gpu/vk/temporal.c:3939`). The licence question is open, in issue #120.

### The upscaler's self-test (`SNX_ARCEUS_UPSCALER_SELFTEST=<adapter>`)

The self-test runs with `fsr`, `dlss` or `xess`, and it does not load the game. `0`, `off` and unset mean off. Its first steps, before it reads the options,
are the dump check, the menu, the guest address space and the game code. The program folder and the run name come from the command line. The run name names
the folders, and the log is the report file. There is no window and no game file.

Without the switch, the program makes one check and then runs as before. If the state of the device is unknown, because the end of a submission cannot be
waited for, the device is no longer used. The remaining cases are not run, and the result is FAIL. The self-test has its own instance and device, with no
window, and it runs the runtime steps (`gpu/vk/upscaler_host.c`). The scene is generated. The lines start with `arceus-upscaler-selftest`, and the adapter
lines start with `arceus-upscaler`. The exit code is 0 on a pass.

The scene is defined in `gpu/vk/upscaler_selftest.h`. It has a position function. The far background is a checker pattern with slanted edges and a fine grid,
and a nearer patch has its own pattern. The true picture is the function averaged over 8 x 8 points per pixel. The frame input is the rasteriser's view. It uses
the pixel centre and the scene point, less a perturbation, in the game's formats (B10G11R11 for colour, D24S8 for depth). The self-test shares no arithmetic with
`temporal_math.h` or with the adapter, so that the same mistake cannot be repeated in both. The inputs are made once, and a variant changes only the numbers that
are handed over.

The output format is `VK_FORMAT_R32G32B32A32_SFLOAT`. In the layer run, `rw_upscaled_output` is four 32 bit floats. The self-test uses a 16 bit float picture.
This is checked at the binding dispatches. The format is asked of the physical device for the combined uses (the adapter, storage and sampled, and read-back
copies) at picture size. If the format is absent, the adapter is not available, and the case is not made. The read-back has 16 bytes per pixel, read as floats.
The staging buffer is checked against the frame uploads and the output before the copy. If any non-finite value appears in red, green or blue, the run ends.

Later runs may move the case numbers, and the cause is still open. The change from 32 bit floats to 16 bit halves changes the numbers. When `rw_upscaled_output`
is absent, the counts hold only for the same dispatches to the end, and only if the table has no limit. They are read only as "no callback of this run named it".
This makes no correctness claim, and the images that the library names in the layer are not changed.

The cases use a negative and a positive viewport height. The render size is 320 x 180, and the output size is 480 x 270.

1. Jitter only, with a still pattern and twice the offsets. Four jitter signs are tried. The configured sign must be strictly the lowest. The margin is the same as the
   edge case's margin. The error must be 1.25 times lower than the error of one plainly scaled frame. The change in the last offset cycle must be at most half of
   the change in the first.
2. Translation on both axes. Sixteen jitter and vector sign combinations are tried. The configured one must be strictly the lowest. Three vector-only turns must each
   have an error of at least 1.25 times the configured error.
3. A patch that moves another way over a still background. Four vector signs are tried, inside the patch. In the left band, after the first cycle of evaluated frames,
   the configured sign must beat the turned depth convention by the margin. The band's pixels are counted and logged. If there are fewer than 500 of them, the case
   fails as a test.
4. A size change in the middle of the run. The frames until the new context are skipped. Case 1 measures the repeat, meaning the error compared with one plainly scaled
   frame, and the settling. After the change, the output is compared with a fresh context at the same frames and the new sizes. The difference must be at most 0.005.
   This budget was fixed before the data was seen. The noise is that of two fresh contexts. A control uses a fresh context whose images are one offset ahead of the
   reported jitter. Its difference must exceed both 0.005 and the noise. Otherwise, nothing is decided.
5. A new pattern with the reset flag. The output must match a context that never saw the old pattern, within 0.01. Without the flag, the difference must be at least
   0.05. Otherwise, the control saw nothing.
6. Content at the position 40, 30 in larger images of another proportion, with foreign padding. It is compared with the same content at the first texel of an image of
   its own size. The difference must be at most 0.01.
7. No message from the layer, and the synchronisation checks asked for, in any case. The layer's feature extension must be on, which is read from the layer list. The
   device line says whether the extension was asked for. If it was not, the line says which way it was not asked: no extension, an incomplete list, a failed call, no
   memory, or no room. What the layer then does with them cannot be read back. If the checks were not
   asked, case 7 and RESULT fail, however silent the layer is. The line then says NOT MET, with a reason line, and the run stops. If no layer is installed, the case is
   not run, which is not a pass.

   The self-test sees only the loader's list of layers, and it says how it saw that list. The list is either whole or absent. It can be in part, after three asks, when the
   list is incomplete or the answer is unknown. Or it can be missing altogether, which means a failed call with its result, or that no list was asked for because the count
   failed or there was no memory, or that there is no result for a call that was not made. A fourth case is a listed layer that is asked for, where the message callback
   failed, and that case gives its result too. The device line and the case 7 line say which of these applies. Each listed layer gets a line, up to 64 lines, and then a
   line for the rest. `VK_LAYER_PATH`, `VK_ADD_LAYER_PATH` and `VULKAN_SDK` are logged as they were seen. Case 7 does not run in any of these four cases.

   When the layer is on, every callback of warning level or higher is counted and printed in full, with its number, its id name and number, and its text, in pieces of the
   log. The first 256 callbacks are printed, with a cap of 4096 characters, and the caps are stated. At the end, one line gives the counts of the callbacks received, printed,
   cut and not printed, with the id name where there is one. One line follows for each id count, up to 64 ids, and a full line. The last line is a sum check.

   An id is the whole name plus the number. A name of 128 characters or more is unclassified. It gets its own line, and it is printed cut, with its length. Only printable
   ASCII is printed as it is. A line break, another control character, or a non-ASCII byte is shown as a visible mark: a backslash with n, r or t, or x and two hexadecimal
   digits. A backslash in the text itself is doubled. Nothing is dropped. A line reads back to one text, and no piece cuts a character. Each piece is one log line with a
   prefix, and the first callback line gives the mark counts.

   The end counts are taken once, under the callback lock, after the device wait, whatever the cases did. The count lines, case 7 and RESULT use that copy. If the wait failed
   or was not made, the copy is still taken. Case 7 then says that the device is not known to be idle, and the case fails. A callback that arrives later gets its own line,
   and it fails case 7. The messenger is not destroyed, and later callbacks are not counted.

   The layer stops an id after a repeat limit, and the count is then the count that the layer reports. The self-test asks for the limit to be off (`enable_message_limit`,
   false, in the layer settings at instance creation, where the layer offers them). If that cannot be read, the result is unreadable. Three facts are kept apart. The first is
   whether the limit was asked for, and if not, which way it was not asked: no settings extension, an incomplete list, a failed call, no memory, or no room in our list. The
   second is whether most callbacks have one id. The third is whether callbacks contain the layer's stop sentence, matched as printed and not word for word. More than ten is
   passed as ten.

   The table holds the variable names in the raw text of every received callback, printed or not. The match uses the layer's pattern, which is the word `variable` followed by a
   quoted name. The table holds whole names, up to 64 of them, each up to 63 characters, with the callbacks. The pattern match is not a resource identity. The table also lists
   the callbacks with no match, and the marks for text that was not read to the end, for an unclosed quoted name, for a long name, and for a full table. When a name is absent,
   the self-test only says "no match for this exact pattern in received callbacks", with no mark. No other wording is used.

The scene depth uses two distances. The background is at 5, and the patch is at 1/6. The game projection gives the depth as (A z + B) / (-z), where A is n over (f - n) and B
is n times f over (f - n). The cases use a near plane of 0.1 with an infinite far plane and the create flag. They also use 24 bit depths of 0.02 and 0.6.

The native suite runs at both heights. At these heights, the render size equals the output size, 480 x 270. There are 8 offsets at the game's full size. The suite has two
forms. The main form uses the game's far plane from a run: near 0.1, far 5000, with the infinity flag off and the far plane given as a number. The second form uses an
infinite far plane with the flag on. Each change is compared with a fresh destination context at the same frames, and the difference must be at most 0.005. The noise of two
fresh contexts is measured. A deliberately wrong control is run too. If the control does not exceed the budget and the noise, nothing is decided.

Each form runs these checks. The first is a reset: a new pattern with the reset flag, a context that saw that frame alone, and a control without the flag. The second is a change
of a create property at the same sizes: a context without the linear-colour flag over one frame, then with it, and a control one offset ahead of the reported jitter. The third is a
size change, from 320 x 180 to native size, with the same control. Between the two forms, in both directions and at the same sizes, the first frames use one far plane, and the frames
of the fresh run use the other. The result is compared with a fresh destination context, with the noise and with a control.

The game form adds a two-depth case. It has a nearer patch over a still background, as in case 3. The vector signs are tested inside the patch. The left band is compared with the
turned depth convention, with the factor 1.25. The patch and background errors are printed. Each case, and each fresh context, checks the session: the contexts in use, the create
flags (the far-plane flag on or off), the far plane handed over each frame, the reset on the first evaluate, and whether the last depth checksum equals the regenerated image for the
expected far plane. The error against the true picture is logged. It has no margin.

The edge case tests the jitter signs with its own picture. The picture is grey, with one near-vertical edge and one near-horizontal edge. Each edge runs from 0.25 to 0.75, with a
slope of 1/10. Each edge lies in a 96 x 96 region of the output. The images are made and checksummed before the adapter is looked up, and the case uses only those checksums. The case
makes seven pictures of the same frozen images: the configured signs, each run twice; each other sign combination; zero jitter while the images are jittered, which is the "told nothing"
run; and one plainly scaled frame.

The edge profile is the mean of the pixels at each distance, in 32 bins, each a quarter pixel wide. The bins are fixed, and no fit or shift is applied. Each profile is compared with the
truth, both by profile and by the same-pixel difference, for each channel. For either height, the case passes when the errors of the edge across an axis are ordered as follows: the configured
sign first, then "told nothing", then the sign of that axis turned. The turned sign must be worse than the configured sign by at least 0.010. The "told nothing" run must be worse by at least
0.003. Both measures must agree. On each edge, the turned signs must also be worse by 0.010. The margin must be at least five times the measure noise, which is the difference of two configured
runs. The configured sign must beat the plainly scaled frame on both edges, and the output must have stopped changing. If the "told nothing" run is not worse, if the noise is too large, or if a bin
is empty, nothing is decided, and the case fails as a test. The budgets are fixed before any run.

A case that cannot separate the variants fails as a test. Every number is logged. The log shows the error of each variant by region (background, patch and left band) and by channel. It shows
each edge-case picture by channel and by edge. The first device run had nearly all of its background error in one channel, and a single mean hid that. The last line is `RESULT: PASS` or
`RESULT: FAIL`. For XeSS and DLSS, the integration result is a line of its own. Quality is not assessed.

The log also shows the two configured edge runs, with every frame input as it was handed over. The adapter prints each dispatch from the SDK structure after the structure is filled, field by
field. Every resource is printed with its description and state. Unset fields are named. A `trace` flag in `gpu/vk/upscaler.h` controls this output, and another line of ours records the caller.
The rule is to know what a library receives before drawing a conclusion from its use.

Two parts do not judge the result. The first is a matched pair. The edge images are made once with y pointing down, and once with y pointing up and the clip offset's y negated. Both use the same
bytes, so the two viewport signs hand identical inputs to the library. The images and the per-frame hand-over are compared. This comparison runs only if both runs succeeded, each run handed over every
gated frame, and the handed frames match. Then the difference between the outputs is printed beside the difference of a repeated run. Otherwise, a line says which condition failed.

The second part is a response table. It changes one axis at a time, with the reported jitter set to -1, -0.5, 0, 0.5, 9/16, 0.75, 1, 1.25, 1.5, 16/9 and 2. The images stay
unchanged, and the edge errors of each run are printed. The table only looks at the results. The lowest error on one picture does not show which factor is wanted.

The record [upscaler-selftest-runs.md](../../records/graphics/upscaler-selftest-runs.md) gives the history of single runs. It says what each run gave, and what was built but not run.

## Log lines

- `arceus-upscaler:` gives the start, which says the feature is asked for, experimental and without effect. It also gives the first contexts, the first frame left as drawn and
  then every 600th such frame, the turn-off, and, every 600 decided frames, the counts. The counts cover the evaluated frames, the jittered frames that were skipped or failed,
  and the frames that were not jittered, by reason: the gate, not the whole target, an unknown part of the configuration, no ready context, or off.
- The handed-images line gives the four handles at the hand-over.
- `arceus-upscaler-selftest` is the self-test line, and `arceus-upscaler` is the adapter line.
- The shared stage and GPU-time lines are described in [motion-vector.md](motion-vector.md).

## Limits

The game path is experimental. Its stop is final, and it has no fallback. The self-test judges the signs and conventions on its own scene, not on the game's picture. A pass says only
that they agree with the SDK on that scene. Region means can pass with a few bad pixels. Only one depth step is tested, and transparency is not covered. The pre-run checks are still open.

## Tests

- `SNX_ARCEUS_UPSCALER_SELFTEST=<adapter>` runs the self-test. The adapter library must be beside the program. Case 7 needs the validation layer, the synchronisation checks, and no message.
- `tests/vk_record/run.sh` runs the `vk_rec_direct` fixture. It runs on Linux, with gcc, pthreads, and the address and thread sanitizers.
- `tests/temporal_spatial_host/run.sh` tests the spatial hand-over, using the D3 fixtures of the reviewer. The decision is in `temporal.c`, and the pair is in `render.c`, as they are in the
  tree. The test uses stand-ins for the driver, the guest memory and the shader factory. It has 112 required-source cases and a refusal log. The pair and key controls run under the address and
  undefined-behaviour sanitizers. The test needs python3, clang and the Vulkan headers. It does not run on Windows.
- `tests/upscaler_selftest_callbacks/run.sh` runs two host regressions for the validation callbacks. It needs python3, clang, gcc and pthreads, and it does not run on Windows. It runs the callback,
  the counts and the last lines from `upscaler_selftest.c` as they are. It covers the ids, the caps, the byte marks and read-back, a multi-byte character at the edge of a piece, and 448 combinations
  of the last lines of case 7. Those combinations cover the wait, the count before, the callback after, and whether the checks were asked, or not asked in six ways. The test runs under the address
  and undefined-behaviour sanitizers. The callback thread against the counts runs under the thread sanitizer.
- `engine/tests/upscaler_boundary` tests the DLSS pure headers against the stand-in NGX.

## Current state

- The game path is experimental, and it has not been played.
- The self-test results are these. XeSS PASS. FSR FAIL, with the open edge budgets and case 4, whose open point is in [upscaler-selftest-runs.md](../../records/graphics/upscaler-selftest-runs.md).
  DLSS FAIL, with one known library deviation, a reset at 1/2.
- The pre-run checks are open. The DLSS licence is open, in issue #120.
