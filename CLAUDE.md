# ferrofluid

Ferrofluid spikes on the beat, for Resolume Arena/Avenue, as two FFGL plugins
from one core: a pool of magnetic fluid over a coil, solved as a free surface,
rendered as a black mirror. C++/GLSL, CMake MODULE → universal `.bundle`
(macOS) + Windows `.dll`. MIT.

| | id | display name | bundle | type |
| --- | --- | --- | --- | --- |
| the pool, lit by soft boxes | `FE01` | `SW Ferrofluid` | `Ferrofluid.bundle`, `com.stoatworks.ffgl.ferrofluid` | source |
| the pool, reflecting the clip | `FE02` | `SW Ferro Over` | `Ferrofluid Over.bundle`, `com.stoatworks.ffgl.ferrofluid.over` | effect |

Read `AGENTS.md` before touching the nonlinear stress (the W form), the local
field approximation, the substep planner, the pool's size, the FFT, or the solved
spikes (`tools/spikes/`, `source/SpikeTable.cpp`, the spikes/deposit/combine passes).

## Commands (CMake)
- Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release`
- Fast dev build: add `-DCMAKE_OSX_ARCHITECTURES=arm64`
- Build: `cmake --build build -j8`
- Install to Resolume: `cmake --install build` (both bundles into Arena's Extra Effects)
- Render offline: `./build/fetest --out /tmp/frame.png` (the source on its defaults at 120 BPM, 3 s in)
- The Over: `./build/fetest --over --out /tmp/over.png` (the look card as the clip),
  or `--clip clip.rgba` (raw RGBA at `--size`, top row first)
- List parameters: `./build/fetest --list` (`--over` for the effect's)
- Set anything by name: `./build/fetest --set "Magnet=Uniform" --set "Bias=0.7"`
  (a standard control takes the host's 0..1; an option takes its name)
- Press something later: `./build/fetest --at 100 "Fire=1"`; music in the Audio buffer: `--kicks`
- Film: `./build/fetest --film 600 --size 1280x720 --script cues | ffmpeg -f rawvideo -pix_fmt rgba -s 1280x720 -r 60 -i - out.mp4`
- A clip through it: `ffmpeg -i in.mov -vf fps=60 -f rawvideo -pix_fmt rgba - | ./build/fetest --over --pipe --size WxH | ffmpeg …`
- The summary line of `--out` gives the substeps, the current (in Ic and amps), m, max |h| and the steepest slope.

## The solved spikes (`tools/spikes/`, numpy and scipy, double)
- `source/SpikeTable.cpp` is GENERATED: never edit it. `python3 tools/spikes/generate.py`
  solves every fluid's branch (`--stage solve`: the axisymmetric seed then the hexagonal
  cell, hours on this Mac; `--stage hex` reuses the seed in `records/`) and writes the
  table (`--stage table`: minutes, needs `build/fetest` for the calibration).
- `python3 tools/spikes/check.py`: the table against its sources and records (byte for
  byte), the records still solutions, the solver against Gollwitzer et al., the circle
  against the hexagon. `verify.sh` runs it.
- `python3 tools/spikes/convergence.py`: the table's records re-solved at other
  resolutions (an hour or more); writes `records/convergence.json`.
- One thread a worker: the scripts set `VECLIB_MAXIMUM_THREADS=1` (numpy and scipy
  are on Accelerate here) and `OPENBLAS_NUM_THREADS=1`.
- Never edit `generate.py`, `solver.py` or `branch.py` while a solve is running: the
  workers are spawned processes and import the files as they are when they start.

## Verify
- Everything: `tools/verify.sh` (shader hygiene, `glslc`, the submodule pin, a fresh
  universal build with no warnings, both bundles through lipo, plist, ad-hoc signature,
  `oxbow probe` and `selftest`; every GPU check at 1280x720 and 320x180; four of them
  again on Apple's software renderer; `--offline`; `--pipe` for both; the spike table's
  checks; the negative controls; 16 mutants; the sweep; the bench): 11 to 19 minutes on
  this Mac, more when other builds share it. In a sandboxed Claude session set `TMPDIR`
  to a writable scratch directory, or `glslc.sh` hangs on its heredoc.
- **No GL** (`--offline` runs these and their negatives): `--coil` (the L/R lag,
  the AGM, a loop and the solenoid against Biot–Savart), `--beat` (a pulse
  starts on its beat, inside its frame), `--names`, `--cues`.
- **The solver**: `--fft` (the Stockham transform against an O(N²) DFT),
  `--dispersion` (each mode against the closed-form damped oscillator),
  `--onset` (Mahr & Rehberg's H_c; the sweep's threshold against M_c),
  `--wavelength` (k_c from the heights and from the picture), `--hexagons`,
  `--hysteresis` (the solver against the model's own three-mode reduction),
  `--spread` (the pattern's edge over a coil), `--extremes` (the corners stay
  finite and bounded).
- **The light**: `--mirror` (the Over at R = 1 returns the clip to the pixel,
  mirrored), `--fresnel` (each pixel's reflectance over R0).
- **The solved spikes**: `--spikes` (a lattice of the pool's spikes at equilibrium is
  the table's cell, from the camera's height field and from the picture), `--blend`
  (a spike part-grown is that share of the table's), `--coilspikes` (over a coil,
  tallest on the axis; also reports rolls or hexagons), `--calibration` (no GL: the
  pattern engine's fold and onset height are the solver's).
- **The rest**: `--audio`, `--resize`, `--state`.
- The checks can fail: `./build/fetest --negative` (21 wrong models),
  `tools/mutate.sh` (16 mutants).
- No dead controls: `python3 tools/sweep.py` (both plugins; `--bare` drops the
  contexts and lists the controls that need one).
- Render cost: `./build/fetest --bench`; one transform alone: `--fftbench`.
- `FETEST_RENDERER=software` runs any of it on Apple's software renderer (slow:
  verify runs only fft, mirror, fresnel and state there).
- `FETEST_TRACE=1` with `--hysteresis` prints each sweep's amplitudes.
- The derived-stress prototype (not shipped): `tools/research/ferrohydro/`, numpy only;
  `python3 -I checks.py` there reproduces AGENTS.md's numbers.

## Notes
- **The state is spectral.** `state` holds (h_k, v_k) per mode. A substep is the
  exact damped oscillator per mode at G_ref (cos/sin or cosh/sinh: the only trig
  in the GLSL, and `verify.sh` enforces that), then a kick by the real-space
  stress, transformed. Transforms are RGBA: two complex fields at once.
- **The pool is fixed per fluid**: 512×256 cells, 12 to a critical wavelength,
  whatever the host's raster. The camera reads a 1024×512 surface made by
  zero-padding the spectrum, so its heights and slopes are spectrally
  interpolated, not bilinear.
- **Bias and Gain are in units of the critical current** I_c: the current at
  which the pool's strongest point reaches onset. `fetest --out` prints it in amps.
- **Substeps come from a plan** (`planSubsteps`): the largest step whose
  symplectic-Euler map is stable at every wavenumber for the frame's strongest
  stress, capped at 2 ms and 48 substeps. Time beyond 48 is dropped and logged,
  never run unstable.
- All host parameters are 0..1 and mapped in `Controls.cpp`; options hold the
  element index; Fire and Settle act on the rising edge.
- GLSL reserved words (`patch sample input output filter common active half
  layout flat packed`, `noise1`–`noise4`) must not be identifiers; `verify.sh`
  greps for them.
- Override `SetTextParameter` to return `FF_SUCCESS` for the About block, or no
  host can instantiate the plugin.
- `ferrofluid_core` is an OBJECT library: each plugin registers itself from a
  file-scope constructor nothing references.
- The macOS build must be universal. Check with `lipo`, never the build log.
- **Provisional hand copies:** `source/StoatworksAbout*.h` (guide = "", no
  user guide yet) and `ATTRIBUTIONS.md`. Once the project is registered in
  stoatworks-backend, `sync-about.py` and `sync-attributions.py` generate them;
  edit them there then.
- Local only: `~/Projects/resolume/ferrofluid`, no remote, nothing released, not on the website.

## Browser demo
- `demo/` is the page at https://ferrofluid-demo.stoatworks-labs.com (Cloudflare Worker
  `ferrofluid-demo`, a ROUTE on a proxied AAAA 100:: record -- the zone's custom domains
  are full; deleting the record takes the page dark with a green deploy). `deploy.yml`
  redeploys it on a push to main; by hand: `cf-run npx wrangler deploy`.
- `demo/shaders.js` and `demo/spiketable.js` are GENERATED: after changing
  `source/Shaders.cpp` or regenerating `source/SpikeTable.cpp` run
  `python3 demo/tools/check_shaders.py --write`; verify.sh fails on drift, on a new piece
  the check does not list, on a change to how pieces are joined, and on a change to the
  `Table` row's shape.
- `demo/plugin.js` ports the CPU half (PORT block: Controls, Fluid, Coil, Drive, TableFor;
  FRAME block: Ferrofluid.cpp's ProcessOpenGL). Nothing checks it but a reader: change the
  C++ and the port with it, and compare a frame against `fetest` again (AGENTS.md, the
  browser demo, has the recipe and the last numbers).
- The page's clock governor (`hostTimeFor`) is the one thing it does that the plugin
  doesn't; the disclosure on the page and AGENTS.md say so.
- `demo/vendor/` is the shared kit: never edit it, re-vendor from
  `stoatworks-backend/resolume-demo/`.

## Not done yet
- Never loaded into Resolume on either platform. No host has played it any music.
- No GitHub repo, no release, no website page, no user guide, no browser demo,
  no OpenFX port, no video, no presets. The workflows in `.github/` have never run.

## Diagnostics

`source/Diag.{h,cpp}`: log file only, no crash handler (this runs inside Resolume).

    ~/Library/Logs/ferrofluid/ferrofluid.YYYY-MM-DD.log
    %LOCALAPPDATA%\ferrofluid\logs\ferrofluid.YYYY-MM-DD.log

`FERROFLUID_LOG_DIR` overrides the folder.
