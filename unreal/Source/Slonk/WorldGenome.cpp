#include "WorldGenome.h"
#include "PlanetBody.h"
#include "PlanetTerrain.h"

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

bool SlonkGenome::HasClimate(EPlanetArchetype Type)
{
	// The kinds APlanetBody::MakeTerrainParams gives a snowline and air: the lava worlds, Venus and Mars have neither
	// to move.
	return Type == EPlanetArchetype::Terrestrial || Type == EPlanetArchetype::Ocean || Type == EPlanetArchetype::Desert || Type == EPlanetArchetype::Ice;
}

FLinearColor SlonkGenome::RayleighOf(const FLinearColor& AtmosphereColor)
{
	const FLinearColor C0 = AtmosphereColor / FMath::Max(AtmosphereColor.GetMax(), KINDA_SMALL_NUMBER);
	return FLinearColor(FMath::Max(FMath::Pow(C0.R, 1.6f), 0.08f), FMath::Max(FMath::Pow(C0.G, 1.6f), 0.08f), FMath::Max(FMath::Pow(C0.B, 1.6f), 0.08f));
}

FWorldGenome SlonkGenome::Roll(EPlanetArchetype Type, float Strangeness, uint32 Hash, float TidalPull)
{
	FWorldGenome G;
	G.Strangeness = FMath::Clamp(Strangeness, 0.f, 1.f);
	G.Solvent = NaturalSolvent(Type);
	const float S = G.Strangeness;
	if (S <= 0.f) { return G; }
	// Every draw is taken whatever the earlier ones gave, so each field keeps its own draws as others change; the
	// climate's come after the seas', skies' and hazes', so those roll as they did before it.
	FRandomStream R((int32)(Hash * 2654435761u + 0x5bd1e995u));
	const float SeaDraw = R.FRand(), SeaKind = R.FRand();
	const float SkyBig = R.FRand(), SkySign = R.FRand(), SkyFar = R.FRand(), SkyNear = R.FRand();
	const float HazeDraw = R.FRand(), HazeKm = R.FRand(), HazeAmount = R.FRand(), HazeHue = R.FRand(), HazeSat = R.FRand();
	const float SkyPale = R.FRand();
	const float LockDraw = R.FRand(), DayDraw = R.FRand(), TermDraw = R.FRand(), NightDraw = R.FRand(), WarmDraw = R.FRand();
	// The geology's, after the climate's: everything before them rolls as it did.
	const float GeoDraw = R.FRand(), GeoKind = R.FRand(), GeoAmount = R.FRand();

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
	// The climate, on a world with one to move. The tides lock a world more often the closer it lies to its star, and
	// some worlds far out were locked by what they formed from. A locked world's heats are rolled as a spinning one's
	// warmth is, so a lock settled later (SettleLock) finds them there: under the star 2.25 to 2.8 times the Earth-like
	// version's equatorial sea temperature, on the terminator 0.35 to 0.9 of it, the far side 0.2 to 0.6 of the liquid
	// range below freezing. That scale is the archetype's own (ApplyClimate), not an Earth's: under the star a temperate
	// world (a 5 km snowline) runs 0.73 to 0.91 of the liquid range, baked, its ground kept under 0.3 of its wet; an
	// ocean world (4.6 km) 0.67 to 0.84, hot to baked; a desert (6.5 km) 0.95 to 1.18, past boiling; an ice world
	// (1 km) only 0.15 to 0.18, never hot.
	if (HasClimate(Type))
	{
		G.bTidallyLocked = LockDraw < S * (0.15f + 0.7f * FMath::Clamp(TidalPull, 0.f, 1.f));
		G.DayHeat = FMath::Lerp(2.25f, 2.8f, DayDraw);
		G.TermHeat = FMath::Lerp(0.35f, 0.9f, TermDraw);
		G.NightCold = FMath::Lerp(0.2f, 0.6f, NightDraw);
		G.WarmthN = S * FMath::Lerp(-0.12f, 0.12f, WarmDraw);
	}
	// Its ground: three strange worlds in four, from a quarter strange up, have a geology of their own, of a kind their
	// archetype's ground can make. None to 0.15, as with what grows there.
	const float GeoChance = 0.75f * FMath::SmoothStep(0.15f, 0.7f, S);
	if (GeoDraw < GeoChance)
	{
		EGeology Kind = EGeology::None;
		switch (Type)
		{
		case EPlanetArchetype::Terrestrial: Kind = GeoKind < 0.3f ? EGeology::Terraces : GeoKind < 0.55f ? EGeology::Crystals : GeoKind < 0.8f ? EGeology::Spires : GeoKind < 0.9f ? EGeology::Polygons : EGeology::Glass; break;
		case EPlanetArchetype::Ocean: Kind = GeoKind < 0.5f ? EGeology::Terraces : GeoKind < 0.8f ? EGeology::Crystals : EGeology::Spires; break;
		case EPlanetArchetype::Desert: Kind = GeoKind < 0.3f ? EGeology::Polygons : GeoKind < 0.6f ? EGeology::Spires : GeoKind < 0.8f ? EGeology::Glass : EGeology::Crystals; break;
		case EPlanetArchetype::Ice: Kind = GeoKind < 0.5f ? EGeology::Polygons : EGeology::Crystals; break;
		case EPlanetArchetype::Mars: Kind = GeoKind < 0.3f ? EGeology::Polygons : GeoKind < 0.6f ? EGeology::Glass : GeoKind < 0.8f ? EGeology::Spires : EGeology::Crystals; break;
		case EPlanetArchetype::Venus: Kind = GeoKind < 0.5f ? EGeology::Glass : GeoKind < 0.8f ? EGeology::Crystals : EGeology::Spires; break;
		case EPlanetArchetype::Lava: Kind = GeoKind < 0.6f ? EGeology::Glass : EGeology::Crystals; break;
		default: break;
		}
		if (Kind != EGeology::None)
		{
			G.Geology = (uint8)Kind;
			G.GeologyStrength = FMath::Lerp(0.5f, 1.f, GeoAmount);
		}
	}
	return G;
}

void SlonkGenome::ApplyGeology(const FWorldGenome& G, FPlanetTerrainParams& P)
{
	if (G.Strangeness <= 0.f || G.Geology == 0 || G.Geology >= (uint8)EGeology::Count || P.Lineae > 0.f) { return; }
	P.Geology = (EGeology)G.Geology;
	P.GeoStrength = G.GeologyStrength;
}

const TCHAR* SlonkGenome::GeologyName(uint8 Geology)
{
	switch ((EGeology)Geology)
	{
	case EGeology::Crystals: return TEXT("crystal fields");
	case EGeology::Polygons: return TEXT("polygon pans");
	case EGeology::Terraces: return TEXT("terraced pools");
	case EGeology::Spires: return TEXT("spire forests");
	case EGeology::Glass: return TEXT("glass plains");
	default: return TEXT("none");
	}
}

void SlonkGenome::GeologyColors(const FWorldGenome& G, const FPlanetStyle& S, FLinearColor& OutA, FLinearColor& OutB)
{
	OutA = FLinearColor(0.5f, 0.5f, 0.5f);
	OutB = FLinearColor(0.2f, 0.2f, 0.2f);
	switch ((EGeology)G.Geology)
	{
	case EGeology::Crystals:
	{
		// Half way round the wheel from the world's sky, so a field stands out under it: clear and bright on its lit
		// faces, deep on the others.
		FLinearColor Hsv = S.AtmosphereColor.LinearRGBToHSV();
		Hsv.R = FMath::Fmod(Hsv.R + 150.f + 60.f * G.GeologyStrength, 360.f);
		Hsv.G = 0.55f;
		Hsv.B = 0.62f;
		OutA = Hsv.HSVToLinearRGB();
		OutB = OutA * 0.3f;
		break;
	}
	case EGeology::Polygons:
		// Salt over the dry ground's own colour, or the world's ice; the cracks the ground showing through.
		OutA = S.IceCapStart < 1.5f && S.LandColor.B > S.LandColor.R ? S.IceColor * 0.92f : FMath::Lerp(S.LandColor2, FLinearColor(0.86f, 0.84f, 0.78f), 0.75f);
		OutB = FMath::Lerp(S.LandColor2, S.MountainColor, 0.5f) * 0.5f;
		break;
	case EGeology::Terraces:
	{
		// Pale mineral stone, and the world's own liquid standing clear in the pools: its shallows' hue, no duller
		// than half saturated and neither black nor washed out.
		OutA = FMath::Lerp(S.MountainColor, FLinearColor(0.84f, 0.8f, 0.68f), 0.7f);
		FLinearColor Hsv = S.ShallowColor.LinearRGBToHSV();
		Hsv.G = FMath::Max(Hsv.G, 0.5f);
		Hsv.B = FMath::Clamp(Hsv.B, 0.3f, 0.75f);
		OutB = Hsv.HSVToLinearRGB();
		break;
	}
	case EGeology::Spires:
		OutA = S.MountainColor * FLinearColor(1.25f, 0.9f, 0.68f);
		OutB = S.MountainColor * FLinearColor(0.62f, 0.48f, 0.42f);
		break;
	case EGeology::Glass:
		OutA = FMath::Lerp(FLinearColor(0.02f, 0.02f, 0.025f), S.MountainColor, 0.12f);
		OutB = FLinearColor(0.34f, 0.36f, 0.38f);
		break;
	default: break;
	}
	OutA.A = 1.f;
	OutB.A = 1.f;
}

void SlonkGenome::SettleLock(FWorldGenome& G, EPlanetArchetype Type, const FVector& StarBearing, bool bForce)
{
	if (StarBearing.IsNearlyZero()) { G.bTidallyLocked = false; return; }
	if (bForce && G.Strangeness > 0.f && HasClimate(Type)) { G.bTidallyLocked = true; }
}

void SlonkGenome::ApplyClimate(const FWorldGenome& G, EPlanetArchetype Type, const FVector& StarBearing, FPlanetTerrainParams& P)
{
	if (G.Strangeness <= 0.f || !HasClimate(Type) || P.SnowlineCm <= 0.0) { return; }
	if (G.bTidallyLocked && !StarBearing.IsNearlyZero())
	{
		// The Earth-like version's equatorial sea temperature is the scale: the archetype's own snowline, cooled to sea
		// level, so a locked desert's day side runs far hotter than a locked ice world's.
		const double T0 = SlonkTerrain::LapseNPerCm * P.SnowlineCm;
		P.Climate = EClimateModel::Locked;
		P.ClimateAxis = StarBearing.GetSafeNormal();
		P.DayN = (float)(T0 * G.DayHeat);
		P.TermN = (float)(T0 * G.TermHeat);
		P.NightN = -G.NightCold;
		return;
	}
	P.Climate = EClimateModel::Spin;
	P.WarmthN = G.WarmthN;
}

float SlonkGenome::FreezeK(ESolvent S)
{
	switch (S)
	{
	case ESolvent::Ammonia: return 195.f;
	case ESolvent::Methane: return 91.f;
	case ESolvent::Acid: return 283.f;
	default: return 273.f;     // water, and the snow of the worlds without seas
	}
}

float SlonkGenome::RangeK(ESolvent S)
{
	switch (S)
	{
	case ESolvent::Ammonia: return 45.f;
	case ESolvent::Methane: return 21.f;
	case ESolvent::Acid: return 327.f;
	default: return 100.f;
	}
}

void SlonkGenome::Apply(const FWorldGenome& G, EPlanetArchetype Type, FPlanetStyle& S)
{
	if (G.Strangeness <= 0.f || !AppliesTo(Type, S.bAtmosphere)) { return; }

	// The sky, and the sunset with it: the sunset is the light the sky leaves behind, so it turns as the sky does.
	// The air scatters no more light than before: turned, the brightest channel is no longer blue, and the eye takes
	// green or amber for far brighter, so the scattering is scaled back to the archetype's sky's brightness, and the
	// far shell's glow with it (the near sky scatters the colour's Rayleigh ratio, the shell shows the colour itself).
	// A sky the turn leaves dimmer is brightened back, by half again at most.
	if (G.SkyHueShiftDeg != 0.f)
	{
		const auto Luma = [](const FLinearColor& C) { return 0.2126f * C.R + 0.7152f * C.G + 0.0722f * C.B; };
		const float Before = Luma(RayleighOf(S.AtmosphereColor)), ShellBefore = Luma(S.AtmosphereColor);
		S.AtmosphereColor = TurnHue(S.AtmosphereColor, G.SkyHueShiftDeg, G.SkySaturation);
		S.SunsetColor = TurnHue(S.SunsetColor, G.SkyHueShiftDeg, G.SkySaturation);
		S.SkyScatterScale *= FMath::Min(Before / FMath::Max(Luma(RayleighOf(S.AtmosphereColor)), KINDA_SMALL_NUMBER), 1.5f);
		S.SkyShellScale *= FMath::Min(ShellBefore / FMath::Max(Luma(S.AtmosphereColor), KINDA_SMALL_NUMBER), 1.5f);
	}
	// The lowland haze; on a dry world it takes the dust's colour, and the clouds take a little of it.
	if (G.LowHazeKm > 0.f)
	{
		FLinearColor Tint = G.HazeTint;
		if (Type == EPlanetArchetype::Desert || Type == EPlanetArchetype::Mars) { Tint = FMath::Lerp(Tint, Normalised(S.LandColor2), 0.6f); }
		// It lies under the air's own haze, a quarter of the air's scale height, or it is no lowland haze: the sky draws
		// it at most 0.8 of that deep, and over a shallow air the style holds it there too, so it says what is drawn.
		S.HazeScaleHeightKm = S.AtmosphereScaleHeightKm > 0.f ? FMath::Min(G.LowHazeKm, S.AtmosphereScaleHeightKm * 0.25f * 0.8f) : G.LowHazeKm;
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

FLinearColor SlonkGenome::ShellSkyColor(const FPlanetStyle& S)
{
	// The haze is blended in at the sky's own brightest channel, as the archetype had it: the shell's scale dims or
	// brightens the turned sky, not the haze.
	const FLinearColor Sky(S.AtmosphereColor.R * S.SkyShellScale, S.AtmosphereColor.G * S.SkyShellScale, S.AtmosphereColor.B * S.SkyShellScale, S.AtmosphereColor.A);
	if (S.HazeColor.Equals(FLinearColor::White, 1.0e-3f)) { return Sky; }
	return FMath::Lerp(Sky, S.HazeColor * S.AtmosphereColor.GetMax(), FMath::Clamp(S.Haze, 0.f, 0.6f));
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
	if (G.bTidallyLocked) { Out += FString::Printf(TEXT(", tidally locked (day x%.2f, terminator x%.2f, night %.2f below freezing)"), G.DayHeat, G.TermHeat, G.NightCold); }
	else if (G.WarmthN != 0.f) { Out += FString::Printf(TEXT(", %s by %.3f of the liquid range"), G.WarmthN > 0.f ? TEXT("warmer") : TEXT("colder"), FMath::Abs(G.WarmthN)); }
	if (G.Geology != 0) { Out += FString::Printf(TEXT(", %s over %.0f%% strength"), GeologyName(G.Geology), G.GeologyStrength * 100.f); }
	return Out;
}
