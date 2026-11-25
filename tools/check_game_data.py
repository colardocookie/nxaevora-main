# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
"""Guards that no game data and no key is tracked. Run from the repository root:

    python tools/check_game_data.py

Each tracked file is checked by name, path, size, header magic and key text. The check prints one ``<path>: <reason>``
line for each violation, then a one-line total. The exit code is 0 when every file passes, 1 when a file fails, and 2
when the check itself fails.
"""

import os
import re
import sys

from repo_files import (
    EXIT_ERROR,
    CheckError,
    file_suffix,
    list_tracked_files,
    read_file_error,
    report,
)

FORBIDDEN_SUFFIXES = frozenset({
    ".nsp", ".nsz", ".xci", ".xcz", ".nca", ".ncz", ".nso", ".nro",
    ".nrr", ".npdm", ".nacp", ".keys", ".tik", ".cert",
})
FORBIDDEN_NAMES = frozenset({"prod.keys", "title.keys", "dev.keys", "main.npdm"})
FORBIDDEN_FIRST_PART = "game"
FORBIDDEN_PARTS = frozenset({"exefs", "romfs"})

CACHE_PART = "__pycache__"
CACHE_SUFFIXES = frozenset({".pyc", ".pyo"})

MAGIC_HEADER_SIZE = 0x104
XCI_HEAD_OFFSET = 0x100
XCI_HEAD_MAGIC = b"HEAD"
MAGIC_SIGNATURES = (
    (0x0, b"PFS0"),
    (0x0, b"HFS0"),
    (0x0, b"NSO0"),
    (0x10, b"NRO0"),
)

# A key file is text and small, so the key scan reads only the files below this size.
KEY_SCAN_MAX_SIZE = 1024 * 1024
MAX_FILE_SIZE = 5 * 1024 * 1024
# The name holds key or kek and the value is a hex key. Name characters are ASCII only.
KEY_LINE = re.compile(
    r"^\s*[A-Za-z0-9_]*(?:key|kek)[A-Za-z0-9_]*\s*=\s*[0-9A-Fa-f]{32,}[ ]*$",
    re.IGNORECASE,
)

# Files that fail a check and are clearly not game data. Each entry needs the check and a reason.
EXCLUDED_FILES = {}


def path_parts(path):
    return path.split("/")


def classify_path(path):
    parts = path_parts(path)
    name = parts[-1]
    suffix = file_suffix(path)
    reasons = []
    if name.lower() in FORBIDDEN_NAMES:
        reasons.append("forbidden file name " + name)
    elif suffix in FORBIDDEN_SUFFIXES:
        reasons.append("forbidden suffix " + suffix)
    if len(parts) > 1 and parts[0].lower() == FORBIDDEN_FIRST_PART:
        reasons.append("game data folder " + FORBIDDEN_FIRST_PART)
    for part in parts[:-1]:
        if part.lower() in FORBIDDEN_PARTS:
            reasons.append("forbidden path part " + part)
    if CACHE_PART in parts[:-1] or suffix in CACHE_SUFFIXES:
        reasons.append("python cache file")
    return reasons


def is_binary(data):
    """A byte range is binary when it holds a NUL byte or is not valid UTF-8."""
    if b"\0" in data:
        return True
    try:
        data.decode("utf-8")
    except UnicodeDecodeError:
        return True
    return False


def classify_magic(data):
    reasons = []
    for offset, signature in MAGIC_SIGNATURES:
        if data[offset:offset + len(signature)] == signature:
            reasons.append("%s magic at offset %#x" % (signature.decode("ascii"), offset))
    # A text file can hold HEAD at 0x100 by chance, so this magic counts only after binary bytes.
    if data[XCI_HEAD_OFFSET:XCI_HEAD_OFFSET + len(XCI_HEAD_MAGIC)] == XCI_HEAD_MAGIC \
            and is_binary(data[:XCI_HEAD_OFFSET]):
        reasons.append("%s magic at offset %#x" % (XCI_HEAD_MAGIC.decode("ascii"), XCI_HEAD_OFFSET))
    return reasons


def classify_keys(data):
    text = data.decode("utf-8", "replace")
    for line in text.splitlines():
        if KEY_LINE.match(line):
            return ["key material line"]
    return []


def classify_size(size):
    if size > MAX_FILE_SIZE:
        return ["file size %d bytes is above %d" % (size, MAX_FILE_SIZE)]
    return []


def classify(path, data, size):
    """Report each violation of one file, from its path, header bytes and size."""
    if path in EXCLUDED_FILES:
        return []
    reasons = classify_path(path)
    reasons.extend(classify_magic(data))
    if size < KEY_SCAN_MAX_SIZE:
        reasons.extend(classify_keys(data))
    reasons.extend(classify_size(size))
    return reasons


def read_file(path):
    """Read the whole text of a small file, or its header magic of a larger one."""
    try:
        size = os.path.getsize(path)
        with open(path, "rb") as handle:
            if size < KEY_SCAN_MAX_SIZE:
                return size, handle.read()
            return size, handle.read(MAGIC_HEADER_SIZE)
    except OSError as exc:
        raise read_file_error(path, exc) from exc


def run():
    paths = list_tracked_files()
    violations = []
    for path in paths:
        size, data = read_file(path)
        for reason in classify(path, data, size):
            violations.append((path, reason))
    return report(violations, len(paths))


def main():
    try:
        return run()
    except CheckError as exc:
        print("error: %s" % exc, file=sys.stderr)
        return EXIT_ERROR


if __name__ == "__main__":
    sys.exit(main())
