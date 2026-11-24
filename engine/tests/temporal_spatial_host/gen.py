# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
# Writes one host test of the spatial hand-over's code in gpu/vk/temporal.c (the reviewer's fixtures, ported): the
# stand-ins (stubs.c), then the code as it is in the tree (the state, the functions named below, buffer.c's owner
# test, temporal_math.c's draw maker), then a case file of this folder. Usage: gen.py <ScarletNX> <case.c> <out.c>
import re
import sys
from pathlib import Path

here = Path(__file__).resolve().parent
root = Path(sys.argv[1])
case = Path(sys.argv[2])
out = Path(sys.argv[3])


def function(text, name):
    """the whole definition of the function name in text, from its first line to its closing brace"""
    match = re.search(r'^(?:static )?[\w* ]+\b' + name + r'\([^;{]*\)\s*\{', text, re.M)
    if not match:
        sys.exit('gen.py: no definition of ' + name)
    i, depth = match.end(), 1
    while depth:
        depth += (text[i] == '{') - (text[i] == '}')
        i += 1
    return text[match.start():i] + '\n'


temporal = (root / 'src/nx/gpu/vk/temporal.c').read_text()
buffer = (root / 'src/nx/gpu/vk/buffer.c').read_text()
tests = (root / 'tests/temporal_math.c').read_text()
mark = "/* SNX_ARCEUS_UPSCALER=spatial's state"
begin = temporal.index('static struct {', temporal.index(mark))
state = temporal[begin:temporal.index('} g_s;\n', begin) + 7]
begin = temporal.index('static struct {', temporal.index('/* SNX_ARCEUS_UPSCALER:'))
upscaler = temporal[begin:temporal.index('} g_u = {.asked = -1};', begin) + len('} g_u = {.asked = -1};')]
watch = re.search(r'^const Surf\* g_vk_watch;\n', temporal, re.M)
names = ['image_ensure', 'spatial_went_off', 'spatial_failed', 'spatial_now', 'spatial_block', 'vk_temporal_watch',
         'vk_temporal_spatial_binding', 'vk_temporal_spatial_fault', 'vk_temporal_spatial_decide',
         'spatial_views_drop', 'spatial_larger', 'spatial_ready', 'spatial_retire', 'upscaler_unavailable']
text = (here / 'stubs.c').read_text()
text += function(buffer, 'range_owned') + function(buffer, 'vk_buf_owned')
text += upscaler + '\n' + state + '\n' + (watch.group(0) if watch else '')
if case.stem == 'startup':
    text += ''.join(re.findall(r'^#define UPSCALED_(?:FORMAT|USAGE) .+\n', temporal, re.M))
    text += (here / 'startup_stubs.c').read_text()
    names += ['fixed_resolution_on', 'spatial_start', 'upscaler_start']
text += ''.join(function(temporal, n) for n in names)
text += function(tests, 'spatial_draw')
text += case.read_text()
out.write_text(text)
