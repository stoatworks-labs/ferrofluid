"""The equilibrium free surface of one spike in an equal-area axisymmetric cell,
with the full nonlinear magnetostatics. CPU, numpy + scipy.sparse, double.

The same physics as solver.py (Gollwitzer et al. 2007's problem: an infinitely
deep Langevin fluid under vacuum, uniform B0 = mu0 Ha far above and below,
gravity, full capillary curvature), but in a circular cell of radius
R = sqrt( 8 pi / sqrt(3) ) / k_c, the area of the hexagonal pattern's cell
(Wigner-Seitz), with no radial flux of field or fluid across r = R. The
circle's fundamental, J0( j11 r / R ), sits at 1.0059 k_c, so the linear onset
moves by 2e-5 in m; what the circle costs against the true hexagon is
measured, not assumed (AGENTS.md, from solver.py's hexagonal cell where both
resolve the tip).

Why a circle at all: the hexagonal solver is spectral on a uniform grid, and an
EFH1 spike's tip is a few tenths of a millimetre across; resolving it there
means hundreds of orbits and a dense Jacobian. Here the radial grid crowds at
the axis, so a tip of 0.05 mm costs nothing.

Discretisation: z = zeta + h( r ) beta( zeta ) maps the surface to zeta = 0;
bilinear (Q1) elements in ( r, zeta ), 2 x 2 Gauss points, the surface
piecewise linear in r. The magnetic energy

    Pi = sum over Gauss points of 2 pi r J w( |H| ) + mu0 Ha ( integral_top phi - integral_bottom phi )

is minimised over the nodal potential; the surface's energy
E = gravity + surface tension - Pi* is made stationary at fixed volume. The
whole system (potential, surface, the volume's Lagrange multiplier, ln Ha) is
solved together by Newton with a sparse Jacobian: the potential's block is the
Hessian (the differential permeability tensor) assembled exactly; the surface's
columns by finite differences, three at a time (a node moves only the elements
either side of it); the mixed block's transpose closes the symmetry. The branch
is parametrised by the spike's peak height.
"""
import numpy as np
import scipy.sparse as sp
import scipy.sparse.linalg as spla

from fluids import Fluid, MU0, GRAVITY

GAUSS = np.array([0.5 - 0.5 / np.sqrt(3), 0.5 + 0.5 / np.sqrt(3)])


class AxiCell:
    def __init__(self, fluid, nr=160, alpha=3.0, dz0=None, growth=1.07, zc=None, ztop=None, zbot=None):
        self.f = fluid
        k = fluid.kc
        lam = fluid.lam
        self.R = np.sqrt(8 * np.pi / np.sqrt(3)) / k
        t = np.linspace(0, 1, nr + 1)
        self.r = self.R * (np.expm1(alpha * t) / np.expm1(alpha)) if alpha > 0 else self.R * t
        self.nr = nr
        dz0 = dz0 if dz0 is not None else lam / 400
        ztop = ztop if ztop is not None else 6.0 * lam
        zbot = zbot if zbot is not None else 4.0 * lam
        # the map's reach: a spike as tall as 1.5 lambda keeps the air's
        # Jacobian above 0.5 (max |beta'| = 0.86 / zc)
        self.zc = zc if zc is not None else 2.6 * lam

        def side(extent):
            out, z, d = [0.0], 0.0, dz0
            while z < extent:
                z += d
                out.append(z)
                d *= growth
            return np.array(out)

        up, down = side(ztop), side(zbot)
        self.zeta = np.concatenate([-down[::-1], up[1:]])
        self.k0 = len(down) - 1
        self.nz = len(self.zeta)
        self.nnode = (nr + 1) * self.nz
        # elements
        I, K = np.meshgrid(np.arange(nr), np.arange(self.nz - 1))
        self.ei = I.ravel()
        self.ek = K.ravel()
        ne = self.ei.size
        n00 = self.ek * (nr + 1) + self.ei
        self.conn = np.stack([n00, n00 + 1, n00 + (nr + 1), n00 + (nr + 1) + 1], axis=1)  # (ne, 4)
        self.dr = (self.r[1:] - self.r[:-1])[self.ei]
        self.dzeta = (self.zeta[1:] - self.zeta[:-1])[self.ek]
        self.fluid_el = (0.5 * (self.zeta[1:] + self.zeta[:-1]))[self.ek] < 0
        # Gauss points: (ne, 4)
        sg = np.array([GAUSS[0], GAUSS[1], GAUSS[0], GAUSS[1]])
        tg = np.array([GAUSS[0], GAUSS[0], GAUSS[1], GAUSS[1]])
        self.sg, self.tg = sg, tg
        self.rho = self.r[self.ei][:, None] + sg[None] * self.dr[:, None]
        self.zg = self.zeta[self.ek][:, None] + tg[None] * self.dzeta[:, None]
        self.beta = np.exp(-((self.zg / self.zc) ** 2))
        self.dbeta = -2 * self.zg / self.zc ** 2 * self.beta
        self.wq = 0.25 * self.dr[:, None] * self.dzeta[:, None] * 2 * np.pi * self.rho  # quadrature weight incl. 2 pi r
        # shape-function derivatives at the Gauss points, reference -> ( d/dr, d/dzeta )
        self.dNr = np.stack([-(1 - tg), (1 - tg), -tg, tg], axis=1)[None] / self.dr[:, None, None]  # (ne,4g,4n)
        self.dNz = np.stack([-(1 - sg), -sg, (1 - sg), sg], axis=1)[None] / self.dzeta[:, None, None]
        # boundary weights for integral of phi 2 pi r dr along a level
        wb = np.zeros(nr + 1)
        dr = self.r[1:] - self.r[:-1]
        wb[:-1] += 2 * np.pi * dr * (self.r[:-1] / 2 + dr / 6)
        wb[1:] += 2 * np.pi * dr * (self.r[:-1] / 2 + dr / 3)
        self.wb = wb  # also the volume weights for integral of h 2 pi r dr
        # the surface's elements (one per radial interval)
        self.rmid_g = self.r[:-1][:, None] + GAUSS[None] * dr[:, None]
        self.dr_s = dr
        # sparse pattern for the potential's Hessian
        rows = np.repeat(self.conn, 4, axis=1).ravel()
        cols = np.tile(self.conn, (1, 4)).ravel()
        self.hrows, self.hcols = rows, cols
        self.fluid = fluid

    def node(self, i, k):
        return k * (self.nr + 1) + i


class AxiProblem:
    fd_step = 1e-5  # wavelengths, the surface columns' difference step
    fd_central = True

    def __init__(self, cell):
        self.c = cell
        self.f = cell.f
        self.Hac = self.f.applied_for_m(1.0)
        nphi = cell.nnode
        self.nphi = nphi
        self.nh = cell.nr + 1
        self.n = nphi + self.nh + 2  # phi, h, lambda, lnHa
        self.pin = cell.node(cell.nr, 0)  # the potential's gauge

    # ---- geometry and field at Gauss points --------------------------------------
    def geometry(self, h):
        c = self.c
        hl = h[c.ei][:, None]
        hr = h[c.ei + 1][:, None]
        hg = hl + c.sg[None] * (hr - hl)
        slope = ((hr - hl)[:, 0] / c.dr)[:, None]
        p = slope * c.beta  # z_r
        J = 1 + hg * c.dbeta  # z_zeta
        return p, J

    def field(self, phi, p, J):
        c = self.c
        pe = phi[c.conn]  # (ne, 4)
        fr = np.einsum("egn,en->eg", c.dNr, pe)
        fz = np.einsum("egn,en->eg", c.dNz, pe)
        Hr = -(fr - p * fz / J)
        Hz = -fz / J
        return Hr, Hz, fz

    def constitutive(self, Hm):
        c, f = self.c, self.f
        fl = np.broadcast_to(c.fluid_el[:, None], Hm.shape)
        B = MU0 * Hm.copy()
        w = MU0 * Hm * Hm / 2
        mc = np.ones_like(Hm)
        mt = np.ones_like(Hm)
        Hf = Hm[fl]
        Mf = f.M(Hf)
        B[fl] = MU0 * (Hf + Mf)
        w[fl] = MU0 * (Hf * Hf / 2 + f.Phi(Hf))
        with np.errstate(divide="ignore", invalid="ignore"):
            mc[fl] = np.where(Hf > 0, 1 + Mf / np.where(Hf > 0, Hf, 1), 1 + f.chi)
        mt[fl] = 1 + f.dM(Hf)
        return B, w, mc, mt

    def magnetic(self, phi, h, Ha, want_hess=False):
        """Pi, dPi/dphi, dPi/dh (at fixed phi), and the potential's Hessian."""
        c = self.c
        p, J = self.geometry(h)
        Hr, Hz, fz = self.field(phi, p, J)
        Hm = np.sqrt(Hr * Hr + Hz * Hz)
        B, w, mc, mt = self.constitutive(Hm)
        with np.errstate(divide="ignore", invalid="ignore"):
            inv = np.where(Hm > 0, 1 / np.where(Hm > 0, Hm, 1), 0)
        ur, uz = Hr * inv, Hz * inv
        Br, Bz = B * ur, B * uz
        vol = c.wq * J
        Pi = np.sum(vol * w) + MU0 * Ha * (np.dot(c.wb, phi[c.node(np.arange(c.nr + 1), c.nz - 1)]) - np.dot(c.wb, phi[c.node(np.arange(c.nr + 1), 0)]))
        # dPi/dphi: dH = -( dfr - p dfz / J, dfz / J )
        Fr = -vol * Br
        Fz = vol * (Br * p - Bz) / J
        ge = np.einsum("eg,egn->en", Fr, c.dNr) + np.einsum("eg,egn->en", Fz, c.dNz)
        g = np.bincount(c.conn.ravel(), weights=ge.ravel(), minlength=c.nnode)
        top = c.node(np.arange(c.nr + 1), c.nz - 1)
        bot = c.node(np.arange(c.nr + 1), 0)
        g[top] += MU0 * Ha * c.wb
        g[bot] -= MU0 * Ha * c.wb
        # dPi/dh at fixed phi: f = J w, p = z_r = slope beta, J = 1 + h_g beta'
        dfdp = Br * fz
        dfdJ = w + (fz / J) * (Bz - p * Br)
        a = c.wq * (dfdp * c.beta)  # times d slope / d h
        b = c.wq * (dfdJ * c.dbeta)  # times d h_g / d h
        left = -np.sum(a, axis=1) / c.dr + np.sum(b * (1 - c.sg[None]), axis=1)
        right = np.sum(a, axis=1) / c.dr + np.sum(b * c.sg[None], axis=1)
        gh = np.bincount(c.ei, weights=left, minlength=c.nr + 1) + np.bincount(c.ei + 1, weights=right, minlength=c.nr + 1)
        out = dict(Pi=Pi, gphi=g, gh=gh, Hm=Hm, B=B, ur=ur, uz=uz)
        if want_hess:
            # dB = mu0 ( mu_c dH + ( mu_t - mu_c ) ( u . dH ) u ); dH = Lg dphi_e
            Lr = -(c.dNr - (p / J)[:, :, None] * c.dNz)
            Lz = -(c.dNz / J[:, :, None])
            D11 = MU0 * (mc + (mt - mc) * ur * ur)
            D12 = MU0 * (mt - mc) * ur * uz
            D22 = MU0 * (mc + (mt - mc) * uz * uz)
            # K_e = sum_g vol ( Lr^T D11 Lr + Lr^T D12 Lz + Lz^T D12 Lr + Lz^T D22 Lz )
            Ke = (np.einsum("eg,egn,egm->enm", vol * D11, Lr, Lr) + np.einsum("eg,egn,egm->enm", vol * D12, Lr, Lz)
                  + np.einsum("eg,egn,egm->enm", vol * D12, Lz, Lr) + np.einsum("eg,egn,egm->enm", vol * D22, Lz, Lz))
            out["K"] = sp.coo_matrix((Ke.ravel(), (c.hrows, c.hcols)), shape=(c.nnode, c.nnode)).tocsr()
        return out

    # ---- the surface's own energy -------------------------------------------------
    def surface_terms(self, h):
        c, f = self.c, self.f
        dr = c.dr_s
        slope = (h[1:] - h[:-1]) / dr
        # gravity: integral rho g h^2 / 2 2 pi r dr, 2-point Gauss per interval (exact)
        hl, hr = h[:-1][:, None], h[1:][:, None]
        hg = hl + GAUSS[None] * (hr - hl)
        wg = 0.5 * dr[:, None] * 2 * np.pi * c.rmid_g
        Eg = np.sum(wg * f.rho * GRAVITY * hg * hg / 2)
        dEg_l = np.sum(wg * f.rho * GRAVITY * hg * (1 - GAUSS[None]), axis=1)
        dEg_r = np.sum(wg * f.rho * GRAVITY * hg * GAUSS[None], axis=1)
        area = np.pi * (c.r[1:] ** 2 - c.r[:-1] ** 2)
        root = np.sqrt(1 + slope * slope)
        Es = f.sigma * np.sum(area * (root - 1))
        ds = f.sigma * area * slope / root / dr
        g = np.zeros(c.nr + 1)
        g[:-1] += dEg_l - ds
        g[1:] += dEg_r + ds
        return Eg + Es, g

    # ---- the coupled residual ------------------------------------------------------
    def unpack(self, X):
        n1 = self.nphi
        n2 = n1 + self.nh
        return X[:n1], X[n1:n2], X[n2], self.Hac * np.exp(X[n2 + 1])

    def residual(self, X, peak, want=False):
        c = self.c
        phi, h, lam, Ha = self.unpack(X)
        mg = self.magnetic(phi, h, Ha, want_hess=want)
        Es, gs = self.surface_terms(h)
        Rphi = mg["gphi"].copy()
        Rphi[self.pin] = 0.0
        Rh = gs - mg["gh"] - lam * c.wb
        RV = np.dot(c.wb, h)
        if isinstance(peak, tuple):
            kind, val = peak
            RP = (np.log(Ha / self.Hac) - val) * self.f.lam if kind == "lnu" else h[0] - val
        else:
            RP = h[0] - peak
        R = np.concatenate([Rphi, Rh, [RV, RP]])
        return (R, mg) if want else R

    def scales(self):
        c, f = self.c, self.f
        lam = f.lam
        area = np.pi * c.R ** 2
        sphi = MU0 * self.Hac * area / c.nnode ** 0.5
        sh = f.rho * GRAVITY * lam * area / c.nr
        return sphi, sh, area * lam, lam

    def jacobian(self, X, peak):
        c = self.c
        R0, mg = self.residual(X, peak, want=True)
        n1, nh = self.nphi, self.nh
        n = self.n
        # columns for h: finite differences three at a time. Central, and a
        # step of 1e-5 wavelengths: near the bifurcation the scaled Jacobian's
        # condition number is about 1e8, and one-sided differences (relative
        # error 1e-8) left Newton converging linearly at a rate of 0.5 to 0.8
        rows, cols, vals = [], [], []
        step = self.fd_step * self.f.lam
        for colour in range(3):
            idx = np.arange(colour, nh, 3)
            Xp = X.copy()
            Xp[n1 + idx] += step
            if self.fd_central:
                Xm = X.copy()
                Xm[n1 + idx] -= step
                d = (self.residual(Xp, peak) - self.residual(Xm, peak)) / (2 * step)
            else:
                d = (self.residual(Xp, peak) - R0) / step
            # each h_i moves the elements either side: potential rows in columns i-1..i+1,
            # surface rows i-1..i+1
            for i in idx:
                lo, hi = max(i - 1, 0), min(i + 1, nh - 1)
                cols_r = np.arange(lo, hi + 1)
                nodes = (np.arange(c.nz)[:, None] * (c.nr + 1) + cols_r[None]).ravel()
                rr = np.concatenate([nodes, n1 + cols_r])
                rows.append(rr)
                cols.append(np.full(rr.size, n1 + i))
                vals.append(d[rr])
        Fd = sp.coo_matrix((np.concatenate(vals), (np.concatenate(rows), np.concatenate(cols))), shape=(n, n)).tocsr()
        # the potential's block, exact
        K = mg["K"].tocoo()
        keep = K.row != self.pin
        blocks = [sp.coo_matrix((K.data[keep], (K.row[keep], K.col[keep])), shape=(n, n))]
        blocks.append(sp.coo_matrix(([1.0], ([self.pin], [self.pin])), shape=(n, n)))
        # the mixed block: d Rh / d phi = -( d Rphi / d h )^T  (both from d^2 Pi / dphi dh)
        Fp = Fd[:n1, n1:n1 + nh].tocoo()
        keep2 = Fp.row != self.pin
        blocks.append(sp.coo_matrix((-Fp.data[keep2], (n1 + Fp.col[keep2], Fp.row[keep2])), shape=(n, n)))
        # FD columns (potential and surface rows) from h
        blocks.append(Fd)
        # lambda column: Rh = ... - lam wb
        blocks.append(sp.coo_matrix((-c.wb, (n1 + np.arange(nh), np.full(nh, n1 + nh))), shape=(n, n)))
        # lnHa column: the boundary term, d/dlnHa of mu0 Ha ( wb at top, -wb at bottom )
        phi, h, lam, Ha = self.unpack(X)
        top = c.node(np.arange(c.nr + 1), c.nz - 1)
        bot = c.node(np.arange(c.nr + 1), 0)
        vv = np.concatenate([MU0 * Ha * c.wb, -MU0 * Ha * c.wb])
        rr = np.concatenate([top, bot])
        ok = rr != self.pin
        blocks.append(sp.coo_matrix((vv[ok], (rr[ok], np.full(ok.sum(), n1 + nh + 1))), shape=(n, n)))
        # the constraints' rows
        blocks.append(sp.coo_matrix((c.wb, (np.full(nh, n1 + nh), n1 + np.arange(nh))), shape=(n, n)))
        if isinstance(peak, tuple) and peak[0] == "lnu":
            blocks.append(sp.coo_matrix(([self.f.lam], ([n1 + nh + 1], [n1 + nh + 1])), shape=(n, n)))
        else:
            blocks.append(sp.coo_matrix(([1.0], ([n1 + nh + 1], [n1])), shape=(n, n)))
        Jm = blocks[0]
        for b in blocks[1:]:
            Jm = Jm + b
        return Jm.tocsc(), R0

    def flat_start(self, Ha):
        c = self.c
        b = self.f.base(Ha)
        Z = np.repeat(c.zeta, c.nr + 1)
        phi = np.where(Z >= 0, -Ha * Z, -b["Hin"] * Z)
        X = np.concatenate([phi, np.zeros(self.nh), [0.0, np.log(Ha / self.Hac)]])
        return X

    def newton(self, X, peak, tol=1e-9, maxit=30, verbose=False):
        sphi, sh, sV, sP = self.scales()
        n1, nh = self.nphi, self.nh
        rs = np.concatenate([np.full(n1, 1 / sphi), np.full(nh, 1 / sh), [1 / sV, 1 / sP]])
        cs = np.concatenate([np.full(n1, self.Hac * self.f.lam), np.full(nh, self.f.lam), [self.f.rho * GRAVITY * self.f.lam, 1.0]])

        def norm(R):
            return np.max(np.abs(R * rs))

        for it in range(maxit):
            Jm, R = self.jacobian(X, peak)
            rn = norm(R)
            if verbose:
                print("     newton %d  |R| %.3e" % (it, rn), flush=True)
            if rn < tol:
                return X, True, it
            # rows and columns both scaled: SuperLU's ordering is three times
            # faster on the balanced matrix than on the row-scaled one
            A = (sp.diags(rs) @ Jm @ sp.diags(cs)).tocsc()
            dX = cs * spla.splu(A).solve(-(R * rs))
            t = 1.0
            while t > 1e-3:
                Xt = X + t * dX
                Rt = self.residual(Xt, peak)
                if norm(Rt) < (1 - 1e-4 * t) * rn:
                    break
                t *= 0.5
            X = Xt
        Jm, R = self.jacobian(X, peak)
        return X, norm(R) < tol, maxit

    # ---- reporting ------------------------------------------------------------------
    def describe(self, X):
        c, f = self.c, self.f
        phi, h, lam, Ha = self.unpack(X)
        b = f.base(Ha)
        # tip radius from a parabola through the first nodes
        r = c.r
        k = 4
        A = np.vstack([np.ones(k), r[:k] ** 2]).T
        coef = np.linalg.lstsq(A, h[:k], rcond=None)[0]
        tip = -1 / (2 * coef[1]) if coef[1] < 0 else float("inf")
        mg = self.magnetic(phi, h, Ha)
        # the fluid side's strongest field, in the elements under the surface
        under = c.fluid_el & (c.ek == c.k0 - 1)
        Hfluid = float(np.max(mg["Hm"][under]))
        return dict(Ha=Ha, u=Ha / self.Hac, eps=(Ha / self.Hac) ** 2 - 1, m=b["m"], B_mT=MU0 * Ha * 1e3,
                    peak=float(h[0]), rim=float(h[-1]), trough=float(h.min()), tip_radius=tip,
                    Hmax_over_Hin=Hfluid / b["Hin"])


STEP_GROW = 7  # Newton steps at or below which the continuation step grows
STEP_SHRINK = 12  # and above which it shrinks
DPEAK_MAX = 0.005  # wavelengths, the largest step in the peak


def trace(fluid, nr=160, alpha=3.0, dz0=None, growth=1.07, u_max=1.6, peak0=None, dpeak0=None, verbose=True, max_points=3000, save=None):
    cell = AxiCell(fluid, nr=nr, alpha=alpha, dz0=dz0, growth=growth)
    P = AxiProblem(cell)
    lam = fluid.lam
    X = P.flat_start(P.Hac)
    # near the bifurcation the amplitude-parametrised Jacobian is nearly
    # singular and Newton crawls; a tenth of a millimetre is clear of it
    peak = peak0 if peak0 is not None else 0.008 * lam
    # start on the linear mode, not the flat surface
    from scipy.special import j0
    kp = 3.8317059702 / cell.R
    X[P.nphi:P.nphi + P.nh] = peak * j0(kp * cell.r)
    dpeak = dpeak0 if dpeak0 is not None else 0.004 * lam
    out = []
    prev = None
    import time

    t0 = time.time()
    while len(out) < max_points:
        guess = X.copy()
        if prev is not None:
            guess = X + (X - prev[1]) * (peak - out[-1]["peak"]) / (out[-1]["peak"] - prev[0])
        Xs, ok, its = P.newton(guess, peak, maxit=100 if not out else 30)
        if not ok and not out and peak < 0.05 * lam:
            # the first point, too near the bifurcation for Newton: start further out
            peak *= 2.0
            X = P.flat_start(P.Hac)
            X[P.nphi:P.nphi + P.nh] = peak * j0(kp * cell.r)
            continue
        if not ok:
            if dpeak < 1e-4 * lam and len(out) > 2:
                break  # the peak stops being a good parameter: carry on in u below
            if dpeak < 1e-6 * lam:
                if verbose:
                    print("  stopped: no convergence at peak %.4f mm" % (peak * 1e3), flush=True)
                break
            dpeak /= 2
            peak = out[-1]["peak"] + dpeak if out else peak / 2
            continue
        rec = P.describe(Xs)
        rec["h"] = Xs[P.nphi:P.nphi + P.nh].tolist()
        if out:
            prev = (out[-1]["peak"], X)
        X = Xs
        out.append(rec)
        if verbose:
            print("  peak %.4f mm  m %.6f  u %.5f  eps %+.5f  tip R %.4f mm  rim %.3f  H/Hin %.2f  (%d its, %.0f s)"
                  % (rec["peak"] * 1e3, rec["m"], rec["u"], rec["eps"], rec["tip_radius"] * 1e3, rec["rim"] * 1e3, rec["Hmax_over_Hin"], its, time.time() - t0), flush=True)
        if save is not None:
            save(cell, out, X)
        if rec["u"] > u_max:
            return cell, out
        # grow while Newton takes up to seven steps from the secant predictor,
        # to at most 0.005 wavelengths: the branch is a table of seeds and the
        # circle's side of a comparison, so it is sampled finely, not just solved
        if its <= STEP_GROW:
            dpeak = min(dpeak * 1.3, DPEAK_MAX * lam)
        elif its > STEP_SHRINK:
            dpeak *= 0.6
        peak += dpeak
    # the rest of the branch with the applied field as the parameter
    if out and out[-1]["u"] < u_max:
        if verbose:
            print("  switching to the field as the parameter at u %.5f" % out[-1]["u"], flush=True)
        du = 0.002
        lnu = np.log(out[-1]["u"]) + du
        prev = None
        while len(out) < max_points and out[-1]["u"] < u_max:
            guess = X.copy()
            if prev is not None:
                guess = X + (X - prev[1]) * (lnu - np.log(out[-1]["u"])) / (np.log(out[-1]["u"]) - prev[0])
            Xs, ok, its = P.newton(guess, ("lnu", lnu))
            if not ok:
                if du < 1e-6:
                    if verbose:
                        print("  stopped: no convergence at u %.5f" % np.exp(lnu), flush=True)
                    break
                du /= 2
                lnu = np.log(out[-1]["u"]) + du
                continue
            rec = P.describe(Xs)
            rec["h"] = Xs[P.nphi:P.nphi + P.nh].tolist()
            prev = (np.log(out[-1]["u"]), X)
            X = Xs
            out.append(rec)
            if save is not None:
                save(cell, out, X)
            if verbose:
                print("  peak %.4f mm  m %.6f  u %.5f  eps %+.5f  tip R %.4f mm  rim %.3f  H/Hin %.2f  (%d its, by u)"
                      % (rec["peak"] * 1e3, rec["m"], rec["u"], rec["eps"], rec["tip_radius"] * 1e3, rec["rim"] * 1e3, rec["Hmax_over_Hin"], its), flush=True)
            if its <= STEP_GROW:
                du = min(du * 1.3, 0.01)
            elif its > STEP_SHRINK:
                du *= 0.6
            lnu += du
    return cell, out


if __name__ == "__main__":
    import sys
    import json

    name = sys.argv[1]
    nr = int(sys.argv[2]) if len(sys.argv) > 2 else 160
    u_max = float(sys.argv[3]) if len(sys.argv) > 3 else 1.6
    path = sys.argv[4] if len(sys.argv) > 4 else None
    f = Fluid(name)

    def save(cell, recs, X):
        if path:
            with open(path, "w") as fh:
                json.dump(dict(fluid=name, nr=nr, nz=cell.nz, R=cell.R, r=cell.r.tolist(), branch=recs), fh)
            np.save(path + ".X.npy", X)

    cell, recs = trace(f, nr=nr, u_max=u_max, save=save)
