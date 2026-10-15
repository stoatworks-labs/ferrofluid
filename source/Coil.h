#pragma once

/**
    The magnets and their current. Plain C++ in double, no GL.

    **The field of one loop** is exact: a circular loop of radius a carrying
    current I, a field point at radius rho from its axis and height z above its
    plane (Simpson, Lane, Immer & Youngquist, NASA/TM-2013-217919):

        alpha^2 = a^2 + rho^2 + z^2 - 2 a rho,   beta^2 = a^2 + rho^2 + z^2 + 2 a rho
        k^2     = 1 - alpha^2 / beta^2
        H_z     = I / ( 2 pi alpha^2 beta ) [ ( a^2 - rho^2 - z^2 ) E( k^2 ) + alpha^2 K( k^2 ) ]

    with K and E the complete elliptic integrals (parameter m = k^2), taken by
    the arithmetic-geometric mean to the last bit. On the axis it is the
    textbook a^2 / ( 2 ( a^2 + z^2 )^{3/2} ) per amp.

    **The solenoid** under the pool is that loop summed over its winding:
    `kSolenoidTurns` turns spread evenly over `kSolenoidLength`, its top face
    `kSolenoidDepth` below the fluid's surface (10 mm of fluid, which is deep:
    more than half of every fluid's lambda_c, on a 3 mm dish base). The sum is a
    32-point Gauss-Legendre quadrature over the length: the integrand is smooth
    there (z >= depth > 0), and the quadrature's error is far below a float.

    **Uniform** is a Helmholtz pair, (4/5)^{3/2} N I / R across the pool,
    uniform to the pool's size by construction (the textbook case).

    Only the normal component reaches the fluid model: the instability is the
    NORMAL-field one, and the tangential field near a coil's edge (which would
    stretch the spikes into ridges along it) is left out. AGENTS.md says so.

    **The current** follows its drive through the coil's own L/R lag,
    L dI/dt = V - R I, solved in closed form: over any stretch of constant
    drive, I relaxes to V/R as exp( -t R / L ), exact for piecewise-constant
    drive.
*/
namespace ferrofluid::coil
{

/// Complete elliptic integrals of the first and second kind, parameter m = k^2.
void Elliptic( double m, double& K, double& E );

/// H_z of one loop, A/m per amp: radius a, field point rho off the axis and
/// z above the loop's plane. Metres.
double LoopAxial( double a, double rho, double z );
/// The same on the axis, a^2 / ( 2 ( a^2 + z^2 )^{3/2} ).
double LoopOnAxis( double a, double z );

constexpr double kSolenoidLength = 0.040;///< m
constexpr double kSolenoidTurns  = 400.0;
constexpr double kSolenoidDepth  = 0.013;///< its top face below the fluid's surface, m
constexpr double kHelmholtzRadius = 0.20;///< m
constexpr double kHelmholtzTurns  = 200.0;///< per coil

/// H_z at the fluid's surface, per amp, rho off the solenoid's axis.
double SolenoidAxial( double radius, double rho );
/// The Helmholtz pair's uniform field per amp, (4/5)^{3/2} N / R.
double HelmholtzField();

/// Hooks for the negative controls.
enum CoilHook : int
{
	kHookOnAxisEverywhere = 1 << 0///< the on-axis formula used at every radius
};

/// The solenoid's surface field per amp at `count` radii from 0 to rhoMax.
void SolenoidProfile( double radius, double rhoMax, int count, float* out, int hooks = 0 );

/// One step of the L/R circuit, exact for a drive held at `target` (amps)
/// for dt seconds: the current relaxes as exp( -dt / tau ), tau = L / R.
double LagStep( double current, double target, double dt, double tau );

} // namespace ferrofluid::coil
