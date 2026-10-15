"""Static ridges: continuation in the fundamental's amplitude, unknowns the
harmonics and the applied field. Exact DtN or the perturbative series."""
import numpy as np, sys
from scipy.optimize import fsolve
from fluid import Fluid, MU0, G
from ferro1d import magnetic_stress, stress_perturbative, curvature, wavenumbers

fl = Fluid(sys.argv[1] if len(sys.argv) > 1 else "EFH1")
method = sys.argv[2] if len(sys.argv) > 2 else "exact"
order = int(sys.argv[3]) if len(sys.argv) > 3 else 3
L = fl.lam
n = 128
J = 24
x = np.arange(n) * L / n
C = np.array([np.cos(j * fl.kc * x) for j in range(1, J + 1)])  # J x n


Hac = fl.applied_for_m(1.0)


def shape(a1, coeffs):
    a = np.concatenate([[a1], np.asarray(coeffs) / fl.kc])
    return a @ C


def F(u, a1):
    coeffs, p = u[:-1], u[-1]
    h = shape(a1, coeffs)
    b = fl.base(p * Hac)
    if method == "exact":
        P, _ = magnetic_stress(h, L, fl, b, "exact")
    else:
        P, _ = stress_perturbative(h, L, fl, b, order)
    R = fl.rho * G * h - fl.sigma * curvature(h, L) - P
    R = R - R.mean()
    proj = C @ R * (2.0 / n)
    return proj / (fl.rho * G * L)  # J equations, J unknowns (J-1 coeffs + Ha)


u = np.zeros(J)
u[-1] = 1.0
rows = []
for A in np.concatenate([np.linspace(0.02, 0.4, 20), np.linspace(0.45, 1.6, 24)]):
    a1 = A / fl.kc
    sol = u.copy()
    ok = False
    for it in range(30):
        f0 = F(sol, a1)
        if np.abs(f0).max() < 1e-13:
            ok = True
            break
        Jm = np.zeros((J, J))
        for i in range(J):
            e = np.zeros(J); e[i] = 1e-7
            Jm[:, i] = (F(sol + e, a1) - f0) / 1e-7
        sol = sol - np.linalg.solve(Jm, f0)
    if not ok:
        print("A %.2f no convergence: resid %.2e" % (A, np.abs(F(sol, a1)).max()))
        break
    u = sol
    h = shape(a1, u[:-1])
    b = fl.base(u[-1] * Hac)
    hx = np.gradient(h, L / n)
    hxx = np.real(np.fft.ifft(-(wavenumbers(n, L) ** 2) * np.fft.fft(h)))
    tipR = 1 / abs(hxx[0]) / (1 + hx[0] ** 2) ** -1.5
    height = h.max() - h.min()
    rows.append((A, b["m"], height / L, tipR / L, np.max(np.abs(hx))))
    print("kA %.3f  m %.4f  height/lambda %.3f  tip radius/lambda %.4f  max slope %.2f  harmonics %s" % (A, b["m"], height / L, tipR / L, np.max(np.abs(hx)), " ".join("%.3f" % c for c in u[:4])), flush=True)
np.save("ridge_%s_%s_%d.npy" % (fl.name, method, order), np.array(rows))
np.save("ridgeshape_%s_%s_%d.npy" % (fl.name, method, order), h)
