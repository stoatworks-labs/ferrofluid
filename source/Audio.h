#pragma once

#include <array>

/**
	The audio side: a spectrum in, a level and an onset out.

	schlieren's, which is millpond's, which is rosette's, which adapted it from
	macroblock's analyser -- the one that has been driven with real music
	through Resolume's FFT buffer. Ferrofluid uses the onset detector (Fire On
	Onset: each onset fires a current pulse), the latched kick (how hard that
	pulse is) and the band levels (what the coil current follows), including
	the primed first frame that keeps it from going deaf on a clip trigger.

	**The bands** are abomerration's split, logarithmic in bin index: Low is
	the first sixteenth of the bins, Mid up to a quarter, High the rest, Full
	all of them. Nobody has measured where Resolume's 64 bins sit in frequency
	(fleet-wide); for a buffer linear from 0 to Nyquist that is roughly two
	octaves a band, and for anything else it is differently weighted. Each band
	is normalised against its own recent peak, so a quiet High band still
	moves its coil.

	**Where the audio comes from.** FFGL has no audio path. What Resolume
	provides is a buffer parameter declared `FF_USAGE_FFT`, which the host
	fills with a 64-bin spectrum once per frame -- a *modulation* source at
	video rate, not a signal source, so the smallest interval this can
	resolve is a frame and a kick lands on the frame after the transient.

	**Normalised against its own recent peak**, so the same Gain means the
	same dynamics on a quiet stem and a mastered track. The cost is that a
	long loud passage reads as "1" throughout, because relative to the last
	few seconds it is.
*/
namespace ferrofluid::audio
{

/// The spectrum Resolume delivers. Fixed by the host, not chosen here.
constexpr int kBins = 64;

struct Settings
{
	float attackSeconds  = 0.010f;
	float releaseSeconds = 0.250f;

	/// How easily an onset fires: the margin over the adaptive flux floor.
	float sensitivity = 0.6f;

	/// The time constant the latched kick decays with.
	float holdSeconds = 0.30f;
};

class Analyser
{
public:
	/// `bins` is what the host handed over; `count` may be less than kBins if
	/// it handed over fewer. `dt` is the frame in seconds.
	void Update( const float* bins, int count, float dt, const Settings& settings );

	/// The full-range level, normalised against its recent peak, 0..1.
	float Level() const;

	/// The latched kick: the level an onset arrived at, decaying since. 0..1.
	float Kick() const;

	/// A band's level, normalised against that band's own recent peak, 0..1.
	/// 0 Low, 1 Mid, 2 High, 3 Full.
	float BandLevel( int band ) const;

	/// Whether anything has been heard lately: the normalising peak is above
	/// its floor. With no audio routed it never is.
	bool HasSignal() const;

	/// True on the frame an onset was detected.
	bool Fired() const
	{
		return fired;
	}

	/// How many onsets have been detected since the plugin loaded.
	unsigned long long Onsets() const
	{
		return onsets;
	}

	/// Forget everything. Used when the host's clock jumps.
	void Reset();

	/// The --audio negative control: the first frame is never primed.
	void SetUnprimedForTest( bool on )
	{
		neverPrime = on;
	}

private:
	std::array< float, kBins > binLevel {};
	std::array< float, kBins > binPrevious {};

	static constexpr int kBands = 4;
	std::array< float, kBands > band {};    ///< enveloped mean per band
	std::array< float, kBands > bandPeak {};///< and its normalising peak

	float level      = 0.0f;///< enveloped, sqrt-compressed
	float peak       = 0.0f;///< slow decay, the normalising reference
	float flux       = 0.0f;///< positive spectral difference this frame
	float fluxMean   = 0.0f;///< running mean of the above, the adaptive floor
	float held       = 0.0f;///< the latched kick, decaying
	float refractory = 0.0f;///< seconds still to wait before another onset
	bool fired       = false;

	/// False until a frame has been seen. The first frame has no previous
	/// frame, so its flux is not small -- it is undefined, and computing it
	/// against a buffer of zeroes reports the entire spectrum as a rise. See
	/// Update().
	bool primed = false;
	bool neverPrime = false;

	unsigned long long onsets = 0;
};

} // namespace ferrofluid::audio
