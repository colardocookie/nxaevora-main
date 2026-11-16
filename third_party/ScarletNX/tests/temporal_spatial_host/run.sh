#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
# Host regressions of the spatial hand-over's decision (gpu/vk/temporal.c) and its draw (gpu/vk/render.c), from the
# reviewer's D3 fixtures. The generators take the code out of the tree as it is and put stand-ins and a case file
# around it: readability.c (the required source, 112 cases), refusal.c (the refusal's log), render_pair.c (the pair
# and key). For a host with python3, clang and the Vulkan headers. Usage: run.sh [build dir] [Vulkan include dir]
set -eu
here=$(cd "$(dirname "$0")" && pwd)
src=$(cd "$here/../.." && pwd)
out=${1:-$(mktemp -d)}
vk=${2:-}
flags="-std=gnu11 -O1 -Wall -Wextra -Werror -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter"
flags="$flags -fsanitize=address,undefined -fno-sanitize-recover=all -I$src/src"
[ -n "$vk" ] && flags="$flags -I$vk"
for t in readability refusal; do
    python3 "$here/gen.py" "$src" "$here/$t.c" "$out/$t.c"
    ${CLANG:-clang} $flags -o "$out/$t" "$out/$t.c" -lm
    "$out/$t" | tail -n 1
done
python3 "$here/render_gen.py" "$src" "$here/render_pair.c" "$out/render_pair.c"
${CLANG:-clang} $flags -o "$out/render_pair" "$out/render_pair.c" -lm
"$out/render_pair" | tail -n 1
echo "temporal_spatial_host: ok"
