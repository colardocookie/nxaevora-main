<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the ScarletNX contributors -->
# edenprobe

edenprobe is a homebrew program of hardware probes: suites of cases, each
asking the machine one question and judging the answer against the console's. A run leaves `report.jsonl`, a line per
case (`pass`, `fail` or `skip`, with a detail) and a summary. `scripts/edenprobe.py` recompiles it, runs it headless and
holds the report against a baseline, how each case ended before: a case of the baseline that no longer ends that way
fails.

## Running it

```sh
python3 scripts/edenprobe.py run --runtime build/nxaevora-engine --nro edenprobe.nro --out /tmp/edenprobe \
    --mode "quick notime fatal cpu graphics fs video display kernel audio" \
    [--baseline tests/edenprobe/linux-x86_64.txt]
```

`--out`, empty or an earlier run's, is emptied, then gets the recompiled game and its XDG folders (`work/`, so
nothing of yours is read or written), `report.jsonl`, the failures' pictures (`images/`), the recompile's and each
launch's log, `summary.md` and `baseline.txt`: this run's passing cases, and those the baseline expects to end
another way that did. A launch that ends before the summary, crashed
or killed after `--timeout` seconds (600) without a new line in the report, is followed by another, which goes on after
the case it ended inside, reported as `crashed`. The baseline is lavapipe's: a GPU passes other cases, so on one the
run says what differs rather than what regressed.

## The mode words

Those of the example. edenprobe's mode file has them all. The judge's suites
(selfcheck, support, reference) run whatever is named:

| word | what it adds |
|---|---|
| `quick` | the run with no one in front of it: every suite named, the report, exit |
| `notime` | each drawing suite once, no timing passes, no times in the report |
| `fatal` | the cases that may end the program: SM4, CNTVCT_EL0 and DAIF reads, geometry shaders, BC4 arrays |
| `cpu` | the processor's instructions |
| `graphics` | the drawing suites: clears, blending, depth, textures and their formats, queries, blits |
| `fs` | the memory card: a 64 MiB file and 256 small ones, written once and read back |
| `video` | the video image compositor, and the VP9 decoder on libvpx's test vectors |
| `display` | a layer's buffer history |
| `kernel` | the scheduler: priority inheritance, preemption, migration |
| `audio` | the audio renderer: resampler, circular buffer, limiter; headless, nothing is played |

Left out: `net` (servers the runner does not have), `pad` and `listen` (a person), `stress` (256 MiB lists), `hang`
(cases that never return on a machine that gets them wrong).

## The baseline

`tests/edenprobe/<platform>.txt` names the edenprobe build and the mode words it was written with, then one case a
line: `suite: name` for a case that passes, `[RESULT] suite: name` for one that ends as RESULT, the result as the summary
lists it (`skip (crashed)`, `fail: DETAIL`; no `]` in it). That second form is for a case that ends the program on the
console too, as an instruction the console refuses: the line keeps the case held to that end, and any other, a pass
included, fails the run. Another build or other words fail the run until it is written again; a case that starts to
pass is listed, to be added. Write it from the runs that check it, never a desktop GPU's: keep the reports of two or three
runs (run it again for more) and keep what ended the same way in each, so a case that passes now and then stays
out. A run given the baseline writes into its `baseline.txt` the other results it expects that the run had, so they
come through:

```sh
python3 scripts/edenprobe.py baseline run1/baseline.txt run2/baseline.txt -o tests/edenprobe/linux-x86_64.txt
```

## A case from a game report

A game report whose failure a maintainer understands gets the label `status: confirmed`: it is ready for an
edenprobe case, one that asks the console the question the failure comes down to, so every build is held to the
answer. `nxaevora-engine --mcp` has the tools an agent writes it with, in a
clone of edenprobe:

- `edenprobe_scaffold` (service cases): writes the parts edenprobe's layout fixes into the clone, every file or none.
  Those are the case's enum entry and `CASES[]` row, its judge and the judge's reference check in
  `source/serviceref.[ch]` and `source/tests/t_serviceref.c`, the device side in `source/tests/t_service.c` called at
  `RunService`'s end, and a new suite's row in `source/ui.c`. Each `TODO` it leaves names the issue. What is left is
  the service call, the judge's logic, the expected answer and where it comes from (reden's commit and line, or a
  console), the wrong answers the reference check holds the judge to, NXAevora Engine's among them, and the docs' counts.
- `edenprobe_check`: `make -f host.mk host-run` in the clone. The judges and their checks run with the host's
  compiler, no devkitPro. Until the docs' counts in words are updated, its `test_judge_counts` fails on purpose.
- `edenprobe_run`: the NRO, built from the clone with edenprobe's pinned devkitPro image or taken from its pull
  request's build artifact, recompiled with this program and run headless in the mode's words until its summary. It
  answers the report's lines for one suite or case and the report's path. The recompile is kept while the NRO is
  unchanged, and nothing of the person's NXAevora Engine folders is read or written.
- `edenprobe_finding`: the case's line in NXAevora Engine's report and in the reference's (Eden's, or a console's), checked
  that it fails on NXAevora Engine and passes on the reference, and the pull request's text: the commit subject (72
  characters at most), the issue, what each machine answered and the rules a case keeps. Nothing of a game goes in
  it: no code, no disassembly, no assets, no value only that game uses.

The commit and the pull request to edenprobe stay with the agent and the person, under edenprobe's
`hooks/commit-msg`.
