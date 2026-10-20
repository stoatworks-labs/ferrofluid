//===========================================================================
// The solved spikes: the table placed at the pool's spikes, checked out of
// the height field the camera reads and out of the picture.
//===========================================================================

/// A box near the shipping cell size (twelve to a wavelength) that holds the
/// hexagonal lattice exactly: k1 = ( k_c, 0 ) is mode ( 22, 0 ) and
/// k2,3 = ( -k_c / 2, +-sqrt 3 k_c / 2 ) are modes ( -11, +-18 ).
Pool spikePool( const physics::Datasheet& fluid )
{
	const double lambda = physics::CriticalWavelength( fluid );
	return Pool { 256, 256, 22.0 * lambda, 36.0 * lambda / std::sqrt( 3.0 ) };
}

/// The three lattice modes, each A cos, phased so that a spike stands at the
/// world's origin (the pool's centre, where the camera looks): 3 A high there.
/// (singleMode's convention, not Hexagons' 2 A cos.)
Floats latticeSeed( const Pool& p, double amplitude )
{
	Floats s( static_cast< size_t >( p.nx ) * p.ny * 4, 0.0f );
	//sample i sits at world x = ( i + 0.5 ) dx - Lx / 2: the origin is i = nx / 2 - 0.5
	const double x0 = ( p.nx / 2 - 0.5 ) * p.lx / p.nx, y0 = ( p.ny / 2 - 0.5 ) * p.ly / p.ny;
	const int modes[ 3 ][ 2 ] = { { 22, 0 }, { -11, 18 }, { -11, -18 } };
	const double a            = amplitude * p.nx * p.ny / 2.0;
	for( const auto& m : modes )
		for( int sign : { 1, -1 } )
		{
			const int mx = sign * m[ 0 ], my = sign * m[ 1 ];
			const double kx = 2.0 * kPi * mx / p.lx, ky = 2.0 * kPi * my / p.ly;
			const double phase = -( kx * x0 + ky * y0 );
			const int i = ( mx % p.nx + p.nx ) % p.nx, j = ( my % p.ny + p.ny ) % p.ny;
			s[ ( static_cast< size_t >( j ) * p.nx + i ) * 4 + 0 ] += static_cast< float >( a * std::cos( phase ) );
			s[ ( static_cast< size_t >( j ) * p.nx + i ) * 4 + 1 ] += static_cast< float >( a * std::sin( phase ) );
		}
	return s;
}

/// The table's hexagonal cell at d from a spike: the sum over the reciprocal
/// lattice of the kernel's transform over the cell's area. Metres. Double.
double tableCell( const spikes::Table& t, double index, double kc, double dx, double dy )
{
	const double a     = 4.0 * kPi / ( std::sqrt( 3.0 ) * kc );
	const double area  = std::sqrt( 3.0 ) / 2.0 * a * a;
	const double k1[ 2 ] = { kc, 0.0 }, k2[ 2 ] = { -0.5 * kc, 0.5 * std::sqrt( 3.0 ) * kc };
	const int reach = static_cast< int >( std::ceil( t.qMax * 2.0 ) ) + 1;
	double sum      = 0.0;
	for( int m = -reach; m <= reach; ++m )
		for( int n = -reach; n <= reach; ++n )
		{
			if( m == 0 && n == 0 )
				continue;
			const double gx = m * k1[ 0 ] + n * k2[ 0 ], gy = m * k1[ 1 ] + n * k2[ 1 ];
			const double q  = std::hypot( gx, gy ) / kc;
			if( q >= t.qMax )
				continue;
			sum += spikes::KernelHat( t, index, q ) * std::cos( gx * dx + gy * dy );
		}
	return sum / area;
}

/// The table cell's tip curvature, -laplacian at the spike, 1/m (its tip
/// radius is 2 over it).
double tableTipCurvature( const spikes::Table& t, double index, double kc )
{
	const double a     = 4.0 * kPi / ( std::sqrt( 3.0 ) * kc );
	const double area  = std::sqrt( 3.0 ) / 2.0 * a * a;
	const double k1[ 2 ] = { kc, 0.0 }, k2[ 2 ] = { -0.5 * kc, 0.5 * std::sqrt( 3.0 ) * kc };
	const int reach = static_cast< int >( std::ceil( t.qMax * 2.0 ) ) + 1;
	double sum      = 0.0;
	for( int m = -reach; m <= reach; ++m )
		for( int n = -reach; n <= reach; ++n )
		{
			const double gx = m * k1[ 0 ] + n * k2[ 0 ], gy = m * k1[ 1 ] + n * k2[ 1 ];
			const double q  = std::hypot( gx, gy ) / kc;
			if( q == 0.0 || q >= t.qMax )
				continue;
			sum += spikes::KernelHat( t, index, q ) * ( gx * gx + gy * gy );
		}
	return sum / area;
}

struct Spike
{
	double x, y, peak;///< texel frame, metres; the pool's peak
	int i, j;
};

std::vector< Spike > spikesOf( Rig& rig )
{
	const int w = rig.plugin.SurfaceWidth(), h = rig.plugin.SurfaceHeight();
	const Floats where = readTexture( rig.plugin.SpikeWhereTextureID(), w, h );
	const Pool& p      = rig.plugin.CurrentPool();
	const double cx = p.lx / w, cy = p.ly / h;
	std::vector< Spike > out;
	for( int j = 0; j < h; ++j )
		for( int i = 0; i < w; ++i )
		{
			const float* v = &where[ ( static_cast< size_t >( j ) * w + i ) * 4 ];
			if( v[ 2 ] > 0.5f )
				out.push_back( { ( i + 0.5 ) * cx + v[ 0 ], ( j + 0.5 ) * cy + v[ 1 ], v[ 3 ], i, j } );
		}
	return out;
}

/// A uniform pool in the spike box at u = Ha / Ha_c, seeded with the lattice at
/// `seedPeak` (metres, the three modes' sum at a spike).
bool latticeRig( Rig& rig, int w, int h, const Perturb& perturb, int fluid, double u, double seedPeak )
{
	if( !startRig( rig, w, h, perturb ) )
		return false;
	//The pattern engine as it ships (quiet() sets Hy 0, Sa 2): its stress is
	//held off below, but its equilibrium is the blend's denominator.
	rig.plugin.ClearModelForTest();
	rig.plugin.SetPoolForTest( spikePool( physics::FluidOf( static_cast< physics::FluidId >( fluid ) ) ) );
	rig.Set( PT_FLUID, static_cast< float >( fluid ) );
	rig.Set( PT_MAGNET, static_cast< float >( Magnet::Uniform ) );
	rig.Set( PT_BIAS, ParamFromCurrent( u ) );
	rig.Set( PT_GAIN, 0.0f );
	rig.Set( PT_COIL_LAG, ParamFromLag( 0.001 ) );
	rig.Set( PT_NOISE, 0.0f );
	rig.Set( PT_FIRE_ON, static_cast< float >( FireOn::Off ) );
	//Two frames before the lattice goes in: the first has no time (dt 0), and
	//the second brings the coil up from cold. Loaded into that ramp, a lattice
	//at onset sat under it for three substeps and lost a quarter of its height.
	if( !rig.Render( 2 ) )
		return false;
	//One frame with the pattern engine's own stress off: the lattice stays
	//three cosines (the linear step only scales them), so its spikes stand
	//exactly on the lattice and nowhere else.
	rig.plugin.SetHooksForTest( perturb.hooks | shaders::kHookNoKick );
	rig.plugin.LoadStateForTest( latticeSeed( rig.plugin.CurrentPool(), seedPeak / 3.0 ) );
	return rig.Render( 1 );
}

/// The pool's own equilibrium peak at u, as the table ships it for the blend.
double poolEquilibrium( const spikes::Table& t, double u )
{
	const double f = std::clamp( ( u - t.eqU0 ) / ( t.eqU1 - t.eqU0 ) * ( t.nEq - 1 ), 0.0, static_cast< double >( t.nEq - 1 ) );
	const int i    = std::min( static_cast< int >( f ), t.nEq - 2 );
	return t.eqPeak[ i ] + ( f - i ) * ( t.eqPeak[ i + 1 ] - t.eqPeak[ i ] );
}

//===========================================================================
// --spikes: at equilibrium, a lattice of the pool's spikes is the table's cell.
//===========================================================================
int runSpikes( const Perturb& perturb )
{
	std::printf( "\n=== spikes: a lattice of the pool's spikes at equilibrium, read from the height field the camera reads, is the table's "
	             "cell (the solver's, band-limited to 8.19 k_c), its peak and its tip; and out of the picture\n" );
	for( int fluid = 0; fluid < spikes::kTableCount; ++fluid )
	{
		const spikes::Table& t          = spikes::TableFor( fluid );
		const physics::Datasheet& datum = physics::FluidOf( static_cast< physics::FluidId >( fluid ) );
		const double kc                 = physics::CriticalWavenumber( datum );
		//Bias reaches 1.5 I_c; the table ends where the solver did
		const double top = std::min( 1.5, t.uEnd - 0.005 );
		for( double u : { 1.0, 0.5 * ( 1.0 + top ), top } )
		{
			const double index = spikes::GridIndex( t, u );
			const double peak  = tableCell( t, index, kc, 0.0, 0.0 );
			for( const Raster& raster : kRasters )
			{
				Rig rig;
				//Seeded above the pool's own equilibrium: fully grown, so the blend is 1.
				if( !latticeRig( rig, raster.w, raster.h, perturb, fluid, u, 1.5 * poolEquilibrium( t, u ) ) )
					return 1;
				const std::vector< Spike > all = spikesOf( rig );
				const Floats surface = rig.Surface();
				const int w = rig.plugin.SurfaceWidth(), h = rig.plugin.SurfaceHeight();
				const Pool& p = rig.plugin.CurrentPool();
				const double cx = p.lx / w, cy = p.ly / h;
				//the spike nearest the box's middle
				const Spike* s = nullptr;
				double best    = 1e9;
				for( const Spike& sp : all )
				{
					const double d = std::hypot( sp.x - 0.5 * p.lx, sp.y - 0.5 * p.ly );
					if( d < best )
					{
						best = d;
						s    = &sp;
					}
				}
				if( s == nullptr )
				{
					Check( false, fmt( "%s u %.3f %dx%d: no spikes found", t.fluid, u, raster.w, raster.h ) );
					continue;
				}
				//every texel within one lattice constant of it, against the cell
				const double a = 4.0 * kPi / ( std::sqrt( 3.0 ) * kc );
				double worst = 0.0;
				int counted  = 0;
				for( int dj = -40; dj <= 40; ++dj )
					for( int di = -40; di <= 40; ++di )
					{
						const int i = s->i + di, j = s->j + dj;
						const double x = ( i + 0.5 ) * cx, y = ( j + 0.5 ) * cy;
						const double dx = x - s->x, dy = y - s->y;
						if( std::hypot( dx, dy ) > a )
							continue;
						const int iw = ( i % w + w ) % w, jw = ( j % h + h ) % h;
						const double got = surface[ ( static_cast< size_t >( jw ) * w + iw ) * 4 ];
						worst = std::max( worst, std::fabs( got - tableCell( t, index, kc, dx, dy ) ) );
						++counted;
					}
				Check( worst < 2e-3 * peak && all.size() > 50,
				       fmt( "%s u %.3f, %dx%d: %zu spikes; %d texels round one against the table's cell, worst %.1e of its %.2f mm peak (bound 2e-3)",
				            t.fluid, u, raster.w, raster.h, all.size(), counted, worst / peak, peak * 1e3 ) );
			}
			//Out of the picture: straight down, a 2.5 lambda field, every pixel
			//where its ray met the surface.
			Rig rig;
			if( !latticeRig( rig, 1280, 720, perturb, fluid, u, 1.5 * poolEquilibrium( t, u ) ) )
				return 1;
			rig.plugin.SetHooksForTest( perturb.hooks | shaders::kHookHeightView | shaders::kHookNoKick );
			rig.Set( PT_TILT, 0.0f );
			rig.Set( PT_FIELD_WIDTH, ParamFromFieldWidth( 2.5 * physics::CriticalWavelength( datum ) ) );
			if( !rig.Render( 1 ) )
				return 1;
			const Floats pic = rig.Output();
			double top = -1e9, bottom = 1e9;
			for( size_t k = 0; k < pic.size(); k += 4 )
			{
				top    = std::max( top, static_cast< double >( pic[ k + 2 ] ) );
				bottom = std::min( bottom, static_cast< double >( pic[ k + 2 ] ) );
			}
			//Its width at half its height above the corners, along the row of
			//pixels through the summit, against the cell's.
			const double a      = 4.0 * kPi / ( std::sqrt( 3.0 ) * kc );
			const double corner = tableCell( t, index, kc, a / std::sqrt( 3.0 ), 0.0 );
			const double half   = 0.5 * ( peak + corner );
			double tableHalf = 0.0;
			{
				double lo = 0.0, hi = a / std::sqrt( 3.0 );
				for( int k = 0; k < 60; ++k )
				{
					const double mid = 0.5 * ( lo + hi );
					( tableCell( t, index, kc, mid, 0.0 ) > half ? lo : hi ) = mid;
				}
				tableHalf = lo + hi;//both sides: the full width
			}
			//the picture's row through the summit: the pixel row of the highest pixel
			size_t best = 0;
			for( size_t k = 0; k < pic.size(); k += 4 )
				if( pic[ k + 2 ] >= pic[ best + 2 ] )
					best = k;
			const int W = 1280;
			const int row = static_cast< int >( best / 4 ) / W, col = static_cast< int >( best / 4 ) % W;
			auto z = [ & ]( int x ) { return static_cast< double >( pic[ ( static_cast< size_t >( row ) * W + x ) * 4 + 2 ] ); };
			auto xs = [ & ]( int x ) { return static_cast< double >( pic[ ( static_cast< size_t >( row ) * W + x ) * 4 ] ); };
			double left = 0.0, right = 0.0;
			for( int x = col; x > 0; --x )
				if( z( x - 1 ) < half )
				{
					left = xs( x - 1 ) + ( half - z( x - 1 ) ) / ( z( x ) - z( x - 1 ) ) * ( xs( x ) - xs( x - 1 ) );
					break;
				}
			for( int x = col; x < W - 1; ++x )
				if( z( x + 1 ) < half )
				{
					right = xs( x ) + ( z( x ) - half ) / ( z( x ) - z( x + 1 ) ) * ( xs( x + 1 ) - xs( x ) );
					break;
				}
			const double picWidth   = right - left;
			const double tableAspect = ( peak - corner ) / physics::CriticalWavelength( datum );
			const double picAspect   = ( top - bottom ) / physics::CriticalWavelength( datum );
			Check( std::fabs( top - peak ) < 5e-3 * peak && std::fabs( picAspect / tableAspect - 1.0 ) < 1e-2 && std::fabs( picWidth / tableHalf - 1.0 ) < 1e-2,
			       fmt( "%s u %.3f, out of the picture: summit %.3f mm against the table's %.3f (bound 0.5%%), peak to corner %.3f lambda against %.3f (1%%), "
			            "width at half height %.3f mm against %.3f (1%%); tip radius %.3f mm as shipped (8.19 k_c), the solver's %.3f mm",
			            t.fluid, u, top * 1e3, peak * 1e3, picAspect, tableAspect, picWidth * 1e3, tableHalf * 1e3,
			            2.0 / tableTipCurvature( t, index, kc ) * 1e3, t.tipRadius[ static_cast< int >( std::lround( index ) ) ] * 1e3 ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --blend: a spike that has grown part of the way is that share of the table's.
//===========================================================================
int runBlend( const Perturb& perturb )
{
	std::printf( "\n=== blend: a lattice seeded at a fraction of the pool's equilibrium shows that fraction of the table's spike\n" );
	//At onset: the lattice's three modes at k_c neither grow nor decay there,
	//so the share it was seeded at is the share it stands at (at 1.2 I_c one
	//frame's growth carried 60% past the clamp).
	const spikes::Table& t = spikes::TableFor( 0 );
	const double kc        = physics::CriticalWavenumber( physics::FluidOf( physics::FluidId::EFH1 ) );
	for( double share : { 0.25, 0.6 } )
		for( const Raster& raster : kRasters )
		{
			const double u = 1.0;
			Rig rig;
			if( !latticeRig( rig, raster.w, raster.h, perturb, 0, u, share * poolEquilibrium( t, u ) ) )
				return 1;
			const std::vector< Spike > all = spikesOf( rig );
			const Floats surface = rig.Surface();
			const int w = rig.plugin.SurfaceWidth();
			double worst = 0.0;
			for( const Spike& s : all )
			{
				const double grown = std::clamp( s.peak / poolEquilibrium( t, u ), 0.0, 1.0 );
				const double want  = grown * tableCell( t, spikes::GridIndex( t, u ), kc, 0.0, 0.0 );
				//the texel nearest the spike, and the cell's value there, scaled
				const Pool& p = rig.plugin.CurrentPool();
				const double cx = p.lx / w;
				const double dx = ( s.i + 0.5 ) * cx - s.x, dy = ( s.j + 0.5 ) * ( p.ly / rig.plugin.SurfaceHeight() ) - s.y;
				const double cell = grown * tableCell( t, spikes::GridIndex( t, u ), kc, dx, dy );
				const double got  = surface[ ( static_cast< size_t >( s.j ) * w + s.i ) * 4 ];
				worst = std::max( worst, std::fabs( got - cell ) / want );
			}
			const double shown = all.empty() ? 0.0 : all[ 0 ].peak / poolEquilibrium( t, u );
			Check( !all.empty() && worst < 3e-3 && std::fabs( shown - share ) < 0.01,
			       fmt( "seeded at %.0f%% of the equilibrium, %dx%d: %zu spikes standing at %.1f%% of it, each its grown share of the table's to %.1e (bound 3e-3)",
			            100 * share, raster.w, raster.h, all.size(), 100 * shown, worst ) );
		}
	return Verdict();
}

/// How hexagonal a pattern is. In the annulus 0.75 to 1.25 k_c, the spectrum's
/// power by direction (modulo 180 degrees, 2 degree bins, summed over +-10
/// degrees); the strongest direction, then the power 60 and 120 degrees round
/// from it, each over the strongest's. A hexagon has three directions of
/// comparable power: both near 1. Rolls have one: both near 0.
struct Balance
{
	double strongest, at60, at120;
};

Balance directionBalance( const std::vector< double >& h, const Pool& p, double kc )
{
	std::vector< Cplx > field( h.begin(), h.end() );
	const auto spectrum = dft2( field, p.nx, p.ny, -1.0 );
	std::vector< double > bins( 90, 0.0 );
	for( int j = 0; j < p.ny; ++j )
		for( int i = 0; i < p.nx; ++i )
		{
			const int mx = i < p.nx / 2 ? i : i - p.nx, my = j < p.ny / 2 ? j : j - p.ny;
			const double kx = 2.0 * kPi * mx / p.lx, ky = 2.0 * kPi * my / p.ly;
			const double q  = std::hypot( kx, ky ) / kc;
			if( q < 0.75 || q > 1.25 )
				continue;
			const double angle = std::fmod( std::atan2( ky, kx ) * 180.0 / kPi + 360.0, 180.0 );
			bins[ static_cast< size_t >( angle / 2.0 ) % 90 ] += std::norm( spectrum[ static_cast< size_t >( j ) * p.nx + i ] );
		}
	auto around = [ & ]( int centre ) {
		double sum = 0.0;
		for( int d = -5; d <= 5; ++d )
			sum += bins[ static_cast< size_t >( ( ( centre + d ) % 90 + 90 ) % 90 ) ];
		return sum;
	};
	int best = 0;
	for( int b = 0; b < 90; ++b )
		if( around( b ) > around( best ) )
			best = b;
	const double top = around( best );
	return { best * 2.0 + 1.0, around( best + 30 ) / top, around( best + 60 ) / top };
}

//===========================================================================
// --coilspikes: over one coil the patch is spikes, tallest where the field is strongest.
//===========================================================================
int runCoilSpikes( const Perturb& perturb )
{
	std::printf( "\n=== coilspikes: over a coil the pattern is solved spikes, the tallest on the coil's axis, falling away with the field\n" );
	for( const Raster& raster : kRasters )
	{
		Rig rig;
		if( !startRig( rig, raster.w, raster.h, perturb ) )
			return 1;
		rig.plugin.ClearModelForTest();//the pattern engine as it ships, not quiet()'s Hy 0, Sa 2
		rig.Set( PT_MAGNET, static_cast< float >( Magnet::Coil ) );
		rig.Set( PT_COIL_SIZE, ParamFromCoilSize( 0.04 ) );//the default coil
		rig.Set( PT_FLUID, 0.0f );
		rig.Set( PT_BIAS, ParamFromCurrent( 1.3 ) );
		rig.Set( PT_GAIN, 0.0f );
		rig.Set( PT_COIL_LAG, ParamFromLag( 0.001 ) );
		rig.Set( PT_NOISE, ParamFromNoise( 1e-3 ) );
		rig.Set( PT_FIRE_ON, static_cast< float >( FireOn::Off ) );
		if( !rig.Render( 300 ) )
			return 1;
		const Pool& p = rig.plugin.CurrentPool();
		const double lambda = physics::CriticalWavelength( rig.plugin.Fluid() );
		const std::vector< Spike > all = spikesOf( rig );
		const Floats surface = rig.Surface();
		const int w = rig.plugin.SurfaceWidth();
		struct Row
		{
			double r, height;
		};
		std::vector< Row > rows;
		for( const Spike& s : all )
		{
			const double height = surface[ ( static_cast< size_t >( s.j ) * w + s.i ) * 4 ];
			if( height < 0.1e-3 )
				continue;
			rows.push_back( { std::hypot( s.x - 0.5 * p.lx, s.y - 0.5 * p.ly ), height } );
		}
		std::sort( rows.begin(), rows.end(), []( const Row& a, const Row& b ) { return a.height > b.height; } );
		//falling with the field: the height against the distance, rank-correlated
		std::vector< Row > byR = rows;
		std::sort( byR.begin(), byR.end(), []( const Row& a, const Row& b ) { return a.r < b.r; } );
		int inversions = 0, pairs = 0;
		for( size_t a = 0; a < byR.size(); ++a )
			for( size_t b = a + 1; b < byR.size(); ++b )
			{
				if( byR[ b ].r - byR[ a ].r < 0.5 * lambda )
					continue;//neighbours at nearly the same field: noise decides
				++pairs;
				inversions += byR[ b ].height > byR[ a ].height ? 1 : 0;
			}
		Check( rows.size() >= 7 && rows[ 0 ].r < lambda && inversions * 10 <= pairs,
		       fmt( "%dx%d: %zu spikes over 0.1 mm; the tallest %.2f mm stands %.1f mm from the axis (bound lambda_c %.1f mm); of %d pairs at least half a "
		            "wavelength apart in radius, %d have the outer one taller (bound 10%%)",
		            raster.w, raster.h, rows.size(), rows.empty() ? 0.0 : rows[ 0 ].height * 1e3, rows.empty() ? 0.0 : rows[ 0 ].r * 1e3, lambda * 1e3, pairs,
		            inversions ) );
	}
	//Rolls or hexagons: the pattern engine's own surface over the default coil,
	//held past onset for ten seconds (the case that laid down rolls under the
	//old constants), with the current arriving at once (a 1 ms coil) and through
	//the default 40 ms coil. Reported, not checked: it is the pattern engine's
	//answer.
	for( double lag : { 0.001, 0.04 } )
		for( double held : { 1.125, 1.3 } )
		{
			Rig rig;
			if( !startRig( rig, 320, 180, perturb ) )
				return 1;
			rig.plugin.ClearModelForTest();
			rig.Set( PT_MAGNET, static_cast< float >( Magnet::Coil ) );
			rig.Set( PT_COIL_SIZE, ParamFromCoilSize( 0.04 ) );
			rig.Set( PT_COIL_LAG, ParamFromLag( lag ) );
			rig.Set( PT_BIAS, ParamFromCurrent( held ) );
			rig.Set( PT_GAIN, 0.0f );
			rig.Set( PT_NOISE, ParamFromNoise( 1e-3 ) );
			rig.Set( PT_FIRE_ON, static_cast< float >( FireOn::Off ) );
			if( !rig.Render( 600 ) )
				return 1;
			const Balance b = directionBalance( poolHeights( rig ), rig.plugin.CurrentPool(), physics::CriticalWavenumber( rig.plugin.Fluid() ) );
			std::printf( "  info  held at %.3f I_c over the default coil for 10 s, a %.0f ms coil: the pool's strongest direction %.0f deg; 60 and 120 deg "
			             "round from it, %.2f and %.2f of its power (hexagons: both near 1; rolls: both near 0)\n",
			             held, lag * 1e3, b.strongest, b.at60, b.at120 );
		}
	return Verdict();
}

//===========================================================================
// --calibration: the pattern engine's constants put its fold at the solver's.
//===========================================================================
int runCalibration( const Perturb& perturb )
{
	std::printf( "\n=== calibration: per fluid, the pattern engine's saddle-node is the solver's fold and its peak at onset the solver's\n" );
	for( int fluid = 0; fluid < spikes::kTableCount; ++fluid )
	{
		const spikes::Table& t = spikes::TableFor( fluid );
		FerrofluidPlugin plugin( false );
		plugin.SetById( PT_FLUID, static_cast< float >( fluid ) );
		if( perturb.uncalibrated )
			plugin.SetModelForTest( physics::Nonlinear { 1.0, 0.5 } );
		const physics::Nonlinear model = plugin.Model();
		const double sn   = physics::SaddleNode( model );
		const double kc   = physics::CriticalWavenumber( physics::FluidOf( static_cast< physics::FluidId >( fluid ) ) );
		const double peak = physics::ThreeMode( model, 1.0 ).peak / kc;
		Check( std::fabs( sn - t.mFold ) < 1e-4 && std::fabs( peak / t.peakAtOnset - 1.0 ) < 1e-4,
		       fmt( "%s: Hy %.4f, Sa %.4f: saddle-node m %.5f against the solver's fold %.5f; peak at onset %.3f mm against %.3f", t.fluid, t.hysteresis,
		            t.saturation, sn, t.mFold, peak * 1e3, t.peakAtOnset * 1e3 ) );
	}
	return Verdict();
}
