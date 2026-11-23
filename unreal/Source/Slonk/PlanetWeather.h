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

	/** The map's masses and cells (0..1 each) at a direction in the frame the map is laid over (the cloud shell's), read
	 *  between its texels and round its seam as the materials' sampler reads it. False without a map of that size. */
	bool SampleAt(const TArray<FColor>& Pixels, int32 W, int32 H, const FVector& Dir, float& OutMasses, float& OutCells);
	/** How much of the sky over a place the cloud layer covers, 0 (clear) to 1 (a deck), from the map's masses and cells
	 *  there, as M_SlonkVolumetricCloud lays its coverage out: the bias it adds to its layout (its Cloud_GlobalCoverage
	 *  as ASlonkGameMode sets it from the style's Coverage and CloudDeck, and what the masses and the cells push it by
	 *  about that Coverage), read as cover from a clear sky at a bias of -0.55 to a deck at 0.25 (the material's
	 *  scattered sky, the engine's default, is -0.2). WeatherIn is how far the map has faded in
	 *  (APlanetBody::GetWeatherStrength): at 0 the style's own even sky. The numbers are the material's node graph's:
	 *  change them together. */
	float CoverOf(float Masses, float Cells, float Coverage, float Deck, float WeatherIn);
	/** Where the sun's light on its way to a place crosses a shell of LayerRadiusCm about the planet's middle (the
	 *  middle of the cloud layer), both from that middle: the place itself if it is at or over the shell or no sun is
	 *  given. OutSunUp is how high the sun stands there, the cosine of its angle from the zenith (1 with no sun given). */
	FVector SunThroughLayer(const FVector& PlaceFromCentre, const FVector& ToSun, double LayerRadiusCm, double& OutSunUp);
	/** How much of the sun's light a sky covered that far keeps from all the ground under it: all of it under a deck
	 *  (cover 0.97 and over), none under anything less than a sky cloud nearly all over (to 0.88). Under a broken sky
	 *  the sun comes and goes between the clouds, and which it is at a place is the layer's own fine noise, which only
	 *  the renderer has: the map knows the share of the sky that is cloud there, not where each cloud's shadow falls;
	 *  and a veil over four fifths of the sky still lets the sun through (seen in game at a cover of 0.83, sharp
	 *  shadows on the ground; every sky of 0.96 and over was a deck with none). */
	float DeckShade(float Cover);
}
