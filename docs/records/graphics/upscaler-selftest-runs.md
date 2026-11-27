<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the NXAevora contributors -->

# Upscaler self-test runs

This record keeps the history of the upscaler self-test's single runs. It is not a specification: what the self-test
checks is in [ai-upscaling.md](../../dev/engine/ai-upscaling.md), "The upscaler's self-test". This file is kept as
history and can be out of date.

## FSR

- Cases 1 to 7 ran once on a device, with FSR 3.1.6, in an earlier state of the test. They failed on the jitter's
  margin of cases 1 and 2, on case 4's error and on the missing validation layer.
- The edge case has been built and has not run. It is in `engine/src/nx/gpu/vk/upscaler_selftest.c` (`edge_case`).
- OPEN: another statement says the result is FAIL because the edge case's budgets and case 4's comparison with a
  plainly scaled frame are not met. The code's startup line says the same
  (`engine/src/nx/gpu/vk/temporal.c:3942`), and its self-test result line keeps the case in the code. The code cannot
  decide whether the edge case ran: the two statements disagree, and this record keeps both.

## XeSS

- The self-test ran at Vulkan 1.3, with DLSS's integration matrix (the native suites, 1/2 and 2/3, the sign protocol)
  and case 2's sign variants, logged.
- Its result was PASS with case 7 silent (report 20261116-040400-report-605f2d).

## DLSS

- The self-test runs the DL1 integration matrix and judges no quality. Its result is FAIL with one known library
  deviation alone: the reset at 1/2 in performance mode (`engine/src/nx/gpu/vk/upscaler_selftest.c:2151`,
  `:2911`; `engine/src/nx/gpu/vk/temporal.c:3939`).
- No report number is recorded for this run.

## What was built and not run

- The FSR edge case is built and, by one statement above, has not run. The other statement says its budgets failed.
  The point is open.
