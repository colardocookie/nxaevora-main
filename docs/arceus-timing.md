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
| `SNX_ARCEUS_TIMING_FIXES` | With a target rate, enables water animation and fractional effect progress. Also a prerequisite for the individual event-camera, blink, menu-repeat and camera switches below. It does not enable those hooks on its own. |
| `SNX_ARCEUS_CAMERA_INPUT_FIX` | Part of the timing fixes. The camera stick keeps its 30 FPS response at any frame rate. |
| `SNX_ARCEUS_CAMERA_SPEED` | With the camera fix, a multiplier of the manual camera rotation speed. |
| `SNX_ARCEUS_EVENT_CAMERA_FIX`, `SNX_ARCEUS_BLINK_FIX`, `SNX_ARCEUS_MENU_REPEAT_FIX` | Set each to `1` to enable its timing fix, with a target rate and `SNX_ARCEUS_TIMING_FIXES=1`. |
| `SNX_ARCEUS_FIXED_RESOLUTION` | Skips the game's automatic 3D resolution governor, which lowers the scene size against a 33.3 ms GPU budget. Explicit scene modes stay. |
| `SNX_ARCEUS_CLOCK_TRACE` | Logs the main-loop clock calls without changing them. |
| `SNX_ARCEUS_TIMING_RESEARCH` | Read-only counters on other fixed-step owners (physics worlds, frame timers, fades). |
| `SNX_ARCEUS_CAMERA_TRACE`, `SNX_ARCEUS_DIALOGUE_TRACE` | Read-only traces of the camera input and the message view. |

A typical run at 120 FPS sets `SNX_ARCEUS_TARGET_FPS=120`, `SNX_ARCEUS_TIMING_FIXES=1`,
`SNX_ARCEUS_CAMERA_INPUT_FIX=1`, `SNX_ARCEUS_EVENT_CAMERA_FIX=1`, `SNX_ARCEUS_BLINK_FIX=1`,
`SNX_ARCEUS_MENU_REPEAT_FIX=1` and `SNX_ARCEUS_CAMERA_SPEED=2.2`. The camera speed multiplier takes effect
only when the camera hook is enabled.

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

## Capture fix

The capture director at `main+180C338` counts its waiting stages in calls. It runs once per rendered frame, so at
120 FPS a field catch took about 3.0 seconds against 6.1 at 30 FPS. With a target rate, `SNX_ARCEUS_CAPTURE_FIX=1`
keeps a sum of frame steps for each capture object. In stages 3, 5, 7, 8, 9, 14 and 16, a call only counts the
stage timer at `+1DC` down once a whole 1/30 second has built up. Otherwise the timer is raised by one before the
call and put back after it, so the call leaves it unchanged. Stage 8 moves from that timer, so its motion updates
at 30 Hz.

Before stages 3, 5 and 7 count, the director waits until its effect handles are ready, which takes a fixed number
of calls. The fix repeats the director's checks of those handles. A call that waits counts as one whole 1/30
second, as it takes at 30 FPS, so the countdown afterwards waits until the elapsed time catches up. That debt is
capped at one second. The `arceus-capture-fix` line confirms the fix.

## Battle sequence fix

Battle effect sequences, the `.wbin` player whose update is `main+9DBEAC`, add their speed at `+F0` to the frame at
`+E8` once per call, so captures in battle and battle moves ran four times fast at 120 FPS. With a target rate,
`SNX_ARCEUS_BATTLE_SEQUENCE_FIX=1` passes speed times 30 times the elapsed time for the call and puts the speed
back after it. The step is cut so the frame lands exactly on each whole frame, because the commands of a sequence
end on exact float compares. Each call processes the elapsed budget through as many whole-frame boundary updates
as needed, up to 32 updates. A fractional tail waits for the next rendered update so command callbacks are not
immediately repeated for a tiny leftover step. Any remaining budget carries over without being discarded. Speeds outside (0, 4] only
update once a whole 1/30 second has built up. `arceus-battle-sequence` lines count the calls, the partial steps,
the skipped calls and the speeds seen.

## Map cursor fix

The map cursors at `main+120F1F4` (field map) and `main+1245DB8` (world map) add stick times speed to the cursor
position once per call, and count held frames in two counters up to 9, so the fast-travel cursor ran four times
fast at 120 FPS. With a target rate, `SNX_ARCEUS_MAP_CURSOR_FIX=1` scales the speed by the elapsed time over 1/30
second for the call and restores it, and lets the counters rise only once a whole 1/30 second has built up. The
`arceus-map-cursor` line says how many of the two cursors it covers.

## Gravity fix

The gravity probe showed that the airborne branch of `main+198D7E4` runs once per rendered frame and adds gravity
times a factor, a rate and 1/30 second to the velocity on each call, so falls accelerated at FPS/30 times gravity.
With a target rate, `SNX_ARCEUS_GRAVITY_FIX=1` makes that constant the negated frame step: the MOV/MOVK pair at
`main+198DA8C` loads it from the frame-step cell of the world timers. The cell setup is now shared by every fix
that loads it, and the first one maps it. The `arceus-gravity-fix` line confirms the fix.

## Movement fix

With only gravity on the elapsed time, sideways motion in the air still ran four times fast against the fall,
because other character movement steps add acceleration times 1/30 once per call. With a target rate,
`SNX_ARCEUS_MOVEMENT_FIX=1` makes nine of those constants load the frame step from the same cell. Each is a
MOV/MOVK pair of plus or minus 1/30:

| Bit | Site | Owner |
| --- | --- | --- |
| 0 | `main+198E020` | shared airborne force (`main+198D7E4`), used by the player and Pokémon |
| 1 | `main+1984A70` | shared vertical (`main+19843A0`) |
| 2 | `main+1A0BEE4` | player velocity acceleration (`main+1A08560`) |
| 3 | `main+1A0BAF0` | player speed acceleration (`main+1A08560`) |
| 4 | `main+1A0B3AC` | player countdown `+750` (`main+1A08560`) |
| 5 | `main+1A0EEA0` | player airborne force (`main+1A0E0EC`) |
| 6 | `main+1A158C8` | player vertical (`main+1A14F80`) |
| 7 | `main+1A17B10` | player state acceleration (`main+1A173B0`) |
| 8 | `main+1A17F3C` | player state force (`main+1A173B0`) |

`SNX_ARCEUS_MOVEMENT_SITES` is an optional hex mask over these bits, to test the sites one by one. Without it all
nine are on. The `arceus-movement-fix` line gives the number installed and the mask.

## Fade fix

The common fade at `main+C21490`, in its linear color mode, adds one to the frame at `+538` and the delta at `+550`
to the color at `+540` on each active call, so fades ran four times fast at 120 FPS: an eight-frame fade ended in
67 ms. With a target rate, `SNX_ARCEUS_FADE_FIX=1` makes each call add the delta times the elapsed time times 30.
The frame only advances once a whole 1/30 second has built up, by staging `+538` one lower before the call. The
call that completes the last frame adds only the share up to the end, so a fade-out does not overshoot while it
waits to finish, and the authored delta is put back after each call unless the call replaced it. The fraction is
kept for each fade object, up to 16. A fade-in frees its slot on the call that ends it. It starts over when the fade starts again (`main+C21D00` and `main+C227BC`)
or when the frame, duration or state differ from what the last call left. With all slots in use a call runs as
the original. The other fade modes are unchanged. `arceus-fade` lines count the calls.
