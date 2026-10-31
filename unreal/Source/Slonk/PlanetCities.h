// Slonk - procedural settlements. The terrain shader lights the night side where three nested Worley fields
// (25.6 km regions, 6.4 km clusters, 1.6 km towns) overlap on low, wet, level land. This is the same field in
// point form: the towns near a place, and the buildings that stand in each, so what glows from orbit is what you
// fly over by day.
#pragma once

#include "CoreMinimal.h"
#include "PlanetTerrain.h"

struct FTownSite
{
	/** Unit direction from the planet's centre, planet-local. */
	FVector Dir = FVector::ZAxisVector;
	/** The town's point on the sea-level sphere, planet-local cm. */
	FVector LocalCm = FVector::ZeroVector;
	/** Region x cluster weight at the point, 0..1: how big the town is. */
	float Strength = 0.f;
	/** Stable id (the cell hash). */
	uint32 Seed = 0;
	/** Radius of the built-up area, cm. */
	double RadiusCm = 0.0;
};

namespace SlonkCities
{
	/** The settlement weight (region x cluster x town) at a planet-local point, the shader's formula without the land gating. */
	float SettlementAt(const FVector& LocalCm);
	/** Towns within ReachCm of a planet-local point that stand on land the shader would light: level, wet, low, dry-shod. */
	void EnumerateTowns(const FPlanetTerrainParams& P, const FVector& CentreLocalCm, double ReachCm, TArray<FTownSite>& Out, int32 MaxTowns = 300);
	/** Building transforms for a town (planet-local, for a 1 m cube: scale is the building's size in metres). Safe off the game thread. */
	void BuildTown(const FPlanetTerrainParams& P, const FTownSite& Town, TArray<FTransform>& Out, int32 MaxBuildings = 2500);
}
