#include "Controls.h"

#include <algorithm>
#include <cmath>

namespace ferrofluid
{
namespace
{
double clamp01( float v )
{
	return std::clamp( static_cast< double >( v ), 0.0, 1.0 );
}
double geometric( float v, double low, double high )
{
	return low * std::pow( high / low, clamp01( v ) );
}
float inverseGeometric( double value, double low, double high )
{
	const double lo = std::min( low, high ), hi = std::max( low, high );
	return static_cast< float >( std::log( std::clamp( value, lo, hi ) / low ) / std::log( high / low ) );
}
double linear( float v, double low, double high )
{
	return low + ( high - low ) * clamp01( v );
}
float inverseLinear( double value, double low, double high )
{
	return static_cast< float >( std::clamp( ( value - low ) / ( high - low ), 0.0, 1.0 ) );
}

constexpr double kCurrentHigh = 1.5;
constexpr double kLagLow = 1e-3, kLagHigh = 0.3;
constexpr double kPulseLow = 0.01, kPulseHigh = 1.0;
constexpr double kCoilLow = 0.005, kCoilHigh = 0.08;
constexpr double kSpreadLow = 0.02, kSpreadHigh = 0.30;
constexpr double kHyHigh = 2.0;
constexpr double kSaLow = 0.35, kSaHigh = 8.0;
constexpr double kViscLow = 0.25, kViscHigh = 100.0;
constexpr double kNoiseLow = 1e-6, kNoiseHigh = 1e-1;
constexpr double kFieldLow = 0.02, kFieldHigh = 0.40;
constexpr double kTiltHigh = 60.0 * kPi / 180.0;
constexpr double kHeightLow = 0.02, kHeightHigh = 1.0;
constexpr double kStops = 4.0;

const char* const kFireOnNames[]   = { "Off", "Beat", "Half Beat", "2 Beats", "Bar", "Onset" };
const char* const kBandNames[]     = { "Low", "Mid", "High", "Full" };
const char* const kMagnetNames[]   = { "Uniform", "Coil", "Three Coils" };
const char* const kFluidNames[]    = { "EFH1", "EMG 901", "EMG 909" };
const char* const kLightsNames[]   = { "Soft Box", "Two Boxes", "Strips", "Backlight", "Window", "Neon" };
const char* const kBackdropNames[] = { "Black", "Studio" };

ControlInfo standard( unsigned int id, const char* name, const char* group, float value )
{
	return { id, name, group, Kind::Standard, value, nullptr, 0 };
}
ControlInfo option( unsigned int id, const char* name, const char* group, float value, const char* const* names, int count )
{
	return { id, name, group, Kind::Option, value, names, count };
}
ControlInfo event( unsigned int id, const char* name, const char* group )
{
	return { id, name, group, Kind::Event, 0.0f, nullptr, 0 };
}

//---------------------------------------------------------------------------
// The defaults: a 12 cm field of EFH1 over one 2.5 cm solenoid, biased just
// under the onset, kicked on every beat; the source seen from 35 degrees
// under one soft box. See AGENTS.md ("Decisions") for how they were chosen.
//---------------------------------------------------------------------------
const ControlInfo kTable[] = {
	option( PT_FIRE_ON, "Fire On", "Drive", 1.0f, kFireOnNames, static_cast< int >( FireOn::Count ) ),
	event( PT_FIRE, "Fire", "Drive" ),
	{ PT_AUDIO, "Audio", "Drive", Kind::Buffer, 0.0f, nullptr, 0 },
	option( PT_BAND, "Band", "Drive", 0.0f, kBandNames, static_cast< int >( Band::Count ) ),
	standard( PT_GAIN, "Gain", "Drive", ParamFromCurrent( 0.2 ) ),
	standard( PT_BIAS, "Bias", "Drive", ParamFromCurrent( 0.98 ) ),
	standard( PT_COIL_LAG, "Coil Lag", "Drive", ParamFromLag( 0.04 ) ),
	standard( PT_THRESHOLD, "Threshold", "Drive", 0.4f ),
	standard( PT_PULSE, "Pulse", "Drive", ParamFromPulse( 0.15 ) ),

	option( PT_MAGNET, "Magnet", "Magnet", 1.0f, kMagnetNames, static_cast< int >( Magnet::Count ) ),
	standard( PT_COIL_SIZE, "Coil Size", "Magnet", ParamFromCoilSize( 0.04 ) ),
	standard( PT_COIL_SPREAD, "Coil Spread", "Magnet", ParamFromSpread( 0.08 ) ),

	option( PT_FLUID, "Fluid", "Fluid", 0.0f, kFluidNames, 3 ),
	standard( PT_HYSTERESIS, "Hysteresis", "Fluid", ParamFromHysteresis( 1.0 ) ),
	standard( PT_SATURATION, "Saturation", "Fluid", ParamFromSaturation( 0.5 ) ),
	standard( PT_VISCOSITY, "Viscosity", "Fluid", ParamFromViscosity( 1.0 ) ),
	standard( PT_NOISE, "Noise", "Fluid", ParamFromNoise( 1e-3 ) ),
	event( PT_SETTLE, "Settle", "Fluid" ),

	standard( PT_FIELD_WIDTH, "Field Width", "View", ParamFromFieldWidth( 0.10 ) ),
	standard( PT_TILT, "Tilt", "View", ParamFromTilt( 35.0 * kPi / 180.0 ) ),
	standard( PT_HEIGHT, "Height", "View", ParamFromHeight( 0.04 ) ),
	standard( PT_EXPOSURE, "Exposure", "View", ParamFromExposure( 0.0 ) ),
	option( PT_LIGHTS, "Lights", "View", 2.0f, kLightsNames, static_cast< int >( Lights::Count ) ),
	option( PT_BACKDROP, "Backdrop", "View", 1.0f, kBackdropNames, static_cast< int >( Backdrop::Count ) ),
	standard( PT_MIX, "Mix", "View", 1.0f ),
};
static_assert( sizeof( kTable ) / sizeof( kTable[ 0 ] ) == PT_ABOUT_TEXT, "every control needs a row in the table" );
} // namespace

const ControlInfo& InfoOf( unsigned int id )
{
	return kTable[ std::min< unsigned int >( id, PT_ABOUT_TEXT - 1 ) ];
}

const std::vector< unsigned int >& HostOrder( bool effect )
{
	auto build = []( bool over ) {
		std::vector< unsigned int > order;
		for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
		{
			const bool overOnly   = id == PT_HEIGHT || id == PT_MIX;
			const bool sourceOnly = id == PT_LIGHTS || id == PT_BACKDROP;
			if( ( over && sourceOnly ) || ( !over && overOnly ) )
				continue;
			order.push_back( id );
		}
		for( unsigned int id = PT_ABOUT_TEXT; id < PT_COUNT; ++id )
			order.push_back( id );
		return order;
	};
	static const std::vector< unsigned int > source = build( false ), over = build( true );
	return effect ? over : source;
}

int OptionIndex( float value, int count )
{
	return std::clamp( static_cast< int >( std::lround( value ) ), 0, count - 1 );
}

double CurrentFromParam( float v )
{
	return linear( v, 0.0, kCurrentHigh );
}
float ParamFromCurrent( double m )
{
	return inverseLinear( m, 0.0, kCurrentHigh );
}

double LagFromParam( float v )
{
	return geometric( v, kLagLow, kLagHigh );
}
float ParamFromLag( double s )
{
	return inverseGeometric( s, kLagLow, kLagHigh );
}

double SensitivityFromThreshold( float v )
{
	return 1.0 - clamp01( v );
}

double PulseFromParam( float v )
{
	return geometric( v, kPulseLow, kPulseHigh );
}
float ParamFromPulse( double s )
{
	return inverseGeometric( s, kPulseLow, kPulseHigh );
}

double CoilSizeFromParam( float v )
{
	return geometric( v, kCoilLow, kCoilHigh );
}
float ParamFromCoilSize( double m )
{
	return inverseGeometric( m, kCoilLow, kCoilHigh );
}

double SpreadFromParam( float v )
{
	return geometric( v, kSpreadLow, kSpreadHigh );
}
float ParamFromSpread( double m )
{
	return inverseGeometric( m, kSpreadLow, kSpreadHigh );
}

double HysteresisFromParam( float v )
{
	return linear( v, 0.0, kHyHigh );
}
float ParamFromHysteresis( double hy )
{
	return inverseLinear( hy, 0.0, kHyHigh );
}

double SaturationFromParam( float v )
{
	return geometric( v, kSaLow, kSaHigh );
}
float ParamFromSaturation( double sa )
{
	return inverseGeometric( sa, kSaLow, kSaHigh );
}

double ViscosityFromParam( float v )
{
	return geometric( v, kViscLow, kViscHigh );
}
float ParamFromViscosity( double s )
{
	return inverseGeometric( s, kViscLow, kViscHigh );
}

double NoiseFromParam( float v )
{
	//Exactly zero at the bottom: a pool with no floor stays flat however hard
	//it is driven, which is the textbook's flat surface and a useful thing to
	//be able to show.
	if( v <= 0.0f )
		return 0.0;
	return geometric( v, kNoiseLow, kNoiseHigh );
}
float ParamFromNoise( double pa )
{
	if( pa <= 0.0 )
		return 0.0f;
	return std::max( 1e-6f, inverseGeometric( pa, kNoiseLow, kNoiseHigh ) );
}

double FieldWidthFromParam( float v )
{
	return geometric( v, kFieldLow, kFieldHigh );
}
float ParamFromFieldWidth( double m )
{
	return inverseGeometric( m, kFieldLow, kFieldHigh );
}

double TiltFromParam( float v )
{
	return linear( v, 0.0, kTiltHigh );
}
float ParamFromTilt( double r )
{
	return inverseLinear( r, 0.0, kTiltHigh );
}

double HeightFromParam( float v )
{
	return geometric( v, kHeightLow, kHeightHigh );
}
float ParamFromHeight( double m )
{
	return inverseGeometric( m, kHeightLow, kHeightHigh );
}

double ExposureFromParam( float v )
{
	return linear( v, -kStops, kStops );
}
float ParamFromExposure( double s )
{
	return inverseLinear( s, -kStops, kStops );
}

} // namespace ferrofluid
