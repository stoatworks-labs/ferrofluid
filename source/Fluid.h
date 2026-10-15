#pragma once

/**
    The fluid: its datasheet, its magnetisation, the Cowley--Rosensweig
    dispersion relation, and the model's nonlinear constants. Plain C++ in
    double, no GL, so `fetest --offline` checks it and the plugin and the
    harness read the same numbers.

    ---------------------------------------------------------- the linear theory

    Cowley & Rosensweig (J. Fluid Mech. 30, 671, 1967; Rosensweig,
    Ferrohydrodynamics, 1985, ch. 7). For a layer deep compared with the
    wavelength (depth > lambda_c / 2), a surface mode h_k under a normal field obeys

        rho omega^2 = rho g k + sigma k^3 - mu0 M^2 ( r0 / ( 1 + r0 ) ) k^2
        r0 = sqrt( mu_c mu_t ) / mu0

    with mu_c = B / H and mu_t = dB / dH the chord and tangent permeabilities
    inside the fluid at its operating point. The flat surface first goes
    unstable at k_c = sqrt( rho g / sigma ), when

        mu0 M^2 r0 / ( 1 + r0 ) = 2 sqrt( rho g sigma ).

    This file calls the left-hand side G, "the magnetic stress coefficient" (Pa):
    the magnetic term of the dispersion is -G k^2 / rho, and onset is G = G_c =
    2 sqrt( rho g sigma ). The magnetic number m = G k_c / ( 2 rho g ) is 1 at
    onset; everything nonlinear below is written in it.

    ------------------------------------------------------------- magnetisation

    Langevin, M( H ) = M_s L( 3 chi H / M_s ), L( x ) = coth x - 1 / x, so the
    initial susceptibility is chi and the saturation M_s. The coil's field is
    the field it would make in empty space, H_a; across a flat surface in a
    normal field B is continuous, so inside the fluid H_in = H_a - M( H_in ),
    which Magnetise() solves.

    -------------------------------------------------------- the nonlinear model

    The surface's nonlinear normal stress is the model's, NOT derived from
    Gailitis's energy expansion (J. Fluid Mech. 82, 1977) or Friedrichs & Engel
    (Phys. Rev. E 64, 2001): that was the preferred route and was not done
    honestly in the time (AGENTS.md). It is

        N( x ) = rho g m( x ) [ Hy k_c h^2 - Sa k_c^2 h^3 ]

    -- the gradient of a local energy -Hy h^3 / 3 + Sa h^4 / 4 (in k_c units)
    scaled with the local magnetic number, so it vanishes where there is no
    field. Hy (Hysteresis) breaks up/down symmetry: it is what makes hexagons
    and a subcritical, hysteretic onset. Sa (Saturation) bounds the spikes.
    Both are CHOSEN constants, exposed as controls.

    ThreeMode() is the model's own weakly nonlinear reduction: three modes at
    k_c 120 degrees apart, with the six harmonics at sqrt( 3 ) k_c and the six
    at 2 k_c slaved adiabatically (the quadratic term drives them and they feed
    back at fourth order). It predicts the hexagons' amplitude and the
    saddle-node where they collapse on a down-sweep; `fetest --hysteresis`
    holds the GPU solver to it.
*/
namespace ferrofluid::physics
{

constexpr double kPi = 3.14159265358979323846;
/// CODATA 2018's vacuum permeability, H/m.
constexpr double kMu0 = 1.25663706212e-6;
/// Standard gravity, m/s^2.
constexpr double kGravity = 9.80665;

/// A commercial fluid, as its datasheet states it. SI units throughout.
struct Datasheet
{
	const char* name;
	double density;   ///< kg/m^3
	double tension;   ///< surface tension, N/m
	double viscosity; ///< dynamic, Pa s
	double chi;       ///< initial susceptibility, SI (dimensionless)
	double saturation;///< M_s, A/m
	const char* source;
};

enum class FluidId
{
	EFH1 = 0,
	EMG901,
	EMG909,
	Count
};
const Datasheet& FluidOf( FluidId id );

//---------------------------------------------------------------------------
// Magnetisation.
//---------------------------------------------------------------------------
double Langevin( double x );
double LangevinSlope( double x );

/// M and dM/dH at an internal field H, A/m.
double Magnetisation( const Datasheet& fluid, double hInside );
double Susceptibility( const Datasheet& fluid, double hInside );

/// The fluid's state in a normal applied (empty-space) field H_a.
struct Operating
{
	double applied = 0.0;///< H_a, A/m
	double inside  = 0.0;///< H_in, A/m
	double m       = 0.0;///< M, A/m
	double chord   = 1.0;///< mu_c / mu0 = 1 + M / H_in
	double tangent = 1.0;///< mu_t / mu0 = 1 + dM/dH
	double r0      = 1.0;///< sqrt( mu_c mu_t ) / mu0
	double g       = 0.0;///< G = mu0 M^2 r0 / ( 1 + r0 ), Pa
};

/// Hooks for the negative controls. A shipped instance sets none.
enum PhysicsHook : int
{
	kHookNoDemag    = 1 << 0,///< H_in = H_a: the demagnetising jump forgotten
	kHookChordOnly  = 1 << 1,///< r0 = mu_c / mu0: the tangent permeability forgotten
	kHookNoR0Factor = 1 << 2 ///< G = mu0 M^2: the r0 / ( 1 + r0 ) factor dropped
};

Operating Magnetise( const Datasheet& fluid, double applied, int hooks = 0 );

//---------------------------------------------------------------------------
// The linear theory.
//---------------------------------------------------------------------------
double CriticalWavenumber( const Datasheet& fluid );///< k_c, 1/m
double CriticalWavelength( const Datasheet& fluid );///< 2 pi / k_c, m
double CriticalG( const Datasheet& fluid );         ///< G_c = 2 sqrt( rho g sigma ), Pa
/// M_c^2 = ( 2 / mu0 )( 1 + 1 / r0 ) sqrt( rho g sigma ), at the operating point's r0.
double CriticalMagnetisationSquared( const Datasheet& fluid, double r0 );
/// The applied field at onset, A/m (bisection on Magnetise().g).
double CriticalApplied( const Datasheet& fluid, int hooks = 0 );
/// m = G k_c / ( 2 rho g ).
double MagneticNumber( const Datasheet& fluid, double g );

/// rho omega^2 / rho, s^-2, for wavenumber k at stress coefficient G.
double OmegaSquared( const Datasheet& fluid, double k, double g );
/// Lamb's weak-damping rate 2 nu k^2 (amplitude), with the viscosity scaled.
double Damping( const Datasheet& fluid, double k, double viscosityScale );

/// Optical constants of the fluid, from a Maxwell-Garnett mix of bulk
/// magnetite in its carrier oil at the volume fraction M_s implies
/// (ASSUMED constants: AGENTS.md).
struct Optics
{
	double n = 1.5, k = 0.0;
	double fraction = 0.0;
};
Optics OpticsOf( const Datasheet& fluid );
/// Unpolarised reflectance at normal incidence, ( ( n - 1 )^2 + k^2 ) / ( ( n + 1 )^2 + k^2 ).
double NormalReflectance( const Optics& optics );
/// Unpolarised reflectance at incidence cos theta, exact for an absorbing medium.
double Reflectance( const Optics& optics, double cosIncidence );

//---------------------------------------------------------------------------
// The model's nonlinear constants, and its own three-mode reduction.
//---------------------------------------------------------------------------
struct Nonlinear
{
	double hysteresis = 1.0;///< Hy, dimensionless
	double saturation = 2.0;///< Sa, dimensionless
};

/// The dimensionless linear operator l( q ) = 1 + q^2 - 2 m q, q = k / k_c.
double LinearOperator( double q, double m );

/// The hexagons' effective quartic coefficient after slaving the harmonics:
/// Q( m ) = 22.5 Sa m - 12 Hy^2 m^2 / l( sqrt 3 ) - 3 Hy^2 m^2 / l( 2 ).
double QuarticEffective( const Nonlinear& model, double m );

struct Hexagons
{
	bool exists      = false;
	double amplitude = 0.0;///< A, in k_c units: each of the six modes is A, h = 2 A sum cos
	double sqrt3     = 0.0;///< the slaved sqrt( 3 ) k_c harmonic, k_c units
	double twice     = 0.0;///< the slaved 2 k_c harmonic, k_c units
	double peak      = 0.0;///< the height at a spike's apex, k_c units
};
/// The upper (stable) branch at m, if there is one.
Hexagons ThreeMode( const Nonlinear& model, double m );
/// The saddle-node: the lowest m at which hexagons exist. 1 when Hy = 0.
double SaddleNode( const Nonlinear& model );

} // namespace ferrofluid::physics
