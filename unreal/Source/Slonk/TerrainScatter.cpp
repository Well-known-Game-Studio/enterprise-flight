#include "TerrainScatter.h"
#include "PlanetCities.h"

namespace
{
	FORCEINLINE uint64 Mix64(uint64 X)
	{
		X ^= X >> 33; X *= 0xff51afd7ed558ccdULL; X ^= X >> 33; X *= 0xc4ceb9fe1a85ec53ULL; X ^= X >> 33;
		return X;
	}
	FORCEINLINE uint64 CellHash(int32 Face, int32 Level, int64 GX, int64 GY, uint64 Salt)
	{
		return Mix64(Mix64(Mix64((uint64)GX * 0x9E3779B97F4A7C15ULL ^ Salt) ^ (uint64)GY * 0xC2B2AE3D27D4EB4FULL) ^ (((uint64)Face << 8) | (uint64)Level));
	}
	/** Successive unit values in [0, 1) from one cell's hash, sixteen bits at a time, remixed when they run out. */
	struct FDraw
	{
		uint64 H;
		int32 Used = 0;
		explicit FDraw(uint64 InH) : H(InH) {}
		double Next()
		{
			if (Used == 4) { H = Mix64(H + 0x9E3779B97F4A7C15ULL); Used = 0; }
			const double V = (double)((H >> (16 * Used)) & 0xffffu) / 65536.0;
			++Used;
			return V;
		}
	};
	FORCEINLINE double SmoothStep(double Lo, double Hi, double V)
	{
		const double T = FMath::Clamp((V - Lo) / (Hi - Lo), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	/** A size class: a lattice of candidates, the chunks it rides on, and how big its rocks are. */
	struct FClass
	{
		/** Wanted spacing of the lattice, cm; it snaps to the nearest level of the quadtree on each planet. */
		double SpacingCm;
		/** Rides on chunks with edges this long or shorter, cm. */
		double MaxEdgeCm;
		/** Across the largest axis, cm. */
		double MinCm, MaxCm;
		uint64 Salt;
	};
	// Boulders on chunks to 150-odd metres (seen to about 600 m), stones to 40 m (150 m), pebbles on the last level
	// (35 m). Each shows down to a couple of pixels before the coarser chunk that lacks it takes over.
	constexpr FClass Classes[3] = {
		{ 750.0, 26000.0, 120.0, 420.0, 0x51ed270b2f1c7e3dULL },
		{ 220.0, 6500.0, 26.0, 95.0, 0x2545f4914f6cdd1dULL },
		{ 60.0, 1700.0, 6.0, 22.0, 0x8c4b5bd7d9a1e5f3ULL },
	};

	/** Height to width of each shape, and the second horizontal axis to the first: ranges a rock is drawn from. */
	struct FProportion { double ZLo, ZHi, YLo, YHi, TiltDeg; };
	constexpr FProportion Proportions[SlonkGroundRocks::NumKinds] = {
		{ 0.50, 0.85, 0.65, 1.0, 10.0 },	// Angular
		{ 0.55, 0.95, 0.60, 1.0, 7.0 },		// Block
		{ 0.16, 0.32, 0.55, 1.0, 5.0 },		// Slab
		{ 0.45, 0.75, 0.65, 1.0, 10.0 },	// Rounded
	};

	int32 PickKind(const double (&W)[SlonkGroundRocks::NumKinds], double U)
	{
		double Sum = 0.0;
		for (double V : W) { Sum += FMath::Max(V, 0.0); }
		double Acc = 0.0;
		for (int32 K = 0; K < SlonkGroundRocks::NumKinds; ++K)
		{
			Acc += FMath::Max(W[K], 0.0);
			if (U * Sum < Acc) { return K; }
		}
		return 0;
	}
}

int32 SlonkScatter::FChunkScatter::Num() const
{
	int32 N = 0;
	for (const TArray<FTransform>& T : Transforms) { N += T.Num(); }
	return N;
}

double SlonkScatter::MaxChunkEdgeCm()
{
	double M = 0.0;
	for (const FClass& C : Classes) { M = FMath::Max(M, C.MaxEdgeCm); }
	return M;
}

void SlonkScatter::ScatterChunk(const FPlanetTerrainParams& P, const FScatterSettings& S, const FChunkGrid& G, FChunkScatter& Out)
{
	if (!S.bEnabled || S.Density <= 0.f || G.EdgeCm > MaxChunkEdgeCm() || !G.Samples || !G.Dirs || !G.Pos || G.Steps <= 0) { return; }
	const double R = P.RadiusCm;
	const double Cells = (double)((int64)1 << G.Depth);
	const double Wd = 2.0 / Cells;
	const double U0 = -1.0 + Wd * (double)G.X, V0 = -1.0 + Wd * (double)G.Y;
	const double U1 = U0 + Wd, V1 = V0 + Wd;
	const double FaceArcCm = R * UE_DOUBLE_HALF_PI;

	// Settlements: the towns the planet would build near this chunk, as circles the rocks keep out of.
	struct FKeepOut { FVector LocalCm; double RadiusSq; };
	TArray<FKeepOut> KeepOut;
	if (S.bCities)
	{
		TArray<FTownSite> Towns;
		SlonkCities::EnumerateTowns(P, G.Origin, G.EdgeCm * 0.75 + SlonkCities::CityRadiusCm(1.f) + 5000.0, Towns, 64);
		for (const FTownSite& T : Towns) { KeepOut.Add({ T.LocalCm, FMath::Square(T.RadiusCm + 2500.0) }); }
	}

	auto Index = [&](int32 i, int32 j) { return (j + G.Margin) * G.W + (i + G.Margin); };
	int32 Total = 0;
	for (int32 CI = 0; CI < (int32)UE_ARRAY_COUNT(Classes) && Total < MaxPerChunk; ++CI)
	{
		const FClass& Cl = Classes[CI];
		if (G.EdgeCm > Cl.MaxEdgeCm) { continue; }
		// The class's lattice level: cells of 2 / 2^L in face coordinates, nested in the chunks of the quadtree.
		const int32 L = FMath::Clamp(FMath::RoundToInt32(FMath::Log2(FaceArcCm / Cl.SpacingCm)), 0, 40);
		const double Delta = 2.0 / (double)((int64)1 << L);
		const int64 GX0 = (int64)FMath::FloorToDouble((U0 + 1.0) / Delta), GX1 = (int64)FMath::CeilToDouble((U1 + 1.0) / Delta) - 1;
		const int64 GY0 = (int64)FMath::FloorToDouble((V0 + 1.0) / Delta), GY1 = (int64)FMath::CeilToDouble((V1 + 1.0) / Delta) - 1;
		const bool bBoulder = CI == 0, bPebble = CI == 2;
		for (int64 GY = GY0; GY <= GY1 && Total < MaxPerChunk; ++GY)
		for (int64 GX = GX0; GX <= GX1 && Total < MaxPerChunk; ++GX)
		{
			FDraw D(CellHash(G.Face, L, GX, GY, Cl.Salt));
			// The candidate: anywhere in its cell. Half-open windows put it in exactly one chunk of each level.
			const double U = -1.0 + ((double)GX + D.Next()) * Delta, V = -1.0 + ((double)GY + D.Next()) * Delta;
			if (U < U0 || U >= U1 || V < V0 || V >= V1) { continue; }
			const double Fu = (U - U0) / Wd * G.Steps, Fv = (V - V0) / Wd * G.Steps;
			const int32 i = FMath::Min((int32)Fu, G.Steps - 1), j = FMath::Min((int32)Fv, G.Steps - 1);
			const double fx = Fu - i, fy = Fv - j;
			const int32 kA = Index(i, j), kB = Index(i + 1, j), kC = Index(i, j + 1), kD = Index(i + 1, j + 1);
			const FPlanetTerrainSample& SA = G.Samples[kA]; const FPlanetTerrainSample& SB = G.Samples[kB];
			const FPlanetTerrainSample& SC = G.Samples[kC]; const FPlanetTerrainSample& SD = G.Samples[kD];
			if (SA.bWater || SB.bWater || SC.bWater || SD.bWater) { continue; }
			auto Bi = [&](float a, float b, float c, float d) { return FMath::Lerp(FMath::Lerp((double)a, (double)b, fx), FMath::Lerp((double)c, (double)d, fx), fy); };
			const double Slope = Bi(SA.Slope, SB.Slope, SC.Slope, SD.Slope);
			const double Rock = Bi(SA.Rock, SB.Rock, SC.Rock, SD.Rock);
			const double Wet = Bi(SA.Wet, SB.Wet, SC.Wet, SD.Wet);
			const double Ice = Bi(SA.Ice, SB.Ice, SC.Ice, SD.Ice);
			const double Canyon = Bi(SA.Canyon, SB.Canyon, SC.Canyon, SD.Canyon);
			const double Wall = Bi(SA.Wall, SB.Wall, SC.Wall, SD.Wall);
			const double Crater = Bi(SA.Crater, SB.Crater, SC.Crater, SD.Crater);
			const double Table = Bi(SA.Table, SB.Table, SC.Table, SD.Table);
			const double River = Bi(SA.River, SB.River, SC.River, SD.River);
			if (River > 0.15) { continue; }

			// The refinement's regimes from the structure's slope (the same at every level of the quadtree), so a rock
			// is kept or not alike on a chunk and on its children.
			const double Cliff = SmoothStep(0.7, 1.3, Slope);
			if (Cliff > 0.6) { continue; }
			const double Talus = SmoothStep(0.18, 0.5, Slope) * (1.0 - Cliff);
			const double Flat = 1.0 - SmoothStep(0.12, 0.4, Slope);
			const double Rocky = SmoothStep(0.35, 0.8, Rock) * (1.0 - Cliff);
			const double WallFoot = SmoothStep(0.05, 0.3, Wall) * (1.0 - Cliff);
			// The canyon floor: sediment, cobbles along the water.
			const double Gorge = Canyon * (1.0 - SmoothStep(0.03, 0.2, Slope));
			const double Air = P.bAirless ? 1.0 : 0.0;
			const double Dry = 1.0 - Wet;
			// Snow and ice bury all but the biggest.
			const double Cover = 1.0 - 0.9 * SmoothStep(0.3, 0.8, Ice);
			double Density = 0.0;
			if (!bPebble)
			{
				// The patches where the refinement raises its own boulders, so the meshes lie among them.
				const FVector CDir = FMath::Lerp(FMath::Lerp(G.Dirs[kA], G.Dirs[kB], fx), FMath::Lerp(G.Dirs[kC], G.Dirs[kD], fx), fy).GetSafeNormal();
				const FVector Q = CDir * (R / SlonkTerrain::RefineStartCm) + P.Seed * 7.3;
				const double Field = SmoothStep(-0.05, 0.35, SlonkTerrain::Fbm(Q * 2.0 + FVector(1.0, 9.0, 4.0), 2));
				Density = bBoulder
					? (0.55 * Field * Talus + 0.12 * Rocky + 0.3 * WallFoot + 0.08 * Gorge + 0.35 * Crater * (1.0 - Cliff) + 0.006 + 0.03 * Air * Flat) * Cover
					: (0.45 * Talus * (0.5 + Field) + 0.3 * Rocky + 0.4 * WallFoot + 0.5 * Gorge * Wet + 0.12 * Flat * Dry * Dry + 0.25 * Air + 0.3 * Crater) * Cover;
			}
			else
			{
				Density = (0.5 * Flat * Dry * Dry + 0.45 * Talus + 0.3 * Rocky + 0.6 * Gorge + 0.45 * Air + 0.25 * WallFoot) * (1.0 - Ice);
			}
			Density = FMath::Min(Density * S.Density, 0.9);
			if (D.Next() >= Density) { continue; }

			// On the drawn triangle: the cell's two triangles split along the (i+1, j)-(i, j+1) diagonal, as built.
			FVector Pt, Pa, Pb, Pc;
			if (fx + fy <= 1.0)
			{
				Pa = G.Pos[kA]; Pb = G.Pos[kB]; Pc = G.Pos[kC];
				Pt = Pa * (1.0 - fx - fy) + Pb * fx + Pc * fy;
			}
			else
			{
				Pa = G.Pos[kD]; Pb = G.Pos[kC]; Pc = G.Pos[kB];
				Pt = Pa * (fx + fy - 1.0) + Pb * (1.0 - fx) + Pc * (1.0 - fy);
			}
			const FVector PlanetLocal = G.Origin + Pt;
			const FVector Up = PlanetLocal.GetSafeNormal();
			if (KeepOut.Num() > 0)
			{
				const FVector OnSphere = Up * R;
				bool bInTown = false;
				for (const FKeepOut& K : KeepOut) { if (FVector::DistSquared(OnSphere, K.LocalCm) < K.RadiusSq) { bInTown = true; break; } }
				if (bInTown) { continue; }
			}
			FVector Nt = FVector::CrossProduct(Pb - Pa, Pc - Pa).GetSafeNormal();
			if (FVector::DotProduct(Nt, Up) < 0.0) { Nt = -Nt; }
			if (Nt.IsNearlyZero()) { Nt = Up; }

			// What it is.
			const double StrataWall = WallFoot * P.Strata;
			double W[SlonkGroundRocks::NumKinds];
			if (bBoulder)
			{
				W[0] = 0.55 + 0.3 * Air + 0.2 * Crater;
				W[1] = 0.18 + 0.45 * StrataWall + 0.2 * Table;
				W[2] = 0.08 + 0.3 * Table + 0.25 * StrataWall;
				W[3] = 0.15 + 0.5 * Gorge * Wet + 0.25 * Flat * Wet * (1.0 - Air);
			}
			else if (!bPebble)
			{
				W[0] = 0.45 + 0.3 * Air;
				W[1] = 0.1 + 0.2 * StrataWall;
				W[2] = 0.12 + 0.2 * Table + 0.15 * StrataWall;
				W[3] = 0.3 + 0.8 * Gorge * Wet + 0.3 * Flat * Wet * (1.0 - Air);
			}
			else
			{
				W[0] = 0.45 + 0.3 * Air;
				W[1] = 0.0;
				W[2] = 0.1;
				W[3] = 0.55 + Gorge * Wet + 0.3 * Wet;
			}
			const int32 Kind = PickKind(W, D.Next());
			const FProportion& Pr = Proportions[Kind];
			// Small ones are many: the size leans to the low end of the class. Mountains break off bigger blocks.
			double Across = Cl.MinCm * FMath::Pow(Cl.MaxCm / Cl.MinCm, FMath::Pow(D.Next(), 1.7));
			if (bBoulder) { Across *= 1.0 + 0.5 * Rocky + 0.3 * Air * Crater; }
			const double Sx = Across, Sy = Across * FMath::Lerp(Pr.YLo, Pr.YHi, D.Next()), Sz = Across * FMath::Lerp(Pr.ZLo, Pr.ZHi, D.Next());

			// Lying on the ground: mostly along the drawn face, a little toward the vertical, tipped and turned.
			FVector Z = (Up * 0.35 + Nt * 0.65).GetSafeNormal();
			{
				const FVector Ref = FMath::Abs(Z.Z) < 0.9 ? FVector::ZAxisVector : FVector::XAxisVector;
				const FVector Ta = FVector::CrossProduct(Ref, Z).GetSafeNormal();
				const FVector Tb = FVector::CrossProduct(Z, Ta);
				const double TiltAng = FMath::DegreesToRadians(Pr.TiltDeg * D.Next());
				const double TiltDir = UE_DOUBLE_TWO_PI * D.Next();
				Z = (Z * FMath::Cos(TiltAng) + (Ta * FMath::Cos(TiltDir) + Tb * FMath::Sin(TiltDir)) * FMath::Sin(TiltAng)).GetSafeNormal();
			}
			const FVector Ref = FMath::Abs(Z.Z) < 0.9 ? FVector::ZAxisVector : FVector::XAxisVector;
			const FVector Ta = FVector::CrossProduct(Ref, Z).GetSafeNormal();
			const FVector Tb = FVector::CrossProduct(Z, Ta);
			const double Yaw = UE_DOUBLE_TWO_PI * D.Next();
			const FVector X = Ta * FMath::Cos(Yaw) + Tb * FMath::Sin(Yaw);
			const FQuat Rot = FRotationMatrix::MakeFromZX(Z, X).ToQuat();
			// Buried a fifth of its height on the flat, more on a slope, where the side that turned less than the
			// ground would otherwise stand clear of it.
			const double CosG = FMath::Clamp(FVector::DotProduct(Nt, Up), 0.2, 1.0);
			const double TanG = FMath::Sqrt(1.0 - CosG * CosG) / CosG;
			const double Bury = FMath::Min(Sz * 0.22 + Sx * 0.5 * FMath::Tan(FMath::Atan(TanG) * 0.35), Sz * 0.6);
			const FVector Centre = Pt + Z * (Sz * 0.5 - Bury);
			const FTransform Xf(Rot, Centre, FVector(Sx, Sy, Sz) / SlonkGroundRocks::UnitCm);

			// Its colour: the shader's rock colour, warmed in a canyon; a rock at a wall's foot is a piece of a bed
			// above it, and the rock material colours the bed by its index as the terrain material does.
			const double Seed = D.Next();
			FLinearColor Tint = S.Palette.Mountain * 0.85f;
			const float Warm = (float)(FMath::Clamp(Canyon * 1.5, 0.0, 1.0) * 0.7);
			Tint = FMath::Lerp(Tint, Tint * FLinearColor(1.25f, 0.9f, 0.72f), Warm);
			Tint *= (float)((0.82 + 0.3 * D.Next()) * (1.0 - 0.3 * Wet * Gorge) * 0.8);
			const double Stratum = FMath::Max(FMath::Max(SA.Stratum, SB.Stratum), FMath::Max(SC.Stratum, SD.Stratum));
			const bool bBed = P.Strata > 0.f && Wall > 0.02 && Stratum > 0.0;
			const double Layer = bBed ? (double)FMath::Min(FMath::FloorToInt32(Stratum * 8.0) + FMath::FloorToInt32(D.Next() * 2.99), 7) : -1.0;
			const double OnWall = bBed ? FMath::Clamp(Wall * 1.5, 0.0, 1.0) : 0.0;
			const double Snow = FMath::Clamp(Ice * 1.2, 0.0, 1.0) * (1.0 - FMath::Clamp(Slope * 2.0, 0.0, 1.0));

			Out.Transforms[Kind].Add(Xf);
			const float C[NumCustomFloats] = { Tint.R, Tint.G, Tint.B, (float)Snow, (float)Seed, (float)(Sx / 100.0), (float)(Sy / 100.0), (float)(Sz / 100.0), (float)Layer, (float)OnWall };
			Out.Custom[Kind].Append(C, NumCustomFloats);
			if (Across >= SolidMinCm) { Out.Solids[Kind].Add(Xf); }
			++Total;
		}
	}
}
