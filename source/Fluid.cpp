#include "Fluid.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

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

namespace
{
/// The reduction's cell: one period of the hexagonal pattern, k_c = 1, on a
/// rectangle 4 pi by 4 pi / sqrt 3 that every mode of the three families is
/// periodic on, sampled 48 x 48. The energy's integrand is a trigonometric
/// polynomial of degree at most 16 in x and 8 in y there, so the sample mean
/// is its exact average.
struct Cell
{
	static constexpr int kN = 48;
	std::vector< double > s1, s3, s2;///< 2 sum cos over each family
	Cell()
	{
		const double r3 = std::sqrt( 3.0 );
		const double k[ 3 ][ 2 ] = { { 1.0, 0.0 }, { -0.5, 0.5 * r3 }, { -0.5, -0.5 * r3 } };
		s1.resize( kN * kN );
		s3.resize( kN * kN );
		s2.resize( kN * kN );
		for( int j = 0; j < kN; ++j )
			for( int i = 0; i < kN; ++i )
			{
				const double x = 4.0 * kPi * i / kN, y = 4.0 * kPi / r3 * j / kN;
				double a = 0.0, b = 0.0, c = 0.0;
				for( int n = 0; n < 3; ++n )
				{
					const int o = ( n + 1 ) % 3;
					a += 2.0 * std::cos( k[ n ][ 0 ] * x + k[ n ][ 1 ] * y );
					b += 2.0 * std::cos( ( k[ n ][ 0 ] - k[ o ][ 0 ] ) * x + ( k[ n ][ 1 ] - k[ o ][ 1 ] ) * y );
					c += 2.0 * std::cos( 2.0 * k[ n ][ 0 ] * x + 2.0 * k[ n ][ 1 ] * y );
				}
				s1[ j * kN + i ] = a;
				s3[ j * kN + i ] = b;
				s2[ j * kN + i ] = c;
			}
	}
};

const Cell& cell()
{
	static const Cell instance;
	return instance;
}

/// The reduced energy per area and its gradient and Hessian in ( A, B, C ):
/// the three modes at k_c of amplitude A each, the six at sqrt( 3 ) k_c of
/// amplitude B, the six at 2 k_c of amplitude C (heights, k_c units), and the
/// model's local energy in g = W h averaged over the cell.
double reducedEnergy( const Nonlinear& model, double m, const double p[ 3 ], double grad[ 3 ], double hess[ 3 ][ 3 ] )
{
	const Cell& c   = cell();
	const double w3 = std::sqrt( 3.0 ), w2 = 2.0;
	const double l1 = LinearOperator( 1.0, m ), l3 = LinearOperator( w3, m ), l2 = LinearOperator( 2.0, m );
	//<h^2> = 6 A^2 + 6 B^2 + 6 C^2: half of l times it.
	double energy = 3.0 * ( l1 * p[ 0 ] * p[ 0 ] + l3 * p[ 1 ] * p[ 1 ] + l2 * p[ 2 ] * p[ 2 ] );
	const double lin[ 3 ] = { 6.0 * l1, 6.0 * l3, 6.0 * l2 };
	for( int i = 0; i < 3; ++i )
	{
		grad[ i ] = lin[ i ] * p[ i ];
		for( int j = 0; j < 3; ++j )
			hess[ i ][ j ] = i == j ? lin[ i ] : 0.0;
	}
	const int n = Cell::kN * Cell::kN;
	double v = 0.0, gv[ 3 ] = {}, hv[ 3 ][ 3 ] = {};
	for( int i = 0; i < n; ++i )
	{
		const double basis[ 3 ] = { c.s1[ i ], w3 * c.s3[ i ], w2 * c.s2[ i ] };
		const double g          = p[ 0 ] * basis[ 0 ] + p[ 1 ] * basis[ 1 ] + p[ 2 ] * basis[ 2 ];
		v += m * ( -model.hysteresis * g * g * g / 3.0 + model.saturation * g * g * g * g / 4.0 );
		const double d1 = m * ( -model.hysteresis * g * g + model.saturation * g * g * g );
		const double d2 = m * ( -2.0 * model.hysteresis * g + 3.0 * model.saturation * g * g );
		for( int a = 0; a < 3; ++a )
		{
			gv[ a ] += d1 * basis[ a ];
			for( int b = 0; b < 3; ++b )
				hv[ a ][ b ] += d2 * basis[ a ] * basis[ b ];
		}
	}
	energy += v / n;
	for( int a = 0; a < 3; ++a )
	{
		grad[ a ] += gv[ a ] / n;
		for( int b = 0; b < 3; ++b )
			hess[ a ][ b ] += hv[ a ][ b ] / n;
	}
	return energy;
}

/// Newton from `p` to a stationary point; true if it converged to a minimum
/// with A > 0.
bool settle( const Nonlinear& model, double m, double p[ 3 ] )
{
	for( int iter = 0; iter < 200; ++iter )
	{
		double g[ 3 ], H[ 3 ][ 3 ];
		reducedEnergy( model, m, p, g, H );
		//Solve H d = g by Cramer (3 x 3).
		const double det = H[ 0 ][ 0 ] * ( H[ 1 ][ 1 ] * H[ 2 ][ 2 ] - H[ 1 ][ 2 ] * H[ 2 ][ 1 ] )
		                   - H[ 0 ][ 1 ] * ( H[ 1 ][ 0 ] * H[ 2 ][ 2 ] - H[ 1 ][ 2 ] * H[ 2 ][ 0 ] )
		                   + H[ 0 ][ 2 ] * ( H[ 1 ][ 0 ] * H[ 2 ][ 1 ] - H[ 1 ][ 1 ] * H[ 2 ][ 0 ] );
		if( !( std::fabs( det ) > 1e-300 ) )
			return false;
		double d[ 3 ];
		for( int k = 0; k < 3; ++k )
		{
			double M[ 3 ][ 3 ];
			for( int r = 0; r < 3; ++r )
				for( int s = 0; s < 3; ++s )
					M[ r ][ s ] = s == k ? g[ r ] : H[ r ][ s ];
			d[ k ] = ( M[ 0 ][ 0 ] * ( M[ 1 ][ 1 ] * M[ 2 ][ 2 ] - M[ 1 ][ 2 ] * M[ 2 ][ 1 ] )
			           - M[ 0 ][ 1 ] * ( M[ 1 ][ 0 ] * M[ 2 ][ 2 ] - M[ 1 ][ 2 ] * M[ 2 ][ 0 ] )
			           + M[ 0 ][ 2 ] * ( M[ 1 ][ 0 ] * M[ 2 ][ 1 ] - M[ 1 ][ 1 ] * M[ 2 ][ 0 ] ) )
			         / det;
		}
		//Damped: never more than half of A in one step.
		double scale = 1.0;
		if( std::fabs( d[ 0 ] ) > 0.5 * std::fabs( p[ 0 ] ) + 1e-12 )
			scale = 0.5 * std::fabs( p[ 0 ] ) / std::fabs( d[ 0 ] );
		for( int k = 0; k < 3; ++k )
			p[ k ] -= scale * d[ k ];
		if( std::fabs( d[ 0 ] ) * scale < 1e-13 && std::fabs( d[ 1 ] ) * scale < 1e-13 && std::fabs( d[ 2 ] ) * scale < 1e-13 )
		{
			//A minimum: the Hessian positive definite (leading minors).
			reducedEnergy( model, m, p, g, H );
			const double m1 = H[ 0 ][ 0 ], m2 = H[ 0 ][ 0 ] * H[ 1 ][ 1 ] - H[ 0 ][ 1 ] * H[ 1 ][ 0 ];
			const double m3 = H[ 0 ][ 0 ] * ( H[ 1 ][ 1 ] * H[ 2 ][ 2 ] - H[ 1 ][ 2 ] * H[ 2 ][ 1 ] )
			                  - H[ 0 ][ 1 ] * ( H[ 1 ][ 0 ] * H[ 2 ][ 2 ] - H[ 1 ][ 2 ] * H[ 2 ][ 0 ] )
			                  + H[ 0 ][ 2 ] * ( H[ 1 ][ 0 ] * H[ 2 ][ 1 ] - H[ 1 ][ 1 ] * H[ 2 ][ 0 ] );
			return p[ 0 ] > 1e-9 && m1 > 0.0 && m2 > 0.0 && m3 > 0.0;
		}
	}
	return false;
}
} // namespace

Hexagons ThreeMode( const Nonlinear& model, double m )
{
	//Continuation from just past onset, where the upper branch is the only
	//nontrivial minimum, to m in steps: the branch followed, not guessed. Up as
	//well as down: settled cold at m 2.05, EMG 909's fitted constants (Hy/Sa
	//0.63) found no minimum from the guess while the branch ran on smoothly.
	Hexagons out;
	if( model.saturation <= 0.0 )
		return out;
	double p[ 3 ] = { 0.5 * std::max( model.hysteresis, 0.1 ) / model.saturation, 0.0, 0.0 };
	const double start = 1.02;
	if( !settle( model, start, p ) )
		return out;
	for( double mm = start; mm < m; )
	{
		mm = std::min( m, mm + 0.01 );
		if( !settle( model, mm, p ) )
			return out;
	}
	for( double mm = start; mm > m; )
	{
		mm = std::max( m, mm - 2e-4 );
		if( !settle( model, mm, p ) )
			return out;
	}
	out.exists    = true;
	out.amplitude = p[ 0 ];
	out.sqrt3     = p[ 1 ];
	out.twice     = p[ 2 ];
	out.peak      = 6.0 * ( p[ 0 ] + p[ 1 ] + p[ 2 ] );
	return out;
}

double SaddleNode( const Nonlinear& model )
{
	if( model.hysteresis <= 0.0 || model.saturation <= 0.0 )
		return 1.0;
	//Down from onset in steps of 1e-4 following the branch, then halve the
	//last step until it is 1e-9: the lowest m at which the minimum survives.
	double p[ 3 ] = { 0.5 * model.hysteresis / model.saturation, 0.0, 0.0 };
	double m      = 1.02;
	if( !settle( model, m, p ) )
		return 1.0;
	double step = 1e-4;
	while( step > 1e-9 && m > 0.3 )
	{
		double trial[ 3 ] = { p[ 0 ], p[ 1 ], p[ 2 ] };
		if( settle( model, m - step, trial ) )
		{
			m -= step;
			for( int k = 0; k < 3; ++k )
				p[ k ] = trial[ k ];
		}
		else
			step *= 0.5;
	}
	return m;
}

} // namespace ferrofluid::physics
