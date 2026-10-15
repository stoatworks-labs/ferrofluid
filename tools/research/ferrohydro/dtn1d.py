"""1D-surface (2D geometry) Dirichlet-Neumann operators: exact (Cauchy integral,
Nystrom with the alternating-point trapezoid rule) and the Craig-Sulem series.

Domain BELOW the curve z = h(x), deep, periodic with period L. G(h) xi is the
unnormalised normal derivative grad(phi).(-h', 1) of the harmonic extension that
decays (to a constant) as z -> -inf.
"""
import numpy as np


def wavenumbers(n, L):
    return 2 * np.pi * np.fft.fftfreq(n, d=L / n)


def absD(f, k):
    return np.real(np.fft.ifft(np.abs(k) * np.fft.fft(f)))


def ddx(f, k):
    return np.real(np.fft.ifft(1j * k * np.fft.fft(f)))


def dtn_series(h, xi, L, order, filt=None):
    """Sum of G_0 .. G_order applied to xi (1D). Recursion
    G_n = (1/n!) D.h^n D |D|^{n-1} - sum_{j<n} G_j (h^{n-j}/(n-j)!) |D|^{n-j}."""
    n = len(h)
    k = wavenumbers(n, L)
    ak = np.abs(k)
    fx = np.fft.fft(xi)
    if filt is not None:
        fx = fx * filt
    # G_j applied to arbitrary functions is needed recursively: implement G_j(f) by recursion with memo on j
    from math import factorial

    def apply(j, f_hat):
        # returns G_j f in real space, f given spectrally
        if j == 0:
            return np.real(np.fft.ifft(ak * f_hat))
        # (1/j!) D.h^j D |D|^{j-1} f  with D.gD f = -d/dx( g df/dx )
        g = np.real(np.fft.ifft((ak ** (j - 1)) * f_hat))
        gx = np.real(np.fft.ifft(1j * k * np.fft.fft(g)))
        term = -np.real(np.fft.ifft(1j * k * np.fft.fft(h ** j * gx))) / factorial(j)
        for i in range(j):
            m = j - i
            q = h ** m / factorial(m) * np.real(np.fft.ifft((ak ** m) * f_hat))
            term = term - apply(i, np.fft.fft(q))
        return term

    out = np.zeros(n)
    terms = []
    for j in range(order + 1):
        t = apply(j, fx)
        terms.append(t)
        out = out + t
    return out, terms


def dtn_exact(h, xi, L):
    """Exact DtN of the domain below z = h(x) by the Cauchy integral."""
    n = len(h)
    s = 2 * np.pi / L
    X = np.arange(n) * (L / n) * s
    Y = h * s
    k = wavenumbers(n, 2 * np.pi)
    Yp = np.real(np.fft.ifft(1j * k * np.fft.fft(Y)))
    z = X + 1j * Y
    zp = 1 + 1j * Yp
    # alternating point trapezoid: K_jk = (1/(pi i)) * 2*(2pi/n) * 0.5*cot((z_k - z_j)/2) z'_k, (k-j) odd
    J, Kk = np.meshgrid(np.arange(n), np.arange(n), indexing="ij")
    odd = ((Kk - J) % 2) == 1
    dz = z[None, :] - z[:, None]
    with np.errstate(all="ignore"):
        cot = np.where(odd, 1.0 / np.tan(dz / 2), 0)
    Kmat = np.where(odd, (1 / (np.pi * 1j)) * 2 * (2 * np.pi / n) * 0.5 * cot * zp[None, :], 0)
    return Kmat, X, Y, k


def dtn_exact_apply(h, xi, L, sign=None):
    n = len(h)
    Kmat, X, Y, k = dtn_exact(h, xi, L)
    # F = sgn*K F + c  for F analytic below and bounded. Unknowns: chi (n real), c (complex).
    # Find sign by a test function F = exp(-i z) (decays below).
    if sign is None:
        z = X + 1j * Y
        F = np.exp(-1j * z)
        r1 = np.linalg.norm(F - Kmat @ F - np.mean(F - Kmat @ F))
        r2 = np.linalg.norm(F + Kmat @ F - np.mean(F + Kmat @ F))
        sign = 1.0 if r1 < r2 else -1.0
    A = np.eye(n) - sign * Kmat  # A F = c
    # A (psi + i chi) = c  ->  A_r psi - A_i chi = c_r ; A_i psi + A_r chi = c_i
    Ar, Ai = A.real, A.imag
    psi = xi
    M = np.zeros((2 * n, n + 2))
    rhs = np.zeros(2 * n)
    M[:n, :n] = -Ai
    M[:n, n] = -1
    rhs[:n] = -Ar @ psi
    M[n:, :n] = Ar
    M[n:, n + 1] = -1
    rhs[n:] = -Ai @ psi
    sol, *_ = np.linalg.lstsq(M, rhs, rcond=None)
    chi = sol[:n]
    chi = chi - chi.mean()
    # G psi = -d chi/dx (sign fixed against the flat case below), d/dX scaled to x
    dchi = np.real(np.fft.ifft(1j * k * np.fft.fft(chi))) * (2 * np.pi / L)
    return sign, chi, dchi


if __name__ == "__main__":
    L = 2 * np.pi
    n = 128
    x = np.arange(n) * L / n
    # flat check: G cos 3x = 3 cos 3x
    h = np.zeros(n)
    sign, chi, dchi = dtn_exact_apply(h, np.cos(3 * x), L)
    print("flat: sign", sign, "dchi vs 3cos:", np.max(np.abs(-dchi - 3 * np.cos(3 * x))), np.max(np.abs(dchi - 3 * np.cos(3 * x))))
    # exact solution test: phi = Re exp(-i k z)?  phi = e^{k z} cos(k x) harmonic, decays below. On the curve: xi = e^{k h} cos kx
    for a in [0.05, 0.1, 0.2, 0.3, 0.4, 0.6]:
        h = a * np.cos(x)
        kk = 2.0
        xi = np.exp(kk * h) * np.cos(kk * x)
        hx = -a * np.sin(x)
        # grad phi . (-h', 1) = -phi_x h' + phi_z
        exact = -(-kk * np.exp(kk * h) * np.sin(kk * x)) * hx + kk * np.exp(kk * h) * np.cos(kk * x)
        sign, chi, dchi = dtn_exact_apply(h, xi, L)
        ex = -dchi
        errs = []
        for order in range(0, 7):
            s, _ = dtn_series(h, xi, L, order)
            errs.append(np.max(np.abs(s - exact)) / np.max(np.abs(exact)))
        print("a=%.2f exact-Cauchy err %.2e | series err by order: %s" % (a, np.max(np.abs(ex - exact)) / np.max(np.abs(exact)), " ".join("%.1e" % e for e in errs)))
