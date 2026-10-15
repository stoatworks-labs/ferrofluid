"""Fluid properties, Langevin magnetisation and the flat base state (double)."""
import numpy as np

MU0 = 1.25663706212e-6
G = 9.80665

FLUIDS = {
    "EFH1": dict(rho=1210.0, sigma=0.029, eta=6e-3, chi=2.58, Ms=0.044 / MU0),
    "EMG901": dict(rho=1530.0, sigma=0.0295, eta=10e-3, chi=1.3, Ms=4.8e4),
    "EMG909": dict(rho=1020.0, sigma=0.0265, eta=6e-3, chi=0.8, Ms=1.6e4),
    # Gollwitzer et al. JFM 571 (2007), table 1: Ferrotec APG512a, a Langevin fit to H = 10 kA/m
    "APG512a": dict(rho=1236.0, sigma=0.03057, eta=0.120, chi=1.172, Ms=14590.0),
}


def langevin(x):
    x = np.asarray(x, dtype=float)
    out = np.empty_like(x)
    small = np.abs(x) < 1e-2
    xs = x[small]
    out[small] = xs * (1 / 3 - xs * xs / 45 + 2 * xs ** 4 / 945)
    xl = x[~small]
    out[~small] = 1 / np.tanh(xl) - 1 / xl
    return out


def lslope(x):
    x = np.asarray(x, dtype=float)
    out = np.empty_like(x)
    small = np.abs(x) < 1e-2
    xs = x[small]
    out[small] = 1 / 3 - xs * xs / 15 + 2 * xs ** 4 / 189
    xl = x[~small]
    out[~small] = 1 / xl ** 2 - 1 / np.sinh(xl) ** 2
    return out


class Fluid:
    def __init__(self, name):
        p = FLUIDS[name]
        self.name = name
        self.rho, self.sigma, self.eta, self.chi, self.Ms = p["rho"], p["sigma"], p["eta"], p["chi"], p["Ms"]
        self.kc = np.sqrt(self.rho * G / self.sigma)
        self.lam = 2 * np.pi / self.kc
        self.Gc = 2 * np.sqrt(self.rho * G * self.sigma)

    def M(self, H):
        return self.Ms * langevin(3 * self.chi * np.asarray(H, float) / self.Ms)

    def dM(self, H):
        return 3 * self.chi * lslope(3 * self.chi * np.asarray(H, float) / self.Ms)

    def MdH(self, H):
        """integral_0^H M dH = (Ms^2 / (3 chi)) ln( sinh x / x ), x = 3 chi H / Ms."""
        x = 3 * self.chi * np.asarray(H, float) / self.Ms
        x = np.abs(x)
        out = np.empty_like(x)
        small = x < 1e-3
        out[small] = x[small] ** 2 / 6 - x[small] ** 4 / 180
        xl = x[~small]
        out[~small] = xl + np.log1p(-np.exp(-2 * xl)) - np.log(2 * xl)
        return self.Ms ** 2 / (3 * self.chi) * out

    def base(self, Ha):
        """Flat state in an applied (empty-space) field Ha: H_in + M(H_in) = Ha."""
        lo, hi = 0.0, Ha
        for _ in range(200):
            mid = 0.5 * (lo + hi)
            if mid + float(self.M(mid)) < Ha:
                lo = mid
            else:
                hi = mid
        Hin = 0.5 * (lo + hi)
        M0 = float(self.M(Hin))
        mc = 1 + M0 / Hin
        mt = 1 + float(self.dM(Hin))
        r0 = np.sqrt(mc * mt)
        Gs = MU0 * M0 ** 2 * r0 / (1 + r0)
        return dict(Ha=Ha, Hin=Hin, M0=M0, mc=mc, mt=mt, r0=r0, s=np.sqrt(mc / mt), G=Gs, m=Gs * self.kc / (2 * self.rho * G))

    def applied_for_m(self, m):
        lo, hi = 1.0, 1e6
        for _ in range(200):
            mid = 0.5 * (lo + hi)
            if self.base(mid)["m"] < m:
                lo = mid
            else:
                hi = mid
        return 0.5 * (lo + hi)
