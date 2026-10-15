#include "Ferrofluid.h"

/**
    The effect: the clip is on a screen hung over the pool, and the pool is a
    black mirror under it. A flat pool shows the clip as a plane mirror would;
    spikes break it into a lattice of small curved reflections.

    See SourcePlugin.cpp for why this file is listed in its own target.
    `SW Ferro Over` is short because FFGL names stop at 16 characters
    (polyhedral's `SW Dice Over` is the precedent).
*/
namespace
{
class FerrofluidEffect : public ferrofluid::FerrofluidPlugin
{
public:
	FerrofluidEffect() :
		FerrofluidPlugin( true )
	{
	}
};
} // namespace

static CFFGLPluginInfo PluginInfo(
	PluginFactory< FerrofluidEffect >,// Create method
	"FE02",                           // Plugin unique ID of maximum length 4
	"SW Ferro Over",                  // Plugin name
	2,                                // API major version number
	1,                                // API minor version number
	0,                                // Plugin major version number
	1,                                // Plugin minor version number
	FF_EFFECT,                        // Plugin type
	"The clip on a screen over a pool of ferrofluid, seen in its mirror. An electromagnet driven by the music stands the "
	"surface up in hexagonal spikes past the Rosensweig onset, and the clip breaks into a lattice of small curved "
	"reflections.",
	"Ferrofluid FFGL effect"// About
);

extern "C" const char* FerrofluidEffectBuildStamp()
{
	return "ferrofluid " FERROFLUID_VERSION " effect, built " __DATE__ " " __TIME__;
}
