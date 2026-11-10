# Arceus high-FPS timing: handoff

Written 2026-11-08. Read this first to continue the 60+ FPS timing work on the recompiled
Pokémon Legends: Arceus 1.1.1 (ScarletNX). The renderer has its own handoff:
`C:\Users\brett\Documents\Codex\2026-11-07\continue-from-renderer-handoff-txt\outputs\CLAUDE-HANDOFF.txt`.

No game or build was running when this was written. Nothing has been launched since the census runs.

## Standing rules (from the user, keep them)

- Keep game dumps, extracted/generated guest modules, saves, raw captures, raw logs and vendor DLLs local.
  Check CONTRIBUTING.md before publishing. Addresses, symbols and short disassembly may be documented.
- Preserve Vulkan `--nvn_native=0`, target 120 FPS, camera multiplier 2.2 and the accepted
  timing/menu/blink/effects fixes. Do not tune the Poké Ball flight further.
- Use the established controller-capable desktop launch route (the helper below, started through
  explorer.exe). Keep personal saves isolated from offline fixtures. The user saves and closes the game.
  Do not kill it, add a timeout, restart it, hot-patch it, send gameplay input or make Escape quit.
  Do not launch another personal test after it closes without new direction. Read
  `outputs\controller-test-launch-notes.txt` before a personal test.
- Run one GPU route at a time. Commit or push only when asked.
- CONTRIBUTING.md style: plain prose, no em dashes, no semicolons between sentences, comments at most
  4 lines, one subject per PR, each PR says what was run, new files carry the SPDX header.

## Where things are

| What | Path |
| --- | --- |
| Fork checkout (origin = colardocookie fork, `upstream` remote = nxaevora) | `C:\Users\brett\Documents\Codex\2026-11-06\files-pasted-by-the-user-project\work\nxaevora-main` |
| Upstream-port worktree (branches `arceus/*`) | `...\work\nxaevora-upstream` (same folder parent as above) |
| Clock source in the fork | `runtime/gfx/world-pt/overlay/src/nx/runtime/arceus_clock.c` |
| Clock source upstream | `third_party/ScarletNX/src/nx/runtime/arceus_clock.c`, docs `docs/arceus-timing.md` |
| Prepared runtime used for test builds | `C:\Users\brett\Documents\Codex\2026-11-07\continue-from-renderer-handoff-txt\work\physics-runtime` |
| Its build dir | `...\work\physics-build` (build script `build-physics.sh`) |
| Upstream tree build dir | `...\work\upstream-build` (build script `build-upstream.sh`) |
| Staged test runtime | `...\work\physics-stage\ArceusTest.exe` |
| Test launcher and its source | `...\work\physics-launch\Arceus-Elapsed-Physics-Test.exe`, `physics-controller.c` |
| Game directory | `C:\Users\brett\Documents\Codex\work\arceus-games\ArceusTest` |
| Run captures (one folder per launch) | `C:\Users\brett\Documents\Codex\work\arceus-runs\timing-*` |
| Research notes, latest | `C:\Users\brett\Documents\Codex\2026-11-06\recompiled-game-folder-c-users-brett\outputs\arceus-engine-research-pass8.txt` |
| Analysis scripts and disassembly listings | `...\recompiled-game-folder-c-users-brett\outputs\arceus-timing-tools` |
| Python | `C:\Users\brett\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe` |

The prepared runtime is a copy. After editing `arceus_clock.c` in a checkout, copy it to
`physics-runtime\src\nx\runtime\arceus_clock.c` before building. Both were identical at handoff.

The analysis scripts run from the research root `C:\Users\brett\Documents\Codex\2026-11-06\recompiled-game-folder-c-users-brett`
(they load `work/explore.py`). `trace.py` still writes new listings to the old session scratchpad, so change
its `OUT` to `outputs/arceus-timing-tools/dis` before using it. The listings are disassembly of game code and
stay local.

## Staged test, not yet run

- Runtime SHA256 `d75ec3fe6b90b180f9cd6bfa0105b79f730805f40d0b588685c0e3ebea4483cc` (fork branch
  `experiment/arceus-gravity-probe`, 7765ce0). The launcher checks this hash, and `--verify-only` exits 0.
- The launcher turns on target 120, camera 2.2, all accepted timing fixes, the physics step, ball flight
  correction, navigation step, world timers, timing research, the cadence census and the gravity probe.
- Previous builds are kept as `ArceusTest.world-timers.exe.bak` and `ArceusTest.census.exe.bak`,
  and the previous launcher as `Arceus-Elapsed-Physics-Test.world-timers.exe.bak`.
- Save backups: `arceus-runs\save-before-gravity-probe-20261108` (latest) and `save-before-census-20261108`.
  The live save is `arceus-games\ArceusTest\ArceusTest_data\save\user.0123456789abcdeffedcba9876543210`.
  Back it up again before a new test.

What the user should do in that test: let time of day pass, watch wild Pokémon movement speed, enter a
cave, stand in rain or snow if possible, jump a few times and drop off a ledge, then save and close.

What to check in the log (`game-out.txt` of the run folder):
- `arceus-physics-timestep`, `arceus-navigation-timestep` and
  `arceus-world-timers: EXPERIMENT: 12 of 12 ... cave light=1` install lines.
- `arceus-cadence: world_minutes` should rise about 5 per 5 s window (1 in-game minute per real second).
- `python census_report.py game-out.txt` for the per-owner cadence table.
- `python gravity_report.py game-out.txt` for falls. If `dv/out` is about 1.0, the velocity gains the
  gravity term once per frame, so gravity runs at FPS/30 speed. `fit a` compared with `g*f` gives the
  real acceleration. It was checked on a synthetic 60 FPS fall, which fitted as twice g.

## Findings so far

Census (two runs, 48 to 65 FPS): these run once per rendered frame: 167F988 event coordinator,
1982408 shared char move, 198D7E4 shared char accel, 1A08560 player controller, 1A0F278 player method,
1B47D10 weather blend, 1D5D59C world time feed, 1E1A25C cave light easing, 24AC8C4 day clock.

| Owner | Status |
| --- | --- |
| Main rigid-body world 2AF25B4 | Fixed (physics step). Played: balls needed the flight wrapper, now close to normal, slightly long |
| Ball flight 1A7191C | Fixed by wrapper. Do not tune further |
| AI navigation 2795204 | Fixed (FCSEL to FMINNM at +40). Not played |
| Day clock 1D5D59C/24AC8C4, event timers 167F988/168300C/1683528, weather 1B47D10, cave light 1E1A25C | Fixed (world timers). Not played |
| Shared char move 1982408 | Not a bug. The 1/30 is a probe horizon multiplied back by 30 |
| Shared accel/gravity 198D7E4 | Unknown. Airborne result y = -g(413A0F8) x factor(+BE8) x rate(198E188) / 30, clamped to -max_fall, never adds current velocity. The gravity probe answers it |
| Player controller 1A08560, method 1A0F278 | Unknown. Adds accel x 1/30 to velocity with a max-speed clamp. Likely only affects how fast top speed is reached |
| Ride gravity 25ABD58, Pokémon motion, countdown 1AAB534 | Never called in the census runs (no riding). Unknown |

Details, guards and instruction words are in `arceus-engine-research-pass8.txt`.

## Git and pull requests

Fork (`colardocookie/nxaevora-main`), stacked branches, all pushed, no fork PRs opened:
`experiment/arceus-elapsed-physics` 6f0dcd6, `-navigation` cc62619, `-world-timers` 96d8023,
`experiment/arceus-gravity-probe` 7765ce0. `PR-DRAFTS.md` in the fork checkout has the unused fork PR links.

Upstream (`nxaevora/nxaevora-main`), opened 2026-11-08 from the `arceus/*` branches, all against `main`:

| PR | Branch | Commit | Content |
| --- | --- | --- | --- |
| #2 | `arceus/clock-layer` | 718e1d6 | The clock layer ported into `third_party/ScarletNX`, host test `arceus_timing`, `docs/arceus-timing.md`, runtime-changes row 0044 |
| #3 | `arceus/elapsed-physics` | 85967d9 | Physics step, ball flight, census (row 0045) |
| #4 | `arceus/elapsed-navigation` | c244422 | AI navigation (row 0046) |
| #5 | `arceus/elapsed-world-timers` | 764452d | World timers (row 0047) |
| #6 | `arceus/gravity-probe` | 28577a9 | Gravity probe (row 0048) |

Each later PR shows the earlier commits too and says "Builds on #N". Merge in order. Every branch was
rebuilt in `upstream-build` with no warnings from the clock source and `arceus_timing` passed. The upstream
port only needs `arceus_clock.c`, `arceus_clock.h`, `arceus_step.h`, `arceus_camera.h`, one CMake line and two
calls in `main.c`. The renderer and the Escape change in `runtime.patch` are not part of it.

Upstream PR #1 (`nvn-coverage-and-missing-handlers`, head f6f0bca) was deliberately fast-forwarded to the
whole fork main earlier, because the connector could not open upstream PRs then (see the renderer handoff).
It still carries about 1,022 files including the renderer, `research/arceus-complete` and the clock overlay,
so it overlaps #2 to #6. The NVN-only snapshot is adb8ab1. Splitting it is the user's decision.

To keep the fork and upstream in step, make a change on the fork branch first, then replay its
`arceus_clock.c` diff onto a new `arceus/*` branch. `stack_upstream.py` did this for the four commits and is
a template: it rewrites the path, appends a docs section and a runtime-changes row, and reuses the commit
message. Python on Windows must pipe bytes, not text, or patches get CRLF and fail.

## Runs on 2026-11-08 afternoon

Both used the gravity-probe runtime (same hash). Logs go to `game-err.txt`, not `game-out.txt`.

- `timing-20261108-144227-148`: the staged 120 FPS test, about 3 minutes at 73 FPS median, exit 0.
  The census showed every listed owner once per rendered frame. `world_minutes` stayed at 480.000 for all
  26 windows. In falls, velocity gained exactly 0.3267 (g/30) per call at 64 to 120 calls/s.
- `baseline30-20261108-144927-512`: a one-time 30 FPS baseline of the unmodified game. Launcher
  `physics-launch\Arceus-30FPS-Baseline.exe`, source `baseline30-controller.c`. It unsets the target FPS and
  turns every fix off, camera 1.0, and keeps only the read-only probes. 12 minutes at a steady 30.0 FPS, exit 0.
  Every owner ran 30 calls/s (obj/fps 1.00). `world_minutes` also held at 480.000 for the first 58 windows,
  then rose 1.000 in-game minute per real second. So the hold in the 120 run is game state, not proof of a bug,
  and the world-timers fix still needs a 120 FPS run long enough to pass the hold.
  Gravity gained the same 0.3267 per call at 30 calls/s, which is 9.8 per second.
- Gravity at high FPS is confirmed as FPS/30 times too strong. The probe's `pos` read is junk (wrong field),
  so `fit a` in `gravity_report.py` is meaningless. Use calls/s times dv/call instead.
- Saves: `save-before-gravity-run`, `save-after-gravity-run`, `save-before-baseline30`, `save-after-baseline30`
  (all `-20261108` in `arceus-runs`).

## Gravity fix, staged 2026-11-08, not yet run

Fork branch `experiment/arceus-elapsed-gravity` (from 7765ce0, uncommitted at writing). `SNX_ARCEUS_GRAVITY_FIX=1`
needs the elapsed-time clocks. The airborne branch of 198D7E4 builds -1/30 with MOV/MOVK W8 at 198DA8C/90
(0x52911128, 0x72B7A108), then FMOV S1, W8 at +2B4 (0x1E270101). The pair becomes ADRP X8 and LDR W8 [X8,#4]
from the shared frame-step cell, as for the world timers. The cell setup is now shared by both fixes.
Runtime SHA256 `5cb4b3085525b43f6a0bdeb296a6c079e7f91bf5324986160699108e8f573186` in `physics-stage\ArceusTest.exe`
(previous kept as `ArceusTest.gravity-probe.exe.bak`). The launcher adds the switch and verifies the new hash.
Its previous build is `Arceus-Elapsed-Physics-Test.gravity-probe.exe.bak`. The 30 FPS baseline launcher pins
the old hash, so it now refuses to start. Rebuild it with the new hash if another baseline is needed.
`gravity_report.py` now fits v_in.y against time (`fit dv/dt`) and prints calls/s x dv (`rate*dv`).
On the earlier runs it gives 10.8 at 30 FPS and 19.3 at 64 FPS against g*f 9.8.
Expected with the fix: dv/call about -0.0817 at 120 calls/s and both columns near 9.8.

## Gravity fix run, 2026-11-08 15:09

`timing-20261108-150953-627`, about 9 minutes at a steady 120 FPS, exit 0. Saves `save-before-gravity-fix` and
`save-after-gravity-fix` (`-20261108`). The fix installed 1 of 1, nothing was refused.
- Gravity: the clean free fall gave dv/call -0.0816 at 121.7 calls/s, rate*dv 9.92, fit 9.84, against 9.8.
  The other segments are short ground contacts.
- Day clock: 88 of 93 windows advanced at a median 1.000 in-game minute per real second, the same as the
  30 FPS baseline. This confirms the world-timers fix for the day clock.
- Census: every listed owner once per frame (obj/fps 1.00), as expected with the fixes scaling per call.
- User report: battles felt sped up. The baseline and this run both include battles (move effect loads),
  but no battle owner is in the census, so the logs cannot say which part is fast. Unresolved.

## Battle investigation and capture fix, staged 2026-11-08

- Static: battle code sits around 7B0000 to 9C0000 (battle_logic, battle_view, BattleDirector 84D75C, which
  passes no time value) and the battle UI around 10AC000 to 10E0000. A constant scan found no per-frame
  1/30 step there. Frame-counted settings exist: IdleAnimation*WaitFrames (9BB904, countdown 9BAD68, which
  never ran in either run), WazaDitherEasingFrames (999EC8), systemMessageHideFrame, battle_result_*_keep_frame.
- Measured: the capture director 180C338 (sequence-clock kind 1) counts its waiting stages in calls. At 30 FPS
  stage 8 took 30 calls in 1.00 s, at 120 FPS the same 30 calls in 0.25 s. A whole capture took about 6.1 s at
  30 FPS and 3.0 s at 120 FPS. Stage 12 is time-based and took 2.17 s in both. `capture_report.py` prints this.
- Fix `SNX_ARCEUS_CAPTURE_FIX=1` (needs the clocks): the capture wrapper (shared with the research probe, which
  now only logs when timing research is on) keeps a per-object sum of frame steps. In stages 3, 5, 7, 8, 9, 14
  and 16 with +1DC above zero, a call without a whole 1/30 s due gets +1DC raised by one first and restored if
  the call left it there, so it does not count down. Stage 8 interpolates from +1DC, so that motion now
  updates at 30 Hz.
- Common fades C21490 also count frames, but the sampled logs were too sparse to time them.
- Runtime SHA256 `f9bf53ae694df332c3010750ae4aacd2d455a3fe01e4018e103c0db572df789c` staged (previous kept as
  `ArceusTest.gravity-fix.exe.bak`). The 120 FPS launcher adds the capture fix, the 30 FPS baseline launcher
  now pins this hash and sets the gravity and capture fixes off. Both verify. Launcher backups `.gravity-fix`
  and `Arceus-30FPS-Baseline.gravity-probe.exe.bak`.
- `trace.py` now writes listings to `arceus-timing-tools/dis`.

## Capture fix run, 2026-11-08 18:19

- Run `timing-20261108-181938-711`, about 7 minutes at 120 FPS, game exit 0. Saves backed up as
  `save-before-capture-fix-20261108` and `save-after-capture-fix-20261108`. All five fix install lines present.
- Launch route mistake: `Start-Process ... -ArgumentList "--verify-only"` passes `--verify-only ` with a trailing
  space, the exact compare missed it and the launcher started the game from the agent shell, not through
  explorer.exe. Both launchers now match the flag with wcsstr, were rebuilt (old copies `.strict-verify.exe.bak`)
  and verify exit 0 through that same call style without starting a game.
- Eight captures measured. Stages 8, 9, 14 and 16 now match the baseline (1.00 s, 0.33 s, 1.47 s, 0.13 s).
  A whole capture is about 5.5 s against 6.1 s at 30 FPS (3.0 s before the fix).
- Remaining gap: stages 3, 5 and 7 take 0.20, 0.20 and 0.10 s against 0.40, 0.40 and 0.20 s. In both runs +1DC
  sits at its start value for about 10 calls before counting down. The director skips its stage body
  (branch to 180C9FC) while the handles at +1E0/+1F0, +218/+228, +250/+260 and +288/+298 are not ready, gated by
  the flags at +210, +248, +280 and +2B8. Those are probably the capture particle effects. Their about 10 frame
  start-up is counted elsewhere and is not reached by the capture fix.
- Gravity still measures about 9.8 per second.

## Capture wait probe, staged 2026-11-08

- `SNX_ARCEUS_CAPTURE_WAIT_PROBE=1` (read-only, needs timing research): in the capture wrapper, before the
  original call, every call in stages 3, 5 and 7 logs `arceus-capture-wait:` with the gate bytes +210, +248,
  +280 and +2B8 and, for each handle pointer at +1E0, +1F0, +218, +228, +250, +260, +288 and +298, the pointer,
  its first word (main-relative when it is code) and the words at +8, +0xC and +0x10. `capture_wait_report.py`
  prints the calls where any of that changes.
- Runtime SHA256 `4f23117448e98ac316eb7e9acdf9d0a01c764ea256df0d78c31406edf3262a9c` staged (previous kept as
  `ArceusTest.capture-fix.exe.bak`). Both launchers pin it and set the probe on. Both verify. Backups
  `.capture-fix` for the launcher sources and executables.
- Goal: name the handle whose +0xC goes nonzero when the countdown starts, then follow its type to the code
  that counts its start-up frames.

## Capture wait probe run and handle start-up fix, 2026-11-08

- Run `timing-20261108-185446-395` (120 FPS, started through explorer.exe, DualSense detected, exit 0). Saves
  `save-before-capture-wait-20261108` and `save-after-capture-wait-20261108`. Four catches logged.
- At stage entry the handle pointers are zero. Each enabled pair (+1E0/+1F0 for stage 3, +218/+228 and
  +250/+260 for stage 5, +288/+298 for stage 7) appears with +0xC = 1 and 3 about 4 calls after the previous
  one. Stage 3 and 5 wait 8 calls, stage 7 waits 4, at any FPS: 67 and 33 ms at 120 FPS against 267 and 133
  ms at 30 FPS. The first word of each pointee is a small counter pair, not a vtable, so the counting owner is
  not identified yet.
- Fix, part of `SNX_ARCEUS_CAPTURE_FIX`: `capture_ready` repeats the director's gate checks (180C558 to
  180C614). A not-ready call adds its frame step minus 1/30 s to the per-object sum (floor -1 s), so the
  countdown afterwards waits until elapsed time catches up. On paper the first countdown of stage 3 lands at
  about 300 ms at both 30 and 120 FPS.
- Runtime SHA256 `da85c8794d46807dee853fa95da2e206fa8af03678af040a861cb319a31349f1` staged (previous kept as
  `ArceusTest.capture-wait-probe.exe.bak`). Both launchers pin it and verify. Launcher backups
  `.capture-wait-probe`.

## Handle start-up run and battle sequence fix, 2026-11-08 19:07

- Run `timing-20261108-190713-275` (120 FPS, explorer.exe, exit 0). Saves `save-before/after-capture-startup-fix-20261108`.
  Five field catches: stages 3, 5 and 7 now 0.40, 0.40 and 0.20 s, whole catch about 6.0 s. Field capture matches.
- User report: capture in battle is far too fast. Battle catches do not use 180C338. They play battle effect
  sequences (`bin/effect/battle/sequence/*.wbin`: ee101 throw/open/close/fall, ee102 smoke, ee104 to ee106) with
  `eb001_01_nor_capture.ptcl`. The 30 FPS baseline loaded no in-battle catch, so there is no measured baseline.
- Sequence class: built by 8BB198 (0x330 bytes, constructor 9DB43C, vtable 0x40D3348). Update 9DBEAC (vtable slot
  0x58) adds the speed float +F0 to the frame float +E8 once per call, capped at +EC (9DC3D4 to 9DC3F8), so every
  .wbin sequence, battle moves included, runs one frame per rendered frame. Commands have start +78 and length +80
  and end on an exact float compare.
- Fix `SNX_ARCEUS_BATTLE_SEQUENCE_FIX=1` (needs the clocks): wrapper on 9DBEAC (MOV X19,X0 at +2C to ADD X19,X0,#0).
  While a sequence is playing (+301, +302, +303 set, speed nonzero, frame below end), a call without a whole 1/30 s
  built up for that object (64 slots) is skipped and returns 0, so frames stay whole and advance 30 per second.
  Sequence-driven motion therefore updates at 30 Hz. `arceus-battle-sequence: calls=... skipped=...` is logged.
- Runtime SHA256 `b0c84c088f76bbc4b21eeea7ab9fc56cb3eb6bea5dc320a5b804bddb88d899c4` staged (previous kept as
  `ArceusTest.capture-startup.exe.bak`). The 120 FPS launcher sets the fix on, the baseline sets it off. Both verify.

## Battle sequence run and fractional advance, 2026-11-08 19:32

- Run `timing-20261108-193249-519` (120 FPS, explorer.exe, steady 120 FPS, exit 0). Saves
  `save-before/after-battle-sequence-20261108`. User: capture speed in battle is now correct, but some things
  looked like 30 FPS. That is the whole-frame skipping (sequence-driven motion updated every fourth frame).
- Change (same switch): at speed 1.0 every call runs. The wrapper writes a step of 30 x elapsed frames into +F0
  for the call (read only at 9DC3E0 in the update) and restores 1.0 after. The step is cut so the frame lands
  exactly on each whole frame (nextafterf nudge, host check: 60000 of 60000 whole frames landed), and the
  remainder carries over (cap 2 frames). Other speeds keep the whole-frame skipping. Log line now reports
  `partial=` and `skipped=`.
- Runtime SHA256 `8d364a052aa3030a1c49aef739031e0e099847f0fddb77ff7a5d2e76d76a9f2e` staged (previous kept as
  `ArceusTest.battle-sequence-whole.exe.bak`). Both launchers pin it and verify.

## Smooth battle sequence run, 2026-11-08 19:39

- Run `timing-20261108-193958-979` (120 FPS, explorer.exe, exit 0, 5 s averages 72 to 120, mean 119).
  Saves `save-before/after-battle-sequence-smooth-20261108`. User: everything is the right speed, nothing looked
  obviously wrong, battles look a lot better, maybe a little room for more smoothness.
- The run made fewer than 20000 sequence calls, so only the first four log lines exist (all partial). The share
  of non-1.0-speed sequences that still step whole frames is unknown. Lower the log interval or add a per-speed
  count to see whether those, or another battle owner, explain the remaining roughness.

## Battle smoothness research build, staged 2026-11-08

- Commit was asked for and then withdrawn by the user in favour of polishing. Nothing is committed.
- Same switch, read-only addition: playing calls are counted per speed (8 slots plus `other`) and the
  `arceus-battle-sequence:` line is now logged every 2000 calls with `speeds=[speed:calls ...]`.
- Runtime SHA256 `e50d1048bb42e2a3be35ec1ef556f49bc2585991ab9e2282c1672958dd617c19` staged (previous kept as
  `ArceusTest.battle-sequence-smooth.exe.bak`). Both launchers pin it and verify.

## Sequence speed run and movement fix, 2026-11-09

- Run `timing-20261109-095000-001` (120 FPS, explorer.exe, exit 0). Saves `save-before/after-sequence-speeds-20261109`.
  User: everything looks good. Saw odd horizontal floating and super-speed gravity on the player and a wild Shinx.
- Sequence speeds while playing: 1.0 for 10987 calls, 0.2 for 858, none other. The 0.2 sequences stepped whole
  frames. Change: the fractional advance now covers every speed in (0, 4] (pending is speed x 30 x elapsed, cap
  2 x max(speed, 1)), still landing exactly on whole frames.
- Gravity probe: airborne horizontal speeds look like the baseline (median per-segment |v.xz| 1.6, max about 11).
  Short 7-call ground segments fit 13 to 25 but are too short to trust.
- Cause, by reading: the gravity fix made vertical motion follow elapsed time while the other accel x 1/30 steps
  stayed per call, so airborne sideways motion ran 4 times fast relative to the fall.
- Fix `SNX_ARCEUS_MOVEMENT_FIX=1` (needs the clocks): 7 MOV/MOVK W8 +-1/30 pairs load the frame-step cell:
  198E020 shared airborne force (198D7E4, used by the player and Pokémon), 1984A70 shared vertical (19843A0),
  1A0BEE4 player velocity accel, 1A0BAF0 player speed accel, 1A0B3AC player countdown +750 (1A08560),
  1A0EEA0 player airborne force (1A0E0EC), 1A158C8 player vertical (1A14F80).
- Left for later: 1A0F304/1A0F55C/1A0F498 (turn-rate arguments to 1713904, one in W22), 1A0DA20 (feeds 19977C4,
  meaning unknown), riding 257AC30, 257C9F4, 257CE38 and 25AC3D8, collision predict 198EC6C.
- Runtime SHA256 `f3ac43510c66892ea898e672d2f5afb75ebf90e907cf9eb560694632b69a9d1c` staged (previous kept as
  `ArceusTest.sequence-speeds.exe.bak`). The 120 FPS launcher sets the movement fix on, the baseline off. Both verify.

## Movement fix run, 2026-11-09 10:17

- Run `timing-20261109-101718-998` (120 FPS, explorer.exe, exit 0, 5 s averages 48 to 120, mean 100). Saves
  `save-before/after-movement-fix-20261109`. All 7 movement sites installed.
- User: gravity glitches remain, clouds in the Obsidian Fieldlands move too fast, and one NPC showed textures
  shooting out of him (looks like exploding vertices). The spikes are new in the first run with the movement fix,
  which touches two sites shared by NPCs (198E020, 1984A70), so the fix is the first suspect.
- Gravity probe logged only 7 segments (one airborne, rate*dv 8.9, fit 11.4 at 66 calls/s), too few to explain it.
- Clouds: every MOV/MOVK 1/30 and 1/60 constant in sky, weather and render owners is already fixed (water 29FAF8C,
  weather 1B47D10 x7). Cloud motion must come from another source (material UV animation, particles, a frame
  counter or shader time). Needs a runtime probe. Unfixed 1/30 owners elsewhere: 8EED5C, AFB898, 13B32FC, 1717594,
  1727DF4, 191CE24, 1A173B0 (3), 1A19654, 1AAB534 (2), 1D1B454, 2F3E870, 30026F4, 301437C, 3014D04, 1/60 in
  1AAF3C0, 1B03814, 1D49144, 3190788.
- A/B launcher `Arceus-Elapsed-Physics-NoMovement.exe` (source `physics-nomove-controller.c`): same runtime and
  settings with `SNX_ARCEUS_MOVEMENT_FIX=0`. Run folder is still `timing-*`, `test-settings.json` says
  `movement_fix:false`. Verifies exit 0.

## Movement-off A/B run and position dump, 2026-11-09 10:33

- Run `timing-20261109-103341-899` (movement fix off, otherwise the same, exit 0). Saves
  `save-before/after-nomove-20261109`. User: NPC spikes gone, so a shared movement site (198E020 or 1984A70)
  caused them. Falls still look wrong for the player and Pokémon: too fast horizontally while falling. Clouds
  look about twice as fast. The user wants a 30 FPS baseline to compare.
- Movement fix now takes an optional hex mask `SNX_ARCEUS_MOVEMENT_SITES` (bit 0 = 198E020, 1 = 1984A70,
  2 = 1A0BEE4, 3 = 1A0BAF0, 4 = 1A0B3AC, 5 = 1A0EEA0, 6 = 1A158C8). The 120 FPS launcher sets `7c` (player sites).
- Gravity probe adds read-only `arceus-gravity-dump:` lines at each segment start and end: object +0..+C00 and
  x1 +0..+200 as hex words, 0x200 bytes per line (log lines are capped at 2048 characters). Offline, the triple
  whose change matches the integrated velocity is the position, giving real horizontal travel per second of fall.
- Runtime SHA256 `542b20a314b2a5264ffb1934a56ec04e710da83ea5ee57592d84b23ba91f6e5f` staged (previous kept as
  `ArceusTest.movement-fix.exe.bak`). All three launchers pin it and verify. Backups `.movement-fix`.

## 30 FPS baseline run and map cursor fix, 2026-11-09 10:45

- Run `baseline30-20261109-104546-070` (all fixes off, exit 0). Saves `save-before/after-baseline30-20261109`.
  User: clouds at 120 FPS are clearly 2 to 4 times too fast. Lots of horizontal movement in a fall is normal,
  so the fall issue needs measuring. The fast-travel map cursor is far too fast at 120 FPS.
- Gravity probe bug: one segment started and never ended (the owner stopped calling), which blocked every later
  segment, so the baseline has no usable fall data. This also explains the few segments in earlier runs. Fix:
  a segment whose owner has not called for 0.5 s ends as `reason=lost` and frees the probe.
- Map cursor 120F1F4 (vtable 4104890, callers 1240DF0, 12451A8): per call, position +D60/+D64 += stick x speed
  +D70 (120F474), held-frame counters +D78 and +D7C rise to 9. Fix `SNX_ARCEUS_MAP_CURSOR_FIX=1`: wrapper (MOV
  X19,X0 at +54 to ADD) scales +D70 by elapsed / (1/30) for the call and restores it, and holds both counters
  back on calls before a whole 1/30 s is due.
- Clouds: still no owner. Every MOV/MOVK 1/30 and 1/60 in sky, weather and render code is fixed, so cloud motion
  comes from another source. Next: a runtime probe (for example sky constant buffer values per frame).
- Runtime SHA256 `e6c1f441ba714460648a03f1db5b6cdbc63be957e83ff5edc8ead8b95535db05` staged (previous kept as
  `ArceusTest.gravity-dump.exe.bak`). 120 FPS launchers set the cursor fix on, the baseline off. All verify.

## Cursor and air test, 2026-11-09 11:01, and follow-up

- Run `timing-20261109-110109-394` (120 FPS, mask 7c, cursor fix, exit 0). Saves `save-before/after-cursor-fix-20261109`.
  User: cursor still far too fast, horizontal air movement still too fast, no NPC spikes.
- 120F1F4 ran only twice (it returns early unless +2C8->+1F4 is set), so it is not the fast-travel cursor.
  1245DB8 (vtable 4106800, `pane_N_map_top_00`) has the same logic with speed +970, position +95C/+960 and
  counters +978/+97C. The cursor fix now wraps both (MOV X19,X0 at +5C in 1245DB8) and logs per kind.
- Gravity probe with the stall fix: 965 segments (929 landed, 56 lost, 3 limit). The x1+90 triple only sometimes
  moves with the integrated velocity (most segments 0), so it is not a reliable position. No conclusion on air speed
  from the dumps yet.
- New player airborne candidates in 1A173B0 (vtable 413DD88, size 0x22A4): 1A17B10 velocity += accel x 1/30, and
  1A17F3C a force term scaled by 1/30 twice (both through W8, so the cell scales both). Added as movement bits 7 and 8.
  The 120 FPS launcher mask is now 1fc. Also seen but left: 1A19104 and 1A196C4 pass 1/30 as the step of the
  rotation helper 1727DF4.
- Runtime SHA256 `1eb6afd6506f31ab6c52e6784a19da06536b706e48a88dd51a6980c43b58b0bd` staged (previous kept as
  `ArceusTest.cursor-fix1.exe.bak`). All three launchers pin it and verify.

## Cursor fix 2 run, 2026-11-09 11:22, and water/fall follow-up

- Run `timing-20261109-112231-516` (mask 1fc, both cursors, exit 0). Saves `save-before/after-cursor-fix2-20261109`.
  User: map cursor speed is fixed. Water and clouds both look sped up. Player falling still does not look fixed.
- Water fix is installed (`arceus-water` line present) and adds the measured frame step per call of 29FA554
  (WaterCurrentTime += step at 29FAF8C, wraps near 10000). A data-side scan found no code loading a 1/30 or 1/60
  float constant (5 such constants exist, none referenced by ADRP+LDR). The world-pt renderer has no own time.
- New read-only counter `arceus-scene-params:` (needs the census switch and the timing fixes): calls per second
  of 29FA554 every 5 s, installed after the water fix (B at +8C to CBZ XZR) so the compiled body keeps the water
  patch. About 120/s means water time is right and the speed-up is another source shared with the clouds. More
  means WaterCurrentTime advances several times per frame.
- Bisect: 1984A70 (shared vertical, -g x factor / 30) may also be used by the player. The 120 FPS mask is now
  `1fe` (1984A70 on, 198E020 off). Spikes back means 1984A70 caused them, otherwise 198E020 did.
- Runtime SHA256 `46ae5079cea7c0944ae3a959fd93a14bf099fd09ecb8bbaaec625fd04fd98573` staged (previous kept as
  `ArceusTest.cursor-fix2.exe.bak`). All three launchers pin it and verify.

## Bisect run, 2026-11-09 11:31

- Run `timing-20261109-113101-021` (mask 1fe, exit 0). Saves `save-before/after-bisect-20261109`. User: water,
  clouds and cloud shadows are still far too fast. Player falls still feel off (they suspect the acceleration).
  The user did not report spikes; asked.
- `arceus-scene-params`: 29FA554 runs 120.0 times per second, once per frame, so WaterCurrentTime gets one step
  per frame. 29BB420 (sky) copies WaterCurrentTime to +8C4 and 2A0FC14 (waves) reads it, so the clouds likely use
  the same clock. No per-call float accumulation was found in 29BB420, 29FA554, 2A0FC14 or 2A0CA7C.
- Fall: the long segment (1.3 s, 157 calls) fits 9.79 per s, exactly g. 1982408's 1/30 is confirmed a lookahead:
  19835E0 returns the displacement unchanged without contact (or a collision-corrected one through 19952A4), and
  the result x 29.99999 is set back as velocity. A fall position is still not identified, so travel cannot be measured.
- Counter now also logs `water_seconds_per_s` (sum of the water cell per call / real time). 1.0 means the water
  clock is right and the speed-up is elsewhere (shader or material animation). Above 1 means the cell is wrong.
- Runtime SHA256 `805af23f2afe09b9f61c4f3d18ea3f939d03d7e7afec770f45cedbf18b0c0b79` staged (previous kept as
  `ArceusTest.bisect.exe.bak`). All three launchers pin it and verify.

## Water scale experiment and 198E020 test, staged 2026-11-09

- User did not check for spikes in the 11:31 run (mask 1fe), so that run says nothing about 1984A70.
- Shader frame counter search: no frame-counter names. The global read before 198E020 (GOT 4272CD8 / 4272CE0,
  73 users including sky 1E1E0B4, sky params 29BCC90, waves 2A0FFB8) is an angle-unit constant used by the
  table acos in 85D8F0, not a clock.
- 198E020 detail: phase from 85D8F0, sine table at GOT 4272CE8, then (base + amp x sin) x direction x 1/30 through
  the rate 198E188, added to the velocity and clamped to +698. It is the shared airborne sideways force.
- Experiment `SNX_ARCEUS_WATER_SCALE` (0 < x <= 4) multiplies the water step. The 120 FPS launcher sets 0.25 and
  mask `1fd` (198E020 on, 1984A70 off). Water, clouds and shadows looking normal means WaterCurrentTime drives them
  and is consumed in a frame-rate-dependent way. No change means another source. No spikes means 198E020 is safe.
- Runtime SHA256 `55fae533035635cbe3d6fb014db930b5d28bb32519c13cea8b5f486e0ab73279` staged (previous kept as
  `ArceusTest.water-counter.exe.bak`). All three launchers pin it and verify.

## Water scale run, 2026-11-09 11:41, and sky time experiment

- Run `timing-20261109-114115-703` (mask 1fd, water scale 0.25, exit 0). Saves `save-before/after-water-scale-20261109`.
  Counter: 119 calls/s, water_seconds_per_s 0.250. User: water now too slow (it was fine at 1.0), clouds only
  slightly fast. So the water clock is right at 1.0, and the clouds run on WaterCurrentTime too but about 4 times
  fast at 120 FPS. Spikes and falls were not reported.
- 29BB420 copies WaterCurrentTime to sky +8C4 (29BCC38). No CPU code reads +8C4 in the sky area (8 readers, none
  sky related), so the sky block likely goes to the shader as is and the cloud scroll is computed on the GPU.
  29BCC90..29BCD14 is the IBL rotation angle (sin/cos table), not the clouds.
- Water scale is back to 1.0 (launcher line removed). New experiment `SNX_ARCEUS_SKY_TIME_SCALE` (0 < x <= 4): a
  wrapper on 29BB420 (MOV X21,X0 at +20 to ADD) scales sky +8C4 after the call. The 120 FPS launcher sets 0.25.
  Clouds and shadows right with water still right means the sky copy is the cloud clock and the factor is the GPU
  side. The 29FA554 counter now also installs on the 30 FPS baseline (before the timing-fix gate) for the rate there.
- Runtime SHA256 `4e01cad43dca03c804e61af617df16044eaefabbd7d6fe20e959f4f94a4db6d9` staged (previous kept as
  `ArceusTest.water-scale.exe.bak`). All three launchers pin it and verify.

## Sky time run, 2026-11-09 11:54

- Run `timing-20261109-115454-173` (mask 1fd, sky time 0.25, water 1.0, exit 0). Saves `save-before/after-sky-time-20261109`.
  User: water too slow compared with gameplay videos, clouds maybe 2x fast. No gravity reference.
- So the water shader also reads the sky copy +8C4, and water is right with it at 1.0. The sky scale is removed from
  the launcher again (switch stays in the runtime, off by default).
- Other cloud inputs checked: AmbientWindPhase (2A0CA7C 2A0D370..2A0D41C) advances by real time (a nanosecond clock
  at [[42EF568]+98]+148, /1e9, delta capped at 1 s) x wind speed x 0.065. NoiseOffset (29B2420) is the camera
  post-process noise (NoiseDelay x 30), not clouds. WaterCurrentTime is only built in 29BB420, 29FA554, 2A0FC14.
  Every CPU-side time the sky shader gets is now correct, so any remaining cloud speed-up is in the shader or in
  something not yet found (for example particles such as ef_cmn_mountain_cloud).
- Spikes with mask 1fd not yet checked.

## Next steps

0. Clouds: compare the same spot and time of day at 30 FPS baseline and 120 FPS, ideally recorded, before
   more changes. Check NPC spikes with mask 1fd.

1. Run the 120 FPS launcher (mask 1fe): watch for NPC spikes, player falls, water and clouds. Read the
   `arceus-scene-params` rate. Then decide which shared site to keep and where water/cloud time comes from.
2. Update the descriptions of upstream #4, #5 and #6 with what was run.
3. If gravity is per frame, fix 198D7E4 the same way as the world timers (scale or a cell load), as an
   opt-in switch, then measure a fall again at two frame rates.
4. Then the player controller 1A08560 and, after a run that includes riding, the ride and Pokémon motion owners.

## 2026-11-09 evening: late-game run and ride gravity

- Installed the downloaded late-game save (old save in `arceus-runs\save-before-complete-save-20261109`). The game
  loaded it and rewrote it in its own format.
- Run `timing-20261109-181311-022` (mask 1fd, 250 s, exit 0): 119.7 FPS after loading, p99 10 ms, 3 hitches over
  50 ms. Saves `save-before/after-fps-late-game-20261109`.
- Ride Pokemon `163e59000` (factor 1.4) fell 4x fast through 198D7E4: 0.457 per call = 13.72/30 at 120 calls/s,
  while factor-1 objects in the same run gained 9.8/120. Both 1/30 sites in 198D7E4 were patched, so the
  multiplier `rate` from 198E188 must be 4. 198E188 returns 1 without a link at object+260, else the float at
  singleton->+230 +124 (1D431D0, GOT 427A648/427A650, set by 24A8668 through 1D4324C). 25ABD58 (its own -1/30 at
  25AC3D8) never ran. The player object 151dc6000 never appeared in the gravity probe: player falls are unmeasured.
- Staged runtime `b641e8e61cf42f4661322a11d12d3f0d4a68e82c55819e6af8b20d46d53cea63` (previous kept as
  `ArceusTest.pre-rate-probe.exe.bak`, source before as `arceus_clock.before-rate-probe-20261109.c`). The gravity
  probe now logs `link=` and `scale=` on start and `rate=` (implied) and `scale=` per call. Read-only.
- Next: a short run with falls while riding. If rate=4 tracks scale, find what writes the +124 scale.
- Run `timing-20261109-184721-655` (rate probe, mask 1fd, exit 0). Saves `save-before/after-ride-rate-20261109`.
  Global scale was 1 everywhere and every object has a link at +260, so 198E188's rate is not the cause.
  Cause: a fall runs at rate 1 while horizontal speed climbs, then switches to rate 4 on the call where horizontal
  speed reaches its cap (11.0 for factor 1, 30.0 for factor 2.5, about 7.8 for the ride) until max_fall 50. In that
  state 19843A0 ("shared vertical", 1.6 to 33 calls/s, not per frame) builds vy + g x factor x -1/30 at 1984A70 and
  queues it at object+6B0 with flag +6A0, and 198D7E4 applies the queued term (ldr q22, [x19, #0x6b0]) on every
  call, 120 per second. 1984A70 is movement mask bit 1, off in 1fd. So bit 1 is needed for capped-speed falls.
- Next: a mask 1ff run (bit 1 on) that checks both capped-speed falls (rate should read 1) and the NPC spikes, since
  bit 0 or bit 1 caused them and 1fd (bit 0 on) has not been checked for spikes yet.
