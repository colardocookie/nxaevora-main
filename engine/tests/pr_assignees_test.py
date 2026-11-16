#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the ScarletNX contributors
"""scripts/pr-assignees.py on the pull requests it meets: none assigned, the author alone, an assignee's approval
and its later change of mind, a dismissal, a comment after an approval, two assignees with one approved, a review by
someone not assigned; the message names who it waits for; input it can't read is refused. Offline."""

import json
import subprocess
import sys
from pathlib import Path

SCRIPT = Path(__file__).resolve().parent.parent / 'scripts' / 'pr-assignees.py'


def pr(assignees, *reviews, author='writer'):
    return {'author': author, 'assignees': assignees, 'reviews': [{'user': u, 'state': s} for u, s in reviews]}


CASES = [
    ('no one assigned', pr([]), 0, None),
    ('the author alone', pr(['writer']), 0, None),
    ('the author, whatever its say', pr(['Writer'], ('writer', 'COMMENTED')), 0, None),
    ('an assignee who approved', pr(['ana'], ('ana', 'APPROVED')), 0, None),
    ('an assignee yet to review', pr(['ana']), 1, '@ana'),
    ('approved, then changes asked', pr(['ana'], ('ana', 'APPROVED'), ('ana', 'CHANGES_REQUESTED')), 1, '@ana'),
    ('changes asked, then approved', pr(['ana'], ('ana', 'CHANGES_REQUESTED'), ('ana', 'APPROVED')), 0, None),
    ('an approval dismissed', pr(['ana'], ('ana', 'APPROVED'), ('ana', 'DISMISSED')), 1, '@ana'),
    ('a comment after an approval', pr(['ana'], ('ana', 'APPROVED'), ('ana', 'COMMENTED')), 0, None),
    ('the login in another case', pr(['Ana'], ('ana', 'APPROVED')), 0, None),
    ('two assigned, one approved', pr(['ana', 'bia'], ('ana', 'APPROVED')), 1, '@bia'),
    ('approved by someone not assigned', pr(['ana'], ('bia', 'APPROVED')), 1, '@ana'),
    ('the author and an approver', pr(['writer', 'ana'], ('ana', 'APPROVED')), 0, None),
]


def run(*args, data=None):
    return subprocess.run([sys.executable, str(SCRIPT), *args], input=data, capture_output=True, text=True)


def main():
    failed = 0
    for name, case, want, named in CASES:
        r = run('-', data=json.dumps(case))
        if r.returncode != want or (named and named not in r.stderr) or (want == 1 and '@writer' in r.stderr):
            print(f'FAIL {name}: exit {r.returncode}, wanted {want}; {r.stdout.strip()} {r.stderr.strip()}')
            failed += 1
    refused = [
        run(),
        run('a', 'b'),
        run('-', data='not json'),
        run('-', data=json.dumps({'author': 'writer'})),
        run('-', data=json.dumps({'author': 'writer', 'assignees': [], 'reviews': [{'state': 'APPROVED'}]})),
    ]
    for i, r in enumerate(refused):
        if r.returncode != 2:
            print(f'FAIL refusal {i}: exit {r.returncode}, wanted 2')
            failed += 1
    total = len(CASES) + len(refused)
    print(f'pr_assignees: {total - failed} of {total} checks passed')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
