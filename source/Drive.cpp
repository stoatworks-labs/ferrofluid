#include "Drive.h"

#include "Coil.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ferrofluid
{

void Drive::Reset()
{
	pulses.clear();
	for( int i = 0; i < kMaxCoils; ++i )
		startCurrent[ i ] = endCurrent[ i ] = 0.0;
}

void Drive::fire( double at, double amplitude, double length )
{
	Pulse p;
	p.start     = at;
	p.end       = at + std::max( length, 0.0 );
	p.amplitude = amplitude;
	pulses.push_back( p );
	++fired;
	//A pulse that has ended is spent; more than a few dozen at once is a
	//pathological Pulse length at Half Beat, and the oldest go first.
	while( pulses.size() > 64 )
		pulses.pop_front();
}

void Drive::Advance( double now, double dt, const DriveInputs& in )
{
	frameStart = dt > 0.0 ? now - dt : now;
	frameEnd   = now;
	for( int i = 0; i < kMaxCoils; ++i )
	{
		startCurrent[ i ] = endCurrent[ i ];
		level[ i ]        = i < in.coils ? std::clamp( in.level[ i ], 0.0, 1.0 ) : 0.0;
	}
	bias = in.bias;
	gain = in.gain;
	tau  = in.tau;

	//The button and the onsets, at the frame's start.
	for( int i = 0; i < in.presses; ++i )
		fire( frameStart, 1.0, in.pulse );
	if( in.fireOn == FireOn::Onset && in.onset )
		fire( frameStart, std::clamp( in.kick, 0.0, 1.0 ), in.pulse );

	//The beat. A host that never moves barPhase gets a clock of the plugin's
	//own at its BPM, so Fire On is never dead.
	const double tempo    = in.bpm > 1.0 ? in.bpm : 120.0;
	const double phaseNow = in.hostBeat ? std::clamp( in.barPhase, 0.0, 1.0 ) : std::fmod( now * tempo / 240.0, 1.0 );
	double step           = 0.0;
	switch( in.fireOn )
	{
	case FireOn::Beat: step = 0.25; break;
	case FireOn::HalfBeat: step = 0.125; break;
	case FireOn::TwoBeats: step = 0.5; break;
	case FireOn::Bar: step = 1.0; break;
	default: break;
	}
	if( step > 0.0 && lastPhase >= 0.0 && dt > 0.0 )
	{
		double p0 = lastPhase, p1 = phaseNow;
		if( p1 < p0 )
			p1 += 1.0;
		if( p1 > p0 )
		{
			double boundary = ( std::floor( p0 / step + 1e-9 ) + 1.0 ) * step;
			for( int n = 0; boundary <= p1 + 1e-12 && n < kMaxBeatsPerFrame; ++n, boundary += step )
			{
				const double inside = ( hooks & kHookBeatAtFrameStart ) != 0 ? 0.0 : ( boundary - p0 ) / ( p1 - p0 );
				fire( frameStart + inside * dt, 1.0, in.pulse );
			}
		}
	}
	lastPhase = phaseNow;

	while( !pulses.empty() && pulses.front().end < frameStart )
		pulses.pop_front();

	for( int i = 0; i < kMaxCoils; ++i )
		endCurrent[ i ] = CurrentAt( i, frameEnd );
}

double Drive::TargetAt( int coil, double t ) const
{
	if( coil < 0 || coil >= kMaxCoils )
		return 0.0;
	double on = 0.0;
	for( const Pulse& p : pulses )
		if( p.start <= t && t < p.end )
			on += p.amplitude;
	return std::clamp( bias + gain * ( level[ coil ] + on ), 0.0, kMaxCurrent );
}

double Drive::CurrentAt( int coil, double t ) const
{
	if( coil < 0 || coil >= kMaxCoils )
		return 0.0;
	t = std::clamp( t, frameStart, frameEnd );

	//The drive is constant between pulse edges: the closed form across each
	//stretch is exact.
	std::vector< double > edges = { frameStart };
	for( const Pulse& p : pulses )
		for( double e : { p.start, p.end } )
			if( e > frameStart && e < t )
				edges.push_back( e );
	edges.push_back( t );
	std::sort( edges.begin(), edges.end() );

	double current = startCurrent[ coil ];
	for( size_t i = 0; i + 1 < edges.size(); ++i )
	{
		const double a = edges[ i ], b = edges[ i + 1 ];
		if( b <= a )
			continue;
		const double target = TargetAt( coil, 0.5 * ( a + b ) );
		if( ( hooks & kHookEulerLag ) != 0 )
			current += ( target - current ) * std::min( ( b - a ) / std::max( tau, 1e-9 ), 1.0 );
		else
			current = coil::LagStep( current, target, b - a, tau );
	}
	return current;
}

} // namespace ferrofluid
