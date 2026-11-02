#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the ScarletNX contributors
"""scripts/area-labels.py on the titles issues and pull requests have: each area of the leading [area] prefixes once, in
their order, an area that names no label skipped, then the one kind the words after them ask for; a call without one
title is refused. Offline."""

import subprocess
import sys
from pathlib import Path

SCRIPT = Path(__file__).resolve().parent.parent / 'scripts' / 'area-labels.py'
CASES = [
    ('[gui] Fix the folders crash', ['area: gui', 'kind: bug']),
    ('[macos, linux] The AppImage starts', ['platform: macos', 'platform: linux']),
    ('[donotmerge needs #374] [gpu] Add a surface format', ['area: gpu', 'kind: enhancement']),
    ('[shader, surface] [vk] Half floats', ['area: gpu']),
    ('[Docs] How to build', ['kind: documentation']),
    ('[mii, am] Random Miis', []),
    ('The launcher hangs on a folder', ['kind: bug']),
    ('[gui] the fix list', ['area: gui']),
    ('Merge pull request #381 from scarletnx/dravee/service/mii-editor', []),
]


def run(*args):
    return subprocess.run([sys.executable, str(SCRIPT), *args], capture_output=True, text=True)


def main():
    failed = 0
    for title, want in CASES:
        got = run(title)
        labels = got.stdout.splitlines()
        if got.returncode != 0 or labels != want:
            print(f'FAIL {title!r}: exit {got.returncode}, {labels}, wanted {want}')
            failed += 1
    if run().returncode != 2 or run('a', 'b').returncode != 2:
        print('FAIL a call without exactly one title is not refused with 2')
        failed += 1
    print(f'area_labels: {len(CASES) + 1 - failed} of {len(CASES) + 1} checks passed')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
