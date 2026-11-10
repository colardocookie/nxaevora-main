"""Read-only: print instruction words. Usage: w.py ADDR... (absolute main offsets)."""
import struct, sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
for a in sys.argv[1:]:
    p = int(a, 16); f = owner(p)
    print(f'{p:X} owner={f:X}+{p-f:X} size={size.get(f,0):X} word=0x{struct.unpack_from("<I", b, p)[0]:08x}')
