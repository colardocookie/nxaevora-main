#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
# Builds the concurrent case of the game's validation lines and runs it once under the address and
# undefined-behaviour sanitizers and once under the thread sanitizer. For a host with gcc or clang and pthreads.
# The cases that need no thread are in tests/temporal_math.c. Usage: tests/validation_lines/run.sh [build directory]
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
out=${1:-$(mktemp -d)}
mkdir -p "$out"
cc=${CC:-gcc}
flags="-std=gnu11 -Wall -Wextra -Werror -g -O1 -UNDEBUG -I$root/src"
$cc $flags -fsanitize=address,undefined -fno-sanitize-recover=all -o "$out/validation_lines_asan" \
    "$here/concurrent.c" -lpthread
$cc $flags -fsanitize=thread -o "$out/validation_lines_tsan" "$here/concurrent.c" -lpthread
# the thread sanitizer of some compilers cannot place its memory with the kernel's address randomisation on
fixed=""
if command -v setarch >/dev/null 2>&1; then fixed="setarch $(uname -m) -R"; fi
"$out/validation_lines_asan"
TSAN_OPTIONS=halt_on_error=1 $fixed "$out/validation_lines_tsan"
echo "validation_lines: ok (address and thread sanitizers)"
