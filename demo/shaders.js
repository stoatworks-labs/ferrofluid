// GENERATED from source/Shaders.cpp by demo/tools/check_shaders.py --write.
// Do not edit: tools/verify.sh fails if a character of this differs from the
// plugin's. The version line is the plugin's too; the kit's port() swaps it
// for GLSL ES 3.00's at compile time.
export const VERSION = '#version 410 core\n';

// kQuadVertex, source/Shaders.cpp
export const QUAD_VERTEX = `
layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;

out vec2 uv;

void main()
{
	gl_Position = vPosition;
	uv          = vUV;
}
`;

// kCommon, source/Shaders.cpp
export const COMMON = `
const int kTableSize = 1024;

//The PCG output mix: integer, so every GPU draws the same noise.
uint pcg( uint v )
{
	uint state = v * 747796405u + 2891336453u;
	uint word  = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
	return ( word >> 22u ) ^ word;
}

//Complex arithmetic on vec2.
vec2 times( vec2 a, vec2 b )
{
	return vec2( a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x );
}
vec2 over( vec2 a, vec2 b )
{
	return vec2( a.x * b.x + a.y * b.y, a.y * b.x - a.x * b.y ) / dot( b, b );
}
//The principal square root, without trig.
vec2 root( vec2 z )
{
	float r  = length( z );
	float re = sqrt( max( 0.5 * ( r + z.x ), 0.0 ) );
	float im = sqrt( max( 0.5 * ( r - z.x ), 0.0 ) );
	return vec2( re, z.y < 0.0 ? -im : im );
}

//sRGB, both ways: reflection multiplies LIGHT, so it happens in linear light.
vec3 toLinear( vec3 code )
{
	vec3 c = max( code, vec3( 0.0 ) );
	return mix( c / 12.92, pow( ( c + 0.055 ) / 1.055, vec3( 2.4 ) ), step( vec3( 0.04045 ), c ) );
}
vec3 toCode( vec3 light )
{
	vec3 l = max( light, vec3( 0.0 ) );
	return mix( l * 12.92, 1.055 * pow( l, vec3( 1.0 / 2.4 ) ) - 0.055, step( vec3( 0.0031308 ), l ) );
}
`;

// kFFTFragment, source/Shaders.cpp
export const FFT_FRAGMENT = `
uniform sampler2D Source;
uniform sampler2D Twiddles;//exp( -2 pi i m / Length ), m = 0 .. Length-1, from doubles
uniform int Length;
uniform int Stride;
uniform int Radix;
uniform int Horizontal;
uniform float Direction;   //-1 forward, +1 inverse

out vec4 fragColor;

void main()
{
	ivec2 cell = ivec2( gl_FragCoord.xy );
	int o      = Horizontal != 0 ? cell.x : cell.y;
	int span   = Stride * Radix;
	int block  = o / span;
	int within = o - block * span;
	int j      = block * Stride + ( within - ( within / Stride ) * Stride );
	int part   = Length / Radix;
	int scale  = Length / span;

	vec4 sum = vec4( 0.0 );
	for( int t = 0; t < 16; ++t )
	{
		if( t >= Radix )
			break;
		ivec2 at = cell;
		if( Horizontal != 0 )
			at.x = j + t * part;
		else
			at.y = j + t * part;
		vec4 v = texelFetch( Source, at, 0 );
		//The twiddle from the double-precision table: integer index, exact.
		int e  = t * within;
		e      = e - ( e / span ) * span;
		vec2 w = texelFetch( Twiddles, ivec2( e * scale, 0 ), 0 ).xy;
		if( Direction > 0.0 )
			w.y = -w.y;
		sum += vec4( times( v.xy, w ), times( v.zw, w ) );
	}
	fragColor = sum;
}
`;

// kEvolveFragment, source/Shaders.cpp
export const EVOLVE_FRAGMENT = `
uniform sampler2D State;  //RGBA32F ( h_k, v_k ), the unnormalised DFT
uniform sampler2D Force;  //RGBA32F: ( outer + i plain )_k and ( sym )_k, the stress's unnormalised DFTs
uniform ivec2 GridSize;
uniform vec2 Domain;      //metres
uniform float Gravity;    //g, m/s^2
uniform float TensionRho; //sigma / rho, m^3/s^2
uniform float StressRho;  //G_ref / rho, m^2/s^2
uniform float InvRho;     //1 / rho
uniform float Kinematic;  //nu (scaled), m^2/s
uniform float Dt;         //s
uniform float ForceCut;   //1/m: the stress is low-passed above this
uniform float Kc;         //k_c, 1/m
uniform float OuterCut;   //1/m: the D-weighted stress is low-passed above this
uniform float Norm;       //1 / ( Nx Ny )
uniform int Hooks;

layout( location = 0 ) out vec4 stateOut;
layout( location = 1 ) out vec4 packOut;

void main()
{
	ivec2 cell = ivec2( gl_FragCoord.xy );

	//The Nyquist row and column: a real field's spectrum there cannot carry a
	//derivative (i k A at -k is not the conjugate of itself), and nothing
	//physical lives on a two-cell wave.
	if( cell.x == GridSize.x / 2 || cell.y == GridSize.y / 2 )
	{
		stateOut = vec4( 0.0 );
		packOut  = vec4( 0.0 );
		return;
	}

	ivec2 m = ivec2( cell.x < GridSize.x / 2 ? cell.x : cell.x - GridSize.x,
	                 cell.y < GridSize.y / 2 ? cell.y : cell.y - GridSize.y );
	vec2 kv = 6.28318530717958648 * vec2( m ) / Domain;
	float k = length( kv );

	vec4 s = texelFetch( State, cell, 0 );
	vec2 h = s.xy;
	vec2 v = s.zw;

	//The kick. Deep water: a normal stress N on the surface accelerates a
	//mode by ( k / rho ) N_k. Low-passed above ForceCut (exp( -( k / cut )^8 )):
	//the product of two fields on a grid aliases, and a stress that changes
	//inside a spike's tip is not one the grid can carry.
	if( ( Hooks & 64 ) == 0 )
	{
		//The stress came through the transform as ( outer + i plain ): two
		//real fields, unpacked by Hermitian symmetry. The outer part takes
		//its W = max( k, k_c ) / k_c here.
		ivec2 opposite = ivec2( ( GridSize.x - cell.x ) % GridSize.x, ( GridSize.y - cell.y ) % GridSize.y );
		vec2 z         = texelFetch( Force, cell, 0 ).xy;
		vec2 zm        = texelFetch( Force, opposite, 0 ).xy;
		zm.y           = -zm.y;
		vec2 outer     = 0.5 * ( z + zm );
		vec2 diff      = 0.5 * ( z - zm );
		vec2 plain     = vec2( diff.y, -diff.x );
		float r        = k / ForceCut;
		float r2       = r * r;
		float r4       = r2 * r2;
		float s        = k / OuterCut;
		float s2       = s * s;
		float s4       = s2 * s2;
		//The second field is the residual's half that |grad| acts on.
		vec2 sym = texelFetch( Force, cell, 0 ).zw;
		v += Dt * k * InvRho * ( exp( -s4 * s4 ) * max( k, Kc ) / Kc * outer + exp( -r4 * r4 ) * ( plain + k * sym ) );
	}

	//Cowley & Rosensweig: rho omega^2 = rho g k + sigma k^3 - G k^2.
	float tension = ( Hooks & 1 ) != 0 ? 0.0 : TensionRho;
	float omega2  = Gravity * k + tension * k * k * k - StressRho * k * k;//= mirrored
	float gamma   = 2.0 * Kinematic * k * k;                               //= mirrored

	//The exact step of  y'' + 2 gamma y' + omega^2 y = 0  over Dt, on whichever
	//side of critical damping the mode is on -- and past onset omega^2 < 0, so
	//the growing branch is the same formula's cosh and sinh.
	float C;
	float S;
	float disc = omega2 - gamma * gamma;
	if( disc > 0.0 )
	{
		float wd    = sqrt( disc );
		float decay = exp( -gamma * Dt );
		C           = decay * cos( wd * Dt );
		S           = decay * sin( wd * Dt ) / wd;
	}
	else
	{
		float wd = sqrt( -disc );
		if( wd * Dt < 1e-4 )
		{
			float decay = exp( -gamma * Dt );
			C           = decay * ( 1.0 + 0.5 * wd * wd * Dt * Dt );
			S           = decay * Dt;
		}
		else
		{
			//cosh and sinh times the decay, written so that neither half
			//overflows before the other can cancel it.
			float slow = exp( ( wd - gamma ) * Dt );
			float fast = exp( -( wd + gamma ) * Dt );
			C          = 0.5 * ( slow + fast );
			S          = 0.5 * ( slow - fast ) / wd;
		}
	}

	vec2 h2 = C * h + S * ( v + gamma * h );
	vec2 v2 = C * v - S * ( omega2 * h + gamma * v );

	//The pool keeps its volume: the mean level never moves.
	if( m.x == 0 && m.y == 0 )
	{
		h2 = vec2( 0.0 );
		v2 = vec2( 0.0 );
	}

	stateOut = vec4( h2, v2 );

	//The next substep's inverse: W h + i |grad| h, two real fields in one
	//complex transform ( A + i B ), and h itself in the second. W = max( k,
	//k_c ): the model's stress is written in W h (Fluid.h); the local field's
	//residual needs |grad| h and h.
	vec2 a  = max( k, Kc ) * h2;
	vec2 b  = k * h2;
	packOut = Norm * vec4( a.x - b.y, a.y + b.x, h2 );
}
`;

// kForceFragment, source/Shaders.cpp
export const FORCE_FRAGMENT = `
uniform sampler2D Real;      //( W h, |grad| h, h ): dimensionless, dimensionless, metres
uniform sampler2D CoilTable; //H_z per amp at the surface, from rho = 0 to RhoMax
uniform sampler2D FluidTable;//G from H_a = 0 to FieldMax
uniform ivec2 GridSize;
uniform vec2 Domain;
uniform int Magnet;          //0 uniform, 1 one coil, 2 three
uniform float UniformStress; //G over the whole pool, Pa (Magnet 0)
uniform vec3 Currents;       //amps, per coil
uniform vec2 Centres[ 3 ];   //metres, the pool's centre at 0
uniform int CoilCount;
uniform float RhoMax;
uniform float FieldMax;
uniform float StressRef;     //G_ref, Pa: what the spectral step already carries
uniform float RhoG;          //rho g
uniform float Kc;            //k_c
uniform float Hy;
uniform float Sa;
uniform float NoiseRms;      //Pa per cell, this substep
uniform uint NoiseSalt;
uniform int Hooks;

out vec4 fragColor;

//A table read by hand: the sampler's fixed-point weights are 8 bits.
float lookup( sampler2D table, float x )
{
	float f = clamp( x, 0.0, 1.0 ) * float( kTableSize - 1 );
	int i   = min( int( f ), kTableSize - 2 );
	float t = f - float( i );
	float a = texelFetch( table, ivec2( i, 0 ), 0 ).x;
	float b = texelFetch( table, ivec2( i + 1, 0 ), 0 ).x;
	return a + t * ( b - a );
}

float unit( uint bits )
{
	return float( bits >> 8u ) * ( 1.0 / 16777216.0 );
}

void main()
{
	ivec2 cell = ivec2( gl_FragCoord.xy );
	vec3 real  = texelFetch( Real, cell, 0 ).xyz;

	//The local field. Uniform: the spectral step carries all of it. A coil:
	//the normal field it makes at this point (the sum over the coils), and the
	//fluid's G there from its response table.
	float stress = UniformStress;
	if( Magnet != 0 )
	{
		vec2 p      = ( ( vec2( cell ) + 0.5 ) / vec2( GridSize ) - 0.5 ) * Domain;
		float field = 0.0;
		for( int c = 0; c < 3; ++c )
		{
			if( c >= CoilCount )
				break;
			field += Currents[ c ] * lookup( CoilTable, length( p - Centres[ c ] ) / RhoMax );
		}
		stress = lookup( FluidTable, field / FieldMax );
	}

	//The model's stress (Fluid.h): N = ( rho g / k_c ) W[ m ( Hy g^2 - Sa g^3 ) ],
	//g = W h, W = max( |grad|, k_c ) / k_c. The bracket is computed here; the
	//W is applied to its spectrum in the evolve, which is why it travels in .x
	//and the stress that is NOT weighted travels in .y.
	float m     = stress * Kc / ( 2.0 * RhoG );
	float g     = real.x;
	float outer = RhoG / Kc * m * g * g * ( Hy - Sa * g );

	//The local approximation, written so it stays the gradient of an energy:
	//the magnetic energy -1/2 integral of G( x ) h |grad| h has the stress
	//1/2 [ G |grad| h + |grad|( G h ) ] (|grad| is self-adjoint; G( x ) |grad|
	//alone is not, so it was no energy at all). It was suspected of turning a
	//coil's hexagons into rolls and wasn't the cause (AGENTS.md); it stays
	//because the model should be an energy. The spectral step
	//has already applied G_ref to every mode; this is the rest, G - G_ref. The
	//second half travels as G h, for the evolve to take its |grad|.
	float N   = 0.0;
	float sym = 0.0;
	if( Magnet != 0 && ( Hooks & 32 ) == 0 )
	{
		N   = 0.5 * ( stress - StressRef ) * real.y;
		sym = 0.5 * ( stress - StressRef ) * real.z;
	}

	//The floor the instability grows from: white in space and time, the sum
	//of four uniform draws (unit variance, integer hashed).
	if( NoiseRms > 0.0 )
	{
		uint base = pcg( uint( cell.x ) * 1973u ^ pcg( uint( cell.y ) * 9277u ^ NoiseSalt ) );
		float sum = unit( pcg( base ) ) + unit( pcg( base + 1u ) ) + unit( pcg( base + 2u ) ) + unit( pcg( base + 3u ) );
		N += NoiseRms * ( sum - 2.0 ) * 1.7320508;
	}

	fragColor = vec4( outer, N, sym, 0.0 );
}
`;

// kPackFragment, source/Shaders.cpp
export const PACK_FRAGMENT = `
uniform sampler2D State;
uniform ivec2 GridSize;//the pool's
uniform ivec2 OutSize; //the camera's: twice it
uniform vec2 Domain;
uniform float Norm;    //1 / ( Nx Ny ) of the POOL: the padded spectrum keeps the surface's scale

out vec4 fragColor;

void main()
{
	ivec2 cell = ivec2( gl_FragCoord.xy );
	ivec2 m    = ivec2( cell.x < OutSize.x / 2 ? cell.x : cell.x - OutSize.x,
	                    cell.y < OutSize.y / 2 ? cell.y : cell.y - OutSize.y );

	//Zero-padding: every frequency the pool carries, and nothing past it (its
	//own Nyquist row and column included, which it holds at zero).
	if( abs( m.x ) >= GridSize.x / 2 || abs( m.y ) >= GridSize.y / 2 )
	{
		fragColor = vec4( 0.0 );
		return;
	}
	ivec2 from = ivec2( ( m.x + GridSize.x ) % GridSize.x, ( m.y + GridSize.y ) % GridSize.y );
	vec2 kv    = 6.28318530717958648 * vec2( m ) / Domain;
	vec2 h     = texelFetch( State, from, 0 ).xy;

	//( h ) and ( h_x + i h_y ) = i k_x h + i ( i k_y h ).
	fragColor = Norm * vec4( h, -kv.x * h.y - kv.y * h.x, kv.x * h.x - kv.y * h.y );
}
`;

// kBoundsFragment, source/Shaders.cpp
export const BOUNDS_FRAGMENT = `
uniform sampler2D Source;  //First: the surface ( h, 0, h_x, h_y ); after: the previous bounds
uniform ivec2 SourceSize;
uniform int First;
uniform vec2 Cell;         //the grid's cell, metres

out vec4 fragColor;

void main()
{
	ivec2 base = ivec2( gl_FragCoord.xy ) * 8;
	vec4 most  = vec4( 0.0 );
	for( int y = 0; y < 8; ++y )
		for( int x = 0; x < 8; ++x )
		{
			ivec2 p = base + ivec2( x, y );
			if( p.x >= SourceSize.x || p.y >= SourceSize.y )
				continue;
			vec4 v = texelFetch( Source, p, 0 );
			if( First != 0 )
			{
				//The bilinear surface the march reads can be steeper than the
				//spectral gradient at its samples: take the cell differences
				//too (wrapping, as the pool does).
				float east  = texelFetch( Source, ivec2( ( p.x + 1 ) % SourceSize.x, p.y ), 0 ).x;
				float north = texelFetch( Source, ivec2( p.x, ( p.y + 1 ) % SourceSize.y ), 0 ).x;
				v = vec4( abs( v.x ), length( v.zw ), abs( east - v.x ) / Cell.x, abs( north - v.x ) / Cell.y );
			}
			most = max( most, v );
		}
	fragColor = most;
}
`;

// kSpikesFragment, source/Shaders.cpp
export const SPIKES_FRAGMENT = `
uniform sampler2D Surface;     //the pool's surface at twice its grid: ( h, 0, h_x, h_y ), metres
uniform ivec2 Size;            //its texels
uniform vec2 Cell;             //metres a texel
uniform vec2 Domain;           //metres, the pool's centre at 0
uniform int Magnet;            //0 uniform, 1 one coil, 2 three
uniform float UniformU;        //Magnet 0: u = Ha / Ha_c over the whole pool
uniform sampler2D CoilTable;   //H_z per amp at the surface, from rho = 0 to RhoMax
uniform float RhoMax;
uniform vec2 Centres[ 3 ];
uniform int CoilCount;
uniform vec3 CurrentsU;        //each coil's current over Ha_c (amps per A/m)
uniform sampler2D Coefficients;//nGrid x 1: the table's basis coefficients at v = sqrt( u - u_fold )
uniform int NGrid;
uniform float UFold;
uniform float VEnd;
uniform sampler2D Equilibrium; //NEq x 1: the pool's own equilibrium peak height, metres, against u
uniform int NEq;
uniform vec2 EqRange;          //its u range
uniform int Reach;             //texels in a quarter of a wavelength
uniform int Hooks;

layout( location = 0 ) out vec4 weights;
layout( location = 1 ) out vec4 where;

vec4 at( ivec2 c )
{
	return texelFetch( Surface, ( c + Size ) % Size, 0 );
}

float lookup( sampler2D table, float x )
{
	float f = clamp( x, 0.0, 1.0 ) * float( kTableSize - 1 );
	int i   = min( int( f ), kTableSize - 2 );
	float t = f - float( i );
	return mix( texelFetch( table, ivec2( i, 0 ), 0 ).x, texelFetch( table, ivec2( i + 1, 0 ), 0 ).x, t );
}

vec4 row( sampler2D table, int n, float f )
{
	f       = clamp( f, 0.0, float( n - 1 ) );
	int i   = min( int( f ), n - 2 );
	float t = f - float( i );
	return mix( texelFetch( table, ivec2( i, 0 ), 0 ), texelFetch( table, ivec2( i + 1, 0 ), 0 ), t );
}

void main()
{
	ivec2 c = ivec2( gl_FragCoord.xy );
	vec4 s  = at( c );
	weights = vec4( 0.0 );
	where   = vec4( 0.0 );
	if( s.x <= 0.0 )
		return;
	//A spike stands where the pool's surface is highest within a quarter of a
	//wavelength (the lattice's spikes are four times that apart; a ripple on
	//a flank is a maximum among its eight neighbours but not this). Ties go
	//to the lower index, so a flat top makes one spike. The eight first, as
	//they reject nearly every texel.
	for( int dy = -1; dy <= 1; ++dy )
		for( int dx = -1; dx <= 1; ++dx )
		{
			if( dx == 0 && dy == 0 )
				continue;
			float n    = at( c + ivec2( dx, dy ) ).x;
			bool after = dy > 0 || ( dy == 0 && dx > 0 );
			if( n > s.x || ( n == s.x && !after ) )
				return;
		}
	for( int dy = -Reach; dy <= Reach; ++dy )
		for( int dx = -Reach; dx <= Reach; ++dx )
		{
			if( abs( dx ) <= 1 && abs( dy ) <= 1 )
				continue;
			if( dx * dx + dy * dy > Reach * Reach )
				continue;
			float n    = at( c + ivec2( dx, dy ) ).x;
			bool after = dy > 0 || ( dy == 0 && dx > 0 );
			if( n > s.x || ( n == s.x && !after ) )
				return;
		}
	//Its position inside the texel: a Newton step on the gradient, with the
	//Hessian from the neighbours' gradients.
	vec4 e = at( c + ivec2( 1, 0 ) ), w = at( c - ivec2( 1, 0 ) );
	vec4 nn = at( c + ivec2( 0, 1 ) ), so = at( c - ivec2( 0, 1 ) );
	float hxx = ( e.z - w.z ) / ( 2.0 * Cell.x );
	float hyy = ( nn.w - so.w ) / ( 2.0 * Cell.y );
	float hxy = 0.5 * ( ( nn.z - so.z ) / ( 2.0 * Cell.y ) + ( e.w - w.w ) / ( 2.0 * Cell.x ) );
	float det = hxx * hyy - hxy * hxy;
	vec2 off  = vec2( 0.0 );
	if( det > 0.0 && hxx < 0.0 )
		off = -vec2( hyy * s.z - hxy * s.w, hxx * s.w - hxy * s.z ) / det;
	off        = clamp( off, -0.5 * Cell, 0.5 * Cell );
	float peak = s.x + 0.5 * dot( s.zw, off );

	//The field it stands in.
	vec2 p  = ( vec2( c ) + 0.5 ) * Cell + off - 0.5 * Domain;
	float u = UniformU;
	if( Magnet != 0 )
	{
		u = 0.0;
		for( int k = 0; k < 3; ++k )
		{
			if( k >= CoilCount )
				break;
			u += CurrentsU[ k ] * lookup( CoilTable, ( Hooks & 1024 ) != 0 ? 0.0 : length( p - Centres[ k ] ) / RhoMax );
		}
	}
	//How far the pool's spike has grown towards the pool's own equilibrium.
	float eq    = row( Equilibrium, NEq, ( u - EqRange.x ) / ( EqRange.y - EqRange.x ) * float( NEq - 1 ) ).x;
	float grown = clamp( peak / max( eq, 1e-9 ), 0.0, 1.0 );
	if( ( Hooks & 256 ) != 0 )
		grown = 1.0;
	//The solved spike at u (the stable branch; below its fold, the fold's).
	float v = sqrt( max( u - UFold, 0.0 ) );
	if( ( Hooks & 512 ) != 0 )
		v = 0.0;
	weights = grown * row( Coefficients, NGrid, v / VEnd * float( NGrid - 1 ) );
	where   = vec4( off, 1.0, peak );
}
`;

// kDepositFragment, source/Shaders.cpp
export const DEPOSIT_FRAGMENT = `
uniform sampler2D Weights;
uniform sampler2D Where;
uniform ivec2 Size;
uniform vec2 Cell;
uniform float Sigma;//metres
uniform float Norm; //1 / ( 2 pi Sigma^2 )

out vec4 fragColor;

void main()
{
	ivec2 c = ivec2( gl_FragCoord.xy );
	vec2 x  = ( vec2( c ) + 0.5 ) * Cell;
	vec4 acc = vec4( 0.0 );
	for( int dy = -4; dy <= 4; ++dy )
		for( int dx = -4; dx <= 4; ++dx )
		{
			ivec2 q  = c + ivec2( dx, dy );
			ivec2 qw = ( q + Size ) % Size;
			vec4 wh  = texelFetch( Where, qw, 0 );
			if( wh.z == 0.0 )
				continue;
			vec2 d = x - ( ( vec2( q ) + 0.5 ) * Cell + wh.xy );
			acc += texelFetch( Weights, qw, 0 ) * exp( -dot( d, d ) / ( 2.0 * Sigma * Sigma ) );
		}
	fragColor = acc * Norm;
}
`;

// kCombineFragment, source/Shaders.cpp
export const COMBINE_FRAGMENT = `
uniform sampler2D Spectrum;//( D0 + i D1, D2 + i D3 ), unnormalised
uniform ivec2 Size;
uniform vec2 Domain;
uniform float Kc;
uniform sampler2D Basis;   //NQ x 1: the four basis kernels' transforms, m^3
uniform int NQ;
uniform float QMax;
uniform float Sigma;
uniform float Norm;        //1 / ( Nx Ny )

out vec4 fragColor;

vec2 conjugate( vec2 a )
{
	return vec2( a.x, -a.y );
}

void main()
{
	ivec2 c  = ivec2( gl_FragCoord.xy );
	ivec2 cm = ( Size - c ) % Size;
	vec4 z   = texelFetch( Spectrum, c, 0 );
	vec4 zm  = texelFetch( Spectrum, cm, 0 );
	//Two real fields in one complex transform: A + i B -> ( Z( k ) + conj Z( -k ) ) / 2
	//and ( Z( k ) - conj Z( -k ) ) / 2i.
	vec2 d0 = 0.5 * ( z.xy + conjugate( zm.xy ) );
	vec2 t1 = 0.5 * ( z.xy - conjugate( zm.xy ) );
	vec2 d1 = vec2( t1.y, -t1.x );
	vec2 d2 = 0.5 * ( z.zw + conjugate( zm.zw ) );
	vec2 t3 = 0.5 * ( z.zw - conjugate( zm.zw ) );
	vec2 d3 = vec2( t3.y, -t3.x );
	ivec2 m = ivec2( c.x < Size.x / 2 ? c.x : c.x - Size.x, c.y < Size.y / 2 ? c.y : c.y - Size.y );
	vec2 k  = 6.283185307179586 * vec2( m ) / Domain;
	float q = length( k ) / Kc;
	vec4 b  = vec4( 0.0 );
	if( q < QMax )
	{
		float f = q / QMax * float( NQ - 1 );
		int i   = min( int( f ), NQ - 2 );
		b       = mix( texelFetch( Basis, ivec2( i, 0 ), 0 ), texelFetch( Basis, ivec2( i + 1, 0 ), 0 ), f - float( i ) );
	}
	float g = exp( -0.5 * Sigma * Sigma * dot( k, k ) );
	vec2 S  = ( d0 * b.x + d1 * b.y + d2 * b.z + d3 * b.w ) * ( Norm / g );
	//No derivative at a Nyquist row or column: it has no sign.
	vec2 kd = vec2( c.x == Size.x / 2 ? 0.0 : k.x, c.y == Size.y / 2 ? 0.0 : k.y );
	fragColor = vec4( S, times( vec2( -kd.y, kd.x ), S ) );
}
`;

// kRenderFragment, source/Shaders.cpp
export const RENDER_FRAGMENT = `
uniform sampler2D Surface;     //( h, 0, h_x, h_y ), REPEAT, LINEAR
uniform sampler2D Bounds;      //1 x 1: ( max |h|, max |grad h|, max |dh/dx|, max |dh/dy| )
uniform sampler2D InputTexture;
uniform vec2 MaxUV;
uniform int IsOver;
uniform ivec4 Viewport;
uniform vec2 Domain;           //metres, the pool's centre at 0
uniform vec2 Cell;             //metres
uniform vec3 Eye;
uniform vec3 Forward;
uniform vec3 Right;
uniform vec3 Up;
uniform vec2 TanHalf;          //tan of the half fields of view
uniform vec2 Optical;          //the fluid's n and k
uniform float Gain;            //2^stops / R0
uniform float ScreenHeight;    //Over: metres above the pool
uniform vec2 ScreenScale;      //Over: D / ( ( D + height ) * frame size )
uniform int BoxCount;
uniform vec3 BoxCentre[ 4 ];
uniform vec3 BoxU[ 4 ];        //half-extent vectors
uniform vec3 BoxV[ 4 ];
uniform vec3 BoxRadiance[ 4 ]; //linear
uniform vec3 Backdrop;         //the studio's walls, linear (0: black)
uniform float MixAmount;
uniform int Hooks;

out vec4 fragColor;

float slab;   //|h| never exceeds this
float lipschitz;
float minStep;

float heightAt( vec2 p )
{
	return texture( Surface, p / Domain + 0.5 ).x;
}

//The normal from the gradient interpolated by Catmull-Rom over 4 x 4 texels,
//not bilinearly: a spike's flank turns through a right angle in a few texels,
//and a gradient linear between them has creases that a mirror shows as flutes
//down every flank. The cubic's gradient field is continuous with its slope.
float catmullRom( float t, out vec4 w )
{
	float t2 = t * t, t3 = t2 * t;
	w = vec4( -0.5 * t3 + t2 - 0.5 * t, 1.5 * t3 - 2.5 * t2 + 1.0, -1.5 * t3 + 2.0 * t2 + 0.5 * t, 0.5 * t3 - 0.5 * t2 );
	return 0.0;
}

vec3 normalAt( vec2 p )
{
	ivec2 size = textureSize( Surface, 0 );
	vec2 f     = ( p / Domain + 0.5 ) * vec2( size ) - 0.5;
	vec2 i     = floor( f );
	vec4 wx, wy;
	catmullRom( f.x - i.x, wx );
	catmullRom( f.y - i.y, wy );
	vec2 g = vec2( 0.0 );
	for( int y = 0; y < 4; ++y )
	{
		vec2 row = vec2( 0.0 );
		for( int x = 0; x < 4; ++x )
		{
			ivec2 c = ( ivec2( i ) + ivec2( x - 1, y - 1 ) + size * 4 ) % size;
			row += wx[ x ] * texelFetch( Surface, c, 0 ).zw;
		}
		g += wy[ y ] * row;
	}
	return normalize( vec3( -g, 1.0 ) );
}

//The unpolarised reflectance of an absorbing dielectric N = n + i k, exact:
//r_s = ( cos i - w ) / ( cos i + w ), r_p = ( N^2 cos i - w ) / ( N^2 cos i + w ),
//w = sqrt( N^2 - sin^2 i ).
float fresnel( float ci )
{
	if( ( Hooks & 2 ) != 0 )
		return 1.0;
	ci         = clamp( ci, 0.0, 1.0 );
	vec2 index = times( Optical, Optical );
	vec2 w     = root( index - vec2( 1.0 - ci * ci, 0.0 ) );
	vec2 rs    = over( vec2( ci, 0.0 ) - w, vec2( ci, 0.0 ) + w );
	vec2 rp    = over( index * ci - w, index * ci + w );
	float s2   = dot( rs, rs );
	return ( Hooks & 16 ) != 0 ? s2 : 0.5 * ( s2 + dot( rp, rp ) );
}

//The first crossing of o + t d with the surface for t in [ t0, t1 ]. A step
//is the gap over the fastest the gap can close (the slope bound), never less
//than minStep; a crossing is then bisected. False if the ray leaves the slab.
bool crossing( vec3 o, vec3 d, float t0, float t1, out float found )
{
	float rate = lipschitz * length( d.xy ) - d.z;
	float t    = t0;
	float prev = t0;
	for( int i = 0; i < 160; ++i )
	{
		vec3 p    = o + d * t;
		float gap = p.z - heightAt( p.xy );
		if( gap <= 0.0 )
		{
			float a = prev, b = t;
			for( int j = 0; j < 12; ++j )
			{
				float c = 0.5 * ( a + b );
				vec3 q  = o + d * c;
				if( q.z - heightAt( q.xy ) > 0.0 )
					a = c;
				else
					b = c;
			}
			found = 0.5 * ( a + b );
			return true;
		}
		if( t >= t1 )
			break;
		prev       = t;
		float jump = rate > 0.0 ? gap / rate : t1 - t;
		t          = min( t + max( jump, minStep ), t1 );
	}
	found = t;
	return false;
}

//What a ray leaving the surface at o in direction d sees.
vec3 environment( vec3 o, vec3 d )
{
	if( ( Hooks & 4 ) != 0 )
		return vec3( 1.0 );

	if( IsOver != 0 )
	{
		//The clip, on a screen ScreenHeight above the pool, facing down. It
		//shows the clip mirrored, as a teleprompter's monitor does, so a flat
		//pool's reflection of it reads the right way round.
		if( d.z <= 1e-6 )
			return vec3( 0.0 );
		vec2 q   = o.xy + d.xy * ( ( ScreenHeight - o.z ) / d.z );
		vec2 suv = 0.5 + q * ScreenScale;
		if( ( Hooks & 8 ) != 0 )
			suv.x = 1.0 - suv.x;
		if( suv.x < 0.0 || suv.y < 0.0 || suv.x > 1.0 || suv.y > 1.0 )
			return vec3( 0.0 );//past the screen: the dark room
		vec4 c = texture( InputTexture, suv * MaxUV );
		return toLinear( c.rgb ) * c.a;
	}

	//The studio: its walls, then the nearest soft box the ray passes through.
	//A seamless studio: the walls dim towards the floor, the ceiling's diffuser
	//brighter overhead.
	float up   = clamp( d.z, 0.0, 1.0 );
	vec3 light = Backdrop * ( 0.15 + 0.85 * up * up );
	float best = 1e30;
	for( int b = 0; b < 4; ++b )
	{
		if( b >= BoxCount )
			break;
		vec3 n    = cross( BoxU[ b ], BoxV[ b ] );
		float den = dot( d, n );
		if( abs( den ) < 1e-20 )
			continue;
		float t = dot( BoxCentre[ b ] - o, n ) / den;
		if( t <= 0.0 || t >= best )
			continue;
		vec3 q  = o + d * t - BoxCentre[ b ];
		float a = dot( q, BoxU[ b ] ) / dot( BoxU[ b ], BoxU[ b ] );
		float e = dot( q, BoxV[ b ] ) / dot( BoxV[ b ], BoxV[ b ] );
		if( abs( a ) > 1.0 || abs( e ) > 1.0 )
			continue;
		best = t;
		//A diffuser is brightest in its middle.
		light = BoxRadiance[ b ] * ( 1.0 - 0.3 * max( a * a, e * e ) );
	}
	return light;
}

//What the camera sees through one point of the pixel: the reflected light,
//linear, and whether its ray went on to a neighbouring spike.
vec3 through( vec2 uv, out bool bounced, out vec3 P )
{
	//The camera ray, and where it meets the surface. Every ray points down
	//(Tilt plus half the field of view stays below 90 degrees), so it must
	//cross the slab's floor if nothing stops it sooner.
	vec3 d = normalize( Forward + ( 2.0 * uv.x - 1.0 ) * TanHalf.x * Right + ( 2.0 * uv.y - 1.0 ) * TanHalf.y * Up );
	float t;
	crossing( Eye, d, ( Eye.z - slab ) / -d.z, ( Eye.z + slab ) / -d.z, t );
	P      = Eye + d * t;
	vec3 n = normalAt( P.xy );

	float reflectance = fresnel( dot( -d, n ) );
	vec3 r            = reflect( d, n );

	//One further bounce: a neighbouring spike, if the reflected ray meets one
	//before it leaves the slab. A third is not followed. The march leaves from
	//a cell and a half along the ray: the camera reads the surface between
	//samples bilinearly while its normal is the cubic gradient's, and the two
	//disagree by a fraction of a cell on a steep flank -- enough for a ray to
	//find the spike it has just left. A reflection off a convex surface never
	//meets that surface again, so nothing real is lost.
	vec3 seen;
	vec3 from   = P + n * minStep;
	float leave = r.z > 0.0 ? ( slab - from.z ) / r.z : ( from.z + slab ) / -r.z;
	float away  = 6.0 * minStep;
	float t2;
	bounced = leave > away && crossing( from, r, away, leave, t2 );
	if( bounced )
	{
		vec3 Q  = from + r * t2;
		vec3 n2 = normalAt( Q.xy );
		seen    = fresnel( dot( -r, n2 ) ) * environment( Q, reflect( r, n2 ) );
	}
	else
		seen = environment( P, r );
	return min( reflectance * seen * Gain, vec3( 1.0 ) );
}

void main()
{
	vec2 frag = gl_FragCoord.xy - vec2( Viewport.xy );
	vec2 uv   = frag / vec2( Viewport.zw );
	//The dry clip is the pixel itself, fetched, not filtered: Mix 0 is the
	//clip bit for bit on every rasteriser (the software renderer's bilinear
	//weights are not exact at a texel's centre).
	vec4 clip = IsOver != 0 ? texelFetch( InputTexture, ivec2( frag ), 0 ) : vec4( 0.0, 0.0, 0.0, 1.0 );
	if( IsOver != 0 && MixAmount <= 0.0 )
	{
		fragColor = clip;
		return;
	}

	vec4 bounds = texelFetch( Bounds, ivec2( 0 ), 0 );
	slab        = bounds.x * 1.02 + 1e-7;
	lipschitz   = 1.1 * max( bounds.y, length( bounds.zw ) ) + 1e-3;
	minStep     = 0.25 * min( Cell.x, Cell.y );

	bool bounced;
	vec3 P;
	vec3 light = through( uv, bounced, P );
	//The harness's picture of the surface: where each pixel's ray met it, metres.
	if( ( Hooks & 128 ) != 0 )
	{
		fragColor = vec4( P, 1.0 );
		return;
	}
	//A pixel whose light came off a neighbour is four samples: a neighbour
	//seen in a spike's flank is a small, sharp, curved mirror, and one ray a
	//pixel turns its grazing edges into glitter.
	if( bounced )
	{
		vec2 px = 1.0 / vec2( Viewport.zw );
		light   = vec3( 0.0 );
		for( int s = 0; s < 4; ++s )
		{
			vec2 o = vec2( s == 0 || s == 3 ? 0.125 : -0.125, s < 2 ? 0.375 : -0.375 );
			o      = s % 2 == 0 ? o : o.yx;
			bool b;
			vec3 q;
			light += 0.25 * through( uv + o * px, b, q );
		}
	}

	vec3 pool   = toCode( light );
	float alpha = IsOver != 0 ? mix( clip.a, 1.0, MixAmount ) : 1.0;
	vec3 rgb    = IsOver != 0 ? mix( clip.rgb, pool, MixAmount ) : pool;
	fragColor   = vec4( rgb, alpha );
}
`;
