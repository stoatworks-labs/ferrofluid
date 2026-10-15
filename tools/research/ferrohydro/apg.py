"""APG512a hexagons, the order-3 perturbative stress, swept down in epsilon against
Gollwitzer et al. (JFM 571, 2007) eq. 4.1 fitted to their data.
    python3 -I apg.py PER_LAMBDA CUT ORDER PREFILTER     e.g. apg.py 12 4 3 1
"""
import numpy as np, sys
from fluid import Fluid, MU0
from ferro2d import *
fl=Fluid('APG512a'); per=int(sys.argv[1]); cut=float(sys.argv[2]); order=int(sys.argv[3]); pf=sys.argv[4]=='1'
g=hex_box(fl,2,1,per); Bc=MU0*fl.applied_for_m(1.0)
ref=lambda e:(0.0873*(1+e)+np.sqrt((0.0873*(1+e))**2+4*e*0.0889))/(2*0.0889)/fl.kc
hk=g.fft(hex_seed(g,fl,0.6e-3)); vk=None
for eps in [0.44,0.35,0.3,0.2,0.1,0.05,0.02,0.0,-0.005,-0.01,-0.02,-0.03]:
    b=fl.base(Bc*np.sqrt(1+eps)/MU0)
    S=Solver(g,fl,b,order,extra_damp=0,cut=cut); S.prefilter=pf; S.hk=hk
    if vk is not None: S.vk=vk
    last=None
    for it in range(100000):
        h,Hm=S.step(4e-4)
        if it%500==0:
            a=h.max()-h.min()
            if last is not None and abs(a-last)<1e-9: break
            last=a
    hk,vk=S.hk,S.vk; h=g.ifft(hk); a=h.max()-h.min()
    print('eps %+.3f  amplitude %.3f mm  Gollwitzer fit %.3f mm  (%+.1f%%)  peak %.2f'%(eps,a*1e3,ref(eps)*1e3 if eps>=0 else float('nan'),100*(a/ref(eps)-1) if eps>=0 else 0,h.max()*1e3),flush=True)
    if a<1e-5: break
