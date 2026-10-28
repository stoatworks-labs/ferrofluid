# ferrofluid

> **AI-assisted project.** This codebase was created with [Claude](https://claude.com/claude-code)
> (Anthropic), directed and reviewed by a human author. It has been in **Resolume Arena 7.28.1
> on macOS**, where both plugins register and every control that doesn't need sound changes
> Arena's output, and in **Arena 7.27.1 on Windows** on software rendering, where both load,
> register with every parameter as declared, and render. What's measured is measured by an offline harness that
> drives the real plugin class in a headless GL context on a synthetic clock, on this Mac's
> GPU at two rasters, and reads the pool back out of the plugin's own state and pixels. Every
> mode of the surface rings or grows at the Cowley–Rosensweig rate to **1.3e-5** of its
> height. The flat pool goes unstable between **12.003 and 12.033 A** against the theory's
> **12.018 A**, and the harness reproduces a published critical field (Mahr & Rehberg's
> **5.9 kA/m**). The settled lattice sits at the critical wavelength, from the heights and
> from the picture, with three peaks **60.7°, 56.1° and 63.2°** apart. The spikes jump up at
> onset and hang on below it until the model's own saddle-node, to within a sweep step.
> **21** deliberately wrong models and **17** one-character mutants of the shipped code are
> all caught. The spikes' **shapes are solved**, offline, with the full nonlinear
> magnetostatics, and the solver matches the fit to Gollwitzer et al.'s measured pattern
> amplitudes to within **1.1%**. Where the spikes stand and how far they've grown is still a
> **chosen** model, fitted to the solver: see [Status](#status).

Ferrofluid spikes on the beat, for [Resolume](https://resolume.com) Arena and Avenue, as
two FFGL plugins: **SW Ferrofluid**, a source that is a dish of ferrofluid in a
photographer's studio, and **SW Ferro Over**, an effect that is the same pool with your
clip hung above it, broken up in its mirror.

![A field of glossy black spikes on a hexagonal lattice, seen from 45 degrees: each spike a
sharp cone with a concave skirt, two hard white highlights from the soft boxes down its near
flank, dark reflections of its neighbours spreading from its base like wings, on a grey
ground](docs/hero.png)

<sub>One frame of the source, rendered by `fetest`, the offline harness, not captured
from Resolume, on 20 October 2026: each spike is the solved one. EFH1 in the Helmholtz pair
at 1.05 I_c for seven seconds, a 10 cm field
seen from 45° under Two Boxes (`--set "Fire On=Off" --set "Magnet=Uniform" --set
"Bias=0.7" --set "Field Width=0.55" --set "Tilt=0.75" --set "Lights=Two Boxes" --frames
420`).</sub>

![Resolume's IntoTheGlow_02 clip seen in a spiked pool from straight above: a lattice of
dark round spikes, their steep flanks black, between them a web of blue reflections with
copper edges where the troughs catch the screen](docs/over.png)

<sub>The Over on the same harness, a frame of Resolume's bundled `IntoTheGlow_02` (two
seconds in) as the clip, at 1.05 I_c with no beats, seven seconds in (`--over --clip
frame.rgba --set "Fire On=Off" --set "Bias=0.7" --frames 420`).</sub>

<!-- downloads:start -->

## Download

**[v0.1.0](https://github.com/stoatworks-labs/ferrofluid/releases/tag/v0.1.0)** — prebuilt for macOS and Windows. Pick your platform:

<details>
<summary><b>macOS</b> — Universal (Apple Silicon + Intel)</summary>

| Build | Download | Size |
| --- | --- | --- |
| Universal (Apple Silicon + Intel) · .dmg disk image | [`ferrofluid-0.1.0-macos-universal.dmg`](https://github.com/stoatworks-labs/ferrofluid/releases/download/v0.1.0/ferrofluid-0.1.0-macos-universal.dmg) | 724 KB |
| Universal (Apple Silicon + Intel) · .zip archive | [`ferrofluid-macos-universal.zip`](https://github.com/stoatworks-labs/ferrofluid/releases/latest/download/ferrofluid-macos-universal.zip) | 661 KB |

</details>

<details>
<summary><b>Windows</b> — x64</summary>

| Build | Download | Size |
| --- | --- | --- |
| x64 · .exe installer | [`ferrofluid-0.1.0-windows-x86_64-setup.exe`](https://github.com/stoatworks-labs/ferrofluid/releases/download/v0.1.0/ferrofluid-0.1.0-windows-x86_64-setup.exe) | 292 KB |
| x64 · .zip archive | [`ferrofluid-windows-x86_64.zip`](https://github.com/stoatworks-labs/ferrofluid/releases/latest/download/ferrofluid-windows-x86_64.zip) | 363 KB |

</details>

All builds, checksums and release notes: [github.com/stoatworks-labs/ferrofluid/releases](https://github.com/stoatworks-labs/ferrofluid/releases).

macOS builds are signed and notarised and open normally. The Windows builds are unsigned, so SmartScreen warns once.

<!-- downloads:end -->

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
  magnetises along a Langevin curve. The pool's surface obeys the linear theory exactly,
  mode by mode, plus a chosen nonlinear stress that makes the hexagons and decides where
  the spikes stand and how far they've grown: the pattern engine. Each spike it raises is
  drawn as the real fluid's equilibrium spike at that field, solved offline;
- **the camera** is a 100 mm lens looking down at the pool. The fluid is a black mirror,
  about 4.5% reflective, so what you see is what it reflects: the studio's soft boxes for
  the source, the clip on a screen above the pool for the Over, and each spike's neighbours.

## What falls out

None of this is drawn. It's the fluid doing what it does.

- **Spikes on the kick.** Below the critical current the pool is flat, whatever the music
  does. Each beat pushes the current past it and the spikes come up, at the speed the
  linear theory gives that much drive.
- **They hang on.** The onset is subcritical: once up, the spikes survive until the current
  falls to the solver's fold: 7% below where they appeared for EFH1, 2.4% for EMG 901, 0.5%
  for EMG 909. The default Bias (0.98) sits inside EFH1's loop, so a kick raises them and
  the quiet between kicks doesn't knock them down. EMG 909's loop is too narrow for that: its spikes fall between kicks.
- **The spikes are solved.** Each one is the equilibrium surface of a hexagonal lattice of
  spikes at the field it stands in, solved offline with the fluid's real magnetisation
  curve, gravity and surface tension: round and low just past onset, a sharp cone with a
  concave skirt at the top of the drive. EFH1's stand 2.2 mm tall at the fold with a tip
  radius of 0.47 mm, 4.5 mm at onset, and 11 mm with a 0.18 mm tip at 1.5 critical
  currents, a wavelength and a half above the troughs between them.
- **The spacing doesn't change with loudness.** Louder pushes the spikes taller, not closer
  together: the lattice sits at λ_c, about a centimetre, at 2% and at 10% past the
  critical current alike.
- **The pattern grows outward from the coil.** A solenoid's field is strongest over its
  middle, so spikes appear there first, and the patch spreads as the current rises, to
  where the local magnetisation crosses the critical value.
- **The coil drags.** Coil Lag is the coil's L/R: at 300 ms a beat barely gets the current
  up before it falls again.
- **The clip, broken up.** In the Over every spike is a small curved mirror whose steep
  flanks throw the clip away from the camera, so from above each one is a dark disc with a
  pinpoint of the clip at its tip and a faint star of its six neighbours, and the troughs
  between spikes catch the clip in a web of bright lines.
- **Spikes reflecting spikes.** Each spike's base shows dark reflections of its neighbours,
  which is most of what makes photographs of ferrofluid look the way they do.

## Controls

| Group | |
| --- | --- |
| **Drive** | Fire On (Off, Beat, Half Beat, 2 Beats, Bar, Onset), Fire (a pulse now), Audio (Resolume's audio input), Band (Low, Mid, High, Full), Gain and Bias (0–1.5 critical currents), Coil Lag (L/R, 1–300 ms), Threshold (how hard an onset must hit), Pulse (10 ms–1 s). |
| **Magnet** | Magnet (Uniform, Coil, Three Coils), Coil Size (the solenoid's radius, 5 mm–8 cm), Coil Spread (Three Coils' triangle, 2–10 cm a side). |
| **Fluid** | Fluid (EFH1, EMG 901, EMG 909), Viscosity (0.25–100× the datasheet's), Noise (the floor the spikes grow from: 0, then 1 µPa–0.1 Pa), Settle (flatten the pool). |
| **View** | Field Width (2–40 cm), Tilt (0–60°), Height (the Over's screen above the pool, 2 cm–1 m), Exposure (±4 stops), Lights (source: Soft Box, Two Boxes, Strips, Backlight, Window, Neon, Rim), Backdrop (source: Black, Studio), Mix (Over). |

**Bias and Gain are in critical currents**, not amps: 1 is where the pool goes unstable,
whatever the fluid and the magnet. The amps behind it are in Status.

The source's defaults are EFH1 over one coil of 4 cm radius, biased at 0.98 of the
critical current and kicked by 0.2 on every beat for 150 ms, through a 40 ms coil, seen
from 35° across a 10 cm field under rim lights. The Over's are the same pool in
the Helmholtz pair, seen from straight above, with the clip on a screen 4 cm up.

## Status

**v0.1.0, released 28 October 2026, and early days. The spikes are the solved ones.**

### Measured offline, on macOS

`tools/verify.sh` passes on this machine (Apple Silicon, macOS 26) against a fresh
universal Release build (20 October 2026, 19 minutes). It runs every picture check at **two
rasters**, 1280×720 and 320×180, and the transform, mirror, Fresnel and GL-state checks
again on **Apple's software renderer** at 320×180. What it establishes:

| check | result |
| --- | --- |
| `--fft` | the shipped radix-16/8/4/2 Stockham transform against an O(N²) DFT in double, both ways, four grid sizes up to the pool's 512×256: relative RMS **2.0e-7** (bound 1e-5) |
| `--dispersion` | six wavenumbers from 0.5 to 4 k_c at 0, 0.7, 0.97 and 1.1 I_c, three of them growing, against the closed-form damped oscillator with the Cowley–Rosensweig ω(k) from the harness's own Langevin curve: worst **1.3e-5** of the largest height (bound 2e-4) |
| `--onset` | Mahr & Rehberg's mixture: H_c **5886 A/m** against their 5.9e3, λ_c 9.14 mm against their 9; EFH1 in the Helmholtz pair, swept in 0.25% steps: still at **12.003 A**, grows at **12.033 A**, theory **12.018 A** |
| `--wavelength` | the settled lattice's dominant mode at **607 /m** from the heights and **682 /m** from the picture, against k_c = 639.7 /m, each within one bin (79 and 95 /m) |
| `--hexagons` | three peaks of strength 1, 0.96 and 0.83, **60.7°, 56.1° and 63.2°** apart (tolerance 4°, the box's angular resolution) |
| `--hysteresis` | two members of the model's invariant family, amplitudes a factor of two apart: each jumps at m 1.001, holds down to m **0.9950** and loses its spikes there; the model's three-mode reduction says **0.9951**, within one 0.0005 step |
| `--calibration` | the pattern engine's two constants, fitted per fluid: its saddle-node is the solver's fold (m **0.86254**, **0.95366** and **0.99206** for EFH1, EMG 901 and EMG 909) and its spike at onset the solver's (**4.484**, **2.335** and **0.934 mm**), to five figures |
| `--spikes` | a lattice of each fluid's spikes at 1, 1.25 and 1.5 I_c, held at equilibrium: every texel within a lattice constant of a spike is the table's cell to **9.5e-4** of the peak (bound 2e-3), both rasters. Out of the picture, straight down: the summit to **0.16%**, the peak-to-corner height to **0.30%** and the width at half height to **0.25%** (bounds 0.5%, 1%, 1%). EFH1's tip at 1.5 I_c: **0.18 mm** solved, **0.19 mm** drawn |
| `--blend` | a lattice seeded at 25% and 60% of the pool's own equilibrium stands at exactly that share, and each spike is that share of the table's to **4.6e-5** (bound 3e-3) |
| `--coilspikes` | over the default coil at 1.3 I_c: 20 spikes, the tallest (**8.97 mm**) 5.5 mm from the axis (bound λ_c), and of 145 pairs half a wavelength or more apart in radius, **5** have the outer one taller (bound 10%) |
| `--coil` | the L/R lag against 1 − e^(−t/τ) to 1.1e-16; the AGM's K(½) and E(½) against the tables; one loop's field against Biot–Savart to 2.8e-13; the solenoid's table against 400 turns summed one by one to 2.5e-5 |
| `--spread` | over the default coil at 1.2 and 1.4 I_c, the outermost spike stands at **17.0** and **21.1 mm** from the axis, where the local magnetisation crosses M_c at **18.1** and **22.5 mm** (tolerance λ_c, 9.8 mm) |
| `--fit` | Three Coils at the top of Coil Spread (10 cm) and of Bias (1.5 I_c), each fluid: every coil has its patch and the nearest spike stands **8.4 mm** from the periodic pool's edge at worst (EMG 901; bound half λ_c, 4.4 mm). At the old 30 cm the top coil had none |
| `--mirror` | the Over straight down at a flat pool with the reflectance forced to 1 returns a noise card with **0 of 3 686 400** channels different in 8 bits; at Mix 0, **0** floats differ from the clip; both rasters |
| `--fresnel` | a flat pool under a uniform sky at Tilt 0 and 45° (incidence 0.6° to 50.3°): each pixel's R/R0 against the exact unpolarised formula for n + ik to **5.5e-7** (bound 2e-4); EFH1's R0 = 0.0450 |
| `--beat` | Beat, Half Beat, 2 Beats and Bar at 120 and 97 BPM: every pulse starts within **1.8e-15 s** of its beat |
| `--audio` | six kicks, six pulses, each on its kick's own frame, none from the cold start; kicks in the low bins swing the coil **0.57 I_c** on Band Low and **0.000** on Band High |
| `--resize`, `--state` | a pool that changes raster (and shape) after 70 frames is **0 bytes** different from a fresh one at the new raster; the host's GL state comes back, both plugins |
| `--extremes` | the drive at its 1.6 I_c ceiling, each magnet, each fluid, a quarter of EMG 901's viscosity with the noise at its top: finite after 4 s, the tallest spike under half as much again as the pattern engine's own height scale (7.58 mm against 10.9 at the worst corner) |
| `tools/spikes/check.py` | the table is what the generator makes from the solved records, byte for byte, against its recorded SHA-256; sampled records still solve the hexagonal cell (residual under **1.7e-11 m**); a lattice of the table's kernels is the solver's cell to **0.2%** of the peak at every solved field; the solver against Gollwitzer et al. (below); the equal-area circle against the hexagon |
| `--pipe` | both plugins: 2.5 frames in, exactly 2 out; a closed stdout (`\| head -c 1`) exits 1, not 141 |
| `--negative` | **21** deliberately wrong models, **21** caught |
| mutation | **17** one-character mutants of the shipped GLSL and C++, **17** caught |
| `tools/sweep.py` | all **20** controls of each plugin that have pixels measurably change the picture |
| shaders | all 10 compile through `glslc`, not merely through Apple's driver |
| the bundles | both universal (`x86_64 arm64`), export `plugMain`, ad-hoc sign; `oxbow` reports `SW Ferrofluid` / `FE01` / `source` and `SW Ferro Over` / `FE02` / `effect` and renders through both |

The critical current behind Bias = 1, at the default coil size and spread:

| fluid | Uniform (Helmholtz pair) | Coil | Three Coils (each) | λ_c | applied field at onset |
| --- | --- | --- | --- | --- | --- |
| EFH1 | 12.02 A | 3.52 A | 3.94 A | 9.82 mm | 8.60 kA/m (10.8 mT) |
| EMG 901 | 17.27 A | 5.05 A | 5.66 A | 8.81 mm | 12.36 kA/m (15.5 mT) |
| EMG 909 | 21.60 A | 6.32 A | 7.07 A | 10.23 mm | 15.46 kA/m (19.4 mT) |

Render cost, GPU time by `GL_TIME_ELAPSED`, median of 120 frames after a warm-up, from
verify.sh's own run on 20 October 2026. The GPU was busy: other sessions' verify runs and a
Parallels VM shared the machine. At the defaults (up to 14 substeps a frame) the
source took **8.40 ms** at 720p, **7.45 ms** at 1080p and **12.5 ms** at 4K; the Over
**10.0**, **12.1** and **22.7 ms**; Three Coils seen from 60° **9.5**, **10.7** and
**16.5 ms**. A quiet run on 15 October, before the solved spikes, gave the source 4.0, 4.4
and 6.2 ms. The spikes add five passes and two transforms of the camera's surface (about
1.8 ms, timed pass by pass on 15 October with the CPUs busy), and the substep plan's wider
margin (AGENTS.md) adds a few substeps. How much of the rest is sharing wasn't measured on a
quiet machine. Most of the cost is the pool, which costs the same at any raster: about 150
passes a frame. At 1.5 I_c the plan takes 31 substeps (EFH1, Uniform) and 36
(Three Coils), under its cap of 48. macOS figures only.

### Not established

Everything above was compiled, rendered and measured offline against the plugin's own
class in a headless CGL context, plus an `oxbow` load. Since then it has been in Resolume:

- **Arena 7.28.1 on macOS** (this Mac, 28 October 2026), driven over Arena's REST API: both
  plugins register (`SW Ferrofluid`, `FE01`, a source; `SW Ferro Over`, `FE02`, an effect),
  with every control in its declared order and the About block's four buttons. Of the
  source's 21 controls, the 18 that don't need sound all change Arena's output; Fire On =
  Beat moves the pool on Arena's beat and Off holds it still. The Over's controls do the same
  over a lattice held still by Viscosity, and at Mix 0 its output is the effect bypassed, bit
  for bit (0 of 2,073,600 pixels different). On a flat pool Field Width, Height and Mix
  change nothing in the Over, as they shouldn't: a flat mirror under the screen hands the
  clip straight back.
- **Arena 7.27.1 on Windows** (the fleet's win-lab, Mesa llvmpipe: software rendering, no
  GPU, no sound device), on a build of 20 October's code: both DLLs load, both register
  (`FE01` a source, `FE02` an effect), all 25 of the source's parameters match name, order,
  type, range and default, and it renders. The pool's 150 passes a frame run Arena at about
  0.1 fps there, so that run says nothing about speed; the gate's control sweep, on this
  release's own Windows zip, is running.

No sound reached it in either host, so Audio, Band and Threshold haven't been tried with real
music, and I haven't seen how Fire On behaves with Arena's transport stopped.

**What's solved and what isn't.** Solved, offline (`tools/spikes/`): the shape of a spike.
For each fluid, the equilibrium surface of the hexagonal pattern's cell with the full
nonlinear magnetostatics (the fluid's Langevin curve, vacuum above), gravity and the exact
surface energy, along the whole branch: the unstable part, the fold, and the stable part to
the top of the drive. It matches the fit to Gollwitzer et al.'s X-ray measurements of
APG512a's pattern (J. Fluid Mech. 571, 2007) to within 1.1% in amplitude between ε 0.05 and
0.44, jumps at onset to their 1.5 mm, and collapses on the down-sweep between their
simulation's field and their experiment's. Re-solved with half as many harmonics again,
the presets' spikes come out up to 0.3% taller (EFH1 at the top of the drive), with tips up
to 8% sharper and still sharpening: the table's peaks are good to about 0.3%, its sharpest
tips are a little round. The shipped table is checked against the solver at every solved
point, and against its own sources byte for byte.

Not solved: where the spikes stand and how far they've grown. That's still the pool, a
spectral model with the linear theory exact and a **chosen** nonlinear stress, now labelled
the pattern engine. Its two constants are fitted per fluid so its fold and its spike height at
onset are the solver's, and are no longer controls. Each of its spikes is drawn as the
solved spike at the field it stands in, scaled by how far it has grown towards its own
equilibrium. So growing and collapsing spikes are scaled equilibrium shapes, not the real
dynamics; a spike without all six neighbours (a patch's edge, a defect) gets a shape the
solver never saw; each spike takes the field at its centre, normal component only; and tips
sharper than the camera's grid come out rounder (EFH1's at 1.5 I_c: 0.18 mm solved, 0.19 mm
drawn). EFH1's table stops at 1.566 I_c, where the seed solver's grid gives out; the drive's
last few hundredths draw that spike. AGENTS.md has the details.

**Over a coil, held past onset, the patch lays down as stripes (rolls), not hexagons.**
Held at 1.125 or 1.3 I_c for ten seconds over the default coil it's rolls, whether the
current steps on (a 1 ms coil) or rises through the default 40 ms. It's marginal: with a
20% longer substep, 1.3 I_c through 40 ms came up as hexagons. Drawn with solved spikes, a
roll is a row of spikes along its crest, which no real roll looks like. In the Helmholtz
pair it's hexagons, and the default (biased under onset, kicked past it on the beat) comes
up as a small hexagonal patch. Open; AGENTS.md has what was tried.

Also assumed rather than measured: the field each point feels is the local one (the local
approximation, normal component only); the fluid's optical constants (magnetite in oil,
mixed by Maxwell Garnett); a deep layer; and the coils' hardware.

There's a [user guide](https://stoatworks-labs.com/software/ferrofluid/guide/) and a browser
demo (below), but no presets and no OpenFX port.

## Browser demo

**<https://ferrofluid-demo.stoatworks-labs.com/>** has both plugins, with their own controls
and defaults. All nine passes are the plugin's own GLSL, spliced unedited into
`demo/shaders.js`, on the plugin's own 512 × 256 pool, and the solved-spike table the plugin
uploads is copied into `demo/spiketable.js`; `demo/tools/check_shaders.py` (run by
`tools/verify.sh`) fails on a changed character or number in either. The CPU half (the
magnetisation, the coils' fields, the drive, the critical current, the substep plan and
every uniform) is a JavaScript port, and only a reader checks it: nothing in the repo runs it
against the C++. It was compared with `fetest` once, frame for frame, and planned the same
substeps and got the same current and m, with 96% and 99.9% of pixels within one level at
the two settings tried (AGENTS.md). There's no audio in a browser, so Audio, Band and
Threshold aren't on the page and the beat comes from a BPM clock the page keeps. If a
browser can't keep up, the page slows its clock, the pool and the beat together, rather than
crawl along at the plugin's substep cap. It needs WebGL2 with `EXT_color_buffer_float` and
`OES_texture_float_linear`. It's a demo, not the plugin, and the page says what it doesn't
reproduce.

## Build

macOS (universal: Apple Silicon and Intel):

```bash
git clone --recursive https://github.com/stoatworks-labs/ferrofluid
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

Windows needs Visual Studio 2022 and vcpkg (for GLEW); see `.github/workflows/ci.yml`,
which builds both platforms on every push.

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
