"""The numerical facts AGENTS.md ("The derived stress, tried") rests on. CPU, double.

    python3 -I checks.py          (from this folder; numpy only, a few minutes)

1. The exact DtN of a deep periodic domain (Cauchy integral, 1D surface) against
   a harmonic function's own normal derivative.
2. The magnetic energy is E_m = -mu0 J*, J* = min_xi 1/2 <xi + M0 h, G(-h)(xi + M0 h)>
   + 1/2 r0 <xi, G(s h) xi>: its gradient, by finite differences on the exact
   DtN, against the exact normal stress of a linear medium.
3. The quartic truncation of J* (quartic.py) against the exact J*: the error
   falls at fifth order or faster.
4. quartic.py's gradients against finite differences.
5. The order-3 perturbative stress (ferro2d.magnetic_stress) is not the gradient
   of anything at the spike's amplitude: its Jacobian's antisymmetric part
   against its symmetric part, at small amplitude and at an EFH1 spike.
"""
import numpy as np
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fluid import Fluid, MU0  # noqa: E402
from dtn1d import dtn_exact_apply, wavenumbers  # noqa: E402
from ferro1d import dtn_matrix_exact  # noqa: E402
import quartic  # noqa: E402
from ferro2d import hex_box, hex_seed, Solver, magnetic_stress  # noqa: E402


def check_dtn():
    L = 2 * np.pi
    n = 128
    x = np.arange(n) * L / n
    worst = 0.0
    for a in [0.1, 0.3, 0.6]:
        h = a * np.cos(x)
        kk = 2.0
        xi = np.exp(kk * h) * np.cos(kk * x)
        hx = -a * np.sin(x)
        exact = kk * np.exp(kk * h) * np.sin(kk * x) * hx + kk * np.exp(kk * h) * np.cos(kk * x)
        _, _, dchi = dtn_exact_apply(h, xi, L)
        worst = max(worst, np.max(np.abs(-dchi - exact)) / np.max(np.abs(exact)))
    print("1. exact DtN against a harmonic function's normal derivative, ka to 0.6: worst %.1e" % worst)


class G1D:
    def __init__(self, n, L):
        self.n, self.L, self.k, self.lx, self.ly = n, L, wavenumbers(n, L), L, 1.0

    def absD(self, f, p=1):
        return np.real(np.fft.ifft(np.abs(self.k) ** p * np.fft.fft(f)))

    def grad(self, f):
        return np.real(np.fft.ifft(1j * self.k * np.fft.fft(f))), np.zeros_like(f)


def check_energy():
    chi = 2.0
    mu = 1 + chi
    Hin = 2000.0
    M0 = chi * Hin
    L = 1e-2
    n = 64
    x = np.arange(n) * L / n
    k = 2 * np.pi / L

    def solve(h):
        Ga = dtn_matrix_exact(-h, L)
        Gf = dtn_matrix_exact(h, L)
        xi, *_ = np.linalg.lstsq(Ga + mu * Gf, -M0 * (Ga @ h), rcond=None)
        return xi - xi.mean(), Ga, Gf

    def Jstar(h):
        xi, Ga, _ = solve(h)
        return 0.5 * M0 * np.dot(h, Ga @ (xi + M0 * h)) * (L / n)

    def stress(h):
        xi, Ga, Gf = solve(h)
        W = Gf @ xi
        kx = wavenumbers(n, L)
        a = np.real(np.fft.ifft(1j * kx * np.fft.fft(xi)))
        g = np.real(np.fft.ifft(1j * kx * np.fft.fft(h)))
        pz = (W + a * g) / (1 + g * g)
        Hx, Hz = -a + pz * g, Hin - pz
        Hn = (-Hx * g + Hz) / np.sqrt(1 + g * g)
        P = MU0 * chi / 2 * (Hx ** 2 + Hz ** 2 - Hin ** 2) + MU0 * chi ** 2 / 2 * (Hn ** 2 - Hin ** 2)
        return P - P.mean()

    worst = 0.0
    for a in [0.05, 0.2, 0.4]:
        h = a / k * np.cos(k * x) + 0.3 * a / k * np.sin(2 * k * x)
        grad = np.zeros(n)
        eps = 1e-9
        for i in range(n):
            e = np.zeros(n)
            e[i] = eps
            grad[i] = (-MU0 * Jstar(h + e) + MU0 * Jstar(h - e)) / (2 * eps) / (L / n)
        grad -= grad.mean()
        P = stress(h)
        worst = max(worst, np.linalg.norm(grad + P) / np.linalg.norm(P))
    print("2. d(-mu0 J*)/dh against the exact normal stress of a linear medium, ka to 0.4: worst %.1e (finite differences)" % worst)

    print("3. the quartic truncation of J* against the exact J* (r0, s; amplitude: |E-E2| |E-E3| |E-E4|):")
    for (r0, s) in [(3.0, 1.0), (3.48, 1.014), (1.86, 1.3)]:
        a_ = M0 / (1 + r0)
        Gs = MU0 * M0 ** 2 * r0 / (1 + r0)
        base = dict(G=Gs, r0=r0, s=s)
        g = G1D(n, L)

        def Jexact(h):
            Ga = dtn_matrix_exact(-h, L)
            Gf = dtn_matrix_exact(s * h, L)
            xi, *_ = np.linalg.lstsq(Ga + r0 * Gf, -M0 * (Ga @ h), rcond=None)
            return 0.5 * M0 * np.dot(h, Ga @ (xi + M0 * h)) * (L / n)

        C3, C4a, C4b = quartic.coefficients(base)
        for amp in [0.02, 0.04, 0.08]:
            h = amp / k * (np.cos(k * x) + 0.5 * np.cos(2 * k * x + 0.3) + 0.3 * np.sin(3 * k * x))
            F3, F4a, F4b = quartic.energy_terms(g, h)
            E2 = -0.5 * Gs * np.sum(h * g.absD(h)) * (L / n)
            Ee = -MU0 * Jexact(h)
            print("   r0 %.2f s %.3f amp %.2f: %.1e %.1e %.1e" % (r0, s, amp, abs(Ee - E2), abs(Ee - E2 - C3 * F3), abs(Ee - E2 - C3 * F3 - C4a * F4a - C4b * F4b)))


def check_gradients():
    fl = Fluid("EFH1")
    g = hex_box(fl, 2, 1, 12)
    rng = np.random.default_rng(1)
    smooth = np.exp(-((g.K / (3 * fl.kc)) ** 4))
    h = hex_seed(g, fl, 1e-3) + 2e-4 * g.ifft(g.fft(rng.standard_normal(g.K.shape)) * smooth)
    D = quartic.gradients(g, h)
    A = g.lx * g.ly / h.size
    worst = 0.0
    for idx in range(3):
        v = g.ifft(g.fft(rng.standard_normal(h.shape)) * smooth)
        eps = 1e-7
        fd = (quartic.energy_terms(g, h + eps * v)[idx] - quartic.energy_terms(g, h - eps * v)[idx]) / (2 * eps)
        worst = max(worst, abs(fd - np.sum(D[idx] * v) * A) / abs(fd))
    print("4. quartic.py's three gradients against finite differences: worst %.1e" % worst)


def check_asymmetry():
    fl = Fluid("EFH1")
    g = hex_box(fl, 2, 1, 12)
    b = fl.base(fl.applied_for_m(1.1))
    S = Solver(g, fl, b, 3, extra_damp=100, cut=4.0)
    S.prefilter = True
    S.hk = g.fft(hex_seed(g, fl, 0.6e-3))
    for _ in range(3000):
        S.step(5e-4)
    h0 = g.ifft(S.hk)

    def mag(h):
        hm = g.ifft(S.filt * g.fft(h))
        P, _ = magnetic_stress(g, hm, fl, b, 3, S.filt)
        return P - P.mean()

    n = h0.size
    for label, hb in [("a 4 um pattern", h0 * 1e-3), ("the spike (%.2f mm tall)" % (h0.max() * 1e3), h0)]:
        J = np.zeros((n, n))
        m0 = mag(hb)
        eps = 1e-8
        for i in range(n):
            e = np.zeros(n)
            e[i] = eps
            J[:, i] = (mag(hb + e.reshape(h0.shape)) - m0).ravel() / eps
        print("5. order-3 stress Jacobian, |antisymmetric| / |symmetric| at %s: %.4f" % (label, np.linalg.norm((J - J.T) / 2) / np.linalg.norm((J + J.T) / 2)))


if __name__ == "__main__":
    import warnings

    warnings.filterwarnings("ignore")
    check_dtn()
    check_energy()
    check_gradients()
    check_asymmetry()
