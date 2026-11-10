"""Read-only: wall-clock cadence of guest subsystems versus frames, from existing run logs."""
import bisect, csv, os, re, statistics as st, sys
ROOT = r"C:\Users\brett\Documents\Codex\work\arceus-runs"
KV = re.compile(rb"(\w+)=([^\s,]+)")
def kv(line): return {k.decode(): v.decode() for k, v in KV.findall(line)}
def frames_between(sub, a, b): return bisect.bisect_right(sub, b) - bisect.bisect_left(sub, a)

def analyze(name):
    d = os.path.join(ROOT, name)
    sub = []
    with open(os.path.join(d, "submissions.csv"), newline="") as f:
        for r in csv.DictReader(f):
            try: sub.append(int(r["begin_ns"]))
            except (TypeError, ValueError): pass
    sub.sort()
    solver, sim = {}, {}
    research, seq, timers, engine = {}, {}, {}, {}
    with open(os.path.join(d, "game-err.txt"), "rb") as f:
        for line in f:
            if line.startswith(b"arceus-physics-simulation:"):
                m = kv(line); sim[(m["count"], m["object"])] = (m["simulation"], m["path"])
            elif line.startswith(b"arceus-physics-solver:"):
                m = kv(line)
                if "postsolve_serial" in m: solver.setdefault(m["object"], []).append(m)
            elif line.startswith(b"arceus-timing-research:") and b"kind=" in line and b"count=" in line:
                m = kv(line); research.setdefault(m["kind"], []).append(m)
            elif line.startswith(b"arceus-sequence-clock:") and b"count=" in line:
                m = kv(line); seq.setdefault((m["kind"], m["object"]), []).append(m)
            elif line.startswith(b"arceus-frame-timer:") and b"count=" in line:
                m = kv(line); timers.setdefault((m["kind"], m["object"]), []).append(m)
            elif line.startswith(b"arceus-engine-clock:") and b"count=" in line:
                m = kv(line); engine.setdefault(m["kind"], []).append(m)
    print(f"\n===== {name}  frames={len(sub)}")
    print("-- physics worlds: per (object,simulation): completed-solve rate vs frame rate")
    for obj, rows in solver.items():
        groups = {}
        for r in rows:
            s = sim.get((r["count"], obj), ("?", "?"))
            groups.setdefault(s, []).append(r)
        for (simu, path), g in groups.items():
            if len(g) < 3: continue
            t0, t1 = int(g[0]["ns"]), int(g[-1]["ns"])
            ser0, ser1 = int(g[0]["postsolve_serial"]), int(g[-1]["postsolve_serial"])
            sec = (t1 - t0) / 1e9
            if sec < 3: continue
            fr = frames_between(sub, t0, t1)
            ins = sorted(float(x["input_seconds"]) for x in g)
            print(f"  {path:7} obj={obj} sim={simu} sec={sec:7.1f} samples={len(g):4} serial/s={(ser1-ser0)/sec:7.2f} fps={fr/sec:6.1f} "
                  f"serial/frame={(ser1-ser0)/max(fr,1):5.2f} input_s med={st.median(ins):.5f} substeps={g[-1]['substeps']} sim_time/wall={(ser1-ser0)*st.median(ins)/sec:5.2f}")
    print("-- timing-research entries (0=nav, 1=rigid): calls/s vs frames")
    for kind, rows in sorted(research.items()):
        byo = {}
        for r in rows:
            if "ns" in r: byo.setdefault((r["caller"]), []).append(r)
        all_ = [r for r in rows if "ns" in r]
        if len(all_) >= 2:
            t0, t1 = int(all_[0]["ns"]), int(all_[-1]["ns"]); c0, c1 = int(all_[0]["count"]), int(all_[-1]["count"])
            sec = (t1 - t0) / 1e9
            print(f"  kind={kind} global calls/s={(c1-c0)/max(sec,1e-9):8.1f} fps={frames_between(sub,t0,t1)/max(sec,1e-9):6.1f} sec={sec:.1f}")
        for caller, g in byo.items():
            dts = sorted(float(x["incoming_dt"]) for x in g)
            modes = sorted({x["variable_mode"] for x in g}); rates = sorted({x["rate"] for x in g})
            print(f"    caller={caller} n={len(g)} incoming_dt med={st.median(dts):.5f} modes={modes} rates={rates[:4]}")
    print("-- integer sequence clocks (0=fade,1=capture): consecutive-sample decrement rate")
    for (kind, obj), rows in sorted(seq.items()):
        rates = []
        for a, b in zip(rows, rows[1:]):
            if a["transition"] == "0" and b["transition"] == "0" and a["state_after"] == b["state_after"]:
                dv, dt = int(a["after"]) - int(b["after"]), (int(b["ns"]) - int(a["ns"])) / 1e9
                if dt > 0.02 and dv != 0: rates.append((dv / dt, frames_between(sub, int(a["ns"]), int(b["ns"])) / dt))
        if rates:
            print(f"  kind={kind} obj={obj} intervals={len(rates)} units/s med={st.median(r[0] for r in rates):7.1f} fps med={st.median(r[1] for r in rates):6.1f}")
    print("-- frame timers (0=recenter,1=battle idle,2-4=throwable): change rate")
    for (kind, obj), rows in sorted(timers.items()):
        rs = []
        for a, b in zip(rows, rows[1:]):
            dt = (int(b["ns"]) - int(a["ns"])) / 1e9; dv = float(b["after"]) - float(a["after"])
            if 0.02 < dt < 5 and dv: rs.append((dv / dt, frames_between(sub, int(a["ns"]), int(b["ns"])) / dt))
        print(f"  kind={kind} obj={obj} n={len(rows)} " + (f"units/s med={st.median(r[0] for r in rs):8.3f} fps med={st.median(r[1] for r in rs):6.1f}" if rs else "too few"))
    print("-- engine clock kind=0 script frames: integer versus seconds per sample")
    rows = engine.get("0", [])
    good = [(int(r["after"]), float(r["seconds"])) for r in rows if float(r["seconds"]) > 0]
    if good:
        print(f"  samples={len(good)} integer med={st.median(g[0] for g in good)} seconds med={st.median(g[1] for g in good):.5f} frames_at_30_equiv={st.median(g[1] for g in good)*30:.3f}")

for n in sys.argv[1:]: analyze(n)
