# Attributions

Ferrofluid is built on other people's work. This file lists what that work is, who did
it, and what it is doing here.

**Provisional hand copy (2026-10-15).** The fleet's attributions files are generated: the
master lists live in the `stoatworks-backend` repo and are pushed out by
`scripts/sync-attributions.py`. Ferrofluid isn't registered there yet, so this one was
written by hand in the same shape. Once it is registered, the generated file replaces
this one and the entries below move into the backend's lists.

## Code derived from other people's work

Someone else solved this first, and this project would not exist in its current form without their work.

### The harness, the tooling and the effect's shape — Stoatworks schlieren and fiche

<https://github.com/stoatworks-labs/schlieren>  
Licence: MIT  
Copyright: Stoatworks Labs

The harness's rig, PNG writer, look card, cue sheets, `--pipe`/`--film` (with SIGPIPE ignored
and a closed stdout an exit of 1), `--negative` with its Perturb, `--state`, `--names`,
`--cues` and `--offline`; `tools/verify.sh`, `tools/mutate.sh` (with its pipefail fix),
`tools/glslc.sh`, `tools/sweep.py`, the CI workflow and the idea of placing a beat at the
moment inside the frame where it fell (`source/Drive.cpp`). Schlieren had them from fiche.
All adapted.

### Two plugins from one core, and the control table's shape — Stoatworks honeydew

<https://github.com/stoatworks-labs/honeydew>  
Licence: MIT  
Copyright: Stoatworks Labs

`CMakeLists.txt`'s two bundles from one OBJECT library, the release workflow's two-bundle
packaging, `scripts/release-lib.sh`, a host order per plugin with the About block last
(`source/Controls.cpp`), and the harness's rule that an option is set by its name.

### The audio analyser, the pass buffers and the log — Stoatworks millpond, schlieren and abomerration

<https://github.com/stoatworks-labs/millpond>  
Licence: MIT  
Copyright: Stoatworks Labs

`source/Audio.{h,cpp}` is schlieren's onset analyser, which is millpond's, which is
rosette's, adapted from macroblock's, with its primed first frame; its Low, Mid and High
bands are abomerration's split. `source/PassBuffer.{h,cpp}` and `source/Diag.{h,cpp}` are
millpond's. `source/GLState.h` is radar's, which is vectrix's, from `resolume-scopes`.

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl.

The plugin ABI itself. An FFGL effect or source is defined by this SDK's headers — there is no other way to be loadable by Resolume Arena and Avenue.

### GLEW — the OpenGL Extension Wrangler Library

<https://github.com/nigels-com/glew>  
Licence: BSD-3-Clause (with Mesa 3-D and Khronos components)  
Copyright: Milan Ikits, Marcelo E. Magallon and Lev Povalahev

Arrives inside the FFGL submodule at external/ffgl/deps/glew-2.1.0. Not fetched separately.

Resolves OpenGL entry points on Windows, where the system headers stop at OpenGL 1.1.

### libpng

<http://www.libpng.org/pub/png/libpng.html>  
Licence: PNG Reference Library License (libpng)  
Copyright: the PNG Reference Library authors

Arrives inside the FFGL submodule, under the SDK's CustomThumbnail sample.

Part of the upstream SDK tree rather than something these plugins call directly — listed because it is present in the checkout.

## Work checked against

No code was taken from these — but they were how I knew I had it right, or where the
numbers came from.

### The normal-field instability — M. D. Cowley and R. E. Rosensweig, J. Fluid Mech. 30 (1967) 671; R. E. Rosensweig, Ferrohydrodynamics (Cambridge University Press, 1985), ch. 7

The dispersion relation ρω² = ρgk + σk³ − μ0M²(r0/(1 + r0))k², r0 = √(μc μt)/μ0, the
critical wavenumber kc = √(ρg/σ) and the onset condition μ0M²r0/(1 + r0) = 2√(ρgσ), in
`source/Fluid.cpp` and the evolve pass in `source/Shaders.cpp`. `fetest --dispersion` holds
every mode to it and `--onset` puts the threshold where it says.

### The EMG 901 and EMG 909 datasheets, and a published critical field — T. Mahr and I. Rehberg, Physica D 111 (1998) 335

The density, surface tension, viscosity, susceptibility and saturation of both fluids
(their section 2), used whole in `source/Fluid.cpp`; and their mixture's critical field
(5.9 kA/m inside the fluid, λc = 9 mm), which `fetest --onset` reproduces from their
numbers by the same theory.

### Ferrotec's EFH1 technical data sheet (EFH1 092110, 2010)

The density, surface tension, viscosity, initial susceptibility and saturation of EFH1, in
`source/Fluid.cpp`. Ferrotec's current web page gives χ = 2.64 and no surface tension, so
the 2010 sheet is used whole.

### The weakly nonlinear theory of the hexagons — A. Gailitis, J. Fluid Mech. 82 (1977) 401; R. Friedrichs and A. Engel, Phys. Rev. E 64 (2001) 021406

Named because they are what the nonlinear stress should have been derived from, and
wasn't. The model's quadratic and cubic stress (`source/Fluid.h`) are chosen constants,
exposed as Hysteresis and Saturation; AGENTS.md says why and what that costs.

### The Dirichlet–Neumann operator series — W. Craig and C. Sulem, J. Comput. Phys. 108 (1993) 73

The series G(h) = G_0 + G_1 + G_2 + ... and its recursion, used in the derived-stress
prototype under `tools/research/ferrohydro/` (not shipped; AGENTS.md says why), on both
sides of the interface.

### The measured surface of a magnetic fluid — C. Gollwitzer, G. Matthies, R. Richter, I. Rehberg and L. Tobiska, J. Fluid Mech. 571 (2007) 455

Their fluid's properties (APG512a, table 1) and the fit of their measured pattern
amplitudes (eq. 4.1), against which the prototype's quartic model was compared; and their
finding that the measured magnetisation curve is essential and the weakly nonlinear
expansion fails above χ0 of about 1.05. Read from the arXiv preprint nlin/0604009.

### The Langevin magnetisation — Paul Langevin

M(H) = Ms L(3χH/Ms), L(x) = coth x − 1/x, in `source/Fluid.cpp`, with its series below
|x| = 0.01. The harness carries its own copy and differentiates it numerically, so a wrong
tangent permeability in the plugin can't be checked against itself.

### The damping of surface waves — H. Lamb, Hydrodynamics (6th ed., 1932), §348

The weak-damping rate 2νk² of each mode, in `source/Fluid.cpp` and the evolve pass.

### The field of a circular loop — J. Simpson, J. Lane, C. Immer and R. Youngquist, NASA/TM-2013-217919

The off-axis field of one loop in complete elliptic integrals, in `source/Coil.cpp`. `fetest
--coil` checks it against Biot–Savart summed round the loop by hand, and the solenoid's
table against 400 turns summed one by one.

### Complete elliptic integrals by the arithmetic–geometric mean — M. Abramowitz and I. A. Stegun, Handbook of Mathematical Functions, 17.6 and 17.3

K and E by the AGM in `source/Coil.cpp`; `fetest --coil` checks K(1/2) and E(1/2) against
the tables.

### A mixed-radix Stockham FFT on a GPU — N. K. Govindaraju, B. Lloyd, Y. Dotsenko, B. Smith and J. Manferdelli, "High performance discrete Fourier transforms on graphics processors", SC 2008; T. G. Stockham

The radix-16, 8, 4, 2 autosort transform in the fft pass of `source/Shaders.cpp`, one output
per fragment, its twiddles from tables built in double. `fetest --fft` holds it to an
O(N²) DFT in double.

### Effective optical constants — J. C. Maxwell Garnett, Phil. Trans. R. Soc. A 203 (1904) 385

The fluid's complex refractive index as magnetite spheres in a mineral oil, in
`OpticsOf()` in `source/Fluid.cpp`. The constants fed to it are assumed, not measured
(AGENTS.md).

## Standards and published specifications

What the implementation is measured against.

- **CODATA 2018** — μ0 = 1.25663706212e-6 H/m and standard gravity 9.80665 m/s², in `source/Fluid.h`.
- **The Fresnel equations for an absorbing medium** — the unpolarised reflectance of N = n + ik at any incidence, exact, in `Reflectance()` in `source/Fluid.cpp` and the render pass; `fetest --fresnel` checks the picture against it.
- **Helmholtz coils** — the uniform field (4/5)^(3/2) NI/R of the Uniform magnet, in `source/Coil.cpp`.
- **Gauss–Legendre quadrature** — the solenoid's winding integrated over its length, 32 points, in `source/Coil.cpp`.
- **IEC 61966-2-1 (sRGB)** — `toLinear()` and `toCode()` in `source/Shaders.cpp`: the clip is linearised so the mirror reflects light, then encoded again.

## Inspirations

What this set out to be. No code, assets or binaries from any of these were used or examined — the debt is to the idea.

### Photographs of ferrofluid spikes

The familiar photograph of spiked ferrofluid in a dish over a magnet, glossy black under a
studio's soft boxes. Built from the physics; nothing is copied from anyone's software,
images or design.

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or you would rather not be listed — open an issue and it will be fixed.
