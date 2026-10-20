#include "SpikeTable.h"

#include <algorithm>
#include <cmath>

namespace ferrofluid::spikes
{
const Table& TableFor( int fluidIndex )
{
	return kTables[ std::clamp( fluidIndex, 0, kTableCount - 1 ) ];
}

double GridIndex( const Table& t, double u )
{
	const double v = std::sqrt( std::max( u - t.uFold, 0.0 ) );
	return std::clamp( v / t.vEnd * ( t.nGrid - 1 ), 0.0, static_cast< double >( t.nGrid - 1 ) );
}

double KernelHat( const Table& t, double index, double q )
{
	if( q < 0.0 || q >= t.qMax )
		return 0.0;
	const double fq = q / t.qMax * ( t.nQ - 1 );
	const int iq    = std::min( static_cast< int >( fq ), t.nQ - 2 );
	const double sq = fq - iq;
	const int iv    = std::min( static_cast< int >( index ), t.nGrid - 2 );
	const double sv = index - iv;
	double out      = 0.0;
	for( int j = 0; j < kBasis; ++j )
	{
		const double c = ( 1.0 - sv ) * t.coefficients[ iv * kBasis + j ] + sv * t.coefficients[ ( iv + 1 ) * kBasis + j ];
		const double b = ( 1.0 - sq ) * t.basis[ iq * kBasis + j ] + sq * t.basis[ ( iq + 1 ) * kBasis + j ];
		out += c * b;
	}
	return out;
}
} // namespace ferrofluid::spikes
