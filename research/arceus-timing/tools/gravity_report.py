"""Summarise arceus-gravity segments: per segment, calls/s, how v_in.y changes per call,
the downward acceleration that implies (rate*dv) and a fit of v_in.y against real time.
Both should match g*f in a free fall when gravity follows real time."""
import re, sys, statistics as st
num = r'(-?[\d.eE+-]+|nan|-?inf)'
seg = None
segs = []
for path in sys.argv[1:]:
    for line in open(path, errors='replace'):
        m = re.search(r'arceus-gravity: start object=(\w+) g=(\S+) factor=(\S+) max_fall=(\S+) damping=(\S+)', line)
        if m:
            seg = {'obj': m[1], 'g': float(m[2]), 'factor': float(m[3]), 'max_fall': float(m[4]),
                   'damping': float(m[5]), 'rows': []}
            segs.append(seg); continue
        m = re.search(r'arceus-gravity: t=(\S+) call=\d+ v_in=(\S+) pos=(\S+) out=(\S+)', line)
        if m and seg:
            v = [float(x) for x in m[2].split(',')]; p = [float(x) for x in m[3].split(',')]
            o = [float(x) for x in m[4].split(',')]
            seg['rows'].append((float(m[1]), v, p, o)); continue
        m = re.search(r'arceus-gravity: end .* reason=(.+)', line)
        if m and seg: seg['end'] = m[1].strip(); seg = None

def fit_accel(rows):
    # Least squares y = a + b t + c t^2, acceleration 2c.
    n = len(rows)
    if n < 5: return float('nan')
    t = [r[0] for r in rows]; y = [r[2][1] for r in rows]
    S = [[sum(ti ** (i + j) for ti in t) for j in range(3)] for i in range(3)]
    R = [sum(yi * ti ** i for ti, yi in zip(t, y)) for i in range(3)]
    for c in range(3):  # Gaussian elimination
        piv = S[c][c] or 1e-30
        for r in range(c + 1, 3):
            f = S[r][c] / piv
            S[r] = [a - f * b for a, b in zip(S[r], S[c])]; R[r] -= f * R[c]
    x = [0.0] * 3
    for c in (2, 1, 0):
        x[c] = (R[c] - sum(S[c][k] * x[k] for k in range(c + 1, 3))) / (S[c][c] or 1e-30)
    return 2 * x[2]

def fit_slope(rows):
    # Least squares v_in.y = a + b t. The logged pos is not a position, so fit velocity.
    n = len(rows)
    if n < 3: return float('nan')
    t = [r[0] for r in rows]; v = [r[1][1] for r in rows]
    mt, mv = sum(t) / n, sum(v) / n
    d = sum((x - mt) ** 2 for x in t)
    return -sum((x - mt) * (y - mv) for x, y in zip(t, v)) / d if d else float('nan')

print(f'{"obj":>12} {"calls":>5} {"secs":>5} {"call/s":>6} {"dv/call":>9} {"out.y":>9} {"rate*dv":>8} {"fit dv/dt":>9} {"g*f":>7} end')
for s in segs:
    r = s['rows']
    if len(r) < 3: continue
    secs = r[-1][0] - r[0][0]
    dv = [b[1][1] - a[1][1] for a, b in zip(r, r[1:])]
    oy = st.median(x[3][1] for x in r)
    mdv = st.median(dv)
    print(f'{s["obj"][-12:]:>12} {len(r):5} {secs:5.2f} {(len(r) - 1) / secs if secs else 0:6.1f} {mdv:9.4f} {oy:9.4f}'
          f' {-mdv * ((len(r) - 1) / secs) if secs else float("nan"):8.2f} {fit_slope(r):9.2f} {s["g"] * s["factor"]:7.2f} {s.get("end", "?")}')
