"""Summarise arceus-cadence lines: per site, windows seen, median object rate / frame rate."""
import re, sys, statistics as st
rows = {}
for path in sys.argv[1:]:
    fps = None
    for line in open(path, errors='replace'):
        m = re.search(r'arceus-cadence: window=([\d.]+)s frames_per_s=([\d.]+)', line)
        if m: fps = float(m[2]); continue
        m = re.search(r'site=main\+(\w+) label="([^"]+)" calls_per_s=([\d.]+) object_calls_per_s=([\d.]+)', line)
        if m and fps:
            rows.setdefault((m[1], m[2]), []).append((fps, float(m[3]), float(m[4])))
print(f'{"site":9} {"label":26} {"win":>4} {"fps":>6} {"total":>7} {"object":>7} {"obj/fps":>7} {"obj/30":>6}')
for (s, lab), r in sorted(rows.items(), key=lambda kv: -len(kv[1])):
    f = st.median(x[0] for x in r); t = st.median(x[1] for x in r); o = st.median(x[2] for x in r)
    ratios = [x[2] / x[0] for x in r if x[0]]
    print(f'{s:9} {lab[:26]:26} {len(r):4} {f:6.1f} {t:7.1f} {o:7.1f} {st.median(ratios):7.2f} {o/30:6.2f}'
          f'  min/max ratio {min(ratios):.2f}/{max(ratios):.2f}')
