#pragma once

#include "Audio.h"
#include "Coil.h"
#include "Controls.h"
#include "Drive.h"
#include "Fluid.h"
#include "PassBuffer.h"

#include <FFGLSDK.h>

// After FFGLSDK.h, which is where FFUInt32 comes from.
#include "StoatworksAboutParams.h"

#include <array>
#include <vector>

namespace ferrofluid
{

/// The pool: a periodic grid of the fluid's surface, in metres.
struct Pool
{
	int nx = 0, ny = 0;
	double lx = 0.0, ly = 0.0;///< metres
	bool operator==( const Pool& o ) const
	{
		return nx == o.nx && ny == o.ny && lx == o.lx && ly == o.ly;
	}
	bool operator!=( const Pool& o ) const
	{
		return !( *this == o );
	}
};

/// What one frame's substep plan came to.
struct Plan
{
	int substeps      = 0;
	double dt         = 0.0;///< seconds a substep
	double lost       = 0.0;///< seconds of the frame the cap dropped
	double stressRef  = 0.0;///< G_ref, Pa
	double stressMax  = 0.0;///< the largest G on the pool this frame, Pa
	double growthMax  = 0.0;///< the fastest linear growth rate, 1/s
};

/**
    The plugin: the source (`SW Ferrofluid`, a studio's soft boxes in the
    mirror) and, with `isEffect`, the Over (`SW Ferro Over`, the clip on a
    screen above the pool). One class, two registrations; see SourcePlugin.cpp
    and EffectPlugin.cpp.

    Host indices are NOT ParamIds: each plugin declares its own dense list,
    `HostOrder( isEffect )`. The host-facing calls translate.
*/
class FerrofluidPlugin : public CFFGLPlugin
{
public:
	explicit FerrofluidPlugin( bool isEffect );

	FFResult InitGL( const FFGLViewportStruct* viewport ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* input ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	char* GetTextParameter( unsigned int index ) override;
	/// The base class's stub fails, and a failed default deletes the instance
	/// -- so without this no real host can load the plugin.
	FFResult SetTextParameter( unsigned int index, const char* value ) override;
	FFResult SetTime( double time ) override;
	void SetBeatInfo( float bpm, float barPhase ) override;

	bool IsEffect() const
	{
		return isEffect;
	}
	int HostIndexOf( unsigned int id ) const
	{
		return id < PT_COUNT ? idToHost[ id ] : -1;
	}
	/// Set a control by what it is, whatever its host index.
	void SetById( unsigned int id, float value );
	float GetById( unsigned int id ) const
	{
		return id < PT_COUNT ? params[ id ] : 0.0f;
	}

	//-------------------------------------------------------------------
	// For the harness. Nothing in the plugin's own operation calls these.
	//-------------------------------------------------------------------
	void SetClockScaleForTest( double scale )
	{
		clockScale = scale;
	}
	/// shaders::Hook bits.
	void SetHooksForTest( int bits )
	{
		hooks = bits;
	}
	/// physics::PhysicsHook bits (the fluid's tables and the critical current).
	void SetPhysicsHooksForTest( int bits )
	{
		physicsHooks = bits;
		tablesFor    = Key {};
	}
	/// coil::CoilHook bits.
	void SetCoilHooksForTest( int bits )
	{
		coilHooks = bits;
		tablesFor = Key {};
	}
	void SetDriveHooksForTest( int bits )
	{
		drive.SetHooksForTest( bits );
	}
	/// A pool of exactly this size, whatever the fluid says (--wavelength
	/// fits the hexagonal lattice to it).
	void SetPoolForTest( const Pool& pool )
	{
		poolOverride = pool;
	}
	/// The --resize negative control: a new raster flattens the pool.
	void SetResizeClearsForTest( bool on )
	{
		resizeClears = on;
	}
	/// The --audio negative control: the analyser never primed.
	void SetUnprimedForTest( bool on )
	{
		unprimed = on;
	}
	/// The --substep check: multiply the planned substep by this.
	void SetSubstepScaleForTest( double scale )
	{
		substepScale = scale;
	}
	/// On the next frame, before any substep: the spectral state is exactly
	/// this (nx x ny x RGBA floats, ( h_k, v_k ), row 0 first).
	void LoadStateForTest( const std::vector< float >& rgba )
	{
		pendingState = rgba;
		loadPending  = true;
	}

	/// Run the shipping transform (the same shader, the same radix plan) on
	/// nx x ny RGBA floats -- two complex fields -- and read the result back.
	/// -1 forward, +1 inverse; unnormalised both ways. Needs a frame first.
	std::vector< float > TransformForTest( const std::vector< float >& rgba, float direction );

	GLuint StateTextureID() const
	{
		return state[ stateIndex ].TextureID();
	}
	/// ( h, 0, h_x, h_y ) as the camera saw it: the pool's surface interpolated
	/// spectrally to twice its grid (SurfaceWidth x SurfaceHeight), metres.
	GLuint SurfaceTextureID() const
	{
		return surfaceWork[ surfaceIndex ].TextureID();
	}
	int SurfaceWidth() const
	{
		return 2 * pool.nx;
	}
	int SurfaceHeight() const
	{
		return 2 * pool.ny;
	}
	GLuint BoundsTextureID() const
	{
		return bounds.empty() ? 0 : bounds.back().TextureID();
	}
	const Pool& CurrentPool() const
	{
		return pool;
	}
	const Plan& LastPlan() const
	{
		return plan;
	}
	/// The critical current of the selected fluid and magnet, amps.
	double CriticalCurrent() const
	{
		return criticalCurrent;
	}
	const physics::Datasheet& Fluid() const;
	physics::Nonlinear Model() const;
	Magnet CurrentMagnet() const;
	/// Coil centres, metres, the pool's centre at 0.
	const std::array< double, 6 >& Centres() const
	{
		return centres;
	}
	const Drive& DriveState() const
	{
		return drive;
	}
	const audio::Analyser& Analyser() const
	{
		return analyser;
	}
	double HostNow() const
	{
		return now;
	}
	/// Fluid seconds simulated since the plugin loaded, and since the last Settle.
	double FluidTime() const
	{
		return fluidTime;
	}
	/// The applied normal field per amp of one coil at radius rho (the table
	/// the GPU reads), and the fluid's G at an applied field (ditto).
	const std::vector< float >& CoilTable() const
	{
		return coilTableData;
	}
	const std::vector< float >& FluidTable() const
	{
		return fluidTableData;
	}
	double CoilTableRadius() const
	{
		return rhoMax;
	}
	double FluidTableField() const
	{
		return fieldMax;
	}
	/// The frame at the pool, metres, as of the last frame.
	double FrameWidth() const
	{
		return frameWidth;
	}
	double FrameHeight() const
	{
		return frameHeight;
	}
	/// The camera, as of the last frame (world metres; the pool's centre at 0).
	const std::array< double, 3 >& EyeForTest() const
	{
		return eye;
	}
	double LensTanHalf() const;
	double ExposureGain() const
	{
		return exposureGain;
	}

private:
	struct Key
	{
		int fluid = -1, magnet = -1;
		float coilSize = -1.0f;
		int physicsHooks = -1, coilHooks = -1;
		double lx = -1.0;
		bool operator==( const Key& o ) const
		{
			return fluid == o.fluid && magnet == o.magnet && coilSize == o.coilSize && physicsHooks == o.physicsHooks
			       && coilHooks == o.coilHooks && lx == o.lx;
		}
	};

	void UpdateClock();
	void UpdateAudio( double dt );
	bool ensurePool( const Pool& want );
	void clearState();
	void updateTables();
	void transform( GLuint source, PassBuffer ( &buffers )[ 2 ], int& last, float direction, int nx, int ny, GLuint twX, GLuint twY );
	void substep( double t, double dt, const Plan& plan );
	void repack();
	void surfaceAndBounds();
	void render( const FFGLTextureStruct* input, const GLint* hostViewport, GLuint hostFBO, int width, int height );
	Plan planSubsteps( double frameDt );
	double stressAt( double appliedField ) const;
	void coilCurrents( double t, double out[ 3 ] ) const;

	/// Uniform locations, looked up once per program and name. A substep is
	/// ten passes and a frame runs a dozen substeps; asking the driver for
	/// every location by name every pass was most of a frame's cost.
	GLint where( const ffglex::FFGLShader& shader, const char* name );
	void uniform( const ffglex::FFGLShader& shader, const char* name, int v );
	void uniform( const ffglex::FFGLShader& shader, const char* name, float v );
	void uniform( const ffglex::FFGLShader& shader, const char* name, float a, float b );
	void uniform( const ffglex::FFGLShader& shader, const char* name, float a, float b, float c );
	std::vector< std::pair< std::pair< GLuint, const char* >, GLint > > uniformCache;

	const bool isEffect;
	const std::vector< unsigned int >& hostOrder;
	int idToHost[ PT_COUNT ];
	float params[ PT_COUNT ] = {};

	ffglex::FFGLShader fftShader, evolveShader, forceShader, packShader, boundsShader, renderShader;
	ffglex::FFGLScreenQuad quad;

	PassBuffer state[ 2 ];      ///< RGBA32F spectral ( h_k, v_k )
	PassBuffer pack;            ///< RG32F: the next inverse's input
	PassBuffer work[ 2 ];       ///< RG32F: the substep transforms' ping-pong
	PassBuffer force;           ///< RG32F: the stress, real space
	PassBuffer surfaceWork[ 2 ];///< RGBA32F, twice the pool's grid: the camera's surface, and its transform's ping-pong
	PassBuffer testWork[ 2 ];   ///< RGBA32F, the pool's grid: TransformForTest only
	std::vector< PassBuffer > bounds;
	GLuint twiddleX = 0, twiddleY = 0, cameraTwiddleX = 0, cameraTwiddleY = 0;
	GLuint coilTable = 0, fluidTable = 0;
	GLuint mrtFBO    = 0;
	int stateIndex   = 0;
	int surfaceIndex = 0;
	bool packValid   = false;

	Pool pool, poolOverride;
	int poolFluid = -1;
	Key tablesFor;
	std::vector< float > coilTableData, fluidTableData;
	double rhoMax = 0.5, fieldMax = 1.0;
	double criticalCurrent = 1.0;///< amps
	double criticalApplied = 1.0;///< A/m
	double centreField     = 1.0;///< applied field per amp at the strongest point, A/m
	std::array< double, 6 > centres {};
	Plan plan;
	unsigned int substepSerial = 0;

	double frameWidth = 0.12, frameHeight = 0.0675;
	std::array< double, 3 > eye {};
	double exposureGain = 1.0;

	//Time (the clock unit vote: rosette's, by way of millpond and schlieren).
	double hostTime = -1.0, lastRawTime = -1.0, lastWallTime = -1.0, wallStart = -1.0;
	double clockScale = 0.0;
	int secondsVotes = 0, millisVotes = 0, clockFrames = 0;
	double now = 0.0, lastNow = -1.0;
	bool settledJump = false;
	double fluidTime = 0.0;
	long long cappedFrames = 0;

	//The beat, as SetBeatInfo last said.
	float bpm = 120.0f, barPhase = 0.0f, lastBarPhase = -1.0f;
	bool hostBeatSeen = false;

	//Presses.
	int firePresses   = 0;
	bool fireHeld     = false;
	bool settleWanted = false;
	bool settleHeld   = false;

	Drive drive;
	audio::Analyser analyser;

	//Harness hooks.
	int hooks = 0, physicsHooks = 0, coilHooks = 0;
	bool resizeClears = false, unprimed = false;
	double substepScale = 1.0;
	std::vector< float > pendingState;
	bool loadPending = false;
	int lastWidth = 0, lastHeight = 0;
};

} // namespace ferrofluid
