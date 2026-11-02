#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the ScarletNX contributors
"""pr-assignees.py FILE: fails while someone assigned to a pull request, its author aside, has not approved it, so a
pull request that touches someone's pending work waits for their approval; taking them off the assignees lets it go.
FILE (- for stdin) is the pull request as .github/workflows/pr-checks.yml reads it from GitHub:
{"author": LOGIN, "assignees": [LOGIN, ...], "reviews": [{"user": LOGIN, "state": STATE}, ...]}, the reviews oldest
first. A reviewer's say is their last review that approves, asks for changes or was dismissed; a comment changes
nothing, as on GitHub."""

import json
import sys

DECIDES = {'APPROVED', 'CHANGES_REQUESTED', 'DISMISSED'}


def waiting(pr):
    """the assignees, author aside, whose say is not an approval, in the order they were assigned"""
    say = {}
    for review in pr['reviews']:
        if review['state'] in DECIDES:
            say[review['user'].lower()] = review['state']
    author = pr['author'].lower()
    return [a for a in pr['assignees'] if a.lower() != author and say.get(a.lower()) != 'APPROVED']


def read(path):
    text = sys.stdin.read() if path == '-' else open(path, encoding='utf-8').read()
    pr = json.loads(text)
    if not isinstance(pr.get('author'), str) or not isinstance(pr.get('assignees'), list):
        raise ValueError('it has no "author" or no "assignees" list')
    if not isinstance(pr.get('reviews'), list) or not all(
        isinstance(r, dict) and isinstance(r.get('user'), str) and isinstance(r.get('state'), str)
        for r in pr['reviews']
    ):
        raise ValueError('its "reviews" are not a list of {"user", "state"}')
    return pr


def main(argv):
    if len(argv) != 2:
        print('usage: pr-assignees.py FILE (- for stdin)', file=sys.stderr)
        return 2
    try:
        pr = read(argv[1])
    except (OSError, ValueError) as e:
        print(f'pr-assignees.py: cannot read the pull request from {argv[1]}: {e}', file=sys.stderr)
        return 2
    left = waiting(pr)
    if left:
        names = ', '.join('@' + a for a in left)
        print(f'Assigned and not approved yet: {names}.', file=sys.stderr)
        print('Each of them approves it, or is taken off the assignees, before it merges.', file=sys.stderr)
        return 1
    others = [a for a in pr['assignees'] if a.lower() != pr['author'].lower()]
    print(f'Approved by every assignee: {", ".join("@" + a for a in others)}.' if others else 'No one else assigned.')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
