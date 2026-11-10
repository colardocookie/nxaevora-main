"""Summarise arceus-capture-wait lines: per capture object and stage, show each call's gate
flags and the +0xC word of every handle, marking the call where the countdown starts."""
import collections, re, sys

for path in sys.argv[1:]:
    print('==', path)
    rows = collections.defaultdict(list)
    for line in open(path, errors='replace'):
        if 'arceus-capture-wait: count=' not in line: continue
        d = dict(re.findall(r'(\w+)=([^\s]+)', line))
        rows[(d['object'], d['stage'])].append(d)
    for (obj, st), r in list(rows.items())[:12]:
        t0 = int(r[0]['ns'])
        print(f' object {obj} stage {st} calls {len(r)}')
        prev = None
        for d in r:
            hs = {k: v.split(':') for k, v in d.items() if k.startswith('h')}
            ready = ' '.join(f'{k[1:]}:{v[3]}' for k, v in sorted(hs.items()))
            key = (d['flags'], ready, d['timer'])
            if key != prev:
                heads = ' '.join(sorted({v[1] for v in hs.values() if v[0] != '0'}))
                print(f'   +{(int(d["ns"]) - t0) / 1e6:7.1f}ms timer={d["timer"]} flags={d["flags"]} ready {ready}  heads {heads}')
            prev = key
