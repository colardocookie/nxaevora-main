import struct, sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
f=0x1a7191c
print('size', hex(size.get(f,0)), 'callers', [hex(c) for c in callers(f)][:8], 'vt', [hex(v) for v in D['rev'].get(f,[])][:4])
for o in (0x274,0x278,0x2c8,0x2d4):
    print(hex(o), hex(f+o), hex(struct.unpack_from('<I',b,f+o)[0]))
