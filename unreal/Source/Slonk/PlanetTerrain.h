// Slonk - the planet height field. One deterministic function of a unit direction, evaluated in doubles on the
// CPU, so the terrain meshes, the ship's altitude and any future feature search all agree to the metre. The
// material only shades; it never moves a vertex.

#pragma once

#include "CoreMinimal.h"

/** Everything the height function needs, derived from the planet's style once per planet. */
struct FPlanetTerrainParams
{
	/** Shifts the whole noise field so two planets with the same style share no coastline. */
	FVector Seed = FVector::ZeroVector;
	double RadiusCm = 6.371e8;
	/** Height of the tallest peak above sea level, cm. */
	double ReliefCm = 3.8e6;
	/** Depth of the deepest ocean floor below sea level, cm. */
	double OceanDepthCm = 5.0e5;
	/** Fraction of the globe under water (0 = all land, 1 = all ocean). */
	float SeaLevel = 0.5f;
	/** Continent frequency (cycles across the sphere at the lowest octave). */
	float ContinentScale = 3.f;
	/** 0..1: how much of the land is ridged mountain ranges. */
	float Mountains = 0.6f;
	/** 0..1: river canyons cut into the plateaus (0 none). */
	float Canyons = 0.5f;
	/** Depth of the canyons, cm (floor below the surrounding plateau). */
	double CanyonDepthCm = 1.6e5;
	/** Width of the canyons, cm, from rim to rim. */
	double CanyonWidthCm = 1.8e6;
	/** 0..1: stepped rock strata on steep slopes (mesas, canyon walls). */
	float Strata = 0.6f;
	/** 0..1 impact craters; 1 is lunar. */
	float Craters = 0.f;
	/** Size scale of the crater fields (1 lunar; larger = smaller craters). */
	float CraterScale = 1.f;
	/** 0..1 smooth dark basins between cratered highlands. */
	float Maria = 0.f;
	/** 0..1 lineae: long double-ridged cracks across an icy crust (Europa). */
	float Lineae = 0.f;
	/** |z| of the unit normal where the polar ice begins; 2 disables caps. */
	float IceCapStart = 0.82f;
	/** 0..1 moisture bias (deserts are dry, ocean worlds wet). */
	float Moisture = 0.5f;
	/** Molten "ocean" worlds: the sea is lava. */
	bool bLava = false;
	/** Airless bodies get no erosion smoothing and no oceans. */
	bool bAirless = false;

	bool operator==(const FPlanetTerrainParams& O) const
	{
		return Seed == O.Seed && RadiusCm == O.RadiusCm && ReliefCm == O.ReliefCm && OceanDepthCm == O.OceanDepthCm && SeaLevel == O.SeaLevel
			&& ContinentScale == O.ContinentScale && Mountains == O.Mountains && Canyons == O.Canyons && CanyonDepthCm == O.CanyonDepthCm
			&& CanyonWidthCm == O.CanyonWidthCm && Strata == O.Strata && Craters == O.Craters && CraterScale == O.CraterScale && Maria == O.Maria && Lineae == O.Lineae
			&& IceCapStart == O.IceCapStart && Moisture == O.Moisture && bLava == O.bLava && bAirless == O.bAirless;
	}
	bool operator!=(const FPlanetTerrainParams& O) const { return !(*this == O); }
};

/** One sample of the field at a direction. */
struct FPlanetTerrainSample
{
	/** Height above the sea-level sphere, cm (negative under the sea). */
	double HeightCm = 0.0;
	/** Height of the solid ground; equals HeightCm on land, the sea floor under water. */
	double GroundCm = 0.0;
	/** Height of the water surface where bWater: 0 for the sea, the river's own level in a canyon. */
	double WaterCm = 0.0;
	/** 0..1 river in the inner gorge (1 mid-stream). */
	float River = 0.f;
	/** 0..1 how mountainous the ground is here (drives rock vs soil). */
	float Rock = 0.f;
	/** 0..1 moisture (drives vegetation vs desert). */
	float Wet = 0.f;
	/** 0..1 canyon mask (1 on the canyon floor). */
	float Canyon = 0.f;
	/** 0..1 canyon wall (peaks halfway down the wall). */
	float Wall = 0.f;
	/** How deep the canyon is cut here below the surrounding plateau, cm. */
	float CutCm = 0.f;
	/** 0..1 uplifted tableland (where canyons are cut). */
	float Table = 0.f;
	/** 0..1 ice. */
	float Ice = 0.f;
	/** 0..1 crater rim/floor term for the shading. */
	float Crater = 0.f;
	/** 0..1 inside a linea (the stained band between its ridges). */
	float Linea = 0.f;
	/** Rise over run of the coarse structure (0 flat, 1 is 45 degrees), set by the refinement stage. */
	float Slope = 0.f;
	/** Position in the canyon's stack of strata: layer index plus the fraction through it, over 8. 0 off the walls. */
	float Stratum = 0.f;
	bool bWater = false;
};

namespace SlonkTerrain
{
	/**
	 * The field has two stages, the way Outerra layers fractal detail over its elevation model. The structure is
	 * everything from continents down to RefineStartCm (ranges, canyons and their strata, craters, hills). The
	 * refinement synthesises what lies below that from the structure's local slope: fractured rock on cliffs,
	 * talus and boulders on the slopes beneath them, hummocky soil on the flats, down to the mesh cell. Both are
	 * deterministic functions of the direction, so nothing has to be stored for a place to look the same twice,
	 * and a measured elevation model could stand in for the structure later without touching the refinement.
	 */
	constexpr double RefineStartCm = 2.0e4;

	/**
	 * Height and biome at a unit direction (planet local frame), both stages. MinWavelengthCm bounds the finest
	 * octave that is evaluated: features smaller than a mesh cell only alias, so each LOD level passes its cell
	 * size and the coarser levels stay cheap. 0 evaluates everything down to the centimetre. The slope the
	 * refinement needs comes from two extra structure samples a metre away, so prefer SampleStructure plus Refine
	 * when sampling a grid.
	 */
	SLONK_API FPlanetTerrainSample Sample(const FPlanetTerrainParams& P, const FVector& Dir, double MinWavelengthCm = 0.0);

	/** The structure stage alone: nothing finer than RefineStartCm. */
	SLONK_API FPlanetTerrainSample SampleStructure(const FPlanetTerrainParams& P, const FVector& Dir, double MinWavelengthCm = 0.0);

	/** The refinement stage: adds detail from RefineStartCm down to MinWavelengthCm to a structure sample, given the
	 *  structure's slope there as rise over run. Does nothing under water or when MinWavelengthCm is coarser. */
	SLONK_API void Refine(const FPlanetTerrainParams& P, const FVector& Dir, double SlopeRiseOverRun, double MinWavelengthCm, FPlanetTerrainSample& S);

	/** Just the surface height (cm above the sea-level sphere; the sea itself is 0) at full detail. */
	SLONK_API double SurfaceHeightCm(const FPlanetTerrainParams& P, const FVector& Dir);

	/** Gradient noise in doubles on an integer lattice: stable at any frequency the sphere can carry. */
	SLONK_API double Noise3(const FVector& P);
	SLONK_API double Fbm(const FVector& P, int32 Octaves, double Lacunarity = 2.0, double Gain = 0.5);
}
