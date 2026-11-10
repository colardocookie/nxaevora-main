"""Read-only: frame rate recorded by every path-tracing validation log under can/work/v2."""
import os, re, statistics as st
ROOT = r"C:\Users\brett\Documents\Codex\2026-11-07\can\work\v2"
P = re.compile(rb"^perf: ([\d.]+) frames")
T = re.compile(rb"target_fps=(\d+)")
SC = re.compile(rb"the frame shown is (\d+x\d+) \(guest (\d+x\d+)\)")
U = re.compile(rb"arceus-upscaler: .*?(DLSS|FSR|RR|ray.reconstruction)", re.I)
rows = []
for base, _, files in os.walk(ROOT):
    for fn in files:
        if not fn.endswith((".log", ".txt")): continue
        path = os.path.join(base, fn)
        try:
            if os.path.getsize(path) > 300_000_000: continue
            perf, tgt, shown, ups = [], None, None, None
            with open(path, "rb") as f:
                for line in f:
                    m = P.match(line)
                    if m: perf.append(float(m[1])); continue
                    if tgt is None:
                        m = T.search(line)
                        if m: tgt = int(m[1])
                    if shown is None:
                        m = SC.search(line)
                        if m: shown = (m[1] + b"/" + m[2]).decode()
                    if ups is None:
                        m = U.search(line)
                        if m: ups = m[1].decode()
            if len(perf) >= 6:
                core = perf[3:]
                rows.append((os.path.relpath(path, ROOT), tgt, shown, ups, len(core), st.median(core), sorted(core)[len(core) // 10], max(core)))
        except OSError:
            pass
rows.sort(key=lambda r: -r[5])
print("log\ttarget\tshown/guest\tupscaler\tperf_n\tmedian\tp10\tmax")
for r in rows[:45]: print("\t".join(str(x) for x in r))
print("total logs", len(rows))
