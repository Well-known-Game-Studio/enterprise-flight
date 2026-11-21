// Slonk - the alien layer over a world's archetype. The archetype stays the world's physical class (a temperate
// world, an ocean, a desert); the genome, rolled once per world from its own hash, says how it departs from the
// Earth-like version of that class: what its seas are of, what colour its sky scatters, whether a haze lies in its
// lowlands. Its strangeness, 0 to 1, sets how often each trait departs and, for the sky, how far: at 0 the genome
// changes nothing, so the home system and the charted neighbours look exactly as they did; the uncharted stars'
// worlds get more of it the further out they lie.
// Later phases add the climate axis and the biosphere here.

#pragma once

#include "CoreMinimal.h"
#include "WorldGenome.generated.h"

enum class EPlanetArchetype : uint8;
struct FPlanetStyle;

/** What a world's seas, lakes and rain are of. */
UENUM(BlueprintType)
enum class ESolvent : uint8
{
	Water,
	/** Ink-blue, as ammonia with metals dissolved in it is, violet in the shallows, and a little calmer: a cold world's sea. */
	Ammonia,
	/** Near-black and mirror-calm, as liquid methane and ethane lie under a thick sky. */
	Methane,
	/** Sulphuric: a heavy green-yellow, slow to swell. */
	Acid,
	/** Molten rock: the lava worlds' seas. */
	Magma,
	/** No seas. */
	None
};

USTRUCT(BlueprintType)
struct FWorldGenome
{
	GENERATED_BODY()

	/** How far the world departs from the Earth-like version of its archetype, 0 to 1. 0 changes nothing. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Genome") float Strangeness = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Genome") ESolvent Solvent = ESolvent::Water;
	/** The sky's colour turned round the colour wheel from the archetype's, degrees (0: as the archetype has it). The
	 *  sunset turns with it, so it stays the light the sky leaves behind. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Genome") float SkyHueShiftDeg = 0.f;
	/** The sky's saturation as a share of the archetype's: a sky turned far from blue is paler (a full-strength one
	 *  reads as a filter over the world, not as a sky). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Genome") float SkySaturation = 1.f;
	/** A haze lying in the lowlands: its scale height, km (0: none, the air's ordinary haze), how much it adds to the
	 *  style's Haze, and its colour. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Genome") float LowHazeKm = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Genome") float LowHazeDensity = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Genome") FLinearColor HazeTint = FLinearColor::White;
};

namespace SlonkGenome
{
	/** Whether a genome changes a world of this kind at all: not a giant (its bands are its own), not a moon, not a
	 *  world without air. A world it does not apply to keeps the strangeness-0 genome. */
	SLONK_API bool AppliesTo(EPlanetArchetype Type, bool bAtmosphere);
	/** The Rayleigh scattering colour the sky takes from a style's atmosphere colour: normalised by its brightest
	 *  channel, raised to the 1.6 (the style's sky-blue becomes the real Rayleigh ratio) and kept above 0.08. The sky
	 *  (SlonkGameMode) and the genome's brightness match both read it. */
	SLONK_API FLinearColor RayleighOf(const FLinearColor& AtmosphereColor);
	/** The solvent an archetype has on its Earth-like roll: water, molten rock on a lava world, none on the dry ones.
	 *  Mars counts among them, so its few low basins keep the archetype's blue water whatever the genome. */
	SLONK_API ESolvent NaturalSolvent(EPlanetArchetype Type);
	/** A world's genome from its hash: the same world always rolls the same one, and at strangeness 0 it is the
	 *  archetype's own (its natural solvent, nothing shifted). Draws from a stream of its own, so nothing else a
	 *  world rolls moves. */
	SLONK_API FWorldGenome Roll(EPlanetArchetype Type, float Strangeness, uint32 Hash);
	/** Applies the genome to a style the archetype resolved: the sky's colour, the haze, the seas. Strangeness 0, a giant
	 *  or a world without air changes nothing. Leaves alone everything the terrain is built from. */
	SLONK_API void Apply(const FWorldGenome& G, EPlanetArchetype Type, FPlanetStyle& S);
	/** How strange a world of an uncharted star is: the further the star, the stranger its worlds may be (a cap of
	 *  0.35 at 6 light-years rising to 1 at 30), each world 45-100% of that cap by its hash, and one in five of them
	 *  only faintly strange (under a tenth of it) even far out. The home system and the charted neighbours stay at 0. */
	SLONK_API float WorldStrangeness(float DistanceLy, uint32 WorldHash);
	SLONK_API const TCHAR* SolventName(ESolvent S);
	/** One line for the logs and the debug readout. */
	SLONK_API FString Describe(const FWorldGenome& G);
	/** The sky colour the far atmosphere shell takes from a style: its atmosphere colour at the shell's brightness
	 *  (SkyShellScale), with a tinted haze blended in, since the shell has no haze colour of its own. */
	SLONK_API FLinearColor ShellSkyColor(const FPlanetStyle& S);
}
