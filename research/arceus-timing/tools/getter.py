import struct,sys
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
print('rev sample',list(D['rev'].items())[:2])
# 1) Writers: who sets +5A0/+5A4 from anywhere near (pair STP S / LDP then STR) and the weak ptr +2E0
# 2) Small d0 getters reachable by vtable: find slots at base+0x30 with sibling at base+0x18
slot2fn={}
for f,vs in D['rev'].items():
    for v in vs: slot2fn[v]=f
cands=[]
for f,vs in D['rev'].items():
    z=size.get(f,0)
    if z>40 or z<8: continue
    w0=struct.unpack_from('<I',b,f)[0]
    # LDR Dt,[Xn,#imm] family, or FMOV D0/ LDR S0 returning
    if (w0&0xffc003ff)==0xfd400000 or (w0&0xffc00000)==0xfd400000 and (w0&31)==0:
        for v in vs:
            cands.append((f,v,(w0>>10&0xfff)*8))
print('d0 getters',len(cands))
near=[c for c in cands if slot2fn.get(c[1]-0x18) and c[0] and 0x1600000<=c[0]<0x1e00000]
for f,v,off in near[:40]:
    a=slot2fn.get(v-0x18); print(f'{f:X} slot={v:X} field=+{off:X} sib18={a:X} sibz={size.get(a)} ')
