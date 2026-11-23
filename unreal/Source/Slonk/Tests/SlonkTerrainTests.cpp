// Slonk - automation tests for the rocks and plants on the ground (TerrainScatter.h), built the way a chunk build
// builds them, without a world. Under "Slonk.Terrain."; tools/run_tests.sh runs them headless.

#include "Misc/AutomationTest.h"
#include "../PlanetTerrain.h"
#include "../PlanetTerrainComponent.h"
#include "../PlanetCities.h"
#include "../TerrainScatter.h"
#include "../PlanetInstancesComponent.h"
#include "../PlantMeshes.h"
#include "../PlanetBody.h"
#include "../WorldGenome.h"
#include "../ScienceScannerComponent.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Async/ParallelFor.h"

#if WITH_DEV_AUTOMATION_TESTS

#ifndef SLONK_TEST_FLAGS
#define SLONK_TEST_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
#endif

namespace
{
	/** An Earth-sized world with ranges, canyons and strata. */
	FPlanetTerrainParams RockWorld()
	{
		FPlanetTerrainParams P;
		P.Seed = FVector(3.1, 7.7, 1.3);
		P.Canyons = 0.6f;
		P.Strata = 0.6f;
		P.Mountains = 0.7f;
		// Its 38 km of relief would put most of the land under an Earth's 5 km snowline; at half the relief the snow
		// keeps to the heights, as the rocks these tests look for were placed under.
		P.SnowlineCm = 0.5 * P.ReliefCm;
		return P;
	}

	/** Directions spread evenly over the globe. */
	FVector Fibonacci(int32 i, int32 N)
	{
		const double Z = 1.0 - 2.0 * (i + 0.5) / N;
		const double Rr = FMath::Sqrt(FMath::Max(1.0 - Z * Z, 0.0));
		const double A = i * UE_DOUBLE_PI * (3.0 - FMath::Sqrt(5.0));
		return FVector(Rr * FMath::Cos(A), Rr * FMath::Sin(A), Z);
	}

	/** Dry rocky slopes, best first: where the talus, and the boulders on it, lie. */
	TArray<FVector> RockySpots(const FPlanetTerrainParams& P, int32 Count)
	{
		struct FSpot { FVector Dir; double Score; };
		TArray<FSpot> Spots;
		const int32 N = 3000;
		for (int32 i = 0; i < N; ++i)
		{
			const FVector Dir = Fibonacci(i, N);
			const FPlanetTerrainSample S = SlonkTerrain::Sample(P, Dir, 200.0);
			if (S.bWater || S.Ice > 0.1f || S.Slope < 0.22f || S.Slope > 0.6f) { continue; }
			Spots.Add({ Dir, (double)S.Rock + (1.0 - S.Wet) * 0.3 });
		}
		Spots.Sort([](const FSpot& A, const FSpot& B) { return A.Score > B.Score; });
		TArray<FVector> Out;
		for (int32 i = 0; i < Spots.Num() && Out.Num() < Count; ++i) { Out.Add(Spots[i].Dir); }
		return Out;
	}

	/** The coarsest depth whose chunks carry a class that rides on chunks up to EdgeCm across. */
	int32 DepthFor(const FPlanetTerrainParams& P, double EdgeCm)
	{
		return FMath::CeilToInt32(FMath::Log2(P.RadiusCm * UE_DOUBLE_HALF_PI / EdgeCm));
	}
	/** The coarsest depth whose chunks carry boulders (their class rides on chunks to 260 m). */
	int32 BoulderDepth(const FPlanetTerrainParams& P) { return DepthFor(P, 26000.0); }

	struct FRocks
	{
		TArray<FTransform> Xf[SlonkScatter::NumMeshes];
		TArray<double> Drawn[SlonkScatter::NumMeshes];
		FVector Origin = FVector::ZeroVector;
		int32 Num() const { int32 N = 0; for (const TArray<FTransform>& T : Xf) { N += T.Num(); } return N; }
		int32 NumPlants() const { int32 N = 0; for (int32 M = SlonkScatter::FirstPlant; M < SlonkScatter::FirstLandmark; ++M) { N += Xf[M].Num(); } return N; }
	};
	/** A chunk's scatter at the terrain's default grid (64 steps a side), or at another the component allows. */
	FRocks Build(const FPlanetTerrainParams& P, const FScatterSettings& S, int32 Face, int32 Depth, int64 X, int64 Y, int32 Steps = 64)
	{
		FRocks R;
		UPlanetTerrainComponent::BuildChunkScatterForTest(P, S, Face, Depth, X, Y, Steps, R.Xf, R.Drawn, &R.Origin);
		return R;
	}

	/** Living settings: grass, bushes and forest as on an Earth-like world. */
	FScatterSettings Living()
	{
		FScatterSettings S;
		S.Life = 1.f;
		return S;
	}

	/** Moist lowlands, level enough for the meadows and the woods, wettest first. */
	TArray<FVector> GreenSpots(const FPlanetTerrainParams& P, int32 Count)
	{
		struct FSpot { FVector Dir; double Score; };
		TArray<FSpot> Spots;
		const int32 N = 3000;
		for (int32 i = 0; i < N; ++i)
		{
			const FVector Dir = Fibonacci(i, N);
			const FPlanetTerrainSample S = SlonkTerrain::Sample(P, Dir, 200.0);
			if (S.bWater || S.Ice > 0.05f || S.Slope > 0.2f || S.HeightCm > 2.0e5 || S.Rock > 0.3f || FMath::Abs(Dir.Z) > 0.6) { continue; }
			Spots.Add({ Dir, (double)S.Wet });
		}
		Spots.Sort([](const FSpot& A, const FSpot& B) { return A.Score > B.Score; });
		TArray<FVector> Out;
		for (int32 i = 0; i < Spots.Num() && Out.Num() < Count; ++i) { Out.Add(Spots[i].Dir); }
		return Out;
	}
	constexpr int32 TreeMeshes[2] = { SlonkScatter::PlantMesh(EPlant::Broadleaf), SlonkScatter::PlantMesh(EPlant::Conifer) };
	/** Boulders only: wider than any stone. */
	int32 CountBoulders(const FRocks& R)
	{
		int32 N = 0;
		for (const TArray<FTransform>& T : R.Xf) { for (const FTransform& X : T) { N += X.GetScale3D().X >= 1.19 ? 1 : 0; } }
		return N;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkTerrainRocksNestTest, "Slonk.Terrain.RocksNest", SLONK_TEST_FLAGS)
bool FSlonkTerrainRocksNestTest::RunTest(const FString& Parameters)
{
	// A chunk and its four children find the same boulders, of the same kind and size, in the same place: the rock
	// stays put when the quadtree refines under it, only settling onto the finer mesh.
	const FPlanetTerrainParams P = RockWorld();
	const FScatterSettings S;
	const int32 D = BoulderDepth(P);
	int32 Tried = 0;
	for (const FVector& Dir : RockySpots(P, 8))
	{
		int32 Face; int64 X, Y;
		if (!UPlanetTerrainComponent::ChunkAt(Dir, D, Face, X, Y)) { continue; }
		const FRocks Parent = Build(P, S, Face, D, X, Y);
		if (CountBoulders(Parent) < 10) { continue; }
		++Tried;
		FRocks Kids[4];
		for (int32 c = 0; c < 4; ++c) { Kids[c] = Build(P, S, Face, D + 1, X * 2 + (c & 1), Y * 2 + (c >> 1)); }
		int32 Boulders = 0, Matched = 0;
		for (int32 K = 0; K < SlonkScatter::NumMeshes; ++K)
		{
			for (const FTransform& A : Parent.Xf[K])
			{
				if (A.GetScale3D().X < 1.19) { continue; }
				++Boulders;
				const FVector Ua = A.GetLocation().GetSafeNormal();
				bool bFound = false;
				for (const FRocks& Kid : Kids)
				{
					for (const FTransform& B : Kid.Xf[K])
					{
						// Across the ground, within half its width; the same size to a tenth (the rock field it is
						// sized by is read off the finer grid).
						const double Apart = FMath::Acos(FMath::Clamp(FVector::DotProduct(Ua, B.GetLocation().GetSafeNormal()), -1.0, 1.0)) * P.RadiusCm;
						if (Apart < A.GetScale3D().X * 50.0 && FMath::Abs(B.GetScale3D().X / A.GetScale3D().X - 1.0) < 0.1) { bFound = true; break; }
					}
					if (bFound) { break; }
				}
				Matched += bFound ? 1 : 0;
			}
		}
		const double Fraction = (double)Matched / FMath::Max(Boulders, 1);
		AddInfo(FString::Printf(TEXT("depth %d chunk f%d (%lld, %lld): %d boulders, %d found again on the children (%.0f%%)"), D, Face, X, Y, Boulders, Matched, Fraction * 100.0));
		TestTrue(TEXT("nearly every boulder is found again on the finer chunks"), Fraction >= 0.85);
		if (Tried >= 2) { break; }
	}
	TestTrue(TEXT("found a rocky chunk with boulders to compare"), Tried > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkTerrainRocksGroundedTest, "Slonk.Terrain.RocksGrounded", SLONK_TEST_FLAGS)
bool FSlonkTerrainRocksGroundedTest::RunTest(const FString& Parameters)
{
	// Every rock sits on the drawn ground, a little buried, never over water; the finest chunks carry pebbles; and
	// the same chunk always carries the same rocks.
	const FPlanetTerrainParams P = RockWorld();
	const FScatterSettings S;
	const TArray<FVector> Spots = RockySpots(P, 3);
	if (!TestTrue(TEXT("found rocky ground"), Spots.Num() > 0)) { return true; }
	const int32 Leaf = FMath::CeilToInt32(FMath::Log2(P.RadiusCm * UE_DOUBLE_HALF_PI / 1700.0));
	int32 Checked = 0, Floating = 0, Sunk = 0, Wet = 0, Pebbles = 0;
	for (const FVector& Dir : Spots)
	{
		for (const int32 D : { BoulderDepth(P) + 1, Leaf })
		{
			int32 Face; int64 X, Y;
			if (!UPlanetTerrainComponent::ChunkAt(Dir, D, Face, X, Y)) { continue; }
			const FRocks R = Build(P, S, Face, D, X, Y);
			const FRocks Again = Build(P, S, Face, D, X, Y);
			for (int32 K = 0; K < SlonkScatter::NumMeshes; ++K)
			{
				TestEqual(TEXT("the same chunk carries the same rocks"), Again.Xf[K].Num(), R.Xf[K].Num());
				for (int32 i = 0; i < R.Xf[K].Num() && i < Again.Xf[K].Num(); ++i)
				{
					if (!R.Xf[K][i].Equals(Again.Xf[K][i], 0.0)) { AddError(TEXT("a rock moved between two builds of one chunk")); break; }
				}
				for (int32 i = 0; i < R.Xf[K].Num(); ++i)
				{
					const FTransform& T = R.Xf[K][i];
					const FVector Sc = T.GetScale3D() * SlonkGroundRocks::UnitCm;
					const double Above = T.GetLocation().Size() - P.RadiusCm - R.Drawn[K][i];
					// The centre stands between a tenth of its height below the drawn surface and half above it, with
					// half a metre's grace for the slope under a tipped rock.
					if (Above > Sc.Z * 0.5 + 50.0) { ++Floating; }
					if (Above < -Sc.Z * 0.6 - 50.0) { ++Sunk; }
					if (SlonkTerrain::Sample(P, T.GetLocation().GetSafeNormal(), 400.0).bWater) { ++Wet; }
					if (Sc.X < 25.0) { ++Pebbles; }
					++Checked;
				}
			}
		}
	}
	AddInfo(FString::Printf(TEXT("%d rocks: %d floating, %d sunk, %d in water, %d pebbles"), Checked, Floating, Sunk, Wet, Pebbles));
	TestTrue(TEXT("rocks were placed"), Checked > 50);
	TestEqual(TEXT("no rock floats over the drawn ground"), Floating, 0);
	TestEqual(TEXT("no rock is buried out of sight"), Sunk, 0);
	TestTrue(TEXT("hardly any rock over water (the field at 4 m against the chunk's grid)"), Wet <= Checked / 100);
	TestTrue(TEXT("the finest chunks carry pebbles"), Pebbles > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkTerrainRocksTownsTest, "Slonk.Terrain.RocksTowns", SLONK_TEST_FLAGS)
bool FSlonkTerrainRocksTownsTest::RunTest(const FString& Parameters)
{
	// On a world with settlements the rocks keep off the streets and lots: none inside a town's radius.
	const FPlanetTerrainParams P = RockWorld();
	FScatterSettings S;
	S.bCities = true;
	S.Density = 4.f;		// plenty of candidates, so an empty town means the rule held
	FTownSite Town;
	bool bFound = false;
	for (int32 i = 0; i < 400 && !bFound; ++i)
	{
		const FVector Dir = Fibonacci(i, 400);
		if (SlonkTerrain::SampleStructure(P, Dir).bWater) { continue; }
		TArray<FTownSite> Towns;
		SlonkCities::EnumerateTowns(P, SlonkCivs::Colonists(), Dir * P.RadiusCm, 3.0e7, Towns, 8);
		for (const FTownSite& T : Towns) { if (!T.bCity) { Town = T; bFound = true; break; } }
	}
	if (!bFound) { AddWarning(TEXT("no town on the test world; the keep-out rule went unchecked")); return true; }
	auto InTown = [&](const FRocks& R)
	{
		int32 Inside = 0;
		for (const TArray<FTransform>& T : R.Xf)
		{
			for (const FTransform& Xf : T) { Inside += FVector::Dist(Xf.GetLocation().GetSafeNormal() * P.RadiusCm, Town.LocalCm) < Town.RadiusCm ? 1 : 0; }
		}
		return Inside;
	};
	FScatterSettings Wild = S;
	Wild.bCities = false;
	int32 WithRule = 0, WithoutRule = 0;
	// A chunk of boulders and stones, and one of the finest, where the pebbles are thick on level ground.
	for (const int32 D : { BoulderDepth(P) + 2, FMath::CeilToInt32(FMath::Log2(P.RadiusCm * UE_DOUBLE_HALF_PI / 1700.0)) })
	{
		int32 Face; int64 X, Y;
		if (!TestTrue(TEXT("the town's chunk"), UPlanetTerrainComponent::ChunkAt(Town.Dir, D, Face, X, Y))) { continue; }
		WithRule += InTown(Build(P, S, Face, D, X, Y));
		WithoutRule += InTown(Build(P, Wild, Face, D, X, Y));
	}
	AddInfo(FString::Printf(TEXT("town of radius %.0f m: %d rocks inside it, %d without the rule"), Town.RadiusCm / 100.0, WithRule, WithoutRule));
	TestTrue(TEXT("the same ground would carry rocks without the rule"), WithoutRule > 0);
	TestEqual(TEXT("no rocks inside the town"), WithRule, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkTerrainPlantsLifeTest, "Slonk.Terrain.PlantsLife", SLONK_TEST_FLAGS)
bool FSlonkTerrainPlantsLifeTest::RunTest(const FString& Parameters)
{
	// The same moist lowland chunks grow grass, bushes and trees on a living world, and nothing on a dead one or one
	// without air.
	const FPlanetTerrainParams P = RockWorld();
	FPlanetTerrainParams Airless = P;
	Airless.bAirless = true;
	FScatterSettings Dead;
	const TArray<FVector> Spots = GreenSpots(P, 4);
	if (!TestTrue(TEXT("found moist lowland"), Spots.Num() > 0)) { return true; }
	int32 Grown = 0, OnDead = 0, OnAirless = 0, Grass = 0, Shrubs = 0, Trees = 0;
	for (const FVector& Dir : Spots)
	{
		// A tree chunk, a bush chunk and a grass chunk over the spot.
		for (const int32 D : { DepthFor(P, 31000.0), DepthFor(P, 8000.0), DepthFor(P, 1700.0) })
		{
			int32 Face; int64 X, Y;
			if (!UPlanetTerrainComponent::ChunkAt(Dir, D, Face, X, Y)) { continue; }
			const FRocks R = Build(P, Living(), Face, D, X, Y);
			Grown += R.NumPlants();
			Grass += R.Xf[SlonkScatter::PlantMesh(EPlant::Grass)].Num();
			Shrubs += R.Xf[SlonkScatter::PlantMesh(EPlant::Shrub)].Num();
			for (const int32 T : TreeMeshes) { Trees += R.Xf[T].Num(); }
			OnDead += Build(P, Dead, Face, D, X, Y).NumPlants();
			OnAirless += Build(Airless, Living(), Face, D, X, Y).NumPlants();
		}
	}
	AddInfo(FString::Printf(TEXT("%d plants on a living world (%d grass, %d bushes, %d trees); %d with no life, %d without air"), Grown, Grass, Shrubs, Trees, OnDead, OnAirless));
	TestTrue(TEXT("grass grows on the moist lowland"), Grass > 0);
	TestTrue(TEXT("bushes grow there"), Shrubs > 0);
	TestTrue(TEXT("and trees"), Trees > 0);
	TestEqual(TEXT("nothing grows on a dead world"), OnDead, 0);
	TestEqual(TEXT("nothing grows without air"), OnAirless, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkTerrainTreesNestTest, "Slonk.Terrain.TreesNest", SLONK_TEST_FLAGS)
bool FSlonkTerrainTreesNestTest::RunTest(const FString& Parameters)
{
	// A tree a coarse chunk carries is carried again by its four children, of the same kind and size, in the same
	// place, and stands in the drawn ground: its foot below the surface, its trunk not lost in it.
	const FPlanetTerrainParams P = RockWorld();
	const int32 D = DepthFor(P, 31000.0);
	int32 Tried = 0, Trees = 0, Matched = 0, Floating = 0, Sunk = 0;
	for (const FVector& Dir : GreenSpots(P, 12))
	{
		int32 Face; int64 X, Y;
		if (!UPlanetTerrainComponent::ChunkAt(Dir, D, Face, X, Y)) { continue; }
		const FRocks Parent = Build(P, Living(), Face, D, X, Y);
		if (Parent.Xf[TreeMeshes[0]].Num() + Parent.Xf[TreeMeshes[1]].Num() < 10) { continue; }
		++Tried;
		FRocks Kids[4];
		for (int32 c = 0; c < 4; ++c) { Kids[c] = Build(P, Living(), Face, D + 1, X * 2 + (c & 1), Y * 2 + (c >> 1)); }
		for (const int32 M : TreeMeshes)
		{
			for (int32 i = 0; i < Parent.Xf[M].Num(); ++i)
			{
				const FTransform& A = Parent.Xf[M][i];
				++Trees;
				const double Foot = A.GetLocation().Size() - P.RadiusCm - Parent.Drawn[M][i];
				if (Foot > 0.0) { ++Floating; }
				if (Foot < -A.GetScale3D().Z * 100.0 * 0.15 - 100.0) { ++Sunk; }
				const FVector Ua = A.GetLocation().GetSafeNormal();
				bool bFound = false;
				for (const FRocks& Kid : Kids)
				{
					for (const FTransform& B : Kid.Xf[M])
					{
						const double Apart = FMath::Acos(FMath::Clamp(FVector::DotProduct(Ua, B.GetLocation().GetSafeNormal()), -1.0, 1.0)) * P.RadiusCm;
						if (Apart < 50.0 && FMath::Abs(B.GetScale3D().Z / A.GetScale3D().Z - 1.0) < 0.1) { bFound = true; break; }
					}
					if (bFound) { break; }
				}
				Matched += bFound ? 1 : 0;
			}
		}
		if (Tried >= 3) { break; }
	}
	AddInfo(FString::Printf(TEXT("%d trees on %d chunks: %d found again on the children, %d standing clear of the ground, %d sunk"), Trees, Tried, Matched, Floating, Sunk));
	TestTrue(TEXT("found wooded chunks"), Tried > 0);
	TestTrue(TEXT("nearly every tree is found again on the finer chunks"), Matched >= Trees * 0.85);
	TestEqual(TEXT("no tree's foot stands clear of the drawn ground"), Floating, 0);
	TestEqual(TEXT("no tree is sunk past its foot"), Sunk, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkTerrainShrubsOnDotsTest, "Slonk.Terrain.ShrubsOnDots", SLONK_TEST_FLAGS)
bool FSlonkTerrainShrubsOnDotsTest::RunTest(const FString& Parameters)
{
	// Every bush stands on a scrub dot the terrain material paints: in a cell the shader's hash gives a dot, at the
	// dot's centre, about as wide as the dot. And on the coarsest grid the terrain allows (8 steps a side, vertices ten
	// metres apart, the dots' cells under five) the chunk finds about as many as on the default one: every dot the
	// drawn ground passes through, not only those in a cell that happens to hold a vertex.
	const FPlanetTerrainParams P = RockWorld();
	const int32 D = DepthFor(P, 8000.0);
	const int32 M = SlonkScatter::PlantMesh(EPlant::Shrub);
	int32 Shrubs = 0, OffDot = 0, WrongSize = 0, Fine = 0, Coarse = 0;
	for (const FVector& Dir : GreenSpots(P, 6))
	{
		int32 Face; int64 X, Y;
		if (!UPlanetTerrainComponent::ChunkAt(Dir, D, Face, X, Y)) { continue; }
		for (const int32 Steps : { 64, 8 })
		{
			const FRocks R = Build(P, Living(), Face, D, X, Y, Steps);
			const FVector OriginNoise = SlonkScatter::ToNoiseSpace(R.Origin);
			(Steps == 64 ? Fine : Coarse) += R.Xf[M].Num();
			for (const FTransform& T : R.Xf[M])
			{
				++Shrubs;
				// The point of the ground the bush stands on (its foot, raised back out of the ground by the 4% of its
				// height it is sunk), in the material's noise space as the chunk hands it to the shader.
				const FVector Ground = T.GetLocation() + T.GetRotation().GetAxisZ() * (T.GetScale3D().Z * SlonkPlants::UnitCm * 0.04);
				const FVector Foot = OriginNoise + (Ground - R.Origin);
				FVector Centre; double Radius;
				if (!SlonkScatter::ScrubDotAt(Foot, Centre, Radius)) { ++OffDot; continue; }
				// Across the ground from the dot's centre: the centre lies above or below the surface, never beside it.
				const FVector Up = T.GetLocation().GetSafeNormal();
				const FVector Off = Foot - Centre;
				if ((Off - Up * FVector::DotProduct(Off, Up)).Size() > 25.0) { ++OffDot; }
				const double Wide = T.GetScale3D().X * 110.0;
				if (Wide < Radius * 1.8 || Wide > Radius * 2.4) { ++WrongSize; }
			}
		}
	}
	AddInfo(FString::Printf(TEXT("%d bushes (%d on the 64-step grids, %d on the 8-step): %d off their dot, %d not the dot's size"), Shrubs, Fine, Coarse, OffDot, WrongSize));
	TestTrue(TEXT("bushes were placed"), Fine > 10);
	TestEqual(TEXT("every bush stands on a painted dot's centre"), OffDot, 0);
	TestEqual(TEXT("every bush is as wide as its dot"), WrongSize, 0);
	// The two grids draw slightly different ground, so a few dots fall in or out; a missed cell would lose most.
	TestTrue(TEXT("a coarse grid finds about as many bushes as the default one"), Coarse >= Fine * 0.75 && Coarse <= Fine * 1.25);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkTerrainShrubsAcrossWrapTest, "Slonk.Terrain.ShrubsAcrossWrap", SLONK_TEST_FLAGS)
bool FSlonkTerrainShrubsAcrossWrapTest::RunTest(const FString& Parameters)
{
	// The material's noise space is each chunk's origin modulo NoisePeriodCm plus the vertex, so where a wrap plane of
	// that period runs between a chunk and its children their noise spaces are a period apart. The scrub dots, and so
	// the bushes, must come out the same in both: a bush a chunk carries is carried again by the children that refine
	// it, at the same place, wrap or no wrap.
	const FPlanetTerrainParams P = RockWorld();
	const int32 D = DepthFor(P, 8000.0);
	const int32 M = SlonkScatter::PlantMesh(EPlant::Shrub);
	const double Period = SlonkScatter::NoisePeriodCm;
	int32 Tried = 0, Bushes = 0, Matched = 0;
	for (const FVector& Spot : GreenSpots(P, 40))
	{
		// To the nearest wrap plane across the ground: along an axis the ground does not face, a few kilometres at most.
		const FVector G = Spot * P.RadiusCm;
		int32 Axis = -1; double Best = 1e30;
		for (int32 a = 0; a < 3; ++a)
		{
			if (FMath::Abs(Spot[a]) > 0.8) { continue; }
			const double To = FMath::RoundToDouble(G[a] / Period) * Period - G[a];
			if (FMath::Abs(To) < Best) { Best = FMath::Abs(To); Axis = a; }
		}
		if (Axis < 0) { continue; }
		FVector Moved = G;
		Moved[Axis] += FMath::RoundToDouble(G[Axis] / Period) * Period - G[Axis];
		int32 Face; int64 X, Y;
		if (!UPlanetTerrainComponent::ChunkAt(Moved.GetSafeNormal(), D, Face, X, Y)) { continue; }
		const FRocks Parent = Build(P, Living(), Face, D, X, Y);
		if (Parent.Xf[M].Num() < 10) { continue; }
		FRocks Kids[4];
		bool bStraddles = false;
		for (int32 c = 0; c < 4; ++c)
		{
			Kids[c] = Build(P, Living(), Face, D + 1, X * 2 + (c & 1), Y * 2 + (c >> 1));
			for (int32 a = 0; a < 3; ++a) { bStraddles |= FMath::FloorToDouble(Kids[c].Origin[a] / Period) != FMath::FloorToDouble(Parent.Origin[a] / Period); }
		}
		if (!bStraddles) { continue; }
		++Tried;
		for (const FTransform& A : Parent.Xf[M])
		{
			++Bushes;
			bool bFound = false;
			for (const FRocks& Kid : Kids)
			{
				for (const FTransform& B : Kid.Xf[M]) { if (FVector::Dist(A.GetLocation(), B.GetLocation()) < 30.0) { bFound = true; break; } }
				if (bFound) { break; }
			}
			Matched += bFound ? 1 : 0;
		}
		if (Tried >= 4) { break; }
	}
	AddInfo(FString::Printf(TEXT("%d chunks with a wrap plane between them and their children: %d bushes, %d found again on the children"), Tried, Bushes, Matched));
	if (!TestTrue(TEXT("found chunks across a wrap plane"), Tried > 0)) { return true; }
	// The finer ground moves a few bushes out of their dot's cell, or into another's; a lattice shifted by the wrap
	// would lose every bush on the children across the plane.
	TestTrue(TEXT("nearly every bush is found again across the wrap"), Matched >= Bushes * 0.8);
	return true;
}

namespace
{
	/** An Earth-like world of a given radius, its relief and ocean depth by SlonkTerrain::ReliefForRadius, as
	 *  APlanetBody::MakeTerrainParams gives them. */
	FPlanetTerrainParams EarthLike(double RadiusKm)
	{
		FPlanetTerrainParams P = RockWorld();
		P.RadiusCm = RadiusKm * 1.0e5;
		P.ReliefCm = SlonkTerrain::ReliefForRadius(0.0016, P.RadiusCm);
		P.OceanDepthCm = SlonkTerrain::ReliefForRadius(0.0008, P.RadiusCm);
		P.Mountains = 0.6f;
		P.Moisture = 0.55f;
		P.CanyonDepthCm = SlonkTerrain::ScaleWithWorld(1.6e5, P.RadiusCm);
		P.CanyonWidthCm = SlonkTerrain::ScaleWithWorld(1.6e6, P.RadiusCm);
		P.SnowlineCm = 5.0e5;
		return P;
	}

	/** A desert world of a given radius, its canyon country as APlanetBody::MakeTerrainParams gives a Desert one. */
	FPlanetTerrainParams DesertLike(double RadiusKm)
	{
		FPlanetTerrainParams P = EarthLike(RadiusKm);
		P.ReliefCm = SlonkTerrain::ReliefForRadius(0.002, P.RadiusCm);
		// The style's: dry basins for a sea, broad continents, some craters and maria, a cap at 0.92 (or none).
		P.SeaLevel = 0.05f; P.ContinentScale = 2.2f;
		P.Craters = 0.45f; P.CraterScale = 1.3f; P.Maria = 0.2f; P.IceCapStart = 0.92f;
		P.Mountains = 0.5f; P.Canyons = 0.9f; P.Strata = 0.9f; P.Moisture = 0.05f;
		P.CanyonDepthCm = SlonkTerrain::ScaleWithWorld(2.0e5, P.RadiusCm);
		P.CanyonWidthCm = SlonkTerrain::ScaleWithWorld(1.8e6, P.RadiusCm);
		P.SnowlineCm = 6.5e5;
		return P;
	}

	/** The other kinds of world, as APlanetBody::MakeTerrainParams gives them: an ocean, an ice world, a lava world and
	 *  a Mars, so every branch of the field (the polar caps, the lava seas, no air) is held by the golden hashes. */
	FPlanetTerrainParams OceanLike()
	{
		FPlanetTerrainParams P = EarthLike(6800.0);
		P.SeaLevel = 0.7f; P.Mountains = 0.4f; P.Canyons = 0.3f; P.Strata = 0.3f; P.Moisture = 0.8f;
		P.CanyonDepthCm = SlonkTerrain::ScaleWithWorld(1.2e5, P.RadiusCm);
		P.CanyonWidthCm = SlonkTerrain::ScaleWithWorld(1.2e6, P.RadiusCm);
		P.SnowlineCm = 4.6e5; P.IceCapStart = 0.75f;
		return P;
	}
	FPlanetTerrainParams IceLike()
	{
		FPlanetTerrainParams P = EarthLike(5000.0);
		P.Seed = FVector(41.3, 2.9, 66.1);
		P.SeaLevel = 0.4f; P.Mountains = 0.35f; P.Canyons = 0.2f; P.Strata = 0.3f; P.Moisture = 0.5f; P.IceCapStart = 0.5f;
		P.CanyonDepthCm = SlonkTerrain::ScaleWithWorld(1.0e5, P.RadiusCm);
		P.CanyonWidthCm = SlonkTerrain::ScaleWithWorld(1.2e6, P.RadiusCm);
		P.SnowlineCm = 1.0e5;
		return P;
	}
	FPlanetTerrainParams LavaLike()
	{
		FPlanetTerrainParams P = EarthLike(7000.0);
		P.Seed = FVector(12.7, 88.2, 5.5);
		P.SeaLevel = 0.25f; P.Mountains = 0.5f; P.Canyons = 0.3f; P.Strata = 0.3f; P.Moisture = 0.f; P.bLava = true; P.IceCapStart = 2.f;
		P.Craters = 0.4f; P.CraterScale = 1.2f;
		P.CanyonDepthCm = SlonkTerrain::ScaleWithWorld(1.5e5, P.RadiusCm);
		P.CanyonWidthCm = SlonkTerrain::ScaleWithWorld(1.4e6, P.RadiusCm);
		P.SnowlineCm = 0.0;
		return P;
	}
	FPlanetTerrainParams MarsLike()
	{
		FPlanetTerrainParams P = EarthLike(3390.0);
		P.Seed = FVector(70.1, 33.3, 19.9);
		P.ReliefCm = SlonkTerrain::ReliefForRadius(0.006, P.RadiusCm);
		P.SeaLevel = 0.3f; P.Mountains = 0.7f; P.Canyons = 1.f; P.Strata = 0.8f; P.Moisture = 0.f; P.bAirless = true; P.IceCapStart = 0.86f;
		P.Craters = 0.5f; P.Maria = 0.3f;
		P.CanyonDepthCm = SlonkTerrain::ScaleWithWorld(8.3e5, P.RadiusCm);
		P.CanyonWidthCm = SlonkTerrain::ScaleWithWorld(2.7e7, P.RadiusCm);
		P.SnowlineCm = 0.0;
		return P;
	}

	/** Every field of a sample, in turn, into a CRC (not the struct's bytes: its padding is not the field's). */
	uint32 CrcSample(const FPlanetTerrainSample& S, uint32 Crc)
	{
		const double D[3] = { S.HeightCm, S.GroundCm, S.WaterCm };
		const float F[14] = { S.River, S.Rock, S.Wet, S.Canyon, S.Wall, S.CutCm, S.Table, S.Ice, S.Crater, S.Linea, S.Slope, S.Stratum, S.Badland, S.bWater ? 1.f : 0.f };
		Crc = FCrc::MemCrc32(D, sizeof(D), Crc);
		return FCrc::MemCrc32(F, sizeof(F), Crc);
	}

	/** The CRC of every field of the samples over N directions spread over the globe, at one level of detail. */
	uint32 CrcField(const FPlanetTerrainParams& P, int32 N, double MinWavelengthCm)
	{
		TArray<FPlanetTerrainSample> Samples;
		Samples.SetNum(N);
		ParallelFor(N, [&](int32 i) { Samples[i] = SlonkTerrain::Sample(P, Fibonacci(i, N), MinWavelengthCm); });
		uint32 Crc = 0;
		for (const FPlanetTerrainSample& S : Samples) { Crc = CrcSample(S, Crc); }
		return Crc;
	}

	/** The chunks the golden hashes build: forest, scrub and meadow chunks over an Earth's green lowlands, a boulder
	 *  chunk on its rocky slopes, one on its high latitudes, and a desert's rock and scrub. Each with the world and the
	 *  settings it is built under. */
	struct FGoldenChunk { FPlanetTerrainParams P; FScatterSettings S; int32 Face; int32 Depth; int64 X, Y; };
	TArray<FGoldenChunk> GoldenChunks()
	{
		TArray<FGoldenChunk> Out;
		const FPlanetTerrainParams Earth = EarthLike(6371.0), Desert = DesertLike(6371.0);
		FScatterSettings Scrub; Scrub.Life = 0.2f;
		auto Add = [&Out](const FPlanetTerrainParams& P, const FScatterSettings& S, const FVector& Dir, int32 Depth)
		{
			FGoldenChunk C{ P, S, 0, Depth, 0, 0 };
			if (UPlanetTerrainComponent::ChunkAt(Dir.GetSafeNormal(), Depth, C.Face, C.X, C.Y)) { Out.Add(C); }
		};
		const TArray<FVector> Green = GreenSpots(Earth, 3);
		for (int32 i = 0; i < Green.Num(); ++i) { Add(Earth, Living(), Green[i], DepthFor(Earth, i == 2 ? 1700.0 : 31000.0)); }
		if (Green.Num() > 0) { Add(Earth, Living(), Green[0], DepthFor(Earth, 8000.0)); }
		const TArray<FVector> Rocky = RockySpots(Earth, 1);
		if (Rocky.Num() > 0) { Add(Earth, Living(), Rocky[0], BoulderDepth(Earth)); }
		Add(Earth, Living(), FVector(0.31, 0.52, 0.79), DepthFor(Earth, 31000.0));
		const TArray<FVector> Dry = RockySpots(Desert, 1);
		if (Dry.Num() > 0) { Add(Desert, Scrub, Dry[0], BoulderDepth(Desert)); Add(Desert, Scrub, Dry[0], DepthFor(Desert, 8000.0)); }
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkTerrainClimateNeutralTest, "Slonk.Terrain.ClimateNeutral", SLONK_TEST_FLAGS)
bool FSlonkTerrainClimateNeutralTest::RunTest(const FString& Parameters)
{
	// A world with no climate of its own (strangeness 0: the home system, the charted neighbours) is the world it always
	// was, bit for bit: every field of the samples of six kinds of world at three levels of detail, and everything a
	// chunk build makes (the drawn ground, its attributes, the forest's cover, the rocks and plants), against hashes taken
	// before the climate axis was added.
	struct FWorld { const TCHAR* Name; FPlanetTerrainParams P; uint32 Golden[3]; };
	const FWorld Worlds[] = {
		{ TEXT("Earth-like"), EarthLike(6371.0), { 0xf58fa661u, 0xc880ab9eu, 0xd1ad6fbbu } },
		{ TEXT("desert"), DesertLike(6371.0), { 0xdb6ff17eu, 0xa672dc69u, 0x0af11a45u } },
		{ TEXT("ocean"), OceanLike(), { 0x702e32ccu, 0x49ce7b9cu, 0xf72ff131u } },
		{ TEXT("ice"), IceLike(), { 0x0bfaa6d1u, 0x4afff9aeu, 0xdc231971u } },
		{ TEXT("lava"), LavaLike(), { 0xb87a6896u, 0x25a3e4c0u, 0xb756a9d0u } },
		{ TEXT("Mars"), MarsLike(), { 0x81055d1cu, 0x6786104du, 0x79007037u } },
	};
	const double Levels[3] = { 0.0, 2.0e4, 2.0e6 };
	for (const FWorld& W : Worlds)
	{
		for (int32 L = 0; L < 3; ++L)
		{
			const uint32 Crc = CrcField(W.P, 4096, Levels[L]);
			AddInfo(FString::Printf(TEXT("%s at %.0f cm: 0x%08xu"), W.Name, Levels[L], Crc));
			TestEqual(FString::Printf(TEXT("the %s world's field at %.0f cm is as it was"), W.Name, Levels[L]), Crc, W.Golden[L]);
		}
	}
	const uint32 GoldenChunks_[] = { 0x222ab2f2u, 0xe1ff6c7eu, 0x820c2b4bu, 0xcde25a2eu, 0xf7ef04abu, 0x10e116c0u, 0x2c68b675u, 0xec783430u };
	const TArray<FGoldenChunk> Chunks = GoldenChunks();
	TestEqual(TEXT("the golden chunks"), Chunks.Num(), (int32)UE_ARRAY_COUNT(GoldenChunks_));
	for (int32 i = 0; i < Chunks.Num() && i < (int32)UE_ARRAY_COUNT(GoldenChunks_); ++i)
	{
		const FGoldenChunk& C = Chunks[i];
		const uint32 Crc = UPlanetTerrainComponent::ChunkBuildCrcForTest(C.P, C.S, C.Face, C.Depth, C.X, C.Y);
		AddInfo(FString::Printf(TEXT("chunk %d (f%d d%d %lld, %lld): 0x%08xu"), i, C.Face, C.Depth, C.X, C.Y, Crc));
		TestEqual(FString::Printf(TEXT("chunk %d builds as it did"), i), Crc, GoldenChunks_[i]);
	}
	// The snowline is the old formula's, bit for bit, at any length of direction, and so is the latitude the searches
	// steer by.
	FRandomStream R(1234);
	int32 Off = 0, ColdOff = 0;
	for (int32 i = 0; i < 10000; ++i)
	{
		const FVector Dir = R.GetUnitVector() * FMath::Pow(10.0, R.FRandRange(-3.0, 9.0));
		FPlanetTerrainParams P = EarthLike(6371.0);
		P.SnowlineCm = R.FRandRange(0.0, 8.0e5);
		const double Cos2 = FMath::Max(1.0 - Dir.Z * Dir.Z / FMath::Max(Dir.SizeSquared(), 1e-12), 0.0);
		const double Old = P.SnowlineCm * FMath::Pow(Cos2, 0.75);
		Off += SlonkTerrain::SnowlineCm(P, Dir) == Old ? 0 : 1;
		ColdOff += SlonkTerrain::ClimateColdness(P, Dir) == FMath::Abs(Dir.Z) ? 0 : 1;
	}
	TestEqual(TEXT("the snowline is the old formula's, bit for bit"), Off, 0);
	TestEqual(TEXT("the searches' coldness is the old latitude, bit for bit"), ColdOff, 0);
	return true;
}

namespace
{
	/** An Earth-like world held to its star along +X: under the star the hottest a roll gives (2.8 times the Earth-like
	 *  equator's heat), its terminator 0.6 of it, its far side 0.4 of the liquid range below freezing. */
	FPlanetTerrainParams LockedEarth(const FVector& Axis = FVector::XAxisVector)
	{
		FPlanetTerrainParams P = EarthLike(6371.0);
		const double T0 = SlonkTerrain::LapseNPerCm * P.SnowlineCm;
		P.Climate = EClimateModel::Locked;
		P.ClimateAxis = Axis.GetSafeNormal();
		P.DayN = (float)(T0 * 2.8);
		P.TermN = (float)(T0 * 0.6);
		P.NightN = -0.4f;
		P.IceCapStart = 2.f;
		return P;
	}
	/** An Earth-like world that spins, a little warmer than an Earth. */
	FPlanetTerrainParams SpinningEarth(float WarmthN = 0.06f)
	{
		FPlanetTerrainParams P = EarthLike(6371.0);
		P.Climate = EClimateModel::Spin;
		P.WarmthN = WarmthN;
		return P;
	}

	/** Level land whose cosine to the climate's axis lies in [MuLo, MuHi], wettest first, for the forest. */
	TArray<FVector> LockedGreenSpots(const FPlanetTerrainParams& P, int32 Count, double MuLo, double MuHi)
	{
		struct FSpot { FVector Dir; double Score; };
		TArray<FSpot> Spots;
		const int32 N = 20000;
		for (int32 i = 0; i < N; ++i)
		{
			const FVector Dir = Fibonacci(i, N);
			const double Mu = FVector::DotProduct(Dir, P.ClimateAxis);
			if (Mu < MuLo || Mu > MuHi) { continue; }
			const FPlanetTerrainSample S = SlonkTerrain::Sample(P, Dir, 200.0);
			if (S.bWater || S.Slope > 0.2f || S.HeightCm > 2.0e5 || S.Rock > 0.3f) { continue; }
			Spots.Add({ Dir, (double)S.Wet - S.Ice });
		}
		Spots.Sort([](const FSpot& A, const FSpot& B) { return A.Score > B.Score; });
		TArray<FVector> Out;
		for (int32 i = 0; i < Spots.Num() && Out.Num() < Count; ++i) { Out.Add(Spots[i].Dir); }
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkTerrainLockedProfileTest, "Slonk.Terrain.LockedProfile", SLONK_TEST_FLAGS)
bool FSlonkTerrainLockedProfileTest::RunTest(const FString& Parameters)
{
	// A locked world's sea-level temperature, pinned where it is defined: DayN under the star, TermN on the terminator,
	// a straight run between them on the day side; on the night side it falls to NightN over the first NightWidth of
	// the cosine and stays there to the far pole. Then the coolest locked temperate world the genome rolls (not the
	// hottest, which the Climate.Locked fixture takes), its climate laid on its terrain by SlonkGenome::ApplyClimate:
	// its ground still bakes toward the star, nothing on its day side freezes, and the scanner calls it baked. That
	// holds for a temperate world's 5 km snowline: the genome scales a locked world's heat by its archetype's own, so an
	// ocean world's coolest roll is only hot, a desert's day side runs past boiling and an ice world's is never hot,
	// which is reported here, not held.
	const FPlanetTerrainParams P = LockedEarth();
	const double D = P.DayN, T = P.TermN, N = P.NightN, W = P.NightWidth;
	struct FPin { double Mu, Want; const TCHAR* Where; };
	const FPin Pins[] = {
		{ 1.0, D, TEXT("under the star") }, { 0.5, T + 0.5 * (D - T), TEXT("half way up the day side") }, { 0.0, T, TEXT("on the terminator") },
		{ -0.5 * W, T - 0.5 * (T - N), TEXT("half way into the night's fall") }, { -W, N, TEXT("where the night's fall ends") }, { -1.0, N, TEXT("at the far pole") } };
	for (const FPin& Pin : Pins)
	{
		const FVector Dir(Pin.Mu, FMath::Sqrt(FMath::Max(0.0, 1.0 - Pin.Mu * Pin.Mu)), 0.0);
		TestNearlyEqual(FString::Printf(TEXT("sea-level temperature %s"), Pin.Where), SlonkTerrain::SeaTempN(P, Dir), Pin.Want, 1.0e-5);
	}

	// The coolest and the hottest under the star of many strange temperate worlds close in to their stars.
	FWorldGenome Coolest, Hottest;
	int32 Locks = 0;
	for (uint32 i = 1; i <= 2000; ++i)
	{
		const FWorldGenome G = SlonkGenome::Roll(EPlanetArchetype::Terrestrial, 1.f, 2246822519u * i + 7u, 1.f);
		if (!G.bTidallyLocked) { continue; }
		if (Locks == 0 || G.DayHeat < Coolest.DayHeat) { Coolest = G; }
		if (Locks == 0 || G.DayHeat > Hottest.DayHeat) { Hottest = G; }
		++Locks;
	}
	auto LockedAs = [](const FWorldGenome& G, EPlanetArchetype Kind, FPlanetTerrainParams Q)
	{
		// A locked world has no polar caps (APlanetBody::ResolveStyleFromArchetype).
		Q.IceCapStart = 2.f;
		SlonkGenome::ApplyClimate(G, Kind, FVector::XAxisVector, Q);
		return Q;
	};
	auto Says = [](const FPlanetTerrainParams& Q)
	{
		TArray<FString> Words;
		UScienceScannerComponent::ClimateSurfaceFacts(Q, Words);
		return Words.Contains(TEXT("a baked day side")) ? TEXT("baked") : Words.Contains(TEXT("a hot day side")) ? TEXT("hot") : TEXT("neither hot nor baked");
	};
	const FPlanetTerrainParams Cool = LockedAs(Coolest, EPlanetArchetype::Terrestrial, EarthLike(6371.0));
	TestTrue(TEXT("strange temperate worlds close in lock"), Locks > 1000);
	TestTrue(TEXT("the coolest roll's climate is laid about its star"), Cool.Climate == EClimateModel::Locked && Cool.ClimateAxis == FVector::XAxisVector);
	struct FKind { const TCHAR* Name; EPlanetArchetype Kind; FPlanetTerrainParams Q; };
	for (const FKind& K : { FKind{ TEXT("temperate"), EPlanetArchetype::Terrestrial, EarthLike(6371.0) }, FKind{ TEXT("ocean"), EPlanetArchetype::Ocean, OceanLike() },
		FKind{ TEXT("desert"), EPlanetArchetype::Desert, DesertLike(6371.0) }, FKind{ TEXT("ice"), EPlanetArchetype::Ice, IceLike() } })
	{
		const FPlanetTerrainParams Lo = LockedAs(Coolest, K.Kind, K.Q), Hi = LockedAs(Hottest, K.Kind, K.Q);
		AddInfo(FString::Printf(TEXT("A locked %s world (%.1f km snowline): day side %.3f to %.3f of the liquid range, the scanner says %s to %s"),
			K.Name, K.Q.SnowlineCm / 1.0e5, Lo.DayN, Hi.DayN, Says(Lo), Says(Hi)));
	}
	double WetHigh = 0.0, WetMid = 0.0; int32 NHigh = 0, NMid = 0, IcyDay = 0;
	for (int32 i = 0; i < 8192; ++i)
	{
		const FVector Dir = Fibonacci(i, 8192);
		const double Mu = FVector::DotProduct(Dir, Cool.ClimateAxis);
		if (Mu < 0.1) { continue; }
		const FPlanetTerrainSample S = SlonkTerrain::Sample(Cool, Dir, 2000.0);
		if (S.bWater || S.HeightCm > 5.0e4 || S.Canyon > 0.1f || S.River > 0.05f) { continue; }
		IcyDay += (Mu > 0.2 && S.Ice > 0.05f) ? 1 : 0;
		if (Mu > 0.8) { WetHigh += S.Wet; ++NHigh; }
		else if (Mu < 0.3) { WetMid += S.Wet; ++NMid; }
	}
	AddInfo(FString::Printf(TEXT("Coolest of %d locked temperate rolls (x%.3f): day side %.3f of the liquid range; lowland wet %.2f under the star (%d samples), %.2f near the terminator (%d); %d icy samples on the day side"),
		Locks, Coolest.DayHeat, Cool.DayN, NHigh ? WetHigh / NHigh : 0.0, NHigh, NMid ? WetMid / NMid : 0.0, NMid, IcyDay));
	TestTrue(TEXT("found lowland on both parts of the day side"), NHigh > 20 && NMid > 20);
	TestTrue(TEXT("the coolest locked temperate world's ground bakes toward the star"), NHigh > 0 && NMid > 0 && WetHigh / NHigh < 0.75 * (WetMid / NMid));
	TestEqual(TEXT("nothing on the coolest locked temperate world's day side freezes"), IcyDay, 0);
	TestEqual(TEXT("the scanner calls the coolest locked temperate world's day side baked"), FString(Says(Cool)), FString(TEXT("baked")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkTerrainSnowlineIsFreezingTest, "Slonk.Terrain.SnowlineIsFreezing", SLONK_TEST_FLAGS)
bool FSlonkTerrainSnowlineIsFreezingTest::RunTest(const FString& Parameters)
{
	// On a world with a climate of its own the snowline is the freezing isotherm of the temperature the plants read
	// (SlonkTerrain::TemperatureN), and the ground's field reads the same one: a spinning world's warmth warms it by
	// WarmthN everywhere, at every height, over its strangeness-0 twin; the same north and south; the field dries the
	// ground by the temperature TemperatureN gives at the ground's own height (none of the twin's wet taken below half
	// the liquid range, all of it past 0.85, some between), on a locked world and on a spinning one hot enough to dry;
	// and the snow lies above the snowline and not below.
	FRandomStream R(77);
	int32 Unwarmed = 0, Asym = 0, Tried = 0;
	for (const FPlanetTerrainParams& P : { SpinningEarth(0.06f), SpinningEarth(-0.1f) })
	{
		FPlanetTerrainParams Twin = P;
		Twin.Climate = EClimateModel::Classic;
		Twin.WarmthN = 0.f;
		for (int32 i = 0; i < 5000; ++i)
		{
			const FVector Dir = R.GetUnitVector();
			const double H = R.FRandRange(-2.0e5, 1.0e6);
			++Tried;
			if (FMath::Abs(SlonkTerrain::TemperatureN(P, Dir, H) - SlonkTerrain::TemperatureN(Twin, Dir, H) - P.WarmthN) > 1e-9) { ++Unwarmed; }
			if (SlonkTerrain::SnowlineCm(P, FVector(Dir.X, Dir.Y, -Dir.Z)) != SlonkTerrain::SnowlineCm(P, Dir)) { ++Asym; }
		}
	}
	TestEqual(TEXT("a spinning world's warmth warms it by WarmthN everywhere, at every height"), Unwarmed, 0);
	TestEqual(TEXT("a spinning world's climate is the same north and south"), Asym, 0);
	// The drying, with no canyons, so no river's banks are wetted after it.
	int32 Kept = 0, KeptOff = 0, Baked = 0, BakedOff = 0, Drying = 0, DryingOff = 0;
	for (FPlanetTerrainParams P : { LockedEarth(), SpinningEarth(0.3f) })
	{
		P.Canyons = 0.f;
		FPlanetTerrainParams Twin = P;
		Twin.Climate = EClimateModel::Classic;
		const int32 N = 8000;
		for (int32 i = 0; i < N; ++i)
		{
			const FVector Dir = Fibonacci(i, N);
			const FPlanetTerrainSample S = SlonkTerrain::SampleStructure(P, Dir, 2.0e4);
			if (S.bWater) { continue; }
			const FPlanetTerrainSample T = SlonkTerrain::SampleStructure(Twin, Dir, 2.0e4);
			const double TN = SlonkTerrain::TemperatureN(P, Dir, S.HeightCm);
			if (TN < 0.5 - 1e-9) { ++Kept; KeptOff += S.Wet == T.Wet ? 0 : 1; }
			else if (TN > 0.85 + 1e-9) { ++Baked; BakedOff += S.Wet == 0.f ? 0 : 1; }
			else if (TN > 0.55 && TN < 0.8 && T.Wet > 0.01f) { ++Drying; DryingOff += S.Wet > 0.f && S.Wet < T.Wet ? 0 : 1; }
		}
	}
	AddInfo(FString::Printf(TEXT("land by its temperature: %d under half the liquid range (%d not the twin's wet), %d past 0.85 (%d not dry), %d drying between (%d not)"),
		Kept, KeptOff, Baked, BakedOff, Drying, DryingOff));
	TestTrue(TEXT("there is land at every temperature"), Kept > 100 && Baked > 20 && Drying > 20);
	TestEqual(TEXT("ground under half the liquid range keeps its wet"), KeptOff, 0);
	TestEqual(TEXT("ground past 0.85 of it is baked dry"), BakedOff, 0);
	TestEqual(TEXT("ground between dries partly"), DryingOff, 0);
	// The snow on the ground follows it, away from the spinning world's polar caps.
	int32 Above = 0, AboveBare = 0, Below = 0, BelowSnowy = 0;
	for (const FPlanetTerrainParams& P : { SpinningEarth(0.06f), LockedEarth() })
	{
		const int32 N = 8000;
		for (int32 i = 0; i < N; ++i)
		{
			const FVector Dir = Fibonacci(i, N);
			if (P.Climate == EClimateModel::Spin && FMath::Abs(Dir.Z) > P.IceCapStart - 0.1f) { continue; }
			const FPlanetTerrainSample S = SlonkTerrain::SampleStructure(P, Dir, 2.0e4);
			if (S.bWater) { continue; }
			const double Line = SlonkTerrain::SnowlineCm(P, Dir);
			const double Band = SlonkTerrain::SnowBandCm(Line);
			if (S.HeightCm > Line + 2.5 * Band) { ++Above; AboveBare += S.Ice < 0.95f ? 1 : 0; }
			else if (S.HeightCm < Line - 1.2 * Band) { ++Below; BelowSnowy += S.Ice > 0.01f ? 1 : 0; }
		}
	}
	AddInfo(FString::Printf(TEXT("%d direction checks; %d land samples well above the freezing isotherm (%d bare), %d below it (%d snowy)"), Tried, Above, AboveBare, Below, BelowSnowy));
	TestTrue(TEXT("some ground stands above it"), Above >= 5);
	TestEqual(TEXT("ground well above it is snow"), AboveBare, 0);
	TestEqual(TEXT("ground below it is not"), BelowSnowy, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkTerrainClimateLockedTest, "Slonk.Terrain.Climate.Locked", SLONK_TEST_FLAGS)
bool FSlonkTerrainClimateLockedTest::RunTest(const FString& Parameters)
{
	// A world the tides hold, along +X with an Earth's numbers: the lowlands under the star baked dry, away from the
	// rivers that keep their banks green, and bare of snow; everything colder than freezing at the sea's surface ice,
	// the sea too; a temperate ring along the terminator with open lowlands, its forest in colour and in trees, conifers
	// on its cold side and broadleaves on its warm one; and no tree where the air is frozen or past three quarters of the
	// liquid range.
	const FPlanetTerrainParams P = LockedEarth();
	FPlanetTerrainParams Twin = P;
	Twin.Climate = EClimateModel::Classic;
	Twin.IceCapStart = EarthLike(6371.0).IceCapStart;
	const int32 N = 20000;
	int32 HotLow = 0, HotWetBad = 0, HotLand = 0, HotIce = 0, Frozen = 0, FrozenOpen = 0, RingLow = 0, RingIcy = 0;
	double WorstRatio = 0.0, LeastFrozenIce = 1.0;
	for (int32 i = 0; i < N; ++i)
	{
		const FVector Dir = Fibonacci(i, N);
		const double Mu = FVector::DotProduct(Dir, P.ClimateAxis);
		const FPlanetTerrainSample S = SlonkTerrain::SampleStructure(P, Dir, 2.0e4);
		if (Mu > 0.8 && !S.bWater)
		{
			++HotLand;
			HotIce += S.Ice > 0.f ? 1 : 0;
			const FPlanetTerrainSample T = SlonkTerrain::SampleStructure(Twin, Dir, 2.0e4);
			// Lowland, off the canyon floors where a river's banks are wetted after the heat is laid on.
			if (S.HeightCm < 5.0e4 && S.Canyon < 0.02f && T.Wet > 0.1f)
			{
				++HotLow;
				const double Ratio = S.Wet / T.Wet;
				WorstRatio = FMath::Max(WorstRatio, Ratio);
				HotWetBad += Ratio < 0.3 ? 0 : 1;
			}
		}
		if (SlonkTerrain::SeaTempN(P, Dir) < -0.02)
		{
			++Frozen;
			LeastFrozenIce = FMath::Min(LeastFrozenIce, (double)S.Ice);
			FrozenOpen += S.Ice > 0.95f ? 0 : 1;
		}
		if (FMath::Abs(Mu) < 0.1 && (S.bWater || S.HeightCm < 3.0e4))
		{
			++RingLow;
			RingIcy += S.Ice < 0.2f ? 0 : 1;
		}
	}
	AddInfo(FString::Printf(TEXT("under the star: %d land samples (%d icy), %d lowland, wet at most %.2f of the spinning twin's (%d at 0.3 or more)"), HotLand, HotIce, HotLow, WorstRatio, HotWetBad));
	AddInfo(FString::Printf(TEXT("%d samples below freezing at the sea's surface, %d not ice (the least %.2f); %d ring lowland and sea samples, %d icy"), Frozen, FrozenOpen, LeastFrozenIce, RingLow, RingIcy));
	TestTrue(TEXT("there is land under the star to look at"), HotLow > 20);
	TestEqual(TEXT("the lowlands under the star are baked dry"), HotWetBad, 0);
	TestEqual(TEXT("and bare of snow"), HotIce, 0);
	TestTrue(TEXT("there is a far side below freezing"), Frozen > 1000);
	TestEqual(TEXT("everything below freezing at the sea's surface is ice, land and sea"), FrozenOpen, 0);
	TestTrue(TEXT("the ring has lowlands and seas"), RingLow > 100);
	TestEqual(TEXT("the ring's lowlands and seas are open"), RingIcy, 0);

	// The forest: on the ring, and across the day side up to where the heat stops it.
	const int32 D = DepthFor(P, 31000.0);
	int32 RingChunks = 0, CoverOn = 0, Broadleaves = 0, Conifers = 0, Trees = 0, OutOfRange = 0;
	double ColdestTree = 1.0, HottestTree = 0.0;
	auto Grow = [&](const FVector& Dir, bool bRing)
	{
		int32 Face; int64 X, Y;
		if (!UPlanetTerrainComponent::ChunkAt(Dir, D, Face, X, Y)) { return; }
		FRocks Rk;
		TArray<float> Cover;
		UPlanetTerrainComponent::BuildChunkScatterForTest(P, Living(), Face, D, X, Y, 64, Rk.Xf, Rk.Drawn, &Rk.Origin, &Cover);
		if (bRing)
		{
			++RingChunks;
			float Most = 0.f;
			for (const float C : Cover) { Most = FMath::Max(Most, C); }
			CoverOn += Most > 0.f ? 1 : 0;
			Broadleaves += Rk.Xf[TreeMeshes[0]].Num();
			Conifers += Rk.Xf[TreeMeshes[1]].Num();
		}
		for (const int32 M : TreeMeshes)
		{
			for (int32 i = 0; i < Rk.Xf[M].Num(); ++i)
			{
				const double TN = SlonkTerrain::TemperatureN(P, Rk.Xf[M][i].GetLocation().GetSafeNormal(), Rk.Drawn[M][i]);
				++Trees;
				ColdestTree = FMath::Min(ColdestTree, TN);
				HottestTree = FMath::Max(HottestTree, TN);
				// A few metres of grace either side: the tree's spot is read off the chunk's grid, its height here off its triangles.
				OutOfRange += TN >= -0.005 && TN < 0.755 ? 0 : 1;
			}
		}
	};
	// The ring's cold side and warm side, and the day side toward the heat's limit.
	for (const FVector& Dir : LockedGreenSpots(P, 3, -0.1, -0.02)) { Grow(Dir, true); }
	for (const FVector& Dir : LockedGreenSpots(P, 3, 0.02, 0.1)) { Grow(Dir, true); }
	for (const FVector& Dir : LockedGreenSpots(P, 4, 0.3, 0.75)) { Grow(Dir, false); }
	for (const FVector& Dir : LockedGreenSpots(P, 2, 0.75, 1.0)) { Grow(Dir, false); }
	for (const FVector& Dir : LockedGreenSpots(P, 2, -0.3, -0.12)) { Grow(Dir, false); }
	AddInfo(FString::Printf(TEXT("%d ring chunks, %d with forest cover: %d broadleaves, %d conifers; %d trees in all, from %.3f to %.3f of the liquid range (%d out of 0..0.75)"),
		RingChunks, CoverOn, Broadleaves, Conifers, Trees, ColdestTree, HottestTree, OutOfRange));
	TestTrue(TEXT("found the ring's lowlands"), RingChunks >= 4);
	TestTrue(TEXT("the ring's forest is in the ground's colour"), CoverOn > 0);
	TestTrue(TEXT("the ring grows broadleaves"), Broadleaves > 0);
	TestTrue(TEXT("and conifers"), Conifers > 0);
	TestEqual(TEXT("no tree stands where the air is frozen or past three quarters of the liquid range"), OutOfRange, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkTerrainClimateLandmarksTest, "Slonk.Terrain.Climate.Landmarks", SLONK_TEST_FLAGS)
bool FSlonkTerrainClimateLandmarksTest::RunTest(const FString& Parameters)
{
	// `ship terrain goto substellar|ring|nightcap` finds a locked world's own landmarks whatever the sun's direction
	// (they are where the star stands, not where it lights): dry ground near the point under the star, the ring's land
	// on the sunlit edge of the terminator, the middle of the far side; and a world that spins has none of them.
	const FPlanetTerrainParams P = LockedEarth(FVector(0.3, -0.9, 0.3));
	UPlanetTerrainComponent* T = NewObject<UPlanetTerrainComponent>();
	T->Configure(P, nullptr);
	const FVector Sun = P.ClimateAxis;
	FVector Dir; double H = 0.0;
	if (TestTrue(TEXT("the substellar point is found"), T->FindFeature(TEXT("substellar"), FVector::ZeroVector, Dir, H, Sun)))
	{
		const FPlanetTerrainSample S = SlonkTerrain::Sample(P, Dir, 1.0e4);
		AddInfo(FString::Printf(TEXT("substellar: the star %.1f deg from the zenith, ground %.0f m, wet %.2f"), FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(Dir, Sun))), H / 100.0, S.Wet));
		TestTrue(TEXT("the substellar ground is near the point under the star"), FVector::DotProduct(Dir, Sun) > 0.9);
		TestFalse(TEXT("and is land"), S.bWater);
	}
	if (TestTrue(TEXT("the ring is found, with the sun given"), T->FindFeature(TEXT("ring"), FVector::ZeroVector, Dir, H, Sun)))
	{
		const double Mu = FVector::DotProduct(Dir, Sun);
		AddInfo(FString::Printf(TEXT("ring: the star %.2f deg up, ground %.0f m, sea-level temperature %.3f of the liquid range"), FMath::RadiansToDegrees(FMath::Asin(Mu)), H / 100.0, SlonkTerrain::SeaTempN(P, Dir)));
		TestTrue(TEXT("the ring lies on the terminator's sunlit edge"), Mu > 0.0 && Mu < 0.08);
		TestTrue(TEXT("on unfrozen ground"), SlonkTerrain::SeaTempN(P, Dir) > 0.0);
	}
	if (TestTrue(TEXT("the night cap is found, the search turned to the night"), T->FindFeature(TEXT("nightcap"), -Sun, Dir, H, -Sun)))
	{
		AddInfo(FString::Printf(TEXT("nightcap: the star %.1f deg below the horizon, ice %.2f"), FMath::RadiansToDegrees(-FMath::Asin(FVector::DotProduct(Dir, Sun))), SlonkTerrain::Sample(P, Dir, 1.0e4).Ice));
		TestTrue(TEXT("the night cap is the far side's middle"), FVector::DotProduct(Dir, Sun) < -0.95);
	}
	T->Configure(SpinningEarth(), nullptr);
	TestFalse(TEXT("a spinning world has no substellar point to find"), T->FindFeature(TEXT("substellar"), FVector::ZeroVector, Dir, H, Sun));
	T->Configure(EarthLike(6371.0), nullptr);
	TestFalse(TEXT("nor a home world a ring"), T->FindFeature(TEXT("ring"), FVector::ZeroVector, Dir, H, Sun));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkTerrainClimateMaterialTest, "Slonk.Terrain.Climate.Material", SLONK_TEST_FLAGS)
bool FSlonkTerrainClimateMaterialTest::RunTest(const FString& Parameters)
{
	// The ground's material is told only what it reads: every parameter the terrain sets on it is one M_PlanetTerrain
	// has, on a home world, a spinning one and a locked one; and it paints frozen seas (SeaIce) on the locked one alone.
	UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Slonk/Materials/M_PlanetTerrain.M_PlanetTerrain"));
	if (!TestNotNull(TEXT("the terrain material loads"), Mat)) { return true; }
	TArray<FMaterialParameterInfo> ScalarInfo, VectorInfo;
	TArray<FGuid> Ids;
	Mat->GetAllScalarParameterInfo(ScalarInfo, Ids);
	Mat->GetAllVectorParameterInfo(VectorInfo, Ids);
	TSet<FName> Scalars, Vectors;
	for (const FMaterialParameterInfo& I : ScalarInfo) { Scalars.Add(I.Name); }
	for (const FMaterialParameterInfo& I : VectorInfo) { Vectors.Add(I.Name); }
	// The component keeps its material instance to itself; it is read here through its reflected property.
	const FObjectProperty* MIDProp = FindFProperty<FObjectProperty>(UPlanetTerrainComponent::StaticClass(), TEXT("MID"));
	if (!TestNotNull(TEXT("the terrain's material instance is a property"), MIDProp)) { return true; }
	TArray<FString> Unread;
	struct FWorld { const TCHAR* Name; FPlanetTerrainParams P; float SeaIce; };
	for (const FWorld& W : { FWorld{ TEXT("home"), EarthLike(6371.0), 0.f }, FWorld{ TEXT("spinning"), SpinningEarth(-0.1f), 0.f }, FWorld{ TEXT("locked"), LockedEarth(), 1.f } })
	{
		UPlanetTerrainComponent* T = NewObject<UPlanetTerrainComponent>();
		T->Configure(W.P, Mat);
		const UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(MIDProp->GetObjectPropertyValue_InContainer(T));
		if (!TestNotNull(FString::Printf(TEXT("the %s world's ground has a material instance"), W.Name), MID)) { continue; }
		float SeaIce = -1.f;
		for (const FScalarParameterValue& V : MID->ScalarParameterValues)
		{
			if (!Scalars.Contains(V.ParameterInfo.Name)) { Unread.AddUnique(V.ParameterInfo.Name.ToString()); }
			if (V.ParameterInfo.Name == FName(TEXT("SeaIce"))) { SeaIce = V.ParameterValue; }
		}
		for (const FVectorParameterValue& V : MID->VectorParameterValues)
		{
			if (!Vectors.Contains(V.ParameterInfo.Name)) { Unread.AddUnique(V.ParameterInfo.Name.ToString()); }
		}
		TestEqual(FString::Printf(TEXT("the %s world's seas are painted %s"), W.Name, W.SeaIce > 0.f ? TEXT("frozen") : TEXT("open")), SeaIce, W.SeaIce);
	}
	AddInfo(FString::Printf(TEXT("M_PlanetTerrain has %d scalar and %d vector parameters; the terrain sets %d it does not have%s%s"),
		Scalars.Num(), Vectors.Num(), Unread.Num(), Unread.Num() ? TEXT(": ") : TEXT(""), *FString::Join(Unread, TEXT(", "))));
	TestEqual(TEXT("every parameter the terrain sets is one its material reads"), Unread.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkTerrainClimateAxisTest, "Slonk.Terrain.Climate.Axis", SLONK_TEST_FLAGS)
bool FSlonkTerrainClimateAxisTest::RunTest(const FString& Parameters)
{
	// A locked world's climate is laid about its axis alone: turning the axis and the direction together leaves the
	// snowline and the coldness the searches steer by where they were (to a billionth of their size).
	FRandomStream R(4242);
	int32 Off = 0;
	double Worst = 0.0;
	for (int32 i = 0; i < 10000; ++i)
	{
		const FQuat Q = FQuat(R.GetUnitVector(), R.FRandRange(0.0, UE_DOUBLE_TWO_PI));
		const FVector Axis = R.GetUnitVector();
		const FPlanetTerrainParams A = LockedEarth(Axis), B = LockedEarth(Q.RotateVector(Axis));
		const FVector Dir = R.GetUnitVector() * R.FRandRange(0.5, 2.0);
		const double La = SlonkTerrain::SnowlineCm(A, Dir), Lb = SlonkTerrain::SnowlineCm(B, Q.RotateVector(Dir));
		const double Err = FMath::Abs(La - Lb) / FMath::Max(FMath::Abs(La), 1.0);
		Worst = FMath::Max(Worst, Err);
		Off += Err <= 1e-9 && FMath::Abs(SlonkTerrain::ClimateColdness(A, Dir) - SlonkTerrain::ClimateColdness(B, Q.RotateVector(Dir))) <= 1e-9 ? 0 : 1;
	}
	AddInfo(FString::Printf(TEXT("worst snowline change with the axis turned: %.2e of its size"), Worst));
	TestEqual(TEXT("turning the axis and the direction together changes nothing"), Off, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkTerrainClimateDeterministicTest, "Slonk.Terrain.Climate.Deterministic", SLONK_TEST_FLAGS)
bool FSlonkTerrainClimateDeterministicTest::RunTest(const FString& Parameters)
{
	// A world with a climate of its own samples and builds the same on any thread as on one, bit for bit: the samples of
	// a locked and a spinning world in a parallel loop against a serial one, and their chunks built side by side.
	int32 SampleOff = 0, ChunkOff = 0, Chunks = 0;
	for (const FPlanetTerrainParams& P : { LockedEarth(), SpinningEarth(-0.08f) })
	{
		const int32 N = 4096;
		TArray<FPlanetTerrainSample> Par;
		Par.SetNum(N);
		ParallelFor(N, [&](int32 i) { Par[i] = SlonkTerrain::Sample(P, Fibonacci(i, N), 0.0); });
		for (int32 i = 0; i < N; ++i) { SampleOff += CrcSample(SlonkTerrain::Sample(P, Fibonacci(i, N), 0.0), 0) == CrcSample(Par[i], 0) ? 0 : 1; }
		struct FChunk { int32 Face, Depth; int64 X, Y; };
		TArray<FChunk> List;
		const TArray<FVector> Spots = P.Climate == EClimateModel::Locked ? LockedGreenSpots(P, 4, -0.1, 0.6) : GreenSpots(P, 4);
		for (int32 i = 0; i < Spots.Num(); ++i)
		{
			FChunk C; C.Depth = DepthFor(P, i % 2 ? 8000.0 : 31000.0);
			if (UPlanetTerrainComponent::ChunkAt(Spots[i], C.Depth, C.Face, C.X, C.Y)) { List.Add(C); }
		}
		TArray<uint32> Crcs;
		Crcs.SetNumZeroed(List.Num());
		ParallelFor(List.Num(), [&](int32 i) { Crcs[i] = UPlanetTerrainComponent::ChunkBuildCrcForTest(P, Living(), List[i].Face, List[i].Depth, List[i].X, List[i].Y); });
		for (int32 i = 0; i < List.Num(); ++i)
		{
			++Chunks;
			ChunkOff += UPlanetTerrainComponent::ChunkBuildCrcForTest(P, Living(), List[i].Face, List[i].Depth, List[i].X, List[i].Y) == Crcs[i] ? 0 : 1;
		}
	}
	AddInfo(FString::Printf(TEXT("8192 samples and %d chunks: %d samples and %d chunks differ between threads"), Chunks, SampleOff, ChunkOff));
	TestTrue(TEXT("chunks were built"), Chunks >= 6);
	TestEqual(TEXT("the samples are the same on any thread"), SampleOff, 0);
	TestEqual(TEXT("and the chunks"), ChunkOff, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkTerrainClimatePerfTest, "Slonk.Terrain.Climate.Perf", SLONK_TEST_FLAGS)
bool FSlonkTerrainClimatePerfTest::RunTest(const FString& Parameters)
{
	// What a locked world's climate costs a sample over the home worlds' (the plan is under 2%): 100,000 samples at the
	// structure's finest level, where the biome is the largest share of the work, timed in turn on one thread, the best
	// of two. Reported, not held to: a timing on a shared machine is no pass or fail.
	const FPlanetTerrainParams Locked = LockedEarth();
	FPlanetTerrainParams Classic = Locked;
	Classic.Climate = EClimateModel::Classic;
	const int32 N = 100000;
	double Sink = 0.0;
	auto Time = [&](const FPlanetTerrainParams& P)
	{
		const double T0 = FPlatformTime::Seconds();
		for (int32 i = 0; i < N; ++i) { Sink += SlonkTerrain::Sample(P, Fibonacci(i, N), 2.0e4).Ice; }
		return FPlatformTime::Seconds() - T0;
	};
	double BestClassic = 1e30, BestLocked = 1e30;
	for (int32 k = 0; k < 2; ++k) { BestClassic = FMath::Min(BestClassic, Time(Classic)); BestLocked = FMath::Min(BestLocked, Time(Locked)); }
	AddInfo(FString::Printf(TEXT("100,000 samples: Classic %.3f s, Locked %.3f s (%+.1f%%; checksum %.0f)"), BestClassic, BestLocked, (BestLocked / BestClassic - 1.0) * 100.0, Sink));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkTerrainMountainsReliefTest, "Slonk.Terrain.MountainsRelief", SLONK_TEST_FLAGS)
bool FSlonkTerrainMountainsReliefTest::RunTest(const FString& Parameters)
{
	// Smaller worlds hold up more relief for their size: an Earth keeps its 10 km, a world a tenth its size gets
	// 3 km rather than 1, a Mars-size one at Mars's 0.6% 28 km. And a range's core climbs toward the relief: the
	// world's tallest ground is in its mountains at about seven tenths of the relief (before Mountains became
	// coverage alone, under half), on a small world and an Earth-size one. The highest of a coarse pass is climbed
	// to its summit, the coarse points being further apart than the ridges.
	TestTrue(TEXT("an Earth-size world keeps 10 km"), FMath::IsNearlyEqual(SlonkTerrain::ReliefForRadius(0.0016, 6.371e8) / 1e5, 10.19, 0.05));
	TestTrue(TEXT("a world a tenth the size gets about 3 km"), FMath::IsNearlyEqual(SlonkTerrain::ReliefForRadius(0.0016, 6.371e7) / 1e5, 3.22, 0.05));
	TestTrue(TEXT("a Mars-size world at 0.6% gets about 28 km"), FMath::IsNearlyEqual(SlonkTerrain::ReliefForRadius(0.006, 3.39e8) / 1e5, 27.9, 0.3));
	for (const double Km : { 600.0, 6371.0 })
	{
		const FPlanetTerrainParams P = EarthLike(Km);
		FVector Top(0.0, 0.0, 1.0);
		double Best = -1e30;
		const int32 N = 20000;
		for (int32 i = 0; i < N; ++i)
		{
			const FVector Dir = Fibonacci(i, N);
			const double Hc = SlonkTerrain::SampleStructure(P, Dir, 2.0e5).HeightCm;
			if (Hc > Best) { Best = Hc; Top = Dir; }
		}
		Best = SlonkTerrain::SampleStructure(P, Top, 2.0e4).HeightCm;
		const FVector E = FVector::CrossProduct(FMath::Abs(Top.Z) < 0.9 ? FVector::ZAxisVector : FVector::XAxisVector, Top).GetSafeNormal();
		const FVector Nn = FVector::CrossProduct(Top, E);
		for (double StepCm = 2.0e6; StepCm >= 2.0e4; )
		{
			bool bMoved = false;
			for (int32 k = 0; k < 8; ++k)
			{
				const double A = k * UE_DOUBLE_PI / 4.0;
				const FVector D = (Top * P.RadiusCm + (E * FMath::Cos(A) + Nn * FMath::Sin(A)) * StepCm).GetSafeNormal();
				const double Hd = SlonkTerrain::SampleStructure(P, D, 2.0e4).HeightCm;
				if (Hd > Best) { Best = Hd; Top = D; bMoved = true; }
			}
			if (!bMoved) { StepCm *= 0.5; }
		}
		const float TopRock = SlonkTerrain::SampleStructure(P, Top, 2.0e4).Rock;
		AddInfo(FString::Printf(TEXT("R %.0f km: relief %.2f km, tallest ground %.2f km (%.0f%%), rock there %.2f"), Km, P.ReliefCm / 1e5, Best / 1e5, Best / P.ReliefCm * 100.0, TopRock));
		TestTrue(FString::Printf(TEXT("the tallest ground of the %.0f km world reaches 55%% of its relief"), Km), Best >= P.ReliefCm * 0.55);
		TestTrue(FString::Printf(TEXT("and is in its mountains (bare rock), on the %.0f km world"), Km), TopRock > 0.5f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkTerrainMountainsLevelsTest, "Slonk.Terrain.MountainsLevels", SLONK_TEST_FLAGS)
bool FSlonkTerrainMountainsLevelsTest::RunTest(const FString& Parameters)
{
	// A coarse level of detail stands where the fine ones average, in the mountains too: the octaves it leaves out
	// count at what they add on average. Counting only the octaves it evaluates, the eroded ranges stood hundreds of
	// metres higher on the coarse chunks (about a kilometre on a Mars-size world), and the ground dropped as the
	// chunks split on the way down.
	for (const double Km : { 600.0, 3390.0, 6371.0 })
	{
		FPlanetTerrainParams P = EarthLike(Km);
		P.Mountains = 0.8f;
		if (Km == 3390.0)
		{
			// A Mars: its 0.6% relief, no sea.
			P.ReliefCm = SlonkTerrain::ReliefForRadius(0.006, P.RadiusCm);
			P.bAirless = true;
			P.Moisture = 0.f;
		}
		double SumCoarse = 0.0, SumFine = 0.0;
		int32 Count = 0;
		const int32 N = 6000;
		for (int32 i = 0; i < N; ++i)
		{
			const FVector Dir = Fibonacci(i, N);
			const FPlanetTerrainSample Fine = SlonkTerrain::SampleStructure(P, Dir, 2.0e4);
			if (Fine.Rock < 0.5f || Fine.bWater) { continue; }
			SumFine += Fine.HeightCm;
			SumCoarse += SlonkTerrain::SampleStructure(P, Dir, 2.0e6).HeightCm;
			++Count;
		}
		const double Drift = Count > 0 ? (SumCoarse - SumFine) / Count : 0.0;
		AddInfo(FString::Printf(TEXT("R %.0f km: %d mountain samples, the 20 km level %+.0f m from the 200 m one on average (relief %.1f km)"), Km, Count, Drift / 100.0, P.ReliefCm / 1e5));
		TestTrue(FString::Printf(TEXT("the %.0f km world has mountains to measure"), Km), Count > 100);
		TestTrue(FString::Printf(TEXT("the coarse level of the %.0f km world's ranges is within 2%% of the relief of the fine"), Km), FMath::Abs(Drift) < 0.02 * P.ReliefCm);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkTerrainSnowlineTest, "Slonk.Terrain.Snowline", SLONK_TEST_FLAGS)
bool FSlonkTerrainSnowlineTest::RunTest(const FString& Parameters)
{
	// Snow lies above the snowline for the latitude and not below it (away from the polar caps), and the forest
	// stops under it: on high ground trees grow up toward the treeline, about three quarters of the snowline, and
	// none stands above it.
	const FPlanetTerrainParams P = EarthLike(6371.0);
	int32 Above = 0, AboveBare = 0, Below = 0, BelowSnowy = 0;
	struct FHigh { double Alpine; FVector Dir; };
	TArray<FHigh> High;
	const int32 N = 8000;
	for (int32 i = 0; i < N; ++i)
	{
		const FVector Dir = Fibonacci(i, N);
		if (FMath::Abs(Dir.Z) > P.IceCapStart - 0.1f) { continue; }
		const FPlanetTerrainSample S = SlonkTerrain::SampleStructure(P, Dir, 2.0e4);
		if (S.bWater) { continue; }
		const double Line = SlonkTerrain::SnowlineCm(P, Dir);
		const double Band = SlonkTerrain::SnowBandCm(Line);
		if (S.HeightCm > Line + 2.5 * Band) { ++Above; AboveBare += S.Ice < 0.95f ? 1 : 0; }
		else if (S.HeightCm < Line - 1.2 * Band) { ++Below; BelowSnowy += S.Ice > 0.01f ? 1 : 0; }
		// Forest country climbing toward the treeline (from 0.65 of the snowline), the highest of it first below.
		if (S.HeightCm > Line * 0.45 && S.HeightCm < Line * 0.75 && S.Wet > 0.4f) { High.Add({ S.HeightCm / Line, Dir }); }
	}
	High.Sort([](const FHigh& A, const FHigh& B) { return A.Alpine > B.Alpine; });
	High.SetNum(FMath::Min(High.Num(), 10));
	AddInfo(FString::Printf(TEXT("%d samples well above the snowline (%d bare), %d below it (%d snowy)"), Above, AboveBare, Below, BelowSnowy));
	TestTrue(TEXT("some ground stands above the snowline"), Above >= 5);
	TestEqual(TEXT("ground well above the snowline is snow"), AboveBare, 0);
	TestEqual(TEXT("ground below it is not"), BelowSnowy, 0);
	// The treeline.
	const int32 D = DepthFor(P, 31000.0);
	int32 Trees = 0, OverLine = 0;
	double Highest = 0.0;
	for (const FHigh& Hi : High)
	{
		int32 Face; int64 X, Y;
		if (!UPlanetTerrainComponent::ChunkAt(Hi.Dir, D, Face, X, Y)) { continue; }
		const FRocks R = Build(P, Living(), Face, D, X, Y);
		for (const int32 M : TreeMeshes)
		{
			for (int32 i = 0; i < R.Xf[M].Num(); ++i)
			{
				const double Alpine = R.Drawn[M][i] / FMath::Max(SlonkTerrain::SnowlineCm(P, R.Xf[M][i].GetLocation().GetSafeNormal()), 1.0);
				++Trees;
				Highest = FMath::Max(Highest, Alpine);
				OverLine += Alpine > 0.92 ? 1 : 0;
			}
		}
	}
	AddInfo(FString::Printf(TEXT("%d trees on %d high chunks: the highest at %.0f%% of the snowline, %d above the treeline"), Trees, High.Num(), Highest * 100.0, OverLine));
	TestTrue(TEXT("trees grow on the high ground"), Trees > 0);
	TestTrue(TEXT("up toward the treeline"), Highest > 0.6);
	TestEqual(TEXT("none stands above the treeline"), OverLine, 0);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkTerrainCanyonsBySizeTest, "Slonk.Terrain.CanyonsBySize", SLONK_TEST_FLAGS)
bool FSlonkTerrainCanyonsBySizeTest::RunTest(const FString& Parameters)
{
	// A world's canyons go with its relief, as the square root of its size: the Grand Canyon's 1.6 km on an Earth is
	// half a kilometre on a world a tenth the size, not 160 m. They are cut that deep, on a small world and an
	// Earth-size one. And they are as wide as they say, rim to rim (the narrowest chord through a floor point, over
	// eighteen directions): the line's width comes from the distance to it, not from how fast its noise climbs, which
	// once made them twice their nominal width (the continents' own warp still stretches them by up to about half
	// locally, so it is the median of the main canyon's floors that is held to it).
	TestTrue(TEXT("an Earth-size world's canyon is 1.6 km deep"), FMath::IsNearlyEqual(SlonkTerrain::ScaleWithWorld(1.6e5, 6.371e8) / 1e5, 1.6, 0.01));
	TestTrue(TEXT("a world a tenth the size, half a kilometre"), FMath::IsNearlyEqual(SlonkTerrain::ScaleWithWorld(1.6e5, 6.371e7) / 1e5, 0.506, 0.01));
	for (const double Km : { 600.0, 6371.0 })
	{
		const FPlanetTerrainParams P = DesertLike(Km);
		double Deepest = 0.0;
		TArray<FVector> Floor;
		const int32 N = 20000;
		for (int32 i = 0; i < N; ++i)
		{
			const FVector Dir = Fibonacci(i, N);
			const FPlanetTerrainSample S = SlonkTerrain::SampleStructure(P, Dir, 2.0e4);
			if (S.bWater) { continue; }
			Deepest = FMath::Max(Deepest, (double)S.CutCm);
			// The main canyon's floor (a side canyon's is under 0.8 of the depth).
			if (S.Canyon >= 0.85f && Floor.Num() < 25) { Floor.Add(Dir); }
		}
		TArray<double> Widths;
		for (const FVector& Dc : Floor)
		{
			const float C0 = SlonkTerrain::SampleStructure(P, Dc, 2.0e4).Canyon;
			const FVector E = FVector::CrossProduct(FMath::Abs(Dc.Z) < 0.9 ? FVector::ZAxisVector : FVector::XAxisVector, Dc).GetSafeNormal();
			const FVector Nn = FVector::CrossProduct(Dc, E);
			double Narrowest = 1e30;
			for (int32 a = 0; a < 18; ++a)
			{
				const double A = a * UE_DOUBLE_PI / 18.0;
				const FVector T = E * FMath::Cos(A) + Nn * FMath::Sin(A);
				double Chord = 0.0;
				for (const double Side : { -1.0, 1.0 })
				{
					double Rim = 3.0 * P.CanyonWidthCm;
					for (double D = 0.0; D < 3.0 * P.CanyonWidthCm; D += P.CanyonWidthCm / 40.0)
					{
						if (SlonkTerrain::SampleStructure(P, (Dc * P.RadiusCm + T * (Side * D)).GetSafeNormal(), 2.0e4).Canyon < 0.05f * C0) { Rim = D; break; }
					}
					Chord += Rim;
				}
				Narrowest = FMath::Min(Narrowest, Chord);
			}
			Widths.Add(Narrowest);
		}
		Widths.Sort();
		const double Width = Widths.Num() > 0 ? Widths[Widths.Num() / 2] : 0.0;
		AddInfo(FString::Printf(TEXT("R %.0f km: canyons %.2f km deep, %.1f km wide; deepest cut %.2f km; %d floor points, the median %.1f km rim to rim"),
			Km, P.CanyonDepthCm / 1e5, P.CanyonWidthCm / 1e5, Deepest / 1e5, Floor.Num(), Width / 1e5));
		TestTrue(FString::Printf(TEXT("the %.0f km world's canyons are cut most of their depth"), Km), Deepest >= 0.7 * P.CanyonDepthCm);
		TestTrue(FString::Printf(TEXT("the %.0f km world has canyon floors to measure"), Km), Floor.Num() >= 10);
		TestTrue(FString::Printf(TEXT("and they are about as wide as they say on the %.0f km world"), Km), Width > 0.75 * P.CanyonWidthCm && Width < 1.3 * P.CanyonWidthCm);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkTerrainEscarpmentTest, "Slonk.Terrain.Escarpment", SLONK_TEST_FLAGS)
bool FSlonkTerrainEscarpmentTest::RunTest(const FString& Parameters)
{
	// The plateaus end in cliffs, not ramps: the scarps and the islands' flanks away from the canyons climb at 35
	// degrees or more over a couple of hundred metres (the old edge rose two and a half kilometres over a hundred
	// and more, a slope of a fiftieth). And the cap stands before the scarp as islands: around the first dozen
	// scarps, bare caprock tops (rock 0.6, no wall, no canyon, on tableland) standing over the ground 3 km away
	// on at least seven sides of eight, by 100 m or more, and 3 km apart.
	const FPlanetTerrainParams P = DesertLike(6371.0);
	TArray<FVector> Flanks;
	const int32 N = 40000;
	for (int32 i = 0; i < N; ++i)
	{
		const FVector Dir = Fibonacci(i, N);
		const FPlanetTerrainSample S = SlonkTerrain::SampleStructure(P, Dir, 2.0e4);
		if (S.bWater || S.Canyon > 0.02f) { continue; }
		if (S.Wall > 0.6f && Flanks.Num() < 40) { Flanks.Add(Dir); }
	}
	TArray<FVector> Tops;
	for (int32 f = 0; f < FMath::Min(Flanks.Num(), 12); ++f)
	{
		const FVector Fc = Flanks[f];
		const FVector E = FVector::CrossProduct(FMath::Abs(Fc.Z) < 0.9 ? FVector::ZAxisVector : FVector::XAxisVector, Fc).GetSafeNormal();
		const FVector Nn = FVector::CrossProduct(Fc, E);
		for (int32 j = -15; j <= 15; ++j)
		{
			for (int32 i = -15; i <= 15; ++i)
			{
				const FVector D = (Fc * P.RadiusCm + E * (i * 4.0e4) + Nn * (j * 4.0e4)).GetSafeNormal();
				const FPlanetTerrainSample S = SlonkTerrain::SampleStructure(P, D, 2.0e4);
				if (S.bWater || S.Table < 0.3f || S.Wall > 0.02f || S.Canyon > 0.02f || !FMath::IsNearlyEqual(S.Rock, 0.6f, 0.05f)) { continue; }
				bool bNear = false;
				for (const FVector& Tp : Tops) { if (FVector::DistSquared(Tp, D) * P.RadiusCm * P.RadiusCm < FMath::Square(3.0e5)) { bNear = true; break; } }
				if (bNear) { continue; }
				const FVector E2 = FVector::CrossProduct(FMath::Abs(D.Z) < 0.9 ? FVector::ZAxisVector : FVector::XAxisVector, D).GetSafeNormal();
				const FVector N2 = FVector::CrossProduct(D, E2);
				int32 Lower = 0;
				for (int32 a = 0; a < 8; ++a)
				{
					const double A = a * UE_DOUBLE_PI / 4.0;
					const FVector Dr = (D * P.RadiusCm + (E2 * FMath::Cos(A) + N2 * FMath::Sin(A)) * 3.0e5).GetSafeNormal();
					Lower += SlonkTerrain::SampleStructure(P, Dr, 2.0e4).HeightCm < S.HeightCm - 1.0e4 ? 1 : 0;
				}
				if (Lower >= 7) { Tops.Add(D); }
			}
		}
	}
	int32 Steep = 0;
	for (const FVector& Dc : Flanks)
	{
		const FVector E = FVector::CrossProduct(FMath::Abs(Dc.Z) < 0.9 ? FVector::ZAxisVector : FVector::XAxisVector, Dc).GetSafeNormal();
		const FVector Nn = FVector::CrossProduct(Dc, E);
		double Steepest = 0.0;
		for (int32 a = 0; a < 8; ++a)
		{
			const double A = a * UE_DOUBLE_PI / 8.0;
			const FVector T = (E * FMath::Cos(A) + Nn * FMath::Sin(A)) * 1.0e4;
			const double H0 = SlonkTerrain::SampleStructure(P, (Dc * P.RadiusCm - T).GetSafeNormal(), 2.0e4).HeightCm;
			const double H1 = SlonkTerrain::SampleStructure(P, (Dc * P.RadiusCm + T).GetSafeNormal(), 2.0e4).HeightCm;
			Steepest = FMath::Max(Steepest, FMath::Abs(H1 - H0) / 2.0e4);
		}
		Steep += Steepest >= 0.7 ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("%d scarp and island flanks, %d of them at 35 degrees or more; %d islands of caprock standing clear around the first dozen"), Flanks.Num(), Steep, Tops.Num()));
	TestTrue(TEXT("the plateaus have scarps"), Flanks.Num() >= 20);
	TestTrue(TEXT("most of them cliffs"), Steep * 2 >= Flanks.Num());
	TestTrue(TEXT("with islands of the cap before them"), Tops.Num() >= 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkTerrainBadlandsTest, "Slonk.Terrain.Badlands", SLONK_TEST_FLAGS)
bool FSlonkTerrainBadlandsTest::RunTest(const FString& Parameters)
{
	// Below the scarps of a dry world the soft beds are badlands, cut into gullies metres deep; a world without air
	// has none, rain having made them.
	FPlanetTerrainParams Airless = DesertLike(6371.0);
	Airless.bAirless = true;
	const FPlanetTerrainParams P = DesertLike(6371.0);
	TArray<FVector> Bad;
	int32 OnAirless = 0;
	const int32 N = 40000;
	for (int32 i = 0; i < N; ++i)
	{
		const FVector Dir = Fibonacci(i, N);
		const FPlanetTerrainSample S = SlonkTerrain::SampleStructure(P, Dir, 2.0e4);
		if (!S.bWater && S.Badland > 0.6f && Bad.Num() < 12) { Bad.Add(Dir); }
		OnAirless += SlonkTerrain::SampleStructure(Airless, Dir, 2.0e4).Badland > 0.f ? 1 : 0;
	}
	// The relief of the ground along 200 m of each, at full detail, less its straight trend.
	int32 Gullied = 0;
	for (const FVector& Dc : Bad)
	{
		const FVector E = FVector::CrossProduct(FMath::Abs(Dc.Z) < 0.9 ? FVector::ZAxisVector : FVector::XAxisVector, Dc).GetSafeNormal();
		TArray<double> H;
		for (int32 k = 0; k <= 40; ++k) { H.Add(SlonkTerrain::Sample(P, (Dc * P.RadiusCm + E * ((k - 20) * 500.0)).GetSafeNormal(), 50.0).HeightCm); }
		double Lo = 1e30, Hi = -1e30;
		for (int32 k = 0; k <= 40; ++k) { const double R = H[k] - FMath::Lerp(H[0], H[40], k / 40.0); Lo = FMath::Min(Lo, R); Hi = FMath::Max(Hi, R); }
		Gullied += Hi - Lo >= 800.0 ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("%d badland samples checked, %d with gullies 8 m deep or more across 200 m; %d badland samples without air"), Bad.Num(), Gullied, OnAirless));
	TestTrue(TEXT("a dry world has badlands"), Bad.Num() >= 5);
	TestTrue(TEXT("cut into gullies"), Gullied * 3 >= Bad.Num() * 2);
	TestEqual(TEXT("a world without air has none"), OnAirless, 0);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkTerrainFindLandformsTest, "Slonk.Terrain.FindLandforms", SLONK_TEST_FLAGS)
bool FSlonkTerrainFindLandformsTest::RunTest(const FString& Parameters)
{
	// `ship terrain goto` finds what it is asked for and says so only when it has: a scarp (a wall with no canyon cut
	// under it), badlands, and a canyon's rim (a wall with a cut, not a scarp's); and no badlands on a world without
	// air. And each search finishes: on the flat top of a scarp's wall or of the badlands' mask the hill-climb once
	// stepped east and west for ever, each step "improving" the latitude term by a rounding error.
	const FPlanetTerrainParams P = DesertLike(6371.0);
	UPlanetTerrainComponent* T = NewObject<UPlanetTerrainComponent>();
	T->Configure(P, nullptr);
	auto Find = [&](const TCHAR* Kind, FVector& Dir) -> bool
	{
		double H = 0.0;
		const double T0 = FPlatformTime::Seconds();
		const bool bFound = T->FindFeature(Kind, FVector::ZeroVector, Dir, H);
		const double Secs = FPlatformTime::Seconds() - T0;
		AddInfo(FString::Printf(TEXT("%s: %s in %.2f s"), Kind, bFound ? TEXT("found") : TEXT("not found"), Secs));
		TestTrue(FString::Printf(TEXT("the %s search finishes promptly"), Kind), Secs < 30.0);
		return bFound;
	};
	FVector Dir;
	if (Find(TEXT("mesa"), Dir))
	{
		const FPlanetTerrainSample S = SlonkTerrain::Sample(P, Dir, 1.0e4);
		AddInfo(FString::Printf(TEXT("  mesa: wall %.2f, canyon cut %.0f m"), S.Wall, S.CutCm / 100.0));
		TestTrue(TEXT("the scarp found is a wall"), S.Wall >= 0.1f);
		TestTrue(TEXT("and not a canyon's"), S.CutCm <= 0.01 * P.CanyonDepthCm);
	}
	else { AddError(TEXT("a desert world has scarps to find")); }
	if (Find(TEXT("badlands"), Dir))
	{
		const FPlanetTerrainSample S = SlonkTerrain::Sample(P, Dir, 1.0e4);
		AddInfo(FString::Printf(TEXT("  badlands: %.2f"), S.Badland));
		TestTrue(TEXT("the badlands found are badlands"), S.Badland >= 0.5f);
	}
	else { AddError(TEXT("a desert world has badlands to find")); }
	if (Find(TEXT("rim"), Dir))
	{
		const FPlanetTerrainSample S = SlonkTerrain::Sample(P, Dir, 1.0e4);
		AddInfo(FString::Printf(TEXT("  rim: wall %.2f, canyon cut %.0f m"), S.Wall, S.CutCm / 100.0));
		TestTrue(TEXT("the rim found is a canyon's"), S.CutCm > 0.01 * P.CanyonDepthCm);
	}
	else { AddError(TEXT("a desert world has canyon rims to find")); }
	FPlanetTerrainParams Airless = P;
	Airless.bAirless = true;
	T->Configure(Airless, nullptr);
	TestFalse(TEXT("a world without air has no badlands to find"), Find(TEXT("badlands"), Dir));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkPlanetInstancesPackingTest, "Slonk.Terrain.PlanetInstancesPacking", SLONK_TEST_FLAGS)
bool FSlonkPlanetInstancesPackingTest::RunTest(const FString& Parameters)
{
	// The GPU-only rocks and plants reach the writer shader (PlanetInstanceWriter.usf) as three float4 rows of a 3x4 and
	// their custom data: the rows must put every point where its transform does, and the custom data go in order.
	UPlanetInstancesComponent* C = NewObject<UPlanetInstancesComponent>();
	C->SetNumCustomDataFloats(SlonkScatter::NumCustomFloats);
	const TArray<FTransform> Xf = {
		FTransform(FRotator(12.0, 47.0, -31.0), FVector(1234.5, -678.0, 90.25), FVector(2.0, 0.5, 3.25)),
		FTransform(FRotator(-80.0, 170.0, 5.0), FVector(-50000.0, 25000.0, 12.0), FVector(11.0)) };
	TArray<float> Custom;
	for (int32 i = 0; i < Xf.Num() * SlonkScatter::NumCustomFloats; ++i) { Custom.Add((float)i * 0.5f + 1.f); }
	C->SetPieces(Xf, Custom);
	const int32 Custom4 = (SlonkScatter::NumCustomFloats + 3) / 4;
	const int32 Stride = 3 + Custom4;
	const TConstArrayView<FVector4f> P = C->GetPacked();
	TestEqual(TEXT("instances"), C->GetInstanceCount(), Xf.Num());
	TestEqual(TEXT("float4s, three rows and the custom data a piece"), P.Num(), Xf.Num() * Stride);
	if (P.Num() != Xf.Num() * Stride) { return true; }
	const FVector Points[] = { FVector::ZeroVector, FVector(100.0, 0.0, 0.0), FVector(-37.0, 250.0, 19.0) };
	double Worst = 0.0;
	bool bCustomInOrder = true;
	for (int32 i = 0; i < Xf.Num(); ++i)
	{
		const FVector4f R0 = P[i * Stride], R1 = P[i * Stride + 1], R2 = P[i * Stride + 2];
		for (const FVector& Q : Points)
		{
			// As the shader multiplies: the point as a row vector, the rows the basis, the w's the translation.
			const FVector Got(Q.X * R0.X + Q.Y * R1.X + Q.Z * R2.X + R0.W, Q.X * R0.Y + Q.Y * R1.Y + Q.Z * R2.Y + R1.W, Q.X * R0.Z + Q.Y * R1.Z + Q.Z * R2.Z + R2.W);
			Worst = FMath::Max(Worst, FVector::Dist(Got, Xf[i].TransformPosition(Q)));
		}
		for (int32 f = 0; f < Custom4 * 4; ++f)
		{
			const float Want = f < SlonkScatter::NumCustomFloats ? Custom[i * SlonkScatter::NumCustomFloats + f] : 0.f;
			bCustomInOrder &= P[i * Stride + 3 + f / 4][f % 4] == Want;
		}
	}
	AddInfo(FString::Printf(TEXT("worst point off its transform's place: %.4f cm"), Worst));
	TestTrue(TEXT("the rows put every point where its transform does"), Worst < 0.05);
	TestTrue(TEXT("the custom data in order, the last float4 padded with zeros"), bCustomInOrder);
	// Every corner of every instance's mesh box (with no mesh, 50 cm each way), turned and scaled as the instance is.
	const FBox Box = C->CalcBounds(FTransform::Identity).GetBox().ExpandBy(0.01);
	bool bHoldsAll = true;
	for (const FTransform& T : Xf)
	{
		for (int32 k = 0; k < 8; ++k) { bHoldsAll &= Box.IsInside(T.TransformPosition(FVector(k & 1 ? 50.0 : -50.0, k & 2 ? 50.0 : -50.0, k & 4 ? 50.0 : -50.0))); }
	}
	TestTrue(TEXT("the set's box holds every instance whole"), bHoldsAll);
	// The radius the renderer narrows the set's LODs by is its largest instance's, scaled, not the unit mesh's: at
	// least the farthest corner of the biggest instance's box from that instance's middle.
	double Corner = 0.0;
	for (const FTransform& T : Xf)
	{
		for (int32 k = 0; k < 8; ++k) { Corner = FMath::Max(Corner, FVector::Dist(T.TransformPosition(FVector(k & 1 ? 50.0 : -50.0, k & 2 ? 50.0 : -50.0, k & 4 ? 50.0 : -50.0)), T.GetLocation())); }
	}
	AddInfo(FString::Printf(TEXT("largest instance radius %.1f cm, farthest corner %.1f cm"), C->GetMaxInstanceRadius(), Corner));
	TestTrue(TEXT("the set's LOD radius is its largest instance's, scaled"), C->GetMaxInstanceRadius() >= Corner && C->GetMaxInstanceRadius() < Corner * 2.0);
	C->ClearPieces();
	TestEqual(TEXT("cleared"), C->GetInstanceCount(), 0);
	TestEqual(TEXT("nothing left to send"), C->GetPacked().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkPlantFarCrownsTest, "Slonk.Terrain.PlantFarCrowns", SLONK_TEST_FLAGS)
bool FSlonkPlantFarCrownsTest::RunTest(const FString& Parameters)
{
	// The forest's far levels are hundreds of thousands of trees a pixel or two across, drawn as each tree's far crown:
	// a handful of triangles, one solid shape wound outward and shaded as it faces (its faces lit from outside), as big
	// as the tree's own crown and no bigger. The tree meshes carry it as a fourth LOD; grass and bushes have none.
	// The bark and the cards show the side their normals point to (the foliage material is two-sided, so a part wound
	// the other way still draws, its faces seen from behind), so the near trees and their far crowns are made alike; a
	// grass blade's front is its side toward the sky (its normal leans up and out across a blade leaning outward, so no
	// side of it agrees: the side seen from above is the one lit by it).
	for (const EPlant Kind : { EPlant::Broadleaf, EPlant::Conifer, EPlant::Grass, EPlant::Shrub })
	{
		int32 Tris = 0; double Volume = 0.0, Facing = 0.0; FBox Box, Near;
		const bool bHas = SlonkPlants::FarCrownStats(Kind, Tris, Volume, Facing, Box, Near);
		const bool bTree = Kind == EPlant::Broadleaf || Kind == EPlant::Conifer;
		const FString Name = Kind == EPlant::Broadleaf ? TEXT("broadleaf") : Kind == EPlant::Conifer ? TEXT("conifer") : Kind == EPlant::Grass ? TEXT("grass") : TEXT("shrub");
		TestEqual(FString::Printf(TEXT("%s has a far crown only if a tree"), *Name), bHas, bTree);
		for (int32 Lod = 0; Lod < 3; ++Lod)
		{
			double BarkOut = 0.0, LeafOut = 0.0, LeafUp = 0.0;
			SlonkPlants::FacingShares(Kind, Lod, BarkOut, LeafOut, LeafUp);
			AddInfo(FString::Printf(TEXT("%s LOD%d: %.2f of bark faces and %.2f of foliage faces show their normals' side, %.2f of foliage faces look up"), *Name, Lod, BarkOut, LeafOut, LeafUp));
			TestTrue(FString::Printf(TEXT("%s LOD%d's bark is wound outward"), *Name, Lod), BarkOut >= 0.99);
			if (Kind == EPlant::Grass) { TestTrue(FString::Printf(TEXT("grass LOD%d's blades face the sky"), Lod), LeafUp >= 0.99); }
			else { TestTrue(FString::Printf(TEXT("%s LOD%d's cards face where their normals point"), *Name, Lod), LeafOut >= 0.9); }
		}
		// The meshes as the game draws them: a tree's fourth LOD is its far crown, from a screen size under its third's.
		if (UStaticMesh* M = SlonkPlants::GetMesh(Kind))
		{
			TestEqual(FString::Printf(TEXT("%s's LODs"), *Name), M->GetNumLODs(), bTree ? 4 : 3);
			const FStaticMeshRenderData* RD = M->GetRenderData();
			if (bTree && RD && RD->LODResources.Num() == 4)
			{
				TestEqual(FString::Printf(TEXT("%s's LOD3 is its far crown"), *Name), (int32)RD->LODResources[3].GetNumTriangles(), Tris);
				TestTrue(FString::Printf(TEXT("%s goes to its far crown from a screen size under its LOD2's, and one the engine tells from none"), *Name),
					RD->ScreenSize[3].Default < RD->ScreenSize[2].Default && RD->ScreenSize[3].Default >= 1.f / 1024.f);
			}
		}
		if (!bHas) { continue; }
		AddInfo(FString::Printf(TEXT("%s far crown: %d triangles, %.3f unit volume, worst facing %.2f, box %s against the tree's %s"), *Name, Tris, Volume, Facing, *Box.ToString(), *Near.ToString()));
		TestTrue(FString::Printf(TEXT("%s's far crown is a handful of triangles"), *Name), Tris > 0 && Tris <= 12);
		TestTrue(FString::Printf(TEXT("%s's far crown is wound outward"), *Name), Volume > 0.02);
		TestTrue(FString::Printf(TEXT("%s's far crown is shaded as it faces"), *Name), Facing > 0.0);
		TestTrue(FString::Printf(TEXT("%s's far crown is within the tree"), *Name), Near.ExpandBy(3.0).IsInside(Box));
		TestTrue(FString::Printf(TEXT("%s's far crown is most of the tree's height and width"), *Name), Box.GetSize().Z > 0.5 * Near.GetSize().Z && Box.GetSize().X > 0.5 * Near.GetSize().X);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkHorizonReachTest, "Slonk.Terrain.HorizonReach", SLONK_TEST_FLAGS)
bool FSlonkHorizonReachTest::RunTest(const FString& Parameters)
{
	// Each ground detail step reaches further than the one before, and Horizon carries the trees past
	// SlonkScatter::HorizonTreeReachCm on worlds of every size (their chunk edges halve from the face's, so a far level
	// lands between half and all of its limit), but not twice past it: the levels it takes are the ones the world needs.
	// The largest chunk carrying anything only grows with the steps, so a rebuild at the wider of two covers both.
	FScatterSettings S;
	S.FadeEdges = 3.0;
	for (const double RadiusKm : { 1000.0, 2500.0, 3700.0, 5707.0, 6371.0, 6746.0, 7804.0, 9500.0 })
	{
		const double FaceEdge = SlonkScatter::FaceEdgeCm(RadiusKm * 1e5);
		double Was = 0.0, WasEdge = 0.0;
		for (int32 Step = 0; Step <= SlonkScatter::HorizonStep; ++Step)
		{
			S.MaxFarLevels = Step;
			const double Reach = SlonkScatter::TreeReachCm(S, FaceEdge);
			const int32 Levels = SlonkScatter::TreeFarLevels(S, FaceEdge);
			const double MaxEdge = SlonkScatter::MaxChunkEdgeCm(S, FaceEdge);
			TestTrue(FString::Printf(TEXT("R %.0f km: step %d reaches further than the one before"), RadiusKm, Step), Reach > Was);
			TestTrue(FString::Printf(TEXT("R %.0f km: step %d's scatter rides chunks no smaller than the step before's"), RadiusKm, Step), MaxEdge >= WasEdge);
			if (Step == SlonkScatter::HorizonStep)
			{
				AddInfo(FString::Printf(TEXT("R %.0f km: Horizon's trees reach %.1f km on %d far levels (Farthest's %.1f km)"), RadiusKm, Reach / 1e5, Levels, Was / 1e5));
				TestTrue(FString::Printf(TEXT("R %.0f km: Horizon's trees reach past %.0f km, and not twice past it"), RadiusKm, SlonkScatter::HorizonTreeReachCm / 1e5),
					Reach >= SlonkScatter::HorizonTreeReachCm && Reach < SlonkScatter::HorizonTreeReachCm * 2.0);
			}
			else { TestEqual(FString::Printf(TEXT("R %.0f km: step %d takes that many far levels"), RadiusKm, Step), Levels, Step); }
			Was = Reach;
			WasEdge = MaxEdge;
		}
	}

	// Every chunk of a world takes the same far levels. On a 9,500 km world Horizon's trees take five, so the chunks
	// six levels above the trees' own (the depth whose chunks run 10-15 km) carry none, even towards a face's edges,
	// where a chunk's own edge runs to 0.7 of its depth's share of the face's and would on its own have asked for a
	// sixth; their children, five levels above, carry the forest.
	FPlanetTerrainParams P = RockWorld();
	P.RadiusCm = 9.5e8;
	FScatterSettings H = Living();
	H.FadeEdges = 3.0;
	H.MaxFarLevels = SlonkScatter::HorizonStep;
	TestEqual(TEXT("a 9,500 km world's trees take five far levels on Horizon"), SlonkScatter::TreeFarLevels(H, SlonkScatter::FaceEdgeCm(P.RadiusCm)), 5);
	const int32 D = DepthFor(P, 31000.0 * 32.0) - 1;
	const double Cells = (double)((int64)1 << D);
	int32 Tried = 0, OnCoarse = 0, OnKids = 0;
	TSet<FIntVector> Seen;
	for (const FVector& Dir : GreenSpots(P, 400))
	{
		int32 Face; int64 X, Y;
		if (!UPlanetTerrainComponent::ChunkAt(Dir, D, Face, X, Y)) { continue; }
		// Towards the face's edge across the chunk's rows, where its edge runs short.
		if (FMath::Abs(-1.0 + 2.0 * (Y + 0.5) / Cells) < 0.7 || Seen.Contains(FIntVector(Face, (int32)X, (int32)Y))) { continue; }
		Seen.Add(FIntVector(Face, (int32)X, (int32)Y));
		int32 Kf; int64 Kx, Ky;
		if (!UPlanetTerrainComponent::ChunkAt(Dir, D + 1, Kf, Kx, Ky)) { continue; }
		const FRocks Kid = Build(P, H, Kf, D + 1, Kx, Ky);
		const int32 KidTrees = Kid.Xf[TreeMeshes[0]].Num() + Kid.Xf[TreeMeshes[1]].Num();
		if (KidTrees == 0) { continue; }
		const FRocks Coarse = Build(P, H, Face, D, X, Y);
		++Tried;
		OnKids += KidTrees;
		OnCoarse += Coarse.Xf[TreeMeshes[0]].Num() + Coarse.Xf[TreeMeshes[1]].Num();
		if (Tried >= 4) { break; }
	}
	AddInfo(FString::Printf(TEXT("9,500 km world, Horizon: %d wooded chunks near a face's edge at depth %d; their children carry %d trees, the chunks themselves %d"), Tried, D, OnKids, OnCoarse));
	TestTrue(TEXT("found wooded chunks near a face's edge"), Tried >= 2);
	TestEqual(TEXT("a chunk six levels above the trees' own carries none on a world whose trees take five"), OnCoarse, 0);
	return true;
}

#endif
