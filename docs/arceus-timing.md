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
