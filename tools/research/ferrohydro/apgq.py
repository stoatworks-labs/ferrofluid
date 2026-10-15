"""APG512a hexagons, the variational quartic model (quartic.py), swept down in
epsilon against Gollwitzer et al. (JFM 571, 2007) eq. 4.1.
    python3 -I apgq.py
"""
import numpy as np
from fluid import Fluid, MU0
from ferro2d import *
fl=Fluid('APG512a'); g=hex_box(fl,2,1,12); Bc=MU0*fl.applied_for_m(1.0)
ref=lambda e:(0.0873*(1+e)+np.sqrt((0.0873*(1+e))**2+4*e*0.0889))/(2*0.0889)/fl.kc
hk=g.fft(hex_seed(g,fl,0.6e-3)); vk=None
for eps in [0.44,0.35,0.3,0.2,0.1,0.05,0.02,0.0,-0.005,-0.01,-0.02,-0.03,-0.05]:
    b=fl.base(Bc*np.sqrt(1+eps)/MU0)
    S=QuarticSolver(g,fl,b,3,extra_damp=0,cut=4.0); S.hk=hk
    if vk is not None: S.vk=vk
    last=None; run=False
    for it in range(40000):
        h,_=S.step(5e-4)
        if h.max()>0.02 or not np.isfinite(h).all(): run=True; break
        if it%400==0:
            a=h.max()-h.min()
            if last is not None and abs(a-last)<1e-9: break
            last=a
    if run: print('eps %+.3f RUNAWAY'%eps); break
    hk,vk=S.hk,S.vk; h=g.ifft(hk); a=h.max()-h.min()
    r=ref(eps) if eps>=0 else float('nan')
    print('eps %+.3f  amplitude %.3f mm  Gollwitzer %.3f  (%+.1f%%)'%(eps,a*1e3,r*1e3,100*(a/r-1) if eps>=0 else 0),flush=True)
    if a<1e-5: break
