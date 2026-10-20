"""The fluids the spike solver knows, and their Langevin magnetisation.

The three presets are source/Fluid.cpp's datasheets, number for number (the
generator checks that the C++ and this agree before it writes the table).
APG512a is Gollwitzer et al.'s fluid (J. Fluid Mech. 571, 2007, table 1), used
only to validate the solver against their measurements and simulations.
"""
import numpy as np

MU0 = 1.25663706212e-6
GRAVITY = 9.80665

FLUIDS = {
    # name: density kg/m^3, surface tension N/m, initial susceptibility, saturation A/m
    "EFH1": dict(rho=1210.0, sigma=0.029, chi=2.58, Ms=0.044 / MU0),
    "EMG 901": dict(rho=1530.0, sigma=0.0295, chi=1.3, Ms=4.8e4),
    "EMG 909": dict(rho=1020.0, sigma=0.0265, chi=0.8, Ms=1.6e4),
    "APG512a": dict(rho=1236.0, sigma=0.03057, chi=1.172, Ms=14590.0),
}
PRESETS = ["EFH1", "EMG 901", "EMG 909"]


def _langevin(x):
    x = np.asarray(x, dtype=float)
    out = np.empty_like(x)
    s = np.abs(x) < 1e-2
    xs = x[s]
    out[s] = xs * (1 / 3 - xs * xs / 45 + 2 * xs ** 4 / 945)
    xl = x[~s]
    out[~s] = 1 / np.tanh(xl) - 1 / xl
    return out


def _lslope(x):
    x = np.asarray(x, dtype=float)
    out = np.empty_like(x)
    s = np.abs(x) < 1e-2
    xs = x[s]
    out[s] = 1 / 3 - xs * xs / 15 + 2 * xs ** 4 / 189
    xl = x[~s]
    big = np.abs(xl) > 350
    v = np.empty_like(xl)
    v[big] = 1 / xl[big] ** 2
    xm = xl[~big]
    v[~big] = 1 / xm ** 2 - 1 / np.sinh(xm) ** 2
    out[~s] = v
    return out


def _lncosh_over(x):
    """ln( sinh x / x ), stable for all x >= 0."""
    x = np.abs(np.asarray(x, dtype=float))
    out = np.empty_like(x)
    s = x < 1e-3
    out[s] = x[s] ** 2 / 6 - x[s] ** 4 / 180
    xl = x[~s]
    out[~s] = xl + np.log1p(-np.exp(-2 * xl)) - np.log(2 * xl)
    return out


class Fluid:
    def __init__(self, name):
        p = FLUIDS[name]
        self.name = name
        self.rho, self.sigma, self.chi, self.Ms = p["rho"], p["sigma"], p["chi"], p["Ms"]
        self.kc = np.sqrt(self.rho * GRAVITY / self.sigma)
        self.lam = 2 * np.pi / self.kc
        self.Gc = 2 * np.sqrt(self.rho * GRAVITY * self.sigma)
        self.gamma = 3 * self.chi / self.Ms

    def M(self, H):
        return self.Ms * _langevin(self.gamma * np.asarray(H, float))

    def dM(self, H):
        return 3 * self.chi * _lslope(self.gamma * np.asarray(H, float))

    def Phi(self, H):
        """integral_0^H M dH'."""
        return self.Ms / self.gamma * _lncosh_over(self.gamma * np.asarray(H, float))

    def base(self, Ha):
        """The flat state in a normal applied (empty-space) field Ha: H_in + M(H_in) = Ha."""
        lo, hi = 0.0, float(Ha)
        for _ in range(200):
            mid = 0.5 * (lo + hi)
            if mid + float(self.M(mid)) < Ha:
                lo = mid
            else:
                hi = mid
        Hin = 0.5 * (lo + hi)
        M0 = float(self.M(Hin))
        mc = 1 + M0 / Hin if Hin > 0 else 1 + self.chi
        mt = 1 + float(self.dM(Hin))
        r0 = np.sqrt(mc * mt)
        G = MU0 * M0 ** 2 * r0 / (1 + r0)
        return dict(Ha=float(Ha), Hin=Hin, M0=M0, mc=mc, mt=mt, r0=r0, G=G, m=G * self.kc / (2 * self.rho * GRAVITY))

    def applied_for_m(self, m):
        lo, hi = 1.0, 1e6
        for _ in range(200):
            mid = 0.5 * (lo + hi)
            if self.base(mid)["m"] < m:
                lo = mid
            else:
                hi = mid
        return 0.5 * (lo + hi)
