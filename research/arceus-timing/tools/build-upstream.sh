#!/bin/bash
# Build the upstream ScarletNX copy with the Arceus clock port, plus its host test.
set -u
W="C:/Users/brett/Documents/Codex/2026-11-07/continue-from-renderer-handoff-txt/work"
T="C:/Users/brett/Documents/Codex/work"
SRC="C:/Users/brett/Documents/Codex/2026-11-06/files-pasted-by-the-user-project/work/nxaevora-upstream/third_party/ScarletNX"
OUT="$W/upstream-build"
export PATH="/c/Users/brett/Documents/Codex/work/llvm-mingw-20261104-ucrt-x86_64/bin:/c/Users/brett/Documents/Codex/work/nvn-src/deps/tools/w64devkit/bin:$PATH"
export TEMP="$W/compiler-temp" TMP="$W/compiler-temp" TMPDIR="$W/compiler-temp"
CM="$T/nvn-src/deps/tools/cmake-4.4.4-windows-x86_64/bin/cmake.exe"
LL="$T/llvm-mingw-20261104-ucrt-x86_64/bin"
"$CM" -S "$SRC" -B "$OUT" -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_MAKE_PROGRAM="$T/nvn-src/deps/tools/ninja-1.13.2/ninja.exe" \
  -DCMAKE_C_COMPILER="$LL/clang.exe" -DCMAKE_CXX_COMPILER="$LL/clang++.exe" -DCMAKE_RC_COMPILER="$LL/windres.exe" \
  -DCMAKE_C_FLAGS=-mcx16 \
  -DCPM_SOURCE_CACHE="$T/nvn-src/.cache/cpm" \
  -DSNX_DEPS_CACHE="$W/depcache" -DPERL_EXECUTABLE="$T/nvn-src/deps/tools/strawberry-perl-5.42.3.1/perl/bin/perl.exe" -DSNX_BUILD_NX=ON -DSNX_BUILD_TESTING=ON -DBUILD_TESTING=ON -DSNX_FFMPEG_ASM=ON   -DSNX_NASM="$T/nvn-src/deps/tools/nasm-3.02/nasm.exe"   -DSNX_FFMPEG_MAKE="$T/nvn-src/deps/tools/w64devkit/bin/make.exe"   -DSNX_FFMPEG_SH="$T/nvn-src/deps/tools/w64devkit/bin/sh.exe"   -DSNX_OPENSSL_MAKE="$T/nvn-src/deps/tools/w64devkit/bin/make.exe"   -DSNX_GLSLANG="$T/nvn-src/.cache/cpm/glslang-tools/819b/bin/glslang.exe"   -DSNX_LLVM_CONFIG="$T/msys64/clang64/bin/llvm-config.exe"   -DSNX_LLVM_OPT="$T/msys64/clang64/bin/opt.exe"   -DSNX_OPT_FLAGS="-O2 -g"   > "$W/upstream-config.log" 2>&1 || { echo CONFIG FAILED; tail -30 "$W/upstream-config.log"; exit 1; }
"$CM" --build "$OUT" --target scarletnx snx-test-arceus-timing > "$W/upstream-build.log" 2>&1
rc=$?
echo "build exit $rc"
tail -6 "$W/upstream-build.log"
grep -n "arceus" "$W/upstream-build.log" | grep -i -e warning -e error | head -10
[ $rc -eq 0 ] && (cd "$OUT" && "$T/nvn-src/deps/tools/cmake-4.4.4-windows-x86_64/bin/ctest.exe" -R arceus_timing --output-on-failure | tail -5)
exit $rc
