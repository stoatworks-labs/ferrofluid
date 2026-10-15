"""The order-3 perturbative stress (surface low-passed first) from a hexagon seed:
which substep is stable, and where it runs away.
    python3 -I dtprobe.py FLUID PER_LAMBDA CUT M1,M2,.. DT1,DT2,..
"""
import numpy as np, sys
from fluid import Fluid, MU0
from ferro2d import *
fl=Fluid(sys.argv[1]); per=int(sys.argv[2]); cut=float(sys.argv[3]); ms=[float(x) for x in sys.argv[4].split(',')]; dts=[float(x) for x in sys.argv[5].split(',')]
g=hex_box(fl,2,1,per)
seed=hex_seed(g,fl,0.6e-3)
for m in ms:
    b=fl.base(fl.applied_for_m(m))
    for dt in dts:
        S=Solver(g,fl,b,3,extra_damp=50,cut=cut); S.prefilter=True; S.hk=g.fft(seed)
        out=[]; nsteps=int(1.5/dt); ok=True
        for it in range(nsteps+1):
            h,Hm=S.step(dt)
            if it%(nsteps//5)==0: out.append('%.2f'%(h.max()*1e3))
            if not np.isfinite(h).all() or h.max()>0.03: out.append('RUNAWAY@%.3fs'%(it*dt)); ok=False; break
        print('%s per %d cut %.1f m %.2f dt %.1e: %s'%(fl.name,per,cut,m,dt,' '.join(out)),flush=True)
        if ok: np.save('st_%s_c%.1f_m%.2f.npy'%(fl.name,cut,m),g.ifft(S.hk))
