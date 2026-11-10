"""Replay the four fork timing commits onto the upstream port branch, one branch each."""
import re, subprocess, sys
FORK = r'C:/Users/brett/Documents/Codex/2026-11-06/files-pasted-by-the-user-project/work/nxaevora-main'
UP = r'C:/Users/brett/Documents/Codex/2026-11-06/files-pasted-by-the-user-project/work/nxaevora-upstream'
SRC = 'runtime/gfx/world-pt/overlay/src/nx/runtime/arceus_clock.c'
DST = 'third_party/ScarletNX/src/nx/runtime/arceus_clock.c'

def run(*a, cwd=UP, inp=None):
    # Bytes, so Windows text mode does not turn LF into CRLF in a patch or message.
    r = subprocess.run(a, cwd=cwd, input=inp.encode() if inp is not None else None, capture_output=True)
    out = r.stdout.decode('utf-8')
    if r.returncode: sys.exit(f'{a} failed:\n{out}{r.stderr.decode()}')
    return out

STEPS = [
 ('6f0dcd6', 'arceus/elapsed-physics', '0045-arceus-physics-step',
  "With `SNX_ARCEUS_PHYSICS_TIMESTEP_FIX=1` and a target rate, the main rigid-body world steps the smaller of 1/30 second and the elapsed time instead of a fixed 1/30 second (`main+2AF25B4`). A wrapper on the thrown-object flight update (`main+1A7191C`) then scales its drag, lift and flight timer by the step times 30 and restores them after each call. `SNX_ARCEUS_CADENCE_CENSUS=1` with timing research adds read-only call counters on 32 fixed-step owners. See `docs/arceus-timing.md`.",
  """## Physics step

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
"""),
 ('cc62619', 'arceus/elapsed-navigation', '0046-arceus-navigation-step',
  "With `SNX_ARCEUS_NAVIGATION_TIMESTEP_FIX=1` and a target rate, the AI navigation world (`main+2795204`) steps the smaller of 1/30 second and the elapsed time. Its mode byte stays 0, which selected a fixed 1/30 second on every rendered frame. The FCSEL at `main+2795244` becomes FMINNM. See `docs/arceus-timing.md`.",
  """## AI navigation step

`main+2795204` steps the AI navigation world once per rendered frame. Its mode byte at +150 stays at the
constructor's 0, which selects a fixed 1/30 second, so at about 66 FPS navigation advanced about 2.2 times real
time. With a target rate, `SNX_ARCEUS_NAVIGATION_TIMESTEP_FIX=1` turns the FCSEL at `main+2795244` into FMINNM, so
the step is the smaller of 1/30 second and the elapsed time. The path has no zero-step check, and the shared step
is never zero. The `arceus-navigation-timestep` line confirms it.
"""),
 ('96d8023', 'arceus/elapsed-world-timers', '0047-arceus-world-timers',
  "With `SNX_ARCEUS_WORLD_TIMERS_FIX=1` and a target rate, the day clock feed (`main+1D5D59C`), the event hold timers (`main+167F988`, `main+168300C`, `main+1683528`) and the weather blend (`main+1B47D10`) load the smaller of 1/30 second and the elapsed time from a cell that the runtime writes on each frame, in place of twelve constant builds. The cave light increment (`main+1E1A25C`) is scaled for each call. The census skips the owners that a fix wraps and logs the day clock. See `docs/arceus-timing.md`.",
  """## World timers

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
"""),
 ('7765ce0', 'arceus/gravity-probe', '0048-arceus-gravity-probe',
  "With the cadence census on, `SNX_ARCEUS_GRAVITY_PROBE=1` logs `arceus-gravity` lines from the shared acceleration (`main+198D7E4`). While its vertical result is negative, it follows one character for up to four seconds and logs the velocity read, the position given and the result, with real time. It only observes. See `docs/arceus-timing.md`.",
  """## Gravity probe

In its airborne branch the shared acceleration at `main+198D7E4` writes a vertical term of gravity times a
per-character factor and rate times 1/30, and it does not add the current velocity. If its caller adds that term to
the velocity on every call, falls and jumps run faster at high FPS. With the census on, `SNX_ARCEUS_GRAVITY_PROBE=1`
logs `arceus-gravity` lines. While that term is negative, it follows one character for up to four seconds. Each
call logs the velocity the function read, the position it was given and its result, with real time. If the velocity
gains the vertical term on every call, gravity runs at FPS/30 speed, and the positions give the real acceleration.
It only observes.
"""),
]

prev = 'arceus/clock-layer'
for commit, branch, row, row_text, section in STEPS:
    run('git', 'switch', '-q', '-c', branch, prev)
    diff = run('git', 'diff', f'{commit}^', commit, '--', SRC, cwd=FORK).replace(SRC, DST)
    run('git', 'apply', '--index', '-', inp=diff)
    doc = open(f'{UP}/docs/arceus-timing.md', encoding='utf-8').read().rstrip('\n') + '\n\n' + section
    open(f'{UP}/docs/arceus-timing.md', 'w', encoding='utf-8', newline='\n').write(doc)
    rc = open(f'{UP}/docs/runtime-changes.md', encoding='utf-8').read().rstrip('\n') + f'\n| {row} | {row_text} |\n'
    open(f'{UP}/docs/runtime-changes.md', 'w', encoding='utf-8', newline='\n').write(rc)
    msg = run('git', 'log', '-1', '--format=%B', commit, cwd=FORK)
    msg = re.sub(r'\s*\(Start-WorldPT\.ps1\s+-\w+\)', '', msg)
    assert 'Start-WorldPT' not in msg, msg
    run('git', 'add', 'docs/arceus-timing.md', 'docs/runtime-changes.md')
    run('git', 'commit', '-q', '-F', '-', inp=msg)
    print(branch, run('git', 'log', '--oneline', '-1').strip())
    prev = branch
