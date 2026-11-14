#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
# Builds the fixture of vk_rec_direct from the recorder's own file and runs every scenario with and without the
# recording thread, once under the address and undefined-behaviour sanitizers and once under the thread
# sanitizer. For a host with gcc or clang and pthreads. Usage: tests/vk_record/run.sh [build directory]
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
out=${1:-$(mktemp -d)}
cc=${CC:-gcc}
sources="$root/src/nx/gpu/vk/record.c $root/src/common/filesystem/host.c $here/fixture.c"
flags="-std=gnu11 -Wall -Wextra -Werror -g -O1 -UNDEBUG -I$here/include -I$root/src"
$cc $flags -fsanitize=address,undefined -fno-sanitize-recover=all -o "$out/vk_record_asan" $sources -lpthread
$cc $flags -fsanitize=thread -o "$out/vk_record_tsan" $sources -lpthread
# the thread sanitizer of some compilers cannot place its memory with the kernel's address randomisation on
fixed=""
if command -v setarch >/dev/null 2>&1; then fixed="setarch $(uname -m) -R"; fi
failed=0
for build in asan tsan; do
    for mode in thread once; do
        for scenario in order intruder failed-begin failed-end failed-submit refused no-buffer wrap barrier; do
            run="$out/vk_record_$build"
            if [ $build = tsan ]; then run="$fixed $run"; fi
            if ! TSAN_OPTIONS=halt_on_error=1 $run $mode $scenario >"$out/last.txt" 2>&1; then
                echo "FAIL $build $mode $scenario"
                sed -n '1,25p' "$out/last.txt"
                failed=1
            fi
        done
    done
done
[ $failed = 0 ] && echo "vk_record: ok (9 scenarios, 2 modes, address and thread sanitizers)"
exit $failed
