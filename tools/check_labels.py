# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
"""Checks the type, size and priority labels of a GitHub issue or pull request event. Run from the repository root:

    python tools/check_labels.py --check --event event.json
    python tools/check_labels.py --triage

A pull request needs at least one type label, exactly one size label and exactly one priority label. Mode ``--check``
prints each problem and exits with 1 when a problem exists, or 0 when the labels are valid. Mode ``--triage`` prints
``needs-triage=true`` or ``needs-triage=false`` for the issue workflow and exits with 0. A missing or unreadable event
is an error with exit code 2.
"""

import argparse
import json
import os
import sys

from repo_files import EXIT_CLEAN, EXIT_ERROR, EXIT_VIOLATION, CheckError

TYPE_LABELS = ("bug", "enhancement", "graphics", "documentation", "accessibility", "question")
SIZE_LABELS = ("size: small", "size: medium", "size: large")
PRIORITY_LABELS = ("priority: low", "priority: medium", "priority: high")

# Each group is (name, label names, minimum count, maximum count). A None maximum has no limit.
LABEL_GROUPS = (
    ("type", TYPE_LABELS, 1, None),
    ("size", SIZE_LABELS, 1, 1),
    ("priority", PRIORITY_LABELS, 1, 1),
)

EVENT_KEYS = ("pull_request", "issue")


def check_labels(labels):
    """Return the problems of one label collection, an empty list when the labels are valid."""
    present = set(labels)
    problems = []
    for group, names, minimum, maximum in LABEL_GROUPS:
        found = [name for name in names if name in present]
        if len(found) < minimum:
            problems.append("%s: missing" % group)
        elif maximum is not None and len(found) > maximum:
            problems.append("%s: more than one (%s)" % (group, ", ".join(found)))
    return problems


def event_labels(event):
    """Return the label names of a pull request or issue event."""
    for key in EVENT_KEYS:
        holder = event.get(key)
        if isinstance(holder, dict):
            return label_names(holder.get("labels"), key)
    raise CheckError("event labels", "the event has neither pull_request nor issue labels")


def label_names(entries, key):
    if entries is None:
        return []
    if not isinstance(entries, list):
        raise CheckError("event labels", "%s.labels is not a list" % key)
    names = []
    for entry in entries:
        if isinstance(entry, dict) and isinstance(entry.get("name"), str):
            names.append(entry["name"])
        elif isinstance(entry, str):
            names.append(entry)
        else:
            raise CheckError("event labels", "%s.labels has an entry without a name" % key)
    return names


def read_event(path):
    try:
        with open(path, "r", encoding="utf-8") as handle:
            event = json.load(handle)
    except OSError as exc:
        raise CheckError("read event " + path, str(exc)) from exc
    except ValueError as exc:
        raise CheckError("parse event " + path, str(exc)) from exc
    if not isinstance(event, dict):
        raise CheckError("parse event " + path, "the event is not a JSON object")
    return event


def build_parser():
    parser = argparse.ArgumentParser(description="Check the type, size and priority labels of a GitHub event.")
    parser.add_argument(
        "--event",
        default=os.environ.get("GITHUB_EVENT_PATH"),
        help="path of the event JSON file (default: GITHUB_EVENT_PATH)",
    )
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--check", action="store_true", help="print each problem and exit 1 on a problem (default)")
    mode.add_argument("--triage", action="store_true", help="print needs-triage=true or false and exit 0")
    return parser


def run(args):
    if not args.event:
        raise CheckError("event path", "give --event or set GITHUB_EVENT_PATH")
    problems = check_labels(event_labels(read_event(args.event)))
    if args.triage:
        print("needs-triage=%s" % ("true" if problems else "false"))
        return EXIT_CLEAN
    for problem in problems:
        print(problem)
    return EXIT_VIOLATION if problems else EXIT_CLEAN


def main(argv=None):
    args = build_parser().parse_args(argv)
    try:
        return run(args)
    except CheckError as exc:
        print("error: %s" % exc, file=sys.stderr)
        return EXIT_ERROR


if __name__ == "__main__":
    sys.exit(main())
