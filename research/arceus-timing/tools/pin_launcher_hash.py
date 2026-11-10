import sys, re
h = sys.argv[1]; b = [f'0x{h[i:i+2]}' for i in range(0, 64, 2)]
arr = '\n        ' + ','.join(b[:16]) + ',\n        ' + ','.join(b[16:]) + '};'
for f in sys.argv[2:]:
    t = open(f).read(); t2 = re.sub(r'expected\[32\] = \{[^}]*\};', 'expected[32] = {' + arr, t); assert t2 != t, f
    open(f, 'w').write(t2)
print('pinned', h[:8])
