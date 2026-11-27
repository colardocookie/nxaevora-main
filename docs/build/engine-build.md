<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- Copyright 2026 the NXAevora contributors -->

# Building NXAevora Engine (Windows, MSYS2 CLANG64)

This guide explains how to build `engine/`, NXAevora Engine, on Windows. The commands are the ones of the CI job
`Windows build and test` in [ci.yml](../../.github/workflows/ci.yml). The upstream steps are in
[engine/docs/BUILD.md](../../engine/docs/BUILD.md), in the section "Windows / MSYS2 CLANG64".

## Requirements

- Windows 11 x64.
- MSYS2, installed in `C:\msys64`. You can get it from [msys2.org](https://www.msys2.org/). Run every command of this
  guide in the **MSYS2 CLANG64** shell.
- A Vulkan 1.3 driver. You need it to run games.
- A path without spaces for the clone. The FFmpeg build fails when a path has a space.
- The current MSYS2 packages. This guide does not pin an LLVM version. It uses the version that MSYS2 ships, as the CI does.

## 1. Install the packages

Update MSYS2 and install the packages of the CI job. Run the commands in the MSYS2 CLANG64 shell.

```sh
pacman -Syu
pacman -S --needed git make patch perl patchutils \
  mingw-w64-clang-x86_64-clang mingw-w64-clang-x86_64-llvm mingw-w64-clang-x86_64-lld \
  mingw-w64-clang-x86_64-cmake mingw-w64-clang-x86_64-ninja mingw-w64-clang-x86_64-nasm \
  mingw-w64-clang-x86_64-pkgconf mingw-w64-clang-x86_64-python mingw-w64-clang-x86_64-spirv-tools \
  mingw-w64-clang-x86_64-glslang
```

If `pacman -Syu` closes the shell, see the Troubleshooting section.

## 2. Get the source

Clone the repository and open its `engine` folder. Every command in the following steps runs in that folder.

```sh
git clone https://github.com/nxaevora/nxaevora-main.git nxaevora
cd nxaevora/engine
```

## 3. Configure

The `default` preset in [engine/CMakePresets.json](../../engine/CMakePresets.json) selects the Clang compilers, the
Ninja generator and the build type `RelWithDebInfo`. The build folder is `engine/build/default`. The preset also turns
on `SNX_DEPLOY`. With this setting, each build copies the runtime next to every recompiled game (see the Options
table). This guide turns it off, as the CI does, because the CI has no recompiled game. Leave out
`-DSNX_DEPLOY=OFF` when you have recompiled games and want each build to update them.

The other options give CMake the MSYS2 tools as Windows paths. `cygpath -m` converts a path to the Windows form with
forward slashes. `-DCMAKE_C_FLAGS=-mcx16` is explained in the Troubleshooting section. The option
`-DCMAKE_DISABLE_FIND_PACKAGE_SDL3=ON` stops CMake from looking for a system copy of SDL3, so the pinned SDL3 is built
from source.

```sh
cmake --preset default \
  -DCMAKE_C_FLAGS=-mcx16 \
  -DSNX_LLVM_CONFIG="$(cygpath -m "$(command -v llvm-config)")" \
  -DSNX_FFMPEG_SH="$(cygpath -m /usr/bin/sh.exe)" \
  -DSNX_FFMPEG_MAKE="$(cygpath -m /usr/bin/make.exe)" \
  -DPERL_EXECUTABLE="$(cygpath -m /usr/bin/perl.exe)" \
  -DSNX_OPENSSL_MAKE="$(cygpath -m /usr/bin/make.exe)" \
  -DCMAKE_DISABLE_FIND_PACKAGE_SDL3=ON \
  -DSNX_DEPLOY=OFF
```

## 4. Build

The following command uses one job for each processor. The first build takes a long time, because FFmpeg and OpenSSL
are built from source. The Options table shows how to keep those two builds for a later build folder.

```sh
cmake --build --preset default --parallel "$(getconf _NPROCESSORS_ONLN)"
```

The output goes to `engine/build/default`. It contains `nxaevora-engine.exe`, the program, and `snx-shaderc.exe`, the
shader tool that translates captured shaders to SPIR-V.

## 5. Check the program

The following command starts the program with its help text. It needs no game. The CI runs the same command and needs it
to succeed.

```sh
build/default/nxaevora-engine.exe --help
```

## 6. Run the tests

The tests run with `ctest`. Set `APPDATA` before you run it. The tests that write settings use the folder that
`APPDATA` names, and that folder is a clean folder of their own. Without `APPDATA`, the tests `native_recompiler` and
`llvm_module` fail with the message `HOME is not set`.

```sh
mkdir -p /tmp/snx-appdata
export APPDATA="$(cygpath -w /tmp/snx-appdata)"
ctest --test-dir build/default --output-on-failure --no-tests=error --timeout 900 --parallel 4
```

The options of this `ctest` command have these effects. `--output-on-failure` prints the output of each failed test.
`--no-tests=error` makes `ctest` fail when it finds no test. `--timeout 900` stops a test after 900 seconds, and
`--parallel 4` runs four tests at a time.

## 7. Make a portable copy

`nxaevora-engine.exe` loads DLLs from the MSYS2 CLANG64 `bin` folder. To run the program without MSYS2, install it to a
folder, and copy those DLLs next to it. The following commands do both.

```sh
cmake --install build/default --prefix "$(cygpath -m "$HOME/nxaevora-portable")"
cp /clang64/bin/libLLVM-23.dll /clang64/bin/libc++.dll /clang64/bin/libxml2-16.dll \
  /clang64/bin/libzstd.dll /clang64/bin/libiconv-2.dll /clang64/bin/libffi-8.dll /clang64/bin/zlib1.dll \
  "$HOME/nxaevora-portable/bin/"
```

The DLL names in this list are those of the state with `libLLVM-23`. The number in the name of `libLLVM` is the LLVM
major version. When MSYS2 ships another major version, the names change, and you must find the new names again. Make the
copy again after each build, because the build changes the program file.

## Options

The following table lists the CMake options that a user is likely to set. Give an option to `cmake --preset default`
with `-D`.

| Option | Value in this guide | What it does |
| --- | --- | --- |
| `SNX_DEPLOY` | `OFF`. The `default` preset sets `ON`. | With `ON`, each build copies the runtime (`nxaevora-engine.exe`) next to every recompiled game in `SNX_RECOMPILED_DIR`. A change to the runtime then reaches the games without a new recompile. |
| `SNX_RECOMPILED_DIR` | Empty. | The folder that holds the recompiled games for `SNX_DEPLOY`. When it is empty, the folder is `recompiled/` in the data folder. |
| `SNX_DEPS_CACHE` | Not set. | A folder that keeps the FFmpeg and OpenSSL builds. A build with the same inputs, such as the compiler, the system headers and the build recipe, uses a finished build in this folder. Several build folders can share it, so each one does not build them again. Use a path without spaces, for example `-DSNX_DEPS_CACHE="$(cygpath -m "$HOME/snx-deps")"`. |
| `SNX_BUILD_TESTING` | `ON`, for the top-level project. | Builds the tests. With `OFF`, no test exists, and the `ctest` command of step 6 fails because of `--no-tests=error`. |

## Troubleshooting

### The build of the atomic tests stops with an error

The build of `snx-test-a64-fp` and `snx-test-a64-fp-arithmetic` stops with this error:

```
helpers/integer.c:45:12: error: large atomic operation may incur significant performance penalty;
the access size (16 bytes) exceeds the max lock-free size (8 bytes) [-Werror,-Watomic-alignment]
```

The tests use 16-byte atomic operations. Without `-mcx16`, Clang treats 8 bytes as the largest lock-free size, and the
warning becomes an error because the build uses `-Werror`.

Add `-DCMAKE_C_FLAGS=-mcx16` to the configure command of step 3, and run steps 3 and 4 again. The flag lets the compiler
use `cmpxchg16b`, an x86-64 instruction that compares and exchanges 16 bytes at once. With it, the 16-byte operations are
lock-free, and the build succeeds. The upstream source is not changed.

### The FFmpeg build fails

The FFmpeg build fails when the path of the clone has a space. Clone the repository to a path without spaces, as in
step 2, and run steps 3 and 4 again in that folder.

### A build folder moved to another path

The program stores the paths of its source folder and build folder. These are `SNX_SOURCE_DIR` and `SNX_BUILD_DIR`, and
they are compiled into the program (`engine/src/nx/runtime/driver/CMakeLists.txt:12`). A build folder that moved keeps
the old paths.

Delete the build folder `engine/build/default`, and run steps 3 and 4 again. The downloads in `engine/.cache/cpm` are kept,
so CPM does not download them again.

### `pacman -Syu` closes the shell

During the update, MSYS2 can replace its own runtime. The shell then closes. Open the MSYS2 CLANG64 shell again, run
`pacman -Syu` again, and then run the command of step 1 that installs the packages.
