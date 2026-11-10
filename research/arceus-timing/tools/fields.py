import struct,sys,re
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
# stores/loads of float fields +5A0/+5A4/+3FC/+61C/+438 (unsigned scaled S or W, STP pair) in projectile range
for name,off in [('drag5A0',0x5a0),('lift5A4',0x5a4),('range3FC',0x3fc)]:
    out=[]
    for p in range(0x1a60000,0x1a90000,4):
        w=struct.unpack_from('<I',b,p)[0]
        if (w&0xffc00000) in (0xbd000000,0xb9000000) and ((w>>10)&0xfff)*4==off: out.append(f'{p:X}(fn {owner(p):X}) STR')
        if (w&0xffc00000) in (0xbd400000,0xb9400000) and ((w>>10)&0xfff)*4==off: out.append(f'{p:X}(fn {owner(p):X}) LDR')
    print(name,out[:14])
# vtable slots: read as raw pointers around vt for the object hierarchy
for vt in (0x41375c0,0x413c5d0):
    print(hex(vt),[hex(struct.unpack_from('<Q',b,vt+8*i)[0]) for i in range(-2,10)])
