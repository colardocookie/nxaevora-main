#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the ScarletNX contributors
# Writes FILE, a static Linux AArch64 program that prints "gnuabi smoke" and exits with 42, for the checks of a
# package of the Linux runtime alone (scripts/make-appimage.sh, scripts/check-package.sh), which has no Switch
# runtime to run an NRO with.
#
#   scripts/aarch64-smoke.sh FILE
set -euo pipefail
if [ $# -ne 1 ]; then
    echo "usage: $0 FILE" >&2
    exit 2
fi

out=
le() { # value, bytes: little-endian
    local i
    for ((i = 0; i < $2; i++)); do out+=$(printf '\\x%02x' $(($1 >> 8 * i & 255))); done
}
base=$((0x400000)) code=$((0x78)) text=$((0x98)) length=13 # the line's 13 bytes, its newline included
size=$((text + length))

# The ELF header: 64-bit, little-endian, version 1, an executable (2) for AArch64 (183), entered at its code, with
# its one program header right after it and no sections
out+='\x7fELF\x02\x01\x01'
le 0 9
le 2 2
le 183 2
le 1 4
le $((base + code)) 8
le 64 8
le 0 8
le 0 4
le 64 2
le 56 2
le 1 2
le 0 6
# The program header: the whole file loaded (1) readable and executable (5) at base
le 1 4
le 5 4
le 0 8
le $base 8
le $base 8
le $size 8
le $size 8
le $((0x1000)) 8
# The code: write(1, line, 13), then exit(42), each through SVC 0 with its Linux number in X8 (64, 93)
le $((0x10000101)) 4 # ADR X1, the line (32 bytes on)
le $((0xd2800020)) 4 # MOV X0, #1
le $((0xd28001a2)) 4 # MOV X2, #13
le $((0xd2800808)) 4 # MOV X8, #64
le $((0xd4000001)) 4 # SVC #0
le $((0xd2800540)) 4 # MOV X0, #42
le $((0xd2800ba8)) 4 # MOV X8, #93
le $((0xd4000001)) 4 # SVC #0
out+='gnuabi smoke\n'

printf '%b' "$out" >"$1"
chmod +x "$1"
