#pragma once

#include <string>

/**
    The passes.

    ------------------------------------------------------------- the surface

    The state is SPECTRAL: ( h_k, v_k ), the unnormalised DFT of the height
    and its rate, on an Nx x Ny periodic grid (the pool). One substep is:

        1. **fft**     inverse, RGBA32F: the pack, Norm ( W h_k + i |k| h_k,
                       h_k ), to real space, giving ( W h, |grad| h, h ) --
                       two real fields in one complex transform, and h in
                       the other.
        2. **force**   RGBA32F: in real space, the model's bracket
                       m ( Hy g^2 - Sa g^3 ) with g = W h, scaled by the local
                       magnetic number (Fluid.h); the local-field residual's
                       two halves, ( G( x ) - G_ref ) |grad| h and
                       ( G( x ) - G_ref ) h; and the noise floor.
        3. **fft**     forward, RGBA32F: the stresses to the spectrum.
        4. **evolve**  RGBA32F x 2 (two targets): every mode kicked by
                       ( k / rho ) N_k dt -- the bracket weighted by W and
                       low-passed above the plan's outer cut, the residual's
                       second half given its |k| -- then advanced EXACTLY over
                       dt as the damped oscillator with the Cowley-Rosensweig
                       omega( k ) at G_ref and Lamb's damping; and the next
                       substep's pack.

    So the linear theory is exact per mode for any dt (millpond's propagator),
    and only the stress is explicit: a kick then an exact drift is symplectic
    Euler, which is why a stiff restoring stress cannot pump energy in the way
    an explicit force held over an exact step does (AGENTS.md, the traps).

    The transform is a Stockham autosort FFT of mixed radix (16, 8, 4, 2:
    Govindaraju et al., SC 2008), one output per fragment, twiddles from a
    double-precision table: a 512 x 256 grid is five passes a transform
    (16, 16, 2 along the 512; 16, 16 along the 256).

    Once a frame:

        5. **pack**    RGBA32F: Norm ( h_k, i k_x h_k + i ( i k_y h_k ) ).
        6. **fft**     inverse, RGBA32F: ( h, 0, h_x, h_y ) in real space, the
                       surface the camera sees.
        7. **bounds**  max | h | and max | grad h | by 8 x 8 blocks, down to 1 x 1:
                       the march's slab and its Lipschitz step, read by the
                       render pass from the texture (no read-back).
        8. **render**  the host's frame: a camera ray marched to the height
                       field, Fresnel reflection of an absorbing dielectric,
                       one further bounce off a neighbouring spike, and the
                       environment -- the clip on a screen above (Over) or a
                       dark studio's soft boxes (source).

    The fragment shaders are assembled at InitGL -- kVersion + kCommon + the
    pass -- by `Assemble()`, the one place the order is written;
    tools/glslc.sh's ASSEMBLED table mirrors it.

    Trig is used in exactly one pass: the evolve's cos and sin of omega dt (the
    oscillator's step; millpond's precedent). Nothing else needs any: the
    camera's basis and the soft boxes come from the CPU.
*/
namespace ferrofluid::shaders
{

extern const char* const kVersion;
extern const char* const kQuadVertex;
extern const char* const kCommon;
extern const char* const kFFTFragment;
extern const char* const kEvolveFragment;
extern const char* const kForceFragment;
extern const char* const kPackFragment;
extern const char* const kBoundsFragment;
extern const char* const kRenderFragment;

enum class Pass
{
	FFT,
	Evolve,
	Force,
	Pack,
	Bounds,
	Render
};
std::string Assemble( Pass pass );

/// The largest radix one FFT pass takes.
constexpr int kMaxRadix = 16;
/// Entries in the coil profile and fluid response tables.
constexpr int kTableSize = 1024;
/// Soft boxes the render pass carries.
constexpr int kMaxBoxes = 4;

/// Negative-control hooks: bits of the `Hooks` uniform. A shipped instance
/// sets 0, and every branch they guard is then the shipped model.
enum Hook : int
{
	kHookNoTension     = 1 << 0,///< the evolve without sigma k^3
	kHookFresnelOne    = 1 << 1,///< reflectance 1 everywhere (--mirror's test hook)
	kHookUniformSky    = 1 << 2,///< every escaping ray sees radiance 1 (--fresnel's test hook)
	kHookFlipScreen    = 1 << 3,///< the Over's screen read unmirrored
	kHookSPolarised    = 1 << 4,///< Fresnel's s term alone, not the unpolarised mean
	kHookNoLocalField  = 1 << 5,///< the local-field residual dropped: the pool sees G_ref everywhere
	kHookNoKick        = 1 << 6,///< the stress never reaches the modes
	kHookHeightView    = 1 << 7 ///< the render shows the height as grey (the harness's picture)
};

} // namespace ferrofluid::shaders
