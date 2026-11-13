// Slonk - what lies on the ground: rocks, placed over each terrain chunk from the chunk's own height grid so every
// one stands on the triangles that are drawn (the drawn mesh and the full field part by metres on rough ground).
// Each size class has a lattice in cube-face coordinates nested in the quadtree, one hashed candidate per cell: a
// chunk and its children find the same rock at the same place, and only its height follows the finer mesh. A
// class rides on the chunks fine enough to show it, boulders from a hundred and fifty metres across down, pebbles
// on the last level alone, so the quadtree's own detail levels are the scatter's. Density comes from the fields the
// material paints with (slope, rock, wet, ice, canyon wall, crater) and the refinement's own regimes, so a fan of
// boulders lies under the cliff the shader draws as cliff. Everything here is a pure function of the planet and
// the chunk: safe off the game thread, and every visit puts every rock back where it was.

#pragma once

#include "CoreMinimal.h"
#include "PlanetTerrain.h"
#include "GroundRocks.h"

/** The terrain material's colour parameters, which the rocks are tinted from. */
struct FScatterPalette
{
	FLinearColor Mountain = FLinearColor(0.55f, 0.52f, 0.50f);
	FLinearColor Land = FLinearColor(0.30f, 0.36f, 0.18f);
	FLinearColor Land2 = FLinearColor(0.55f, 0.45f, 0.28f);
	FLinearColor Ice = FLinearColor(0.90f, 0.93f, 0.97f);
};

struct FScatterSettings
{
	bool bEnabled = true;
	/** Scales every class's density: 0 none, 1 as designed. */
	float Density = 1.f;
	/** Worlds with settlements keep the rocks off their streets and lots. */
	bool bCities = false;
	FScatterPalette Palette;
};

namespace SlonkScatter
{
	/** Per-instance custom data the rock material reads: tint RGB, snow cover, seed, the size in metres along the
	 *  mesh's three axes (its texture is laid in centimetres of rock, whatever the scale), and for a rock fallen
	 *  from a canyon wall the bed it came from (-1 for none) and how much of its colour it wears. */
	constexpr int32 NumCustomFloats = 10;
	/** Rocks at least this big across (cm) get collision near the crew: something to stand on or walk into. */
	constexpr double SolidMinCm = 45.0;
	/** Most rocks one chunk carries; the finer classes are the ones left out. */
	constexpr int32 MaxPerChunk = 6000;

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
		/** Chunk-relative transforms by kind, for a mesh filling the unit box: the scale is the size in metres. */
		TArray<FTransform> Transforms[SlonkGroundRocks::NumKinds];
		/** NumCustomFloats per instance, in the order of Transforms. */
		TArray<float> Custom[SlonkGroundRocks::NumKinds];
		/** The rocks of SolidMinCm and over, chunk-relative, by kind. */
		TArray<FTransform> Solids[SlonkGroundRocks::NumKinds];
		int32 Num() const;
	};

	/** The largest chunk edge any class rides on, cm: coarser chunks carry nothing. */
	SLONK_API double MaxChunkEdgeCm();

	SLONK_API void ScatterChunk(const FPlanetTerrainParams& P, const FScatterSettings& S, const FChunkGrid& G, FChunkScatter& Out);
}
