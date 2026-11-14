// Slonk - automation tests for the peoples of the worlds (Civilization.h) and the settlements they build
// (PlanetCities.h, BuildingMeshes.h), without a world. Under "Slonk.Cities."; tools/run_tests.sh runs them headless.

#include "Misc/AutomationTest.h"
#include "Engine/StaticMesh.h"
#include "../Civilization.h"
#include "../PlanetBody.h"
#include "../PlanetCities.h"
#include "../PlanetTerrain.h"
#include "../BuildingMeshes.h"

#if WITH_DEV_AUTOMATION_TESTS

#ifndef SLONK_TEST_FLAGS
#define SLONK_TEST_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
#endif

namespace
{
	FVector Fib(int32 i, int32 N)
	{
		const double Z = 1.0 - 2.0 * (i + 0.5) / N;
		const double Rr = FMath::Sqrt(FMath::Max(1.0 - Z * Z, 0.0));
		const double A = i * UE_DOUBLE_PI * (3.0 - FMath::Sqrt(5.0));
		return FVector(Rr * FMath::Cos(A), Rr * FMath::Sin(A), Z);
	}

	/** An Earth-like world and a desert one, as APlanetBody::MakeTerrainParams gives them. */
	FPlanetTerrainParams Earthish()
	{
		FPlanetTerrainParams P;
		P.Seed = FVector(3.1, 7.7, 1.3);
		P.RadiusCm = 6.371e8;
		P.ReliefCm = SlonkTerrain::ReliefForRadius(0.0016, P.RadiusCm);
		P.OceanDepthCm = SlonkTerrain::ReliefForRadius(0.0008, P.RadiusCm);
		P.Mountains = 0.6f; P.Canyons = 0.5f; P.Strata = 0.6f; P.Moisture = 0.55f;
		P.CanyonDepthCm = SlonkTerrain::ScaleWithWorld(1.6e5, P.RadiusCm);
		P.CanyonWidthCm = SlonkTerrain::ScaleWithWorld(1.6e6, P.RadiusCm);
		return P;
	}
	FPlanetTerrainParams Deserty()
	{
		FPlanetTerrainParams P = Earthish();
		P.ReliefCm = SlonkTerrain::ReliefForRadius(0.002, P.RadiusCm);
		P.SeaLevel = 0.05f; P.ContinentScale = 2.2f;
		P.Craters = 0.45f; P.CraterScale = 1.3f; P.Maria = 0.2f; P.IceCapStart = 0.92f;
		P.Mountains = 0.5f; P.Canyons = 0.9f; P.Strata = 0.9f; P.Moisture = 0.05f;
		P.CanyonDepthCm = SlonkTerrain::ScaleWithWorld(2.0e5, P.RadiusCm);
		P.CanyonWidthCm = SlonkTerrain::ScaleWithWorld(1.8e6, P.RadiusCm);
		P.SnowlineCm = 6.5e5;
		return P;
	}

	/** The first town of the kind asked for (on a cliff or on open ground) that a people builds near a run of
	 *  points over the globe. */
	bool FindTown(const FPlanetTerrainParams& P, const FCivilization& Civ, bool bCliff, FTownSite& Out, int32 Tries = 300)
	{
		for (int32 i = 0; i < Tries; ++i)
		{
			const FVector Dir = Fib(i * 7 + 3, Tries * 7);
			if (SlonkTerrain::SampleStructure(P, Dir, 2.0e5).bWater) { continue; }
			TArray<FTownSite> Towns;
			SlonkCities::EnumerateTowns(P, Civ, Dir * P.RadiusCm, 2.0e6, Towns, 12);
			for (const FTownSite& T : Towns) { if (T.bCliff == bCliff) { Out = T; return true; } }
		}
		return false;
	}

	/** The kinds a town's buildings come in. */
	TSet<EBuildingKind> KindsOf(const TArray<FTransform>* ByKind)
	{
		TSet<EBuildingKind> K;
		for (int32 i = 0; i < (int32)EBuildingKind::Count; ++i) { if (ByKind[i].Num() > 0) { K.Add((EBuildingKind)i); } }
		return K;
	}
	FString KindList(const TSet<EBuildingKind>& K)
	{
		TArray<FString> S;
		for (EBuildingKind Kind : K) { S.Add(SlonkBuildings::KindName(Kind)); }
		S.Sort();
		return FString::Join(S, TEXT(", "));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkCitiesPeoplesTest, "Slonk.Cities.Peoples", SLONK_TEST_FLAGS)
bool FSlonkCitiesPeoplesTest::RunTest(const FString& Parameters)
{
	// Who lives where. An Earth-like world the style lit keeps an electric people (industrial or later, its ground
	// aglow); one it left dark is home to a people before electricity about half the time, with no ground glow; a
	// desert about two in five, mostly early and mostly cliff dwellers; the dead worlds nobody. Only a modern or later
	// people grows its buildings, and a grown or early people lays no street grid. The same seed is the same people.
	int32 LitBad = 0, DarkPeople = 0, DarkBad = 0, Desert = 0, DesertCliff = 0, Dead = 0, OrganicBad = 0, GridBad = 0;
	const int32 N = 2000;
	for (int32 i = 0; i < N; ++i)
	{
		const uint32 H = (uint32)i * 2654435761u;
		const FCivilization Lit = SlonkCivs::Natives(EPlanetArchetype::Terrestrial, H, true);
		LitBad += !Lit.bInhabited || Lit.Age < ETechAge::Industrial || Lit.GroundLights <= 0.f ? 1 : 0;
		const FCivilization Dark = SlonkCivs::Natives(EPlanetArchetype::Terrestrial, H, false);
		if (Dark.bInhabited) { ++DarkPeople; DarkBad += Dark.Age > ETechAge::Iron || Dark.GroundLights > 0.f ? 1 : 0; }
		const FCivilization Des = SlonkCivs::Natives(EPlanetArchetype::Desert, H, false);
		if (Des.bInhabited) { ++Desert; DesertCliff += Des.CliffAffinity >= 0.5f ? 1 : 0; }
		for (const EPlanetArchetype K : { EPlanetArchetype::Lava, EPlanetArchetype::Mars, EPlanetArchetype::Moon, EPlanetArchetype::GasGiant, EPlanetArchetype::Venus })
		{
			Dead += SlonkCivs::Natives(K, H, true).bInhabited ? 1 : 0;
		}
		for (const FCivilization* C : { &Lit, &Dark, &Des })
		{
			if (!C->bInhabited) { continue; }
			OrganicBad += C->IsOrganic() && C->Age < ETechAge::Modern ? 1 : 0;
			GridBad += (C->IsOrganic() || C->Age <= ETechAge::Iron) && C->StreetGrid > 0.f ? 1 : 0;
		}
	}
	AddInfo(FString::Printf(TEXT("of %d seeds: dark Earth-like worlds with a people %d, deserts with one %d (%d of them cliff dwellers)"), N, DarkPeople, Desert, DesertCliff));
	TestEqual(TEXT("a lit Earth-like world has an electric people, its ground aglow"), LitBad, 0);
	TestTrue(TEXT("about half the dark Earth-like worlds have a people"), DarkPeople > N * 0.42 && DarkPeople < N * 0.58);
	TestEqual(TEXT("and none of them electric"), DarkBad, 0);
	TestTrue(TEXT("about two deserts in five have a people"), Desert > N * 0.33 && Desert < N * 0.47);
	TestTrue(TEXT("most of them cliff dwellers"), DesertCliff * 3 > Desert * 2);
	TestEqual(TEXT("the dead worlds have nobody"), Dead, 0);
	TestEqual(TEXT("only a modern or later people grows its buildings"), OrganicBad, 0);
	TestEqual(TEXT("no grid under a grown or an early people's towns"), GridBad, 0);
	const FCivilization A = SlonkCivs::Natives(EPlanetArchetype::Desert, 12345u, false), B = SlonkCivs::Natives(EPlanetArchetype::Desert, 12345u, false);
	TestTrue(TEXT("the same seed is the same people"), A.bInhabited == B.bInhabited && A.Age == B.Age && A.Organic == B.Organic && A.CliffAffinity == B.CliffAffinity);
	TestFalse(TEXT("a people describes itself"), SlonkCivs::Make(ETechAge::Iron, 0.f, 0.9f, true).Describe().IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkCitiesShapesTest, "Slonk.Cities.Shapes", SLONK_TEST_FLAGS)
bool FSlonkCitiesShapesTest::RunTest(const FString& Parameters)
{
	// Every building shape builds, and fills the unit box the town builder scales (a metre each way, its base at
	// the bottom), so a building's size is its instance scale and it stands on its lot.
	for (int32 i = 0; i < (int32)EBuildingKind::Count; ++i)
	{
		const EBuildingKind K = (EBuildingKind)i;
		UStaticMesh* M = SlonkBuildings::GetMesh(K);
		if (!TestNotNull(FString::Printf(TEXT("%s builds"), SlonkBuildings::KindName(K)), M)) { continue; }
		const FBox B = M->GetBoundingBox();
		const FVector Ext = B.GetSize();
		AddInfo(FString::Printf(TEXT("%s: %.0f x %.0f x %.0f cm, base at %.0f"), SlonkBuildings::KindName(K), Ext.X, Ext.Y, Ext.Z, B.Min.Z));
		TestTrue(FString::Printf(TEXT("%s stays in the unit box"), SlonkBuildings::KindName(K)), B.Min.X >= -50.5 && B.Max.X <= 50.5 && B.Min.Y >= -50.5 && B.Max.Y <= 50.5 && B.Min.Z >= -50.5 && B.Max.Z <= 50.5);
		TestTrue(FString::Printf(TEXT("%s stands on the box's floor and reaches most of its height"), SlonkBuildings::KindName(K)), FMath::IsNearlyEqual(B.Min.Z, -50.0, 1.0) && B.Max.Z > 40.0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkCitiesTownsByAgeTest, "Slonk.Cities.TownsByAge", SLONK_TEST_FLAGS)
bool FSlonkCitiesTownsByAgeTest::RunTest(const FString& Parameters)
{
	// Each people builds its own: huts for the stone age; houses, a hut or two and a round keep for the iron age;
	// brick, halls and works for the industrial; blocks, towers and houses for the modern; spires and domes for the
	// starfaring; pods, domes and bulb towers for a grown people. And every building stands on its ground, its foot
	// set a little into it.
	const FPlanetTerrainParams P = Earthish();
	struct FCase { const TCHAR* Name; FCivilization Civ; TArray<EBuildingKind> Allowed; TArray<EBuildingKind> Some; };
	const TArray<FCase> Cases = {
		{ TEXT("stone"), SlonkCivs::Make(ETechAge::Stone, 0.f, 0.f, false), { EBuildingKind::Hut }, { EBuildingKind::Hut } },
		{ TEXT("iron"), SlonkCivs::Make(ETechAge::Iron, 0.f, 0.f, false), { EBuildingKind::House, EBuildingKind::Hut, EBuildingKind::RoundTower }, { EBuildingKind::House } },
		{ TEXT("industrial"), SlonkCivs::Make(ETechAge::Industrial, 0.f, 0.f, false), { EBuildingKind::Block, EBuildingKind::House, EBuildingKind::Works }, { EBuildingKind::Works } },
		{ TEXT("modern"), SlonkCivs::Make(ETechAge::Modern, 0.f, 0.f, false), { EBuildingKind::Block, EBuildingKind::Tower, EBuildingKind::House }, { EBuildingKind::Block } },
		{ TEXT("starfaring"), SlonkCivs::Make(ETechAge::Starfaring, 0.f, 0.f, false), { EBuildingKind::Block, EBuildingKind::Tower, EBuildingKind::Spire, EBuildingKind::Dome }, { EBuildingKind::Dome } },
		{ TEXT("grown"), SlonkCivs::Make(ETechAge::Starfaring, 1.f, 0.f, false), { EBuildingKind::Pod, EBuildingKind::Dome, EBuildingKind::BulbTower }, { EBuildingKind::Pod, EBuildingKind::BulbTower } },
	};
	for (const FCase& C : Cases)
	{
		// A few towns together, so the rarer kinds turn up.
		TArray<FTransform> ByKind[(int32)EBuildingKind::Count];
		int32 Towns = 0;
		for (int32 i = 0; i < 120 && Towns < 4; ++i)
		{
			const FVector Dir = Fib(i * 11 + 5, 1320);
			if (SlonkTerrain::SampleStructure(P, Dir, 2.0e5).bWater) { continue; }
			TArray<FTownSite> Sites;
			SlonkCities::EnumerateTowns(P, C.Civ, Dir * P.RadiusCm, 2.0e6, Sites, 6);
			for (const FTownSite& T : Sites) { if (!T.bCliff && Towns < 4) { SlonkCities::BuildTown(P, C.Civ, T, ByKind, 2500); ++Towns; } }
		}
		const TSet<EBuildingKind> Kinds = KindsOf(ByKind);
		int32 Total = 0, Off = 0;
		for (int32 k = 0; k < (int32)EBuildingKind::Count; ++k)
		{
			for (const FTransform& Xf : ByKind[k])
			{
				++Total;
				// The foot: the centre less half the height; the ground at the centre (as the builder sampled it).
				const FVector Up = Xf.GetLocation().GetSafeNormal();
				const double Foot = Xf.GetLocation().Size() - P.RadiusCm - Xf.GetScale3D().Z * 50.0;
				const double Ground = SlonkTerrain::Sample(P, Up, 400.0).HeightCm;
				Off += (Foot > Ground + 10.0 || Foot < Ground - 300.0) ? 1 : 0;
			}
		}
		AddInfo(FString::Printf(TEXT("%s: %d towns, %d buildings: %s; %d off their ground"), C.Name, Towns, Total, *KindList(Kinds), Off));
		TestTrue(FString::Printf(TEXT("the %s people build towns"), C.Name), Total > 20);
		for (EBuildingKind K : Kinds) { TestTrue(FString::Printf(TEXT("the %s people build no %s"), C.Name, SlonkBuildings::KindName(K)), C.Allowed.Contains(K)); }
		for (EBuildingKind K : C.Some) { TestTrue(FString::Printf(TEXT("the %s people build %s"), C.Name, SlonkBuildings::KindName(K)), Kinds.Contains(K)); }
		TestEqual(FString::Printf(TEXT("every %s building stands on its ground"), C.Name), Off, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkCitiesCliffTownsTest, "Slonk.Cities.CliffTowns", SLONK_TEST_FLAGS)
bool FSlonkCitiesCliffTownsTest::RunTest(const FString& Parameters)
{
	// A desert's cliff dwellers build into the walls of its scarps and canyons: at the foot of each rise, each house
	// standing on the ground at its front (not hanging over a drop) or on the house below it (the early ages' climb two
	// and three storeys), and the ground floors' backs set into the rise (the rock behind higher than the foot). A grown
	// people's pods hold to the rock, on the ledges and the faces.
	const FPlanetTerrainParams P = Deserty();
	for (const bool bGrown : { false, true })
	{
		const FCivilization Civ = bGrown ? SlonkCivs::Make(ETechAge::Starfaring, 1.f, 1.f, true) : SlonkCivs::Make(ETechAge::Iron, 0.f, 1.f, true);
		const TCHAR* Who = bGrown ? TEXT("grown") : TEXT("iron-age");
		FTownSite Town;
		if (!FindTown(P, Civ, true, Town)) { AddError(FString::Printf(TEXT("the %s people built no town into a cliff"), Who)); continue; }
		TArray<FTransform> ByKind[(int32)EBuildingKind::Count];
		SlonkCities::BuildTown(P, Civ, Town, ByKind, 2500);
		// Every building's centre, foot and top, for the storeys to find the ones under them.
		struct FB { FVector C; double Foot, Top, Half; };
		TArray<FB> All;
		for (int32 k = 0; k < (int32)EBuildingKind::Count; ++k)
		{
			for (const FTransform& Xf : ByKind[k])
			{
				const double Hz = Xf.GetScale3D().Z * 50.0;
				const double Mid = Xf.GetLocation().Size() - P.RadiusCm;
				All.Add({ Xf.GetLocation(), Mid - Hz, Mid + Hz, FMath::Max(Xf.GetScale3D().X, Xf.GetScale3D().Y) * 50.0 });
			}
		}
		int32 Total = 0, OnLedge = 0, OnStorey = 0, Ground = 0, IntoRock = 0;
		for (int32 k = 0; k < (int32)EBuildingKind::Count; ++k)
		{
			for (const FTransform& Xf : ByKind[k])
			{
				++Total;
				const FVector C = Xf.GetLocation();
				const double Foot = C.Size() - P.RadiusCm - Xf.GetScale3D().Z * 50.0;
				// The mesh's +Y is its back, into the rock.
				const FVector Back = Xf.GetRotation().GetAxisY();
				const double Depth = Xf.GetScale3D().Y * 100.0;
				const double Front = SlonkTerrain::Sample(P, (C - Back * (Depth * 0.45)).GetSafeNormal(), 400.0).HeightCm;
				const double Rear = SlonkTerrain::Sample(P, (C + Back * (Depth * 0.6)).GetSafeNormal(), 400.0).HeightCm;
				if (Foot <= Front + 150.0 && Foot >= Front - 800.0)
				{
					++OnLedge; ++Ground;
					IntoRock += Rear > Foot + 300.0 ? 1 : 0;
					continue;
				}
				// Or on a storey: another building's top within a metre or two of this one's foot, under it.
				for (const FB& B : All)
				{
					if (FMath::Abs(B.Top - Foot) < 200.0 && FVector::Dist(B.C.GetSafeNormal() * P.RadiusCm, C.GetSafeNormal() * P.RadiusCm) < B.Half) { ++OnStorey; break; }
				}
			}
		}
		AddInfo(FString::Printf(TEXT("%s: a cliff town of %d buildings (%s): %d on the ground at their front, %d on a storey under them; %d of the ground floors with the back in the rock"),
			Who, Total, *KindList(KindsOf(ByKind)), OnLedge, OnStorey, IntoRock));
		TestTrue(FString::Printf(TEXT("the %s cliff town has buildings"), Who), Total >= 10);
		if (!bGrown)
		{
			TestTrue(TEXT("each stands on the ground at its front or on the storey below"), (OnLedge + OnStorey) * 10 >= Total * 9);
			TestTrue(TEXT("and most ground floors are set into the rock behind"), IntoRock * 10 >= Ground * 6);
			TestTrue(TEXT("their houses are adobe"), ByKind[(int32)EBuildingKind::Pueblo].Num() > 0);
		}
		else
		{
			TestTrue(TEXT("a grown people's cliff town is pods"), ByKind[(int32)EBuildingKind::Pod].Num() == Total);
			TestTrue(TEXT("held to the rock"), IntoRock * 10 >= Total * 5);
		}
	}
	return true;
}

#endif
