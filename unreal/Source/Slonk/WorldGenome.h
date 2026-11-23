// Slonk - the alien layer over a world's archetype. The archetype stays the world's physical class (a temperate
// world, an ocean, a desert); the genome, rolled once per world from its own hash, says how it departs from the
// Earth-like version of that class: what its seas are of, what colour its sky scatters, whether a haze lies in its
// lowlands. Its strangeness, 0 to 1, sets how often each trait departs and, for the sky, how far: at 0 the genome
// changes nothing, so the home system and the charted neighbours look exactly as they did; the uncharted stars'
// worlds get more of it the further out they lie. It sets the world's climate too: how much warmer or colder it runs
// than the Earth-like version, or whether the tides hold one face to its star for ever, baked under it, frozen
// behind it, with a temperate ring between.
// What grows on it is rolled from it, apart: its flora (FloraProfile.h), as its animals are (FaunaProfile.h).

#pragma once

#include "CoreMinimal.h"
#include "WorldGenome.generated.h"

enum class EPlanetArchetype : uint8;
struct FPlanetStyle;
struct FPlanetTerrainParams;

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
	/** The tides hold one face to the star: the world does not turn, and its climate is laid about the star's bearing
	 *  (EClimateModel::Locked) rather than its poles. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Genome") bool bTidallyLocked = false;
	/** A spinning world's warmth over the Earth-like version's, as a share of its solvent's liquid range (+-0.12 at
	 *  strangeness 1, about 1.8 km of snowline): unused while it is locked. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Genome") float WarmthN = 0.f;
	/** A locked world's heat under the star and on its terminator, as multiples of the Earth-like version's equatorial
	 *  sea temperature (the archetype's own snowline cooled to the sea, not an Earth's), and how far below freezing its
	 *  far side lies, as a share of the liquid range. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Genome") float DayHeat = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Genome") float TermHeat = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Genome") float NightCold = 0.f;
	/** The world's own geology (EGeology, PlanetTerrain.h: 0 none), and how much of its land it takes, 0..1: fields of
	 *  crystal, pans cracked into polygons, terraced pools, forests of spires or plains of glass. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Genome") uint8 Geology = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Genome") float GeologyStrength = 0.f;
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
	/** Whether a world of this kind has a climate the genome can move: a snowline and air to carry it (a temperate world,
	 *  an ocean, a desert, an ice world). Only these warm, cool or lock. */
	SLONK_API bool HasClimate(EPlanetArchetype Type);
	/** A world's genome from its hash: the same world always rolls the same one, and at strangeness 0 it is the
	 *  archetype's own (its natural solvent, nothing shifted, no climate of its own). Draws from a stream of its own,
	 *  so nothing else a world rolls moves. TidalPull, 0..1, is how hard its star's tides pull on it (1 close in to the
	 *  star, 0 from 0.6 AU out): a world with a climate is locked with a chance of strangeness x (0.15 + 0.7 x pull). */
	SLONK_API FWorldGenome Roll(EPlanetArchetype Type, float Strangeness, uint32 Hash, float TidalPull = 0.f);
	/** Settles the lock once the world's place is known: a world with no bearing to its star (one placed by hand) is
	 *  never locked, and a forced lock (bForce, a debug spawn) holds any strange world that has a climate. */
	SLONK_API void SettleLock(FWorldGenome& G, EPlanetArchetype Type, const FVector& StarBearing, bool bForce);
	/** Applies the genome to a style the archetype resolved: the sky's colour, the haze, the seas. Strangeness 0, a giant
	 *  or a world without air changes nothing. Leaves alone everything the terrain is built from. */
	SLONK_API void Apply(const FWorldGenome& G, EPlanetArchetype Type, FPlanetStyle& S);
	/** Applies the genome's climate to the terrain's parameters: a locked world's laid about StarBearing (planet-local,
	 *  unit), a spinning one's warmth. Strangeness 0, or a world without a climate, changes nothing. */
	SLONK_API void ApplyClimate(const FWorldGenome& G, EPlanetArchetype Type, const FVector& StarBearing, FPlanetTerrainParams& P);
	/** Gives the terrain's parameters the genome's geology. Strangeness 0, a world without one or one whose crust is
	 *  already cracked into lineae changes nothing. (A Mars's and a Venus's ground, which the terrain shapes without
	 *  rain as it does an airless world's, takes its geology like any other: only worlds with air roll one.) */
	SLONK_API void ApplyGeology(const FWorldGenome& G, FPlanetTerrainParams& P);
	/** The two colours the terrain material draws a world's geology in (its GeoColor and GeoColor2), from the style the
	 *  genome has been applied to: a crystal field's own hue and its shadowed faces; a pan's pale crust and its cracks;
	 *  a terrace's stone and its pools (the world's shallows); a spire's rock and its darker beds; a glass plain's dark
	 *  sheet and the pale lines it is crazed with. */
	SLONK_API void GeologyColors(const FWorldGenome& G, const FPlanetStyle& S, FLinearColor& OutA, FLinearColor& OutB);
	SLONK_API const TCHAR* GeologyName(uint8 Geology);
	/** A solvent's freezing point and its liquid range, K: the temperature a share of the range (TempN) reads as is
	 *  FreezeK + RangeK x TempN. Water 273 and 100, ammonia 195 and 45, methane 91 and 21, sulphuric acid 283 and 327. */
	SLONK_API float FreezeK(ESolvent S);
	SLONK_API float RangeK(ESolvent S);
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
