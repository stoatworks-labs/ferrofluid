"""The equilibrium free surface of a hexagonal pattern of magnetic-fluid spikes,
with the full nonlinear magnetostatics. CPU, numpy, double precision.

The problem (Gollwitzer et al., J. Fluid Mech. 571, 2007, section 3, which is
the same problem their finite elements solve): an infinitely deep layer of
fluid below an infinite air space, a uniform applied induction B0 = mu0 Ha far
above and far below, the fluid magnetised along its Langevin curve
M = Ms L( 3 chi H / Ms ) and the air not at all. The surface z = h(x, y) is
periodic on the hexagonal lattice of wavenumber k_c (two spikes in the
rectangular cell a x sqrt(3) a, a = 4 pi / ( sqrt(3) k_c ): Gollwitzer et al.
fix the wavenumber the same way). Its equilibrium is a stationary point of

    E[h] = integral( rho g h^2 / 2 + sigma ( sqrt( 1 + |grad h|^2 ) - 1 ) dA ) - Pi*[h]

with Pi*[h] = min over phi of Pi[phi; h], the magnetic potential energy

    Pi = integral( w( |grad phi| ) dV ) + mu0 Ha ( integral_top phi dA - integral_bottom phi dA ),
    w( H ) = mu0 H^2 / 2 ( + mu0 integral_0^H M dH' in the fluid ),

whose minimiser is the field (grad . B = 0, B = dw/dH, B_z = mu0 Ha at both
ends). The sign of the magnetic term is fixed in __main__'s test by the linear
theory: the solver must reproduce the Cowley-Rosensweig stress coefficient G
with r0 = sqrt( mu_c mu_t ) / mu0, which it computes nowhere.

Discretisation. Horizontally spectral (FFT, the cell is periodic). Vertically
the space is mapped, z = zeta + h( x, y ) beta( zeta ), beta = exp( -( zeta / Zc )^2 ),
so the surface is the level zeta = 0 and the air and the fluid are each a
union of whole intervals; zeta is geometrically stretched away from the surface.
Pi is discretised as a sum over intervals with midpoint values (an exact
energy for the discrete problem, so the discrete operator is symmetric and the
discrete equilibrium is variational: the magnetic stress is the exact
derivative of the discrete Pi*, by the envelope theorem, not a differenced
field at the surface). Inner problem: Newton on the convex Pi, each step by
conjugate gradients preconditioned with the flat surface's linearised operator
(FFT in x, y, tridiagonal in zeta). Outer problem: the surface, by Newton-Krylov.
"""
import numpy as np
from fluids import Fluid, MU0, GRAVITY


class Cell:
    """The rectangular periodic cell of the hexagonal lattice, and the mapped vertical grid."""

    def __init__(self, fluid, nx=32, dz0=None, growth=1.07, zc=None, ztop=None, zbot=None, ny=None):
        self.fluid = fluid
        k = fluid.kc
        self.a = 4 * np.pi / (np.sqrt(3) * k)
        self.lx = self.a
        self.ly = np.sqrt(3) * self.a
        self.nx = nx
        self.ny = ny if ny is not None else int(2 * round(np.sqrt(3) * nx / 2))
        self.dA = self.lx * self.ly / (self.nx * self.ny)
        kx = 2 * np.pi * np.fft.fftfreq(self.nx, d=self.lx / self.nx)
        ky = 2 * np.pi * np.fft.fftfreq(self.ny, d=self.ly / self.ny)
        self.KX, self.KY = np.meshgrid(kx, ky)
        # The Nyquist rows carry no derivative (real fields stay real).
        if self.nx % 2 == 0:
            self.KX[:, self.nx // 2] = 0.0
        if self.ny % 2 == 0:
            self.KY[self.ny // 2, :] = 0.0
        self.K2 = self.KX ** 2 + self.KY ** 2
        self.x = np.arange(self.nx) * self.lx / self.nx
        self.y = np.arange(self.ny) * self.ly / self.ny
        self.X, self.Y = np.meshgrid(self.x, self.y)
        lam = fluid.lam
        self.zc = zc if zc is not None else 1.0 * lam
        dz0 = dz0 if dz0 is not None else lam / 160
        ztop = ztop if ztop is not None else 3.5 * lam
        zbot = zbot if zbot is not None else 3.5 * lam

        def side(extent):
            out, z, d = [0.0], 0.0, dz0
            while z < extent:
                z += d
                out.append(z)
                d *= growth
            return np.array(out)

        up = side(ztop)
        down = side(zbot)
        self.zeta = np.concatenate([-down[::-1], up[1:]])
        self.k0 = len(down) - 1  # the surface level
        self.nz = len(self.zeta)
        self.dz = np.diff(self.zeta)
        self.zm = 0.5 * (self.zeta[1:] + self.zeta[:-1])
        self.fluid_interval = self.zm < 0
        self.beta = np.exp(-((self.zm / self.zc) ** 2))
        self.dbeta = -2 * self.zm / self.zc ** 2 * self.beta
        self.beta_nodes = np.exp(-((self.zeta / self.zc) ** 2))

    # spectral derivatives on (..., ny, nx)
    def dx(self, f):
        return np.real(np.fft.ifft2(1j * self.KX * np.fft.fft2(f)))

    def dy(self, f):
        return np.real(np.fft.ifft2(1j * self.KY * np.fft.fft2(f)))

    def physical_z(self, h):
        return self.zeta[:, None, None] + h[None] * self.beta_nodes[:, None, None]


class Magnetics:
    """Pi[phi; h] and its derivatives, for one cell, one fluid, one applied field."""

    def __init__(self, cell, Ha):
        self.c = cell
        self.f = cell.fluid
        self.Ha = Ha
        self.base = self.f.base(Ha)
        self._prec = None

    # --- the field at interval midpoints -------------------------------------
    def geometry(self, h):
        c = self.c
        hx, hy = c.dx(h), c.dy(h)
        b = c.beta[:, None, None]
        p = b * hx[None]
        q = b * hy[None]
        J = 1 + c.dbeta[:, None, None] * h[None]
        return p, q, J

    def field(self, phi, geo):
        c = self.c
        p, q, J = geo
        s = 0.5 * (phi[1:] + phi[:-1])
        a = c.dx(s)
        b = c.dy(s)
        cz = (phi[1:] - phi[:-1]) / c.dz[:, None, None]
        Hx = -(a - p * cz / J)
        Hy = -(b - q * cz / J)
        Hz = -cz / J
        return Hx, Hy, Hz, cz

    def constitutive(self, Hm):
        """B magnitude, energy density w, chord and tangent relative permeabilities, per interval."""
        c, f = self.c, self.f
        B = MU0 * Hm.copy()
        w = MU0 * Hm * Hm / 2
        mc = np.ones_like(Hm)
        mt = np.ones_like(Hm)
        fl = c.fluid_interval
        Hf = Hm[fl]
        Mf = f.M(Hf)
        B[fl] = MU0 * (Hf + Mf)
        w[fl] = MU0 * (Hf * Hf / 2 + f.Phi(Hf))
        with np.errstate(divide="ignore", invalid="ignore"):
            mcf = np.where(Hf > 0, 1 + Mf / np.where(Hf > 0, Hf, 1), 1 + f.chi)
        mc[fl] = mcf
        mt[fl] = 1 + f.dM(Hf)
        return B, w, mc, mt

    def energy(self, phi, geo):
        c = self.c
        p, q, J = geo
        Hx, Hy, Hz, cz = self.field(phi, geo)
        Hm = np.sqrt(Hx * Hx + Hy * Hy + Hz * Hz)
        _, w, _, _ = self.constitutive(Hm)
        vol = np.sum(c.dz[:, None, None] * J * w) * c.dA
        bnd = MU0 * self.Ha * c.dA * np.sum(phi[-1] - phi[0])
        return vol + bnd

    def _adjoint(self, Fa, Fb, Fc):
        """The gradient in phi of sum( Fa*a + Fb*b + Fc*cz ) where a, b are the midpoint
        horizontal derivatives and cz the vertical difference."""
        c = self.c
        g = np.zeros((c.nz, c.ny, c.nx))
        ga = -c.dx(Fa) - c.dy(Fb)  # adjoint of the spectral derivative is minus it
        g[1:] += 0.5 * ga
        g[:-1] += 0.5 * ga
        t = Fc / c.dz[:, None, None]
        g[1:] += t
        g[:-1] -= t
        return g

    def gradient(self, phi, geo, want_state=False):
        c = self.c
        p, q, J = geo
        Hx, Hy, Hz, cz = self.field(phi, geo)
        Hm = np.sqrt(Hx * Hx + Hy * Hy + Hz * Hz)
        B, w, mc, mt = self.constitutive(Hm)
        with np.errstate(divide="ignore", invalid="ignore"):
            scale = np.where(Hm > 0, B / np.where(Hm > 0, Hm, 1), MU0 * mc)
        Bx, By, Bz = scale * Hx, scale * Hy, scale * Hz
        vol = c.dz[:, None, None] * J * c.dA
        # dH = -( da - p dc / J, db - q dc / J, dc / J )
        Fa = -vol * Bx
        Fb = -vol * By
        Fc = vol * (Bx * p + By * q - Bz) / J
        g = self._adjoint(Fa, Fb, Fc)
        g[-1] += MU0 * self.Ha * c.dA
        g[0] -= MU0 * self.Ha * c.dA
        if want_state:
            return g, dict(Hx=Hx, Hy=Hy, Hz=Hz, Hm=Hm, B=B, w=w, mc=mc, mt=mt, Bx=Bx, By=By, Bz=Bz, cz=cz)
        return g

    def hessvec(self, v, geo, st):
        c = self.c
        p, q, J = geo
        dHx, dHy, dHz, dcz = self.field(v, geo)
        Hm = st["Hm"]
        with np.errstate(divide="ignore", invalid="ignore"):
            inv = np.where(Hm > 0, 1 / np.where(Hm > 0, Hm, 1), 0)
        ux, uy, uz = st["Hx"] * inv, st["Hy"] * inv, st["Hz"] * inv
        proj = ux * dHx + uy * dHy + uz * dHz
        mc, mt = st["mc"], st["mt"]
        dBx = MU0 * (mc * dHx + (mt - mc) * proj * ux)
        dBy = MU0 * (mc * dHy + (mt - mc) * proj * uy)
        dBz = MU0 * (mc * dHz + (mt - mc) * proj * uz)
        vol = c.dz[:, None, None] * J * c.dA
        Fa = -vol * dBx
        Fb = -vol * dBy
        Fc = vol * (dBx * p + dBy * q - dBz) / J
        return self._adjoint(Fa, Fb, Fc)

    # --- the flat surface's linearised operator, as a preconditioner -----------
    def preconditioner(self):
        if self._prec is not None:
            return self._prec
        c = self.c
        b = self.base
        mu_h = np.where(c.fluid_interval, MU0 * b["mc"], MU0)
        mu_v = np.where(c.fluid_interval, MU0 * b["mt"], MU0)
        K2 = c.K2.ravel()
        nz, M = c.nz, K2.size
        diag = np.zeros((nz, M))
        off = np.zeros((nz - 1, M))
        for k in range(nz - 1):
            e_h = mu_h[k] * c.dz[k] / 4 * K2
            e_v = mu_v[k] / c.dz[k]
            diag[k] += e_h + e_v
            diag[k + 1] += e_h + e_v
            off[k] += e_h - e_v
        diag *= c.dA
        off *= c.dA
        # the constant mode is free: pin it at the bottom
        zero = K2 == 0
        diag[0, zero] += 1e3 * np.max(diag[:, zero])
        # Thomas factorisation, once
        cp = np.zeros((nz - 1, M))
        dd = np.zeros((nz, M))
        dd[0] = diag[0]
        for k in range(nz - 1):
            cp[k] = off[k] / dd[k]
            dd[k + 1] = diag[k + 1] - off[k] * cp[k]
        self._prec = (off, cp, dd)
        return self._prec

    def apply_prec(self, r):
        c = self.c
        off, cp, dd = self.preconditioner()
        nz = c.nz
        # the nodal operator is dA * T in the sense sum_xy; Fourier-diagonalise with
        # unitary scaling: the per-mode matrix is the same as the nodal one.
        R = np.fft.fft2(r).reshape(nz, -1)
        y = np.empty_like(R)
        y[0] = R[0] / dd[0]
        for k in range(1, nz):
            y[k] = (R[k] - off[k - 1] * y[k - 1]) / dd[k]
        x = np.empty_like(R)
        x[-1] = y[-1]
        for k in range(nz - 2, -1, -1):
            x[k] = y[k] - cp[k] * x[k + 1]
        return np.real(np.fft.ifft2(x.reshape(nz, c.ny, c.nx)))

    # --- the inner solve ----------------------------------------------------
    def initial_phi(self, h):
        c = self.c
        z = c.physical_z(h)
        b = self.base
        phi = np.where(c.zeta[:, None, None] >= 0, -self.Ha * z, -b["Hin"] * z)
        return phi

    def solve(self, h, phi=None, tol=1e-11, max_newton=40, verbose=False):
        """Minimise Pi over phi for the surface h. Returns phi, Pi*, and the state."""
        geo = self.geometry(h)
        if phi is None:
            phi = self.initial_phi(h)
        scale = MU0 * self.Ha * self.c.dA * self.c.nx * self.c.ny
        E = self.energy(phi, geo)
        for it in range(max_newton):
            g, st = self.gradient(phi, geo, True)
            gn = np.sqrt(np.sum(g * g))
            if verbose:
                print("   newton %d |g| %.3e  Pi %.12e" % (it, gn / scale, E))
            if gn < tol * scale:
                break
            d = self._pcg(lambda v: self.hessvec(v, geo, st), -g, rtol=min(0.1, 0.5 * np.sqrt(gn / scale)))
            # backtracking on the convex energy
            t = 1.0
            for _ in range(30):
                trial = phi + t * d
                Et = self.energy(trial, geo)
                if Et <= E + 1e-4 * t * np.sum(g * d):
                    break
                t *= 0.5
            phi, E = trial, Et
        return phi, E, geo

    def _pcg(self, A, b, rtol=1e-3, maxit=500):
        x = np.zeros_like(b)
        r = b.copy()
        z = self.apply_prec(r)
        p = z.copy()
        rz = np.sum(r * z)
        b0 = np.sqrt(np.sum(b * b))
        for it in range(maxit):
            Ap = A(p)
            alpha = rz / np.sum(p * Ap)
            x += alpha * p
            r -= alpha * Ap
            if np.sqrt(np.sum(r * r)) < rtol * b0:
                break
            z = self.apply_prec(r)
            rz_new = np.sum(r * z)
            p = z + (rz_new / rz) * p
            rz = rz_new
        self.last_cg = it + 1
        return x

    # --- d Pi* / d h, by the envelope theorem -----------------------------------
    def shape_gradient(self, phi, geo):
        c = self.c
        p, q, J = geo
        _, st = self.gradient(phi, geo, True)
        Bx, By, Bz, w, cz = st["Bx"], st["By"], st["Bz"], st["w"], st["cz"]
        dz = c.dz[:, None, None] * c.dA
        b = c.beta[:, None, None]
        db = c.dbeta[:, None, None]
        # f = J w( |H| ), with p = beta h_x, q = beta h_y, J = 1 + beta' h
        dfdp = Bx * cz
        dfdq = By * cz
        dfdJ = w + (cz / J) * (Bz - p * Bx - q * By)
        gx = np.sum(dz * b * dfdp, axis=0)
        gy = np.sum(dz * b * dfdq, axis=0)
        gJ = np.sum(dz * db * dfdJ, axis=0)
        return -c.dx(gx) - c.dy(gy) + gJ

    def surface_field(self, phi, geo):
        """The fluid-side field magnitude in the surface's interval (for reporting)."""
        _, st = self.gradient(phi, geo, True)
        return st["Hm"][self.c.k0 - 1]


def hexagon_mode(cell):
    """The fundamental: three cosines at k_c, 120 degrees apart, a spike at the origin,
    normalised so its peak is 1."""
    k = cell.fluid.kc
    ks = [(0.0, k), (k * np.sqrt(3) / 2, -k / 2), (-k * np.sqrt(3) / 2, -k / 2)]
    return sum(np.cos(a * cell.X + b * cell.Y) for a, b in ks) / 3.0


if __name__ == "__main__":
    # The linear limit: a tiny fundamental, the magnetic part of dE/dh against
    # the Cowley-Rosensweig G k h (r0 = sqrt( mu_c mu_t )), which is computed
    # nowhere in this file.
    import sys

    name = sys.argv[1] if len(sys.argv) > 1 else "EFH1"
    f = Fluid(name)
    for nx in [16, 24]:
        cell = Cell(f, nx=nx)
        Ha = f.applied_for_m(1.2)
        mag = Magnetics(cell, Ha)
        e1 = hexagon_mode(cell)
        rows = []
        for amp in [1e-6, 2e-6]:
            h = amp * e1
            phi, Pi, geo = mag.solve(h, tol=1e-12)
            g = mag.shape_gradient(phi, geo) / cell.dA
            proj = np.sum(g * e1) / np.sum(e1 * e1)
            rows.append(proj / (mag.base["G"] * f.kc * amp))
        print("%s nx %d nz %d: dPi*/dh along the fundamental / ( G k_c A ) = %.6f, %.6f" % (name, nx, cell.nz, rows[0], rows[1]))


class Surface:
    """The free surface's equilibria for one fluid in one cell.

    The surface is held in the hexagonal pattern's own symmetric basis: one
    unknown per orbit of reciprocal lattice vectors under the hexagon's twelve
    symmetries (rotations by 60 degrees and reflections), each the sum of its
    orbit's cosines, so the two spikes of the cell stay identical and
    hexagonal. Equations: the energy's gradient projected on each orbit
    (Galerkin). The branch is parametrised by the fundamental's amplitude A
    and the applied field is an unknown, so Newton reaches the subcritical,
    unstable part of the branch as well as the stable one."""

    def __init__(self, cell, kmax=None):
        self.c = cell
        self.f = cell.fluid
        self.Hac = self.f.applied_for_m(1.0)
        self.phi = None
        self.mag = None
        self.Ha_of_mag = None
        self.evals = 0
        self._orbits(kmax)

    def _orbits(self, kmax):
        c = self.c
        kc = self.f.kc
        nyq = min(np.pi * c.nx / c.lx, np.pi * c.ny / c.ly)
        kmax = kmax if kmax is not None else (2.0 / 3.0) * nyq
        # rect modes (i, j): k = ( 2 pi i / lx, 2 pi j / ly ); on the hexagonal reciprocal
        # lattice when i + j is even (the second spike sits at ( lx/2, ly/2 )).
        found = {}
        ii = np.fft.fftfreq(c.nx, d=1.0 / c.nx).astype(int)
        jj = np.fft.fftfreq(c.ny, d=1.0 / c.ny).astype(int)
        for i in ii:
            for j in jj:
                if (i + j) % 2:
                    continue
                kx, ky = 2 * np.pi * i / c.lx, 2 * np.pi * j / c.ly
                kk = np.hypot(kx, ky)
                if kk == 0 or kk > kmax:
                    continue
                # canonical representative of the orbit: its 12 images, rounded
                imgs = []
                for r in range(6):
                    th = r * np.pi / 3
                    for sgn in (1, -1):
                        ax, ay = kx, sgn * ky
                        bx = ax * np.cos(th) - ay * np.sin(th)
                        by = ax * np.sin(th) + ay * np.cos(th)
                        imgs.append((round(bx * c.lx / (2 * np.pi)), round(by * c.ly / (2 * np.pi))))
                key = min(imgs)
                found.setdefault(key, set()).add((int(i), int(j)))
        keys = sorted(found, key=lambda k: (np.hypot(2 * np.pi * k[0] / c.lx, 2 * np.pi * k[1] / c.ly), k))
        self.orbit_k = []
        self.orbit_members = []
        basis = []
        for key in keys:
            members = found[key]
            self.orbit_members.append(sorted(members))
            f = np.zeros((c.ny, c.nx))
            for (i, j) in members:
                f += np.cos(2 * np.pi * i * c.X / c.lx + 2 * np.pi * j * c.Y / c.ly)
            f /= f[0, 0]  # 1 at the spike
            basis.append(f)
            self.orbit_k.append(np.hypot(2 * np.pi * key[0] / c.lx, 2 * np.pi * key[1] / c.ly) / kc)
        self.basis = np.array(basis)  # (n_orbits, ny, nx)
        self.norm2 = np.sum(self.basis ** 2, axis=(1, 2))
        assert abs(self.orbit_k[0] - 1) < 1e-9, "the first orbit must be the fundamental"
        self.e1 = self.basis[0]

    def magnetics(self, Ha):
        if self.mag is None or self.Ha_of_mag != Ha:
            self.mag = Magnetics(self.c, Ha)
            self.Ha_of_mag = Ha
        return self.mag

    def gradient(self, h, Ha, tol=1e-11):
        """dE/dh per node (Pa m^2), zero-mean, and the magnetic state."""
        c, f = self.c, self.f
        mag = self.magnetics(Ha)
        phi, Pi, geo = mag.solve(h, phi=self.phi, tol=tol)
        self.phi = phi
        self.evals += 1
        hx, hy = c.dx(h), c.dy(h)
        nn = np.sqrt(1 + hx * hx + hy * hy)
        kappa = c.dx(hx / nn) + c.dy(hy / nn)
        g = c.dA * (f.rho * GRAVITY * h - f.sigma * kappa) - mag.shape_gradient(phi, geo)
        return g - g.mean(), phi, geo, Pi

    def energy(self, h, Ha):
        c, f = self.c, self.f
        mag = self.magnetics(Ha)
        phi, Pi, geo = mag.solve(h, phi=self.phi)
        hx, hy = c.dx(h), c.dy(h)
        return c.dA * np.sum(f.rho * GRAVITY * h * h / 2 + f.sigma * (np.sqrt(1 + hx * hx + hy * hy) - 1)) - Pi

    def compose(self, A, x):
        """x = ( coefficients of orbits 2.., ln( Ha / Ha_c ) )."""
        coef = np.concatenate([[A], x[:-1]])
        h = np.tensordot(coef, self.basis, axes=1)
        return h - h.mean(), self.Hac * np.exp(x[-1])

    def residual(self, A, x):
        c, f = self.c, self.f
        h, Ha = self.compose(A, x)
        g, _, _, _ = self.gradient(h, Ha)
        proj = np.tensordot(self.basis, g, axes=([1, 2], [0, 1])) / (self.norm2 * c.dA * f.rho * GRAVITY)
        return proj  # one equation per orbit, the fundamental's included

    def newton(self, A, x, tol=1e-12, maxit=30, verbose=False):
        n = x.size
        F = self.residual(A, x)
        phi0 = None
        for it in range(maxit):
            fn = np.max(np.abs(F))
            if verbose:
                _, Ha = self.compose(A, x)
                print("    newton %d |F| %.3e  m %.7f" % (it, fn, self.f.base(Ha)["m"]), flush=True)
            if fn < tol:
                return x, True
            phi0 = self.phi.copy()
            Jm = np.zeros((n, n))
            for i in range(n):
                step = 1e-7 * self.f.lam if i < n - 1 else 1e-7
                xp = x.copy()
                xp[i] += step
                self.phi = phi0.copy()
                Jm[:, i] = (self.residual(A, xp) - F) / step
            dx = np.linalg.solve(Jm, -F)
            t = 1.0
            for _ in range(20):
                self.phi = phi0.copy()
                trial = x + t * dx
                Ft = self.residual(A, trial)
                if np.max(np.abs(Ft)) < (1 - 1e-4 * t) * fn or t < 1e-4:
                    break
                t *= 0.5
            x, F = trial, Ft
        return x, False

    def amplitude(self, h):
        """Gollwitzer et al.'s amplitude: the spike's height minus the mean of the
        cell's corners (the three-fold points between three spikes)."""
        c = self.c
        a = c.a
        corners = [(a / 2, a / (2 * np.sqrt(3))), (0.0, a / np.sqrt(3))]
        H = np.fft.fft2(h) / h.size
        vals = []
        for (px, py) in corners:
            vals.append(np.real(np.sum(H * np.exp(1j * (c.KX * px + c.KY * py)))))
        return h[0, 0] - np.mean(vals)
