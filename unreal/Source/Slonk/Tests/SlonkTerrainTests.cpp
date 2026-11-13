// Slonk - automation tests for the rocks on the ground (TerrainScatter.h), built the way a chunk build builds them,
// without a world. Under "Slonk.Terrain."; tools/run_tests.sh runs them headless.

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

	/** The coarsest depth whose chunks carry boulders. */
	int32 BoulderDepth(const FPlanetTerrainParams& P)
	{
		return FMath::CeilToInt32(FMath::Log2(P.RadiusCm * UE_DOUBLE_HALF_PI / SlonkScatter::MaxChunkEdgeCm()));
	}

	struct FRocks
	{
		TArray<FTransform> Xf[SlonkGroundRocks::NumKinds];
		TArray<double> Drawn[SlonkGroundRocks::NumKinds];
		int32 Num() const { int32 N = 0; for (const TArray<FTransform>& T : Xf) { N += T.Num(); } return N; }
	};
	FRocks Build(const FPlanetTerrainParams& P, const FScatterSettings& S, int32 Face, int32 Depth, int64 X, int64 Y)
	{
		FRocks R;
		UPlanetTerrainComponent::BuildChunkScatterForTest(P, S, Face, Depth, X, Y, 64, R.Xf, R.Drawn);
		return R;
	}
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
		for (int32 K = 0; K < SlonkGroundRocks::NumKinds; ++K)
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
			for (int32 K = 0; K < SlonkGroundRocks::NumKinds; ++K)
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

#endif
