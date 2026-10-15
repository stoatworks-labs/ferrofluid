#pragma once

#include <deque>

/**
    The coil current: what the music, the beat and the buttons ask for, and
    what the coil's inductance lets through. Plain C++ in double, no GL, so
    `fetest --offline` checks it.

    The drive each coil is asked for is

        I_target( t ) = Bias + Gain ( level + sum of the Fire pulses on at t )

    clamped to [ 0, kMaxCurrent ] (in units of the critical current; Controls.h),
    and the coil's current follows it through its own L / R lag, exactly (the
    target is piecewise constant: a frame's audio level is one value, and a
    pulse switches on and off at known instants).

    **A beat inside a frame.** The host's bar phase is read every frame; a beat
    boundary crossed between two frames starts a pulse at the moment inside
    the frame where the phase crossed it, interpolated (schlieren's, which is
    fiche's lesson). A press of Fire and an onset carry no such moment and
    start at the frame's start, so they show on the frame that saw them. A
    pulse is a rectangle of the Pulse control's length, height 1 for a beat or
    a press and the analyser's latched kick for an onset.

    **Three Coils** take three levels, one per band (Low, Mid, High); every
    pulse goes to all three.
*/
namespace ferrofluid
{

enum class FireOn
{
	Off = 0, ///< only the Fire button
	Beat,    ///< every beat of Resolume's clock
	HalfBeat,///< every quaver
	TwoBeats,
	Bar,
	Onset,   ///< every onset the audio analyser finds
	Count
};

struct DriveInputs
{
	FireOn fireOn   = FireOn::Beat;
	int presses     = 0;
	bool onset      = false;
	double kick     = 0.0;///< the analyser's latched hit, 0..1
	double level[ 3 ] = { 0.0, 0.0, 0.0 };///< the level each coil follows, 0..1
	int coils       = 1;
	double bias     = 0.0;///< units of the critical current
	double gain     = 0.0;///< units of the critical current
	double pulse    = 0.1;///< s
	double tau      = 0.05;///< L / R, s
	double bpm      = 120.0;
	double barPhase = 0.0;///< 0..1
	bool hostBeat   = false;///< the host has been moving barPhase
};

/// Hooks for the negative controls. A shipped instance sets 0.
enum DriveHook : int
{
	kHookBeatAtFrameStart = 1 << 0,///< a beat fires at its frame's start, not where it fell
	kHookEulerLag         = 1 << 1 ///< the L / R lag stepped by forward Euler, not exactly
};

class Drive
{
public:
	static constexpr int kMaxCoils = 3;
	/// The highest current the drive will ask for, in units of the critical one.
	static constexpr double kMaxCurrent = 4.0;
	/// The most beat boundaries one frame may fire (more is a seek, not music).
	static constexpr int kMaxBeatsPerFrame = 8;

	struct Pulse
	{
		double start = 0.0, end = 0.0;///< host seconds
		double amplitude = 1.0;
	};

	/// Forget the pulses and the current. The clocks carry on.
	void Reset();

	/// One host frame: [ now - dt, now ]. Fires whatever is due inside it,
	/// fixes the frame's drive, and advances every coil's current to `now`.
	void Advance( double now, double dt, const DriveInputs& in );

	/// A coil's current at host time t inside the last frame, exactly.
	double CurrentAt( int coil, double t ) const;
	/// At the end of the last frame.
	double Current( int coil ) const
	{
		return coil >= 0 && coil < kMaxCoils ? endCurrent[ coil ] : 0.0;
	}
	/// What the coil is being asked for at host time t inside the last frame.
	double TargetAt( int coil, double t ) const;

	double FrameStart() const
	{
		return frameStart;
	}
	double FrameEnd() const
	{
		return frameEnd;
	}
	const std::deque< Pulse >& Pulses() const
	{
		return pulses;
	}
	/// Pulses started since the plugin loaded.
	unsigned long long Fired() const
	{
		return fired;
	}

	void SetHooksForTest( int bits )
	{
		hooks = bits;
	}

private:
	void fire( double at, double amplitude, double length );

	std::deque< Pulse > pulses;
	double frameStart = 0.0, frameEnd = 0.0;
	double startCurrent[ kMaxCoils ] = {};
	double endCurrent[ kMaxCoils ]   = {};
	double level[ kMaxCoils ]        = {};
	double bias = 0.0, gain = 0.0, tau = 0.05;
	double lastPhase = -1.0;
	unsigned long long fired = 0;
	int hooks = 0;
};

} // namespace ferrofluid
