"""Render every parameter at both ends of its range and fail if any made no
difference -- for both plugins.

**This is the only thing in the repo that catches a dead control.** A GLSL
uniform whose name does not match the C++ is silently ignored --
`glGetUniformLocation` returns -1 and `glUniform` on -1 is a documented no-op
-- so a slider can be stone dead while everything compiles, links, loads and
renders.

Each control is swept where it can act. Every render is 180 frames (3 s at
60 fps) at 120 BPM, 320x180 (CI's raster). The drive's controls act on the
coil's current, so most are swept on the defaults (a coil biased just under
onset, kicked on every beat); the fluid's controls need a pattern to act on,
so they are swept with the Bias past onset; Threshold and Band need music
(`--kicks`, the harness's kick every half second over a hi-hat); Fire and
Settle are presses (`--at`). An option parameter reads back 0..1 whatever its
count (the fleet's trap), so options are set here by name. A control whose ends
differ by less than `--floor` (mean 8-bit difference per channel) is barely
alive: vectrix's lesson.

Usage::

    tools/sweep.py [--build BUILD_DIR] [--verbose] [--jobs N] [--bare]

`--bare` drops every per-control context (keeping only the base), the
addendum's honesty check: what goes dead there is the list of controls that
only act with another one set, which AGENTS.md records.
"""

import argparse
import concurrent.futures
import pathlib
import re
import subprocess
import sys
import tempfile
import zlib

REPO = pathlib.Path(__file__).resolve().parent.parent

BASE = []

MUSIC = [["--kicks"]]
PATTERN = ["Bias=0.7"]  # 1.05 Ic: a pattern on the pool to act on

# name -> (low settings, high settings, context settings)
SWEEP = {
    "Fire On": (["Fire On=Off"], ["Fire On=Beat"], []),
    "Fire": ([], [["--at", "40", "Fire=1"]], ["Fire On=Off"]),
    "Band": (["Band=Low"], ["Band=High"], MUSIC + ["Fire On=Off", "Gain=0.6", "Bias=0.5"]),
    "Gain": (["Gain=0"], ["Gain=0.5"], []),
    "Bias": (["Bias=0.5"], ["Bias=0.75"], []),
    "Coil Lag": (["Coil Lag=0"], ["Coil Lag=1"], []),
    "Threshold": (["Threshold=0"], ["Threshold=1"], MUSIC + ["Fire On=Onset", "Gain=0.6"]),
    "Pulse": (["Pulse=0"], ["Pulse=1"], []),
    "Magnet": (["Magnet=Uniform"], ["Magnet=Three Coils"], []),
    # The Over sits on a Helmholtz pair (Uniform), where no coil has a size.
    "Coil Size": (["Coil Size=0.3"], ["Coil Size=0.8"], ["Magnet=Coil"]),
    "Coil Spread": (["Coil Spread=0.2"], ["Coil Spread=0.8"], ["Magnet=Three Coils"]),
    "Fluid": (["Fluid=EFH1"], ["Fluid=EMG 909"], []),
    "Hysteresis": (["Hysteresis=0"], ["Hysteresis=1"], PATTERN),
    "Saturation": (["Saturation=0"], ["Saturation=1"], PATTERN),
    "Viscosity": (["Viscosity=0"], ["Viscosity=1"], PATTERN),
    "Noise": (["Noise=0"], ["Noise=1"], PATTERN),
    "Settle": ([], [["--at", "175", "Settle=1"]], PATTERN),
    "Field Width": (["Field Width=0.3"], ["Field Width=0.7"], PATTERN),
    "Tilt": (["Tilt=0"], ["Tilt=1"], PATTERN),
    "Height": (["Height=0.2"], ["Height=0.8"], PATTERN),
    "Exposure": (["Exposure=0.3"], ["Exposure=0.7"], PATTERN),
    "Lights": (["Lights=Soft Box"], ["Lights=Neon"], PATTERN),
    "Backdrop": (["Backdrop=Black"], ["Backdrop=Studio"], PATTERN),
    "Mix": (["Mix=0"], ["Mix=1"], PATTERN),
}

# The About block: a text line and browser buttons, with no pixel to sweep.
# Audio is the host's FFT buffer: --kicks writes it, and Threshold and Band
# are swept through it.
SKIP = {"About", "Project page", "Source on GitHub", "Support the work", "User guide", "Audio"}


def read_png(path):
    """Enough of PNG for fetest's own writer: 8-bit RGBA, filter 0 rows."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos, width, height, idat = 8, 0, 0, b""
    while pos < len(data):
        length = int.from_bytes(data[pos:pos + 4], "big")
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width = int.from_bytes(body[0:4], "big")
            height = int.from_bytes(body[4:8], "big")
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = width * 4
    out = bytearray()
    for y in range(height):
        start = y * (stride + 1)
        out += raw[start + 1:start + 1 + stride]
    return bytes(out)


def difference(a, b):
    if len(a) != len(b):
        return 255.0
    return sum(abs(x - y) for x, y in zip(a, b)) / len(a)


def render(fetest, out, settings, over):
    args = [str(fetest), "--out", str(out), "--size", "320x180", "--frames", "180"] + (["--over"] if over else [])
    for setting in settings:
        args += list(setting) if isinstance(setting, list) else ["--set", setting]
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"fetest failed: {' '.join(args)}\n{result.stderr.strip()}")
    return read_png(out)


def parameters(fetest, over):
    result = subprocess.run([str(fetest), "--list"] + (["--over"] if over else []), capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"fetest --list failed: {result.stderr.strip()}")
    names = []
    for line in result.stdout.splitlines()[1:]:
        parts = re.split(r"\s{2,}", line.strip())
        if len(parts) >= 3 and parts[0].isdigit():
            names.append(parts[1].strip())
    return names


def sweep_one(fetest, scratch, name, bare, over):
    low, high, context = SWEEP[name]
    context = BASE + ([] if bare else context)
    tag = f"p{abs(hash(name))}{'o' if over else 's'}"
    before = render(fetest, scratch / f"{tag}-a.png", context + low, over)
    after = render(fetest, scratch / f"{tag}-b.png", context + high, over)
    return name, difference(before, after)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=pathlib.Path, default=REPO / "build")
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--bare", action="store_true")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--floor", type=float, default=0.05,
                        help="mean 8-bit difference below which a control is 'barely alive'")
    args = parser.parse_args()

    build = args.build if args.build.is_absolute() else REPO / args.build
    fetest = build / "fetest"
    if not fetest.exists():
        print(f"{fetest} not found", file=sys.stderr)
        return 1

    failed = False
    for over in (False, True):
        plugin = "SW Ferro Over" if over else "SW Ferrofluid"
        declared = parameters(fetest, over)
        unknown = [n for n in declared if n not in SWEEP and n not in SKIP]
        if unknown:
            # A new parameter with no sweep is a hole, not a pass.
            print(f"{plugin}: no sweep defined for: {', '.join(unknown)}", file=sys.stderr)
            return 1
        dead, weak = [], []
        with tempfile.TemporaryDirectory() as scratch:
            scratch = pathlib.Path(scratch)
            with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
                names = [n for n in declared if n in SWEEP]
                results = list(pool.map(lambda n: sweep_one(fetest, scratch, n, args.bare, over), names))
        for name, delta in results:
            if delta == 0.0:
                dead.append(name)
                print(f"  DEAD {plugin}: {name:16s} both ends identical")
            elif delta < args.floor:
                weak.append(name)
                print(f"  WEAK {plugin}: {name:16s} mean delta {delta:.4f}")
            elif args.verbose:
                print(f"  ok   {plugin}: {name:16s} mean delta {delta:.3f}")
        print(f"{plugin}: {len(results)} parameters swept{' (bare: base context only)' if args.bare else ''}, "
              f"{len(dead)} dead, {len(weak)} barely alive")
        failed = failed or bool(dead or weak)
    if failed:
        if not args.bare:
            print("\nA parameter that changes nothing is usually a uniform name that does not match\n"
                  "the C++, or a setting nothing reads. Both are silent everywhere else.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
