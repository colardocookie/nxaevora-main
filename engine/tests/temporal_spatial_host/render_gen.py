# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 the NXAevora contributors
# Writes the host test of the spatial path's draw in gpu/vk/render.c (the reviewer's revision 4 fixture, ported): the
# stand-ins (render_stubs.c), then fs_variant, spatial_draw and the draw's block that settles the larger texture's pair,
# as they are in the tree, then a case file. Usage: render_gen.py <ScarletNX> <case.c> <out.c>
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
        sys.exit('render_gen.py: no definition of ' + name)
    i, depth = match.end(), 1
    while depth:
        depth += (text[i] == '{') - (text[i] == '}')
        i += 1
    return text[match.start():i] + '\n'


render = (root / 'src/nx/gpu/vk/render.c').read_text()
# the block of the draw that settles the pair: from its comment's first statement to the failed program's check
begin = render.index('        if (g_spatial.larger) {', render.index('/* the larger texture with its pair'))
pair = render[begin:render.index('\n        if (fs && fs->failed)', begin)]
struct = re.search(r'^static struct \{\n(?:    .*\n)+?\} g_spatial;\n', render, re.M)
if not struct:
    sys.exit('render_gen.py: no g_spatial')
text = (here / 'render_stubs.c').read_text() + '\n' + struct.group(0)
text += function(render, 'fs_variant') + '\n' + function(render, 'spatial_draw') + '\n'
text += ('static VkSh* final_pair(GpuChannel* ch, VkSh* initial, VkSh* fs, FsKey* key) {\n'
         '    struct { VkSh* fs0; } sh = {initial};\n    FsKey fk = *key;\n    int kept = 1;\n    u64 code = 0;\n'
         '    u32* r = ch->r3d;\n' + pair + '\n    (void)kept;\n    (void)code;\n    (void)r;\n    *key = fk;\n'
         '    return fs;\n}\n')
text += case.read_text()
out.write_text(text)
