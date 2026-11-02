#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the ScarletNX contributors
"""A CIA read where it lies against its extraction: the same CIAs accepted and refused, the same bytes read."""

import random
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from ctr_fixtures import NONE, cia, ncch, put


def main():
    # absolute, not resolved: scarletnx-3ds may be a link to scarletnx, which runs the driver its name says
    driver, probe = [str(Path(p).absolute()) for p in sys.argv[1:3]]
    checked = 0
    with tempfile.TemporaryDirectory(prefix='snx-ctr-container-') as temp:
        root = Path(temp)

        def both(blob, name):
            nonlocal checked
            checked += 1
            source, out = root / f'{name}.cia', root / name
            source.write_bytes(blob)
            # the home folder is the test's: no 3DS keys of the user's decrypt an encrypted case
            env = {**os.environ, 'HOME': str(root), 'USERPROFILE': str(root)}
            x = subprocess.run([driver, '-x', '--output', str(out), str(source)], capture_output=True, text=True,
                               timeout=20, env=env)
            args = [probe, str(source)] + ([str(out)] if x.returncode == 0 else [])
            p = subprocess.run(args, capture_output=True, text=True, timeout=20)
            text = x.stdout + x.stderr + p.stdout + p.stderr
            assert 'AddressSanitizer' not in text and 'runtime error:' not in text, text
            # accepted by both, or refused by both, refused as unsupported by both or neither. The read in place takes no
            # whole-data hash (a content's, an ExeFS file's), so it also accepts a CIA whose only fault is one of those
            if x.returncode and not p.returncode:
                assert 'SHA-256 mismatch' in text, (name, text)
                source.unlink()
                shutil.rmtree(out, ignore_errors=True)
                return x.returncode
            assert (x.returncode == 0) == (p.returncode == 0), (name, x.returncode, p.returncode, text)
            if x.returncode:
                assert x.returncode in (1, 3) and p.returncode in range(21, 26), (name, text)
                assert (x.returncode == 3) == (p.returncode == 23), (name, text)
            else:
                assert 'match its extraction' in p.stdout, (name, text)
                shutil.rmtree(out)
            source.unlink()
            return x.returncode

        assert both(cia(), 'plain') == 0
        assert both(cia(application=ncch(compressed=True)), 'compressed') == 0
        assert both(cia(application=ncch(with_romfs=False)), 'no-romfs') == 0
        for signature in [0x10000, 0x10002, 0x10005]:
            assert both(cia(signature=signature, with_meta=False), f'signature-{signature:x}') == 0
        assert both(cia(encrypted=True), 'encrypted') == 3
        assert both(cia(application=ncch(encrypted=True)), 'encrypted-ncch') == 3
        assert both(cia(application=ncch(romfs_options={'duplicate': True})), 'duplicate') == 1
        assert both(cia(application=ncch(patch_exefs=lambda e: put(e, 0x1E0, '<I', 0))), 'exefs-hash') == 1
        plain = cia()
        for n in [0, 31, 0x2041, len(plain) - 1]:
            assert both(plain[:n], f'truncated-{n}') == 1
        # the extraction's own mutation smoke tests, through both readers
        rng = random.Random(0x435452)
        for i in range(40):
            mutant = bytearray(plain)
            for _ in range(1 + i % 4):
                at = rng.randrange(len(mutant))
                mutant[at] ^= 1 << rng.randrange(8)
            both(bytes(mutant), f'mutant-{i}')
        for i in range(48):
            choice = i % 3
            offset = rng.choice([list(range(0, 0x28, 4)), list(range(0, 24, 4)), list(range(0, 32, 4))][choice])
            value = rng.choice([0, NONE, 1, 4, 24, 32, 0xFFFFFFF0])

            def patch(h, d, f):
                put([h, d, f][choice], offset, '<I', value)

            both(cia(application=ncch(romfs_options={'patch': patch})), f'inner-mutant-{i}')
        assert not list(root.rglob('.snx-*'))
    print(f'CIA read where it lies: {checked} CIAs accepted and refused as their extraction, the same bytes read')


if __name__ == '__main__':
    main()
