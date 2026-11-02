# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the ScarletNX contributors
# cmake -DWORDS= -DOUT= -DNAME= -DHEADER= -DSOURCE= -P spirv_source.cmake: glslang's SPIR-V words as text (-x) into
# the C file that holds them (cmake/glsl.cmake)
file(READ "${WORDS}" text)
# glslang's first line is a comment with its version
string(REGEX REPLACE "^[ \t]*//[^\n]*\n" "" text "${text}")
file(WRITE "${OUT}.part" "/* Generated from ${SOURCE} by glslang (cmake/glsl.cmake). Do not edit. */
#include \"${HEADER}\"
// clang-format off
const u32 ${NAME}[] = {
${text}};
// clang-format on
const u64 ${NAME}_size = sizeof ${NAME};
")
file(RENAME "${OUT}.part" "${OUT}")
