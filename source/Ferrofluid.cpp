#include "Ferrofluid.h"

#include "Diag.h"
#include "GLState.h"
#include "Shaders.h"

//FFGLSDK.h includes every other scoped binding and omits this one (SDK
//b1afaf9), so it has to be asked for by name.
#include <ffglex/FFGLScopedFBOBinding.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

using namespace ffglex;

namespace ferrofluid
{
namespace
{
/// Frames that must agree before the host's clock unit is settled.
constexpr int kClockVotes = 4;
/// Host seconds a single frame may advance by: a stalled host must not throw
/// the pool a tenth of a second of current at once.
constexpr double kMaxFrameDelta = 0.1;
/// The pool's grid: 512 x 256 cells at 12 a critical wavelength (AGENTS.md).
constexpr int kPoolX = 512, kPoolY = 256;
constexpr double kCellsPerWavelength = 12.0;
/// The stress is low-passed above this many k_c (the evolve's ForceCut), and
/// the model's W-weighted part above the plan's outerCut: that part's
/// stiffness grows as q^3, so it is cut as low as is safe -- never below 3 k_c
/// (the harmonics that shape a spike, sqrt 3 and 2 k_c, are well inside), and
/// never below the top of the band the frame's strongest field makes
/// unstable, m + sqrt( m^2 - 1 ): a mode that grows must be one the
/// saturation can hold, or it grows without end (`fetest --extremes`).
constexpr double kForceCut  = 4.0;
constexpr double kOuterCutMin = 3.0;
/// Substeps a frame may run; past it the pool falls behind real time.
constexpr int kMaxSubsteps = 48;
/// The longest substep, s, whatever the plan allows.
constexpr double kMaxSubstep = 2e-3;
/// The lens: a 100 mm on a 36 mm frame, so the half field of view's tangent
/// is 18 / 100 across the frame.
constexpr double kLensTanHalf = 0.18;

double wallSeconds()
{
	using namespace std::chrono;
	static const steady_clock::time_point start = steady_clock::now();
	return duration_cast< duration< double > >( steady_clock::now() - start ).count();
}

std::string glStringOrUnknown( GLenum name )
{
	const GLubyte* value = glGetString( name );
	return value ? reinterpret_cast< const char* >( value ) : "unknown";
}

GLuint makeTable( GLint format, GLenum layout, int width, const float* data )
{
	GLuint texture = 0;
	glGenTextures( 1, &texture );
	//Bound and unbound by hand: this runs before anything is bound, and must
	//leave nothing.
	glBindTexture( GL_TEXTURE_2D, texture );
	glTexImage2D( GL_TEXTURE_2D, 0, format, width, 1, 0, layout, GL_FLOAT, data );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glBindTexture( GL_TEXTURE_2D, 0 );
	return texture;
}

/// exp( -2 pi i m / length ), m = 0 .. length-1, from doubles: a cos and sin
/// in the shader cost a few ulp, and the SAME few every frame, which a state
/// fed back sixty times a second accumulates (millpond's lesson).
GLuint makeTwiddles( int length )
{
	std::vector< float > table( static_cast< size_t >( length ) * 2 );
	for( int m = 0; m < length; ++m )
	{
		const double angle = -2.0 * physics::kPi * static_cast< double >( m ) / length;
		table[ m * 2 + 0 ] = static_cast< float >( std::cos( angle ) );
		table[ m * 2 + 1 ] = static_cast< float >( std::sin( angle ) );
	}
	return makeTable( GL_RG32F, GL_RG, length, table.data() );
}

/// Every pass here writes every texel of its target, so what the target held
/// does not matter -- but Apple's GL runs on Metal, where binding a
/// framebuffer starts a render pass that LOADS the attachment unless it is
/// cleared first: a full read of the texture before every pass, for nothing.
/// A clear becomes the pass's load action, which costs nothing. (GL 4.1 has
/// no glInvalidateFramebuffer.)
void discardTarget()
{
	glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
	glClear( GL_COLOR_BUFFER_BIT );
}

/// The radices for a power of two: sixteens, then what is left.
std::vector< int > radicesOf( int length )
{
	std::vector< int > out;
	while( length > 1 )
	{
		const int r = length >= 16 ? 16 : length;
		out.push_back( r );
		length /= r;
	}
	return out;
}

void unitVector( double v[ 3 ] )
{
	const double l = std::sqrt( v[ 0 ] * v[ 0 ] + v[ 1 ] * v[ 1 ] + v[ 2 ] * v[ 2 ] );
	for( int i = 0; i < 3; ++i )
		v[ i ] /= l;
}

/// One soft box, in units of the field width: its centre, its half width
/// and half height, and its radiance (linear). It faces the pool's centre.
struct Box
{
	double x, y, z, halfW, halfH;
	float r, g, b;
};
} // namespace

static_assert( PT_COUNT - PT_ABOUT_TEXT == stoatworks::about::kParamCount,
               "the About run no longer matches StoatworksAbout.h" );

//---------------------------------------------------------------------------
FerrofluidPlugin::FerrofluidPlugin( bool effect ) :
	isEffect( effect ),
	hostOrder( HostOrder( effect ) )
{
	SetMinInputs( isEffect ? 1 : 0 );
	SetMaxInputs( isEffect ? 1 : 0 );

	//The pool runs on the host's clock: its waves have frequencies in hertz,
	//so the plugin has to know what a second is.
	SetTimeSupported( true );

	std::fill( std::begin( idToHost ), std::end( idToHost ), -1 );
	for( size_t i = 0; i < hostOrder.size(); ++i )
		idToHost[ hostOrder[ i ] ] = static_cast< int >( i );

	for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
		params[ id ] = InfoOf( id ).defaultValue;
	//The Over looks straight down at the clip's reflection across the whole
	//pool (a Helmholtz pair); the source from the photographer's angle at the
	//patch of spikes one coil makes.
	if( isEffect )
	{
		params[ PT_TILT ]   = 0.0f;
		params[ PT_MAGNET ] = static_cast< float >( Magnet::Uniform );
	}

	for( unsigned int host = 0; host < hostOrder.size(); ++host )
	{
		const unsigned int id = hostOrder[ host ];
		if( id >= PT_ABOUT_TEXT )
			break;
		const ControlInfo& info = InfoOf( id );
		switch( info.kind )
		{
		case Kind::Standard: SetParamInfo( host, info.name, FF_TYPE_STANDARD, params[ id ] ); break;
		case Kind::Option:
			SetOptionParamInfo( host, info.name, static_cast< unsigned int >( info.optionCount ), params[ id ] );
			for( int i = 0; i < info.optionCount; ++i )
				SetParamElementInfo( host, static_cast< unsigned int >( i ), info.options[ i ], static_cast< float >( i ) );
			break;
		case Kind::Event: SetParamInfo( host, info.name, FF_TYPE_EVENT, false ); break;
		case Kind::Buffer:
			//An FFT buffer: Resolume shows it as an audio-source picker and
			//writes the spectrum into it every frame. With nothing routed the
			//band levels are 0 and Onset never fires.
			SetBufferParamInfo( host, info.name, audio::kBins, FF_USAGE_FFT );
			for( int i = 0; i < audio::kBins; ++i )
				SetParamElementInfo( host, static_cast< unsigned int >( i ), "", 0.0f );
			break;
		}
		SetParamGroup( host, info.group );
	}

	// The About block, last. Inline rather than through a helper: SetParamInfo
	// is protected on CFFGLPlugin, so nothing outside the class can call it.
	const unsigned int aboutHost = static_cast< unsigned int >( idToHost[ PT_ABOUT_TEXT ] );
	SetParamInfo( aboutHost, "About", FF_TYPE_TEXT, stoatworks::about::defaultText() );
	{
		FFUInt32 aboutId = aboutHost + 1;
		for( const auto& b : stoatworks::about::buttons() )
			SetParamInfo( aboutId++, b.label, FF_TYPE_EVENT, false );
	}
	for( FFUInt32 i = aboutHost; i < hostOrder.size(); ++i )
		SetParamGroup( i, "About" );
}

//---------------------------------------------------------------------------
GLint FerrofluidPlugin::where( const FFGLShader& shader, const char* name )
{
	//Keyed by the literal's address: every name here is a string literal.
	const auto key = std::make_pair( shader.GetGLID(), name );
	for( const auto& entry : uniformCache )
		if( entry.first == key )
			return entry.second;
	const GLint location = glGetUniformLocation( shader.GetGLID(), name );
	uniformCache.emplace_back( key, location );
	return location;
}
void FerrofluidPlugin::uniform( const FFGLShader& shader, const char* name, int v )
{
	glUniform1i( where( shader, name ), v );
}
void FerrofluidPlugin::uniform( const FFGLShader& shader, const char* name, float v )
{
	glUniform1f( where( shader, name ), v );
}
void FerrofluidPlugin::uniform( const FFGLShader& shader, const char* name, float a, float b )
{
	glUniform2f( where( shader, name ), a, b );
}
void FerrofluidPlugin::uniform( const FFGLShader& shader, const char* name, float a, float b, float c )
{
	glUniform3f( where( shader, name ), a, b, c );
}

//---------------------------------------------------------------------------
FFResult FerrofluidPlugin::InitGL( const FFGLViewportStruct* vp )
{
	diag::init();
	diag::info( std::string( "GL vendor=" ) + glStringOrUnknown( GL_VENDOR ) + " renderer=" + glStringOrUnknown( GL_RENDERER )
	            + " version=" + glStringOrUnknown( GL_VERSION ) + ( isEffect ? " (Over)" : " (source)" ) );

	const std::string vertex = std::string( shaders::kVersion ) + shaders::kQuadVertex;
	struct Stage
	{
		FFGLShader* shader;
		shaders::Pass pass;
		const char* name;
	};
	const Stage stages[] = {
		{ &fftShader, shaders::Pass::FFT, "fft" },
		{ &evolveShader, shaders::Pass::Evolve, "evolve" },
		{ &forceShader, shaders::Pass::Force, "force" },
		{ &packShader, shaders::Pass::Pack, "pack" },
		{ &boundsShader, shaders::Pass::Bounds, "bounds" },
		{ &renderShader, shaders::Pass::Render, "render" },
	};
	for( const Stage& stage : stages )
	{
		const std::string fragment = shaders::Assemble( stage.pass );
		if( stage.shader->Compile( vertex.c_str(), fragment.c_str() ) )
			continue;
		//Returning FF_FAIL is invisible to the operator: the plugin simply
		//does nothing in Resolume. These lines are the only record of which
		//pass it was.
		diag::error( std::string( "the " ) + stage.name + " shader failed to compile - the plugin will do nothing" );
		FFGLLog::LogToHost( "Ferrofluid: shader failed to compile" );
		DeInitGL();
		return FF_FAIL;
	}

	if( !quad.Initialise() )
	{
		diag::error( "quad geometry failed to initialise" );
		DeInitGL();
		return FF_FAIL;
	}
	glGenFramebuffers( 1, &mrtFBO );

	diag::info( "initialised" );
	return CFFGLPlugin::InitGL( vp );
}

//---------------------------------------------------------------------------
const physics::Datasheet& FerrofluidPlugin::Fluid() const
{
	return physics::FluidOf( static_cast< physics::FluidId >( OptionIndex( params[ PT_FLUID ], 3 ) ) );
}

physics::Nonlinear FerrofluidPlugin::Model() const
{
	physics::Nonlinear model;
	model.hysteresis = HysteresisFromParam( params[ PT_HYSTERESIS ] );
	model.saturation = SaturationFromParam( params[ PT_SATURATION ] );
	return model;
}

Magnet FerrofluidPlugin::CurrentMagnet() const
{
	return static_cast< Magnet >( OptionIndex( params[ PT_MAGNET ], static_cast< int >( Magnet::Count ) ) );
}

double FerrofluidPlugin::LensTanHalf() const
{
	return kLensTanHalf;
}

//---------------------------------------------------------------------------
bool FerrofluidPlugin::ensurePool( const Pool& want )
{
	//All of it up front, before anything is bound: every ffglex::Scoped*
	//binding clears to 0 on exit rather than restoring, and
	//FFGLFBO::Initialise binds under one.
	if( want != pool )
	{
		for( PassBuffer& b : state )
			b.Destroy();
		for( PassBuffer& b : work )
			b.Destroy();
		for( PassBuffer& b : surfaceWork )
			b.Destroy();
		pack.Destroy();
		force.Destroy();
		for( PassBuffer& b : bounds )
			b.Destroy();
		bounds.clear();
		for( GLuint* tw : { &twiddleX, &twiddleY, &cameraTwiddleX, &cameraTwiddleY } )
			if( *tw != 0 )
				glDeleteTextures( 1, tw );
		twiddleX       = makeTwiddles( want.nx );
		twiddleY       = makeTwiddles( want.ny );
		cameraTwiddleX = makeTwiddles( 2 * want.nx );
		cameraTwiddleY = makeTwiddles( 2 * want.ny );
		stateIndex   = 0;
		surfaceIndex = 0;
		packValid    = false;
		//The bounds chain: 8 x 8 blocks of the camera's surface down to one texel.
		int w = 2 * want.nx, h = 2 * want.ny;
		do
		{
			w = std::max( 1, ( w + 7 ) / 8 );
			h = std::max( 1, ( h + 7 ) / 8 );
			bounds.emplace_back();
		} while( w > 1 || h > 1 );
	}

	bool ok = true;
	for( PassBuffer& b : state )
		ok = ok && b.Ensure( want.nx, want.ny, GL_RGBA32F, PassBuffer::Sampling::Nearest );
	for( PassBuffer& b : work )
		ok = ok && b.Ensure( want.nx, want.ny, GL_RG32F, PassBuffer::Sampling::Nearest );
	ok = ok && pack.Ensure( want.nx, want.ny, GL_RG32F, PassBuffer::Sampling::Nearest );
	ok = ok && force.Ensure( want.nx, want.ny, GL_RG32F, PassBuffer::Sampling::Nearest );
	//The surface the camera reads between texels, at twice the pool's grid:
	//the spectrum zero-padded, which is the band-limited surface sampled
	//finer, not a smoothing of it. A spike is only fifteen of the pool's
	//cells round, and a gradient interpolated bilinearly between so few
	//dips between them -- which a mirror shows as flutes down every flank.
	//The pool wraps.
	for( PassBuffer& b : surfaceWork )
		ok = ok && b.Ensure( 2 * want.nx, 2 * want.ny, GL_RGBA32F, PassBuffer::Sampling::Linear, PassBuffer::Wrap::Repeat );
	int w = 2 * want.nx, h = 2 * want.ny;
	for( PassBuffer& b : bounds )
	{
		w  = std::max( 1, ( w + 7 ) / 8 );
		h  = std::max( 1, ( h + 7 ) / 8 );
		ok = ok && b.Ensure( w, h, GL_RGBA32F, PassBuffer::Sampling::Nearest );
	}
	if( !ok || twiddleX == 0 || twiddleY == 0 || cameraTwiddleX == 0 || cameraTwiddleY == 0 )
		return false;
	pool = want;
	return true;
}

void FerrofluidPlugin::clearState()
{
	for( PassBuffer& b : state )
		b.Clear();
	packValid = false;
}

//---------------------------------------------------------------------------
double FerrofluidPlugin::stressAt( double applied ) const
{
	return physics::Magnetise( Fluid(), applied, physicsHooks ).g;
}

void FerrofluidPlugin::updateTables()
{
	Key key;
	key.fluid        = OptionIndex( params[ PT_FLUID ], 3 );
	key.magnet       = static_cast< int >( CurrentMagnet() );
	key.coilSize     = params[ PT_COIL_SIZE ];
	key.spread       = CurrentMagnet() == Magnet::ThreeCoils ? params[ PT_COIL_SPREAD ] : -1.0f;
	key.physicsHooks = physicsHooks;
	key.coilHooks    = coilHooks;
	key.lx           = pool.lx;
	if( key == tablesFor && coilTable != 0 && fluidTable != 0 )
		return;
	tablesFor = key;

	const physics::Datasheet& fluid = Fluid();
	criticalApplied = physics::CriticalApplied( fluid, physicsHooks );

	//The coil's surface field per amp, from its axis to past the pool's far
	//corner from any coil the Spread can place.
	const double radius = CoilSizeFromParam( params[ PT_COIL_SIZE ] );
	rhoMax              = 0.5 * std::sqrt( pool.lx * pool.lx + pool.ly * pool.ly ) + SpreadFromParam( 1.0f );
	coilTableData.assign( shaders::kTableSize, 0.0f );
	coil::SolenoidProfile( radius, rhoMax, shaders::kTableSize, coilTableData.data(), coilHooks );

	//The critical current: what puts the pool's strongest point at onset. For
	//three coils, all three at the same current: the strongest point of their
	//summed field, found on a fine grid round the triangle.
	if( CurrentMagnet() == Magnet::Uniform )
		centreField = coil::HelmholtzField();
	else if( CurrentMagnet() == Magnet::Coil )
		centreField = coil::SolenoidAxial( radius, 0.0 );
	else
	{
		const double side = SpreadFromParam( params[ PT_COIL_SPREAD ] ), R = side / std::sqrt( 3.0 );
		const double cx[ 3 ] = { 0.0, -0.5 * side, 0.5 * side }, cy[ 3 ] = { R, -0.5 * R, -0.5 * R };
		const double reach   = R + 2.0 * radius;
		centreField          = 0.0;
		for( int iy = -40; iy <= 40; ++iy )
			for( int ix = -40; ix <= 40; ++ix )
			{
				const double px = reach * ix / 40.0, py = reach * iy / 40.0;
				double field    = 0.0;
				for( int j = 0; j < 3; ++j )
				{
					const double r = std::hypot( px - cx[ j ], py - cy[ j ] );
					const double f = std::clamp( r / rhoMax, 0.0, 1.0 ) * ( shaders::kTableSize - 1 );
					const int i0   = std::min( static_cast< int >( f ), shaders::kTableSize - 2 );
					field += coilTableData[ i0 ] + ( f - i0 ) * ( coilTableData[ i0 + 1 ] - coilTableData[ i0 ] );
				}
				centreField = std::max( centreField, field );
			}
	}
	criticalCurrent = criticalApplied / centreField;

	//The fluid's G against the applied field, to past three coils at the
	//drive's ceiling on top of one another.
	fieldMax = 3.0 * Drive::kMaxCurrent * criticalApplied * 1.04;
	fluidTableData.assign( shaders::kTableSize, 0.0f );
	for( int i = 0; i < shaders::kTableSize; ++i )
		fluidTableData[ i ] = static_cast< float >( stressAt( fieldMax * i / ( shaders::kTableSize - 1 ) ) );

	if( coilTable != 0 )
		glDeleteTextures( 1, &coilTable );
	if( fluidTable != 0 )
		glDeleteTextures( 1, &fluidTable );
	coilTable  = makeTable( GL_R32F, GL_RED, shaders::kTableSize, coilTableData.data() );
	fluidTable = makeTable( GL_R32F, GL_RED, shaders::kTableSize, fluidTableData.data() );

	diag::info( std::string( "fluid " ) + fluid.name + ": lambda_c " + std::to_string( physics::CriticalWavelength( fluid ) * 1e3 )
	            + " mm, H_a at onset " + std::to_string( criticalApplied ) + " A/m, critical current "
	            + std::to_string( criticalCurrent ) + " A" );
}

void FerrofluidPlugin::coilCurrents( double t, double out[ 3 ] ) const
{
	for( int j = 0; j < 3; ++j )
		out[ j ] = drive.CurrentAt( j, t ) * criticalCurrent;
}

//---------------------------------------------------------------------------
// The substep plan. The exact drift has no step limit, so the step is set by
// what is explicit: the stress's stiffness and, with a coil, the local field's
// residual. A kick-then-drift step is stable for a mode while the trace of its
// map stays inside +-2 wherever the mode itself is stable; the plan scans the
// band the stress reaches for the longest step that keeps every one so.
//---------------------------------------------------------------------------
Plan FerrofluidPlugin::planSubsteps( double frameDt )
{
	Plan out;
	if( frameDt <= 0.0 )
		return out;

	const physics::Datasheet& fluid = Fluid();
	const double rho = fluid.density, g = physics::kGravity;
	const double kc  = physics::CriticalWavenumber( fluid );
	const Magnet magnet = CurrentMagnet();
	const physics::Nonlinear model = Model();

	//The strongest field this frame: the currents' peaks (the lag is monotone
	//between pulse edges, so the peaks are at the edges or the ends).
	std::vector< double > times = { drive.FrameStart(), drive.FrameEnd() };
	for( const Drive::Pulse& p : drive.Pulses() )
		for( double e : { p.start, p.end } )
			if( e > drive.FrameStart() && e < drive.FrameEnd() )
				times.push_back( e );
	double peak[ 3 ] = {};
	for( double t : times )
		for( int j = 0; j < 3; ++j )
			peak[ j ] = std::max( peak[ j ], drive.CurrentAt( j, t ) * criticalCurrent );
	//The strongest applied field on the pool: one coil's is at its centre;
	//three coils' sum is looked for over the pool, on a coarse grid of the
	//table the GPU reads (it is smooth on the scale of the coil's depth).
	double applied = 0.0;
	if( magnet == Magnet::Uniform || magnet == Magnet::Coil )
		applied = peak[ 0 ] * centreField;
	else
		for( int iy = 0; iy <= 48; ++iy )
			for( int ix = 0; ix <= 96; ++ix )
			{
				const double px = ( ix / 96.0 - 0.5 ) * pool.lx, py = ( iy / 48.0 - 0.5 ) * pool.ly;
				double field    = 0.0;
				for( int j = 0; j < 3; ++j )
				{
					const double r = std::hypot( px - centres[ 2 * j ], py - centres[ 2 * j + 1 ] );
					const double f = std::clamp( r / rhoMax, 0.0, 1.0 ) * ( shaders::kTableSize - 1 );
					const int i0   = std::min( static_cast< int >( f ), shaders::kTableSize - 2 );
					field += peak[ j ] * ( coilTableData[ i0 ] + ( f - i0 ) * ( coilTableData[ i0 + 1 ] - coilTableData[ i0 ] ) );
				}
				applied = std::max( applied, field );
			}
	out.stressMax = stressAt( applied );
	out.stressRef = magnet == Magnet::Uniform ? out.stressMax : 0.5 * out.stressMax;
	const double residual = magnet == Magnet::Uniform ? 0.0 : 0.5 * out.stressMax;

	//The stress's slope. Its energy is local in g = |grad| h, with its minimum
	//at g = Hy / Sa: the bracket's slope m ( 2 Hy g - 3 Sa g^2 ) is m Hy^2 / Sa
	//there and grows past it, so g is bounded at 1.5 Hy / Sa (and at 1, for a
	//pattern still growing). A mode at k feels ( k / rho ) ( rho g / k_c ) q^2
	//times it, q = k / k_c.
	const double mMax   = physics::MagneticNumber( fluid, out.stressMax );
	const double nyquist = physics::kPi * std::min( pool.nx / pool.lx, pool.ny / pool.ly ) / kc;
	const double bandTop = mMax > 1.0 ? mMax + std::sqrt( mMax * mMax - 1.0 ) : 1.0;
	out.outerCut         = std::min( std::max( kOuterCutMin, bandTop + 0.5 ), 0.9 * nyquist );
	const double x      = 1.2 * std::max( 1.0, model.hysteresis / std::max( model.saturation, 1e-6 ) );
	const double nlRate = g * mMax * ( 2.0 * model.hysteresis * x + 3.0 * model.saturation * x * x );

	const double cut    = std::min( kForceCut * kc, 0.67 * physics::kPi * std::min( pool.nx / pool.lx, pool.ny / pool.ly ) );
	const double qTop   = 1.3 * cut / kc;
	const double sigmaR = fluid.tension / rho;
	auto stableAt = [ & ]( double dt ) {
		for( int i = 1; i <= 400; ++i )
		{
			const double k      = qTop * kc * i / 400.0;
			const double r      = k / cut;
			const double pass   = std::exp( -std::pow( r, 8.0 ) );
			const double omega2 = g * k + sigmaR * k * k * k - out.stressRef / rho * k * k;
			const double r3     = k / ( out.outerCut * kc );
			const double outer  = std::exp( -std::pow( r3, 8.0 ) );
			const double kappa  = residual * k * k / rho * pass + nlRate * k * ( k / kc ) * ( k / kc ) * outer;
			double c, s;//cos and sin( omega dt ) / omega, or the hyperbolic pair
			if( omega2 >= 0.0 )
			{
				const double w = std::sqrt( omega2 );
				c              = std::cos( w * dt );
				s              = w > 0.0 ? std::sin( w * dt ) / w : dt;
			}
			else
			{
				const double w = std::sqrt( -omega2 );
				c              = std::cosh( w * dt );
				s              = std::sinh( w * dt ) / w;
			}
			for( double sign : { -1.0, 1.0 } )
			{
				const double k2 = sign * kappa;
				if( omega2 + k2 <= 0.0 )
					continue;//unstable for real: it should grow
				//Inside +-2 the map's eigenvalues sit on the unit circle (an
				//undamped mode's |tr| tends to 2 as dt does, which is neutral,
				//not unstable); a margin is kept only at the -2 end, where a
				//stiff mode turns over.
				const double trace = 2.0 * c - k2 * dt * s;
				if( trace > 2.0 + 1e-12 || trace < -1.9 )
					return false;
			}
		}
		return true;
	};
	double dt = kMaxSubstep;
	while( dt > 1e-5 && !stableAt( dt ) )
		dt *= 0.85;

	//With a coil the growth beyond G_ref is in the kicks, so it must be
	//resolved: a third of an e-folding a step at the fastest mode.
	for( int i = 1; i <= 400; ++i )
	{
		const double k      = qTop * kc * i / 400.0;
		const double omega2 = g * k + sigmaR * k * k * k - out.stressMax / rho * k * k;
		if( omega2 < 0.0 )
			out.growthMax = std::max( out.growthMax, std::sqrt( -omega2 ) );
	}
	if( magnet != Magnet::Uniform && out.growthMax > 0.0 )
		dt = std::min( dt, 0.33 / out.growthMax );

	dt *= substepScale;
	int n = static_cast< int >( std::ceil( frameDt / dt - 1e-9 ) );
	n     = std::max( n, 1 );
	if( n > kMaxSubsteps )
	{
		out.lost = frameDt - kMaxSubsteps * dt;
		n        = kMaxSubsteps;
		if( ++cappedFrames % 600 == 1 )
			diag::warn( "substep cap: the pool runs " + std::to_string( out.lost * 1e3 ) + " ms a frame behind real time" );
	}
	else
		dt = frameDt / n;
	out.substeps = n;
	out.dt       = dt;
	return out;
}

//---------------------------------------------------------------------------
void FerrofluidPlugin::transform( GLuint source, PassBuffer ( &buffers )[ 2 ], int& last, float direction, int nx, int ny,
                                  GLuint twX, GLuint twY )
{
	glUseProgram( fftShader.GetGLID() );
	uniform( fftShader, "Source", 0 );
	uniform( fftShader, "Twiddles", 1 );
	uniform( fftShader, "Direction", direction );

	//`last` names the buffer `source` is, or -1 for a texture of its own.
	int current  = last;
	GLuint input = source;
	struct Axis
	{
		int horizontal, length;
		GLuint twiddles;
	};
	const Axis axes[ 2 ] = { { 1, nx, twX }, { 0, ny, twY } };
	for( const Axis& axis : axes )
	{
		uniform( fftShader, "Horizontal", axis.horizontal );
		uniform( fftShader, "Length", axis.length );
		int stride = 1;
		for( int radix : radicesOf( axis.length ) )
		{
			const int target = current == 0 ? 1 : 0;
			glBindFramebuffer( GL_FRAMEBUFFER, buffers[ target ].GetGLID() );
			glViewport( 0, 0, nx, ny );
			discardTarget();
			bindUnit( 0, input );
			bindUnit( 1, axis.twiddles );
			uniform( fftShader, "Stride", stride );
			uniform( fftShader, "Radix", radix );
			quad.Draw();
			stride *= radix;
			current = target;
			input   = buffers[ target ].TextureID();
		}
	}
	unbindTextureUnits( 2 );
	last = current;
}

void FerrofluidPlugin::substep( double t, double dt, const Plan& p )
{
	const physics::Datasheet& fluid = Fluid();
	const Magnet magnet             = CurrentMagnet();
	const physics::Nonlinear model  = Model();
	const double kc                 = physics::CriticalWavenumber( fluid );

	double currents[ 3 ];
	coilCurrents( t, currents );
	const double uniformStress = magnet == Magnet::Uniform ? stressAt( currents[ 0 ] * centreField ) : 0.0;
	const double stressRef     = magnet == Magnet::Uniform ? uniformStress : p.stressRef;

	//1. Back to real space: ( h, |grad| h ).
	int real = -1;
	transform( pack.TextureID(), work, real, 1.0f, pool.nx, pool.ny, twiddleX, twiddleY );

	//2. The stress.
	{
		glBindFramebuffer( GL_FRAMEBUFFER, force.GetGLID() );
		glViewport( 0, 0, pool.nx, pool.ny );
		discardTarget();
		glUseProgram( forceShader.GetGLID() );
		bindUnit( 0, work[ real ].TextureID() );
		bindUnit( 1, coilTable );
		bindUnit( 2, fluidTable );
		uniform( forceShader, "Real", 0 );
		uniform( forceShader, "CoilTable", 1 );
		uniform( forceShader, "FluidTable", 2 );
		glUniform2i( where( forceShader, "GridSize" ), pool.nx, pool.ny );
		uniform( forceShader, "Domain", static_cast< float >( pool.lx ), static_cast< float >( pool.ly ) );
		uniform( forceShader, "Magnet", static_cast< int >( magnet ) );
		uniform( forceShader, "UniformStress", static_cast< float >( uniformStress ) );
		uniform( forceShader, "Currents", static_cast< float >( currents[ 0 ] ), static_cast< float >( currents[ 1 ] ),
		                 static_cast< float >( currents[ 2 ] ) );
		float c[ 6 ];
		for( int i = 0; i < 6; ++i )
			c[ i ] = static_cast< float >( centres[ i ] );
		glUniform2fv( where( forceShader, "Centres" ), 3, c );
		uniform( forceShader, "CoilCount", magnet == Magnet::ThreeCoils ? 3 : 1 );
		uniform( forceShader, "RhoMax", static_cast< float >( rhoMax ) );
		uniform( forceShader, "FieldMax", static_cast< float >( fieldMax ) );
		uniform( forceShader, "StressRef", static_cast< float >( stressRef ) );
		uniform( forceShader, "RhoG", static_cast< float >( fluid.density * physics::kGravity ) );
		uniform( forceShader, "Kc", static_cast< float >( kc ) );
		uniform( forceShader, "Hy", static_cast< float >( model.hysteresis ) );
		uniform( forceShader, "Sa", static_cast< float >( model.saturation ) );
		//Noise: Pa RMS over a square millimetre and a millisecond, so a cell
		//and a substep of other sizes see the same white pressure.
		const double cellArea = ( pool.lx / pool.nx ) * ( pool.ly / pool.ny );
		const double rms      = NoiseFromParam( params[ PT_NOISE ] ) * std::sqrt( 1e-3 / dt ) * std::sqrt( 1e-6 / cellArea );
		uniform( forceShader, "NoiseRms", static_cast< float >( rms ) );
		glUniform1ui( where( forceShader, "NoiseSalt" ), ++substepSerial * 2654435761u );
		uniform( forceShader, "Hooks", hooks );
		quad.Draw();
		unbindTextureUnits( 3 );
	}

	//3. The stress to N_k.
	int spectral = -1;
	transform( force.TextureID(), work, spectral, -1.0f, pool.nx, pool.ny, twiddleX, twiddleY );

	//4. Kick and drift, into the other state and the next pack.
	{
		const int next = 1 - stateIndex;
		glBindFramebuffer( GL_FRAMEBUFFER, mrtFBO );
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, state[ next ].TextureID(), 0 );
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, pack.TextureID(), 0 );
		const GLenum targets[ 2 ] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
		glDrawBuffers( 2, targets );
		glViewport( 0, 0, pool.nx, pool.ny );
		discardTarget();
		glUseProgram( evolveShader.GetGLID() );
		bindUnit( 0, state[ stateIndex ].TextureID() );
		bindUnit( 1, work[ spectral ].TextureID() );
		uniform( evolveShader, "State", 0 );
		uniform( evolveShader, "Force", 1 );
		glUniform2i( where( evolveShader, "GridSize" ), pool.nx, pool.ny );
		uniform( evolveShader, "Domain", static_cast< float >( pool.lx ), static_cast< float >( pool.ly ) );
		uniform( evolveShader, "Gravity", static_cast< float >( physics::kGravity ) );
		uniform( evolveShader, "TensionRho", static_cast< float >( fluid.tension / fluid.density ) );
		uniform( evolveShader, "StressRho", static_cast< float >( stressRef / fluid.density ) );
		uniform( evolveShader, "InvRho", static_cast< float >( 1.0 / fluid.density ) );
		uniform( evolveShader, "Kinematic",
		                  static_cast< float >( ViscosityFromParam( params[ PT_VISCOSITY ] ) * fluid.viscosity / fluid.density ) );
		uniform( evolveShader, "Dt", static_cast< float >( dt ) );
		const double cut = std::min( kForceCut * kc, 0.67 * physics::kPi * std::min( pool.nx / pool.lx, pool.ny / pool.ly ) );
		uniform( evolveShader, "ForceCut", static_cast< float >( cut ) );
		uniform( evolveShader, "Kc", static_cast< float >( kc ) );
		uniform( evolveShader, "OuterCut", static_cast< float >( p.outerCut * kc ) );
		uniform( evolveShader, "Norm", static_cast< float >( 1.0 / ( static_cast< double >( pool.nx ) * pool.ny ) ) );
		uniform( evolveShader, "Hooks", hooks );
		quad.Draw();
		unbindTextureUnits( 2 );
		stateIndex = next;
	}
	packValid = true;
}

void FerrofluidPlugin::repack()
{
	//The evolve at dt = 0 with no kick: the state copied through unchanged and
	//its pack written beside it.
	const physics::Datasheet& fluid = Fluid();
	const int next                  = 1 - stateIndex;
	glBindFramebuffer( GL_FRAMEBUFFER, mrtFBO );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, state[ next ].TextureID(), 0 );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, pack.TextureID(), 0 );
	const GLenum targets[ 2 ] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
	glDrawBuffers( 2, targets );
	glViewport( 0, 0, pool.nx, pool.ny );
	glUseProgram( evolveShader.GetGLID() );
	bindUnit( 0, state[ stateIndex ].TextureID() );
	bindUnit( 1, work[ 0 ].TextureID() );
	uniform( evolveShader, "State", 0 );
	uniform( evolveShader, "Force", 1 );
	glUniform2i( where( evolveShader, "GridSize" ), pool.nx, pool.ny );
	uniform( evolveShader, "Domain", static_cast< float >( pool.lx ), static_cast< float >( pool.ly ) );
	uniform( evolveShader, "Gravity", static_cast< float >( physics::kGravity ) );
	uniform( evolveShader, "TensionRho", static_cast< float >( fluid.tension / fluid.density ) );
	uniform( evolveShader, "StressRho", 0.0f );
	uniform( evolveShader, "InvRho", static_cast< float >( 1.0 / fluid.density ) );
	uniform( evolveShader, "Kinematic", 0.0f );
	uniform( evolveShader, "Dt", 0.0f );
	uniform( evolveShader, "ForceCut", 1.0f );
	uniform( evolveShader, "Kc", 1.0f );
	uniform( evolveShader, "OuterCut", 1.0f );
	uniform( evolveShader, "Norm", static_cast< float >( 1.0 / ( static_cast< double >( pool.nx ) * pool.ny ) ) );
	uniform( evolveShader, "Hooks", hooks | shaders::kHookNoKick );
	quad.Draw();
	unbindTextureUnits( 2 );
	stateIndex = next;
	packValid  = true;
}

void FerrofluidPlugin::surfaceAndBounds()
{
	//The camera's surface: ( h, 0, h_x, h_y ), the spectrum zero-padded to
	//twice the grid.
	const int cx = 2 * pool.nx, cy = 2 * pool.ny;
	glBindFramebuffer( GL_FRAMEBUFFER, surfaceWork[ 0 ].GetGLID() );
	glViewport( 0, 0, cx, cy );
	glUseProgram( packShader.GetGLID() );
	bindUnit( 0, state[ stateIndex ].TextureID() );
	uniform( packShader, "State", 0 );
	glUniform2i( where( packShader, "GridSize" ), pool.nx, pool.ny );
	glUniform2i( where( packShader, "OutSize" ), cx, cy );
	uniform( packShader, "Domain", static_cast< float >( pool.lx ), static_cast< float >( pool.ly ) );
	uniform( packShader, "Norm", static_cast< float >( 1.0 / ( static_cast< double >( pool.nx ) * pool.ny ) ) );
	quad.Draw();
	unbindTextureUnits( 1 );

	int last = 0;
	transform( surfaceWork[ 0 ].TextureID(), surfaceWork, last, 1.0f, cx, cy, cameraTwiddleX, cameraTwiddleY );
	surfaceIndex = last;

	//The march's slab and slope bound, by blocks down to one texel.
	glUseProgram( boundsShader.GetGLID() );
	uniform( boundsShader, "Source", 0 );
	uniform( boundsShader, "Cell", static_cast< float >( pool.lx / cx ), static_cast< float >( pool.ly / cy ) );
	GLuint source = surfaceWork[ surfaceIndex ].TextureID();
	int w = cx, h = cy;
	for( size_t i = 0; i < bounds.size(); ++i )
	{
		const int ow = static_cast< int >( bounds[ i ].GetWidth() ), oh = static_cast< int >( bounds[ i ].GetHeight() );
		glBindFramebuffer( GL_FRAMEBUFFER, bounds[ i ].GetGLID() );
		glViewport( 0, 0, ow, oh );
		bindUnit( 0, source );
		glUniform2i( where( boundsShader, "SourceSize" ), w, h );
		uniform( boundsShader, "First", i == 0 ? 1 : 0 );
		quad.Draw();
		source = bounds[ i ].TextureID();
		w      = ow;
		h      = oh;
	}
	unbindTextureUnits( 1 );
}

//---------------------------------------------------------------------------
void FerrofluidPlugin::render( const FFGLTextureStruct* input, const GLint* hostViewport, GLuint hostFBO, int width,
                               int height )
{
	const physics::Datasheet& fluid = Fluid();
	const physics::Optics optics    = physics::OpticsOf( fluid );

	//The camera: a 100 mm lens whose frame is Field Width across at the
	//pool, orbiting the pool's centre by Tilt (towards the viewer's side, so
	//the far side of the pool is the top of the frame).
	frameWidth          = FieldWidthFromParam( params[ PT_FIELD_WIDTH ] );
	frameHeight         = frameWidth * static_cast< double >( height ) / static_cast< double >( width );
	const double tilt   = TiltFromParam( params[ PT_TILT ] );
	const double D      = 0.5 * frameWidth / kLensTanHalf;
	eye                 = { 0.0, -D * std::sin( tilt ), D * std::cos( tilt ) };
	const double fwd[ 3 ] = { 0.0, std::sin( tilt ), -std::cos( tilt ) };
	const double up[ 3 ]  = { 0.0, std::cos( tilt ), std::sin( tilt ) };

	const double reference = ( hooks & shaders::kHookFresnelOne ) != 0 ? 1.0 : physics::NormalReflectance( optics );
	exposureGain           = std::pow( 2.0, ExposureFromParam( params[ PT_EXPOSURE ] ) ) / reference;

	glBindFramebuffer( GL_FRAMEBUFFER, hostFBO );
	glViewport( hostViewport[ 0 ], hostViewport[ 1 ], hostViewport[ 2 ], hostViewport[ 3 ] );
	glUseProgram( renderShader.GetGLID() );

	bindUnit( 0, surfaceWork[ surfaceIndex ].TextureID() );
	bindUnit( 1, bounds.back().TextureID() );
	//Always a real texture on the clip's unit, even in the source: a sampler on
	//texture 0 is "unloadable" to Apple's GL (boreal's trap).
	bindUnit( 2, input != nullptr ? input->Handle : coilTable );
	uniform( renderShader, "Surface", 0 );
	uniform( renderShader, "Bounds", 1 );
	uniform( renderShader, "InputTexture", 2 );
	if( input != nullptr )
	{
		const FFGLTexCoords maxCoords = GetMaxGLTexCoords( *input );
		uniform( renderShader, "MaxUV", maxCoords.s, maxCoords.t );
	}
	else
		uniform( renderShader, "MaxUV", 1.0f, 1.0f );
	uniform( renderShader, "IsOver", isEffect ? 1 : 0 );
	glUniform4i( where( renderShader, "Viewport" ), hostViewport[ 0 ], hostViewport[ 1 ], width, height );
	uniform( renderShader, "Domain", static_cast< float >( pool.lx ), static_cast< float >( pool.ly ) );
	uniform( renderShader, "Cell", static_cast< float >( pool.lx / ( 2 * pool.nx ) ), static_cast< float >( pool.ly / ( 2 * pool.ny ) ) );
	uniform( renderShader, "Eye", static_cast< float >( eye[ 0 ] ), static_cast< float >( eye[ 1 ] ), static_cast< float >( eye[ 2 ] ) );
	uniform( renderShader, "Forward", static_cast< float >( fwd[ 0 ] ), static_cast< float >( fwd[ 1 ] ), static_cast< float >( fwd[ 2 ] ) );
	uniform( renderShader, "Right", 1.0f, 0.0f, 0.0f );
	uniform( renderShader, "Up", static_cast< float >( up[ 0 ] ), static_cast< float >( up[ 1 ] ), static_cast< float >( up[ 2 ] ) );
	uniform( renderShader, "TanHalf", static_cast< float >( kLensTanHalf ),
	                  static_cast< float >( kLensTanHalf * static_cast< double >( height ) / width ) );
	uniform( renderShader, "Optical", static_cast< float >( optics.n ), static_cast< float >( optics.k ) );
	uniform( renderShader, "Gain", static_cast< float >( exposureGain ) );

	const double screen = HeightFromParam( params[ PT_HEIGHT ] );
	uniform( renderShader, "ScreenHeight", static_cast< float >( screen ) );
	uniform( renderShader, "ScreenScale", static_cast< float >( D / ( ( D + screen ) * frameWidth ) ),
	                  static_cast< float >( D / ( ( D + screen ) * frameHeight ) ) );

	//The studio: soft boxes in units of the field width, facing the pool.
	const auto lights = static_cast< Lights >( OptionIndex( params[ PT_LIGHTS ], static_cast< int >( Lights::Count ) ) );
	std::vector< Box > boxes;
	//Where they stand matters more than anything else in the look. A flat pool
	//seen from Tilt mirrors the sky beyond the pool's centre, so a light placed
	//there turns the whole pool into a white sheet. A spike's steep near flank
	//mirrors the low horizon behind the photographer, and its side flanks the
	//horizon to either side: that is where a ferrofluid photographer puts the
	//lights, and why every spike carries a vertical stripe of each one.
	switch( lights )
	{
	case Lights::SoftBox: boxes = { { 0.0, -3.0, 1.0, 2.2, 1.1, 1.0f, 1.0f, 1.0f } }; break;
	case Lights::TwoBoxes:
		boxes = { { -2.8, -1.2, 0.5, 0.9, 0.7, 1.0f, 0.98f, 0.95f }, { 2.8, -0.6, 0.4, 0.8, 0.6, 0.3f, 0.31f, 0.34f } };
		break;
	case Lights::Strips:
		boxes = { { -1.4, -2.8, 0.6, 0.16, 0.9, 1.0f, 1.0f, 1.0f },
			      { 0.0, -3.0, 0.6, 0.16, 0.9, 1.0f, 1.0f, 1.0f },
			      { 1.4, -2.8, 0.6, 0.16, 0.9, 1.0f, 1.0f, 1.0f } };
		break;
	case Lights::Backlight:
		//A wide low panel beyond the pool: every spike's outline, seen at
		//grazing incidence where the fluid reflects nearly everything, carries
		//it as a bright rim (the classic ferrofluid photograph).
		boxes = { { 0.0, 3.2, 0.5, 3.0, 0.5, 1.0f, 1.0f, 1.0f } };
		break;
	case Lights::Window:
		for( int i = 0; i < 4; ++i )
			boxes.push_back( { -2.4 + ( i % 2 ) * 0.62, -1.8, 0.7 + ( i / 2 ) * 0.62, 0.28, 0.28, 1.0f, 0.82f, 0.6f } );
		break;
	case Lights::Neon:
		boxes = { { -2.4, -1.2, 0.6, 0.08, 0.7, 1.0f, 0.12f, 0.75f }, { 2.4, -1.2, 0.6, 0.08, 0.7, 0.1f, 0.85f, 1.0f } };
		break;
	default: break;
	}
	float centre[ 12 ] = {}, uvec[ 12 ] = {}, vvec[ 12 ] = {}, radiance[ 12 ] = {};
	const int count = std::min( static_cast< int >( boxes.size() ), shaders::kMaxBoxes );
	for( int i = 0; i < count; ++i )
	{
		const Box& b = boxes[ i ];
		double c[ 3 ] = { b.x * frameWidth, b.y * frameWidth, b.z * frameWidth };
		double n[ 3 ] = { -c[ 0 ], -c[ 1 ], -c[ 2 ] };
		unitVector( n );
		//A horizontal edge, square to the box's facing; then the other edge.
		double u[ 3 ] = { n[ 1 ], -n[ 0 ], 0.0 };
		if( std::fabs( u[ 0 ] ) + std::fabs( u[ 1 ] ) < 1e-9 )
			u[ 0 ] = 1.0;
		unitVector( u );
		double v[ 3 ] = { n[ 1 ] * u[ 2 ] - n[ 2 ] * u[ 1 ], n[ 2 ] * u[ 0 ] - n[ 0 ] * u[ 2 ], n[ 0 ] * u[ 1 ] - n[ 1 ] * u[ 0 ] };
		for( int k = 0; k < 3; ++k )
		{
			centre[ i * 3 + k ] = static_cast< float >( c[ k ] );
			uvec[ i * 3 + k ]   = static_cast< float >( u[ k ] * b.halfW * frameWidth );
			vvec[ i * 3 + k ]   = static_cast< float >( v[ k ] * b.halfH * frameWidth );
		}
		radiance[ i * 3 + 0 ] = b.r;
		radiance[ i * 3 + 1 ] = b.g;
		radiance[ i * 3 + 2 ] = b.b;
	}
	uniform( renderShader, "BoxCount", isEffect ? 0 : count );
	glUniform3fv( where( renderShader, "BoxCentre" ), shaders::kMaxBoxes, centre );
	glUniform3fv( where( renderShader, "BoxU" ), shaders::kMaxBoxes, uvec );
	glUniform3fv( where( renderShader, "BoxV" ), shaders::kMaxBoxes, vvec );
	glUniform3fv( where( renderShader, "BoxRadiance" ), shaders::kMaxBoxes, radiance );
	const bool studio = static_cast< Backdrop >( OptionIndex( params[ PT_BACKDROP ], 2 ) ) == Backdrop::Studio;
	uniform( renderShader, "Backdrop", studio ? 0.06f : 0.0f, studio ? 0.06f : 0.0f, studio ? 0.065f : 0.0f );
	uniform( renderShader, "MixAmount", isEffect ? std::clamp( params[ PT_MIX ], 0.0f, 1.0f ) : 1.0f );
	uniform( renderShader, "Hooks", hooks );
	quad.Draw();
	unbindTextureUnits( 3 );
	glActiveTexture( GL_TEXTURE0 );
	glUseProgram( 0 );
}

//---------------------------------------------------------------------------
void FerrofluidPlugin::UpdateClock()
{
	const double wallNow = wallSeconds();
	if( wallStart < 0.0 )
		wallStart = wallNow;
	const double raw = hostTime;

	//Resolume has been seen sending both seconds and milliseconds through
	//SetTime, and nothing in the call says which. Vote on it against the wall
	//clock for a few frames, then stop asking (rosette's, by way of millpond).
	if( clockScale == 0.0 && raw >= 0.0 && lastRawTime >= 0.0 && lastWallTime >= 0.0 )
	{
		const double hostDelta = raw - lastRawTime;
		const double wallDelta = wallNow - lastWallTime;
		if( hostDelta > 0.0 && wallDelta >= 0.0005 )
		{
			const double ratio = hostDelta / wallDelta;
			if( ratio > 0.1 && ratio < 10.0 )
				++secondsVotes;
			else if( ratio > 100.0 && ratio < 10000.0 )
				++millisVotes;
			if( secondsVotes >= kClockVotes || millisVotes >= kClockVotes )
			{
				clockScale  = millisVotes > secondsVotes ? 0.001 : 1.0;
				settledJump = true;//wall time to the host's own: no dt across it
			}
		}
	}
	if( raw >= 0.0 )
		lastRawTime = raw;
	lastWallTime = wallNow;

	//Double throughout: Resolume's clock has been measured at ~499 million ms,
	//where a float resolves 0.03 s.
	now = ( raw >= 0.0 && clockScale != 0.0 ) ? raw * clockScale : wallNow - wallStart;
	if( ++clockFrames == 60 )
		diag::info( "host clock at frame 60: raw=" + std::to_string( raw ) + " scale=" + std::to_string( clockScale )
		            + " seconds=" + std::to_string( now ) );
}

void FerrofluidPlugin::UpdateAudio( double dt )
{
	float bins[ audio::kBins ] = {};
	int binCount               = 0;
	const int host             = idToHost[ PT_AUDIO ];
	if( const ParamInfo* info = FindParamInfo( static_cast< FFUInt32 >( host ) ) )
	{
		binCount = static_cast< int >( std::min< size_t >( info->elements.size(), audio::kBins ) );
		for( int i = 0; i < binCount; ++i )
			bins[ i ] = info->elements[ static_cast< size_t >( i ) ].value;
	}
	audio::Settings settings;
	settings.sensitivity = static_cast< float >( SensitivityFromThreshold( params[ PT_THRESHOLD ] ) );
	analyser.SetUnprimedForTest( unprimed );
	analyser.Update( bins, binCount, static_cast< float >( dt ), settings );
}

//---------------------------------------------------------------------------
FFResult FerrofluidPlugin::ProcessOpenGL( ProcessOpenGLStruct* pgl )
{
	if( pgl == nullptr )
		return FF_FAIL;
	const FFGLTextureStruct* input = nullptr;
	if( isEffect )
	{
		if( pgl->numInputTextures < 1 || pgl->inputTextures[ 0 ] == nullptr )
			return FF_FAIL;
		input = pgl->inputTextures[ 0 ];
	}

	//The host's state, put back however this returns.
	ScopedGLState restore;
	const GLint* hostViewport = restore.saved.viewport;
	glDisable( GL_BLEND );

	const int width  = input != nullptr ? static_cast< int >( input->Width ) : hostViewport[ 2 ];
	const int height = input != nullptr ? static_cast< int >( input->Height ) : hostViewport[ 3 ];
	if( width <= 0 || height <= 0 )
		return FF_FAIL;

	//---------------------------------------------------------------------
	// Time, the audio and the beat.
	//---------------------------------------------------------------------
	UpdateClock();
	if( settledJump )
	{
		lastNow     = -1.0;
		settledJump = false;
	}
	const double dt = lastNow >= 0.0 ? std::clamp( now - lastNow, 0.0, kMaxFrameDelta ) : 0.0;
	lastNow         = now;
	UpdateAudio( dt );
	if( barPhase != lastBarPhase && lastBarPhase >= 0.0f )
		hostBeatSeen = true;
	lastBarPhase = barPhase;

	//---------------------------------------------------------------------
	// The pool. A new fluid is a new pool: it starts flat.
	//---------------------------------------------------------------------
	const int fluidIndex = OptionIndex( params[ PT_FLUID ], 3 );
	Pool want            = poolOverride;
	if( want.nx <= 0 )
	{
		const double cell = physics::CriticalWavelength( Fluid() ) / kCellsPerWavelength;
		want              = Pool { kPoolX, kPoolY, kPoolX * cell, kPoolY * cell };
	}
	const bool fresh = want != pool || fluidIndex != poolFluid;
	if( !ensurePool( want ) )
	{
		diag::error( "could not allocate the pool's buffers" );
		return FF_FAIL;
	}
	if( fresh )
	{
		clearState();
		poolFluid = fluidIndex;
	}
	if( resizeClears && lastWidth > 0 && ( width != lastWidth || height != lastHeight ) )
		clearState();
	lastWidth  = width;
	lastHeight = height;
	if( settleWanted )
	{
		clearState();
		settleWanted = false;
	}
	if( loadPending )
	{
		if( pendingState.size() == static_cast< size_t >( pool.nx ) * pool.ny * 4 )
		{
			glBindTexture( GL_TEXTURE_2D, state[ stateIndex ].TextureID() );
			glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, pool.nx, pool.ny, GL_RGBA, GL_FLOAT, pendingState.data() );
			glBindTexture( GL_TEXTURE_2D, 0 );
			packValid = false;
		}
		loadPending = false;
	}
	updateTables();

	//The coils' places: one in the middle, or three on a triangle of side
	//Coil Spread round it (Low at the far side, Mid left, High right).
	const Magnet magnet = CurrentMagnet();
	{
		const double side = SpreadFromParam( params[ PT_COIL_SPREAD ] );
		const double R    = side / std::sqrt( 3.0 );
		if( magnet == Magnet::ThreeCoils )
			centres = { 0.0, R, -0.5 * side, -0.5 * R, 0.5 * side, -0.5 * R };
		else
			centres = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
	}

	//---------------------------------------------------------------------
	// The drive.
	//---------------------------------------------------------------------
	DriveInputs in;
	in.fireOn   = static_cast< FireOn >( OptionIndex( params[ PT_FIRE_ON ], static_cast< int >( FireOn::Count ) ) );
	in.presses  = firePresses;
	firePresses = 0;
	in.onset    = analyser.Fired();
	in.kick     = analyser.Kick();
	in.coils    = magnet == Magnet::ThreeCoils ? 3 : 1;
	if( magnet == Magnet::ThreeCoils )
		for( int j = 0; j < 3; ++j )
			in.level[ j ] = analyser.BandLevel( j );
	else
		in.level[ 0 ] = analyser.BandLevel( OptionIndex( params[ PT_BAND ], static_cast< int >( Band::Count ) ) );
	in.bias     = CurrentFromParam( params[ PT_BIAS ] );
	in.gain     = CurrentFromParam( params[ PT_GAIN ] );
	in.pulse    = PulseFromParam( params[ PT_PULSE ] );
	in.tau      = LagFromParam( params[ PT_COIL_LAG ] );
	in.bpm      = bpm;
	in.barPhase = barPhase;
	in.hostBeat = hostBeatSeen;
	drive.Advance( now, dt, in );

	//---------------------------------------------------------------------
	// The surface: the substeps, then the camera's copy of it.
	//---------------------------------------------------------------------
	plan = planSubsteps( dt );
	if( !packValid )
		repack();
	for( int s = 0; s < plan.substeps; ++s )
		substep( drive.FrameStart() + ( s + 0.5 ) * plan.dt, plan.dt, plan );
	fluidTime += plan.substeps * plan.dt;
	surfaceAndBounds();

	//---------------------------------------------------------------------
	// The picture, into the host's framebuffer and viewport.
	//---------------------------------------------------------------------
	render( input, hostViewport, pgl->HostFBO, width, height );
	return FF_SUCCESS;
}

std::vector< float > FerrofluidPlugin::TransformForTest( const std::vector< float >& rgba, float direction )
{
	std::vector< float > out;
	if( pool.nx <= 0 || rgba.size() != static_cast< size_t >( pool.nx ) * pool.ny * 4 )
		return out;
	ScopedGLState restore;
	for( PassBuffer& b : testWork )
		if( !b.Ensure( pool.nx, pool.ny, GL_RGBA32F, PassBuffer::Sampling::Nearest ) )
			return out;
	glBindTexture( GL_TEXTURE_2D, testWork[ 0 ].TextureID() );
	glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, pool.nx, pool.ny, GL_RGBA, GL_FLOAT, rgba.data() );
	glBindTexture( GL_TEXTURE_2D, 0 );
	int last = 0;
	transform( testWork[ 0 ].TextureID(), testWork, last, direction, pool.nx, pool.ny, twiddleX, twiddleY );
	out.resize( rgba.size() );
	glBindTexture( GL_TEXTURE_2D, testWork[ last ].TextureID() );
	glPixelStorei( GL_PACK_ALIGNMENT, 1 );
	glGetTexImage( GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, out.data() );
	glBindTexture( GL_TEXTURE_2D, 0 );
	glUseProgram( 0 );
	return out;
}

//---------------------------------------------------------------------------
FFResult FerrofluidPlugin::DeInitGL()
{
	uniformCache.clear();
	fftShader.FreeGLResources();
	evolveShader.FreeGLResources();
	forceShader.FreeGLResources();
	packShader.FreeGLResources();
	boundsShader.FreeGLResources();
	renderShader.FreeGLResources();
	quad.Release();
	for( PassBuffer& b : state )
		b.Destroy();
	for( PassBuffer& b : work )
		b.Destroy();
	for( PassBuffer& b : surfaceWork )
		b.Destroy();
	for( PassBuffer& b : testWork )
		b.Destroy();
	pack.Destroy();
	force.Destroy();
	for( PassBuffer& b : bounds )
		b.Destroy();
	bounds.clear();
	for( GLuint* t : { &twiddleX, &twiddleY, &cameraTwiddleX, &cameraTwiddleY, &coilTable, &fluidTable } )
	{
		if( *t != 0 )
			glDeleteTextures( 1, t );
		*t = 0;
	}
	if( mrtFBO != 0 )
		glDeleteFramebuffers( 1, &mrtFBO );
	mrtFBO    = 0;
	pool      = Pool {};
	poolFluid = -1;
	tablesFor = Key {};
	packValid = false;
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult FerrofluidPlugin::SetTime( double time )
{
	hostTime = time;
	return FF_SUCCESS;
}

void FerrofluidPlugin::SetBeatInfo( float newBpm, float newBarPhase )
{
	CFFGLPlugin::SetBeatInfo( newBpm, newBarPhase );
	if( newBpm > 0.0f && std::isfinite( newBpm ) )
		bpm = newBpm;
	if( std::isfinite( newBarPhase ) )
		barPhase = newBarPhase - std::floor( newBarPhase );
}

char* FerrofluidPlugin::GetTextParameter( unsigned int index )
{
	if( index < hostOrder.size() && hostOrder[ index ] == PT_ABOUT_TEXT )
	{
		static const std::string text = stoatworks::about::textParam( 0 );
		return const_cast< char* >( text.c_str() );
	}
	return CFFGLPlugin::GetTextParameter( index );
}

FFResult FerrofluidPlugin::SetTextParameter( unsigned int index, const char* value )
{
	if( index < hostOrder.size() && hostOrder[ index ] == PT_ABOUT_TEXT )
		return FF_SUCCESS;
	return CFFGLPlugin::SetTextParameter( index, value );
}

void FerrofluidPlugin::SetById( unsigned int id, float value )
{
	if( id >= PT_COUNT || idToHost[ id ] < 0 )
		return;
	SetFloatParameter( static_cast< unsigned int >( idToHost[ id ] ), value );
}

FFResult FerrofluidPlugin::SetFloatParameter( unsigned int index, float value )
{
	if( index >= hostOrder.size() )
		return FF_FAIL;
	const unsigned int id = hostOrder[ index ];

	// The About buttons open a browser and store nothing.
	if( id >= PT_ABOUT_TEXT )
		return stoatworks::about::handleParam( id - PT_ABOUT_TEXT, value ) ? FF_SUCCESS : FF_FAIL;

	//The buttons act on the press, once: an event arrives as 1 on press and 0
	//on release, and a host is free to restate either.
	const bool down = value >= 0.5f;
	if( id == PT_FIRE )
	{
		if( down && !fireHeld )
			++firePresses;
		fireHeld = down;
	}
	else if( id == PT_SETTLE )
	{
		if( down && !settleHeld )
			settleWanted = true;
		settleHeld = down;
	}
	params[ id ] = value;
	return FF_SUCCESS;
}

float FerrofluidPlugin::GetFloatParameter( unsigned int index )
{
	return index < hostOrder.size() ? params[ hostOrder[ index ] ] : 0.0f;
}

} // namespace ferrofluid
