#include "Landmarks.h"
#include "FloraProfile.h"
#include "PlanetBody.h"

namespace
{
	// An arch by its span, 40 to 140 m; a standing stone by its height, 35 to 120 m, alone or in a row of up to five, a
	// little under its height apart; a ring by its width across, 70 to 220 m; reef towers by their height over the sea,
	// 30 to 90 m, two to five clusters to a site; mounds by their height, 18 to 55 m, three to seven to a city.
	constexpr SlonkLandmarks::FKindSpec LandmarkSpecs[SlonkLandmarks::NumKinds] = {
		{ 4000.0, 14000.0, 1, 1, 0.0, 0.62, 2.0, false, 0.12 },	// Arch
		{ 3500.0, 12000.0, 1, 5, 0.9, 0.5, 4.0, false, 0.08 },	// Monolith
		{ 7000.0, 22000.0, 1, 1, 0.0, 0.7, 6.0, false, 0.05 },	// Ring
		{ 3000.0, 9000.0, 2, 5, 0.8, 0.3, 2.0, true, 0.35 },	// ReefTower
		{ 1800.0, 5500.0, 3, 7, 0.9, 0.6, 2.0, false, 0.1 },	// Mound
	};
}

const SlonkLandmarks::FKindSpec& SlonkLandmarks::SpecOf(ELandmark Kind)
{
	return LandmarkSpecs[FMath::Clamp((int32)Kind, 0, NumKinds - 1)];
}

FLandmarkProfile SlonkLandmarks::Roll(const FWorldGenome& G, EPlanetArchetype Type, uint32 Hash, float Life, bool bLivingSeas, uint8 ForcedKinds)
{
	FLandmarkProfile L;
	L.Strangeness = G.Strangeness;
	L.Hash = Hash;
	const float Chance = SlonkFlora::DepartChance(G.Strangeness);
	// Nothing to 0.15, before any stream is made; and nothing on a world with no ground to stand anything on.
	if ((Chance <= 0.f && ForcedKinds == 0) || Type == EPlanetArchetype::GasGiant || Type == EPlanetArchetype::IceGiant || Type == EPlanetArchetype::Moon) { return L; }

	// Every draw up front, in a fixed order, whatever comes of it. The seed is the hash mixed, not stepped: a stream
	// seeded a fixed step from another's (the genome's is the hash times the same odd number, plus its own constant)
	// draws a fixed step from that one's every time, and a world's arches would follow its seas.
	FRandomStream R((int32)HashCombine(Hash, 0x632BE5ABu));
	const float UArch = R.FRand(), UArchShare = R.FRand(), UArchSize = R.FRand();
	const float URuin = R.FRand(), URuinKind = R.FRand(), URuinShare = R.FRand(), URuinSize = R.FRand();
	const float URuinHue = R.FRand(), URuinSat = R.FRand(), URuinValue = R.FRand();
	const float UReef = R.FRand(), UReefShare = R.FRand(), UReefSize = R.FRand();
	const float UReefHue = R.FRand(), UReefSat = R.FRand(), UReefValue = R.FRand();
	const float UMound = R.FRand(), UMoundShare = R.FRand(), UMoundSize = R.FRand();

	auto Make = [&L](ELandmark Kind, float UShare, float USize, const FLinearColor& Color)
	{
		FLandmarkKind& K = L.Kinds[(int32)Kind];
		K.bLive = true;
		K.Share = 0.35f + 0.65f * UShare;
		K.Size = 0.8f + 0.45f * USize;
		K.Color = Color;
	};
	// A vanished people's stone: dark, of one faint hue. A reef's: pale, of a hue of its own.
	const FLinearColor RuinStone = FLinearColor(URuinHue * 360.f, 0.15f + 0.25f * URuinSat, 0.035f + 0.055f * URuinValue).HSVToLinearRGB();
	const FLinearColor ReefStone = FLinearColor(UReefHue * 360.f, 0.3f + 0.25f * UReefSat, 0.4f + 0.25f * UReefValue).HSVToLinearRGB();
	if (UArch < 0.65f * Chance) { Make(ELandmark::Arch, UArchShare, UArchSize, FLinearColor(0.4f, 0.4f, 0.4f)); }
	if (URuin < 0.4f * Chance) { Make(URuinKind < 0.55f ? ELandmark::Monolith : ELandmark::Ring, URuinShare, URuinSize, RuinStone); }
	if (Life > 0.f && bLivingSeas && UReef < 0.6f * Chance) { Make(ELandmark::ReefTower, UReefShare, UReefSize, ReefStone); }
	if (Life > 0.f && UMound < 0.5f * Chance) { Make(ELandmark::Mound, UMoundShare, UMoundSize, FLinearColor(0.4f, 0.35f, 0.28f)); }
	// The kinds asked for by name, where the roll gave none: as readily as any stands, at the size the world drew for it.
	const float USizeOf[NumKinds] = { UArchSize, URuinSize, URuinSize, UReefSize, UMoundSize };
	const FLinearColor ColorOf[NumKinds] = { FLinearColor(0.4f, 0.4f, 0.4f), RuinStone, RuinStone, ReefStone, FLinearColor(0.4f, 0.35f, 0.28f) };
	for (int32 K = 0; K < NumKinds; ++K)
	{
		if ((ForcedKinds & (1 << K)) && !L.Kinds[K].bLive) { Make((ELandmark)K, 1.f, USizeOf[K], ColorOf[K]); }
	}
	return L;
}

void SlonkLandmarks::FitToGround(FLandmarkProfile& L, float LandShare)
{
	if (LandShare < 0.02f) { for (FLandmarkKind& K : L.Kinds) { K = FLandmarkKind(); } }
	else if (LandShare > 0.98f) { L.Kinds[(int32)ELandmark::ReefTower] = FLandmarkKind(); }
}

const TCHAR* SlonkLandmarks::KindName(ELandmark Kind)
{
	switch (Kind)
	{
	case ELandmark::Arch: return TEXT("arches");
	case ELandmark::Monolith: return TEXT("monoliths");
	case ELandmark::Ring: return TEXT("great rings");
	case ELandmark::ReefTower: return TEXT("reef towers");
	case ELandmark::Mound: return TEXT("mound cities");
	default: return TEXT("none");
	}
}

const TCHAR* SlonkLandmarks::KindKey(ELandmark Kind)
{
	switch (Kind)
	{
	case ELandmark::Arch: return TEXT("arch");
	case ELandmark::Monolith: return TEXT("monolith");
	case ELandmark::Ring: return TEXT("greatring");
	case ELandmark::ReefTower: return TEXT("reef");
	case ELandmark::Mound: return TEXT("mound");
	default: return TEXT("none");
	}
}

ELandmark SlonkLandmarks::KindNamed(const FString& Key)
{
	const FString K = Key.ToLower();
	for (int32 i = 0; i < NumKinds; ++i) { if (!K.IsEmpty() && K.StartsWith(KindKey((ELandmark)i))) { return (ELandmark)i; } }
	return ELandmark::Count;
}

FString SlonkLandmarks::Describe(const FLandmarkProfile& L)
{
	FString Out;
	for (int32 i = 0; i < NumKinds; ++i)
	{
		if (L.Kinds[i].bLive) { Out += FString::Printf(TEXT("%s%s"), Out.IsEmpty() ? TEXT("landmarks: ") : TEXT(", "), KindName((ELandmark)i)); }
	}
	return Out.IsEmpty() ? FString(TEXT("no landmarks")) : Out;
}
