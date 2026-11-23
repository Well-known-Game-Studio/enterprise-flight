// Slonk - automation tests for a strange world's own geology (EGeology, PlanetTerrain.h): what a world rolls
// (WorldGenome.h), where the field marks it, what the refinement makes of the ground there, and that nothing grows on
// it. Under "Slonk.Geology."; tools/run_tests.sh runs them headless.

#include "Misc/AutomationTest.h"
#include "../FaunaHabitat.h"
#include "../PlanetBody.h"
#include "../PlanetTerrain.h"
#include "../PlanetTerrainComponent.h"
#include "../TerrainScatter.h"
#include "../WorldGenome.h"
#include "Materials/MaterialInterface.h"

#if WITH_DEV_AUTOMATION_TESTS

#ifndef SLONK_TEST_FLAGS
#define SLONK_TEST_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
#endif

namespace SlonkGeologyTestHelpers
{
	const EPlanetArchetype GeoArchetypes[] = { EPlanetArchetype::Terrestrial, EPlanetArchetype::Ocean, EPlanetArchetype::Desert, EPlanetArchetype::Ice,
		EPlanetArchetype::Lava, EPlanetArchetype::GasGiant, EPlanetArchetype::Moon, EPlanetArchetype::Venus, EPlanetArchetype::Mars, EPlanetArchetype::IceGiant };
	const EGeology GeoKinds[] = { EGeology::Crystals, EGeology::Polygons, EGeology::Terraces, EGeology::Spires, EGeology::Glass };

	/** An Earth-like world, as APlanetBody::MakeTerrainParams gives one (SlonkTerrainTests.cpp's), with a geology. */
	FPlanetTerrainParams GeoWorld(EGeology Kind, float Strength = 0.8f)
	{
		FPlanetTerrainParams P;
		P.Seed = FVector(3.1, 7.7, 1.3);
		P.Canyons = 0.6f;
		P.Strata = 0.6f;
		P.RadiusCm = 6371.0 * 1.0e5;
		P.ReliefCm = SlonkTerrain::ReliefForRadius(0.0016, P.RadiusCm);
		P.OceanDepthCm = SlonkTerrain::ReliefForRadius(0.0008, P.RadiusCm);
		P.Mountains = 0.6f;
		P.Moisture = 0.55f;
		P.CanyonDepthCm = SlonkTerrain::ScaleWithWorld(1.6e5, P.RadiusCm);
		P.CanyonWidthCm = SlonkTerrain::ScaleWithWorld(1.6e6, P.RadiusCm);
		P.SnowlineCm = 5.0e5;
		P.Geology = Kind;
		P.GeoStrength = Kind == EGeology::None ? 0.f : Strength;
		return P;
	}
	FVector GeoGlobeDir(int32 i, int32 N)
	{
		const double Z = 1.0 - 2.0 * (i + 0.5) / N;
		const double Rr = FMath::Sqrt(FMath::Max(1.0 - Z * Z, 0.0));
		const double A = i * UE_DOUBLE_PI * (3.0 - FMath::Sqrt(5.0));
		return FVector(Rr * FMath::Cos(A), Rr * FMath::Sin(A), Z);
	}
	/** The places a geology marks most strongly, the strongest first: for a terrace, those on a slope it can step. */
	TArray<FVector> MarkedPlaces(const FPlanetTerrainParams& P, int32 Count)
	{
		struct FPlace { FVector Dir; double Mark; };
		TArray<FPlace> Places;
		const int32 N = 4000;
		for (int32 i = 0; i < N; ++i)
		{
			const FVector Dir = GeoGlobeDir(i, N);
			const FPlanetTerrainSample S = SlonkTerrain::Sample(P, Dir, 2000.0);
			if (S.bWater || S.Geo < 0.6f || S.Ice > 0.05f) { continue; }
			if (P.Geology == EGeology::Terraces && (S.Slope < 0.06f || S.Slope > 0.3f)) { continue; }
			Places.Add({ Dir, (double)S.Geo });
		}
		Places.Sort([](const FPlace& A, const FPlace& B) { return A.Mark > B.Mark; });
		TArray<FVector> Out;
		for (int32 i = 0; i < Places.Num() && Out.Num() < Count; ++i) { Out.Add(Places[i].Dir); }
		return Out;
	}
	/** The ground's height over a square of it, Side samples a side and StepCm apart, at the detail a near chunk draws
	 *  (or a mesh of another fineness: the shortest wavelength it carries). */
	void GroundPatch(const FPlanetTerrainParams& P, const FVector& Dir, int32 Side, double StepCm, TArray<double>& Out, double WavelengthCm = 100.0)
	{
		const FVector E = FVector::CrossProduct(FMath::Abs(Dir.Z) < 0.9 ? FVector::ZAxisVector : FVector::XAxisVector, Dir).GetSafeNormal();
		const FVector N = FVector::CrossProduct(Dir, E);
		Out.Reset();
		for (int32 j = 0; j < Side; ++j)
		{
			for (int32 i = 0; i < Side; ++i)
			{
				const FVector D = (Dir + (E * ((i - Side / 2) * StepCm) + N * ((j - Side / 2) * StepCm)) / P.RadiusCm).GetSafeNormal();
				Out.Add(SlonkTerrain::Sample(P, D, WavelengthCm).HeightCm);
			}
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkGeologyRollsTest, "Slonk.Geology.Rolls", SLONK_TEST_FLAGS)
bool FSlonkGeologyRollsTest::RunTest(const FString& Parameters)
{
	// A world's geology is its genome's: none to strangeness 0.15, on about three strange worlds in four at 1, of a kind
	// its archetype's ground can make (an ocean world never a salt pan or a plain of glass, an ice world a pan or
	// crystal, a lava world glass or crystal), half to all of its strength, and the same on every roll. A world grown
	// stranger keeps the geology it had. It reaches the terrain's parameters on every world that rolled one (a Mars's
	// or a Venus's ground too, which the terrain shapes as it does an airless world's, without rain) unless its crust is
	// already cracked into lineae, and at strangeness 0 it leaves them as they were.
	using namespace SlonkGeologyTestHelpers;
	int32 Early = 0, Unstable = 0, Changed = 0, WrongKind = 0, BadStrength = 0, AtOne = 0, WithAtOne = 0, Kinds[(int32)EGeology::Count] = {};
	for (const EPlanetArchetype Type : GeoArchetypes)
	{
		const bool bGround = Type != EPlanetArchetype::GasGiant && Type != EPlanetArchetype::IceGiant && Type != EPlanetArchetype::Moon;
		for (uint32 k = 0; k < 200; ++k)
		{
			const uint32 Hash = 7919u * k + 31u;
			uint8 Had = 0;
			for (int32 t = 0; t <= 10; ++t)
			{
				const float S = t / 10.f;
				const FWorldGenome G = SlonkGenome::Roll(Type, S, Hash);
				Unstable += G.Geology == SlonkGenome::Roll(Type, S, Hash).Geology ? 0 : 1;
				if (S <= 0.15f && G.Geology != 0) { ++Early; }
				if (Had != 0 && G.Geology != Had) { ++Changed; }
				if (G.Geology != 0) { Had = G.Geology; }
				const EGeology Kind = (EGeology)G.Geology;
				if (G.Geology >= (uint8)EGeology::Count || (!bGround && G.Geology != 0)) { ++WrongKind; }
				if (Type == EPlanetArchetype::Ocean && (Kind == EGeology::Polygons || Kind == EGeology::Glass)) { ++WrongKind; }
				if (Type == EPlanetArchetype::Ice && Kind != EGeology::None && Kind != EGeology::Polygons && Kind != EGeology::Crystals) { ++WrongKind; }
				if (Type == EPlanetArchetype::Lava && Kind != EGeology::None && Kind != EGeology::Glass && Kind != EGeology::Crystals) { ++WrongKind; }
				if (G.Geology != 0 && (G.GeologyStrength < 0.5f || G.GeologyStrength > 1.f)) { ++BadStrength; }
				if (G.Geology == 0 && G.GeologyStrength != 0.f) { ++BadStrength; }
				if (t == 10 && bGround) { ++AtOne; WithAtOne += G.Geology != 0 ? 1 : 0; Kinds[FMath::Min((int32)G.Geology, (int32)EGeology::Count - 1)]++; }
			}
		}
	}
	AddInfo(FString::Printf(TEXT("of %d worlds with ground at strangeness 1, %d have a geology: %d crystal, %d pans, %d terraces, %d spires, %d glass"), AtOne, WithAtOne,
		Kinds[1], Kinds[2], Kinds[3], Kinds[4], Kinds[5]));
	TestEqual(TEXT("no geology to strangeness 0.15"), Early, 0);
	TestEqual(TEXT("the same world rolls the same geology"), Unstable, 0);
	TestEqual(TEXT("a world grown stranger keeps the geology it had"), Changed, 0);
	TestEqual(TEXT("each of a kind its archetype's ground can make, and none on a giant or a moon"), WrongKind, 0);
	TestEqual(TEXT("half to all of its strength, and none without one"), BadStrength, 0);
	TestTrue(TEXT("about three strange worlds in four at 1, and every kind among them"), WithAtOne > AtOne * 65 / 100 && WithAtOne < AtOne * 85 / 100
		&& Kinds[1] > 0 && Kinds[2] > 0 && Kinds[3] > 0 && Kinds[4] > 0 && Kinds[5] > 0);

	FWorldGenome G;
	G.Strangeness = 0.8f;
	G.Geology = (uint8)EGeology::Spires;
	G.GeologyStrength = 0.7f;
	const FPlanetTerrainParams Was = GeoWorld(EGeology::None);
	FPlanetTerrainParams Now = Was, Airless = Was, Cracked = Was, Tame = Was;
	SlonkGenome::ApplyGeology(G, Now);
	Airless.bAirless = true;
	SlonkGenome::ApplyGeology(G, Airless);
	Cracked.Lineae = 0.6f;
	SlonkGenome::ApplyGeology(G, Cracked);
	G.Strangeness = 0.f;
	SlonkGenome::ApplyGeology(G, Tame);
	TestTrue(TEXT("the terrain takes a strange world's geology and its strength"), Now.Geology == EGeology::Spires && Now.GeoStrength == 0.7f && Now != Was);
	TestTrue(TEXT("a world's without rain too, but not one whose crust is cracked into lineae, nor at strangeness 0"),
		Airless.Geology == EGeology::Spires && Cracked.Geology == EGeology::None && Tame == Was);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkGeologyFieldTest, "Slonk.Geology.Field", SLONK_TEST_FLAGS)
bool FSlonkGeologyFieldTest::RunTest(const FString& Parameters)
{
	// Where a world's geology takes the ground. The field marks it on a share of the land, in provinces, never in the
	// water; a world without one is marked nowhere. The mark moves nothing of the structure: the world's coasts, ranges
	// and canyons are where they would be without it, and only the last two hundred metres of the ground are its own.
	using namespace SlonkGeologyTestHelpers;
	const FPlanetTerrainParams Plain = GeoWorld(EGeology::None);
	const int32 N = 3000;
	int32 MarkedPlain = 0;
	for (int32 i = 0; i < N; ++i) { MarkedPlain += SlonkTerrain::SampleStructure(Plain, GeoGlobeDir(i, N), 2.0e4).Geo != 0.f ? 1 : 0; }
	TestEqual(TEXT("a world with no geology is marked nowhere"), MarkedPlain, 0);
	for (const EGeology Kind : GeoKinds)
	{
		const FPlanetTerrainParams P = GeoWorld(Kind);
		int32 Land = 0, Marked = 0, InWater = 0, Moved = 0;
		for (int32 i = 0; i < N; ++i)
		{
			const FVector Dir = GeoGlobeDir(i, N);
			const FPlanetTerrainSample S = SlonkTerrain::SampleStructure(P, Dir, 2.0e4);
			const FPlanetTerrainSample S0 = SlonkTerrain::SampleStructure(Plain, Dir, 2.0e4);
			Moved += S.HeightCm == S0.HeightCm && S.bWater == S0.bWater && S.Wet == S0.Wet && S.Rock == S0.Rock ? 0 : 1;
			if (S.bWater) { InWater += S.Geo != 0.f ? 1 : 0; continue; }
			++Land;
			Marked += S.Geo > 0.3f ? 1 : 0;
		}
		const TCHAR* Name = SlonkGenome::GeologyName((uint8)Kind);
		AddInfo(FString::Printf(TEXT("%s: %d of %d samples of land marked"), Name, Marked, Land));
		TestTrue(FString::Printf(TEXT("%s take a share of the land, not most of it"), Name), Marked > Land / 100 && Marked < Land * 4 / 10);
		TestEqual(FString::Printf(TEXT("%s: none in the water"), Name), InWater, 0);
		TestEqual(FString::Printf(TEXT("%s move nothing of the world's structure"), Name), Moved, 0);
	}
	// A world the terrain shapes without rain (a Mars, a Venus) has its geology too, on its dry ground.
	FPlanetTerrainParams Dry = GeoWorld(EGeology::Glass);
	Dry.bAirless = true;
	Dry.Moisture = 0.f;
	int32 MarkedDry = 0;
	for (int32 i = 0; i < N; ++i) { MarkedDry += SlonkTerrain::SampleStructure(Dry, GeoGlobeDir(i, N), 2.0e4).Geo > 0.3f ? 1 : 0; }
	AddInfo(FString::Printf(TEXT("glass on a world without rain: %d of %d samples marked"), MarkedDry, N));
	TestTrue(TEXT("a world without rain has its geology too"), MarkedDry > N / 50 && MarkedDry < N * 5 / 10);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkGeologyShapesTest, "Slonk.Geology.Shapes", SLONK_TEST_FLAGS)
bool FSlonkGeologyShapesTest::RunTest(const FString& Parameters)
{
	// What each geology makes of the ground where it is marked, against the same ground without it, over squares two
	// hundred metres across sampled every five metres. A pan and a plain of glass lie smoother than the soil they
	// replace. Crystals and spires stand out of it: metres of crystal column over and under the ground, prisms and
	// hoodoos tens of metres tall (a prism fifty at most over a column fourteen over the ground, a hoodoo fifty-five
	// with a needle on its shoulder). Terraces step a slope into level treads: most neighbouring samples lie level with
	// each other where few did.
	using namespace SlonkGeologyTestHelpers;
	const int32 Side = 40;
	const double StepCm = 500.0;
	for (const EGeology Kind : GeoKinds)
	{
		const FPlanetTerrainParams P = GeoWorld(Kind);
		FPlanetTerrainParams Plain = P;
		Plain.Geology = EGeology::None;
		Plain.GeoStrength = 0.f;
		const TCHAR* Name = SlonkGenome::GeologyName((uint8)Kind);
		const TArray<FVector> Places = MarkedPlaces(P, 4);
		if (!TestTrue(FString::Printf(TEXT("%s: found ground they mark"), Name), Places.Num() >= 2)) { continue; }
		double RoughWith = 0.0, RoughPlain = 0.0, Tallest = 0.0, Deepest = 0.0;
		int32 LevelWith = 0, LevelPlain = 0, Pairs = 0;
		for (const FVector& Dir : Places)
		{
			TArray<double> With, Without;
			GroundPatch(P, Dir, Side, StepCm, With);
			GroundPatch(Plain, Dir, Side, StepCm, Without);
			for (int32 j = 0; j < Side; ++j)
			{
				for (int32 i = 0; i < Side; ++i)
				{
					const int32 k = j * Side + i;
					Tallest = FMath::Max(Tallest, With[k] - Without[k]);
					Deepest = FMath::Min(Deepest, With[k] - Without[k]);
					if (i + 1 < Side && j + 1 < Side && i > 0 && j > 0)
					{
						// How far a sample stands off the mean of its four neighbours: the ground's roughness at five metres.
						RoughWith += FMath::Abs(With[k] - 0.25 * (With[k - 1] + With[k + 1] + With[k - Side] + With[k + Side]));
						RoughPlain += FMath::Abs(Without[k] - 0.25 * (Without[k - 1] + Without[k + 1] + Without[k - Side] + Without[k + Side]));
						++Pairs;
						LevelWith += FMath::Abs(With[k + 1] - With[k]) < 12.0 && FMath::Abs(With[k + Side] - With[k]) < 12.0 ? 1 : 0;
						LevelPlain += FMath::Abs(Without[k + 1] - Without[k]) < 12.0 && FMath::Abs(Without[k + Side] - Without[k]) < 12.0 ? 1 : 0;
					}
				}
			}
		}
		AddInfo(FString::Printf(TEXT("%s on %d squares: roughness %.1f cm against %.1f without; %.1f m over the plain ground at most and %.1f m under; %d of %d samples level with their neighbours against %d"),
			Name, Places.Num(), RoughWith / Pairs, RoughPlain / Pairs, Tallest / 100.0, -Deepest / 100.0, LevelWith, Pairs, LevelPlain));
		switch (Kind)
		{
		case EGeology::Polygons: case EGeology::Glass:
			TestTrue(FString::Printf(TEXT("%s lie smoother than the ground they replace"), Name), RoughWith < RoughPlain * 0.5);
			break;
		case EGeology::Crystals:
			TestTrue(TEXT("crystal stands metres over and under the ground, its prisms tens of metres"), Tallest > 1500.0 && Tallest < 6600.0 && Deepest < -300.0 && RoughWith > RoughPlain * 2.0);
			break;
		case EGeology::Spires:
			TestTrue(TEXT("spires stand tens of metres out of the ground, and none is a pit"), Tallest > 2000.0 && Tallest < 8000.0 && Deepest > -600.0);
			break;
		default:
			TestTrue(TEXT("terraces step a slope into level treads"), LevelWith > Pairs * 3 / 10 && LevelWith > LevelPlain * 3);
			break;
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkGeologyFarLevelsTest, "Slonk.Geology.FarLevels", SLONK_TEST_FLAGS)
bool FSlonkGeologyFarLevelsTest::RunTest(const FString& Parameters)
{
	// A forest of spires or a field of crystal does not end where the fine chunks do: a mesh too coarse to stand a spire
	// on still carries the forest, lower and blunter (at cells of thirty metres two fifths of its height and more, at sixty some of it),
	// and under a metre of it is left just short of where the refinement itself begins, at two hundred metres of
	// wavelength, so nothing pops there. The coarse levels carry at least half of what the fine ones average and stand
	// no more than twenty metres over it (a broadened spire is more rock than the spire it stands for), so a chunk does
	// not rise or sink by the forest's height as it splits. And the
	// ground a flier reads, with the relief the terrain says the geology can stand over it, clears every spire and
	// prism at full detail.
	using namespace SlonkGeologyTestHelpers;
	for (const EGeology Kind : { EGeology::Spires, EGeology::Crystals })
	{
		const FPlanetTerrainParams P = GeoWorld(Kind);
		FPlanetTerrainParams Plain = P;
		Plain.Geology = EGeology::None;
		Plain.GeoStrength = 0.f;
		const TCHAR* Name = SlonkGenome::GeologyName((uint8)Kind);
		const TArray<FVector> Places = MarkedPlaces(P, 4);
		if (!TestTrue(FString::Printf(TEXT("%s: found ground they mark"), Name), Places.Num() >= 2)) { continue; }
		// Each level sampled as its own mesh would be: its cells half its shortest wavelength, over the same 600 m square.
		const double Waves[] = { 100.0, 6000.0, 12000.0, 19000.0 };
		double Tallest[4] = {}, Mean[4] = {};
		for (int32 w = 0; w < 4; ++w)
		{
			const double CellCm = FMath::Max(Waves[w] * 0.5, 500.0);
			const int32 Side = FMath::Max(FMath::RoundToInt32(60000.0 / CellCm), 6);
			int32 Count = 0;
			for (const FVector& Dir : Places)
			{
				TArray<double> With, Without;
				GroundPatch(P, Dir, Side, CellCm, With, Waves[w]);
				GroundPatch(Plain, Dir, Side, CellCm, Without, Waves[w]);
				for (int32 k = 0; k < With.Num(); ++k)
				{
					Tallest[w] = FMath::Max(Tallest[w], With[k] - Without[k]);
					Mean[w] += With[k] - Without[k];
					++Count;
				}
			}
			Mean[w] /= FMath::Max(Count, 1);
		}
		AddInfo(FString::Printf(TEXT("%s over the plain ground, tallest and mean: %.1f and %.1f m at cells of 0.5 m; %.1f and %.1f m at 30 m; %.1f and %.1f m at 60 m; %.2f and %.2f m at 95 m"), Name,
			Tallest[0] / 100.0, Mean[0] / 100.0, Tallest[1] / 100.0, Mean[1] / 100.0, Tallest[2] / 100.0, Mean[2] / 100.0, Tallest[3] / 100.0, Mean[3] / 100.0));
		TestTrue(FString::Printf(TEXT("%s: a mesh of thirty-metre cells still carries two fifths of their height and more"), Name), Tallest[1] > Tallest[0] * 0.4 && Tallest[1] <= Tallest[0] * 1.05);
		TestTrue(FString::Printf(TEXT("%s: one of sixty-metre cells some of it, and less"), Name), Tallest[2] > 300.0 && Tallest[2] < Tallest[0] * 0.75);
		TestTrue(FString::Printf(TEXT("%s: under a metre and a half just short of where the refinement begins"), Name), Tallest[3] > 0.0 && Tallest[3] < 150.0);
		TestTrue(FString::Printf(TEXT("%s: the coarse levels carry half of what the fine ones average and more, and stand no more than twenty metres over it"), Name),
			Mean[0] > 100.0 && Mean[1] > Mean[0] * 0.5 && Mean[1] < Mean[0] + 2000.0 && Mean[2] > 0.0 && Mean[2] < Mean[0] + 2000.0);
		// What a flier flies over (SlonkFauna's FlightGroundCm: the ground at fifty metres of wavelength and the
		// geology's relief) against the ground at full detail, over the same squares.
		int32 Through = 0, Checked = 0;
		double Least = 1.0e9;
		for (const FVector& Dir : Places)
		{
			const FVector E = FVector::CrossProduct(FMath::Abs(Dir.Z) < 0.9 ? FVector::ZAxisVector : FVector::XAxisVector, Dir).GetSafeNormal();
			const FVector N = FVector::CrossProduct(Dir, E);
			for (int32 j = -30; j <= 30; ++j)
			{
				for (int32 i = -30; i <= 30; ++i)
				{
					const FVector D = (Dir + (E * (i * 700.0) + N * (j * 700.0)) / P.RadiusCm).GetSafeNormal();
					const FPlanetTerrainSample Read = SlonkTerrain::Sample(P, D, 5000.0);
					if (Read.bWater) { continue; }
					const double Clear = Read.HeightCm + SlonkTerrain::GeologyReliefCm(P, Read.Geo) - SlonkTerrain::Sample(P, D, 0.0).HeightCm;
					Least = FMath::Min(Least, Clear);
					// (Three metres of slack: the soil's hummocks finer than fifty metres, which a flier's cruise clears on any world.)
					Through += Clear < -300.0 ? 1 : 0;
					++Checked;
				}
			}
		}
		AddInfo(FString::Printf(TEXT("%s: of %d places a flier's ground was read, %d lie more than 3 m under the ground at full detail; the least it clears it by %.1f m"), Name, Checked, Through, Least / 100.0));
		TestTrue(FString::Printf(TEXT("%s: the ground a flier reads clears every one of them"), Name), Checked > 10000 && Through == 0);
		// A chunk on that ground hangs its skirt as far as the geology can stand over a neighbour at another level
		// (a spire or a prism its border cuts through has a back to it), where the same chunk without it hangs a few
		// metres.
		int32 Face; int64 X, Y;
		const int32 Depth = FMath::CeilToInt32(FMath::Log2(P.RadiusCm * UE_DOUBLE_HALF_PI / 31000.0));
		if (TestTrue(FString::Printf(TEXT("%s: a chunk on the marked ground"), Name), UPlanetTerrainComponent::ChunkAt(Places[0], Depth, Face, X, Y)))
		{
			const FScatterSettings Bare;
			TArray<FTransform> Xf[SlonkScatter::NumMeshes], Xf0[SlonkScatter::NumMeshes];
			TArray<double> Drawn[SlonkScatter::NumMeshes], Drawn0[SlonkScatter::NumMeshes];
			double SkirtWith = 0.0, SkirtWithout = 0.0;
			UPlanetTerrainComponent::BuildChunkScatterForTest(P, Bare, Face, Depth, X, Y, 64, Xf, Drawn, nullptr, nullptr, nullptr, &SkirtWith);
			UPlanetTerrainComponent::BuildChunkScatterForTest(Plain, Bare, Face, Depth, X, Y, 64, Xf0, Drawn0, nullptr, nullptr, nullptr, &SkirtWithout);
			AddInfo(FString::Printf(TEXT("%s: the chunk's skirt hangs %.1f m, %.1f m without the geology"), Name, SkirtWith / 100.0, SkirtWithout / 100.0));
			TestTrue(FString::Printf(TEXT("%s: the chunk's skirt hangs as far as they stand"), Name),
				SkirtWith > SlonkTerrain::GeologyReliefCm(P, 1.f) * 0.99 && SkirtWith > SkirtWithout * 3.0 && SkirtWithout > 0.0);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkGeologyBareTest, "Slonk.Geology.Bare", SLONK_TEST_FLAGS)
bool FSlonkGeologyBareTest::RunTest(const FString& Parameters)
{
	// Nothing grows on a world's own geology: where the field marks the ground past half (the material draws it all the
	// geology's from there), no tree stands, though on the same ground without the geology they would; and the forest's
	// cover the material paints is gone there with them. No herd grazes it or sets foot on it, where on the same ground
	// without it some would.
	using namespace SlonkGeologyTestHelpers;
	const FPlanetTerrainParams P = GeoWorld(EGeology::Terraces, 1.f);
	FPlanetTerrainParams Plain = P;
	Plain.Geology = EGeology::None;
	Plain.GeoStrength = 0.f;
	FScatterSettings S;
	S.Life = 1.f;
	const int32 D = FMath::CeilToInt32(FMath::Log2(P.RadiusCm * UE_DOUBLE_HALF_PI / 31000.0));
	const int32 Warm = SlonkScatter::PlantMesh(EPlant::Broadleaf), Cold = SlonkScatter::PlantMesh(EPlant::Conifer);
	int32 Chunks = 0, OnMarked = 0, WouldBe = 0, MarkCount = 0;
	double CoverOn = 0.0, CoverPlain = 0.0, MarkOn = 0.0, MarkPlain = 0.0;
	// Wooded ground the terraces take.
	const int32 N = 4000;
	for (int32 i = 0; i < N && Chunks < 4; ++i)
	{
		const FVector Dir = GeoGlobeDir(i, N);
		if (FMath::Abs(Dir.Z) > 0.6 || SlonkTerrain::SampleStructure(P, Dir, 2.0e4).Geo < 0.9f || SlonkScatter::HabitatAt(Plain, Dir).Forest < 0.4) { continue; }
		int32 Face; int64 X, Y;
		if (!UPlanetTerrainComponent::ChunkAt(Dir, D, Face, X, Y)) { continue; }
		TArray<FTransform> With[SlonkScatter::NumMeshes], Without[SlonkScatter::NumMeshes];
		TArray<double> Dw[SlonkScatter::NumMeshes], Dn[SlonkScatter::NumMeshes];
		TArray<float> CoverWith, CoverWithout, MarkWith, MarkWithout;
		UPlanetTerrainComponent::BuildChunkScatterForTest(P, S, Face, D, X, Y, 64, With, Dw, nullptr, &CoverWith, &MarkWith);
		UPlanetTerrainComponent::BuildChunkScatterForTest(Plain, S, Face, D, X, Y, 64, Without, Dn, nullptr, &CoverWithout, &MarkWithout);
		++Chunks;
		for (const float M : MarkWith) { MarkOn += M; ++MarkCount; }
		for (const float M : MarkWithout) { MarkPlain += M; }
		for (const int32 M : { Warm, Cold })
		{
			for (const FTransform& T : With[M]) { OnMarked += SlonkTerrain::SampleStructure(P, T.GetLocation().GetSafeNormal(), 2.0e4).Geo > 0.6f ? 1 : 0; }
			for (const FTransform& T : Without[M]) { WouldBe += SlonkTerrain::SampleStructure(P, T.GetLocation().GetSafeNormal(), 2.0e4).Geo > 0.6f ? 1 : 0; }
		}
		for (const float C : CoverWith) { CoverOn += C; }
		for (const float C : CoverWithout) { CoverPlain += C; }
	}
	AddInfo(FString::Printf(TEXT("%d chunks the terraces take in wooded country: %d trees on ground marked theirs, where %d would stand without them; the forest's cover %.0f against %.0f"), Chunks, OnMarked,
		WouldBe, CoverOn, CoverPlain));
	if (!TestTrue(TEXT("found wooded ground the geology takes"), Chunks >= 2 && WouldBe > 50)) { return true; }
	TestEqual(TEXT("no tree stands on ground that is the geology's"), OnMarked, 0);
	int32 Grazed = 0, Crossed = 0, WouldCross = 0;
	for (const EGeology Kind : GeoKinds)
	{
		const FPlanetTerrainParams Pk = GeoWorld(Kind);
		for (int32 i = 0; i < N; ++i)
		{
			const FVector Dir = GeoGlobeDir(i, N);
			if (SlonkTerrain::SampleStructure(Pk, Dir, 2.0e4).Geo < 0.5f) { continue; }
			const SlonkScatter::FHabitat H = SlonkScatter::HabitatAt(Pk, Dir);
			Grazed += SlonkFauna::HerdGrass(H, false) ? 1 : 0;
			Crossed += SlonkFauna::HerdCrosses(H) ? 1 : 0;
			WouldCross += SlonkFauna::HerdCrosses(SlonkScatter::HabitatAt(Plain, Dir)) ? 1 : 0;
		}
	}
	AddInfo(FString::Printf(TEXT("of the places the five geologies take, herds would cross %d without them; with them they graze %d and cross %d"), WouldCross, Grazed, Crossed));
	TestTrue(TEXT("no herd grazes a geology or sets foot on it, where some would without it"), Grazed == 0 && Crossed == 0 && WouldCross > 50);
	TestTrue(TEXT("and the forest's painted cover is gone there with the trees"), CoverOn < CoverPlain * 0.3);
	// What the material draws the geology from: the mark each vertex carries in the lineae's place (none on the same
	// ground without the geology, which has no lineae either).
	AddInfo(FString::Printf(TEXT("the vertices' mark on those chunks: %.2f a vertex with the geology, %.2f in all without"), MarkOn / FMath::Max(MarkCount, 1), MarkPlain));
	TestTrue(TEXT("the chunk's vertices carry the geology's mark to the material, and none without it"), MarkCount > 0 && MarkOn / MarkCount > 0.5 && MarkPlain == 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkGeologyMaterialTest, "Slonk.Geology.Material", SLONK_TEST_FLAGS)
bool FSlonkGeologyMaterialTest::RunTest(const FString& Parameters)
{
	// The terrain's material draws a geology from three parameters the planet sets: which kind the vertex's mark means
	// (none on a world without one, as its default is, so every other world's mark is its lineae's as it was), and on
	// a world that has one its two colours. The material has all three. And each kind has two colours of its own.
	UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Slonk/Materials/M_PlanetTerrain.M_PlanetTerrain"));
	if (!TestTrue(TEXT("the terrain material loads"), Mat != nullptr)) { return true; }
	float Kind = -1.f;
	FLinearColor A, B;
	const bool bKind = Mat->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("GeoKind")), Kind);
	const bool bA = Mat->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("GeoColor")), A), bB = Mat->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("GeoColor2")), B);
	TestTrue(TEXT("the terrain material has the geology's three parameters (tools/planet_materials/terrain_material.py builds them; without them a geology's mark is drawn as lineae), the kind at none by default"),
		bKind && bA && bB && Kind == 0.f);
	FPlanetStyle Style;
	int32 Same = 0;
	for (uint8 K = 1; K < (uint8)EGeology::Count; ++K)
	{
		FWorldGenome G;
		G.Strangeness = 0.8f;
		G.Geology = K;
		G.GeologyStrength = 0.8f;
		FLinearColor Ca, Cb;
		SlonkGenome::GeologyColors(G, Style, Ca, Cb);
		Same += Ca.Equals(Cb, 0.02f) ? 1 : 0;
	}
	TestEqual(TEXT("each geology's two colours are two colours"), Same, 0);
	return true;
}

#endif
