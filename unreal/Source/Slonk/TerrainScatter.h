// Slonk - what lies on the ground and grows from it: rocks, and on a living world grass, shrubs and trees, placed
// over each terrain chunk from the chunk's own height grid so every one stands on the triangles that are drawn
// (the drawn mesh and the full field part by metres on rough ground). Each size class has a lattice in cube-face
// coordinates nested in the quadtree, one hashed candidate per cell: a chunk and its children find the same rock
// or tree at the same place, and only its height follows the finer mesh. A class rides on the chunks fine enough to
// show it, trees and boulders from a few hundred metres across down, pebbles and grass on the last level alone, so
// the quadtree's own detail levels are the scatter's. Density comes from the fields the material paints with
// (slope, rock, wet, ice, canyon wall, crater) and the refinement's own regimes, so a fan of boulders lies under the
// cliff the shader draws as cliff; the shrubs stand on the very dots the material paints as scrub. Everything here
// is a pure function of the planet and the chunk: safe off the game thread, and every visit puts everything back
// where it was.

#pragma once

#include "CoreMinimal.h"
#include "PlanetTerrain.h"
#include "GroundRocks.h"
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
	/** How much grows, 0..1: 1 on a temperate living world, a little scrub on a desert, nothing on the dead ones. */
	float Life = 0.f;
	FScatterPalette Palette;
};

namespace SlonkScatter
{
	/** The meshes the scatter places, one instanced set each: the rock shapes, then the plants. */
	constexpr int32 FirstPlant = SlonkGroundRocks::NumKinds;
	constexpr int32 NumMeshes = SlonkGroundRocks::NumKinds + SlonkPlants::NumKinds;
	constexpr int32 PlantMesh(EPlant Kind) { return FirstPlant + (int32)Kind; }
	constexpr bool IsPlant(int32 Mesh) { return Mesh >= FirstPlant; }

	/** Per-instance custom data. For a rock: tint RGB, snow cover, seed, the size in metres along the mesh's three
	 *  axes (its texture is laid in centimetres of rock, whatever the scale), and for a rock fallen from a canyon
	 *  wall the bed it came from (-1 for none) and how much of its colour it wears. For a plant: the foliage tint
	 *  RGB, snow, seed, the size in metres, how dry it is (0 lush, 1 parched), and 0. */
	constexpr int32 NumCustomFloats = 10;
	/** Rocks at least this big across (cm), and trees at least this tall, get collision near the crew. */
	constexpr double SolidMinCm = 45.0;
	constexpr double SolidTreeCm = 200.0;
	/** Most rocks, and most plants, one chunk carries; the finer classes are the ones left out. */
	constexpr int32 MaxPerChunk = 6000;
	constexpr int32 MaxPlantsPerChunk = 9000;
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
		/** The rocks and trees big enough to stand on or walk into, chunk-relative, by mesh. */
		TArray<FTransform> Solids[NumMeshes];
		int32 Num() const;
		int32 NumPlants() const;
	};

	/** The largest chunk edge any class rides on, cm: coarser chunks carry nothing. */
	SLONK_API double MaxChunkEdgeCm();

	SLONK_API void ScatterChunk(const FPlanetTerrainParams& P, const FScatterSettings& S, const FChunkGrid& G, FChunkScatter& Out);

	/** A planet-local position in the terrain material's noise space: each axis modulo NoisePeriodCm, as the chunk
	 *  origins reach the shader. */
	SLONK_API FVector ToNoiseSpace(const FVector& PlanetLocalCm);
	/** The material's scrub dot in the cell holding a point of noise space, exactly as the shader hashes it: false if
	 *  the cell has none; else the dot's centre (noise space) and radius, cm. */
	SLONK_API bool ScrubDotAt(const FVector& NoiseP, FVector& OutCentre, double& OutRadiusCm);
}
