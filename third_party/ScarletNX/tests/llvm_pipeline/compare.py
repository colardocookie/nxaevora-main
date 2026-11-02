import ctypes
import pathlib
import random
import re
import sys

class CPU(ctypes.Structure):
    _fields_ = [('x', ctypes.c_uint64 * 32), ('pc', ctypes.c_uint64)]

libs = [ctypes.CDLL(sys.argv[1]), ctypes.CDLL(sys.argv[2])]
for lib in libs:
    lib.F199.argtypes = [ctypes.POINTER(CPU), ctypes.c_uint64]
    lib.F199.restype = ctypes.c_uint64
rng = random.Random(4670)
for i in range(500):
    values = [rng.getrandbits(64) for _ in range(32)]
    t = rng.getrandbits(64)
    states = [CPU(), CPU()]
    for state in states:
        for j, v in enumerate(values):
            state.x[j] = v
    results = [lib.F199(ctypes.byref(state), t) for lib, state in zip(libs, states)]
    assert results[0] == results[1] and bytes(states[0]) == bytes(states[1]), i
print('PASS 500 randomized compiled call chains: return value and full CPU state parity')
for label, path in zip(['LLVM default O2', 'bounded O2'], sys.argv[3:]):
    lines = pathlib.Path(path).read_text().strip().splitlines()
    values = [dict((k, float(v)) for k,v in re.findall(r'(passes_ms|codegen_ms|obj_bytes)=([\d.]+)',line)) for line in lines]
    print(f'{label}: median passes_ms={sorted(v["passes_ms"] for v in values)[1]:.1f} '
          f'codegen_ms={sorted(v["codegen_ms"] for v in values)[1]:.1f} '
          f'obj_bytes={values[-1]["obj_bytes"]:.0f}')
