"""How far the table's discretisation is from converged: a few solved records
re-solved at other resolutions, at the same fundamental amplitude.

    python3 convergence.py [--workers N]

For each sampled record of records/<fluid>.hex.json: the hexagonal cell at
nx 24 and 48 (the table's is 32), and at nx 32 with the vertical grid's first
step halved and its growth slowed (1.07 -> 1.04). Each starts from the table's
own solution, spectrally interpolated, so Newton only moves it by the
discretisation's difference. Writes records/convergence.json; check.py and
AGENTS.md quote it.
"""
import argparse
import json
import multiprocessing as mp
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
os.environ.setdefault("OPENBLAS_NUM_THREADS", "1")
os.environ.setdefault("VECLIB_MAXIMUM_THREADS", "1")  # numpy and scipy here are on Accelerate

import generate as G  # noqa: E402
from fluids import Fluid, PRESETS  # noqa: E402

VARIANTS = {
    "nx24": dict(nx=24),
    "nx48": dict(nx=48),
    "fine_z": dict(dz0=1 / 800, growth=1.04),
}


def solve(args):
    name, variant, rec, base_orbit_k = args
    from solver import Cell, Surface
    from branch import Tracer

    f = Fluid(name)
    lam = f.lam
    s = dict(G.CELL)
    s.update(VARIANTS[variant])
    cell = Cell(f, nx=s["nx"], dz0=lam * s["dz0"], growth=s["growth"], zc=s["zc"] * lam, ztop=s["ztop"] * lam, zbot=s["zbot"] * lam)
    T = Tracer(f, s["nx"], cell=cell, verbose=False)
    S = T.S
    # the table's solution, as a function on this grid: its orbit coefficients by wavenumber
    coef = dict(zip(np.round(base_orbit_k, 9), rec["coef"]))
    x0 = []
    for k in S.orbit_k[1:]:
        x0.append(coef.get(round(k, 9), 0.0))
    x0 = np.array(x0 + [np.log(rec["current"])])
    sol, ok, its = T.solve(rec["A"], x0, maxit=60)
    out = T.record(rec["A"], sol)
    return dict(fluid=name, variant=variant, ok=bool(ok), its=its, A=rec["A"], u=out["current"], u_table=rec["current"], peak=out["peak"],
                peak_table=rec["peak"], amplitude=out["amplitude"], amplitude_table=rec["amplitude"], tip=out["tip_radius"], tip_table=rec["tip_radius"])


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--workers", type=int, default=6)
    ap.add_argument("--fluids", default="EFH1,APG512a")
    a = ap.parse_args()
    jobs = []
    for name in [n.strip() for n in a.fluids.split(",")]:
        with open(os.path.join(G.RECORDS, G.slug(name) + ".hex.json")) as fh:
            hexrec = json.load(fh)
        R = [r for r in hexrec["records"] if r["ok"]]
        u = np.array([r["current"] for r in R])
        i_fold = int(np.argmin(u))
        picks = sorted(set([i_fold, i_fold + (len(R) - i_fold) // 2, len(R) - 1]))
        for i in picks:
            for v in VARIANTS:
                jobs.append((name, v, R[i], hexrec["orbit_k"]))
    with mp.get_context("spawn").Pool(a.workers) as pool:
        res = pool.map(solve, jobs, chunksize=1)
    for r in res:
        print("%-8s %-7s u %.5f (table %.5f, %+.1e)  peak %.4f mm (%+.2f%%)  amplitude %+.2f%%  tip %.3f mm (table %.3f)" % (
            r["fluid"], r["variant"], r["u"], r["u_table"], r["u"] - r["u_table"], r["peak"] * 1e3, 100 * (r["peak"] / r["peak_table"] - 1),
            100 * (r["amplitude"] / r["amplitude_table"] - 1), r["tip"] * 1e3, r["tip_table"] * 1e3))
    with open(os.path.join(G.RECORDS, "convergence.json"), "w") as fh:
        json.dump(res, fh, indent=1)
