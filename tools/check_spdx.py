# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
"""Checks the license header of the tracked files. Run from the repository root:

    python tools/check_spdx.py

A file in scope needs a license line and a copyright line in the comment form of its language, within the first ten
lines. A file may start with a UTF-8 byte order mark, which the check skips. Each header part must be one complete
comment line, so a multi-line block comment is not accepted. The check prints one ``<path>: <reason>`` line for each
missing part, then a one-line total. The exit code is 0 when every header is correct, 1 when a header is missing or
wrong, and 2 when the check itself fails.
"""

import re
import sys

from repo_files import (
    EXIT_ERROR,
    CheckError,
    file_name,
    file_suffix,
    list_tracked_files,
    read_file_error,
    report,
)

# A shebang may take line 1, so the header has ten lines for both parts.
HEADER_WINDOW_LINES = 10
LICENSE_ID = "GPL-3.0-or-later"

CHECKED_SUFFIXES = frozenset({
    ".py", ".md", ".yml", ".yaml", ".sh", ".ps1", ".toml", ".cmake",
    ".c", ".h", ".cpp", ".hpp", ".patch", ".pchtxt",
})
CHECKED_NAMES = frozenset({"CMakeLists.txt", ".gitignore"})

# The vendored engine keeps the upstream copyright lines. A later issue covers them.
EXCLUDED_PREFIXES = ("engine/",)
# Files in scope that cannot hold a correct comment header. Each entry needs its reason.
EXCLUDED_FILES = {}

HASH_SUFFIXES = frozenset({".py", ".yml", ".yaml", ".sh", ".ps1", ".toml", ".cmake", ".patch", ".pchtxt"})
HTML_SUFFIXES = frozenset({".md"})
SLASH_SUFFIXES = frozenset({".c", ".h", ".cpp", ".hpp"})

SPDX_PREFIX = "SPDX-License-Identifier:"
COPYRIGHT_PREFIX = "Copyright"
SPDX_LINE = re.compile(r"SPDX-License-Identifier:\s*" + re.escape(LICENSE_ID))
COPYRIGHT_LINE = re.compile(r"Copyright\s+\d{4}(?:-\d{4})?\s+the NXAevora contributors")


def comment_form(path):
    if file_name(path) in CHECKED_NAMES:
        return "hash"
    suffix = file_suffix(path)
    if suffix in HASH_SUFFIXES:
        return "hash"
    if suffix in HTML_SUFFIXES:
        return "html"
    if suffix in SLASH_SUFFIXES:
        return "slash"
    return None


def is_in_scope(path):
    if path in EXCLUDED_FILES or path.startswith(EXCLUDED_PREFIXES):
        return False
    if file_name(path) in CHECKED_NAMES:
        return True
    return file_suffix(path) in CHECKED_SUFFIXES


def strip_comment(form, line):
    stripped = line.strip()
    if form == "hash":
        if stripped.startswith("#"):
            return stripped[1:]
        return None
    if form == "html":
        if stripped.startswith("<!--") and stripped.endswith("-->"):
            return stripped[4:-3]
        return None
    if stripped.startswith("//"):
        return stripped[2:]
    if stripped.startswith("/*") and stripped.endswith("*/"):
        return stripped[2:-2]
    return None


def comment_contents(path, data):
    form = comment_form(path)
    if form is None:
        return []
    lines = data.decode("utf-8-sig", "replace").splitlines()[:HEADER_WINDOW_LINES]
    contents = []
    for line in lines:
        content = strip_comment(form, line)
        if content is not None:
            contents.append(content.strip())
    return contents


def classify(path, data):
    """Report the missing or wrong header parts of one file, as a list of reasons."""
    if not is_in_scope(path):
        return []
    contents = comment_contents(path, data)
    reasons = []
    if not any(SPDX_LINE.fullmatch(content) for content in contents):
        if any(SPDX_PREFIX in content for content in contents):
            reasons.append("license line does not name %s" % LICENSE_ID)
        else:
            reasons.append("missing license line %s %s" % (SPDX_PREFIX, LICENSE_ID))
    if not any(COPYRIGHT_LINE.fullmatch(content) for content in contents):
        if any(COPYRIGHT_PREFIX in content for content in contents):
            reasons.append("copyright line is not 'Copyright <year> the NXAevora contributors'")
        else:
            reasons.append("missing copyright line")
    return reasons


def read_file(path):
    try:
        with open(path, "rb") as handle:
            return handle.read()
    except OSError as exc:
        raise read_file_error(path, exc) from exc


def run():
    paths = list_tracked_files()
    checked = 0
    violations = []
    for path in paths:
        if not is_in_scope(path):
            continue
        checked += 1
        for reason in classify(path, read_file(path)):
            violations.append((path, reason))
    return report(violations, checked)


def main():
    try:
        return run()
    except CheckError as exc:
        print("error: %s" % exc, file=sys.stderr)
        return EXIT_ERROR


if __name__ == "__main__":
    sys.exit(main())
