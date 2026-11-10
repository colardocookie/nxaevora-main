import struct,sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
T={0x5a0:'drag',0x5a4:'lift',0x3fc:'range',0x580:'vel',0x438:'scale438'}
for p in range(0x1900000,0x1b00000,4):
    w=struct.unpack_from('<I',b,p)[0]; r=None
    if (w&0xffc00000)==0xbd000000: r=('STR s',((w>>10)&0xfff)*4)
    elif (w&0xffc00000)==0xb9000000: r=('STR w',((w>>10)&0xfff)*4)
    elif (w&0xffc00000)==0xf9000000: r=('STR x',((w>>10)&0xfff)*8)
    elif (w&0xffc00000)==0x3d800000: r=('STR q',((w>>10)&0xfff)*16)
    elif (w&0xffc00000) in (0x2d000000,0x29000000):
        i=(w>>15)&0x7f; i=(i-128 if i>63 else i)*4; r=('STP',i)
    elif (w&0xffc00000)==0xa9000000:
        i=(w>>15)&0x7f; i=(i-128 if i>63 else i)*8; r=('STPx',i)
    if r and (r[1] in T or (r[0].startswith('STP') and r[1]+4 in T) or (r[0] in('STR x','STR q','STPx') and r[1]+4 in T)):
        print(f'{p:X} fn={owner(p):X} {r[0]} +{r[1]:X} {T.get(r[1],T.get(r[1]+4))} vt={[hex(v) for v in D["rev"].get(owner(p),[])][:1]}')
