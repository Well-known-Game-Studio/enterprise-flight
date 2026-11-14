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

	enum class EClass : uint8 { Boulder, Stone, Pebble, Tree, Grass };
	/** A size class: a lattice of candidates, the chunks it rides on, and how big its pieces are. */
	struct FClass
	{
		EClass Kind;
		/** Wanted spacing of the lattice, cm; it snaps to the nearest level of the quadtree on each planet. */
		double SpacingCm;
		/** Rides on chunks with edges this long or shorter, cm. */
		double MaxEdgeCm;
		/** A rock's size across its largest axis, a plant's height, cm. */
		double MinCm, MaxCm;
		uint64 Salt;
	};
	// Boulders on chunks to 150-odd metres (seen to about 600 m), stones to 40 m (150 m), pebbles and grass on the
	// last level (35 m), trees on chunks to 300 m (a kilometre). Each shows down to a few pixels before the coarser
	// chunk that lacks it takes over; past the trees the terrain's own colours carry the forest.
	constexpr FClass Classes[] = {
		{ EClass::Boulder, 750.0, 26000.0, 120.0, 420.0, 0x51ed270b2f1c7e3dULL },
		{ EClass::Stone, 220.0, 6500.0, 26.0, 95.0, 0x2545f4914f6cdd1dULL },
		{ EClass::Pebble, 60.0, 1700.0, 6.0, 22.0, 0x8c4b5bd7d9a1e5f3ULL },
		{ EClass::Tree, 600.0, 31000.0, 700.0, 2200.0, 0x3c6ef372fe94f82bULL },
		{ EClass::Grass, 45.0, 1700.0, 25.0, 60.0, 0xa54ff53a5f1d36f1ULL },
	};
	/** Shrubs stand on the material's scrub dots, on chunks to 76 m (seen to about 250 m; the painted dots beyond). */
	constexpr double ShrubMaxEdgeCm = 8000.0;

	/** Height to width of each rock shape, and the second horizontal axis to the first: ranges a rock is drawn from. */
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

	/** What a point of the chunk stands on: the chunk's fields there, bilinear, and its place on the drawn mesh. */
	struct FSpot
	{
		double Slope = 0, Rock = 0, Wet = 0, Ice = 0, Canyon = 0, Wall = 0, Crater = 0, Table = 0, River = 0, HeightCm = 0, Stratum = 0;
		/** On the drawn triangle, chunk-relative; the triangle's normal; the radial up and unit direction there. */
		FVector Pt, Nt, Up;
		/** The refinement's regimes from the structure's slope (the same at every level of the quadtree). */
		double Cliff = 0, Talus = 0, Flat = 0, Rocky = 0, WallFoot = 0, Gorge = 0;
		/** The drawn triangle's three grid vertices and the point's weights on them, as the rasteriser interpolates. */
		int32 Vk[3] = { 0, 0, 0 };
		double Vw[3] = { 0.0, 0.0, 0.0 };
	};

	struct FChunkFrame
	{
		const SlonkScatter::FChunkGrid& G;
		double U0, V0, U1, V1, Wd;
		explicit FChunkFrame(const SlonkScatter::FChunkGrid& InG) : G(InG)
		{
			const double Cells = (double)((int64)1 << G.Depth);
			Wd = 2.0 / Cells;
			U0 = -1.0 + Wd * (double)G.X; V0 = -1.0 + Wd * (double)G.Y;
			U1 = U0 + Wd; V1 = V0 + Wd;
		}
		bool Inside(double U, double V) const { return U >= U0 && U < U1 && V >= V0 && V < V1; }
		int32 Index(int32 i, int32 j) const { return (j + G.Margin) * G.W + (i + G.Margin); }

		/** False over water, where a cell's corner is water. */
		bool SpotAt(double U, double V, FSpot& S) const
		{
			const double Fu = (U - U0) / Wd * G.Steps, Fv = (V - V0) / Wd * G.Steps;
			const int32 i = FMath::Clamp((int32)Fu, 0, G.Steps - 1), j = FMath::Clamp((int32)Fv, 0, G.Steps - 1);
			const double fx = FMath::Clamp(Fu - i, 0.0, 1.0), fy = FMath::Clamp(Fv - j, 0.0, 1.0);
			const int32 kA = Index(i, j), kB = Index(i + 1, j), kC = Index(i, j + 1), kD = Index(i + 1, j + 1);
			const FPlanetTerrainSample& SA = G.Samples[kA]; const FPlanetTerrainSample& SB = G.Samples[kB];
			const FPlanetTerrainSample& SC = G.Samples[kC]; const FPlanetTerrainSample& SD = G.Samples[kD];
			if (SA.bWater || SB.bWater || SC.bWater || SD.bWater) { return false; }
			auto Bi = [&](double a, double b, double c, double d) { return FMath::Lerp(FMath::Lerp(a, b, fx), FMath::Lerp(c, d, fx), fy); };
			S.Slope = Bi(SA.Slope, SB.Slope, SC.Slope, SD.Slope);
			S.Rock = Bi(SA.Rock, SB.Rock, SC.Rock, SD.Rock);
			S.Wet = Bi(SA.Wet, SB.Wet, SC.Wet, SD.Wet);
			S.Ice = Bi(SA.Ice, SB.Ice, SC.Ice, SD.Ice);
			S.Canyon = Bi(SA.Canyon, SB.Canyon, SC.Canyon, SD.Canyon);
			S.Wall = Bi(SA.Wall, SB.Wall, SC.Wall, SD.Wall);
			S.Crater = Bi(SA.Crater, SB.Crater, SC.Crater, SD.Crater);
			S.Table = Bi(SA.Table, SB.Table, SC.Table, SD.Table);
			S.River = Bi(SA.River, SB.River, SC.River, SD.River);
			S.HeightCm = Bi(SA.HeightCm, SB.HeightCm, SC.HeightCm, SD.HeightCm);
			S.Stratum = FMath::Max(FMath::Max(SA.Stratum, SB.Stratum), FMath::Max(SC.Stratum, SD.Stratum));
			// On the drawn triangle: the cell's two triangles split along the (i+1, j)-(i, j+1) diagonal, as built.
			FVector Pa, Pb, Pc;
			if (fx + fy <= 1.0)
			{
				Pa = G.Pos[kA]; Pb = G.Pos[kB]; Pc = G.Pos[kC];
				S.Vk[0] = kA; S.Vk[1] = kB; S.Vk[2] = kC;
				S.Vw[0] = 1.0 - fx - fy; S.Vw[1] = fx; S.Vw[2] = fy;
			}
			else
			{
				Pa = G.Pos[kD]; Pb = G.Pos[kC]; Pc = G.Pos[kB];
				S.Vk[0] = kD; S.Vk[1] = kC; S.Vk[2] = kB;
				S.Vw[0] = fx + fy - 1.0; S.Vw[1] = 1.0 - fx; S.Vw[2] = 1.0 - fy;
			}
			S.Pt = Pa * S.Vw[0] + Pb * S.Vw[1] + Pc * S.Vw[2];
			S.Up = (G.Origin + S.Pt).GetSafeNormal();
			S.Nt = FVector::CrossProduct(Pb - Pa, Pc - Pa).GetSafeNormal();
			if (FVector::DotProduct(S.Nt, S.Up) < 0.0) { S.Nt = -S.Nt; }
			if (S.Nt.IsNearlyZero()) { S.Nt = S.Up; }
			S.Cliff = SmoothStep(0.7, 1.3, S.Slope);
			S.Talus = SmoothStep(0.18, 0.5, S.Slope) * (1.0 - S.Cliff);
			S.Flat = 1.0 - SmoothStep(0.12, 0.4, S.Slope);
			S.Rocky = SmoothStep(0.35, 0.8, S.Rock) * (1.0 - S.Cliff);
			S.WallFoot = SmoothStep(0.05, 0.3, S.Wall) * (1.0 - S.Cliff);
			S.Gorge = S.Canyon * (1.0 - SmoothStep(0.03, 0.2, S.Slope));
			return true;
		}

		/** The slope the chunk build writes into a vertex for the material: from its normal by central differences
		 *  over the grid (the margin covers the edges), not from any one triangle's face. */
		double VertexSlope(int32 k) const
		{
			const FVector& D = G.Dirs[k];
			FVector N = FVector::CrossProduct(G.Pos[k + 1] - G.Pos[k - 1], G.Pos[k + G.W] - G.Pos[k - G.W]).GetSafeNormal();
			if (FVector::DotProduct(N, D) < 0.0) { N = -N; }
			if (N.IsNearlyZero()) { N = D; }
			return FMath::Clamp(1.0 - FVector::DotProduct(N, D), 0.0, 1.0);
		}

		/** The terrain material's scrub zone at a spot (terrain_material.py): level, moist, low ground, from the vertex
		 *  attributes it reads (slope, wet, height over the relief), interpolated across the drawn triangle as the
		 *  pixel sees them, so a bush goes where the material draws its dot as strongly as it draws it. */
		double ScrubZoneAt(const FPlanetTerrainParams& P, const FSpot& S) const
		{
			double Slope = 0.0, Wet = 0.0, Hn = 0.0;
			for (int32 c = 0; c < 3; ++c)
			{
				const int32 k = S.Vk[c];
				const double H = G.Samples[k].HeightCm;
				Slope += S.Vw[c] * VertexSlope(k);
				Wet += S.Vw[c] * G.Samples[k].Wet;
				Hn += S.Vw[c] * H / FMath::Max(H >= 0.0 ? P.ReliefCm : P.OceanDepthCm, 1.0);
			}
			const double Steep = FMath::Clamp(Slope * 7.0 - 0.15, 0.0, 1.0);
			return (1.0 - Steep) * FMath::Clamp(Wet * 1.6 + 0.15, 0.0, 1.0) * (1.0 - FMath::Clamp(Hn * P.ReliefCm / 350000.0, 0.0, 1.0));
		}
	};

	/** A frame for a piece standing on the ground: its up between the vertical and the ground's normal, tipped a few
	 *  degrees and turned. */
	FQuat StandingFrame(const FSpot& Sp, double ToGround, double TiltDeg, FDraw& D)
	{
		FVector Z = (Sp.Up * (1.0 - ToGround) + Sp.Nt * ToGround).GetSafeNormal();
		{
			const FVector Ref = FMath::Abs(Z.Z) < 0.9 ? FVector::ZAxisVector : FVector::XAxisVector;
			const FVector Ta = FVector::CrossProduct(Ref, Z).GetSafeNormal();
			const FVector Tb = FVector::CrossProduct(Z, Ta);
			const double TiltAng = FMath::DegreesToRadians(TiltDeg * D.Next());
			const double TiltDir = UE_DOUBLE_TWO_PI * D.Next();
			Z = (Z * FMath::Cos(TiltAng) + (Ta * FMath::Cos(TiltDir) + Tb * FMath::Sin(TiltDir)) * FMath::Sin(TiltAng)).GetSafeNormal();
		}
		const FVector Ref = FMath::Abs(Z.Z) < 0.9 ? FVector::ZAxisVector : FVector::XAxisVector;
		const FVector Ta = FVector::CrossProduct(Ref, Z).GetSafeNormal();
		const FVector Tb = FVector::CrossProduct(Z, Ta);
		const double Yaw = UE_DOUBLE_TWO_PI * D.Next();
		return FRotationMatrix::MakeFromZX(Z, Ta * FMath::Cos(Yaw) + Tb * FMath::Sin(Yaw)).ToQuat();
	}

	/** The settlements near a chunk, as circles the scatter keeps out of. */
	struct FKeepOut
	{
		TArray<TPair<FVector, double>> Towns;
		void Build(const FPlanetTerrainParams& P, const SlonkScatter::FChunkGrid& G)
		{
			TArray<FTownSite> Sites;
			SlonkCities::EnumerateTowns(P, G.Origin, G.EdgeCm * 0.75 + SlonkCities::CityRadiusCm(1.f) + 5000.0, Sites, 64);
			for (const FTownSite& T : Sites) { Towns.Add({ T.LocalCm, FMath::Square(T.RadiusCm + 2500.0) }); }
		}
		bool Contains(const FVector& Up, double R) const
		{
			const FVector OnSphere = Up * R;
			for (const TPair<FVector, double>& T : Towns) { if (FVector::DistSquared(OnSphere, T.Key) < T.Value) { return true; } }
			return false;
		}
	};

	/** The climate a plant grows in, from the world's snowline (SlonkTerrain::SnowlineCm), so the treeline is a
	 *  height for the latitude, as on an Earth, not a fraction of the world's relief. */
	struct FClimate
	{
		/** The height over the snowline there: 0 at the sea, 1 at the snowline. The treeline is about 0.75 of it. */
		double Alpine = 0.0;
		/** How cold it is for what grows, 0 in a warm lowland: the snowline's drop toward the poles, and the height. */
		double Cold = 0.0;
	};
	FClimate ClimateAt(const FPlanetTerrainParams& P, const FSpot& Sp)
	{
		FClimate C;
		// A world with no snow still has a treeline: as if its snowline stood at its relief.
		FPlanetTerrainParams Q = P;
		if (Q.SnowlineCm <= 0.0) { Q.SnowlineCm = FMath::Max(P.ReliefCm, 1.0); }
		const double Line = SlonkTerrain::SnowlineCm(Q, Sp.Up);
		C.Alpine = FMath::Clamp(FMath::Max(Sp.HeightCm, 0.0) / FMath::Max(Line, 1.0), 0.0, 1.5);
		C.Cold = FMath::Clamp(1.0 - Line / Q.SnowlineCm + 0.5 * C.Alpine, 0.0, 1.5);
		return C;
	}

	/** How parched a plant is, 0 lush to 1 dry: the per-instance dryness the foliage material yellows it by. */
	double DrynessAt(const FSpot& Sp) { return FMath::Clamp(1.0 - Sp.Wet * 1.3, 0.0, 1.0); }

	/** The colour of the living ground where a plant stands: the material's wet colour, browned toward its dry one. */
	FLinearColor FoliageAt(const FScatterPalette& Pal, double Dry, double Vary)
	{
		return FMath::Lerp(Pal.Land, Pal.Land2, (float)(Dry * 0.55)) * (float)Vary;
	}

	void AddPiece(SlonkScatter::FChunkScatter& Out, int32 Mesh, const FTransform& Xf, const float (&C)[SlonkScatter::NumCustomFloats], bool bSolid)
	{
		Out.Transforms[Mesh].Add(Xf);
		Out.Custom[Mesh].Append(C, SlonkScatter::NumCustomFloats);
		if (bSolid) { Out.Solids[Mesh].Add(Xf); }
	}
}

int32 SlonkScatter::FChunkScatter::Num() const
{
	int32 N = 0;
	for (const TArray<FTransform>& T : Transforms) { N += T.Num(); }
	return N;
}

int32 SlonkScatter::FChunkScatter::NumPlants() const
{
	int32 N = 0;
	for (int32 M = FirstPlant; M < NumMeshes; ++M) { N += Transforms[M].Num(); }
	return N;
}

double SlonkScatter::MaxChunkEdgeCm()
{
	double M = ShrubMaxEdgeCm;
	for (const FClass& C : Classes) { M = FMath::Max(M, C.MaxEdgeCm); }
	return M;
}

FVector SlonkScatter::ToNoiseSpace(const FVector& PlanetLocalCm)
{
	auto Wrap = [](double V) { double M = FMath::Fmod(V, NoisePeriodCm); if (M < 0.0) { M += NoisePeriodCm; } return M; };
	return FVector(Wrap(PlanetLocalCm.X), Wrap(PlanetLocalCm.Y), Wrap(PlanetLocalCm.Z));
}

FIntVector SlonkScatter::ScrubCellKey(const FIntVector& Cell)
{
	auto Wrap = [](int32 I) { const int32 W = I % ScrubCellsPerPeriod; return W < 0 ? W + ScrubCellsPerPeriod : W; };
	return FIntVector(Wrap(Cell.X), Wrap(Cell.Y), Wrap(Cell.Z));
}

bool SlonkScatter::ScrubDotAt(const FVector& NoiseP, FVector& OutCentre, double& OutRadiusCm)
{
	// terrain_material.py, the first pass of the jittered dots: the same cells, the index wrapped to the period the
	// same way, the same hash. The centre stays in the caller's noise space (the cell unwrapped).
	const FIntVector Fl((int32)FMath::FloorToDouble(NoiseP.X / ScrubCellCm), (int32)FMath::FloorToDouble(NoiseP.Y / ScrubCellCm), (int32)FMath::FloorToDouble(NoiseP.Z / ScrubCellCm));
	const FIntVector C = ScrubCellKey(Fl);
	uint32 H = ((uint32)C.X * 73856093u) ^ ((uint32)C.Y * 19349663u) ^ ((uint32)C.Z * 83492791u) ^ (31u * 2654435761u);
	H = (H ^ (H >> 13)) * 0x5bd1e995u;
	H ^= H >> 15;
	if ((float)(H & 0xffu) / 255.f >= 0.35f) { return false; }
	const FVector Ctr = FVector((double)((H >> 8) & 0xffu), (double)((H >> 16) & 0xffu), (double)((H >> 24) & 0xffu)) / 255.0 * 0.5 + FVector(0.25);
	OutCentre = (FVector(Fl) + Ctr) * ScrubCellCm;
	OutRadiusCm = (0.12 + 0.13 * (double)((H >> 4) & 0xffu) / 255.0) * ScrubCellCm;
	return true;
}

void SlonkScatter::ScatterChunk(const FPlanetTerrainParams& P, const FScatterSettings& S, const FChunkGrid& G, FChunkScatter& Out)
{
	if (!S.bEnabled || S.Density <= 0.f || G.EdgeCm > MaxChunkEdgeCm() || !G.Samples || !G.Dirs || !G.Pos || G.Steps <= 0) { return; }
	const double R = P.RadiusCm;
	const double FaceArcCm = R * UE_DOUBLE_HALF_PI;
	const FChunkFrame F(G);
	const bool bLiving = S.Life > 0.f && !P.bAirless;
	FKeepOut KeepOut;
	if (S.bCities) { KeepOut.Build(P, G); }

	int32 Rocks = 0, Plants = 0;
	for (const FClass& Cl : Classes)
	{
		if (G.EdgeCm > Cl.MaxEdgeCm) { continue; }
		const bool bRock = Cl.Kind == EClass::Boulder || Cl.Kind == EClass::Stone || Cl.Kind == EClass::Pebble;
		if (!bRock && !bLiving) { continue; }
		// The class's lattice level: cells of 2 / 2^L in face coordinates, nested in the chunks of the quadtree.
		const int32 L = FMath::Clamp(FMath::RoundToInt32(FMath::Log2(FaceArcCm / Cl.SpacingCm)), 0, 40);
		const double Delta = 2.0 / (double)((int64)1 << L);
		const int64 GX0 = (int64)FMath::FloorToDouble((F.U0 + 1.0) / Delta), GX1 = (int64)FMath::CeilToDouble((F.U1 + 1.0) / Delta) - 1;
		const int64 GY0 = (int64)FMath::FloorToDouble((F.V0 + 1.0) / Delta), GY1 = (int64)FMath::CeilToDouble((F.V1 + 1.0) / Delta) - 1;
		// Rocks and plants are capped apart; a class stops where its count reaches the cap.
		const int32& Count = bRock ? Rocks : Plants;
		const int32 Cap = bRock ? MaxPerChunk : MaxPlantsPerChunk;
		for (int64 GY = GY0; GY <= GY1 && Count < Cap; ++GY)
		for (int64 GX = GX0; GX <= GX1 && Count < Cap; ++GX)
		{
			FDraw D(CellHash(G.Face, L, GX, GY, Cl.Salt));
			// The candidate: anywhere in its cell. Half-open windows put it in exactly one chunk of each level.
			const double U = -1.0 + ((double)GX + D.Next()) * Delta, V = -1.0 + ((double)GY + D.Next()) * Delta;
			if (!F.Inside(U, V)) { continue; }
			FSpot Sp;
			if (!F.SpotAt(U, V, Sp) || Sp.River > (bRock ? 0.15 : 0.08)) { continue; }
			if (Sp.Cliff > (bRock ? 0.6 : 0.3)) { continue; }
			const double Air = P.bAirless ? 1.0 : 0.0;
			const double Dry = 1.0 - Sp.Wet;
			// For a plant: the climate, which sets how much grows and what (the treeline, the conifers).
			const FClimate Clim = bRock ? FClimate() : ClimateAt(P, Sp);
			double Density = 0.0;
			if (bRock)
			{
				// Snow and ice bury all but the biggest.
				const double Cover = 1.0 - 0.9 * SmoothStep(0.3, 0.8, Sp.Ice);
				if (Cl.Kind != EClass::Pebble)
				{
					// The patches where the refinement raises its own boulders, so the meshes lie among them.
					const FVector Q = Sp.Up * (R / SlonkTerrain::RefineStartCm) + P.Seed * 7.3;
					const double Field = SmoothStep(-0.05, 0.35, SlonkTerrain::Fbm(Q * 2.0 + FVector(1.0, 9.0, 4.0), 2));
					Density = Cl.Kind == EClass::Boulder
						? (0.55 * Field * Sp.Talus + 0.12 * Sp.Rocky + 0.3 * Sp.WallFoot + 0.08 * Sp.Gorge + 0.35 * Sp.Crater * (1.0 - Sp.Cliff) + 0.006 + 0.03 * Air * Sp.Flat) * Cover
						: (0.45 * Sp.Talus * (0.5 + Field) + 0.3 * Sp.Rocky + 0.4 * Sp.WallFoot + 0.5 * Sp.Gorge * Sp.Wet + 0.12 * Sp.Flat * Dry * Dry + 0.25 * Air + 0.3 * Sp.Crater) * Cover;
				}
				else
				{
					Density = (0.5 * Sp.Flat * Dry * Dry + 0.45 * Sp.Talus + 0.3 * Sp.Rocky + 0.6 * Sp.Gorge + 0.45 * Air + 0.25 * Sp.WallFoot) * (1.0 - Sp.Ice);
				}
			}
			else
			{
				// Stands of forest with clearings between them, at the kilometre scale, thickest where it is wet.
				const double Forest = SmoothStep(-0.15, 0.25, SlonkTerrain::Fbm(Sp.Up * (R / 120000.0) + P.Seed * 3.7, 3));
				// Nothing grows through lying snow.
				const double SnowLying = SmoothStep(0.2, 0.6, Sp.Ice);
				if (Cl.Kind == EClass::Tree)
				{
					Density = (Forest * SmoothStep(0.3, 0.65, Sp.Wet) * (1.0 - SmoothStep(0.35, 0.75, Sp.Slope)) * (1.0 - 0.7 * Sp.Rocky) * 0.8
						// A line of trees along the river on a canyon floor, whatever the forest does.
						+ Sp.Gorge * Sp.Wet * 0.35)
						* (1.0 - SmoothStep(0.65, 0.9, Clim.Alpine)) * (1.0 - SnowLying);
				}
				else
				{
					Density = SmoothStep(0.15, 0.45, Sp.Wet) * (1.0 - SmoothStep(0.3, 0.7, Sp.Slope)) * (1.0 - Sp.Rocky) * (1.0 - SnowLying)
						* (1.0 - SmoothStep(0.75, 1.0, Clim.Alpine)) * (1.0 - 0.5 * Forest * Sp.Wet) * 0.9;
				}
				Density *= S.Life;
			}
			Density = FMath::Min(Density * S.Density, 0.9);
			if (D.Next() >= Density) { continue; }
			if (KeepOut.Towns.Num() > 0 && KeepOut.Contains(Sp.Up, R)) { continue; }
			const double Seed = D.Next();
			const double Snow = FMath::Clamp(Sp.Ice * 1.2, 0.0, 1.0) * (1.0 - FMath::Clamp(Sp.Slope * 2.0, 0.0, 1.0));

			if (bRock)
			{
				// What it is.
				const bool bBoulder = Cl.Kind == EClass::Boulder, bPebble = Cl.Kind == EClass::Pebble;
				const double StrataWall = Sp.WallFoot * P.Strata;
				double W[SlonkGroundRocks::NumKinds];
				if (bBoulder)
				{
					W[0] = 0.55 + 0.3 * Air + 0.2 * Sp.Crater;
					W[1] = 0.18 + 0.45 * StrataWall + 0.2 * Sp.Table;
					W[2] = 0.08 + 0.3 * Sp.Table + 0.25 * StrataWall;
					W[3] = 0.15 + 0.5 * Sp.Gorge * Sp.Wet + 0.25 * Sp.Flat * Sp.Wet * (1.0 - Air);
				}
				else if (!bPebble)
				{
					W[0] = 0.45 + 0.3 * Air;
					W[1] = 0.1 + 0.2 * StrataWall;
					W[2] = 0.12 + 0.2 * Sp.Table + 0.15 * StrataWall;
					W[3] = 0.3 + 0.8 * Sp.Gorge * Sp.Wet + 0.3 * Sp.Flat * Sp.Wet * (1.0 - Air);
				}
				else
				{
					W[0] = 0.45 + 0.3 * Air;
					W[1] = 0.0;
					W[2] = 0.1;
					W[3] = 0.55 + Sp.Gorge * Sp.Wet + 0.3 * Sp.Wet;
				}
				const int32 Kind = PickKind(W, D.Next());
				const FProportion& Pr = Proportions[Kind];
				// Small ones are many: the size leans to the low end of the class. Mountains break off bigger blocks.
				double Across = Cl.MinCm * FMath::Pow(Cl.MaxCm / Cl.MinCm, FMath::Pow(D.Next(), 1.7));
				if (bBoulder) { Across *= 1.0 + 0.5 * Sp.Rocky + 0.3 * Air * Sp.Crater; }
				const double Sx = Across, Sy = Across * FMath::Lerp(Pr.YLo, Pr.YHi, D.Next()), Sz = Across * FMath::Lerp(Pr.ZLo, Pr.ZHi, D.Next());
				// Lying on the ground: mostly along the drawn face, a little toward the vertical, tipped and turned.
				const FQuat Rot = StandingFrame(Sp, 0.65, Pr.TiltDeg, D);
				const FVector Z = Rot.GetAxisZ();
				// Buried a fifth of its height on the flat, more on a slope, where the side that turned less than the
				// ground would otherwise stand clear of it.
				const double CosG = FMath::Clamp(FVector::DotProduct(Sp.Nt, Sp.Up), 0.2, 1.0);
				const double TanG = FMath::Sqrt(1.0 - CosG * CosG) / CosG;
				const double Bury = FMath::Min(Sz * 0.22 + Sx * 0.5 * FMath::Tan(FMath::Atan(TanG) * 0.35), Sz * 0.6);
				const FTransform Xf(Rot, Sp.Pt + Z * (Sz * 0.5 - Bury), FVector(Sx, Sy, Sz) / SlonkGroundRocks::UnitCm);
				// Its colour: the shader's rock colour, warmed in a canyon; a rock at a wall's foot is a piece of a bed
				// above it, and the rock material colours the bed by its index as the terrain material does.
				FLinearColor Tint = S.Palette.Mountain * 0.85f;
				const float Warm = (float)(FMath::Clamp(Sp.Canyon * 1.5, 0.0, 1.0) * 0.7);
				Tint = FMath::Lerp(Tint, Tint * FLinearColor(1.25f, 0.9f, 0.72f), Warm);
				Tint *= (float)((0.82 + 0.3 * D.Next()) * (1.0 - 0.3 * Sp.Wet * Sp.Gorge) * 0.8);
				const bool bBed = P.Strata > 0.f && Sp.Wall > 0.02 && Sp.Stratum > 0.0;
				const double Layer = bBed ? (double)FMath::Min(FMath::FloorToInt32(Sp.Stratum * 8.0) + FMath::FloorToInt32(D.Next() * 2.99), 7) : -1.0;
				const double OnWall = bBed ? FMath::Clamp(Sp.Wall * 1.5, 0.0, 1.0) : 0.0;
				const float C[NumCustomFloats] = { Tint.R, Tint.G, Tint.B, (float)Snow, (float)Seed, (float)(Sx / 100.0), (float)(Sy / 100.0), (float)(Sz / 100.0), (float)Layer, (float)OnWall };
				AddPiece(Out, Kind, Xf, C, Across >= SolidMinCm);
				++Rocks;
			}
			else
			{
				const double Dryness = DrynessAt(Sp);
				const bool bTree = Cl.Kind == EClass::Tree;
				// The cold, the heights and the poor ground grow conifers; the warm, wet lowlands broadleaves.
				const bool bConifer = bTree && D.Next() < SmoothStep(0.25, 0.6, Clim.Cold) + 0.3 * Sp.Rocky;
				const EPlant Kind = bTree ? (bConifer ? EPlant::Conifer : EPlant::Broadleaf) : EPlant::Grass;
				double H = Cl.MinCm * FMath::Pow(Cl.MaxCm / Cl.MinCm, FMath::Pow(D.Next(), 1.3));
				if (bConifer) { H *= 1.15; }
				// Near the treeline what grows is stunted.
				if (bTree) { H *= 1.0 - 0.45 * SmoothStep(0.45, 0.85, Clim.Alpine); }
				const double Wx = FMath::Lerp(0.85, 1.15, D.Next()), Wy = Wx * FMath::Lerp(0.9, 1.1, D.Next());
				// Upright: trees grow to the vertical whatever the slope; a tuft of grass follows the ground a little.
				const FQuat Rot = StandingFrame(Sp, bTree ? 0.05 : 0.35, bTree ? 3.0 : 8.0, D);
				const FVector Z = Rot.GetAxisZ();
				// The base sunk into the ground, deeper for a tree on a slope so its downhill side meets it.
				const double SlopeTan = FMath::Sqrt(FMath::Max(1.0 - FMath::Square(FVector::DotProduct(Sp.Nt, Sp.Up)), 0.0));
				const double Sink = bTree ? 30.0 + H * 0.02 * SlopeTan * 3.0 : 2.0;
				const FTransform Xf(Rot, Sp.Pt - Z * Sink, FVector(H * Wx, H * Wy, H) / SlonkPlants::UnitCm);
				const double Vary = 0.78 + 0.34 * D.Next();
				FLinearColor Tint = FoliageAt(S.Palette, Dryness, Vary);
				// Needles are darker and bluer than leaves; the grass is the ground's own colour so the two meet.
				if (Kind == EPlant::Conifer) { Tint *= FLinearColor(0.62f, 0.72f, 0.7f); }
				else if (Kind == EPlant::Broadleaf) { Tint *= 0.85f; }
				else { Tint = FMath::Lerp(S.Palette.Land2, S.Palette.Land, (float)FMath::Clamp(Sp.Wet * 1.2 - Sp.Rocky * 0.3, 0.0, 1.0)) * (float)(0.8 * Vary); }
				const float C[NumCustomFloats] = { Tint.R, Tint.G, Tint.B, (float)Snow, (float)Seed, (float)(H * Wx / 100.0), (float)(H * Wy / 100.0), (float)(H / 100.0), (float)Dryness, 0.f };
				AddPiece(Out, PlantMesh(Kind), Xf, C, bTree && H >= SolidTreeCm);
				++Plants;
			}
		}
	}

	// Shrubs: on the scrub dots the terrain material paints, the same cells, hashed the same way, so the bush close to
	// stands where the dark dot was from the air. The material adds each dot through the cell its pixel falls in, so
	// a dot is drawn about its centre only if the ground there is in the dot's own cell; that is where a bush goes.
	if (bLiving && G.EdgeCm <= ShrubMaxEdgeCm)
	{
		const FVector OriginNoise = ToNoiseSpace(G.Origin);
		auto CellOf = [](const FVector& Pn) { return FIntVector((int32)FMath::FloorToDouble(Pn.X / ScrubCellCm), (int32)FMath::FloorToDouble(Pn.Y / ScrubCellCm), (int32)FMath::FloorToDouble(Pn.Z / ScrubCellCm)); };
		// Every cell the drawn ground passes through, from each grid cell's box (its two triangles lie inside it):
		// on a coarse grid the vertices are metres further apart than the dots' cells, and most of those cells hold
		// none. A cell with water at a corner carries nothing (SpotAt), so it adds none.
		TSet<FIntVector> Cells;
		for (int32 j = 0; j < G.Steps; ++j)
		{
			for (int32 i = 0; i < G.Steps; ++i)
			{
				const int32 Corner[4] = { F.Index(i, j), F.Index(i + 1, j), F.Index(i, j + 1), F.Index(i + 1, j + 1) };
				FBox Box(ForceInit);
				bool bWet = false;
				for (const int32 k : Corner) { bWet |= G.Samples[k].bWater; Box += OriginNoise + G.Pos[k]; }
				if (bWet) { continue; }
				const FIntVector Lo = CellOf(Box.Min), Hi = CellOf(Box.Max);
				for (int32 z = Lo.Z; z <= Hi.Z; ++z)
				for (int32 y = Lo.Y; y <= Hi.Y; ++y)
				for (int32 x = Lo.X; x <= Hi.X; ++x) { Cells.Add(FIntVector(x, y, z)); }
			}
		}
		for (const FIntVector& Cell : Cells)
		{
			if (Plants >= MaxPlantsPerChunk) { break; }
			FVector CentreN; double DotR;
			if (!ScrubDotAt((FVector(Cell) + FVector(0.5)) * ScrubCellCm, CentreN, DotR)) { continue; }
			const FVector Planet = G.Origin + (CentreN - OriginNoise);
			int32 Face; double U, V;
			if (!SlonkTerrain::FaceUV(Planet.GetSafeNormal(), Face, U, V) || Face != G.Face || !F.Inside(U, V)) { continue; }
			FSpot Sp;
			if (!F.SpotAt(U, V, Sp) || Sp.River > 0.08 || Sp.Cliff > 0.3 || Sp.Ice > 0.3) { continue; }
			if (CellOf(OriginNoise + Sp.Pt) != Cell) { continue; }
			const double Zone = F.ScrubZoneAt(P, Sp);
			// The bush's own draws from the dot's place on the periodic lattice, so it is the same bush whichever side
			// of a wrap the chunk's origin lies.
			const FIntVector Key = ScrubCellKey(Cell);
			FDraw D(Mix64(((uint64)(uint32)Key.X << 42) ^ ((uint64)(uint32)Key.Y << 21) ^ (uint64)(uint32)Key.Z ^ 0x6a09e667f3bcc909ULL));
			if (D.Next() >= S.Life * S.Density * SmoothStep(0.25, 0.55, Zone)) { continue; }
			if (KeepOut.Towns.Num() > 0 && KeepOut.Contains(Sp.Up, R)) { continue; }
			// As wide as the painted dot, a little over half as tall; the mesh's crown is 110 cm across at unit scale.
			const double Wide = DotR * 2.0 * FMath::Lerp(0.95, 1.15, D.Next());
			const double H = Wide * FMath::Lerp(0.55, 0.85, D.Next());
			const FQuat Rot = StandingFrame(Sp, 0.25, 6.0, D);
			const FTransform Xf(Rot, Sp.Pt - Rot.GetAxisZ() * (H * 0.04), FVector(Wide / 110.0, Wide / 110.0 * FMath::Lerp(0.85, 1.1, D.Next()), H / SlonkPlants::UnitCm));
			const double Dryness = DrynessAt(Sp);
			// The dot's own darkening of the ground, which the bush now stands over.
			const FLinearColor Tint = FoliageAt(S.Palette, Dryness, 0.8 + 0.3 * D.Next()) * FLinearColor(0.55f, 0.62f, 0.45f) * 1.5f;
			const double Snow = FMath::Clamp(Sp.Ice * 1.2, 0.0, 1.0);
			const float C[NumCustomFloats] = { Tint.R, Tint.G, Tint.B, (float)Snow, (float)D.Next(), (float)(Wide / 100.0), (float)(Wide / 100.0), (float)(H / 100.0), (float)Dryness, 0.f };
			AddPiece(Out, PlantMesh(EPlant::Shrub), Xf, C, false);
			++Plants;
		}
	}
}
