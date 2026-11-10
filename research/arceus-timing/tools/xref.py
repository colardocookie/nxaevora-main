"""Read-only: direct calls from suspected throwable code into the physics layer. Run from the research root."""
import sys, collections
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
src = [(0x1A60000, 0x1A90000), (0x1CC0000, 0x1D60000), (0x16D0000, 0x1700000), (0x1810000, 0x1830000), (0x1AB0000, 0x1AC0000)]
phys = (0x2A60000, 0x2B10000)
def in_src(p): return any(a <= p < z for a, z in src)
res = collections.defaultdict(lambda: collections.Counter())
for t, sites in D['calls'].items():
    if not (phys[0] <= t < phys[1]): continue
    for s in sites:
        if in_src(s): res[t][owner(s)] += 1
for t in sorted(res, key=lambda k: -sum(res[k].values())):
    print(f'{t:X} size={size.get(t)} ncallers_all={len(D["calls"][t])} strs={str(vals(t))[:70]} <- ' +
          ' '.join(f'{f:X}x{n}' for f, n in res[t].most_common(6)))
