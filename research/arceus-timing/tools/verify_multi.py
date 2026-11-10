"""Read-only check: interval-by-interval solves per frame for the fixed-step (multi) world in every run with the serial."""
import bisect, csv, os, re, statistics as st
ROOT = r"C:\Users\brett\Documents\Codex\work\arceus-runs"
KV = re.compile(rb"(\w+)=([^\s,]+)")
print("run\tintervals\tserial/frame p10\tmed\tp90\tserial/s med\tfps med\tsim/wall med\tinput_s set")
for name in sorted(os.listdir(ROOT)):
    d = os.path.join(ROOT, name)
    err, sub_p = os.path.join(d, "game-err.txt"), os.path.join(d, "submissions.csv")
    if not (name.startswith("timing-") and os.path.exists(err) and os.path.exists(sub_p)): continue
    sims, rows = {}, []
    with open(err, "rb") as f:
        for line in f:
            if line.startswith(b"arceus-physics-simulation:"):
                m = {k.decode(): v.decode() for k, v in KV.findall(line)}
                sims[m["count"]] = m["path"]
            elif line.startswith(b"arceus-physics-solver:") and b"postsolve_serial" in line:
                rows.append({k.decode(): v.decode() for k, v in KV.findall(line)})
    multi = [r for r in rows if sims.get(r["count"]) == "multi"]
    if len(multi) < 5: continue
    sub = []
    with open(sub_p, newline="") as f:
        for r in csv.DictReader(f):
            try: sub.append(int(r["begin_ns"]))
            except (TypeError, ValueError): pass
    sub.sort()
    per_frame, per_sec, fps, sim_wall = [], [], [], []
    for a, b in zip(multi, multi[1:]):
        if a["object"] != b["object"]: continue
        t0, t1 = int(a["ns"]), int(b["ns"])
        ds = int(b["postsolve_serial"]) - int(a["postsolve_serial"])
        sec = (t1 - t0) / 1e9
        fr = bisect.bisect_right(sub, t1) - bisect.bisect_left(sub, t0)
        if sec < 0.5 or fr < 10 or ds < 0: continue
        per_frame.append(ds / fr); per_sec.append(ds / sec); fps.append(fr / sec)
        sim_wall.append(ds * float(a["input_seconds"]) / sec)
    if len(per_frame) < 4: continue
    q = sorted(per_frame)
    inputs = sorted({r["input_seconds"] for r in multi})
    print(f"{name[7:]}\t{len(q)}\t{q[len(q)//10]:.3f}\t{st.median(q):.3f}\t{q[9*len(q)//10]:.3f}\t{st.median(per_sec):.1f}\t{st.median(fps):.1f}\t{st.median(sim_wall):.2f}\t{inputs[:4]}")
