#include "Coil.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace ferrofluid::coil
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

/// Gauss-Legendre nodes and weights on [-1, 1], by Newton's method on P_n
/// from the Chebyshev guess. Computed once, in double.
template< int N >
struct GaussLegendre
{
	std::array< double, N > x {}, w {};
	GaussLegendre()
	{
		for( int i = 0; i < ( N + 1 ) / 2; ++i )
		{
			double z = std::cos( kPi * ( i + 0.75 ) / ( N + 0.5 ) );
			double dp = 0.0;
			for( int iter = 0; iter < 100; ++iter )
			{
				double p0 = 1.0, p1 = z;
				for( int k = 2; k <= N; ++k )
				{
					const double p2 = ( ( 2.0 * k - 1.0 ) * z * p1 - ( k - 1.0 ) * p0 ) / k;
					p0              = p1;
					p1              = p2;
				}
				dp             = N * ( z * p1 - p0 ) / ( z * z - 1.0 );
				const double dz = p1 / dp;
				z -= dz;
				if( std::fabs( dz ) < 1e-16 )
					break;
			}
			x[ i ]         = -z;
			x[ N - 1 - i ] = z;
			w[ i ] = w[ N - 1 - i ] = 2.0 / ( ( 1.0 - z * z ) * dp * dp );
		}
	}
};

const GaussLegendre< 32 >& rule32()
{
	static const GaussLegendre< 32 > rule;
	return rule;
}
} // namespace

void Elliptic( double m, double& K, double& E )
{
	//The arithmetic-geometric mean: a and b meet quadratically, and E falls out
	//of the same sequence as K( 1 - sum 2^( n - 1 ) c_n^2 ), c_0^2 = m.
	m = std::clamp( m, 0.0, 1.0 - 1e-16 );
	double a = 1.0, b = std::sqrt( 1.0 - m );
	double sum = 0.5 * m, power = 0.5;
	for( int i = 0; i < 64; ++i )
	{
		const double c = 0.5 * ( a - b );
		const double an = 0.5 * ( a + b );
		b               = std::sqrt( a * b );
		a               = an;
		power *= 2.0;
		const double term = power * c * c;
		sum += term;
		//Stop on the TERM, not on c: a and b can end one ulp apart and stay
		//there, and with the weight doubling every step that ulp squared adds
		//1e-13 to E( 1/2 ) over 64 steps (`fetest --coil` caught it).
		if( term <= 1e-17 * sum )
			break;
	}
	K = kPi / ( 2.0 * a );
	E = K * ( 1.0 - sum );
}

double LoopAxial( double a, double rho, double z )
{
	rho                = std::fabs( rho );
	const double r2    = rho * rho + z * z;
	const double alpha2 = a * a + r2 - 2.0 * a * rho;
	const double beta2  = a * a + r2 + 2.0 * a * rho;
	if( alpha2 <= 0.0 )
		return 0.0;//on the wire itself
	const double beta = std::sqrt( beta2 );
	double K = 0.0, E = 0.0;
	Elliptic( 1.0 - alpha2 / beta2, K, E );
	return ( ( a * a - r2 ) * E + alpha2 * K ) / ( 2.0 * kPi * alpha2 * beta );
}

double LoopOnAxis( double a, double z )
{
	return a * a / ( 2.0 * std::pow( a * a + z * z, 1.5 ) );
}

double SolenoidAxial( double radius, double rho )
{
	//N / L turns per metre, each a loop at depth z below the surface: the
	//surface is z above it.
	const auto& rule = rule32();
	const double half = 0.5 * kSolenoidLength, mid = kSolenoidDepth + half;
	double sum = 0.0;
	for( int i = 0; i < 32; ++i )
		sum += rule.w[ i ] * LoopAxial( radius, rho, mid + half * rule.x[ i ] );
	return sum * half * kSolenoidTurns / kSolenoidLength;
}

double HelmholtzField()
{
	return std::pow( 0.8, 1.5 ) * kHelmholtzTurns / kHelmholtzRadius;
}

void SolenoidProfile( double radius, double rhoMax, int count, float* out, int hooks )
{
	const auto& rule  = rule32();
	const double half = 0.5 * kSolenoidLength, mid = kSolenoidDepth + half;
	for( int i = 0; i < count; ++i )
	{
		const double rho = rhoMax * static_cast< double >( i ) / static_cast< double >( count - 1 );
		double value     = 0.0;
		if( ( hooks & kHookOnAxisEverywhere ) != 0 )
		{
			for( int j = 0; j < 32; ++j )
				value += rule.w[ j ] * LoopOnAxis( radius, mid + half * rule.x[ j ] );
			value *= half * kSolenoidTurns / kSolenoidLength;
		}
		else
			value = SolenoidAxial( radius, rho );
		out[ i ] = static_cast< float >( value );
	}
}

double LagStep( double current, double target, double dt, double tau )
{
	if( dt <= 0.0 )
		return current;
	if( tau <= 0.0 )
		return target;
	return target + ( current - target ) * std::exp( -dt / tau );
}

} // namespace ferrofluid::coil
