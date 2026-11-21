#include "FaunaHabitat.h"
#include "HAL/PlatformTime.h"
#include "ScatterSpot.h"
#include "TerrainScatter.h"

bool SlonkFauna::ShoalAt(const FVector& PlanetLocalCm, double RadiusCm, float Density, double TimeSec, FShoal& Out)
{
	const FVector U = PlanetLocalCm.GetSafeNormal();
	if (Density <= 0.f || U.IsNearlyZero()) { return false; }
	// terrain_material.py's water block, number for number: the plane across the direction's largest axis, the point's
	// cell on it in the material's noise space, the cell's index wrapped to the period, its hash and its gate. The
	// material lays its lattice on the sea's own surface, so a point above or below it is first brought down onto the
	// sea-level sphere along its direction: a leaping fish or a camera over the water finds the shoal drawn under it.
	const FVector SeaPt = U * RadiusCm;
	const FVector Pn = SlonkScatter::ToNoiseSpace(SeaPt);
	const FVector A = U.GetAbs();
	int32 Ax, Iu, Iv;
	if (A.X >= A.Y && A.X >= A.Z) { Ax = 0; Iu = 1; Iv = 2; }
	else if (A.Y >= A.Z) { Ax = 1; Iu = 0; Iv = 2; }
	else { Ax = 2; Iu = 0; Iv = 1; }
	const double Cell = SlonkScatter::NoisePeriodCm / ShoalCellsPerPeriod;
	const double Fu = FMath::FloorToDouble(Pn[Iu] / Cell), Fv = FMath::FloorToDouble(Pn[Iv] / Cell);
	auto Wrap = [](int32 I) { const int32 W = I % ShoalCellsPerPeriod; return W < 0 ? W + ShoalCellsPerPeriod : W; };
	const int32 Cu = Wrap((int32)Fu), Cv = Wrap((int32)Fv);
	uint32 H = ((uint32)Cu * 73856093u) ^ ((uint32)Cv * 19349663u) ^ ((41u + (uint32)Ax) * 2654435761u);
	H = (H ^ (H >> 13)) * 0x5bd1e995u;
	H ^= H >> 15;
	if ((float)(H & 0xffu) / 255.f >= Density) { return false; }
	uint32 H2 = (H ^ 0x9e3779b9u) * 0x85ebca6bu;
	H2 ^= H2 >> 13;
	H2 *= 0xc2b2ae35u;
	H2 ^= H2 >> 16;
	// Its loop round a point near the middle of the cell, its depth, its phase and speed round the loop, its size, which
	// way it turns: in cells, as the material has them.
	const double CtrU = 0.4 + 0.2 * (double)((H >> 8) & 0xffu) / 255.0, CtrV = 0.4 + 0.2 * (double)((H >> 16) & 0xffu) / 255.0;
	const double LoopR = 0.08 + 0.07 * (double)((H >> 24) & 0xffu) / 255.0;
	const double Depth = 300.0 + 3700.0 * (double)(H2 & 0xffu) / 255.0;
	const double Phase = 6.2831853 * (double)((H2 >> 8) & 0xffu) / 255.0;
	const double Speed = 60.0 + 90.0 * (double)((H2 >> 16) & 0xffu) / 255.0;
	const double Half = 0.06 + 0.09 * (double)((H2 >> 24) & 0x7fu) / 127.0;
	const double Turn = (H2 >> 31) != 0u ? -1.0 : 1.0;
	const double Ang = Phase + Turn * Speed / (LoopR * Cell) * TimeSec;
	const double Sn = FMath::Sin(Ang), Cs = FMath::Cos(Ang);
	// Back on the planet: the middle's place on the plane, unwrapped as the point's own was, then up onto the sphere on
	// the point's side of it.
	const double Pu = (Fu + CtrU + LoopR * Cs) * Cell + (SeaPt[Iu] - Pn[Iu]);
	const double Pv = (Fv + CtrV + LoopR * Sn) * Cell + (SeaPt[Iv] - Pn[Iv]);
	const double Rest = RadiusCm * RadiusCm - Pu * Pu - Pv * Pv;
	if (Rest <= 0.0) { return false; }
	FVector Centre;
	Centre[Iu] = Pu;
	Centre[Iv] = Pv;
	Centre[Ax] = (U[Ax] < 0.0 ? -1.0 : 1.0) * FMath::Sqrt(Rest);
	// A direction on the plane is the sea's that projects onto it: the same two components, the third keeping it level.
	const FVector Up = Centre.GetSafeNormal();
	auto OnSea = [&](double Du, double Dv)
	{
		FVector T;
		T[Iu] = Du;
		T[Iv] = Dv;
		T[Ax] = -(Up[Iu] * Du + Up[Iv] * Dv) / Up[Ax];
		return T;
	};
	const FVector Along = OnSea(-Turn * Sn, Turn * Cs);
	const FVector Across = OnSea(-Turn * Cs, -Turn * Sn);
	Out.Axis = Ax;
	Out.Key = FIntPoint(Cu, Cv);
	Out.CentreCm = Centre;
	Out.Heading = Along.GetSafeNormal();
	Out.Side = Across.GetSafeNormal();
	Out.HalfLengthCm = Half * Cell * Along.Size();
	Out.HalfWidthCm = 0.45 * Half * Cell * Across.Size();
	Out.DepthCm = Depth;
	return true;
}

double SlonkFauna::ShoalShapeAt(const FShoal& S, const FVector& PlanetLocalCm)
{
	// The point's offset in the shoal's own axes, which need not be square: solved against both at once.
	const FVector Rel = PlanetLocalCm - S.CentreCm;
	const double C = FVector::DotProduct(S.Heading, S.Side), Det = FMath::Max(1.0 - C * C, 1e-9);
	const double RH = FVector::DotProduct(Rel, S.Heading), RS = FVector::DotProduct(Rel, S.Side);
	const double X = (RH - C * RS) / Det / FMath::Max(S.HalfLengthCm, 1.0), Y = (RS - C * RH) / Det / FMath::Max(S.HalfWidthCm, 1.0);
	return FMath::Sqrt(X * X + Y * Y);
}

namespace
{
	using SlonkFauna::FFaunaGroup;

	/** The divisors of the material's hour a path's period may take, s: each a whole number of loops an hour. */
	const double PeriodChoices[] = { 60.0, 72.0, 75.0, 80.0, 90.0, 100.0, 120.0, 144.0, 150.0, 180.0, 200.0, 225.0, 240.0, 300.0, 360.0, 400.0, 450.0, 600.0,
		720.0, 900.0, 1200.0, 1800.0, 3600.0 };

	/** The plane tangent at a unit direction: an east, and the north square to it (any pair will do, so long as the same
	 *  direction always gives the same). */
	void TangentAxes(const FVector& Up, FVector& OutEast, FVector& OutNorth)
	{
		const FVector Ref = FMath::Abs(Up.Z) < 0.95 ? FVector::ZAxisVector : FVector::XAxisVector;
		OutEast = FVector::CrossProduct(Ref, Up).GetSafeNormal();
		OutNorth = FVector::CrossProduct(Up, OutEast);
	}

	/** The ground a flier flies over: the water's surface over water, the field's ground elsewhere, nothing finer than
	 *  50 m (the cruise clears the rest). */
	double FlightGroundCm(const FPlanetTerrainParams& P, const FVector& Dir)
	{
		const FPlanetTerrainSample S = SlonkTerrain::Sample(P, Dir, 5000.0);
		return S.bWater ? S.WaterCm : S.HeightCm;
	}

	/** A group's loop in its tangent plane at theta, cm from its middle. */
	FVector LoopPoint(const FFaunaGroup& G, double Th)
	{
		const double R = G.RadiusCm * (1.0 + G.Lobe * FMath::Cos(G.Lobes * Th + G.LobePhase)) + G.MeanderCm * FMath::Sin(G.MeanderK * Th + 1.7 * G.LobePhase);
		return (G.East * FMath::Cos(Th) + G.North * FMath::Sin(Th)) * R;
	}

	/** The ground along the loop at theta, from its samples (Catmull-Rom, round the loop). */
	double LoopGround(const FFaunaGroup& G, double Th)
	{
		const int32 N = G.Ground.Num();
		if (N == 0) { return 0.0; }
		double F = Th / UE_DOUBLE_TWO_PI;
		F = (F - FMath::FloorToDouble(F)) * N;
		const int32 I = FMath::Clamp((int32)F, 0, N - 1);
		const double T = F - I;
		auto At = [&](int32 K) { return (double)G.Ground[((K % N) + N) % N]; };
		const double P0 = At(I - 1), P1 = At(I), P2 = At(I + 1), P3 = At(I + 2);
		return 0.5 * ((2.0 * P1) + (-P0 + P2) * T + (2.0 * P0 - 5.0 * P1 + 4.0 * P2 - P3) * T * T + (-P0 + 3.0 * P1 - 3.0 * P2 + P3) * T * T * T);
	}

	/** What an Earth's flock of each habitat is: how many, how big (span, m), how far apart (in spans), how high over the
	 *  ground (m), how fast (m/s) and how fast its wings beat (Hz). */
	struct FFlierSpecies
	{
		int32 MinN, MaxN;
		float SpanLo, SpanHi, SpacingLo, SpacingHi, CruiseLo, CruiseHi, SpeedLo, SpeedHi, FlapLo, FlapHi;
	};
	const FFlierSpecies FlierSpecies[(int32)EFlierHabitat::Count] = {
		// Crows, jays and pigeons over the woods.
		{ 3, 14, 0.35f, 0.9f, 3.f, 5.f, 25.f, 55.f, 10.f, 13.f, 4.f, 6.f },
		// Starlings over the wet meadows.
		{ 40, 120, 0.37f, 0.42f, 1.6f, 2.4f, 12.f, 40.f, 15.f, 19.f, 9.f, 11.f },
		// Gulls along the shore.
		{ 4, 30, 1.f, 1.4f, 3.f, 6.f, 12.f, 50.f, 9.f, 12.f, 2.6f, 3.4f },
		// Swifts and rock doves round the cliffs.
		{ 8, 40, 0.4f, 0.7f, 2.f, 4.f, 25.f, 90.f, 12.f, 17.f, 6.f, 8.f },
	};

	float SmoothStepF(float A, float B, float X)
	{
		const float T = FMath::Clamp((X - A) / (B - A), 0.f, 1.f);
		return T * T * (3.f - 2.f * T);
	}
	float FracF(float X) { return X - FMath::FloorToFloat(X); }
	FVector3f NormalizeF(const FVector3f& V) { const float L = FMath::Sqrt(V.X * V.X + V.Y * V.Y + V.Z * V.Z); return L > 0.f ? V / L : V; }
}

uint64 SlonkFauna::FFaunaWorld::Salt() const
{
	uint64 Bits[3];
	FMemory::Memcpy(&Bits[0], &P.Seed.X, sizeof(uint64));
	FMemory::Memcpy(&Bits[1], &P.Seed.Y, sizeof(uint64));
	FMemory::Memcpy(&Bits[2], &P.Seed.Z, sizeof(uint64));
	return SlonkScatter::Mix64(SlonkScatter::Mix64(SlonkScatter::Mix64((uint64)Profile.Seed * 0x9E3779B97F4A7C15ULL ^ Bits[0]) ^ Bits[1]) ^ Bits[2]);
}

bool SlonkFauna::FFaunaWorld::operator==(const FFaunaWorld& O) const
{
	return P == O.P && SameProfile(Profile, O.Profile) && Palette.Mountain == O.Palette.Mountain && Palette.Land == O.Palette.Land && Palette.Land2 == O.Palette.Land2
		&& Palette.Ice == O.Palette.Ice && Life == O.Life && Density == O.Density && KindMask == O.KindMask;
}

bool SlonkFauna::FFaunaGroup::operator==(const FFaunaGroup& O) const
{
	return Id == O.Id && Kind == O.Kind && Habitat == O.Habitat && Seed == O.Seed && Centre == O.Centre && East == O.East && North == O.North
		&& RadiusCm == O.RadiusCm && Lobe == O.Lobe && LobePhase == O.LobePhase && MeanderCm == O.MeanderCm && Lobes == O.Lobes && MeanderK == O.MeanderK
		&& BobK == O.BobK && PeriodSec == O.PeriodSec && Phase0 == O.Phase0 && Turn == O.Turn && CruiseCm == O.CruiseCm && BobCm == O.BobCm
		&& LengthCm == O.LengthCm && SpeedCmS == O.SpeedCmS && Ground == O.Ground && Members == O.Members && SpanM == O.SpanM && SpacingM == O.SpacingM
		&& FlapHz == O.FlapHz && Pigment == O.Pigment && Glow == O.Glow;
}

int64 SlonkFauna::CellsAcross(const FPlanetTerrainParams& P, EFaunaKind K)
{
	return FMath::Max<int64>(1, FMath::RoundToInt64(SlonkScatter::FaceEdgeCm(P.RadiusCm) / FMath::Max(KindDesc(K).CellCm, 1.0)));
}

SlonkFauna::FFaunaCell SlonkFauna::CellOf(const FPlanetTerrainParams& P, EFaunaKind K, const FVector& Dir)
{
	FFaunaCell C;
	C.Kind = K;
	double U = 0.0, V = 0.0;
	SlonkTerrain::FaceUV(Dir.GetSafeNormal(), C.Face, U, V);
	const int64 N = CellsAcross(P, K);
	C.X = FMath::Clamp<int64>((int64)FMath::FloorToDouble((U + 1.0) * 0.5 * (double)N), 0, N - 1);
	C.Y = FMath::Clamp<int64>((int64)FMath::FloorToDouble((V + 1.0) * 0.5 * (double)N), 0, N - 1);
	return C;
}

void SlonkFauna::CellsNear(const FPlanetTerrainParams& P, EFaunaKind K, const FVector& Dir, double RadiusCm, TArray<FFaunaCell>& Out)
{
	Out.Reset();
	const FVector Up = Dir.GetSafeNormal();
	if (Up.IsNearlyZero()) { return; }
	FVector E, N;
	TangentAxes(Up, E, N);
	const double Cell = KindDesc(K).CellCm;
	// The disc and a cell beyond it, sampled at half a cell (across a face's edge too: each point finds its own face).
	const double Reach = RadiusCm + Cell;
	const double Step = Cell * 0.5;
	const int32 Steps = FMath::CeilToInt32(Reach / Step);
	TSet<uint64> Seen;
	TArray<TPair<double, FFaunaCell>> Found;
	const int64 Across = CellsAcross(P, K);
	for (int32 j = -Steps; j <= Steps; ++j)
	{
		for (int32 i = -Steps; i <= Steps; ++i)
		{
			const double A = i * Step, B = j * Step;
			if (A * A + B * B > Reach * Reach) { continue; }
			const FFaunaCell C = CellOf(P, K, (Up + (E * A + N * B) / P.RadiusCm).GetSafeNormal());
			bool bAlready = false;
			Seen.Add(C.Id(), &bAlready);
			if (bAlready) { continue; }
			// Nearest first, by the cell's own middle.
			const double Wd = 2.0 / (double)Across;
			const FVector Mid = SlonkTerrain::CubeDir(C.Face, -1.0 + Wd * (C.X + 0.5), -1.0 + Wd * (C.Y + 0.5));
			Found.Add({ FVector::Dist(Mid, Up) * P.RadiusCm, C });
		}
	}
	Found.Sort([](const TPair<double, FFaunaCell>& L, const TPair<double, FFaunaCell>& R) { return L.Key < R.Key || (L.Key == R.Key && L.Value.Id() < R.Value.Id()); });
	Out.Reserve(Found.Num());
	for (const TPair<double, FFaunaCell>& F : Found) { Out.Add(F.Value); }
}

SlonkFauna::FFlierHabitatRead SlonkFauna::FlierHabitatAt(const FPlanetTerrainParams& P, const FVector& Dir)
{
	FFlierHabitatRead R;
	const FVector Up = Dir.GetSafeNormal();
	FVector E, N;
	TangentAxes(Up, E, N);
	double WetSum = 0.0, FlatSum = 0.0, IceSum = 0.0;
	SlonkScatter::FHabitat Centre;
	auto Read = [&](const FVector& D, bool bCentre)
	{
		const SlonkScatter::FHabitat H = SlonkScatter::HabitatAt(P, D);
		if (bCentre) { Centre = H; R.CentreHeightCm = H.HeightCm; }
		if (H.bWater) { ++R.WaterPoints; return; }
		++R.LandPoints;
		R.MaxForest = FMath::Max(R.MaxForest, H.Forest);
		R.MaxCliff = FMath::Max(R.MaxCliff, FMath::Max(H.Cliff, 0.6 * H.WallFoot));
		WetSum += H.Wet;
		FlatSum += H.Flat;
		IceSum += H.Ice;
	};
	Read(Up, true);
	// Rings of six at 150 m and at 400 m, the outer turned half a step: a shore or a cliff near the place.
	for (const double Ring : { 1.5e4, 4.0e4 })
	{
		for (int32 k = 0; k < 6; ++k)
		{
			const double A = UE_DOUBLE_TWO_PI * k / 6.0 + (Ring > 2.0e4 ? UE_DOUBLE_PI / 6.0 : 0.0);
			Read((Up + (E * FMath::Cos(A) + N * FMath::Sin(A)) * (Ring / P.RadiusCm)).GetSafeNormal(), false);
		}
	}
	// And whether there is water within 900 m, for the shore's birds that range along it (the structure's water alone).
	R.ShoreWaterPoints = R.WaterPoints;
	for (int32 k = 0; k < 6; ++k)
	{
		const double A = UE_DOUBLE_TWO_PI * k / 6.0;
		R.ShoreWaterPoints += SlonkTerrain::SampleStructure(P, (Up + (E * FMath::Cos(A) + N * FMath::Sin(A)) * (9.0e4 / P.RadiusCm)).GetSafeNormal(), 2.0e4).bWater ? 1 : 0;
	}
	if (R.LandPoints == 0) { return R; }
	R.MeanWet = WetSum / R.LandPoints;
	R.MeanFlat = FlatSum / R.LandPoints;
	// No flock over ice, near the snowline, or where a locked world's air is too hot or frozen for anything to live.
	if ((!Centre.bWater && (Centre.Ice > 0.35 || Centre.Alpine > 0.85)) || IceSum / R.LandPoints > 0.4) { return R; }
	if (P.Climate == EClimateModel::Locked)
	{
		const double Heat = SlonkTerrain::TemperatureN(P, Up, FMath::Max(R.CentreHeightCm, 0.0));
		if (Heat < 0.03 || Heat > 0.72) { return R; }
	}
	R.Score[(int32)EFlierHabitat::Coast] = R.ShoreWaterPoints >= 2 && R.LandPoints >= 3 ? 1.f : 0.f;
	R.Score[(int32)EFlierHabitat::Cliff] = (float)SlonkScatter::SmoothStep(0.15, 0.5, R.MaxCliff);
	R.Score[(int32)EFlierHabitat::Woodland] = (float)SlonkScatter::SmoothStep(0.25, 0.6, R.MaxForest);
	R.Score[(int32)EFlierHabitat::Meadow] = R.WaterPoints == 0
		? (float)(SlonkScatter::SmoothStep(0.75, 0.95, R.MeanFlat) * SlonkScatter::SmoothStep(0.25, 0.45, R.MeanWet) * (1.0 - SlonkScatter::SmoothStep(0.2, 0.45, R.MaxForest)))
		: 0.f;
	// The best of them, the shore before the cliff before the wood before the meadow where they tie.
	for (const EFlierHabitat H : { EFlierHabitat::Coast, EFlierHabitat::Cliff, EFlierHabitat::Woodland, EFlierHabitat::Meadow })
	{
		if (R.Score[(int32)H] > R.BestScore) { R.BestScore = R.Score[(int32)H]; R.Best = H; }
	}
	return R;
}

bool SlonkFauna::GroupInCell(const FFaunaWorld& W, const FFaunaCell& C, FFaunaGroup& Out)
{
	if (!W.Spawns(C.Kind) || C.Kind != EFaunaKind::Flier) { return false; }
	const FFaunaKindDesc& D = KindDesc(C.Kind);
	const int64 Across = CellsAcross(W.P, C.Kind);
	if (C.X < 0 || C.Y < 0 || C.X >= Across || C.Y >= Across || C.Face < 0 || C.Face > 5) { return false; }
	SlonkScatter::FDraw Dr(SlonkScatter::CellHash(C.Face, 40 + (int32)C.Kind, C.X, C.Y, W.Salt()));
	// Where in the cell its middle lies, and whether the cell holds a group: at most half the cells where the habitat
	// suits fully, on a world full of life, at the density asked for.
	const double Wd = 2.0 / (double)Across;
	const double U = -1.0 + Wd * ((double)C.X + 0.2 + 0.6 * Dr.Next());
	const double V = -1.0 + Wd * ((double)C.Y + 0.2 + 0.6 * Dr.Next());
	const double Gate = Dr.Next();
	const double MaxChance = FMath::Min(0.5 * (double)W.Life * (double)W.Density, 1.0);
	if (Gate >= MaxChance) { return false; }
	const FVector Centre = SlonkTerrain::CubeDir(C.Face, U, V).GetSafeNormal();
	const FFlierHabitatRead H = FlierHabitatAt(W.P, Centre);
	if (Gate >= MaxChance * H.BestScore) { return false; }

	FFaunaGroup G;
	G.Id = C.Id();
	G.Kind = C.Kind;
	G.Habitat = H.Best;
	G.Seed = ((uint32)(Dr.Next() * 65536.0) << 16) | (uint32)(Dr.Next() * 65536.0);
	G.Centre = Centre;
	TangentAxes(Centre, G.East, G.North);
	const FFlierSpecies& Sp = FlierSpecies[(int32)G.Habitat];
	// Its members: mostly the smaller flocks of the habitat's range.
	const double Un = Dr.Next();
	G.Members = FMath::Clamp(FMath::Min(Sp.MinN + (int32)((Sp.MaxN - Sp.MinN + 1) * Un * Un), Sp.MaxN), FMath::Max(D.GroupMin, 1), D.GroupMax);
	G.SpanM = FMath::Lerp(Sp.SpanLo, Sp.SpanHi, (float)Dr.Next());
	G.SpacingM = G.SpanM * FMath::Lerp(Sp.SpacingLo, Sp.SpacingHi, (float)Dr.Next());
	G.FlapHz = FMath::Lerp(Sp.FlapLo, Sp.FlapHi, (float)Dr.Next());
	// Its loop: 300 to 600 m round its middle, two to four lobes, a meander of a few dozen metres along it.
	G.RadiusCm = FMath::Lerp(3.0e4, 6.0e4, Dr.Next());
	G.Lobes = 2 + FMath::Min((int32)(Dr.Next() * 3.0), 2);
	G.Lobe = FMath::Lerp(0.12, 0.3, Dr.Next());
	G.LobePhase = UE_DOUBLE_TWO_PI * Dr.Next();
	G.MeanderCm = FMath::Lerp(2.0e3, 6.0e3, Dr.Next());
	G.MeanderK = G.Lobes + 2 + FMath::Min((int32)(Dr.Next() * 3.0), 2);
	G.BobK = 1 + FMath::Min((int32)(Dr.Next() * 3.0), 2);
	G.BobCm = FMath::Lerp(300.0, 800.0, Dr.Next());
	G.Turn = Dr.Next() < 0.5 ? -1.0 : 1.0;
	G.Phase0 = UE_DOUBLE_TWO_PI * Dr.Next();
	G.CruiseCm = 100.0 * FMath::Lerp((double)Sp.CruiseLo, (double)Sp.CruiseHi, Dr.Next());
	const double WantSpeed = 100.0 * FMath::Lerp((double)Sp.SpeedLo, (double)Sp.SpeedHi, Dr.Next());
	const double Vary = 0.85 + 0.3 * Dr.Next();
	// The loop's length, and the period of the hour's divisors nearest the one its speed asks for.
	double Len = 0.0;
	FVector Prev = LoopPoint(G, 0.0);
	for (int32 k = 1; k <= 96; ++k)
	{
		const FVector Q = LoopPoint(G, UE_DOUBLE_TWO_PI * k / 96.0);
		Len += FVector::Dist(Q, Prev);
		Prev = Q;
	}
	G.LengthCm = Len;
	const double Want = Len / FMath::Max(WantSpeed, 1.0);
	double Best = PeriodChoices[0];
	for (const double Pc : PeriodChoices)
	{
		if (FMath::Abs(FMath::Loge(Pc / Want)) < FMath::Abs(FMath::Loge(Best / Want))) { Best = Pc; }
	}
	G.PeriodSec = Best;
	G.SpeedCmS = Len / G.PeriodSec;
	// The ground along it, at its highest two samples either way, so the flock rises ahead of a ridge.
	TArray<float> Raw;
	Raw.SetNum(NumGround);
	for (int32 k = 0; k < NumGround; ++k)
	{
		const FVector Q = LoopPoint(G, UE_DOUBLE_TWO_PI * k / NumGround);
		Raw[k] = (float)FlightGroundCm(W.P, (Centre * W.P.RadiusCm + Q).GetSafeNormal());
	}
	G.Ground.SetNum(NumGround);
	for (int32 k = 0; k < NumGround; ++k)
	{
		float M = Raw[k];
		for (int32 d = -2; d <= 2; ++d) { M = FMath::Max(M, Raw[((k + d) % NumGround + NumGround) % NumGround]); }
		G.Ground[k] = M;
	}
	G.Pigment = FlierPigment(W.Profile.Look, W.Palette, G.Habitat, FMath::Clamp(1.0 - H.MeanWet * 1.3, 0.0, 1.0), Vary);
	G.Glow = 0.f;
	Out = MoveTemp(G);
	return true;
}

bool SlonkFauna::FindGroupNear(const FFaunaWorld& W, EFaunaKind K, const FVector& Dir, double MaxCm, int32 MaxCells, FFaunaGroup& Out)
{
	if (!W.Spawns(K)) { return false; }
	const FVector Up = Dir.GetSafeNormal();
	FVector E, N;
	TangentAxes(Up, E, N);
	const double Cell = KindDesc(K).CellCm;
	const int32 Points = FMath::Max(MaxCells * 2, 1);
	TSet<uint64> Seen;
	double BestDist = TNumericLimits<double>::Max();
	bool bFound = false;
	for (int32 i = 0; i < Points; ++i)
	{
		const double R = MaxCm * FMath::Sqrt((i + 0.5) / Points);
		// Nearest first: once one is found, nothing past it by more than a cell can be nearer.
		if (bFound && R > BestDist + Cell) { break; }
		const double A = i * 2.399963229728653;
		const FFaunaCell C = CellOf(W.P, K, (Up + (E * FMath::Cos(A) + N * FMath::Sin(A)) * (R / W.P.RadiusCm)).GetSafeNormal());
		bool bAlready = false;
		Seen.Add(C.Id(), &bAlready);
		if (bAlready) { continue; }
		if (Seen.Num() > MaxCells) { break; }
		FFaunaGroup G;
		if (!GroupInCell(W, C, G)) { continue; }
		const double Dist = FVector::Dist(G.Centre, Up) * W.P.RadiusCm;
		if (Dist < BestDist) { BestDist = Dist; Out = MoveTemp(G); bFound = true; }
	}
	return bFound;
}

SlonkFauna::FFaunaPose SlonkFauna::PathAt(const FFaunaGroup& G, double RadiusCm, double Seconds)
{
	// The material's hour: every period divides it, so the wrap changes nothing.
	double T = FMath::Fmod(Seconds, ClockPeriodSec);
	if (T < 0.0) { T += ClockPeriodSec; }
	auto At = [&](double S)
	{
		const double Th = G.Phase0 + G.Turn * UE_DOUBLE_TWO_PI * S / FMath::Max(G.PeriodSec, 1.0);
		const FVector Dir = (G.Centre * RadiusCm + LoopPoint(G, Th)).GetSafeNormal();
		// Its cruise over the ground, rising BobCm above it and back, never under it.
		return Dir * (RadiusCm + LoopGround(G, Th) + G.CruiseCm + G.BobCm * 0.5 * (1.0 + FMath::Sin(G.BobK * Th + 0.6 * G.LobePhase)));
	};
	constexpr double H = 0.25;
	const FVector P0 = At(T), Pm = At(T - H), Pp = At(T + H);
	FFaunaPose Pose;
	Pose.PosCm = P0;
	Pose.VelCmS = (Pp - Pm) / (2.0 * H);
	const FVector Up = P0.GetSafeNormal();
	auto Level = [&Up](const FVector& V) { return V - Up * FVector::DotProduct(V, Up); };
	FVector Heading = Level(Pose.VelCmS).GetSafeNormal();
	if (Heading.IsNearlyZero()) { Heading = Level(G.East).GetSafeNormal(); }
	Pose.Rot = FRotationMatrix::MakeFromXZ(Heading, Up).ToQuat();
	// The turn: the heading's change about the up, and the bank a bird holds in it (into the turn: a turn to the right,
	// toward +y, drops the right wing).
	const FVector A = Level(P0 - Pm), B = Level(Pp - P0);
	const double Yaw = FMath::Atan2(FVector::DotProduct(FVector::CrossProduct(A, B), Up), FVector::DotProduct(A, B)) / H;
	Pose.Bank = FMath::Clamp(-FMath::Atan(Level(Pose.VelCmS).Size() * Yaw / 981.0), -0.7, 0.7);
	return Pose;
}

void SlonkFauna::MembersOf(const FFaunaGroup& G, TArray<FTransform>& OutXf, TArray<float>& OutCustom)
{
	OutXf.Reset();
	OutCustom.Reset();
	const int32 N = FMath::Max(G.Members, 0);
	if (N == 0) { return; }
	FRandomStream Rng((int32)G.Seed);
	const double Sp = G.SpacingM * 100.0;
	// The formation: a cloud for the starlings, a looser, flatter spread for the rest, of about the volume their spacing
	// asks for, the birds pushed apart where they crowd.
	const bool bCloud = G.Habitat == EFlierHabitat::Meadow;
	const FVector Axes = (bCloud ? FVector(1.4, 1.0, 0.45) : FVector(1.2, 1.0, 0.3)) * (Sp * FMath::Pow((double)N, 1.0 / 3.0) * (bCloud ? 0.62 : 0.7));
	TArray<FVector> P;
	P.SetNum(N);
	for (int32 i = 0; i < N; ++i)
	{
		FVector B;
		do { B = FVector(Rng.FRandRange(-1.f, 1.f), Rng.FRandRange(-1.f, 1.f), Rng.FRandRange(-1.f, 1.f)); } while (B.SizeSquared() > 1.0);
		P[i] = B * Axes;
	}
	for (int32 Pass = 0; Pass < 3; ++Pass)
	{
		for (int32 i = 0; i < N; ++i)
		{
			for (int32 j = i + 1; j < N; ++j)
			{
				const FVector D = P[j] - P[i];
				const double L = D.Size();
				if (L >= Sp * 0.8) { continue; }
				const FVector Push = (L > 1.0 ? D / L : FVector(0.0, 1.0, 0.0)) * ((Sp * 0.8 - L) * 0.5);
				P[i] -= Push;
				P[j] += Push;
			}
		}
	}
	const FFaunaKindDesc& D = KindDesc(G.Kind);
	const double Wave = Sp * 6.0;
	OutXf.Reserve(N);
	OutCustom.Reserve(N * Custom::Num);
	for (int32 i = 0; i < N; ++i)
	{
		const float Scale = G.SpanM * Rng.FRandRange(0.92f, 1.08f);
		const float Yaw = Rng.FRandRange(-0.1f, 0.1f);
		OutXf.Add(FTransform(FQuat(FVector::ZAxisVector, Yaw), P[i], FVector(Scale)));
		const float Shade = Rng.FRandRange(0.9f, 1.1f);
		const float Seed = Rng.FRand();
		const float PhaseJitter = Rng.FRand();
		const float RateJitter = Rng.FRandRange(0.92f, 1.08f);
		float Ph = (float)(P[i].X / Wave) + 0.15f * PhaseJitter;
		Ph -= FMath::FloorToFloat(Ph);
		const float C[Custom::Num] = {
			G.Pigment.R * Shade, G.Pigment.G * Shade, G.Pigment.B * Shade, G.Glow, Seed, Scale, Ph,
			// A whole number of beats a minute, so the clock's wrap shows no seam.
			FMath::Max(FMath::RoundToFloat(G.FlapHz * RateJitter * 60.f), 1.f) / 60.f,
			(float)FMath::Atan2(P[i].Y, P[i].X), (float)D.FadeCm };
		OutCustom.Append(C, Custom::Num);
	}
}

FVector SlonkFauna::PanicOffset(const FPanic& P, const FFaunaKindDesc& D, double Now)
{
	const double Age = Now - P.T0;
	if (Age < 0.0 || Age >= PanicSec) { return FVector::ZeroVector; }
	// Away at a bird's escape speed (FleeCm over 8 s, at first), held for half a minute, then back over the rest; what
	// was left of the last flight fades as this runs.
	const double Rise = 1.0 - FMath::Exp(-Age / 8.0);
	const double Hold = 1.0 - SlonkScatter::SmoothStep(0.3 * PanicSec, PanicSec, Age);
	const double CarryFade = 1.0 - SlonkScatter::SmoothStep(0.0, PanicSec, Age);
	return P.Carry * CarryFade + P.Dir * (D.FleeCm * Rise * Hold);
}

bool SlonkFauna::Scares(const FFaunaKindDesc& D, const FVector& GroupPosCm, const FFaunaThreat& T)
{
	const double Dist = FVector::Dist(GroupPosCm, T.PosCm);
	if (T.bCraft) { return T.AltitudeCm >= 0.0 && T.AltitudeCm < D.ScareAltitudeCm && Dist < D.ScareRadiusCm; }
	return Dist < D.CrewScareCm;
}

namespace
{
	/** How hard a startled flier's wings beat for its fright, 0 to 1, as the material has it (fauna_material.py's urge):
	 *  coming in over 0.3 s from its onset, in full until 20 s after its latest startle, eased to nothing by PanicSec. */
	double WingUrge(const SlonkFauna::FPanic& P, double Now)
	{
		if (!P.IsLive(Now)) { return 0.0; }
		const double Panic = 1.0 - SlonkScatter::SmoothStep(20.0, SlonkFauna::PanicSec, Now - P.T0);
		return FMath::Clamp(Panic * 3.0, 0.0, 1.0) * SlonkScatter::SmoothStep(0.0, 0.3, Now - P.OnsetT0);
	}

	/** Where smoothstep(0, 1, x) is Y, for Y from 0 to 1. */
	double InverseSmoothStep(double Y)
	{
		return 0.5 - FMath::Sin(FMath::Asin(FMath::Clamp(1.0 - 2.0 * Y, -1.0, 1.0)) / 3.0);
	}

	/** A moment on the paths' clock on the material's: less the clock's offset, wrapped to its hour. */
	float OnMaterialClock(double Seconds, double ClockOffsetSec)
	{
		const double C = FMath::Fmod(Seconds - ClockOffsetSec, SlonkFauna::ClockPeriodSec);
		return (float)(C < 0.0 ? C + SlonkFauna::ClockPeriodSec : C);
	}
}

bool SlonkFauna::StartleGroup(FPanic& P, const FFaunaKindDesc& D, const FVector& GroupPosCm, const FFaunaThreat& T, double Now)
{
	if (!Scares(D, GroupPosCm, T)) { return false; }
	if (P.IsLive(Now) && Now - P.T0 < RestartleSec) { return false; }
	// A flock already chased half again its flight off its path keeps the flight it has: chased on, it would be driven
	// off for ever.
	if (PanicOffset(P, D, Now).Size() > 1.5 * D.FleeCm) { return false; }
	StartleNow(P, D, GroupPosCm, T, Now);
	return true;
}

void SlonkFauna::StartleNow(FPanic& P, const FFaunaKindDesc& D, const FVector& GroupPosCm, const FFaunaThreat& T, double Now)
{
	// Away from it along the ground, and up: straight away, or off its track if it is right overhead.
	const FVector Up = GroupPosCm.GetSafeNormal();
	auto Level = [&Up](const FVector& V) { return V - Up * FVector::DotProduct(V, Up); };
	FVector Away = Level(GroupPosCm - T.PosCm);
	if (Away.Size() < 500.0) { Away = FVector::CrossProduct(Up, Level(T.VelCmS)); }
	if (Away.IsNearlyZero()) { FVector E, N; TangentAxes(Up, E, N); Away = E; }
	// Its members' scatter, if still open, stays as it is: the flight turns, the birds already spread do not.
	const bool bOpen = P.IsScattered(Now);
	// Its wingbeat's urge goes on from where it stands: from nothing on a calm flock; in full on one beating in full (its
	// onset a second back, past the coming-in, so however long a craft keeps it shaken the material's hour never wraps
	// round to it); and on one whose fright has worn on and eased its beat, back up from there, its onset set where the
	// coming-in reaches that.
	const double Urge = WingUrge(P, Now);
	P.Carry = PanicOffset(P, D, Now);
	P.Dir = (Away.GetSafeNormal() + Up * 0.35).GetSafeNormal();
	P.T0 = Now;
	if (!bOpen)
	{
		P.ScatterT0 = Now;
		P.ScatterDir = P.Dir;
	}
	P.OnsetT0 = Urge <= 0.0 ? Now : Now - (Urge >= 1.0 ? 1.0 : 0.3 * InverseSmoothStep(Urge));
}

void SlonkFauna::FrightData(const FPanic& P, double Now, const FQuat& Rot, double ClockOffsetSec, float* Group)
{
	const bool bLive = P.IsLive(Now);
	const FVector Opened = bLive ? Rot.UnrotateVector(P.ScatterDir).GetSafeNormal() : FVector::ZeroVector;
	Group[GroupData::ScatterT0] = bLive ? OnMaterialClock(P.ScatterT0, ClockOffsetSec) : 0.f;
	Group[GroupData::ScatterDirX] = (float)Opened.X;
	Group[GroupData::ScatterDirY] = (float)Opened.Y;
	Group[GroupData::ScatterDirZ] = (float)Opened.Z;
	Group[GroupData::Startled] = bLive ? 1.f : 0.f;
	Group[GroupData::PanicT0] = bLive ? OnMaterialClock(P.T0, ClockOffsetSec) : 0.f;
	Group[GroupData::OnsetT0] = bLive ? OnMaterialClock(P.OnsetT0, ClockOffsetSec) : 0.f;
}

FVector3f SlonkFauna::FlierMotion(const FVector3f& LP, const FLinearColor& VC, float T, const float* C, const float* Gd, float DistCm, float PxPerRad)
{
	// fauna_material.py's WPO_CODE, line for line.
	const float Size = C[Custom::SizeM], Seed = C[Custom::Seed], Phase = C[Custom::Phase], Rate = C[Custom::RateHz], Bearing = C[Custom::PerKind];
	const float ScatterT0 = Gd[GroupData::ScatterT0], PanicT0 = Gd[GroupData::PanicT0], OnsetT0 = Gd[GroupData::OnsetT0];
	const float Bank = Gd[GroupData::Bank], Startled = Gd[GroupData::Startled], Spread = Gd[GroupData::SpreadM];
	const FVector3f SDir(Gd[GroupData::ScatterDirX], Gd[GroupData::ScatterDirY], Gd[GroupData::ScatterDirZ]);
	// As the material: a moment stamped a hair ahead of the clock reads as just now, an age up to a second short of zero.
	const float ScatterAge = FMath::Fmod(T - ScatterT0 + 7201.f, 3600.f) - 1.f;
	const float PanicAge = FMath::Fmod(T - PanicT0 + 7201.f, 3600.f) - 1.f;
	const float OnsetAge = FMath::Fmod(T - OnsetT0 + 7201.f, 3600.f) - 1.f;
	const float Live = Startled > 0.5f ? 1.f : 0.f;
	const float Panic = Live * (1.f - SmoothStepF(20.f, 120.f, PanicAge));
	const float Dist = FMath::Max(DistCm, 1.f);
	const float Px = FMath::Max(Size, 0.01f) * 100.f * PxPerRad / Dist;
	const float K = FMath::Clamp(2.f / FMath::Max(Px, 0.001f), 1.f, 4.f);
	const float Nb = 514.f + FMath::FloorToFloat(FracF(Seed * 7.31f) * 686.f);
	const float Cyc = FracF(T * (Nb / 3600.f) + Seed);
	const float Share = 0.45f + 0.35f * FracF(Seed * 3.77f);
	const float Burst = SmoothStepF(0.f, 0.08f, Cyc) * (1.f - SmoothStepF(Share, Share + 0.08f, Cyc));
	const float Urge = FMath::Clamp(Panic * 3.f, 0.f, 1.f) * SmoothStepF(0.f, 0.3f, OnsetAge);
	const float FlapOn = FMath::Max(Burst, Urge);
	const float BeatCalm = FMath::Sin(6.2831853f * FracF(T * Rate + Seed * 5.f));
	const float BeatFast = FMath::Sin(6.2831853f * FracF(T * Rate * 1.5f + Seed * 5.f));
	const float Beat = FMath::Lerp(BeatCalm, BeatFast, Urge);
	const float Ang = FMath::Lerp(0.12f, 0.9f * Beat, FlapOn) / K;
	FVector3f P = LP;
	const float Va = VC.A;
	if (Va > 0.4f && Va < 0.6f)
	{
		const float S = VC.B > 0.5f ? 1.f : -1.f;
		const float D = FMath::Max(FMath::Abs(LP.Y) - 4.f, 0.f);
		const float A = Ang * (0.8f + 0.4f * VC.G);
		P.Y = S * (4.f + D * FMath::Cos(A));
		P.Z = LP.Z + D * FMath::Sin(A);
	}
	else if (Va > 0.15f && Va < 0.35f)
	{
		P.Z = LP.Z - VC.G * 3.f * FlapOn * Beat;
	}
	const float B = FMath::Clamp(Bank, -0.7f, 0.7f) / K;
	const float Cb = FMath::Cos(B), Sb = FMath::Sin(B);
	P = FVector3f(P.X, P.Y * Cb - P.Z * Sb, P.Y * Sb + P.Z * Cb) * K;
	P.Z += 100.f * FMath::Sin(6.2831853f * FracF(T * (600.f / 3600.f) + Phase));
	P.X += 50.f * FMath::Sin(6.2831853f * FracF(T * (420.f / 3600.f) + Phase * 1.3f + Seed));
	const FVector3f Out = NormalizeF(FVector3f(FMath::Cos(Bearing), FMath::Sin(Bearing), 0.25f));
	const FVector3f Sv = NormalizeF(Out + SDir * 0.8f);
	const float Delay = 0.6f * FMath::Clamp(0.5f + 0.5f * (Out | SDir), 0.f, 1.f);
	const float Open = Live * SmoothStepF(0.f, 1.2f, ScatterAge - Delay) * (1.f - SmoothStepF(8.f, 40.f, ScatterAge));
	const float SpreadUnits = FMath::Min(FMath::Max(Spread, 0.f), 2.f * FMath::Max(Size, 0.01f)) * 100.f / FMath::Max(Size, 0.01f);
	P += Sv * (SpreadUnits * Open);
	return P - LP;
}

void SlonkFauna::FFaunaField::SetWorld(const FFaunaWorld& W)
{
	if (bHasWorld && W == World) { return; }
	Clear(true);
	World = W;
	bHasWorld = true;
}

double SlonkFauna::FFaunaField::ScanRadiusCm() const
{
	double R = 0.0;
	for (int32 K = 0; K < NumKinds; ++K)
	{
		if (!World.Spawns((EFaunaKind)K)) { continue; }
		const FFaunaKindDesc& D = KindDesc((EFaunaKind)K);
		R = FMath::Max(R, D.ReachCm * SpawnHysteresis + D.CellCm / 6.0 + D.CellCm);
	}
	return R;
}

bool SlonkFauna::FFaunaField::WantsScan(const FVector& GroundDir) const
{
	if (!bHasWorld || !World.SpawnsAny()) { return false; }
	if (!bScanned) { return true; }
	double Step = TNumericLimits<double>::Max();
	for (int32 K = 0; K < NumKinds; ++K)
	{
		if (World.Spawns((EFaunaKind)K)) { Step = FMath::Min(Step, KindDesc((EFaunaKind)K).CellCm / 6.0); }
	}
	return FVector::Dist(GroundDir.GetSafeNormal(), LastScanDir) * World.P.RadiusCm > Step;
}

TSharedRef<SlonkFauna::FFaunaField::FScan, ESPMode::ThreadSafe> SlonkFauna::FFaunaField::Scan(const FFaunaWorld& W, const FVector& GroundDir, double RadiusCm, TSharedPtr<const FCellCache, ESPMode::ThreadSafe> InCache)
{
	const double Start = FPlatformTime::Seconds();
	TSharedRef<FScan, ESPMode::ThreadSafe> S = MakeShared<FScan, ESPMode::ThreadSafe>();
	S->CentreDir = GroundDir.GetSafeNormal();
	S->GroundCm = FMath::Max(FlightGroundCm(W.P, S->CentreDir), 0.0);
	TArray<FFaunaCell> Cells;
	for (int32 K = 0; K < NumKinds; ++K)
	{
		if (!W.Spawns((EFaunaKind)K)) { continue; }
		const FFaunaKindDesc& D = KindDesc((EFaunaKind)K);
		CellsNear(W.P, (EFaunaKind)K, S->CentreDir, FMath::Min(RadiusCm, D.ReachCm * SpawnHysteresis + D.CellCm / 6.0 + D.CellCm), Cells);
		for (const FFaunaCell& C : Cells)
		{
			const uint64 Id = C.Id();
			FGroupRef Ref;
			if (const FGroupRef* Known = InCache.IsValid() ? InCache->Find(Id) : nullptr) { Ref = *Known; }
			else
			{
				FFaunaGroup G;
				if (GroupInCell(W, C, G)) { Ref = MakeShared<const FFaunaGroup, ESPMode::ThreadSafe>(MoveTemp(G)); }
				++S->Evaluated;
			}
			S->CellIds.Add(Id);
			S->CellGroups.Add(Ref);
		}
	}
	S->Ms = (FPlatformTime::Seconds() - Start) * 1000.0;
	return S;
}

void SlonkFauna::FFaunaField::Accept(const FScan& S)
{
	TSharedPtr<FCellCache, ESPMode::ThreadSafe> Next = MakeShared<FCellCache, ESPMode::ThreadSafe>();
	Next->Reserve(S.CellIds.Num());
	Candidates.Reset();
	for (int32 i = 0; i < S.CellIds.Num(); ++i)
	{
		Next->Add(S.CellIds[i], S.CellGroups[i]);
		if (S.CellGroups[i].IsValid()) { Candidates.Add(S.CellGroups[i]); }
	}
	Cache = Next;
	LastScanDir = S.CentreDir;
	GroundCm = S.GroundCm;
	bScanned = true;
}

void SlonkFauna::FFaunaField::LetGo(uint64 Id)
{
	if (const FGroupRef* G = Active.Find(Id))
	{
		Members[(int32)(*G)->Kind] -= (*G)->Members;
		Groups[(int32)(*G)->Kind] -= 1;
		Active.Remove(Id);
	}
}

void SlonkFauna::FFaunaField::Select(const FVector& CamLocal, TArray<FGroupRef>& OutSpawned, TArray<uint64>& OutLetGo)
{
	OutSpawned.Reset();
	OutLetGo.Reset();
	if (!bHasWorld) { return; }
	const FVector CamDir = CamLocal.GetSafeNormal();
	const double Alt = CameraAltitudeCm(CamLocal);
	const double R = World.P.RadiusCm;
	// Gone past the hysteresis (or its kind gone, or the camera too high): let go.
	for (const TPair<uint64, FGroupRef>& A : Active)
	{
		const FFaunaKindDesc& D = KindDesc(A.Value->Kind);
		const double Dist = FVector::Dist(CamDir, A.Value->Centre) * R;
		if (!World.Spawns(A.Value->Kind) || Dist > D.ReachCm * SpawnHysteresis || Alt > D.MaxAltitudeCm * SpawnHysteresis) { OutLetGo.Add(A.Key); }
	}
	OutLetGo.Sort();
	for (const uint64 Id : OutLetGo) { LetGo(Id); }
	// Come within reach: spawned nearest first while the kind's caps allow, or while groups out of sight further off can
	// make room.
	TArray<TPair<double, int32>> Order;
	for (int32 i = 0; i < Candidates.Num(); ++i)
	{
		const FFaunaGroup& G = *Candidates[i];
		if (Active.Contains(G.Id) || !World.Spawns(G.Kind)) { continue; }
		const FFaunaKindDesc& D = KindDesc(G.Kind);
		const double Dist = FVector::Dist(CamDir, G.Centre) * R;
		if (Dist < D.ReachCm && Alt < D.MaxAltitudeCm) { Order.Add({ Dist, i }); }
	}
	Order.Sort([this](const TPair<double, int32>& L, const TPair<double, int32>& Rt) { return L.Key < Rt.Key || (L.Key == Rt.Key && Candidates[L.Value]->Id < Candidates[Rt.Value]->Id); });
	TArray<TPair<double, uint64>> Farther;
	for (const TPair<double, int32>& O : Order)
	{
		const FGroupRef& G = Candidates[O.Value];
		const int32 K = (int32)G->Kind;
		const FFaunaKindDesc& D = KindDesc(G->Kind);
		auto Fits = [&](int32 FreedGroups, int32 FreedMembers) { return Groups[K] - FreedGroups < D.MaxGroups && Members[K] - FreedMembers + G->Members <= D.MaxMembers; };
		if (!Fits(0, 0))
		{
			// The caps are full: the kind's groups out of sight (past the fade by more than their loops) and farther than
			// this one by a sixth of a cell make room for it, the farthest first, so which groups are about follows from
			// where the camera is and not from the way it came, and none goes where it would be seen. Those within a sixth
			// of a cell of it stay, so none churn: a flock may wait for a place held by one out of sight up to that much
			// farther off. If they cannot make
			// room enough, none goes, and it waits (and may come in sight before it has room, where more are in sight than
			// the caps allow).
			Farther.Reset();
			for (const TPair<uint64, FGroupRef>& A : Active)
			{
				if (A.Value->Kind != G->Kind) { continue; }
				const double Dist = FVector::Dist(CamDir, A.Value->Centre) * R;
				if (Dist - A.Value->ExtentCm() > D.FadeCm && Dist > O.Key + D.CellCm / 6.0) { Farther.Add({ Dist, A.Key }); }
			}
			Farther.Sort([](const TPair<double, uint64>& L, const TPair<double, uint64>& Rt) { return L.Key > Rt.Key || (L.Key == Rt.Key && L.Value < Rt.Value); });
			int32 Need = 0, FreedGroups = 0, FreedMembers = 0;
			while (Need < Farther.Num() && !Fits(FreedGroups, FreedMembers))
			{
				FreedGroups += 1;
				FreedMembers += Active[Farther[Need].Value]->Members;
				++Need;
			}
			if (!Fits(FreedGroups, FreedMembers)) { continue; }
			for (int32 i = 0; i < Need; ++i)
			{
				LetGo(Farther[i].Value);
				OutLetGo.Add(Farther[i].Value);
			}
		}
		Active.Add(G->Id, G);
		Groups[K] += 1;
		Members[K] += G->Members;
		OutSpawned.Add(G);
	}
}

void SlonkFauna::FFaunaField::Clear(bool bForgetCells)
{
	Active.Reset();
	Candidates.Reset();
	FMemory::Memzero(Members);
	FMemory::Memzero(Groups);
	bScanned = false;
	GroundCm = 0.0;
	if (bForgetCells) { Cache.Reset(); }
}
