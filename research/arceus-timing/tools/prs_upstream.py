"""Write UPSTREAM-PR-DRAFTS.md: prefilled cross-fork PR links into nxaevora/nxaevora-main."""
import sys, urllib.parse
UP = 'https://github.com/nxaevora/nxaevora-main'
FORK = 'colardocookie:nxaevora-main'
FOOT = '\n\n🤖 Generated with [Claude Code](https://claude.com/claude-code)'
STACK = ('Builds on {prev}. GitHub cannot stack a pull request on a fork branch, so this one also shows the commits '
         'of the earlier ones. Only the last commit, "{title}", is new here. Merge the earlier ones first.\n\n')
prs = [
 ('arceus/clock-layer', 'Add the Arceus 1.1.1 elapsed-time clock layer',
  """## What this changes
- New `third_party/ScarletNX/src/nx/runtime/arceus_clock.c`, with the pure headers `arceus_step.h` and `arceus_camera.h`. `snx_main` calls `arceus_clock_init` after the NVN trace and `arceus_clock_post_jit` after the JIT starts. That is the only change to existing runtime code.
- For Pokémon Legends: Arceus 1.1.1 only. It checks the build id of `main` and every instruction word it replaces, and logs a refusal and changes nothing when they differ.
- Off unless an environment switch is set. `SNX_ARCEUS_TARGET_FPS=60` or `120` moves the four main-loop clocks to the elapsed frame time. `SNX_ARCEUS_TIMING_FIXES=1` adds fixes for the water, effects, event camera, eye blinks, menu repeat and camera stick. The other switches are an opt-in skip of the automatic 3D resolution governor and read-only traces.
- New `docs/arceus-timing.md` lists every switch. `docs/runtime-changes.md` gets row 0044.
- New host test `arceus_timing`.

## Why
The game adds a fixed 1/30 second to most clocks on each rendered frame, so above 30 FPS it runs fast. This layer has been developed in the colardocookie fork as an overlay of the world path tracer. Here it stands alone, without the renderer.

## What I ran
- This tree builds (`scarletnx` target), and test `arceus_timing` passes. It checks four-channel agreement of the shared step at 60/40 and 120/60 FPS, the stall cap, the never-zero step, and the camera stick update at 30, 60 and 120 FPS.
- The same source has been played in the fork at a 120 FPS target with `SNX_ARCEUS_TIMING_FIXES=1` and camera speed 2.2 over several sessions. Timing, menu repeat, blinks and effects were accepted there. That was with the fork's renderer build, not this tree.

No game data is included."""),
 ('arceus/elapsed-physics', 'Add opt-in elapsed-time physics step for Arceus',
  """## What this changes
- `SNX_ARCEUS_PHYSICS_TIMESTEP_FIX=1` makes the main rigid-body step at `main+2AF25B4` the smaller of 1/30 second and the elapsed time. Off by default.
- With that step on, a wrapper on the thrown-object flight update at `main+1A7191C` scales drag, lift and the flight timer by step x 30 for each call, then restores the authored values.
- `SNX_ARCEUS_CADENCE_CENSUS=1` with `SNX_ARCEUS_TIMING_RESEARCH=1` adds read-only call counters on 32 fixed-step functions.

## Why
The main rigid-body world takes one fixed 1/30 second step per rendered frame, so physics ran at FPS/30 speed. With only the step corrected, thrown balls flew in slow motion.

## What I ran
- In the fork build: gameplay at about 71 FPS with the step alone (balls slow), then about 63 to 90 FPS with the wrapper (flight timer 0.81 s real, 0.81 s timer, throws still slightly long). Two census runs of about 2.5 and 2 minutes, all 32 counters installed, normal exit.
- This tree builds and `arceus_timing` passes.

No game data is included."""),
 ('arceus/elapsed-navigation', 'Add opt-in elapsed-time step for Arceus AI navigation',
  """## What this changes
`SNX_ARCEUS_NAVIGATION_TIMESTEP_FIX=1` turns the FCSEL at `main+2795244` into FMINNM. The AI navigation step becomes the smaller of 1/30 second and the elapsed input. Off by default.

## Why
`main+2795204` steps the AI navigation world once per rendered frame, and its mode byte +150 stays 0, which selects a fixed 1/30 second. At about 66 FPS navigation advanced about 2.2 times real time. The shared step is never zero, and FMINNM maps NaN to 1/30.

## What I ran
This tree builds and `arceus_timing` passes. The encoding disassembles as `fminnm s8, s1, s0`. Not yet run in gameplay.

No game data is included."""),
 ('arceus/elapsed-world-timers', 'Add opt-in elapsed-time steps for Arceus world timers',
  """## What this changes
`SNX_ARCEUS_WORLD_TIMERS_FIX=1` gives these owners the smaller of 1/30 second and the elapsed time instead of a fixed 1/30 per call. Off by default.
- Day clock feed `main+1D5D59C` into `main+24AC8C4`, which counts in-game minutes.
- Event hold timers in `main+167F988`, `main+168300C` and `main+1683528`.
- Weather blend rise and fade in `main+1B47D10`.
- Cave light easing in `main+1E1A25C`.

## Why
Two census runs showed each of these owners running once per rendered frame. At 60 FPS an in-game day took half as long, event hold thresholds were met twice as fast, and weather and cave light transitions finished early.

## What I ran
This tree builds and `arceus_timing` passes. The cell loads disassemble as `adrp x8` plus `ldr w8, [x8, #0]` or `#4`. Not yet run in gameplay.

No game data is included."""),
 ('arceus/gravity-probe', 'Add read-only gravity probe for Arceus shared acceleration',
  """## What this changes
With the census on, `SNX_ARCEUS_GRAVITY_PROBE=1` logs `arceus-gravity` lines from the census wrapper on `main+198D7E4`. While the vertical result is negative, it follows one character for up to four seconds and logs the velocity read, the position given and the result, with real time. It only observes and is off by default.

## Why
The airborne branch writes gravity times a factor and rate times 1/30 and does not add the current velocity. A logged fall shows whether the caller applies it once per rendered frame.

## What I ran
This tree builds and `arceus_timing` passes. Not yet run in gameplay.

No game data is included."""),
]
out = ['# Upstream pull request drafts (nxaevora/nxaevora-main)', '',
       'Local file, excluded from git. Open each link while signed in to GitHub, check the text and press Create pull request.',
       'Branches are in colardocookie/nxaevora-main. Merge in order 1 to 5.', '']
prev = None
for i, (head, title, body) in enumerate(prs, 1):
    if prev: body = STACK.format(prev=f'"{prev}"', title=title) + body
    body += FOOT
    url = f'{UP}/compare/main...{FORK}:{head}?expand=1&title={urllib.parse.quote(title)}&body={urllib.parse.quote(body)}'
    out += [f'## {i}. {title}', '', f'Branch `{head}`, base `main`.', '', f'[Open the prefilled pull request]({url})', '',
            '<details><summary>Description to paste</summary>', '', body, '', '</details>', '']
    prev = title
open(sys.argv[1], 'w', encoding='utf-8').write('\n'.join(out))
print(max(len(u) for u in out))
