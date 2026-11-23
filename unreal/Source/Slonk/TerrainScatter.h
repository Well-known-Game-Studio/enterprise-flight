// Slonk - what lies on the ground and grows from it: rocks, and on a living world grass, shrubs and trees, placed
// over each terrain chunk from the chunk's own height grid so every one stands on the triangles that are drawn
// (the drawn mesh and the full field part by metres on rough ground). Each size class has a lattice in cube-face
// coordinates nested in the quadtree, one hashed candidate per cell: a chunk and its children find the same rock
// or tree at the same place, and only its height follows the finer mesh. A class rides on the chunks fine enough to
// show it, trees and boulders from a few hundred metres across down, pebbles and grass on the last level alone, so
// the quadtree's own detail levels are the scatter's. Density comes from the fields the material paints with
// (slope, rock, wet, ice, canyon wall, crater) and the refinement's own regimes, so a fan of boulders lies under the
// cliff the shader draws as cliff; the shrubs stand on the very dots the material paints as scrub. What the plants are
// is the world's flora (FFloraProfile, FloraTypes.h): its species by slot, the Earth's grass, bush, broadleaf and
// conifer in the four roles on the worlds near home, other life forms in their places and beside them on a strange
// world, and giants of a hundred metres and more on a lattice of their own. A strange world's landmarks (Landmarks.h:
// arches, a vanished people's stones, reef towers, mound cities) stand on lattices of theirs, one or two to a hundred
// square kilometres. Everything here is a pure function of the planet, the settings and the chunk: safe off the game
// thread, and every visit puts everything back where it was.

#pragma once

#include "CoreMinimal.h"
#include "Civilization.h"
#include "PlanetTerrain.h"
#include "GroundRocks.h"
#include "Landmarks.h"
#include "PlantMeshes.h"

/** The terrain material's colour parameters, which the rocks and the plants are tinted from. */
struct FScatterPalette
{
	FLinearColor Mountain = FLinearColor(0.55f, 0.52f, 0.50f);
	/** The wet ground's colour: the vegetation, on a living world. */
	FLinearColor Land = FLinearColor(0.30f, 0.36f, 0.18f);
	/** The dry ground's. */
	FLinearColor Land2 = FLinearColor(0.55f, 0.45f, 0.28f);
	FLinearColor Ice = FLinearColor(0.90f, 0.93f, 0.97f);
};

struct FScatterSettings
{
	bool bEnabled = true;
	/** Scales every class's density: 0 none, 1 as designed. */
	float Density = 1.f;
	/** Worlds with settlements keep the scatter off their streets and lots. */
	bool bCities = false;
	/** Who lives there, for where their towns stand (nobody: the Federation's colonists, if bCities). */
	FCivilization People;
	/** How much grows, 0..1: 1 on a temperate living world, a little scrub on a desert, nothing on the dead ones. */
	float Life = 0.f;
	/** What grows: the world's species by slot. The Earth's, unless the world rolled its own (SlonkFlora::Roll). */
	FFloraProfile Flora;
	/** The landmarks that stand on it (SlonkLandmarks::Roll): none, on every world near home. */
	FLandmarkProfile Landmarks;
	FScatterPalette Palette;
	/** A plant fades out this many edges of the coarsest chunk carrying it from the camera: under the distance at which
	 *  finer chunks carrying it arrive (the terrain component sets it from its split factor), so none pops in. */
	double FadeEdges = 3.0;
	/** At most this many of each class's far levels are used (0: each class on its own levels alone, as it ends). */
	int32 MaxFarLevels = 8;
};

namespace SlonkScatter
{
	/** The meshes the scatter places, one instanced set each: the rock shapes, then the flora's slots (SlonkFlora::Slot),
	 *  then the landmarks' kinds (ELandmark). The Earth's four plants are the first four slots, on every world: a strange
	 *  world's species of the same roles stand in them, and its others in the slots after. A slot with no species, and a
	 *  kind a world has none of, carries nothing. */
	constexpr int32 FirstPlant = SlonkGroundRocks::NumKinds;
	constexpr int32 FirstLandmark = FirstPlant + SlonkFlora::MaxSpecies;
	constexpr int32 NumMeshes = FirstLandmark + SlonkLandmarks::NumKinds;
	constexpr int32 PlantMesh(EPlant Kind) { return FirstPlant + (int32)Kind; }
	constexpr int32 FloraMesh(int32 Slot) { return FirstPlant + Slot; }
	constexpr int32 SlotOf(int32 Mesh) { return Mesh - FirstPlant; }
	constexpr bool IsPlant(int32 Mesh) { return Mesh >= FirstPlant && Mesh < FirstLandmark; }
	constexpr int32 LandmarkMesh(ELandmark Kind) { return FirstLandmark + (int32)Kind; }
	constexpr ELandmark LandmarkOf(int32 Mesh) { return (ELandmark)(Mesh - FirstLandmark); }
	constexpr bool IsLandmark(int32 Mesh) { return Mesh >= FirstLandmark && Mesh < NumMeshes; }

	/** Per-instance custom data. For a rock: tint RGB, snow cover, seed, the size in metres along the mesh's three
	 *  axes (its texture is laid in centimetres of rock, whatever the scale), and for a rock fallen from a canyon
	 *  wall the bed it came from (-1 for none) and how much of its colour it wears. For a plant: the foliage tint
	 *  RGB, snow, seed, the size in metres, how dry it is (0 lush, 1 parched), and the distance from the camera it
	 *  fades out by, cm (M_Foliage dithers it away over the last fifth; 0 never). */
	constexpr int32 NumCustomFloats = 10;
	/** Rocks at least this big across (cm), and trees at least this tall, get collision near the crew. */
	constexpr double SolidMinCm = 45.0;
	constexpr double SolidTreeCm = 200.0;
	/**
	 * A strange world's giants: a class of their own, on a lattice of about GiantSpacingCm, on chunks up to
	 * GiantMaxEdgeCm across and on GiantFarLevels coarser levels besides, half of them kept on each (as the trees thin
	 * with distance), from GiantMinCm to GiantMaxCm tall before their species' own height. They stand in loose groves
	 * (GiantDensity) on the wet, level ground the forests take and out over the meadows, none on a cliff, in lying snow,
	 * above the treeline or in a town. Whether one stands in a cell, where and how tall are read from the planet's
	 * field, not from a chunk's drawn mesh, so every level of the quadtree carries the same giants, and nothing else the
	 * scatter places stands in one's foot. A world without a giant species carries none, and its chunks are no coarser
	 * for them (MaxChunkEdgeCm).
	 */
	constexpr double GiantSpacingCm = 12000.0;
	constexpr double GiantMaxEdgeCm = 250000.0;
	constexpr int32 GiantFarLevels = 3;
	constexpr double GiantKeep = 0.5;
	constexpr double GiantMinCm = 6000.0;
	constexpr double GiantMaxCm = 20000.0;
	/**
	 * A strange world's landmarks: each kind on a lattice of its own of about LandmarkSpacingCm, a share of whose cells
	 * are sites for it (SlonkLandmarks::FKindSpec::Sites) where the ground suits the kind (an arch on rocky or dry
	 * ground, a vanished people's stones on open level ground, a reef's towers in the shallows off a coast, a mound city
	 * on open grassland; none on a cliff, in a river, on the world's own geology, in a town or across a way). At a site
	 * one stands, or a row or a cluster of them. Which cells are sites (a world's own: its hash is in their draws), what
	 * stands there, where and how big are read from the planet's field and the cell's own draws
	 * (SlonkScatter::LandmarksNear reads them the same way), never from a chunk's drawn mesh: every level of the
	 * quadtree carries the same landmarks, on chunks up to LandmarkMaxEdgeCm across (seen from thirty kilometres and
	 * more, a few pixels tall), and at every one of those levels nothing else the scatter places stands in one's foot.
	 * A world without any carries none, and its chunks are no coarser for them (MaxChunkEdgeCm).
	 */
	constexpr double LandmarkSpacingCm = 250000.0;
	constexpr double LandmarkMaxEdgeCm = 2.0e6;
	/** Chunks up to this edge keep their solid copies, cm: round someone on the ground, and under a ship hovering up to
	 *  about 6 km, whose transporter's arrival trace needs them (every level of every class at the default ground
	 *  detail). The coarser chunks further off keep none: on Horizon, most of the trees. */
	constexpr double SolidMaxEdgeCm = 2.5e5;
	/** Most rocks, and most plants, one chunk carries; the finer classes are the ones left out, and a class capped stops
	 *  row by row, so a cap reached would leave a strip bare. The trees' farthest level is one in 244 of the candidates
	 *  on a chunk to 20 km, whose cells snap to between 4.2 and 8.5 m, so 4^12 (16.8 million) candidates at most:
	 *  62,000 trees at the densest forest's 0.9, the most any chunk can be given. */
	constexpr int32 MaxPerChunk = 6000;
	constexpr int32 MaxPlantsPerChunk = 64000;
	/** The settings' last step (Horizon): the trees take as many more far levels as they need to reach this far, cm. */
	constexpr int32 HorizonStep = 4;
	constexpr double HorizonTreeReachCm = 2.0e6;
	/** A world's faces' chunk edge, cm: the arc across a cube face's middle. How many far levels a class uses is worked
	 *  out from it, so every chunk of a world uses the same (a chunk's own edge runs from 0.7 to 1 of its depth's share
	 *  of it across a face). */
	inline double FaceEdgeCm(double RadiusCm) { return RadiusCm * UE_DOUBLE_HALF_PI; }
	/** Period of the terrain material's planet-local noise, cm: the chunk origins reach the shader modulo this. */
	constexpr double NoisePeriodCm = 1048576.0;
	/** The material's scrub dots: one hashed disc per cell, a whole number of cells to the noise period along each
	 *  axis (about 450 cm a cell), so the lattice is periodic and a chunk whose origin has wrapped finds the same dots
	 *  on the same ground as its neighbour or its parent. */
	constexpr int32 ScrubCellsPerPeriod = 2330;
	constexpr double ScrubCellCm = NoisePeriodCm / ScrubCellsPerPeriod;
	/** A scrub cell's index on the periodic lattice (the one the shader hashes) from its index in a chunk's noise space. */
	SLONK_API FIntVector ScrubCellKey(const FIntVector& Cell);

	/** A chunk's grid as the chunk build has it, margin included. */
	struct FChunkGrid
	{
		int32 Face = 0, Depth = 0;
		int64 X = 0, Y = 0;
		int32 Steps = 0, Margin = 0, W = 0;
		double EdgeCm = 0.0;
		/** W * W of each, row-major from (-Margin, -Margin): the samples, their unit directions, and the drawn vertex
		 *  positions relative to the chunk origin (the water surface where there is water). */
		const FPlanetTerrainSample* Samples = nullptr;
		const FVector* Dirs = nullptr;
		const FVector* Pos = nullptr;
		/** The chunk origin, planet-local cm. */
		FVector Origin = FVector::ZeroVector;
	};

	struct FChunkScatter
	{
		/** Chunk-relative transforms by mesh: a rock's scale is its size in metres, a plant's its height. */
		TArray<FTransform> Transforms[NumMeshes];
		/** NumCustomFloats per instance, in the order of Transforms. */
		TArray<float> Custom[NumMeshes];
		/** The rocks and trees big enough to stand on or walk into, chunk-relative, by mesh: on chunks up to SolidMaxEdgeCm
		 *  only (the far levels' pieces are the same ones, on the coarser chunks further off). */
		TArray<FTransform> Solids[NumMeshes];
		int32 Num() const;
		int32 NumPlants() const;
	};

	/** The largest chunk edge any class rides on under the settings, on a world whose faces' chunk edge is FaceEdge
	 *  (FaceEdgeCm), cm: coarser chunks carry nothing. It only grows with the settings' MaxFarLevels. The giants count
	 *  only on a world whose flora has one. */
	SLONK_API double MaxChunkEdgeCm(const FScatterSettings& S, double FaceEdge);
	/** How many far levels the trees use under the settings on such a world, and how far from the camera they reach, cm,
	 *  where its chunks are their depth's share of FaceEdge (towards a face's edges they run smaller, and so a little
	 *  nearer). */
	SLONK_API int32 TreeFarLevels(const FScatterSettings& S, double FaceEdge);
	SLONK_API double TreeReachCm(const FScatterSettings& S, double FaceEdge);

	/**
	 * The settlements near one chunk that what is placed on it keeps out of (its towns' grounds, the ways the terrain
	 * paints round them), looked for the first time anything placed there asks and kept for the rest of the chunk's build:
	 * the scatter and the motes of one build share it, and a chunk where nothing asks looks for none. It holds the first
	 * chunk it was asked for; one build, one chunk.
	 */
	struct FKeepOut;
	struct SLONK_API FChunkKeepOut
	{
		FChunkKeepOut();
		~FChunkKeepOut();
		FChunkKeepOut(const FChunkKeepOut&) = delete;
		FChunkKeepOut& operator=(const FChunkKeepOut&) = delete;
		const FKeepOut& Get(const FPlanetTerrainParams& P, const FCivilization& People, const FChunkGrid& G);
	private:
		TUniquePtr<FKeepOut> KeepOut;
	};

	/** The rocks and plants on a chunk. Shared, if given, is the chunk build's keep-out (else one of its own is looked for). */
	SLONK_API void ScatterChunk(const FPlanetTerrainParams& P, const FScatterSettings& S, const FChunkGrid& G, FChunkScatter& Out, FChunkKeepOut* Shared = nullptr);

	/**
	 * The forest's density at each vertex of a chunk's grid ((Steps + 1)^2, row major, the margin left out): the
	 * chance of a tree in its lattice cell, the same that places the trees, before the world's life and the density
	 * setting. The terrain material makes it the share of the ground the crowns cover (UV3.y, times its TreeLife, which
	 * the terrain component sets from those) and paints the ground the canopy's colour by it, so a forest too far off
	 * for its trees to be drawn is there in colour and its trees fade in over their own colour rather than over bare
	 * soil. Every chunk carries it, however coarse; the settings reach it through the material, with no rebuild.
	 */
	SLONK_API void TreeCover(const FPlanetTerrainParams& P, const FChunkGrid& G, TArray<float>& Out);

	/** The slot of the species that stands at a spot, of a role's two (First, one of SlonkFlora::Slot's first of a role):
	 *  the first where the role has one species; else by stands a few hundred metres across, each species its share of
	 *  them, the edges between two stands mixed by Mix (a number 0..1 of the plant's own, the same at every level of the
	 *  quadtree). A pure function of the planet, the flora and the place. */
	SLONK_API int32 SpeciesAt(const FPlanetTerrainParams& P, const FFloraProfile& Flora, int32 First, const FVector& Up, double Mix);
	/** The chance of a giant in its lattice cell at a unit direction, as ScatterChunk reckons it, read from the field
	 *  (finer than ten metres left out) rather than a chunk's drawn mesh, before the world's life and the density
	 *  setting: 0 over water. For the searches and the tests. */
	SLONK_API double GiantDensityAt(const FPlanetTerrainParams& P, const FVector& Dir);

	/** A landmark standing on a world. */
	struct FLandmarkStand
	{
		ELandmark Kind = ELandmark::Arch;
		/** Its direction from the planet's middle, a unit vector in the planet's frame. */
		FVector Up = FVector::ZeroVector;
		/** Its size in its kind's own measure and how far round it nothing else stands, cm; and what it stands on, cm over
		 *  the sea's level: the field's ground there, or the sea's surface for a kind that stands in the sea. */
		double SizeCm = 0.0, FootCm = 0.0, BaseCm = 0.0;
		/** The site's cell on its face, and which of the site's it is: what its own draws are made of. */
		int32 Face = 0, Member = 0;
		int64 GX = 0, GY = 0;
		/** Its place on its face. */
		double U = 0.0, V = 0.0;
		/** How it is turned about its up, radians from the first tangent at its place (SlonkScatter::LandmarkTangents). */
		double Yaw = 0.0;
	};
	/** How well the ground at a direction suits a landmark of a kind and a size, 0 (not at all: none stands there) to 1,
	 *  read from the field as the sites are; Life is how much grows on the world (its woods keep the stones out). */
	SLONK_API double LandmarkGroundAt(const FPlanetTerrainParams& P, ELandmark Kind, const FVector& Dir, double SizeCm, float Life);
	/** The landmarks of a kind (all kinds for ELandmark::Count) standing within MaxCm over the ground of a direction, on
	 *  that direction's face of the planet, the nearest first: exactly those ScatterChunk places there. A pure function
	 *  of the planet and the settings. */
	SLONK_API void LandmarksNear(const FPlanetTerrainParams& P, const FScatterSettings& S, ELandmark Kind, const FVector& FromDir, double MaxCm, TArray<FLandmarkStand>& Out);
	/** The two level directions a landmark's place is laid out by, at right angles to its up and each other. */
	SLONK_API void LandmarkTangents(const FVector& Up, FVector& OutA, FVector& OutB);

	/** A planet-local position in the terrain material's noise space: each axis modulo NoisePeriodCm, as the chunk
	 *  origins reach the shader. */
	SLONK_API FVector ToNoiseSpace(const FVector& PlanetLocalCm);
	/** The material's scrub dot in the cell holding a point of noise space, exactly as the shader hashes it: false if
	 *  the cell has none; else the dot's centre (noise space) and radius, cm. */
	SLONK_API bool ScrubDotAt(const FVector& NoiseP, FVector& OutCentre, double& OutRadiusCm);

	/**
	 * The motes over the ground (pollen and gnats by day, fireflies at dusk and through the night), carried on the chunks
	 * as the plants are: a cloud of them hovers over a cell of a lattice of about 9 m in cube-face coordinates, nested in
	 * the quadtree like the plants' (a chunk and its children find the same cloud at the same place), on the chunks up to
	 * MoteMaxEdgeCm across and on the next level up for a share of them, fading out as the plants do. They are the fauna's
	 * (slonk.Fauna.Enable and slonk.Fauna.Density), not the scatter's: kept apart from FChunkScatter and its count of
	 * meshes, so nothing the scatter places moves for them.
	 */
	struct FMoteSettings
	{
		/** Whether any are placed: the fauna on and the motes among its kinds allowed, life and air, and M_Fauna able to
		 *  draw them (the terrain component sees to the last). */
		bool bEnabled = false;
		/** How much lives there (APlanetBody::GroundLife: 0 on a dead world), and slonk.Fauna.Density. */
		float Life = 0.f;
		float Density = 1.f;
		/** The clouds' colour by day, the pollen's (its gnats a dark shade of it): SlonkFauna::FFaunaLook::Pollen. */
		FLinearColor Pollen = FLinearColor(0.9f, 0.62f, 0.16f);
		/** The fireflies' light at night (SlonkFauna::FFaunaLook::Firefly): the material's, set on it, not on the clouds,
		 *  so a change of it builds nothing again. */
		FLinearColor Glow = FLinearColor(0.55f, 1.f, 0.08f);
		/** Whether two settings place the same clouds with the same data (the glow aside). */
		bool SamePlacement(const FMoteSettings& O) const
		{
			if (bEnabled != O.bEnabled) { return false; }
			return !bEnabled || (Life == O.Life && Density == O.Density && Pollen == O.Pollen);
		}
	};
	/** The motes' lattice: wanted spacing, cm (it snaps to a level of the quadtree on each world); all of them on chunks
	 *  this long or shorter, cm, and MoteKeep of them on the next level up, while the settings allow a far level. */
	constexpr double MoteSpacingCm = 900.0;
	constexpr double MoteMaxEdgeCm = 16000.0;
	constexpr int32 MoteFarLevels = 1;
	constexpr double MoteKeep = 0.25;
	/** Most clouds one chunk carries. */
	constexpr int32 MaxMotesPerChunk = 3000;
	/** The fauna a chunk carries, apart from its scatter: its mote clouds, chunk-relative (a cloud's scale is its span in
	 *  metres, the unit mesh being a metre across, its z the ground's up), with SlonkFauna::Custom::Num custom floats each
	 *  (SlonkFauna::Mote: what they hold). */
	struct FChunkFauna
	{
		TArray<FTransform> Motes;
		TArray<float> MoteCustom;
		int32 Num() const { return Motes.Num(); }
	};
	/** The largest chunk edge carrying any motes under the settings, cm (0: none). */
	SLONK_API double MoteMaxChunkEdgeCm(const FScatterSettings& S, const FMoteSettings& M);
	/** How many far levels the motes use under the scatter's settings (its ground detail): at most MoteFarLevels. */
	SLONK_API int32 MoteFarLevelsUsed(const FScatterSettings& S);
	/** The mote clouds on a chunk, on its drawn ground: none over water, none on a world without life or air; the most
	 *  over wet, level ground along a forest's edge, none on lying snow and ice. S gives the fade, the far levels and the
	 *  settlements to keep out of (Shared, if given, the chunk build's keep-out, which the scatter may have looked for
	 *  already). A pure function of the planet and the chunk, safe off the game thread. */
	SLONK_API void PlaceMotes(const FPlanetTerrainParams& P, const FScatterSettings& S, const FMoteSettings& M, const FChunkGrid& G, FChunkFauna& Out,
		FChunkKeepOut* Shared = nullptr);
	/** The chance of a mote cloud over a cell where a planet-local unit direction is, as PlaceMotes reckons it, from the
	 *  field (finer than ten metres left out) rather than a chunk's drawn mesh, before the world's life and the density
	 *  setting: 0 over water. For the searches. */
	SLONK_API double MoteDensityAt(const FPlanetTerrainParams& P, const FVector& Dir);
	/** The densest mote country near a planet-local direction, searched over the field within MaxCm of it (a few thousand
	 *  points, coarse then fine round the best): false if none is found there. Synchronous: debug use. */
	SLONK_API bool FindMoteSpot(const FPlanetTerrainParams& P, const FVector& From, double MaxCm, FVector& OutDir, double& OutDensity);

	/** What a spot offers whatever lives there, read from the field rather than from a chunk's drawn mesh (finer than
	 *  ten metres left out, near enough for a habitat): how wet it is, the regimes a chunk reads from the slope (level
	 *  ground, a cliff, the foot of a wall), how likely a tree is to stand there (TreeDensity, before the world's life),
	 *  the height over the snowline (0 at the sea, 1 at the snowline) and the ice; and over water, how deep it is. */
	struct FHabitat
	{
		double Wet = 0.0, Flat = 0.0, Cliff = 0.0, WallFoot = 0.0, Forest = 0.0, Alpine = 0.0, Ice = 0.0;
		bool bWater = false;
		/** The water's depth over the ground (0 on land) and the height above the sea-level sphere, cm. */
		double DepthCm = 0.0, HeightCm = 0.0;
		/** A strange world's own geology there (FPlanetTerrainSample::Geo), 0..1: 0 on a world without one. */
		double Geo = 0.0;
	};
	/** The habitat at a unit direction, planet-local. A pure function of the planet, safe off the game thread. */
	SLONK_API FHabitat HabitatAt(const FPlanetTerrainParams& P, const FVector& Dir);
}
