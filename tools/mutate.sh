#!/usr/bin/env bash
#
# Mutation testing: change ONE character of the shipped code and require that a
# check fails. A check that still passes against a mutant was not looking at
# the code it claims to cover -- and a harness that compiled its own copy of
# the shaders would pass every GLSL mutant here.
#
# Each mutant is a copy of the tree in a temporary directory (the FFGL SDK is
# symlinked, not copied), built arm64-only, with only the named check run at
# CI's raster, under a time limit (a mutant can hang: honeydew's lesson; a
# timeout counts as caught).
#
#     tools/mutate.sh
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# file | the exact text | its one-character mutant | the check that must fail | what it is
# The fields are split on '|', so neither a target nor a mutant may contain one
# (conway's trap).
MUTANTS=(
	"source/Shaders.cpp|Gravity * k + tension * k * k * k|Gravity * k - tension * k * k * k|--dispersion|GLSL: the evolve's surface tension with the wrong sign, + -> -"
	"source/Shaders.cpp|float gamma   = 2.0 * Kinematic * k * k;|float gamma   = 3.0 * Kinematic * k * k;|--dispersion|GLSL: Lamb's damping 3 nu k^2, 2 -> 3"
	"source/Shaders.cpp|e      = e - ( e / span ) * span;|e      = e + ( e / span ) * span;|--fft|GLSL: the twiddle's index not reduced mod span, - -> +"
	"source/Shaders.cpp|m * g * g * ( Hy - Sa * g )|m * g * g * ( Hy + Sa * g )|--hysteresis|GLSL: the saturation pushing the spikes up, - -> +"
	"source/Shaders.cpp|v += Dt * k * InvRho|v -= Dt * k * InvRho|--hysteresis|GLSL: the stress kicking the modes backwards, + -> -"
	"source/Shaders.cpp|field += Currents[ c ] * lookup|field -= Currents[ c ] * lookup|--spread|GLSL: the coil's field subtracted, + -> -"
	"source/Shaders.cpp|0.5 * ( s2 + dot( rp, rp ) )|0.6 * ( s2 + dot( rp, rp ) )|--fresnel|GLSL: the unpolarised mean 0.6 of the sum, 5 -> 6"
	"source/Shaders.cpp|vec2 suv = 0.5 + q * ScreenScale;|vec2 suv = 0.6 + q * ScreenScale;|--mirror|GLSL: the screen's centre off the pool's, 5 -> 6"
	"source/Fluid.cpp|op.r0 / ( 1.0 + op.r0 )|op.r0 / ( 2.0 + op.r0 )|--dispersion|C++: Cowley-Rosensweig's r0 / ( 2 + r0 ), 1 -> 2"
	"source/Fluid.cpp|if( mid + Magnetisation( fluid, mid ) < op.applied )|if( mid - Magnetisation( fluid, mid ) < op.applied )|--onset|C++: the demagnetising jump with the wrong sign, + -> -"
	"source/Coil.cpp|( ( a * a - r2 ) * E + alpha2 * K )|( ( a * a + r2 ) * E + alpha2 * K )|--coil|C++: the loop's elliptic formula with + r^2, - -> +"
	"source/Coil.cpp|std::exp( -dt / tau )|std::exp( -dt * tau )|--coil|C++: the L/R lag's exponent dt tau, / -> *"
	"source/Drive.cpp|( boundary - p0 ) / ( p1 - p0 )|( boundary + p0 ) / ( p1 - p0 )|--beat|C++: the beat's place in its frame from the sum of phases, - -> +"
	"source/Audio.cpp|			flux = 0.0f;|			flux = 1.0f;|--audio|C++: the first frame primed with a flux of 1, not 0, 0 -> 1"
)

limit() {
	# macOS has no timeout(1): perl's alarm is enough.
	perl -e 'alarm shift; exec @ARGV' "$@"
}

caught=0
for entry in "${MUTANTS[@]}"; do
	IFS='|' read -r file original mutant check what <<<"$entry"
	tree="$WORK/tree"
	rm -rf "$tree"
	mkdir -p "$tree/external"
	cp -R "$REPO/source" "$REPO/tools" "$REPO/cmake" "$REPO/CMakeLists.txt" "$tree/"
	ln -s "$REPO/external/ffgl" "$tree/external/ffgl"

	python3 - "$tree/$file" "$original" "$mutant" <<'PY'
import sys, pathlib
path, original, mutant = pathlib.Path(sys.argv[1]), sys.argv[2], sys.argv[3]
text = path.read_text()
if text.count(original) != 1:
    sys.exit(f"mutation target found {text.count(original)} times in {path}: '{original}'")
if sum(a != b for a, b in zip(original, mutant)) != 1 or len(original) != len(mutant):
    sys.exit("a mutant must differ by exactly one character")
path.write_text(text.replace(original, mutant))
PY

	printf '\n== mutant: %s\n' "$what"
	cmake -S "$tree" -B "$tree/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 >/dev/null
	cmake --build "$tree/build" --target fetest -j8 >/dev/null 2>&1
	status=0
	extra=()
	[[ "$check" == "--coil" || "$check" == "--beat" ]] || extra=( --size 320x180 )
	limit 300 "$tree/build/fetest" "$check" "${extra[@]}" >"$WORK/log" 2>&1 || status=$?
	if [[ "$status" -eq 0 ]]; then
		printf '   FAIL  %s still PASSES -- the check does not cover this code\n' "$check"
	else
		printf '   ok    %s fails against the mutant (exit %d):\n' "$check" "$status"
		# A mutant that timed out printed no FAIL: grep finding none must not end the run.
		{ grep -E '^  FAIL' "$WORK/log" || true; } | head -2 | cut -c1-160 | sed 's/^/        /'
		caught=$(( caught + 1 ))
	fi
done

printf '\nmutants: %d, caught: %d\n' "${#MUTANTS[@]}" "$caught"
[[ "$caught" -eq "${#MUTANTS[@]}" ]]
