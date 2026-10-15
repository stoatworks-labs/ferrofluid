# AGENTS.md: Ferrofluid

The why. `CLAUDE.md` has the commands; `README.md` the claims and the numbers.

## The idea

The spec (`specs/SPEC-ferrofluid.md`, 2026-10-15) asked for "a pool of ferrofluid over an
electromagnet whose current is the music, seen as the black mirror it is", with nothing
drawn. So the plugins are the whole chain:

1. **The drive** (`Drive.cpp`). The coil is asked for Bias + Gain (level + pulses), in units
   of the critical current, and its current follows through its own L/R lag in closed form.
   Pulses fire on the host's beat (placed inside the frame where the beat fell), on onsets in
   the audio, or on Fire.
2. **The magnet** (`Coil.cpp`). Uniform is a Helmholtz pair. Coil is a solenoid under the
   pool: the exact field of a loop (complete elliptic integrals by the AGM) summed over its
   winding. Three Coils are three of them in a triangle, one per band.
3. **The fluid** (`Fluid.cpp`). Langevin magnetisation with the demagnetising jump across the
   surface, H_in = H_a − M(H_in), solved by bisection; the chord and tangent permeabilities;
   the Cowley–Rosensweig stress coefficient G = μ0 M² r0/(1 + r0). Three datasheets.
4. **The surface** (`Shaders.cpp`: fft, force, evolve). The height is a field on a periodic
   grid, held as its spectrum (h_k, v_k). Each mode is advanced exactly as the damped
   oscillator of the linear theory at a reference stress G_ref; everything else (the local
   field's excess over G_ref, the nonlinear stress and a noise floor) is computed in real
   space and kicks the modes. Kick-then-exact-drift is symplectic Euler.
5. **The light** (`Shaders.cpp`: pack, bounds, render). The spectrum is zero-padded to twice
   the grid and transformed back with its gradient; a camera ray is marched to that height
   field, reflected with the exact Fresnel reflectance of an absorbing dielectric, followed
   for one more bounce off a neighbouring spike, and sent to the environment: the clip on a
   screen above (the Over) or a studio's soft boxes (the source).

## Decisions taken without asking (the brief says decide and write it down)

- **Names and ids** as the spec gave them: `SW Ferrofluid` `FE01` source,
  `com.stoatworks.ffgl.ferrofluid`, `Ferrofluid.bundle`; `SW Ferro Over` `FE02` effect,
  `com.stoatworks.ffgl.ferrofluid.over`, `Ferrofluid Over.bundle`. Harness `fetest`. One
  class (`FerrofluidPlugin(isEffect)`), two registrations, a host order per plugin with the
  About block last (honeydew's shape).
- **Bias and Gain are in units of the critical current, I_c**: the current at which the
  selected fluid over the selected magnet reaches onset at the pool's strongest point. In
  amps, onset moves by a factor of six across the fluids and magnets (3.5 A for EFH1 over
  the default coil, 4 cm in radius; 21.6 A for EMG 909 in the Helmholtz pair), so a slider
  in amps would put the interesting bit somewhere different every time you changed the
  fluid. In I_c, 1 is always onset. For Three Coils, I_c is the current all three carry
  when the strongest point of their SUMMED field reaches onset (found on a grid round the
  triangle); the first definition, one coil's own centre, put the overlap far past onset and
  blew up at the corners.
- **The drive is clamped to 1.6 I_c** and Bias and Gain run 0–1.5. 1.6 I_c is m ≈ 2.5 for
  EFH1, where the fastest-growing wavelength is already a third of λ_c and the grid's 12
  cells a λ_c are four of it. At 1.7 I_c the coil's patch grew a fine-grained transient
  that the grid couldn't draw.
- **The pool is fixed per fluid**: 512 × 256 cells at 12 cells to a critical wavelength
  (EFH1: 0.82 mm cells, a pool 419 × 210 mm), whatever the host's raster. The physics never
  depends on the raster, which is what `--resize` holds to 0 bytes; Field Width is only the
  camera. At 40 cm with EMG 901 (a 376 mm pool) the frame can show the edge of the next
  periodic copy; I left it.
- **The nonlinear stress is chosen, not derived: the spec's fallback.** I didn't derive
  Gailitis's or Friedrichs and Engel's coefficients honestly in the time, so the stress is the
  gradient of a local energy with two constants exposed as Hysteresis and Saturation
  (`Fluid.h` has the formula). Its quadratic part breaks up/down symmetry (hexagons, a
  subcritical onset); its cubic part bounds the spikes. Both scale with the local magnetic
  number, so they vanish where there's no field. What this costs: the spike's height and the
  width of the hysteresis loop are the model's, not a real fluid's. The wavelength, the onset
  and every linear rate are the real theory's.
- **The energy is local in g = W h, W = max(|∇|, k_c)/k_c, not in h** (the traps say why).
- **Defaults Hy 1, Sa 0.5.** The lattice stays at k_c: the settled pattern's dominant mode
  is at q = 1.00 at m 1.04 and at m 1.21. Hy 2 pulled it to 0.89 k_c. The saddle-node is then
  m = 0.863, which is 0.928 I_c for EFH1.
- **Bias 0.98 I_c, Gain 0.2, Fire On Beat, Pulse 150 ms, Coil Lag 40 ms.** The default
  bias sits inside the hysteresis loop, between the saddle-node (0.93 I_c) and onset (1): the
  flat pool stays flat until a beat kicks it past onset, then the spikes hang on after the
  kick. That's the look in the photographs and the spec's "hanging on after it".
- **The local field approximation.** In a coil's field each point of the surface feels the
  stress coefficient of the field there. Only the normal component reaches the model: the
  tangential field near a coil's edge (which would stretch spikes into ridges along it) is
  left out. The residual G(x) − G_ref is applied as ½[(G − G_ref)|∇|h + |∇|((G − G_ref)h)],
  the variational form, so the model stays the gradient of an energy.
- **G_ref = min(G_max, G_c) for a coil**, G_max for Uniform (where it's exact and the kick
  carries only the nonlinear stress). Past onset, a reference at G_max makes the far pool,
  where the coil's field is nothing, unstable on its own, with nothing to hold it but the
  kick's residual, which is band-limited; Three Coils ran away above the band.
- **The substep comes from a stability plan, not a constant** (`planSubsteps`): the largest
  step at which the kick–drift map's trace stays in [−1.9, 2] at every wavenumber up to the
  stress's cut, for the frame's strongest stress, capped at 2 ms and at 48 substeps. A frame
  that needs more drops the excess time and logs it, rather than run unstable.
- **The camera is a 100 mm lens** (half-field tangent 0.18), the product photographer's
  choice. The source looks from Tilt 35° at a 10 cm field; the Over looks straight down
  (Tilt 0) at a Uniform pool, so the clip is broken up across the whole frame.
- **The Over's screen hangs at Height facing down and shows the clip mirrored**, as a
  teleprompter's monitor does, so a flat pool's reflection reads the right way round. Past
  the screen's edge is a dark room (black). The clip's light is linearised (sRGB) and scaled
  by its own alpha.
- **The Over's alpha is mix(clip alpha, 1, Mix).** A wet frame is a picture of a pool, which
  is opaque wherever the clip was transparent; Mix 0 gives the clip back bit for bit,
  alpha included (fetched with `texelFetch`, never filtered).
- **The source's studio**: six Lights presets (Soft Box, Two Boxes, Strips, Backlight,
  Window, Neon) and a Backdrop (Black or Studio, a seamless sweep that dims towards the
  floor). The spec asked for count, size and colour; presets kept it near 25 controls.
- **Exposure is 2^stops / R0**: 0 stops shows a diffuser of radiance 1 seen in a flat pool
  at normal incidence as 1. A black mirror reflects 4.5%, so without the R0 the picture is
  nearly black.
- **The optical constants are assumed**: Maxwell Garnett spheres of bulk magnetite
  (n 2.4, k 0.6 at 550 nm) in a mineral oil (n 1.47) at the volume fraction M_s implies.
  EFH1 comes out n = 1.537, k = 0.032, R0 = 0.045. They're order-of-magnitude literature
  values, not a measurement; R0 moves by about 0.003 across any plausible choice.
- **Datasheets.** EFH1 from Ferrotec's 2010 technical data sheet, used whole (its 2.58
  initial susceptibility is quoted in Gauss/Oe, i.e. 4πM over H, which is the SI χ; the
  current web page says 2.64 and gives no surface tension). EMG 901 and EMG 909 from Mahr &
  Rehberg (1998), section 2, whose sets are complete; Ferrotec's pages for them omit the
  surface tension.
- **The magnets' hardware**: the solenoid is 400 turns over 40 mm, its top face 13 mm below
  the surface (10 mm of fluid, deep compared with every fluid's λ_c/2, on a 3 mm dish base);
  the Helmholtz pair is R = 0.2 m, 200 turns a coil. Coil Size is the solenoid's radius.
- **Noise is a pressure in Pa RMS over a square millimetre and a millisecond**, so a cell
  and a substep of any size see the same white floor.
- **Settle flattens the pool**; so does changing the Fluid (another fluid is another pool).
- **Three Coils follow Low, Mid and High**, one each, as the spec asked, so Band does
  nothing with Three Coils. Every pulse goes to all three.
- **Provisional hand copies**: `source/StoatworksAbout*.h` (guide = "", as graticule did)
  and `ATTRIBUTIONS.md`. The project isn't registered in stoatworks-backend, so
  `sync-about.py` and `sync-attributions.py` can't generate them yet.
- **Not built**: a user guide, a browser demo, an OpenFX port, presets, a video.

## The traps actually hit

**An explicit stress held over an exact step pumps energy.** The first evolve applied the
stress as a constant forcing across the oscillator's exact step. That isn't symplectic and
the stiff modes grew. Kick then exact drift is symplectic Euler: stable while the map's trace
stays inside ±2.

**The first plan was a 9 µs substep.** Two bugs: an extra k_c in the stiffness, and a
criterion of |trace| ≤ 1.96, which no small step passes, because an undamped mode's trace
tends to 2 as dt does. That's neutral, not unstable. The plan now fails a step only above 2
(plus rounding) or below −1.9, where a stiff mode turns over.

**An energy local in h makes mesas.** With the cubic acting on h, every point saturated at
the same height and the spikes came out as flat-topped domes with lobed wings, the opposite
of a real spike's sharp tip and gentle trough. Local in |∇|h the cap is on the slope and the
tips are cusped, but the cap then costs short waves more than long ones and dragged the
lattice to 0.73 k_c at m 1.43. W = max(|∇|, k_c) is |∇| at the tips and k_c below it
(neutral in wavelength), and the lattice stays at k_c. A mixed form (quadratic in g, cubic
in h) blew up to NaN.

**The W-weighted stress is stiff at high k.** Its cut has to sit above the unstable band's
top (m + √(m² − 1) k_c) or the band's upper modes run away unopposed: `--extremes` found NaN
at the drive's ceiling. The outer cut is now max(3, top + 0.5) k_c, at most 0.9 of Nyquist,
re-planned every frame.

**Spikes showed flutes down their flanks.** A spike is about fifteen cells round, and a
gradient read bilinearly between so few samples dips between them; a mirror shows that as
vertical flutes against strip lights. The surface the camera reads is now the spectrum
zero-padded to twice the grid, which is the same band-limited surface sampled finer. The
arms that remain round each spike's base are the neighbours' reflections, which is physical.

**A coil's patch lays down rolls, not hexagons, when held past onset.** At a steady
1.125 I_c over the default coil the patch is parallel stripes; in the Helmholtz pair the
same model at the same m is hexagons every time. I tried a quarter of the substep (same
rolls, so not the splitting), the variational residual (same rolls, so not the residual's
missing symmetry; it stays anyway, because the model should be an energy), and G_ref =
G_max (blew up Three Coils, see above). What's left is the model: with a local quartic,
rolls are stable a little past onset, and a front spreading from the coil's centre selects
them. The default, biased under onset and kicked past it on the beat, comes up as spikes;
past onset held steady over a coil it doesn't. Open.

**The closed-form three-mode reduction disagreed with the solver.** The truncation to fourth
order in the amplitude gave coefficients 12 and 3 (36 and 12 in the W form) and missed the
GPU's harmonics by a factor of two to thirteen: the cubic drives the harmonics at A³ Sa, the
same order as the quadratic's A² Hy wherever A ~ Hy/Sa, which is everywhere on the branch.
The reduction is now exact: the model's energy averaged over one hexagonal cell (48 × 48
samples) for three modes at k_c with their six √3 k_c and six 2 k_c harmonics, minimised by
Newton, and followed down to its saddle-node by continuation. It matches the solver within a
sweep step (A 0.0457 predicted against 0.0465 measured). The family (λHy, λ²Sa) has the same
saddle-node exactly, which is what makes the second hysteresis case a check.

**The AGM stopped one ulp short.** E(1/2) came out 1e-13 off: c sat at one ulp while its
weight kept doubling. It terminates on the size of the term now; `--coil` caught it.

**Checks that measured the wrong thing.** `--dispersion` first ran its growing modes so long
they went nonlinear: now 1e-8 m and six frames past onset. `--onset` once sat a sample on the
onset itself, where a mode neither grows nor decays and the verdict is rounding's: the sweep
is now half a step off 1. `--wavelength` from the picture measured the backdrop's gradient,
and only horizontally: it's a 2D Hann-windowed spectrum of the picture in 0.5–1.5 k_c now.
`--hexagons` failed a lattice that settled rotated: its tolerance is 4°, the lattice's own
resolution on that pool. `--spread`'s ring threshold tripped on the wave floor: it's the
outermost spike now. `--audio`'s hi-hat made onsets of its own, and each band is normalised
against its own peak, so it now tests the swing on Low against High.

**Mix 0 wasn't the clip on the software renderer.** Its bilinear weights aren't exact at a
texel's centre. The dry clip is `texelFetch`ed.

**macOS has no `timeout`.** `mutate.sh` uses perl's alarm (schlieren's).

**The software renderer is too slow for the physics.** A 512 × 256 pool at 12 substeps a
frame for hundreds of frames takes it many minutes a check, so `verify.sh` runs fft,
mirror, fresnel and state there and the rest on the GPU.

**Per-pass overhead dominates the cost**, not the arithmetic: 12 substeps a frame is about
150 passes. A uniform-location cache and a cleared (not loaded) target before each pass
bought little. The bench is in the README.

## Verified, and assumed

Verified (numbers in the README's Status):

- every mode's motion against the closed-form damped oscillator with the Cowley–Rosensweig
  ω(k), with and without field, growing modes included (`--dispersion`);
- the onset current within a 0.25% step of the theory's, from the harness's own
  magnetisation and permeabilities; Mahr & Rehberg's published critical field from their
  numbers (`--onset`);
- the lattice at k_c, from the heights and from the picture (`--wavelength`); three peaks
  60° apart (`--hexagons`);
- the subcritical jump at onset and the collapse at the model's own saddle-node, for two
  members of the invariant family (`--hysteresis`);
- the coil's field against Biot–Savart, the lag against its exponential (`--coil`); the
  patterned radius against where the local M crosses M_c (`--spread`);
- the mirror's geometry (`--mirror`) and the Fresnel reflectance (`--fresnel`);
- the beat, the onset, the raster change, the GL state, the corners (`--beat`, `--audio`,
  `--resize`, `--state`, `--extremes`).

Assumed, and said so:

- the nonlinear stress (chosen constants, a local energy);
- the local field approximation, normal component only;
- the optical constants;
- weak damping (Lamb's 2νk²; good for these viscosities at these wavelengths, not
  derived for a layer of finite depth);
- a deep layer: the dispersion is the infinite-depth one;
- the coils' hardware (turns, sizes, depth): plausible, not anyone's real rig.

## Would this hold on another rasteriser, at another raster?

- `--coil`, `--beat`, `--names`, `--cues`: no GL, double precision.
- `--fft`: relative RMS against an O(N²) DFT in double, bound 1e-5, twiddles from tables
  built in double. Runs on the software renderer too.
- `--dispersion`: reads the state, not the picture, so the raster can't matter; bound 2e-4
  of the largest height for float32 over hundreds of substeps. Both rasters.
- `--onset`: a bracket, one current step (0.25%) wide, around a threshold computed in double.
- `--wavelength`: within one bin of the pool's lattice (heights) and of the picture's
  spectrum (picture), both rasters. The picture is box-averaged to about 160 pixels across
  before its transform, so its bin is the frame's, 2π/W, at any raster.
- `--hexagons`: 4° (the lattice's angular resolution on that pool) and a 20% balance.
- `--hysteresis`: within a sweep step plus 5% of the loop's width, both members; the
  prediction is double.
- `--spread`: the outermost spike within λ_c of the prediction, a spike's spacing.
- `--mirror`: 8-bit equality with Fresnel forced to 1; Mix 0 float-exact (texelFetch).
  Runs on the software renderer too.
- `--fresnel`: each pixel's R/R0 against the exact formula to 2e-4, with the complex
  arithmetic written out (no trig). Runs on the software renderer too.
- `--audio`: frame-exact; `--resize`: 0 bytes; `--state`: state queries. Raster-independent.
- `--extremes`: finite, and the tallest spike under 1.5(1 + Hy/Sa)/k_c.

All of the picture checks run at 1280 × 720 and 320 × 180 on this Mac's GPU, and fft,
mirror, fresnel and state again on Apple's software renderer at 320 × 180.

## The negative controls and mutants

`fetest --negative`: 17 wrong models, each must fail its check.

| check | the wrong model |
| --- | --- |
| dispersion | the evolve without surface tension |
| dispersion | G without its r0/(1 + r0) |
| dispersion | r0 from the chord permeability alone |
| onset | the demagnetising jump forgotten: H_in = H_a |
| wavelength | the evolve without surface tension |
| hexagons | no quadratic term (Hysteresis 0): up/down symmetric |
| hysteresis | no quadratic term: a supercritical onset |
| coil | the L/R lag stepped by forward Euler |
| coil | the on-axis field used at every radius |
| spread | the local field dropped: the pool sees G_ref everywhere (at onset's, nothing grows) |
| mirror | the screen read unmirrored |
| fresnel | the s-polarised reflectance alone |
| beat | a beat fires at its frame's start |
| audio | the analyser never primed |
| resize | a new raster flattens the pool |
| extremes | the substep three times what the plan allows |
| cues | every control ramped between keys |

`tools/mutate.sh`: 14 one-character mutants, each must fail its named check.

| mutant | caught by |
| --- | --- |
| GLSL: the evolve's surface tension with the wrong sign | `--dispersion` |
| GLSL: Lamb's damping 3νk² | `--dispersion` |
| GLSL: the twiddle's index not reduced mod span | `--fft` |
| GLSL: the saturation pushing the spikes up | `--hysteresis` |
| GLSL: the stress kicking the modes backwards | `--hysteresis` |
| GLSL: the coil's field subtracted | `--spread` |
| GLSL: the unpolarised mean 0.6 of the sum | `--fresnel` |
| GLSL: the screen's centre off the pool's | `--mirror` |
| C++: Cowley–Rosensweig's r0/(2 + r0) | `--dispersion` |
| C++: the demagnetising jump with the wrong sign | `--onset` |
| C++: the loop's elliptic formula with + r² | `--coil` |
| C++: the L/R lag's exponent dt·τ | `--coil` |
| C++: the beat's place in its frame from the sum of phases | `--beat` |
| C++: the first frame primed with a flux of 1 | `--audio` |

## The sweep's honest list

`python3 tools/sweep.py` sweeps all 22 controls of each plugin that have pixels (the About
block's four and the Audio buffer have none; Audio is driven through Band and Threshold) and
finds every one live. `--bare` drops the contexts and finds these dead, each for the reason
its context exists:

- **Band** and **Threshold**, both plugins: they act on the audio, and with no music in the
  buffer there's nothing for them to act on.
- **Coil Spread**, both plugins: it places the three coils, and only Three Coils has three.
- **Coil Size**, the Over: the Over sits on a Helmholtz pair (Uniform), where no coil has a
  size. On the source, whose default magnet is a coil, it's live bare.

## Open questions

- **Rolls over a coil past onset** (the traps). Whether a derived (Gailitis/Friedrichs–Engel)
  stress, or the tangential field, would give hexagons there is the first thing to try.
- **The derived coefficients.** The spike's height and the loop's width are the model's.
  Gollwitzer et al. (J. Fluid Mech. 571, 2007) measured real spike profiles by X-ray;
  matching them would be the check.
- **The tangential field** at a coil's edge, which stretches real spikes into ridges.
- **Finite depth** and the dish's wall: the pool is deep and periodic.
- **Cost.** About 150 passes a frame; fusing the transform's passes, or fewer substeps at
  low drive, would be the place to start.
- **Never seen in a host.** How Fire On follows Arena's transport, how the controls read in
  the inspector, what the audio does with real music.
