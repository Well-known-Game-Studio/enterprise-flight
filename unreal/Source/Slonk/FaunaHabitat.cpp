#include "FaunaHabitat.h"
#include "HAL/PlatformTime.h"
#include "PlanetCities.h"
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
	// The towns the herds keep out of: whether there are any, and whose (where they stand and how wide they are).
	auto SamePeople = [](const FCivilization& A, const FCivilization& B)
	{
		return A.bInhabited == B.bInhabited && A.bColonists == B.bColonists && A.Age == B.Age && A.Organic == B.Organic && A.CliffAffinity == B.CliffAffinity
			&& A.Density == B.Density && A.WetMin == B.WetMin;
	};
	return P == O.P && SameProfile(Profile, O.Profile) && Palette.Mountain == O.Palette.Mountain && Palette.Land == O.Palette.Land && Palette.Land2 == O.Palette.Land2
		&& Palette.Ice == O.Palette.Ice && Life == O.Life && Density == O.Density && KindMask == O.KindMask && bTowns == O.bTowns && (!bTowns || SamePeople(People, O.People));
}

bool SlonkFauna::FFaunaGroup::operator==(const FFaunaGroup& O) const
{
	return Id == O.Id && Kind == O.Kind && Habitat == O.Habitat && Seed == O.Seed && Centre == O.Centre && East == O.East && North == O.North
		&& RadiusCm == O.RadiusCm && Lobe == O.Lobe && LobePhase == O.LobePhase && MeanderCm == O.MeanderCm && Lobes == O.Lobes && MeanderK == O.MeanderK
		&& BobK == O.BobK && PeriodSec == O.PeriodSec && Phase0 == O.Phase0 && Turn == O.Turn && CruiseCm == O.CruiseCm && BobCm == O.BobCm
		&& LengthCm == O.LengthCm && SpeedCmS == O.SpeedCmS && Ground == O.Ground && Members == O.Members && SpanM == O.SpanM && SpacingM == O.SpacingM
		&& FlapHz == O.FlapHz && Pigment == O.Pigment && Glow == O.Glow
		&& CycleSec == O.CycleSec && WalkCmS == O.WalkCmS && TrotCmS == O.TrotCmS && WalkA == O.WalkA && WalkB == O.WalkB && TrotA == O.TrotA && TrotB == O.TrotB
		&& RampSec == O.RampSec && StartCm == O.StartCm && ArcCm == O.ArcCm && FormationCm == O.FormationCm && HerdRadiusCm == O.HerdRadiusCm && GroundHalfCm == O.GroundHalfCm;
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

bool SlonkFauna::HerdGrass(const SlonkScatter::FHabitat& H, bool bStrict)
{
	if (H.bWater || H.Ice > 0.2 || H.Alpine > 0.7) { return false; }
	if (bStrict) { return H.Flat > 0.85 && H.Wet >= 0.2 && H.Wet <= 0.6 && H.Forest < 0.3; }
	return H.Flat > 0.7 && H.Wet >= 0.14 && H.Wet <= 0.7 && H.Forest < 0.45;
}

namespace
{
	/** The divisors of the hour a herd's schedule may repeat on, s. */
	const double HerdCycles[] = { 240.0, 300.0, 360.0, 400.0, 450.0, 600.0 };

	/** The integral of smoothstep(0, 1, x) from 0, x clamped to the ramp and run on past it. */
	double RampIntegral(double X)
	{
		if (X <= 0.0) { return 0.0; }
		if (X >= 1.0) { return X - 0.5; }
		return X * X * X - 0.5 * X * X * X * X;
	}
	/** A window coming in over Ramp from A and going over Ramp to B, and its integral from 0, at U (s into the cycle). */
	double Window(double U, double A, double B, double Ramp)
	{
		return SlonkScatter::SmoothStep(A, A + Ramp, U) - SlonkScatter::SmoothStep(B - Ramp, B, U);
	}
	double WindowIntegral(double U, double A, double B, double Ramp)
	{
		return Ramp * (RampIntegral((U - A) / Ramp) - RampIntegral((U - (B - Ramp)) / Ramp));
	}
	/** How far a herd walks in one cycle of its schedule, cm. */
	double CycleWalkCm(const FFaunaGroup& G)
	{
		return G.WalkCmS * (G.WalkB - G.WalkA - G.RampSec) + (G.TrotCmS - G.WalkCmS) * (G.TrotB - G.TrotA - G.RampSec);
	}
	double WrapHour(double Seconds)
	{
		const double T = FMath::Fmod(Seconds, SlonkFauna::ClockPeriodSec);
		return T < 0.0 ? T + SlonkFauna::ClockPeriodSec : T;
	}
	/** The theta along a herd's loop at a way along it from theta 0, cm (wrapped to the loop), from its table. */
	double ThetaAlong(const FFaunaGroup& G, double Cm)
	{
		const int32 N = G.ArcCm.Num() - 1;
		if (N < 1 || G.LengthCm <= 0.0) { return 0.0; }
		const double Total = G.ArcCm.Last();
		double S = FMath::Fmod(Cm / G.LengthCm, 1.0);
		if (S < 0.0) { S += 1.0; }
		S *= Total;
		int32 Lo = 0, Hi = N;
		while (Hi - Lo > 1)
		{
			const int32 Mid = (Lo + Hi) / 2;
			if ((double)G.ArcCm[Mid] <= S) { Lo = Mid; } else { Hi = Mid; }
		}
		const double A = G.ArcCm[Lo], B = G.ArcCm[Hi];
		const double F = B > A ? (S - A) / (B - A) : 0.0;
		return UE_DOUBLE_TWO_PI * ((double)Lo + F) / N;
	}
	/** Where along its loop a herd is at a time, cm from theta 0 (the loop's own way), as its schedule walks it. */
	double HerdAlongCm(const FFaunaGroup& G, double Seconds)
	{
		return G.StartCm + G.Turn * SlonkFauna::HerdWalkedCm(G, Seconds);
	}
	/** A herd's longest member, head and all, m: its walkers are at most a tenth longer than its span. */
	double HerdBodyM(const FFaunaGroup& G)
	{
		return 1.1 * 1.15 * (double)G.SpanM;
	}
	/** How far apart a herd's walkers' grazing circles' middles are kept, cm: its spacing, but never less than the longest
	 *  of them and the two smallest circles they amble round (three of the longest one's strides round) between them. */
	double HerdKeepCm(const FFaunaGroup& G)
	{
		const double SmallCircleM = 3.0 * SlonkFauna::Walker::StrideShare * 1.1 * (double)G.SpanM / UE_DOUBLE_TWO_PI;
		return 100.0 * FMath::Max(0.8 * (double)G.SpacingM, HerdBodyM(G) + 2.0 * SmallCircleM);
	}

	/** How far from its loop's middle a herd can be taken, cm: its loop's farthest, a fright's flight half again (a second
	 *  fright carries the first on) and the loop it comes back round, and the most the crew can walk it off its path. */
	double HerdFlightReachCm(const FFaunaGroup& G)
	{
		const SlonkFauna::FFaunaKindDesc& D = SlonkFauna::KindDesc(EFaunaKind::Herd);
		return G.ExtentCm() + 1.5 * D.FleeCm + D.FleeLoopCm + 3.0e4;
	}

	/** A herd's habitat read along its loop: its middle, 96 points round the loop; and for water, rings 60 m apart out to
	 *  as far as it can be taken, each read every 60 m or less round it (about 1,450 points for the widest loops, the
	 *  field's water alone, stopping at the first). */
	SlonkFauna::FHerdHabitatRead ReadHerdLoop(const SlonkFauna::FFaunaWorld& W, const FFaunaGroup& G)
	{
		SlonkFauna::FHerdHabitatRead R;
		const double Rad = W.P.RadiusCm;
		double WetSum = 0.0;
		auto Read = [&](const FVector& Dir)
		{
			const SlonkScatter::FHabitat H = SlonkScatter::HabitatAt(W.P, Dir);
			++R.Points;
			if (!SlonkFauna::HerdGrass(H, false)) { ++R.Off; return; }
			if (!SlonkFauna::HerdGrass(H, true)) { ++R.Rough; }
			R.MinFlat = FMath::Min(R.MinFlat, H.Flat);
			R.MaxForest = FMath::Max(R.MaxForest, H.Forest);
			R.MinWet = FMath::Min(R.MinWet, H.Wet);
			R.MaxWet = FMath::Max(R.MaxWet, H.Wet);
			WetSum += H.Wet;
			// On a locked world, only where the air is neither baked nor frozen.
			if (W.P.Climate == EClimateModel::Locked)
			{
				const double Heat = SlonkTerrain::TemperatureN(W.P, Dir, FMath::Max(H.HeightCm, 0.0));
				if (Heat < 0.06 || Heat > 0.6) { ++R.Off; }
			}
		};
		Read(G.Centre);
		for (int32 k = 0; k < 96 && R.Off == 0; ++k)
		{
			Read((G.Centre * Rad + LoopPoint(G, UE_DOUBLE_TWO_PI * k / 96.0)).GetSafeNormal());
		}
		if (R.Off > 0) { return R; }
		R.MeanWet = WetSum / FMath::Max(R.Points - R.Off, 1);
		// No water anywhere it could be taken: across its loop and out to a flight from it (half again, carried on by a
		// second fright) and the crew's 300 m beyond, read on rings 60 m apart, every 60 m or less round each, the field's
		// water to ten metres as HabitatAt reads it, so no pond or river much wider than a step of them is missed; the
		// first found is enough.
		const double FlightReach = HerdFlightReachCm(G);
		constexpr double WaterStepCm = 6000.0;
		const int32 Rings = FMath::CeilToInt32(FlightReach / WaterStepCm);
		for (int32 Ring = 1; Ring <= Rings && R.WaterNear == 0; ++Ring)
		{
			const double Rr = FlightReach * Ring / Rings;
			const int32 Points = FMath::Max(FMath::CeilToInt32(UE_DOUBLE_TWO_PI * Rr / WaterStepCm), 6);
			for (int32 k = 0; k < Points; ++k)
			{
				const double A = UE_DOUBLE_TWO_PI * (k + 0.5 * (Ring % 2)) / Points;
				const FVector Dir = (G.Centre + (G.East * FMath::Cos(A) + G.North * FMath::Sin(A)) * (Rr / Rad)).GetSafeNormal();
				if (SlonkTerrain::Sample(W.P, Dir, 1000.0).bWater) { ++R.WaterNear; break; }
			}
		}
		// No town near it, nor its ground (a cliff town's runs along its wall either way).
		if (W.bTowns)
		{
			const FCivilization Civ = W.People.bInhabited ? W.People : SlonkCivs::Colonists();
			TArray<FTownSite> Sites;
			SlonkCities::EnumerateTowns(W.P, Civ, G.Centre * Rad, G.ExtentCm() + 4.0e5, Sites, 32);
			for (const FTownSite& T : Sites)
			{
				const double Reach = (T.bCliff ? FMath::Sqrt(FMath::Square(T.RadiusCm) + FMath::Square(30000.0)) : T.RadiusCm) + 3.0e4;
				R.bTown |= FVector::Dist(T.LocalCm, G.Centre * Rad) < G.ExtentCm() + G.HerdRadiusCm + Reach;
			}
		}
		R.bSuits = R.Off == 0 && R.Rough <= 6 && R.WaterNear == 0 && !R.bTown;
		return R;
	}

	/** A herd in a cell: its schedule, its loop as long as the hour's schedule walks, its members; false where the
	 *  ground along its loop is not its grass, or the cell holds none. */
	bool HerdInCell(const SlonkFauna::FFaunaWorld& W, const SlonkFauna::FFaunaCell& C, FFaunaGroup& Out)
	{
		using namespace SlonkFauna;
		const FFaunaKindDesc& D = KindDesc(EFaunaKind::Herd);
		const int64 Across = CellsAcross(W.P, C.Kind);
		SlonkScatter::FDraw Dr(SlonkScatter::CellHash(C.Face, 40 + (int32)C.Kind, C.X, C.Y, W.Salt()));
		const double Wd = 2.0 / (double)Across;
		const double U = -1.0 + Wd * ((double)C.X + 0.2 + 0.6 * Dr.Next());
		const double V = -1.0 + Wd * ((double)C.Y + 0.2 + 0.6 * Dr.Next());
		const double Gate = Dr.Next();
		if (Gate >= FMath::Min(0.8 * (double)W.Life * (double)W.Density, 1.0)) { return false; }
		const FVector Centre = SlonkTerrain::CubeDir(C.Face, U, V).GetSafeNormal();
		// A first look at its middle: most cells are no grassland at all.
		if (!HerdGrass(SlonkScatter::HabitatAt(W.P, Centre), true)) { return false; }

		FFaunaGroup G;
		G.Id = C.Id();
		G.Kind = EFaunaKind::Herd;
		G.Habitat = EFlierHabitat::Meadow;
		G.Seed = ((uint32)(Dr.Next() * 65536.0) << 16) | (uint32)(Dr.Next() * 65536.0);
		G.Centre = Centre;
		TangentAxes(Centre, G.East, G.North);
		// Its schedule: grazing most of the cycle, walking a little over a quarter to a third of it, trotting a tenth
		// within the walk: about 2 km an hour.
		G.CycleSec = HerdCycles[FMath::Min((int32)(Dr.Next() * UE_ARRAY_COUNT(HerdCycles)), (int32)UE_ARRAY_COUNT(HerdCycles) - 1)];
		G.WalkCmS = 100.0 * FMath::Lerp(1.0, 1.4, Dr.Next());
		G.TrotCmS = 100.0 * FMath::Lerp(2.4, 3.4, Dr.Next());
		G.RampSec = 4.0;
		const double WalkLen = G.CycleSec * FMath::Lerp(0.28, 0.36, Dr.Next());
		const double TrotLen = FMath::Max(G.CycleSec * FMath::Lerp(0.07, 0.11, Dr.Next()), 2.0 * G.RampSec + 4.0);
		G.WalkA = FMath::Lerp(12.0, G.CycleSec - WalkLen - 12.0, Dr.Next());
		G.WalkB = G.WalkA + WalkLen;
		G.TrotA = G.WalkA + G.RampSec + (WalkLen - 2.0 * G.RampSec - TrotLen) * Dr.Next();
		G.TrotB = G.TrotA + TrotLen;
		G.PeriodSec = ClockPeriodSec;
		G.LengthCm = CycleWalkCm(G) * (ClockPeriodSec / G.CycleSec);
		// Its loop: two or three lobes, a gentle meander, sized to the length the hour walks.
		G.Lobes = 2 + FMath::Min((int32)(Dr.Next() * 2.0), 1);
		G.Lobe = FMath::Lerp(0.08, 0.2, Dr.Next());
		G.LobePhase = UE_DOUBLE_TWO_PI * Dr.Next();
		const double Meander = FMath::Lerp(0.02, 0.05, Dr.Next());
		G.MeanderK = G.Lobes + 2 + FMath::Min((int32)(Dr.Next() * 2.0), 1);
		G.Turn = Dr.Next() < 0.5 ? -1.0 : 1.0;
		const double StartShare = Dr.Next();
		G.RadiusCm = 1.0;
		G.MeanderCm = Meander;
		double Unit = 0.0;
		for (int32 k = 1; k <= NumArc; ++k) { Unit += FVector::Dist(LoopPoint(G, UE_DOUBLE_TWO_PI * k / NumArc), LoopPoint(G, UE_DOUBLE_TWO_PI * (k - 1) / NumArc)); }
		G.RadiusCm = G.LengthCm / FMath::Max(Unit, 1e-6);
		G.MeanderCm = Meander * G.RadiusCm;
		G.ArcCm.SetNum(NumArc + 1);
		G.ArcCm[0] = 0.f;
		double Run = 0.0;
		for (int32 k = 1; k <= NumArc; ++k)
		{
			Run += FVector::Dist(LoopPoint(G, UE_DOUBLE_TWO_PI * k / NumArc), LoopPoint(G, UE_DOUBLE_TWO_PI * (k - 1) / NumArc));
			G.ArcCm[k] = (float)Run;
		}
		G.StartCm = StartShare * G.LengthCm;
		G.SpeedCmS = G.LengthCm / G.PeriodSec;
		// Its members: mostly the smaller herds, from 15 to 60; a grazer's length, its spacing as cattle graze.
		const double Un = Dr.Next();
		G.Members = FMath::Clamp(15 + (int32)(46.0 * Un * Un), FMath::Max(D.GroupMin, 1), D.GroupMax);
		G.SpanM = FMath::Lerp(1.6f, 2.3f, (float)Dr.Next());
		G.SpacingM = FMath::Lerp(4.5f, 7.f, (float)Dr.Next());
		// Its formation wide enough for its circles' middles to lie that far apart (they fill under half of it).
		G.FormationCm = FMath::Sqrt((double)G.Members) * FMath::Max(100.0 * G.SpacingM * 0.55, 0.75 * HerdKeepCm(G));
		G.HerdRadiusCm = G.FormationCm + 100.0 * Walker::MaxCircleM + 0.6 * 110.0 * G.SpanM;
		G.GroundHalfCm = G.HerdRadiusCm + 100.0;
		G.CruiseCm = 0.0;
		G.FlapHz = 0.f;
		const double Tone = Dr.Next(), Vary = 0.85 + 0.3 * Dr.Next();
		// The grass all along its loop, no water within its flight, no town near.
		const FHerdHabitatRead H = ReadHerdLoop(W, G);
		if (!H.bSuits) { return false; }
		// The ground along its loop, at full detail (the game stands it on the drawn ground).
		G.Ground.SetNum(NumGround);
		for (int32 k = 0; k < NumGround; ++k)
		{
			G.Ground[k] = (float)FMath::Max(SlonkTerrain::SurfaceHeightCm(W.P, (Centre * W.P.RadiusCm + LoopPoint(G, UE_DOUBLE_TWO_PI * k / NumGround)).GetSafeNormal()), 0.0);
		}
		G.Pigment = HerdPigment(W.Profile.Look, W.Palette, FMath::Clamp(1.0 - H.MeanWet * 1.3, 0.0, 1.0), Tone, Vary);
		G.Glow = 0.f;
		Out = MoveTemp(G);
		return true;
	}
}

SlonkFauna::FHerdHabitatRead SlonkFauna::HerdHabitatAt(const FFaunaWorld& W, const FFaunaGroup& G)
{
	return ReadHerdLoop(W, G);
}

double SlonkFauna::HerdWalkedCm(const FFaunaGroup& G, double Seconds)
{
	if (G.CycleSec <= 0.0) { return 0.0; }
	const double T = WrapHour(Seconds);
	const double N = FMath::FloorToDouble(T / G.CycleSec);
	const double U = T - N * G.CycleSec;
	return N * CycleWalkCm(G) + G.WalkCmS * WindowIntegral(U, G.WalkA, G.WalkB, G.RampSec) + (G.TrotCmS - G.WalkCmS) * WindowIntegral(U, G.TrotA, G.TrotB, G.RampSec);
}

double SlonkFauna::HerdSpeedCmS(const FFaunaGroup& G, double Seconds)
{
	if (G.CycleSec <= 0.0) { return 0.0; }
	const double U = FMath::Fmod(WrapHour(Seconds), G.CycleSec);
	return G.WalkCmS * Window(U, G.WalkA, G.WalkB, G.RampSec) + (G.TrotCmS - G.WalkCmS) * Window(U, G.TrotA, G.TrotB, G.RampSec);
}

double SlonkFauna::HerdIntent(const FFaunaGroup& G, double Seconds)
{
	if (G.CycleSec <= 0.0) { return 0.0; }
	const double U = FMath::Fmod(WrapHour(Seconds), G.CycleSec);
	return U >= G.WalkA - HerdLeadSec && U < G.WalkB + 0.5 ? 1.0 : 0.0;
}

bool SlonkFauna::GroupInCell(const FFaunaWorld& W, const FFaunaCell& C, FFaunaGroup& Out)
{
	if (!W.Spawns(C.Kind) || (C.Kind != EFaunaKind::Flier && C.Kind != EFaunaKind::Herd)) { return false; }
	const FFaunaKindDesc& D = KindDesc(C.Kind);
	const int64 Across = CellsAcross(W.P, C.Kind);
	if (C.X < 0 || C.Y < 0 || C.X >= Across || C.Y >= Across || C.Face < 0 || C.Face > 5) { return false; }
	if (C.Kind == EFaunaKind::Herd) { return HerdInCell(W, C, Out); }
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

namespace
{
	/** A herd's frame on its loop at a time: on the field's ground, its own east and north laid level, the way it walks
	 *  along the loop (which it faces when it walks), whether it is set to walk. */
	SlonkFauna::FFaunaPose HerdPathAt(const FFaunaGroup& G, double RadiusCm, double Seconds)
	{
		auto At = [&](double S)
		{
			const double Th = ThetaAlong(G, HerdAlongCm(G, S));
			const FVector Dir = (G.Centre * RadiusCm + LoopPoint(G, Th)).GetSafeNormal();
			return Dir * (RadiusCm + LoopGround(G, Th));
		};
		SlonkFauna::FFaunaPose Pose;
		const double T = WrapHour(Seconds);
		Pose.PosCm = At(T);
		constexpr double H = 0.25;
		Pose.VelCmS = (At(T + H) - At(T - H)) / (2.0 * H);
		const FVector Up = Pose.PosCm.GetSafeNormal();
		const FVector East = (G.East - Up * FVector::DotProduct(G.East, Up)).GetSafeNormal();
		Pose.Rot = FRotationMatrix::MakeFromXZ(East, Up).ToQuat();
		// The way along the loop it walks, whether or not it is walking now (its tangent there, from half a metre either side).
		const double Along = HerdAlongCm(G, T);
		const FVector Ahead = LoopPoint(G, ThetaAlong(G, Along + G.Turn * 50.0)) - LoopPoint(G, ThetaAlong(G, Along - G.Turn * 50.0));
		Pose.Ahead = (Ahead - Up * FVector::DotProduct(Ahead, Up)).GetSafeNormal();
		Pose.Heading = FMath::Atan2(FVector::DotProduct(Ahead, Pose.Rot.GetAxisY()), FVector::DotProduct(Ahead, Pose.Rot.GetAxisX()));
		Pose.Intent = SlonkFauna::HerdIntent(G, T);
		double D = FMath::Fmod(Along, G.LengthCm);
		Pose.DistCm = D < 0.0 ? D + G.LengthCm : D;
		Pose.Bank = 0.0;
		return Pose;
	}

	/** A herd's members: their grazing circles' middles spread over its formation, at least HerdKeepCm apart; each
	 *  walker's length, stride (a whole number of them round its herd's loop) and circle (a whole number of strides round
	 *  it, leaving its longest member's length between it and its nearest neighbour's, so no two walkers ever meet). */
	void HerdMembersOf(const FFaunaGroup& G, TArray<FTransform>& OutXf, TArray<float>& OutCustom)
	{
		const int32 N = FMath::Max(G.Members, 0);
		if (N == 0) { return; }
		FRandomStream Rng((int32)G.Seed);
		const double Rf = G.FormationCm, Keep = HerdKeepCm(G);
		TArray<FVector> P;
		P.Reserve(N);
		// Each middle thrown into the formation until it lands Keep from every one before it: the formation is wide enough
		// that one nearly always does within 48 throws, and is widened by a twentieth for each 48 more that miss, so one
		// always does (the circles round them then always fit: HerdKeepCm).
		for (int32 i = 0; i < N; ++i)
		{
			FVector Best = FVector::ZeroVector;
			double BestGap = -1.0, Reach = Rf;
			for (int32 Try = 0; BestGap < Keep; ++Try)
			{
				if (Try > 0 && Try % 48 == 0) { Reach *= 1.05; }
				FVector B;
				do { B = FVector(Rng.FRandRange(-1.f, 1.f), Rng.FRandRange(-1.f, 1.f), 0.0); } while (B.SizeSquared() > 1.0);
				B *= Reach;
				double Gap = TNumericLimits<double>::Max();
				for (const FVector& Q : P) { Gap = FMath::Min(Gap, FVector::Dist2D(B, Q)); }
				if (Gap > BestGap) { BestGap = Gap; Best = B; }
			}
			P.Add(Best);
		}
		// How far each middle is from its nearest neighbour's, m.
		TArray<double> GapM;
		GapM.Init(1.0e9, N);
		for (int32 i = 0; i < N; ++i)
		{
			for (int32 j = i + 1; j < N; ++j)
			{
				const double L = FVector::Dist2D(P[i], P[j]) / 100.0;
				GapM[i] = FMath::Min(GapM[i], L);
				GapM[j] = FMath::Min(GapM[j], L);
			}
		}
		const double BodyM = HerdBodyM(G);
		const SlonkFauna::FFaunaKindDesc& D = SlonkFauna::KindDesc(G.Kind);
		const double LoopM = G.LengthCm / 100.0;
		OutXf.Reserve(N);
		OutCustom.Reserve(N * SlonkFauna::Custom::Num);
		for (int32 i = 0; i < N; ++i)
		{
			const FVector At = P[i];
			const double Size = FMath::Clamp((double)G.SpanM * Rng.FRandRange(0.9f, 1.1f), SlonkFauna::Walker::MinSizeM, 2.6);
			// A stride of about its share of its length, its herd's loop a whole number of them.
			const double Laps = FMath::Max(FMath::RoundToDouble(LoopM / (SlonkFauna::Walker::StrideShare * Size)), 1.0);
			const double Stride = LoopM / Laps;
			// Its grazing circle, a whole number of strides round (at least three), no wider than the box allows, and
			// keeping to its half of the room its nearest neighbour leaves it past the longest walker's length: any two
			// circles then leave that length between the walkers going round them.
			int32 K = FMath::Max(FMath::RoundToInt32(UE_DOUBLE_TWO_PI * Rng.FRandRange(1.2f, (float)SlonkFauna::Walker::MaxCircleM) / Stride), 3);
			while (K > 3 && K * Stride / UE_DOUBLE_TWO_PI > SlonkFauna::Walker::MaxCircleM) { --K; }
			while (K > 3 && K * Stride / UE_DOUBLE_TWO_PI > 0.5 * (GapM[i] - BodyM)) { --K; }
			const double Rho = K * Stride / UE_DOUBLE_TWO_PI;
			const float Way = Rng.FRand() < 0.5f ? -1.f : 1.f;
			const float Shade = Rng.FRandRange(0.82f, 1.18f);
			const float Seed = Rng.FRand();
			const float Start = Rng.FRand();
			OutXf.Add(FTransform(FQuat::Identity, FVector(At.X, At.Y, 0.0), FVector(Size)));
			const float C[SlonkFauna::Custom::Num] = { G.Pigment.R * Shade, G.Pigment.G * Shade, G.Pigment.B * Shade, G.Glow, Seed, (float)Size, Start, (float)Stride,
				Way * (float)Rho, (float)D.FadeCm };
			OutCustom.Append(C, SlonkFauna::Custom::Num);
		}
	}
}

SlonkFauna::FFaunaPose SlonkFauna::PathAt(const FFaunaGroup& G, double RadiusCm, double Seconds)
{
	if (G.Kind == EFaunaKind::Herd) { return HerdPathAt(G, RadiusCm, Seconds); }
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
	if (G.Kind == EFaunaKind::Herd) { HerdMembersOf(G, OutXf, OutCustom); return; }
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

namespace
{
	/** smootherstep of V from A to B: its first and second derivatives 0 at both ends. */
	double EaseC2(double A, double B, double V)
	{
		const double U = FMath::Clamp((V - A) / (B - A), 0.0, 1.0);
		return U * U * U * (U * (U * 6.0 - 15.0) + 10.0);
	}
	/** A steady pace eased in and out, 0 at U 0 to 1 at U 1: its rate comes in smoothly over the first quarter to a steady
	 *  4/3 and goes over the last, its first and second derivatives continuous throughout. */
	double PaceEase(double U)
	{
		constexpr double W = 0.25, C = 1.0 / (1.0 - W);
		U = FMath::Clamp(U, 0.0, 1.0);
		if (U < W) { return C * W * RampIntegral(U / W); }
		if (U > 1.0 - W) { return 1.0 - C * W * RampIntegral((1.0 - U) / W); }
		return C * (0.5 * W + (U - W));
	}
	/** How far a galloping kind's gallop has come on, 0 to 1: from a stand at its FleeT0, at its fastest FleeRiseSec in. */
	double GallopRise(const SlonkFauna::FPanic& P, const SlonkFauna::FFaunaKindDesc& D, double Now)
	{
		const double X = FMath::Max(Now - P.FleeT0, 0.0) / FMath::Max(D.FleeRiseSec, 0.1);
		return 1.0 - FMath::Exp(-X) * (1.0 + X);
	}
	/** How far a galloping kind has come back from its flight, 0 to 1, Age seconds after its startle: at a steady pace
	 *  from its FleeBackSec to the fright's end. */
	double ComeBack(const SlonkFauna::FPanic& P, const SlonkFauna::FFaunaKindDesc& D, double Age)
	{
		return PaceEase((Age - D.FleeBackSec) / FMath::Max(P.LiveSec - D.FleeBackSec, 1.0));
	}
}

FVector SlonkFauna::PanicOffset(const FPanic& P, const FFaunaKindDesc& D, double Now)
{
	const double Age = Now - P.T0;
	const double Live = P.LiveSec;
	if (Age < 0.0 || Age >= Live) { return FVector::ZeroVector; }
	const double Hold = 1.0 - SlonkScatter::SmoothStep(0.3 * Live, Live, Age);
	const double CarryFade = 1.0 - SlonkScatter::SmoothStep(0.0, Live, Age);
	if (D.bGallop)
	{
		// A walker turns to flee where it stands, then breaks into a gallop from a stand (at its FleeT0), at its fastest
		// (FleeCm over e times FleeRiseSec) that long in, and slows as it nears its flight's end; from FleeBackSec it comes
		// back at a steady walking pace, eased in and out, onto its path by the fright's end. A second fright carries on
		// the gallop it was in as it turns (slowing over a few seconds), what was left of the last flight fading as this
		// one runs.
		const double Rise = GallopRise(P, D, Now);
		constexpr double Coast = 3.0;
		// Its way eased in and out smoothly enough (to the second derivative) that its walkers' way never turns at a
		// jump: back from its flight, and back round a loop rather than turning about where it stops: off to its side over
		// the forty seconds its flight slows and turns back, held there as it walks back, and onto its path over the last
		// half of its walk back.
		const double Back = 1.0 - ComeBack(P, D, Age);
		const double Loop = D.FleeLoopCm * EaseC2(D.FleeBackSec - 10.0, D.FleeBackSec + 30.0, Age) * (1.0 - EaseC2(0.5 * (D.FleeBackSec + Live), Live, Age));
		return (P.Carry + P.CarryVel * (Coast * (1.0 - FMath::Exp(-Age / Coast)))) * CarryFade + P.Dir * (D.FleeCm * Rise * Back) + P.Side * Loop;
	}
	// Away at a bird's escape speed (FleeCm over 8 s, at first), held for half a minute, then back over the rest; what
	// was left of the last flight fades as this runs.
	const double Rise = 1.0 - FMath::Exp(-Age / D.FleeRiseSec);
	return P.Carry * CarryFade + P.Dir * (D.FleeCm * Rise * Hold);
}

FVector SlonkFauna::HerdFrightOffset(const FFaunaGroup& G, const FFaunaKindDesc& D, const FPanic& P, double RadiusCm, double Now)
{
	FVector Off = PanicOffset(P, D, Now);
	if (P.bHold && P.IsLive(Now))
	{
		// Its path's own walk since the startle, held back while its walkers turn to flee, let go as its gallop comes on
		// (and what is left of it as it comes back), so it gallops off from where it stood.
		const double Let = (1.0 - GallopRise(P, D, Now)) * (1.0 - ComeBack(P, D, Now - P.T0));
		if (Let > 0.0) { Off += (PathAt(G, RadiusCm, P.T0).PosCm - PathAt(G, RadiusCm, Now).PosCm) * Let; }
	}
	return Off;
}

SlonkFauna::FFaunaPose SlonkFauna::FlightAt(const FFaunaGroup& G, const FFaunaKindDesc& D, const FPanic& P, double RadiusCm, double Now, const FFaunaPose& Was, double Dt)
{
	const FFaunaPose Path = PathAt(G, RadiusCm, Now);
	if (!P.IsLive(Now)) { return Path; }
	// Where its path and its flight put it now, a moment on and twice that (on from now: before its latest startle it
	// was on another flight).
	constexpr double H = 0.25, Hv = 0.05;
	auto Flown = [&](double T) { return PathAt(G, RadiusCm, T).PosCm + PanicOffset(P, D, T); };
	const FVector X0 = Path.PosCm + PanicOffset(P, D, Now), X1 = Flown(Now + H), X2 = Flown(Now + 2.0 * H);
	const FVector Up = X0.GetSafeNormal();
	auto Level = [&Up](const FVector& V) { return V - Up * FVector::DotProduct(V, Up); };
	FFaunaPose Pose;
	Pose.PosCm = X0;
	Pose.VelCmS = Path.VelCmS + (PanicOffset(P, D, Now + Hv) - PanicOffset(P, D, Now)) / Hv;
	// Its heading: from the way it faced a moment before, round toward the way it flies.
	FVector Fwd0 = Level(Was.Rot.GetForwardVector()).GetSafeNormal();
	if (Fwd0.IsNearlyZero()) { Fwd0 = Level(Path.Rot.GetForwardVector()).GetSafeNormal(); }
	FVector Want = Level(Pose.VelCmS).GetSafeNormal();
	if (Want.IsNearlyZero()) { Want = Fwd0; }
	const FVector Right = FVector::CrossProduct(Up, Fwd0);
	const double Ang = FMath::Atan2(FVector::DotProduct(Want, Right), FVector::DotProduct(Want, Fwd0));
	const double MaxTurn = FlightTurnRadS * FMath::Max(Dt, 0.0);
	const double Turn = FMath::Clamp(Ang, -MaxTurn, MaxTurn);
	Pose.Rot = FRotationMatrix::MakeFromXZ((Fwd0 * FMath::Cos(Turn) + Right * FMath::Sin(Turn)).GetSafeNormal(), Up).ToQuat();
	// Its bank (as PathAt's: a turn toward +y drops the right wing): hard over while it wheels round, else into the turn
	// its flight makes; eased toward that from the bank it had.
	const bool bWheeling = FMath::Abs(Ang) > MaxTurn + 0.02;
	const FVector A = Level(X1 - X0), B = Level(X2 - X1);
	const double Yaw = bWheeling ? FMath::Sign(Ang) * FlightTurnRadS : FMath::Atan2(FVector::DotProduct(FVector::CrossProduct(A, B), Up), FVector::DotProduct(A, B)) / H;
	const double WantBank = FMath::Clamp(-FMath::Atan(Level(Pose.VelCmS).Size() * Yaw / 981.0), -0.7, 0.7);
	const double MaxBank = FlightBankRadS * FMath::Max(Dt, 0.0);
	Pose.Bank = Was.Bank + FMath::Clamp(WantBank - Was.Bank, -MaxBank, MaxBank);
	return Pose;
}

bool SlonkFauna::Scares(const FFaunaKindDesc& D, const FVector& GroupPosCm, const FFaunaThreat& T, double GroupRadiusCm)
{
	const double Dist = FVector::Dist(GroupPosCm, T.PosCm);
	if (T.bCraft) { return !T.bLanded && T.AltitudeCm >= 0.0 && T.AltitudeCm < D.ScareAltitudeCm && Dist < D.ScareRadiusCm; }
	return Dist - GroupRadiusCm < D.CrewScareCm;
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

bool SlonkFauna::StartleGroup(FPanic& P, const FFaunaKindDesc& D, const FVector& GroupPosCm, const FFaunaThreat& T, double Now, double GroupRadiusCm)
{
	if (!Scares(D, GroupPosCm, T, GroupRadiusCm)) { return false; }
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
	// Where its last flight has it now, and (a galloping kind) how fast it was going: it carries on from both as it
	// turns (a calm one from none).
	const FVector Was = PanicOffset(P, D, Now);
	const FVector WasVel = D.bGallop && P.IsLive(Now) ? (Was - PanicOffset(P, D, Now - 0.05)) / 0.05 : FVector::ZeroVector;
	if (D.bGallop) { P.CarryVel = WasVel; }
	P.Carry = Was;
	P.Dir = (Away.GetSafeNormal() + Up * D.FleeUp).GetSafeNormal();
	// The side it loops round to come back (a galloping kind): away from the way the threat goes.
	P.Side = FVector::CrossProduct(Up, Away).GetSafeNormal();
	if (FVector::DotProduct(Level(T.VelCmS), P.Side) > 0.0) { P.Side = -P.Side; }
	P.T0 = Now;
	P.LiveSec = D.FrightSec;
	P.FleeT0 = Now + D.FleeTurnSec;
	P.bHold = false;
	if (!bOpen)
	{
		P.ScatterT0 = Now;
		P.ScatterDir = P.Dir;
	}
	P.OnsetT0 = Urge <= 0.0 ? Now : Now - (Urge >= 1.0 ? 1.0 : 0.3 * InverseSmoothStep(Urge));
}

bool SlonkFauna::StartleHerd(FPanic& P, FHerdState& S, const FFaunaGroup& G, const FFaunaKindDesc& D, double RadiusCm, const FFaunaThreat& T, double Now, bool bForce)
{
	if (!S.bStarted) { return false; }
	// Where its fright has it now (its hold of its path among it) and how fast that is taking it.
	const FVector Was = HerdFrightOffset(G, D, P, RadiusCm, Now);
	if (!bForce)
	{
		if (!Scares(D, S.FramePos, T, T.bCraft ? 0.0 : G.HerdRadiusCm)) { return false; }
		if (P.IsLive(Now) && Now - P.T0 < RestartleSec) { return false; }
		// A herd already chased half again its flight off its path keeps the flight it has.
		if (Was.Size() > 1.5 * D.FleeCm) { return false; }
	}
	const FVector WasVel = P.IsLive(Now) ? (Was - HerdFrightOffset(G, D, P, RadiusCm, Now - 0.05)) / 0.05 : FVector::ZeroVector;
	const FVector Up = S.FramePos.GetSafeNormal();
	auto Level = [&Up](const FVector& V) { return V - Up * FVector::DotProduct(V, Up); };
	// How fast its frame goes over the ground (its path's own walk and its fright's), and the way it faces.
	const double FrameCmS = Level(PathAt(G, RadiusCm, Now).VelCmS + WasVel).Size();
	const FVector Facing = Level(S.FrameRot.GetAxisX() * FMath::Cos(S.Heading) + S.FrameRot.GetAxisY() * FMath::Sin(S.Heading)).GetSafeNormal();
	const bool bGalloping = S.Travel >= 1.0 && FrameCmS > 100.0 * HerdGallopMS;
	// Its alarm, its moment and the way away from the threat, as any group's.
	StartleNow(P, D, S.FramePos, T, Now);
	FVector Away = Level(P.Dir).GetSafeNormal();
	if (Away.IsNearlyZero()) { Away = Facing; }
	P.Carry = Was;
	if (bGalloping)
	{
		// It cannot stop dead: it turns away on the run, carrying its speed, or (the fright ahead of it, more than it can
		// turn on the run) flees on the way it is going.
		if (FVector::DotProduct(Away, Facing) < FMath::Cos(TurnAroundRad)) { Away = Facing; }
		P.CarryVel = WasVel;
		P.bHold = false;
		P.FleeT0 = Now + D.FleeTurnSec;
	}
	else
	{
		// Its path walking while it is frightened, it flees across the way its path goes (between a third of a turn and a
		// sixth of one off it, on the side away from the threat): fled along that way or back against it, its flight and
		// its path's walk would cancel as its gallop slows or as it comes back, its way swinging right round as it all but
		// stood.
		const FFaunaPose Path = PathAt(G, RadiusCm, Now);
		bool bPathWalks = false;
		for (double At = 0.0; At <= D.FrightSec && !bPathWalks; At += 5.0) { bPathWalks = HerdSpeedCmS(G, Now + At) > 1.0; }
		const FVector PathWay = Level(Path.Ahead).GetSafeNormal();
		if (bPathWalks && !PathWay.IsNearlyZero())
		{
			FVector Across = FVector::CrossProduct(Up, PathWay);
			const double Side = FVector::DotProduct(Away, Across);
			if (Side < 0.0 || (Side == 0.0 && FVector::DotProduct(Level(T.VelCmS), Across) > 0.0)) { Across = -Across; }
			// Turned from straight across toward the way away from the threat, a twelfth of a turn at most.
			const double Off = FMath::Atan2(FVector::DotProduct(Away, PathWay), FMath::Max(FVector::DotProduct(Away, Across), 0.0));
			const double Turn = FMath::Clamp(Off, -UE_DOUBLE_PI / 6.0, UE_DOUBLE_PI / 6.0);
			Away = (Across * FMath::Cos(Turn) + PathWay * FMath::Sin(Turn)).GetSafeNormal();
		}
		// It stops where it stands, its path held there (and its walk out of the crew's way), its walkers turning back and
		// round to the way it flees before its gallop comes on: the rest of any turn under way, back to their grazing, and
		// round to it.
		P.CarryVel = FVector::ZeroVector;
		P.bHold = true;
		double Stand = 0.0;
		if (S.Travel > 0.0)
		{
			Stand = (S.PivotSign > 0.0 ? 2.0 - S.Travel : S.Travel) / HerdShakenTurnRate;
			S.bTurnAround = true;
		}
		S.AvoidPace = 0.0;
		S.AvoidGoing = FVector::ZeroVector;
		P.FleeT0 = Now + Stand + D.FleeTurnSec;
	}
	P.Dir = (Away + Up * D.FleeUp).GetSafeNormal();
	P.Side = FVector::CrossProduct(Up, Away).GetSafeNormal();
	if (FVector::DotProduct(Level(T.VelCmS), P.Side) > 0.0) { P.Side = -P.Side; }
	return true;
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

namespace
{
	/** A herd's own east and north laid on the ground's tangent plane at a place, and the up there. */
	void HerdAxes(const FFaunaGroup& G, const FVector& FramePos, FVector& OutUp, FVector& OutEast, FVector& OutNorth)
	{
		OutUp = FramePos.GetSafeNormal();
		OutEast = (G.East - OutUp * FVector::DotProduct(G.East, OutUp)).GetSafeNormal();
		if (OutEast.IsNearlyZero()) { FVector N; TangentAxes(OutUp, OutEast, N); }
		OutNorth = FVector::CrossProduct(OutUp, OutEast);
	}
	/** The plane's normal from its rise over run along an east and a north. */
	FVector PlaneNormal(const FVector& Up, const FVector& East, const FVector& North, double SlopeE, double SlopeN)
	{
		return (Up - East * SlopeE - North * SlopeN).GetSafeNormal();
	}
	/** The grid's node m (HerdData's order: row by row from the most negative y, the corners left out) as (i, j). */
	FIntPoint GroundNodeAt(int32 M)
	{
		if (M < 3) { return FIntPoint(1 + M, 0); }
		if (M < 18) { return FIntPoint((M - 3) % 5, 1 + (M - 3) / 5); }
		return FIntPoint(1 + (M - 18), 4);
	}
	float SmoothF(float A, float B, float X) { const float T = FMath::Clamp((X - A) / (B - A), 0.f, 1.f); return T * T * (3.f - 2.f * T); }
}

void SlonkFauna::SampleHerdGround(const FFaunaGroup& G, double RadiusCm, const FVector& FramePos, TFunctionRef<double(const FVector&)> HeightAt, FHerdGround& Out)
{
	FVector Up, East, North;
	HerdAxes(G, FramePos, Up, East, North);
	// The nodes laid on the plane last fitted there (the tangent plane the first time), so each is read under where the
	// material puts it; the ground at each straight down the radial from it.
	const FVector Z0 = Out.bValid ? PlaneNormal(Up, East, North, Out.SlopeE, Out.SlopeN) : Up;
	const FVector X0 = (East - Z0 * FVector::DotProduct(East, Z0)).GetSafeNormal();
	const FVector Y0 = FVector::CrossProduct(Z0, X0);
	const double Half = FMath::Max(G.GroundHalfCm, 100.0), Sp = Half * 0.5;
	FVector Ground[HerdData::GroundNodes];
	double SumXZ = 0.0, SumXX = 0.0, SumYZ = 0.0, SumYY = 0.0;
	for (int32 M = 0; M < HerdData::GroundNodes; ++M)
	{
		const FIntPoint Ij = GroundNodeAt(M);
		const double X = (Ij.X - 2) * Sp, Y = (Ij.Y - 2) * Sp;
		const FVector Dir = (FramePos + X0 * X + Y0 * Y).GetSafeNormal();
		Ground[M] = Dir * (RadiusCm + HeightAt(Dir));
		const double Z = FVector::DotProduct(Ground[M] - FramePos, Z0);
		SumXZ += X * Z; SumXX += X * X; SumYZ += Y * Z; SumYY += Y * Y;
	}
	// The plane through the frame's middle (on the ground) fitted to them: the nodes lie evenly either way, so its tilt
	// along each axis is the least squares' alone.
	const FVector N = (Z0 - X0 * (SumXZ / SumXX) - Y0 * (SumYZ / SumYY)).GetSafeNormal();
	const double Nu = FMath::Max(FVector::DotProduct(N, Up), 0.2);
	Out.SlopeE = -FVector::DotProduct(N, East) / Nu;
	Out.SlopeN = -FVector::DotProduct(N, North) / Nu;
	const FVector Z = PlaneNormal(Up, East, North, Out.SlopeE, Out.SlopeN);
	for (int32 M = 0; M < HerdData::GroundNodes; ++M)
	{
		Out.Node[M] = (float)FMath::Clamp(FVector::DotProduct(Ground[M] - FramePos, Z), -HerdData::MaxGroundCm, HerdData::MaxGroundCm);
	}
	Out.HalfCm = Half;
	Out.bValid = true;
}

FQuat SlonkFauna::HerdFrameRot(const FFaunaGroup& G, const FVector& FramePos, const FHerdGround& Ground)
{
	FVector Up, East, North;
	HerdAxes(G, FramePos, Up, East, North);
	const FVector Z = Ground.bValid ? PlaneNormal(Up, East, North, Ground.SlopeE, Ground.SlopeN) : Up;
	const FVector X = (East - Z * FVector::DotProduct(East, Z)).GetSafeNormal();
	return FRotationMatrix::MakeFromXZ(X, Z).ToQuat();
}

float SlonkFauna::HerdGroundAt(const float* Gd, float X, float Y)
{
	// fauna_material.py's walker branch, line for line.
	const float Half = FMath::Max(Gd[HerdData::GroundHalfCm], 100.f), Sp = Half * 0.5f;
	float H[25];
	for (int32 M = 0; M < HerdData::GroundNodes; ++M) { const FIntPoint Ij = GroundNodeAt(M); H[Ij.Y * 5 + Ij.X] = Gd[HerdData::Ground0 + M]; }
	H[0] = H[1] + H[5] - H[6];
	H[4] = H[3] + H[9] - H[8];
	H[20] = H[21] + H[15] - H[16];
	H[24] = H[23] + H[19] - H[18];
	const float Gx = FMath::Clamp(X / Sp + 2.f, 0.f, 4.f), Gy = FMath::Clamp(Y / Sp + 2.f, 0.f, 4.f);
	const int32 Ix = FMath::Min((int32)FMath::FloorToFloat(Gx), 3), Iy = FMath::Min((int32)FMath::FloorToFloat(Gy), 3);
	const float Fx = Gx - Ix, Fy = Gy - Iy;
	const float A = FMath::Lerp(H[Iy * 5 + Ix], H[Iy * 5 + Ix + 1], Fx), B = FMath::Lerp(H[(Iy + 1) * 5 + Ix], H[(Iy + 1) * 5 + Ix + 1], Fx);
	return FMath::Clamp(FMath::Lerp(A, B, Fy), -(float)HerdData::MaxGroundCm, (float)HerdData::MaxGroundCm);
}

void SlonkFauna::HerdFrightData(const FPanic& P, double Now, double ClockOffsetSec, float* Group)
{
	const bool bLive = P.IsLive(Now);
	Group[HerdData::Startled] = bLive ? 1.f : 0.f;
	Group[HerdData::PanicT0] = bLive ? OnMaterialClock(P.T0, ClockOffsetSec) : 0.f;
	Group[HerdData::OnsetT0] = bLive ? OnMaterialClock(P.OnsetT0, ClockOffsetSec) : 0.f;
}

void SlonkFauna::StartHerd(FHerdState& S, const FFaunaGroup& G, double Seconds)
{
	S = FHerdState();
	// As far along its loop as the hour has walked it (whole strides round the loop, so any start will do but this one
	// is the time's alone), as long grazed as the hour, turned to walk as its schedule has it; it faces the way along
	// its loop at its first step. Its last turn long past, the way it now is (to walk on the move, back to graze if not).
	S.DistM = FMath::Fmod(FMath::Max(HerdWalkedCm(G, Seconds), 0.0) / 100.0, FMath::Max(G.LengthCm / 100.0, 1.0));
	S.GrazeSec = WrapHour(Seconds);
	S.Travel = HerdIntent(G, Seconds);
	S.SpeedMS = HerdSpeedCmS(G, Seconds) / 100.0;
	S.GaitMS = S.Travel >= 1.0 ? S.SpeedMS : 0.0;
	S.PivotSign = S.Travel >= 1.0 ? 1.0 : -1.0;
	S.PivotStartM = S.DistM - 1000.0;
	S.GrazeRef = S.GrazeSec - 900.0;
	S.SinceLastTurn = S.PivotSign > 0.0 ? S.GrazeSec - 900.0 : 1000.0;
	S.bStarted = false;
}

double SlonkFauna::CrewGapCm(const FFaunaGroup& G, const FVector& FramePos, const FVector& CrewPosCm)
{
	const FVector Up = FramePos.GetSafeNormal();
	FVector D = CrewPosCm - FramePos;
	D -= Up * FVector::DotProduct(D, Up);
	return D.Size() - G.HerdRadiusCm;
}

bool SlonkFauna::KeepFromCrew(FHerdState& S, const FFaunaGroup& G, const FFaunaKindDesc& D, const TArray<FVector>& CrewPosCm, double Dt)
{
	if (!S.bStarted || Dt <= 0.0) { return false; }
	const FVector Up = S.FramePos.GetSafeNormal();
	auto Level = [&Up](const FVector& V) { return V - Up * FVector::DotProduct(V, Up); };
	const double Keep = D.KeepFromCrewCm;
	FVector Push = FVector::ZeroVector;
	double Nearest = TNumericLimits<double>::Max();
	for (const FVector& C : CrewPosCm)
	{
		const FVector Away = Level(S.FramePos - C);
		const double Gap = Away.Size() - G.HerdRadiusCm;
		Nearest = FMath::Min(Nearest, Gap);
		// Out to 10 m past its keeping distance, the nearer the harder.
		if (Keep > 0.0 && Gap < Keep + 1000.0) { Push += Away.GetSafeNormal() * (Keep + 1000.0 - Gap); }
	}
	// It goes once all of it is on the move the way it means to go (its walkers having turned to it where they stood),
	// coming up to its pace over a second, and eases off over half a second when it may not (or would go well off the way
	// it faces: it turns about first).
	const FVector Facing = S.FrameRot.GetAxisX() * FMath::Cos(S.Heading) + S.FrameRot.GetAxisY() * FMath::Sin(S.Heading);
	const bool bOnTheMove = S.Travel >= 1.0 && !S.bTurnAround;
	auto Pace = [&](const FVector& Way)
	{
		const bool bGo = bOnTheMove && FVector::DotProduct(Way.GetSafeNormal(), Facing) > FMath::Cos(TurnAroundRad);
		S.AvoidPace = FMath::Clamp(S.AvoidPace + (bGo ? Dt : -2.0 * Dt), 0.0, 1.0);
		// It walks the way it wants once it may; easing off, on along the way it was walking, so it stops going forward
		// (its legs walking it down) and turns about only once it stands.
		if (bGo || S.AvoidPace <= 0.0) { S.AvoidGoing = Way; }
		S.Avoid += S.AvoidGoing * (S.AvoidPace * Dt);
	};
	if (!Push.IsNearlyZero())
	{
		// Off at a walk, at a trot for anyone close in; never further than 300 m off its path.
		S.AvoidWay = Push.GetSafeNormal() * (Nearest < Keep * 0.6 ? 260.0 : 140.0);
		Pace(S.AvoidWay);
		S.Avoid = S.Avoid.GetClampedToMaxSize(3.0e4);
		return true;
	}
	if (S.Avoid.IsNearlyZero(0.01)) { S.Avoid = FVector::ZeroVector; S.AvoidWay = FVector::ZeroVector; S.AvoidGoing = FVector::ZeroVector; S.AvoidPace = 0.0; return false; }
	// Nobody near: back to its path at a stroll, unless that would walk it at someone.
	const FVector Back = -S.Avoid.GetSafeNormal();
	for (const FVector& C : CrewPosCm)
	{
		if (Level(S.FramePos + Back * 100.0 - C).Size() - G.HerdRadiusCm < Keep + 3000.0) { S.AvoidWay = FVector::ZeroVector; S.AvoidGoing = FVector::ZeroVector; S.AvoidPace = 0.0; return false; }
	}
	S.AvoidWay = Back * FMath::Min(60.0, S.Avoid.Size() / FMath::Max(Dt, 1e-3));
	Pace(S.AvoidWay);
	return true;
}

void SlonkFauna::StepHerd(FHerdState& S, const FFaunaGroup& G, double RadiusCm, double Now, double Dt, const FVector& OffsetCm, bool bShaken, bool bMoving, bool bRefreshGround,
	TFunctionRef<double(const FVector&)> HeightAt, double ClockOffsetSec, float* OutData, const FVector* OffsetAheadCm)
{
	const FFaunaPose Pose = PathAt(G, RadiusCm, Now);
	// Its place on its path, off it by its fright and the crew, set on the drawn ground there.
	const FVector Dir = (Pose.PosCm + OffsetCm).GetSafeNormal();
	const FVector Pos = Dir * (RadiusCm + HeightAt(Dir));
	if (bRefreshGround || !S.Ground.bValid) { SampleHerdGround(G, RadiusCm, Pos, HeightAt, S.Ground); }
	const FQuat Rot = HerdFrameRot(G, Pos, S.Ground);
	const FVector Ax = Rot.GetAxisX(), Ay = Rot.GetAxisY(), Az = Rot.GetAxisZ();
	// A way over the ground (level) laid on its plane straight up or down, so it runs over the same ground.
	auto LiftOnPlane = [&Dir, &Az](const FVector& V)
	{
		const FVector Level = V - Dir * FVector::DotProduct(V, Dir);
		return Level - Dir * (FVector::DotProduct(Level, Az) / FMath::Max(FVector::DotProduct(Dir, Az), 0.2));
	};
	auto AngleOf = [&Ax, &Ay](const FVector& V) { return FMath::Atan2(FVector::DotProduct(V, Ay), FVector::DotProduct(V, Ax)); };
	const bool bStep = S.bStarted && Dt > 0.0;
	// How far its frame went along its plane since the last step, laid level first and then onto the plane (so the drawn
	// ground's own rise or fall under it, as its chunks split, moves no feet), and how much of that was ahead, the way its
	// walkers faced: only that walks their legs on.
	double Moved = 0.0, Ahead = 0.0;
	FVector Way = FVector::ZeroVector;
	if (bStep)
	{
		Way = LiftOnPlane(Pos - S.FramePos);
		Moved = Way.Size();
		// A jump (the paths' clock moved): no walk.
		if (Moved > 5000.0) { Moved = 0.0; Way = FVector::ZeroVector; }
		Ahead = FMath::Max(FVector::DotProduct(Way, Ax * FMath::Cos(S.Heading) + Ay * FMath::Sin(S.Heading)), 0.0);
	}
	// The way it is going, on the move (its path's way now and its fright's and the crew's since the last step, once it
	// goes faster than a creep); else the way it is about to go (its path and that offset a second on); else, its walk
	// near, the way along its loop; else none (it holds the way it has). Standing grazing, only the way it is about to go:
	// it turns to that before its frame moves.
	const FVector Moving = bStep ? LiftOnPlane(Pose.VelCmS + (OffsetCm - S.LastOffset) / Dt) : FVector::ZeroVector;
	const FVector Going = LiftOnPlane(PathAt(G, RadiusCm, Now + 1.0).PosCm - Pose.PosCm + ((OffsetAheadCm ? *OffsetAheadCm : OffsetCm) - OffsetCm));
	double Target = S.Heading;
	bool bTarget = true;
	if (S.Travel > 0.0 && Moving.Size() > 10.0) { Target = AngleOf(Moving); }
	else if (Going.Size() > 2.0) { Target = AngleOf(Going); }
	else if (Pose.Intent > 0.0 || !S.bStarted) { Target = AngleOf(Pose.Ahead); }
	else { bTarget = false; }
	const double Off = FMath::UnwindRadians(Target - S.Heading);
	// Asked, on the move, to go well off the way it walks (or, its frame all but standing, any way much off it): it stops,
	// its walkers turn back to their grazing where they stand, the way is set while it stands grazing, and it sets off
	// again (its walkers turning to it).
	const double Travel0 = S.Travel;
	const bool bStanding = Moving.Size() <= 10.0;
	if (bStep && bTarget && Travel0 > 0.0 && FMath::Abs(Off) > (bStanding ? TurnStandingRad : TurnAroundRad)) { S.bTurnAround = true; }
	if (Travel0 <= 0.0) { S.bTurnAround = false; }
	// Turned to walk as its schedule, its fright or the crew ask (quickly when frightened), or back to graze; a turn under
	// way runs on to its end the way it began, never turned back half way (its walkers' steps through it are one turn).
	double Want = S.bTurnAround ? 0.0 : FMath::Max3(Pose.Intent, bShaken ? 1.0 : 0.0, bMoving ? 1.0 : 0.0);
	if (Travel0 > 0.0 && Travel0 < 1.0) { Want = S.PivotSign > 0.0 ? 1.0 : 0.0; }
	const double Rate = bShaken ? HerdShakenTurnRate : HerdTurnRate;
	const double Travel1 = bStep ? FMath::Clamp(Travel0 + FMath::Clamp(Want - Travel0, -Rate * Dt, Rate * Dt), 0.0, 1.0) : Travel0;
	// Its way: set at once while it grazes (no walker faces it then), turned as it walks, and held while its walkers turn
	// to walk or back to graze (so each of them turns the same way all through its turn). As it walks it turns by how
	// sharply its way turned over the step (WalkKap, as the material ran it on) for how far it went ahead; how sharply it
	// turns from here is steered toward the way it goes (as sharply as that way is turning, and more to close what is
	// left of the gap over SteerM, or as sharply as it can still straighten out by the time it faces it if that is less),
	// no more than MaxWalkKap a metre nor a few radians a second, and
	// changes by no more than MaxKapRate a second: a planted hoof is carried round by how sharply its body turns now.
	if (!S.bStarted)
	{
		// Come about on the move, it faces the way its path goes now and turns as sharply as that way turns.
		if (Travel0 >= 1.0 && Pose.VelCmS.Size() > 10.0)
		{
			Target = AngleOf(LiftOnPlane(Pose.VelCmS));
			const FVector Was = LiftOnPlane(PathAt(G, RadiusCm, Now - 0.5).VelCmS), Will = LiftOnPlane(PathAt(G, RadiusCm, Now + 0.5).VelCmS);
			S.WalkKap = FMath::Clamp(FMath::UnwindRadians(AngleOf(Will) - AngleOf(Was)) / FMath::Max(Pose.VelCmS.Size() / 100.0, 0.1), -Walker::MaxWalkKap, Walker::MaxWalkKap);
		}
		S.Heading = Target; S.TurnHeading = Target; S.LastTurnHeading = Target;
	}
	else if (Travel0 <= 0.0) { if (bTarget) { S.Heading += Off; } }
	else if (bStep && Travel0 >= 1.0 && Travel1 >= 1.0)
	{
		const double AheadM = Ahead / 100.0;
		S.Heading += S.WalkKap * AheadM;
		const double V = FMath::Max(AheadM / Dt, 0.1);
		const double Left = bTarget ? FMath::UnwindRadians(Target - S.Heading) : 0.0;
		const double Along = bTarget && S.bTargetWas && AheadM > 0.005 ? FMath::Clamp(FMath::UnwindRadians(Target - S.TargetWas) / AheadM, -Walker::MaxWalkKap, Walker::MaxWalkKap) : 0.0;
		const double Cap = FMath::Min(Walker::MaxWalkKap, (bShaken ? 2.5 : 1.5) / V);
		const double Close = FMath::Min(FMath::Abs(Left) / Walker::SteerM, FMath::Sqrt(2.0 * Walker::MaxKapRate * FMath::Abs(Left) / V));
		const double Steer = FMath::Clamp(Along + FMath::Sign(Left) * Close, -Cap, Cap);
		const double Most = Walker::MaxKapRate * Dt;
		S.WalkKap = FMath::Clamp(S.WalkKap + FMath::Clamp(Steer - S.WalkKap, -Most, Most), -Walker::MaxWalkKap, Walker::MaxWalkKap);
	}
	S.TargetWas = Target;
	S.bTargetWas = bTarget && Travel0 >= 1.0;
	S.Heading = FMath::UnwindRadians(S.Heading);
	// A new turn begins: what came between it and the last (the other way), so a walker's feet still down from the last are
	// carried through the last, that and this one: to walk, the grazing clock the last stop left it at (it has grazed
	// since by its own steps); back to graze, how far its legs have walked since it last set off. Its way through each.
	const bool bNewTurn = Travel1 != Travel0 && (Travel0 <= 0.0 || Travel0 >= 1.0);
	if (bNewTurn)
	{
		// A walk sets off straight (no foot is carried by how sharply it turns until its walkers have turned to it).
		if (Travel1 > Travel0) { S.SinceLastTurn = S.GrazeRef; S.WalkKap = 0.0; }
		else { S.SinceLastTurn = FMath::Clamp(S.DistM - (S.PivotStartM + Walker::PivotM), 0.0, 1000.0); }
		S.LastTurnHeading = S.TurnHeading;
		S.TurnHeading = S.Heading;
	}
	// Its legs walked on by how far it went ahead and by its walkers' steps as they turn (PivotM a whole turn), wrapped
	// to its loop (its last turn's start carried back with it); its grazing clock by the time it stood grazing (none on a
	// step that sets off).
	const double LoopM = FMath::Max(G.LengthCm / 100.0, 1.0);
	const double Pivot = Walker::PivotM * FMath::Abs(Travel1 - Travel0);
	S.DistM += Ahead / 100.0 + Pivot;
	if (S.DistM >= LoopM)
	{
		const double Laps = FMath::FloorToDouble(S.DistM / LoopM);
		S.DistM -= Laps * LoopM;
		S.PivotStartM -= Laps * LoopM;
	}
	if (bStep && Travel0 <= 0.0 && Travel1 <= 0.0)
	{
		S.GrazeSec += Dt;
		if (S.GrazeSec >= ClockPeriodSec)
		{
			S.GrazeSec -= ClockPeriodSec;
			S.GrazeRef -= ClockPeriodSec;
			if (S.PivotSign > 0.0) { S.SinceLastTurn -= ClockPeriodSec; }
		}
	}
	// How fast its frame goes over the ground, and how fast its legs walk on until the next step: ahead, and its
	// walkers' steps while they are still turning.
	S.SpeedMS = bStep ? Moved / Dt / 100.0 : HerdSpeedCmS(G, Now) / 100.0;
	S.VelCmS = bStep ? Way / Dt : FVector::ZeroVector;
	const bool bTurning = Travel1 > 0.0 && Travel1 < 1.0;
	S.GaitMS = bStep ? Ahead / Dt / 100.0 + (bTurning ? Pivot / Dt : 0.0) : (Travel1 >= 1.0 ? S.SpeedMS : 0.0);
	// Its latest turn: begun where its legs' walk less a whole turn's share of how far it has gone (so the share it is on
	// the move runs on from it in step with its legs), the way it went, and the grazing clock it stood at (grazing stops
	// while it turns); each kept within reach of now.
	if (Travel1 != Travel0)
	{
		S.PivotSign = Travel1 > Travel0 ? 1.0 : -1.0;
		S.PivotStartM = S.DistM - Walker::PivotM * (S.PivotSign > 0.0 ? Travel1 : 1.0 - Travel1);
	}
	if (Travel0 > 0.0 || Travel1 > 0.0) { S.GrazeRef = S.GrazeSec; }
	S.PivotStartM = FMath::Max(S.PivotStartM, S.DistM - 1000.0);
	S.GrazeRef = FMath::Max(S.GrazeRef, S.GrazeSec - 900.0);
	if (S.PivotSign > 0.0) { S.SinceLastTurn = FMath::Max(S.SinceLastTurn, S.GrazeSec - 900.0); }
	S.Travel = Travel1;
	S.LastOffset = OffsetCm;
	S.FramePos = Pos;
	S.FrameRot = Rot;
	S.bStarted = true;
	if (OutData)
	{
		OutData[HerdData::PivotRef] = (float)(S.PivotStartM + (S.PivotSign < 0.0 ? HerdData::PivotBack : 0.0));
		// Through a turn to walk and the walk after it, its grazing clock stands in slot 11: slot 2 is the clock the stop
		// before left it at; through a turn back to graze, how far it walked between; after it, its clock through it.
		OutData[HerdData::GrazeRef] = (float)(S.PivotSign > 0.0 || S.Travel > 0.0 ? S.SinceLastTurn : S.GrazeRef);
		OutData[HerdData::WalkKap] = (float)S.WalkKap;
		OutData[HerdData::TurnHeading] = HerdData::PackTurns(S.LastTurnHeading, S.TurnHeading);
		OutData[HerdData::Heading] = (float)S.Heading;
		OutData[HerdData::SpeedMS] = (float)S.GaitMS;
		OutData[HerdData::DistM] = (float)S.DistM;
		OutData[HerdData::GrazeSec] = (float)S.GrazeSec;
		OutData[HerdData::ClockT0] = OnMaterialClock(Now, ClockOffsetSec);
		OutData[HerdData::Travel] = (float)S.Travel;
		for (int32 M = 0; M < HerdData::GroundNodes; ++M) { OutData[HerdData::Ground0 + M] = S.Ground.Node[M]; }
		OutData[HerdData::GroundHalfCm] = (float)S.Ground.HalfCm;
	}
}

namespace
{
	/** The walker's grazing round its circle and its walk with the herd, as the material reckons them (fauna_material.py's
	 *  walker branch): its yaw in its herd's frame, its strides walked, how fast it walks, m/s, its herd's share on the
	 *  move, where on its circle it stands from its instance, frame cm; and the ways its body has gone lately, as its
	 *  feet take them: the turn before the latest (P0), what came between (grazing before a turn to walk, walking before
	 *  one back to graze), the latest turn (P1) and what came after it, each a stride of it so far ahead (metres) and round
	 *  (radians); how many of its strides ago each of them ended (Ago: the latest turn's end (negative while it runs), its
	 *  start, the turn before's end, its start) and its strides walked at each of those moments (At). */
	struct FWalkerPace
	{
		float Yaw = 0.f, Strides = 0.f, SpeedMS = 0.f, Travel = 0.f, Gait = 0.f;
		FVector2f Circle = FVector2f::ZeroVector;
		float Ago[4] = {}, At[4] = {};
		float StepAhead[4] = {}, StepRound[4] = {};
		bool bBetween = false;
	};
	/** A grazing clock's steps round a walker's circle: its stair of whole strides, each step coming in and going
	 *  smoothly. */
	float GrazeStair(float Graze, float Ps, float Of)
	{
		const float U = Graze / Ps + Of;
		const float Q = FMath::Clamp((FracF(U) - 0.15f) / 0.35f, 0.f, 1.f);
		return FMath::FloorToFloat(U) + Q * Q * (3.f - 2.f * Q);
	}
	FWalkerPace WalkerPace(float T, const float* C, const float* Gd)
	{
		using namespace SlonkFauna;
		FWalkerPace W;
		const float Ls = FMath::Max(C[Custom::RateHz], 0.05f);
		const float Rho = C[Custom::PerKind];
		const float ARho = FMath::Clamp(FMath::Abs(Rho), 0.01f, (float)Walker::MaxCircleM);
		const float Sgn = Rho < 0.f ? -1.f : 1.f;
		const float Seed = C[Custom::Seed];
		const float PivotM = (float)Walker::PivotM;
		const float Age = FMath::Fmod(T - Gd[HerdData::ClockT0] + 7201.f, 3600.f) - 1.f;
		const float Gait = FMath::Max(Gd[HerdData::SpeedMS], 0.f);
		const float Dist = Gd[HerdData::DistM] + Gait * Age;
		const float Travel0 = FMath::Clamp(Gd[HerdData::Travel], 0.f, 1.f);
		// The latest turn: which way it went, where it began.
		const float PKind = FMath::FloorToFloat((Gd[HerdData::PivotRef] + 0.5f * (float)HerdData::PivotBack) / (float)HerdData::PivotBack);
		const float PSgn = FMath::Fmod(PKind, 2.f) > 0.5f ? -1.f : 1.f;
		const float PStart = Gd[HerdData::PivotRef] - PKind * (float)HerdData::PivotBack;
		const float Travel = Travel0 > 0.f && Travel0 < 1.f ? FMath::Clamp(Travel0 + PSgn * Gait * Age / PivotM, 0.f, 1.f) : Travel0;
		const float Graze = Gd[HerdData::GrazeSec] + (Travel0 <= 0.f ? Age : 0.f);
		const float K = FMath::Max(FMath::RoundToFloat(6.2831853f * ARho / Ls), 1.f);
		const float Pw = 18.f + 16.f * FracF(Seed * 5.17f);
		const float N = FMath::Max(FMath::RoundToFloat(3600.f / (K * Pw)), 1.f);
		const float Ps = 3600.f / (N * K);
		const float Of = FracF(Seed * 3.31f);
		const float Stair = GrazeStair(Graze, Ps, Of);
		const float T1 = FMath::Clamp((FracF(Graze / Ps + Of) - 0.15f) / 0.35f, 0.f, 1.f);
		auto YawAt = [&](float St) { return 6.2831853f * (C[Custom::Phase] + Sgn * St / K) + Sgn * 1.5707963f; };
		auto Wrap = [](float A) { return A - 6.2831853f * FMath::RoundToFloat(A / 6.2831853f); };
		const float Th = 6.2831853f * (C[Custom::Phase] + Sgn * Stair / K);
		W.Circle = FVector2f(FMath::Cos(Th), FMath::Sin(Th)) * (ARho * 100.f);
		const float YawC = YawAt(Stair);
		// Its ways through the latest turn and the one before, each to the turn's step (HerdData::PackTurns).
		const float Steps = (float)HerdData::TurnSteps;
		const float QLast = FMath::FloorToFloat((Gd[HerdData::TurnHeading] + 0.5f) / Steps);
		const float Way1 = (Gd[HerdData::TurnHeading] - QLast * Steps) * (6.2831853f / Steps), Way0 = QLast * (6.2831853f / Steps);
		// The grazing clock through the latest turn (it grazes no more till it stands grazing again: slot 11 while any of
		// it is on the move, after a turn back to graze slot 2), so where it stood on its circle then; how far it turned.
		const float StairRef = GrazeStair(Travel0 > 0.f ? Gd[HerdData::GrazeSec] : Gd[HerdData::GrazeRef], Ps, Of);
		const float Dyp = Wrap(Way1 - YawAt(StairRef));
		// Turned the way the herd walks by its share on the move; through a turn by the turn as its feet take it (Dyp: its
		// stair then is the one it stands on now), so body and feet go the same way round however near a half turn it is.
		const float Hd = Gd[HerdData::Heading] + (Travel0 >= 1.f ? Gd[HerdData::WalkKap] * Gait * Age : 0.f);
		W.Yaw = YawC + Travel * (Travel0 > 0.f && Travel0 < 1.f ? Dyp : Wrap(Hd - YawC));
		W.Strides = Stair + Dist / Ls;
		W.SpeedMS = Gait + (Travel0 <= 0.f ? Ls * (6.f * T1 * (1.f - T1) / 0.35f) / Ps : 0.f);
		W.Travel = Travel;
		W.Gait = Gait;
		// The turn before, the other way, and what came between: before a turn to walk, the last stop (from the herd's way
		// then back to where it stood grazing at the clock it left it at) and its own steps grazing since; before one back
		// to graze, the last set-off (from its circle's way to the herd's) and the herd's walk since (none once it grazes
		// again: by then no foot is still down from it).
		float Between = 0.f, AtLastEnd = 0.f, Dy0 = 0.f;
		if (PSgn > 0.f)
		{
			const float StairOld = GrazeStair(Gd[HerdData::GrazeRef], Ps, Of);
			Between = StairRef - StairOld;
			AtLastEnd = StairOld + PStart / Ls;
			Dy0 = -Wrap(Way0 - YawAt(StairOld));
		}
		else
		{
			const float WalkedM = Travel0 > 0.f ? Gd[HerdData::GrazeRef] : 1000.f;
			Between = WalkedM / Ls;
			AtLastEnd = StairRef + (PStart - WalkedM) / Ls;
			Dy0 = Wrap(Way0 - YawAt(StairRef));
		}
		W.Ago[0] = (Dist - PStart - PivotM) / Ls + (Stair - StairRef);
		W.Ago[1] = W.Ago[0] + PivotM / Ls;
		W.Ago[2] = W.Ago[1] + Between;
		W.Ago[3] = W.Ago[2] + PivotM / Ls;
		W.At[1] = StairRef + PStart / Ls;
		W.At[0] = W.At[1] + PivotM / Ls;
		W.At[2] = AtLastEnd;
		W.At[3] = AtLastEnd - PivotM / Ls;
		W.bBetween = Between > 1e-4f;
		// The ways: the turn before (stepping round where it stands), what came between, the latest turn, what came after
		// (grazing round its circle, walking with the herd as its way turns).
		const float Graz = Sgn * Ls / ARho, Walk = Gd[HerdData::WalkKap] * Ls;
		W.StepAhead[0] = 0.f; W.StepAhead[1] = Ls; W.StepAhead[2] = 0.f; W.StepAhead[3] = Ls;
		W.StepRound[0] = Dy0 * Ls / PivotM;
		W.StepRound[1] = PSgn > 0.f ? Graz : Walk;
		W.StepRound[2] = PSgn * Dyp * Ls / PivotM;
		W.StepRound[3] = PSgn > 0.f ? Walk : Graz;
		return W;
	}

	/** A point in a walker's body's frame (unit-mesh cm, xy) carried back by N of its strides of a way its body goes,
	 *  each stride Ahead metres ahead and Round radians round: turned back by the turn and moved back by the way (a chord
	 *  of its arc), as a foot planted on the ground is seen from the body going on over it. */
	FVector2f MoveBack(const FVector2f& Q, float N, float Ahead, float Round, float Size)
	{
		const float A = Round * N;
		const float Ca = FMath::Cos(A), Sa = FMath::Sin(A);
		const FVector2f Ch = FMath::Abs(Round) < 1e-4f ? FVector2f(-Ahead * N, 0.5f * Ahead * N * A) : FVector2f(-Sa, 1.f - Ca) * (Ahead / Round);
		return FVector2f(Q.X * Ca + Q.Y * Sa, -Q.X * Sa + Q.Y * Ca) + Ch * (100.f / Size);
	}

	/** Whether a foot of a leg's place in the gait, Lp of its walker's strides since it was put down, was put down before
	 *  the moment Ago strides back (its strides walked then At): known by its phase then (Lp less Ago is that phase, not
	 *  a stride less), however its strides are rounded, so a foot put down just as a turn began is so put down for good. */
	bool DownBefore(float Lp, float Ago, float At, float Leg)
	{
		return FMath::RoundToFloat(Lp - Ago - FracF(At - Leg)) >= 0.f;
	}

	/** Where a foot put down is in its body's frame (unit-mesh cm, xy), Lp of its strides after it was put down and Shift
	 *  strides ago (now for a planted foot; for one in the air, when it lifted): put down half a stance ahead of where it
	 *  stands at its stance's middle (Vertex: its own place) by the way its body was then going, then carried back by each
	 *  way its body has gone since, the vertex turned back by the body's turn and moved back by its way, a chord of its
	 *  arc. */
	FVector2f PlantedAt(const FWalkerPace& W, const FVector2f& Vertex, float Lp, float Shift, float Size, float Leg)
	{
		float B[4];
		for (int32 i = 0; i < 4; ++i) { B[i] = W.Ago[i] - Shift; }
		// How much of each way since it was put down: the turn before, between, the latest turn, after.
		const float Am[4] = { FMath::Max(FMath::Min(Lp, B[3]) - FMath::Max(B[2], 0.f), 0.f), FMath::Max(FMath::Min(Lp, B[2]) - FMath::Max(B[1], 0.f), 0.f),
			FMath::Max(FMath::Min(Lp, B[1]) - FMath::Max(B[0], 0.f), 0.f), FMath::Clamp(B[0], 0.f, Lp) };
		const int32 Td = DownBefore(Lp, B[2], W.At[2], Leg) ? 0 : (DownBefore(Lp, B[1], W.At[1], Leg) ? 1 : (DownBefore(Lp, B[0], W.At[0], Leg) ? 2 : 3));
		FVector2f Q = MoveBack(Vertex, -0.5f * SlonkFauna::Walker::Duty, W.StepAhead[Td], W.StepRound[Td], Size);
		for (int32 i = 0; i < 4; ++i) { Q = MoveBack(Q, Am[i], W.StepAhead[i], W.StepRound[i], Size); }
		return Q;
	}
}

float SlonkFauna::WalkerYaw(float T, const float* Custom, const float* Group) { return WalkerPace(T, Custom, Group).Yaw; }
float SlonkFauna::WalkerStrides(float T, const float* Custom, const float* Group) { return WalkerPace(T, Custom, Group).Strides; }
float SlonkFauna::WalkerSpeedMS(float T, const float* Custom, const float* Group) { return WalkerPace(T, Custom, Group).SpeedMS; }

FVector3f SlonkFauna::WalkerMotion(const FVector3f& LP, const FVector3f& LPP, const FLinearColor& VC, float T, const float* C, const float* Gd, float DistCm, float PxPerRad)
{
	// fauna_material.py's walker branch, line for line.
	const FWalkerPace W = WalkerPace(T, C, Gd);
	const float S = FMath::Max(C[Custom::SizeM], 0.1f);
	const float Seed = C[Custom::Seed];
	// Its head: down in the grass as it grazes, looking up now and then (a whole number of times an hour); carried as it
	// walks, a little higher at a trot; up and alert while it is frightened.
	const float PanicAge = FMath::Fmod(T - Gd[HerdData::PanicT0] + 7201.f, 3600.f) - 1.f;
	const float OnsetAge = FMath::Fmod(T - Gd[HerdData::OnsetT0] + 7201.f, 3600.f) - 1.f;
	const float Live = Gd[HerdData::Startled] > 0.5f ? 1.f : 0.f;
	const float Alarm = Live * FMath::Clamp((1.f - SmoothStepF(20.f, 120.f, PanicAge)) * 3.f, 0.f, 1.f) * SmoothStepF(0.f, 0.3f, OnsetAge);
	const float Nl = 60.f + FMath::FloorToFloat(FracF(Seed * 9.13f) * 90.f);
	const float Lc = FracF(T * (Nl / 3600.f) + Seed * 7.7f);
	const float Look = SmoothStepF(0.f, 0.03f, Lc) * (1.f - SmoothStepF(0.09f, 0.12f, Lc));
	const float GrazeP = FMath::Lerp(Walker::GrazePitch, -0.35f, Look);
	const float WalkP = FMath::Lerp(Walker::WalkPitch, Walker::TrotPitch, SmoothStepF(1.5f, 2.5f, W.Travel >= 1.f ? W.Gait : 0.f));
	float Pitch = FMath::Lerp(GrazeP, WalkP, W.Travel);
	Pitch = FMath::Lerp(Pitch, Walker::AlarmPitch, Alarm);
	FVector3f P = LP;
	const float Va = VC.A;
	if (Va > 0.65f && Va < 0.85f)
	{
		// A leg, sheared from its hip: its foot planted where it was put down for Duty of each stride while the body goes
		// on over it (walking, grazing round its circle, stepping round where it stands as the herd turns to walk or back
		// to graze), then swung forward to where it will next be put down, lifted the more the faster it walks.
		const float Lp = FracF(W.Strides - VC.B);
		const bool bSwing = Lp >= Walker::Duty;
		// The stance it is in, or for a foot in the air the one it lifted from, as it was then.
		const float Sh = bSwing ? Lp - Walker::Duty : 0.f;
		const FVector2f V(LP.X, LP.Y);
		FVector2f Foot = PlantedAt(W, V, Lp - Sh, Sh, S, VC.B) - V;
		float Zf = 0.f;
		if (bSwing)
		{
			// Swung forward to where it will be put down: half a stance ahead of its place, as the way its body will then
			// be going moves it (through the turn or after it, as the turn's end falls before or after); and, the turn
			// having begun while it was in the air, from where it was to be put down before, its aim moving over as it
			// comes down.
			const float Q = Sh / (1.f - Walker::Duty);
			const float Sq = Q * Q * (3.f - 2.f * Q);
			const float AtStart = FracF(W.At[1] - VC.B);
			// Put down next (1 less Lp strides on) before the latest turn's end, or after it.
			const int32 New = DownBefore(Lp - 1.f, W.Ago[0], W.At[0], VC.B) ? 2 : 3;
			const bool bLiftedFirst = DownBefore(Lp, W.Ago[1], W.At[1], VC.B) && AtStart >= Walker::Duty;
			const int32 Old = bLiftedFirst ? (W.bBetween ? 1 : 0) : New;
			const FVector2f AimOld = MoveBack(V, -0.5f * Walker::Duty, W.StepAhead[Old], W.StepRound[Old], S) - V;
			const FVector2f AimNew = MoveBack(V, -0.5f * Walker::Duty, W.StepAhead[New], W.StepRound[New], S) - V;
			const float Qb = FMath::Clamp((AtStart - Walker::Duty) / (1.f - Walker::Duty), 0.f, 1.f);
			const float H = bLiftedFirst ? FMath::Clamp((Q - Qb) / FMath::Max(1.f - Qb, 1e-3f), 0.f, 1.f) : 1.f;
			Foot = FMath::Lerp(Foot, FMath::Lerp(AimOld, AimNew, H * H * (3.f - 2.f * H)), Sq);
			Zf = Walker::LiftCm * FMath::Sin(3.14159265f * Q) * SmoothStepF(0.03f, 0.25f, W.SpeedMS);
		}
		P.X += Foot.X * VC.G;
		P.Y += Foot.Y * VC.G;
		P.Z += Zf * VC.G;
	}
	else if (Va > 0.3f && Va < 0.45f)
	{
		// The neck and head, turned about the neck's root, the root itself held to the body.
		const float A = Pitch * SmoothStepF(0.f, 0.3f, VC.G);
		const float Rx = P.X - Walker::NeckRootX, Rz = P.Z - Walker::NeckRootZ;
		const float Ca = FMath::Cos(A), Sa = FMath::Sin(A);
		P.X = Walker::NeckRootX + Rx * Ca - Rz * Sa;
		P.Z = Walker::NeckRootZ + Rx * Sa + Rz * Ca;
	}
	else if (Va > 0.15f && Va < 0.35f)
	{
		// The tail swishes, a whole number of times an hour.
		const float Nt = 400.f + FMath::FloorToFloat(FracF(Seed * 2.71f) * 500.f);
		P.Y += VC.G * 10.f * FMath::Sin(6.2831853f * FracF(T * (Nt / 3600.f) + Seed * 1.9f));
	}
	// The speck floor: no shorter than SpeckPx far off, at most SpeckMax times its size, grown from its feet.
	const float Px = S * 100.f * PxPerRad / FMath::Max(DistCm, 1.f);
	const float Kf = FMath::Clamp(Walker::SpeckPx / FMath::Max(Px, 0.001f), 1.f, Walker::SpeckMax);
	P *= Kf;
	// Turned the way it faces, and set where it stands on its grazing circle.
	const float Cy = FMath::Cos(W.Yaw), Sy = FMath::Sin(W.Yaw);
	P = FVector3f(P.X * Cy - P.Y * Sy, P.X * Sy + P.Y * Cy, P.Z);
	P.X += W.Circle.X / S;
	P.Y += W.Circle.Y / S;
	// On the drawn ground under each vertex, over its herd's plane.
	const float HomeX = LPP.X - S * LP.X, HomeY = LPP.Y - S * LP.Y;
	P.Z += HerdGroundAt(Gd, HomeX + S * P.X, HomeY + S * P.Y) / S;
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
