"""Generate the spike table the plugin ships: source/SpikeTable.cpp.

    python3 generate.py [--fluids "EFH1,EMG 901,EMG 909,APG512a"] [--workers 8] [--stage all|solve|hex|repair|table]

(solve: stages 1 and 2; hex: stage 2 alone, from the branch already in records/;
repair: stage 2 again for the records whose Newton stopped short; table: stage 3
alone, from the records.)

For each fluid:
  1. its axisymmetric branch (axisym.py), the flat surface's bifurcation to
     the end of the solvable branch or u = 1.62 (the drive's ceiling is 1.6
     critical currents): fast, and the seed for 2;
  2. the hexagonal cell (solver.py, Gollwitzer et al.'s cell) at points along
     that branch, each seeded from the axisymmetric spike placed at the
     cell's two spikes, by Newton at the seed's fundamental amplitude; the
     records (every coefficient and the applied field) are kept in
     records/<fluid>.hex.json, so a check can put any of them back into the
     solver and see it is still a solution;
  3. the table: the stable branch resampled on a grid uniform in
     v = sqrt( u - u_fold ), each sample's isotropic kernel (kernel_hat: the
     cell's Fourier amplitudes at the lattice's shells), and a rank-four basis
     of those kernels (SVD) that the GPU convolves with.

APG512a is solved for the record and the Gollwitzer check only; it is not a
preset and not in the table.
"""
import argparse
import hashlib
import json
import multiprocessing as mp
import os
import sys
import time

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
os.environ.setdefault("OPENBLAS_NUM_THREADS", "1")
os.environ.setdefault("VECLIB_MAXIMUM_THREADS", "1")  # numpy and scipy here are on Accelerate
os.environ.setdefault("OMP_NUM_THREADS", "1")

from fluids import Fluid, FLUIDS, PRESETS, MU0  # noqa: E402

# The discretisations the table is made with (AGENTS.md has their convergence).
CELL = dict(nx=32, dz0=1 / 400, growth=1.07, zc=2.6, ztop=6.0, zbot=4.0)  # lengths in wavelengths
AXI = dict(nr=200, alpha=3.0, dz0=1 / 400, growth=1.07)
U_MAX = 1.62
U_MAX_OF = {"APG512a": 1.25}  # validation only: Gollwitzer et al. measured to epsilon 0.44, u 1.2
N_UNSTABLE = 5
N_STABLE = 18
N_GRID = 64  # the table's grid in v = sqrt( u - u_fold )
# The kernel carries the cell's shells up to Q_KERNEL k_c (the last is sqrt 67 = 8.185)
# and tapers to nothing exactly at the next shell (sqrt 73 = 8.544), so no shell of the
# lattice sees the taper: a lattice of kernels is the cell, band-limited, exactly.
Q_KERNEL = 8.2
N_Q = 512
N_BASIS = 4
EQ_RANGE = (0.6, 1.62)  # the pattern engine's equilibrium is tabulated over this u
N_EQ = 256
FETEST = os.path.join(HERE, "..", "..", "build", "fetest")
SOURCES = ["fluids.py", "solver.py", "branch.py", "axisym.py", "generate.py"]

RECORDS = os.path.join(HERE, "records")


def slug(name):
    return name.replace(" ", "")


def make_cell(fluid):
    from solver import Cell

    lam = fluid.lam
    return Cell(fluid, nx=CELL["nx"], dz0=lam * CELL["dz0"], growth=CELL["growth"], zc=CELL["zc"] * lam, ztop=CELL["ztop"] * lam,
                zbot=CELL["zbot"] * lam)


def axi_branch(name):
    import axisym

    f = Fluid(name)
    path = os.path.join(RECORDS, slug(name) + ".axi.json")
    cell, recs = axisym.trace(f, nr=AXI["nr"], alpha=AXI["alpha"], dz0=f.lam * AXI["dz0"], growth=AXI["growth"], u_max=U_MAX_OF.get(name, U_MAX),
                              verbose=True)
    out = dict(fluid=name, settings=AXI, R=cell.R, r=cell.r.tolist(), branch=recs)
    with open(path, "w") as fh:
        json.dump(out, fh)
    return out


def pick(axi):
    """Points along the axisymmetric branch to solve in the hexagonal cell: a few on
    the unstable branch, the fold bracketed, then the stable branch to its end."""
    b = axi["branch"]
    u = np.array([x["u"] for x in b])
    i_fold = int(np.argmin(u))
    unstable = np.unique(np.linspace(0, max(i_fold - 1, 0), N_UNSTABLE).round().astype(int))
    fold = [max(i_fold - 1, 0), i_fold, min(i_fold + 1, len(b) - 1)]
    # stable: uniform in v = sqrt( u - u_fold ) from the fold to the end
    us = u[i_fold:]
    v = np.sqrt(np.maximum(us - u[i_fold], 0))
    targets = np.linspace(0, v[-1], N_STABLE + 1)[1:]
    stable = [i_fold + int(np.argmin(np.abs(v - t))) for t in targets]
    idx = sorted(set(list(unstable) + fold + stable))
    return idx


def solve_hex(args):
    name, rec, r = args
    from solver import Surface
    from branch import Tracer

    f = Fluid(name)
    cell = make_cell(f)
    T = Tracer(f, CELL["nx"], cell=cell, verbose=False)
    S = T.S
    a = cell.a
    best = np.full(cell.X.shape, 1e9)
    for sx, sy in [(0, 0), (a / 2, cell.ly / 2)]:
        for ix in (-1, 0, 1):
            for iy in (-1, 0, 1):
                best = np.minimum(best, np.hypot(cell.X - sx - ix * cell.lx, cell.Y - sy - iy * cell.ly))
    h0 = np.interp(best, np.array(r), np.array(rec["h"]))
    h0 -= h0.mean()
    coef = np.tensordot(S.basis, h0, axes=([1, 2], [0, 1])) / S.norm2
    A = float(coef[0])
    x = np.concatenate([coef[1:], [np.log(rec["u"])]])
    t0 = time.time()
    sol, ok, its = T.solve(A, x, maxit=60)
    out = T.record(A, sol)
    out.update(ok=bool(ok), its=int(its), seconds=time.time() - t0, x=sol.tolist(), axi_u=rec["u"], axi_peak=rec["peak"],
               axi_tip_radius=rec["tip_radius"])
    return out


def hex_records(name, axi, workers):
    idx = pick(axi)
    jobs = [(name, axi["branch"][i], axi["r"]) for i in idx]
    t0 = time.time()
    res = []
    with mp.get_context("spawn").Pool(workers) as pool:
        for r in pool.imap_unordered(solve_hex, jobs, chunksize=1):
            res.append(r)
            print("   %2d/%d  A %.5f  u %.5f  peak %.4f mm  amplitude %.4f mm  tip R %.4f mm  %s in %d its, %.0f s" % (
                len(res), len(jobs), r["A"], r["current"], r["peak"] * 1e3, r["amplitude"] * 1e3, r["tip_radius"] * 1e3,
                "solved" if r["ok"] else "FAILED", r["its"], r["seconds"]), flush=True)
    f = Fluid(name)
    cell = make_cell(f)
    from solver import Surface

    S = Surface(cell)
    # each orbit's member count: its basis function is the members' cosines over their count
    counts = [len(m) for m in S.orbit_members]
    out = dict(fluid=name, settings=CELL, orbit_k=S.orbit_k, orbit_count=counts, kc=f.kc, lam=f.lam, Ha_c=S.Hac,
               cell_area=cell.lx * cell.ly / 2, records=sorted(res, key=lambda d: d["A"]), seconds=time.time() - t0)
    with open(os.path.join(RECORDS, slug(name) + ".hex.json"), "w") as fh:
        json.dump(out, fh)
    return out


def repair(name, workers):
    """Re-solve the records whose Newton stopped short, from the same seeds (the hexagonal
    Jacobian was one-sided until 2026-10-15 and stalled near some folds; a record that
    converged is the same solution whichever Jacobian found it)."""
    path = os.path.join(RECORDS, slug(name) + ".hex.json")
    with open(path) as fh:
        hexrec = json.load(fh)
    with open(os.path.join(RECORDS, slug(name) + ".axi.json")) as fh:
        axi = json.load(fh)
    seeds = {round(axi["branch"][i]["peak"], 12): axi["branch"][i] for i in pick(axi)}
    bad = [k for k, r in enumerate(hexrec["records"]) if not r["ok"]]
    if not bad:
        return hexrec
    jobs = [(name, seeds[round(hexrec["records"][k]["axi_peak"], 12)], axi["r"]) for k in bad]
    with mp.get_context("spawn").Pool(min(workers, len(jobs))) as pool:
        fixed = pool.map(solve_hex, jobs, chunksize=1)
    for k, r in zip(bad, fixed):
        print("   repaired A %.5f: %s in %d its" % (r["A"], "solved" if r["ok"] else "FAILED again", r["its"]), flush=True)
        hexrec["records"][k] = r
    hexrec["records"] = sorted(hexrec["records"], key=lambda d: d["A"])
    with open(path, "w") as fh:
        json.dump(hexrec, fh)
    return hexrec


def fold_of(hexrec):
    """u and A at the fold, from a parabola through the three lowest-u records."""
    R = hexrec["records"]
    u = np.array([r["current"] for r in R])
    A = np.array([r["A"] for r in R])
    i = int(np.argmin(u))
    if 0 < i < len(R) - 1:
        p = np.polyfit(A[i - 1:i + 2], u[i - 1:i + 2], 2)
        Af = -p[1] / (2 * p[0])
        uf = np.polyval(p, Af)
        return float(uf), float(Af)
    return float(u[i]), float(A[i])


def shell_values(hexrec, rec):
    """s^( q ) at the shells (m^3): A_cell c_o / n_o, orbits of one length averaged."""
    q = np.array(hexrec["orbit_k"])
    n = np.array(hexrec["orbit_count"], float)
    c = np.array(rec["coef"])
    vals = hexrec["cell_area"] * c / n
    out = {}
    for qi, vi in zip(np.round(q, 9), vals):
        out.setdefault(qi, []).append(vi)
    qs = np.array(sorted(out))
    vs = np.array([np.mean(out[k]) for k in qs])
    spread = max((np.ptp(out[k]) for k in qs if len(out[k]) > 1), default=0.0)
    return qs, vs, spread


def next_shell(hexrec):
    """The first shell of the reciprocal lattice past Q_KERNEL, k_c units."""
    return float(min(q for q in hexrec["orbit_k"] if q > Q_KERNEL + 1e-9))


def kernel_hat(qs, vs, qgrid, q_next):
    """The kernel's transform on qgrid: the cell's shell values up to Q_KERNEL, 0 with zero
    slope at q = 0, a cubic spline between, and a cosine taper from the last shell kept to
    zero at q_next, the first shell not kept."""
    from scipy.interpolate import CubicSpline

    keep = qs <= Q_KERNEL + 1e-9
    qk = np.concatenate([[0.0], qs[keep]])
    vk = np.concatenate([[0.0], vs[keep]])
    spl = CubicSpline(qk, vk, bc_type=((1, 0.0), "natural"))
    last = qk[-1]
    out = np.where(qgrid <= last, spl(np.minimum(qgrid, last)), 0.0)
    t = np.clip((qgrid - last) / (q_next - last), 0, 1)
    out = out + np.where((qgrid > last) & (qgrid < q_next), vk[-1] * 0.5 * (1 + np.cos(np.pi * t)), 0.0)
    return out


def build_table(name, hexrec):
    from scipy.interpolate import PchipInterpolator

    R = [r for r in hexrec["records"] if r["ok"]]
    u = np.array([r["current"] for r in R])
    A = np.array([r["A"] for r in R])
    uf, Af = fold_of(hexrec)
    upper = [r for r in R if r["A"] >= Af]
    Au = np.array([r["A"] for r in upper])
    uu = np.array([r["current"] for r in upper])
    order = np.argsort(Au)
    Au, uu = Au[order], uu[order]
    upper = [upper[i] for i in order]
    # v = sqrt( u - u_fold ) is monotone and smooth in A along the stable branch
    vv = np.sqrt(np.maximum(uu - uf, 0))
    vv[0] = max(vv[0], 0.0)
    v_end = vv[-1]
    vgrid = np.linspace(0, v_end, N_GRID)
    A_of_v = PchipInterpolator(np.concatenate([[0.0], vv]) if vv[0] > 0 else vv, np.concatenate([[Af], Au]) if vv[0] > 0 else Au)
    q_next = next_shell(hexrec)
    qgrid = np.linspace(0, q_next, N_Q)
    # The kernels and the summaries are smooth in A through the fold (only u( A ) turns
    # there), so they're interpolated across it from the last record below it: the grid's
    # first point is the fold itself, not the first record above it
    below = [r for r in R if r["A"] < Af]
    span = ([max(below, key=lambda r: r["A"])] if below else []) + upper
    As = np.array([r["A"] for r in span])
    K = []
    for r in span:
        qs, vs, spread = shell_values(hexrec, r)
        K.append(kernel_hat(qs, vs, qgrid, q_next))
    K = np.array(K)
    # interpolate kernels (and summaries) in A onto the grid
    Agrid = A_of_v(vgrid)

    def interp_rows(M):
        from scipy.interpolate import PchipInterpolator as P

        return P(As, M, axis=0)(np.clip(Agrid, As[0], As[-1]))

    Kg = interp_rows(K)
    peak = interp_rows(np.array([r["peak"] for r in span]))
    amp = interp_rows(np.array([r["amplitude"] for r in span]))
    tip = interp_rows(np.array([r["tip_radius"] for r in span]))
    # Rank-N basis, weighted as the picture sees it: each kernel over its own largest
    # value (the fold's spike is a seventh of the top's, and a plain SVD left it eight
    # times the error), and each q by the lattice vectors a shell holds, which grow as q
    # (a lattice's sum is what the camera reads; the 0.5 keeps q = 0 in)
    wr = np.abs(Kg).max(axis=1, keepdims=True)
    wc = np.maximum(qgrid, 0.5)
    U, s, Vt = np.linalg.svd(Kg / wr * wc, full_matrices=False)
    coef = wr * U[:, :N_BASIS] * s[:N_BASIS]
    basis = Vt[:N_BASIS] / wc
    err = np.max(np.abs(coef @ basis - Kg) / wr)
    return dict(fluid=name, u_fold=uf, A_fold=Af, v_end=float(v_end), u_end=float(uf + v_end ** 2), n_grid=N_GRID, q_max=float(qgrid[-1]),
                n_q=N_Q, coef=coef.tolist(), basis=basis.tolist(), peak=peak.tolist(), amplitude=amp.tolist(), tip=tip.tolist(),
                svd_error=float(err), singular=s[:8].tolist(), branch_u=u.tolist(), branch_peak=[r["peak"] for r in R],
                branch_amplitude=[r["amplitude"] for r in R], m_fold=float(Fluid(name).base(uf * hexrec["Ha_c"])["m"]),
                peak_at_onset=float(np.interp(1.0, uu, [r["peak"] for r in upper])))


def calibrate(t, fluid_index):
    """The pattern engine's constants from the harness (the C++ three-mode reduction)."""
    import subprocess

    f = Fluid(t["fluid"])
    # float() first: numpy 2's repr of a float64 is "np.float64(...)", which atof reads as 0
    peak_kc = float(t["peak_at_onset"] * f.kc)
    out = subprocess.run([FETEST, "--calibrate", str(fluid_index), repr(float(t["m_fold"])), repr(peak_kc), repr(EQ_RANGE[0]), repr(EQ_RANGE[1]), str(N_EQ)],
                         capture_output=True, text=True, check=True)
    return json.loads(out.stdout.strip().splitlines()[-1])


def inputs_hash():
    """The generator's sources and the solved records the table is built from."""
    h = hashlib.sha256()
    for name in SOURCES:
        with open(os.path.join(HERE, name), "rb") as fh:
            h.update(name.encode() + b"\0" + fh.read() + b"\0")
    for name in PRESETS:
        path = os.path.join(RECORDS, slug(name) + ".hex.json")
        with open(path, "rb") as fh:
            h.update(os.path.basename(path).encode() + b"\0" + fh.read() + b"\0")
    return h.hexdigest()


def write_cpp(tables, path):
    lines = []
    lines.append("// GENERATED by tools/spikes/generate.py -- do not edit. Inputs: %s" % inputs_hash())
    lines.append('#include "SpikeTable.h"')
    lines.append("namespace ferrofluid::spikes")
    lines.append("{")
    lines.append('const char* const kInputsHash = "%s";' % inputs_hash())
    lines.append("namespace")
    lines.append("{")

    def arr(name, values):
        vals = ", ".join("%.9ef" % v for v in values)
        lines.append("const float %s[] = { %s };" % (name, vals))

    for t in tables:
        s = slug(t["fluid"]).replace("-", "_")
        arr("kEq%s" % s, t["calibration"]["eq"])
        arr("kCoef%s" % s, np.array(t["coef"]).ravel())
        arr("kBasis%s" % s, np.array(t["basis"]).T.ravel())  # q-major: n_q x 4
        arr("kPeak%s" % s, t["peak"])
        arr("kAmplitude%s" % s, t["amplitude"])
        arr("kTip%s" % s, t["tip"])
        arr("kBranchU%s" % s, t["branch_u"])
        arr("kBranchPeak%s" % s, t["branch_peak"])
    lines.append("} // namespace")
    lines.append("const Table kTables[] = {")
    for t in tables:
        s = slug(t["fluid"]).replace("-", "_")
        cal = t["calibration"]
        lines.append(
            '\t{ "%s", %.12g, %.12g, %.12g, %.12g, %.12g, %d, kCoef%s, kPeak%s, kAmplitude%s, kTip%s, %d, %.12g, kBasis%s, %d, kBranchU%s, kBranchPeak%s, %.12g, %.12g, %d, %.12g, %.12g, kEq%s },'
            % (t["fluid"], t["u_fold"], t["m_fold"], t["peak_at_onset"], t["v_end"], t["u_end"], t["n_grid"], s, s, s, s, t["n_q"], t["q_max"], s,
               len(t["branch_u"]), s, s, cal["hysteresis"], cal["saturation"], len(cal["eq"]), EQ_RANGE[0], EQ_RANGE[1], s))
    lines.append("};")
    lines.append("} // namespace ferrofluid::spikes")
    with open(path, "w") as fh:
        fh.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--fluids", default=",".join(PRESETS + ["APG512a"]))
    ap.add_argument("--workers", type=int, default=8)
    ap.add_argument("--stage", default="all")
    ap.add_argument("--no-calibrate", action="store_true")
    a = ap.parse_args()
    os.makedirs(RECORDS, exist_ok=True)
    names = [n.strip() for n in a.fluids.split(",")]
    if a.stage == "repair":
        for name in names:
            print("== %s: re-solving the records that stopped short" % name, flush=True)
            repair(name, a.workers)
    if a.stage in ("all", "solve", "hex"):
        for name in names:
            if a.stage == "hex":
                # the hexagonal cell only, seeded from the axisymmetric branch already in records/
                with open(os.path.join(RECORDS, slug(name) + ".axi.json")) as fh:
                    axi = json.load(fh)
            else:
                print("== %s: the axisymmetric branch" % name, flush=True)
                axi = axi_branch(name)
            print("== %s: the hexagonal cell at %d points" % (name, len(pick(axi))), flush=True)
            hexrec = hex_records(name, axi, a.workers)
            bad = [r for r in hexrec["records"] if not r["ok"]]
            print("   %d solved, %d failed, %.0f s" % (len(hexrec["records"]) - len(bad), len(bad), hexrec["seconds"]), flush=True)
    if a.stage in ("all", "table"):
        tables = []
        for name in PRESETS:
            with open(os.path.join(RECORDS, slug(name) + ".hex.json")) as fh:
                hexrec = json.load(fh)
            t = build_table(name, hexrec)
            t["calibration"] = calibrate(t, PRESETS.index(name)) if os.path.exists(FETEST) and not a.no_calibrate else dict(
                hysteresis=1.0, saturation=0.5, eq=[0.0] * N_EQ)
            print("%s: fold u %.5f (m %.5f), stable branch to u %.4f, rank-4 basis error %.1e of each kernel's largest value" % (name, t["u_fold"], t["m_fold"], t["u_end"], t["svd_error"]),
                  flush=True)
            tables.append(t)
        write_cpp(tables, os.path.join(HERE, "..", "..", "source", "SpikeTable.cpp"))
        with open(os.path.join(HERE, "inputs.sha256"), "w") as fh:
            fh.write(inputs_hash() + "\n")
