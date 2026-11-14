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

	/** SlonkCities' roll of a point against a people's liking for cliffs: under it, a point they would sooner have
	 *  built into a cliff, where no ways of its own are laid. */
	float CliffRoll(uint32 Seed)
	{
		uint32 H = Seed ^ (0xC1u + 0x9e3779b9u + (Seed << 6) + (Seed >> 2));
		H = (H ^ (H >> 13)) * 0x5bd1e995u; H ^= H >> 15;
		return (H & 0xffffu) / 65535.f;
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

	/**
	 * The buildings of some towns as the tests see them, from their transforms alone: each one's centre, axes (its
	 * +Y the back), half-sizes, and foot and top (cm above the sea-level sphere), and which town it is of; hashed by
	 * 100 m cells to find the ones near each other.
	 */
	struct FBuilt
	{
		struct FB { EBuildingKind Kind; FVector C, X, Y, Up; double Hw, Hd, Foot, Top; int32 Town; };
		TArray<FB> All;
		TMap<FIntVector, TArray<int32>> Cells;
		double R = 0.0;
		explicit FBuilt(double InR) : R(InR) {}
		void Add(const TArray<FTransform>* ByKind, int32 Town)
		{
			for (int32 k = 0; k < (int32)EBuildingKind::Count; ++k)
			{
				for (const FTransform& Xf : ByKind[k])
				{
					const FVector C = Xf.GetLocation(), S = Xf.GetScale3D();
					const double Mid = C.Size() - R;
					const int32 Id = All.Add({ (EBuildingKind)k, C, Xf.GetRotation().GetAxisX(), Xf.GetRotation().GetAxisY(), C.GetSafeNormal(), S.X * 50.0, S.Y * 50.0, Mid - S.Z * 50.0, Mid + S.Z * 50.0, Town });
					Cells.FindOrAdd(Cell(C)).Add(Id);
				}
			}
		}
		static FIntVector Cell(const FVector& C) { return FIntVector(FMath::FloorToInt(C.X / 10000.0), FMath::FloorToInt(C.Y / 10000.0), FMath::FloorToInt(C.Z / 10000.0)); }
		template <typename F> void ForNear(int32 i, F&& Fn) const
		{
			const FIntVector C0 = Cell(All[i].C);
			for (int32 Dz = -1; Dz <= 1; ++Dz) for (int32 Dy = -1; Dy <= 1; ++Dy) for (int32 Dx = -1; Dx <= 1; ++Dx)
			{
				if (const TArray<int32>* L = Cells.Find(C0 + FIntVector(Dx, Dy, Dz))) { for (const int32 j : *L) { if (j != i) { Fn(j); } } }
			}
		}
		/** A point of the plane at A, in A's footprint axes. */
		static FVector2D InPlane(const FB& A, const FVector& V) { const FVector Vp = V - A.Up * FVector::DotProduct(V, A.Up); return FVector2D(FVector::DotProduct(Vp, A.X), FVector::DotProduct(Vp, A.Y)); }
		/** Two footprints overlap by more than Slack cm (separating axes, in the plane at A). */
		static bool Overlap(const FB& A, const FB& B, double Slack)
		{
			const FVector2D D = InPlane(A, B.C - A.C), Bx = InPlane(A, B.X).GetSafeNormal(), By = InPlane(A, B.Y).GetSafeNormal();
			for (const FVector2D& L : { FVector2D(1.0, 0.0), FVector2D(0.0, 1.0), Bx, By })
			{
				const double Ra = A.Hw * FMath::Abs(L.X) + A.Hd * FMath::Abs(L.Y);
				const double Rb = B.Hw * FMath::Abs(FVector2D::DotProduct(Bx, L)) + B.Hd * FMath::Abs(FVector2D::DotProduct(By, L));
				if (FMath::Abs(FVector2D::DotProduct(D, L)) >= Ra + Rb - Slack) { return false; }
			}
			return true;
		}
		/** A standing on B: its foot at B's top and its centre over B's footprint (a house on its footing, a storey on the
		 *  one below). */
		static bool StandsOn(const FB& A, const FB& B)
		{
			if (FMath::Abs(B.Top - A.Foot) > 150.0) { return false; }
			const FVector2D D = InPlane(B, A.C - B.C);
			return FMath::Abs(D.X) <= B.Hw && FMath::Abs(D.Y) <= B.Hd;
		}
		bool OnAnother(int32 i) const
		{
			bool bOn = false;
			ForNear(i, [&](int32 j) { bOn = bOn || StandsOn(All[i], All[j]); });
			return bOn;
		}
		/** A base corner's direction: Sx, Sy -1 or 1 (Sy 1 the back). */
		FVector Corner(int32 i, double Sx, double Sy) const { const FB& B = All[i]; return (B.C + B.X * (Sx * B.Hw) + B.Y * (Sy * B.Hd)).GetSafeNormal(); }
		/** Pairs whose footprints overlap by more than half a metre at heights that overlap too (a footing and the house on
		 *  it, a storey on the one below, aside): within a town and across towns, or across towns only. */
		int32 Overlaps(bool bAcrossOnly) const
		{
			int32 N = 0;
			for (int32 i = 0; i < All.Num(); ++i)
			{
				ForNear(i, [&](int32 j)
				{
					const FB& A = All[i]; const FB& B = All[j];
					if (j < i || (bAcrossOnly && A.Town == B.Town)) { return; }
					if (A.Top < B.Foot + 150.0 || B.Top < A.Foot + 150.0) { return; }
					N += Overlap(A, B, 50.0) ? 1 : 0;
				});
			}
			return N;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkCitiesPeoplesTest, "Slonk.Cities.Peoples", SLONK_TEST_FLAGS)
bool FSlonkCitiesPeoplesTest::RunTest(const FString& Parameters)
{
	// Who lives where. An Earth-like world the style lit keeps an electric people (industrial or later, its ground
	// aglow); one it left dark is home to a people before electricity about half the time, its ground lit only dimly
	// by their fires; a desert about two in five, mostly early and mostly cliff dwellers; the dead worlds nobody. Only
	// a modern or later people grows its buildings, and a grown or early people lays no street grid; the Federation's
	// colonists build on the open land of their grid, never into a cliff. The same seed is the same people.
	int32 LitBad = 0, DarkPeople = 0, DarkBad = 0, Desert = 0, DesertCliff = 0, Dead = 0, OrganicBad = 0, GridBad = 0;
	const int32 N = 2000;
	for (int32 i = 0; i < N; ++i)
	{
		const uint32 H = (uint32)i * 2654435761u;
		const FCivilization Lit = SlonkCivs::Natives(EPlanetArchetype::Terrestrial, H, true);
		LitBad += !Lit.bInhabited || Lit.Age < ETechAge::Industrial || Lit.GroundLights <= 0.f ? 1 : 0;
		const FCivilization Dark = SlonkCivs::Natives(EPlanetArchetype::Terrestrial, H, false);
		if (Dark.bInhabited) { ++DarkPeople; DarkBad += Dark.Age > ETechAge::Iron || Dark.GroundLights <= 0.f || Dark.GroundLights >= 2.f || Dark.StreetGrid > 0.f ? 1 : 0; }
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
	TestEqual(TEXT("and none of them electric: lit by their fires, dimly, with no paved streets"), DarkBad, 0);
	TestTrue(TEXT("about two deserts in five have a people"), Desert > N * 0.33 && Desert < N * 0.47);
	TestTrue(TEXT("most of them cliff dwellers"), DesertCliff * 3 > Desert * 2);
	TestEqual(TEXT("the dead worlds have nobody"), Dead, 0);
	TestEqual(TEXT("only a modern or later people grows its buildings"), OrganicBad, 0);
	TestEqual(TEXT("no grid under a grown or an early people's towns"), GridBad, 0);
	TestEqual(TEXT("the colonists build into no cliff"), SlonkCivs::Colonists().CliffAffinity, 0.f);
	const FCivilization A = SlonkCivs::Natives(EPlanetArchetype::Desert, 12345u, false), B = SlonkCivs::Natives(EPlanetArchetype::Desert, 12345u, false);
	TestTrue(TEXT("the same seed is the same people"), A.bInhabited == B.bInhabited && A.Age == B.Age && A.Organic == B.Organic && A.CliffAffinity == B.CliffAffinity);
	TestFalse(TEXT("a people describes itself"), SlonkCivs::Make(ETechAge::Iron, 0.f, 0.9f, true).Describe().IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkCitiesShapesTest, "Slonk.Cities.Shapes", SLONK_TEST_FLAGS)
bool FSlonkCitiesShapesTest::RunTest(const FString& Parameters)
{
	// Every building shape builds, and fills the unit box the town builder scales (a metre each way, its base at
	// the bottom), so a building's size is its instance scale and it stands on its lot. Each is a closed solid with
	// every face wound outward and shaded as it faces (a positive volume; each face's normal agreeing with its
	// corners'), and has collision of its own shape (a fire none).
	for (int32 i = 0; i < (int32)EBuildingKind::Count; ++i)
	{
		const EBuildingKind K = (EBuildingKind)i;
		const TCHAR* Name = SlonkBuildings::KindName(K);
		UStaticMesh* M = SlonkBuildings::GetMesh(K);
		if (!TestNotNull(FString::Printf(TEXT("%s builds"), Name), M)) { continue; }
		const FBox B = M->GetBoundingBox();
		const FVector Ext = B.GetSize();
		double Volume = 0.0, Facing = 0.0;
		int32 Parts = 0;
		SlonkBuildings::ShapeStats(K, Volume, Facing, Parts);
		AddInfo(FString::Printf(TEXT("%s: %.0f x %.0f x %.0f cm, base at %.0f; %.2f m3, worst facing %.2f, %d collision parts"), Name, Ext.X, Ext.Y, Ext.Z, B.Min.Z, Volume, Facing, Parts));
		TestTrue(FString::Printf(TEXT("%s stays in the unit box"), Name), B.Min.X >= -50.5 && B.Max.X <= 50.5 && B.Min.Y >= -50.5 && B.Max.Y <= 50.5 && B.Min.Z >= -50.5 && B.Max.Z <= 50.5);
		TestTrue(FString::Printf(TEXT("%s stands on the box's floor and reaches most of its height"), Name), FMath::IsNearlyEqual(B.Min.Z, -50.0, 1.0) && B.Max.Z > 40.0);
		TestTrue(FString::Printf(TEXT("%s is wound outward (a positive volume, under the box's)"), Name), Volume > 0.01 && Volume <= 1.2);
		TestTrue(FString::Printf(TEXT("%s shades each face as it faces"), Name), Facing > 0.0);
		TestTrue(FString::Printf(TEXT("%s has collision of its own shape"), Name), K == EBuildingKind::Fire ? Parts == 0 : Parts > 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkCitiesTownsByAgeTest, "Slonk.Cities.TownsByAge", SLONK_TEST_FLAGS)
bool FSlonkCitiesTownsByAgeTest::RunTest(const FString& Parameters)
{
	// Each people builds its own: huts round their fires for the stone age; houses, a hut or two, fires and a round
	// keep for the iron age; brick, halls and works for the industrial; blocks, towers and houses for the modern;
	// spires and domes for the starfaring; pods, domes and bulb towers for a grown people. A built people's buildings
	// on a slope stand on footings. Every building stands on its ground: no corner of it hangs over the ground below
	// (sampled at each corner, not where the builder looked), unless it stands on its footing; and no two of them
	// stand in each other, in a town or across towns.
	const FPlanetTerrainParams P = Earthish();
	struct FCase { const TCHAR* Name; FCivilization Civ; TArray<EBuildingKind> Allowed; TArray<EBuildingKind> Some; };
	const TArray<FCase> Cases = {
		{ TEXT("stone"), SlonkCivs::Make(ETechAge::Stone, 0.f, 0.f, false), { EBuildingKind::Hut, EBuildingKind::Fire }, { EBuildingKind::Hut, EBuildingKind::Fire } },
		{ TEXT("iron"), SlonkCivs::Make(ETechAge::Iron, 0.f, 0.f, false), { EBuildingKind::House, EBuildingKind::Hut, EBuildingKind::RoundTower, EBuildingKind::Fire, EBuildingKind::Footing }, { EBuildingKind::House, EBuildingKind::Fire } },
		{ TEXT("industrial"), SlonkCivs::Make(ETechAge::Industrial, 0.f, 0.f, false), { EBuildingKind::Block, EBuildingKind::House, EBuildingKind::Works, EBuildingKind::Footing }, { EBuildingKind::Works } },
		{ TEXT("modern"), SlonkCivs::Make(ETechAge::Modern, 0.f, 0.f, false), { EBuildingKind::Block, EBuildingKind::Tower, EBuildingKind::House, EBuildingKind::Footing }, { EBuildingKind::Block } },
		{ TEXT("starfaring"), SlonkCivs::Make(ETechAge::Starfaring, 0.f, 0.f, false), { EBuildingKind::Block, EBuildingKind::Tower, EBuildingKind::Spire, EBuildingKind::Dome, EBuildingKind::Footing }, { EBuildingKind::Dome } },
		{ TEXT("grown"), SlonkCivs::Make(ETechAge::Starfaring, 1.f, 0.f, false), { EBuildingKind::Pod, EBuildingKind::Dome, EBuildingKind::BulbTower }, { EBuildingKind::Pod, EBuildingKind::BulbTower } },
	};
	for (const FCase& C : Cases)
	{
		// All the towns round a few places, so the rarer kinds turn up and neighbours meet.
		TArray<FTransform> ByKind[(int32)EBuildingKind::Count];
		FBuilt Built(P.RadiusCm);
		TSet<uint32> Seen;
		int32 Towns = 0;
		for (int32 i = 0; i < 120 && Towns < 10; ++i)
		{
			const FVector Dir = Fib(i * 11 + 5, 1320);
			if (SlonkTerrain::SampleStructure(P, Dir, 2.0e5).bWater) { continue; }
			TArray<FTownSite> Sites;
			SlonkCities::EnumerateTowns(P, C.Civ, Dir * P.RadiusCm, 6.0e5, Sites, 8);
			for (const FTownSite& T : Sites)
			{
				if (T.bCliff || Seen.Contains(T.Seed) || Towns >= 10) { continue; }
				Seen.Add(T.Seed);
				TArray<FTransform> One[(int32)EBuildingKind::Count];
				SlonkCities::BuildTown(P, C.Civ, T, One, 2500);
				for (int32 k = 0; k < (int32)EBuildingKind::Count; ++k) { ByKind[k].Append(One[k]); }
				Built.Add(One, Towns);
				++Towns;
			}
		}
		const TSet<EBuildingKind> Kinds = KindsOf(ByKind);
		int32 Corners = 0, Hang = 0, OnFooting = 0;
		for (int32 i = 0; i < Built.All.Num(); ++i)
		{
			if (Built.OnAnother(i)) { ++OnFooting; continue; }
			for (const double Sx : { -1.0, 1.0 }) for (const double Sy : { -1.0, 1.0 })
			{
				++Corners;
				Hang += Built.All[i].Foot > SlonkTerrain::Sample(P, Built.Corner(i, Sx, Sy), 400.0).HeightCm + 150.0 ? 1 : 0;
			}
		}
		const int32 Overlaps = Built.Overlaps(false);
		AddInfo(FString::Printf(TEXT("%s: %d towns, %d buildings: %s; %d on footings; %d of %d corners hang over the ground; %d pairs stand in each other"),
			C.Name, Towns, Built.All.Num(), *KindList(Kinds), OnFooting, Hang, Corners, Overlaps));
		TestTrue(FString::Printf(TEXT("the %s people build towns"), C.Name), Built.All.Num() > 20);
		for (EBuildingKind K : Kinds) { TestTrue(FString::Printf(TEXT("the %s people build no %s"), C.Name, SlonkBuildings::KindName(K)), C.Allowed.Contains(K)); }
		for (EBuildingKind K : C.Some) { TestTrue(FString::Printf(TEXT("the %s people build %s"), C.Name, SlonkBuildings::KindName(K)), Kinds.Contains(K)); }
		TestTrue(FString::Printf(TEXT("every %s building stands on its ground (no more than one corner in a hundred hangs 1.5 m over it)"), C.Name), Hang * 100 <= Corners);
		TestEqual(FString::Printf(TEXT("no two %s buildings stand in each other"), C.Name), Overlaps, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkCitiesWaysTest, "Slonk.Cities.Ways", SLONK_TEST_FLAGS)
bool FSlonkCitiesWaysTest::RunTest(const FString& Parameters)
{
	// Each people's ways, as the terrain paints them (terrain_material.py mirrors SlonkCities' PaintedWayCm): a hamlet's
	// paths, a village's lanes, a grown town's ring ways and spiral spokes, and the roads between their settlements,
	// straight for a grid-building people. No part of any building stands on one: every footprint is sampled all over
	// (every metre, or every metre and a half for a grid people whose only ways are roads nine metres wide), whatever the builder
	// checked and wherever the material gives the ground to another settlement. The ways are there: in most towns a
	// share of the ground, and roads leaving most of them. And a village house near its lane fronts onto it: its front
	// (the mesh's -Y) faces down the slope of the way's clearance there, toward the lane, measured from the painted
	// ways alone.
	const FPlanetTerrainParams P = Earthish();
	struct FCase { const TCHAR* Name; FCivilization Civ; double MinShare; };
	const TArray<FCase> Cases = {
		{ TEXT("stone"), SlonkCivs::Make(ETechAge::Stone, 0.f, 0.f, false), 0.0015 },
		{ TEXT("iron"), SlonkCivs::Make(ETechAge::Iron, 0.f, 0.f, false), 0.004 },
		{ TEXT("modern"), SlonkCivs::Make(ETechAge::Modern, 0.f, 0.f, false), 0.0 },
		{ TEXT("grown"), SlonkCivs::Make(ETechAge::Starfaring, 1.f, 0.f, false), 0.06 },
	};
	for (const FCase& C : Cases)
	{
		const bool bGrid = C.Civ.Ways() == EWayStyle::Grid;
		auto Painted = [&](const FVector& At) { return SlonkCities::PaintedWayCm(P, C.Civ, At.GetSafeNormal()); };
		TArray<FTownSite> Built;
		int32 Buildings = 0, OnWay = 0, Near = 0, Fronting = 0, WithWays = 0, WaysTowns = 0, WithRoads = 0, RoadTowns = 0;
		TSet<uint32> Seen;
		// The early peoples' villages are small and cheap: twice as many of them.
		const int32 MaxTowns = C.Civ.Age <= ETechAge::Iron ? 16 : 8;
		for (int32 i = 0; i < 120 && Built.Num() < MaxTowns; ++i)
		{
			const FVector Dir = Fib(i * 11 + 5, 1320);
			if (SlonkTerrain::SampleStructure(P, Dir, 2.0e5).bWater) { continue; }
			TArray<FTownSite> Sites;
			SlonkCities::EnumerateTowns(P, C.Civ, Dir * P.RadiusCm, 6.0e5, Sites, 8);
			for (const FTownSite& T : Sites)
			{
				if (T.bCliff || Seen.Contains(T.Seed) || Built.Num() >= MaxTowns) { continue; }
				Seen.Add(T.Seed);
				Built.Add(T);
				TArray<FTransform> ByKind[(int32)EBuildingKind::Count];
				SlonkCities::BuildTown(P, C.Civ, T, ByKind, 2500);
				int32 TownBuildings = 0;
				for (int32 k = 0; k < (int32)EBuildingKind::Count; ++k) { TownBuildings += ByKind[k].Num(); }
				const double Step = bGrid ? 150.0 : 100.0;
				for (int32 k = 0; k < (int32)EBuildingKind::Count; ++k)
				{
					for (const FTransform& Xf : ByKind[k])
					{
						++Buildings;
						const FVector Ctr = Xf.GetLocation().GetSafeNormal() * P.RadiusCm;
						const FVector X = Xf.GetRotation().GetAxisX(), Y = Xf.GetRotation().GetAxisY();
						const double W = Xf.GetScale3D().X * 100.0, D = Xf.GetScale3D().Y * 100.0;
						const int32 Nx = FMath::Max(1, FMath::CeilToInt(W / Step)), Ny = FMath::Max(1, FMath::CeilToInt(D / Step));
						bool bOn = false;
						for (int32 a = 0; a <= Nx && !bOn; ++a)
						{
							for (int32 b = 0; b <= Ny && !bOn; ++b) { bOn = Painted(Ctr + X * ((a / (double)Nx - 0.5) * W) + Y * ((b / (double)Ny - 0.5) * D)) < 0.0; }
						}
						OnWay += bOn ? 1 : 0;
						// A village house within a few metres of a lane: the clearance's slope at its front, from the painted
						// ways (a metre each way across the ground), against its front's direction.
						if (C.Civ.Age == ETechAge::Iron && (EBuildingKind)k == EBuildingKind::House)
						{
							const FVector Front = Ctr - Y * (D * 0.5);
							if (Painted(Front) < 500.0)
							{
								++Near;
								const FVector Up = Front.GetSafeNormal();
								const FVector E = FVector::CrossProduct(FVector::ZAxisVector, Up).GetSafeNormal(), N = FVector::CrossProduct(Up, E);
								const FVector Grad = E * (Painted(Front + E * 100.0) - Painted(Front - E * 100.0)) + N * (Painted(Front + N * 100.0) - Painted(Front - N * 100.0));
								Fronting += FVector::DotProduct(-Y, -Grad.GetSafeNormal()) > 0.82 ? 1 : 0;
							}
						}
					}
				}
				// Its own ways' share of its ground (a 4 m grid over it; a hamlet's is small whatever its point's weight,
				// a village's under 400 m), and the roads leaving it (how often a ring 30% beyond its edge goes onto one).
				const FVector Up = T.Dir;
				const FVector E = FVector::CrossProduct(FVector::ZAxisVector, Up).GetSafeNormal(), N = FVector::CrossProduct(Up, E);
				const double Rad = C.Civ.Age == ETechAge::Stone ? FMath::Min(T.RadiusCm, 18000.0) : C.Civ.Age == ETechAge::Iron ? FMath::Min(T.RadiusCm, 40000.0) : T.RadiusCm;
				// (Of the towns built: a point the walk finds but whose ground at the surface is all another's builds nothing.)
				if (C.MinShare > 0.0 && TownBuildings >= 10 && CliffRoll(T.Seed) >= C.Civ.CliffAffinity)
				{
					int32 Ground = 0, Way = 0;
					for (double u = -Rad; u <= Rad; u += 400.0)
					{
						for (double v = -Rad; v <= Rad; v += 400.0)
						{
							if (u * u + v * v > Rad * Rad) { continue; }
							++Ground;
							Way += Painted(T.LocalCm + E * u + N * v) < 0.0 ? 1 : 0;
						}
					}
					++WaysTowns;
					WithWays += Way >= Ground * C.MinShare ? 1 : 0;
					if (Way < Ground * C.MinShare)
					{
						// What the material paints there instead, to say why.
						AddInfo(FString::Printf(TEXT("  %s town %u (%d buildings, radius %.0f m) has few ways: at its centre %s"), C.Name, T.Seed, TownBuildings, T.RadiusCm / 100.0, *SlonkCities::DescribePaint(P, C.Civ, T.Dir)));
					}
				}
				{
					// (A city's ground is a disc no road crosses; a road comes up to it, as to a town's edge.)
					const double Rr = T.RadiusCm * (T.bCity ? 1.15 : 1.3);
					const int32 Steps = FMath::CeilToInt(2.0 * PI * Rr / 250.0);
					int32 Crossings = 0;
					bool bWas = false;
					for (int32 s = 0; s <= Steps; ++s)
					{
						const double A = 2.0 * PI * s / Steps;
						const bool bOnRoad = Painted(T.LocalCm + (E * FMath::Cos(A) + N * FMath::Sin(A)) * Rr) < 0.0;
						Crossings += bOnRoad && !bWas && s > 0 ? 1 : 0;
						bWas = bOnRoad;
					}
					++RoadTowns;
					WithRoads += Crossings > 0 ? 1 : 0;
				}
			}
		}
		AddInfo(FString::Printf(TEXT("%s: %d towns, %d buildings, %d on a way; %d of %d towns with ways of their own; %d of %d towns with a road leaving; %d of %d houses near a lane fronting onto it"),
			C.Name, Built.Num(), Buildings, OnWay, WithWays, WaysTowns, WithRoads, RoadTowns, Fronting, Near));
		TestTrue(FString::Printf(TEXT("the %s people build towns"), C.Name), Buildings > 20);
		TestEqual(FString::Printf(TEXT("no %s building stands on a way"), C.Name), OnWay, 0);
		if (WaysTowns > 0) { TestTrue(FString::Printf(TEXT("most %s towns have ways of their own"), C.Name), WithWays * 4 >= WaysTowns * 3); }
		// A hamlet's people live at a third of the points, so its neighbours are few; the rest have roads to most.
		TestTrue(FString::Printf(TEXT("roads leave the %s towns"), C.Name), C.Civ.Age == ETechAge::Stone ? WithRoads > 0 : WithRoads * 5 >= RoadTowns * 2);
		if (Near > 0) { TestTrue(TEXT("a village's houses front onto their lanes"), Fronting * 10 >= Near * 8); }
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkCitiesCliffTownsTest, "Slonk.Cities.CliffTowns", SLONK_TEST_FLAGS)
bool FSlonkCitiesCliffTownsTest::RunTest(const FString& Parameters)
{
	// A desert's cliff dwellers build into the walls of its scarps and canyons: at the foot of each rise, each house
	// on the ground at every corner, or on the footing carried down to the ground at its front, or on the house below
	// it (the early ages climb two and three storeys); the ground floors' backs set into the rise at both rear
	// corners. A grown people's pods hold to the rock, on the ledges and the faces (those on a face cling to it, over
	// the drop below, as they are grown to). And the towns of a stretch of
	// cliffs keep to their own walls: no two build on each other.
	const FPlanetTerrainParams P = Deserty();
	for (const bool bGrown : { false, true })
	{
		const FCivilization Civ = bGrown ? SlonkCivs::Make(ETechAge::Starfaring, 1.f, 1.f, true) : SlonkCivs::Make(ETechAge::Iron, 0.f, 1.f, true);
		const TCHAR* Who = bGrown ? TEXT("grown") : TEXT("iron-age");
		FTownSite Town;
		if (!FindTown(P, Civ, true, Town)) { AddError(FString::Printf(TEXT("the %s people built no town into a cliff"), Who)); continue; }
		TArray<FTransform> ByKind[(int32)EBuildingKind::Count];
		SlonkCities::BuildTown(P, Civ, Town, ByKind, 2500);
		FBuilt Built(P.RadiusCm);
		Built.Add(ByKind, 0);
		int32 Total = 0, OnOther = 0, Corners = 0, Hang = 0, Ground = 0, IntoRock = 0;
		for (int32 i = 0; i < Built.All.Num(); ++i)
		{
			const FBuilt::FB& B = Built.All[i];
			if (B.Kind != EBuildingKind::Footing && B.Kind != EBuildingKind::Fire) { ++Total; }
			const bool bOn = Built.OnAnother(i);
			// A ground floor: on the ground or on its footing (not a storey on a house, nor the footing itself).
			bool bOnFooting = false;
			Built.ForNear(i, [&](int32 j) { bOnFooting = bOnFooting || (Built.All[j].Kind == EBuildingKind::Footing && FBuilt::StandsOn(B, Built.All[j])); });
			if (B.Kind != EBuildingKind::Footing && B.Kind != EBuildingKind::Fire && (!bOn || bOnFooting))
			{
				++Ground;
				const bool bLeft = SlonkTerrain::Sample(P, Built.Corner(i, -1.0, 1.0), 400.0).HeightCm > B.Foot + 150.0;
				const bool bRight = SlonkTerrain::Sample(P, Built.Corner(i, 1.0, 1.0), 400.0).HeightCm > B.Foot + 150.0;
				IntoRock += bLeft && bRight ? 1 : 0;
			}
			if (bOn) { ++OnOther; continue; }
			for (const double Sx : { -1.0, 1.0 }) for (const double Sy : { -1.0, 1.0 })
			{
				++Corners;
				Hang += B.Foot > SlonkTerrain::Sample(P, Built.Corner(i, Sx, Sy), 400.0).HeightCm + 150.0 ? 1 : 0;
			}
		}
		AddInfo(FString::Printf(TEXT("%s: a cliff town of %d buildings (%s): %d on a footing or a storey; %d of %d corners on the ground hang over it; %d of %d ground floors with both rear corners in the rock"),
			Who, Total, *KindList(KindsOf(ByKind)), OnOther, Hang, Corners, IntoRock, Ground));
		TestTrue(FString::Printf(TEXT("the %s cliff town has buildings"), Who), Total >= 10);
		if (!bGrown)
		{
			TestTrue(TEXT("no corner hangs over the drop (no more than one in fifty, 1.5 m)"), Hang * 50 <= Corners);
			TestTrue(TEXT("the ground floors are set into the rock behind, at both corners"), IntoRock * 10 >= Ground * 9);
			TestTrue(TEXT("their houses are adobe"), ByKind[(int32)EBuildingKind::Pueblo].Num() > 0);
		}
		else
		{
			TestTrue(TEXT("a grown people's cliff town is pods"), ByKind[(int32)EBuildingKind::Pod].Num() == Total);
			TestTrue(TEXT("held to the rock"), IntoRock * 10 >= Ground * 5);
		}

		// Every town round it, built: none in another's (two towns took the same wall before each kept to its cell).
		TArray<FTownSite> Near;
		SlonkCities::EnumerateTowns(P, Civ, Town.LocalCm, 1.5e6, Near, 60);
		FBuilt Area(P.RadiusCm);
		int32 CliffTowns = 0;
		for (int32 t = 0; t < Near.Num(); ++t)
		{
			TArray<FTransform> One[(int32)EBuildingKind::Count];
			SlonkCities::BuildTown(P, Civ, Near[t], One, 2500);
			Area.Add(One, t);
			CliffTowns += Near[t].bCliff ? 1 : 0;
		}
		const int32 Across = Area.Overlaps(true);
		AddInfo(FString::Printf(TEXT("%s: %d towns within 15 km (%d in the cliffs), %d buildings; %d pairs of two towns' buildings in each other"), Who, Near.Num(), CliffTowns, Area.All.Num(), Across));
		TestTrue(FString::Printf(TEXT("the %s people build several towns into the cliffs there"), Who), CliffTowns >= 2);
		TestEqual(FString::Printf(TEXT("no %s town builds in another"), Who), Across, 0);
	}
	return true;
}

#endif
