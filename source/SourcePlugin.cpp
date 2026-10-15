#include "Ferrofluid.h"

/**
    The source: a pool of ferrofluid over an electromagnet, photographed in a
    dark studio with soft boxes in its mirror.

    Listed directly in the FerrofluidSource target, not in ferrofluid_core:
    both plugins share the class and not the `CFFGLPluginInfo` below, and
    putting either registration in the shared library would register both
    plugins into both bundles. The core is an OBJECT library because this
    registers itself from a file-scope constructor nothing references.

    `SW Ferrofluid` is thirteen characters; the FFGL name field is char[ 16 ]
    and not null-terminated. `oxbow probe` reads it back the way a host does.
*/
namespace
{
class FerrofluidSource : public ferrofluid::FerrofluidPlugin
{
public:
	FerrofluidSource() :
		FerrofluidPlugin( false )
	{
	}
};
} // namespace

static CFFGLPluginInfo PluginInfo(
	PluginFactory< FerrofluidSource >,// Create method
	"FE01",                           // Plugin unique ID of maximum length 4
	"SW Ferrofluid",                  // Plugin name
	2,                                // API major version number
	1,                                // API minor version number
	0,                                // Plugin major version number
	1,                                // Plugin minor version number
	FF_SOURCE,                        // Plugin type
	"A pool of ferrofluid over an electromagnet whose current is the music, seen as the black mirror it is. The coil's "
	"field magnetises the fluid, and past a critical magnetisation the flat surface stands up in spikes on a hexagonal "
	"lattice whose spacing gravity and surface tension fix (the Rosensweig instability), lit by a studio's soft boxes.",
	"Ferrofluid FFGL source"// About
);

extern "C" const char* FerrofluidSourceBuildStamp()
{
	return "ferrofluid " FERROFLUID_VERSION " source, built " __DATE__ " " __TIME__;
}
