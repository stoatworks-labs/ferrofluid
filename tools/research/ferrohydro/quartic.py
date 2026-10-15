"""The magnetic energy truncated at quartic order in h (variational):
E_m = -mu0 J*, J* = min_xi 1/2<xi + M0 h, G(-h)(xi + M0 h)> + 1/2 r0 <xi, G(s h) xi>,
expanded: E_m = -G/2 <h,|D|h> + C3 F3 + C4a F4a + C4b F4b."""
import numpy as np
from fluid import MU0, G as GRAV


def coefficients(base):
    G, r0, s = base["G"], base["r0"], base["s"]
    C3 = 0.5 * G * (r0 - s) / (1 + r0)
    C4a = -0.5 * G * (r0 + s * s) / (1 + r0)
    C4b = 0.5 * G * r0 * (1 + s) ** 2 / (1 + r0) ** 2
    return C3, C4a, C4b


def energy_terms(g, h):
    """F3, F4a, F4b (the functionals), for checking the gradient by finite differences."""
    u = g.absD(h); w = g.absD(h, 2)
    gx, gy = g.grad(h)
    A = g.lx * g.ly / h.size
    F3 = np.sum(h * (gx * gx + gy * gy) - h * u * u) * A
    q = h * u
    F4a = np.sum(-h * h * u * w + q * g.absD(q)) * A
    r = 0.5 * g.absD(h * h) - q
    F4b = np.sum(r * g.absD(r)) * A
    return F3, F4a, F4b


def gradients(g, h):
    u = g.absD(h); w = g.absD(h, 2)
    gx, gy = g.grad(h)
    q = h * u
    Dq = g.absD(q)
    r = 0.5 * g.absD(h * h) - q
    Dr = g.absD(r); D2r = g.absD(r, 2)
    dF3 = (gx * gx + gy * gy) - u * u + g.absD(h * h, 2) - 2 * Dq
    dF4a = -2 * h * u * w - g.absD(h * h * w) - g.absD(h * h * u, 2) + 2 * u * Dq + 2 * g.absD(h * Dq)
    dF4b = 2 * (h * D2r - u * Dr - g.absD(h * Dr))
    return dF3, dF4a, dF4b


def stress(g, h, base, filt=None):
    """P = -dE_m/dh, minus the linear part G|D|h (which the propagator carries)."""
    C3, C4a, C4b = coefficients(base)
    hm = h if filt is None else g.ifft(filt * g.fft(h))
    dF3, dF4a, dF4b = gradients(g, hm)
    N = -(C3 * dF3 + C4a * dF4a + C4b * dF4b)
    if filt is not None:
        N = g.ifft(filt * g.fft(N))
    return N
