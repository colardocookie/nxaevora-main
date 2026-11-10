"""Read-only: per-run submission pacing plus the runtime's own perf/dropped lines."""
import csv, json, os, re, statistics as st, sys
ROOT = r"C:\Users\brett\Documents\Codex\work\arceus-runs"
PERF = re.compile(rb"perf: ([\d.]+) frames a second")
DROP = re.compile(rb"vulkan: (\d+) frames not shown")
TGT = re.compile(rb"target_fps=(\d+)")
SC = re.compile(rb"vulkan: swapchain (\d+x\d+), (\d+) images.*?, (\w+)\s*$")
SHOWN = re.compile(rb"the frame shown is (\d+x\d+) \(guest (\d+x\d+)\)")
def pct(v, p): return v[min(len(v) - 1, int(p * len(v)))]
out = []
for name in sorted(os.listdir(ROOT)):
    d = os.path.join(ROOT, name)
    sub, err = os.path.join(d, "submissions.csv"), os.path.join(d, "game-err.txt")
    if not name.startswith("timing-") or not os.path.exists(sub): continue
    groups = {}
    with open(sub, newline="") as f:
        for r in csv.DictReader(f):
            try: groups.setdefault(r["swapchain"], []).append(int(r["begin_ns"]))
            except (TypeError, ValueError): pass
    if not groups: continue
    calls = max(groups.values(), key=len)
    cut = calls[0] + 20e9
    s = [c for c in calls if c >= cut]
    iv = sorted((b - a) / 1e6 for a, b in zip(s, s[1:]))
    rec = {"run": name[7:], "n": len(s)}
    if len(iv) > 50:
        rec.update(sub_fps=round((len(s) - 1) * 1e9 / (s[-1] - s[0]), 1), med=round(st.median(iv), 2),
                   p95=round(pct(iv, .95), 2), p99=round(pct(iv, .99), 2),
                   pct_le_8_6=round(100 * sum(x <= 8.6 for x in iv) / len(iv), 1),
                   pct_gt_16_9=round(100 * sum(x > 16.9 for x in iv) / len(iv), 1),
                   pct_gt_33_6=round(100 * sum(x > 33.6 for x in iv) / len(iv), 1))
    if os.path.exists(err):
        perf, drop, tgt, sc, shown = [], 0, None, None, None
        with open(err, "rb") as f:
            for line in f:
                line = line.rstrip()
                m = PERF.search(line)
                if m: perf.append(float(m[1])); continue
                m = DROP.search(line)
                if m: drop = max(drop, int(m[1])); continue
                m = TGT.search(line)
                if m: tgt = int(m[1]); continue
                m = SC.search(line)
                if m: sc = (m[1] + b" " + m[3]).decode(); continue
                m = SHOWN.search(line)
                if m: shown = (m[1] + b"/" + m[2]).decode()
        rec.update(target=tgt, swap=sc, shown=shown, dropped=drop)
        if perf:
            p = sorted(perf)
            rec.update(perf_n=len(p), perf_med=round(st.median(p), 1), perf_p10=pct(p, .1), perf_max=p[-1])
    out.append(rec)
keys = ["run", "target", "swap", "shown", "n", "sub_fps", "med", "p95", "p99", "pct_le_8_6", "pct_gt_16_9", "pct_gt_33_6", "perf_n", "perf_med", "perf_p10", "perf_max", "dropped"]
print("\t".join(keys))
for r in out: print("\t".join(str(r.get(k, "")) for k in keys))
