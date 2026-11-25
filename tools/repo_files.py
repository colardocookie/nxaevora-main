# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
"""Shared helpers of the repository checks on the tracked files.

``check_spdx.py`` and ``check_game_data.py`` both list the tracked files and print the same report. This module holds
that code once. The scripts run from the repository root, and the tests put the tools folder on ``sys.path``, so a
plain ``import repo_files`` works in both cases.
"""

import subprocess

EXIT_CLEAN = 0
EXIT_VIOLATION = 1
EXIT_ERROR = 2

# A gitlink stores the commit of another repository, so its path is not a file in this tree.
GITLINK_MODE = "160000"


class CheckError(Exception):
    """An internal error of the check, with the stage that failed."""

    def __init__(self, stage, detail):
        super().__init__("%s: %s" % (stage, detail))
        self.stage = stage
        self.detail = detail


def list_tracked_files():
    try:
        result = subprocess.run(
            ["git", "ls-files", "-s", "-z"],
            capture_output=True,
            check=False,
        )
    except OSError as exc:
        raise CheckError("git ls-files", str(exc)) from exc
    if result.returncode != 0:
        detail = result.stderr.decode("utf-8", "replace").strip()
        raise CheckError("git ls-files", detail or "exit code %d" % result.returncode)
    text = result.stdout.decode("utf-8", "replace")
    paths = []
    for entry in text.split("\0"):
        if not entry:
            continue
        metadata, _, path = entry.partition("\t")
        if not path:
            continue
        mode = metadata.split(" ", 1)[0]
        if mode == GITLINK_MODE:
            continue
        paths.append(path)
    return paths


def read_file_error(path, exc):
    """Build the error for a tracked file that cannot be read."""
    if isinstance(exc, FileNotFoundError):
        detail = "tracked file is missing from the working tree (commit the deletion or restore the file)"
    else:
        detail = str(exc)
    return CheckError("read " + path, detail)


def file_name(path):
    return path.rsplit("/", 1)[-1]


def file_suffix(path):
    name = file_name(path)
    dot = name.rfind(".")
    if dot <= 0:
        return ""
    return name[dot:].lower()


def report(violations, checked):
    for path, reason in violations:
        print("%s: %s" % (path, reason))
    print("%d files checked, %d violations" % (checked, len(violations)))
    return EXIT_CLEAN if not violations else EXIT_VIOLATION
