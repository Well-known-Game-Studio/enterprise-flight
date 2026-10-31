// Slonk - per-world weather maps. An equirectangular texture over the cloud shell's frame that both the shell
// material and the ray-marched layer sample, so the two read as one sky at the hand-over: R the continent-scale
// masses (drawn into zonal fronts), G the kilometre cells, B the cloud type (0 heaped, 1 sheet).
#pragma once

#include "CoreMinimal.h"

class UTexture2D;

struct FPlanetWeatherSpec
{
	/** Noise offset: the planet's seed, so two worlds with one style never share a front. */
	FVector Seed = FVector::ZeroVector;
	/** Scale of the masses on the unit sphere (the shell's CloudScale: 4 gives a few masses per hemisphere). */
	float Scale = 4.f;
	/** 0..1: how far the masses are pulled into east-west fronts. */
	float Fronts = 0.5f;
	/** Scale of the cell field relative to the masses (60 puts the cells at 100 to 200 km on an Earth). */
	float CellScale = 60.f;
	/** 1 for a giant: zones and belts by latitude with festoons and storm ovals; B is then zone (1) against belt (0). */
	float Zonal = 0.f;
	/** Band frequency for a zonal map (the style's BandScale). */
	float BandScale = 14.f;
};

namespace SlonkWeather
{
	/** Fills Out (W x H, BGRA) with the weather fields over the sphere, row = latitude. Safe off the game thread. */
	void Generate(const FPlanetWeatherSpec& Spec, int32 W, int32 H, TArray<FColor>& Out);
	/** Writes the map as a PNG (debugging: set SLONK_DUMP_WEATHER in the editor's environment). Any thread. */
	void SavePng(const TArray<FColor>& Pixels, int32 W, int32 H, const FString& Path);
	/** A transient texture with a full mip chain built from the pixels. Game thread. */
	UTexture2D* MakeTexture(const TArray<FColor>& Pixels, int32 W, int32 H, const FName& Name);
}
