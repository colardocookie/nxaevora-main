# Model of 1A7191C flight + solver step; drag/lift values are illustrative only.
import math
def fly(fps, correct, drag=0.02, lift=0.01, T=1.2, g=-9.8, v0=(20.0, 8.0), fixed=False):
    dt = 1/30 if fixed else min(1/30, 1/fps)
    r = max(1/16, min(1.0, dt*30))
    vx, vy = v0; x = y = 0.0; e = 0.0; t = 0.0
    while True:
        es, Ts, d, l = e, T, drag, lift
        if correct and r < 1:
            es, Ts = e/r, T/r; d = 1-(1-drag)**r; l = lift*r
        es += 1/30                                   # S = 1
        ended = es/Ts > 1
        h = abs(vx); vy += l*h; vx *= 1-d; vy *= 1-d # guest update
        e = es*r if (correct and r < 1) else es
        vy += g*dt; x += vx*dt; y += vy*dt           # solver
        t += 1/fps
        if ended: return t, x, y
for fps in (30, 71, 120):
    print(fps, 'orig-fixed-step', ['%.3f' % v for v in fly(fps, False, fixed=True)],
          'fix-only', ['%.3f' % v for v in fly(fps, False)], 'fix+ball', ['%.3f' % v for v in fly(fps, True)])
