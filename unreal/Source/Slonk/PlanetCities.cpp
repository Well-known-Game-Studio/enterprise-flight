#include "PlanetCities.h"

namespace
{
	// The terrain shader's hash, cell by cell, so the towns stand under their own lights.
	uint32 CellHash(const FIntVector& C, uint32 Salt)
	{
		uint32 H = (uint32)(C.X & 4095) * 73856093u ^ (uint32)(C.Y & 4095) * 19349663u ^ (uint32)(C.Z & 4095) * 83492791u ^ Salt;
		H = (H ^ (H >> 13)) * 0x5bd1e995u; H ^= H >> 15;
		return H;
	}
	constexpr double LevelCm[3] = { 2560000.0, 640000.0, 160000.0 };
	constexpr uint32 LevelKeep[3] = { 3u, 3u, 3u };			// of four cells carry a point
	uint32 LevelSalt(int32 Level) { return 0x9e3779b9u + (uint32)Level * 0x85ebca6bu; }
	FVector Jitter(uint32 H) { return FVector((H & 0x3ffu) / 1023.0, ((H >> 10) & 0x3ffu) / 1023.0, ((H >> 20) & 0x3ffu) / 1023.0); }
	float SmoothStep(float A, float B, float X) { return FMath::SmoothStep(A, B, X); }

	/** Squared distance (cell units) from a point to the nearest present point of a level, as the shader measures it. */
	double NearestSq(const FVector& LocalCm, int32 Level)
	{
		const double Cell = LevelCm[Level];
		const FVector Q = LocalCm / Cell - FVector(0.5, 0.5, 0.5);
		const FVector Fl(FMath::Floor(Q.X), FMath::Floor(Q.Y), FMath::Floor(Q.Z));
		const FVector Fr = Q - Fl;
		double Best = 9.0;
		for (int32 i = 0; i < 8; ++i)
		{
			const FIntVector Off(i & 1, (i >> 1) & 1, (i >> 2) & 1);
			const FIntVector C((int32)Fl.X + Off.X, (int32)Fl.Y + Off.Y, (int32)Fl.Z + Off.Z);
			const uint32 H = CellHash(C, LevelSalt(Level));
			if ((H >> 30) >= LevelKeep[Level]) { continue; }
			const FVector Dp = FVector(Off) + Jitter(H) - Fr;
			Best = FMath::Min(Best, Dp.SizeSquared());
		}
		return Best;
	}
	float Hash01(uint32 H) { return (H & 0xffffu) / 65535.f; }
	uint32 Mix(uint32 A, uint32 B) { uint32 H = A ^ (B + 0x9e3779b9u + (A << 6) + (A >> 2)); H = (H ^ (H >> 13)) * 0x5bd1e995u; return H ^ (H >> 15); }
}

float SlonkCities::SettlementAt(const FVector& LocalCm)
{
	const float Region = 1.f - SmoothStep(0.05f, 0.7f, (float)NearestSq(LocalCm, 0));
	const float Cluster = 1.f - SmoothStep(0.02f, 0.5f, (float)NearestSq(LocalCm, 1));
	const float Town = 1.f - SmoothStep(0.03f, 0.3f, (float)NearestSq(LocalCm, 2));
	return Region * Cluster * Town;
}

void SlonkCities::EnumerateTowns(const FPlanetTerrainParams& P, const FVector& CentreLocalCm, double ReachCm, TArray<FTownSite>& Out, int32 MaxTowns)
{
	const double Cell = LevelCm[2];
	const double R = P.RadiusCm;
	struct FCand { FVector LocalCm; float Strength; uint32 Seed; double DistSq; };
	TArray<FCand> Cands;
	// Walk the cells along the sphere, not through a box of them: columns over the two axes the centre direction
	// is least aligned with, and in each column only the few cells about where the sphere passes.
	const FVector CDir = CentreLocalCm.GetSafeNormal();
	int32 Axis = 0; if (FMath::Abs(CDir.Y) > FMath::Abs(CDir[Axis])) { Axis = 1; } if (FMath::Abs(CDir.Z) > FMath::Abs(CDir[Axis])) { Axis = 2; }
	const int32 A1 = (Axis + 1) % 3, A2 = (Axis + 2) % 3;
	const double Sign = CDir[Axis] >= 0.0 ? 1.0 : -1.0;
	const int32 N = FMath::CeilToInt(ReachCm / Cell) + 1;
	// Cell c spans [(c + 0.5) Cell, (c + 1.5) Cell) in q-space shifted by half a cell; its point sits at (c + jitter + 0.5) Cell.
	const int32 C1 = (int32)FMath::Floor(CentreLocalCm[A1] / Cell - 0.5), C2 = (int32)FMath::Floor(CentreLocalCm[A2] / Cell - 0.5);
	for (int32 j = -N; j <= N; ++j)
	for (int32 i = -N; i <= N; ++i)
	{
		const double X1 = (C1 + i + 1.0) * Cell, X2 = (C2 + j + 1.0) * Cell;	// the column's centre
		const double Rho2 = X1 * X1 + X2 * X2;
		if (Rho2 >= R * R) { continue; }
		const double Sa = Sign * FMath::Sqrt(R * R - Rho2);					// where the sphere crosses this column
		const int32 Ca = (int32)FMath::Floor(Sa / Cell - 1.0);
		for (int32 k = -1; k <= 1; ++k)
		{
			FIntVector C; C[Axis] = Ca + k; C[A1] = C1 + i; C[A2] = C2 + j;
			const uint32 H = CellHash(C, LevelSalt(2));
			if ((H >> 30) >= LevelKeep[2]) { continue; }
			const FVector Pt = (FVector(C) + Jitter(H) + FVector(0.5, 0.5, 0.5)) * Cell;
			if (FMath::Abs(Pt.Size() - R) > Cell * 0.9) { continue; }
			const double DistSq = FVector::DistSquared(Pt, CentreLocalCm);
			if (DistSq > ReachCm * ReachCm) { continue; }
			const float Region = 1.f - SmoothStep(0.05f, 0.7f, (float)NearestSq(Pt, 0));
			const float Cluster = 1.f - SmoothStep(0.02f, 0.5f, (float)NearestSq(Pt, 1));
			const float Strength = Region * Cluster;
			if (Strength < 0.3f) { continue; }
			Cands.Add({ Pt, Strength, H, DistSq });
		}
	}
	Cands.Sort([](const FCand& A, const FCand& B) { return A.DistSq < B.DistSq; });
	int32 NWater = 0, NDry = 0, NHigh = 0, NSteep = 0;
	for (const FCand& Cd : Cands)
	{
		if (Out.Num() >= MaxTowns) { break; }
		// The shader's land gating: dry, wet enough, low, and level (two samples 200 m apart).
		const FVector Dir = Cd.LocalCm.GetSafeNormal();
		const FPlanetTerrainSample S = SlonkTerrain::SampleStructure(P, Dir);
		if (S.bWater) { ++NWater; continue; }
		if (S.Wet < 0.3f) { ++NDry; continue; }
		if (S.HeightCm > 200000.0 || S.Canyon > 0.3f) { ++NHigh; continue; }
		const FVector East = FVector::CrossProduct(FVector::ZAxisVector, Dir).GetSafeNormal();
		const FVector North = FVector::CrossProduct(Dir, East);
		const double Step = 20000.0;
		const double HE = SlonkTerrain::SampleStructure(P, (Dir * R + East * Step).GetSafeNormal()).HeightCm;
		const double HN = SlonkTerrain::SampleStructure(P, (Dir * R + North * Step).GetSafeNormal()).HeightCm;
		const double Slope = FMath::Sqrt(FMath::Square(HE - S.HeightCm) + FMath::Square(HN - S.HeightCm)) / Step;
		if (Slope > 0.25) { ++NSteep; continue; }
		FTownSite T;
		T.Dir = Dir; T.LocalCm = Dir * R; T.Strength = Cd.Strength * (1.f - (float)FMath::Clamp(Slope / 0.25, 0.0, 0.6)); T.Seed = Cd.Seed;
		// A hamlet at the weak end, a city two and a half kilometres across at the strong end.
		T.RadiusCm = Cell * (0.3 + 1.2 * Cd.Strength);
		Out.Add(T);
	}
	UE_LOG(LogTemp, Verbose, TEXT("EnumerateTowns: %d candidates within %.0f km, %d towns (water %d, dry %d, high %d, steep %d)"), Cands.Num(), ReachCm / 1e5, Out.Num(), NWater, NDry, NHigh, NSteep);

}

void SlonkCities::BuildTown(const FPlanetTerrainParams& P, const FTownSite& Town, TArray<FTransform>& Out, int32 MaxBuildings)
{
	const double R = P.RadiusCm;
	const FVector Up = Town.Dir;
	FVector East = FVector::CrossProduct(FVector::ZAxisVector, Up).GetSafeNormal();
	if (East.IsNearlyZero()) { East = FVector::XAxisVector; }
	const FVector North = FVector::CrossProduct(Up, East).GetSafeNormal();
	// The street grid's bearing and its pitch from the seed.
	const float Angle = Hash01(Mix(Town.Seed, 11u)) * PI;
	const FVector GX = East * FMath::Cos(Angle) + North * FMath::Sin(Angle);
	const FVector GY = FVector::CrossProduct(Up, GX);
	const double Pitch = 3600.0 + 1400.0 * Hash01(Mix(Town.Seed, 17u));		// 36 to 50 m lots
	const int32 Half = FMath::CeilToInt(Town.RadiusCm / Pitch);
	const FQuat Base = FRotationMatrix::MakeFromZX(Up, GX).ToQuat();
	int32 Count = 0;
	for (int32 gy = -Half; gy <= Half && Count < MaxBuildings; ++gy)
	for (int32 gx = -Half; gx <= Half && Count < MaxBuildings; ++gx)
	{
		const uint32 H = Mix(Town.Seed, (uint32)(gx + 4096) * 65537u + (uint32)(gy + 4096));
		const double D2 = (double)(gx * gx + gy * gy) * Pitch * Pitch / FMath::Max(Town.RadiusCm * Town.RadiusCm, 1.0);
		if (D2 > 1.0) { continue; }
		// Blocks are full in the middle and thin toward the edge; every fifth and seventh row is a wider street.
		const float Density = FMath::Min(1.f, 0.45f + Town.Strength) * (float)(1.0 - D2 * D2);
		if (Hash01(H) > Density) { continue; }
		if ((gx % 5 == 0) || (gy % 7 == 0)) { continue; }
		const FVector Jit((Hash01(Mix(H, 3u)) - 0.5) * 0.3 * Pitch, (Hash01(Mix(H, 5u)) - 0.5) * 0.3 * Pitch, 0.0);
		const FVector Flat = GX * (gx * Pitch + Jit.X) + GY * (gy * Pitch + Jit.Y);
		const FVector Dir = (Town.LocalCm + Flat).GetSafeNormal();
		const FPlanetTerrainSample S = SlonkTerrain::Sample(P, Dir, 400.0);
		if (S.bWater) { continue; }
		const double W = Pitch * (0.5 + 0.4 * Hash01(Mix(H, 7u)));
		const double Dp = Pitch * (0.5 + 0.4 * Hash01(Mix(H, 9u)));
		// Towers in the middle of a strong town, houses at the edge of a weak one.
		const double Core = FMath::Square(1.0 - D2) * Town.Strength;
		const double Hgt = (500.0 + 15000.0 * Core * Core + 2500.0 * Core) * (0.5 + 1.0 * Hash01(Mix(H, 13u)));
		const double Sink = 150.0;
		const FVector Loc = Dir * (R + S.HeightCm - Sink + Hgt * 0.5);
		const FQuat Rot = FRotationMatrix::MakeFromZX(Dir, GX - Dir * FVector::DotProduct(GX, Dir)).ToQuat();
		Out.Emplace(Rot, Loc, FVector(W / 100.0, Dp / 100.0, (Hgt + Sink) / 100.0));
		++Count;
	}
}
