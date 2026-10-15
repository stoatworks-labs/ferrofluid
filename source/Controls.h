#pragma once

#include "Drive.h"
#include "StoatworksAboutLinks.h"

#include <vector>

/**
    The host's parameters, and what they mean in physical units.

    Every ranged parameter the host sees is 0..1, because `SetParamInfo`
    clamps an `FF_TYPE_STANDARD` default into 0..1 before `SetParamRange`
    could widen it. The conversions live in Controls.cpp, one function per
    control, each with its inverse where a default or the harness needs it.
    Option parameters hold the element index (an option reads back 0..1
    whatever its count -- map by index); Fire and Settle are presses; Audio is
    Resolume's 64-bin FFT buffer.

    Units: metres, seconds, amperes as MULTIPLES OF THE CRITICAL CURRENT (the
    current at which the selected fluid over the selected magnet reaches the
    Rosensweig onset at the pool's strongest point: AGENTS.md says why), and
    stops of exposure.
*/
namespace ferrofluid
{
/// Every control either plugin has, by what it is -- NOT by the index a host
/// sees. Each plugin declares its own subset, densely, in `HostOrder()`'s
/// order, with the About block LAST in both (honeydew's shape).
enum ParamId : unsigned int
{
	// -- Drive: what the coil current follows ----------------------------------
	PT_FIRE_ON = 0,
	PT_FIRE,
	PT_AUDIO,
	PT_BAND,
	PT_GAIN,
	PT_BIAS,
	PT_COIL_LAG,
	PT_THRESHOLD,
	PT_PULSE,

	// -- Magnet ------------------------------------------------------------------
	PT_MAGNET,
	PT_COIL_SIZE,
	PT_COIL_SPREAD,

	// -- Fluid -------------------------------------------------------------------
	PT_FLUID,
	PT_HYSTERESIS,
	PT_SATURATION,
	PT_VISCOSITY,
	PT_NOISE,
	PT_SETTLE,

	// -- View --------------------------------------------------------------------
	PT_FIELD_WIDTH,
	PT_TILT,
	PT_HEIGHT,  ///< Over only: the screen's height above the pool
	PT_EXPOSURE,
	PT_LIGHTS,  ///< source only
	PT_BACKDROP,///< source only
	PT_MIX,     ///< Over only

	// -- The Stoatworks About block: a text line, then one button per link.
	PT_ABOUT_TEXT,
	PT_COUNT = PT_ABOUT_TEXT + 1 + stoatworks::about::kButtonCount
};

enum class Kind
{
	Standard,
	Option,
	Event,
	Buffer
};

/// One control as the host is told about it.
struct ControlInfo
{
	unsigned int id;
	const char* name;///< at most 16 characters: FFGL's name field is not terminated
	const char* group;
	Kind kind;
	float defaultValue;///< the host's 0..1, or the element index
	const char* const* options;
	int optionCount;
};

/// The table, indexed by ParamId, for every id below PT_ABOUT_TEXT.
const ControlInfo& InfoOf( unsigned int id );

/// The ids a plugin declares, in the order the host sees them.
const std::vector< unsigned int >& HostOrder( bool effect );

/// An option's index from the value the host stored.
int OptionIndex( float value, int count );

//---------------------------------------------------------------------------
// Options.
//---------------------------------------------------------------------------
enum class Band
{
	Low = 0,
	Mid,
	High,
	Full,
	Count
};

enum class Magnet
{
	Uniform = 0,///< a Helmholtz pair: H uniform over the pool
	Coil,       ///< one solenoid under the middle of the frame
	ThreeCoils, ///< three, in a triangle, one per band
	Count
};

enum class Lights
{
	SoftBox = 0,///< one big soft box overhead, a little behind the camera
	TwoBoxes,   ///< a key and a fill
	Strips,     ///< three strip boxes
	Backlight,  ///< a wide low panel beyond the pool: bright rims
	Window,     ///< a warm four-pane window
	Neon,       ///< a magenta and a cyan strip
	Count
};

enum class Backdrop
{
	Black = 0,
	Studio,
	Count
};

//---------------------------------------------------------------------------
// The mappings. Each says its range and its shape.
//---------------------------------------------------------------------------

/// Gain and Bias: 0 to 2 critical currents, linearly.
double CurrentFromParam( float value );
float ParamFromCurrent( double multiple );

/// Coil Lag, L / R: 1 to 300 ms, geometrically. Seconds.
double LagFromParam( float value );
float ParamFromLag( double seconds );

/// The onset detector's bar: Threshold 0 fires on anything, 1 on very little.
/// Returned as the analyser's sensitivity (1 - Threshold).
double SensitivityFromThreshold( float value );

/// Pulse: 10 ms to 1 s, geometrically. Seconds.
double PulseFromParam( float value );
float ParamFromPulse( double seconds );

/// Coil Size: the solenoid's radius, 5 mm to 8 cm, geometrically. Metres.
double CoilSizeFromParam( float value );
float ParamFromCoilSize( double metres );

/// Coil Spread: the triangle's side, 2 to 30 cm, geometrically. Metres.
double SpreadFromParam( float value );
float ParamFromSpread( double metres );

/// Hysteresis: the model's Hy, 0 to 4, linearly.
double HysteresisFromParam( float value );
float ParamFromHysteresis( double hy );

/// Saturation: the model's Sa, 0.5 to 8, geometrically.
double SaturationFromParam( float value );
float ParamFromSaturation( double sa );

/// Viscosity: a multiple of the datasheet's, 0.25x to 100x, geometrically.
double ViscosityFromParam( float value );
float ParamFromViscosity( double scale );

/// Noise: the RMS of a white random pressure on the surface, Pa over a square
/// millimetre and a millisecond. Exactly 0 at the bottom, then 1e-6 to 1e-1
/// Pa, geometrically.
double NoiseFromParam( float value );
float ParamFromNoise( double pascals );

/// Field Width: the frame's width at the pool, 2 to 40 cm, geometrically. Metres.
double FieldWidthFromParam( float value );
float ParamFromFieldWidth( double metres );

/// Tilt: 0 to 60 degrees, linearly. Radians.
double TiltFromParam( float value );
float ParamFromTilt( double radians );

/// Height: the Over's screen above the pool, 2 cm to 1 m, geometrically. Metres.
double HeightFromParam( float value );
float ParamFromHeight( double metres );

/// Exposure: -4 to +4 stops, linearly; 0 makes a flat pool show the clip (or
/// a soft box seen straight on) at its own brightness.
double ExposureFromParam( float value );
float ParamFromExposure( double stops );

constexpr double kPi = 3.14159265358979323846;

} // namespace ferrofluid
