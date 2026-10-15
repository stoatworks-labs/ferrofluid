"""2D surface (3D geometry): the magnetic stress by the Craig-Sulem series,
perturbative in h to `order`, full curvature, and a spectral solver whose
linear part is exact per mode (as the GPU's)."""
import numpy as np
from math import factorial
from fluid import Fluid, MU0, G


class Grid:
    def __init__(self, nx, ny, lx, ly):
        self.nx, self.ny, self.lx, self.ly = nx, ny, lx, ly
        kx = 2 * np.pi * np.fft.fftfreq(nx, d=lx / nx)
        ky = 2 * np.pi * np.fft.fftfreq(ny, d=ly / ny)
        self.KX, self.KY = np.meshgrid(kx, ky)  # (ny, nx)
        self.K = np.hypot(self.KX, self.KY)
        self.x = np.arange(nx) * lx / nx
        self.y = np.arange(ny) * ly / ny
        self.X, self.Y = np.meshgrid(self.x, self.y)

    def fft(self, f):
        return np.fft.fft2(f)

    def ifft(self, F):
        return np.real(np.fft.ifft2(F))

    def absD(self, f, power=1):
        return self.ifft(self.K ** power * self.fft(f))

    def grad(self, f):
        F = self.fft(f)
        return self.ifft(1j * self.KX * F), self.ifft(1j * self.KY * F)

    def div(self, fx, fy):
        return self.ifft(1j * self.KX * self.fft(fx) + 1j * self.KY * self.fft(fy))


class Series2D:
    def __init__(self, grid, h, filt=None):
        self.g, self.h, self.filt = grid, h, filt

    def apply(self, j, f):
        g, h = self.g, self.h
        if j == 0:
            return g.absD(f)
        q = g.absD(f, j - 1)
        qx, qy = g.grad(q)
        hj = h ** j
        term = -g.div(hj * qx, hj * qy) / factorial(j)
        for i in range(j):
            m = j - i
            term = term - self.apply(i, h ** m / factorial(m) * g.absD(f, m))
        if self.filt is not None:
            term = g.ifft(self.filt * g.fft(term))
        return term


def magnetic_field(grid, h, fl, base, order=3, filt=None):
    """The fluid-side field at the surface, the perturbation potential solved in
    powers of h to `order` (Craig-Sulem DtN on both sides, the fluid linearised
    about the flat state)."""
    M0, Hin, r0, s = base["M0"], base["Hin"], base["r0"], base["s"]
    S = Series2D(grid, h, filt)
    K = grid.K
    inv = np.where(K > 0, 1 / np.where(K > 0, K, 1), 0) / (1 + r0)
    xis = [None]
    for m in range(1, order + 1):
        r = -M0 * (-1) ** (m - 1) * S.apply(m - 1, h)
        for j in range(1, m):
            r = r - ((-1) ** j + r0 * s ** j) * S.apply(j, xis[m - j])
        xis.append(grid.ifft(inv * grid.fft(r)))
    xi = sum(xis[1:])
    W = np.zeros_like(h)
    for m in range(1, order + 1):
        for j in range(0, m):
            W = W + s ** j * S.apply(j, xis[m - j])
    ax, ay = grid.grad(xi)
    gx, gy = grid.grad(h)
    pz = (W + s * (ax * gx + ay * gy)) / (1 + s * s * (gx * gx + gy * gy))
    Hx = -ax + s * pz * gx
    Hy = -ay + s * pz * gy
    Hz = Hin - s * pz
    return Hx, Hy, Hz, gx, gy


def magnetic_stress(grid, h, fl, base, order=3, filt=None):
    Hx, Hy, Hz, gx, gy = magnetic_field(grid, h, fl, base, order, filt)
    Hm = np.sqrt(Hx ** 2 + Hy ** 2 + Hz ** 2)
    nn = np.sqrt(1 + gx * gx + gy * gy)
    Hn = (-Hx * gx - Hy * gy + Hz) / nn
    Mm = fl.M(Hm)
    P = MU0 * fl.MdH(Hm) + MU0 * (Mm * Hn / Hm) ** 2 / 2
    Hin = base["Hin"]
    P0 = MU0 * fl.MdH(np.array([Hin]))[0] + MU0 * float(fl.M(Hin)) ** 2 / 2
    return P - P0, Hm


def curvature(grid, h):
    gx, gy = grid.grad(h)
    nn = np.sqrt(1 + gx * gx + gy * gy)
    return grid.div(gx / nn, gy / nn)


class Solver:
    """h_t = v, v_t = -(|k|/rho)[rho g h - sigma kappa - P] - 2 nu k^2 v, the
    linear part exact at G_ref per mode, the rest kicked (symplectic Euler)."""

    def __init__(self, grid, fl, base, order=3, extra_damp=0.0, cut=None, viscosity=1.0):
        self.g, self.fl, self.base, self.order = grid, fl, base, order
        self.Gref = base["G"]
        self.nu = fl.eta / fl.rho * viscosity
        self.extra = extra_damp
        K = grid.K
        self.filt = None if cut is None else np.exp(-((K / (cut * fl.kc)) ** 8))
        self.hk = np.zeros_like(K, dtype=complex)
        self.vk = np.zeros_like(K, dtype=complex)

    def residual_force(self, h):
        fl, g = self.fl, self.g
        hm = h if self.filt is None or not getattr(self, 'prefilter', False) else g.ifft(self.filt * g.fft(h))
        P, Hm = magnetic_stress(g, hm, fl, self.base, self.order, self.filt)
        lin_mag = self.Gref * g.absD(hm)
        N = fl.sigma * (curvature(g, h) - g.ifft(-(g.K ** 2) * g.fft(h))) + (P - lin_mag)
        return N, Hm

    def step(self, dt):
        fl, g = self.fl, self.g
        K = g.K
        h = g.ifft(self.hk)
        N, Hm = self.residual_force(h)
        Nk = g.fft(N)
        if self.filt is not None:
            Nk = Nk * self.filt
        self.vk = self.vk + dt * K / fl.rho * Nk
        w2 = G * K + fl.sigma / fl.rho * K ** 3 - self.Gref / fl.rho * K ** 2
        # amplitude decay rate: Lamb's 2 nu k^2, plus any extra (statics only)
        a = 2 * self.nu * K ** 2 + self.extra
        disc = w2 - a * a
        hk, vk = self.hk, self.vk
        with np.errstate(all="ignore"):
            wd = np.sqrt(np.abs(disc))
            osc = disc > 0
            c = np.where(osc, np.cos(wd * dt), np.cosh(wd * dt))
            sn = np.where(osc, np.sin(wd * dt) / np.where(wd > 0, wd, 1), np.sinh(wd * dt) / np.where(wd > 0, wd, 1))
            sn = np.where(wd > 0, sn, dt)
        e = np.exp(-a * dt)
        hn = e * (hk * (c + a * sn) + vk * sn)
        vn = e * (vk * (c - a * sn) - hk * (w2 * sn))
        hn[0, 0] = 0
        vn[0, 0] = 0
        self.hk, self.vk = hn, vn
        return h, Hm


def hex_box(fl, cells_x=2, cells_y=1, per_lambda=32):
    lx = cells_x * fl.lam
    ly = cells_y * fl.lam * 2 / np.sqrt(3)
    nx = int(round(per_lambda * cells_x))
    ny = int(round(per_lambda * cells_y * 2 / np.sqrt(3)))
    ny += ny % 2
    return Grid(nx, ny, lx, ly)


def hex_seed(grid, fl, amp):
    k = fl.kc
    ks = [(k, 0.0), (-k / 2, k * np.sqrt(3) / 2), (-k / 2, -k * np.sqrt(3) / 2)]
    return amp * sum(np.cos(a * grid.X + b * grid.Y) for a, b in ks)


class QuarticSolver(Solver):
    """The variational model: E_m truncated at quartic order, its exact gradient."""

    def residual_force(self, h):
        import quartic
        fl, g = self.fl, self.g
        Nm = quartic.stress(g, h, self.base, self.filt)
        N = fl.sigma * (curvature(g, h) - g.ifft(-(g.K ** 2) * g.fft(h))) + Nm
        return N, np.zeros_like(h)
