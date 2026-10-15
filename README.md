# ferrofluid

> **AI-assisted project.** This codebase was created with [Claude](https://claude.com/claude-code)
> (Anthropic), directed and reviewed by a human author. It has **never been loaded into
> Resolume**, on either platform. What's measured is measured by an offline harness that
> drives the real plugin class in a headless GL context on a synthetic clock, on this Mac's
> GPU at two rasters, and reads the pool back out of the plugin's own state and pixels. Every
> mode of the surface rings or grows at the Cowley–Rosensweig rate to **1.3e-5** of its
> height. The flat pool goes unstable between **12.003 and 12.033 A** against the theory's
> **12.018 A**, and the harness reproduces a published critical field (Mahr & Rehberg's
> **5.9 kA/m**). The settled lattice sits at the critical wavelength, from the heights and
> from the picture, with three peaks **59.3°, 59.3° and 61.4°** apart. The spikes jump up at
> onset and hang on below it until the model's own saddle-node, to within a sweep step.
> **17** deliberately wrong models and **14** one-character mutants of the shipped code are
> all caught. The nonlinear stress is a **chosen** model, not derived from the literature:
> see [Status](#status).

Ferrofluid spikes on the beat, for [Resolume](https://resolume.com) Arena and Avenue, as
two FFGL plugins: **SW Ferrofluid**, a source that is a dish of ferrofluid in a
photographer's studio, and **SW Ferro Over**, an effect that is the same pool with your
clip hung above it, broken up in its mirror.

![A field of glossy black spikes on a hexagonal lattice, seen from 45 degrees: each spike a
cone with a hard white highlight from two soft boxes on its near side, its base fringed
with dark reflections of its neighbours, on a grey ground](docs/hero.png)

<sub>One frame of the source, rendered by `fetest`, the offline harness, not captured
from Resolume. EFH1 in the Helmholtz pair at 1.05 I_c for seven seconds, a 10 cm field
seen from 45° under Two Boxes (`--set "Fire On=Off" --set "Magnet=Uniform" --set
"Bias=0.7" --set "Field Width=0.55" --set "Tilt=0.75" --set "Lights=Two Boxes" --frames
420`).</sub>

![Resolume's IntoTheGlow_02 clip seen in a spiked pool from straight above: a lattice of
dark round spikes, each with a pinpoint of the clip at its tip, between them a web of
bright blue reflections where the troughs catch the screen](docs/over.png)

<sub>The Over on the same harness, a frame of Resolume's bundled `IntoTheGlow_02` as the
clip, at 1.05 I_c with no beats, seven seconds in.</sub>

## The one idea

**A pool of ferrofluid over an electromagnet whose current is the music, seen as the
black mirror it is.**

Ferrofluid is oil full of magnetite particles about ten nanometres across. Put it in a
magnetic field and it magnetises. Push the field up past a critical value and the flat
surface can't hold: it stands up in spikes, all at once, on a hexagonal lattice. That's the
Rosensweig (or normal-field) instability, worked out by Cowley and Rosensweig in 1967. The
spacing of the spikes is set by gravity against surface tension, about a centimetre for an
oil-based fluid, and the field doesn't change it.

So the plugins are the electromagnet, the fluid and the camera:

- **the drive** is a DC Bias plus the music (a band of Resolume's audio input) times Gain,
  plus a pulse on every beat, onset or press of Fire. The coil's own inductance lags it;
- **the magnet** is a Helmholtz pair (a uniform field), one solenoid under the dish, or
  three, each following its own band;
- **the fluid** is a real one off a datasheet: Ferrotec's EFH1, or EMG 901 or 909. It
  magnetises along a Langevin curve, and the surface obeys the linear theory exactly, mode
  by mode, plus a nonlinear stress that makes the hexagons and stops the spikes growing for
  ever;
- **the camera** is a 100 mm lens looking down at the pool. The fluid is a black mirror,
  about 4.5% reflective, so what you see is what it reflects: the studio's soft boxes for
  the source, the clip on a screen above the pool for the Over, and each spike's neighbours.

## What falls out

None of this is drawn. It's the fluid doing what it does.

- **Spikes on the kick.** Below the critical current the pool is flat, whatever the music
  does. Each beat pushes the current past it and the spikes come up, at the speed the
  linear theory gives that much drive.
- **They hang on.** The onset is subcritical: once up, the spikes survive until the current
  falls about 7% below where they appeared (at the default Hysteresis and Saturation). The
  default Bias sits inside that loop, so a kick raises them and the quiet between kicks
  doesn't knock them down.
- **The spacing doesn't change with loudness.** Louder pushes the spikes taller, not closer
  together: the lattice sits at λ_c, about a centimetre, at 2% and at 10% past the
  critical current alike.
- **The pattern grows outward from the coil.** A solenoid's field is strongest over its
  middle, so spikes appear there first, and the patch spreads as the current rises, to
  where the local magnetisation crosses the critical value.
- **The coil drags.** Coil Lag is the coil's L/R: at 300 ms a beat barely gets the current
  up before it falls again.
- **The clip, broken up.** In the Over every spike is a small curved mirror, so the clip
  turns into a lattice of tiny reflections, and the troughs between spikes catch it in a
  web of bright lines.
- **Spikes reflecting spikes.** Each spike's base shows dark reflections of its neighbours,
  which is most of what makes photographs of ferrofluid look the way they do.

## Controls

| Group | |
| --- | --- |
| **Drive** | Fire On (Off, Beat, Half Beat, 2 Beats, Bar, Onset), Fire (a pulse now), Audio (Resolume's audio input), Band (Low, Mid, High, Full), Gain and Bias (0–1.5 critical currents), Coil Lag (L/R, 1–300 ms), Threshold (how hard an onset must hit), Pulse (10 ms–1 s). |
| **Magnet** | Magnet (Uniform, Coil, Three Coils), Coil Size (the solenoid's radius, 5 mm–8 cm), Coil Spread (Three Coils' triangle, 2–30 cm a side). |
| **Fluid** | Fluid (EFH1, EMG 901, EMG 909), Hysteresis (0–2), Saturation (0.35–8), Viscosity (0.25–100× the datasheet's), Noise (the floor the spikes grow from: 0, then 1 µPa–0.1 Pa), Settle (flatten the pool). |
| **View** | Field Width (2–40 cm), Tilt (0–60°), Height (the Over's screen above the pool, 2 cm–1 m), Exposure (±4 stops), Lights (source: Soft Box, Two Boxes, Strips, Backlight, Window, Neon), Backdrop (source: Black, Studio), Mix (Over). |

**Bias and Gain are in critical currents**, not amps: 1 is where the pool goes unstable,
whatever the fluid and the magnet. The amps behind it are in Status.

The source's defaults are EFH1 over one coil of 4 cm radius, biased at 0.98 of the
critical current and kicked by 0.2 on every beat for 150 ms, through a 40 ms coil, seen
from 35° across a 10 cm field under three strip lights. The Over's are the same pool in
the Helmholtz pair, seen from straight above, with the clip on a screen 4 cm up.

## Status

**v0.1.0, unreleased, 15 October 2026: the first build, local only, early days.**

### Measured offline, on macOS

`tools/verify.sh` passes on this machine (Apple Silicon, macOS 26) against a fresh
universal Release build. It runs every picture check at **two rasters**, 1280×720 and
320×180, and the transform, mirror, Fresnel and GL-state checks again on **Apple's
software renderer** at 320×180. What it establishes:

| check | result |
| --- | --- |
| `--fft` | the shipped radix-16/8/4/2 Stockham transform against an O(N²) DFT in double, both ways, four grid sizes up to the pool's 512×256: relative RMS **2.0e-7** (bound 1e-5) |
| `--dispersion` | six wavenumbers from 0.5 to 4 k_c at 0, 0.7, 0.97 and 1.1 I_c, three of them growing, against the closed-form damped oscillator with the Cowley–Rosensweig ω(k) from the harness's own Langevin curve: worst **1.3e-5** of the largest height (bound 2e-4) |
| `--onset` | Mahr & Rehberg's mixture: H_c **5886 A/m** against their 5.9e3, λ_c 9.14 mm against their 9; EFH1 in the Helmholtz pair, swept in 0.25% steps: still at **12.003 A**, grows at **12.033 A**, theory **12.018 A** |
| `--wavelength` | the settled lattice's dominant mode at **620 /m** from the heights and **663 /m** from the picture, against k_c = 639.7 /m, each within one bin (79 and 95 /m) |
| `--hexagons` | three peaks of equal strength (1, 1, 0.98) **59.3°, 59.3° and 61.4°** apart (tolerance 4°, the box's angular resolution) |
| `--hysteresis` | two members of the model's invariant family, amplitudes a factor of two apart: each jumps at m 1.001, holds down to m **0.9950** and loses its spikes there; the model's three-mode reduction says **0.9951**, within one 0.0005 step |
| `--coil` | the L/R lag against 1 − e^(−t/τ) to 1.1e-16; the AGM's K(½) and E(½) against the tables; one loop's field against Biot–Savart to 2.8e-13; the solenoid's table against 400 turns summed one by one to 2.5e-5 |
| `--spread` | over the default coil at 1.2 and 1.4 I_c, the outermost spike stands at **17.0** and **20.2 mm** from the axis, where the local magnetisation crosses M_c at **18.1** and **22.5 mm** (tolerance λ_c, 9.8 mm) |
| `--mirror` | the Over straight down at a flat pool with the reflectance forced to 1 returns a noise card with **0 of 3 686 400** channels different in 8 bits; at Mix 0, **0** floats differ from the clip; both rasters |
| `--fresnel` | a flat pool under a uniform sky at Tilt 0 and 45° (incidence 0.6° to 50.3°): each pixel's R/R0 against the exact unpolarised formula for n + ik to **5.5e-7** (bound 2e-4); EFH1's R0 = 0.0450 |
| `--beat` | Beat, Half Beat, 2 Beats and Bar at 120 and 97 BPM: every pulse starts within **1.8e-15 s** of its beat |
| `--audio` | six kicks, six pulses, each on its kick's own frame, none from the cold start; kicks in the low bins swing the coil **0.57 I_c** on Band Low and **0.000** on Band High |
| `--resize`, `--state` | a pool that changes raster (and shape) after 70 frames is **0 bytes** different from a fresh one at the new raster; the host's GL state comes back, both plugins |
| `--extremes` | the drive at its 1.6 I_c ceiling, the model at both ends, each magnet, each fluid: finite after 4 s, the tallest spike under 1.5(1 + Hy/Sa)/k_c (9.8 mm against 15.7 at the worst corner) |
| `--pipe` | both plugins: 2.5 frames in, exactly 2 out; a closed stdout (`\| head -c 1`) exits 1, not 141 |
| `--negative` | **17** deliberately wrong models, **17** caught |
| mutation | **14** one-character mutants of the shipped GLSL and C++, **14** caught |
| `tools/sweep.py` | all **22** controls of each plugin that have pixels measurably change the picture |
| shaders | all 7 compile through `glslc`, not merely through Apple's driver |
| the bundles | both universal (`x86_64 arm64`), export `plugMain`, ad-hoc sign; `oxbow` reports `SW Ferrofluid` / `FE01` / `source` and `SW Ferro Over` / `FE02` / `effect` and renders through both |

The critical current behind Bias = 1, at the default coil size and spread:

| fluid | Uniform (Helmholtz pair) | Coil | Three Coils (each) | λ_c | applied field at onset |
| --- | --- | --- | --- | --- | --- |
| EFH1 | 12.02 A | 3.52 A | 3.94 A | 9.82 mm | 8.60 kA/m (10.8 mT) |
| EMG 901 | 17.27 A | 5.05 A | 5.66 A | 8.81 mm | 12.36 kA/m (15.5 mT) |
| EMG 909 | 21.60 A | 6.32 A | 7.07 A | 10.23 mm | 15.46 kA/m (19.4 mT) |

Render cost, GPU time by `GL_TIME_ELAPSED`, median of 120 frames after a warm-up. The
quietest of three runs within the hour, at the defaults (12 substeps a frame): the source
**4.01 ms** at 720p, **4.42 ms** at 1080p and **6.15 ms** at 4K; the Over **4.03**,
**4.60** and **7.99 ms**. Three Coils seen from 60°: **4.22**, **4.60** and **7.71 ms**.
The GPU was shared, with Resolume Arena running on it for another job, and the other two
runs came out up to three and a half times slower (verify.sh's own: the source 4.28, 4.78
and 8.30 ms; Three Coils at 4K 27 ms). Treat the quiet run as the cost and the others as
what sharing does. Most of it is the pool, which costs the same at any
raster: about 150 passes a frame. The substeps follow the drive: at the ceiling with
Hysteresis 2 and Saturation 0.35 the plan wants more than its cap of 48, and the pool then
runs at 70% of real time (the dropped time is logged). The cost there wasn't benched.
macOS figures only.

### Not established

It's **never been loaded into Resolume**, on either platform. Everything above was
compiled, rendered and measured offline against the plugin's own class in a headless CGL
context, plus an `oxbow` load. I haven't seen how the controls read in Arena's inspector,
how Fire On follows Arena's transport, or what Resolume's own FFT does to Band and
Threshold. Nothing has been built or run on Windows, and the workflows in `.github/` have
never run.

**The nonlinear stress is a model I chose, not one I derived.** The spec preferred
coefficients from Gailitis's (1977) or Friedrichs and Engel's (2001) energy expansion, and
I didn't do that honestly in the time. So the stress is the gradient of a local energy
with two constants in the open as Hysteresis and Saturation, and its defaults were chosen
so the lattice stays at λ_c and the spikes look like the photographs. The wavelength, the
onset and every linear rate are the real theory's; the spikes' height and the width of the
hysteresis loop are the model's. AGENTS.md has the formula and why.

**Over a coil, held past onset, the patch lays down as stripes (rolls), not hexagons.** In
the Helmholtz pair the same model gives hexagons every time, and the default (biased under
onset, kicked past it on the beat) comes up as spikes. A steady Bias above 1 over a coil
doesn't. I haven't found why beyond the model itself; it's the first open question in
AGENTS.md.

Also assumed rather than measured: the field each point feels is the local one (the local
approximation, normal component only); the fluid's optical constants (magnetite in oil,
mixed by Maxwell Garnett); a deep layer; and the coils' hardware.

There's no user guide, no browser demo, no OpenFX port, no video and no presets yet.

## Build

macOS (universal: Apple Silicon and Intel). The repo isn't published yet; from a checkout:

```bash
git submodule update --init
```

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build
```

```bash
cmake --install build
```

The last one copies `Ferrofluid.bundle` and `Ferrofluid Over.bundle` into
`~/Documents/Resolume Arena/Extra Effects`. Restart Arena and they're under Sources as
**SW Ferrofluid** and under Effects as **SW Ferro Over**.

Windows needs Visual Studio 2022 and vcpkg (for GLEW); see `.github/workflows/ci.yml`.
It hasn't been built there.

## Building and testing

```bash
tools/verify.sh
```

That's the whole lot: shader hygiene and `glslc`, a fresh universal build with no
warnings, both bundles' architecture, plist and ad-hoc signature, a host's view of them
through `oxbow`, every check at two rasters and four of them on the software renderer, the
pipe, the negative controls, the mutants, the sweep for dead controls and the render cost.
`CLAUDE.md` lists the checks one by one.

Render a frame of your own:

```bash
./build/fetest --out /tmp/ferrofluid.png --set "Magnet=Uniform" --set "Bias=0.7"
```

## License

MIT. See [LICENSE](LICENSE) and [ATTRIBUTIONS.md](ATTRIBUTIONS.md).

<!-- attributions:start -->
This project is built on other people's work — see [ATTRIBUTIONS.md](ATTRIBUTIONS.md).
<!-- attributions:end -->
