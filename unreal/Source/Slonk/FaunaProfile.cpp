#include "FaunaProfile.h"
#include "FaunaMeshes.h"
#include "ScatterSpot.h"
#include "TerrainScatter.h"

namespace
{
	using SlonkFauna::FFaunaKindDesc;

	/**
	 * The kind table. The fliers' figures: a 1.2 km cell holds at most one flock, which flies a loop of a few
	 * kilometres round it at 9 to 20 m/s; flocks are spawned within 3.7 km of the camera's ground point and kept to
	 * 4.6 km (their loops reach 0.85 km from their middle, so one that comes at its reach or goes past that is beyond the
	 * 2.5 km their members fade by), at most 12 of them and 1,000 birds. With the caps full, the farthest out of sight
	 * (within reach or past it) give way to a flock 200 m nearer; where the caps are held by flocks it cannot take the
	 * place of (more in sight than the caps allow, or as far off as it), it waits, and may come in sight before there is
	 * room. A craft in the air (not set down) lower than 250 m over the ground and within 250 m of a flock, or someone
	 * on foot within 40 m, startles it: it turns and flees 220 m, and drifts back. Their shadows within 300 m. The material moves a bird's
	 * vertices up to 4 unit-mesh metres past its box (its wings, the ripple through the flock, a startle's scatter of up
	 * to two spans and the speck floor's growth: fauna_material.py), which every instance's box is padded by; their LOD
	 * screen sizes are set with that box (about 7.8 spans across, where a bird is 1 span), so a bird goes to its 12
	 * triangles about 40 pixels across and to its 2 about 4.
	 * The motes are not the field's: the terrain's chunks carry them (SlonkScatter::PlaceMotes), clouds of 24 over cells of
	 * about 9 m, all of them on the chunks to 160 m and a quarter of them on the next level up, so on an Earth they fade
	 * out by about 460 m from the camera and the share kept by about 920 m (MoteMaxEdgeCm, MoteKeep, the plants' fade), far
	 * enough for a ship's chase camera to see the meadows twinkle; none are built while the camera and the pawn are both
	 * more than about a kilometre over the ground, the chunks carrying them not being made. A cloud past about 110 m is 8
	 * cards and past 550 m 2 (16 and 4 triangles, on the GPU instances: SlonkFaunaMeshes::MoteLodScreenSize), so their row
	 * here is the budget of those drawn whole, at 24 cards: 400 clouds (about 170 in the densest country,
	 * Slonk.Fauna.MotesNest; some 1,700 clouds in all about a camera on an Earth's land, 5,000 in the densest country,
	 * about 65,000 triangles there).
	 * The rest are the later phases' budgets (not spawned yet): 2,000 walkers in 16 herds, 30 leapers in 6 groups, 24
	 * floaters alone. 3,454 instances and 58 group sets in all.
	 */
	FFaunaKindDesc MakeDesc(EFaunaKind K)
	{
		FFaunaKindDesc D;
		D.Kind = K;
		switch (K)
		{
		case EFaunaKind::Flier:
			D.bLive = true;
			D.CellCm = 1.2e5; D.ReachCm = 3.7e5; D.MaxAltitudeCm = 2.5e5;
			D.GroupMin = 3; D.GroupMax = 120; D.MaxGroups = 12; D.MaxMembers = 1000;
			D.ScareRadiusCm = 2.5e4; D.ScareAltitudeCm = 2.5e4; D.CrewScareCm = 4.0e3; D.FleeCm = 2.2e4;
			D.ShadowReachCm = 3.0e4; D.FadeCm = 2.5e5;
			D.CullPaddingUnits = 400.0;
			D.LodScreenSize[0] = 1.f; D.LodScreenSize[1] = 0.55f; D.LodScreenSize[2] = 0.055f;
			break;
		case EFaunaKind::Herd:
			D.CellCm = 2.0e5; D.ReachCm = 3.0e5; D.MaxAltitudeCm = 1.5e5;
			D.GroupMin = 8; D.GroupMax = 200; D.MaxGroups = 16; D.MaxMembers = 2000;
			D.ScareRadiusCm = 1.5e4; D.ScareAltitudeCm = 1.5e4; D.CrewScareCm = 6.0e3; D.FleeCm = 3.0e4;
			D.ShadowReachCm = 6.0e4; D.FadeCm = 2.2e5;
			break;
		case EFaunaKind::Mote:
			D.CellCm = SlonkScatter::MoteSpacingCm; D.ReachCm = 9.2e4; D.MaxAltitudeCm = 1.0e5;
			D.GroupMin = SlonkFauna::Mote::MotesPerCloud; D.GroupMax = SlonkFauna::Mote::MotesPerCloud; D.MaxGroups = 0; D.MaxMembers = 400;
			D.FadeCm = 4.6e4;
			D.CullPaddingUnits = SlonkFauna::Mote::ReachCm;
			for (int32 L = 0; L < 3; ++L) { D.LodScreenSize[L] = SlonkFaunaMeshes::MoteLodScreenSize[L]; }
			break;
		case EFaunaKind::Leaper:
			D.CellCm = 1048576.0 / 52.0; D.ReachCm = 1.5e5; D.MaxAltitudeCm = 5.0e4;
			D.GroupMin = 1; D.GroupMax = 5; D.MaxGroups = 6; D.MaxMembers = 30;
			D.ScareRadiusCm = 5.0e3; D.ScareAltitudeCm = 5.0e3; D.FadeCm = 1.2e5;
			break;
		case EFaunaKind::Floater:
			D.CellCm = 3.0e5; D.ReachCm = 1.2e6; D.MaxAltitudeCm = 1.0e6;
			D.GroupMin = 1; D.GroupMax = 1; D.MaxGroups = 24; D.MaxMembers = 24;
			D.ShadowReachCm = 2.0e5; D.FadeCm = 1.0e6;
			break;
		default: break;
		}
		return D;
	}

	const FFaunaKindDesc GKinds[SlonkFauna::NumKinds] = { MakeDesc(EFaunaKind::Flier), MakeDesc(EFaunaKind::Herd), MakeDesc(EFaunaKind::Mote), MakeDesc(EFaunaKind::Leaper), MakeDesc(EFaunaKind::Floater) };

	/** A colour turned round the colour wheel, its saturation and brightness kept. */
	FLinearColor TurnPigment(const FLinearColor& C, float Deg)
	{
		if (Deg == 0.f) { return C; }
		FLinearColor Hsv = C.LinearRGBToHSV();
		Hsv.R = FMath::Fmod(Hsv.R + Deg + 720.f, 360.f);
		FLinearColor Out = Hsv.HSVToLinearRGB();
		Out.A = 1.f;
		return Out;
	}
}

const SlonkFauna::FFaunaKindDesc& SlonkFauna::KindDesc(EFaunaKind K)
{
	return GKinds[FMath::Clamp((int32)K, 0, NumKinds - 1)];
}

const TCHAR* SlonkFauna::KindName(EFaunaKind K)
{
	switch (K)
	{
	case EFaunaKind::Flier: return TEXT("flier");
	case EFaunaKind::Herd: return TEXT("herd");
	case EFaunaKind::Mote: return TEXT("mote");
	case EFaunaKind::Leaper: return TEXT("leaper");
	case EFaunaKind::Floater: return TEXT("floater");
	default: return TEXT("?");
	}
}

bool SlonkFauna::KindFromName(const FString& Name, EFaunaKind& Out)
{
	const FString N = Name.ToLower();
	for (int32 K = 0; K < NumKinds; ++K)
	{
		const FString Kn = KindName((EFaunaKind)K);
		if (N == Kn || N == Kn + TEXT("s")) { Out = (EFaunaKind)K; return true; }
	}
	if (N == TEXT("bird") || N == TEXT("birds")) { Out = EFaunaKind::Flier; return true; }
	return false;
}

const TCHAR* SlonkFauna::HabitatName(EFlierHabitat H)
{
	switch (H)
	{
	case EFlierHabitat::Woodland: return TEXT("woodland");
	case EFlierHabitat::Meadow: return TEXT("meadow");
	case EFlierHabitat::Coast: return TEXT("coast");
	case EFlierHabitat::Cliff: return TEXT("cliff");
	default: return TEXT("?");
	}
}

FFaunaBodyPlan SlonkFauna::EarthPlan(EFaunaKind K)
{
	FFaunaBodyPlan P;
	switch (K)
	{
	case EFaunaKind::Flier:
		// A bird: two feathered wings, a pair of legs tucked under it, a fanned tail.
		P.LimbPairs = 1; P.Limbs = EFaunaLimbs::Legs; P.Wings = EFaunaWings::Feathered;
		P.Length = 0.45f; P.Girth = 0.09f; P.Tail = 0.14f; P.Chord = 0.24f;
		break;
	case EFaunaKind::Herd:
		// Four legs, a long body.
		P.LimbPairs = 2; P.Limbs = EFaunaLimbs::Legs; P.Length = 1.f; P.Girth = 0.35f; P.Tail = 0.2f; P.Chord = 0.f;
		break;
	case EFaunaKind::Leaper:
		// A fish: fins, a tail, no legs.
		P.LimbPairs = 1; P.Limbs = EFaunaLimbs::Fins; P.Length = 1.f; P.Girth = 0.2f; P.Tail = 0.25f; P.Chord = 0.f;
		break;
	case EFaunaKind::Floater:
		// Radial: a bell and its trailing arms (none on the Earth; the shape a strange world's would start from).
		P.Symmetry = 8; P.LimbPairs = 4; P.Limbs = EFaunaLimbs::Tentacles; P.Length = 1.f; P.Girth = 1.f; P.Tail = 0.f; P.Chord = 0.f;
		break;
	default:
		// A mote is a speck.
		P.Symmetry = 1; P.Length = 1.f; P.Girth = 1.f; P.Tail = 0.f; P.Chord = 0.f;
		break;
	}
	return P;
}

SlonkFauna::FFaunaLook SlonkFauna::LookOf(const FWorldGenome& G)
{
	FFaunaLook L;
	const float Turn = G.SkyHueShiftDeg * G.Strangeness;
	L.HueTurnDeg = Turn;
	L.Vivid = FMath::Clamp(G.Strangeness, 0.f, 1.f);
	if (Turn != 0.f)
	{
		// Round the colour wheel, its saturation and brightness kept: the plankton's glow, the fireflies' and the pollen's.
		L.Glow = TurnPigment(L.Glow, Turn);
		L.Firefly = TurnPigment(L.Firefly, Turn);
		L.Pollen = TurnPigment(L.Pollen, Turn);
	}
	return L;
}

SlonkFauna::FFaunaProfile SlonkFauna::Roll(const FWorldGenome& G, EPlanetArchetype Type, uint32 Hash)
{
	FFaunaProfile F;
	F.Strangeness = FMath::Clamp(G.Strangeness, 0.f, 1.f);
	F.Type = Type;
	F.Hash = Hash;
	// A stream of its own: none of the world's other rolls (its style, its genome, its people) moves.
	FRandomStream R((int32)(Hash * 0x9E3779B1u + 0x27d4eb2fu));
	F.Seed = (uint32)R.GetUnsignedInt();
	// One draw per kind for the biosphere phase's plans, taken now so the seed above never moves when they come. At
	// strangeness 0, and in this phase, every kind is the Earth's.
	for (int32 K = 0; K < NumKinds; ++K)
	{
		R.FRand();
		F.Plans[K] = EarthPlan((EFaunaKind)K);
	}
	F.Look = LookOf(G);
	return F;
}

bool SlonkFauna::SameProfile(const FFaunaProfile& A, const FFaunaProfile& B)
{
	if (A.Strangeness != B.Strangeness || A.Type != B.Type || A.Hash != B.Hash || A.Seed != B.Seed) { return false; }
	for (int32 K = 0; K < NumKinds; ++K) { if (A.Plans[K] != B.Plans[K]) { return false; } }
	return A.Look.Glow == B.Look.Glow && A.Look.Firefly == B.Look.Firefly && A.Look.Pollen == B.Look.Pollen && A.Look.HueTurnDeg == B.Look.HueTurnDeg
		&& A.Look.Vivid == B.Look.Vivid;
}

FLinearColor SlonkFauna::FlierPigment(const FFaunaLook& L, const FScatterPalette& Pal, EFlierHabitat H, double Dry, double Vary)
{
	// The living ground's colour where the flock lives, the plants' own.
	const FLinearColor Ground = SlonkScatter::FoliageAt(Pal, Dry, 1.0);
	const float Luma = FMath::Max(0.2126f * Ground.R + 0.7152f * Ground.G + 0.0722f * Ground.B, 1e-3f);
	// As an Earth's birds wear it: its brightness alone, browned, and set to the habitat's tone (the woods' birds dark,
	// the meadows' near black, the gulls pale, the cliffs' a slate).
	FLinearColor Earth;
	switch (H)
	{
	case EFlierHabitat::Meadow: Earth = FLinearColor(0.045f, 0.043f, 0.05f); break;
	case EFlierHabitat::Coast: Earth = FLinearColor(0.62f, 0.64f, 0.66f); break;
	case EFlierHabitat::Cliff: Earth = FLinearColor(0.2f, 0.21f, 0.24f); break;
	default: Earth = FLinearColor(0.16f, 0.12f, 0.085f) * FMath::Clamp(Luma / 0.3f, 0.6f, 1.4f); break;
	}
	// A strange world's: the ground's colour itself, as bright as the Earth's tone, at full strength on the strangest.
	FLinearColor Own = Ground * ((0.2126f * Earth.R + 0.7152f * Earth.G + 0.0722f * Earth.B) / Luma);
	FLinearColor C = FMath::Lerp(Earth, Own, L.Vivid);
	C = TurnPigment(C, L.HueTurnDeg) * (float)Vary;
	C.A = 1.f;
	return C;
}

SlonkFauna::FSeaLife SlonkFauna::SeaLifeOf(float GroundLife, const FWorldGenome& G)
{
	FSeaLife Sea;
	Sea.PlanktonColor = LookOf(G).Glow;
	const float Life = FMath::Clamp(GroundLife, 0.f, 1.f);
	if (Life <= 0.f || G.Solvent == ESolvent::Magma || G.Solvent == ESolvent::None) { return Sea; }
	Sea.PlanktonGlow = PlanktonGlowAtLife * Life;
	Sea.ShoalDensity = ShoalDensityAtLife * Life;
	return Sea;
}
