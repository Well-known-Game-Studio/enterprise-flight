#include "WorldGenome.h"
#include "PlanetBody.h"

namespace
{
	/** A colour turned round the colour wheel, its saturation and brightness kept. */
	FLinearColor TurnHue(const FLinearColor& C, float Deg, float Saturation = 1.f)
	{
		FLinearColor Hsv = C.LinearRGBToHSV();
		Hsv.R = FMath::Fmod(Hsv.R + Deg + 720.f, 360.f);
		Hsv.G = FMath::Clamp(Hsv.G * Saturation, 0.f, 1.f);
		FLinearColor Out = Hsv.HSVToLinearRGB();
		Out.A = C.A;
		return Out;
	}

	FLinearColor Normalised(const FLinearColor& C)
	{
		const float M = FMath::Max3(C.R, C.G, C.B);
		return M > KINDA_SMALL_NUMBER ? FLinearColor(C.R / M, C.G / M, C.B / M, 1.f) : FLinearColor::White;
	}

	/** How often a world of the archetype has a haze lying in its lowlands, at strangeness 1. */
	float HazeChance(EPlanetArchetype Type)
	{
		switch (Type)
		{
		case EPlanetArchetype::Terrestrial: case EPlanetArchetype::Ocean: return 0.7f;
		case EPlanetArchetype::Lava: return 0.6f;      // ash
		case EPlanetArchetype::Venus: case EPlanetArchetype::Ice: return 0.5f;
		case EPlanetArchetype::Desert: return 0.4f;    // dust
		case EPlanetArchetype::Mars: return 0.3f;
		default: return 0.f;
		}
	}
}

ESolvent SlonkGenome::NaturalSolvent(EPlanetArchetype Type)
{
	switch (Type)
	{
	case EPlanetArchetype::Terrestrial: case EPlanetArchetype::Ocean: case EPlanetArchetype::Ice: return ESolvent::Water;
	case EPlanetArchetype::Lava: return ESolvent::Magma;
	default: return ESolvent::None;
	}
}

bool SlonkGenome::AppliesTo(EPlanetArchetype Type, bool bAtmosphere)
{
	return bAtmosphere && Type != EPlanetArchetype::GasGiant && Type != EPlanetArchetype::IceGiant && Type != EPlanetArchetype::Moon;
}

FLinearColor SlonkGenome::RayleighOf(const FLinearColor& AtmosphereColor)
{
	const FLinearColor C0 = AtmosphereColor / FMath::Max(AtmosphereColor.GetMax(), KINDA_SMALL_NUMBER);
	return FLinearColor(FMath::Max(FMath::Pow(C0.R, 1.6f), 0.08f), FMath::Max(FMath::Pow(C0.G, 1.6f), 0.08f), FMath::Max(FMath::Pow(C0.B, 1.6f), 0.08f));
}

FWorldGenome SlonkGenome::Roll(EPlanetArchetype Type, float Strangeness, uint32 Hash)
{
	FWorldGenome G;
	G.Strangeness = FMath::Clamp(Strangeness, 0.f, 1.f);
	G.Solvent = NaturalSolvent(Type);
	const float S = G.Strangeness;
	if (S <= 0.f) { return G; }
	// Every draw is taken whatever the earlier ones gave, so each field keeps its own draws as others change.
	FRandomStream R((int32)(Hash * 2654435761u + 0x5bd1e995u));
	const float SeaDraw = R.FRand(), SeaKind = R.FRand();
	const float SkyBig = R.FRand(), SkySign = R.FRand(), SkyFar = R.FRand(), SkyNear = R.FRand();
	const float HazeDraw = R.FRand(), HazeKm = R.FRand(), HazeAmount = R.FRand(), HazeHue = R.FRand(), HazeSat = R.FRand();
	const float SkyPale = R.FRand();

	// The seas: a world with water swaps it for another solvent more often the stranger it is. An ice world's cold
	// keeps methane and ammonia liquid; a temperate one's may be any of them.
	if (G.Solvent == ESolvent::Water && SeaDraw < S * 0.85f)
	{
		if (Type == EPlanetArchetype::Ice) { G.Solvent = SeaKind < 0.6f ? ESolvent::Methane : ESolvent::Ammonia; }
		else { G.Solvent = SeaKind < 0.45f ? ESolvent::Ammonia : SeaKind < 0.75f ? ESolvent::Acid : ESolvent::Methane; }
	}
	// The sky: a large turn of its colour as often as the world is strange (green, violet, amber, rust skies), a
	// small one otherwise.
	const float Sign = SkySign < 0.5f ? -1.f : 1.f;
	const bool bFar = SkyBig < S;
	G.SkyHueShiftDeg = Sign * (bFar ? FMath::Lerp(60.f, 180.f, SkyFar) * (0.5f + 0.5f * S) : 25.f * S * SkyNear);
	G.SkySaturation = bFar ? FMath::Lerp(0.4f, 0.8f, SkyPale) : 1.f;
	// A haze in the lowlands, a kilometre or two deep, of a colour of its own.
	if (HazeDraw < HazeChance(Type) * S)
	{
		G.LowHazeKm = FMath::Lerp(0.5f, 1.8f, HazeKm);
		G.LowHazeDensity = FMath::Lerp(0.05f, 0.25f, HazeAmount) * (0.5f + 0.5f * S);
		G.HazeTint = FLinearColor(360.f * HazeHue, FMath::Lerp(0.25f, 0.6f, HazeSat), 1.f).HSVToLinearRGB();
	}
	return G;
}

void SlonkGenome::Apply(const FWorldGenome& G, EPlanetArchetype Type, FPlanetStyle& S)
{
	if (G.Strangeness <= 0.f || !AppliesTo(Type, S.bAtmosphere)) { return; }

	// The sky, and the sunset with it: the sunset is the light the sky leaves behind, so it turns as the sky does.
	// The air scatters as much light as before: turned, the brightest channel is no longer blue, and the eye takes
	// green or amber for far brighter, so the scattering is scaled back to the archetype's sky's brightness.
	if (G.SkyHueShiftDeg != 0.f)
	{
		const auto Luma = [](const FLinearColor& C) { return 0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B; };
		const float Before = Luma(RayleighOf(S.AtmosphereColor));
		S.AtmosphereColor = TurnHue(S.AtmosphereColor, G.SkyHueShiftDeg, G.SkySaturation);
		S.SunsetColor = TurnHue(S.SunsetColor, G.SkyHueShiftDeg, G.SkySaturation);
		S.SkyScatterScale *= FMath::Clamp(Before / FMath::Max(Luma(RayleighOf(S.AtmosphereColor)), KINDA_SMALL_NUMBER), 0.4f, 1.5f);
	}
	// The lowland haze; on a dry world it takes the dust's colour, and the clouds take a little of it.
	if (G.LowHazeKm > 0.f)
	{
		FLinearColor Tint = G.HazeTint;
		if (Type == EPlanetArchetype::Desert || Type == EPlanetArchetype::Mars) { Tint = FMath::Lerp(Tint, Normalised(S.LandColor2), 0.6f); }
		S.HazeScaleHeightKm = G.LowHazeKm;
		S.Haze = FMath::Clamp(S.Haze + G.LowHazeDensity, 0.f, 1.f);
		S.HazeColor = Normalised(Tint);
		const float CloudLevel = FMath::Max3(S.CloudColor.R, S.CloudColor.G, S.CloudColor.B);
		S.CloudColor = FMath::Lerp(S.CloudColor, FLinearColor(S.HazeColor.R * CloudLevel, S.HazeColor.G * CloudLevel, S.HazeColor.B * CloudLevel, S.CloudColor.A), 0.3f);
	}
	// The seas.
	switch (G.Solvent)
	{
	case ESolvent::Ammonia:
		S.OceanColor = FLinearColor(0.03f, 0.02f, 0.12f);
		S.ShallowColor = FLinearColor(0.22f, 0.17f, 0.40f);
		S.SwellCm *= 0.8f;
		S.IceColor = FMath::Lerp(S.IceColor, FLinearColor(0.95f, 0.93f, 0.86f), 0.5f);
		break;
	case ESolvent::Methane:
		S.OceanColor = FLinearColor(0.018f, 0.016f, 0.014f);
		S.ShallowColor = FLinearColor(0.07f, 0.06f, 0.04f);
		S.OceanRoughness = 0.05f;
		S.SwellCm = 12.f;
		S.IceColor = FMath::Lerp(S.IceColor, FLinearColor(0.86f, 0.85f, 0.80f), 0.5f);
		break;
	case ESolvent::Acid:
		S.OceanColor = FLinearColor(0.07f, 0.14f, 0.02f);
		S.ShallowColor = FLinearColor(0.36f, 0.48f, 0.08f);
		S.OceanRoughness = 0.14f;
		S.SwellCm *= 0.45f;
		break;
	default:
		break;
	}
}

float SlonkGenome::WorldStrangeness(float DistanceLy, uint32 WorldHash)
{
	const float Cap = FMath::Clamp(0.35f + 0.65f * (DistanceLy - 6.f) / 24.f, 0.35f, 1.f);
	FRandomStream R((int32)(WorldHash ^ 0x9e3779b9u));
	const float U = R.FRand(), Tame = R.FRand();
	return Tame < 0.2f ? Cap * 0.1f * U : Cap * (0.45f + 0.55f * U);
}

const TCHAR* SlonkGenome::SolventName(ESolvent S)
{
	switch (S)
	{
	case ESolvent::Water: return TEXT("water");
	case ESolvent::Ammonia: return TEXT("ammonia");
	case ESolvent::Methane: return TEXT("methane");
	case ESolvent::Acid: return TEXT("acid");
	case ESolvent::Magma: return TEXT("magma");
	default: return TEXT("none");
	}
}

FString SlonkGenome::Describe(const FWorldGenome& G)
{
	FString Out = FString::Printf(TEXT("strangeness %.2f, %s seas, sky turned %+.0f deg at %.0f%% saturation"), G.Strangeness, SolventName(G.Solvent), G.SkyHueShiftDeg, G.SkySaturation * 100.f);
	if (G.LowHazeKm > 0.f) { Out += FString::Printf(TEXT(", lowland haze %.1f km (+%.2f, tint %.2f %.2f %.2f)"), G.LowHazeKm, G.LowHazeDensity, G.HazeTint.R, G.HazeTint.G, G.HazeTint.B); }
	return Out;
}
