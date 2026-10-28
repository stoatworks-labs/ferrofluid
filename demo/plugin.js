/**
 * Ferrofluid — browser demo.
 *
 * A pool of ferrofluid over an electromagnet whose current is the music, seen
 * as the black mirror it is. SW Ferrofluid (FE01, a source) is the pool in a
 * photographer's studio; SW Ferro Over (FE02, an effect) is the same pool with
 * the clip on a screen hung above it, broken up in its mirror. Nothing is
 * drawn: past a critical field the flat surface stands up in spikes on a
 * hexagonal lattice (the Rosensweig instability), and the camera sees what the
 * spikes reflect.
 *
 * Two halves, and they are not equally faithful here:
 *
 *   **The GPU half is the plugin's own GLSL.** `shaders.js` is the eleven
 *   `R"( ... )"` pieces of `source/Shaders.cpp` plus kVersion, spliced by
 *   `demo/tools/check_shaders.py --write` and never typed; the same script
 *   compares them character for character and checks that they are joined as
 *   `Assemble()` and InitGL join them, and `tools/verify.sh` runs it. They run
 *   as the plugin's passes, on the plugin's buffers: a 512 x 256 RGBA32F
 *   spectral pool, a Stockham FFT of radix 16, 8, 4 and 2 with its twiddles
 *   from double-precision tables, the force and evolve passes once a substep,
 *   and once a frame the pack, the spikes, their deposit and combine, the
 *   bounds and the render, on the camera's 1024 x 512 surface.
 *
 *   **The spike table is the plugin's data.** `spiketable.js` is the part of
 *   `source/SpikeTable.cpp` (generated offline by tools/spikes/ from the
 *   solved spikes) that the plugin uploads to the GPU, spliced by the same
 *   script and compared literal for literal.
 *
 *   **The CPU half is a port.** Between the PORT markers below: Controls.cpp
 *   (every mapping and default), Fluid.cpp (the datasheets, the Langevin
 *   magnetisation with its demagnetising jump, the critical field and
 *   wavenumber, the optics), Coil.cpp (the loop's field by the AGM, the
 *   solenoid's Gauss-Legendre sum, the Helmholtz pair, the L/R lag), Drive.cpp
 *   (the beat, the pulses, the current) and Spikes.cpp's TableFor. Between the
 *   FRAME markers: Ferrofluid.cpp's ProcessOpenGL and everything it calls on
 *   the CPU -- the pool, the coil and fluid tables, the critical current, the
 *   substep plan, every pass and every uniform, the camera and the soft boxes.
 *   JavaScript's numbers are doubles, as the plugin's CPU half is. **Nothing
 *   in the repository holds this port to the C++**: only a reader does, and
 *   the page says so. It was compared with the plugin's harness by hand, once
 *   (AGENTS.md has the numbers).
 *
 * ---------------------------------------------------------------------------
 * Decisions this page made, and why
 * ---------------------------------------------------------------------------
 *
 * **Both plugins, one page.** One class with a flag in the plugin. The Over
 * declares Height and Mix where the source declares Lights and Backdrop, and
 * its constructor puts the pool in the Helmholtz pair (Magnet Uniform) and the
 * camera straight down (Tilt 0). The Plugin dropdown is a new instance, as it
 * would be in Resolume: a flat pool and that plugin's own defaults.
 * `?plugin=over` opens the effect; Copy link carries it.
 *
 * **No audio.** Audio is a Resolume FFT buffer, and a browser has nothing to
 * put in it. The plugin's Audio, Band and Threshold are absent from the panel
 * rather than present and dead (bassalt's, cadence's and vocoder's rule). The
 * removal is exact: with silence in the buffer the plugin's analyser
 * (Audio.cpp, not ported) reports every band level as 0 / max( 0.02, peak ) =
 * 0 and never fires an onset, so that is what this page hands the drive. Fire
 * On keeps its Onset element, because the list is the plugin's, and it never
 * fires here.
 *
 * **The transport is simulated, as fetest's is.** Fire On's default, Beat,
 * fires on Resolume's beat. The page keeps a BPM clock (the Host BPM field
 * under the picture, 120 at first) and hands the plugin that BPM and a bar
 * phase advanced from the page's own seconds every frame, through the port of
 * SetBeatInfo, as `fetest` does (zoetrope's page is the precedent). The phase
 * is accumulated, so a new BPM changes the rate from then on and never jumps
 * it; Restart puts it back to the top of the bar.
 *
 * **The clock.** The kit's: seconds from frame deltas, capped at a tenth of a
 * second a frame, handed over as the host's time already in seconds, so the
 * plugin's seconds-or-milliseconds vote is not ported. The first frame has no
 * time (dt 0), as in the plugin. Restart takes the clock back, which the
 * plugin clamps to no time passing; the pool carries on from where it was.
 *
 * **Events are toggles.** Fire and Settle are FF_TYPE_EVENT; the kit has no
 * event control, so they are toggles the page lets go of after one frame, one
 * rising edge each, which is what the plugin counts.
 *
 * **The pool and the plan are the plugin's; the clock may run slow.** Nothing
 * here is made smaller to keep a browser interactive: the same 512 x 256 pool,
 * the same plan of up to 48 substeps a frame. A browser that can't draw that
 * in real time would otherwise spiral down to the cap (slower frames, longer
 * frames, more substeps each) and drop time at a few frames a second. So the
 * renderer's clock governor (createRenderer, below) slows the host time the
 * plugin and the transport see while the frames come slower than 20 a second
 * and the plan is still running more than six substeps a frame, and lets it
 * back to real time above 30: the pool and the beat in slow motion together,
 * every frame still the plugin's for the time it is given. On the M4 Max this
 * was built on, the defaults (and Three Coils past onset) run at 60 fps and
 * full speed once the page has loaded; at a 4K composition it engages. The
 * disclosure says so and the status line gives the speed.
 *
 * **The harness's test hooks are not ported** (Hooks is 0 except where the
 * plugin itself sets kHookNoKick in repack); the About block is not a
 * parameter here, as on every page in the suite.
 */

import { mountDemo } from './vendor/demo.js';
import { Program, PassBuffer, GLError, bindTexture } from './vendor/gl.js';
import * as S from './shaders.js';
import { TABLES } from './spiketable.js';

// ==== BEGIN PORT ===========================================================
// Controls.h/.cpp, Fluid.h/.cpp, Coil.h/.cpp, Drive.h/.cpp and Spikes.cpp,
// ported. No DOM and no GL between the markers.

const f32 = Math.fround;
/** std::clamp, std::min and std::max exactly: comparisons, so -0 stays -0. */
const clamp = (v, lo, hi) => (v < lo ? lo : hi < v ? hi : v);
const min2 = (a, b) => (b < a ? b : a);
const max2 = (a, b) => (a < b ? b : a);
/** std::lround: half away from zero (Math.round alone rounds -2.5 to -2). */
const lround = (v) => (v < 0 ? -Math.round(-v) : Math.round(v));
const kPi = 3.14159265358979323846;

//---------------------------------------------------------------------------
// Controls.h: ParamIds, by what a control is; the table; the host orders.
//---------------------------------------------------------------------------

const PT = {
  FIRE_ON: 0, FIRE: 1, AUDIO: 2, BAND: 3, GAIN: 4, BIAS: 5, COIL_LAG: 6, THRESHOLD: 7, PULSE: 8,
  MAGNET: 9, COIL_SIZE: 10, COIL_SPREAD: 11,
  FLUID: 12, VISCOSITY: 13, NOISE: 14, SETTLE: 15,
  FIELD_WIDTH: 16, TILT: 17, HEIGHT: 18, EXPOSURE: 19, LIGHTS: 20, BACKDROP: 21, MIX: 22,
  ABOUT_TEXT: 23,
};

const FireOn = { Off: 0, Beat: 1, HalfBeat: 2, TwoBeats: 3, Bar: 4, Onset: 5, Count: 6 };
const Band = { Low: 0, Mid: 1, High: 2, Full: 3, Count: 4 };
const Magnet = { Uniform: 0, Coil: 1, ThreeCoils: 2, Count: 3 };
const Lights = { SoftBox: 0, TwoBoxes: 1, Strips: 2, Backlight: 3, Window: 4, Neon: 5, Rim: 6, Count: 7 };
const Backdrop = { Black: 0, Studio: 1, Count: 2 };

const FIRE_ON_NAMES = ['Off', 'Beat', 'Half Beat', '2 Beats', 'Bar', 'Onset'];
const BAND_NAMES = ['Low', 'Mid', 'High', 'Full'];
const MAGNET_NAMES = ['Uniform', 'Coil', 'Three Coils'];
const FLUID_NAMES = ['EFH1', 'EMG 901', 'EMG 909'];
const LIGHTS_NAMES = ['Soft Box', 'Two Boxes', 'Strips', 'Backlight', 'Window', 'Neon', 'Rim'];
const BACKDROP_NAMES = ['Black', 'Studio'];

const clamp01 = (v) => clamp(v, 0.0, 1.0);
const geometric = (v, low, high) => low * Math.pow(high / low, clamp01(v));
function inverseGeometric(value, low, high) {
  const lo = min2(low, high);
  const hi = max2(low, high);
  return f32(Math.log(clamp(value, lo, hi) / low) / Math.log(high / low));
}
const linear = (v, low, high) => low + (high - low) * clamp01(v);
const inverseLinear = (value, low, high) => f32(clamp((value - low) / (high - low), 0.0, 1.0));

const kCurrentHigh = 1.5;
const kLagLow = 1e-3;
const kLagHigh = 0.3;
const kPulseLow = 0.01;
const kPulseHigh = 1.0;
const kCoilLow = 0.005;
const kCoilHigh = 0.08;
const kSpreadLow = 0.02;
const kSpreadHigh = 0.30;
const kViscLow = 0.25;
const kViscHigh = 100.0;
const kNoiseLow = 1e-6;
const kNoiseHigh = 1e-1;
const kFieldLow = 0.02;
const kFieldHigh = 0.40;
const kTiltHigh = (60.0 * kPi) / 180.0;
const kHeightLow = 0.02;
const kHeightHigh = 1.0;
const kStops = 4.0;

/** Gain and Bias: 0 to 1.5 critical currents, linearly. */
const CurrentFromParam = (v) => linear(v, 0.0, kCurrentHigh);
const ParamFromCurrent = (m) => inverseLinear(m, 0.0, kCurrentHigh);
/** Coil Lag, L / R: 1 to 300 ms, geometrically. Seconds. */
const LagFromParam = (v) => geometric(v, kLagLow, kLagHigh);
const ParamFromLag = (s) => inverseGeometric(s, kLagLow, kLagHigh);
/** Pulse: 10 ms to 1 s, geometrically. Seconds. */
const PulseFromParam = (v) => geometric(v, kPulseLow, kPulseHigh);
const ParamFromPulse = (s) => inverseGeometric(s, kPulseLow, kPulseHigh);
/** Coil Size: the solenoid's radius, 5 mm to 8 cm, geometrically. Metres. */
const CoilSizeFromParam = (v) => geometric(v, kCoilLow, kCoilHigh);
const ParamFromCoilSize = (m) => inverseGeometric(m, kCoilLow, kCoilHigh);
/** Coil Spread: the triangle's side, 2 to 30 cm, geometrically. Metres. */
const SpreadFromParam = (v) => geometric(v, kSpreadLow, kSpreadHigh);
const ParamFromSpread = (m) => inverseGeometric(m, kSpreadLow, kSpreadHigh);
/** Viscosity: a multiple of the datasheet's, 0.25x to 100x, geometrically. */
const ViscosityFromParam = (v) => geometric(v, kViscLow, kViscHigh);
const ParamFromViscosity = (s) => inverseGeometric(s, kViscLow, kViscHigh);
/** Noise: exactly 0 at the bottom, then 1e-6 to 1e-1 Pa, geometrically. */
const NoiseFromParam = (v) => (v <= 0.0 ? 0.0 : geometric(v, kNoiseLow, kNoiseHigh));
const ParamFromNoise = (pa) => (pa <= 0.0 ? 0.0 : max2(f32(1e-6), inverseGeometric(pa, kNoiseLow, kNoiseHigh)));
/** Field Width: the frame's width at the pool, 2 to 40 cm, geometrically. Metres. */
const FieldWidthFromParam = (v) => geometric(v, kFieldLow, kFieldHigh);
const ParamFromFieldWidth = (m) => inverseGeometric(m, kFieldLow, kFieldHigh);
/** Tilt: 0 to 60 degrees, linearly. Radians. */
const TiltFromParam = (v) => linear(v, 0.0, kTiltHigh);
const ParamFromTilt = (r) => inverseLinear(r, 0.0, kTiltHigh);
/** Height: the Over's screen above the pool, 2 cm to 1 m, geometrically. Metres. */
const HeightFromParam = (v) => geometric(v, kHeightLow, kHeightHigh);
const ParamFromHeight = (m) => inverseGeometric(m, kHeightLow, kHeightHigh);
/** Exposure: -4 to +4 stops, linearly. */
const ExposureFromParam = (v) => linear(v, -kStops, kStops);
const ParamFromExposure = (s) => inverseLinear(s, -kStops, kStops);

const OptionIndex = (value, count) => clamp(lround(value), 0, count - 1);

/** The table, indexed by ParamId (Controls.cpp's kTable). */
const standard = (id, name, group, value) => ({ id, name, group, kind: 'standard', defaultValue: f32(value), options: null });
const option = (id, name, group, value, options) => ({ id, name, group, kind: 'option', defaultValue: f32(value), options });
const event = (id, name, group) => ({ id, name, group, kind: 'event', defaultValue: 0, options: null });
const INFO = [
  option(PT.FIRE_ON, 'Fire On', 'Drive', 1.0, FIRE_ON_NAMES),
  event(PT.FIRE, 'Fire', 'Drive'),
  { id: PT.AUDIO, name: 'Audio', group: 'Drive', kind: 'buffer', defaultValue: 0, options: null },
  option(PT.BAND, 'Band', 'Drive', 0.0, BAND_NAMES),
  standard(PT.GAIN, 'Gain', 'Drive', ParamFromCurrent(0.2)),
  standard(PT.BIAS, 'Bias', 'Drive', ParamFromCurrent(0.98)),
  standard(PT.COIL_LAG, 'Coil Lag', 'Drive', ParamFromLag(0.04)),
  standard(PT.THRESHOLD, 'Threshold', 'Drive', 0.4),
  standard(PT.PULSE, 'Pulse', 'Drive', ParamFromPulse(0.15)),

  option(PT.MAGNET, 'Magnet', 'Magnet', 1.0, MAGNET_NAMES),
  standard(PT.COIL_SIZE, 'Coil Size', 'Magnet', ParamFromCoilSize(0.04)),
  standard(PT.COIL_SPREAD, 'Coil Spread', 'Magnet', ParamFromSpread(0.08)),

  option(PT.FLUID, 'Fluid', 'Fluid', 0.0, FLUID_NAMES),
  standard(PT.VISCOSITY, 'Viscosity', 'Fluid', ParamFromViscosity(1.0)),
  standard(PT.NOISE, 'Noise', 'Fluid', ParamFromNoise(1e-3)),
  event(PT.SETTLE, 'Settle', 'Fluid'),

  standard(PT.FIELD_WIDTH, 'Field Width', 'View', ParamFromFieldWidth(0.10)),
  standard(PT.TILT, 'Tilt', 'View', ParamFromTilt((35.0 * kPi) / 180.0)),
  standard(PT.HEIGHT, 'Height', 'View', ParamFromHeight(0.04)),
  standard(PT.EXPOSURE, 'Exposure', 'View', ParamFromExposure(0.0)),
  option(PT.LIGHTS, 'Lights', 'View', 6.0, LIGHTS_NAMES),
  option(PT.BACKDROP, 'Backdrop', 'View', 1.0, BACKDROP_NAMES),
  standard(PT.MIX, 'Mix', 'View', 1.0),
];
if (INFO.length !== PT.ABOUT_TEXT) throw new Error('every control needs a row in the table');

/** HostOrder(): the ids each plugin declares, in host order, the About block left off. */
function HostOrder(effect) {
  const order = [];
  for (let id = 0; id < PT.ABOUT_TEXT; id += 1) {
    const overOnly = id === PT.HEIGHT || id === PT.MIX;
    const sourceOnly = id === PT.LIGHTS || id === PT.BACKDROP;
    if ((effect && sourceOnly) || (!effect && overOnly)) continue;
    order.push(id);
  }
  return order;
}

/** The constructor's params[]: the table's defaults, and the Over's two of its own. */
function DefaultParams(effect) {
  const params = new Float32Array(PT.ABOUT_TEXT);
  for (let id = 0; id < PT.ABOUT_TEXT; id += 1) params[id] = INFO[id].defaultValue;
  if (effect) {
    params[PT.TILT] = 0.0;
    params[PT.MAGNET] = Magnet.Uniform;
  }
  return params;
}

//---------------------------------------------------------------------------
// Fluid.h / Fluid.cpp: the datasheets, the magnetisation, the linear theory,
// the optics. (ThreeMode, the pattern engine's own reduction, is only needed
// when the harness overrides the fitted constants: the plugin ships its
// equilibrium in the table, so it is not ported.)
//---------------------------------------------------------------------------

const kMu0 = 1.25663706212e-6;
const kGravity = 9.80665;

const DATASHEETS = [
  { name: 'EFH1', density: 1210.0, tension: 0.029, viscosity: 6.0e-3, chi: 2.58, saturation: 0.044 / kMu0 },
  { name: 'EMG 901', density: 1530.0, tension: 0.0295, viscosity: 10.0e-3, chi: 1.3, saturation: 4.8e4 },
  { name: 'EMG 909', density: 1020.0, tension: 0.0265, viscosity: 6.0e-3, chi: 0.8, saturation: 1.6e4 },
];
const FluidOf = (id) => DATASHEETS[clamp(id, 0, DATASHEETS.length - 1)];

const kMagnetiteSaturation = 4.46e5;
const kMagnetiteN = 2.4;
const kMagnetiteK = 0.6;
const kCarrierN = 1.47;

const sq = (x) => x * x;

function Langevin(x) {
  const a = Math.abs(x);
  if (a < 1e-2) {
    const x2 = x * x;
    return x * (1.0 / 3.0 - x2 / 45.0 + (2.0 * x2 * x2) / 945.0);
  }
  return 1.0 / Math.tanh(x) - 1.0 / x;
}

function LangevinSlope(x) {
  const a = Math.abs(x);
  if (a < 1e-2) {
    const x2 = x * x;
    return 1.0 / 3.0 - x2 / 15.0 + (2.0 * x2 * x2) / 189.0;
  }
  if (a > 350.0) return 1.0 / (x * x);
  const s = Math.sinh(x);
  return 1.0 / (x * x) - 1.0 / (s * s);
}

const Magnetisation = (fluid, h) => fluid.saturation * Langevin((3.0 * fluid.chi * h) / fluid.saturation);
const Susceptibility = (fluid, h) => 3.0 * fluid.chi * LangevinSlope((3.0 * fluid.chi * h) / fluid.saturation);

/** The fluid's state in a normal applied field: H_in = H_a - M( H_in ) by bisection. */
function Magnetise(fluid, applied) {
  const op = { applied: max2(applied, 0.0), inside: 0.0, m: 0.0, chord: 1.0, tangent: 1.0, r0: 1.0, g: 0.0 };
  let lo = 0.0;
  let hi = op.applied;
  for (let i = 0; i < 200 && hi - lo > 0.0; i += 1) {
    const mid = 0.5 * (lo + hi);
    if (mid === lo || mid === hi) break;
    if (mid + Magnetisation(fluid, mid) < op.applied) lo = mid;
    else hi = mid;
  }
  op.inside = 0.5 * (lo + hi);
  op.m = Magnetisation(fluid, op.inside);
  op.chord = op.inside > 0.0 ? 1.0 + op.m / op.inside : 1.0 + fluid.chi;
  op.tangent = 1.0 + Susceptibility(fluid, op.inside);
  op.r0 = Math.sqrt(op.chord * op.tangent);
  op.g = kMu0 * op.m * op.m * (op.r0 / (1.0 + op.r0));
  return op;
}

const CriticalWavenumber = (fluid) => Math.sqrt((fluid.density * kGravity) / fluid.tension);
const CriticalWavelength = (fluid) => (2.0 * kPi) / CriticalWavenumber(fluid);
const CriticalG = (fluid) => 2.0 * Math.sqrt(fluid.density * kGravity * fluid.tension);
const MagneticNumber = (fluid, g) => (g * CriticalWavenumber(fluid)) / (2.0 * fluid.density * kGravity);

function CriticalApplied(fluid) {
  const target = CriticalG(fluid);
  let lo = 0.0;
  let hi = 1.0;
  while (Magnetise(fluid, hi).g < target && hi < 1e9) hi *= 2.0;
  for (let i = 0; i < 200; i += 1) {
    const mid = 0.5 * (lo + hi);
    if (mid === lo || mid === hi) break;
    if (Magnetise(fluid, mid).g < target) lo = mid;
    else hi = mid;
  }
  return 0.5 * (lo + hi);
}

// std::complex, as far as OpticsOf needs it.
const cmul = (a, b) => [a[0] * b[0] - a[1] * b[1], a[0] * b[1] + a[1] * b[0]];
const cdiv = (a, b) => {
  const d = b[0] * b[0] + b[1] * b[1];
  return [(a[0] * b[0] + a[1] * b[1]) / d, (a[1] * b[0] - a[0] * b[1]) / d];
};
const cadd = (a, b) => [a[0] + b[0], a[1] + b[1]];
const csub = (a, b) => [a[0] - b[0], a[1] - b[1]];
const cscale = (a, s) => [a[0] * s, a[1] * s];
function csqrt(z) {
  const r = Math.hypot(z[0], z[1]);
  const re = Math.sqrt(max2(0.5 * (r + z[0]), 0.0));
  const im = Math.sqrt(max2(0.5 * (r - z[0]), 0.0));
  return [re, z[1] < 0.0 ? -im : im];
}

/** Maxwell Garnett spheres of bulk magnetite in the carrier oil (ASSUMED constants: AGENTS.md). */
function OpticsOf(fluid) {
  const phi = clamp(fluid.saturation / kMagnetiteSaturation, 0.0, 0.5);
  const particle = cmul([kMagnetiteN, kMagnetiteK], [kMagnetiteN, kMagnetiteK]);
  const host = [kCarrierN * kCarrierN, 0.0];
  const mixed = cdiv(
    cmul(host, cadd(cadd(particle, cscale(host, 2.0)), cscale(csub(particle, host), 2.0 * phi))),
    csub(cadd(particle, cscale(host, 2.0)), cscale(csub(particle, host), phi)),
  );
  const index = csqrt(mixed);
  return { n: index[0], k: index[1], fraction: phi };
}

const NormalReflectance = (o) => (sq(o.n - 1.0) + sq(o.k)) / (sq(o.n + 1.0) + sq(o.k));

//---------------------------------------------------------------------------
// Coil.h / Coil.cpp: the loop, the solenoid, the Helmholtz pair, the lag.
//---------------------------------------------------------------------------

const kSolenoidLength = 0.040;
const kSolenoidTurns = 400.0;
const kSolenoidDepth = 0.013;
const kHelmholtzRadius = 0.20;
const kHelmholtzTurns = 200.0;

/** Gauss-Legendre nodes and weights on [-1, 1], by Newton's method on P_n. */
function GaussLegendre(N) {
  const x = new Float64Array(N);
  const w = new Float64Array(N);
  for (let i = 0; i < Math.trunc((N + 1) / 2); i += 1) {
    let z = Math.cos((kPi * (i + 0.75)) / (N + 0.5));
    let dp = 0.0;
    for (let iter = 0; iter < 100; iter += 1) {
      let p0 = 1.0;
      let p1 = z;
      for (let k = 2; k <= N; k += 1) {
        const p2 = ((2.0 * k - 1.0) * z * p1 - (k - 1.0) * p0) / k;
        p0 = p1;
        p1 = p2;
      }
      dp = (N * (z * p1 - p0)) / (z * z - 1.0);
      const dz = p1 / dp;
      z -= dz;
      if (Math.abs(dz) < 1e-16) break;
    }
    x[i] = -z;
    x[N - 1 - i] = z;
    w[i] = w[N - 1 - i] = 2.0 / ((1.0 - z * z) * dp * dp);
  }
  return { x, w };
}
const RULE32 = GaussLegendre(32);

/** Complete elliptic integrals K and E of parameter m, by the AGM. */
function Elliptic(m) {
  m = clamp(m, 0.0, 1.0 - 1e-16);
  let a = 1.0;
  let b = Math.sqrt(1.0 - m);
  let sum = 0.5 * m;
  let power = 0.5;
  for (let i = 0; i < 64; i += 1) {
    const c = 0.5 * (a - b);
    const an = 0.5 * (a + b);
    b = Math.sqrt(a * b);
    a = an;
    power *= 2.0;
    const term = power * c * c;
    sum += term;
    if (term <= 1e-17 * sum) break;
  }
  const K = kPi / (2.0 * a);
  return { K, E: K * (1.0 - sum) };
}

/** H_z of one loop, A/m per amp. */
function LoopAxial(a, rho, z) {
  rho = Math.abs(rho);
  const r2 = rho * rho + z * z;
  const alpha2 = a * a + r2 - 2.0 * a * rho;
  const beta2 = a * a + r2 + 2.0 * a * rho;
  if (alpha2 <= 0.0) return 0.0;
  const beta = Math.sqrt(beta2);
  const { K, E } = Elliptic(1.0 - alpha2 / beta2);
  return ((a * a - r2) * E + alpha2 * K) / (2.0 * kPi * alpha2 * beta);
}

/** H_z at the fluid's surface, per amp, rho off the solenoid's axis. */
function SolenoidAxial(radius, rho) {
  const half = 0.5 * kSolenoidLength;
  const mid = kSolenoidDepth + half;
  let sum = 0.0;
  for (let i = 0; i < 32; i += 1) sum += RULE32.w[i] * LoopAxial(radius, rho, mid + half * RULE32.x[i]);
  return (sum * half * kSolenoidTurns) / kSolenoidLength;
}

const HelmholtzField = () => (Math.pow(0.8, 1.5) * kHelmholtzTurns) / kHelmholtzRadius;

function SolenoidProfile(radius, rhoMax, count) {
  const out = new Float32Array(count);
  for (let i = 0; i < count; i += 1) out[i] = SolenoidAxial(radius, (rhoMax * i) / (count - 1));
  return out;
}

function LagStep(current, target, dt, tau) {
  if (dt <= 0.0) return current;
  if (tau <= 0.0) return target;
  return target + (current - target) * Math.exp(-dt / tau);
}

//---------------------------------------------------------------------------
// Drive.h / Drive.cpp: what the beat and the buttons ask for, and what the
// coil's inductance lets through.
//---------------------------------------------------------------------------

class Drive {
  static kMaxCoils = 3;
  static kMaxCurrent = 1.6;
  static kMaxBeatsPerFrame = 8;

  constructor() {
    this.pulses = [];
    this.frameStart = 0.0;
    this.frameEnd = 0.0;
    this.startCurrent = [0, 0, 0];
    this.endCurrent = [0, 0, 0];
    this.level = [0, 0, 0];
    this.bias = 0.0;
    this.gain = 0.0;
    this.tau = 0.05;
    this.lastPhase = -1.0;
    this.fired = 0;
  }

  Reset() {
    this.pulses = [];
    for (let i = 0; i < Drive.kMaxCoils; i += 1) this.startCurrent[i] = this.endCurrent[i] = 0.0;
  }

  fire(at, amplitude, length) {
    this.pulses.push({ start: at, end: at + max2(length, 0.0), amplitude });
    this.fired += 1;
    while (this.pulses.length > 64) this.pulses.shift();
  }

  /** One host frame: [ now - dt, now ]. `input` is DriveInputs. */
  Advance(now, dt, input) {
    this.frameStart = dt > 0.0 ? now - dt : now;
    this.frameEnd = now;
    for (let i = 0; i < Drive.kMaxCoils; i += 1) {
      this.startCurrent[i] = this.endCurrent[i];
      this.level[i] = i < input.coils ? clamp(input.level[i], 0.0, 1.0) : 0.0;
    }
    this.bias = input.bias;
    this.gain = input.gain;
    this.tau = input.tau;

    for (let i = 0; i < input.presses; i += 1) this.fire(this.frameStart, 1.0, input.pulse);
    if (input.fireOn === FireOn.Onset && input.onset) this.fire(this.frameStart, clamp(input.kick, 0.0, 1.0), input.pulse);

    const tempo = input.bpm > 1.0 ? input.bpm : 120.0;
    const phaseNow = input.hostBeat ? clamp(input.barPhase, 0.0, 1.0) : ((now * tempo) / 240.0) % 1.0;
    let step = 0.0;
    switch (input.fireOn) {
      case FireOn.Beat: step = 0.25; break;
      case FireOn.HalfBeat: step = 0.125; break;
      case FireOn.TwoBeats: step = 0.5; break;
      case FireOn.Bar: step = 1.0; break;
      default: break;
    }
    if (step > 0.0 && this.lastPhase >= 0.0 && dt > 0.0) {
      const p0 = this.lastPhase;
      let p1 = phaseNow;
      if (p1 < p0) p1 += 1.0;
      if (p1 > p0) {
        let boundary = (Math.floor(p0 / step + 1e-9) + 1.0) * step;
        for (let n = 0; boundary <= p1 + 1e-12 && n < Drive.kMaxBeatsPerFrame; n += 1, boundary += step) {
          const inside = (boundary - p0) / (p1 - p0);
          this.fire(this.frameStart + inside * dt, 1.0, input.pulse);
        }
      }
    }
    this.lastPhase = phaseNow;

    while (this.pulses.length > 0 && this.pulses[0].end < this.frameStart) this.pulses.shift();

    for (let i = 0; i < Drive.kMaxCoils; i += 1) this.endCurrent[i] = this.CurrentAt(i, this.frameEnd);
  }

  TargetAt(coil, t) {
    if (coil < 0 || coil >= Drive.kMaxCoils) return 0.0;
    let on = 0.0;
    for (const p of this.pulses) if (p.start <= t && t < p.end) on += p.amplitude;
    return clamp(this.bias + this.gain * (this.level[coil] + on), 0.0, Drive.kMaxCurrent);
  }

  CurrentAt(coil, t) {
    if (coil < 0 || coil >= Drive.kMaxCoils) return 0.0;
    t = clamp(t, this.frameStart, this.frameEnd);
    const edges = [this.frameStart];
    for (const p of this.pulses) {
      for (const e of [p.start, p.end]) if (e > this.frameStart && e < t) edges.push(e);
    }
    edges.push(t);
    edges.sort((a, b) => a - b);
    let current = this.startCurrent[coil];
    for (let i = 0; i + 1 < edges.length; i += 1) {
      const a = edges[i];
      const b = edges[i + 1];
      if (b <= a) continue;
      current = LagStep(current, this.TargetAt(coil, 0.5 * (a + b)), b - a, this.tau);
    }
    return current;
  }

  Current(coil) { return coil >= 0 && coil < Drive.kMaxCoils ? this.endCurrent[coil] : 0.0; }
  FrameStart() { return this.frameStart; }
  FrameEnd() { return this.frameEnd; }
  Pulses() { return this.pulses; }
}

//---------------------------------------------------------------------------
// Spikes.cpp: the table for a preset.
//---------------------------------------------------------------------------
const TableFor = (fluidIndex) => TABLES[clamp(fluidIndex, 0, TABLES.length - 1)];

// ==== END PORT =============================================================

// ==== BEGIN FRAME ==========================================================
// Ferrofluid.cpp: the plugin class, its CPU half and its passes, in the order
// ProcessOpenGL runs them. The GL calls are WebGL2's spelling of the
// plugin's; the arithmetic is the plugin's.

const kMaxFrameDelta = 0.1;
const kPoolX = 512;
const kPoolY = 256;
const kCellsPerWavelength = 12.0;
const kForceCut = 4.0;
const kOuterCutMin = 3.0;
const kMaxSubsteps = 48;
const kMaxSubstep = 2e-3;
const kLensTanHalf = 0.18;
/** Shaders.h. */
const kTableSize = 1024;
const kMaxBoxes = 4;
const kHookNoKick = 1 << 6;

/** The radices for a power of two: sixteens, then what is left. */
function radicesOf(length) {
  const out = [];
  while (length > 1) {
    const r = length >= 16 ? 16 : length;
    out.push(r);
    length = Math.trunc(length / r);
  }
  return out;
}

function unitVector(v) {
  const l = Math.sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  for (let i = 0; i < 3; i += 1) v[i] /= l;
}

/** The soft boxes, in units of the field width (render()'s switch). */
function boxesFor(lights) {
  const box = (x, y, z, halfW, halfH, r, g, b) => ({ x, y, z, halfW, halfH, r, g, b });
  switch (lights) {
    case Lights.SoftBox: return [box(0.0, -3.0, 1.0, 2.2, 1.1, 1.0, 1.0, 1.0)];
    case Lights.TwoBoxes: return [box(-2.8, -1.2, 0.5, 0.9, 0.7, 1.0, 0.98, 0.95), box(2.8, -0.6, 0.4, 0.8, 0.6, 0.3, 0.31, 0.34)];
    case Lights.Strips:
      return [box(-1.4, -2.8, 0.6, 0.16, 0.9, 1.0, 1.0, 1.0), box(0.0, -3.0, 0.6, 0.16, 0.9, 1.0, 1.0, 1.0), box(1.4, -2.8, 0.6, 0.16, 0.9, 1.0, 1.0, 1.0)];
    case Lights.Backlight: return [box(0.0, 3.2, 0.5, 3.0, 0.5, 1.0, 1.0, 1.0)];
    case Lights.Window: {
      const out = [];
      for (let i = 0; i < 4; i += 1) out.push(box(-2.4 + (i % 2) * 0.62, -1.8, 0.7 + Math.trunc(i / 2) * 0.62, 0.28, 0.28, 1.0, 0.82, 0.6));
      return out;
    }
    case Lights.Neon: return [box(-2.4, -1.2, 0.6, 0.08, 0.7, 1.0, 0.12, 0.75), box(2.4, -1.2, 0.6, 0.08, 0.7, 0.1, 0.85, 1.0)];
    case Lights.Rim:
      return [box(-3.0, 0.6, 0.4, 0.12, 0.9, 1.0, 1.0, 1.0), box(3.0, 0.6, 0.4, 0.12, 0.9, 1.0, 1.0, 1.0), box(0.0, -2.5, 2.0, 1.5, 0.8, 0.05, 0.05, 0.05)];
    default: return [];
  }
}

/** A 1-row float table, nearest and clamped: makeTable(). */
function makeTable(gl, internal, layout, width, data) {
  const texture = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, texture);
  gl.pixelStorei(gl.UNPACK_ALIGNMENT, 1);
  gl.texImage2D(gl.TEXTURE_2D, 0, internal, width, 1, 0, layout, gl.FLOAT, data);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
  gl.pixelStorei(gl.UNPACK_ALIGNMENT, 4);
  gl.bindTexture(gl.TEXTURE_2D, null);
  return texture;
}

/** exp( -2 pi i m / length ), m = 0 .. length-1, from doubles. */
function makeTwiddles(gl, length) {
  const table = new Float32Array(length * 2);
  for (let m = 0; m < length; m += 1) {
    const angle = (-2.0 * kPi * m) / length;
    table[m * 2 + 0] = Math.cos(angle);
    table[m * 2 + 1] = Math.sin(angle);
  }
  return makeTable(gl, gl.RG32F, gl.RG, length, table);
}

class FerrofluidPlugin {
  constructor(gl, quad, programs, isEffect) {
    this.gl = gl;
    this.quad = quad;
    this.programs = programs;
    this.isEffect = isEffect;
    this.params = DefaultParams(isEffect);

    this.pool = { nx: 0, ny: 0, lx: 0, ly: 0 };
    this.poolFluid = -1;
    this.buffers = null;
    this.twiddleX = this.twiddleY = this.cameraTwiddleX = this.cameraTwiddleY = null;
    this.coilTable = this.fluidTable = null;
    this.coefficientTexture = this.basisTexture = this.equilibriumTexture = null;
    this.tableFluid = -1;
    this.mrtFBO = gl.createFramebuffer();
    this.stateIndex = 0;
    this.surfaceIndex = 0;
    this.renderIndex = 0;
    this.packValid = false;

    this.tablesFor = null;
    this.coilTableData = null;
    this.rhoMax = 0.5;
    this.fieldMax = 1.0;
    this.criticalCurrent = 1.0;
    this.criticalApplied = 1.0;
    this.centreField = 1.0;
    this.centres = [0, 0, 0, 0, 0, 0];
    this.plan = { substeps: 0, dt: 0, lost: 0, stressRef: 0, stressMax: 0, growthMax: 0, outerCut: 3.0 };
    this.substepSerial = 0;

    this.frameWidth = 0.12;
    this.frameHeight = 0.0675;
    this.exposureGain = 1.0;

    this.now = 0.0;
    this.lastNow = -1.0;
    this.fluidTime = 0.0;
    this.lostTime = 0.0;

    this.bpm = 120.0;
    this.barPhase = 0.0;
    this.lastBarPhase = -1.0;
    this.hostBeatSeen = false;

    this.firePresses = 0;
    this.fireHeld = false;
    this.settleWanted = false;
    this.settleHeld = false;

    this.drive = new Drive();
  }

  //-------------------------------------------------------------------------
  // The host-facing calls.
  //-------------------------------------------------------------------------
  SetFloatParameter(id, value) {
    const down = value >= 0.5;
    if (id === PT.FIRE) {
      if (down && !this.fireHeld) this.firePresses += 1;
      this.fireHeld = down;
    } else if (id === PT.SETTLE) {
      if (down && !this.settleHeld) this.settleWanted = true;
      this.settleHeld = down;
    }
    this.params[id] = f32(value);
  }

  SetBeatInfo(newBpm, newBarPhase) {
    if (newBpm > 0.0 && Number.isFinite(newBpm)) this.bpm = f32(newBpm);
    if (Number.isFinite(newBarPhase)) this.barPhase = f32(f32(newBarPhase) - Math.floor(f32(newBarPhase)));
  }

  Fluid() { return FluidOf(OptionIndex(this.params[PT.FLUID], 3)); }

  Model() {
    const table = TableFor(OptionIndex(this.params[PT.FLUID], 3));
    return { hysteresis: table.hysteresis, saturation: table.saturation };
  }

  CurrentMagnet() { return OptionIndex(this.params[PT.MAGNET], Magnet.Count); }

  stressAt(applied) { return Magnetise(this.Fluid(), applied).g; }

  coilCurrents(t) {
    return [0, 1, 2].map((j) => this.drive.CurrentAt(j, t) * this.criticalCurrent);
  }

  //-------------------------------------------------------------------------
  // The pool's buffers (ensurePool, clearState).
  //-------------------------------------------------------------------------
  ensurePool(want) {
    const gl = this.gl;
    const p = this.pool;
    if (want.nx === p.nx && want.ny === p.ny && want.lx === p.lx && want.ly === p.ly && this.buffers) return;
    this.disposeBuffers();
    const nearest = (w, h) => new PassBuffer(gl, { filter: 'nearest' }).ensure(w, h, gl.RGBA32F);
    // The surface the camera reads, at twice the pool's grid, LINEAR and
    // REPEAT: the pool wraps.
    const linearRepeat = (w, h) => {
      const b = new PassBuffer(gl, { filter: 'linear' }).ensure(w, h, gl.RGBA32F);
      gl.bindTexture(gl.TEXTURE_2D, b.texture);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT);
      gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.REPEAT);
      gl.bindTexture(gl.TEXTURE_2D, null);
      return b;
    };
    const cx = 2 * want.nx;
    const cy = 2 * want.ny;
    const bounds = [];
    let w = cx;
    let h = cy;
    do {
      w = max2(1, Math.trunc((w + 7) / 8));
      h = max2(1, Math.trunc((h + 7) / 8));
      bounds.push(nearest(w, h));
    } while (w > 1 || h > 1);
    this.buffers = {
      state: [nearest(want.nx, want.ny), nearest(want.nx, want.ny)],
      work: [nearest(want.nx, want.ny), nearest(want.nx, want.ny)],
      pack: nearest(want.nx, want.ny),
      force: nearest(want.nx, want.ny),
      surfaceWork: [linearRepeat(cx, cy), linearRepeat(cx, cy)],
      renderWork: [linearRepeat(cx, cy), linearRepeat(cx, cy)],
      spikeWeights: nearest(cx, cy),
      spikeWhere: nearest(cx, cy),
      bounds,
    };
    for (const all of Object.values(this.buffers)) for (const b of [].concat(all)) b.clearTo(0, 0, 0, 0);
    this.twiddleX = makeTwiddles(gl, want.nx);
    this.twiddleY = makeTwiddles(gl, want.ny);
    this.cameraTwiddleX = makeTwiddles(gl, 2 * want.nx);
    this.cameraTwiddleY = makeTwiddles(gl, 2 * want.ny);
    this.stateIndex = 0;
    this.surfaceIndex = 0;
    this.renderIndex = 0;
    this.packValid = false;
    this.pool = { ...want };
  }

  clearState() {
    for (const b of this.buffers.state) b.clearTo(0, 0, 0, 0);
    this.packValid = false;
  }

  //-------------------------------------------------------------------------
  // updateTables(): the coil's profile, the critical current, the fluid's G.
  //-------------------------------------------------------------------------
  updateTables() {
    const gl = this.gl;
    const magnet = this.CurrentMagnet();
    const key = [
      OptionIndex(this.params[PT.FLUID], 3), magnet, this.params[PT.COIL_SIZE],
      magnet === Magnet.ThreeCoils ? this.params[PT.COIL_SPREAD] : -1.0, this.pool.lx,
    ].join('|');
    if (key === this.tablesFor && this.coilTable && this.fluidTable) return;
    this.tablesFor = key;

    const fluid = this.Fluid();
    this.criticalApplied = CriticalApplied(fluid);

    const radius = CoilSizeFromParam(this.params[PT.COIL_SIZE]);
    this.rhoMax = 0.5 * Math.sqrt(this.pool.lx * this.pool.lx + this.pool.ly * this.pool.ly) + SpreadFromParam(f32(1.0));
    this.coilTableData = SolenoidProfile(radius, this.rhoMax, kTableSize);
    const table = this.coilTableData;

    if (magnet === Magnet.Uniform) this.centreField = HelmholtzField();
    else if (magnet === Magnet.Coil) this.centreField = SolenoidAxial(radius, 0.0);
    else {
      const side = SpreadFromParam(this.params[PT.COIL_SPREAD]);
      const R = side / Math.sqrt(3.0);
      const cx = [0.0, -0.5 * side, 0.5 * side];
      const cy = [R, -0.5 * R, -0.5 * R];
      const reach = R + 2.0 * radius;
      this.centreField = 0.0;
      for (let iy = -40; iy <= 40; iy += 1) {
        for (let ix = -40; ix <= 40; ix += 1) {
          const px = (reach * ix) / 40.0;
          const py = (reach * iy) / 40.0;
          let field = 0.0;
          for (let j = 0; j < 3; j += 1) {
            const r = Math.hypot(px - cx[j], py - cy[j]);
            const f = clamp(r / this.rhoMax, 0.0, 1.0) * (kTableSize - 1);
            const i0 = min2(Math.trunc(f), kTableSize - 2);
            field += table[i0] + (f - i0) * (table[i0 + 1] - table[i0]);
          }
          this.centreField = max2(this.centreField, field);
        }
      }
    }
    this.criticalCurrent = this.criticalApplied / this.centreField;

    this.fieldMax = 3.0 * Drive.kMaxCurrent * this.criticalApplied * 1.04;
    const fluidTableData = new Float32Array(kTableSize);
    for (let i = 0; i < kTableSize; i += 1) fluidTableData[i] = this.stressAt((this.fieldMax * i) / (kTableSize - 1));

    if (this.coilTable) gl.deleteTexture(this.coilTable);
    if (this.fluidTable) gl.deleteTexture(this.fluidTable);
    this.coilTable = makeTable(gl, gl.R32F, gl.RED, kTableSize, table);
    this.fluidTable = makeTable(gl, gl.R32F, gl.RED, kTableSize, fluidTableData);
  }

  //-------------------------------------------------------------------------
  // planSubsteps(): the longest step the kick-drift map is stable at.
  //-------------------------------------------------------------------------
  planSubsteps(frameDt) {
    const out = { substeps: 0, dt: 0.0, lost: 0.0, stressRef: 0.0, stressMax: 0.0, growthMax: 0.0, outerCut: 3.0 };
    if (frameDt <= 0.0) return out;

    const fluid = this.Fluid();
    const rho = fluid.density;
    const g = kGravity;
    const kc = CriticalWavenumber(fluid);
    const magnet = this.CurrentMagnet();
    const model = this.Model();
    const drive = this.drive;
    const pool = this.pool;

    const times = [drive.FrameStart(), drive.FrameEnd()];
    for (const p of drive.Pulses()) {
      for (const e of [p.start, p.end]) if (e > drive.FrameStart() && e < drive.FrameEnd()) times.push(e);
    }
    const peak = [0, 0, 0];
    for (const t of times) {
      for (let j = 0; j < 3; j += 1) peak[j] = max2(peak[j], drive.CurrentAt(j, t) * this.criticalCurrent);
    }
    let applied = 0.0;
    if (magnet === Magnet.Uniform || magnet === Magnet.Coil) applied = peak[0] * this.centreField;
    else {
      const table = this.coilTableData;
      for (let iy = 0; iy <= 48; iy += 1) {
        for (let ix = 0; ix <= 96; ix += 1) {
          const px = (ix / 96.0 - 0.5) * pool.lx;
          const py = (iy / 48.0 - 0.5) * pool.ly;
          let field = 0.0;
          for (let j = 0; j < 3; j += 1) {
            const r = Math.hypot(px - this.centres[2 * j], py - this.centres[2 * j + 1]);
            const f = clamp(r / this.rhoMax, 0.0, 1.0) * (kTableSize - 1);
            const i0 = min2(Math.trunc(f), kTableSize - 2);
            field += peak[j] * (table[i0] + (f - i0) * (table[i0 + 1] - table[i0]));
          }
          applied = max2(applied, field);
        }
      }
    }
    out.stressMax = this.stressAt(applied);
    out.stressRef = magnet === Magnet.Uniform ? out.stressMax : min2(out.stressMax, CriticalG(fluid));
    const residual = magnet === Magnet.Uniform ? 0.0 : max2(out.stressRef, out.stressMax - out.stressRef);

    const mMax = MagneticNumber(fluid, out.stressMax);
    const nyquist = (kPi * min2(pool.nx / pool.lx, pool.ny / pool.ly)) / kc;
    const bandTop = mMax > 1.0 ? mMax + Math.sqrt(mMax * mMax - 1.0) : 1.0;
    out.outerCut = min2(max2(kOuterCutMin, bandTop + 0.5), 0.9 * nyquist);
    const x = 1.5 * max2(1.0, model.hysteresis / max2(model.saturation, 1e-6));
    const nlRate = g * mMax * (2.0 * model.hysteresis * x + 3.0 * model.saturation * x * x);

    const cut = min2(kForceCut * kc, 0.67 * kPi * min2(pool.nx / pool.lx, pool.ny / pool.ly));
    const qTop = (1.3 * cut) / kc;
    const sigmaR = fluid.tension / rho;
    const stableAt = (dt) => {
      for (let i = 1; i <= 400; i += 1) {
        const k = (qTop * kc * i) / 400.0;
        const r = k / cut;
        const pass = Math.exp(-Math.pow(r, 8.0));
        const omega2 = g * k + sigmaR * k * k * k - (out.stressRef / rho) * k * k;
        const r3 = k / (out.outerCut * kc);
        const outer = Math.exp(-Math.pow(r3, 8.0));
        const kappa = ((residual * k * k) / rho) * pass + nlRate * k * (k / kc) * (k / kc) * outer;
        let c;
        let s;
        if (omega2 >= 0.0) {
          const w = Math.sqrt(omega2);
          c = Math.cos(w * dt);
          s = w > 0.0 ? Math.sin(w * dt) / w : dt;
        } else {
          const w = Math.sqrt(-omega2);
          c = Math.cosh(w * dt);
          s = Math.sinh(w * dt) / w;
        }
        for (const sign of [-1.0, 1.0]) {
          const k2 = sign * kappa;
          if (omega2 + k2 <= 0.0) continue;
          const trace = 2.0 * c - k2 * dt * s;
          if (trace > 2.0 + 1e-12 || trace < -1.9) return false;
        }
      }
      return true;
    };
    let dt = kMaxSubstep;
    while (dt > 1e-5 && !stableAt(dt)) dt *= 0.85;

    for (let i = 1; i <= 400; i += 1) {
      const k = (qTop * kc * i) / 400.0;
      const omega2 = g * k + sigmaR * k * k * k - (out.stressMax / rho) * k * k;
      if (omega2 < 0.0) out.growthMax = max2(out.growthMax, Math.sqrt(-omega2));
    }
    if (magnet !== Magnet.Uniform && out.growthMax > 0.0) dt = min2(dt, 0.33 / out.growthMax);

    let n = Math.ceil(frameDt / dt - 1e-9);
    n = max2(n, 1);
    if (n > kMaxSubsteps) {
      out.lost = frameDt - kMaxSubsteps * dt;
      n = kMaxSubsteps;
    } else dt = frameDt / n;
    out.substeps = n;
    out.dt = dt;
    return out;
  }

  //-------------------------------------------------------------------------
  // GL plumbing: binding, uniforms.
  //-------------------------------------------------------------------------
  loc(program, name) {
    const where = program.location(name);
    if (where === null) program.missing.add(name);
    return where;
  }
  i1(p, name, v) { this.gl.uniform1i(this.loc(p, name), v); }
  u1(p, name, v) { this.gl.uniform1ui(this.loc(p, name), v >>> 0); }
  f1(p, name, v) { this.gl.uniform1f(this.loc(p, name), v); }
  f2(p, name, a, b) { this.gl.uniform2f(this.loc(p, name), a, b); }
  f3(p, name, a, b, c) { this.gl.uniform3f(this.loc(p, name), a, b, c); }
  i2(p, name, a, b) { this.gl.uniform2i(this.loc(p, name), a, b); }
  i4(p, name, a, b, c, d) { this.gl.uniform4i(this.loc(p, name), a, b, c, d); }
  f2v(p, name, v) { this.gl.uniform2fv(this.loc(p, `${name}[0]`), v); }
  f3v(p, name, v) { this.gl.uniform3fv(this.loc(p, `${name}[0]`), v); }

  unbind(count) {
    for (let unit = count - 1; unit >= 0; unit -= 1) bindTexture(this.gl, unit, null);
    this.gl.activeTexture(this.gl.TEXTURE0);
  }

  /** Every pass writes every texel; a clear is the plugin's discardTarget(). */
  discardTarget() {
    const gl = this.gl;
    gl.clearColor(0, 0, 0, 0);
    gl.clear(gl.COLOR_BUFFER_BIT);
  }

  bindMRT(a, b) {
    const gl = this.gl;
    gl.bindFramebuffer(gl.FRAMEBUFFER, this.mrtFBO);
    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, a.texture, 0);
    gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT1, gl.TEXTURE_2D, b.texture, 0);
    gl.drawBuffers([gl.COLOR_ATTACHMENT0, gl.COLOR_ATTACHMENT1]);
  }

  //-------------------------------------------------------------------------
  // transform(): the Stockham FFT, both axes. `last` names the buffer
  // `source` is, or -1 for a texture of its own; returns the buffer it ended in.
  //-------------------------------------------------------------------------
  transform(source, buffers, last, direction, nx, ny, twX, twY) {
    const gl = this.gl;
    const p = this.programs.fft.use();
    this.i1(p, 'Source', 0);
    this.i1(p, 'Twiddles', 1);
    this.f1(p, 'Direction', direction);
    let current = last;
    let input = source;
    for (const axis of [{ horizontal: 1, length: nx, twiddles: twX }, { horizontal: 0, length: ny, twiddles: twY }]) {
      this.i1(p, 'Horizontal', axis.horizontal);
      this.i1(p, 'Length', axis.length);
      let stride = 1;
      for (const radix of radicesOf(axis.length)) {
        const target = current === 0 ? 1 : 0;
        gl.bindFramebuffer(gl.FRAMEBUFFER, buffers[target].fbo);
        gl.viewport(0, 0, nx, ny);
        this.discardTarget();
        bindTexture(gl, 0, input);
        bindTexture(gl, 1, axis.twiddles);
        this.i1(p, 'Stride', stride);
        this.i1(p, 'Radix', radix);
        this.quad.draw();
        stride *= radix;
        current = target;
        input = buffers[target].texture;
      }
    }
    this.unbind(2);
    return current;
  }

  //-------------------------------------------------------------------------
  // substep(): back to real space, the stress, to the spectrum, kick and drift.
  //-------------------------------------------------------------------------
  substep(t, dt, plan) {
    const gl = this.gl;
    const B = this.buffers;
    const pool = this.pool;
    const fluid = this.Fluid();
    const magnet = this.CurrentMagnet();
    const model = this.Model();
    const kc = CriticalWavenumber(fluid);

    const currents = this.coilCurrents(t);
    const uniformStress = magnet === Magnet.Uniform ? this.stressAt(currents[0] * this.centreField) : 0.0;
    const stressRef = magnet === Magnet.Uniform ? uniformStress : plan.stressRef;

    // 1. Back to real space: ( W h, |grad| h, h ).
    const real = this.transform(B.pack.texture, B.work, -1, 1.0, pool.nx, pool.ny, this.twiddleX, this.twiddleY);

    // 2. The stress.
    {
      gl.bindFramebuffer(gl.FRAMEBUFFER, B.force.fbo);
      gl.viewport(0, 0, pool.nx, pool.ny);
      this.discardTarget();
      const p = this.programs.force.use();
      bindTexture(gl, 0, B.work[real].texture);
      bindTexture(gl, 1, this.coilTable);
      bindTexture(gl, 2, this.fluidTable);
      this.i1(p, 'Real', 0);
      this.i1(p, 'CoilTable', 1);
      this.i1(p, 'FluidTable', 2);
      this.i2(p, 'GridSize', pool.nx, pool.ny);
      this.f2(p, 'Domain', pool.lx, pool.ly);
      this.i1(p, 'Magnet', magnet);
      this.f1(p, 'UniformStress', uniformStress);
      this.f3(p, 'Currents', currents[0], currents[1], currents[2]);
      this.f2v(p, 'Centres', new Float32Array(this.centres));
      this.i1(p, 'CoilCount', magnet === Magnet.ThreeCoils ? 3 : 1);
      this.f1(p, 'RhoMax', this.rhoMax);
      this.f1(p, 'FieldMax', this.fieldMax);
      this.f1(p, 'StressRef', stressRef);
      this.f1(p, 'RhoG', fluid.density * kGravity);
      this.f1(p, 'Kc', kc);
      this.f1(p, 'Hy', model.hysteresis);
      this.f1(p, 'Sa', model.saturation);
      const cellArea = (pool.lx / pool.nx) * (pool.ly / pool.ny);
      const rms = NoiseFromParam(this.params[PT.NOISE]) * Math.sqrt(1e-3 / dt) * Math.sqrt(1e-6 / cellArea);
      this.f1(p, 'NoiseRms', rms);
      this.substepSerial = (this.substepSerial + 1) >>> 0;
      this.u1(p, 'NoiseSalt', Math.imul(this.substepSerial, 2654435761 | 0));
      this.i1(p, 'Hooks', 0);
      this.quad.draw();
      this.unbind(3);
    }

    // 3. The stress to N_k.
    const spectral = this.transform(B.force.texture, B.work, -1, -1.0, pool.nx, pool.ny, this.twiddleX, this.twiddleY);

    // 4. Kick and drift, into the other state and the next pack.
    {
      const next = 1 - this.stateIndex;
      this.bindMRT(B.state[next], B.pack);
      gl.viewport(0, 0, pool.nx, pool.ny);
      this.discardTarget();
      const p = this.programs.evolve.use();
      bindTexture(gl, 0, B.state[this.stateIndex].texture);
      bindTexture(gl, 1, B.work[spectral].texture);
      this.i1(p, 'State', 0);
      this.i1(p, 'Force', 1);
      this.i2(p, 'GridSize', pool.nx, pool.ny);
      this.f2(p, 'Domain', pool.lx, pool.ly);
      this.f1(p, 'Gravity', kGravity);
      this.f1(p, 'TensionRho', fluid.tension / fluid.density);
      this.f1(p, 'StressRho', stressRef / fluid.density);
      this.f1(p, 'InvRho', 1.0 / fluid.density);
      this.f1(p, 'Kinematic', (ViscosityFromParam(this.params[PT.VISCOSITY]) * fluid.viscosity) / fluid.density);
      this.f1(p, 'Dt', dt);
      const cut = min2(kForceCut * kc, 0.67 * kPi * min2(pool.nx / pool.lx, pool.ny / pool.ly));
      this.f1(p, 'ForceCut', cut);
      this.f1(p, 'Kc', kc);
      this.f1(p, 'OuterCut', plan.outerCut * kc);
      this.f1(p, 'Norm', 1.0 / (pool.nx * pool.ny));
      this.i1(p, 'Hooks', 0);
      this.quad.draw();
      this.unbind(2);
      this.stateIndex = next;
    }
    this.packValid = true;
  }

  /** repack(): the evolve at dt 0 with no kick, the state copied and its pack written. */
  repack() {
    const gl = this.gl;
    const B = this.buffers;
    const pool = this.pool;
    const fluid = this.Fluid();
    const next = 1 - this.stateIndex;
    this.bindMRT(B.state[next], B.pack);
    gl.viewport(0, 0, pool.nx, pool.ny);
    const p = this.programs.evolve.use();
    bindTexture(gl, 0, B.state[this.stateIndex].texture);
    bindTexture(gl, 1, B.work[0].texture);
    this.i1(p, 'State', 0);
    this.i1(p, 'Force', 1);
    this.i2(p, 'GridSize', pool.nx, pool.ny);
    this.f2(p, 'Domain', pool.lx, pool.ly);
    this.f1(p, 'Gravity', kGravity);
    this.f1(p, 'TensionRho', fluid.tension / fluid.density);
    this.f1(p, 'StressRho', 0.0);
    this.f1(p, 'InvRho', 1.0 / fluid.density);
    this.f1(p, 'Kinematic', 0.0);
    this.f1(p, 'Dt', 0.0);
    this.f1(p, 'ForceCut', 1.0);
    this.f1(p, 'Kc', 1.0);
    this.f1(p, 'OuterCut', 1.0);
    this.f1(p, 'Norm', 1.0 / (pool.nx * pool.ny));
    this.i1(p, 'Hooks', kHookNoKick);
    this.quad.draw();
    this.unbind(2);
    this.stateIndex = next;
    this.packValid = true;
  }

  //-------------------------------------------------------------------------
  // The solved spikes: updateSpikeTables() and spikes().
  //-------------------------------------------------------------------------
  updateSpikeTables() {
    const gl = this.gl;
    const fluidIndex = OptionIndex(this.params[PT.FLUID], 3);
    if (fluidIndex === this.tableFluid && this.coefficientTexture && this.basisTexture && this.equilibriumTexture) return;
    this.tableFluid = fluidIndex;
    const table = TableFor(fluidIndex);
    for (const t of [this.coefficientTexture, this.basisTexture, this.equilibriumTexture]) if (t) gl.deleteTexture(t);
    this.coefficientTexture = makeTable(gl, gl.RGBA32F, gl.RGBA, table.nGrid, table.coefficients);
    this.basisTexture = makeTable(gl, gl.RGBA32F, gl.RGBA, table.nQ, table.basis);
    this.equilibriumTexture = makeTable(gl, gl.R32F, gl.RED, table.nEq, table.eqPeak);
  }

  spikes() {
    const gl = this.gl;
    const B = this.buffers;
    const pool = this.pool;
    this.updateSpikeTables();
    const table = TableFor(this.tableFluid);
    const cx = 2 * pool.nx;
    const cy = 2 * pool.ny;
    const cellX = f32(pool.lx / cx);
    const cellY = f32(pool.ly / cy);
    const magnet = this.CurrentMagnet();
    const currents = this.coilCurrents(this.drive.FrameEnd());

    // 1. Where the spikes stand, and their weights.
    this.bindMRT(B.spikeWeights, B.spikeWhere);
    gl.viewport(0, 0, cx, cy);
    this.discardTarget();
    {
      const p = this.programs.spikes.use();
      bindTexture(gl, 0, B.surfaceWork[this.surfaceIndex].texture);
      bindTexture(gl, 1, this.coilTable);
      bindTexture(gl, 2, this.coefficientTexture);
      bindTexture(gl, 3, this.equilibriumTexture);
      this.i1(p, 'Surface', 0);
      this.i1(p, 'CoilTable', 1);
      this.i1(p, 'Coefficients', 2);
      this.i1(p, 'Equilibrium', 3);
      this.i2(p, 'Size', cx, cy);
      this.f2(p, 'Cell', cellX, cellY);
      this.f2(p, 'Domain', pool.lx, pool.ly);
      this.i1(p, 'Magnet', magnet);
      this.f1(p, 'UniformU', (currents[0] * this.centreField) / this.criticalApplied);
      this.f1(p, 'RhoMax', this.rhoMax);
      this.f2v(p, 'Centres', new Float32Array(this.centres));
      this.i1(p, 'CoilCount', magnet === Magnet.ThreeCoils ? 3 : 1);
      this.f3(p, 'CurrentsU', currents[0] / this.criticalApplied, currents[1] / this.criticalApplied, currents[2] / this.criticalApplied);
      this.i1(p, 'NGrid', table.nGrid);
      this.f1(p, 'UFold', table.uFold);
      this.f1(p, 'VEnd', table.vEnd);
      this.i1(p, 'NEq', table.nEq);
      this.f2(p, 'EqRange', table.eqU0, table.eqU1);
      this.i1(p, 'Reach', max2(2, Math.trunc((0.25 * CriticalWavelength(this.Fluid())) / max2(cellX, cellY))));
      this.i1(p, 'Hooks', 0);
      this.quad.draw();
      this.unbind(4);
    }
    gl.drawBuffers([gl.COLOR_ATTACHMENT0]);

    // 2. The deposit: a Gaussian of one texel at each spike.
    const sigma = min2(cellX, cellY);
    gl.bindFramebuffer(gl.FRAMEBUFFER, B.renderWork[0].fbo);
    gl.viewport(0, 0, cx, cy);
    this.discardTarget();
    {
      const p = this.programs.deposit.use();
      bindTexture(gl, 0, B.spikeWeights.texture);
      bindTexture(gl, 1, B.spikeWhere.texture);
      this.i1(p, 'Weights', 0);
      this.i1(p, 'Where', 1);
      this.i2(p, 'Size', cx, cy);
      this.f2(p, 'Cell', cellX, cellY);
      this.f1(p, 'Sigma', sigma);
      this.f1(p, 'Norm', 1.0 / (2.0 * kPi * sigma * sigma));
      this.quad.draw();
      this.unbind(2);
    }

    // 3. Its transform, times the basis, and back.
    let last = this.transform(B.renderWork[0].texture, B.renderWork, 0, -1.0, cx, cy, this.cameraTwiddleX, this.cameraTwiddleY);
    const target = 1 - last;
    gl.bindFramebuffer(gl.FRAMEBUFFER, B.renderWork[target].fbo);
    gl.viewport(0, 0, cx, cy);
    this.discardTarget();
    {
      const p = this.programs.combine.use();
      bindTexture(gl, 0, B.renderWork[last].texture);
      bindTexture(gl, 1, this.basisTexture);
      this.i1(p, 'Spectrum', 0);
      this.i1(p, 'Basis', 1);
      this.i2(p, 'Size', cx, cy);
      this.f2(p, 'Domain', pool.lx, pool.ly);
      this.f1(p, 'Kc', CriticalWavenumber(this.Fluid()));
      this.i1(p, 'NQ', table.nQ);
      this.f1(p, 'QMax', table.qMax);
      this.f1(p, 'Sigma', sigma);
      this.f1(p, 'Norm', 1.0 / (cx * cy));
      this.quad.draw();
      this.unbind(2);
    }
    last = target;
    this.renderIndex = this.transform(B.renderWork[target].texture, B.renderWork, last, 1.0, cx, cy, this.cameraTwiddleX, this.cameraTwiddleY);
  }

  /** surfaceAndBounds(): the camera's surface, the spikes on it, the march's bounds. */
  surfaceAndBounds() {
    const gl = this.gl;
    const B = this.buffers;
    const pool = this.pool;
    const cx = 2 * pool.nx;
    const cy = 2 * pool.ny;
    gl.bindFramebuffer(gl.FRAMEBUFFER, B.surfaceWork[0].fbo);
    gl.viewport(0, 0, cx, cy);
    {
      const p = this.programs.pack.use();
      bindTexture(gl, 0, B.state[this.stateIndex].texture);
      this.i1(p, 'State', 0);
      this.i2(p, 'GridSize', pool.nx, pool.ny);
      this.i2(p, 'OutSize', cx, cy);
      this.f2(p, 'Domain', pool.lx, pool.ly);
      this.f1(p, 'Norm', 1.0 / (pool.nx * pool.ny));
      this.quad.draw();
      this.unbind(1);
    }
    this.surfaceIndex = this.transform(B.surfaceWork[0].texture, B.surfaceWork, 0, 1.0, cx, cy, this.cameraTwiddleX, this.cameraTwiddleY);

    this.spikes();

    const p = this.programs.bounds.use();
    this.i1(p, 'Source', 0);
    this.f2(p, 'Cell', pool.lx / cx, pool.ly / cy);
    let source = B.renderWork[this.renderIndex].texture;
    let w = cx;
    let h = cy;
    B.bounds.forEach((b, i) => {
      gl.bindFramebuffer(gl.FRAMEBUFFER, b.fbo);
      gl.viewport(0, 0, b.width, b.height);
      bindTexture(gl, 0, source);
      this.i2(p, 'SourceSize', w, h);
      this.i1(p, 'First', i === 0 ? 1 : 0);
      this.quad.draw();
      source = b.texture;
      w = b.width;
      h = b.height;
    });
    this.unbind(1);
  }

  //-------------------------------------------------------------------------
  // render(): the camera, the black mirror and what it reflects.
  //-------------------------------------------------------------------------
  render(input, width, height, target) {
    const gl = this.gl;
    const B = this.buffers;
    const pool = this.pool;
    const fluid = this.Fluid();
    const optics = OpticsOf(fluid);

    this.frameWidth = FieldWidthFromParam(this.params[PT.FIELD_WIDTH]);
    this.frameHeight = (this.frameWidth * height) / width;
    const tilt = TiltFromParam(this.params[PT.TILT]);
    const D = (0.5 * this.frameWidth) / kLensTanHalf;
    const eye = [0.0, -D * Math.sin(tilt), D * Math.cos(tilt)];
    const fwd = [0.0, Math.sin(tilt), -Math.cos(tilt)];
    const up = [0.0, Math.cos(tilt), Math.sin(tilt)];
    const reference = NormalReflectance(optics);
    this.exposureGain = Math.pow(2.0, ExposureFromParam(this.params[PT.EXPOSURE])) / reference;

    gl.bindFramebuffer(gl.FRAMEBUFFER, target);
    gl.viewport(0, 0, width, height);
    const p = this.programs.render.use();
    bindTexture(gl, 0, B.renderWork[this.renderIndex].texture);
    bindTexture(gl, 1, B.bounds[B.bounds.length - 1].texture);
    // Always a real texture on the clip's unit, even in the source (the
    // plugin's choice: the coil table).
    bindTexture(gl, 2, input ? input.texture : this.coilTable);
    this.i1(p, 'Surface', 0);
    this.i1(p, 'Bounds', 1);
    this.i1(p, 'InputTexture', 2);
    // The kit's clip texture is exactly the clip's size: its max texcoords are 1.
    this.f2(p, 'MaxUV', 1.0, 1.0);
    this.i1(p, 'IsOver', this.isEffect ? 1 : 0);
    this.i4(p, 'Viewport', 0, 0, width, height);
    this.f2(p, 'Domain', pool.lx, pool.ly);
    this.f2(p, 'Cell', pool.lx / (2 * pool.nx), pool.ly / (2 * pool.ny));
    this.f3(p, 'Eye', eye[0], eye[1], eye[2]);
    this.f3(p, 'Forward', fwd[0], fwd[1], fwd[2]);
    this.f3(p, 'Right', 1.0, 0.0, 0.0);
    this.f3(p, 'Up', up[0], up[1], up[2]);
    this.f2(p, 'TanHalf', kLensTanHalf, (kLensTanHalf * height) / width);
    this.f2(p, 'Optical', optics.n, optics.k);
    this.f1(p, 'Gain', this.exposureGain);

    const screen = HeightFromParam(this.params[PT.HEIGHT]);
    this.f1(p, 'ScreenHeight', screen);
    this.f2(p, 'ScreenScale', D / ((D + screen) * this.frameWidth), D / ((D + screen) * this.frameHeight));

    const boxes = boxesFor(OptionIndex(this.params[PT.LIGHTS], Lights.Count));
    const centre = new Float32Array(12);
    const uvec = new Float32Array(12);
    const vvec = new Float32Array(12);
    const radiance = new Float32Array(12);
    const count = min2(boxes.length, kMaxBoxes);
    for (let i = 0; i < count; i += 1) {
      const b = boxes[i];
      const c = [b.x * this.frameWidth, b.y * this.frameWidth, b.z * this.frameWidth];
      const n = [-c[0], -c[1], -c[2]];
      unitVector(n);
      const u = [n[1], -n[0], 0.0];
      if (Math.abs(u[0]) + Math.abs(u[1]) < 1e-9) u[0] = 1.0;
      unitVector(u);
      const v = [n[1] * u[2] - n[2] * u[1], n[2] * u[0] - n[0] * u[2], n[0] * u[1] - n[1] * u[0]];
      for (let k = 0; k < 3; k += 1) {
        centre[i * 3 + k] = c[k];
        uvec[i * 3 + k] = u[k] * b.halfW * this.frameWidth;
        vvec[i * 3 + k] = v[k] * b.halfH * this.frameWidth;
      }
      radiance[i * 3 + 0] = b.r;
      radiance[i * 3 + 1] = b.g;
      radiance[i * 3 + 2] = b.b;
    }
    this.i1(p, 'BoxCount', this.isEffect ? 0 : count);
    this.f3v(p, 'BoxCentre', centre);
    this.f3v(p, 'BoxU', uvec);
    this.f3v(p, 'BoxV', vvec);
    this.f3v(p, 'BoxRadiance', radiance);
    const studio = OptionIndex(this.params[PT.BACKDROP], 2) === Backdrop.Studio;
    this.f3(p, 'Backdrop', studio ? 0.06 : 0.0, studio ? 0.06 : 0.0, studio ? 0.065 : 0.0);
    this.f1(p, 'MixAmount', this.isEffect ? clamp(this.params[PT.MIX], 0.0, 1.0) : 1.0);
    this.i1(p, 'Hooks', 0);
    this.quad.draw();
    this.unbind(3);
  }

  //-------------------------------------------------------------------------
  // ProcessOpenGL. `now` is the host's time in seconds; `input` the clip
  // (the Over); `target` the framebuffer to draw into (null: the canvas).
  //-------------------------------------------------------------------------
  ProcessOpenGL(input, width, height, now, target = null) {
    const gl = this.gl;
    gl.disable(gl.BLEND);
    if (this.isEffect) {
      if (!input) return false;
      width = input.width;
      height = input.height;
    }
    if (width <= 0 || height <= 0) return false;

    // Time, the audio and the beat.
    this.now = now;
    const dt = this.lastNow >= 0.0 ? clamp(this.now - this.lastNow, 0.0, kMaxFrameDelta) : 0.0;
    this.lastNow = this.now;
    // UpdateAudio: silence. The analyser's band levels are 0 and it never fires.
    if (this.barPhase !== this.lastBarPhase && this.lastBarPhase >= 0.0) this.hostBeatSeen = true;
    this.lastBarPhase = this.barPhase;

    // The pool. A new fluid is a new pool: it starts flat.
    const fluidIndex = OptionIndex(this.params[PT.FLUID], 3);
    const cell = CriticalWavelength(this.Fluid()) / kCellsPerWavelength;
    const want = { nx: kPoolX, ny: kPoolY, lx: kPoolX * cell, ly: kPoolY * cell };
    const fresh = want.nx !== this.pool.nx || want.ny !== this.pool.ny || want.lx !== this.pool.lx
      || want.ly !== this.pool.ly || fluidIndex !== this.poolFluid;
    this.ensurePool(want);
    if (fresh) {
      this.clearState();
      this.poolFluid = fluidIndex;
    }
    if (this.settleWanted) {
      this.clearState();
      this.settleWanted = false;
    }
    this.updateTables();

    // The coils' places.
    const magnet = this.CurrentMagnet();
    {
      const side = SpreadFromParam(this.params[PT.COIL_SPREAD]);
      const R = side / Math.sqrt(3.0);
      this.centres = magnet === Magnet.ThreeCoils ? [0.0, R, -0.5 * side, -0.5 * R, 0.5 * side, -0.5 * R] : [0, 0, 0, 0, 0, 0];
    }

    // The drive.
    const input_ = {
      fireOn: OptionIndex(this.params[PT.FIRE_ON], FireOn.Count),
      presses: this.firePresses,
      onset: false,
      kick: 0.0,
      level: [0.0, 0.0, 0.0],
      coils: magnet === Magnet.ThreeCoils ? 3 : 1,
      bias: CurrentFromParam(this.params[PT.BIAS]),
      gain: CurrentFromParam(this.params[PT.GAIN]),
      pulse: PulseFromParam(this.params[PT.PULSE]),
      tau: LagFromParam(this.params[PT.COIL_LAG]),
      bpm: this.bpm,
      barPhase: this.barPhase,
      hostBeat: this.hostBeatSeen,
    };
    this.firePresses = 0;
    this.drive.Advance(this.now, dt, input_);

    // The surface: the substeps, then the camera's copy of it.
    this.plan = this.planSubsteps(dt);
    if (!this.packValid) this.repack();
    for (let s = 0; s < this.plan.substeps; s += 1) {
      this.substep(this.drive.FrameStart() + (s + 0.5) * this.plan.dt, this.plan.dt, this.plan);
    }
    this.fluidTime += this.plan.substeps * this.plan.dt;
    this.lostTime += this.plan.lost;
    this.surfaceAndBounds();

    // The picture.
    this.render(this.isEffect ? input : null, width, height, target);
    return true;
  }

  disposeBuffers() {
    const gl = this.gl;
    if (this.buffers) for (const all of Object.values(this.buffers)) for (const b of [].concat(all)) b.dispose();
    this.buffers = null;
    for (const t of [this.twiddleX, this.twiddleY, this.cameraTwiddleX, this.cameraTwiddleY]) if (t) gl.deleteTexture(t);
    this.twiddleX = this.twiddleY = this.cameraTwiddleX = this.cameraTwiddleY = null;
  }

  dispose() {
    const gl = this.gl;
    this.disposeBuffers();
    for (const t of [this.coilTable, this.fluidTable, this.coefficientTexture, this.basisTexture, this.equilibriumTexture]) {
      if (t) gl.deleteTexture(t);
    }
    gl.deleteFramebuffer(this.mrtFBO);
  }
}

// ==== END FRAME ============================================================

//===========================================================================
// The transport: the page's BPM clock, standing in for Resolume's.
//===========================================================================

const query = new URLSearchParams(window.location.search);
const BPM_LOW = 20;
const BPM_HIGH = 300;
const queryBpm = Number(query.get('bpm'));
const transport = {
  bpm: Number.isFinite(queryBpm) && queryBpm >= BPM_LOW && queryBpm <= BPM_HIGH ? queryBpm : 120,
  bars: 0, ///< bars since the top, accumulated from the page's seconds
  last: null, ///< the page's seconds at the last frame
  /** The bar phase at the page's `seconds`: advanced at the BPM, four beats to the bar. */
  advance(seconds) {
    if (this.last === null || seconds < this.last) this.bars = 0; // a start, or Restart
    else this.bars += ((seconds - this.last) * this.bpm) / 240;
    this.last = seconds;
    return this.bars - Math.floor(this.bars);
  },
};

//===========================================================================
// The renderer: one plugin instance, driven as a host drives it.
//===========================================================================

function createRenderer(gl, quad) {
  // The camera's surface is RGBA32F read through texture() with LINEAR
  // filtering, which WebGL2 allows a float texture only with this. Without it
  // the texture is incomplete, every fetch reads zero and the pool would
  // render flat: a plausible wrong picture, so the page stops instead.
  if (!gl.getExtension('OES_texture_float_linear')) {
    throw new GLError('This browser can’t filter a 32-bit float texture (OES_texture_float_linear), and the plugin’s camera reads the pool’s surface that way. Without it every spike would read as zero and the pool would look flat, so the demo stops here.');
  }

  const vertex = S.VERSION + S.QUAD_VERTEX;
  const assemble = (piece) => S.VERSION + S.COMMON + piece;
  const programs = {
    fft: new Program(gl, vertex, assemble(S.FFT_FRAGMENT), 'fft'),
    evolve: new Program(gl, vertex, assemble(S.EVOLVE_FRAGMENT), 'evolve'),
    force: new Program(gl, vertex, assemble(S.FORCE_FRAGMENT), 'force'),
    pack: new Program(gl, vertex, assemble(S.PACK_FRAGMENT), 'pack'),
    bounds: new Program(gl, vertex, assemble(S.BOUNDS_FRAGMENT), 'bounds'),
    render: new Program(gl, vertex, assemble(S.RENDER_FRAGMENT), 'render'),
    spikes: new Program(gl, vertex, assemble(S.SPIKES_FRAGMENT), 'spikes'),
    deposit: new Program(gl, vertex, assemble(S.DEPOSIT_FRAGMENT), 'deposit'),
    combine: new Program(gl, vertex, assemble(S.COMBINE_FRAGMENT), 'combine'),
  };

  let instance = null;
  let instanceVariant = null;
  let frames = 0;
  let fpsAt = performance.now();
  let fpsFrames = 0;
  let fps = 0;
  let stats = null;

  //-------------------------------------------------------------------------
  // The page's clock governor: the one thing this page does that the plugin
  // doesn't. A frame is a dozen substeps of the plugin's plan at 60 fps (more
  // past onset), twelve passes each, and a browser that can't draw them in
  // real time spirals: slower frames are longer frames, the plan takes more
  // substeps for each, and it ends up at the plugin's cap of 48, dropping time
  // at a few frames a second. So while the frames come slower than 20 a second
  // AND the plan is still running more than six substeps a frame (so the
  // substeps are where the time goes, not the picture), the host time the
  // plugin and the transport see runs slower than the page's: the pool and the
  // beat together, in slow motion. It creeps back to real time while frames
  // come faster than 30 a second, or when the plan is down to four substeps a
  // frame and slowing it further would only freeze the pool. Each frame is
  // still what the plugin computes for the time it is given; nothing about the
  // pool, the plan or the passes changes. The status line gives the speed.
  //-------------------------------------------------------------------------
  const kSlowest = 0.02;
  let hostTime = 0;
  let lastPageTime = null;
  let lastWall = null;
  let interval = null;
  let speed = 1;
  function hostTimeFor(time, lastSubsteps) {
    const wall = performance.now() / 1000;
    if (lastPageTime === null || time < lastPageTime) {
      hostTime = time; // the start, or Restart: the plugin sees the clock go back
    } else {
      const delta = time - lastPageTime;
      const gap = lastWall === null ? 0 : wall - lastWall;
      // Only frames that played: a pause, a Step after one, a hidden tab are not a frame rate.
      if (delta > 0 && gap > 0 && gap < 0.5) {
        interval = interval === null ? gap : 0.8 * interval + 0.2 * gap;
        if (interval > 1 / 20 && lastSubsteps > 6) speed = Math.max(kSlowest, speed * 0.92);
        else if (interval < 1 / 30 || lastSubsteps <= 4) speed = Math.min(1, speed * 1.04 + 0.005);
      }
      hostTime += delta * speed;
    }
    lastPageTime = time;
    lastWall = wall;
    return hostTime;
  }

  return {
    programs,
    get stats() { return stats; },
    get instance() { return instance; },
    newInstance: (isEffect) => new FerrofluidPlugin(gl, quad, programs, isEffect),

    render({ input, params, width, height, time, variant }) {
      if (!instance || instanceVariant !== variant) {
        instance?.dispose();
        instance = new FerrofluidPlugin(gl, quad, programs, variant === 'over');
        instanceVariant = variant;
      }
      const plugin = instance;

      // What the host sets: the page's controls, as the floats a host sends.
      for (const p of PARAMS) {
        if (p.only && p.only !== variant) continue;
        plugin.SetFloatParameter(p.slot, params.get(p.id));
      }
      const fired = params.get('fire') > 0.5;
      const settled = params.get('settle') > 0.5;
      const now = hostTimeFor(time, plugin.plan.substeps);
      plugin.SetBeatInfo(transport.bpm, transport.advance(now));

      plugin.ProcessOpenGL(input, width, height, now, null);

      // A host lets go of an event; the kit has none, so the page does.
      if (fired) params.set('fire', 0);
      if (settled) params.set('settle', 0);

      frames += 1;
      fpsFrames += 1;
      const wall = performance.now();
      if (wall - fpsAt >= 1000) {
        fps = (fpsFrames * 1000) / (wall - fpsAt);
        fpsAt = wall;
        fpsFrames = 0;
      }
      stats = {
        frames,
        fps,
        plan: plugin.plan,
        current: plugin.drive.Current(0),
        currents: [0, 1, 2].map((j) => plugin.drive.Current(j)),
        criticalCurrent: plugin.criticalCurrent,
        m: MagneticNumber(plugin.Fluid(), plugin.plan.stressMax),
        fluid: plugin.Fluid().name,
        magnet: plugin.CurrentMagnet(),
        fluidTime: plugin.fluidTime,
        lostTime: plugin.lostTime,
        pulses: plugin.drive.fired,
        speed,
      };
    },
  };
}

//===========================================================================
// The parameters: the constructor's, in HostOrder()'s order and groups, with
// the plugin's names and defaults. `only` says which plugin declares it.
// Audio, Band and Threshold are absent: see the header.
//===========================================================================

const fixed = (n, d) => n.toFixed(d);
const sig = (n) => (n >= 100 ? n.toFixed(0) : n >= 10 ? n.toFixed(1) : n >= 1 ? n.toFixed(2) : n.toFixed(3));
const V = (v) => f32(v);
const ms = (s) => `${sig(s * 1000)} ms`;
const cm = (m) => `${(m * 100).toFixed(1)} cm`;
const mm = (m) => `${(m * 1000).toFixed(1)} mm`;
const pascals = (pa) => {
  if (pa <= 0) return 'off';
  const exponent = Math.floor(Math.log10(pa));
  return `${(pa / 10 ** exponent).toFixed(2)}e${exponent} Pa`;
};

const SPEC = [
  ['fireOn', PT.FIRE_ON, null, 'What fires a pulse of current. Beat, Half Beat, 2 Beats and Bar follow the transport: on this page that’s the Host BPM clock under the picture, in Resolume the composition’s tempo. Onset fires on hits in Resolume’s audio, which a browser hasn’t got, so here it never fires. Off: only Fire.'],
  ['fire', PT.FIRE, null, 'A pulse now, Pulse long and Gain high. It’s FF_TYPE_EVENT in the plugin; here it’s a toggle the page lets go of after one frame.'],
  ['gain', PT.GAIN, (v) => `${fixed(CurrentFromParam(V(v)), 3)} Ic`, 'How far a pulse pushes the coil, in critical currents, 0 to 1.5. In Resolume the music’s level adds to it as well; there’s no music here.'],
  ['bias', PT.BIAS, (v) => `${fixed(CurrentFromParam(V(v)), 3)} Ic`, 'The steady current, in critical currents, 0 to 1.5. 1 is where the flat pool goes unstable, whatever the fluid and the magnet. The default, 0.98, sits just under it and inside EFH1’s hysteresis loop, so a beat raises spikes and the quiet between beats doesn’t knock them down.'],
  ['coilLag', PT.COIL_LAG, (v) => ms(LagFromParam(V(v))), 'The coil’s L/R, 1 to 300 ms: how long its current takes to follow what it’s asked for. At 300 ms a beat barely gets it going before it falls again.'],
  ['pulse', PT.PULSE, (v) => ms(PulseFromParam(V(v))), 'How long each pulse lasts, 10 ms to 1 s.'],

  ['magnet', PT.MAGNET, null, 'Uniform: a Helmholtz pair, the same field across the pool. Coil: one solenoid under the middle, so the spikes appear there first and spread as the current rises. Three Coils: three in a triangle, which follow Low, Mid and High in Resolume; with no music here, all three move only with the pulses.'],
  ['coilSize', PT.COIL_SIZE, (v) => mm(CoilSizeFromParam(V(v))), 'The solenoid’s radius, 5 mm to 8 cm. Uniform has no coil to size.'],
  ['coilSpread', PT.COIL_SPREAD, (v) => cm(SpreadFromParam(V(v))), 'Three Coils: the side of their triangle, 2 to 30 cm. Nothing else has a spread.'],

  ['fluid', PT.FLUID, null, 'Which datasheet: Ferrotec’s EFH1, or EMG 901 or EMG 909 as Mahr & Rehberg measured them. Another fluid is another pool, so it starts flat.'],
  ['viscosity', PT.VISCOSITY, (v) => `×${sig(ViscosityFromParam(V(v)))}`, 'A multiple of the datasheet’s viscosity, 0.25× to 100×.'],
  ['noise', PT.NOISE, (v) => pascals(NoiseFromParam(V(v))), 'The random pressure the spikes grow from: 0, then 1 µPa to 0.1 Pa RMS over a square millimetre and a millisecond. At 0 a flat pool stays flat however hard it’s driven, as the textbook’s does.'],
  ['settle', PT.SETTLE, null, 'Flattens the pool. It’s FF_TYPE_EVENT in the plugin; here it’s a toggle the page lets go of after one frame.'],

  ['fieldWidth', PT.FIELD_WIDTH, (v) => cm(FieldWidthFromParam(V(v))), 'How much of the pool the frame shows, 2 to 40 cm across. The pool is periodic, so near 40 cm with EMG 901 you can see the edge of the next copy.'],
  ['tilt', PT.TILT, (v) => `${fixed((TiltFromParam(V(v)) * 180) / kPi, 1)}°`, 'The camera’s angle from straight down, 0° to 60°, through a 100 mm lens, as a product photographer would shoot it.'],
  ['height', PT.HEIGHT, (v) => cm(HeightFromParam(V(v))), 'SW Ferro Over: how far above the pool the screen showing your clip hangs, 2 cm to 1 m.', 'over'],
  ['exposure', PT.EXPOSURE, (v) => `${ExposureFromParam(V(v)) >= 0 ? '+' : '−'}${fixed(Math.abs(ExposureFromParam(V(v))), 2)} stops`, '±4 stops. At 0, a soft box (or the clip, in the Over) seen straight on in a flat pool shows at its own brightness. The fluid only reflects about 4.5%, and the exposure makes up for it.'],
  ['lights', PT.LIGHTS, null, 'SW Ferrofluid: the studio’s soft boxes. Every spike carries a reflection of each one.', 'source'],
  ['backdrop', PT.BACKDROP, null, 'SW Ferrofluid: Black, or Studio, a seamless sweep that dims towards the floor.', 'source'],
  ['mix', PT.MIX, (v) => fixed(V(v), 2), 'SW Ferro Over: the pool against the clip. At 0 the clip comes back as it went in.', 'over'],
];

const initialVariant = query.get('plugin') === 'over' ? 'over' : 'source';

function defaultsFor(variant) {
  const params = DefaultParams(variant === 'over');
  return Object.fromEntries(SPEC.map(([id, slot]) => [id, params[slot]]));
}

const PARAMS = SPEC.map(([id, slot, display, hint, only]) => {
  const info = INFO[slot];
  if (!HostOrder(only !== 'source').includes(slot) || info.kind === 'buffer') throw new Error(`${id}: not a declared parameter`);
  const p = { id, slot, name: info.name, group: info.group, hint, only };
  if (info.kind === 'option') Object.assign(p, { type: 'option', elements: info.options });
  else if (info.kind === 'event') p.type = 'boolean';
  else Object.assign(p, { type: 'standard', display });
  p.default = defaultsFor(initialVariant)[id];
  return p;
});

let renderer = null;

const mounted = mountDemo({
  name: 'Ferrofluid',
  pluginId: 'FE01 · FE02',
  kind: ['source', 'effect'],
  tagline:
    'Ferrofluid spikes on the beat. A pool of magnetic fluid over an electromagnet whose current is the music, seen as the black mirror it is. Past a critical field the flat surface stands up in spikes on a hexagonal lattice, all at once, and they hang on after the kick has gone. Nothing is drawn: each spike is the fluid’s own, solved offline with its magnetisation curve, gravity and surface tension, and what you see in it is what it reflects. SW Ferrofluid is the pool in a photographer’s studio; SW Ferro Over hangs your clip above it and breaks it up in the mirror.',
  repo: 'https://github.com/stoatworks-labs/ferrofluid',

  blurb:
    'It’s Ferrofluid’s own GLSL, all nine passes, ported from the repository to WebGL2 unedited and running on the plugin’s own 512 × 256 spectral pool, with its table of solved spikes copied in as data. A JavaScript port of the plugin’s CPU half (the fluid’s magnetisation, the coils’ fields, the drive and the substep plan) feeds it, and only a reader checks that. There’s no audio here, so the beat is a clock the page keeps. SW Ferrofluid reads no video; SW Ferro Over runs on generated clips or your own.',

  needFloat: true,

  variants: {
    label: 'Plugin',
    default: initialVariant,
    options: [
      { id: 'source', name: 'SW Ferrofluid (source)', hint: 'FE01, FF_SOURCE: the pool over one coil in a photographer’s studio, lit by soft boxes.' },
      { id: 'over', name: 'SW Ferro Over (effect)', hint: 'FE02, FF_EFFECT: the pool in a Helmholtz pair, seen from above, with your clip on a screen hung over it.' },
    ],
  },

  // For the Over only. The scene is a picture to break up; the geometry card
  // shows where each spike's mirror throws the screen; lights on black is the
  // clip's highlights scattered across the lattice.
  sources: ['scene', 'grid', 'spot', 'bars', 'ramp', 'detail', 'alpha'],

  params: PARAMS,

  differences: [
    'The CPU half is a port, and only a reader checks it. The plugin works out on the CPU, in double: every control’s mapping (Controls.cpp); the fluid’s Langevin magnetisation, solved across the demagnetising jump by bisection, and its critical field, wavenumber and optics (Fluid.cpp); the coils’ fields, a loop’s by complete elliptic integrals and the solenoid’s as a Gauss–Legendre sum of loops, and the coil’s L/R lag (Coil.cpp); the beat, the pulses and the current (Drive.cpp); and, every frame, the critical current, the tables the GPU reads, the plan of up to 48 substeps, the camera, the soft boxes and every uniform of every pass (Ferrofluid.cpp). Here all of that is JavaScript, whose numbers are doubles too. Nothing in the repository runs the port against the C++. It was compared with the plugin’s own harness once, by hand, when the page was built: driven frame for frame as the harness drives the plugin, at the harness’s defaults three seconds in and at the README’s first picture seven seconds in, the page planned the same substeps and got the same coil current and magnetic number to the figures the harness prints, and 96% and 99.9% of the pixels came out within one level of 255 of the harness’s. In the settled lattice the rest are glints on the spikes’ edges; in the default’s growing patch a few spikes also stand a little higher or lower, because an instability grows small differences and two shader compilers never round quite alike.',
    'The GPU half isn’t a port: the fft, force, evolve, pack, spikes, deposit, combine, bounds and render passes are the plugin’s own GLSL, on the same 32-bit float buffers, and demo/tools/check_shaders.py fails the repository’s verify script if a character of them, or the way they’re joined, drifts. WebGL2 needs EXT_color_buffer_float to render into those buffers and OES_texture_float_linear to filter them, and the page stops and says so without either.',
    'The solved spikes are copied in as data. The table the plugin uploads (each fluid’s spike against the field on a four-kernel basis, and the pattern engine’s own equilibrium) comes straight out of the generated SpikeTable.cpp, and the same check fails if a number of it drifts. The solver that made the table (tools/spikes/, hours of nonlinear magnetostatics) isn’t in the page and doesn’t need to be.',
    'There’s no audio. The plugin reads Resolume’s 64-bin audio spectrum, and a browser has none to give it, so Audio, Band and Threshold are left off the panel rather than shown doing nothing. With silence in that buffer the plugin’s analyser reports a level of 0 in every band and never fires an onset, which is what this page hands the drive. So Gain moves the coil only on pulses, Fire On’s Onset never fires, and Three Coils, which follow Low, Mid and High in Resolume, all move together on the pulses alone.',
    'The tempo is simulated. Resolume hands the plugin its BPM and bar phase every frame; this page keeps a BPM clock of its own (the Host BPM field under the picture, 120 at first) and hands the plugin that BPM and a bar phase advanced from the page’s own seconds, as the plugin’s harness does. Beat, Half Beat, 2 Beats and Bar fire on it. You can’t hear it: there’s no tap tempo and no music for it to follow. Pause stops it with the clock, and Restart puts it back to the top of a bar.',
    'The clock is the page’s: seconds from frame deltas, capped at a tenth of a second a frame, as the plugin caps its own. The plugin’s vote on whether the host sends seconds or milliseconds isn’t ported, because this host’s unit is known. The first frame has no time in it, as in the plugin. Restart takes the clock backwards, which the plugin treats as no time passing, so the pool carries on as it was; Settle flattens it. Step is one frame of a sixtieth of a second.',
    'The pool is the plugin’s full 512 × 256 grid and the substep plan is its own; nothing is made smaller for the browser. That’s a dozen substeps a frame at 60 fps with the defaults, twelve passes each, and twenty-seven passes more, which is more than some browsers can draw in real time. So the page does one thing the plugin doesn’t: when the frames come slower than 20 a second and the substeps are where the time goes, it slows its own clock, the pool and the beat together, and lets it creep back to real time once they come faster than 30 a second. The pool then runs in slow motion, but each frame is still what the plugin computes for the time it’s given. The line under the picture gives the speed. The plugin’s own cap is still there too (48 substeps a frame, the excess time dropped rather than run unstable), and the line says when that bites; a frame or two at the cap while the page loads is normal.',
    'Fire and Settle are FF_TYPE_EVENT in the plugin; here they’re toggles the page lets go of after one frame, one rising edge each, which is what the plugin counts. The About block isn’t on the panel.',
    'Switching the Plugin dropdown makes a new instance, as it would in Resolume: a flat pool and that plugin’s own defaults. SW Ferro Over puts the pool in the Helmholtz pair and looks straight down, and declares Height and Mix where SW Ferrofluid declares Lights and Backdrop.',
    'In the Over the clip is the kit’s generated one (or your own file), 8-bit and premultiplied, as Resolume hands an effect its input. The README’s Over picture was made with one of Resolume’s bundled clips, which isn’t here.',
    'Nothing here is measured. The plugin’s harness holds every mode of the pool to the Cowley–Rosensweig rate to 1.3e-5 of its height, puts the onset within a quarter of a per cent of the theory’s current and the lattice at the critical wavelength with its peaks 60° apart, and holds each spike’s summit to the solved one’s to 0.16%, with twenty-one deliberately wrong models and sixteen mutants all caught. That harness, not this page, is the reason to believe the pool. The plugin itself has been loaded into Resolume Arena 7.28.1 on macOS, where both plugins register and their controls change Arena’s output; the README’s Status says what was and wasn’t tested there.',
  ],

  createRenderer: (gl, quad) => {
    renderer = createRenderer(gl, quad);
    return renderer;
  },
});

//---------------------------------------------------------------------------
// Which controls belong to which plugin; the Host BPM; the status line.
//
// By inline style, not the `hidden` attribute: kit.css gives these elements a
// `display` of their own, which beats the attribute's user-agent rule. Rows
// are found by their control's id, falling back to the name for the toggles,
// which carry no id.
//---------------------------------------------------------------------------
if (mounted) {
  const params = mounted.params;
  const embed = query.has('embed') && query.get('embed') !== '0';
  let shownVariant = mounted.state.variant;

  // Copy link carries the plugin and the BPM, since they are not parameters.
  const toQuery = params.toQuery.bind(params);
  params.toQuery = () => {
    const q = toQuery();
    if (mounted.state.variant === 'over') q.set('plugin', 'over');
    if (transport.bpm !== 120) q.set('bpm', String(transport.bpm));
    return q;
  };

  const show = (node, on) => { if (node) node.style.display = on ? '' : 'none'; };
  const rowOf = (p) => document.getElementById(`p-${p.id}`)?.closest('.prow')
    ?? [...document.querySelectorAll('.prow')].find((row) => row.querySelector('.prow__name')?.textContent === p.name);

  function showForVariant(variant) {
    for (const p of PARAMS) if (p.only) show(rowOf(p), p.only === variant);
    const effect = variant === 'over';
    for (const field of document.querySelectorAll('.transport__field')) {
      if (field.querySelector('.transport__label')?.textContent === 'Clip') show(field, effect);
    }
    show(document.querySelector('.transport__file'), effect);
  }

  document.addEventListener('demo:state', () => {
    const variant = mounted.state.variant;
    if (variant !== shownVariant) {
      shownVariant = variant;
      // A new instance: that plugin's constructor's defaults.
      params.defaults = defaultsFor(variant);
      params.reset();
    }
    if (!embed) showForVariant(variant);
  });

  if (!embed) {
    showForVariant(shownVariant);

    //-----------------------------------------------------------------------
    // The transport's BPM: what Resolume's would hand the plugin.
    //-----------------------------------------------------------------------
    const bar = document.querySelector('.transport');
    if (bar) {
      const label = document.createElement('label');
      label.className = 'transport__field';
      const span = document.createElement('span');
      span.className = 'transport__label';
      span.textContent = 'Host BPM';
      const bpm = document.createElement('input');
      bpm.type = 'number';
      bpm.className = 'prow__text';
      bpm.min = String(BPM_LOW);
      bpm.max = String(BPM_HIGH);
      bpm.step = '1';
      bpm.value = String(transport.bpm);
      bpm.inputMode = 'numeric';
      bpm.style.width = '6em';
      bpm.style.flex = '0 0 auto';
      bpm.id = 'ferrofluid-bpm';
      label.title = 'A simulated transport: this BPM, and a bar phase from this page’s clock, handed to the plugin every frame as Resolume hands over the composition’s. Fire On’s Beat, Half Beat, 2 Beats and Bar fire on it. You can’t hear it.';
      bpm.addEventListener('change', () => {
        const v = Number(bpm.value);
        if (Number.isFinite(v) && v >= BPM_LOW && v <= BPM_HIGH) {
          transport.bpm = v;
          if (!mounted.state.playing) mounted.redraw();
        } else {
          bpm.value = String(transport.bpm);
        }
      });
      label.append(span, bpm);
      bar.append(label);
    }

    //-----------------------------------------------------------------------
    // A status line: what the port computed this frame, as fetest's summary
    // line reports it. It reports; it measures nothing.
    //-----------------------------------------------------------------------
    const stage = document.querySelector('.stage');
    if (stage) {
      const line = document.createElement('p');
      line.className = 'stage__status';
      line.id = 'ferrofluid-stats';
      line.setAttribute('aria-live', 'off');
      stage.append(line);
      const magnets = ['the Helmholtz pair', 'one coil', 'three coils'];
      setInterval(() => {
        const s = renderer?.stats;
        if (!s) return;
        const bars = transport.bars;
        const beat = Math.floor((bars - Math.floor(bars)) * 4) + 1;
        const amps = s.current * s.criticalCurrent;
        const lost = s.lostTime > 0 ? ` · ${fixed(s.lostTime, 2)} s dropped at the cap so far` : '';
        line.textContent =
          `${s.fluid} in ${magnets[s.magnet]} · ${s.plan.substeps} substeps of ${fixed(s.plan.dt * 1e3, 3)} ms`
          + ` · coil at ${fixed(s.current, 3)} Ic (${fixed(amps, 2)} A of ${fixed(s.criticalCurrent, 2)} A critical)`
          + ` · m ${fixed(s.m, 3)} · ${s.pulses} pulse${s.pulses === 1 ? '' : 's'} fired`
          + ` · transport ${transport.bpm} BPM, bar ${Math.floor(bars) + 1} beat ${beat}`
          + ` · ${fixed(s.fps, 0)} frames a second`
          + (s.speed < 0.995 ? ` · the page’s clock slowed to ${fixed(100 * s.speed, 0)}% of real time to keep up` : '')
          + lost;
      }, 250);
    }
  }

  // For the headless checks in AGENTS.md: the page's pieces, read-only by convention.
  window.ferrofluidDemo = {
    mounted, transport, PARAMS, PT, get renderer() { return renderer; },
    port: { CriticalApplied, CriticalWavelength, FluidOf, SolenoidAxial, HelmholtzField, OpticsOf, NormalReflectance, Magnetise, MagneticNumber },
  };
}
