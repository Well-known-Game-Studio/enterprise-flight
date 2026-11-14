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

namespace
{
	/** Present points of one Worley level within reach of a planet-local point, walking the cells along the sphere. */
	struct FPoint { FVector LocalCm; uint32 Seed; double DistSq; };
	void WalkLevel(int32 Level, double R, const FVector& CentreLocalCm, double ReachCm, TArray<FPoint>& Out)
	{
		const double Cell = LevelCm[Level];
		const FVector CDir = CentreLocalCm.GetSafeNormal();
		int32 Axis = 0; if (FMath::Abs(CDir.Y) > FMath::Abs(CDir[Axis])) { Axis = 1; } if (FMath::Abs(CDir.Z) > FMath::Abs(CDir[Axis])) { Axis = 2; }
		const int32 A1 = (Axis + 1) % 3, A2 = (Axis + 2) % 3;
		const double Sign = CDir[Axis] >= 0.0 ? 1.0 : -1.0;
		const int32 N = FMath::CeilToInt(ReachCm / Cell) + 1;
		const int32 C1 = (int32)FMath::Floor(CentreLocalCm[A1] / Cell - 0.5), C2 = (int32)FMath::Floor(CentreLocalCm[A2] / Cell - 0.5);
		for (int32 j = -N; j <= N; ++j)
		for (int32 i = -N; i <= N; ++i)
		{
			const double X1 = (C1 + i + 1.0) * Cell, X2 = (C2 + j + 1.0) * Cell;
			const double Rho2 = X1 * X1 + X2 * X2;
			if (Rho2 >= R * R) { continue; }
			const double Sa = Sign * FMath::Sqrt(R * R - Rho2);
			const int32 Ca = (int32)FMath::Floor(Sa / Cell - 1.0);
			for (int32 k = -1; k <= 1; ++k)
			{
				FIntVector C; C[Axis] = Ca + k; C[A1] = C1 + i; C[A2] = C2 + j;
				const uint32 H = CellHash(C, LevelSalt(Level));
				if ((H >> 30) >= LevelKeep[Level]) { continue; }
				const FVector Pt = (FVector(C) + Jitter(H) + FVector(0.5, 0.5, 0.5)) * Cell;
				if (FMath::Abs(Pt.Size() - R) > Cell * 0.9) { continue; }
				const double DistSq = FVector::DistSquared(Pt, CentreLocalCm);
				if (DistSq > ReachCm * ReachCm) { continue; }
				Out.Add({ Pt, H, DistSq });
			}
		}
	}
	/** The shader's land gating at a point: dry-shod, wet enough for the people (a desert people needs no wet ground),
	 *  low, level. Returns the slope penalty factor, or -1 to reject. */
	float LandFactor(const FPlanetTerrainParams& P, const FCivilization& Civ, const FVector& Dir, int32& NWater, int32& NDry, int32& NHigh, int32& NSteep)
	{
		const FPlanetTerrainSample S = SlonkTerrain::SampleStructure(P, Dir);
		if (S.bWater) { ++NWater; return -1.f; }
		if (S.Wet < Civ.WetMin) { ++NDry; return -1.f; }
		if (S.HeightCm > 200000.0 || S.Canyon > 0.3f) { ++NHigh; return -1.f; }
		const double R = P.RadiusCm;
		const FVector East = FVector::CrossProduct(FVector::ZAxisVector, Dir).GetSafeNormal();
		const FVector North = FVector::CrossProduct(Dir, East);
		const double Step = 20000.0;
		const double HE = SlonkTerrain::SampleStructure(P, (Dir * R + East * Step).GetSafeNormal()).HeightCm;
		const double HN = SlonkTerrain::SampleStructure(P, (Dir * R + North * Step).GetSafeNormal()).HeightCm;
		const double Slope = FMath::Sqrt(FMath::Square(HE - S.HeightCm) + FMath::Square(HN - S.HeightCm)) / Step;
		if (Slope > 0.25) { ++NSteep; return -1.f; }
		return 1.f - (float)FMath::Clamp(Slope / 0.25, 0.0, 0.6);
	}

	/** A direction this many cm along the ground from Dir, along a tangent. */
	FORCEINLINE FVector Along(const FPlanetTerrainParams& P, const FVector& Dir, const FVector& Tangent, double Cm)
	{
		return (Dir * P.RadiusCm + Tangent * Cm).GetSafeNormal();
	}

	/**
	 * The best cliff within SearchCm of Dir: a scarp's or a canyon's wall (the sample's Wall) that drops at least
	 * 80 m over 300 m across it, the biggest drop of a grid of samples. OutFace is the way it faces: the level
	 * direction downhill, out from the rock. False where there is none.
	 */
	bool FindCliff(const FPlanetTerrainParams& P, const FVector& Dir, double SearchCm, FVector& OutDir, FVector& OutFace)
	{
		// Walls are only where the tablelands are (their scarps, their canyons): none elsewhere to look for.
		if (SlonkTerrain::SampleStructure(P, Dir, 2.0e5).Table < 0.05f) { return false; }
		const FVector East = FVector::CrossProduct(FMath::Abs(Dir.Z) < 0.9 ? FVector::ZAxisVector : FVector::XAxisVector, Dir).GetSafeNormal();
		const FVector North = FVector::CrossProduct(Dir, East);
		// A grid over the search, the four most wall-like of it measured.
		TArray<TPair<float, FVector>> Walls;
		for (int32 j = -3; j <= 3; ++j)
		{
			for (int32 i = -3; i <= 3; ++i)
			{
				const FVector D = (Dir * P.RadiusCm + East * (i * SearchCm / 3.0) + North * (j * SearchCm / 3.0)).GetSafeNormal();
				const FPlanetTerrainSample S = SlonkTerrain::SampleStructure(P, D, 2.0e4);
				if (!S.bWater && S.Wall >= 0.5f) { Walls.Add({ S.Wall, D }); }
			}
		}
		Walls.Sort([](const TPair<float, FVector>& A, const TPair<float, FVector>& B) { return A.Key > B.Key; });
		double BestDrop = 8000.0;
		bool bFound = false;
		for (int32 w = 0; w < FMath::Min(Walls.Num(), 4); ++w)
		{
			const FVector D = Walls[w].Value;
			// The way down: the gradient from samples 150 m off along two tangents.
			const FVector E = FVector::CrossProduct(FMath::Abs(D.Z) < 0.9 ? FVector::ZAxisVector : FVector::XAxisVector, D).GetSafeNormal();
			const FVector Nn = FVector::CrossProduct(D, E);
			const double Ge = SlonkTerrain::SampleStructure(P, Along(P, D, E, 15000.0), 2.0e4).HeightCm - SlonkTerrain::SampleStructure(P, Along(P, D, E, -15000.0), 2.0e4).HeightCm;
			const double Gn = SlonkTerrain::SampleStructure(P, Along(P, D, Nn, 15000.0), 2.0e4).HeightCm - SlonkTerrain::SampleStructure(P, Along(P, D, Nn, -15000.0), 2.0e4).HeightCm;
			const FVector Down = -(E * Ge + Nn * Gn);
			if (Down.IsNearlyZero()) { continue; }
			const FVector Face = Down.GetSafeNormal();
			const double Drop = SlonkTerrain::SampleStructure(P, Along(P, D, Face, -15000.0), 2.0e4).HeightCm - SlonkTerrain::SampleStructure(P, Along(P, D, Face, 15000.0), 2.0e4).HeightCm;
			if (Drop > BestDrop) { BestDrop = Drop; OutDir = D; OutFace = Face; bFound = true; }
		}
		return bFound;
	}

	/** One building: its kind, footprint (W across, D deep, cm), height (cm, above Sink), facing. */
	void Place(TArray<FTransform>* OutByKind, EBuildingKind Kind, const FVector& Dir, const FVector& Facing, double R, double BaseCm, double W, double D, double Hgt, double Sink)
	{
		const FVector Fx = (Facing - Dir * FVector::DotProduct(Facing, Dir)).GetSafeNormal();
		const FQuat Rot = FRotationMatrix::MakeFromZX(Dir, Fx.IsNearlyZero() ? FVector::CrossProduct(Dir, FVector::ZAxisVector).GetSafeNormal() : Fx).ToQuat();
		const FVector Loc = Dir * (R + BaseCm - Sink + (Hgt + Sink) * 0.5);
		OutByKind[(int32)Kind].Emplace(Rot, Loc, FVector(W / 100.0, D / 100.0, (Hgt + Sink) / 100.0));
	}

	/** Ground a building can stand on: dry, not a wall, not steep. */
	bool Buildable(const FPlanetTerrainSample& S) { return !S.bWater && S.Wall <= 0.3f && S.Slope <= 0.5f; }
}

// Radii: a city at a cluster point grows with the region weight there; a town at a town point with region x cluster.
// Towns inside a city's radius are the city's own blocks and are not built again (overlapping grids made a carpet).
double SlonkCities::CityRadiusCm(float Region) { return LevelCm[1] * (0.25 + 0.2 * Region); }
double SlonkCities::TownRadiusCm(float Strength) { return LevelCm[2] * (0.2 + 0.3 * Strength); }

void SlonkCities::EnumerateTowns(const FPlanetTerrainParams& P, const FCivilization& Civ, const FVector& CentreLocalCm, double ReachCm, TArray<FTownSite>& Out, int32 MaxTowns)
{
	const double R = P.RadiusCm;
	int32 NWater = 0, NDry = 0, NHigh = 0, NSteep = 0, NCliff = 0;
	// A point the people do not live at (their density thins the field), one they build into a cliff near it (their
	// liking for cliffs, where one is), or one on open land the shader would light.
	auto Site = [&](const FVector& LocalCm, uint32 Seed, double RadiusCm, float Strength, bool bCity, FTownSite& T) -> bool
	{
		if (Hash01(Mix(Seed, 0xD5u)) > Civ.Density) { return false; }
		const FVector Dir = LocalCm.GetSafeNormal();
		T.Seed = Seed; T.bCity = bCity;
		if (Hash01(Mix(Seed, 0xC1u)) < Civ.CliffAffinity)
		{
			FVector CDir, Face;
			if (FindCliff(P, Dir, FMath::Max(RadiusCm, 80000.0), CDir, Face))
			{
				// Along the wall, not round a centre: a few hundred metres of ledges either side.
				T.Dir = CDir; T.LocalCm = CDir * R; T.Face = Face; T.bCliff = true;
				T.RadiusCm = FMath::Clamp(RadiusCm * 0.6, 12000.0, 60000.0);
				T.Strength = Strength;
				++NCliff;
				return true;
			}
		}
		const float Land = LandFactor(P, Civ, Dir, NWater, NDry, NHigh, NSteep);
		if (Land < 0.f) { return false; }
		T.Dir = Dir; T.LocalCm = Dir * R; T.RadiusCm = RadiusCm; T.Strength = Strength * Land; T.bCliff = false; T.Face = FVector::ZeroVector;
		return true;
	};
	// Cities first: the cluster points, by distance.
	TArray<FPoint> Clusters; WalkLevel(1, R, CentreLocalCm, ReachCm + LevelCm[1], Clusters);
	Clusters.Sort([](const FPoint& A, const FPoint& B) { return A.DistSq < B.DistSq; });
	TArray<FTownSite> Cities;
	for (const FPoint& C : Clusters)
	{
		const float Region = 1.f - SmoothStep(0.05f, 0.7f, (float)NearestSq(C.LocalCm, 0));
		if (Region < 0.35f) { continue; }
		FTownSite T;
		if (!Site(C.LocalCm, C.Seed, CityRadiusCm(Region), FMath::Min(1.f, 0.5f + Region * 0.6f), true, T)) { continue; }
		Cities.Add(T);
	}
	// Then the towns, skipping any inside a city.
	TArray<FPoint> Towns; WalkLevel(2, R, CentreLocalCm, ReachCm, Towns);
	Towns.Sort([](const FPoint& A, const FPoint& B) { return A.DistSq < B.DistSq; });
	for (const FTownSite& C : Cities) { if (C.LocalCm.SizeSquared() > 0.0 && FVector::DistSquared(C.LocalCm, CentreLocalCm) <= ReachCm * ReachCm && Out.Num() < MaxTowns) { Out.Add(C); } }
	for (const FPoint& Tp : Towns)
	{
		if (Out.Num() >= MaxTowns) { break; }
		const float Region = 1.f - SmoothStep(0.05f, 0.7f, (float)NearestSq(Tp.LocalCm, 0));
		const float Cluster = 1.f - SmoothStep(0.02f, 0.5f, (float)NearestSq(Tp.LocalCm, 1));
		const float Strength = Region * Cluster;
		if (Strength < 0.3f) { continue; }
		bool bInCity = false;
		for (const FTownSite& C : Cities)
		{
			if (FVector::DistSquared(C.LocalCm, Tp.LocalCm) < FMath::Square(C.RadiusCm + TownRadiusCm(Strength) + 30000.0)) { bInCity = true; break; }
		}
		if (bInCity) { continue; }
		FTownSite T;
		if (!Site(Tp.LocalCm, Tp.Seed, TownRadiusCm(Strength), Strength * 0.6f, false, T)) { continue; }
		Out.Add(T);
	}
	UE_LOG(LogTemp, Verbose, TEXT("EnumerateTowns: %d cities, %d sites within %.0f km (cliff %d; water %d, dry %d, high %d, steep %d)"), Cities.Num(), Out.Num(), ReachCm / 1e5, NCliff, NWater, NDry, NHigh, NSteep);
}

namespace
{
	/** The town's frame on the ground: up, and the grid's two axes at a bearing from the seed. */
	struct FTownFrame
	{
		FVector Up, GX, GY;
		explicit FTownFrame(const FTownSite& Town)
		{
			Up = Town.Dir;
			FVector East = FVector::CrossProduct(FVector::ZAxisVector, Up).GetSafeNormal();
			if (East.IsNearlyZero()) { East = FVector::XAxisVector; }
			const FVector North = FVector::CrossProduct(Up, East).GetSafeNormal();
			const float Angle = Hash01(Mix(Town.Seed, 11u)) * PI;
			GX = East * FMath::Cos(Angle) + North * FMath::Sin(Angle);
			GY = FVector::CrossProduct(Up, GX);
		}
	};

	/** The grid towns of the industrial, modern and starfaring built peoples: blocks between streets, as the terrain
	 *  material paves them. */
	void BuildGridTown(const FPlanetTerrainParams& P, const FCivilization& Civ, const FTownSite& Town, TArray<FTransform>* OutByKind, int32 MaxBuildings)
	{
		const double R = P.RadiusCm;
		const FTownFrame F(Town);
		const double Pitch = 3600.0 + 1400.0 * Hash01(Mix(Town.Seed, 17u));		// 36 to 50 m lots
		const int32 Half = FMath::CeilToInt(Town.RadiusCm / Pitch);
		const FQuat Base = FRotationMatrix::MakeFromZX(F.Up, F.GX).ToQuat();
		int32 Count = 0;
		// A strong town has a landing pad at its heart: a 90 m slab half a metre proud of the ground, the lots around
		// it left clear. The material reads the slab's height (under a metre) to paint it as a pad. Not before the
		// modern age.
		const bool bPad = Town.bCity && Civ.Age >= ETechAge::Modern;
		if (bPad)
		{
			const FPlanetTerrainSample S0 = SlonkTerrain::Sample(P, F.Up, 400.0);
			if (!S0.bWater)
			{
				OutByKind[(int32)EBuildingKind::Block].Emplace(Base, F.Up * (R + S0.HeightCm - 150.0 + 100.0), FVector(90.0, 90.0, 2.0));
				++Count;
			}
		}
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
			if (bPad && FMath::Abs(gx) * Pitch < 7000.0 && FMath::Abs(gy) * Pitch < 7000.0) { continue; }
			const FVector Jit((Hash01(Mix(H, 3u)) - 0.5) * 0.3 * Pitch, (Hash01(Mix(H, 5u)) - 0.5) * 0.3 * Pitch, 0.0);
			const FVector Flat = F.GX * (gx * Pitch + Jit.X) + F.GY * (gy * Pitch + Jit.Y);
			const FVector Dir = (Town.LocalCm + Flat).GetSafeNormal();
			const FPlanetTerrainSample S = SlonkTerrain::Sample(P, Dir, 400.0);
			// No building in the water, or on a cliff: a town at a scarp's or a canyon's rim ends at the edge rather
			// than standing on stilts down the face.
			if (!Buildable(S)) { continue; }
			double W = Pitch * (0.5 + 0.4 * Hash01(Mix(H, 7u)));
			double Dp = Pitch * (0.5 + 0.4 * Hash01(Mix(H, 9u)));
			// Towers in the middle of a strong town, houses at the edge of a weak one; an industrial town keeps low.
			const double Core = FMath::Square(1.0 - D2) * Town.Strength;
			const double Tall = Civ.Age == ETechAge::Industrial ? 0.3 : 1.0;
			const double Hgt = (500.0 + 15000.0 * Tall * Core * Core + 2500.0 * Core) * (0.5 + 1.0 * Hash01(Mix(H, 13u)));
			const float Pick = Hash01(Mix(H, 19u));
			EBuildingKind Kind = EBuildingKind::Block;
			switch (Civ.Age)
			{
			case ETechAge::Industrial:
				// Brick terraces and halls; a works with its chimney in one lot in six, broader than the rest.
				if (Pick < 0.17f) { Kind = EBuildingKind::Works; W *= 1.3; Dp *= 1.3; }
				else if (Hgt < 1400.0 && Pick < 0.75f) { Kind = EBuildingKind::House; }
				break;
			case ETechAge::Starfaring:
				// Spires for the tall, domes among the rest.
				if (Hgt > 4500.0) { Kind = Pick < 0.6f ? EBuildingKind::Spire : EBuildingKind::Tower; W = Dp = FMath::Max(W, Dp); }
				else if (Pick < 0.25f) { Kind = EBuildingKind::Dome; W = Dp = FMath::Max(W, Dp); }
				break;
			default:
				// The shape: tall ones are tiered towers (square in plan), low ones at the edge pitched-roof houses, the rest blocks.
				if (Hgt > 4500.0) { Kind = EBuildingKind::Tower; W = Dp = FMath::Max(W, Dp); }
				else if (Hgt < 1400.0 && Pick < 0.7f) { Kind = EBuildingKind::House; }
				break;
			}
			Place(OutByKind, Kind, Dir, F.GX, R, S.HeightCm, W, Dp, Hgt, 150.0);
			++Count;
		}
	}

	/** A stone-age hamlet: huts in loose clusters, a longhouse or two; an iron-age village: houses (adobe on a desert)
	 *  turned every way along lanes that wander, a round keep at a strong one's heart. */
	void BuildVillage(const FPlanetTerrainParams& P, const FCivilization& Civ, const FTownSite& Town, TArray<FTransform>* OutByKind, int32 MaxBuildings)
	{
		const double R = P.RadiusCm;
		const FTownFrame F(Town);
		const bool bStone = Civ.Age == ETechAge::Stone;
		const bool bAdobe = Civ.WetMin < 0.f;
		const double Pitch = bStone ? 1500.0 + 500.0 * Hash01(Mix(Town.Seed, 17u)) : 2000.0 + 600.0 * Hash01(Mix(Town.Seed, 17u));
		// A hamlet is small whatever its point's weight.
		const double Radius = bStone ? FMath::Min(Town.RadiusCm, 18000.0) : FMath::Min(Town.RadiusCm, 40000.0);
		const int32 Half = FMath::CeilToInt(Radius / Pitch);
		int32 Count = 0;
		if (!bStone && Town.bCity)
		{
			const FPlanetTerrainSample S0 = SlonkTerrain::Sample(P, F.Up, 400.0);
			if (Buildable(S0)) { Place(OutByKind, EBuildingKind::RoundTower, F.Up, F.GX, R, S0.HeightCm, 1400.0, 1400.0, 2200.0, 150.0); ++Count; }
		}
		for (int32 gy = -Half; gy <= Half && Count < MaxBuildings; ++gy)
		for (int32 gx = -Half; gx <= Half && Count < MaxBuildings; ++gx)
		{
			const uint32 H = Mix(Town.Seed, (uint32)(gx + 4096) * 65537u + (uint32)(gy + 4096));
			// Loose: each lot wanders most of a lot from its place, so no rows show.
			const FVector Jit((Hash01(Mix(H, 3u)) - 0.5) * 0.9 * Pitch, (Hash01(Mix(H, 5u)) - 0.5) * 0.9 * Pitch, 0.0);
			const double Dx = gx * Pitch + Jit.X, Dy = gy * Pitch + Jit.Y;
			const double D2 = (Dx * Dx + Dy * Dy) / FMath::Max(Radius * Radius, 1.0);
			if (D2 > 1.0) { continue; }
			if (!bStone && Town.bCity && D2 < 0.03) { continue; }		// round the keep
			const float Density = (bStone ? 0.45f : 0.6f) * (float)(1.0 - D2);
			if (Hash01(H) > Density) { continue; }
			const FVector Dir = (Town.LocalCm + F.GX * Dx + F.GY * Dy).GetSafeNormal();
			const FPlanetTerrainSample S = SlonkTerrain::Sample(P, Dir, 400.0);
			if (!Buildable(S)) { continue; }
			// Turned any way: there is no street to face.
			const double Yaw = Hash01(Mix(H, 23u)) * 2.0 * PI;
			const FVector Facing = F.GX * FMath::Cos(Yaw) + F.GY * FMath::Sin(Yaw);
			const float Pick = Hash01(Mix(H, 19u));
			if (bStone)
			{
				// Huts 4 to 8 m across; one in eight a longhouse, drawn out to three times its width.
				const double Wd = 400.0 + 400.0 * Hash01(Mix(H, 7u));
				const bool bLong = Pick < 0.12f;
				Place(OutByKind, EBuildingKind::Hut, Dir, Facing, R, S.HeightCm, bLong ? Wd * 2.6 : Wd, Wd, 300.0 + 250.0 * Hash01(Mix(H, 13u)), 60.0);
			}
			else
			{
				const double W = 700.0 + 600.0 * Hash01(Mix(H, 7u)), D = 600.0 + 500.0 * Hash01(Mix(H, 9u));
				const EBuildingKind Kind = bAdobe ? EBuildingKind::Pueblo : (Pick < 0.15f ? EBuildingKind::Hut : EBuildingKind::House);
				Place(OutByKind, Kind, Dir, Facing, R, S.HeightCm, W, D, 450.0 + 350.0 * Hash01(Mix(H, 13u)), 100.0);
			}
			++Count;
		}
	}

	/** A grown town: rings of pods round a heart of bulb towers, the ways between them curving, every building
	 *  turned to face the heart. */
	void BuildGrownTown(const FPlanetTerrainParams& P, const FCivilization& Civ, const FTownSite& Town, TArray<FTransform>* OutByKind, int32 MaxBuildings)
	{
		const double R = P.RadiusCm;
		const FTownFrame F(Town);
		const double Ring = 3200.0 + 1200.0 * Hash01(Mix(Town.Seed, 17u));
		const int32 Rings = FMath::Max(1, FMath::FloorToInt(Town.RadiusCm / Ring));
		int32 Count = 0;
		// The heart: a bulb tower, and in a city a cluster of them, the tallest a hundred metres and more.
		const int32 Hearts = Town.bCity ? 3 + (int32)(Hash01(Mix(Town.Seed, 29u)) * 4.f) : 1;
		for (int32 h = 0; h < Hearts && Count < MaxBuildings; ++h)
		{
			const uint32 H = Mix(Town.Seed, 0xB0u + (uint32)h);
			const double A = Hash01(Mix(H, 1u)) * 2.0 * PI, Rd = h == 0 ? 0.0 : Ring * 0.6 * (0.5 + Hash01(Mix(H, 2u)));
			const FVector Dir = (Town.LocalCm + (F.GX * FMath::Cos(A) + F.GY * FMath::Sin(A)) * Rd).GetSafeNormal();
			const FPlanetTerrainSample S = SlonkTerrain::Sample(P, Dir, 400.0);
			if (!Buildable(S)) { continue; }
			const double Hgt = (Town.bCity ? 6000.0 + 9000.0 * Town.Strength : 2500.0) * (0.6 + 0.4 * Hash01(Mix(H, 3u)));
			const double W = Hgt * (0.3 + 0.1 * Hash01(Mix(H, 4u)));
			Place(OutByKind, EBuildingKind::BulbTower, Dir, F.GX, R, S.HeightCm, W, W, Hgt, 200.0);
			++Count;
		}
		for (int32 k = 1; k <= Rings && Count < MaxBuildings; ++k)
		{
			const double Rk = k * Ring;
			const int32 N = FMath::Max(6, FMath::FloorToInt(2.0 * PI * Rk / 2200.0));
			const double Phase = Hash01(Mix(Town.Seed, 0x51u + (uint32)k)) * 2.0 * PI;
			for (int32 n = 0; n < N && Count < MaxBuildings; ++n)
			{
				const uint32 H = Mix(Town.Seed, (uint32)k * 7919u + (uint32)n);
				// Sparser than a built town's lots: gardens between the pods, thinning outward.
				if (Hash01(H) > 0.45f * (1.0f - 0.5f * (float)k / Rings)) { continue; }
				// The ring wanders in and out, so the ways curve.
				const double A = Phase + 2.0 * PI * (n + 0.35 * (Hash01(Mix(H, 3u)) - 0.5)) / N;
				const double Rr = Rk * (1.0 + 0.18 * FMath::Sin(A * 3.0 + Phase) + 0.12 * (Hash01(Mix(H, 5u)) - 0.5));
				const FVector Out = F.GX * FMath::Cos(A) + F.GY * FMath::Sin(A);
				const FVector Dir = (Town.LocalCm + Out * Rr).GetSafeNormal();
				const FPlanetTerrainSample S = SlonkTerrain::Sample(P, Dir, 400.0);
				if (!Buildable(S)) { continue; }
				const float Pick = Hash01(Mix(H, 19u));
				const double W = 800.0 + 900.0 * Hash01(Mix(H, 7u));
				if (Pick < 0.18f)
				{
					Place(OutByKind, EBuildingKind::Dome, Dir, Out, R, S.HeightCm, W * 1.5, W * 1.5, W * 0.6, 150.0);
				}
				else
				{
					Place(OutByKind, EBuildingKind::Pod, Dir, Out, R, S.HeightCm, W, W * (0.8 + 0.4 * Hash01(Mix(H, 9u))), W * (0.7 + 0.6 * Hash01(Mix(H, 13u))), 150.0);
				}
				++Count;
			}
		}
	}

	/**
	 * A town built into a cliff (Town.bCliff): along the wall, either side of the town's point, the profile across
	 * the wall is read and on each ledge at the foot of a rise the people build against the rock behind it, the rear
	 * of each building set into the rise: adobe houses stepped back a storey and round towers for the early ages,
	 * houses and blocks for the later, a facade of glass deep in the rock for the starfaring; a grown people's pods
	 * on the ledges and on the faces between them.
	 */
	void BuildCliffTown(const FPlanetTerrainParams& P, const FCivilization& Civ, const FTownSite& Town, TArray<FTransform>* OutByKind, int32 MaxBuildings)
	{
		const double R = P.RadiusCm;
		const FVector Up0 = Town.Dir;
		const FVector Face0 = (Town.Face - Up0 * FVector::DotProduct(Town.Face, Up0)).GetSafeNormal();
		if (Face0.IsNearlyZero()) { return; }
		const FVector Strike0 = FVector::CrossProduct(Up0, Face0);
		const bool bEarly = Civ.Age <= ETechAge::Iron;
		const bool bGlass = Civ.Age == ETechAge::Starfaring && !Civ.IsOrganic();
		const double StepCm = bEarly ? 1400.0 : Civ.IsOrganic() ? 2000.0 : bGlass ? 3200.0 : 1800.0;
		const double SpanCm = 30000.0, DtCm = 400.0;
		const int32 NT = FMath::RoundToInt(2.0 * SpanCm / DtCm);
		TArray<double> Hp; Hp.SetNum(NT + 1);
		int32 Count = 0;
		for (double Sx = -Town.RadiusCm; Sx <= Town.RadiusCm && Count < MaxBuildings; Sx += StepCm)
		{
			const uint32 Hs = Mix(Town.Seed, (uint32)FMath::RoundToInt(Sx / 100.0 + 100000.0));
			// The wall bends: the line across it at each step starts from the town's line along the strike, and the
			// profile is read from 300 m inside to 300 m out.
			const FVector Mid = Along(P, Up0, Strike0, Sx + (Hash01(Hs) - 0.5) * StepCm * 0.5);
			const FVector Face = (Face0 - Mid * FVector::DotProduct(Face0, Mid)).GetSafeNormal();
			const FVector Strike = FVector::CrossProduct(Mid, Face);
			for (int32 k = 0; k <= NT; ++k) { Hp[k] = SlonkTerrain::SampleStructure(P, Along(P, Mid, Face, -SpanCm + k * DtCm), 2.0e4).HeightCm; }
			// The feet of the rises: a wall here is a stair of sheer risers, each over a concave apron of scree, the level
			// ground only beginning tens of metres out. So the people build at the foot of each riser, where the rock goes
			// up sheer behind (more than 1 in 1 over the 8 m behind, 12 m or more within 30) and the ground in front is
			// still walkable (under 1.2 here), the back of each house set into the riser and its floor carried down to
			// the apron at its front on a footing, as a cliff dweller walls up the slope under an alcove. Each foot once.
			const int32 Back = FMath::RoundToInt(3000.0 / DtCm);
			int32 LastLedge = -1000;
			int32 Ledges = 0;
			for (int32 k = Back; k < NT && Count < MaxBuildings && Ledges < 5; ++k)
			{
				const double Slope = (Hp[k - 1] - Hp[k + 1]) / (2.0 * DtCm);
				const double Rise = Hp[k - Back] - Hp[k];
				if (Slope > 1.2 || Slope < -0.3 || Rise < 1200.0 || Hp[k - 2] - Hp[k] < 800.0) { continue; }
				if (k - LastLedge < 8) { continue; }
				LastLedge = k;
				const uint32 H = Mix(Hs, (uint32)k);
				if (Hash01(H) > 0.85f) { continue; }
				++Ledges;
				const double T = -SpanCm + k * DtCm;
				// What, and how big (W along the wall, D into it).
				EBuildingKind Kind; double W, D, Hgt, Embed;
				const float Pick = Hash01(Mix(H, 19u));
				if (bEarly)
				{
					Kind = Pick < 0.15f ? EBuildingKind::RoundTower : EBuildingKind::Pueblo;
					// Rooms of two or three storeys run together along the ledge, as a cliff palace's do.
					W = Kind == EBuildingKind::RoundTower ? 600.0 + 400.0 * Hash01(Mix(H, 7u)) : 900.0 + 1000.0 * Hash01(Mix(H, 7u));
					D = Kind == EBuildingKind::RoundTower ? W : 700.0 + 500.0 * Hash01(Mix(H, 9u));
					Hgt = (Kind == EBuildingKind::RoundTower ? 1000.0 : 650.0) + 500.0 * Hash01(Mix(H, 13u));
					Embed = 0.4;
				}
				else if (Civ.IsOrganic())
				{
					Kind = EBuildingKind::Pod; W = 800.0 + 700.0 * Hash01(Mix(H, 7u)); D = W; Hgt = W * (0.8 + 0.5 * Hash01(Mix(H, 13u))); Embed = 0.4;
				}
				else if (bGlass)
				{
					Kind = EBuildingKind::Block; W = 2000.0 + 1500.0 * Hash01(Mix(H, 7u)); D = 1600.0 + 800.0 * Hash01(Mix(H, 9u)); Hgt = 900.0 + 1500.0 * Hash01(Mix(H, 13u)); Embed = 0.65;
				}
				else
				{
					Kind = Pick < 0.5f ? EBuildingKind::House : EBuildingKind::Block;
					W = 800.0 + 800.0 * Hash01(Mix(H, 7u)); D = 700.0 + 500.0 * Hash01(Mix(H, 9u)); Hgt = 600.0 + 700.0 * Hash01(Mix(H, 13u)); Embed = 0.3;
				}
				// Its rear set into the riser, its floor at the riser's foot; the footing carried down to the ground at its
				// front (no deeper than its own depth: a steeper apron is left bare), so nothing hangs over the drop.
				const double Tc = T + D * (0.5 - Embed);
				const FVector Dir = Along(P, Mid, Face, Tc);
				const FPlanetTerrainSample Sf = SlonkTerrain::Sample(P, Along(P, Mid, Face, T + 200.0), 300.0);
				if (Sf.bWater) { continue; }
				const double Front = SlonkTerrain::Sample(P, Along(P, Mid, Face, Tc + D * 0.5), 300.0).HeightCm;
				const double Footing = FMath::Max(Sf.HeightCm - Front, 0.0);
				if (Footing > D) { continue; }
				// Facing out from the rock (the mesh's rear, +Y, into it): X along the wall.
				Place(OutByKind, Kind, Dir, Strike, R, Sf.HeightCm - Footing, W, D, Hgt + Footing, 100.0);
				++Count;
				// An early people's houses climb: a smaller storey set back on the first, against the rock, and on some
				// a third on that.
				if (bEarly && Kind == EBuildingKind::Pueblo && Hash01(Mix(H, 31u)) < 0.6f && Count < MaxBuildings)
				{
					const FVector Dir2 = Along(P, Mid, Face, Tc - D * 0.2);
					Place(OutByKind, EBuildingKind::Pueblo, Dir2, Strike, R, Sf.HeightCm + Hgt - 50.0, W * 0.75, D * 0.6, Hgt * 0.8, 50.0);
					++Count;
					if (Hash01(Mix(H, 37u)) < 0.4f && Count < MaxBuildings)
					{
						const FVector Dir3 = Along(P, Mid, Face, Tc - D * 0.35);
						Place(OutByKind, EBuildingKind::Pueblo, Dir3, Strike, R, Sf.HeightCm + Hgt * 1.8 - 100.0, W * 0.55, D * 0.35, Hgt * 0.7, 50.0);
						++Count;
					}
				}
			}
			// The grown and the glass also hold to the faces between the ledges: pods grown out of the rock, facades set
			// into it, at a few heights.
			if ((Civ.IsOrganic() || bGlass) && Count < MaxBuildings)
			{
				int32 OnFace = 0;
				for (int32 k = 2; k < NT - 2 && OnFace < 3 && Count < MaxBuildings; k += 3)
				{
					const double Slope = (Hp[k - 1] - Hp[k + 1]) / (2.0 * DtCm);
					if (Slope < 1.2) { continue; }
					const uint32 H = Mix(Hs, 0x9000u + (uint32)k);
					if (Hash01(H) > 0.3f) { continue; }
					++OnFace;
					const double T = -SpanCm + k * DtCm;
					const double W = bGlass ? 1600.0 + 1000.0 * Hash01(Mix(H, 7u)) : 1000.0 + 1200.0 * Hash01(Mix(H, 7u));
					const double D = bGlass ? 1400.0 : W;
					const double Hgt = bGlass ? 800.0 + 600.0 * Hash01(Mix(H, 13u)) : W * 1.1;
					// Its centre a little out from the face at that height, the rest in the rock.
					const FVector Dir = Along(P, Mid, Face, T + D * (bGlass ? -0.25 : 0.05));
					Place(OutByKind, bGlass ? EBuildingKind::Block : EBuildingKind::Pod, Dir, Strike, R, Hp[k] - Hgt * 0.5, W, D, Hgt, 0.0);
					++Count;
				}
			}
		}
	}
}

void SlonkCities::BuildTown(const FPlanetTerrainParams& P, const FCivilization& Civ, const FTownSite& Town, TArray<FTransform>* OutByKind, int32 MaxBuildings)
{
	if (Town.bCliff) { BuildCliffTown(P, Civ, Town, OutByKind, MaxBuildings); }
	else if (Civ.IsOrganic()) { BuildGrownTown(P, Civ, Town, OutByKind, MaxBuildings); }
	else if (Civ.Age <= ETechAge::Iron) { BuildVillage(P, Civ, Town, OutByKind, MaxBuildings); }
	else { BuildGridTown(P, Civ, Town, OutByKind, MaxBuildings); }
}
