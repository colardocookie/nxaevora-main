#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
# Extract the current spatial decision and draw code with host stand-ins: required-source readability, refusal
# diagnostics, adapter fallback and the renderer's texture/program pair. runner_test.py checks this runner's status.
# For a host with python3, clang and the Vulkan headers. Usage: run.sh [build dir] [Vulkan include dir]
set -eu
here=$(cd "$(dirname "$0")" && pwd)
src=$(cd "$here/../.." && pwd)
out=${1:-$(mktemp -d)}
vk=${2:-}
flags="-std=gnu11 -O1 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter"
flags="$flags -fsanitize=address,undefined -fno-sanitize-recover=all -I$src/src"
[ -n "$vk" ] && flags="$flags -I$vk"
run_case() {
    if "$out/$1" >"$out/$1.log" 2>&1; then
        tail -n 1 "$out/$1.log"
    else
        status=$?
        cat "$out/$1.log" >&2
        return "$status"
    fi
}
for t in readability refusal fallback; do
    python3 "$here/gen.py" "$src" "$here/$t.c" "$out/$t.c"
    ${CLANG:-clang} $flags -o "$out/$t" "$out/$t.c" -lm
    run_case "$t"
done
python3 "$here/render_gen.py" "$src" "$here/render_pair.c" "$out/render_pair.c"
${CLANG:-clang} $flags -o "$out/render_pair" "$out/render_pair.c" -lm
run_case render_pair
echo "temporal_spatial_host: ok"
