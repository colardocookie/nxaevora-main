"""Read-only: owners of audited fixed-rate sites, plus an equivalent B->CBZ XZR rewrite per owner."""
import struct, sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
sites = {  # site or owner : label
 0x13B339C:'credits timer', 0x167FF0C:'event coord +190', 0x16832C0:'event coord 168300C', 0x16836D0:'event coord 1683528',
 0x17177B4:'orientation helper', 0x1728054:'orientation helper 2', 0x1982930:'shared char move', 0x198E020:'shared char accel',
 0x198EC6C:'char collision predict', 0x1A0BAF0:'player controller', 0x1A0DA20:'player move helper', 0x1A0EEA0:'player move 1A0E0EC',
 0x1A0F304:'player method 1A0F278', 0x1A17B10:'player motion 1A173B0', 0x1A196C4:'player alt helper', 0x1B48A48:'weather blend',
 0x1D1B7CC:'player-state predict', 0x1D5D684:'world time feed', 0x24AC8C4:'world time update', 0x257AC30:'pokemon/ride motion',
 0x257C9F4:'pokemon/ride accel', 0x2527E8C:'pokemon 2527E8C', 0x252B124:'pokemon 252B124', 0x252D448:'pokemon 252D448',
 0x25A8A84:'pokemon/ride 25A8A84', 0x25AC3D8:'pokemon/ride gravity', 0xAFBBC4:'script char motion', 0x1984A70:'shared char vertical',
 0x1A158C8:'player vertical', 0x1AAB5A4:'countdown 1AAB534', 0x1D4892C:'transform timer 1/120', 0x1D49838:'transform timer 1/60',
 0x1E1B278:'cave light easing', 0x180C338:'capture sequence', 0x1458300:'movie skip fade', 0x13B32FC:'credits owner',
}
skip = {0x180c338,0x193633c,0x193c04c,0x194f110,0x194f20c,0x19b2140,0x1a7191c,0x1aaf0b4,0x1d482b0,0x1d49144,0x1d498b4,
        0x2795204,0x29fa554,0x2af25b4,0xcc35a4,0x1059a78,0x120ae38,0xc21490,0x9bad68,0x2482818,0x2518ba8,0x2518ca4}
seen = {}
for s, lab in sorted(sites.items()):
    f = owner(s)
    if f is None: print('# no owner', hex(s), lab); continue
    if f in seen or f in skip: print(f'# skip {lab} owner {f:X} ({"dup" if f in seen else "wrapped"})'); continue
    z = size[f]; seen[f] = lab
    best = None
    for p in range(f, f + z, 4):
        w = struct.unpack_from('<I', b, p)[0]
        if (w & 0xfc000000) == 0x14000000:
            off = ((w & 0x3ffffff) ^ 0x2000000) - 0x2000000
            t = p + off * 4
            if f <= t < f + z and -0x40000 <= off < 0x40000:
                cbz = 0xb4000000 | ((off & 0x7ffff) << 5) | 31
                best = (p - f, w, cbz); break
    head = struct.unpack_from('<I', b, f)[0]
    if not best: print(f'# no internal B: {lab} {f:X} z={z:X}'); continue
    print(f'    {{0x{f:x},0x{z:x},0x{head:08x},0x{best[0]:x},0x{best[1]:08x},0x{best[2]:08x},"{lab}"}},')
