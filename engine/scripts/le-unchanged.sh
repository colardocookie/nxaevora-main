#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the ScarletNX contributors
# Shows that a change for big-endian hosts leaves a little-endian host's code as it was (docs/PPC64.md): it builds
# BASE and the working tree with the default preset, then compares the disassembly of every object of the project
# (not of the third-party packages).
# Run it on an x86-64 or AArch64 machine. It prints each object whose code differs: every one must be a file the change
# meant to change, and the commit says why.
#
#   scripts/le-unchanged.sh [BASE]     (BASE: default the merge base with origin/main)
set -euo pipefail

root=$(git rev-parse --show-toplevel)
base=${1:-$(git -C "$root" merge-base HEAD origin/main)}
work="$root/.cache/le-unchanged"
mkdir -p "$work"

# BASE as a worktree of its own, refreshed to the commit asked for
if [ -d "$work/src" ]; then
    git -C "$work/src" checkout -q --detach "$base"
else
    git -C "$root" worktree add -q --detach "$work/src" "$base"
fi

build() { # build SOURCE DIR
    (cd "$1" && cmake --preset default -B "$2" -DSNX_DEPLOY=OFF \
        -DCPM_SOURCE_CACHE="$root/.cache/cpm" -DCMAKE_C_COMPILER_LAUNCHER=ccache \
        -DCMAKE_CXX_COMPILER_LAUNCHER=ccache >"$2.configure.log" 2>&1) || {
        tail -20 "$2.configure.log"
        exit 1
    }
    cmake --build "$2" >"$2.build.log" 2>&1 || {
        grep -m 20 -E 'error|FAILED' "$2.build.log"
        exit 1
    }
}
# a build folder per commit: a folder another configuration built keeps objects this one doesn't make, which would
# show as changes
short=$(git -C "$root" rev-parse --short "$base")
basedir="$work/base-$short" headdir="$work/head"
echo "building $short and the working tree"
build "$work/src" "$basedir"
build "$root" "$headdir"

# the project's objects: everything under the build tree but the packages CPM fetched (in _deps, or compiled from the
# CPM cache, whose path differs between the two source trees)
objects() { (cd "$1" && find . -name '*.o' -not -path './_deps/*' -not -path '*/cpm/*' | sort); }
code() { llvm-objdump -d --no-show-raw-insn --no-leading-addr "$1" 2>/dev/null | tail -n +3; }
differ=0
while read -r o; do
    if [ ! -f "$basedir/$o" ]; then
        echo "new:     $o"
        continue
    fi
    if ! cmp -s <(code "$basedir/$o") <(code "$headdir/$o"); then
        echo "changed: $o"
        differ=1
    fi
done < <(objects "$headdir")
comm -23 <(objects "$basedir") <(objects "$headdir") | sed 's/^/gone:    /'
[ $differ = 0 ] && echo "little-endian code unchanged"
exit 0
