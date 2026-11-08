<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the NXAevora contributors -->

# Arceus timing above 30 FPS

Pokémon Legends: Arceus 1.1.1 runs its world at 30 updates per second. Most of its clocks add a fixed 1/30 second
on each rendered frame, so above 30 FPS the game runs fast. `src/nx/runtime/arceus_clock.c` in the vendored
ScarletNX changes those clocks to the real elapsed time. Everything in it is off unless a switch below is set, and
it does nothing for another game or another build. Each change first checks the build id of `main` and the
instruction words it replaces, and it logs a refusal and changes nothing when they differ.

## Switches

The switches are environment variables. A switch is on when its value is `1`.

| Switch | What it does |
| --- | --- |
| `SNX_ARCEUS_TARGET_FPS` | `60` or `120`. The four main-loop duration writes take the elapsed time of the frame instead of 1/30 second, and the runtime's frame limit is set to this rate. Another value is refused. `SNX_ARCEUS_FPS60=1` is the older form of `60`. |
| `SNX_ARCEUS_TIMING_FIXES` | With a target rate, the fixes of the clocks that do not use the main-loop duration: the water animation, fractional effect progress, the event camera, eye blinks, menu button repeat and the camera stick response. |
| `SNX_ARCEUS_CAMERA_INPUT_FIX` | Part of the timing fixes. The camera stick keeps its 30 FPS response at any frame rate. |
| `SNX_ARCEUS_CAMERA_SPEED` | With the camera fix, a multiplier of the manual camera rotation speed. |
| `SNX_ARCEUS_EVENT_CAMERA_FIX`, `SNX_ARCEUS_BLINK_FIX`, `SNX_ARCEUS_MENU_REPEAT_FIX` | Parts of the timing fixes that can be turned off one by one. |
| `SNX_ARCEUS_FIXED_RESOLUTION` | Skips the game's automatic 3D resolution governor, which lowers the scene size against a 33.3 ms GPU budget. Explicit scene modes stay. |
| `SNX_ARCEUS_CLOCK_TRACE` | Logs the main-loop clock calls without changing them. |
| `SNX_ARCEUS_TIMING_RESEARCH` | Read-only counters on other fixed-step owners (physics worlds, frame timers, fades). |
| `SNX_ARCEUS_CAMERA_TRACE`, `SNX_ARCEUS_DIALOGUE_TRACE` | Read-only traces of the camera input and the message view. |

A typical run at 120 FPS sets `SNX_ARCEUS_TARGET_FPS=120`, `SNX_ARCEUS_TIMING_FIXES=1` and
`SNX_ARCEUS_CAMERA_SPEED=2.2`.

## The shared step

The four main-loop clocks are updated one after another on each frame. `arceus_step.h` takes one wall-clock sample
for the four updates, so they always agree. The step is never zero: a clock that did not move gives the initial
step of the target rate. A stall longer than 0.1 second is cut to 0.1 second, so the world does not jump. Test
`arceus_timing` checks this and the camera stick update of `arceus_camera.h`.

## Physics step

The main rigid-body world takes one fixed 1/30 second step per rendered frame, so above 30 FPS physics runs at
FPS/30 speed. With a target rate, `SNX_ARCEUS_PHYSICS_TIMESTEP_FIX=1` makes the step the smaller of 1/30 second and
the elapsed time. The `arceus-physics-timestep` line in the log confirms it.

With only the step corrected, thrown Poké Balls flew in slow motion. The flight update at `main+1A7191C` applies
drag, lift and its flight timer once per update and assumes 1/30 second each. While the step fix is on, a wrapper
scales those three by the step times 30 for each call and restores the authored values afterwards. With timing
research on and the step fix off, the same wrapper only observes. `arceus-ball-flight` lines record each launch and
flight end.

`SNX_ARCEUS_CADENCE_CENSUS=1` with `SNX_ARCEUS_TIMING_RESEARCH=1` counts calls per second of 32 functions that use
other fixed 1/30 second steps, and logs `arceus-cadence` lines every five seconds. It only observes. It shows which
of them run once per rendered frame (a high-FPS bug) and which run at 30 Hz.

## AI navigation step

`main+2795204` steps the AI navigation world once per rendered frame. Its mode byte at +150 stays at the
constructor's 0, which selects a fixed 1/30 second, so at about 66 FPS navigation advanced about 2.2 times real
time. With a target rate, `SNX_ARCEUS_NAVIGATION_TIMESTEP_FIX=1` turns the FCSEL at `main+2795244` into FMINNM, so
the step is the smaller of 1/30 second and the elapsed time. The path has no zero-step check, and the shared step
is never zero. The `arceus-navigation-timestep` line confirms it.

## World timers

The cadence census found these owners running once per rendered frame, each adding a fixed 1/30 second per call:

- `main+1D5D59C` passes 1/30 to the day clock `main+24AC8C4`, which counts in-game minutes. At 60 FPS a day took
  half as long.
- `main+167F988` counts how long an event condition has held against a threshold. Its callees `main+168300C` and
  `main+1683528` keep three more such timers.
- `main+1B47D10` raises and lowers three weather blend values by a configured rate times 1/30.
- `main+1E1A25C` adds 1/120 per call to a cave light transition.

With a target rate, `SNX_ARCEUS_WORLD_TIMERS_FIX=1` gives them the smaller of 1/30 second and the elapsed time. The
twelve constant builds load it from a cell that the runtime writes on each frame, as the water clock does. The
cave light increment is scaled for each call and restored. The `arceus-world-timers` line confirms it. With the
census on, `arceus-cadence: world_minutes` lines show the day clock, which should advance about five minutes in
each five-second window.

The shared character movement at `main+1982408` also scales velocity by 1/30, but only to probe a collision-aware
move, and multiplies the result by 30 again. It does not depend on the frame rate.

## Gravity probe

In its airborne branch the shared acceleration at `main+198D7E4` writes a vertical term of gravity times a
per-character factor and rate times 1/30, and it does not add the current velocity. If its caller adds that term to
the velocity on every call, falls and jumps run faster at high FPS. With the census on, `SNX_ARCEUS_GRAVITY_PROBE=1`
logs `arceus-gravity` lines. While that term is negative, it follows one character for up to four seconds. Each
call logs the velocity the function read, the position it was given and its result, with real time. If the velocity
gains the vertical term on every call, gravity runs at FPS/30 speed, and the positions give the real acceleration.
It only observes.
