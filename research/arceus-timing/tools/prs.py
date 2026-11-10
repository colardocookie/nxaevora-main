"""Write PR-DRAFTS.md with prefilled GitHub compare links for the three stacked branches."""
import sys, urllib.parse
REPO = 'https://github.com/colardocookie/nxaevora-main'
FOOT = '\n\n🤖 Generated with [Claude Code](https://claude.com/claude-code)'
prs = [
 ('main', 'experiment/arceus-elapsed-physics', 'Add opt-in elapsed-time physics step for Arceus',
  """## What this changes
- `SNX_ARCEUS_PHYSICS_TIMESTEP_FIX=1` (`Start-WorldPT.ps1 -ElapsedPhysics`) makes the main rigid-body step at `main+2AF25B4` the smaller of 1/30 second and the elapsed time. Off by default.
- With that step active, a wrapper on the thrown-object flight update at `main+1A7191C` scales drag, lift and the flight timer by step x 30 for each call, then restores the authored values.
- `SNX_ARCEUS_CADENCE_CENSUS=1` with `SNX_ARCEUS_TIMING_RESEARCH=1` adds a read-only call counter on 32 audited fixed-step functions. It logs `arceus-cadence` lines every five seconds.

## Why
The main rigid-body world takes one fixed 1/30 second step per rendered frame, so physics ran at about FPS/30 speed above 30 FPS. With only the step corrected, thrown balls flew in slow motion, because the flight update applies drag, lift and its timer once per update.

## What I ran
- The runtime built cleanly.
- Gameplay at about 71 FPS with the step alone: balls flew in slow motion, which led to the flight wrapper.
- Gameplay at about 63 to 90 FPS with the wrapper: the flight timer tracked real time (0.81 s real, 0.81 s timer). Throws looked better, still slightly long in the air.
- Two census runs of about 2.5 and 2 minutes at 48 to 65 FPS. All 32 counters installed, and both runs exited normally. Eight owners ran once per rendered frame.

No game data is included."""),
 ('experiment/arceus-elapsed-physics', 'experiment/arceus-elapsed-navigation', 'Add opt-in elapsed-time step for Arceus AI navigation',
  """Stacked on the elapsed-time physics PR. Merge that one first.

## What this changes
`SNX_ARCEUS_NAVIGATION_TIMESTEP_FIX=1` (`Start-WorldPT.ps1 -ElapsedNavigation`) turns the FCSEL at `main+2795244` into FMINNM. The AI navigation step becomes the smaller of 1/30 second and the elapsed input, matching the rigid-body step. Off by default.

## Why
`main+2795204` steps the AI navigation world (hkaiWorld::stepWorld) once per rendered frame. Its mode byte +150 stays at its constructor default 0, which selects a fixed 1/30 second. Research logs showed that on every frame while the real input was 0.008 to 0.016 seconds, so at about 66 FPS navigation advanced about 2.2 times real time.

The path has no zero-step check of its own. `arceus_step` never produces a zero step, and FMINNM maps NaN to 1/30. A nonzero mode, never observed, would also be capped at 1/30.

## What I ran
The runtime builds, and the encoding disassembles as `fminnm s8, s1, s0`. Not yet run in gameplay."""),
 ('experiment/arceus-elapsed-navigation', 'experiment/arceus-elapsed-world-timers', 'Add opt-in elapsed-time steps for Arceus world timers',
  """Stacked on the AI navigation PR. Merge that one first.

## What this changes
`SNX_ARCEUS_WORLD_TIMERS_FIX=1` (`Start-WorldPT.ps1 -ElapsedWorldTimers`) gives these owners the smaller of 1/30 second and the elapsed time instead of a fixed 1/30 per call. Off by default.
- Day clock feed `main+1D5D59C` into `main+24AC8C4`, which counts in-game minutes.
- Event hold timers in `main+167F988`, `main+168300C` and `main+1683528`.
- Weather blend rise and fade in `main+1B47D10`.
- Cave light easing in `main+1E1A25C`.

The twelve MOV/MOVK constant builds load the value from a runtime-written cell, as the water clock already does. The cave light increment of 1/120 is scaled for each call and restored afterwards. The census skips owners a fix wraps, and logs the day clock's minutes so a run can check its rate.

## Why
Two census runs showed each of these owners running once per rendered frame, so each ran at FPS/30 speed. At 60 FPS an in-game day took half as long, event hold thresholds were met twice as fast, and weather and cave light transitions finished early.

The census also explains why player movement looks right at high FPS. The shared movement code at `main+1982408` scales velocity by 1/30 only to probe a collision-aware move, and multiplies the result by 30 before submitting it. Gravity and acceleration in `main+198D7E4` and the player controller remain unresolved.

## What I ran
The runtime builds, and the cell load encodings disassemble as `adrp x8` plus `ldr w8, [x8, #0]` or `#4`. Not yet run in gameplay."""),
 ('experiment/arceus-elapsed-world-timers', 'experiment/arceus-gravity-probe', 'Add read-only gravity probe for Arceus shared acceleration',
  """Stacked on the world timers PR. Merge that one first.

## What this changes
With `SNX_ARCEUS_CADENCE_CENSUS=1` and `SNX_ARCEUS_TIMING_RESEARCH=1`, setting `SNX_ARCEUS_GRAVITY_PROBE=1` logs `arceus-gravity` lines from the census wrapper on `main+198D7E4`. While the result's vertical term is negative, it follows one character for up to four seconds. Each call logs the velocity the function read (`x1+120`), the position it was given (`x2`) and its result (`+40`), with real time. It only observes and is off by default.

## Why
The census showed this shared acceleration running once per rendered frame. In its airborne branch it writes gravity times a factor and rate times 1/30, and it does not add the current velocity. Whether the caller adds that term to the velocity on every call decides whether falls and jumps speed up at high FPS. A logged fall answers that, and the positions give the real acceleration.

## What I ran
The runtime builds with no new warnings. The local summary script was checked against a synthetic per-frame fall at 60 FPS, which it fitted as twice the authored gravity. Not yet run in gameplay."""),
]
out = ['# Pull request drafts', '',
       'Local file, excluded from git. Open each link while signed in to GitHub. The title and description are filled in, and you press Create pull request.',
       'Merge in order: 1, then 2, then 3. Each later one is stacked on the one before it, so GitHub shows only its own commit.', '']
for i, (base, head, title, body) in enumerate(prs, 1):
    body += FOOT
    url = f'{REPO}/compare/{base}...{head}?expand=1&title={urllib.parse.quote(title)}&body={urllib.parse.quote(body)}'
    out += [f'## {i}. {title}', '', f'Base `{base}`, branch `{head}`.', '', f'[Open the prefilled pull request]({url})', '',
            '<details><summary>Description to paste</summary>', '', body, '', '</details>', '']
open(sys.argv[1], 'w', encoding='utf-8').write('\n'.join(out))
print(max(len(l) for l in out))
