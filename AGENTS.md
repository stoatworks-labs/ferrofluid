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
4. **The pattern engine** (`Shaders.cpp`: fft, force, evolve). The pool's height is a field
   on a periodic grid, held as its spectrum (h_k, v_k). Each mode is advanced exactly as the
   damped oscillator of the linear theory at a reference stress G_ref; everything else (the
   local field's excess over G_ref, the chosen nonlinear stress and a noise floor) is
   computed in real space and kicks the modes. Kick-then-exact-drift is symplectic Euler.
   It decides where spikes stand and how far they've grown. It isn't what the camera sees.
5. **The spikes** (`Shaders.cpp`: spikes, deposit, combine; `SpikeTable.cpp`, generated
   by `tools/spikes/`). Each of the pool's spikes is replaced by the real fluid's
   equilibrium spike at the field it stands in, solved offline, scaled by how far the
   pool's spike has grown. At equilibrium a lattice of them is the solved cell exactly.
6. **The light** (`Shaders.cpp`: pack, bounds, render). A camera ray is marched to that
   surface (twice the pool's grid, with its gradient), reflected with the exact Fresnel
   reflectance of an absorbing dielectric, followed for one more bounce off a neighbouring
   spike, and sent to the environment: the clip on a screen above (the Over) or a studio's
   soft boxes (the source).

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
- **The pattern engine's nonlinear stress is chosen, not derived; its constants are fitted
  to the solver.** The stress is the gradient of a local energy with two constants
  (`Fluid.h` has the formula). Its quadratic part breaks up/down symmetry (hexagons, a
  subcritical onset); its cubic part bounds the spikes. Both scale with the local magnetic
  number, so they vanish where there's no field. Since the second review they're no longer
  controls (Hysteresis and Saturation are gone): `fetest --calibrate` fits them per fluid so
  the engine's saddle-node is the solver's fold and its spike height at onset the solver's
  (the solved spikes, below). What's left of the choice: how fast the pattern grows past the
  linear stage, and its lattice. The wavelength, the onset and every linear rate are the
  real theory's.
- **The energy is local in g = W h, W = max(|∇|, k_c)/k_c, not in h** (the traps say why).
- **The fitted constants** (Hy, Sa): EFH1 0.6956 and 0.2410, EMG 901 0.4474 and 0.2612,
  EMG 909 0.2892 and 0.4558 (`generate.py --stage table` asks `fetest --calibrate`). Before
  the solver they were controls, defaulting to Hy 1, Sa 0.5, which put EFH1's saddle-node
  at 0.928 I_c; the solver puts its fold at 0.9276 I_c (m 0.8625). So the old guess had the
  fold right, by luck, and the spikes at onset a third too short: 3.1 mm against the
  solver's 4.48.
- **Bias 0.98 I_c, Gain 0.2, Fire On Beat, Pulse 150 ms, Coil Lag 40 ms.** The default
  bias sits inside EFH1's hysteresis loop, between the solver's fold (0.928 I_c) and
  onset (1): the flat pool stays flat until a beat kicks it past onset, then the spikes hang
  on after the kick. That's the look in the photographs and the spec's "hanging on after
  it". EMG 901's fold is 0.976 I_c, so 0.98 is only just inside it; EMG 909's is
  0.995 I_c, so with 909 the spikes fall between kicks. That's the fluid, not a setting
  to tune round.
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
  that needs more drops the excess time and logs it, rather than run unstable. The
  nonlinear stress's stiffness is taken at a slope of 1.5 Hy/Sa (the traps).
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
- **The source's studio**: seven Lights presets (Soft Box, Two Boxes, Strips, Backlight,
  Window, Neon, Rim) and a Backdrop (Black or Studio, a seamless sweep that dims towards
  the floor). The spec asked for count, size and colour; presets kept it near 25 controls.
  Rim is the default (added 2026-10-15 at review): two tall strips low on either side and a
  little beyond the pool, and a dim overhead fill, so each spike's side flanks, seen nearly
  edge on, carry a thin line of light, as in product photographs of ferrofluid.
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
- **Not built**: an OpenFX port, presets.

## The solved spikes (2026-10-15, second review)

Allan chose the second route from the derived-stress review: exact spike shapes solved
offline, and the real-time pool deciding where spikes stand and how big they are. So the
picture is now two things layered, and it matters which is which.

**Solved (offline, `tools/spikes/`, numpy, double).** The equilibrium free surface of one
cell of the hexagonal pattern, with the full nonlinear magnetostatics: the fluid
magnetised along its Langevin curve, vacuum above, gravity and the exact surface energy
(the full curvature). It's Gollwitzer et al.'s problem (J. Fluid Mech. 571, 2007, §3), in
their cell: the rectangle a × √3 a holding two spikes, a = 4π/(√3 k_c), periodic, the
fluid infinitely deep, a uniform applied field far above and below.

- **The energy.** E[h] = ∫(ρgh²/2 + σ(√(1 + |∇h|²) − 1)) dA − Π*[h], with
  Π* = min over φ of ∫w(|∇φ|) dV + μ0 H_a(∫top φ − ∫bottom φ), w = μ0H²/2 (+ μ0∫M dH in
  the fluid). The magnetic stress is the derivative of the discrete Π* at fixed φ (the
  envelope theorem), so the discrete problem is variational and nothing is differenced at
  the surface. Its linear limit reproduces Cowley–Rosensweig's G with r0 = √(μ_c μ_t)/μ0,
  which the solver computes nowhere: 1.000214 of it (`solver.py`'s own test).
- **The discretisation.** Spectral across the cell (32 × 56). Vertically the space is
  mapped, z = ζ + h(x, y) exp(−(ζ/2.6λ)²), so the surface is the level ζ = 0; ζ is graded
  geometrically from λ/400 at the surface by 1.07 a step, to 6λ above and 4λ below (147
  levels). The surface is expanded in the D6-symmetric orbits of the reciprocal lattice
  (33 unknowns, up to 9.2 k_c) and solved by Newton at a fixed fundamental amplitude A,
  with the applied field as an unknown; the inner field by Newton and conjugate gradients
  preconditioned with the flat surface's operator. Every record is converged to 1e-12 m.
- **Validation against Gollwitzer et al.** For their fluid, APG512a, the pattern's
  amplitude (peak minus the cell's corners) against their eq. 4.1, the fit to their X-ray
  measurements: +0.6% at ε 0.05, +1.1% at 0.10, +0.8% at 0.20, +0.2% at 0.30, −0.1% at 0.35
  and −0.8% at 0.44. The down-sweep collapses at ε −0.0143 from 0.78 mm:
  their simulation's bistable range ends at ε −0.007 and their experiment's at −0.020,
  both from about 1 mm. The up-sweep jumps at onset to 1.50 mm (theirs about 1.5).
  Bounds and why in `check.py`.
- **Convergence** (`convergence.py`, records/convergence.json). Re-solving the table's own
  records at nx 24 and 48 (the table's is 32), and at a vertical grid twice as fine at the
  surface and graded more slowly, at each fluid's fold, mid branch and top (2026-10-20,
  5 h on 10 workers; all 36 converged). The vertical grid is converged: the peak moves by at
  most 9e-5, the tip radius by 4e-5, the field by 1.6e-4 in u. The cell's harmonics are what
  limit the sharp fluids. At nx 48 the peak rises by up to 0.27% (EFH1; 0.19% EMG 901, 9e-5
  EMG 909, 3e-5 APG512a), most at the top of the branch: EFH1's at 1.566 I_c goes 11.91,
  11.97, 12.00 mm at nx 24, 32, 48. The tip radius tightens by up to 8% (EFH1's there 0.195,
  0.176, 0.161 mm) and 6% (EMG 901), and is still moving; EMG 909's and APG512a's by 1%. So
  the table's peak is good to about 0.3% and low by that much, and the tips of EFH1 and
  EMG 901, a curvature at a single point, are rounder than converged by 8% or more. The
  camera rounds them further anyway (the shipped tip is the table's to 5%).
- **The equal-area circle.** An axisymmetric cell of the same area (R = √(8π/√3)/k_c,
  `axisym.py`, finite elements, 200 radial nodes) is fast enough to trace each fluid's
  whole branch in minutes, so it seeds the hexagonal cell and nothing else. What it would
  have cost as the answer: for the hexagon's peak it needs a field up to 0.019 off the
  hexagon's (EFH1; 0.012 for EMG 901, 0.006 for EMG 909), and at the hexagon's field, clear
  of the fold, a peak up to 1.8% off; its folds are within 0.0006 of the hexagon's
  (`check.py`). That's why the table is the hexagon's.
- **The branch.** Each fluid's branch from the flat surface's bifurcation, back round the
  fold (the subcritical onset: the unstable branch is solved too), and up the stable
  branch to u = 1.62 (the drive's ceiling is 1.6) or as far as the solver goes. Per fluid
  (u = H_a/H_a,c, which is I/I_c in a uniform field):

  | fluid | λ_c | the fold: u (m), peak, tip radius | u 1: peak, tip | u 1.5: peak, tip | the branch's end: u, peak, tip |
  | --- | --- | --- | --- | --- | --- |
  | EFH1 | 9.82 mm | 0.9276 (0.8625), 2.21 mm, 0.47 mm | 4.48 mm, 0.31 mm | 11.15 mm, 0.18 mm | 1.566, 11.97 mm (1.22 λ_c), 0.18 mm |
  | EMG 901 | 8.81 mm | 0.9762 (0.9537), 1.23 mm, 1.40 mm | 2.33 mm, 0.65 mm | 8.49 mm, 0.22 mm | 1.624, 9.88 mm (1.12 λ_c), 0.19 mm |
  | EMG 909 | 10.23 mm | 0.9953 (0.9921), 0.48 mm, 9.1 mm | 0.93 mm, 4.3 mm | 5.57 mm, 0.75 mm | 1.627, 6.22 mm (0.61 λ_c), 0.68 mm |
  | APG512a | 9.98 mm | 0.9928 (0.9886), 0.56 mm, 6.9 mm | 1.11 mm, 3.0 mm | not solved | 1.252, 3.64 mm (0.36 λ_c), 1.04 mm |

  The peak is above the cell's mean level, the tip radius 2 over the curvature at the apex.
  25 records a fluid, five or six of them on the unstable branch; every one converged
  (three of EMG 901's and 909's only after the repair, 2026-10-20: the traps).

**The table** (`source/SpikeTable.cpp`, generated; `generate.py --stage table`). The
stable branch is resampled on 64 points uniform in v = √(u − u_fold). At each, the
solved cell becomes an isotropic kernel s(r), given by its transform: at each reciprocal
lattice shell up to √67 k_c = 8.19 k_c the kernel's transform is the cell's area times the
cell's Fourier amplitude there (the shell's orbits averaged), at q = 0 it's 0 with zero
slope (the pool's volume), a cubic spline in between, and a cosine taper from the last
shell kept to nothing exactly at the next, √73 k_c. A lattice's sum only ever samples the
transform at its shells, so a lattice of kernels is the solved cell exactly, band-limited
at 8.19 k_c. (The first taper ran 0.5 k_c past the 8 k_c shell, and the √67 shell inside
it took 0.7 of the 8 k_c value instead of its own.) Between the shells the spline is a
choice, and only a spike without its six neighbours sees it.
The 64 kernels are a rank-four basis: an SVD weighted as the camera reads it, each kernel
over its own largest value and each q by the lattice vectors its shell holds (which grow as
q). The fourth term leaves 8.9e-4 (EFH1), 6.4e-4 (EMG 901) and 4.0e-4 (EMG 909) of each
kernel's largest value, and a lattice of the table's kernels is the solver's cell to 0.19%,
0.20% and 0.06% of the peak at every solved point (`check.py`). Unweighted, the fold's
spike, a seventh of the top's, came out 0.9% wrong; rank eight gets 0.06%, at a second
transform of the deposit a frame, which wasn't worth tens of microns. Between the grid's
points, near the fold, the kernels are interpolated in A from the last record below the fold
(the traps). `inputs.sha256` and the header of `SpikeTable.cpp` carry the SHA-256 of the
generator's sources and the solved records, and `check.py` regenerates the file and
compares it byte for byte, so the table can't drift from what made it.

**Approximated (real time, every frame).**

1. **The pattern engine decides where and how big.** The spectral pool is unchanged: the
   linear theory exact, mode by mode, and the chosen local stress for the nonlinear part.
   It's now labelled for what it is, the pattern engine. Its two constants are no longer
   controls: they're fitted per fluid (`fetest --calibrate`) so that its saddle-node is the
   solver's fold and its spike height at onset is the solver's. The family (λHy, λ²Sa)
   keeps the saddle-node and scales the height as 1/λ, so the fit is two one-dimensional
   solves. Its growth, its lattice and where its spikes stand are the model's.
2. **Each of its spikes gets the solved spike.** A texel of the pool's surface (twice the
   pool's grid) is a spike if it's the highest within λ/4; a Newton step on the gradient
   places it inside the texel. It takes the field where it stands: in the Helmholtz pair
   I/I_c, over a coil the coil table's normal field there over the onset's. Its share is
   how far the pool's spike has grown towards the pool's own equilibrium at that field,
   clamped to [0, 1] (below the pool's fold, the fold's). It's deposited as a one-texel
   Gaussian carrying its share of the table's four basis coefficients at that field, the
   deposit is transformed, multiplied by the basis kernels' transforms with the Gaussian
   divided back out, and transformed back: the camera's surface and its gradient, in four
   passes and two transforms. At equilibrium, a lattice of the pool's spikes is the table's
   cell, which `--spikes` measures.
3. **What that costs.** The shape is always an equilibrium shape: growing and collapsing
   spikes are the equilibrium spike scaled by their share, not the dynamics' own shapes. The
   table is the hexagonal lattice's: a spike with all six neighbours is exact, a spike at a
   patch's edge or in a defect gets the kernel's isolated profile, which only the spline
   between the shells decides. Each spike takes one field, the one at its centre, as if the
   field were uniform across its cell, and only the normal component. Tips sharper than the
   camera's grid (λ/24, 0.41 mm for EFH1) and the kernel's 8.19 k_c come out rounder than the
   solver's: EFH1's tip at u 1.5 is 0.181 mm solved and 0.189 mm shipped. Past
   the table's last field the shape is clamped.

**Cost.** Five passes and two transforms of the camera's surface (1024 × 512 for every
fluid's pool) a frame, whatever the raster. Timed pass by pass with `GL_TIME_ELAPSED`
(2026-10-15, a scratch build, the machine's CPUs busy with the solver): finding the spikes 0.06 ms,
the deposit 0.5 ms, the forward transform 0.56 ms, the combine 0.007 ms and the inverse
0.6 ms, about 1.8 ms in all. The frame's total is in the README. The deposit is a 9 × 9
gather at every texel for a few hundred spikes, which is the obvious waste (open
questions).

## The derived stress, tried (2026-10-15; superseded the same day by the solved spikes)

Kept for the record: it's why the spikes are solved offline rather than derived in real
time. The review asked for the chosen stress to be replaced by one derived from the
magnetostatics: Craig & Sulem's Dirichlet–Neumann series on both sides of the interface,
the full curvature, and Langevin saturation. I did it in a CPU prototype,
`tools/research/ferrohydro/` (numpy, double; `checks.py` reproduces the numbers below). It
isn't shipped, and this is why.

- **The formulation.** Fluid below, air above. The trace ξ of the perturbation potential
  on the surface solves (G(−h) + r0 G(sh)) ξ = −M0 G(−h) h. The fluid is linearised about
  the flat state: μ_t along the field, μ_c across it, and the stretch s = √(μ_c/μ_t) turns
  its anisotropic Laplace equation into an isotropic one with the same r0 that
  Cowley–Rosensweig carry. The surface field follows from ξ and G(sh)ξ, and the stress
  μ0∫M dH + μ0 M_n²/2 from that field with the full Langevin curve. Its linear part is
  exactly G|D|h with the Cowley–Rosensweig G.
- **The exact operator, for ridges.** A Cauchy integral gives the exact DtN of a 1D
  surface (1.7e-13 against a harmonic function's own normal derivative). Against it, the
  series' stress at third order is off by 1.2% at ka 0.5, 3.8% at 0.7 and 8.9% at 0.9 for
  a single cosine. Exact EFH1 ridges sharpen fast: at ka 0.7 the crest's radius is 0.06 λ
  (0.23 λ for the cosine); third order gets 0.08 λ.
- **Third order, perturbative** (ξ to third order, the stress evaluated exactly from it).
  The shape is right: EFH1 hexagons at m 1.1 have straight conical flanks, a tip radius of
  0.89 mm (0.09 λ), 0.53 λ from peak to trough and 2.4 times the flat field at the tip.
  But it only gets there under 25 to 75 times the fluid's own damping. With EFH1's real
  viscosity it runs away even at m 1.05 (after 3 s), and at m 1.45 or more it runs away
  whatever the damping. Truncated, it isn't the gradient of an energy any more: at the
  spike its Jacobian's antisymmetric part is 0.61 of its symmetric part (2e-4 at a 4 µm
  pattern), and a stress that isn't a gradient can feed a damped pool. It also needs the
  surface low-passed (4 k_c) before the series, or the terms with k h ≫ 1 blow up within
  a millisecond.
- **Variational, quartic.** The magnetic energy is exactly −μ0 J*, J* = min over ξ of
  ½⟨ξ + M0 h, G(−h)(ξ + M0 h)⟩ + ½ r0⟨ξ, G(sh) ξ⟩: its gradient matches the exact
  stress of a linear medium to the finite differences' 4.5e-4. Expanded to quartic order
  (`quartic.py`: three coefficients, all from G, r0 and s) it matches the exact J* with an
  error of fifth order or better, and the stress it gives can't pump energy. For APG512a,
  Gollwitzer et al.'s fluid (χ0 1.17), it settles with the fluid's own viscosity and puts
  the pattern's amplitude at +4.2% of their fit at ε 0.1, −3.8% at 0.2, −9.8% at 0.3,
  −12.4% at 0.35 and −16.6% at 0.44; it collapses between ε −0.02 and −0.03 from 1.2 mm
  (they measured about −0.02, their finite elements −0.007, from about 1 mm). But it runs
  away for EFH1 at every m tried (1.05 to 2.5), and for EMG 909, EMG 901 and APG512a
  above m 1.2 to 1.5. That matches Gollwitzer et al. and Friedrichs & Engel: the weakly
  nonlinear expansion holds up to χ0 of about 1.05.
- **What stops a real spike** is the magnetisation saturating inside the fluid near its
  tip, which no linear DtN operator carries; Gollwitzer et al. found the measured
  magnetisation curve essential. That needs the full nonlinear magnetostatics, which they
  solved by finite elements (400 000 unknowns for one cell, offline). A spectral method
  can't carry it. A body-fitted 3D multigrid on the GPU might, at several milliseconds
  per V-cycle on half the pool and a cycle per substep. Not attempted.
- **The petals** round each dome in the hero are the neighbours' reflections, not the
  surface: the height's azimuthal harmonics of order 3 and up stay under 0.2% of a spike's
  height (order 6 at 0.3 λ: 0.007 mm of 0.96), and with the second bounce switched off the
  petals vanish. On a dome six neighbours fill a wide angle; on a cone they'd be slivers.
  The fix is the shape, not the renderer.

So the stress stayed the chosen one; the domes went when the spikes were solved (above).

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
past onset held steady over a coil it doesn't. Open. With the fitted constants
(2026-10-20, `--coilspikes` reports it), held for ten seconds over the default coil at
1.125 or 1.3 I_c, the patch is rolls whether the current steps on (a 1 ms coil) or rises
through the default 40 ms. Before the plan's slope margin went from 1.2 to 1.5 (a 20%
longer substep), 1.3 I_c through 40 ms gave hexagons: the choice is marginal there, and
the time step tips it. A roll drawn with solved spikes is a row of them along its crest
(the detector takes the highest texel within λ/4, which a crest offers every quarter
wavelength or so), which no real roll looks like. Biased under onset and kicked on the
beat, the default still comes up as a small hexagonal patch.

**The closed-form three-mode reduction disagreed with the solver.** The truncation to fourth
order in the amplitude gave coefficients 12 and 3 (36 and 12 in the W form) and missed the
GPU's harmonics by a factor of two to thirteen: the cubic drives the harmonics at A³ Sa, the
same order as the quadratic's A² Hy wherever A ~ Hy/Sa, which is everywhere on the branch.
The reduction is now exact: the model's energy averaged over one hexagonal cell (48 × 48
samples) for three modes at k_c with their six √3 k_c and six 2 k_c harmonics, minimised by
Newton, and followed down to its saddle-node by continuation. It matches the solver within a
sweep step (A 0.0457 predicted against 0.0465 measured). The family (λHy, λ²Sa) has the same
saddle-node exactly, which is what makes the second hysteresis case a check.

**One-sided differences near a fold** (the solved spikes). Both solvers build the
surface's Jacobian columns by differences. Near the bifurcation and the fold the scaled
Jacobian's condition number is about 1e8, and the hexagon's residual carries its inner
field solve's own error (about 1e-11 of it). One-sided differences at 1e-7 wavelengths
left the circle's Newton converging linearly (rate 0.5 to 0.8: twenty steps where five
do, and a continuation that grew its step only below six crawled at micrometres a step:
EMG 909 ran out of points at u 1.33) and stalled the hexagon's short of its tolerance at
two of EMG 901's and 909's records. Central differences at 1e-5 wavelengths fixed both;
`generate.py --stage repair` re-solves records that stopped short; it was only run on
2026-10-20, when three were still marked failed (EMG 901's at A 0.00048, EMG 909's at
0.00049 and 0.00359), and all three converged in 4 to 9 iterations. Testing the circle's Jacobian against full
differences first said its potential block was 40% wrong: the test's step was in the
residual's scale, not the potential's. In the potential's own units (H_a λ) it agrees to
2e-9.

**The circle stops at u 1.56 for EFH1.** Past it the residual has a floor (6e-8, scaled)
that rises a hundredfold in one 0.05 mm step: it sits in the fluid a wavelength below the
tip's flank, alternating in sign from one radial column to the next, where the mapped
grid's elements are 20 to 1 and sheared by a slope of 3. A grid mode, not the fluid. A
looser tolerance gets two more steps. Since the circle only seeds the hexagon, EFH1's
table ends there (above).

**The circle's kernel fit was ill-posed.** Fitting the kernel's transform to the
axisymmetric profile (B-splines in q, through the Hankel transform) gave huge cancelling
coefficients. The hexagon's Fourier coefficients give the transform at the shells
directly, which is all a lattice ever samples, so the table is built from those.

**Every local maximum isn't a spike.** The first detection took any texel above its
eight neighbours and found 1188 to 3664 spikes on a lattice of 396: ripples on the
flanks. A spike is the highest texel within λ/4 now, and `--spikes` checks its lattice in
a frame where the pattern engine's stress is held off, so the lattice is three cosines
with its maxima exactly on the lattice.

**A tip radius out of the picture means nothing below a texel.** The camera's grid is
λ/24 and EFH1's tip is a sixth of that at u 1.5, so `--spikes` compares the width at
half height and the peak-to-corner height out of the picture, and only reports the tip
radius (shipped and solved).

**`%.9gf` writes `0f`** for a zero, which C++ reads as an octal literal: `%.9ef`.

**The table as first committed was a stand-in.** The builder stopped before the table
stage ran: `SpikeTable.cpp` had EMG 909 as a copy of EFH1, a linear ramp for every
equilibrium, Hy 1 and Sa 0.5, and no `inputs.sha256`. Generating it for real (2026-10-20)
turned up the rest of this list.

**numpy 2 writes a float64 as `np.float64(2.87)`.** `generate.py` passed `repr()` of one to
`fetest --calibrate`; `atof` read 0, the height scale came out infinite and the JSON had
`inf` in it. Everything going to a command line is `float()`ed first.

**The three-mode reduction lost the branch at m 2.05.** Settled cold at m from a guess of
A = Hy / 2 Sa, EMG 909's fitted constants (Hy/Sa 0.63) found no minimum while the branch
ran on smoothly, and the blend's equilibrium went to 0 above u 1.608. `ThreeMode` now
follows the branch from 1.02 up as well as down, in steps of 0.01; where both found it, the
answers are the same.

**The table's first point clamped to the first record above the fold.** The kernels were
interpolated in A over the records at or above the fold, and the fold's own A lies between
two records, so the grid's v = 0 got the next record's kernel: the record nearest the fold
was 0.9% off. They're interpolated from the last record below the fold now (the branch is
smooth in A through it; only u turns).

**check.py compared two hexagons 30° apart.** Its reciprocal lattice had k_c along x
(spikes at 30°, 90°, 150°, fetest's frame), the solver's cell and its sample points have
spikes along x. At the summit the two agree, everywhere else they don't: 35% of the peak
for EMG 909. The lattice is in the cell's frame now.

**quiet() sets the pattern engine to Hy 0, Sa 2, and the spike checks never undid it.**
With no fold, that model's equilibrium at onset is nothing, so every spike was clamped
fully grown: `--spikes` at u 1 passed by accident and `--blend` failed by a factor of five.
The lattice and coil rigs clear the override now. The rolls report also never selected the
coil (quiet() leaves Uniform), so it was measuring the Helmholtz pair.

**The first frame has no time.** `dt` is 0 until a frame has gone by, so the second frame
is the one that brings a 1 ms coil up from cold, and a lattice loaded before it sat under
onset for three substeps and lost a quarter of its height. `latticeRig` renders two frames
first. Its seed was also half what it said: `latticeSeed` makes each mode A cos (3 A at a
spike, `singleMode`'s convention) and was handed the peak over 6 (Hexagons' 2 A cos).

**The plan's slope margin was 1.2 Hy/Sa; its own comment said 1.5.** The old constants'
Hy/Sa of 2 hid it. With the fitted ones (EMG 901 1.7, EMG 909 0.6), a quarter of the
viscosity and Noise at its top went to NaN within half a second at 1.5 I_c: `--extremes`
caught EMG 901, and EMG 909 did the same by hand. At 1.5 both hold, at a few more substeps
(EFH1 at the ceiling 26 to 31, Three Coils 36). `--extremes`'s own bound assumed a spike
about Hy/Sa tall, which is true near onset only: it's the larger of that and the pattern
engine's equilibrium at the corner's m now.

**verify.sh hung in glslc.sh, at 0% CPU, in a sandboxed Claude session** (2026-10-20).
bash 5.2 deadlocked on the 3 KB heredoc: its child held both ends of a pipe and python
never started. A plain 5 KB `cat <<EOF` script did the same with the session's own
`TMPDIR`, and neither did with `TMPDIR` set to the session's scratch directory. Why the
default fails wasn't found; nothing in the repo needs changing for it.

**Spawned workers import the solver as it is when they start.** An edit to `generate.py`
during a run changed the workers spawned after it. Don't edit the solver's sources while
a solve runs. And keep each worker to one thread (`VECLIB_MAXIMUM_THREADS`: numpy and
scipy here are on Accelerate, so `OPENBLAS_NUM_THREADS` alone does nothing): four fluids
at four workers each is already all sixteen cores.

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
  `--resize`, `--state`, `--extremes`);
- the solved spikes: the solver against Gollwitzer et al.'s measurements and against
  itself at other resolutions, the shipped table against the solver and against its own
  sources byte for byte (`tools/spikes/check.py`, `convergence.py`); at equilibrium a
  lattice of the pool's spikes is the table's cell, from the camera's height field and from
  the picture (`--spikes`); a part-grown spike is its share (`--blend`); over a coil the
  tallest spike stands on the axis and the heights fall with the field (`--coilspikes`); the
  pattern engine's fold and onset height are the solver's (`--calibration`).

Assumed, and said so:

- the pattern engine's nonlinear stress (a chosen local energy; its two constants fitted to
  the solver's fold and onset height, not derived), so how the pattern grows and where its
  spikes stand;
- that each spike is the hexagonal lattice's equilibrium spike at the field at its centre,
  scaled by how far it has grown (the solved spikes' approximations, above);
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
- `--extremes`: finite, and the tallest spike under 1.5 times the larger of (1 + Hy/Sa)/k_c
  and the pattern engine's own equilibrium peak at the corner's m (the fitted constants put
  that well above Hy/Sa at the ceiling).
- `--spikes`: every texel within a lattice constant of a spike against the table's cell
  (computed in double from the table) to 2e-3 of the peak, both rasters; out of the
  picture (1280 × 720, straight down), the summit to 0.5% and the peak-to-corner height and
  the width at half height to 1%: worst 0.16%, 0.30% and 0.25% over the three fluids at
  1, 1.25 and 1.5 I_c (2026-10-20).
- `--blend`: at onset, where the lattice neither grows nor decays: the lattice stands at its
  seeded share to 1%, and each spike's height is that share of the table's to 3e-3, both
  rasters.
- `--coilspikes`: an ordering (tallest within λ_c of the axis, at most 10% of pairs half a
  wavelength apart in radius inverted), both rasters.
- `--calibration`: no GL; double.
- `tools/spikes/check.py`: CPU, double; byte-for-byte for the table, 1e-9 m for a record's
  residual, the Gollwitzer bounds argued in the script.

All of the picture checks run at 1280 × 720 and 320 × 180 on this Mac's GPU, and fft,
mirror, fresnel and state again on Apple's software renderer at 320 × 180.

## The negative controls and mutants

`fetest --negative`: 21 wrong models, each must fail its check.

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
| spikes | every spike the fold's, whatever the field it stands in |
| blend | every spike placed fully grown |
| coilspikes | every spike given the field on its coil's axis |
| calibration | the pattern engine left at the old defaults, Hy 1 and Sa 0.5 |

`tools/mutate.sh`: 17 one-character mutants, each must fail its named check.

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
| GLSL: the deposit's Gaussian multiplied in, not divided out | `--spikes` |
| GLSL: a spike's share its height times its equilibrium's | `--blend` |
| C++: Cowley–Rosensweig's r0/(2 + r0) | `--dispersion` |
| C++: the demagnetising jump with the wrong sign | `--onset` |
| C++: the loop's elliptic formula with + r² | `--coil` |
| C++: the L/R lag's exponent dt·τ | `--coil` |
| C++: the beat's place in its frame from the sum of phases | `--beat` |
| C++: the first frame primed with a flux of 1 | `--audio` |
| C++: Coil Spread back to 30 cm, past the pool | `--fit` |

## The sweep's honest list

`python3 tools/sweep.py` sweeps all 20 controls of each plugin that have pixels (the About
block's four and the Audio buffer have none; Audio is driven through Band and Threshold) and
finds every one live. `--bare` drops the contexts and finds these dead, each for the reason
its context exists:

- **Band** and **Threshold**, both plugins: they act on the audio, and with no music in the
  buffer there's nothing for them to act on.
- **Coil Spread**, both plugins: it places the three coils, and only Three Coils has three.

## Coil Spread stops at 10 cm (2026-10-28)

It went to 30 cm until the guide-writer found the far coil's patch cut off in a straight line
past about 12 cm (seen straight down at Field Width 40 cm): the pool is periodic and fixed
per fluid (EMG 901's is 376 × 188 mm), and the coils' field isn't, so a coil the triangle
puts near the pool's edge has its patch wrapped onto the far side, where its field is weak.
`fetest --fit` puts each fluid at the top of the Spread and of the Bias, with the default
coil and the shipped model, and asks for every coil's patch with no spike within half a
wavelength of the edge. At 12 cm EMG 901's nearest spike stood 2.6 mm from it, so 10 cm
(8.4 mm); at 30 cm the top coil had no patch at all. A bigger pool was the other fix and
costs every frame. Narrowing a range was free before the first release; after it, Arena's
saved compositions would read the old values as new distances. A Coil Size near its 8 cm
top can still reach the edge at 10 cm: the check holds the default coil.
- **Coil Size**, the Over: the Over sits on a Helmholtz pair (Uniform), where no coil has a
  size. On the source, whose default magnet is a coil, it's live bare.

## The browser demo (2026-10-28)

`demo/` is <https://ferrofluid-demo.stoatworks-labs.com>, built to the fleet's
`resolume-demo` kit rules (`~/Projects/resolume/specs/DEMO-BRIEF.md`) by a sub-agent, from
slipstream's demo. Deployed by hand first on 2026-10-28, then by `deploy.yml` on every push to
main. What a reader of it must know:

- **The shaders are the plugin's**, all eleven pieces plus kVersion, spliced by
  `demo/tools/check_shaders.py --write` into `demo/shaders.js` and compared character for
  character. It also fails on a piece it doesn't list and on a change to the join on either
  side (`Assemble()`'s switch, InitGL's vertex stage and its table of nine, and plugin.js's
  copy). The version line is left to the kit's `port()`, as in every demo. **All nine
  programs compile and link in WebGL2 as spliced** (Chrome, ANGLE on Metal, M4 Max), and no
  uniform the page sets is missing from its program.
- **The spike table is the plugin's data.** The same script splices the arrays the plugin
  uploads (kEq, kCoef and kBasis for each fluid) and the 14 fields of each `kTables` row the
  frame reads into `demo/spiketable.js`, and compares them as text, the C++ `f` dropped. It
  also checks each row's 23 fields against SpikeTable.h's order, and proves in rationals
  that every literal rounds to the same float through a double (the page's path) as
  straight to a float (clang's): they're 10-digit prints of doubles, so a midpoint is
  possible in principle; none of the 7,680 is on one.
- **The CPU half is a port, and only a reader checks it.** Between plugin.js's PORT markers:
  Controls.cpp, Fluid.cpp (not ThreeMode, SaddleNode or PoolEquilibrium, which only a
  harness override of the fitted constants reaches), Coil.cpp, Drive.cpp, Spikes.cpp's
  TableFor; between the FRAME markers, Ferrofluid.cpp's ProcessOpenGL and everything it
  calls. Not ported: Audio.cpp (with silence in the buffer every band level is
  0 / max( 0.02, peak ) = 0 and it never fires, so the page hands the drive exactly that),
  the clock's seconds-or-milliseconds vote (the page's unit is known), Diag, the test hooks.
  Nothing in the repo runs the port against the C++.
- **Compared with fetest once, by hand (2026-10-28).** A fresh instance driven in the page
  as `fetest`'s Rig drives the plugin (frame f at f/60 s, `SetBeatInfo( 120, fmod( t 120 /
  240, 1 ) )`, a float framebuffer, quantised as `writePng` does), against
  `fetest --out` built from this tree, at 1280 x 720:

  | settings | the page's summary against fetest's | pixels identical | within 1 level | over 32 levels |
  | --- | --- | --- | --- | --- |
  | the defaults, 180 frames | 12 substeps of 1.389 ms, 0.980 Ic (Ic 3.52 A), m 0.961: the same | 83.2% | 96.2% | 0.075% |
  | the README's first picture, 420 frames | 12 substeps of 1.389 ms, 1.050 Ic (Ic 12.02 A), m 1.101: the same | 99.4% | 99.92% | 0.013% (121 px) |

  In the settled lattice what differs is glints on the spikes' edges. In the default's
  growing patch a few spikes also stand a little higher or lower: the pool is an
  instability, and the two shader compilers (Apple's GL, ANGLE's Metal) don't round alike.
  Nothing repeats this; a change to the frame logic needs it done again.
- **The clock governor is the page's, not the plugin's.** A frame at 60 fps is a dozen
  substeps of twelve passes and 27 passes more. A browser that can't draw that in real
  time spirals: longer frames, more substeps each, then the plugin's cap of 48 and dropped
  time at a few frames a second (seen once, headless, before the governor existed:
  switching to Three Coils at 1.08 Ic straight after loading sat at 48 substeps and 13 fps).
  So `hostTimeFor()` slows the host time the plugin and the transport see while frames come
  slower than 20 a second and the plan is still running more than six substeps a frame,
  and lets it back to real time above 30 a second or at four substeps or fewer. On the M4
  Max the defaults run at 60 fps and full speed, 1080p at about 41, and a 4K composition of
  Three Coils past onset settles at about 25 fps and 12% of real time. The disclosure says
  so and the status line gives the speed. Nothing about the pool, the plan or the passes
  changes.
- **The page's decisions.** Audio, Band and Threshold are left off the panel (bassalt's,
  cadence's and vocoder's rule); Fire On keeps Onset, which never fires. Fire and Settle are
  toggles released after one frame. The beat is a Host BPM clock (120 at first) whose bar
  phase is accumulated from the page's seconds and sent through the port of SetBeatInfo, as
  `fetest` and zoetrope's page do. The Plugin dropdown is a new instance with that plugin's
  constructor defaults (the Over's Uniform and Tilt 0). The source binds the coil table to
  the clip's unit, as the plugin does; MaxUV is 1, the kit's clip being exactly its size.
- **It needs WebGL2 with `EXT_color_buffer_float` and `OES_texture_float_linear`** (the
  camera's surface is RGBA32F read through `texture()`), and stops with a message without
  either rather than show a flat pool.
- **Checking it.** Serve `demo/`, load it headlessly with
  `stoatworks-backend/release/cdpshot.py` (Chrome's `--use-angle=metal` gets the GPU
  headless on this Mac; SwiftShader is far too slow for the pool), and drive it through
  `window.ferrofluidDemo`: `renderer.newInstance( isEffect )` gives a plugin the page can
  run frame by frame into its own framebuffer, as above.
- **Deploying.** A Worker ROUTE, not a custom domain (the zone is full): the proxied AAAA
  100:: record for `ferrofluid-demo` has to exist before the first deploy, and
  `deploy.yml` needs the `CLOUDFLARE_API_TOKEN` secret and `CLOUDFLARE_ACCOUNT_ID` variable.

## Open questions

- **The dynamics between equilibria.** A growing or collapsing spike is the equilibrium
  spike scaled by its share; the real one passes through other shapes, and the pattern
  engine's growth past the linear stage is the chosen model's. Solving the dynamics
  offline (a few spikes' worth of time-dependent free surface) would say how wrong that
  looks.
- **Spikes without six neighbours.** The kernel between the lattice's shells is a spline:
  an isolated spike, a patch's edge or a defect gets that, not a solved shape. A second
  table (the isolated spike, or the patch's edge over a coil, solved in the circle) would
  pin it.
- **The field gradient across a spike.** Each spike takes the field at its centre. Over a
  coil the field falls across a cell; the shapes there are the uniform field's.
- **EFH1 past u 1.56.** The circle's grid gives out under the sharpest tips (the traps);
  a body-fitted grid round the tip, or a narrower mapping, would carry the table to 1.62.
- **Rolls over a coil past onset** (the traps): still open with the fitted constants, and
  marginal: the substep can tip it. Drawn with solved spikes, a roll is a row of spikes.
  Hexagons need the pattern engine's quadratic to win at the front; a quadratic fitted to
  the fold alone may simply be too weak there.
- **Cost.** The spike stage is about 1.8 ms of the frame: the deposit (a 9 × 9 gather at
  every texel, 0.5 ms) could be point sprites with additive blending, and detecting on the
  pool's own grid would save most of one camera-sized transform (the pool's own surface is
  then only needed at its own resolution).
- **The tangential field** at a coil's edge, which stretches real spikes into ridges.
- **Finite depth** and the dish's wall: the pool is deep and periodic.
- **The pool's cost.** About 150 passes a frame; fusing the transform's passes, or fewer
  substeps at low drive, would be the place to start.
- **Never seen in a host.** How Fire On follows Arena's transport, how the controls read in
  the inspector, what the audio does with real music.
