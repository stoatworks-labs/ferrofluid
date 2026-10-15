"""The variational quartic model from a hexagon seed, with the fluid's own viscosity
(extra damping 0): does it settle or run away?
    python3 -I qprobe.py FLUID PER_LAMBDA CUT M1,M2,.. DT EXTRA_DAMP SECONDS
    e.g. qprobe.py EFH1 12 4 1.05,1.1,1.3 1e-3 0 4
"""
import numpy as np, sys
from fluid import Fluid, MU0
from ferro2d import *
fl=Fluid(sys.argv[1]); per=int(sys.argv[2]); cut=float(sys.argv[3]); ms=[float(x) for x in sys.argv[4].split(',')]; dt=float(sys.argv[5]); extra=float(sys.argv[6]); T=float(sys.argv[7])
g=hex_box(fl,2,1,per)
for m in ms:
    b=fl.base(fl.applied_for_m(m))
    S=QuarticSolver(g,fl,b,3,extra_damp=extra,cut=cut); S.hk=g.fft(hex_seed(g,fl,0.6e-3))
    out=[]; n=int(T/dt)
    for it in range(n):
        h,_=S.step(dt)
        if it%(n//12)==0: out.append('%.2f'%(h.max()*1e3))
        if h.max()>0.02 or not np.isfinite(h).all(): out.append('RUN@%.2fs'%(it*dt)); break
    h=g.ifft(S.hk)
    print('%s m %.2f: %s  | amp %.2f mm'%(fl.name,m,' '.join(out),(h.max()-h.min())*1e3),flush=True)
    np.save('q_%s_m%.2f.npy'%(fl.name,m),h)
