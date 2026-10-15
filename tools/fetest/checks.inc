// The checks. Included into main.cpp's anonymous namespace.
//
// Each takes a Perturb. With every field at its default the check scores the
// plugin against the stated law; `--negative` sets one field at a time to a
// deliberately wrong MODEL -- a hook in the plugin, so the shipped code
// computes the wrong thing -- and requires the check to FAIL.

//===========================================================================
// Shared by the checks.
//===========================================================================

/// The quiet pool: nothing fires, no audio, Uniform, EFH1, no noise, the
/// model's coefficients at zero, the camera straight down. Every check then
/// sets what it measures (honeydew's lesson: a check sets every control it
/// depends on).
void quiet( Rig& rig )
{
	rig.Set( PT_FIRE_ON, static_cast< float >( FireOn::Off ) );
	rig.Set( PT_BAND, static_cast< float >( Band::Full ) );
	rig.Set( PT_GAIN, 0.0f );
	rig.Set( PT_BIAS, 0.0f );
	rig.Set( PT_COIL_LAG, ParamFromLag( 0.001 ) );
	rig.Set( PT_PULSE, ParamFromPulse( 0.1 ) );
	rig.Set( PT_MAGNET, static_cast< float >( Magnet::Uniform ) );
	rig.Set( PT_COIL_SIZE, ParamFromCoilSize( 0.025 ) );
	rig.Set( PT_COIL_SPREAD, ParamFromSpread( 0.08 ) );
	rig.Set( PT_FLUID, 0.0f );
	rig.Set( PT_HYSTERESIS, 0.0f );
	rig.Set( PT_SATURATION, ParamFromSaturation( 2.0 ) );
	rig.Set( PT_VISCOSITY, ParamFromViscosity( 1.0 ) );
	rig.Set( PT_NOISE, 0.0f );
	rig.Set( PT_FIELD_WIDTH, ParamFromFieldWidth( 0.12 ) );
	rig.Set( PT_TILT, 0.0f );
	rig.Set( PT_HEIGHT, ParamFromHeight( 0.25 ) );
	rig.Set( PT_EXPOSURE, ParamFromExposure( 0.0 ) );
	rig.Set( PT_LIGHTS, static_cast< float >( Lights::SoftBox ) );
	rig.Set( PT_BACKDROP, static_cast< float >( Backdrop::Black ) );
	rig.Set( PT_MIX, 1.0f );
}

bool startRig( Rig& rig, int w, int h, const Perturb& perturb, const Floats* picture = nullptr )
{
	if( !rig.Init( w, h, picture ) )
		return false;
	rig.plugin.SetHooksForTest( perturb.hooks );
	rig.plugin.SetPhysicsHooksForTest( perturb.physicsHooks );
	rig.plugin.SetCoilHooksForTest( perturb.coilHooks );
	rig.plugin.SetDriveHooksForTest( perturb.driveHooks );
	rig.plugin.SetResizeClearsForTest( perturb.resizeClears );
	rig.plugin.SetUnprimedForTest( perturb.unprimed );
	quiet( rig );
	return true;
}

/// The harness's own Cowley-Rosensweig, from the datasheet: its own Langevin
/// solve (bisection on B continuity), its own tangent permeability (a central
/// difference of M, not the analytic slope), its own r0. It shares nothing
/// with Fluid.cpp but the datasheet's numbers.
struct Reference
{
	double rho, sigma, eta, chi, ms;
	double mOf( double h ) const
	{
		const double x = 3.0 * chi * h / ms;
		const double l = std::fabs( x ) < 1e-3 ? x / 3.0 - x * x * x / 45.0 : 1.0 / std::tanh( x ) - 1.0 / x;
		return ms * l;
	}
	/// G = mu0 M^2 r0 / ( 1 + r0 ) at an applied normal field.
	double stress( double applied ) const
	{
		double lo = 0.0, hi = applied;
		for( int i = 0; i < 200; ++i )
		{
			const double mid = 0.5 * ( lo + hi );
			( mid + mOf( mid ) < applied ? lo : hi ) = mid;
		}
		const double h  = 0.5 * ( lo + hi );
		const double m  = mOf( h );
		const double dh = std::max( 1e-3, 1e-5 * h );
		const double mu_t = 1.0 + ( mOf( h + dh ) - mOf( h - dh ) ) / ( 2.0 * dh );
		const double mu_c = h > 0.0 ? 1.0 + m / h : 1.0 + chi;
		const double r0   = std::sqrt( mu_c * mu_t );
		return 1.25663706212e-6 * m * m * r0 / ( 1.0 + r0 );
	}
	double omega2( double k, double g ) const
	{
		return ( rho * 9.80665 * k + sigma * k * k * k - g * k * k ) / rho;
	}
	double gamma( double k ) const
	{
		return 2.0 * eta / rho * k * k;
	}
};

Reference referenceOf( const physics::Datasheet& d )
{
	return Reference { d.density, d.tension, d.viscosity, d.chi, d.saturation };
}

/// A single real mode h = A cos( k . x ), as the plugin's spectral state:
/// the unnormalised DFT puts A Nx Ny / 2 at ( mx, my ) and at ( -mx, -my ).
Floats singleMode( const Pool& p, int mx, int my, double amplitude )
{
	Floats s( static_cast< size_t >( p.nx ) * p.ny * 4, 0.0f );
	const float a = static_cast< float >( amplitude * p.nx * p.ny / 2.0 );
	auto put      = [ & ]( int x, int y ) {
		x = ( x % p.nx + p.nx ) % p.nx;
		y = ( y % p.ny + p.ny ) % p.ny;
		s[ ( static_cast< size_t >( y ) * p.nx + x ) * 4 ] += a;
	};
	put( mx, my );
	put( -mx, -my );
	return s;
}

/// Height at grid point ( i, j ) of a surface read back.
double heightOf( const Floats& surface, const Pool& p, int i, int j )
{
	return surface[ ( static_cast< size_t >( j ) * p.nx + i ) * 4 ];
}

//===========================================================================
// --fft: the shipping transform against an O(N^2) DFT in double.
//===========================================================================
void dft1( std::vector< Cplx >& v, double sign )
{
	const size_t n = v.size();
	std::vector< Cplx > out( n );
	for( size_t k = 0; k < n; ++k )
	{
		Cplx sum = 0.0;
		for( size_t j = 0; j < n; ++j )
		{
			const double a = sign * 2.0 * kPi * static_cast< double >( ( k * j ) % n ) / static_cast< double >( n );
			sum += v[ j ] * Cplx( std::cos( a ), std::sin( a ) );
		}
		out[ k ] = sum;
	}
	v = out;
}

std::vector< Cplx > dft2( std::vector< Cplx > field, int nx, int ny, double sign )
{
	std::vector< Cplx > row( nx ), col( ny );
	for( int y = 0; y < ny; ++y )
	{
		for( int x = 0; x < nx; ++x )
			row[ x ] = field[ static_cast< size_t >( y ) * nx + x ];
		dft1( row, sign );
		for( int x = 0; x < nx; ++x )
			field[ static_cast< size_t >( y ) * nx + x ] = row[ x ];
	}
	for( int x = 0; x < nx; ++x )
	{
		for( int y = 0; y < ny; ++y )
			col[ y ] = field[ static_cast< size_t >( y ) * nx + x ];
		dft1( col, sign );
		for( int y = 0; y < ny; ++y )
			field[ static_cast< size_t >( y ) * nx + x ] = col[ y ];
	}
	return field;
}

int runFFT( const Perturb& perturb )
{
	std::printf( "\n=== fft: the shipping Stockham transform (radix 16, 8, 4, 2) against an O(N^2) DFT in double, both ways, and its round trip\n" );
	const std::vector< Pool > pools = { { 512, 256, 0.4, 0.2 }, { 128, 64, 0.1, 0.05 }, { 64, 32, 0.1, 0.05 }, { 256, 128, 0.2, 0.1 } };
	for( const Pool& p : pools )
	{
		Rig rig;
		if( !startRig( rig, 64, 36, perturb ) )
			return 1;
		rig.plugin.SetPoolForTest( p );
		if( !rig.Render( 1 ) )
			return 1;
		const size_t n = static_cast< size_t >( p.nx ) * p.ny;
		Floats in( n * 4 );
		std::vector< Cplx > a( n ), b( n );
		for( size_t i = 0; i < n; ++i )
		{
			for( int c = 0; c < 4; ++c )
				in[ i * 4 + c ] = static_cast< float >( 2.0 * hash01( static_cast< uint32_t >( i * 4 + c ), 3 ) - 1.0 );
			a[ i ] = Cplx( in[ i * 4 ], in[ i * 4 + 1 ] );
			b[ i ] = Cplx( in[ i * 4 + 2 ], in[ i * 4 + 3 ] );
		}
		for( float direction : { -1.0f, 1.0f } )
		{
			const Floats got = rig.plugin.TransformForTest( in, direction );
			const auto ra = dft2( a, p.nx, p.ny, direction ), rb = dft2( b, p.nx, p.ny, direction );
			double err = 0.0, norm = 0.0;
			for( size_t i = 0; i < n && got.size() == n * 4; ++i )
			{
				err += std::norm( Cplx( got[ i * 4 ], got[ i * 4 + 1 ] ) - ra[ i ] ) + std::norm( Cplx( got[ i * 4 + 2 ], got[ i * 4 + 3 ] ) - rb[ i ] );
				norm += std::norm( ra[ i ] ) + std::norm( rb[ i ] );
			}
			const double rel = got.size() == n * 4 ? std::sqrt( err / norm ) : 1.0;
			//A float32 sum of N terms through log16 N stages: a few 1e-7 relative
			//RMS (measured below); a wrong index or sign is O(1).
			Check( rel < 1e-5, fmt( "%d x %d %s: relative RMS error %.2e against the double DFT (bound 1e-5)", p.nx, p.ny,
			                        direction < 0 ? "forward" : "inverse", rel ) );
		}
		const Floats there = rig.plugin.TransformForTest( in, -1.0f );
		const Floats back  = rig.plugin.TransformForTest( there, 1.0f );
		double err = 0.0, norm = 0.0;
		for( size_t i = 0; i < n * 4 && back.size() == n * 4; ++i )
		{
			const double v = back[ i ] / static_cast< double >( n );
			err += ( v - in[ i ] ) * ( v - in[ i ] );
			norm += static_cast< double >( in[ i ] ) * in[ i ];
		}
		Check( back.size() == n * 4 && std::sqrt( err / norm ) < 1e-5,
		       fmt( "%d x %d round trip: relative RMS %.2e (bound 1e-5)", p.nx, p.ny, std::sqrt( err / std::max( norm, 1e-30 ) ) ) );
	}
	return Verdict();
}

//===========================================================================
// --names (no GL)
//===========================================================================
int runNames( const Perturb& )
{
	std::printf( "\n=== names: every parameter unique (as Arena addresses them, too), within FFGL's 16 characters, the About block last\n" );
	for( bool over : { false, true } )
	{
		FerrofluidPlugin plugin( over );
		std::map< std::string, int > seen, address;
		int longNames = 0, dupes = 0, clashes = 0, slashes = 0;
		for( unsigned int i = 0; i < plugin.GetNumParams(); ++i )
		{
			const std::string name = plugin.GetParamName( i ) ? plugin.GetParamName( i ) : "";
			if( name.size() > 16 )
			{
				std::printf( "    too long: %s\n", name.c_str() );
				++longNames;
			}
			if( seen[ name ]++ > 0 )
				++dupes;
			slashes += name.find( '/' ) != std::string::npos;
			std::string key;
			for( char c : name )
				if( c != ' ' )
					key += static_cast< char >( std::tolower( static_cast< unsigned char >( c ) ) );
			if( address[ key ]++ > 0 )
				++clashes;
		}
		const unsigned int aboutFirst = plugin.GetNumParams() - stoatworks::about::kParamCount;
		const bool aboutLast          = std::string( plugin.GetParamName( aboutFirst ) ) == "About";
		Check( longNames == 0 && dupes == 0 && clashes == 0 && slashes == 0 && aboutLast,
		       fmt( "%s: %u parameters, %d too long, %d duplicated, %d clashing addresses, %d with a '/'; the About block is last (%s)",
		            over ? "SW Ferro Over" : "SW Ferrofluid", plugin.GetNumParams(), longNames, dupes, clashes, slashes,
		            aboutLast ? "yes" : "NO" ) );
	}
	return Verdict();
}

//===========================================================================
// --cues (no GL): options and events step; the rest ramp; a press waits for
// its key.
//===========================================================================
int runCues( const Perturb& perturb )
{
	std::printf( "\n=== cues: a cue sheet steps options and events, ramps standard controls, and names an option by its name\n" );
	FerrofluidPlugin plugin( false );
	std::istringstream sheet( "0 Magnet Uniform\n60 Magnet Three Coils\n0 Fire 0\n60 Fire 1\n0 Bias 0\n60 Bias 1\n" );
	std::map< unsigned int, Cue > cues;
	std::string error;
	bindScript( plugin, sheet, "cues", cues, error );
	int bad = 0;
	std::string what;
	for( const auto& entry : cues )
	{
		const unsigned int type = plugin.GetParamType( entry.first );
		const bool ramp         = perturb.cuesRamp ? true : entry.second.ramp;
		const float mid         = valueAt( entry.second.track, 29, ramp );
		const float a = entry.second.track.front().second, b = entry.second.track.back().second;
		const bool expectRamp = type == FF_TYPE_STANDARD;
		const bool ok         = expectRamp ? ( mid > a && mid < b ) : ( mid == a && valueAt( entry.second.track, 60, ramp ) == b );
		bad += !ok;
		what += fmt( " %s %s(%g at frame 29)", plugin.GetParamName( entry.first ), expectRamp ? "ramps " : "steps ", mid );
	}
	const auto magnet = cues.find( static_cast< unsigned int >( plugin.HostIndexOf( PT_MAGNET ) ) );
	const bool named  = magnet != cues.end() && magnet->second.track.back().second == static_cast< float >( Magnet::ThreeCoils );
	Check( error.empty() && bad == 0 && cues.size() == 3 && named,
	       fmt( "%d wrong:%s; 'Three Coils' read as option %s", bad, what.c_str(), named ? "2" : "WRONG" ) );
	return Verdict();
}

#include "physics.inc"

//===========================================================================
// The registry.
//===========================================================================
struct CheckEntry
{
	const char* flag;
	CheckFn run;
	bool offline;///< needs no GL context
};

const std::vector< CheckEntry >& checks()
{
	static const std::vector< CheckEntry > list = {
		{ "fft", runFFT, false },
		{ "dispersion", runDispersion, false },
		{ "onset", runOnset, false },
		{ "wavelength", runWavelength, false },
		{ "hexagons", runHexagons, false },
		{ "hysteresis", runHysteresis, false },
		{ "coil", runCoil, true },
		{ "spread", runSpread, false },
		{ "mirror", runMirror, false },
		{ "fresnel", runFresnel, false },
		{ "beat", runBeat, true },
		{ "audio", runAudio, false },
		{ "resize", runResize, false },
		{ "state", runState, false },
		{ "extremes", runExtremes, false },
		{ "names", runNames, true },
		{ "cues", runCues, true },
	};
	return list;
}

bool isOffline( const std::string& flag )
{
	for( const CheckEntry& c : checks() )
		if( flag == c.flag )
			return c.offline;
	return false;
}

//===========================================================================
// --negative
//===========================================================================
int runNegative( bool offlineOnly = false )
{
	struct Case
	{
		const char* name;
		CheckFn check;
		Perturb perturb;
		const char* what;
	};
	std::vector< Case > cases;
	auto add = [ & ]( const char* name, CheckFn fn, const char* what, std::function< void( Perturb& ) > set ) {
		Perturb p;
		set( p );
		cases.push_back( { name, fn, p, what } );
	};
	using namespace shaders;
	add( "dispersion", runDispersion, "the evolve without surface tension", []( Perturb& p ) { p.hooks = kHookNoTension; } );
	add( "dispersion", runDispersion, "G without its r0 / ( 1 + r0 )", []( Perturb& p ) { p.physicsHooks = physics::kHookNoR0Factor; } );
	add( "dispersion", runDispersion, "r0 from the chord permeability alone", []( Perturb& p ) { p.physicsHooks = physics::kHookChordOnly; } );
	add( "onset", runOnset, "the demagnetising jump forgotten: H_in = H_a", []( Perturb& p ) { p.physicsHooks = physics::kHookNoDemag; } );
	add( "wavelength", runWavelength, "the evolve without surface tension", []( Perturb& p ) { p.hooks = kHookNoTension; } );
	add( "hexagons", runHexagons, "no quadratic term (Hysteresis 0): up/down symmetric", []( Perturb& p ) { p.hyScale = 0.0; } );
	add( "hysteresis", runHysteresis, "no quadratic term (Hysteresis 0): a supercritical onset", []( Perturb& p ) { p.hyScale = 0.0; } );
	add( "coil", runCoil, "the L/R lag stepped by forward Euler", []( Perturb& p ) { p.driveHooks = kHookEulerLag; } );
	add( "coil", runCoil, "the on-axis field used at every radius", []( Perturb& p ) { p.coilHooks = coil::kHookOnAxisEverywhere; } );
	add( "spread", runSpread, "the local field dropped: the pool sees G_ref (onset's, at most) everywhere", []( Perturb& p ) { p.hooks = kHookNoLocalField; } );
	add( "mirror", runMirror, "the screen read unmirrored", []( Perturb& p ) { p.hooks = kHookFlipScreen; } );
	add( "fresnel", runFresnel, "the s-polarised reflectance alone", []( Perturb& p ) { p.hooks = kHookSPolarised; } );
	add( "beat", runBeat, "a beat fires at its frame's start", []( Perturb& p ) { p.driveHooks = kHookBeatAtFrameStart; } );
	add( "audio", runAudio, "the analyser never primed", []( Perturb& p ) { p.unprimed = true; } );
	add( "resize", runResize, "a new raster flattens the pool", []( Perturb& p ) { p.resizeClears = true; } );
	add( "extremes", runExtremes, "the substep three times what the plan allows", []( Perturb& p ) { p.substepScale = 3.0; } );
	add( "cues", runCues, "ramp every control between keys", []( Perturb& p ) { p.cuesRamp = true; } );

	if( offlineOnly )
		cases.erase( std::remove_if( cases.begin(), cases.end(), []( const Case& c ) { return !isOffline( c.name ); } ), cases.end() );

	int unfalsifiable = 0;
	for( const Case& c : cases )
	{
		std::printf( "\n=== negative control: %s -- %s\n", c.name, c.what );
		const int before = g_failures;
		g_failures       = 0;
		c.check( c.perturb );
		const int observed = g_failures;
		g_failures         = before;
		if( observed > 0 )
			std::printf( "  ok    %s failed %d check%s, as it must\n", c.name, observed, observed == 1 ? "" : "s" );
		else
		{
			std::printf( "  FAIL  %s PASSED against a wrong model -- it cannot fail, so it is not a check\n", c.name );
			++unfalsifiable;
		}
	}
	std::printf( "\nnegative controls: %zu wrong models, %d of them undetected\n", cases.size(), unfalsifiable );
	std::printf( "\n  %s\n", unfalsifiable == 0 ? "PASS" : "FAIL" );
	return unfalsifiable == 0 ? 0 : 1;
}

int runOffline()
{
	int failed = 0;
	for( const CheckEntry& check : checks() )
		if( check.offline )
			failed |= check.run( Perturb {} );
	failed |= runNegative( true );
	std::printf( "\n  offline: the checks that need no GL context. The picture checks were NOT run --\n"
	             "  tools/verify.sh runs them against a real driver, at 320x180 and above, and again on the software renderer.\n"
	             "\n  %s\n",
	             failed == 0 ? "PASS" : "FAIL" );
	return failed == 0 ? 0 : 1;
}

//===========================================================================
// --bench: GPU time per frame from GL_TIME_ELAPSED.
//===========================================================================
int runBench()
{
	struct Size
	{
		int w, h;
		const char* name;
	};
	const Size sizes[] = { { 1280, 720, "720p" }, { 1920, 1080, "1080p" }, { 3840, 2160, "4K" } };
	struct Case
	{
		const char* what;
		bool over;
		std::function< void( Rig& ) > set;
	};
	const Case cases[] = {
		{ "source, defaults at 120 BPM", false, []( Rig& ) {} },
		{ "Over, defaults at 120 BPM", true, []( Rig& ) {} },
		{ "source, Three Coils at Tilt 60", false, []( Rig& r ) {
			 r.Set( PT_MAGNET, static_cast< float >( Magnet::ThreeCoils ) );
			 r.Set( PT_TILT, 1.0f );
		 } },
	};
	std::printf( "\n=== bench: GPU time per frame (GL_TIME_ELAPSED), after 240 frames of warm-up, median and worst of 120\n" );
	GLuint query = 0;
	glGenQueries( 1, &query );
	for( const Case& c : cases )
		for( const Size& size : sizes )
		{
			Rig rig( c.over );
			rig.bpm = 120.0;
			if( !rig.Init( size.w, size.h ) )
				return 1;
			c.set( rig );
			if( !rig.Render( 240 ) )
				return 1;
			constexpr int kTimed = 120;
			std::vector< double > times, cpu;
			int substeps = 0;
			for( int f = 0; f < kTimed; ++f )
			{
				const auto start = std::chrono::steady_clock::now();
				glBeginQuery( GL_TIME_ELAPSED, query );
				if( !rig.Render( 1 ) )
					return 1;
				glEndQuery( GL_TIME_ELAPSED );
				GLuint64 ns = 0;
				glGetQueryObjectui64v( query, GL_QUERY_RESULT, &ns );
				times.push_back( static_cast< double >( ns ) * 1e-6 );
				cpu.push_back( std::chrono::duration< double, std::milli >( std::chrono::steady_clock::now() - start ).count() );
				substeps = std::max( substeps, rig.plugin.LastPlan().substeps );
			}
			std::sort( times.begin(), times.end() );
			std::sort( cpu.begin(), cpu.end() );
			std::printf( "  %-34s %-6s GPU median %6.2f ms, worst %6.2f  (%4.1f%% of a 60 fps frame); wall %5.2f ms; up to %d substeps\n",
			             c.what, size.name, times[ kTimed / 2 ], times.back(), 100.0 * times[ kTimed / 2 ] / ( 1000.0 / 60.0 ), cpu[ kTimed / 2 ],
			             substeps );
		}
	glDeleteQueries( 1, &query );
	return 0;
}

//===========================================================================
// --fftbench: one RGBA transform of the pool's grid, timed (development aid).
//===========================================================================
int runFFTBench()
{
	Rig rig;
	if( !rig.Init( 64, 36 ) || !rig.Render( 1 ) )
		return 1;
	const Pool& p = rig.plugin.CurrentPool();
	Floats in( static_cast< size_t >( p.nx ) * p.ny * 4, 0.5f );
	GLuint query = 0;
	glGenQueries( 1, &query );
	std::vector< double > times;
	for( int i = 0; i < 60; ++i )
	{
		glBeginQuery( GL_TIME_ELAPSED, query );
		rig.plugin.TransformForTest( in, -1.0f );
		glEndQuery( GL_TIME_ELAPSED );
		GLuint64 ns = 0;
		glGetQueryObjectui64v( query, GL_QUERY_RESULT, &ns );
		times.push_back( ns * 1e-6 );
	}
	std::sort( times.begin(), times.end() );
	std::printf( "%d x %d RGBA transform (with upload and read-back): median %.3f ms\n", p.nx, p.ny, times[ 30 ] );
	glDeleteQueries( 1, &query );
	return 0;
}
