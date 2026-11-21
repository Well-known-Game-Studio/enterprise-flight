// Slonk - the planet height field. One deterministic function of a unit direction, evaluated in doubles on the
// CPU, so the terrain meshes, the ship's altitude and any future feature search all agree to the metre. The
// material only shades; it never moves a vertex.

#pragma once

#include "CoreMinimal.h"

/** How a world's temperature is laid over its globe. */
enum class EClimateModel : uint8
{
	/** The home worlds' climate, as it always was: the snowline falls with the latitude to the sea at the poles. */
	Classic,
	/** A spinning world's, warmed or cooled as a whole by its genome (FPlanetTerrainParams::WarmthN): the snowline still
	 *  falls with the latitude, and the seas freeze where the air at their surface does. */
	Spin,
	/** A world held to its star by the tides: hot under the star, cold on the far side for ever, with a temperate ring
	 *  between, about ClimateAxis instead of the spin's. */
	Locked
};

/** Everything the height function needs, derived from the planet's style once per planet. */
struct FPlanetTerrainParams
{
	/** Shifts the whole noise field so two planets with the same style share no coastline. */
	FVector Seed = FVector::ZeroVector;
	double RadiusCm = 6.371e8;
	/** The relief, cm: the scale of the land's heights. The highest peaks of the ranges stand at about seven tenths
	 *  of it (an Earth's 10 km gives about 7 km), the plateaus and lowlands lower. */
	double ReliefCm = 3.8e6;
	/** Depth of the deepest ocean floor below sea level, cm. */
	double OceanDepthCm = 5.0e5;
	/** Fraction of the globe under water (0 = all land, 1 = all ocean). */
	float SeaLevel = 0.5f;
	/** Continent frequency (cycles across the sphere at the lowest octave). */
	float ContinentScale = 3.f;
	/** 0..1: how much of the land is ridged mountain ranges (coverage, not height; below 0.2 they fade out). */
	float Mountains = 0.6f;
	/** 0..1: how tall a range's core stands, as a share of what the relief allows. 1 puts a world's tallest ground
	 *  in its mountains; a moon's ranges stay low beside the craters its relief was tuned for. */
	float MountainHeight = 1.f;
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
	/** Height of the permanent snowline at the equator, cm (an Earth's is about 5 km). It falls with latitude, as
	 *  cos^1.5, to the sea at the poles (SnowlineCm()); the treeline follows it. 0 for a world with no snow. */
	double SnowlineCm = 5.0e5;
	/** Molten "ocean" worlds: the sea is lava. */
	bool bLava = false;
	/** Airless bodies get no erosion smoothing and no oceans. */
	bool bAirless = false;
	/** How the world's temperature is laid out (EClimateModel). Classic, the home worlds', reads none of the fields below. */
	EClimateModel Climate = EClimateModel::Classic;
	/** The climate's axis, planet-local and unit: on a locked world it points at the star. */
	FVector ClimateAxis = FVector::ZAxisVector;
	/** A spinning world's warmth over an Earth's, as a share of its solvent's liquid range (SlonkTerrain::LapseNPerCm):
	 *  it raises or lowers the snowline everywhere by WarmthN / LapseNPerCm. */
	float WarmthN = 0.f;
	/** A locked world's temperature at sea level, as a share of its solvent's liquid range above freezing: under the star
	 *  (DayN), on the terminator (TermN) and over the far side's cap (NightN, below 0). The night falls from TermN to NightN
	 *  over the first NightWidth of the far hemisphere (in the cosine of the angle from the star) and stays there. */
	float DayN = 0.f;
	float TermN = 0.f;
	float NightN = 0.f;
	float NightWidth = 0.35f;

	bool operator==(const FPlanetTerrainParams& O) const
	{
		return Seed == O.Seed && RadiusCm == O.RadiusCm && ReliefCm == O.ReliefCm && OceanDepthCm == O.OceanDepthCm && SeaLevel == O.SeaLevel
			&& ContinentScale == O.ContinentScale && Mountains == O.Mountains && MountainHeight == O.MountainHeight && Canyons == O.Canyons && CanyonDepthCm == O.CanyonDepthCm
			&& CanyonWidthCm == O.CanyonWidthCm && Strata == O.Strata && Craters == O.Craters && CraterScale == O.CraterScale && Maria == O.Maria && Lineae == O.Lineae
			&& IceCapStart == O.IceCapStart && Moisture == O.Moisture && SnowlineCm == O.SnowlineCm && bLava == O.bLava && bAirless == O.bAirless
			&& Climate == O.Climate && ClimateAxis == O.ClimateAxis && WarmthN == O.WarmthN && DayN == O.DayN && TermN == O.TermN && NightN == O.NightN && NightWidth == O.NightWidth;
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
	/** 0..1 wall: a canyon's walls (peaking halfway down), and the flanks of a scarp and of its mesas and buttes. */
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
	/** Position in the stack of strata on a wall: layer index plus the fraction through it, over 8, by the canyon's
	 *  staircase on its walls and by height on a scarp's and its islands' flanks. 0 off the walls. */
	float Stratum = 0.f;
	/** 0..1 badlands: the soft beds bared below an escarpment, which the refinement cuts into gullies. */
	float Badland = 0.f;
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
	/** The distance the structure's slope is measured over for the refinement, whatever the detail level: the
	 *  chunks take it from a lattice this far apart, the point sample from two structure samples this far away. */
	constexpr double SlopeStepCm = 2.0e3;

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

	/** The permanent snowline at a direction, cm above the sea: P.SnowlineCm at the equator falling as cos(latitude)^1.5
	 *  (an Earth's 5 km gives 3 km at 45 degrees, 1.8 km at 60, 1 km at 70), 0 at the poles. It is the freezing
	 *  isotherm: on a world with a climate of its own (P.Climate not Classic) it is where the air's temperature falls to
	 *  the solvent's freezing point, and below the sea (negative) where even the sea's surface freezes. */
	SLONK_API double SnowlineCm(const FPlanetTerrainParams& P, const FVector& Dir);

	/**
	 * How fast the air cools with height, as a share of the solvent's liquid range per cm: Earth's 6.5 K a kilometre
	 * over water's 100 K is 6.5e-7, as is Titan's 1.3 K a kilometre over methane's 21. Temperatures in the climate are
	 * kept in that share (TempN: 0 freezing, 1 boiling), so a world's habitats read the same whatever its seas are of;
	 * kelvin are only a readout (SlonkGenome::FreezeK, RangeK). At an Earth's 5 km equatorial snowline the sea there
	 * stands at 0.33 of the range, about 30 C.
	 */
	constexpr double LapseNPerCm = 6.5e-7;
	/** The temperature at sea level at a direction, as a share of the solvent's liquid range (0 freezing, 1 boiling). */
	SLONK_API double SeaTempN(const FPlanetTerrainParams& P, const FVector& Dir);
	/** The temperature of the ground at a direction and height (cm above the sea; the sea's surface below it), as a
	 *  share of the liquid range: the sea's, cooled by LapseNPerCm with the height. 0 at the snowline. */
	SLONK_API double TemperatureN(const FPlanetTerrainParams& P, const FVector& Dir, double HeightCm);
	/** How far a direction lies toward the climate's cold, 0..1, for the searches that look for temperate ground: the
	 *  latitude (|z| of the unit direction) on a spinning world, the distance from the terminator (|cos| of the angle
	 *  from the star) on a locked one. */
	SLONK_API double ClimateColdness(const FPlanetTerrainParams& P, const FVector& Dir);

	/** How far round a snowline LineCm high its snow thins out: patchy cover from this far below it, deep from twice
	 *  this above it. A few hundred metres at an Earth's equator, never under 20 m. */
	SLONK_API double SnowBandCm(double LineCm);

	/** A world's relief (FPlanetTerrainParams::ReliefCm), cm, from the fraction of the radius it would be on an Earth-size world.
	 *  Weaker gravity holds up taller mountains, so smaller worlds get more relief for their size: it goes as the
	 *  square root of the radius from the Earth-size figure (Earth 0.16% keeps 10 km; a world a tenth the size gets
	 *  3 km, not 1; at Mars's size and 0.6%, 28 km, near Mars's own 29). */
	SLONK_API double ReliefForRadius(double FractionAtEarthSize, double RadiusCm);

	/** A length of the landscape given at an Earth's size (a canyon's depth or width), on a world of this radius: by
	 *  the same square root as the relief (ReliefForRadius), so a world's canyons keep their proportion to its
	 *  mountains and plateaus (the Grand Canyon's 1.6 km on an Earth is 506 m on a world a tenth the size, not 150). */
	SLONK_API double ScaleWithWorld(double AtEarthSizeCm, double RadiusCm);

	/** Gradient noise in doubles on an integer lattice: stable at any frequency the sphere can carry. */
	SLONK_API double Noise3(const FVector& P);
	SLONK_API double Fbm(const FVector& P, int32 Octaves, double Lacunarity = 2.0, double Gain = 0.5);

	/** The cube-sphere the terrain's quadtree lies on: the unit direction at (U, V) in -1..1 on a face, tangent-warped
	 *  so equal steps are near-equal angles (chunks are the same size at a face's middle and its corners). */
	SLONK_API FVector CubeDir(int32 Face, double U, double V);
	/** The inverse of CubeDir: which face a direction falls on and its (U, V) there; false if the round trip fails. */
	SLONK_API bool FaceUV(const FVector& Dir, int32& OutFace, double& OutU, double& OutV);
}
