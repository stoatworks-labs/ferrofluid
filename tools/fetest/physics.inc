// The physics checks. Included into checks.inc, inside main.cpp's anonymous
// namespace.

//===========================================================================
// Shared.
//===========================================================================

/// The current, as a multiple of the critical one, that puts a UNIFORM pool
/// at magnetic number m (bisection on the fluid's own response: setup, not
/// the thing measured).
double multipleForM( const physics::Datasheet& fluid, double m, int physicsHooks = 0 )
{
	const double target = m * 2.0 * fluid.density * physics::kGravity / physics::CriticalWavenumber( fluid );
	const double hc     = physics::CriticalApplied( fluid, physicsHooks );
	double lo = 0.0, hi = 8.0 * hc;
	for( int i = 0; i < 200; ++i )
	{
		const double mid = 0.5 * ( lo + hi );
		( physics::Magnetise( fluid, mid, physicsHooks ).g < target ? lo : hi ) = mid;
	}
	return 0.5 * ( lo + hi ) / hc;
}

/// The pool's real-space heights at its own grid points: every other sample of
/// the camera's surface (the zero-padded inverse samples the pool's points
/// exactly at the even indices).
std::vector< double > poolHeights( Rig& rig )
{
	const Floats s = rig.PoolSurface();
	const Pool& p  = rig.plugin.CurrentPool();
	const int w    = rig.plugin.SurfaceWidth();
	std::vector< double > h( static_cast< size_t >( p.nx ) * p.ny );
	for( int j = 0; j < p.ny; ++j )
		for( int i = 0; i < p.nx; ++i )
			h[ static_cast< size_t >( j ) * p.nx + i ] = s[ ( static_cast< size_t >( 2 * j ) * w + 2 * i ) * 4 ];
	return h;
}

double maxAbs( const std::vector< double >& v )
{
	double m = 0.0;
	for( double x : v )
		m = std::max( m, std::fabs( x ) );
	return m;
}

/// The pool fitted to the hexagonal lattice: k1 = ( k_c, 0 ) is mode ( 12, 0 )
/// and k2,3 = ( -k_c / 2, +-sqrt 3 k_c / 2 ) are modes ( -6, +-7 ): Lx = 12
/// lambda_c, Ly = 14 lambda_c / sqrt 3. Cells are not square here, which the
/// spectral step does not mind.
Pool hexagonPool( const physics::Datasheet& fluid )
{
	const double lambda = physics::CriticalWavelength( fluid );
	return Pool { 256, 128, 12.0 * lambda, 14.0 * lambda / std::sqrt( 3.0 ) };
}

/// The three hexagon modes at amplitude A each (h = 2 A sum cos), as a state.
Floats hexagonSeed( const Pool& p, double amplitude )
{
	Floats s = singleMode( p, 12, 0, 2.0 * amplitude );
	const Floats b = singleMode( p, -6, 7, 2.0 * amplitude ), c = singleMode( p, -6, -7, 2.0 * amplitude );
	for( size_t i = 0; i < s.size(); ++i )
		s[ i ] += b[ i ] + c[ i ];
	return s;
}

/// Uniform, the model's coefficients set, the current at a multiple, no noise,
/// a fast coil (1 ms: the field is the bias by the time anything is measured).
void uniformPool( Rig& rig, double hy, double sa, double multiple, double noise = 0.0 )
{
	rig.Set( PT_MAGNET, static_cast< float >( Magnet::Uniform ) );
	rig.plugin.SetModelForTest( physics::Nonlinear { hy, sa } );
	rig.Set( PT_BIAS, ParamFromCurrent( multiple ) );
	rig.Set( PT_GAIN, 0.0f );
	rig.Set( PT_COIL_LAG, ParamFromLag( 0.001 ) );
	rig.Set( PT_NOISE, ParamFromNoise( noise ) );
}

//===========================================================================
// --dispersion: every mode rings (or grows) at the Cowley-Rosensweig rate.
//===========================================================================
int runDispersion( const Perturb& perturb )
{
	std::printf( "\n=== dispersion: a single mode of a quiet pool against the closed-form damped oscillator with the Cowley-Rosensweig omega(k) "
	             "(the harness's own Langevin, its own derivative), with and without field\n" );
	const physics::Datasheet& fluid = physics::FluidOf( physics::FluidId::EFH1 );
	const Reference ref             = referenceOf( fluid );
	const double lambda             = physics::CriticalWavelength( fluid );
	const Pool pool { 256, 128, 16.0 * lambda, 8.0 * lambda };
	struct Mode
	{
		int mx, my;
	};
	const Mode modes[] = { { 8, 0 }, { 16, 0 }, { 24, 0 }, { 16, 8 }, { 40, 0 }, { 64, 0 } };
	for( const Raster& raster : kRasters )
		for( double multiple : { 0.0, 0.7, 0.97, 1.1 } )
		{
			Rig rig;
			if( !startRig( rig, raster.w, raster.h, perturb ) )
				return 1;
			rig.plugin.SetPoolForTest( pool );
			uniformPool( rig, 0.0, 0.5, multiple );
			if( !rig.Render( 30 ) )
				return 1;
			//The applied field, from the current in amps and the Helmholtz
			//pair's own constant, worked out here.
			const double perAmp  = std::pow( 0.8, 1.5 ) * 200.0 / 0.20;
			const double applied = rig.plugin.CriticalCurrent() * multiple * perAmp;
			const double g       = ref.stress( applied );
			double worst = 0.0;
			std::string what;
			for( const Mode& mode : modes )
			{
				//Past onset the unstable modes grow at up to ~55/s: 0.1 s from
				//10 nm keeps the mode linear (k h ~ 1e-3, the cubic 1e-6 of it)
				//and float round-off in the other unstable modes under 1e-4
				//of it. Below onset, 0.4 s from 0.1 um.
				const bool past        = multiple > 1.0;
				const double amplitude = past ? 1e-8 : 1e-7;
				const int frames       = past ? 6 : 24;
				rig.plugin.LoadStateForTest( singleMode( pool, mode.mx, mode.my, amplitude ) );
				const double t0 = rig.plugin.FluidTime();
				const double kx = 2.0 * kPi * mode.mx / pool.lx, ky = 2.0 * kPi * mode.my / pool.ly;
				const double k  = std::hypot( kx, ky );
				const double w2 = ref.omega2( k, g ), gam = ref.gamma( k );
				double err = 0.0, scale = amplitude;
				for( int f = 0; f < frames; ++f )
				{
					if( !rig.Render( 1 ) )
						return 1;
					const double t   = rig.plugin.FluidTime() - t0;
					const double got = poolHeights( rig )[ 0 ];
					//y( 0 ) = A, y'( 0 ) = 0.
					double want;
					const double disc = w2 - gam * gam;
					if( disc > 0.0 )
					{
						const double wd = std::sqrt( disc );
						want            = amplitude * std::exp( -gam * t ) * ( std::cos( wd * t ) + gam / wd * std::sin( wd * t ) );
					}
					else
					{
						const double wd = std::sqrt( -disc );
						want            = amplitude * std::exp( -gam * t ) * ( std::cosh( wd * t ) + gam / wd * std::sinh( wd * t ) );
					}
					scale = std::max( scale, std::fabs( want ) );
					err   = std::max( err, std::fabs( got - want ) );
				}
				worst = std::max( worst, err / scale );
				what += fmt( " q=%.2f:%s%.0e", k / physics::CriticalWavenumber( fluid ), w2 < 0.0 ? "grows," : "", err / scale );
			}
			//float32 through two transforms and a dozen exact steps a frame for
			//24 frames: a few 1e-6 of the largest height; a wrong omega by a
			//percent is 1e-2 of it within the run (AGENTS.md).
			Check( worst < 2e-4, fmt( "%dx%d, %.2f Ic (G %.2f Pa, m %.3f):%s -- worst %.1e of the largest height (bound 2e-4)", raster.w,
			                          raster.h, multiple, g, physics::MagneticNumber( fluid, g ), what.c_str(), worst ) );
		}
	return Verdict();
}

//===========================================================================
// --onset: the flat surface goes unstable where Cowley-Rosensweig say, and a
// published critical field is reproduced.
//===========================================================================
int runOnset( const Perturb& perturb )
{
	std::printf( "\n=== onset: a seeded k_c mode decays below M_c and grows above it; the threshold current's M brackets "
	             "M_c^2 = ( 2 / mu0 )( 1 + 1 / r0 ) sqrt( rho g sigma ) within one current step\n" );
	{
		//Mahr & Rehberg, Physica D 111 (1998) 335: their EMG 901 / 909 mixture,
		//linear magnetisation (mu = 2.15), infinite layer: H_c = 5.9e3 A/m
		//inside the fluid and lambda_c = 9 mm, by the same theory.
		const double rho = 1377.0, sigma = 2.86e-2, mu = 2.15;
		const double mc2  = ( 2.0 / physics::kMu0 ) * ( 1.0 + 1.0 / mu ) * std::sqrt( rho * physics::kGravity * sigma );
		const double hc   = std::sqrt( mc2 ) / ( mu - 1.0 );
		const double lc   = 2.0 * kPi * std::sqrt( sigma / ( rho * physics::kGravity ) );
		Check( std::fabs( hc - 5.9e3 ) < 0.05e3 && std::fabs( lc - 9e-3 ) < 0.5e-3,
		       fmt( "Mahr & Rehberg's mixture: H_c %.0f A/m against their 5.9e3, lambda_c %.2f mm against their 9 (their rounding)", hc,
		            lc * 1e3 ) );
	}
	const physics::Datasheet& fluid = physics::FluidOf( physics::FluidId::EFH1 );
	const Reference ref             = referenceOf( fluid );
	const double lambda             = physics::CriticalWavelength( fluid );
	const Pool pool { 128, 64, 8.0 * lambda, 4.0 * lambda };
	const double perAmp = std::pow( 0.8, 1.5 ) * 200.0 / 0.20;
	//The reference threshold, in amps: the harness's own G (its Langevin, its
	//numerical tangent permeability) against the published G_c, bisected on
	//the Helmholtz pair's applied field.
	const double gc = 2.0 * std::sqrt( ref.rho * physics::kGravity * ref.sigma );
	double lo = 0.0, hi = 1e5;
	for( int i = 0; i < 200; ++i )
	{
		const double mid = 0.5 * ( lo + hi );
		( ref.stress( mid ) < gc ? lo : hi ) = mid;
	}
	const double referenceAmps = 0.5 * ( lo + hi ) / perAmp;
	for( const Raster& raster : kRasters )
	{
		//Half a step off 1, so no sample sits on the onset itself, where a
		//mode neither grows nor decays and the verdict is rounding's.
		const double step = 0.0025;
		double grew = -1.0, decayed = -1.0, amps = 0.0;
		std::string trail;
		for( double multiple = 0.98625; multiple < 1.0151; multiple += step )
		{
			Rig rig;
			if( !startRig( rig, raster.w, raster.h, perturb ) )
				return 1;
			rig.plugin.SetPoolForTest( pool );
			uniformPool( rig, 0.0, 0.5, multiple );
			if( !rig.Render( 30 ) )
				return 1;
			const double amplitude = 1e-7;
			rig.plugin.LoadStateForTest( singleMode( pool, 8, 0, amplitude ) );
			if( !rig.Render( 60 ) )
				return 1;
			const double now = maxAbs( poolHeights( rig ) );
			const bool up    = now > 2.0 * amplitude;
			trail += up ? "+" : "-";
			if( up && grew < 0.0 )
			{
				grew = multiple;
				amps = rig.plugin.CriticalCurrent();
			}
			if( !up )
				decayed = multiple;
		}
		const double down = amps * ( grew - step ), up = amps * grew;
		Check( grew > 0.0 && decayed > 0.0 && decayed < grew && down < referenceAmps && referenceAmps <= up,
		       fmt( "%dx%d: the sweep %s crosses once: still at %.3f A, grows at %.3f A; the onset from the harness's own magnetisation and "
		            "M_c^2 = ( 2 / mu0 )( 1 + 1 / r0 ) sqrt( rho g sigma ): %.4f A, between",
		            raster.w, raster.h, trail.c_str(), down, up, referenceAmps ) );
	}
	return Verdict();
}

//===========================================================================
// --wavelength and --hexagons: the settled pattern's spacing and symmetry.
//===========================================================================
struct Peak
{
	int mx, my;
	double power;
};

/// The strongest modes of a real field on a pool, one of each +-k pair.
std::vector< Peak > spectralPeaks( const std::vector< double >& h, const Pool& p, int count )
{
	std::vector< Cplx > field( h.begin(), h.end() );
	const auto spectrum = dft2( field, p.nx, p.ny, -1.0 );
	std::vector< Peak > all;
	for( int j = 0; j < p.ny; ++j )
		for( int i = 0; i < p.nx; ++i )
		{
			const int mx = i < p.nx / 2 ? i : i - p.nx, my = j < p.ny / 2 ? j : j - p.ny;
			if( my < 0 || ( my == 0 && mx <= 0 ) )
				continue;//one of each pair, and not the mean
			all.push_back( { mx, my, std::norm( spectrum[ static_cast< size_t >( j ) * p.nx + i ] ) } );
		}
	std::sort( all.begin(), all.end(), []( const Peak& a, const Peak& b ) { return a.power > b.power; } );
	all.resize( std::min< size_t >( all.size(), static_cast< size_t >( count ) ) );
	return all;
}

/// A settled hexagonal pool: just past onset, the shipped model, a noise floor.
bool settledPattern( Rig& rig, const Perturb& perturb, double hy, double sa, double m, int frames )
{
	const physics::Datasheet& fluid = physics::FluidOf( physics::FluidId::EFH1 );
	rig.plugin.SetPoolForTest( hexagonPool( fluid ) );
	const double h = perturb.hyScale >= 0.0 ? perturb.hyScale : hy;
	uniformPool( rig, h, sa, multipleForM( fluid, m ), 1e-3 );
	return rig.Render( frames );
}

int runWavelength( const Perturb& perturb )
{
	//the pattern engine as it ships for EFH1: calibrated to the solver's branch
	const spikes::Table& shipped = spikes::TableFor( 0 );
	std::printf( "\n=== wavelength: the settled pattern's dominant wavenumber is k_c = sqrt( rho g / sigma ), from the height and from the picture\n" );
	const physics::Datasheet& fluid = physics::FluidOf( physics::FluidId::EFH1 );
	const double kc                 = physics::CriticalWavenumber( fluid );
	const Pool pool                 = hexagonPool( fluid );
	for( const Raster& raster : kRasters )
	{
		Rig rig;
		if( !startRig( rig, raster.w, raster.h, perturb ) )
			return 1;
		//The picture: straight down, the frame exactly the pool's width, the
		//studio's ceiling in the flat parts and darker flanks.
		rig.Set( PT_FIELD_WIDTH, ParamFromFieldWidth( pool.lx ) );
		rig.Set( PT_BACKDROP, static_cast< float >( Backdrop::Studio ) );
		if( !settledPattern( rig, perturb, shipped.hysteresis, shipped.saturation, 1.04, 300 ) )
			return 1;
		const std::vector< Peak > peaks = spectralPeaks( poolHeights( rig ), pool, 1 );
		const double k   = std::hypot( 2.0 * kPi * peaks[ 0 ].mx / pool.lx, 2.0 * kPi * peaks[ 0 ].my / pool.ly );
		const double bin = 2.0 * kPi / std::min( pool.lx, pool.ly );
		Check( std::fabs( k - kc ) <= bin + 1e-9,
		       fmt( "%dx%d height: dominant mode ( %d, %d ), k = %.1f /m against k_c %.1f /m (one bin %.1f)", raster.w, raster.h, peaks[ 0 ].mx,
		            peaks[ 0 ].my, k, kc, bin ) );

		//The picture's luminance, box-averaged to a coarser raster, Hann-windowed
		//both ways and transformed in two dimensions: the pool's lattice is the
		//picture's, so its strongest frequency in the band round k_c is k_c in
		//the frame's metres, to the picture's own bin, 2 pi / W. (Below the
		//band is the studio's brightness falling across the frame.)
		const Floats out = rig.Output();
		const int box    = std::max( 1, rig.width / 160 );
		const int cw = rig.width / box, ch = rig.height / box;
		const double W = rig.plugin.FrameWidth(), H = rig.plugin.FrameHeight();
		std::vector< Cplx > lum( static_cast< size_t >( cw ) * ch );
		for( int y = 0; y < ch; ++y )
			for( int x = 0; x < cw; ++x )
			{
				double sum = 0.0;
				for( int dy = 0; dy < box; ++dy )
					for( int dx = 0; dx < box; ++dx )
					{
						const float* px = &out[ ( static_cast< size_t >( y * box + dy ) * rig.width + x * box + dx ) * 4 ];
						sum += 0.2126 * px[ 0 ] + 0.7152 * px[ 1 ] + 0.0722 * px[ 2 ];
					}
				const double hann = ( 0.5 - 0.5 * std::cos( 2.0 * kPi * ( x + 0.5 ) / cw ) ) * ( 0.5 - 0.5 * std::cos( 2.0 * kPi * ( y + 0.5 ) / ch ) );
				lum[ static_cast< size_t >( y ) * cw + x ] = sum / ( box * box ) * hann;
			}
		const auto picture = dft2( lum, cw, ch, -1.0 );
		double best = 0.0, kPicture = 0.0;
		for( int j = 0; j < ch; ++j )
			for( int i = 0; i < cw; ++i )
			{
				const int mx = i < cw / 2 ? i : i - cw, my = j < ch / 2 ? j : j - ch;
				const double k = std::hypot( 2.0 * kPi * mx / W, 2.0 * kPi * my / H );
				if( k < 0.5 * kc || k > 1.5 * kc )
					continue;
				const double power = std::norm( picture[ static_cast< size_t >( j ) * cw + i ] );
				if( power > best )
				{
					best     = power;
					kPicture = k;
				}
			}
		const double pictureBin = 2.0 * kPi / std::min( W, H );
		Check( std::fabs( kPicture - kc ) <= pictureBin + 1e-9,
		       fmt( "%dx%d picture: the strongest frequency between 0.5 and 1.5 k_c is k = %.1f /m against k_c %.1f /m (the picture's bin %.1f)",
		            raster.w, raster.h, kPicture, kc, pictureBin ) );
	}
	return Verdict();
}

int runHexagons( const Perturb& perturb )
{
	const spikes::Table& shipped = spikes::TableFor( 0 );
	std::printf( "\n=== hexagons: the settled pattern's spectrum has three dominant peaks at k_c, 60 degrees apart, of comparable strength\n" );
	const physics::Datasheet& fluid = physics::FluidOf( physics::FluidId::EFH1 );
	const Pool pool                 = hexagonPool( fluid );
	for( const Raster& raster : kRasters )
	{
		Rig rig;
		if( !startRig( rig, raster.w, raster.h, perturb ) )
			return 1;
		if( !settledPattern( rig, perturb, shipped.hysteresis, shipped.saturation, 1.04, 300 ) )
			return 1;
		const std::vector< Peak > peaks = spectralPeaks( poolHeights( rig ), pool, 3 );
		double angles[ 3 ];
		std::string what;
		for( int i = 0; i < 3; ++i )
		{
			angles[ i ] = std::atan2( peaks[ i ].my / pool.ly, peaks[ i ].mx / pool.lx ) * 180.0 / kPi;
			what += fmt( " (%d,%d) %.1f deg %.2g", peaks[ i ].mx, peaks[ i ].my, angles[ i ], peaks[ i ].power / peaks[ 0 ].power );
		}
		//Directions modulo 180 (a mode and its conjugate are one peak): the
		//three must be 60 degrees apart pairwise, to the lattice's own
		//resolution. The pool fits a hexagon with k1 along x exactly; one that
		//grows the other way up lands on the nearest lattice points, up to half
		//the coarser bin off: atan( 0.5 x 2 pi / Ly / k_c ) = 3.5 degrees each,
		//so 4 between two.
		std::vector< double > d = { std::fmod( angles[ 0 ] + 360.0, 180.0 ), std::fmod( angles[ 1 ] + 360.0, 180.0 ),
			                        std::fmod( angles[ 2 ] + 360.0, 180.0 ) };
		std::sort( d.begin(), d.end() );
		const double a = d[ 1 ] - d[ 0 ], b = d[ 2 ] - d[ 1 ], c = 180.0 - ( d[ 2 ] - d[ 0 ] );
		const bool spaced = std::fabs( a - 60.0 ) < 4.0 && std::fabs( b - 60.0 ) < 4.0 && std::fabs( c - 60.0 ) < 4.0;
		//A hexagon is three modes at comparable strength; rolls are one.
		const bool balanced = peaks[ 2 ].power > 0.2 * peaks[ 0 ].power;
		Check( spaced && balanced, fmt( "%dx%d:%s -- %.1f, %.1f, %.1f degrees apart", raster.w, raster.h, what.c_str(), a, b, c ) );
	}
	return Verdict();
}

//===========================================================================
// --hysteresis: the subcritical loop, against the model's own reduction.
//===========================================================================
int runHysteresis( const Perturb& perturb )
{
	std::printf( "\n=== hysteresis: a slow up-sweep jumps to finite spikes at onset; a slow down-sweep holds them below it and loses them at the "
	             "saddle-node the model's own three-mode reduction (its energy averaged exactly over a hexagonal cell) predicts\n" );
	const physics::Datasheet& fluid = physics::FluidOf( physics::FluidId::EFH1 );
	const Pool pool                 = hexagonPool( fluid );
	const double kc                 = physics::CriticalWavenumber( fluid );
	//Two members of the family ( lambda Hy, lambda^2 Sa ): the same loop,
	//spikes a factor 2 apart (below).
	struct Member
	{
		double hy, sa;
	};
	const Member family[] = { { 0.35, 1.0 }, { 0.7, 4.0 } };
	const double step     = 0.0005;//in m
	double errors[ 2 ]    = { 0.0, 0.0 };
	int which             = 0;
	for( const Member& member : family )
	{
		physics::Nonlinear model;
		model.hysteresis  = perturb.hyScale >= 0.0 ? perturb.hyScale : member.hy;
		model.saturation  = member.sa;
		const double msn  = physics::SaddleNode( physics::Nonlinear { member.hy, member.sa } );
		const physics::Hexagons at1 = physics::ThreeMode( physics::Nonlinear { member.hy, member.sa }, 1.0 );
		Rig rig;
		const Raster& raster = kRasters.back();
		if( !startRig( rig, raster.w, raster.h, perturb ) )
			return 1;
		rig.plugin.SetPoolForTest( pool );
		uniformPool( rig, model.hysteresis, model.saturation, multipleForM( fluid, 0.99 ) );
		if( !rig.Render( 10 ) )
			return 1;
		//Up: a tiny hexagon seeded afresh at each step (far below the unstable
		//branch's amplitude, so it decays wherever the flat state is stable).
		const double seed = 1e-6 / kc;
		double jump = -1.0, jumpAmplitude = 0.0;
		for( double m = 0.996; m < 1.0026; m += step )
		{
			rig.Set( PT_BIAS, ParamFromCurrent( multipleForM( fluid, m ) ) );
			rig.plugin.LoadStateForTest( hexagonSeed( pool, seed ) );
			if( !rig.Render( 150 ) )
				return 1;
			const double a = maxAbs( poolHeights( rig ) ) * kc;
			if( a > 100.0 * seed * kc )
			{
				//Let the jump finish before reading how big it is.
				if( !rig.Render( 240 ) )
					return 1;
				jump          = m;
				jumpAmplitude = maxAbs( poolHeights( rig ) ) * kc;
				break;
			}
		}
		//Down, from the pattern the up-sweep left, continuing the state.
		double collapse = -1.0;
		for( double m = jump - step; jump > 0.0 && m > 0.95; m -= step )
		{
			rig.Set( PT_BIAS, ParamFromCurrent( multipleForM( fluid, m ) ) );
			if( !rig.Render( 240 ) )
				return 1;
			const double a = maxAbs( poolHeights( rig ) ) * kc;
			if( a < 0.1 * jumpAmplitude )
			{
				collapse = m;
				break;
			}
		}
		errors[ which ] = collapse > 0.0 ? std::fabs( collapse - msn ) : 1.0;
		//The tolerance: one step of the sweep (the collapse is seen at the first
		//step past the saddle-node), plus 5% of the loop's width for what the
		//reduction leaves out -- the outer cut passes 96% of the stress at 2 k_c
		//and 99% at sqrt 3 k_c, and the families past 2 k_c enter at the square
		//of the last kept one's share of the pattern, ( C / A )^2 ~ 1e-4 here.
		const physics::Hexagons atSn = physics::ThreeMode( physics::Nonlinear { member.hy, member.sa }, msn + 1e-6 );
		const double width           = 1.0 - msn;
		const double tolerance       = step + 0.05 * width;
		//Up: no jump below onset (the flat state is stable there), and one by
		//two steps above it (a mode at m - 1 = 5e-4 grows at a fraction of a
		//per second, too slowly to show in 2.5 s; the next step's does).
		Check( jump > 0.0 && jump >= 1.0 - 1e-9 && jump < 1.0 + 2.0 * step + 1e-9,
		       fmt( "Hy %.2f, Sa %.1f: the up-sweep jumps at m %.4f to %.3f k_c^-1 (the reduction: onset at 1, %.3f at its spikes' apex)",
		            model.hysteresis, model.saturation, jump, jumpAmplitude, at1.peak ) );
		Check( collapse > 0.0 && errors[ which ] <= tolerance && collapse < 1.0 - 2.0 * step,
		       fmt( "Hy %.2f, Sa %.1f: the down-sweep holds to m %.4f and loses them there; the reduction's saddle-node %.4f (loop width "
		            "%.4f, tolerance %.4f = a step + 5%% of the width); at the saddle-node the reduction's spikes are %.3f k_c^-1",
		            model.hysteresis, model.saturation, collapse, msn, width, tolerance, atSn.peak ) );
		++which;
	}
	//The model's energy is exactly invariant under ( lambda Hy, lambda^2 Sa )
	//with heights 1 / lambda (it scales by 1 / lambda^2 as a whole), so the two
	//members share one saddle-node while their spikes differ by a factor of 2.
	//Both landing on it says the solver's loop is the model's, at either size.
	Check( errors[ 0 ] <= step && errors[ 1 ] <= step,
	       fmt( "both members, amplitudes a factor 2 apart, collapse within a step of the same predicted saddle-node (%.4f and %.4f off)",
	            errors[ 0 ], errors[ 1 ] ) );
	return Verdict();
}

//===========================================================================
// --coil (no GL): the circuit's lag, and the loop's field against Biot-Savart.
//===========================================================================
/// H_z of a loop by Biot-Savart summed round it by hand: the harness's own,
/// sharing nothing with the elliptic integrals. Periodic trapezoid in phi.
double biotSavart( double a, double rho, double z )
{
	const int n = 20000;
	double sum  = 0.0;
	for( int i = 0; i < n; ++i )
	{
		const double phi = 2.0 * kPi * ( i + 0.5 ) / n;
		const double d2  = rho * rho + a * a - 2.0 * a * rho * std::cos( phi ) + z * z;
		sum += ( a * a - a * rho * std::cos( phi ) ) / std::pow( d2, 1.5 );
	}
	return sum * ( 2.0 * kPi / n ) / ( 4.0 * kPi );
}

int runCoil( const Perturb& perturb )
{
	std::printf( "\n=== coil: the current's step response is 1 - exp( -t R / L ); one loop's field against Biot-Savart and its on-axis limit; the "
	             "solenoid table against the winding summed by hand\n" );
	{
		Drive drive;
		drive.SetHooksForTest( perturb.driveHooks );
		DriveInputs in;
		in.fireOn = FireOn::Off;
		in.tau    = 0.05;
		in.gain   = 0.0;
		in.bias   = 0.0;
		drive.Advance( 0.0, 0.0, in );
		in.bias      = 1.0;
		double worst = 0.0;
		for( int f = 1; f <= 30; ++f )
		{
			drive.Advance( f / 60.0, 1.0 / 60.0, in );
			const double want = 1.0 - std::exp( -( f / 60.0 ) / 0.05 );
			worst             = std::max( worst, std::fabs( drive.Current( 0 ) - want ) );
		}
		//A pulse with edges inside frames: on at 0.51 s for 0.12 s, the
		//current read inside the frames against the closed form across both
		//edges.
		Drive pulse;
		pulse.SetHooksForTest( perturb.driveHooks );
		DriveInputs p = in;
		p.bias        = 0.0;
		p.gain        = 1.0;
		p.pulse       = 0.12;
		pulse.Advance( 0.0, 0.0, p );
		double worstPulse = 0.0;
		for( int f = 1; f <= 60; ++f )
		{
			const double t1 = f / 60.0;
			p.presses       = ( f == 31 ) ? 1 : 0;//a press lands at its frame's start, 0.5 s
			pulse.Advance( t1, 1.0 / 60.0, p );
			for( double s : { 0.25, 0.5, 0.75, 1.0 } )
			{
				const double t = t1 - ( 1.0 - s ) / 60.0;
				const double on = 0.5, off = 0.62;
				double want = 0.0;
				if( t > on )
					want = 1.0 - std::exp( -( std::min( t, off ) - on ) / 0.05 );
				if( t > off )
					want *= std::exp( -( t - off ) / 0.05 );
				worstPulse = std::max( worstPulse, std::fabs( pulse.CurrentAt( 0, t ) - want ) );
			}
		}
		Check( worst < 1e-12 && worstPulse < 1e-12,
		       fmt( "L/R = 50 ms: a step against 1 - exp( -t / tau ) to %.1e, a 120 ms pulse read inside its frames to %.1e (bound 1e-12)", worst,
		            worstPulse ) );
	}
	{
		double K = 0.0, E = 0.0;
		coil::Elliptic( 0.5, K, E );
		//K( 1/2 ) and E( 1/2 ), tabulated (Abramowitz & Stegun 17.3, m = 1/2).
		Check( std::fabs( K - 1.854074677301372 ) < 1e-14 && std::fabs( E - 1.350643881047675 ) < 1e-14,
		       fmt( "the AGM's K( 1/2 ) = %.15f, E( 1/2 ) = %.15f against the tables", K, E ) );
		double worst = 0.0, worstAxis = 0.0;
		for( double rho : { 0.0, 0.004, 0.012, 0.02, 0.031, 0.05, 0.09 } )
			for( double z : { 0.013, 0.025, 0.053 } )
			{
				const double a = 0.025, got = coil::LoopAxial( a, rho, z ), want = biotSavart( a, rho, z );
				worst = std::max( worst, std::fabs( got - want ) / std::fabs( want ) );
				if( rho == 0.0 )
					worstAxis = std::max( worstAxis, std::fabs( got - a * a / ( 2.0 * std::pow( a * a + z * z, 1.5 ) ) ) / std::fabs( got ) );
			}
		Check( worst < 1e-9 && worstAxis < 1e-13,
		       fmt( "one 25 mm loop at 21 points to 9 cm off axis: elliptic formula against Biot-Savart to %.1e; on axis against a^2 / ( 2 ( a^2 + z^2 )^1.5 ) "
		            "to %.1e",
		            worst, worstAxis ) );
		//The solenoid's table, as the plugin builds it (with its hooks),
		//against 400 turns summed one by one with Biot-Savart.
		const double radius = 0.04, rhoMax = 0.4;
		std::vector< float > table( shaders::kTableSize );
		coil::SolenoidProfile( radius, rhoMax, shaders::kTableSize, table.data(), perturb.coilHooks );
		double worstTable = 0.0;
		for( int i : { 0, 60, 100, 140, 200, 400 } )
		{
			const double rho = rhoMax * i / ( shaders::kTableSize - 1 );
			double want      = 0.0;
			for( int turn = 0; turn < 400; ++turn )
				want += biotSavart( radius, rho, coil::kSolenoidDepth + ( turn + 0.5 ) * coil::kSolenoidLength / 400.0 );
			worstTable = std::max( worstTable, std::fabs( table[ i ] - want ) / std::max( std::fabs( want ), 1e-3 * table[ 0 ] ) );
		}
		//A float table of a sum over 32 quadrature points against 400 discrete
		//turns: the turns' discreteness is 1e-5 here (the depth is 13 turns'
		//pitch), float is 6e-8.
		Check( worstTable < 1e-4, fmt( "the 4 cm solenoid's table at six radii against 400 turns of Biot-Savart: %.1e (bound 1e-4)", worstTable ) );
	}
	return Verdict();
}

//===========================================================================
// --spread: with one coil, spikes where the local M passes M_c, and not past.
//===========================================================================
int runSpread( const Perturb& perturb )
{
	std::printf( "\n=== spread: with Magnet = Coil the patterned radius is where the local magnetisation crosses M_c (to a spike's spacing), "
	             "and grows with the current\n" );
	const physics::Datasheet& fluid = physics::FluidOf( physics::FluidId::EFH1 );
	const Reference ref             = referenceOf( fluid );
	const double lambda             = physics::CriticalWavelength( fluid );
	const double gc                 = 2.0 * std::sqrt( fluid.density * physics::kGravity * fluid.tension );
	const double radius             = 0.03;
	double last                     = 0.0;
	for( double multiple : { 1.2, 1.4 } )
	{
		Rig rig;
		const Raster& raster = kRasters.back();
		if( !startRig( rig, raster.w, raster.h, perturb ) )
			return 1;
		rig.Set( PT_MAGNET, static_cast< float >( Magnet::Coil ) );
		rig.Set( PT_COIL_SIZE, ParamFromCoilSize( radius ) );
		rig.plugin.SetModelForTest( physics::Nonlinear { 0.0, 1.0 } );//supercritical: no fronts to invade the bistable ring
		rig.Set( PT_BIAS, ParamFromCurrent( multiple ) );
		rig.Set( PT_COIL_LAG, ParamFromLag( 0.001 ) );
		rig.Set( PT_NOISE, ParamFromNoise( 1e-3 ) );
		if( !rig.Render( 360 ) )
			return 1;
		//The predicted radius: the harness's own field (400 turns of
		//Biot-Savart) at the current in amps, its own G, bisected for G_c.
		const double amps = rig.plugin.CriticalCurrent() * multiple;
		auto gAt          = [ & ]( double r ) {
			double field = 0.0;
			for( int turn = 0; turn < 400; ++turn )
				field += biotSavart( radius, r, coil::kSolenoidDepth + ( turn + 0.5 ) * coil::kSolenoidLength / 400.0 );
			return ref.stress( field * amps );
		};
		double lo = 0.0, hi = 0.15;
		for( int i = 0; i < 40; ++i )
		{
			const double mid = 0.5 * ( lo + hi );
			( gAt( mid ) > gc ? lo : hi ) = mid;
		}
		const double predicted = 0.5 * ( lo + hi );

		//The measured radius: the outermost spike, a local maximum taller than a
		//quarter of the tallest. (A ring's height would count the capillary
		//waves the pattern radiated while it grew, which cross the whole
		//periodic pool at a twentieth of a millimetre.) A spike's centre sits
		//within a spacing of the threshold: the outermost ring of a lattice is
		//up to one spacing inside it, and its spikes' bodies half one outside.
		const std::vector< double > h = poolHeights( rig );
		const Pool& p                 = rig.plugin.CurrentPool();
		double tallest = 0.0;
		for( double v : h )
			tallest = std::max( tallest, v );
		double measured = 0.0;
		for( int j = 0; j < p.ny; ++j )
			for( int i = 0; i < p.nx; ++i )
			{
				const double v = h[ static_cast< size_t >( j ) * p.nx + i ];
				if( v < 0.25 * tallest )
					continue;
				bool top = true;
				for( int dj = -1; dj <= 1 && top; ++dj )
					for( int di = -1; di <= 1 && top; ++di )
						if( ( di || dj ) && h[ static_cast< size_t >( ( j + dj + p.ny ) % p.ny ) * p.nx + ( i + di + p.nx ) % p.nx ] > v )
							top = false;
				if( !top )
					continue;
				const double x = ( i + 0.5 ) / p.nx * p.lx - 0.5 * p.lx, y = ( j + 0.5 ) / p.ny * p.ly - 0.5 * p.ly;
				measured = std::max( measured, std::hypot( x, y ) );
			}
		const double centre = tallest;
		Check( centre > 0.1 / physics::CriticalWavenumber( fluid ) && std::fabs( measured - predicted ) <= lambda,
		       fmt( "%.1f Ic (%.2f A): the outermost spike stands %.1f mm from the axis, the local M crosses M_c at %.1f mm (tolerance lambda_c "
		            "%.1f mm); the tallest %.2f mm",
		            multiple, amps, measured * 1e3, predicted * 1e3, lambda * 1e3, centre * 1e3 ) );
		last = measured;
	}
	return Verdict();
}

//===========================================================================
// --mirror: a flat pool, Fresnel set to 1, is the clip, to the pixel.
//===========================================================================
int runMirror( const Perturb& perturb )
{
	std::printf( "\n=== mirror: the Over, straight down at a flat pool with the reflectance forced to 1, returns the clip to the pixel; "
	             "at Mix 0 it returns it bit for bit\n" );
	for( const Raster& raster : kRasters )
	{
		const Floats card = noiseCard( raster.w, raster.h );
		Rig rig( true );
		if( !startRig( rig, raster.w, raster.h, perturb, &card ) )
			return 1;
		rig.plugin.SetHooksForTest( perturb.hooks | shaders::kHookFresnelOne );
		rig.Set( PT_HEIGHT, ParamFromHeight( 0.25 ) );
		if( !rig.Render( 20 ) )
			return 1;
		const Floats out = rig.Output();
		int differ = 0;
		for( size_t i = 0; i < out.size(); ++i )
			differ += byteOf( out[ i ] ) != byteOf( card[ i ] );
		Check( differ == 0, fmt( "%dx%d: %d of %zu channels differ in 8 bits (every pixel of the card is different from its neighbours)", raster.w,
		                         raster.h, differ, out.size() ) );
		rig.Set( PT_MIX, 0.0f );
		if( !rig.Render( 1 ) )
			return 1;
		const Floats dry = rig.Output();
		int exact        = 0;
		for( size_t i = 0; i < dry.size(); ++i )
			exact += dry[ i ] != card[ i ];
		Check( exact == 0, fmt( "%dx%d, Mix 0: %d floats differ from the clip", raster.w, raster.h, exact ) );
	}
	return Verdict();
}

//===========================================================================
// --fresnel: the reflectance the camera sees, against the closed form.
//===========================================================================
int runFresnel( const Perturb& perturb )
{
	std::printf( "\n=== fresnel: a flat pool under a uniform sky shows each pixel's reflectance over R0; at normal incidence R0 is "
	             "( ( n - 1 )^2 + k^2 ) / ( ( n + 1 )^2 + k^2 ), and at 35-55 degrees the exact unpolarised formula for n + i k\n" );
	const physics::Optics optics = physics::OpticsOf( physics::FluidOf( physics::FluidId::EFH1 ) );
	const double r0              = ( std::pow( optics.n - 1.0, 2 ) + optics.k * optics.k ) / ( std::pow( optics.n + 1.0, 2 ) + optics.k * optics.k );
	auto exact = [ & ]( double ci ) {
		const Cplx N( optics.n, optics.k ), N2 = N * N;
		const Cplx w  = std::sqrt( N2 - Cplx( 1.0 - ci * ci, 0.0 ) );
		const Cplx rs = ( ci - w ) / ( ci + w ), rp = ( N2 * ci - w ) / ( N2 * ci + w );
		return 0.5 * ( std::norm( rs ) + std::norm( rp ) );
	};
	auto linearOf = []( double code ) { return code <= 0.04045 ? code / 12.92 : std::pow( ( code + 0.055 ) / 1.055, 2.4 ); };
	for( const Raster& raster : kRasters )
		for( double tilt : { 0.0, 45.0 } )
		{
			Rig rig;
			if( !startRig( rig, raster.w, raster.h, perturb ) )
				return 1;
			rig.plugin.SetHooksForTest( perturb.hooks | shaders::kHookUniformSky );
			rig.Set( PT_TILT, ParamFromTilt( tilt * kPi / 180.0 ) );
			rig.Set( PT_EXPOSURE, ParamFromExposure( -1.0 ) );
			if( !rig.Render( 3 ) )
				return 1;
			const Floats out = rig.Output();
			const double th  = tilt * kPi / 180.0, tx = rig.plugin.LensTanHalf(), ty = tx * raster.h / raster.w;
			double worst     = 0.0, lowest = 90.0, highest = 0.0;
			for( int y = 0; y < raster.h; y += std::max( 1, raster.h / 9 ) )
				for( int x = 0; x < raster.w; x += std::max( 1, raster.w / 16 ) )
				{
					//The pixel's ray, here in double: forward ( 0, sin, -cos ),
					//up ( 0, cos, sin ), right ( 1, 0, 0 ).
					const double u = ( x + 0.5 ) / raster.w, v = ( y + 0.5 ) / raster.h;
					const double dx = ( 2.0 * u - 1.0 ) * tx, dup = ( 2.0 * v - 1.0 ) * ty;
					const double d[ 3 ] = { dx, std::sin( th ) + dup * std::cos( th ), -std::cos( th ) + dup * std::sin( th ) };
					const double ci     = -d[ 2 ] / std::sqrt( d[ 0 ] * d[ 0 ] + d[ 1 ] * d[ 1 ] + d[ 2 ] * d[ 2 ] );
					const double want   = exact( ci ) / r0;
					const double got    = 2.0 * linearOf( out[ ( static_cast< size_t >( y ) * raster.w + x ) * 4 ] );
					worst               = std::max( worst, std::fabs( got - want ) / want );
					lowest              = std::min( lowest, std::acos( ci ) * 180.0 / kPi );
					highest             = std::max( highest, std::acos( ci ) * 180.0 / kPi );
				}
			//float complex arithmetic and sRGB's pow both ways: a few 1e-6; the
			//s term alone is 40% off at 45 degrees.
			Check( worst < 2e-4, fmt( "%dx%d, Tilt %.0f: incidence %.1f to %.1f degrees, R / R0 against the exact formula to %.1e (R0 = %.5f; bound 2e-4)",
			                          raster.w, raster.h, tilt, lowest, highest, worst, r0 ) );
		}
	return Verdict();
}

//===========================================================================
// --beat (no GL): a pulse starts where its beat fell inside its frame.
//===========================================================================
int runBeat( const Perturb& perturb )
{
	std::printf( "\n=== beat: Beat, Half Beat, 2 Beats and Bar, at 120 and 97 BPM, on the host's bar phase: every pulse starts at its beat, to the clock\n" );
	struct Rate
	{
		FireOn on;
		double beats;
		const char* name;
	};
	const Rate rates[] = { { FireOn::Beat, 1.0, "Beat" }, { FireOn::HalfBeat, 0.5, "Half Beat" }, { FireOn::TwoBeats, 2.0, "2 Beats" }, { FireOn::Bar, 4.0, "Bar" } };
	for( double bpm : { 120.0, 97.0 } )
		for( const Rate& rate : rates )
		{
			Drive drive;
			drive.SetHooksForTest( perturb.driveHooks );
			DriveInputs in;
			in.fireOn   = rate.on;
			in.hostBeat = true;
			in.bpm      = bpm;
			in.pulse    = 0.001;
			std::vector< double > starts;
			for( int f = 0; f <= 600; ++f )
			{
				const double t = f / 60.0;
				in.barPhase    = std::fmod( t * bpm / 240.0 + 0.1, 1.0 );//the bar does not start at 0
				drive.Advance( t, f == 0 ? 0.0 : 1.0 / 60.0, in );
				for( const Drive::Pulse& p : drive.Pulses() )
					if( starts.empty() || p.start > starts.back() + 1e-9 )
						starts.push_back( p.start );
			}
			//Beat n of the rate is at phase n * step, which the bar reaches at
			//( n * step - 0.1 ) * 240 / bpm.
			const double period = rate.beats * 60.0 / bpm;
			double worst        = 0.0;
			for( double s : starts )
			{
				const double n = std::round( ( s + 0.1 * 240.0 / bpm ) / period );
				worst          = std::max( worst, std::fabs( s - ( n * period - 0.1 * 240.0 / bpm ) ) );
			}
			const size_t expected = static_cast< size_t >( std::floor( ( 10.0 + 0.1 * 240.0 / bpm ) / period ) - std::floor( ( 0.1 * 240.0 / bpm ) / period ) );
			Check( worst < 1e-9 && ( starts.size() == expected || starts.size() + 1 == expected ),
			       fmt( "%s at %.0f BPM: %zu pulses in 10 s (%zu due), every one within %.1e s of its beat", rate.name, bpm, starts.size(), expected, worst ) );
		}
	return Verdict();
}

//===========================================================================
// --audio: onsets fire pulses, primed on the first frame; a coil follows its band.
//===========================================================================
int runAudio( const Perturb& perturb )
{
	std::printf( "\n=== audio: an onset fires a pulse on the kick's own frame, none from the cold start; with no Fire On the coil follows the "
	             "level of its band\n" );
	const unsigned int bins = audio::kBins;
	{
		Rig rig;
		if( !startRig( rig, 64, 36, perturb ) )
			return 1;
		rig.Set( PT_FIRE_ON, static_cast< float >( FireOn::Onset ) );
		rig.Set( PT_GAIN, ParamFromCurrent( 0.5 ) );
		rig.Set( PT_THRESHOLD, 0.4f );
		std::vector< int > kicks, fired;
		unsigned long long before = 0;
		const unsigned int host   = static_cast< unsigned int >( rig.plugin.HostIndexOf( PT_AUDIO ) );
		rig.beforeFrame = [ & ]( int f ) {
			//A bed of hi-hat and a kick every half second from frame 20; the
			//first two frames already loud (a clip triggered mid-song).
			const int since = ( f - 20 ) % 30;
			for( unsigned int bin = 0; bin < bins; ++bin )
			{
				double v = bin > 40 ? 0.05 : 0.02;
				if( f >= 20 && since < 4 && bin < 10 )
					v += 0.9 * std::exp( -since * 0.8 );
				if( f < 2 )
					v += 0.3;
				rig.plugin.SetParamElementValue( host, bin, static_cast< float >( v ) );
			}
			if( f >= 20 && since == 0 )
				kicks.push_back( f );
		};
		for( int f = 0; f < 200; ++f )
		{
			if( !rig.Render( 1 ) )
				return 1;
			if( rig.plugin.DriveState().Fired() > before )
				fired.push_back( f );
			before = rig.plugin.DriveState().Fired();
		}
		int matched = 0;
		for( int k : kicks )
			matched += std::count( fired.begin(), fired.end(), k ) > 0;
		const bool early = !fired.empty() && fired.front() < 20;
		Check( matched == static_cast< int >( kicks.size() ) && fired.size() == kicks.size() && !early,
		       fmt( "%zu kicks, %zu pulses, %d on the kick's own frame; %s before the first kick", kicks.size(), fired.size(), matched,
		            early ? "one FIRED" : "none" ) );
	}
	{
		//No Fire On: the coil's target is Bias + Gain x its band's level, each
		//band normalised against its own recent peak. A kick in the low bins
		//swings Low between kicks; a steady High band reads steady.
		double low = 0.0, high = 0.0;
		for( Band band : { Band::Low, Band::High } )
		{
			Rig rig;
			if( !startRig( rig, 64, 36, perturb ) )
				return 1;
			rig.Set( PT_FIRE_ON, static_cast< float >( FireOn::Off ) );
			rig.Set( PT_BAND, static_cast< float >( band ) );
			rig.Set( PT_GAIN, ParamFromCurrent( 1.0 ) );
			const unsigned int host = static_cast< unsigned int >( rig.plugin.HostIndexOf( PT_AUDIO ) );
			double top = 0.0, bottom = 1e9;
			for( int f = 0; f < 150; ++f )
			{
				const bool kick = f % 30 < 3;
				for( unsigned int bin = 0; bin < bins; ++bin )
					rig.plugin.SetParamElementValue( host, bin, static_cast< float >( bin < 4 ? ( kick ? 0.8 : 0.05 ) : ( bin > 16 ? 0.1 : 0.02 ) ) );
				if( !rig.Render( 1 ) )
					return 1;
				if( f >= 60 )
				{
					top    = std::max( top, rig.plugin.DriveState().Current( 0 ) );
					bottom = std::min( bottom, rig.plugin.DriveState().Current( 0 ) );
				}
			}
			( band == Band::Low ? low : high ) = top - bottom;
		}
		Check( low > 0.3 && high < 0.1 * low, fmt( "kicks in the low bins over a steady top: the coil swings %.2f Ic on Band Low, %.3f Ic on Band High",
		                                           low, high ) );
	}
	return Verdict();
}

//===========================================================================
// --resize: the pool lives in metres, not pixels.
//===========================================================================
int runResize( const Perturb& perturb )
{
	std::printf( "\n=== resize: after the host's raster changes (and its shape), the picture is exactly a fresh instance's that saw the same frames\n" );
	const Raster small = kRasters.back(), big = { 2 * small.w, 2 * small.h }, tall = { small.w, small.w * 3 / 4 };
	for( const Raster& to : { small, tall } )
	{
		auto configure = []( Rig& rig ) {
			rig.bpm = 120.0;
			rig.Set( PT_NOISE, ParamFromNoise( 1e-3 ) );
			rig.plugin.SetModelForTest( physics::Nonlinear { 2.0, 0.7 } );
			rig.Set( PT_BIAS, ParamFromCurrent( 1.05 ) );
		};
		Rig resized;
		if( !startRig( resized, big.w, big.h, perturb ) )
			return 1;
		configure( resized );
		if( !resized.Render( 70 ) || !resized.Resize( to.w, to.h ) || !resized.Render( 3 ) )
			return 1;
		Rig fresh;
		if( !startRig( fresh, to.w, to.h, Perturb {} ) )
			return 1;
		configure( fresh );
		if( !fresh.Render( 73 ) )
			return 1;
		const Floats x = resized.Output(), y = fresh.Output();
		int differ = 0;
		for( size_t i = 0; i < x.size() && i < y.size(); ++i )
			differ += byteOf( x[ i ] ) != byteOf( y[ i ] );
		const double tallestH = maxAbs( poolHeights( fresh ) );
		Check( differ == 0 && tallestH > 1e-4, fmt( "%dx%d for 70 frames, then %dx%d for 3, against %dx%d for all 73: %d bytes differ (spikes %.2f mm)",
		                                            big.w, big.h, to.w, to.h, to.w, to.h, differ, tallestH * 1e3 ) );
	}
	return Verdict();
}

//===========================================================================
// --state: the GL state the host hands over is the state it gets back.
//===========================================================================
int runState( const Perturb& )
{
	std::printf( "\n=== state: the GL state the host hands over is the state it gets back, both plugins\n" );
	for( bool over : { false, true } )
	{
		Rig rig( over );
		if( !rig.Init( 320, 180 ) )
			return 1;
		rig.Set( PT_MAGNET, static_cast< float >( Magnet::ThreeCoils ) );
		GLuint hostArray = 0, hostBuffer = 0;
		glGenVertexArrays( 1, &hostArray );
		glGenBuffers( 1, &hostBuffer );
		int problems = 0;
		std::string what;
		for( int frame = 0; frame < 6; ++frame )
		{
			rig.Set( PT_FIRE, frame == 1 ? 1.0f : 0.0f );
			glBindFramebuffer( GL_FRAMEBUFFER, rig.outputFBO );
			glViewport( 0, 0, 320, 180 );
			glBindVertexArray( hostArray );
			glBindBuffer( GL_ARRAY_BUFFER, hostBuffer );
			glEnable( GL_BLEND );
			glBlendFuncSeparate( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO );
			glClearColor( 0.2f, 0.3f, 0.4f, 0.5f );
			glEnable( GL_SCISSOR_TEST );
			glScissor( 0, 0, 320, 180 );
			glActiveTexture( GL_TEXTURE0 );
			glUseProgram( 0 );
			rig.plugin.SetTime( frame / 60.0 );
			if( rig.plugin.ProcessOpenGL( &rig.process ) != FF_SUCCESS )
				return 1;
			GLint viewport[ 4 ] = {}, array = 0, buffer = 0, program = 0, unit = 0, fbo = 0, src = 0, dst = 0;
			GLfloat clear[ 4 ]   = {};
			GLboolean mask[ 4 ]  = {};
			glGetIntegerv( GL_VIEWPORT, viewport );
			glGetIntegerv( GL_VERTEX_ARRAY_BINDING, &array );
			glGetIntegerv( GL_ARRAY_BUFFER_BINDING, &buffer );
			glGetIntegerv( GL_CURRENT_PROGRAM, &program );
			glGetIntegerv( GL_ACTIVE_TEXTURE, &unit );
			glGetIntegerv( GL_FRAMEBUFFER_BINDING, &fbo );
			glGetIntegerv( GL_BLEND_SRC_RGB, &src );
			glGetIntegerv( GL_BLEND_DST_RGB, &dst );
			glGetFloatv( GL_COLOR_CLEAR_VALUE, clear );
			glGetBooleanv( GL_COLOR_WRITEMASK, mask );
			auto expect = [ & ]( bool ok, const char* name ) {
				if( !ok )
				{
					++problems;
					what += std::string( " " ) + name;
				}
			};
			expect( viewport[ 0 ] == 0 && viewport[ 1 ] == 0 && viewport[ 2 ] == 320 && viewport[ 3 ] == 180, "viewport" );
			expect( array == static_cast< GLint >( hostArray ), "vertex-array" );
			expect( buffer == static_cast< GLint >( hostBuffer ), "array-buffer" );
			expect( program == 0, "program" );
			expect( unit == GL_TEXTURE0, "active-unit" );
			expect( fbo == static_cast< GLint >( rig.outputFBO ), "framebuffer" );
			expect( glIsEnabled( GL_BLEND ) && src == GL_SRC_ALPHA && dst == GL_ONE_MINUS_SRC_ALPHA, "blend" );
			expect( glIsEnabled( GL_SCISSOR_TEST ), "scissor" );
			expect( clear[ 0 ] == 0.2f && clear[ 1 ] == 0.3f && clear[ 2 ] == 0.4f && clear[ 3 ] == 0.5f, "clear-colour" );
			expect( mask[ 0 ] && mask[ 1 ] && mask[ 2 ] && mask[ 3 ], "colour-mask" );
			for( int u = 0; u < 8; ++u )
			{
				GLint bound = 0;
				glActiveTexture( static_cast< GLenum >( GL_TEXTURE0 + u ) );
				glGetIntegerv( GL_TEXTURE_BINDING_2D, &bound );
				expect( bound == 0, "texture-unit" );
			}
			glActiveTexture( GL_TEXTURE0 );
		}
		glDisable( GL_SCISSOR_TEST );
		glDisable( GL_BLEND );
		glBindVertexArray( 0 );
		glBindBuffer( GL_ARRAY_BUFFER, 0 );
		glDeleteVertexArrays( 1, &hostArray );
		glDeleteBuffers( 1, &hostBuffer );
		Check( problems == 0, fmt( "%s, six frames of Three Coils and a Fire: viewport, vertex array, array buffer, program, active unit, framebuffer, "
		                           "blend, scissor, clear colour, colour mask, eight texture units (%d wrong:%s)",
		                           over ? "SW Ferro Over" : "SW Ferrofluid", problems, what.empty() ? " none" : what.c_str() ) );
	}
	return Verdict();
}

//===========================================================================
// --extremes: every corner of the controls stays a pool.
//===========================================================================
int runExtremes( const Perturb& perturb )
{
	std::printf( "\n=== extremes: the controls' corners -- the drive at its ceiling, the model at both ends, each magnet, each fluid -- leave a "
	             "bounded surface (finite, and under half as much again as the model's own height scale) after 4 s\n" );
	struct Corner
	{
		const char* what;
		std::function< void( Rig& ) > set;
	};
	const Corner corners[] = {
		{ "Uniform EFH1, the drive at its 1.6 Ic ceiling on every beat", []( Rig& r ) {
			 r.Set( PT_MAGNET, 0.0f );
			 r.Set( PT_BIAS, 1.0f );
			 r.Set( PT_GAIN, 1.0f );
		 } },
		{ "Three Coils, 8 cm, 2 cm apart, 1.5 Ic each, EFH1", []( Rig& r ) {
			 r.Set( PT_MAGNET, 2.0f );
			 r.Set( PT_COIL_SIZE, 1.0f );
			 r.Set( PT_COIL_SPREAD, 0.0f );
			 r.Set( PT_BIAS, 1.0f );
		 } },
		{ "Coil, 5 mm, the ceiling, Viscosity 100x, EMG 909", []( Rig& r ) {
			 r.Set( PT_MAGNET, 1.0f );
			 r.Set( PT_COIL_SIZE, 0.0f );
			 r.Set( PT_BIAS, 1.0f );
			 r.Set( PT_GAIN, 1.0f );
			 r.Set( PT_VISCOSITY, 1.0f );
			 r.Set( PT_FLUID, 2.0f );
		 } },
		{ "Uniform EMG 901 at 1.5 Ic, Viscosity 0.25x, Noise at its top", []( Rig& r ) {
			 r.Set( PT_MAGNET, 0.0f );
			 r.Set( PT_FLUID, 1.0f );
			 r.Set( PT_BIAS, 1.0f );
			 r.Set( PT_VISCOSITY, 0.0f );
			 r.Set( PT_NOISE, 1.0f );
		 } },
	};
	for( const Corner& corner : corners )
	{
		Rig rig;
		rig.bpm = 120.0;
		if( !rig.Init( 160, 90 ) )
			return 1;
		rig.plugin.SetHooksForTest( perturb.hooks );
		rig.plugin.SetSubstepScaleForTest( perturb.substepScale );
		corner.set( rig );
		if( !rig.Render( 240 ) )
			return 1;
		const std::vector< double > h = poolHeights( rig );
		bool finite                   = true;
		for( double v : h )
			finite = finite && std::isfinite( v );
		//The model's own scale: its energy, local in W h, has its minimum at
		//W h = Hy / Sa, and a spike stands about that tall over its trough.
		//Half as much again is the bound; a blow-up is orders past it.
		const double tallest = maxAbs( h );
		const physics::Nonlinear model = rig.plugin.Model();
		const double bound   = 1.5 * ( 1.0 + model.hysteresis / model.saturation ) / physics::CriticalWavenumber( rig.plugin.Fluid() );
		Check( finite && tallest < bound, fmt( "%s: tallest %.2f mm (bound %.1f mm), %d substeps of %.3f ms", corner.what, tallest * 1e3, bound * 1e3,
		                                       rig.plugin.LastPlan().substeps, rig.plugin.LastPlan().dt * 1e3 ) );
	}
	return Verdict();
}
