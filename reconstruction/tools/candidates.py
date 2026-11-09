#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
"""Lists main-module functions the harness can verify as they are: leaf functions (no calls, no branches leaving
the function, no SVC or system registers), ranked by how many direct branches reach them. The manifest's entries
are left out.

    candidates.py --functions FUNCTIONS.TSV --main MAIN.BIN [--max-size 200] [--top 50] [--csv OUT]
"""

import argparse
import collections
import csv
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MANIFEST = os.path.join(os.path.dirname(HERE), "manifest", "functions.csv")
MAIN_BASE, MAIN_END = 0x8005000, 0xC312000


def branch_target(word, pc):
    if word & 0x7C000000 == 0x14000000:  # B and BL
        imm = word & 0x3FFFFFF
        return pc + ((imm - (1 << 26)) if imm & (1 << 25) else imm) * 4
    return None


def is_leaf(words, start):
    """True when nothing in the function leaves it except RET. Reports how many ADRP (data references) it has."""
    adrp = 0
    end = start + 4 * len(words)
    for i, w in enumerate(words):
        pc = start + 4 * i
        if w & 0xFC000000 == 0x94000000:  # BL
            return None
        if w & 0xFFFFFC1F in (0xD63F0000, 0xD61F0000):  # BLR, BR
            return None
        if w & 0xFFFFFC1F == 0xD65F0000 and w != 0xD65F03C0:  # RET through another register
            return None
        if w & 0xFFE0001F == 0xD4000001 or w & 0xFFD00000 == 0xD5100000:  # SVC, MSR and MRS
            return None
        t = branch_target(w, pc)
        if t is None and w & 0xFF000010 == 0x54000000:  # B.cond
            imm = (w >> 5) & 0x7FFFF
            t = pc + ((imm - (1 << 19)) if imm & (1 << 18) else imm) * 4
        if t is not None and not start <= t < end:
            return None
        if w & 0x9F000000 == 0x90000000:
            adrp += 1
    return adrp


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--functions", required=True)
    p.add_argument("--main", required=True)
    p.add_argument("--max-size", type=int, default=200)
    p.add_argument("--top", type=int, default=50)
    p.add_argument("--csv")
    a = p.parse_args()
    image = open(a.main, "rb").read()
    done = {int(e["offset"], 16) for e in csv.DictReader(open(MANIFEST))}
    fns = []
    for line in open(a.functions):
        parts = line.split()
        if len(parts) >= 2 and MAIN_BASE <= int(parts[0], 16) < MAIN_END:
            fns.append((int(parts[0], 16) - MAIN_BASE, int(parts[1])))
    text_end = max(o + s for o, s in fns)
    callers = collections.Counter()
    for i in range(0, text_end, 4):
        t = branch_target(struct.unpack_from("<I", image, i)[0], i)
        if t is not None:
            callers[t] += 1
    rows = []
    for off, size in fns:
        if off in done or not 4 <= size <= a.max_size or size % 4:
            continue
        adrp = is_leaf(struct.unpack_from("<%dI" % (size // 4), image, off), off)
        if adrp is not None:
            rows.append((callers[off], off, size, adrp))
    rows.sort(key=lambda r: (-r[0], r[1]))
    print("%d leaf candidates up to %d bytes (%d with no data reference)" %
          (len(rows), a.max_size, sum(1 for r in rows if r[3] == 0)))
    for c, off, size, adrp in rows[:a.top]:
        print("main+%-8x %4d bytes  %6d callers  %d ADRP" % (off, size, c, adrp))
    if a.csv:
        with open(a.csv, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["offset", "size", "callers", "adrp"])
            for c, off, size, adrp in rows:
                w.writerow(["%x" % off, size, c, adrp])
    return 0


if __name__ == "__main__":
    sys.exit(main())
