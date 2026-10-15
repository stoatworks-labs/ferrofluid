"""Ridges (1D surface): the magnetic normal stress over a deformed surface, exact
(Cauchy DtN) or by the Craig-Sulem series, and the static shapes."""
import numpy as np
from math import factorial
from fluid import Fluid, MU0, G
from dtn1d import dtn_exact, wavenumbers


def dtn_matrix_exact(h, L):
    n = len(h)
    Kmat, X, Y, k = dtn_exact(h, None, L)
    sign = -1.0
    A = np.eye(n) - sign * Kmat
    Ar, Ai = A.real, A.imag
    M = np.zeros((2 * n, n + 2))
    M[:n, :n] = -Ai
    M[:n, n] = -1
    M[n:, :n] = Ar
    M[n:, n + 1] = -1
    R = np.vstack([-Ar, -Ai])  # rhs = R @ psi
    P = np.linalg.pinv(M) @ R  # sol = P psi
    chi = P[:n, :]
    chi = chi - chi.mean(axis=0, keepdims=True)
    # derivative matrix (spectral) in x
    kx = wavenumbers(n, L)
    F = np.fft.fft(np.eye(n), axis=0)
    Dm = np.real(np.fft.ifft(1j * kx[:, None] * F, axis=0))
    return -Dm @ chi


class Series:
    """Truncated Craig-Sulem operator G(h) = G_0 + ... + G_order (deep, 1D)."""

    def __init__(self, h, L, order):
        self.h, self.L, self.order = h, L, order
        n = len(h)
        self.k = wavenumbers(n, L)
        self.ak = np.abs(self.k)

    def _apply(self, j, f_hat):
        k, ak, h = self.k, self.ak, self.h
        if j == 0:
            return np.real(np.fft.ifft(ak * f_hat))
        g = np.real(np.fft.ifft((ak ** (j - 1)) * f_hat))
        gx = np.real(np.fft.ifft(1j * k * np.fft.fft(g)))
        term = -np.real(np.fft.ifft(1j * k * np.fft.fft(h ** j * gx))) / factorial(j)
        for i in range(j):
            m = j - i
            q = h ** m / factorial(m) * np.real(np.fft.ifft((ak ** m) * f_hat))
            term = term - self._apply(i, np.fft.fft(q))
        return term

    def __call__(self, f):
        fh = np.fft.fft(f)
        return sum(self._apply(j, fh) for j in range(self.order + 1))


def magnetic_stress(h, L, fl, base, method="exact", order=3):
    """P_mag = mu0 int_0^|H| M dH + mu0 M_n^2 / 2 on the fluid side, minus its flat value.
    The perturbation field inside the fluid is linearised about the flat state
    (anisotropic: mu_t along the field, mu_c across it)."""
    n = len(h)
    M0, Hin, r0, s = base["M0"], base["Hin"], base["r0"], base["s"]
    kx = wavenumbers(n, L)
    if method == "exact":
        Ga = dtn_matrix_exact(-h, L)  # air: G(-h)
        Gf = dtn_matrix_exact(s * h, L)  # fluid, stretched
        A = Ga + r0 * Gf
        rhs = -M0 * (Ga @ h)
        # A annihilates constants; solve in the mean-free space
        xi, *_ = np.linalg.lstsq(A, rhs, rcond=None)
        xi -= xi.mean()
        W = Gf @ xi
    else:
        Ga = Series(-h, L, order)
        Gf = Series(s * h, L, order)
        # fixed point: (1 + r0)|D| xi = -M0 Ga h - (Ga - |D|) xi - r0 (Gf - |D|) xi
        ak = np.abs(kx)
        inv = np.where(ak > 0, 1 / np.where(ak > 0, ak, 1), 0) / (1 + r0)
        D = lambda f: np.real(np.fft.ifft(ak * np.fft.fft(f)))
        xi = -M0 * h / (1 + r0)
        rhs0 = -M0 * Ga(h)
        for it in range(200):
            r = rhs0 - (Ga(xi) - D(xi)) - r0 * (Gf(xi) - D(xi))
            new = np.real(np.fft.ifft(inv * np.fft.fft(r)))
            if np.max(np.abs(new - xi)) < 1e-12 * max(1e-30, np.max(np.abs(new))):
                xi = new
                break
            xi = new
        W = Gf(xi)
    a = np.real(np.fft.ifft(1j * kx * np.fft.fft(xi)))
    g = np.real(np.fft.ifft(1j * kx * np.fft.fft(h)))
    pz = (W + s * a * g) / (1 + s * s * g * g)
    Hx = -a + s * pz * g
    Hz = Hin - s * pz
    Hm = np.sqrt(Hx ** 2 + Hz ** 2)
    nx, nz = -g / np.sqrt(1 + g * g), 1 / np.sqrt(1 + g * g)
    Hn = Hx * nx + Hz * nz
    Mm = fl.M(Hm)
    P = MU0 * fl.MdH(Hm) + MU0 * (Mm * Hn / Hm) ** 2 / 2
    P0 = MU0 * fl.MdH(np.array([Hin]))[0] + MU0 * float(fl.M(Hin)) ** 2 / 2
    return P - P0, dict(Hm=Hm, Hn=Hn, xi=xi)


def curvature(h, L):
    n = len(h)
    kx = wavenumbers(n, L)
    hx = np.real(np.fft.ifft(1j * kx * np.fft.fft(h)))
    return np.real(np.fft.ifft(1j * kx * np.fft.fft(hx / np.sqrt(1 + hx * hx))))


def residual(h, L, fl, base, method, order):
    P, _ = magnetic_stress(h, L, fl, base, method, order)
    F = fl.rho * G * h - fl.sigma * curvature(h, L) - P
    return F - F.mean()


def stress_perturbative(h, L, fl, base, order=3):
    """xi and W = G(s h) xi expanded in powers of h to `order`, the field and the
    stress then evaluated exactly from them."""
    n = len(h)
    M0, Hin, r0, s = base["M0"], base["Hin"], base["r0"], base["s"]
    kx = wavenumbers(n, L)
    ak = np.abs(kx)
    Gh = Series(h, L, order)
    Gj = lambda j, f: Gh._apply(j, np.fft.fft(f))
    inv = np.where(ak > 0, 1 / np.where(ak > 0, ak, 1), 0) / (1 + r0)
    xis = [None]
    for m in range(1, order + 1):
        r = -M0 * (-1) ** (m - 1) * Gj(m - 1, h)
        for j in range(1, m):
            r = r - ((-1) ** j + r0 * s ** j) * Gj(j, xis[m - j])
        xis.append(np.real(np.fft.ifft(inv * np.fft.fft(r))))
    xi = sum(xis[1:])
    W = np.zeros(n)
    for m in range(1, order + 1):
        for j in range(0, m):
            W = W + s ** j * Gj(j, xis[m - j])
    a = np.real(np.fft.ifft(1j * kx * np.fft.fft(xi)))
    g = np.real(np.fft.ifft(1j * kx * np.fft.fft(h)))
    pz = (W + s * a * g) / (1 + s * s * g * g)
    Hx = -a + s * pz * g
    Hz = Hin - s * pz
    Hm = np.sqrt(Hx ** 2 + Hz ** 2)
    nx, nz = -g / np.sqrt(1 + g * g), 1 / np.sqrt(1 + g * g)
    Hn = Hx * nx + Hz * nz
    Mm = fl.M(Hm)
    P = MU0 * fl.MdH(Hm) + MU0 * (Mm * Hn / Hm) ** 2 / 2
    P0 = MU0 * fl.MdH(np.array([Hin]))[0] + MU0 * float(fl.M(Hin)) ** 2 / 2
    return P - P0, dict(Hm=Hm, Hn=Hn, xi=xi, W=W)
