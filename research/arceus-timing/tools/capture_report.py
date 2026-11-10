"""Summarise arceus-sequence-clock kind=1 (capture director 180C338) per capture object:
real seconds and probe calls per stage. Frame-counted waiting stages (3, 5, 7, 8, 9, 14, 16)
should take the same real time at any FPS once the capture fix is on."""
import collections, re, sys

for path in sys.argv[1:]:
    print('==', path)
    rows = collections.defaultdict(list)
    for line in open(path, errors='replace'):
        if 'arceus-sequence-clock: kind=1' not in line: continue
        d = dict(re.findall(r'(\w+)=([^\s]+)', line))
        rows[d['object']].append((int(d['ns']), int(d['state_after']), int(d['count']), d['after']))
    for obj, r in rows.items():
        segs, s0 = [], r[0]
        for a, b in zip(r, r[1:]):
            if b[1] != a[1]:
                segs.append((a[1], (b[0] - s0[0]) / 1e9, b[2] - s0[2], s0[3], a[3])); s0 = b
        segs.append((r[-1][1], (r[-1][0] - s0[0]) / 1e9, r[-1][2] - s0[2], s0[3], r[-1][3]))
        print(f' object {obj} samples {len(r)} span {(r[-1][0] - r[0][0]) / 1e9:.2f}s')
        print('   ' + '  '.join(f'st{st}:{sec:.2f}s/{n}c[{a}->{b}]' for st, sec, n, a, b in segs))
