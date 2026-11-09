#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
"""Reconstruction progress against the game's function inventory, with every claim checked.

    progress.py --functions FUNCTIONS.TSV --main MAIN.BIN --results RESULTS.CSV [--main-base 0x8005000]

FUNCTIONS.TSV is the function inventory (guest address and size per line, the recompiler's discovery), the
denominator. MAIN.BIN is the main module dump the verifier ran on. RESULTS.CSV is recon-verify's --results output.
An entry of the manifest counts as verified only when its offset and size are a function of the inventory, its
bytes in MAIN.BIN still hash to the manifest's value, and the results show it passing at least --min-runs runs on
those same bytes. Anything else is reported, and the exit status is 1.
"""

import argparse
import csv
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MANIFEST = os.path.join(os.path.dirname(HERE), "manifest", "functions.csv")
MODULES = [("main", 0x8005000, 0xC312000), ("subsdk0", 0xC312000, 0xC9B9000), ("subsdk1", 0xC9B9000, 0xD382000),
           ("sdk", 0xD382000, 0x20000000)]


def fnv1a(data):
    h = 0xCBF29CE484222325
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--functions", required=True)
    p.add_argument("--main", required=True)
    p.add_argument("--results", required=True)
    p.add_argument("--main-base", type=lambda s: int(s, 0), default=0x8005000)
    p.add_argument("--min-runs", type=int, default=10000)
    a = p.parse_args()

    inventory, sizes = {}, {}
    for line in open(a.functions):
        parts = line.split()
        if len(parts) < 2:
            continue
        addr, size = int(parts[0], 16), int(parts[1])
        for name, lo, hi in MODULES:
            if lo <= addr < hi:
                inventory.setdefault(name, []).append(size)
                if name == "main":
                    sizes[addr - a.main_base] = size
                break
    image = open(a.main, "rb").read()
    results = {}
    for r in csv.DictReader(open(a.results)):
        results[(int(r["main_offset"], 16), int(r["size"]))] = r

    problems, verified = [], []
    for e in csv.DictReader(open(MANIFEST)):
        off, size = int(e["offset"], 16), int(e["size"])
        where = f'{e["name"]} (main+{off:x})'
        if e["module"] != "main":
            problems.append(f"{where}: only main entries are checked so far")
            continue
        if sizes.get(off) != size:
            problems.append(f"{where}: not a {size}-byte function of the inventory")
            continue
        h = f"{fnv1a(image[off:off + size]):016x}"
        if h != e["bytes_fnv1a"]:
            problems.append(f"{where}: bytes hash {h}, manifest {e['bytes_fnv1a']}")
            continue
        if e["status"] == "verified":
            r = results.get((off, size))
            if not r or r["result"] != "pass" or r["bytes_fnv1a"] != h or int(r["runs"]) < a.min_runs:
                problems.append(f"{where}: no passing result of {a.min_runs}+ runs on these bytes")
                continue
            verified.append((e, size))

    total_fns = sum(len(v) for v in inventory.values())
    total_bytes = sum(sum(v) for v in inventory.values())
    main_fns, main_bytes = len(inventory.get("main", [])), sum(inventory.get("main", []))
    vb = sum(s for _, s in verified)
    print("Function inventory: %d functions, %d bytes (main %d functions, %d bytes)" %
          (total_fns, total_bytes, main_fns, main_bytes))
    for name, _, _ in MODULES:
        if name in inventory:
            print("  %-8s %7d functions %10d bytes" % (name, len(inventory[name]), sum(inventory[name])))
    print("Verified reconstructions: %d functions, %d bytes" % (len(verified), vb))
    print("  of main: %.4f%% of functions, %.5f%% of bytes" %
          (100.0 * len(verified) / max(main_fns, 1), 100.0 * vb / max(main_bytes, 1)))
    libraries = {}
    for e, s in verified:
        libraries.setdefault(e["library"], []).append(s)
    for lib, ss in sorted(libraries.items()):
        print("  %-20s %3d functions %6d bytes" % (lib, len(ss), sum(ss)))
    for msg in problems:
        print("PROBLEM:", msg)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
