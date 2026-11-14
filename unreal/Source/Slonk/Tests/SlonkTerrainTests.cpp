// Slonk - automation tests for the rocks and plants on the ground (TerrainScatter.h), built the way a chunk build
// builds them, without a world. Under "Slonk.Terrain."; tools/run_tests.sh runs them headless.

#include "Misc/AutomationTest.h"
#include "../PlanetTerrain.h"
#include "../PlanetTerrainComponent.h"
#include "../PlanetCities.h"
#include "../TerrainScatter.h"

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
		int32 NumPlants() const { int32 N = 0; for (int32 M = SlonkScatter::FirstPlant; M < SlonkScatter::NumMeshes; ++M) { N += Xf[M].Num(); } return N; }
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
		SlonkCities::EnumerateTowns(P, Dir * P.RadiusCm, 3.0e7, Towns, 8);
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
		const double Earth = P.RadiusCm / 6.371e8;
		P.CanyonDepthCm = 1.6e5 * Earth;
		P.CanyonWidthCm = 7.0e5 * Earth;
		P.SnowlineCm = 5.0e5;
		return P;
	}
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

#endif
