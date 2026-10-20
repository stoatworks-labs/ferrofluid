"""Trace a fluid's branch of hexagonal equilibria with solver.Surface: from the
flat surface's subcritical bifurcation, through the fold (the down-sweep's
collapse), up the stable branch to a target drive.

    python3 branch.py FLUID NX M_MAX OUT.json [--dz0-per-lambda N] [--growth G]

Continuation in the fundamental's amplitude A, with a secant predictor; at each
point Newton on the orbit coefficients and ln( Ha / Ha_c ), its Jacobian by
finite differences, reused (Broyden-updated) between points and rebuilt when
Newton stalls.
"""
import json
import sys
import time

import numpy as np

from fluids import Fluid, MU0, GRAVITY
from solver import Cell, Surface


class Tracer:
    def __init__(self, fluid, nx, dz0=None, growth=1.07, verbose=True, cell=None):
        self.f = fluid
        lam = fluid.lam
        self.cell = cell if cell is not None else Cell(fluid, nx=nx, dz0=(lam / dz0 if dz0 else None), growth=growth)
        self.S = Surface(self.cell)
        self.J = None
        self.verbose = verbose

    def log(self, *a):
        if self.verbose:
            print(*a, flush=True)

    def jacobian(self, A, x, F):
        # Central differences, a step of 1e-5 wavelengths (1e-5 in ln Ha). The
        # residual carries the inner magnetostatic solve's own error, about 1e-11
        # of it; one-sided differences at 1e-7 amplified that to 1e-4 of the
        # Jacobian and Newton stalled short of its tolerance near the fold
        S = self.S
        n = x.size
        phi0 = S.phi.copy()
        Jm = np.zeros((n, n))
        for i in range(n):
            step = 1e-5 * self.f.lam if i < n - 1 else 1e-5
            xp = x.copy()
            xp[i] += step
            xm = x.copy()
            xm[i] -= step
            S.phi = phi0.copy()
            Fp = S.residual(A, xp)
            S.phi = phi0.copy()
            Fm = S.residual(A, xm)
            Jm[:, i] = (Fp - Fm) / (2 * step)
        S.phi = phi0
        return Jm

    def solve(self, A, x, tol=1e-12, maxit=40):
        S = self.S
        F = S.residual(A, x)
        fresh = False
        for it in range(maxit):
            fn = np.max(np.abs(F))
            if fn < tol:
                return x, True, it
            if self.J is None:
                self.J = self.jacobian(A, x, F)
                fresh = True
            dx = np.linalg.solve(self.J, -F)
            phi0 = S.phi.copy()
            t = 1.0
            while True:
                S.phi = phi0.copy()
                trial = x + t * dx
                Ft = S.residual(A, trial)
                if np.max(np.abs(Ft)) < (1 - 1e-4 * t) * fn:
                    break
                t *= 0.5
                if t < 1.0 / 64:
                    break
            if np.max(np.abs(Ft)) >= fn:
                # no descent: a fresh Jacobian, or give up if it already is one
                S.phi = phi0
                if fresh:
                    return x, False, it
                self.J = None
                continue
            # Broyden's update keeps the Jacobian close between rebuilds
            s = trial - x
            y = Ft - F
            self.J += np.outer(y - self.J @ s, s) / np.dot(s, s)
            fresh = False
            x, F = trial, Ft
        return x, False, maxit

    def record(self, A, x):
        S, f = self.S, self.f
        h, Ha = S.compose(A, x)
        coef = np.concatenate([[A], x[:-1]])
        kk = np.array(S.orbit_k) * f.kc
        lap0 = -np.sum(coef * kk ** 2)
        b = f.base(Ha)
        return dict(
            A=A,
            Ha=Ha,
            B_mT=MU0 * Ha * 1e3,
            eps=(Ha / S.Hac) ** 2 - 1,
            m=b["m"],
            current=Ha / S.Hac,  # applied field over the onset's: the plugin's I / I_c in a uniform field
            amplitude=S.amplitude(h),
            peak=float(h[0, 0]),
            trough=float(h.min()),
            tip_radius=2.0 / -lap0 if lap0 < 0 else float("inf"),
            coef=coef.tolist(),
        )

    def trace(self, m_max, A0=None, dA0=None, A_max=None, max_points=400):
        f = self.f
        S = self.S
        A = A0 if A0 is not None else 0.02 / f.kc
        dA = dA0 if dA0 is not None else 0.05 / f.kc
        n = len(S.orbit_k)
        x = np.zeros(n)
        out = []
        prev = None
        t0 = time.time()
        while len(out) < max_points:
            guess = x.copy()
            if prev is not None:
                # secant predictor in A
                guess = x + (x - prev[1]) * (A - out[-1]["A"]) / (out[-1]["A"] - prev[0])
            sol, ok, its = self.solve(A, guess)
            if not ok:
                self.J = None
                sol, ok, its = self.solve(A, guess)
            if not ok:
                if dA < 1e-4 / f.kc:
                    self.log("  stopped: Newton fails at k A %.4f even in small steps" % (A * f.kc))
                    break
                A = out[-1]["A"] + dA / 2 if out else A / 2
                dA /= 2
                continue
            rec = self.record(A, sol)
            if out:
                prev = (out[-1]["A"], x)
            x = sol
            out.append(rec)
            self.log(
                "  kA %.4f  m %.6f  eps %+.5f  B %.4f mT  amplitude %.3f mm  peak %.3f  tip R %.3f mm  (%d its, %d evals, %.0f s)"
                % (A * f.kc, rec["m"], rec["eps"], rec["B_mT"], rec["amplitude"] * 1e3, rec["peak"] * 1e3, rec["tip_radius"] * 1e3, its, S.evals, time.time() - t0)
            )
            if rec["m"] > m_max or (A_max is not None and A > A_max):
                break
            # step control: aim at about 4 Newton iterations
            if its <= 3:
                dA = min(dA * 1.4, 0.2 / f.kc)
            elif its > 8:
                dA *= 0.6
            A += dA
        return out


if __name__ == "__main__":
    name, nx, m_max, path = sys.argv[1], int(sys.argv[2]), float(sys.argv[3]), sys.argv[4]
    dz0 = None
    growth = 1.07
    if "--dz0-per-lambda" in sys.argv:
        dz0 = float(sys.argv[sys.argv.index("--dz0-per-lambda") + 1])
    if "--growth" in sys.argv:
        growth = float(sys.argv[sys.argv.index("--growth") + 1])
    f = Fluid(name)
    T = Tracer(f, nx, dz0=dz0, growth=growth)
    print("%s: cell %d x %d, %d levels, %d orbits up to %.2f k_c" % (name, T.cell.nx, T.cell.ny, T.cell.nz, len(T.S.orbit_k), T.S.orbit_k[-1]), flush=True)
    recs = T.trace(m_max)
    meta = dict(fluid=name, nx=nx, ny=T.cell.ny, nz=T.cell.nz, orbits=len(T.S.orbit_k), orbit_k=T.S.orbit_k, kc=f.kc, lam=f.lam, Ha_c=T.S.Hac)
    with open(path, "w") as fh:
        json.dump(dict(meta=meta, branch=recs), fh)
