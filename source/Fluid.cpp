#include "Fluid.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace ferrofluid::physics
{
namespace
{
//---------------------------------------------------------------------------
// The datasheets. Every number is the source's; nothing is tuned.
//---------------------------------------------------------------------------
const Datasheet kFluids[] = {
	//Ferrotec's EFH1 technical data sheet (EFH1 092110, 2010): 440 G (44 mT)
	//saturation, 2.58 initial susceptibility (stated in Gauss/Oe, i.e. 4 pi M
	//over H: the SI chi), 6 mPa s at 27 C, 1.21 g/ml, 29 mN/m. Ferrotec's
	//current web page gives chi 2.64 and no surface tension; the 2010 sheet is
	//used whole so every number comes from one source.
	{ "EFH1", 1210.0, 0.029, 6.0e-3, 2.58, 0.044 / kMu0,
	  "Ferrotec EFH1 technical data sheet, 2010" },
	//Mahr & Rehberg, Physica D 111 (1998) 335, section 2: the properties of the
	//EMG 901 they used. (Ferrofluidics' oil-based fluid; now Ferrotec's.)
	{ "EMG 901", 1530.0, 0.0295, 10.0e-3, 1.3, 4.8e4,
	  "Mahr & Rehberg, Physica D 111 (1998) 335" },
	//The same paper's EMG 909. Ferrotec's current page lists 22 mT, chi 1.38
	//and < 5 mPa s, and no surface tension; Mahr & Rehberg's set is complete.
	{ "EMG 909", 1020.0, 0.0265, 6.0e-3, 0.8, 1.6e4,
	  "Mahr & Rehberg, Physica D 111 (1998) 335" },
};
static_assert( sizeof( kFluids ) / sizeof( kFluids[ 0 ] ) == static_cast< size_t >( FluidId::Count ),
               "a datasheet per fluid" );

/// Bulk magnetite's saturation magnetisation at room temperature, A/m: what
/// turns a fluid's M_s into its particles' volume fraction. EFH1's sheet says
/// 7.9% by volume; 35 014 / 446 000 is 7.85%.
constexpr double kMagnetiteSaturation = 4.46e5;
/// ASSUMED optical constants at 550 nm: bulk magnetite n = 2.4, k = 0.6, and a
/// light mineral oil carrier n = 1.47. Order-of-magnitude literature values,
/// not a cited measurement (AGENTS.md); the fluid's reflectance at normal
/// incidence moves by about 0.003 across any plausible choice.
constexpr double kMagnetiteN = 2.4, kMagnetiteK = 0.6, kCarrierN = 1.47;

double sq( double x )
{
	return x * x;
}
} // namespace

const Datasheet& FluidOf( FluidId id )
{
	const int i = std::clamp( static_cast< int >( id ), 0, static_cast< int >( FluidId::Count ) - 1 );
	return kFluids[ i ];
}

//---------------------------------------------------------------------------
double Langevin( double x )
{
	//coth x - 1/x loses everything to cancellation near 0; the series is exact
	//to double well past where the closed form is safe.
	const double a = std::fabs( x );
	if( a < 1e-2 )
	{
		const double x2 = x * x;
		return x * ( 1.0 / 3.0 - x2 / 45.0 + 2.0 * x2 * x2 / 945.0 );
	}
	return 1.0 / std::tanh( x ) - 1.0 / x;
}

double LangevinSlope( double x )
{
	const double a = std::fabs( x );
	if( a < 1e-2 )
	{
		const double x2 = x * x;
		return 1.0 / 3.0 - x2 / 15.0 + 2.0 * x2 * x2 / 189.0;
	}
	if( a > 350.0 )
		return 1.0 / ( x * x );
	const double s = std::sinh( x );
	return 1.0 / ( x * x ) - 1.0 / ( s * s );
}

double Magnetisation( const Datasheet& fluid, double h )
{
	return fluid.saturation * Langevin( 3.0 * fluid.chi * h / fluid.saturation );
}

double Susceptibility( const Datasheet& fluid, double h )
{
	return 3.0 * fluid.chi * LangevinSlope( 3.0 * fluid.chi * h / fluid.saturation );
}

Operating Magnetise( const Datasheet& fluid, double applied, int hooks )
{
	Operating op;
	op.applied = std::max( applied, 0.0 );

	//B is continuous across the flat surface: H_a = H_in + M( H_in ). The
	//left side grows monotonically with H_in, so bisection on [ 0, H_a ] cannot
	//miss; 200 halvings of anything below 1e9 A/m reach the last bit.
	double lo = 0.0, hi = op.applied;
	if( ( hooks & kHookNoDemag ) != 0 )
		lo = hi;
	else
		for( int i = 0; i < 200 && hi - lo > 0.0; ++i )
		{
			const double mid = 0.5 * ( lo + hi );
			if( mid == lo || mid == hi )
				break;
			if( mid + Magnetisation( fluid, mid ) < op.applied )
				lo = mid;
			else
				hi = mid;
		}
	op.inside = 0.5 * ( lo + hi );
	op.m      = Magnetisation( fluid, op.inside );

	//The chord permeability at zero field is its limit, 1 + chi.
	op.chord   = op.inside > 0.0 ? 1.0 + op.m / op.inside : 1.0 + fluid.chi;
	op.tangent = 1.0 + Susceptibility( fluid, op.inside );
	op.r0      = ( hooks & kHookChordOnly ) != 0 ? op.chord : std::sqrt( op.chord * op.tangent );
	const double factor = ( hooks & kHookNoR0Factor ) != 0 ? 1.0 : op.r0 / ( 1.0 + op.r0 );
	op.g = kMu0 * op.m * op.m * factor;
	return op;
}

//---------------------------------------------------------------------------
double CriticalWavenumber( const Datasheet& fluid )
{
	return std::sqrt( fluid.density * kGravity / fluid.tension );
}

double CriticalWavelength( const Datasheet& fluid )
{
	return 2.0 * kPi / CriticalWavenumber( fluid );
}

double CriticalG( const Datasheet& fluid )
{
	return 2.0 * std::sqrt( fluid.density * kGravity * fluid.tension );
}

double CriticalMagnetisationSquared( const Datasheet& fluid, double r0 )
{
	return ( 2.0 / kMu0 ) * ( 1.0 + 1.0 / r0 ) * std::sqrt( fluid.density * kGravity * fluid.tension );
}

double CriticalApplied( const Datasheet& fluid, int hooks )
{
	const double target = CriticalG( fluid );
	double lo = 0.0, hi = 1.0;
	while( Magnetise( fluid, hi, hooks ).g < target && hi < 1e9 )
		hi *= 2.0;
	for( int i = 0; i < 200; ++i )
	{
		const double mid = 0.5 * ( lo + hi );
		if( mid == lo || mid == hi )
			break;
		if( Magnetise( fluid, mid, hooks ).g < target )
			lo = mid;
		else
			hi = mid;
	}
	return 0.5 * ( lo + hi );
}

double MagneticNumber( const Datasheet& fluid, double g )
{
	return g * CriticalWavenumber( fluid ) / ( 2.0 * fluid.density * kGravity );
}

double OmegaSquared( const Datasheet& fluid, double k, double g )
{
	return ( fluid.density * kGravity * k + fluid.tension * k * k * k - g * k * k ) / fluid.density;
}

double Damping( const Datasheet& fluid, double k, double viscosityScale )
{
	return 2.0 * viscosityScale * fluid.viscosity / fluid.density * k * k;
}

//---------------------------------------------------------------------------
Optics OpticsOf( const Datasheet& fluid )
{
	using C            = std::complex< double >;
	const double phi   = std::clamp( fluid.saturation / kMagnetiteSaturation, 0.0, 0.5 );
	const C particle   = C( kMagnetiteN, kMagnetiteK ) * C( kMagnetiteN, kMagnetiteK );
	const C host       = C( kCarrierN * kCarrierN, 0.0 );
	//Maxwell Garnett: spheres of the particle in the host.
	const C mixed      = host * ( particle + 2.0 * host + 2.0 * phi * ( particle - host ) )
	                     / ( particle + 2.0 * host - phi * ( particle - host ) );
	const C index      = std::sqrt( mixed );
	Optics out;
	out.n        = index.real();
	out.k        = index.imag();
	out.fraction = phi;
	return out;
}

double NormalReflectance( const Optics& o )
{
	return ( sq( o.n - 1.0 ) + sq( o.k ) ) / ( sq( o.n + 1.0 ) + sq( o.k ) );
}

double Reflectance( const Optics& o, double cosIncidence )
{
	using C          = std::complex< double >;
	const double ci  = std::clamp( cosIncidence, 0.0, 1.0 );
	const C index    = C( o.n, o.k );
	const C index2   = index * index;
	const C root     = std::sqrt( index2 - C( 1.0 - ci * ci, 0.0 ) );
	const C rs       = ( C( ci, 0.0 ) - root ) / ( C( ci, 0.0 ) + root );
	const C rp       = ( index2 * ci - root ) / ( index2 * ci + root );
	return 0.5 * ( std::norm( rs ) + std::norm( rp ) );
}

//---------------------------------------------------------------------------
double LinearOperator( double q, double m )
{
	return 1.0 + q * q - 2.0 * m * q;
}

double QuarticEffective( const Nonlinear& model, double m )
{
	const double l3 = LinearOperator( std::sqrt( 3.0 ), m );
	const double l2 = LinearOperator( 2.0, m );
	if( l3 <= 0.0 || l2 <= 0.0 )
		return -1e300;//a harmonic is itself unstable: no weakly nonlinear answer
	const double hy2 = model.hysteresis * model.hysteresis;
	return 22.5 * model.saturation * m - 12.0 * hy2 * m * m / l3 - 3.0 * hy2 * m * m / l2;
}

Hexagons ThreeMode( const Nonlinear& model, double m )
{
	Hexagons out;
	const double q = QuarticEffective( model, m );
	if( !( q > 0.0 ) || m <= 0.0 )
		return out;
	//dF/dA = 0 on the symmetric hexagon, A != 0:
	//    Q A^2 - 3 Hy m A + 3 ( 1 - m ) = 0
	const double hy   = model.hysteresis;
	const double disc = 9.0 * hy * hy * m * m - 12.0 * q * ( 1.0 - m );
	if( disc < 0.0 )
		return out;
	const double a = ( 3.0 * hy * m + std::sqrt( disc ) ) / ( 2.0 * q );
	if( !( a > 0.0 ) )
		return out;
	out.exists    = true;
	out.amplitude = a;
	out.sqrt3     = 2.0 * hy * m * a * a / LinearOperator( std::sqrt( 3.0 ), m );
	out.twice     = hy * m * a * a / LinearOperator( 2.0, m );
	out.peak      = 6.0 * ( out.amplitude + out.sqrt3 + out.twice );
	return out;
}

double SaddleNode( const Nonlinear& model )
{
	if( model.hysteresis <= 0.0 )
		return 1.0;
	auto exists = [ & ]( double m ) {
		const double q = QuarticEffective( model, m );
		return q > 0.0 && 9.0 * sq( model.hysteresis * m ) - 12.0 * q * ( 1.0 - m ) >= 0.0;
	};
	double lo = 0.3, hi = 1.0;
	if( exists( lo ) )
		return lo;
	for( int i = 0; i < 200; ++i )
	{
		const double mid = 0.5 * ( lo + hi );
		if( mid == lo || mid == hi )
			break;
		if( exists( mid ) )
			hi = mid;
		else
			lo = mid;
	}
	return hi;
}

} // namespace ferrofluid::physics
