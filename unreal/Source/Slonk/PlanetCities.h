// Slonk - procedural settlements. The terrain shader lights the night side where three nested Worley fields
// (25.6 km regions, 6.4 km clusters, 1.6 km towns) overlap on low, wet, level land. This is the same field in
// point form: the towns near a place, and the buildings that stand in each, so what glows from orbit is what you
// fly over by day. Who lives there (Civilization.h) decides which points they settle, how their towns are laid out
// and what they build: hamlets of huts, villages round a keep, grid towns, spires and domes, grown rings of pods, or
// towns built into the cliffs on the ledges of a scarp or a canyon's wall.
#pragma once

#include "CoreMinimal.h"
#include "PlanetTerrain.h"
#include "BuildingMeshes.h"
#include "Civilization.h"

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
	/** A city at a cluster point (towers, a pad) rather than a town at a town point. */
	bool bCity = false;
	/** Built into a cliff: Dir is on the wall, RadiusCm how far it runs along it either way, Face the way the wall
	 *  faces (level, out from the rock). */
	bool bCliff = false;
	FVector Face = FVector::ZeroVector;
};

namespace SlonkCities
{
	/** The settlement weight (region x cluster x town) at a planet-local point, the shader's formula without the land gating. */
	float SettlementAt(const FVector& LocalCm);
	double CityRadiusCm(float Region);
	double TownRadiusCm(float Strength);
	/** Cities at the cluster points and towns at the town points outside them, within ReachCm of a planet-local point,
	 *  for a people: the share of points they settle (their density), and at each either a town built into a cliff
	 *  near it (as often as they like cliffs, where there is one) or one on land the shader would light: level, low,
	 *  dry-shod and wet enough for them. */
	SLONK_API void EnumerateTowns(const FPlanetTerrainParams& P, const FCivilization& Civ, const FVector& CentreLocalCm, double ReachCm, TArray<FTownSite>& Out, int32 MaxTowns = 300);
	/** Building transforms for a town by kind (planet-local, for a 1 m unit mesh: scale is the building's size in
	 *  metres; OutByKind has EBuildingKind::Count arrays), laid out as its people build. Safe off the game thread. */
	SLONK_API void BuildTown(const FPlanetTerrainParams& P, const FCivilization& Civ, const FTownSite& Town, TArray<FTransform>* OutByKind, int32 MaxBuildings = 2500);
}
