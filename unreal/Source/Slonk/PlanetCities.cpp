#include "PlanetCities.h"
#include "Misc/ScopeRWLock.h"

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

	/**
	 * The present points of a level near a planet-local point (within two cells each way), as WalkLevel keeps them:
	 * only those near the surface are anyone's. In cell units, offset by half a cell (the space NearestSq works in).
	 */
	template <typename FVisit>
	void PointsAround(const FVector& LocalCm, double R, int32 Level, FVisit&& Visit)
	{
		const double Cell = LevelCm[Level];
		const FVector Q = LocalCm / Cell - FVector(0.5, 0.5, 0.5);
		const FIntVector Fl((int32)FMath::Floor(Q.X), (int32)FMath::Floor(Q.Y), (int32)FMath::Floor(Q.Z));
		for (int32 Dz = -2; Dz <= 2; ++Dz)
		for (int32 Dy = -2; Dy <= 2; ++Dy)
		for (int32 Dx = -2; Dx <= 2; ++Dx)
		{
			const FIntVector C(Fl.X + Dx, Fl.Y + Dy, Fl.Z + Dz);
			const uint32 H = CellHash(C, LevelSalt(Level));
			if ((H >> 30) >= LevelKeep[Level]) { continue; }
			const FVector Pt = FVector(C) + Jitter(H);
			if (FMath::Abs(((Pt + FVector(0.5, 0.5, 0.5)) * Cell).Size() - R) > Cell * 0.9) { continue; }
			Visit(Pt, H, Q);
		}
	}

	/**
	 * Whether a planet-local point lies in the Voronoi cell of the level's point with this seed, at least MarginCm
	 * inside it: no other point of the level is nearer, by that margin. A town that keeps its buildings to its own
	 * cell cannot build on a neighbour's, and stands where the terrain material, which paves the nearest point's grid,
	 * paves its own.
	 */
	bool InOwnCell(const FVector& LocalCm, double R, int32 Level, uint32 Seed, double MarginCm)
	{
		FVector Own = FVector::ZeroVector, Q = FVector::ZeroVector;
		bool bOwn = false;
		TArray<FVector, TInlineAllocator<128>> Others;
		PointsAround(LocalCm, R, Level, [&](const FVector& Pt, uint32 H, const FVector& InQ)
		{
			Q = InQ;
			if (H == Seed) { Own = Pt; bOwn = true; } else { Others.Add(Pt); }
		});
		if (!bOwn) { return false; }
		const double M = MarginCm / LevelCm[Level];
		const double DOwn = FVector::DistSquared(Q, Own);
		for (const FVector& B : Others)
		{
			// How far it is from the bisector between the two, on the own point's side.
			const double L = FVector::Dist(Own, B);
			if (L < 1e-9 || (FVector::DistSquared(Q, B) - DOwn) / (2.0 * L) < M) { return false; }
		}
		return true;
	}

	/** The ground a town may build on: its point's cell (a city's among the cluster points, a town's among the town
	 *  points). Within half the way to its nearest neighbour it is the town's own without asking. */
	struct FOwnGround
	{
		const FPlanetTerrainParams& P;
		int32 Level;
		uint32 Seed;
		FVector PointCm = FVector::ZeroVector;
		double SafeCm = 0.0;
		FOwnGround(const FPlanetTerrainParams& InP, const FTownSite& Town) : P(InP), Level(Town.bCity ? 1 : 2), Seed(Town.Seed)
		{
			const double Cell = LevelCm[Level];
			bool bFound = false;
			// The town's point (a cliff town's site is off it, on its wall), then its nearest neighbour.
			PointsAround(Town.LocalCm, P.RadiusCm, Level, [&](const FVector& Pt, uint32 H, const FVector&) { if (H == Seed) { PointCm = (Pt + FVector(0.5, 0.5, 0.5)) * Cell; bFound = true; } });
			if (!bFound) { return; }
			double Nearest = 1.0e30;
			PointsAround(PointCm, P.RadiusCm, Level, [&](const FVector& Pt, uint32 H, const FVector&) { if (H != Seed) { Nearest = FMath::Min(Nearest, FVector::Dist((Pt + FVector(0.5, 0.5, 0.5)) * Cell, PointCm)); } });
			SafeCm = Nearest < 1.0e29 ? Nearest * 0.5 : 0.0;
		}
		/** A building HalfCm across (from its centre to its farthest corner) at Dir, five metres clear of the edge. */
		bool Owns(const FVector& Dir, double HalfCm) const
		{
			const FVector X = Dir * P.RadiusCm;
			if (FVector::Dist(X, PointCm) + HalfCm + 500.0 < SafeCm) { return true; }
			return InOwnCell(X, P.RadiusCm, Level, Seed, HalfCm + 500.0);
		}
	};

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
			// Five cells about where the surface crosses the column: where the surface leans across it (toward 45 degrees from
			// the axis) it spans more than three, and the points near it in the others were missed.
			for (int32 k = -2; k <= 2; ++k)
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
	 * The best cliff within SearchCm of Dir and in the cell of the level's point with this seed (so two towns never
	 * take the same wall): a scarp's or a canyon's wall (the sample's Wall) that drops at least 80 m over 300 m across
	 * it, the biggest drop of a grid of samples. OutFace is the way it faces: the level direction downhill, out from
	 * the rock. False where there is none.
	 */
	bool SearchCliff(const FPlanetTerrainParams& P, const FVector& Dir, double SearchCm, int32 Level, uint32 Seed, FVector& OutDir, FVector& OutFace)
	{
		// Walls are only where the tablelands are (their scarps, their canyons): none elsewhere to look for.
		if (SlonkTerrain::SampleStructure(P, Dir, 2.0e5).Table < 0.05f) { return false; }
		const FVector East = FVector::CrossProduct(FMath::Abs(Dir.Z) < 0.9 ? FVector::ZAxisVector : FVector::XAxisVector, Dir).GetSafeNormal();
		const FVector North = FVector::CrossProduct(Dir, East);
		// A grid over the search, the four most wall-like of it measured; a town's run along its wall is 600 m at
		// most, so a wall less than 200 m inside the cell is left to the neighbour's.
		TArray<TPair<float, FVector>> Walls;
		for (int32 j = -3; j <= 3; ++j)
		{
			for (int32 i = -3; i <= 3; ++i)
			{
				const FVector D = (Dir * P.RadiusCm + East * (i * SearchCm / 3.0) + North * (j * SearchCm / 3.0)).GetSafeNormal();
				if (!InOwnCell(D * P.RadiusCm, P.RadiusCm, Level, Seed, 20000.0)) { continue; }
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

	/** The cliffs found, by world and point: the walls do not move, and the same points are asked about again and
	 *  again (every terrain chunk's rock keep-out, every rebuild of the towns round the view, the goto search). */
	struct FCliffHit { bool bFound = false; FVector Dir = FVector::ZeroVector, Face = FVector::ZeroVector; };
	FRWLock GCliffLock;
	TMap<uint64, FCliffHit> GCliffs;
	uint32 ParamsHash(const FPlanetTerrainParams& P)
	{
		uint32 H = GetTypeHash(P.Seed);
		for (const double X : { P.RadiusCm, P.ReliefCm, P.OceanDepthCm, P.CanyonDepthCm, P.CanyonWidthCm, P.SnowlineCm }) { H = HashCombineFast(H, GetTypeHash(X)); }
		for (const float X : { P.SeaLevel, P.ContinentScale, P.Mountains, P.MountainHeight, P.Canyons, P.Strata, P.Craters, P.CraterScale, P.Maria, P.Lineae, P.IceCapStart, P.Moisture }) { H = HashCombineFast(H, GetTypeHash(X)); }
		return HashCombineFast(H, (P.bLava ? 2u : 0u) + (P.bAirless ? 1u : 0u));
	}
	bool FindCliff(const FPlanetTerrainParams& P, const FVector& Dir, double SearchCm, int32 Level, uint32 Seed, FVector& OutDir, FVector& OutFace)
	{
		const uint64 Key = ((uint64)ParamsHash(P) << 32) | HashCombineFast(Seed, (uint32)Level * 0x9e3779b9u + (uint32)(SearchCm / 1000.0));
		FCliffHit Hit;
		bool bCached = false;
		{
			FReadScopeLock Lock(GCliffLock);
			if (const FCliffHit* Found = GCliffs.Find(Key)) { Hit = *Found; bCached = true; }
		}
		if (!bCached)
		{
			Hit.bFound = SearchCliff(P, Dir, SearchCm, Level, Seed, Hit.Dir, Hit.Face);
			FWriteScopeLock Lock(GCliffLock);
			if (GCliffs.Num() > 100000) { GCliffs.Reset(); }
			GCliffs.Add(Key, Hit);
		}
		OutDir = Hit.Dir; OutFace = Hit.Face;
		return Hit.bFound;
	}

	/** One building: its kind, footprint (W across, D deep, cm), height (cm, above Sink), facing. */
	void Place(TArray<FTransform>* OutByKind, EBuildingKind Kind, const FVector& Dir, const FVector& Facing, double R, double BaseCm, double W, double D, double Hgt, double Sink)
	{
		const FVector Fx = (Facing - Dir * FVector::DotProduct(Facing, Dir)).GetSafeNormal();
		const FQuat Rot = FRotationMatrix::MakeFromZX(Dir, Fx.IsNearlyZero() ? FVector::CrossProduct(Dir, FVector::ZAxisVector).GetSafeNormal() : Fx).ToQuat();
		const FVector Loc = Dir * (R + BaseCm - Sink + (Hgt + Sink) * 0.5);
		OutByKind[(int32)Kind].Emplace(Rot, Loc, FVector(W / 100.0, D / 100.0, (Hgt + Sink) / 100.0));
	}

	/**
	 * One building on open ground at a sample, its foot below the ground at its low side as well as at its centre: by
	 * the slope there its far corner lies half its diagonal times the slope lower. Under a metre that goes into its
	 * sink; more, and a built people raise it on a footing of masonry (bFooting), an early or a grown one sinks it the
	 * deeper (a hut dug into the slope, a pod grown from it).
	 */
	void PlaceOnGround(TArray<FTransform>* OutByKind, EBuildingKind Kind, const FVector& Dir, const FVector& Facing, double R, const FPlanetTerrainSample& S, double W, double D, double Hgt, double Sink, bool bFooting)
	{
		const double Drop = 0.5 * FMath::Sqrt(W * W + D * D) * S.Slope * 1.2;
		if (bFooting && Drop > 100.0)
		{
			Place(OutByKind, EBuildingKind::Footing, Dir, Facing, R, S.HeightCm, W, D, 20.0, Drop + Sink);
			Place(OutByKind, Kind, Dir, Facing, R, S.HeightCm, W, D, Hgt, 20.0);
			return;
		}
		Place(OutByKind, Kind, Dir, Facing, R, S.HeightCm, W, D, Hgt, Sink + Drop);
	}

	/** The footprints a town has laid out, to keep its buildings out of each other: rectangles in the town's plane
	 *  (cm, along its frame's axes), hashed by 25 m cells. */
	struct FLots
	{
		struct FFoot { FVector2D C, X; double Hw, Hd; };
		TArray<FFoot> Feet;
		TMap<FIntPoint, TArray<int32, TInlineAllocator<4>>> Cells;
		static constexpr double CellCm = 2500.0;
		/** The two rectangles come within Gap of each other (separating axes). */
		static bool Near(const FFoot& A, const FFoot& B, double Gap)
		{
			const FVector2D D = B.C - A.C;
			const FVector2D Ay(-A.X.Y, A.X.X), By(-B.X.Y, B.X.X);
			for (const FVector2D& L : { A.X, Ay, B.X, By })
			{
				const double Ra = A.Hw * FMath::Abs(FVector2D::DotProduct(A.X, L)) + A.Hd * FMath::Abs(FVector2D::DotProduct(Ay, L));
				const double Rb = B.Hw * FMath::Abs(FVector2D::DotProduct(B.X, L)) + B.Hd * FMath::Abs(FVector2D::DotProduct(By, L));
				if (FMath::Abs(FVector2D::DotProduct(D, L)) >= Ra + Rb + Gap) { return false; }
			}
			return true;
		}
		/** Takes a lot, W along Facing and D across it, centred at C, if nothing laid out yet comes within Gap of it. */
		bool Take(const FVector2D& C, const FVector2D& Facing, double W, double D, double Gap = 150.0)
		{
			FFoot F{ C, Facing.GetSafeNormal(), W * 0.5, D * 0.5 };
			if (F.X.IsNearlyZero()) { F.X = FVector2D(1.0, 0.0); }
			const double Rad = FMath::Sqrt(F.Hw * F.Hw + F.Hd * F.Hd) + Gap;
			const int32 X0 = FMath::FloorToInt((C.X - Rad) / CellCm), X1 = FMath::FloorToInt((C.X + Rad) / CellCm);
			const int32 Y0 = FMath::FloorToInt((C.Y - Rad) / CellCm), Y1 = FMath::FloorToInt((C.Y + Rad) / CellCm);
			for (int32 Y = Y0; Y <= Y1; ++Y)
			for (int32 X = X0; X <= X1; ++X)
			{
				if (const TArray<int32, TInlineAllocator<4>>* L = Cells.Find(FIntPoint(X, Y)))
				{
					for (const int32 i : *L) { if (Near(Feet[i], F, Gap)) { return false; } }
				}
			}
			const int32 Id = Feet.Add(F);
			for (int32 Y = Y0; Y <= Y1; ++Y)
			for (int32 X = X0; X <= X1; ++X) { Cells.FindOrAdd(FIntPoint(X, Y)).Add(Id); }
			return true;
		}
	};

	/** Ground a building can stand on: dry, not a wall, not steep. */
	bool Buildable(const FPlanetTerrainSample& S) { return !S.bWater && S.Wall <= 0.3f && S.Slope <= 0.5f; }
	/** From a building's centre to its farthest corner. */
	double HalfDiag(double W, double D) { return 0.5 * FMath::Sqrt(W * W + D * D); }
}

// Radii: a city at a cluster point grows with the region weight there; a town at a town point with region x cluster.
// Towns inside a city's radius are the city's own blocks and are not built again (overlapping grids made a carpet).
double SlonkCities::CityRadiusCm(float Region) { return LevelCm[1] * (0.25 + 0.2 * Region); }
double SlonkCities::TownRadiusCm(float Strength) { return LevelCm[2] * (0.2 + 0.3 * Strength); }

void SlonkCities::EnumerateTowns(const FPlanetTerrainParams& P, const FCivilization& Civ, const FVector& CentreLocalCm, double ReachCm, TArray<FTownSite>& Out, int32 MaxTowns)
{
	const double R = P.RadiusCm;
	int32 NWater = 0, NDry = 0, NHigh = 0, NSteep = 0, NCliff = 0;
	// A point the people do not live at (their density thins the field, as the terrain material's SettleDensity does),
	// one they build into a cliff near it (their liking for cliffs, where one is in its cell), or one on open land the
	// shader would light. A people who lay a street grid try the open land first and turn to a cliff only where it
	// will not do, so the grid the terrain paves at a point always has its town; the others, whose ground is not paved,
	// go to the cliff first.
	auto Site = [&](const FVector& LocalCm, uint32 Seed, int32 Level, double RadiusCm, float Strength, bool bCity, FTownSite& T) -> bool
	{
		if (Hash01(Mix(Seed, 0xD5u)) > Civ.Density) { return false; }
		const FVector Dir = LocalCm.GetSafeNormal();
		T.Seed = Seed; T.bCity = bCity;
		const bool bLikesCliff = Hash01(Mix(Seed, 0xC1u)) < Civ.CliffAffinity;
		auto Cliff = [&]() -> bool
		{
			FVector CDir, Face;
			if (!FindCliff(P, Dir, FMath::Max(RadiusCm, 80000.0), Level, Seed, CDir, Face)) { return false; }
			// Along the wall, not round a centre: a few hundred metres of ledges either side.
			T.Dir = CDir; T.LocalCm = CDir * R; T.Face = Face; T.bCliff = true;
			T.RadiusCm = FMath::Clamp(RadiusCm * 0.6, 12000.0, 60000.0);
			T.Strength = Strength;
			++NCliff;
			return true;
		};
		if (bLikesCliff && Civ.StreetGrid <= 0.f && Cliff()) { return true; }
		const float Land = LandFactor(P, Civ, Dir, NWater, NDry, NHigh, NSteep);
		if (Land < 0.f) { return bLikesCliff && Civ.StreetGrid > 0.f && Cliff(); }
		T.Dir = Dir; T.LocalCm = Dir * R; T.RadiusCm = RadiusCm; T.Strength = Strength * Land; T.bCliff = false; T.Face = FVector::ZeroVector;
		return true;
	};
	// Cities first: the cluster points, by distance. Each claims its ground whether its people live there or not, on
	// it or moved to a cliff: the terrain paves and lights a city's grid there or nothing, never a town's.
	TArray<FPoint> Clusters; WalkLevel(1, R, CentreLocalCm, ReachCm + LevelCm[1], Clusters);
	Clusters.Sort([](const FPoint& A, const FPoint& B) { return A.DistSq < B.DistSq; });
	TArray<FTownSite> Cities;
	struct FClaim { FVector LocalCm; double RadiusCm; };
	TArray<FClaim> Claims;
	for (const FPoint& C : Clusters)
	{
		const float Region = 1.f - SmoothStep(0.05f, 0.7f, (float)NearestSq(C.LocalCm, 0));
		if (Region < 0.35f) { continue; }
		// On the surface, as the shader measures a city's reach across the ground (the point itself may lie off it).
		Claims.Add({ C.LocalCm.GetSafeNormal() * R, CityRadiusCm(Region) });
		FTownSite T;
		if (!Site(C.LocalCm, C.Seed, 1, CityRadiusCm(Region), FMath::Min(1.f, 0.5f + Region * 0.6f), true, T)) { continue; }
		Cities.Add(T);
	}
	// Then the towns, skipping any inside a city's ground.
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
		const FVector OnGround = Tp.LocalCm.GetSafeNormal() * R;
		for (const FClaim& C : Claims)
		{
			if (FVector::DistSquared(C.LocalCm, OnGround) < FMath::Square(C.RadiusCm + TownRadiusCm(Strength) + 30000.0)) { bInCity = true; break; }
		}
		if (bInCity) { continue; }
		FTownSite T;
		if (!Site(Tp.LocalCm, Tp.Seed, 2, TownRadiusCm(Strength), Strength * 0.6f, false, T)) { continue; }
		// A town or a city moved to a cliff is somewhere else than its point: it keeps clear of the other's run along
		// its wall (300 m across it either way) or its streets.
		bool bClash = false;
		for (const FTownSite& C : Cities)
		{
			if ((T.bCliff || C.bCliff) && FVector::DistSquared(C.LocalCm, T.LocalCm) < FMath::Square(C.RadiusCm + T.RadiusCm + 30000.0)) { bClash = true; break; }
		}
		if (bClash) { continue; }
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

	/**
	 * The two nearest settlement points of a level to a point on the sea-level sphere (Dir), as the terrain material
	 * finds them: among the 27 cells round the cell corner nearest it, only points within nine tenths of a cell of the
	 * surface (WalkLevel's, measured along the point's up), in the material's order. How many of the two it found.
	 */
	int32 NearestPoints(const FVector& Dir, double R, int32 Level, FVector& OutA, uint32& OutHA, FVector& OutB, uint32& OutHB)
	{
		const double Cell = LevelCm[Level];
		const FVector X = Dir * R;
		const FVector Q = X / Cell - FVector(0.5, 0.5, 0.5);
		const FVector Fl(FMath::Floor(Q.X), FMath::Floor(Q.Y), FMath::Floor(Q.Z));
		const FVector Fr = Q - Fl;
		const FIntVector Base((int32)Fl.X + (Fr.X >= 0.5 ? 0 : -1), (int32)Fl.Y + (Fr.Y >= 0.5 ? 0 : -1), (int32)Fl.Z + (Fr.Z >= 0.5 ? 0 : -1));
		double D1 = 1.0e30, D2 = 1.0e30;
		int32 N = 0;
		for (int32 i = 0; i < 27; ++i)
		{
			const FIntVector C = Base + FIntVector(i % 3, (i / 3) % 3, i / 9);
			const uint32 H = CellHash(C, LevelSalt(Level));
			if ((H >> 30) >= LevelKeep[Level]) { continue; }
			const FVector Pt = (FVector(C) + Jitter(H) + FVector(0.5, 0.5, 0.5)) * Cell;
			if (FMath::Abs(FVector::DotProduct(Pt - X, Dir)) > Cell * 0.9) { continue; }
			const double Dd = (FVector(C) + Jitter(H) - Q).SizeSquared();
			++N;
			if (Dd < D1) { OutB = OutA; OutHB = OutHA; D2 = D1; OutA = Pt; OutHA = H; D1 = Dd; }
			else if (Dd < D2) { OutB = Pt; OutHB = H; D2 = Dd; }
		}
		return FMath::Min(N, 2);
	}
	float RegionAt(const FVector& LocalCm) { return 1.f - SmoothStep(0.05f, 0.7f, (float)NearestSq(LocalCm, 0)); }
	float ClusterAt(const FVector& LocalCm) { return 1.f - SmoothStep(0.02f, 0.5f, (float)NearestSq(LocalCm, 1)); }
	bool LikesCliffOf(const FCivilization& Civ, uint32 H) { return Hash01(Mix(H, 0xC1u)) < Civ.CliffAffinity; }
	bool KeptOf(const FCivilization& Civ, uint32 H) { return Hash01(Mix(H, 0xD5u)) <= Civ.Density; }

	/**
	 * What the terrain material paints a point (on the sea-level sphere) as, as it chooses (terrain_material.py's
	 * settlement block, mirrored): a city's ground, where the nearest cluster point has a city's weight and the point
	 * is within its radius (lived at or not: no road crosses it); else a town's, where the nearest town point has a
	 * town's weight, no city has claimed it and the point is within its radius; and the road between its two nearest
	 * town points. "Nearest" is the material's (its eight cells round the point); weights, radii and claims are taken
	 * at the points, as EnumerateTowns takes them, so the material's settlements are the ones built.
	 */
	struct FPaint
	{
		bool bSite = false, bCityGround = false, bKept = false;
		FTownSite Site;
		bool bRoad = false;
		FVector A = FVector::ZeroVector, B = FVector::ZeroVector;
		uint32 HA = 0, HB = 0;
		double RoadCutCm = 0.0;
		bool SameSite(const FPaint& O) const { return bSite == O.bSite && (!bSite || (Site.Seed == O.Site.Seed && Site.bCity == O.Site.bCity)); }
		bool SameRoad(const FPaint& O) const { return bRoad == O.bRoad && (!bRoad || ((HA == O.HA && HB == O.HB) || (HA == O.HB && HB == O.HA))); }
	};
	FPaint PaintAt(const FPlanetTerrainParams& P, const FCivilization& Civ, const FVector& Dir)
	{
		FPaint S;
		const double R = P.RadiusCm;
		const FVector X = Dir * R;
		FVector Cp = FVector::ZeroVector, Cq = FVector::ZeroVector; uint32 HC = 0, HCq = 0;
		const bool bHasC = NearestPoints(Dir, R, 1, Cp, HC, Cq, HCq) >= 1;
		const float RegionC = bHasC ? RegionAt(Cp) : 0.f;
		const bool bClaim = bHasC && RegionC >= 0.35f;
		const double CityR = SlonkCities::CityRadiusCm(RegionC);
		const FVector CpOn = Cp.GetSafeNormal() * R;
		if (bClaim && FVector::Dist(X, CpOn) < CityR * 1.05)
		{
			S.bSite = true; S.bCityGround = true; S.bKept = KeptOf(Civ, HC);
			S.Site.Seed = HC; S.Site.bCity = true; S.Site.Dir = Cp.GetSafeNormal(); S.Site.LocalCm = CpOn; S.Site.RadiusCm = CityR;
			return S;
		}
		// The two nearest town points, in the material's order.
		FVector A = FVector::ZeroVector, B = FVector::ZeroVector; uint32 HA = 0, HB = 0;
		const int32 NT = NearestPoints(Dir, R, 2, A, HA, B, HB);
		if (NT < 1) { return S; }
		const float StrengthA = RegionAt(A) * ClusterAt(A);
		const double TownR = SlonkCities::TownRadiusCm(StrengthA);
		const FVector AOn = A.GetSafeNormal() * R;
		const bool bClaimed = bClaim && FVector::Dist(AOn, CpOn) < CityR + TownR + 30000.0;
		if (StrengthA >= 0.3f && !bClaimed && FVector::Dist(X, AOn) < TownR * 1.05)
		{
			S.bSite = true; S.bKept = KeptOf(Civ, HA);
			S.Site.Seed = HA; S.Site.bCity = false; S.Site.Dir = A.GetSafeNormal(); S.Site.LocalCm = AOn; S.Site.RadiusCm = TownR;
		}
		const bool bGrid = Civ.Ways() == EWayStyle::Grid;
		if (NT >= 2 && KeptOf(Civ, HA) && KeptOf(Civ, HB) && RegionAt(X) * ClusterAt(X) > 0.08f && (bGrid || (!LikesCliffOf(Civ, HA) && !LikesCliffOf(Civ, HB))))
		{
			S.bRoad = true; S.A = A; S.B = B; S.HA = HA; S.HB = HB; S.RoadCutCm = TownR;
		}
		return S;
	}

	/** From a point to the edge of the road the material paints between a pair (FPaint's), cm, below 0 on it: straight
	 *  for a grid-building people, an S-curve for a grown one, winding for the early ages, out of the town at the end
	 *  nearest the point (short of that, the distance to where it begins: a bound everywhere). */
	double RoadCurveCm(const FVector& Dir, const FPaint& S, EWayStyle Style, double R)
	{
		const FVector X = Dir * R;
		const FVector Ap = (X - S.A) - Dir * FVector::DotProduct(X - S.A, Dir);
		const FVector Ab = (S.B - S.A) - Dir * FVector::DotProduct(S.B - S.A, Dir);
		const double L2 = FMath::Max(Ab.SizeSquared(), 1.0), L = FMath::Sqrt(L2);
		const double T = FMath::Clamp(FVector::DotProduct(Ap, Ab) / L2, 0.0, 1.0);
		const FVector Perp = FVector::CrossProduct(Dir, Ab).GetSafeNormal();
		const double Sgn = (Mix(S.HA ^ S.HB, 0x70u) & 1u) ? 1.0 : -1.0;
		const double St = FMath::Sin(2.0 * PI * T);
		double Off = 0.0, HW = 450.0;
		switch (Style)
		{
		case EWayStyle::Grown: Off = 0.12 * St; HW = 300.0; break;
		case EWayStyle::Paths: Off = 0.06 * St + 0.03 * FMath::Sin(6.0 * PI * T); HW = 75.0; break;
		case EWayStyle::Lanes: Off = 0.05 * St + 0.025 * FMath::Sin(4.0 * PI * T); HW = 200.0; break;
		default: break;
		}
		const double Alongs = FVector::DotProduct(Ap, Ab) / L, Lat = FVector::DotProduct(Ap, Perp);
		const double Curve = FVector2D(Alongs - T * L, Lat - Sgn * Off * L).Size() - HW;
		return T * L < S.RoadCutCm ? FMath::Max(Curve, S.RoadCutCm - T * L) : Curve;
	}

	struct FWays;
	double PaintedClearance(const FPlanetTerrainParams& P, const FCivilization& Civ, const FPaint& S, const FVector& Dir);

	/** How much faster than the distance any way's clearance can change (the grown ways' lobes and spirals, the roads'
	 *  curves): a clearance of C at a point puts the way at least C / WayLip from it. */
	constexpr double WayLip = 1.4;

	/**
	 * A town's ways, as the terrain material paints them (terrain_material.py's SlonkTownWays and SlonkRoad mirror
	 * this, number for number): a grown town's ring ways between its rings of pods and the spiral spokes out from its
	 * plaza; a village's lanes, or a hamlet's paths, wandering out from its heart; and the roads to the neighbouring
	 * settlements, out of the towns at their ends. A town's own ways are laid only where the material paints them:
	 * not at a point the people would sooner have built into a cliff (the material cannot tell whether they found
	 * one). A grid town's streets are the gaps between its lots, and it has none of these but the roads.
	 */
	struct FWays
	{
		const FPlanetTerrainParams& P;
		const FCivilization& Civ;
		EWayStyle Style;
		FTownFrame F;
		FVector Centre;
		bool bCity;
		double RadiusCm;
		bool bTownWays = false;
		uint32 Seed;
		// A grown town's: ring spacing, the lobes the rings wander by, the first ring of pods (inside it the plaza), the
		// last, the ways' half-width, and its spokes (how many, where, how they turn).
		double Ring = 1.0, Lobes = 0.0, K0 = 1.0, Rings = 1.0, GrownHW = 250.0, NS = 3.0, Phi0 = 0.0, Tw = 0.55;
		// A village's or a hamlet's: how far its lanes run, from how near its heart, how they wander, their half-width.
		double VR = 0.0, R0 = 0.0, LW = 0.0, Lam = 1.0, LaneHW = 0.0;
		int32 NL = 0;
		double LanePhi[5] = {}, LanePsi[5] = {};

		FWays(const FPlanetTerrainParams& InP, const FCivilization& InCiv, const FTownSite& Town)
			: P(InP), Civ(InCiv), Style(InCiv.Ways()), F(Town), Centre(Town.LocalCm), bCity(Town.bCity), RadiusCm(Town.RadiusCm), Seed(Town.Seed)
		{
			const uint32 S = Town.Seed;
			bTownWays = Style != EWayStyle::Grid && !Town.bCliff && !LikesCliff(S);
			if (Style == EWayStyle::Grown)
			{
				Ring = 3200.0 + 1200.0 * Hash01(Mix(S, 17u));
				Lobes = Hash01(Mix(S, 0x50u)) * 2.0 * PI;
				K0 = bCity ? 2.0 : 1.0;
				Rings = FMath::Max(K0, FMath::FloorToDouble(RadiusCm / Ring));
				GrownHW = bCity ? 350.0 : 250.0;
				NS = 3.0 + (double)(Mix(S, 0x52u) % 3u);
				Phi0 = Hash01(Mix(S, 0x53u)) * 2.0 * PI;
				Tw = Hash01(Mix(S, 0x54u)) < 0.5f ? -0.55 : 0.55;
			}
			else if (Style == EWayStyle::Lanes || Style == EWayStyle::Paths)
			{
				const bool bStone = Style == EWayStyle::Paths;
				VR = FMath::Min(RadiusCm, bStone ? 18000.0 : 40000.0);
				R0 = bStone ? 500.0 : 1200.0;
				NL = bStone ? 2 + (int32)(Mix(S, 0x60u) % 2u) : 3 + (int32)(Mix(S, 0x60u) % 3u);
				const double P0 = Hash01(Mix(S, 0x61u)) * 2.0 * PI;
				LW = bStone ? 300.0 : 600.0; Lam = bStone ? 1800.0 : 2600.0; LaneHW = bStone ? 75.0 : 175.0;
				for (int32 l = 0; l < NL; ++l)
				{
					const uint32 Hl = Mix(S, 0x62u + (uint32)l);
					LanePhi[l] = P0 + 2.0 * PI * l / NL + (Hash01(Hl) - 0.5) * 1.8849556 / NL;
					LanePsi[l] = Hash01(Mix(Hl, 5u)) * 2.0 * PI;
				}
			}
		}
		bool LikesCliff(uint32 H) const { return LikesCliffOf(Civ, H); }
		/** A point's place in the town's plane (cm along its frame's axes), and back. */
		FVector2D Planar(const FVector& Dir) const { const FVector Off = Dir * P.RadiusCm - Centre; return FVector2D(FVector::DotProduct(Off, F.GX), FVector::DotProduct(Off, F.GY)); }
		FVector DirOf(const FVector2D& XY) const { return (Centre + F.GX * XY.X + F.GY * XY.Y).GetSafeNormal(); }
		/** A grown town's plaza, inside its first ring way, is clear of the ways to this radius however its lobes turn. */
		double PlazaCm() const { return (K0 - 0.5) * Ring * 0.82 - GrownHW; }

		/**
		 * From a point of the town's plane to the edge of its nearest own way, cm (below 0 on it): within a factor of
		 * WayLip of the distance, and never more than it, so a value is a bound everywhere (a plaza's, a hamlet's heart,
		 * the country beyond its last way or a lane's far side included, where the material paints nothing).
		 */
		double TownClearance(const FVector2D& XY) const
		{
			if (!bTownWays) { return 1.0e9; }
			const double R = XY.Size(), A = FMath::Atan2(XY.Y, XY.X);
			if (Style == EWayStyle::Grown)
			{
				// Ring ways halfway between the rings of pods (the same lobed shape scaled ring by ring), and spokes that
				// turn as they go, at a fixed angle to the radius (logarithmic spirals) between the first ring way and the
				// last: inside the plaza and beyond the last, the nearest way is that ring way.
				const double W = 1.0 + 0.18 * FMath::Sin(3.0 * A + Lobes);
				const double Rho = R / (Ring * W);
				const double Inner = K0 - 0.5;
				const double Kk = FMath::Clamp(FMath::RoundToDouble(Rho - 0.5), K0 - 1.0, Rings);
				const double RingClr = FMath::Abs(Rho - (Kk + 0.5)) * Ring * W - GrownHW;
				if (Rho < Inner || Rho > Rings + 0.5) { return RingClr; }
				const double Sector = 2.0 * PI / NS;
				const double D0 = A - Phi0 - Tw * FMath::Loge(Rho / Inner);
				const double Dl = D0 - Sector * FMath::RoundToDouble(D0 / Sector);
				return FMath::Min(RingClr, R * FMath::Abs(Dl) / FMath::Sqrt(1.0 + Tw * Tw) - GrownHW);
			}
			// Lanes out from the heart, each wandering to either side of its line (straight at first). Inside the heart
			// and beyond the village none: the nearest is where one starts or ends; on a lane's far side, the heart.
			if (R < R0) { return R0 - R; }
			if (R > VR) { return R - VR; }
			const double Ramp = FMath::SmoothStep(0.0, 3000.0, R);
			double Best = 1.0e9;
			for (int32 l = 0; l < NL; ++l)
			{
				double Da = A - LanePhi[l];
				Da -= 2.0 * PI * FMath::RoundToDouble(Da / (2.0 * PI));
				if (FMath::Abs(Da) > 0.5 * PI) { Best = FMath::Min(Best, FMath::Max(R0, R - LW) - LaneHW); continue; }
				const double Lat = LW * FMath::Sin(R / Lam + LanePsi[l]) * Ramp;
				Best = FMath::Min(Best, FMath::Abs(R * FMath::Sin(Da) - Lat) - LaneHW);
			}
			return Best;
		}

		/** A village's nearest lane at a point of its plane: how far (as TownClearance), and the way out from it to the
		 *  point. False away from the lanes. */
		bool NearestLane(const FVector2D& XY, double& OutClearance, FVector2D& OutAway) const
		{
			if (!bTownWays || (Style != EWayStyle::Lanes && Style != EWayStyle::Paths)) { return false; }
			const double R = XY.Size(), A = FMath::Atan2(XY.Y, XY.X);
			if (R < R0 || R > VR) { return false; }
			const double Ramp = FMath::SmoothStep(0.0, 3000.0, R);
			const double U = FMath::Clamp(R / 3000.0, 0.0, 1.0), DRamp = 6.0 * U * (1.0 - U) / 3000.0;
			OutClearance = 1.0e9;
			for (int32 l = 0; l < NL; ++l)
			{
				double Da = A - LanePhi[l];
				Da -= 2.0 * PI * FMath::RoundToDouble(Da / (2.0 * PI));
				if (FMath::Abs(Da) > 0.5 * PI) { continue; }
				const double Lat = LW * FMath::Sin(R / Lam + LanePsi[l]) * Ramp;
				const double Side = R * FMath::Sin(Da) - Lat;
				if (FMath::Abs(Side) - LaneHW >= OutClearance) { continue; }
				OutClearance = FMath::Abs(Side) - LaneHW;
				// The lane runs out along its own line (not the point's bearing), turned by its wander's slope (the ramp's
				// included); out from it is square to that, on the point's side.
				const FVector2D Along2(FMath::Cos(LanePhi[l]), FMath::Sin(LanePhi[l])), Across2(-Along2.Y, Along2.X);
				const double Slope = LW * (FMath::Cos(R / Lam + LanePsi[l]) / Lam * Ramp + FMath::Sin(R / Lam + LanePsi[l]) * DRamp);
				const FVector2D T = (Along2 + Across2 * Slope).GetSafeNormal();
				OutAway = FVector2D(-T.Y, T.X) * (Side >= 0.0 ? 1.0 : -1.0);
			}
			return OutClearance < 1.0e8;
		}

		/**
		 * Whether a footprint in the town's plane (its centre, its X axis, W along it and D across) keeps Margin clear of
		 * every way the material paints over it: this town's own, another settlement's where the material gives the
		 * ground to that one, and the roads. Where the material paints the same settlement and the same road at its
		 * centre and corners, the clearance there is one smooth bound and the centre's settles it when it is far enough
		 * (WayLip); otherwise, or near, every 1.5 m over it, so with a 1.5 m margin no part of it is on a way.
		 */
		bool Clear(const FVector2D& C, const FVector2D& X, double W, double D, double Margin = 150.0) const
		{
			const FVector2D Xn = X.GetSafeNormal(), Yn(-Xn.Y, Xn.X);
			const int32 Nx = FMath::Max(1, FMath::CeilToInt(W / 150.0)), Ny = FMath::Max(1, FMath::CeilToInt(D / 150.0));
			auto At = [&](int32 i, int32 j) { return DirOf(C + Xn * ((i / (double)Nx - 0.5) * W) + Yn * ((j / (double)Ny - 0.5) * D)); };
			const FPaint P0 = PaintAt(P, Civ, DirOf(C));
			bool bSame = true;
			// A city's own ground is a disc round its point with no road on it: corners inside it need no asking.
			const bool bOwnCity = P0.bCityGround && bCity && P0.Site.Seed == Seed;
			for (const FIntPoint Cn : { FIntPoint(0, 0), FIntPoint(Nx, 0), FIntPoint(0, Ny), FIntPoint(Nx, Ny) })
			{
				if (bOwnCity && FVector::Dist(At(Cn.X, Cn.Y) * P.RadiusCm, Centre) < RadiusCm * 1.05 - 100.0) { continue; }
				const FPaint Pc = PaintAt(P, Civ, At(Cn.X, Cn.Y));
				bSame = bSame && Pc.SameSite(P0) && Pc.SameRoad(P0);
			}
			if (bSame)
			{
				// One settlement's ways and one road over all of it: their clearance, the settlement's ways built once.
				TOptional<FWays> Site;
				if (P0.bSite && P0.bKept && Style != EWayStyle::Grid) { Site.Emplace(P, Civ, P0.Site); }
				auto Clr = [&](const FVector& Dq)
				{
					double Cq = Site.IsSet() ? Site->TownClearance(Site->Planar(Dq)) : 1.0e9;
					if (P0.bRoad) { Cq = FMath::Min(Cq, RoadCurveCm(Dq, P0, Style, P.RadiusCm)); }
					return Cq;
				};
				if (Clr(DirOf(C)) >= WayLip * HalfDiag(W, D) + Margin) { return true; }
				for (int32 i = 0; i <= Nx; ++i) { for (int32 j = 0; j <= Ny; ++j) { if (Clr(At(i, j)) < Margin) { return false; } } }
				return true;
			}
			for (int32 i = 0; i <= Nx; ++i)
			{
				for (int32 j = 0; j <= Ny; ++j)
				{
					const FVector Dq = At(i, j);
					if (PaintedClearance(P, Civ, PaintAt(P, Civ, Dq), Dq) < Margin) { return false; }
				}
			}
			return true;
		}
	};

	double PaintedClearance(const FPlanetTerrainParams& P, const FCivilization& Civ, const FPaint& S, const FVector& Dir)
	{
		double C = 1.0e9;
		if (S.bSite && S.bKept && Civ.Ways() != EWayStyle::Grid)
		{
			const FWays W(P, Civ, S.Site);
			C = W.TownClearance(W.Planar(Dir));
		}
		if (S.bRoad) { C = FMath::Min(C, RoadCurveCm(Dir, S, Civ.Ways(), P.RadiusCm)); }
		return C;
	}

	/** The grid towns of the industrial, modern and starfaring built peoples: blocks between streets, as the terrain
	 *  material paves them. */
	void BuildGridTown(const FPlanetTerrainParams& P, const FCivilization& Civ, const FTownSite& Town, TArray<FTransform>* OutByKind, int32 MaxBuildings)
	{
		const double R = P.RadiusCm;
		const FTownFrame F(Town);
		const FOwnGround Own(P, Town);
		FLots Lots;
		// Keeps off the ways the terrain paints over its ground.
		const FWays Ways(P, Civ, Town);
		const double Pitch = 3600.0 + 1400.0 * Hash01(Mix(Town.Seed, 17u));		// 36 to 50 m lots
		const int32 Half = FMath::CeilToInt(Town.RadiusCm / Pitch);
		int32 Count = 0;
		// A strong town has a landing pad at its heart: a 90 m slab half a metre proud of the ground (on a footing
		// where the ground falls away under it), the lots around it left clear. The material reads the slab's height
		// (under three metres) to paint it as a pad. Not before the modern age.
		const bool bPad = Town.bCity && Civ.Age >= ETechAge::Modern;
		if (bPad)
		{
			const FPlanetTerrainSample S0 = SlonkTerrain::Sample(P, F.Up, 400.0);
			if (!S0.bWater && Own.Owns(F.Up, HalfDiag(9000.0, 9000.0)) && Ways.Clear(FVector2D::ZeroVector, FVector2D(1.0, 0.0), 9000.0, 9000.0))
			{
				PlaceOnGround(OutByKind, EBuildingKind::Block, F.Up, F.GX, R, S0, 9000.0, 9000.0, 50.0, 150.0, true);
				Lots.Take(FVector2D::ZeroVector, FVector2D(1.0, 0.0), 9000.0, 9000.0);
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
			const FVector2D C2(gx * Pitch + Jit.X, gy * Pitch + Jit.Y);
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
			// In the town's own cell, and clear of its neighbours (a works is broader than its lot).
			// Off the ways the material paints over it (the cheapest test of the lot first), on dry, level ground (no
			// building in the water, or on a cliff: a town at a scarp's or a canyon's rim ends at the edge rather than
			// standing on stilts down the face), in the town's own cell, and clear of its neighbours (a works is broader
			// than its lot).
			if (!Ways.Clear(C2, FVector2D(1.0, 0.0), W, Dp)) { continue; }
			const FVector Dir = Ways.DirOf(C2);
			const FPlanetTerrainSample S = SlonkTerrain::Sample(P, Dir, 400.0);
			if (!Buildable(S) || !Own.Owns(Dir, HalfDiag(W, Dp)) || !Lots.Take(C2, FVector2D(1.0, 0.0), W, Dp)) { continue; }
			PlaceOnGround(OutByKind, Kind, Dir, F.GX, R, S, W, Dp, Hgt, 150.0, true);
			++Count;
		}
	}

	/** A stone-age hamlet: huts in loose clusters round a fire, a longhouse or two, fires between them; an iron-age
	 *  village: houses (adobe on a desert) turned every way along lanes that wander, round a keep in a strong one and a
	 *  fire in the rest. */
	void BuildVillage(const FPlanetTerrainParams& P, const FCivilization& Civ, const FTownSite& Town, TArray<FTransform>* OutByKind, int32 MaxBuildings)
	{
		const double R = P.RadiusCm;
		const FTownFrame F(Town);
		const FOwnGround Own(P, Town);
		FLots Lots;
		// Keeps off the ways the terrain paints over its ground.
		const FWays Ways(P, Civ, Town);
		const bool bStone = Civ.Age == ETechAge::Stone;
		const bool bAdobe = Civ.WetMin < 0.f;
		const double Pitch = bStone ? 1500.0 + 500.0 * Hash01(Mix(Town.Seed, 17u)) : 2000.0 + 600.0 * Hash01(Mix(Town.Seed, 17u));
		// A hamlet is small whatever its point's weight.
		const double Radius = bStone ? FMath::Min(Town.RadiusCm, 18000.0) : FMath::Min(Town.RadiusCm, 40000.0);
		const int32 Half = FMath::CeilToInt(Radius / Pitch);
		int32 Count = 0;
		{
			// The heart: a round keep for a strong iron-age village, a fire for any other, lit day and night.
			const FPlanetTerrainSample S0 = SlonkTerrain::Sample(P, F.Up, 400.0);
			const bool bKeep = !bStone && Town.bCity;
			const double W0 = bKeep ? 1400.0 : 250.0;
			if (Buildable(S0) && Own.Owns(F.Up, HalfDiag(W0, W0)))
			{
				Lots.Take(FVector2D::ZeroVector, FVector2D(1.0, 0.0), W0, W0, 300.0);
				if (bKeep) { PlaceOnGround(OutByKind, EBuildingKind::RoundTower, F.Up, F.GX, R, S0, W0, W0, 2200.0, 150.0, true); }
				else { PlaceOnGround(OutByKind, EBuildingKind::Fire, F.Up, F.GX, R, S0, W0, W0, 180.0, 20.0, false); }
				++Count;
			}
		}
		for (int32 gy = -Half; gy <= Half && Count < MaxBuildings; ++gy)
		for (int32 gx = -Half; gx <= Half && Count < MaxBuildings; ++gx)
		{
			const uint32 H = Mix(Town.Seed, (uint32)(gx + 4096) * 65537u + (uint32)(gy + 4096));
			// Loose: each lot wanders most of a lot from its place, so no rows show (a lot that comes too near one
			// already laid out is left empty).
			const FVector Jit((Hash01(Mix(H, 3u)) - 0.5) * 0.9 * Pitch, (Hash01(Mix(H, 5u)) - 0.5) * 0.9 * Pitch, 0.0);
			const double Dx = gx * Pitch + Jit.X, Dy = gy * Pitch + Jit.Y;
			const double D2 = (Dx * Dx + Dy * Dy) / FMath::Max(Radius * Radius, 1.0);
			if (D2 > 1.0) { continue; }
			const float Density = (bStone ? 0.45f : 0.6f) * (float)(1.0 - D2);
			if (Hash01(H) > Density) { continue; }
			const double Yaw = Hash01(Mix(H, 23u)) * 2.0 * PI;
			FVector2D C2(Dx, Dy), X2(FMath::Cos(Yaw), FMath::Sin(Yaw));
			const float Pick = Hash01(Mix(H, 19u));
			EBuildingKind Kind; double W, D, Hgt, Sink;
			bool bFooting = false;
			if (Hash01(Mix(H, 41u)) < 0.125f)
			{
				// One lot in eight is a fire between the houses: a cooking fire, a forge, a lamp at a door.
				Kind = EBuildingKind::Fire; W = D = 120.0 + 100.0 * Hash01(Mix(H, 43u)); Hgt = W * 0.8; Sink = 20.0;
			}
			else if (bStone)
			{
				// Huts 4 to 8 m across; one in eight a longhouse, drawn out to three times its width.
				const double Wd = 400.0 + 400.0 * Hash01(Mix(H, 7u));
				Kind = EBuildingKind::Hut; W = Pick < 0.12f ? Wd * 2.6 : Wd; D = Wd; Hgt = 300.0 + 250.0 * Hash01(Mix(H, 13u)); Sink = 60.0;
			}
			else
			{
				Kind = bAdobe ? EBuildingKind::Pueblo : (Pick < 0.15f ? EBuildingKind::Hut : EBuildingKind::House);
				W = 700.0 + 600.0 * Hash01(Mix(H, 7u)); D = 600.0 + 500.0 * Hash01(Mix(H, 9u)); Hgt = 450.0 + 350.0 * Hash01(Mix(H, 13u)); Sink = 100.0;
				bFooting = Kind != EBuildingKind::Hut;
			}
			// Turned any way where there is no lane to face. Near one, a house fronts onto it: turned along it, its door
			// side (the mesh's -Y) toward it, set back far enough to keep its corners off it. A fire is wherever it is.
			double LaneClr = 0.0;
			FVector2D Away;
			if (Kind != EBuildingKind::Fire && Ways.NearestLane(C2, LaneClr, Away) && LaneClr < Pitch * 0.6)
			{
				X2 = FVector2D(Away.Y, -Away.X);
				const double Need = D * 0.5 + 250.0;
				if (LaneClr < Need) { C2 += Away * (Need - LaneClr); }
			}
			if (!Ways.Clear(C2, X2, W, D)) { continue; }
			const FVector Dir = Ways.DirOf(C2);
			const FPlanetTerrainSample S = SlonkTerrain::Sample(P, Dir, 400.0);
			if (!Buildable(S)) { continue; }
			const FVector Facing = F.GX * X2.X + F.GY * X2.Y;
			if (!Own.Owns(Dir, HalfDiag(W, D)) || !Lots.Take(C2, X2, W, D)) { continue; }
			PlaceOnGround(OutByKind, Kind, Dir, Facing, R, S, W, D, Hgt, Sink, bFooting);
			++Count;
		}
	}

	/** A grown town: rings of pods round a plaza and its bulb towers, ring ways between them curving side by side and
	 *  spiral spokes out from the plaza (FWays), every building turned to face the heart. */
	void BuildGrownTown(const FPlanetTerrainParams& P, const FCivilization& Civ, const FTownSite& Town, TArray<FTransform>* OutByKind, int32 MaxBuildings)
	{
		const double R = P.RadiusCm;
		const FTownFrame F(Town);
		const FOwnGround Own(P, Town);
		FLots Lots;
		// Keeps off the ways the terrain paints over its ground.
		const FWays Ways(P, Civ, Town);
		const double Ring = Ways.Ring;
		const int32 K0 = (int32)Ways.K0, Rings = (int32)Ways.Rings;
		const double Plaza = Ways.PlazaCm();
		int32 Count = 0;
		// The heart: a bulb tower, and in a city a cluster of them, the tallest a hundred metres and more, all on the
		// plaza inside the first ring way (a city's plaza takes in its first two rings). Laid out first, so the pods keep
		// clear of them.
		const int32 Hearts = Town.bCity ? 3 + (int32)(Hash01(Mix(Town.Seed, 29u)) * 4.f) : 1;
		for (int32 h = 0; h < Hearts && Count < MaxBuildings; ++h)
		{
			const uint32 H = Mix(Town.Seed, 0xB0u + (uint32)h);
			const double Hgt = (Town.bCity ? 6000.0 + 9000.0 * Town.Strength : 2500.0) * (0.6 + 0.4 * Hash01(Mix(H, 3u)));
			// Square in plan to the lots and the ways: its corners, not its round sides, kept on the plaza.
			const double W = FMath::Min(Hgt * (0.3 + 0.1 * Hash01(Mix(H, 4u))), 2.0 * (Plaza - 300.0) / UE_SQRT_2);
			if (W < 300.0) { continue; }
			const double A = Hash01(Mix(H, 1u)) * 2.0 * PI;
			const double Rd = h == 0 ? 0.0 : FMath::Min(Ring * 0.6 * (0.5 + Hash01(Mix(H, 2u))), FMath::Max(0.0, Plaza - HalfDiag(W, W) - 300.0));
			const FVector Dir = (Town.LocalCm + (F.GX * FMath::Cos(A) + F.GY * FMath::Sin(A)) * Rd).GetSafeNormal();
			const FPlanetTerrainSample S = SlonkTerrain::Sample(P, Dir, 400.0);
			if (!Buildable(S)) { continue; }
			if (!Own.Owns(Dir, HalfDiag(W, W)) || !Ways.Clear(FVector2D(FMath::Cos(A), FMath::Sin(A)) * Rd, FVector2D(1.0, 0.0), W, W) || !Lots.Take(FVector2D(FMath::Cos(A), FMath::Sin(A)) * Rd, FVector2D(1.0, 0.0), W, W, 400.0)) { continue; }
			PlaceOnGround(OutByKind, EBuildingKind::BulbTower, Dir, F.GX, R, S, W, W, Hgt, 200.0, false);
			++Count;
		}
		// The rings wander in and out together (one lobed shape, scaled ring by ring), so the ways between them curve
		// side by side and never cross; each pod strays a little from its ring, and none stands on a way.
		const double Lobes = Ways.Lobes;
		for (int32 k = K0; k <= Rings && Count < MaxBuildings; ++k)
		{
			const double Rk = k * Ring;
			const int32 N = FMath::Max(6, FMath::FloorToInt(2.0 * PI * Rk / 2200.0));
			const double Phase = Hash01(Mix(Town.Seed, 0x51u + (uint32)k)) * 2.0 * PI;
			for (int32 n = 0; n < N && Count < MaxBuildings; ++n)
			{
				const uint32 H = Mix(Town.Seed, (uint32)k * 7919u + (uint32)n);
				// Sparser than a built town's lots: gardens between the pods, thinning outward.
				if (Hash01(H) > 0.45f * (1.0f - 0.5f * (float)k / Rings)) { continue; }
				const double A = Phase + 2.0 * PI * (n + 0.35 * (Hash01(Mix(H, 3u)) - 0.5)) / N;
				const double Rr = Rk * (1.0 + 0.18 * FMath::Sin(A * 3.0 + Lobes)) + Ring * 0.2 * (Hash01(Mix(H, 5u)) - 0.5);
				const FVector Out = F.GX * FMath::Cos(A) + F.GY * FMath::Sin(A);
				const FVector2D C2 = FVector2D(FMath::Cos(A), FMath::Sin(A)) * Rr;
				const float Pick = Hash01(Mix(H, 19u));
				const double W = 800.0 + 900.0 * Hash01(Mix(H, 7u));
				const bool bDome = Pick < 0.18f;
				const double Wd = bDome ? W * 1.5 : W, Dd = bDome ? W * 1.5 : W * (0.8 + 0.4 * Hash01(Mix(H, 9u)));
				const double Hgt = bDome ? W * 0.6 : W * (0.7 + 0.6 * Hash01(Mix(H, 13u)));
				if (!Ways.Clear(C2, FVector2D(FMath::Cos(A), FMath::Sin(A)), Wd, Dd)) { continue; }
				const FVector Dir = (Town.LocalCm + Out * Rr).GetSafeNormal();
				const FPlanetTerrainSample S = SlonkTerrain::Sample(P, Dir, 400.0);
				if (!Buildable(S) || !Own.Owns(Dir, HalfDiag(Wd, Dd)) || !Lots.Take(C2, FVector2D(FMath::Cos(A), FMath::Sin(A)), Wd, Dd)) { continue; }
				PlaceOnGround(OutByKind, bDome ? EBuildingKind::Dome : EBuildingKind::Pod, Dir, Out, R, S, Wd, Dd, Hgt, 150.0, false);
				++Count;
			}
		}
	}

	/**
	 * A town built into a cliff (Town.bCliff): along the wall, either side of the town's point, the profile across
	 * the wall is read and on each ledge at the foot of a rise the people build against the rock behind it, the rear
	 * of each building set into the rise: adobe houses stepped back a storey and round towers for the early ages,
	 * houses and blocks for the later, a facade of glass deep in the rock for the starfaring; a grown people's pods
	 * on the ledges and on the faces between them. Rooms run together along a ledge, as a cliff palace's do; the town
	 * keeps to its point's cell.
	 */
	void BuildCliffTown(const FPlanetTerrainParams& P, const FCivilization& Civ, const FTownSite& Town, TArray<FTransform>* OutByKind, int32 MaxBuildings)
	{
		const double R = P.RadiusCm;
		const FOwnGround Own(P, Town);
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
			// The ground at a point of this line: Across out from the rock, AlongWall along it.
			auto Ground = [&](double Across, double AlongWall) { return SlonkTerrain::Sample(P, (Mid * R + Face * Across + Strike * AlongWall).GetSafeNormal(), 300.0); };
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
				// Its rear set into the riser, its floor at the riser's foot.
				const double Tc = T + D * (0.5 - Embed);
				const FVector Dir = Along(P, Mid, Face, Tc);
				const FPlanetTerrainSample Sf = Ground(T + 200.0, 0.0);
				if (Sf.bWater) { continue; }
				// Both rear corners in the rock, where the wall bends away from the line across it too: or a narrower
				// house, or none.
				auto BackInRock = [&](double Wd) { const double Ar = Tc - D * 0.5; return Ground(Ar, -Wd * 0.5).HeightCm > Sf.HeightCm + 150.0 && Ground(Ar, Wd * 0.5).HeightCm > Sf.HeightCm + 150.0; };
				if (!BackInRock(W)) { W *= 0.6; if (Kind == EBuildingKind::RoundTower || Kind == EBuildingKind::Pod) { D = W; } if (!BackInRock(W)) { continue; } }
				if (!Own.Owns(Dir, HalfDiag(W, D))) { continue; }
				// The ground under its front: the footing goes down to the lowest of it, corners and middle, so nothing
				// hangs over the drop (no deeper than the house's own depth: a steeper apron is left bare). The footing
				// is masonry of its own; the house stands on it at its floor.
				const double Af = Tc + D * 0.5;
				const double Front = FMath::Min3(Ground(Af, -W * 0.5).HeightCm, Ground(Af, W * 0.5).HeightCm, Ground(Af, 0.0).HeightCm);
				const double Footing = FMath::Max(Sf.HeightCm - Front, 0.0);
				if (Footing > D) { continue; }
				// Facing out from the rock (the mesh's rear, +Y, into it): X along the wall. A grown pod has no masonry
				// under it: it grows down to the apron.
				const bool bFooting = Footing > 50.0 && !Civ.IsOrganic();
				if (bFooting)
				{
					Place(OutByKind, EBuildingKind::Footing, Dir, Strike, R, Sf.HeightCm, W, D, 20.0, Footing + 100.0);
					++Count;
				}
				Place(OutByKind, Kind, Dir, Strike, R, Sf.HeightCm, W, D, Hgt, bFooting ? 20.0 : Footing + 100.0);
				++Count;
				// An early people's fire on the ledge in front of one house in five.
				if (bEarly && Hash01(Mix(H, 47u)) < 0.2f && Count < MaxBuildings)
				{
					const FVector FireDir = Along(P, Mid, Face, Af + 250.0);
					const FPlanetTerrainSample Sfire = SlonkTerrain::Sample(P, FireDir, 300.0);
					if (!Sfire.bWater && Sfire.HeightCm > Front - 300.0 && Own.Owns(FireDir, 120.0))
					{
						Place(OutByKind, EBuildingKind::Fire, FireDir, Strike, R, Sfire.HeightCm, 160.0, 160.0, 130.0, 20.0 + 160.0 * Sfire.Slope);
						++Count;
					}
				}
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
					const double T = -SpanCm + k * DtCm;
					const double W = bGlass ? 1600.0 + 1000.0 * Hash01(Mix(H, 7u)) : 1000.0 + 1200.0 * Hash01(Mix(H, 7u));
					const double D = bGlass ? 1400.0 : W;
					const double Hgt = bGlass ? 800.0 + 600.0 * Hash01(Mix(H, 13u)) : W * 1.1;
					// Its centre a little out from the face at that height, the rest in the rock.
					const FVector Dir = Along(P, Mid, Face, T + D * (bGlass ? -0.25 : 0.05));
					if (!Own.Owns(Dir, HalfDiag(W, D))) { continue; }
					++OnFace;
					Place(OutByKind, bGlass ? EBuildingKind::Block : EBuildingKind::Pod, Dir, Strike, R, Hp[k] - Hgt * 0.5, W, D, Hgt, 0.0);
					++Count;
				}
			}
		}
	}
}

double SlonkCities::PaintedWayCm(const FPlanetTerrainParams& P, const FCivilization& Civ, const FVector& Dir)
{
	return PaintedClearance(P, Civ, PaintAt(P, Civ, Dir), Dir);
}

FString SlonkCities::DescribePaint(const FPlanetTerrainParams& P, const FCivilization& Civ, const FVector& Dir)
{
	const FPaint S = PaintAt(P, Civ, Dir);
	FString Out = S.bSite ? FString::Printf(TEXT("%s %u (%s, radius %.0f m, %.0f m from it)"), S.Site.bCity ? TEXT("city") : TEXT("town"), S.Site.Seed,
		S.bKept ? TEXT("lived at") : TEXT("empty"), S.Site.RadiusCm / 100.0, FVector::Dist(Dir * P.RadiusCm, S.Site.LocalCm) / 100.0) : FString(TEXT("no settlement's ground"));
	if (S.bRoad) { Out += FString::Printf(TEXT("; road %u-%u"), S.HA, S.HB); }
	return Out + FString::Printf(TEXT("; way clearance %.0f cm"), PaintedClearance(P, Civ, S, Dir));
}

void SlonkCities::BuildTown(const FPlanetTerrainParams& P, const FCivilization& Civ, const FTownSite& Town, TArray<FTransform>* OutByKind, int32 MaxBuildings)
{
	if (Town.bCliff) { BuildCliffTown(P, Civ, Town, OutByKind, MaxBuildings); }
	else if (Civ.IsOrganic()) { BuildGrownTown(P, Civ, Town, OutByKind, MaxBuildings); }
	else if (Civ.Age <= ETechAge::Iron) { BuildVillage(P, Civ, Town, OutByKind, MaxBuildings); }
	else { BuildGridTown(P, Civ, Town, OutByKind, MaxBuildings); }
}
