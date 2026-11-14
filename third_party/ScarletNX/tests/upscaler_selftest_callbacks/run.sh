#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
# Host regressions for the self-test's validation callbacks (gpu/vk/upscaler_selftest.c), from the reviewer. Each
# generator takes the callback's code, its counts and the end of the self-test out of the source as it is and puts
# a test body around it. final.py: ids, caps, marks and case 7's last lines, under the address and
# undefined-behaviour sanitizers. concurrent.py: a callback thread against the counts, under the thread sanitizer.
# For a host with python3, clang, gcc and pthreads. Usage: tests/upscaler_selftest_callbacks/run.sh [build dir]
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
out=${1:-$(mktemp -d)}
flags="-std=gnu11 -Wall -Wextra -Werror -I$root/third_party/ScarletNX/src"
python3 "$here/final.py" "$root" "$out/final.c" >/dev/null
python3 "$here/concurrent.py" "$root" "$out/concurrent.c" >/dev/null
${CLANG:-clang} $flags -O2 -fsanitize=address,undefined -fno-sanitize-recover=all -o "$out/final" "$out/final.c"
"$out/final"
${CC:-gcc} $flags -O1 -fsanitize=thread -o "$out/concurrent" "$out/concurrent.c" -lpthread
# the thread sanitizer of some compilers cannot place its memory with the kernel's address randomisation on
fixed=""
if command -v setarch >/dev/null 2>&1; then fixed="setarch $(uname -m) -R"; fi
TSAN_OPTIONS=halt_on_error=1 $fixed "$out/concurrent"
echo "upscaler_selftest_callbacks: ok"
