"""The spike table's checks, for tools/verify.sh. CPU, numpy, a minute or two.

    python3 check.py [--fetest PATH]

1. Drift: the generator's sources and the solved records hash to what the
   table was made from, and regenerating the table from the records (kernels,
   basis, the pattern engine's calibration through the harness) writes
   SpikeTable.cpp byte for byte.
2. The datasheets: fluids.py's presets are source/Fluid.cpp's numbers.
3. The table against the solver: solved records at sampled points, put back
   into the hexagonal solver, are still solutions (the residual's size); the
   table's peak at those points is theirs; and the shipped table's lattice, read
   as the GPU reads it, is each solved cell (cut at the same shells) at twelve
   points across the cell, at every solved field of the stable branch.
4. The solver against Gollwitzer et al. (J. Fluid Mech. 571, 2007): APG512a's
   pattern amplitude against their eq. 4.1 (their fit to the measurements,
   which their finite elements match), and the down-sweep's collapse between
   their simulation's and their experiment's.
5. The equal-area circle against the hexagon: the axisymmetric solver's peak
   against the hexagonal one's at the same field, every preset.
"""
import argparse
import hashlib
import json
import os
import re
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
os.environ.setdefault("OPENBLAS_NUM_THREADS", "1")
os.environ.setdefault("VECLIB_MAXIMUM_THREADS", "1")  # numpy and scipy here are on Accelerate

import generate as G  # noqa: E402
from fluids import Fluid, FLUIDS, PRESETS, MU0  # noqa: E402

failures = 0


def check(ok, message):
    global failures
    print("  %s  %s" % ("ok  " if ok else "FAIL", message), flush=True)
    if not ok:
        failures += 1


def load(name, kind):
    with open(os.path.join(G.RECORDS, G.slug(name) + "." + kind + ".json")) as fh:
        return json.load(fh)


def drift(fetest):
    print("\n=== drift: the table is what the generator makes from the records")
    want = open(os.path.join(HERE, "inputs.sha256")).read().strip()
    have = G.inputs_hash()
    check(want == have, "the generator's sources and records hash to %s (recorded %s)" % (have[:16], want[:16]))
    cpp = os.path.join(HERE, "..", "..", "source", "SpikeTable.cpp")
    text = open(cpp).read()
    check(have in text, "SpikeTable.cpp says it was made from the same")
    tables = []
    G.FETEST = fetest
    for name in PRESETS:
        t = G.build_table(name, load(name, "hex"))
        t["calibration"] = G.calibrate(t, PRESETS.index(name))
        tables.append(t)
    tmp = cpp + ".check"
    G.write_cpp(tables, tmp)
    same = open(tmp).read() == text
    os.remove(tmp)
    check(same, "regenerated from the records, SpikeTable.cpp is byte for byte the shipped one")


def datasheets():
    print("\n=== datasheets: fluids.py's presets are Fluid.cpp's")
    text = open(os.path.join(HERE, "..", "..", "source", "Fluid.cpp")).read()
    for name in PRESETS:
        m = re.search(r'\{\s*"%s",\s*([^,]+),\s*([^,]+),\s*([^,]+),\s*([^,]+),\s*([^,\n]+),' % re.escape(name), text)
        vals = [eval(v.replace("kMu0", "1.25663706212e-6")) for v in m.groups()]
        f = FLUIDS[name]
        ok = abs(vals[0] - f["rho"]) < 1e-9 and abs(vals[1] - f["sigma"]) < 1e-12 and abs(vals[3] - f["chi"]) < 1e-12 and abs(vals[4] / f["Ms"] - 1) < 1e-12
        check(ok, "%s: rho %g, sigma %g, chi %g, Ms %.6g" % (name, vals[0], vals[1], vals[3], vals[4]))


def table_vs_solver():
    print("\n=== table against solver: sampled records are still solutions of the hexagonal cell")
    from solver import Surface
    from branch import Tracer

    for name in PRESETS + ["APG512a"]:
        hexrec = load(name, "hex")
        R = [r for r in hexrec["records"] if r["ok"]]
        u = np.array([r["current"] for r in R])
        picks = sorted(set([0, int(np.argmin(u)), len(R) - 1]))
        f = Fluid(name)
        cell = G.make_cell(f)
        S = Surface(cell)
        worst = 0.0
        for i in picks:
            S.phi = None
            F = S.residual(R[i]["A"], np.array(R[i]["x"]))
            worst = max(worst, float(np.max(np.abs(F))))
        check(worst < 1e-9, "%s: %d of %d records, the residual's largest equation %.1e m (Newton stopped below 1e-12; bound 1e-9)" % (name, len(picks), len(R), worst))
        if name in PRESETS:
            t = G.build_table(name, hexrec)
            vgrid = np.linspace(0, t["v_end"], t["n_grid"])
            Af = t["A_fold"]
            up = [r for r in R if r["A"] >= Af]
            err = 0.0
            for r in up:
                v = np.sqrt(max(r["current"] - t["u_fold"], 0))
                err = max(err, abs(np.interp(v, vgrid, t["peak"]) - r["peak"]) / r["peak"])
            check(err < 2e-3, "%s: the table's peak at each solved point of the stable branch is the solver's to %.1e (bound 2e-3: the grid's interpolation)" % (name, err))


def lattice(kc, qmax):
    """Every reciprocal lattice vector of the hexagon with 0 < |G| < qmax k_c, in the
    solver's cell's frame: spikes a apart along x, so the fundamentals at 30, 90 and
    150 degrees (fetest's lattice has them along x: its own frame, its own points)."""
    out = []
    n = int(np.ceil(qmax * 2)) + 1
    for m in range(-n, n + 1):
        for k in range(-n, n + 1):
            gx, gy = 0.5 * np.sqrt(3) * m * kc, -0.5 * m * kc + k * kc
            q = np.hypot(gx, gy) / kc
            if 0 < q < qmax:
                out.append((gx, gy, q))
    return np.array(out)


def kernel_hat_at(t, index, q):
    """The shipped kernel's transform, as the GPU and KernelHat read it: linear in the grid's
    index and in q (Spikes.cpp)."""
    coef = np.array(t["coef"])
    basis = np.array(t["basis"]).T  # n_q x 4
    n_q, q_max = t["n_q"], t["q_max"]
    iv = min(int(index), t["n_grid"] - 2)
    sv = index - iv
    c = (1 - sv) * coef[iv] + sv * coef[iv + 1]
    fq = np.clip(q / q_max * (n_q - 1), 0, n_q - 1)
    iq = np.minimum(fq.astype(int), n_q - 2)
    sq = fq - iq
    b = (1 - sq)[:, None] * basis[iq] + sq[:, None] * basis[iq + 1]
    return np.where(q < q_max, b @ c, 0.0)


def table_against_cells():
    print("\n=== the shipped table's lattice against the solver's cell, at every solved point of the stable branch")
    from solver import Surface

    for name in PRESETS:
        hexrec = load(name, "hex")
        f = Fluid(name)
        kc = f.kc
        t = G.build_table(name, hexrec)
        cell = G.make_cell(f)
        S = Surface(cell)
        a = cell.a
        area = hexrec["cell_area"]
        # the spike, the saddle between two, the three-fold corner, and the way there
        pts = [(0.0, 0.0), (a / 2, 0.0)] + [(s * a / 2, s * a / (2 * np.sqrt(3))) for s in np.linspace(0.1, 1.0, 10)]
        pts = np.array(pts)
        G_ = lattice(kc, t["q_max"])
        phase = np.cos(pts[:, 0:1] * G_[None, :, 0] + pts[:, 1:2] * G_[None, :, 1])
        worst, band = 0.0, 0.0
        uf, Af = G.fold_of(hexrec)
        for r in hexrec["records"]:
            if not r["ok"] or r["A"] < Af:
                continue
            index = min(np.sqrt(max(r["current"] - t["u_fold"], 0)) / t["v_end"] * (t["n_grid"] - 1), t["n_grid"] - 1)
            table = (kernel_hat_at(t, index, G_[:, 2]) * phase).sum(axis=1) / area
            full, kept = np.zeros(len(pts)), np.zeros(len(pts))
            for c, q, members in zip(r["coef"], hexrec["orbit_k"], S.orbit_members):
                wave = np.zeros(len(pts))
                for (i, j) in members:
                    wave += np.cos(2 * np.pi * i * pts[:, 0] / cell.lx + 2 * np.pi * j * pts[:, 1] / cell.ly)
                term = c * wave / len(members)
                full += term
                if q <= G.Q_KERNEL + 1e-9:
                    kept += term
            worst = max(worst, float(np.max(np.abs(table - kept))) / r["peak"])
            band = max(band, (full[0] - kept[0]) / full[0])
        # The bound: the rank-four basis is the floor, about 2e-3 of a spike just past the
        # fold (the grid in v and the linear q cost 6e-4 together: rank eight measured that,
        # at a second transform of the deposit a frame). 3e-3 of the tallest spike is 36 um,
        # a tenth of the camera's texel. The first bound, 2e-3, was set before the table was.
        check(worst < 3e-3, "%s: the table's lattice against the solver's cell cut at the same shells, worst %.1e of the peak over 12 points of the cell "
              "at each of %d solved fields (bound 3e-3: four basis terms, the grid in v, linear in q); the cut itself costs the peak up to %.1f%%"
              % (name, worst, len([r for r in hexrec["records"] if r["ok"] and r["A"] >= Af]), 100 * band))


def gollwitzer():
    print("\n=== Gollwitzer et al. 2007: APG512a's amplitude and its collapse")
    hexrec = load("APG512a", "hex")
    R = sorted([r for r in hexrec["records"] if r["ok"]], key=lambda r: r["A"])
    eps = np.array([r["eps"] for r in R])
    amp = np.array([r["amplitude"] for r in R])
    i = int(np.argmin(eps))
    # their eq. 4.1, the fit to their measurements: A k_c = ( b2 (1 + e) + sqrt( b2^2 (1 + e)^2 + 4 e b1 ) ) / ( 2 b1 )
    b1, b2, kc = 0.0889, 0.0873, 0.629e3
    ref = lambda e: (b2 * (1 + e) + np.sqrt((b2 * (1 + e)) ** 2 + 4 * e * b1)) / (2 * b1) / kc
    worst = 0.0
    rows = []
    for e in [0.05, 0.1, 0.2, 0.3, 0.35, 0.44]:
        a = float(np.interp(e, eps[i:], amp[i:]))
        rows.append("%.2f: %.3f mm (%+.1f%%)" % (e, a * 1e3, 100 * (a / ref(e) - 1)))
        worst = max(worst, abs(a / ref(e) - 1))
    check(worst < 0.025, "the hexagonal cell's amplitude (peak minus corners) against eq. 4.1 at epsilon %s; worst %.1f%% (bound 2.5%%: their simulation "
          "and measurement agree to 1%%, their fit to its data about 1%%, the solver's resolution 0.5%%)" % ("; ".join(rows), 100 * worst))
    # the fold: a parabola through the three lowest-field records, in A
    uf, Af = G.fold_of(hexrec)
    ef = uf * uf - 1
    af = float(np.interp(Af, [r["A"] for r in R], amp))
    # their simulation's bistable range is 0.06 mT below the jump at 17.25 mT, their experiment's 0.17 mT below 16.75 mT
    sim = (17.19 / 17.25) ** 2 - 1
    exp = ((16.747 - 0.17) / 16.747) ** 2 - 1
    check(exp <= ef <= sim, "the down-sweep collapses at epsilon %.4f, from %.2f mm: between their simulation's %.4f and their experiment's %.4f "
          "(both from about 1 mm)" % (ef, af * 1e3, sim, exp))
    a0 = float(np.interp(0.0, eps[i:], amp[i:]))
    check(abs(a0 - 1.5e-3) < 0.25e-3, "the up-sweep jumps at onset to %.2f mm (theirs: about 1.5 mm)" % (a0 * 1e3))


def circle_vs_hexagon():
    print("\n=== the equal-area circle against the hexagon: what the axisymmetric cell would have cost")
    for name in PRESETS + ["APG512a"]:
        hexrec = load(name, "hex")
        axi = load(name, "axi")
        au = np.array([b["u"] for b in axi["branch"]])
        ap = np.array([b["peak"] for b in axi["branch"]])
        ia = int(np.argmin(au))
        uf, _ = G.fold_of(hexrec)
        # the field the circle needs for the hexagon's peak (the branch is monotone in the
        # peak, so this is well posed at the fold too), and on the stable branch clear of
        # the fold (where the peak is a good function of the field) the circle's peak at
        # the hexagon's field
        du, dp = 0.0, 0.0
        for r in hexrec["records"]:
            if not r["ok"] or r["A"] <= 0:
                continue
            du = max(du, abs(np.interp(r["peak"], ap, au) - r["current"]))
            if r["current"] > uf + 0.02 and r["peak"] > ap[ia]:
                dp = max(dp, abs(np.interp(r["current"], au[ia:], ap[ia:]) / r["peak"] - 1))
        check(du < 0.02 and dp < 0.05, "%s: for the hexagon's peak the circle needs a field within %.4f of the hexagon's (in u; bound 0.02); clear of the fold, "
              "at the hexagon's field its peak is within %.1f%% (bound 5%%). Its fold: u %.4f against the hexagon's %.4f" % (name, du, 100 * dp, au[ia], uf))


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--fetest", default=os.path.join(HERE, "..", "..", "build", "fetest"))
    a = ap.parse_args()
    drift(a.fetest)
    datasheets()
    table_vs_solver()
    table_against_cells()
    gollwitzer()
    circle_vs_hexagon()
    print("\n  %s" % ("PASS" if failures == 0 else "FAIL (%d)" % failures))
    sys.exit(1 if failures else 0)
