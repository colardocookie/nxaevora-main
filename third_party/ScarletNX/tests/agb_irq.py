#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the ScarletNX contributors
"""VBlankIntrWait returns as the BIOS does: once the game's interrupt handler flagged a vertical blank in the BIOS's
flags (0x03007FF8), not at each blank the hardware raised.

A ROM made here installs a handler that acknowledges each vertical blank and flags only every second one; its main
loop counts in r5 the waits that returned. Over 60 frames the BIOS returns about 30 times. The code, assembled with
llvm-mc for armv4t at 0x080000C0:

        ldr   r0, =0x03007FFC
        adr   r1, handler
        str   r1, [r0]              @ the IRQ handler
        mov   r0, #0x04000000
        mov   r1, #8
        strh  r1, [r0, #4]          @ DISPSTAT: VBlank IRQ on
        add   r2, r0, #0x200
        mov   r1, #1
        strh  r1, [r2]              @ IE = VBlank
        strh  r1, [r2, #8]          @ IME = 1
        mov   r5, #0
    loop:
        swi   #0x50000              @ VBlankIntrWait
        add   r5, r5, #1            @ the waits that returned
        b     loop
    handler:
        mov   r0, #0x04000000
        add   r0, r0, #0x200
        mov   r1, #1
        strh  r1, [r0, #2]          @ IF: VBlank acknowledged
        ldr   r2, =0x03000000
        ldr   r3, [r2]
        add   r3, r3, #1
        str   r3, [r2]              @ the blanks the handler saw
        tst   r3, #1
        bxne  lr                    @ odd ones: no BIOS flag
        ldr   r2, =0x03007FF8
        ldrh  r3, [r2]
        orr   r3, r3, #1
        strh  r3, [r2]              @ even ones: the BIOS's VBlank flag
        bx    lr
"""

import argparse
import pathlib
import re
import subprocess
import sys
import tempfile

CODE = [
    0xE59F006C, 0xE28F102C, 0xE5801000, 0xE3A00301, 0xE3A01008, 0xE1C010B4, 0xE2802C02, 0xE3A01001,
    0xE1C210B0, 0xE1C210B8, 0xE3A05000, 0xEF050000, 0xE2855001, 0xEAFFFFFC, 0xE3A00301, 0xE2800C02,
    0xE3A01001, 0xE1C010B2, 0xE3A02403, 0xE5923000, 0xE2833001, 0xE5823000, 0xE3130001, 0x112FFF1E,
    0xE59F2010, 0xE1D230B0, 0xE3833001, 0xE1C230B0, 0xE12FFF1E, 0x03007FFC, 0x03007FF8,
]  # fmt: skip


def rom():
    """A cartridge the board takes (its branch, fixed byte and complement), its code at 0x080000C0."""
    image = bytearray(0xC0)
    image[0:4] = (0xEA00002E).to_bytes(4, 'little')  # b 0x080000C0
    image[0xA0:0xAC] = b'IRQS'.ljust(12, b'\0')
    image[0xAC:0xB2] = b'AIRQ01'
    image[0xB2] = 0x96
    image[0xBD] = -(sum(image[0xA0:0xBD]) + 0x19) & 0xFF
    for word in CODE:
        image += word.to_bytes(4, 'little')
    return bytes(image)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('runner')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='snx-agb-irq-') as temporary:
        work = pathlib.Path(temporary)
        (work / 'irq.gba').write_bytes(rom())
        run = subprocess.run(
            [args.runner, '--frames=60', '--registers', '--outdir', str(work), str(work / 'irq.gba')],
            capture_output=True,
            text=True,
            timeout=600,
        )
        found = re.search(r'\br5=([0-9A-F]{8})', run.stdout)
        if run.returncode or not found:
            sys.exit('agb_irq: the runner exited with %d\n%s%s' % (run.returncode, run.stdout, run.stderr))
        waits = int(found.group(1), 16)
        if not 28 <= waits <= 31:
            sys.exit('agb_irq: VBlankIntrWait returned %d times in 60 frames, not about 30\n%s' % (waits, run.stdout))
    print('agb_irq: passed (%d waits)' % waits)


if __name__ == '__main__':
    main()
