/**
    fetest -- render Ferrofluid offline, and measure what the pool and the
    light are doing.

    It drives the REAL plugin class, through the same ProcessOpenGL a host
    calls, on a synthetic clock, in a headless CGL context, and reads back the
    host's output -- and the plugin's own spectral state and surface -- as
    floats. What they are measured against is independent of the code that
    made them: an O(N^2) double DFT, the closed-form damped oscillator with
    the Cowley-Rosensweig frequency computed here from the datasheet (its own
    Langevin, its own derivative), Biot-Savart summed round the loop by hand,
    the exact Fresnel formula in double, a published critical field, and the
    clock.

        fetest --out /tmp/ff.png          the source at its defaults, 120 BPM, 3 s in
        fetest --over --out /tmp/o.png    the Over on the harness's card
        fetest --list                     every parameter and its default (--over: the effect's)
        fetest --pipe                     raw RGBA frames in on stdin, out on stdout
        fetest --film N                   N frames, raw RGBA on stdout
        fetest --offline                  the checks that need no GL context (CI)

    `--script` is the fleet's cue format: `frame  Parameter Name  value`
    lines, held before the first key and after the last. A STANDARD control is
    linear between its keys; an option or an event STEPS.

    FETEST_RENDERER=software asks for Apple's software renderer by id: what a
    GPU-less CI runner falls back to.
*/

#include "Coil.h"
#include "Controls.h"
#include "Drive.h"
#include "Ferrofluid.h"
#include "Fluid.h"
#include "Shaders.h"

#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <zlib.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <csignal>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <map>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

using namespace ferrofluid;

namespace
{
using Floats = std::vector< float >;
using Bytes  = std::vector< unsigned char >;
using Cplx   = std::complex< double >;
constexpr double kPi = 3.14159265358979323846;

//---------------------------------------------------------------------------
// Reporting.
//---------------------------------------------------------------------------
int g_failures = 0;
int g_checks   = 0;

std::string fmt( const char* format, ... )
{
	char buffer[ 4096 ];
	va_list args;
	va_start( args, format );
	std::vsnprintf( buffer, sizeof( buffer ), format, args );
	va_end( args );
	return buffer;
}

void Check( bool condition, const std::string& message )
{
	++g_checks;
	std::printf( "  %s  %s\n", condition ? "ok  " : "FAIL", message.c_str() );
	if( !condition )
		++g_failures;
}

int Verdict()
{
	std::printf( "\n  %s\n", g_failures == 0 ? "PASS" : "FAIL" );
	return g_failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// PNG. zlib ships with the OS.
//---------------------------------------------------------------------------
void putU32( Bytes& out, uint32_t value )
{
	out.push_back( static_cast< unsigned char >( value >> 24 ) );
	out.push_back( static_cast< unsigned char >( value >> 16 ) );
	out.push_back( static_cast< unsigned char >( value >> 8 ) );
	out.push_back( static_cast< unsigned char >( value ) );
}

void putChunk( Bytes& out, const char* type, const Bytes& data )
{
	putU32( out, static_cast< uint32_t >( data.size() ) );
	const size_t start = out.size();
	out.insert( out.end(), type, type + 4 );
	out.insert( out.end(), data.begin(), data.end() );
	uLong crc = crc32( 0L, Z_NULL, 0 );
	crc       = crc32( crc, out.data() + start, static_cast< uInt >( 4 + data.size() ) );
	putU32( out, static_cast< uint32_t >( crc ) );
}

/// `rgba` is floats, row 0 at the BOTTOM (GL's order); the file is written
/// top row first, which is the only place anything here flips.
bool writePng( const std::string& path, int width, int height, const Floats& rgba )
{
	Bytes raw;
	raw.reserve( static_cast< size_t >( height ) * ( 1 + static_cast< size_t >( width ) * 4 ) );
	for( int y = height - 1; y >= 0; --y )
	{
		raw.push_back( 0 );
		for( int x = 0; x < width; ++x )
			for( int c = 0; c < 4; ++c )
			{
				const float v = c == 3 ? 1.0f : rgba[ ( static_cast< size_t >( y ) * width + x ) * 4 + c ];
				raw.push_back( static_cast< unsigned char >( std::lround( std::clamp( v, 0.0f, 1.0f ) * 255.0f ) ) );
			}
	}
	uLongf compressedSize = compressBound( static_cast< uLong >( raw.size() ) );
	Bytes compressed( compressedSize );
	if( compress2( compressed.data(), &compressedSize, raw.data(), static_cast< uLong >( raw.size() ), 6 ) != Z_OK )
		return false;
	compressed.resize( compressedSize );
	Bytes png = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
	Bytes ihdr;
	putU32( ihdr, static_cast< uint32_t >( width ) );
	putU32( ihdr, static_cast< uint32_t >( height ) );
	ihdr.insert( ihdr.end(), { 8, 6, 0, 0, 0 } );
	putChunk( png, "IHDR", ihdr );
	putChunk( png, "IDAT", compressed );
	putChunk( png, "IEND", {} );
	FILE* file = std::fopen( path.c_str(), "wb" );
	if( !file )
		return false;
	const size_t written = std::fwrite( png.data(), 1, png.size(), file );
	std::fclose( file );
	return written == png.size();
}

//---------------------------------------------------------------------------
// The context.
//---------------------------------------------------------------------------
bool g_software = false;

CGLContextObj createContext()
{
	const CGLPixelFormatAttribute accelerated[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFAAccelerated,
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};
	const CGLPixelFormatAttribute fallback[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};
	const CGLPixelFormatAttribute generic[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFARendererID, static_cast< CGLPixelFormatAttribute >( kCGLRendererGenericFloatID ),
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};
	CGLPixelFormatObj format = nullptr;
	GLint formatCount        = 0;
	const char* renderer     = std::getenv( "FETEST_RENDERER" );
	if( renderer != nullptr && std::strcmp( renderer, "software" ) == 0 )
	{
		if( CGLChoosePixelFormat( generic, &format, &formatCount ) != kCGLNoError || format == nullptr )
			return nullptr;
		g_software = true;
		std::fprintf( stderr, "fetest: FETEST_RENDERER=software, Apple's software renderer\n" );
	}
	else if( CGLChoosePixelFormat( accelerated, &format, &formatCount ) != kCGLNoError || format == nullptr )
	{
		if( CGLChoosePixelFormat( fallback, &format, &formatCount ) != kCGLNoError || format == nullptr )
			return nullptr;
	}
	CGLContextObj context = nullptr;
	const CGLError error  = CGLCreateContext( format, nullptr, &context );
	CGLDestroyPixelFormat( format );
	if( error != kCGLNoError )
		return nullptr;
	CGLSetCurrentContext( context );
	return context;
}

const char* kindName( unsigned int type )
{
	switch( type )
	{
	case FF_TYPE_BOOLEAN: return "bool";
	case FF_TYPE_EVENT: return "event";
	case FF_TYPE_OPTION: return "option";
	case FF_TYPE_STANDARD: return "standard";
	case FF_TYPE_TEXT: return "text";
	case FF_TYPE_BUFFER: return "buffer";
	default: return "other";
	}
}

bool stepsBetweenCues( unsigned int type )
{
	return type == FF_TYPE_OPTION || type == FF_TYPE_BOOLEAN || type == FF_TYPE_EVENT;
}

//---------------------------------------------------------------------------
// The cue sheet.
//---------------------------------------------------------------------------
using Track = std::vector< std::pair< int, float > >;

struct RawCue
{
	int frame;
	std::string words;//everything after the frame: a name and a value
	int line;
};

float valueAt( const Track& track, int frame, bool ramp )
{
	if( track.empty() )
		return 0.0f;
	if( frame <= track.front().first )
		return track.front().second;
	if( frame >= track.back().first )
		return track.back().second;
	for( size_t i = 0; i + 1 < track.size(); ++i )
	{
		const auto& a = track[ i ];
		const auto& b = track[ i + 1 ];
		if( frame >= a.first && frame < b.first )
		{
			if( !ramp )
				return a.second;
			const float t = static_cast< float >( frame - a.first ) / static_cast< float >( b.first - a.first );
			return a.second + ( b.second - a.second ) * t;
		}
	}
	return track.back().second;
}

//---------------------------------------------------------------------------
// Pictures. Every one is rows BOTTOM first (GL's order), as uploaded.
//---------------------------------------------------------------------------
uint32_t pcg( uint32_t v )
{
	const uint32_t state = v * 747796405u + 2891336453u;
	const uint32_t word  = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
	return ( word >> 22u ) ^ word;
}
double hash01( uint32_t a, uint32_t b = 0 )
{
	return pcg( a * 2654435761u ^ pcg( b + 0x9e3779b9u ) ) * ( 1.0 / 4294967296.0 );
}

/// The look card (schlieren's): a sunset sky, a sun, a skyline, a stripe of
/// saturated bars and a white title block.
Floats buildCard( int width, int height )
{
	Floats card( static_cast< size_t >( width ) * height * 4 );
	const double s = std::min( width, height );
	for( int y = 0; y < height; ++y )
		for( int x = 0; x < width; ++x )
		{
			const double u = ( x + 0.5 - 0.5 * width ) / s, v = ( y + 0.5 - 0.5 * height ) / s;
			const double k = std::clamp( 0.5 + v, 0.0, 1.0 );
			double r = 0.95 - 0.8 * k, g = 0.45 - 0.3 * k, b = 0.25 + 0.5 * k;
			const double d = std::sqrt( ( u + 0.25 ) * ( u + 0.25 ) + ( v + 0.02 ) * ( v + 0.02 ) );
			if( d < 0.2 )
			{
				r = 1.0;
				g = 0.85 - 0.5 * d;
				b = 0.35;
			}
			const int block  = static_cast< int >( std::floor( ( u + 2.0 ) * 11.0 ) );
			const double top = -0.12 + 0.16 * hash01( static_cast< uint32_t >( block ), 7 );
			if( v < top )
			{
				r = g = b = 0.05;
				const int wx = static_cast< int >( std::floor( ( u + 2.0 ) * 70.0 ) ), wy = static_cast< int >( std::floor( v * 60.0 + 100.0 ) );
				if( ( wx % 3 ) != 0 && ( wy % 2 ) == 0 && hash01( static_cast< uint32_t >( wx * 131 + wy ), 9 ) > 0.55 )
				{
					r = 1.0;
					g = 0.9;
					b = 0.55;
				}
			}
			if( v < -0.36 )
			{
				const int bar               = std::clamp( static_cast< int >( std::floor( ( x + 0.5 ) / width * 7.0 ) ), 0, 6 );
				const double bars[ 7 ][ 3 ] = { { 1, 1, 1 }, { 1, 1, 0 }, { 0, 1, 1 }, { 0, 1, 0 }, { 1, 0, 1 }, { 1, 0, 0 }, { 0, 0, 1 } };
				r = 0.75 * bars[ bar ][ 0 ];
				g = 0.75 * bars[ bar ][ 1 ];
				b = 0.75 * bars[ bar ][ 2 ];
			}
			if( std::fabs( u - 0.42 ) < 0.28 && std::fabs( v - 0.3 ) < 0.06 )
				r = g = b = 0.95;
			float* o = &card[ ( static_cast< size_t >( y ) * width + x ) * 4 ];
			o[ 0 ]   = static_cast< float >( r );
			o[ 1 ]   = static_cast< float >( g );
			o[ 2 ]   = static_cast< float >( b );
			o[ 3 ]   = 1.0f;
		}
	return card;
}

Floats flatCard( int width, int height, float r, float g, float b, float a = 1.0f )
{
	Floats card( static_cast< size_t >( width ) * height * 4 );
	for( size_t i = 0; i < card.size(); i += 4 )
	{
		card[ i ]     = r;
		card[ i + 1 ] = g;
		card[ i + 2 ] = b;
		card[ i + 3 ] = a;
	}
	return card;
}

/// Every pixel different: a hash per pixel, so a pixel read from its
/// neighbour cannot pass for itself (--mirror).
Floats noiseCard( int width, int height )
{
	Floats card( static_cast< size_t >( width ) * height * 4 );
	for( int y = 0; y < height; ++y )
		for( int x = 0; x < width; ++x )
		{
			float* o = &card[ ( static_cast< size_t >( y ) * width + x ) * 4 ];
			for( int c = 0; c < 3; ++c )
				o[ c ] = static_cast< float >( std::lround( 255.0 * hash01( static_cast< uint32_t >( ( y * width + x ) * 3 + c ), 5 ) ) ) / 255.0f;
			o[ 3 ] = 1.0f;
		}
	return card;
}

GLuint makeTexture( int width, int height, const float* pixels, GLint format = GL_RGBA32F )
{
	GLuint texture = 0;
	glGenTextures( 1, &texture );
	glBindTexture( GL_TEXTURE_2D, texture );
	glTexImage2D( GL_TEXTURE_2D, 0, format, width, height, 0, GL_RGBA, GL_FLOAT, pixels );
	//Linear, as a host's clip texture is.
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glBindTexture( GL_TEXTURE_2D, 0 );
	return texture;
}

Floats readTexture( GLuint texture, int width, int height, GLenum layout = GL_RGBA, int channels = 4 )
{
	Floats out( static_cast< size_t >( width ) * height * channels );
	glBindTexture( GL_TEXTURE_2D, texture );
	glPixelStorei( GL_PACK_ALIGNMENT, 1 );
	glGetTexImage( GL_TEXTURE_2D, 0, layout, GL_FLOAT, out.data() );
	glBindTexture( GL_TEXTURE_2D, 0 );
	return out;
}

//---------------------------------------------------------------------------
// A rig: the real plugin, a float output framebuffer, a synthetic clock and,
// when asked for, a synthetic beat.
//---------------------------------------------------------------------------
struct Rig
{
	FerrofluidPlugin plugin;
	bool over;
	int width = 0, height = 0;
	GLuint sourceTexture = 0, outputTexture = 0, outputFBO = 0;
	int frame  = 0;
	double fps = 60.0;
	double bpm = 0.0;///< 0: no beat sent
	std::function< void( int ) > beforeFrame;

	ProcessOpenGLStruct process    = {};
	FFGLTextureStruct inputStruct  = {};
	FFGLTextureStruct* inputs[ 1 ] = { nullptr };

	explicit Rig( bool effect = false ) :
		plugin( effect ),
		over( effect )
	{
	}
	~Rig()
	{
		plugin.DeInitGL();
		release();
	}

	void release()
	{
		if( outputFBO )
			glDeleteFramebuffers( 1, &outputFBO );
		if( outputTexture )
			glDeleteTextures( 1, &outputTexture );
		if( sourceTexture )
			glDeleteTextures( 1, &sourceTexture );
		outputFBO = outputTexture = sourceTexture = 0;
	}

	bool attach( int w, int h, const Floats* picture )
	{
		width         = w;
		height        = h;
		outputTexture = makeTexture( width, height, nullptr );
		glGenFramebuffers( 1, &outputFBO );
		glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outputTexture, 0 );
		if( glCheckFramebufferStatus( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE )
			return false;
		process.HostFBO = outputFBO;
		if( over )
		{
			const Floats card = picture ? *picture : buildCard( width, height );
			sourceTexture     = makeTexture( width, height, card.data() );
			inputStruct.Width = inputStruct.HardwareWidth = static_cast< FFUInt32 >( width );
			inputStruct.Height = inputStruct.HardwareHeight = static_cast< FFUInt32 >( height );
			inputStruct.Handle                              = sourceTexture;
			inputs[ 0 ]                                     = &inputStruct;
			process.numInputTextures                        = 1;
			process.inputTextures                           = inputs;
		}
		else
		{
			process.numInputTextures = 0;
			process.inputTextures    = nullptr;
		}
		return true;
	}

	bool Init( int w, int h, const Floats* picture = nullptr )
	{
		FFGLViewportStruct viewport = {};
		viewport.width              = static_cast< FFUInt32 >( w );
		viewport.height             = static_cast< FFUInt32 >( h );
		if( plugin.InitGL( &viewport ) != FF_SUCCESS )
		{
			std::fprintf( stderr, "InitGL failed -- see ~/Library/Logs/ferrofluid for which shader\n" );
			return false;
		}
		plugin.SetClockScaleForTest( 1.0 );
		return attach( w, h, picture );
	}

	/// The host's raster changes under a running instance: no InitGL.
	bool Resize( int w, int h, const Floats* picture = nullptr )
	{
		release();
		return attach( w, h, picture );
	}

	void Upload( const Floats& picture )
	{
		glBindTexture( GL_TEXTURE_2D, sourceTexture );
		glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_FLOAT, picture.data() );
		glBindTexture( GL_TEXTURE_2D, 0 );
	}

	void Set( unsigned int id, float value )
	{
		plugin.SetById( id, value );
	}

	double Seconds( int f ) const
	{
		return static_cast< double >( f ) / fps;
	}

	bool Render( int frames = 1 )
	{
		for( int i = 0; i < frames; ++i )
		{
			if( beforeFrame )
				beforeFrame( frame );
			plugin.SetTime( Seconds( frame ) );
			if( bpm > 0.0 )
				plugin.SetBeatInfo( static_cast< float >( bpm ), static_cast< float >( std::fmod( Seconds( frame ) * bpm / 240.0, 1.0 ) ) );
			++frame;
			glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
			glViewport( 0, 0, width, height );
			glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
			glClear( GL_COLOR_BUFFER_BIT );
			if( plugin.ProcessOpenGL( &process ) != FF_SUCCESS )
			{
				std::fprintf( stderr, "ProcessOpenGL failed\n" );
				return false;
			}
		}
		return true;
	}

	/// Press an event on the next frame and render it.
	bool Press( unsigned int id )
	{
		Set( id, 1.0f );
		const bool ok = Render( 1 );
		Set( id, 0.0f );
		return ok;
	}

	Floats Output() const
	{
		Floats pixels( static_cast< size_t >( width ) * height * 4 );
		glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
		glPixelStorei( GL_PACK_ALIGNMENT, 1 );
		glReadPixels( 0, 0, width, height, GL_RGBA, GL_FLOAT, pixels.data() );
		return pixels;
	}

	/// ( h, 0, h_x, h_y ) as the camera sees it, twice the pool's grid.
	Floats Surface() const
	{
		return readTexture( plugin.SurfaceTextureID(), plugin.SurfaceWidth(), plugin.SurfaceHeight() );
	}
	/// The spectral state ( h_k, v_k ).
	Floats State() const
	{
		const Pool& p = plugin.CurrentPool();
		return readTexture( plugin.StateTextureID(), p.nx, p.ny );
	}
};

int byteOf( float v )
{
	return static_cast< int >( std::lround( std::clamp( v, 0.0f, 1.0f ) * 255.0f ) );
}

//---------------------------------------------------------------------------
// Parameters by display name.
//---------------------------------------------------------------------------
struct NamedParameter
{
	std::string name;
	unsigned int index;
	float value;
	unsigned int type;
};

std::vector< NamedParameter > listParameters( FerrofluidPlugin& plugin )
{
	std::vector< NamedParameter > list;
	for( unsigned int i = 0; i < plugin.GetNumParams(); ++i )
	{
		const char* const name = plugin.GetParamName( i );
		list.push_back( NamedParameter { name ? name : "?", i, plugin.GetFloatParameter( i ), plugin.GetParamType( i ) } );
	}
	return list;
}

/// `Name=Value`. The value is a number, or for an option the name of one of
/// its elements; anything else is refused, never read as 0 (polyhedral's trap).
bool applySetting( FerrofluidPlugin& plugin, const std::string& assignment, std::string& error )
{
	const size_t equals = assignment.find( '=' );
	if( equals == std::string::npos )
	{
		error = "expected Name=Value";
		return false;
	}
	const std::string name  = assignment.substr( 0, equals );
	const std::string value = assignment.substr( equals + 1 );
	for( const NamedParameter& parameter : listParameters( plugin ) )
		if( parameter.name == name )
		{
			if( parameter.type == FF_TYPE_TEXT || parameter.type == FF_TYPE_BUFFER )
			{
				error = name + " cannot be set from here";
				return false;
			}
			if( parameter.type == FF_TYPE_OPTION )
				for( unsigned int e = 0; e < plugin.GetNumParamElements( parameter.index ); ++e )
					if( value == plugin.GetParamElementName( parameter.index, e ) )
					{
						plugin.SetFloatParameter( parameter.index, static_cast< float >( e ) );
						return true;
					}
			char* end       = nullptr;
			const float got = std::strtof( value.c_str(), &end );
			if( value.empty() || end == value.c_str() || *end != '\0' )
			{
				error = "'" + value + "' is not a number" + ( parameter.type == FF_TYPE_OPTION ? " or an option of " + name : "" );
				return false;
			}
			plugin.SetFloatParameter( parameter.index, got );
			return true;
		}
	error = "no parameter called '" + name + "'";
	return false;
}

struct Cue
{
	Track track;
	bool ramp;
};

/// A cue line's name is the longest run of words that is a parameter, and the
/// rest is its value: a number, or an option's name (honeydew's rule).
bool bindScript( FerrofluidPlugin& plugin, std::istream& in, const std::string& path, std::map< unsigned int, Cue >& out,
                 std::string& error )
{
	const std::vector< NamedParameter > known = listParameters( plugin );
	std::string line;
	int lineNumber = 0;
	while( std::getline( in, line ) )
	{
		++lineNumber;
		const size_t hash = line.find( '#' );
		if( hash != std::string::npos )
			line.erase( hash );
		std::istringstream words( line );
		int frame = 0;
		if( !( words >> frame ) )
			continue;
		std::vector< std::string > parts;
		std::string word;
		while( words >> word )
			parts.push_back( word );
		const NamedParameter* found = nullptr;
		size_t used                 = 0;
		for( size_t n = parts.size() > 0 ? parts.size() - 1 : 0; n >= 1 && found == nullptr; --n )
		{
			std::string name = parts[ 0 ];
			for( size_t i = 1; i < n; ++i )
				name += " " + parts[ i ];
			for( const NamedParameter& p : known )
				if( p.name == name )
				{
					found = &p;
					used  = n;
				}
		}
		if( found == nullptr )
		{
			error = path + ":" + std::to_string( lineNumber ) + ": names no parameter (try --list)";
			return false;
		}
		std::string value = parts[ used ];
		for( size_t i = used + 1; i < parts.size(); ++i )
			value += " " + parts[ i ];
		float v      = 0.0f;
		bool matched = false;
		if( found->type == FF_TYPE_OPTION )
			for( unsigned int e = 0; e < plugin.GetNumParamElements( found->index ); ++e )
				if( value == plugin.GetParamElementName( found->index, e ) )
				{
					v       = static_cast< float >( e );
					matched = true;
				}
		if( !matched )
		{
			char* end = nullptr;
			v         = std::strtof( value.c_str(), &end );
			if( value.empty() || end == value.c_str() || *end != '\0' )
			{
				error = path + ":" + std::to_string( lineNumber ) + ": '" + value + "' is not a number";
				return false;
			}
		}
		Cue& cue = out[ found->index ];
		cue.ramp = !stepsBetweenCues( found->type );
		cue.track.emplace_back( frame, v );
	}
	for( auto& entry : out )
		std::stable_sort( entry.second.track.begin(), entry.second.track.end(),
		                  []( const std::pair< int, float >& a, const std::pair< int, float >& b ) { return a.first < b.first; } );
	return true;
}

/// The harness's music: a busy hi-hat (a fresh level every frame, so the
/// onset detector's floor is not zero) and a kick every half second, soft
/// and hard by turns, written into the Audio buffer the way the host writes it.
void feedKicks( FerrofluidPlugin& plugin, int frame )
{
	const int since  = frame % 30;
	const bool hard  = ( frame / 30 ) % 2 == 1;
	const double hat = 0.3 * hash01( static_cast< uint32_t >( frame ), 11 );
	const unsigned int host = static_cast< unsigned int >( plugin.HostIndexOf( PT_AUDIO ) );
	for( int bin = 0; bin < audio::kBins; ++bin )
	{
		double v = bin > 40 ? 0.02 + hat : 0.02;
		if( since < 4 && bin < 10 )
			v += ( hard ? 0.9 : 0.25 ) * std::exp( -since * 0.8 );
		plugin.SetParamElementValue( host, static_cast< unsigned int >( bin ), static_cast< float >( v ) );
	}
}

//===========================================================================
// --pipe and --film. Raw RGBA, top row first, on the synthetic clock, with a
// beat at --bpm (120 unless told otherwise). --pipe reads a frame from stdin
// for every frame it writes (the Over's clip; the source reads it and only
// keeps the pace); --film N renders N on the harness's card.
//===========================================================================
int runPipe( bool over, int width, int height, double fps, double bpm, const std::string& scriptPath, int count, bool readStdin,
             const std::vector< std::string >& settings, bool kicks )
{
	Rig rig( over );
	rig.fps = fps;
	rig.bpm = bpm;
	if( !rig.Init( width, height ) )
		return 1;
	for( const std::string& setting : settings )
	{
		std::string error;
		if( !applySetting( rig.plugin, setting, error ) )
		{
			std::fprintf( stderr, "--set %s: %s\n", setting.c_str(), error.c_str() );
			return 2;
		}
	}
	std::map< unsigned int, Cue > automation;
	if( !scriptPath.empty() )
	{
		std::ifstream file( scriptPath );
		std::string error;
		if( !file )
			error = "cannot open " + scriptPath;
		else
			bindScript( rig.plugin, file, scriptPath, automation, error );
		if( !error.empty() )
		{
			std::fprintf( stderr, "%s\n", error.c_str() );
			return 2;
		}
	}

	Bytes in( static_cast< size_t >( width ) * height * 4 );
	Floats picture( in.size() );
	for( int index = 0; readStdin || index < count; ++index )
	{
		if( readStdin )
		{
			size_t filled = 0;
			while( filled < in.size() )
			{
				const ssize_t got = read( STDIN_FILENO, in.data() + filled, in.size() - filled );
				if( got <= 0 )
					break;
				filled += static_cast< size_t >( got );
			}
			//A partial frame is the end of the stream, never a frame.
			if( filled < in.size() )
			{
				if( filled > 0 )
					std::fprintf( stderr, "partial frame at the end (%zu of %zu bytes): dropped\n", filled, in.size() );
				break;
			}
			if( over )
			{
				for( int y = 0; y < height; ++y )
					for( int x = 0; x < width * 4; ++x )
						picture[ static_cast< size_t >( height - 1 - y ) * width * 4 + x ] = in[ static_cast< size_t >( y ) * width * 4 + x ] / 255.0f;
				rig.Upload( picture );
			}
		}
		if( kicks )
			feedKicks( rig.plugin, index );
		//A track does nothing before its first key: a press cued at frame 30
		//must not be held from frame 0 (honeydew's trap).
		for( const auto& cue : automation )
			if( index >= cue.second.track.front().first || cue.second.ramp )
				rig.plugin.SetFloatParameter( cue.first, valueAt( cue.second.track, index, cue.second.ramp ) );
		if( !rig.Render( 1 ) )
			return 1;

		const Floats out = rig.Output();
		Bytes bytes( in.size() );
		for( int y = 0; y < height; ++y )
			for( int x = 0; x < width * 4; ++x )
				bytes[ static_cast< size_t >( y ) * width * 4 + x ] = static_cast< unsigned char >(
					std::lround( std::clamp( out[ static_cast< size_t >( height - 1 - y ) * width * 4 + x ], 0.0f, 1.0f ) * 255.0f ) );
		size_t written = 0;
		while( written < bytes.size() )
		{
			const ssize_t put = write( STDOUT_FILENO, bytes.data() + written, bytes.size() - written );
			//The reader has gone. SIGPIPE is ignored in main(), so this is
			//EPIPE and not a silent 141: say so and stop.
			if( put <= 0 )
			{
				std::fprintf( stderr, "stdout closed at frame %d\n", index );
				return 1;
			}
			written += static_cast< size_t >( put );
		}
	}
	return 0;
}

//===========================================================================
// The checks are below main's helpers; see checks().
//===========================================================================
struct Perturb
{
	int hooks        = 0;///< shaders::Hook bits
	int physicsHooks = 0;///< physics::PhysicsHook bits
	int coilHooks    = 0;///< coil::CoilHook bits
	int driveHooks   = 0;///< DriveHook bits
	bool resizeClears = false;
	bool unprimed     = false;
	bool cuesRamp     = false;
	double hyScale    = -1.0;///< --hysteresis's negative: the quadratic coefficient forced to this
};

using CheckFn = int ( * )( const Perturb& );

struct Raster
{
	int w, h;
};
std::vector< Raster > kRasters = { { 1280, 720 }, { 320, 180 } };

#include "checks.inc"

} // namespace

//---------------------------------------------------------------------------
int main( int argc, char** argv )
{
	std::string outPath = "/tmp/ferrofluid.png";
	std::vector< std::string > settings;
	int width = 1280, height = 720, frames = 180;
	double fps = 60.0, bpm = 120.0;
	std::string mode, scriptPath;
	int filmFrames = -1;
	bool sizeGiven = false, kicks = false, over = false;
	std::map< int, std::vector< std::string > > later;//--at FRAME "Name=V"
	std::string clipPath;
	int debugHooks = 0;//--hooks N: shaders::Hook bits on the plain render, for looking at one stage
	std::string dumpPath;//--dump PATH: the surface ( h, 0, h_x, h_y ) as raw float32, nx ny first

	for( int i = 1; i < argc; ++i )
	{
		const std::string argument = argv[ i ];
		const bool hasNext         = i + 1 < argc;
		if( argument == "--help" || argument == "-h" )
		{
			std::printf( "fetest -- render Ferrofluid offline and measure its pool and its light\n\n"
			             "  --over            the Over (SW Ferro Over) instead of the source\n"
			             "  --out PATH        render and write a PNG (default /tmp/ferrofluid.png)\n"
			             "  --clip PATH       (with --over --out) the clip: raw RGBA, top row first, at --size\n"
			             "  --size WxH        render size (default 1280x720)\n"
			             "  --frames N        frames before reading back (default 180)\n"
			             "  --fps N           the synthetic clock's rate (default 60)\n"
			             "  --bpm N           the synthetic beat (default 120; 0 sends none)\n"
			             "  --set \"Name=V\"    set a parameter by its display name (an option by its name). Repeatable.\n"
			             "  --at N \"Name=V\"  (with --out) set it at frame N instead; an event is released the frame after\n"
			             "  --kicks           the harness's music in the Audio buffer: a kick every half second\n"
			             "  --list            every parameter and its default\n"
			             "  --pipe            raw RGBA frames in on stdin, out on stdout\n"
			             "  --film N          N frames, raw RGBA on stdout\n"
			             "  --script PATH     cues for --pipe/--film: 'frame Name value'\n\n"
			             "  checks: see tools/verify.sh;  --negative   --bench\n"
			             "  --offline         the checks and negative controls that need no GL context (CI)\n" );
			return 0;
		}
		else if( argument == "--over" )
			over = true;
		else if( argument == "--out" && hasNext )
			outPath = argv[ ++i ];
		else if( argument == "--dump" && hasNext )
			dumpPath = argv[ ++i ];
		else if( argument == "--hooks" && hasNext )
			debugHooks = std::atoi( argv[ ++i ] );
		else if( argument == "--clip" && hasNext )
			clipPath = argv[ ++i ];
		else if( argument == "--set" && hasNext )
			settings.push_back( argv[ ++i ] );
		else if( argument == "--frames" && hasNext )
			frames = std::atoi( argv[ ++i ] );
		else if( argument == "--fps" && hasNext )
			fps = std::strtod( argv[ ++i ], nullptr );
		else if( argument == "--bpm" && hasNext )
			bpm = std::strtod( argv[ ++i ], nullptr );
		else if( argument == "--kicks" )
			kicks = true;
		else if( argument == "--at" && i + 2 < argc )
		{
			const int at = std::atoi( argv[ ++i ] );
			later[ at ].push_back( argv[ ++i ] );
		}
		else if( argument == "--pipe" )
			mode = "pipe";
		else if( argument == "--film" && hasNext )
		{
			mode       = "film";
			filmFrames = std::max( 1, std::atoi( argv[ ++i ] ) );
		}
		else if( argument == "--script" && hasNext )
			scriptPath = argv[ ++i ];
		else if( argument == "--list" )
			mode = "list";
		else if( argument == "--size" && hasNext )
		{
			const std::string value = argv[ ++i ];
			const size_t cross      = value.find( 'x' );
			if( cross != std::string::npos )
			{
				width  = std::atoi( value.substr( 0, cross ).c_str() );
				height = std::atoi( value.substr( cross + 1 ).c_str() );
			}
			sizeGiven = true;
		}
		else if( argument.rfind( "--", 0 ) == 0 )
			mode = argument.substr( 2 );
		else
		{
			std::fprintf( stderr, "unknown argument '%s' (try --help)\n", argument.c_str() );
			return 2;
		}
	}
	if( width <= 0 || height <= 0 || !( fps > 0.0 ) )
	{
		std::fprintf( stderr, "--size and --fps must be positive\n" );
		return 2;
	}
	//A check given --size runs at that raster alone (the software pass).
	if( sizeGiven )
		kRasters = { { width, height } };

	if( mode == "list" )
	{
		FerrofluidPlugin plugin( over );
		std::printf( "%-3s %-18s %-9s %s\n", "id", "name", "kind", "default" );
		for( const NamedParameter& parameter : listParameters( plugin ) )
			std::printf( "%-3u %-18s %-9s %.4f\n", parameter.index, parameter.name.c_str(), kindName( parameter.type ), parameter.value );
		return 0;
	}

	//A reader that hangs up must end --pipe/--film with exit 1 and a message,
	//not SIGPIPE's silent 141: ignored here, the write fails with EPIPE.
	std::signal( SIGPIPE, SIG_IGN );

	if( mode == "offline" )
		return runOffline();
	for( const CheckEntry& check : checks() )
		if( mode == check.flag && check.offline )
			return check.run( Perturb {} );

	CGLContextObj context = createContext();
	if( context == nullptr )
	{
		std::fprintf( stderr, "could not create an OpenGL 4.1 core context\n" );
		return 1;
	}

	int result = 0;
	bool ran   = false;
	for( const CheckEntry& check : checks() )
		if( mode == check.flag )
		{
			result = check.run( Perturb {} );
			ran    = true;
		}

	if( ran )
		;
	else if( mode == "pipe" )
		result = runPipe( over, width, height, fps, bpm, scriptPath, 0, true, settings, kicks );
	else if( mode == "film" )
		result = runPipe( over, width, height, fps, bpm, scriptPath, filmFrames, false, settings, kicks );
	else if( mode == "negative" )
		result = runNegative();
	else if( mode == "bench" )
		result = runBench();
	else if( mode == "fftbench" )
		result = runFFTBench();
	else if( !mode.empty() )
	{
		std::fprintf( stderr, "unknown mode --%s (try --help)\n", mode.c_str() );
		result = 2;
	}
	else
	{
		Rig rig( over );
		rig.fps = fps;
		rig.bpm = bpm;
		Floats clip;
		if( !clipPath.empty() )
		{
			std::ifstream file( clipPath, std::ios::binary );
			Bytes raw( static_cast< size_t >( width ) * height * 4 );
			if( !file.read( reinterpret_cast< char* >( raw.data() ), static_cast< std::streamsize >( raw.size() ) ) )
			{
				std::fprintf( stderr, "--clip %s: not %d x %d raw RGBA\n", clipPath.c_str(), width, height );
				return 2;
			}
			clip.resize( raw.size() );
			for( int y = 0; y < height; ++y )
				for( int x = 0; x < width * 4; ++x )
					clip[ static_cast< size_t >( height - 1 - y ) * width * 4 + x ] = raw[ static_cast< size_t >( y ) * width * 4 + x ] / 255.0f;
		}
		if( !rig.Init( width, height, clip.empty() ? nullptr : &clip ) )
			result = 1;
		else
		{
			rig.plugin.SetHooksForTest( debugHooks );
			for( const std::string& setting : settings )
			{
				std::string error;
				if( !applySetting( rig.plugin, setting, error ) )
				{
					std::fprintf( stderr, "--set %s: %s\n", setting.c_str(), error.c_str() );
					return 2;
				}
			}
			bool rendered = true;
			std::vector< std::pair< int, std::string > > release;
			for( int f = 0; f < std::max( frames, 1 ) && rendered; ++f )
			{
				if( kicks )
					feedKicks( rig.plugin, f );
				for( const auto& r : release )
					if( r.first == f )
					{
						std::string ignored;
						applySetting( rig.plugin, r.second + "=0", ignored );
					}
				const auto due = later.find( f );
				if( due != later.end() )
					for( const std::string& setting : due->second )
					{
						std::string error;
						if( !applySetting( rig.plugin, setting, error ) )
						{
							std::fprintf( stderr, "--at %d %s: %s\n", f, setting.c_str(), error.c_str() );
							return 2;
						}
						const std::string name = setting.substr( 0, setting.find( '=' ) );
						for( const NamedParameter& parameter : listParameters( rig.plugin ) )
							if( parameter.name == name && parameter.type == FF_TYPE_EVENT )
								release.emplace_back( f + 1, name );
					}
				rendered = rig.Render( 1 );
			}
			if( rendered && !dumpPath.empty() )
			{
				const Floats s  = rig.Surface();
				const Pool& p   = rig.plugin.CurrentPool();
				FILE* file      = std::fopen( dumpPath.c_str(), "wb" );
				const int32_t n[ 2 ] = { rig.plugin.SurfaceWidth(), rig.plugin.SurfaceHeight() };
				const double l[ 2 ]  = { p.lx, p.ly };
				if( file )
				{
					std::fwrite( n, sizeof( n ), 1, file );
					std::fwrite( l, sizeof( l ), 1, file );
					std::fwrite( s.data(), sizeof( float ), s.size(), file );
					std::fclose( file );
				}
			}
			if( !rendered )
				result = 1;
			else if( writePng( outPath, width, height, rig.Output() ) )
			{
				const Plan& p       = rig.plugin.LastPlan();
				const Floats bound  = readTexture( rig.plugin.BoundsTextureID(), 1, 1 );
				const double lambda = physics::CriticalWavelength( rig.plugin.Fluid() );
				std::printf( "wrote %s -- %dx%d, %d frames at %g fps (%.2f s); %d substeps of %.3f ms; current %.3f Ic (Ic %.2f A), "
				             "m %.3f; max |h| %.2f mm (%.2f lambda_c), steepest slope %.2f\n",
				             outPath.c_str(), width, height, frames, fps, frames / fps, p.substeps, p.dt * 1e3,
				             rig.plugin.DriveState().Current( 0 ), rig.plugin.CriticalCurrent(),
				             physics::MagneticNumber( rig.plugin.Fluid(), p.stressMax ), bound[ 0 ] * 1e3, bound[ 0 ] / lambda, bound[ 1 ] );
			}
			else
				result = 1;
		}
	}

	CGLSetCurrentContext( nullptr );
	CGLDestroyContext( context );
	return result;
}
