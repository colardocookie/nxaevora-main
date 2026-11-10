"""Read-only: float constants built per function in an address range. Run from the research root."""
import struct, sys, collections
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
lo, hi = int(sys.argv[1], 16), int(sys.argv[2], 16)
want = {0x3d088889: '1/30', 0x41f00000: '30', 0x3c888889: '1/60', 0x42700000: '60', 0x3c088889: '1/120', 0x42f00000: '120',
        0x3cb60b61: '1/45?', 0x411ccccd: '9.8', 0xc11ccccd: '-9.8', 0x3f7d70a4: '.99', 0x3f733333: '.95', 0x3f666666: '.9'}
hits = collections.defaultdict(list)
for p in range(lo & ~3, hi, 4):
    w = struct.unpack_from('<I', b, p)[0]
    # MOVZ Wd,#imm then MOVK Wd,#imm,LSL#16 (same register), or MOVZ LSL#16 alone
    if (w & 0xffe00000) == 0x52800000:
        rd, low = w & 31, (w >> 5) & 0xffff
        n = struct.unpack_from('<I', b, p + 4)[0]
        if (n & 0xffe00000) == 0x72a00000 and (n & 31) == rd:
            v = low | ((n >> 5) & 0xffff) << 16
            if v in want: hits[owner(p)].append((p, want[v]))
    elif (w & 0xffe00000) == 0x52a00000:
        v = ((w >> 5) & 0xffff) << 16
        if v in want: hits[owner(p)].append((p, want[v]))
for f in sorted(k for k in hits if k is not None):
    print(f'{f:X} size={size[f]} callers={[hex(c) for c in callers(f)][:4]} vt={[hex(v) for v in D["rev"].get(f, [])][:3]} ' +
          ' '.join(f'{t}@{p:X}' for p, t in hits[f]) + f' {str(vals(f))[:120]}')
