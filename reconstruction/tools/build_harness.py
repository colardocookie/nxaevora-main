#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
"""Builds the differential verifier (recon-verify) against a ScarletNX build configured with SNX_BUILD_TESTING=ON in
which the target snx_codegen_test was built. It takes that target's compile flags and link line from Ninja, so the
LLVM backend, the loader and their libraries are exactly the ones ScarletNX tests itself with.

    build_harness.py --build SCARLETNX_BUILD --ninja NINJA [--out DIR]
"""

import argparse
import os
import shlex
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SOURCES = [
    "harness/verify.c",
    "harness/imports.c",
    "harness/callees_lua53.c",
    "harness/fixtures_lua53.c",
    "harness/mutants_lua53.c",
    "src/lua53/lapi.c",
    "src/lua53/lapi_guestfn.c",
]


def commands(ninja, build):
    out = subprocess.run([ninja, "-C", build, "-t", "commands", "tests/snx_codegen_test.exe"], check=True,
                         capture_output=True, text=True).stdout
    return out.splitlines()


def split(line):
    return shlex.split(line, posix=False)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--build", required=True)
    p.add_argument("--ninja", required=True)
    p.add_argument("--out", default=os.path.join(ROOT, "build"))
    a = p.parse_args()
    lines = commands(a.ninja, a.build)
    compile_line = next(l for l in lines if l.rstrip().endswith("llvm_steps.c"))
    link_line = next(l for l in lines if "snx_codegen_test.exe" in l and "llvm_steps.c.obj" in l and " -c " not in l)
    tokens = split(compile_line)
    compiler = tokens[0]
    flags, i = [], 1
    while i < len(tokens):
        t = tokens[i]
        if t.startswith(("-I", "-D", "-m")) or t in ("-pthread",):
            flags.append(t)
        elif t == "-isystem":
            flags += [t, tokens[i + 1]]
            i += 1
        i += 1
    llvm_steps = tokens[-1]
    tests_dir = os.path.dirname(llvm_steps)
    os.makedirs(a.out, exist_ok=True)
    objects = []
    for src in SOURCES + [llvm_steps]:
        path = src if os.path.isabs(src) or src == llvm_steps else os.path.join(ROOT, src)
        obj = os.path.join(a.out, os.path.basename(path) + ".obj")
        std = "-std=c99" if path == llvm_steps else "-std=c11"
        cmd = [compiler] + flags + [std, "-O2", "-g", "-ffp-contract=off", "-I" + os.path.join(ROOT, "include"),
                                    "-I" + tests_dir, "-c", path, "-o", obj]
        print(" ".join(os.path.basename(c) if c == compiler else c for c in cmd[:1]), os.path.relpath(path, ROOT)
              if path.startswith(ROOT) else path)
        subprocess.run(cmd, check=True)
        objects.append(obj)
    # The link line, run from the build directory as Ninja does, with our objects and output in place of the test's
    # On Windows Ninja wraps it as cmd.exe /C "cd . && <link> && cd ."
    if "&&" in link_line:
        link_line = link_line.split("&&")[1]
    link = [t.strip('"') for t in split(link_line.strip())]
    exe = os.path.join(a.out, "recon-verify.exe" if os.name == "nt" else "recon-verify")
    out_link, skip = [], False
    for t in link:
        if skip:
            skip = False
            continue
        if t.endswith(".obj") and "snx_codegen_test.dir" in t:
            continue
        if t == "-o":
            out_link += ["-o", exe]
            skip = True
            continue
        out_link.append(t)
    out_link[1:1] = objects
    subprocess.run(out_link, check=True, cwd=a.build)
    print("built", exe)


if __name__ == "__main__":
    sys.exit(main())
