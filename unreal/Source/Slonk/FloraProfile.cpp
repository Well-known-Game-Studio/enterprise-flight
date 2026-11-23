#include "FloraProfile.h"

#include "FaunaProfile.h"
#include "PlanetBody.h"
#include "PlantMeshes.h"
#include "ScatterSpot.h"

namespace SlonkFloraRoll
{
	/** A colour turned round the colour wheel, its saturation and brightness kept. */
	FLinearColor TurnHue(const FLinearColor& C, float Deg)
	{
		if (Deg == 0.f) { return C; }
		FLinearColor Hsv = C.LinearRGBToHSV();
		Hsv.R = FMath::Fmod(Hsv.R + Deg + 720.f, 360.f);
		FLinearColor Out = Hsv.HSVToLinearRGB();
		Out.A = 1.f;
		return Out;
	}

	/** The plan a slot's strange species takes, counted round from the world's first: a role's two species never
	 *  alike, and a giant the giant of its warm tree's plan. */
	constexpr int32 PlanOffset[SlonkFlora::MaxSpecies] = { 0, 1, 2, 3, 1, 0, 2, 3, 2, 3 };

	/** The pigments under each light, three apiece. */
	const FLinearColor Pigments[(int32)EStarLight::Count][3] = {
		{ FLinearColor(0.035f, 0.03f, 0.05f), FLinearColor(0.16f, 0.07f, 0.22f), FLinearColor(0.14f, 0.04f, 0.05f) },
		{ FLinearColor(0.16f, 0.17f, 0.06f), FLinearColor(0.30f, 0.19f, 0.07f), FLinearColor(0.26f, 0.06f, 0.10f) },
		{ FLinearColor(0.30f, 0.36f, 0.18f), FLinearColor(0.30f, 0.12f, 0.34f), FLinearColor(0.50f, 0.40f, 0.10f) },
		{ FLinearColor(0.10f, 0.34f, 0.26f), FLinearColor(0.07f, 0.30f, 0.34f), FLinearColor(0.42f, 0.50f, 0.16f) },
		{ FLinearColor(0.62f, 0.30f, 0.08f), FLinearColor(0.48f, 0.10f, 0.07f), FLinearColor(0.60f, 0.42f, 0.10f) } };

	EFloraSurface SurfaceOf(EFloraPlan Plan, float U)
	{
		switch (Plan)
		{
		case EFloraPlan::StalkCap: return U < 0.6f ? EFloraSurface::Fungal : (U < 0.85f ? EFloraSurface::Membrane : EFloraSurface::Chitin);
		case EFloraPlan::Fan: return U < 0.3f ? EFloraSurface::Leaf : (U < 0.65f ? EFloraSurface::Membrane : (U < 0.9f ? EFloraSurface::Chitin : EFloraSurface::Crystal));
		case EFloraPlan::Tubes: return U < 0.4f ? EFloraSurface::Chitin : (U < 0.7f ? EFloraSurface::Fungal : EFloraSurface::Crystal);
		default: return U < 0.65f ? EFloraSurface::Membrane : (U < 0.85f ? EFloraSurface::Fungal : EFloraSurface::Chitin);
		}
	}

	EFloraGlow GlowOf(EFloraPlan Plan, float U)
	{
		switch (Plan)
		{
		case EFloraPlan::StalkCap: return U < 0.4f ? EFloraGlow::Spots : (U < 0.75f ? EFloraGlow::Ribs : EFloraGlow::Rim);
		case EFloraPlan::Fan: return U < 0.6f ? EFloraGlow::Ribs : EFloraGlow::Rim;
		case EFloraPlan::Tubes: return U < 0.65f ? EFloraGlow::Rim : EFloraGlow::Body;
		default: return U < 0.6f ? EFloraGlow::Body : EFloraGlow::Spots;
		}
	}
}

const TCHAR* SlonkFlora::PlanName(EFloraPlan P)
{
	switch (P)
	{
	case EFloraPlan::Tuft: return TEXT("grass");
	case EFloraPlan::Bush: return TEXT("bush");
	case EFloraPlan::Crown: return TEXT("broadleaf");
	case EFloraPlan::Tiers: return TEXT("conifer");
	case EFloraPlan::StalkCap: return TEXT("stalk-and-cap");
	case EFloraPlan::Fan: return TEXT("fan");
	case EFloraPlan::Tubes: return TEXT("tube cluster");
	case EFloraPlan::Bulb: return TEXT("bladder");
	default: return TEXT("?");
	}
}

const TCHAR* SlonkFlora::ClassName(EFloraClass C)
{
	switch (C)
	{
	case EFloraClass::Grass: return TEXT("ground cover");
	case EFloraClass::Shrub: return TEXT("bush");
	case EFloraClass::Tree: return TEXT("tree");
	case EFloraClass::Giant: return TEXT("giant");
	default: return TEXT("?");
	}
}

const TCHAR* SlonkFlora::SurfaceName(EFloraSurface S)
{
	switch (S)
	{
	case EFloraSurface::Leaf: return TEXT("leaf");
	case EFloraSurface::Fungal: return TEXT("fungal");
	case EFloraSurface::Membrane: return TEXT("membrane");
	case EFloraSurface::Chitin: return TEXT("chitin");
	case EFloraSurface::Crystal: return TEXT("crystal");
	default: return TEXT("?");
	}
}

const TCHAR* SlonkFlora::GlowName(EFloraGlow G)
{
	switch (G)
	{
	case EFloraGlow::None: return TEXT("no glow");
	case EFloraGlow::Spots: return TEXT("glowing spots");
	case EFloraGlow::Ribs: return TEXT("glowing ribs");
	case EFloraGlow::Rim: return TEXT("glowing rims");
	case EFloraGlow::Body: return TEXT("a glow all over");
	default: return TEXT("?");
	}
}

const TCHAR* SlonkFlora::StarLightName(EStarLight L)
{
	switch (L)
	{
	case EStarLight::Red: return TEXT("red");
	case EStarLight::Orange: return TEXT("orange");
	case EStarLight::Yellow: return TEXT("yellow");
	case EStarLight::White: return TEXT("white");
	case EStarLight::Blue: return TEXT("blue-white");
	default: return TEXT("?");
	}
}

EFloraPlan SlonkFlora::AlienPlan(int32 Index)
{
	static const EFloraPlan Plans[NumAlienPlans] = { EFloraPlan::StalkCap, EFloraPlan::Fan, EFloraPlan::Tubes, EFloraPlan::Bulb };
	return Plans[((Index % NumAlienPlans) + NumAlienPlans) % NumAlienPlans];
}

float SlonkFlora::DepartChance(float Strangeness)
{
	const float S = FMath::Clamp(Strangeness, 0.f, 1.f);
	return S <= 0.15f ? 0.f : 0.95f * (float)SlonkScatter::SmoothStep(0.15, 0.95, (double)S);
}

FLinearColor SlonkFlora::StarPigment(EStarLight Star, float Draw)
{
	const int32 L = FMath::Clamp((int32)Star, 0, (int32)EStarLight::Count - 1);
	return SlonkFloraRoll::Pigments[L][FMath::Clamp((int32)(Draw * 3.f), 0, 2)];
}

EStarLight SlonkFlora::StarLightOf(const FLinearColor& StarColor)
{
	// How much redder it is than it is blue, of its brightest channel: a red dwarf 0.7, the Sun 0.38, the hot stars
	// under nothing.
	const float Warm = (StarColor.R - StarColor.B) / FMath::Max(FMath::Max3(StarColor.R, StarColor.G, StarColor.B), 1e-3f);
	return Warm > 0.6f ? EStarLight::Red : (Warm > 0.45f ? EStarLight::Orange : (Warm > 0.25f ? EStarLight::Yellow : (Warm > 0.05f ? EStarLight::White : EStarLight::Blue)));
}

FFloraProfile SlonkFlora::Roll(const FWorldGenome& G, EPlanetArchetype Type, uint32 Hash, EStarLight Star)
{
	using namespace SlonkFloraRoll;
	FFloraProfile F;
	F.Strangeness = G.Strangeness;
	F.Hash = Hash;
	F.Star = Star;
	const float Chance = DepartChance(G.Strangeness);
	// The Earth's, whatever the star, while nothing would depart: no stream made, nothing drawn.
	if (Chance <= 0.f) { return F; }
	const float S = FMath::Clamp(G.Strangeness, 0.f, 1.f);
	// The world's own draws, every one taken: which plan its strange species count round from, its pigment, and
	// whether its nights glow.
	FRandomStream R((int32)(Hash * 0x7FEB352Du + 0x846CA68Bu));
	const float UFirst = R.FRand(), UPigment = R.FRand(), UPigVary = R.FRand(), UGlowWorld = R.FRand();
	const int32 FirstPlan = FMath::Min((int32)(UFirst * NumAlienPlans), NumAlienPlans - 1);
	F.Vivid = (float)SlonkScatter::SmoothStep(0.15, 0.6, (double)S);
	F.Pigment = StarPigment(Star, UPigment) * (0.85f + 0.3f * UPigVary);
	F.Pigment.A = 1.f;
	// A red dwarf's worlds make their own light more often than not: there is little enough of the star's.
	const bool bGlows = UGlowWorld < (Star == EStarLight::Red ? 0.75f : 0.45f) * (float)SlonkScatter::SmoothStep(0.2, 0.7, (double)S);
	const FLinearColor GlowBase = SlonkFauna::LookOf(G).Glow;
	bool bGiant = false;
	for (int32 Sl = 0; Sl < MaxSpecies; ++Sl)
	{
		// Each slot a stream of its own, and every draw of it taken whatever comes of them.
		FRandomStream RS((int32)HashCombine(Hash * 0x85EBCA6Bu + 0xC2B2AE35u * (uint32)(Sl + 1), 0x27D4EB2Fu));
		const float UThere = RS.FRand(), UDepart = RS.FRand(), UShare = RS.FRand(), UHeight = RS.FRand();
		float U[SlotShapeDraws];
		for (float& V : U) { V = RS.FRand(); }
		bool bLive = true, bStrange = true;
		if (Sl < 4) { bStrange = UDepart < Chance; }
		else if (Sl == Slot::Giant) { bLive = S >= GiantMinStrangeness && UThere < 0.8f * Chance; bGiant = bLive; }
		else if (Sl == Slot::Giant2) { bLive = bGiant && UThere < 0.5f * Chance * Chance; }
		else { bLive = UThere < Chance * Chance * ((Sl == Slot::Warm2 || Sl == Slot::Cold2) ? 0.75f : 0.5f); }
		if (!bLive) { F.Species[Sl] = FFloraSpecies(); continue; }
		if (!bStrange) { F.Species[Sl] = EarthSpecies(Sl); continue; }

		FFloraSpecies Sp;
		Sp.bLive = true;
		Sp.Form.Plan = AlienPlan(FirstPlan + PlanOffset[Sl]);
		Sp.Form.Class = ClassOf(Sl);
		Sp.Form.Spread = U[0];
		Sp.Form.Stem = U[1];
		Sp.Form.Arch = U[2];
		Sp.Form.Lean = U[3] * U[3] * 0.6f;
		Sp.Form.Parts = (uint8)FMath::Min((int32)(U[4] * 5.f), 4);
		Sp.Form.Variant = (uint16)FMath::Min((int32)(U[5] * 65536.f), 65535);
		Sp.Share = 0.4f + 0.6f * UShare;
		switch (Sp.Form.Class)
		{
		case EFloraClass::Grass: Sp.Height = FMath::Lerp(0.7, 1.6, (double)UHeight); break;
		case EFloraClass::Shrub: Sp.Height = FMath::Lerp(0.8, 1.3, (double)UHeight); break;
		case EFloraClass::Giant: Sp.Height = FMath::Lerp(0.75, 1.25, (double)UHeight); break;
		default: Sp.Height = FMath::Lerp(0.8, 1.7, (double)UHeight); break;
		}
		// A bush's width, which the scrub's dots fit it to, and a giant's foot, which everything else keeps out of:
		// each measured off the form's own mesh, and only where it is read.
		if (Sp.Form.Class == EFloraClass::Shrub) { Sp.UnitWideCm = SlonkPlants::CrownWideCm(Sp.Form); }
		if (Sp.Form.Class == EFloraClass::Giant) { Sp.UnitFootCm = SlonkPlants::FootRadiusCm(Sp.Form); }
		Sp.Surface = SurfaceOf(Sp.Form.Plan, U[6]);
		// The living ground's colour, a shade lighter or darker; and for some a colour of their own, turned well away
		// from the pigment's. The fans less often: they are what catches the light, and wear the pigment that does it.
		// The two trees that make a world's forests keep near the ground's colour, a shade under it as the Earth's are:
		// the forest far off is painted in that colour, and its trees must come up out of the paint as themselves.
		const bool bForest = Sl == Slot::Warm || Sl == Slot::Cold;
		const float Shade = bForest ? FMath::Lerp(0.75f, 1.f, U[7]) : FMath::Lerp(0.7f, 1.25f, U[7]);
		Sp.Tint = FLinearColor(Shade, Shade, Shade);
		if (U[8] >= (Sp.Form.Plan == EFloraPlan::Fan ? 0.8f : 0.55f))
		{
			FLinearColor Hsv = F.Pigment.LinearRGBToHSV();
			const float Deg = (U[10] < 0.5f ? -1.f : 1.f) * FMath::Lerp(40.f, 160.f, FMath::Frac(U[10] * 2.f));
			Hsv.R = FMath::Fmod(Hsv.R + Deg + 720.f, 360.f);
			Hsv.G = FMath::Clamp(FMath::Max(Hsv.G, 0.45f), 0.f, 0.85f);
			Hsv.B = FMath::Lerp(0.2f, 0.55f, U[9]);
			Sp.Own = Hsv.HSVToLinearRGB();
			Sp.Own.A = 1.f;
			Sp.OwnShare = bForest ? FMath::Lerp(0.1f, 0.3f, U[9]) : FMath::Lerp(0.3f, 0.85f, U[9]);
		}
		const float StemShade = FMath::Lerp(0.75f, 1.2f, U[11]);
		switch (Sp.Surface)
		{
		case EFloraSurface::Fungal: Sp.StemColor = FLinearColor(0.62f, 0.58f, 0.48f) * StemShade; break;
		case EFloraSurface::Chitin: Sp.StemColor = FLinearColor(0.07f, 0.06f, 0.06f) * StemShade; break;
		case EFloraSurface::Crystal: Sp.StemColor = FLinearColor(0.5f, 0.55f, 0.6f) * StemShade; break;
		default: Sp.StemColor = FMath::Lerp(FLinearColor(0.24f, 0.18f, 0.13f), F.Pigment, 0.4f) * StemShade; break;
		}
		Sp.StemColor.A = 1.f;
		if (bGlows && U[12] < 0.7f)
		{
			Sp.Glow = GlowOf(Sp.Form.Plan, U[13]);
			Sp.GlowStrength = FMath::Lerp(1.5f, 4.f, U[14]);
			Sp.GlowColor = TurnHue(GlowBase, FMath::Lerp(-35.f, 35.f, U[15]));
			Sp.GlowColor.A = 1.f;
		}
		F.Species[Sl] = Sp;
	}
	return F;
}

void SlonkFlora::ApplyPigment(const FFloraProfile& F, float Life, FPlanetStyle& S)
{
	const float Share = F.Vivid * (Life >= 1.f ? 1.f : (Life > 0.f ? 0.5f : 0.f));
	if (Share <= 0.f) { return; }
	const float A1 = S.LandColor.A, A2 = S.LandColor2.A;
	S.LandColor = FMath::Lerp(S.LandColor, F.Pigment, Share);
	S.LandColor2 = FMath::Lerp(S.LandColor2, F.Pigment, Share / 3.f);
	S.LandColor.A = A1;
	S.LandColor2.A = A2;
}

SlonkFlora::FFloraLook SlonkFlora::LookOf(const FFloraSpecies& Sp)
{
	FFloraLook L;
	if (!Sp.bLive || Sp.HasEarthLook()) { return L; }
	L.Stem = Sp.StemColor;
	switch (Sp.Surface)
	{
	case EFloraSurface::Fungal: L.Gloss = 0.05f; L.Translucence = 0.45f; break;
	case EFloraSurface::Membrane: L.Gloss = 0.35f; L.Translucence = 1.8f; break;
	case EFloraSurface::Chitin: L.Gloss = 0.7f; L.Translucence = 0.15f; break;
	case EFloraSurface::Crystal: L.Gloss = 0.92f; L.Translucence = 1.3f; break;
	default: break;
	}
	if (Sp.GlowStrength > 0.f && Sp.Glow != EFloraGlow::None)
	{
		L.GlowColor = Sp.GlowColor;
		L.Glow = Sp.GlowStrength;
		switch (Sp.Glow)
		{
		case EFloraGlow::Spots: L.GlowForm = FLinearColor(1.f, 0.f, 0.f, 0.06f); break;
		case EFloraGlow::Ribs: L.GlowForm = FLinearColor(0.f, 1.f, 0.f, 0.06f); break;
		case EFloraGlow::Rim: L.GlowForm = FLinearColor(0.f, 0.f, 1.f, 0.06f); break;
		default: L.GlowForm = FLinearColor(0.f, 0.f, 0.f, 1.f); break;
		}
	}
	return L;
}

FString SlonkFlora::DescribeSpecies(const FFloraProfile& F, int32 Sl)
{
	if (Sl < 0 || Sl >= MaxSpecies || !F.Species[Sl].bLive) { return FString(); }
	const FFloraSpecies& Sp = F.Species[Sl];
	static const TCHAR* Roles[MaxSpecies] = { TEXT("ground cover"), TEXT("bush"), TEXT("lowland tree"), TEXT("cold tree"), TEXT("second lowland tree"), TEXT("second cold tree"),
		TEXT("giant"), TEXT("second giant"), TEXT("second ground cover"), TEXT("second bush") };
	if (Sp.Form.IsEarth()) { return FString::Printf(TEXT("slot %d, %s: the Earth's %s"), Sl, Roles[Sl], PlanName(Sp.Form.Plan)); }
	return FString::Printf(TEXT("slot %d, %s: %s of %d parts (spread %.2f, stem %.2f, arch %.2f, lean %.2f), %s, %.0f%% of its role, %.2f of its class's height, %.0f%% its own colour, %s"),
		Sl, Roles[Sl], PlanName(Sp.Form.Plan), (int32)Sp.Form.Parts, Sp.Form.Spread, Sp.Form.Stem, Sp.Form.Arch, Sp.Form.Lean, SurfaceName(Sp.Surface), Sp.Share * 100.f, Sp.Height,
		Sp.OwnShare * 100.f, Sp.GlowStrength > 0.f ? GlowName(Sp.Glow) : TEXT("no glow"));
}

FString SlonkFlora::Describe(const FFloraProfile& F)
{
	if (F.IsEarth()) { return TEXT("the Earth's flora"); }
	int32 Strange = 0, Glowing = 0;
	FString Giants;
	for (int32 Sl = 0; Sl < MaxSpecies; ++Sl)
	{
		const FFloraSpecies& Sp = F.Species[Sl];
		if (!Sp.bLive) { continue; }
		Strange += Sp.Form.IsEarth() ? 0 : 1;
		Glowing += Sp.GlowStrength > 0.f ? 1 : 0;
		if (ClassOf(Sl) == EFloraClass::Giant) { Giants += FString::Printf(TEXT("%s%s"), Giants.IsEmpty() ? TEXT("") : TEXT(" and "), PlanName(Sp.Form.Plan)); }
	}
	return FString::Printf(TEXT("%d species under a %s star, %d of them strange (trees: %s and %s), giants: %s; pigment (%.2f, %.2f, %.2f) taken %.0f%%; %d glow at night"),
		F.Num(), StarLightName(F.Star), Strange, PlanName(F.Species[Slot::Warm].Form.Plan), PlanName(F.Species[Slot::Cold].Form.Plan), Giants.IsEmpty() ? TEXT("none") : *Giants,
		F.Pigment.R, F.Pigment.G, F.Pigment.B, F.Vivid * 100.f, Glowing);
}
