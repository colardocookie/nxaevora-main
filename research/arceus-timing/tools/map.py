"""Read-only: per-function summary for a range: vtable use, strings, float ops, FMOV immediates, physics callees."""
import re, struct, sys, subprocess, os, collections
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
lo, hi = int(sys.argv[1], 16), int(sys.argv[2], 16)
minsize = int(sys.argv[3]) if len(sys.argv) > 3 else 64
callee = collections.defaultdict(set)
for t, sites in D['calls'].items():
    for s in sites:
        if lo <= s < hi: callee[owner(s)].add(t)
def fp8(imm):
    s = (imm >> 7) & 1; e = (imm >> 4) & 7; m = imm & 15
    v = (16 + m) / 16.0 * 2.0 ** ((e ^ 4) - 3 if True else 0)
    return -v if s else v
for f, z in fns:
    if not (lo <= f < hi) or z < minsize: continue
    imms, fm, fd = collections.Counter(), 0, 0
    dtarg = False
    for p in range(f, f + z, 4):
        w = struct.unpack_from('<I', b, p)[0]
        if (w & 0xff201fe0) == 0x1e201000:   # FMOV scalar immediate
            imms[round(fp8((w >> 13) & 0xff), 4)] += 1
        if (w & 0xff20fc00) == 0x1e200800: fm += 1   # FMUL scalar
        if (w & 0xff20fc00) == 0x1e201800: fd += 1   # FDIV scalar
    phys = sorted(t for t in callee[f] if 0x2a60000 <= t < 0x2b10000 and len(D['calls'].get(t, [])) < 200)
    if not (imms or phys or vals(f) or D['rev'].get(f)): continue
    print(f'{f:X} z={z} vt={[hex(v) for v in D["rev"].get(f, [])][:2]} fmul={fm} fdiv={fd} imm={dict(imms)} phys={[hex(t) for t in phys][:6]} {str(vals(f))[:80]}')
