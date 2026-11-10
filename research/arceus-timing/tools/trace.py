"""Read-only: helpers over the research function index. Run from the research root.
   Usage: trace.py strings REGEX | d ADDR... | callers ADDR... | floats ADDR..."""
import re, struct, subprocess, sys, tempfile, os
sys.path.pop(0)
exec(open('work/explore.py').read().split('for arg in sys.argv[1:]:')[0])
BIN = r'C:/Users/brett/Documents/Codex/work/llvm-mingw-20261104-ucrt-x86_64/bin'
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'dis')
os.makedirs(OUT, exist_ok=True)

def disasm(f):
    out = f'{OUT}/{f:x}.txt'
    if os.path.exists(out): return open(out).read().splitlines()
    raw, elf = f'{OUT}/{f:x}.bin', f'{OUT}/{f:x}.elf'
    open(raw, 'wb').write(b[f:f + size[f]])
    subprocess.run([BIN + '/llvm-objcopy.exe', '-I', 'binary', '-O', 'elf64-littleaarch64',
                    '--rename-section', '.data=.text,alloc,load,readonly,code', raw, elf], check=True)
    asm = subprocess.check_output([BIN + '/llvm-objdump.exe', '-d', '--no-show-raw-insn', '--triple=aarch64',
                                   f'--adjust-vma={f}', elf], text=True)
    notes = {p: s for p, t, s in D['srefs'].get(f, [])}
    lines = []
    for line in asm.splitlines():
        m = re.match(r'\s*([0-9a-f]+):\s+(.*)', line)
        if not m: continue
        p = int(m[1], 16); ins = m[2].split(' <')[0]
        lines.append(f'{p:08X}: {ins}' + (f' ; STR {notes[p]!r}' if p in notes else ''))
    os.remove(raw); os.remove(elf)
    open(out, 'w').write('\n'.join(lines))
    return lines

mode, args = sys.argv[1], sys.argv[2:]
if mode == 'strings':
    rx = re.compile(args[0])
    for f, refs in sorted(D['srefs'].items()):
        hit = sorted({s for p, t, s in refs if rx.search(s)})
        if hit: print(f'{f:X} size={size.get(f)} callers={[hex(c) for c in callers(f)][:6]} {hit[:10]}')
elif mode == 'callers':
    for a in args:
        f = owner(int(a, 16))
        print(f'{f:X} size={size[f]} vtable={[hex(v) for v in D["rev"].get(f, [])][:5]}')
        for c in callers(f): print(f'  {c:X} size={size.get(c)} {str(vals(c))[:240]}')
elif mode == 'd':
    for a in args:
        f = owner(int(a, 16))
        print(f'== {f:X} size={size[f]} callers={[hex(c) for c in callers(f)][:8]} vtable={[hex(v) for v in D["rev"].get(f, [])][:4]} strs={vals(f)[:12]}')
        print('\n'.join(disasm(f)))
elif mode == 'floats':
    # Float arithmetic, constants and calls only, to skim long functions.
    keep = re.compile(r'\b(fmul|fdiv|fadd|fsub|fmadd|fmsub|fnmsub|fmov|fcmp|fcsel|fmin|fmax|scvtf|ucvtf|bl|blr)\b')
    for a in args:
        f = owner(int(a, 16))
        print(f'== {f:X} size={size[f]} callers={[hex(c) for c in callers(f)][:8]} strs={vals(f)[:12]}')
        ls = disasm(f)
        for i, l in enumerate(ls):
            if keep.search(l): print(l)
            m = re.search(r'mov\s+w\d+, #(0x[0-9a-f]+)', l)
            if m and i + 1 < len(ls) and 'movk' in ls[i + 1]:
                k = re.search(r'#(0x[0-9a-f]+), lsl #16', ls[i + 1])
                if k:
                    v = int(m[1], 16) | int(k[1], 16) << 16
                    print(f'{l[:8]}  CONST float {struct.unpack("<f", struct.pack("<I", v))[0]:.7g}')
