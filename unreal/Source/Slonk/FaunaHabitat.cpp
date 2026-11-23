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
	// The loop's own middle, lifted onto the sphere the same way.
	const double Lu = (Fu + CtrU) * Cell + (SeaPt[Iu] - Pn[Iu]), Lv = (Fv + CtrV) * Cell + (SeaPt[Iv] - Pn[Iv]);
	const double LRest = RadiusCm * RadiusCm - Lu * Lu - Lv * Lv;
	Out.LoopCentreCm[Iu] = Lu;
	Out.LoopCentreCm[Iv] = Lv;
	Out.LoopCentreCm[Ax] = (U[Ax] < 0.0 ? -1.0 : 1.0) * FMath::Sqrt(FMath::Max(LRest, 0.0));
	Out.LoopRadiusCm = LoopR * Cell;
	return true;
}

int32 SlonkFauna::ProjectionAxis(const FVector& Dir)
{
	const FVector A = Dir.GetAbs();
	if (A.X >= A.Y && A.X >= A.Z) { return 0; }
	return A.Y >= A.Z ? 1 : 2;
}

FVector SlonkFauna::SwellAt(const FVector& PlanetLocalCm, double TimeSec, double SwellCm)
{
	// terrain_material.py's SlonkGerstner, wave for wave, on the projection's plane at the point (its two components there,
	// which whole wave numbers across the noise period leave the same however many periods are taken off them).
	const int32 Ax = ProjectionAxis(PlanetLocalCm);
	const int32 Iu = Ax == 0 ? 1 : 0, Iv = Ax == 2 ? 1 : 2;
	const FVector Pn = SlonkScatter::ToNoiseSpace(PlanetLocalCm);
	const double Pu = Pn[Iu], Pv = Pn[Iv];
	static const double Kx[4] = { 100.0, 180.0, 0.0, -300.0 }, Ky[4] = { 70.0, -60.0, 256.0, 400.0 };
	static const double Amp[4] = { 0.55, 0.3, 0.25, 0.12 };
	constexpr double Q = 0.55;
	FVector D = FVector::ZeroVector;
	for (int32 i = 0; i < 4; ++i)
	{
		const double Ku = UE_DOUBLE_TWO_PI * Kx[i] / SlonkScatter::NoisePeriodCm, Kv = UE_DOUBLE_TWO_PI * Ky[i] / SlonkScatter::NoisePeriodCm;
		const double Kl = FMath::Sqrt(Ku * Ku + Kv * Kv);
		const double W = FMath::Sqrt(981.0 * Kl);
		const double Ph = Ku * Pu + Kv * Pv - W * TimeSec;
		const double A = Amp[i] * SwellCm;
		const double Sn = FMath::Sin(Ph), Cs = FMath::Cos(Ph);
		D.X += Ku / Kl * (Q * A * Cs);
		D.Y += Kv / Kl * (Q * A * Cs);
		D.Z += A * Sn;
	}
	return D;
}

double SlonkFauna::SwellFade(double DistFromCameraCm)
{
	return 1.0 - SlonkScatter::SmoothStep(80000.0, 250000.0, DistFromCameraCm);
}

double SlonkFauna::SeaSurfaceCm(const FVector& PlanetLocalCm, double RadiusCm, double TimeSec, double SwellCm, double Fade)
{
	if (Fade <= 0.0 || SwellCm <= 0.0) { return 0.0; }
	const FVector U = PlanetLocalCm.GetSafeNormal();
	const FVector S = U * RadiusCm;
	// The surface over the point came from a vertex the swell sways across the plane onto it: one step back along that
	// sway finds it near enough (the sway's own change over the step, a few centimetres at most, is left).
	const int32 Ax = ProjectionAxis(U);
	const int32 Iu = Ax == 0 ? 1 : 0, Iv = Ax == 2 ? 1 : 2;
	const FVector D0 = SwellAt(S, TimeSec, SwellCm) * Fade;
	FVector Back = S;
	Back[Iu] -= D0.X;
	Back[Iv] -= D0.Y;
	// Its rise there: the swell's lift up its radial, and its sway's, which runs along its plane's fixed axes rather than
	// along the sphere, so it lifts or lowers the vertex as well as carrying it across wherever the radial leans off the
	// plane's (the more toward the projection's seams).
	const FVector B = Back.GetSafeNormal();
	const int32 Bx = ProjectionAxis(B);
	const int32 Bu = Bx == 0 ? 1 : 0, Bv = Bx == 2 ? 1 : 2;
	const FVector Db = SwellAt(B * RadiusCm, TimeSec, SwellCm);
	return (Db.Z + B[Bu] * Db.X + B[Bv] * Db.Y) * Fade;
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
		if (S.bWater) { return S.WaterCm; }
		// Over a strange world's crystal or spires, their tops: they stand tens of metres over the ground read here.
		return P.Geology == EGeology::None ? S.HeightCm : S.HeightCm + SlonkTerrain::GeologyReliefCm(P, S.Geo);
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

	/** The highest of the ground along a loop, cm over the sea-level sphere: its samples' highest (the curve through them
	 *  rises a few metres past them at the most, a jelly's ground rising and falling gently along its loop). */
	double LoopGroundTop(const FFaunaGroup& G)
	{
		double Top = G.Ground.Num() > 0 ? (double)G.Ground[0] : 0.0;
		for (const float H : G.Ground) { Top = FMath::Max(Top, (double)H); }
		return Top;
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

	/** A leaper cell's plane of the projection, its two axes and its side of the planet, from its face. */
	void LeaperPlane(int32 Face, int32& OutAx, int32& OutIu, int32& OutIv, double& OutSide)
	{
		OutAx = FMath::Clamp(Face / 2, 0, 2);
		OutIu = OutAx == 0 ? 1 : 0;
		OutIv = OutAx == 2 ? 1 : 2;
		OutSide = (Face & 1) != 0 ? -1.0 : 1.0;
	}
	/** The side of a cell of the shoals' lattice, cm. */
	constexpr double ShoalCellCm = SlonkScatter::NoisePeriodCm / SlonkFauna::ShoalCellsPerPeriod;
	/** A point of a leaper cell's plane (cm across its two axes) lifted onto the sea-level sphere on its side: false where
	 *  the plane is past the sphere's edge there. */
	bool LeaperPlanePoint(const SlonkFauna::FFaunaCell& C, double RadiusCm, double Au, double Av, FVector& Out)
	{
		int32 Ax, Iu, Iv;
		double Side;
		LeaperPlane(C.Face, Ax, Iu, Iv, Side);
		const double Rest = RadiusCm * RadiusCm - Au * Au - Av * Av;
		if (Rest <= 0.0) { return false; }
		Out[Iu] = Au;
		Out[Iv] = Av;
		Out[Ax] = Side * FMath::Sqrt(Rest);
		return true;
	}

	float SmoothStepF(float A, float B, float X)
	{
		const float T = FMath::Clamp((X - A) / (B - A), 0.f, 1.f);
		return T * T * (3.f - 2.f * T);
	}
	float FracF(float X) { return X - FMath::FloorToFloat(X); }
	FVector3f NormalizeF(const FVector3f& V) { const float L = FMath::Sqrt(V.X * V.X + V.Y * V.Y + V.Z * V.Z); return L > 0.f ? V / L : V; }

	/** A group's glow at night: none unless its plan carries glow organs (no Earth plan does), else GlowOrgan::GlowLo to
	 *  GlowHi by its seed. */
	float GlowOf(const FFaunaGroup& G)
	{
		if (G.Plan.GlowOrgans == 0) { return 0.f; }
		const float U = (float)((G.Seed >> 5) & 0xffffu) / 65535.f;
		return FMath::Lerp(SlonkFauna::GlowOrgan::GlowLo, SlonkFauna::GlowOrgan::GlowHi, U);
	}
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
		&& Palette.Ice == O.Palette.Ice && Life == O.Life && Density == O.Density && KindMask == O.KindMask && bTowns == O.bTowns && (!bTowns || SamePeople(People, O.People))
		&& Sea == O.Sea;
}

bool SlonkFauna::FFaunaGroup::operator==(const FFaunaGroup& O) const
{
	return Id == O.Id && Kind == O.Kind && Habitat == O.Habitat && Seed == O.Seed && Species == O.Species && Plan == O.Plan && Centre == O.Centre && East == O.East && North == O.North
		&& RadiusCm == O.RadiusCm && Lobe == O.Lobe && LobePhase == O.LobePhase && MeanderCm == O.MeanderCm && Lobes == O.Lobes && MeanderK == O.MeanderK
		&& BobK == O.BobK && PeriodSec == O.PeriodSec && Phase0 == O.Phase0 && Turn == O.Turn && CruiseCm == O.CruiseCm && BobCm == O.BobCm
		&& LengthCm == O.LengthCm && SpeedCmS == O.SpeedCmS && Ground == O.Ground && Members == O.Members && SpanM == O.SpanM && SpacingM == O.SpacingM
		&& FlapHz == O.FlapHz && Pigment == O.Pigment && Glow == O.Glow
		&& CycleSec == O.CycleSec && WalkCmS == O.WalkCmS && TrotCmS == O.TrotCmS && WalkA == O.WalkA && WalkB == O.WalkB && TrotA == O.TrotA && TrotB == O.TrotB
		&& RampSec == O.RampSec && StartCm == O.StartCm && ArcCm == O.ArcCm && FormationCm == O.FormationCm && HerdRadiusCm == O.HerdRadiusCm && GroundHalfCm == O.GroundHalfCm
		&& DryCm == O.DryCm && ShoalDensity == O.ShoalDensity && ShoalHalfLengthCm == O.ShoalHalfLengthCm && ShoalHalfWidthCm == O.ShoalHalfWidthCm && ShoalDepthCm == O.ShoalDepthCm
		&& WaterCm == O.WaterCm && Ribs == O.Ribs && HangCm == O.HangCm;
}

int64 SlonkFauna::CellsAcross(const FPlanetTerrainParams& P, EFaunaKind K)
{
	return FMath::Max<int64>(1, FMath::RoundToInt64(SlonkScatter::FaceEdgeCm(P.RadiusCm) / FMath::Max(KindDesc(K).CellCm, 1.0)));
}

SlonkFauna::FFaunaCell SlonkFauna::CellOf(const FPlanetTerrainParams& P, EFaunaKind K, const FVector& Dir)
{
	FFaunaCell C;
	C.Kind = K;
	if (K == EFaunaKind::Leaper)
	{
		// The shoals' own lattice on the projection's plane at the direction (as ShoalAt lays it, its cells unwrapped), the
		// direction first brought onto the sea-level sphere.
		const FVector Up = Dir.GetSafeNormal();
		const int32 Ax = ProjectionAxis(Up);
		const int32 Iu = Ax == 0 ? 1 : 0, Iv = Ax == 2 ? 1 : 2;
		C.Face = Ax * 2 + (Up[Ax] < 0.0 ? 1 : 0);
		C.X = (int64)FMath::FloorToDouble(Up[Iu] * P.RadiusCm / ShoalCellCm);
		C.Y = (int64)FMath::FloorToDouble(Up[Iv] * P.RadiusCm / ShoalCellCm);
		return C;
	}
	double U = 0.0, V = 0.0;
	SlonkTerrain::FaceUV(Dir.GetSafeNormal(), C.Face, U, V);
	const int64 N = CellsAcross(P, K);
	C.X = FMath::Clamp<int64>((int64)FMath::FloorToDouble((U + 1.0) * 0.5 * (double)N), 0, N - 1);
	C.Y = FMath::Clamp<int64>((int64)FMath::FloorToDouble((V + 1.0) * 0.5 * (double)N), 0, N - 1);
	return C;
}

bool SlonkFauna::CellMiddle(const FPlanetTerrainParams& P, const FFaunaCell& C, FVector& OutDir)
{
	if (C.Kind == EFaunaKind::Leaper)
	{
		FVector Pt;
		if (!LeaperPlanePoint(C, P.RadiusCm, ((double)C.X + 0.5) * ShoalCellCm, ((double)C.Y + 0.5) * ShoalCellCm, Pt)) { return false; }
		OutDir = Pt.GetSafeNormal();
		return true;
	}
	const double Wd = 2.0 / (double)CellsAcross(P, C.Kind);
	OutDir = SlonkTerrain::CubeDir(C.Face, -1.0 + Wd * (C.X + 0.5), -1.0 + Wd * (C.Y + 0.5)).GetSafeNormal();
	return true;
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
			// Nearest first, by the cell's own middle (a leaper's cell's, on its plane of the projection: one whose middle is off
			// the sphere is the corner of a plane, which a point met on it never lies in).
			FVector Mid;
			if (K == EFaunaKind::Leaper)
			{
				if (!CellMiddle(P, C, Mid)) { continue; }
			}
			else
			{
				const double Wd = 2.0 / (double)Across;
				Mid = SlonkTerrain::CubeDir(C.Face, -1.0 + Wd * (C.X + 0.5), -1.0 + Wd * (C.Y + 0.5));
			}
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
	// Nothing grazes a strange world's own geology: crystal, a pan, pools, spires or glass.
	if (H.bWater || H.Ice > 0.2 || H.Alpine > 0.7 || H.Geo > 0.25) { return false; }
	// The ground's wetness has no upper bound here: lush grass reads wet to the top of its range (water and rivers are their
	// own), and only the driest land, desert, is too poor for a herd.
	if (bStrict) { return H.Flat > 0.85 && H.Wet >= 0.2 && H.Forest < 0.3; }
	return H.Flat > 0.7 && H.Wet >= 0.14 && H.Forest < 0.45;
}

bool SlonkFauna::HerdCrosses(const SlonkScatter::FHabitat& H)
{
	return !H.bWater && H.Ice <= 0.2 && H.Alpine <= 0.7 && H.Cliff <= 0.3 && H.Flat > 0.45 && H.Forest < 0.6 && H.Geo <= 0.25;
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
	/** A herd's longest member, head and all, m (its footprint's whole reach, for a walker whose legs or antennae reach
	 *  past its length: Walker::FShape::Reach): its walkers are at most a tenth longer than its span. */
	double HerdBodyM(const FFaunaGroup& G)
	{
		return 1.1 * 1.15 * (double)G.SpanM * SlonkFauna::Walker::ShapeOf(G.Plan).Reach;
	}
	/** A herd's shortest walkers' stride, m: their share of the shortest length its span gives a walker (HerdMembersOf),
	 *  to within the rounding that fits a whole number of them round its loop. */
	double HerdShortStrideM(const FFaunaGroup& G)
	{
		const SlonkFauna::Walker::FShape S = SlonkFauna::Walker::ShapeOf(G.Plan);
		return S.StrideShare * FMath::Clamp(0.9 * (double)G.SpanM, S.MinSizeM, S.MaxSizeM);
	}
	/** How far apart a herd's walkers' grazing circles' middles are kept, cm: its spacing, but never less than the longest
	 *  of them and the two smallest circles they amble round (three of the longest one's strides round) between them. */
	double HerdKeepCm(const FFaunaGroup& G)
	{
		const double SmallCircleM = 3.0 * SlonkFauna::Walker::ShapeOf(G.Plan).StrideShare * 1.1 * (double)G.SpanM / UE_DOUBLE_TWO_PI;
		return 100.0 * FMath::Max(0.8 * (double)G.SpacingM, HerdBodyM(G) + 2.0 * SmallCircleM);
	}

	/** A herd's loop at the k-th of HerdLoopReads points round it (theta evenly from 0), Off across it (along its normal
	 *  there in its plane, outward positive): a planet-local unit direction. */
	FVector HerdLoopReadDir(const FFaunaGroup& G, double RadiusCm, int32 K, double Off = 0.0)
	{
		const double Th = UE_DOUBLE_TWO_PI * K / SlonkFauna::HerdLoopReads;
		const FVector Pt = LoopPoint(G, Th);
		if (Off == 0.0) { return (G.Centre * RadiusCm + Pt).GetSafeNormal(); }
		const FVector Across = FVector::CrossProduct(LoopPoint(G, Th + 1.0e-3) - LoopPoint(G, Th - 1.0e-3), G.Centre).GetSafeNormal();
		return (G.Centre * RadiusCm + Pt + Across * Off).GetSafeNormal();
	}

	/** A herd's habitat read along its loop: its middle and HerdLoopReads points round the loop, each as a chunk reads it
	 *  there, and across its walkers' footprint at each of them (HerdFootReads either side, out to its radius); the towns
	 *  near it and the roads between them (at every fifth point); and the water across its loop at every fifth point, half
	 *  its margin apart out to HerdDryReadCm either side (the field's structure alone, which is where the water is), each
	 *  side read out no further than the nearest water found yet. Every fifth point is read first, so ground it will not do
	 *  shows soonest. */
	SlonkFauna::FHerdHabitatRead ReadHerdLoop(const SlonkFauna::FFaunaWorld& W, const FFaunaGroup& G)
	{
		using namespace SlonkFauna;
		FHerdHabitatRead R;
		const double Rad = W.P.RadiusCm;
		double WetSum = 0.0;
		int32 Grass = 0;
		bool bCrossed[HerdLoopReads] = {};
		// Whether it sets foot on a spot at all: on a locked world only where the air is neither baked nor frozen.
		auto SetsFoot = [&](const FVector& Dir, const SlonkScatter::FHabitat& H)
		{
			if (!HerdCrosses(H)) { return false; }
			if (W.P.Climate != EClimateModel::Locked) { return true; }
			const double Heat = SlonkTerrain::TemperatureN(W.P, Dir, FMath::Max(H.HeightCm, 0.0));
			return Heat >= 0.06 && Heat <= 0.6;
		};
		// On its loop's line: whether it sets foot there, and if so whether on its grass (as it likes it best or not) or on
		// ground it crosses.
		auto Read = [&](const FVector& Dir, int32 K)
		{
			const SlonkScatter::FHabitat H = SlonkScatter::HabitatAt(W.P, Dir);
			++R.Points;
			if (!SetsFoot(Dir, H)) { ++R.Off; return; }
			R.MinFlat = FMath::Min(R.MinFlat, H.Flat);
			R.MaxForest = FMath::Max(R.MaxForest, H.Forest);
			if (!HerdGrass(H, false))
			{
				++R.Crossed;
				++R.Rough;
				if (K >= 0) { bCrossed[K] = true; }
				return;
			}
			if (!HerdGrass(H, true)) { ++R.Rough; }
			R.MinWet = FMath::Min(R.MinWet, H.Wet);
			R.MaxWet = FMath::Max(R.MaxWet, H.Wet);
			WetSum += H.Wet;
			++Grass;
		};
		Read(G.Centre, -1);
		for (int32 Pass = 0; Pass < 5 && R.Off == 0 && R.Crossed <= HerdCrossMax; ++Pass)
		{
			for (int32 k = Pass; k < HerdLoopReads && R.Off == 0 && R.Crossed <= HerdCrossMax; k += 5) { Read(HerdLoopReadDir(G, Rad, k), k); }
		}
		if (R.Off > 0 || R.Crossed > HerdCrossMax) { return R; }
		// Its longest stretch of crossed ground in a row, round the loop.
		for (int32 k = 0, Run = 0; k < 2 * HerdLoopReads; ++k)
		{
			Run = bCrossed[k % HerdLoopReads] ? Run + 1 : 0;
			R.LongestCrossed = FMath::Max(R.LongestCrossed, FMath::Min(Run, HerdLoopReads));
		}
		R.MeanWet = WetSum / FMath::Max(Grass, 1);
		if (R.LongestCrossed > HerdCrossRun || R.Rough > HerdRoughMax) { return R; }
		// Off every town's ground (a cliff town's runs along its wall either way, 300 m across it) by its own radius and
		// HerdTownMarginCm at every point of its loop, and off the roads between them by its radius and HerdRoadMarginCm.
		if (W.bTowns)
		{
			const FCivilization Civ = W.People.bInhabited ? W.People : SlonkCivs::Colonists();
			TArray<FTownSite> Sites;
			SlonkCities::EnumerateTowns(W.P, Civ, G.Centre * Rad, G.ExtentCm() + 4.0e5, Sites, 64);
			for (int32 k = 0; k < HerdLoopReads && !R.bTown && !R.bRoad; k += 5)
			{
				const FVector Dir = HerdLoopReadDir(G, Rad, k);
				for (const FTownSite& T : Sites)
				{
					const double Reach = (T.bCliff ? FMath::Sqrt(FMath::Square(T.RadiusCm) + FMath::Square(30000.0)) : T.RadiusCm) + G.HerdRadiusCm + HerdTownMarginCm;
					R.bTown |= FVector::DistSquared(T.LocalCm, Dir * Rad) < Reach * Reach;
				}
				R.bRoad = !R.bTown && SlonkCities::PaintedWayCm(W.P, Civ, Dir) < G.HerdRadiusCm + HerdRoadMarginCm;
			}
			if (R.bTown || R.bRoad) { return R; }
		}
		// Its walkers' footprint: across its loop at every point, out to its radius either side (the outermost first, where
		// ground it will not stand on comes in), nowhere a foot could fall that it never sets one.
		for (int32 Pass = 0; Pass < 5 && R.Off == 0; ++Pass)
		{
			for (int32 k = Pass; k < HerdLoopReads && R.Off == 0; k += 5)
			{
				for (int32 j = HerdFootReads; j >= 1 && R.Off == 0; --j)
				{
					for (const double Side : { 1.0, -1.0 })
					{
						const FVector Dir = HerdLoopReadDir(G, Rad, k, Side * G.HerdRadiusCm * j / HerdFootReads);
						const SlonkScatter::FHabitat H = SlonkScatter::HabitatAt(W.P, Dir);
						++R.Foot;
						if (!SetsFoot(Dir, H)) { ++R.Off; break; }
						R.MinFlat = FMath::Min(R.MinFlat, H.Flat);
						R.MaxForest = FMath::Max(R.MaxForest, H.Forest);
					}
				}
			}
		}
		if (R.Off > 0) { return R; }
		// The water across its loop, out either side of every fifth point (along the loop's normal there): none within its
		// margin, and how far the nearest is.
		R.DryCm = HerdDryReadCm;
		constexpr double Step = 0.5 * HerdWaterMarginCm;
		for (int32 k = 0; k < HerdLoopReads && R.WaterNear == 0; k += 5)
		{
			for (const double Side : { 1.0, -1.0 })
			{
				for (double Off = Step; Off < R.DryCm + 1.0; Off += Step)
				{
					if (SlonkTerrain::SampleStructure(W.P, HerdLoopReadDir(G, Rad, k, Side * Off), 1000.0).bWater)
					{
						// The last read that was dry: the shore lies somewhere past it, up to a step on.
						R.DryCm = FMath::Min(R.DryCm, Off - Step);
						R.WaterNear += Off <= HerdWaterMarginCm + 1.0 ? 1 : 0;
						break;
					}
				}
			}
		}
		R.bSuits = R.WaterNear == 0;
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
		// At most HerdCellShare of the cells hold a herd, on a world full of life at the density asked for.
		if (Gate >= FMath::Min(HerdCellShare * (double)W.Life * (double)W.Density, 1.0)) { return false; }
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
		// Its species: the cell's own (a number of its own, apart from these draws), its plan and its size against an Earth
		// grazer's; its walkers' shape from the plan.
		G.Species = SpeciesInCell(W, C);
		const FFaunaSpecies& Sp = W.Profile.Species[(int32)EFaunaKind::Herd][G.Species];
		G.Plan = Sp.Plan;
		const Walker::FShape Shape = Walker::ShapeOf(G.Plan);
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
		// Its members: mostly the smaller herds, from 15 to 60; a grazer's length, its spacing as cattle graze (each the
		// species' size against a grazer's: a six-legged grazer's larger, an insectoid's from a little smaller to half again).
		const double Un = Dr.Next();
		G.Members = FMath::Clamp(15 + (int32)(46.0 * Un * Un), FMath::Max(D.GroupMin, 1), D.GroupMax);
		G.SpanM = FMath::Lerp(1.6f, 2.3f, (float)Dr.Next());
		G.SpacingM = FMath::Lerp(4.5f, 7.f, (float)Dr.Next());
		if (Sp.SizeScale != 1.f)
		{
			G.SpanM = FMath::Clamp(G.SpanM * Sp.SizeScale, (float)Shape.MinSizeM, (float)(Shape.MaxSizeM / 1.1));
			G.SpacingM *= Sp.SizeScale;
		}
		// Its formation wide enough for its circles' middles to lie that far apart (they fill under half of it).
		G.FormationCm = FMath::Sqrt((double)G.Members) * FMath::Max(100.0 * G.SpacingM * 0.55, 0.75 * HerdKeepCm(G));
		G.HerdRadiusCm = G.FormationCm + 100.0 * Walker::MaxCircleM + 0.6 * 110.0 * G.SpanM * Shape.Reach;
		G.GroundHalfCm = G.HerdRadiusCm + 100.0;
		G.CruiseCm = 0.0;
		G.FlapHz = 0.f;
		const double Tone = Dr.Next(), Vary = 0.85 + 0.3 * Dr.Next();
		// Its grass along its loop (crossing a little rougher ground), clear of the water, off the towns and their roads; how
		// much room the water leaves the crew to walk it off its path.
		const FHerdHabitatRead H = ReadHerdLoop(W, G);
		if (!H.bSuits) { return false; }
		G.DryCm = H.DryCm;
		// The ground along its loop, at full detail (the game stands it on the drawn ground).
		G.Ground.SetNum(NumGround);
		for (int32 k = 0; k < NumGround; ++k)
		{
			G.Ground[k] = (float)FMath::Max(SlonkTerrain::SurfaceHeightCm(W.P, (Centre * W.P.RadiusCm + LoopPoint(G, UE_DOUBLE_TWO_PI * k / NumGround)).GetSafeNormal()), 0.0);
		}
		// A coat, or an insectoid's carapace, from the world's look.
		const double Dry = FMath::Clamp(1.0 - H.MeanWet * 1.3, 0.0, 1.0);
		G.Pigment = Shape.Rig.Carapace > 0.5f ? CarapacePigment(W.Profile.Look, W.Palette, Dry, Tone, Vary) : HerdPigment(W.Profile.Look, W.Palette, Dry, Tone, Vary);
		G.Glow = GlowOf(G);
		Out = MoveTemp(G);
		return true;
	}
}

int32 SlonkFauna::SpeciesInCell(const FFaunaWorld& W, const FFaunaCell& C)
{
	const FFaunaKindSpecies& S = W.Profile.Species[FMath::Clamp((int32)C.Kind, 0, NumKinds - 1)];
	if (S.Num <= 1) { return 0; }
	// The cell's own number, salted apart from its group's draws (40 and the kind's).
	SlonkScatter::FDraw Dr(SlonkScatter::CellHash(C.Face, 70 + (int32)C.Kind, C.X, C.Y, W.Salt()));
	return S.Pick(Dr.Next());
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

double SlonkFauna::LeaperReachCm(const FShoal& S, double LongestM)
{
	// Its fish's homes within 0.55 of its painted shape; each leap out from its home up to Jitter of the longest fish's
	// lengths either way and RangeLo + RangeSpan on, and half of it past that.
	const double Longest = 100.0 * LongestM;
	return 0.55 * FMath::Max(S.HalfLengthCm, S.HalfWidthCm) + Longest * (UE_DOUBLE_SQRT_2 * Leaper::Jitter + Leaper::RangeLo + Leaper::RangeSpan + 0.5);
}

double SlonkFauna::LeaperMaxSizeM(const FFaunaBodyPlan& Plan)
{
	switch (FishForm::FormOf(Plan))
	{
	case FishForm::Eel: return FishForm::EelMaxSizeM;
	case FishForm::Spinefin: return Leaper::MaxSizeM * 10.0 / 9.0;
	default: return Leaper::MaxSizeM;
	}
}

float SlonkFauna::LeaperSpanM(const FFaunaBodyPlan& Plan, float SizeScale, float Drawn)
{
	// An eel's lengths its own, the cell's same draw laid over them (a metre to 1.5 m, its species a tenth either way, its
	// fish on to 1.8 m); any other strange species of its own size.
	const double Longest = LeaperMaxSizeM(Plan);
	if (FishForm::FormOf(Plan) == FishForm::Eel) { return FMath::Clamp(FMath::Lerp(FishForm::EelSpanLoM, FishForm::EelSpanHiM, (Drawn - 0.35f) / 0.45f) * SizeScale, FishForm::EelSpanLoM, (float)(Longest / 1.1)); }
	if (SizeScale != 1.f) { return FMath::Clamp(Drawn * SizeScale, (float)Leaper::MinSizeM, (float)(Longest / 1.1)); }
	return Drawn;
}

double SlonkFauna::SplashScaleOf(const FFaunaBodyPlan& Plan)
{
	switch (FishForm::FormOf(Plan))
	{
	case FishForm::Eel: return 0.6;
	case FishForm::Spinefin: return 1.35;
	default: return 1.0;
	}
}

SlonkFauna::FLeaperHabitatRead SlonkFauna::LeaperHabitatAt(const FFaunaWorld& W, const FFaunaCell& C, double LongestM)
{
	FLeaperHabitatRead R;
	if (C.Kind != EFaunaKind::Leaper) { return R; }
	int32 Ax, Iu, Iv;
	double Side;
	LeaperPlane(C.Face, Ax, Iu, Iv, Side);
	const double Rad = W.P.RadiusCm;
	// The shoal of the cell, found at the cell's middle as the material finds it there: this cell's, on this plane.
	FVector Mid;
	if (!LeaperPlanePoint(C, Rad, ((double)C.X + 0.5) * ShoalCellCm, ((double)C.Y + 0.5) * ShoalCellCm, Mid)) { return R; }
	FShoal S;
	auto Wrap = [](int64 I) { const int64 Wr = I % ShoalCellsPerPeriod; return (int32)(Wr < 0 ? Wr + ShoalCellsPerPeriod : Wr); };
	if (!ShoalAt(Mid, Rad, W.Sea.ShoalDensity, 0.0, S) || S.Axis != Ax || S.Key != FIntPoint(Wrap(C.X), Wrap(C.Y)) || (S.LoopCentreCm[Ax] < 0.0) != (Side < 0.0)) { return R; }
	R.bShoal = true;
	R.Shoal = S;
	// Wholly on its plane: its loop and its fish's reach past it, all round, nowhere another plane's (the material lays
	// both its swell and its shoals by the plane at each pixel).
	const double Lu = S.LoopCentreCm[Iu], Lv = S.LoopCentreCm[Iv];
	const double Out = S.LoopRadiusCm + LeaperReachCm(S, LongestM);
	R.bOnePlane = true;
	for (int32 k = 0; k < 8 && R.bOnePlane; ++k)
	{
		const double A = UE_DOUBLE_TWO_PI * k / 8.0;
		FVector Pt;
		R.bOnePlane = LeaperPlanePoint(C, Rad, Lu + Out * 1.2 * FMath::Cos(A), Lv + Out * 1.2 * FMath::Sin(A), Pt) && ProjectionAxis(Pt) == Ax && (Pt[Ax] < 0.0) == (Side < 0.0);
	}
	if (!R.bOnePlane) { return R; }
	// Among the share of the shoals that hold leapers, on a world full of life at the density asked for.
	SlonkScatter::FDraw Dr(SlonkScatter::CellHash(C.Face, 40 + (int32)C.Kind, C.X, C.Y, W.Salt()));
	R.bGate = Dr.Next() < FMath::Min(Leaper::ShoalShare * (double)W.Life * (double)W.Density, 1.0);
	if (!R.bGate) { return R; }
	// (Read from the loop's middle out, and no further once a point will not do.)
	// The open sea under its leaps, deep enough, no ice: its loop's middle and eight points round it, out to the loop and
	// its reach (the field's water at ten metres, as the chunks lay it).
	R.NeedDepthCm = FMath::Max(Leaper::MinWaterCm, S.DepthCm + Leaper::ShoalClearCm);
	R.MinDepthCm = TNumericLimits<double>::Max();
	for (int32 k = -1; k < 8 && R.NotSea == 0 && R.Shallow == 0; ++k)
	{
		FVector Pt;
		const double A = UE_DOUBLE_TWO_PI * k / 8.0;
		if (k >= 0 ? !LeaperPlanePoint(C, Rad, Lu + Out * FMath::Cos(A), Lv + Out * FMath::Sin(A), Pt) : !LeaperPlanePoint(C, Rad, Lu, Lv, Pt)) { ++R.NotSea; continue; }
		const FVector Dir = Pt.GetSafeNormal();
		const FPlanetTerrainSample T = SlonkTerrain::Sample(W.P, Dir, 1000.0);
		++R.Points;
		bool bSea = T.bWater && FMath::Abs(T.WaterCm) < 1.0 && T.River < 0.05f && T.Ice < 0.3f;
		if (bSea && W.P.Climate == EClimateModel::Locked)
		{
			// A locked world's sea only where the air is neither baked nor frozen.
			const double Heat = SlonkTerrain::TemperatureN(W.P, Dir, 0.0);
			bSea = Heat >= 0.03 && Heat <= 0.72;
		}
		if (!bSea) { ++R.NotSea; R.MinDepthCm = FMath::Min(R.MinDepthCm, 0.0); continue; }
		const double Depth = T.WaterCm - T.GroundCm;
		R.MinDepthCm = FMath::Min(R.MinDepthCm, Depth);
		R.Shallow += Depth < R.NeedDepthCm ? 1 : 0;
	}
	R.bSuits = R.NotSea == 0 && R.Shallow == 0;
	return R;
}

namespace
{
	/** A group of leapers in a cell of the shoals' lattice: false where the cell holds no painted shoal, no leapers, or the
	 *  water under them is not the open sea deep enough. */
	bool LeaperInCell(const SlonkFauna::FFaunaWorld& W, const SlonkFauna::FFaunaCell& C, FFaunaGroup& Out)
	{
		using namespace SlonkFauna;
		// Its species, the cell's own: the habitat read out as far as its longest fish leap.
		const int32 Species = SpeciesInCell(W, C);
		const FFaunaSpecies& Sp = W.Profile.Species[(int32)EFaunaKind::Leaper][Species];
		const double Longest = LeaperMaxSizeM(Sp.Plan);
		const FLeaperHabitatRead H = LeaperHabitatAt(W, C, Longest);
		if (!H.bSuits) { return false; }
		const FShoal& S = H.Shoal;
		const FFaunaKindDesc& D = KindDesc(EFaunaKind::Leaper);
		// The cell's draws after its gate (LeaperHabitatAt's first).
		SlonkScatter::FDraw Dr(SlonkScatter::CellHash(C.Face, 40 + (int32)C.Kind, C.X, C.Y, W.Salt()));
		Dr.Next();
		FFaunaGroup G;
		G.Id = C.Id();
		G.Kind = EFaunaKind::Leaper;
		G.Habitat = EFlierHabitat::Coast;
		// Its species, the cell's own (the Earth's fish at strangeness 0; on a strange world an eel or a spinefin too).
		G.Species = Species;
		G.Plan = Sp.Plan;
		G.Seed = ((uint32)(Dr.Next() * 65536.0) << 16) | (uint32)(Dr.Next() * 65536.0);
		G.Centre = S.LoopCentreCm.GetSafeNormal();
		TangentAxes(G.Centre, G.East, G.North);
		// One to five fish, of 35 to 80 cm (each a tenth either way of it), leaping in turn on a cycle of about four seconds
		// to each of them (the hour's divisor nearest, 10 to 24 s).
		G.Members = FMath::Clamp(1 + (int32)(Dr.Next() * Leaper::MaxPerGroup), FMath::Max(D.GroupMin, 1), FMath::Min(D.GroupMax, Leaper::MaxPerGroup));
		// An eel's lengths its own, the cell's same draw laid over them; any other strange species of its own size
		// (LeaperSpanM, which fauna plans reads out too).
		G.SpanM = LeaperSpanM(Sp.Plan, Sp.SizeScale, FMath::Lerp(0.35f, 0.8f, (float)Dr.Next()));
		const double Want = Leaper::SlotSec * G.Members + Leaper::SlotSpread * Dr.Next();
		G.CycleSec = Leaper::Cycles[0];
		for (const double Cyc : Leaper::Cycles) { if (FMath::Abs(Cyc - Want) < FMath::Abs(G.CycleSec - Want)) { G.CycleSec = Cyc; } }
		G.PeriodSec = G.CycleSec;
		G.Phase0 = Dr.Next();
		G.FlapHz = 0.f;
		// Its shoal: the loop it swims round (its extent the loop and its fish's reach past it), how it is painted, how deep
		// it swims, and the shallowest water read under its leaps.
		G.ShoalDensity = W.Sea.ShoalDensity;
		G.RadiusCm = S.LoopRadiusCm;
		G.Lobe = 0.0;
		G.MeanderCm = LeaperReachCm(S, Longest);
		G.ShoalHalfLengthCm = S.HalfLengthCm;
		G.ShoalHalfWidthCm = S.HalfWidthCm;
		G.ShoalDepthCm = S.DepthCm;
		G.WaterCm = H.MinDepthCm;
		G.SpacingM = (float)(S.HalfWidthCm / 100.0);
		G.Pigment = LeaperPigment(W.Profile.Look, W.Sea.Shallow, 0.85 + 0.3 * Dr.Next());
		G.Glow = GlowOf(G);
		Out = MoveTemp(G);
		return true;
	}

	/** The ground a jelly drifts over at a place on its loop: the highest within Floater::GroundReachCm of it (the place,
	 *  six points half that far out and six that far, the outer turned half a step), the sea's surface over water, the
	 *  field without the detail under 100 m (its margin clears that). */
	double FloaterGroundCm(const FPlanetTerrainParams& P, const FVector& Dir)
	{
		const FVector Up = Dir.GetSafeNormal();
		FVector E, N;
		TangentAxes(Up, E, N);
		auto At = [&P](const FVector& D)
		{
			const FPlanetTerrainSample S = SlonkTerrain::Sample(P, D, 1.0e4);
			if (S.bWater) { return FMath::Max(S.WaterCm, S.HeightCm); }
			// Over a strange world's crystal or spires, their tops, as the fliers take them.
			return P.Geology == EGeology::None ? S.HeightCm : S.HeightCm + SlonkTerrain::GeologyReliefCm(P, S.Geo);
		};
		double H = At(Up);
		for (const double Ring : { 0.5, 1.0 })
		{
			for (int32 k = 0; k < 6; ++k)
			{
				const double A = UE_DOUBLE_TWO_PI * (k + (Ring > 0.75 ? 0.5 : 0.0)) / 6.0;
				H = FMath::Max(H, At((Up + (E * FMath::Cos(A) + N * FMath::Sin(A)) * (Ring * SlonkFauna::Floater::GroundReachCm / P.RadiusCm)).GetSafeNormal()));
			}
		}
		return H;
	}

	/** A jelly in a floater cell: false where the cell holds none (the share of the cells its world rolled, at its life and
	 *  the density asked for; nothing else about the place matters to it, over land or sea). */
	bool FloaterInCell(const SlonkFauna::FFaunaWorld& W, const SlonkFauna::FFaunaCell& C, FFaunaGroup& Out)
	{
		using namespace SlonkFauna;
		const int64 Across = CellsAcross(W.P, C.Kind);
		SlonkScatter::FDraw Dr(SlonkScatter::CellHash(C.Face, 40 + (int32)C.Kind, C.X, C.Y, W.Salt()));
		// Where in the cell its loop's middle lies (its middle half, so its loop of under a kilometre keeps to it), and
		// whether the cell holds one.
		const double Wd = 2.0 / (double)Across;
		const double U = -1.0 + Wd * ((double)C.X + 0.25 + 0.5 * Dr.Next());
		const double V = -1.0 + Wd * ((double)C.Y + 0.25 + 0.5 * Dr.Next());
		const double Gate = Dr.Next();
		if (Gate >= FMath::Min((double)W.Profile.FloaterShare * (double)W.Life * (double)W.Density, 1.0)) { return false; }
		FFaunaGroup G;
		G.Id = C.Id();
		G.Kind = EFaunaKind::Floater;
		// Its species (the jellies keep a plan of their own, one species of it) and that plan.
		G.Species = SpeciesInCell(W, C);
		G.Plan = W.Profile.Species[(int32)EFaunaKind::Floater][G.Species].Plan;
		const FFaunaBodyPlan& Plan = G.Plan;
		G.Seed = ((uint32)(Dr.Next() * 65536.0) << 16) | (uint32)(Dr.Next() * 65536.0);
		G.Centre = SlonkTerrain::CubeDir(C.Face, U, V).GetSafeNormal();
		TangentAxes(G.Centre, G.East, G.North);
		G.Members = 1;
		// Its bell, mostly the smaller of 20 to 80 m across; its pulse 3 to 6 s apart (the bigger the slower, half a second
		// either way of that), a whole number of pulses an hour; its ribs and how far its tentacles hang under its middle.
		const double Sz = Dr.Next();
		G.SpanM = (float)FMath::Lerp(Floater::MinSpanM, Floater::MaxSpanM, Sz * Sz);
		const double BySize = ((double)G.SpanM - Floater::MinSpanM) / (Floater::MaxSpanM - Floater::MinSpanM);
		const double PulseSec = FMath::Clamp(FMath::Lerp(Floater::PulseLoSec, Floater::PulseHiSec, BySize) + Dr.Next() - 0.5, Floater::PulseLoSec, Floater::PulseHiSec);
		G.FlapHz = (float)(FMath::RoundToDouble(ClockPeriodSec / PulseSec) / ClockPeriodSec);
		G.Ribs = Plan.Symmetry;
		G.HangCm = Floater::HalfHeightCm(Plan) * (double)G.SpanM;
		G.SpacingM = G.SpanM;
		// Its loop: 400 to 700 m round its middle, two to four lobes, a meander of 10 to 50 m along it; rising and sinking 20
		// to 60 m over it once to three times a lap.
		G.RadiusCm = FMath::Lerp(4.0e4, 7.0e4, Dr.Next());
		G.Lobes = 2 + FMath::Min((int32)(Dr.Next() * 3.0), 2);
		G.Lobe = FMath::Lerp(0.1, 0.25, Dr.Next());
		G.LobePhase = UE_DOUBLE_TWO_PI * Dr.Next();
		G.MeanderCm = FMath::Lerp(1.0e3, 5.0e3, Dr.Next());
		G.MeanderK = G.Lobes + 2 + FMath::Min((int32)(Dr.Next() * 3.0), 2);
		G.BobK = 1 + FMath::Min((int32)(Dr.Next() * 3.0), 2);
		G.BobCm = FMath::Lerp(2.0e3, 6.0e3, Dr.Next());
		G.Turn = Dr.Next() < 0.5 ? -1.0 : 1.0;
		G.Phase0 = UE_DOUBLE_TWO_PI * Dr.Next();
		// Its middle 300 m to 3 km over that ground, mostly the lower; never so low that its tentacles' tips, drawn twice
		// their size far off (the speck floor) and sunk as far as startles sink it, come within ClearCm of the ground.
		const double Cr = Dr.Next();
		G.CruiseCm = FMath::Max(FMath::Lerp(Floater::MinCruiseCm, Floater::MaxCruiseCm, Cr * Cr), 2.0 * G.HangCm + Floater::SinkAllowCm + Floater::ClearCm);
		const double WantSpeed = 100.0 * FMath::Lerp(1.5, 4.0, Dr.Next());
		const double Hue = 2.0 * Dr.Next() - 1.0;
		const double Vary = 0.9 + 0.2 * Dr.Next();
		G.Glow = (float)FMath::Lerp(1.5, 3.0, Dr.Next());
		// The loop's length, and the period of the hour's divisors nearest the one its drift asks for (a few m/s).
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
		// The ground along it, each sample the highest near it, laid under an envelope that rises and falls no steeper than
		// Floater::GroundSlope along the loop (a sample's spacing its share of the loop's length), so it climbs ahead of a
		// ridge and sinks after it gently.
		TArray<float> Raw;
		Raw.SetNum(NumGround);
		for (int32 k = 0; k < NumGround; ++k)
		{
			const FVector Q = LoopPoint(G, UE_DOUBLE_TWO_PI * k / NumGround);
			Raw[k] = (float)FloaterGroundCm(W.P, (G.Centre * W.P.RadiusCm + Q).GetSafeNormal());
		}
		const double StepCm = Len / NumGround;
		G.Ground.SetNum(NumGround);
		for (int32 k = 0; k < NumGround; ++k)
		{
			double M = Raw[k];
			for (int32 j = 0; j < NumGround; ++j)
			{
				const int32 Apart = FMath::Min(FMath::Abs(k - j), NumGround - FMath::Abs(k - j));
				M = FMath::Max(M, (double)Raw[j] - Floater::GroundSlope * StepCm * Apart);
			}
			G.Ground[k] = (float)M;
		}
		G.Pigment = FloaterPigment(W.Profile.Look, Hue, Vary);
		Out = MoveTemp(G);
		return true;
	}
}

FString SlonkFauna::FloaterAbsence(const FFaunaWorld& W)
{
	if (W.Life <= 0.f) { return TEXT("nothing lives here"); }
	if (W.P.bAirless) { return TEXT("it has no air"); }
	if (W.Profile.Strangeness < Floater::MinStrangeness) { return FString::Printf(TEXT("its strangeness %.2f is under %.1f"), W.Profile.Strangeness, Floater::MinStrangeness); }
	if (!W.Profile.bThickAir) { return TEXT("its air is thin (no haze in its lowlands)"); }
	if (W.Density <= 0.f) { return TEXT("slonk.Fauna.Density is 0"); }
	if ((W.KindMask & KindBit(EFaunaKind::Floater)) == 0u) { return TEXT("the kind is switched off"); }
	return W.Spawns(EFaunaKind::Floater) ? FString() : FString(TEXT("none were rolled"));
}

bool SlonkFauna::GroupInCell(const FFaunaWorld& W, const FFaunaCell& C, FFaunaGroup& Out)
{
	if (!W.Spawns(C.Kind) || (C.Kind != EFaunaKind::Flier && C.Kind != EFaunaKind::Herd && C.Kind != EFaunaKind::Leaper && C.Kind != EFaunaKind::Floater)) { return false; }
	if (C.Kind == EFaunaKind::Leaper) { return LeaperInCell(W, C, Out); }
	const FFaunaKindDesc& D = KindDesc(C.Kind);
	const int64 Across = CellsAcross(W.P, C.Kind);
	if (C.X < 0 || C.Y < 0 || C.X >= Across || C.Y >= Across || C.Face < 0 || C.Face > 5) { return false; }
	if (C.Kind == EFaunaKind::Herd) { return HerdInCell(W, C, Out); }
	if (C.Kind == EFaunaKind::Floater) { return FloaterInCell(W, C, Out); }
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
	// Its species: the cell's own (the Earth's bird at strangeness 0; on a strange world four-winged and membrane-winged
	// fliers too), and its plan.
	G.Species = SpeciesInCell(W, C);
	const FFaunaSpecies& Spc = W.Profile.Species[(int32)C.Kind][G.Species];
	G.Plan = Spc.Plan;
	const int32 Form = Wings::FormOf(G.Plan);
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
	if (Form != Wings::Bird)
	{
		// A strange world's flier: its species' size against its habitat's bird's, its flock as far apart for it, its beat a
		// four-winged one's faster and a membrane-winged one's slower.
		G.SpanM = FMath::Clamp(G.SpanM * Spc.SizeScale, 0.25f, 2.6f);
		G.SpacingM *= Spc.SizeScale;
		G.FlapHz = Form == Wings::Four ? FMath::Min(G.FlapHz * Wings::FourFlapK, Wings::FourFlapMax) : G.FlapHz * Wings::MembraneFlapK;
	}
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
	double WantSpeed = 100.0 * FMath::Lerp((double)Sp.SpeedLo, (double)Sp.SpeedHi, Dr.Next());
	// A four-winged flock flies its loop slower than a bird's (it hovers and darts as it goes), a membrane-winged one a little.
	if (Form != Wings::Bird) { WantSpeed *= Form == Wings::Four ? Wings::FourSpeedK : Wings::MembraneSpeedK; }
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
	G.Glow = GlowOf(G);
	Out = MoveTemp(G);
	return true;
}

bool SlonkFauna::FindGroupNear(const FFaunaWorld& W, EFaunaKind K, const FVector& Dir, double MaxCm, int32 MaxCells, FFaunaGroup& Out, FGroupSearch* OutSearch, int32 Species)
{
	FGroupSearch Search;
	bool bFound = false;
	const FVector Up = Dir.GetSafeNormal();
	if (W.Spawns(K) && !Up.IsNearlyZero())
	{
		FVector E, N;
		TangentAxes(Up, E, N);
		const double Cell = KindDesc(K).CellCm, Step = Cell / 3.0;
		const double Wd = 2.0 / (double)CellsAcross(W.P, K);
		TSet<uint64> Seen;
		TArray<TPair<double, FFaunaCell>> Ring;
		double BestDist = TNumericLimits<double>::Max();
		bool bFull = false;
		// Ring by ring outward, a cell wide each: every cell under the ring (points a third of a cell apart) not read yet,
		// nearest first by its middle. A group's middle lies in its cell, so once one is found no cell first met more than
		// a cell further out can hold a nearer one.
		for (double R0 = 0.0; R0 < MaxCm && !bFull && !(bFound && R0 - Cell > BestDist); R0 += Cell)
		{
			const double R1 = FMath::Min(R0 + Cell, MaxCm);
			Ring.Reset();
			for (double Rr = R0; Rr < R1; Rr += Step)
			{
				const int32 Round = FMath::Max(FMath::CeilToInt32(UE_DOUBLE_TWO_PI * Rr / Step), 1);
				for (int32 k = 0; k < Round; ++k)
				{
					const double A = UE_DOUBLE_TWO_PI * (k + 0.5) / Round;
					const FFaunaCell C = CellOf(W.P, K, (Up + (E * FMath::Cos(A) + N * FMath::Sin(A)) * (Rr / W.P.RadiusCm)).GetSafeNormal());
					bool bAlready = false;
					Seen.Add(C.Id(), &bAlready);
					if (bAlready) { continue; }
					FVector Mid;
					if (K == EFaunaKind::Leaper)
					{
						if (!CellMiddle(W.P, C, Mid)) { continue; }
					}
					else { Mid = SlonkTerrain::CubeDir(C.Face, -1.0 + Wd * (C.X + 0.5), -1.0 + Wd * (C.Y + 0.5)).GetSafeNormal(); }
					Ring.Add({ FVector::Dist(Mid, Up) * W.P.RadiusCm, C });
				}
			}
			Ring.Sort([](const TPair<double, FFaunaCell>& L, const TPair<double, FFaunaCell>& R) { return L.Key < R.Key || (L.Key == R.Key && L.Value.Id() < R.Value.Id()); });
			for (const TPair<double, FFaunaCell>& Pr : Ring)
			{
				if (Search.Cells >= MaxCells) { bFull = true; break; }
				++Search.Cells;
				if (Species >= 0 && SpeciesInCell(W, Pr.Value) != Species) { continue; }
				FFaunaGroup G;
				if (!GroupInCell(W, Pr.Value, G)) { continue; }
				const double Dist = FVector::Dist(G.Centre, Up) * W.P.RadiusCm;
				if (Dist < BestDist) { BestDist = Dist; Out = MoveTemp(G); bFound = true; }
			}
			if (!bFull) { Search.ReachCm = R1; }
		}
	}
	if (OutSearch) { *OutSearch = Search; }
	return bFound;
}

namespace
{
	/** A herd's place on its loop a way along it (cm from theta 0, the loop's own way), on the field's ground. */
	FVector HerdLoopAt(const FFaunaGroup& G, double RadiusCm, double AlongCm)
	{
		const double Th = ThetaAlong(G, AlongCm);
		const FVector Dir = (G.Centre * RadiusCm + LoopPoint(G, Th)).GetSafeNormal();
		return Dir * (RadiusCm + LoopGround(G, Th));
	}
	/** Where a herd's path has it at a time: its frame's place alone (HerdPathAt's PosCm). */
	FVector HerdPathPos(const FFaunaGroup& G, double RadiusCm, double Seconds)
	{
		return HerdLoopAt(G, RadiusCm, HerdAlongCm(G, WrapHour(Seconds)));
	}

	/** A herd's frame on its loop at a time: on the field's ground, its own east and north laid level, the way it walks
	 *  along the loop (which it faces when it walks), whether it is set to walk. */
	SlonkFauna::FFaunaPose HerdPathAt(const FFaunaGroup& G, double RadiusCm, double Seconds)
	{
		auto At = [&](double S) { return HerdLoopAt(G, RadiusCm, HerdAlongCm(G, S)); };
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
		const SlonkFauna::Walker::FShape Shape = SlonkFauna::Walker::ShapeOf(G.Plan);
		const double LoopM = G.LengthCm / 100.0;
		OutXf.Reserve(N);
		OutCustom.Reserve(N * SlonkFauna::Custom::Num);
		for (int32 i = 0; i < N; ++i)
		{
			const FVector At = P[i];
			const double Size = FMath::Clamp((double)G.SpanM * Rng.FRandRange(0.9f, 1.1f), Shape.MinSizeM, Shape.MaxSizeM);
			// A stride of about its form's share of its length, its herd's loop a whole number of them.
			const double Laps = FMath::Max(FMath::RoundToDouble(LoopM / (Shape.StrideShare * Size)), 1.0);
			const double Stride = LoopM / Laps;
			// Its grazing circle, a whole number of strides round (at least three), no wider than the box allows, and
			// keeping to its half of the room its nearest neighbour leaves it past the longest walker's length: any two
			// circles then leave that length between the walkers going round them.
			int32 K = FMath::Max(FMath::RoundToInt32(UE_DOUBLE_TWO_PI * Rng.FRandRange(1.2f, (float)SlonkFauna::Walker::MaxCircleM) / Stride), 3);
			while (K > 3 && K * Stride / UE_DOUBLE_TWO_PI > SlonkFauna::Walker::MaxCircleM) { --K; }
			// No wider than its form lets the speck floor's growth and its circle stay in its instance's box together (an
			// insectoid's: Walker::FShape::CircleShare; no bound on the Earth's grazer).
			while (K > 3 && K * Stride / UE_DOUBLE_TWO_PI > Size * Shape.CircleShare) { --K; }
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

namespace
{
	/** A group of leapers' frame: its shoal's middle at the world's seconds (never wrapped: the material swims its shoals
	 *  on them), on the sea-level sphere, x the way it swims, z up; at its loop's middle, x its own east, should the shoal
	 *  not be found (it always is). */
	SlonkFauna::FFaunaPose LeaperPathAt(const FFaunaGroup& G, double RadiusCm, double Seconds)
	{
		auto At = [&](double S, FVector& OutHeading)
		{
			SlonkFauna::FShoal Sh;
			if (SlonkFauna::ShoalAt(G.Centre * RadiusCm, RadiusCm, G.ShoalDensity, S, Sh)) { OutHeading = Sh.Heading; return Sh.CentreCm; }
			OutHeading = G.East;
			return G.Centre * RadiusCm;
		};
		SlonkFauna::FFaunaPose Pose;
		FVector Heading, Hm, Hp;
		Pose.PosCm = At(Seconds, Heading);
		constexpr double H = 0.25;
		Pose.VelCmS = (At(Seconds + H, Hp) - At(Seconds - H, Hm)) / (2.0 * H);
		const FVector Up = Pose.PosCm.GetSafeNormal();
		FVector Fwd = (Heading - Up * FVector::DotProduct(Heading, Up)).GetSafeNormal();
		if (Fwd.IsNearlyZero()) { FVector N; TangentAxes(Up, Fwd, N); }
		Pose.Rot = FRotationMatrix::MakeFromXZ(Fwd, Up).ToQuat();
		return Pose;
	}

	/** A group of leapers' fish: each at its home within its shoal's painted shape (out to 0.55 of its half-length along it
	 *  and of its half-width across, no two closer than a longest fish's length where there is room), unturned, of its
	 *  length; its slot in the group's cycle the group's offset and its place in the group's order. */
	void LeaperMembersOf(const FFaunaGroup& G, TArray<FTransform>& OutXf, TArray<float>& OutCustom)
	{
		const int32 N = FMath::Clamp(G.Members, 0, SlonkFauna::Leaper::MaxPerGroup);
		if (N == 0) { return; }
		FRandomStream Rng((int32)G.Seed);
		const SlonkFauna::FFaunaKindDesc& D = SlonkFauna::KindDesc(G.Kind);
		const double Ha = 0.55 * G.ShoalHalfLengthCm, Hw = 0.55 * G.ShoalHalfWidthCm;
		TArray<FVector> Homes;
		for (int32 i = 0; i < N; ++i)
		{
			FVector Best = FVector::ZeroVector;
			double BestGap = -1.0;
			for (int32 Try = 0; Try < 24 && BestGap < 100.0 * SlonkFauna::Leaper::MaxSizeM; ++Try)
			{
				FVector B;
				do { B = FVector(Rng.FRandRange(-1.f, 1.f), Rng.FRandRange(-1.f, 1.f), 0.0); } while (B.SizeSquared() > 1.0);
				B = FVector(B.X * Ha, B.Y * Hw, 0.0);
				double Gap = TNumericLimits<double>::Max();
				for (const FVector& Q : Homes) { Gap = FMath::Min(Gap, FVector::Dist2D(B, Q)); }
				if (Gap > BestGap) { BestGap = Gap; Best = B; }
			}
			Homes.Add(Best);
		}
		OutXf.Reserve(N);
		OutCustom.Reserve(N * SlonkFauna::Custom::Num);
		for (int32 i = 0; i < N; ++i)
		{
			const double Size = FMath::Clamp((double)G.SpanM * Rng.FRandRange(0.9f, 1.1f), SlonkFauna::Leaper::MinSizeM, SlonkFauna::LeaperMaxSizeM(G.Plan));
			const float Shade = Rng.FRandRange(0.9f, 1.1f);
			const float Seed = Rng.FRand();
			// Its slot in the group's cycle: the group's offset and its place in the order, so the group's fish leap in turn,
			// a slot (the cycle over their count) apart about.
			float Ph = (float)G.Phase0 + (float)i / (float)N;
			Ph -= FMath::FloorToFloat(Ph);
			OutXf.Add(FTransform(FQuat::Identity, Homes[i], FVector(Size)));
			const float C[SlonkFauna::Custom::Num] = { G.Pigment.R * Shade, G.Pigment.G * Shade, G.Pigment.B * Shade, G.Glow, Seed, (float)Size, Ph, (float)G.CycleSec,
				(float)(8 * i + N), (float)D.FadeCm };
			OutCustom.Append(C, SlonkFauna::Custom::Num);
		}
	}
}

SlonkFauna::FFaunaPose SlonkFauna::PathAt(const FFaunaGroup& G, double RadiusCm, double Seconds)
{
	if (G.Kind == EFaunaKind::Herd) { return HerdPathAt(G, RadiusCm, Seconds); }
	if (G.Kind == EFaunaKind::Leaper) { return LeaperPathAt(G, RadiusCm, Seconds); }
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
	if (G.Kind == EFaunaKind::Floater)
	{
		// A jelly turns no way as it drifts (its body is round): its loop's own east and north laid level where it is.
		Pose.Rot = FRotationMatrix::MakeFromXZ(Level(G.East).GetSafeNormal(), Up).ToQuat();
		return Pose;
	}
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
	if (G.Kind == EFaunaKind::Leaper) { LeaperMembersOf(G, OutXf, OutCustom); return; }
	const int32 N = FMath::Max(G.Members, 0);
	if (N == 0) { return; }
	if (G.Kind == EFaunaKind::Floater)
	{
		// A jelly alone at its group's middle, unturned, its span its scale; its pulse's phase its own.
		FRandomStream Rng((int32)G.Seed);
		const float Seed = Rng.FRand();
		const float Ph = Rng.FRand();
		OutXf.Add(FTransform(FQuat::Identity, FVector::ZeroVector, FVector(G.SpanM)));
		const float C[Custom::Num] = { G.Pigment.R, G.Pigment.G, G.Pigment.B, G.Glow, Seed, G.SpanM, Ph, G.FlapHz, (float)G.Ribs, (float)KindDesc(G.Kind).FadeCm };
		OutCustom.Append(C, Custom::Num);
		return;
	}
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
		// Turned a little from the flock's way (a four-winged flier not: it darts with its flock, the same way in its frame).
		const float Yaw = Rng.FRandRange(-0.1f, 0.1f);
		OutXf.Add(FTransform(FQuat(FVector::ZAxisVector, G.Kind == EFaunaKind::Flier && Wings::FormOf(G.Plan) == Wings::Four ? 0.f : Yaw), P[i], FVector(Scale)));
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
	/** How far a galloping kind's gallop has come on, 0 to 1: from a stand at its FleeT0, at its fastest its rise's time in
	 *  (FPanic::RiseSec: its kind's FleeRiseSec, or longer for a herd of smaller walkers). */
	double GallopRise(const SlonkFauna::FPanic& P, const SlonkFauna::FFaunaKindDesc& D, double Now)
	{
		const double X = FMath::Max(Now - P.FleeT0, 0.0) / FMath::Max(P.RiseSec > 0.0 ? P.RiseSec : D.FleeRiseSec, 0.1);
		return 1.0 - FMath::Exp(-X) * (1.0 + X);
	}
	/** How far a galloping kind has come back from its flight, 0 to 1, Age seconds after its startle: at a steady pace
	 *  from its FleeBackSec to the fright's end. */
	double ComeBack(const SlonkFauna::FPanic& P, const SlonkFauna::FFaunaKindDesc& D, double Age)
	{
		return PaceEase((Age - D.FleeBackSec) / FMath::Max(P.LiveSec - D.FleeBackSec, 1.0));
	}
}

double SlonkFauna::HerdRiseSec(const FFaunaGroup& G, const FFaunaKindDesc& D)
{
	// At its fastest its gallop goes its kind's FleeCm over e times its rise's time.
	const double FastestCmS = 100.0 * HerdGallopStridesHz * HerdShortStrideM(G);
	return FMath::Max(D.FleeRiseSec, D.FleeCm / (FMath::Exp(1.0) * FMath::Max(FastestCmS, 1.0)));
}

double SlonkFauna::HerdKapRate(const FFaunaGroup& G)
{
	const Walker::FShape S = Walker::ShapeOf(G.Plan);
	if (S.Form == Walker::EForm::Grazer || S.NumLegs <= 0) { return Walker::MaxKapRate; }
	// Its longest walker's farthest foot's reach from its body's middle and its stance (how far it walks on each foot), m,
	// against an Earth grazer's of 2 m (its hooves 0.34 of its length out, its stance 0.6 of its stride).
	const double Longest = FMath::Min(1.1 * (double)G.SpanM, S.MaxSizeM);
	double Foot = 0.0;
	for (int32 i = 0; i < S.NumLegs; ++i) { Foot = FMath::Max(Foot, (double)FVector2f(S.Legs[i].Foot.X, S.Legs[i].Foot.Y).Size() / 100.0); }
	const double Carried = Foot * Longest * Walker::Duty * S.StrideShare * Longest;
	const double Earth = (FVector2D(33.0, 8.0).Size() / 100.0 * 2.0) * (Walker::Duty * Walker::StrideShare * 2.0);
	return Walker::MaxKapRate * FMath::Min(1.0, Earth / FMath::Max(Carried, 1e-6));
}

double SlonkFauna::HerdTurnRateOf(const FFaunaGroup& G, bool bShaken)
{
	if (!bShaken) { return HerdTurnRate; }
	return FMath::Min(HerdShakenTurnRate, HerdGallopStridesHz * HerdShortStrideM(G) / Walker::PivotM);
}

FVector SlonkFauna::PanicOffset(const FPanic& P, const FFaunaKindDesc& D, double Now)
{
	const double Age = Now - P.T0;
	const double Live = P.LiveSec;
	if (Age < 0.0 || Age >= Live) { return FVector::ZeroVector; }
	const double Hold = 1.0 - SlonkScatter::SmoothStep(0.3 * Live, Live, Age);
	const double CarryFade = 1.0 - SlonkScatter::SmoothStep(0.0, Live, Age);
	if (D.Kind == EFaunaKind::Floater)
	{
		// A jelly squeezes itself away and down from the drift it had (none if it was calm; a fright before left it the
		// drift it carries, which coasts off over Floater::CoastSec), its pace coming up from nothing to its fastest
		// FleeRiseSec in (FleeCm over e times that), held a while and back over the rest; what was left of the last flight
		// fades as this runs. So its frame's velocity never jumps.
		const double X = Age / FMath::Max(D.FleeRiseSec, 0.1);
		const double Rise = 1.0 - FMath::Exp(-X) * (1.0 + X);
		constexpr double Coast = Floater::CoastSec;
		return (P.Carry + P.CarryVel * (Coast * (1.0 - FMath::Exp(-Age / Coast)))) * CarryFade + P.Dir * (D.FleeCm * Rise * Hold);
	}
	if (D.bGallop)
	{
		// A walker turns to flee where it stands, then breaks into a gallop from a stand (at its FleeT0), at its fastest
		// (FleeCm over e times its rise's time) that long in, and slows as it nears its flight's end; from FleeBackSec it comes
		// back at a steady walking pace, eased in and out, onto its path by the fright's end. A second fright carries on
		// the gallop it was in as it turns (slowing over a few seconds), and what was left of the last flight comes back
		// with this one, at the same steady pace (its fright lengthened for the further way back: StartleHerd).
		const double Rise = GallopRise(P, D, Now);
		constexpr double Coast = SlonkFauna::HerdCoastSec;
		// Its way eased in and out smoothly enough (to the second derivative) that its walkers' way never turns at a
		// jump: back from its flight, and back round a loop rather than turning about where it stops: off to its side over
		// the forty seconds its flight slows and turns back, held there as it walks back, and onto its path over the last
		// half of its walk back.
		const double Back = 1.0 - ComeBack(P, D, Age);
		const double Loop = D.FleeLoopCm * EaseC2(D.FleeBackSec - 10.0, D.FleeBackSec + 30.0, Age) * (1.0 - EaseC2(0.5 * (D.FleeBackSec + Live), Live, Age));
		return (P.Carry + P.CarryVel * (Coast * (1.0 - FMath::Exp(-Age / Coast)))) * Back + P.Dir * (D.FleeCm * Rise * Back) + P.Side * Loop;
	}
	// Away at a bird's escape speed (FleeCm over 8 s, at first), held for half a minute, then back over the rest; what
	// was left of the last flight fades as this runs.
	const double Rise = 1.0 - FMath::Exp(-Age / D.FleeRiseSec);
	return P.Carry * CarryFade + P.Dir * (D.FleeCm * Rise * Hold);
}

FVector SlonkFauna::HerdFrightOffset(const FFaunaGroup& G, const FFaunaKindDesc& D, const FPanic& P, double RadiusCm, double Now)
{
	FVector Off = PanicOffset(P, D, Now);
	if (!P.IsLive(Now)) { return Off; }
	if (P.LoopWay != 0 && D.bGallop)
	{
		// Fled along its own loop: as far along it from where its path has it now as its gallop has taken it and its walk
		// back not yet brought it back.
		const double Along = HerdAlongCm(G, Now);
		const double Flown = D.FleeCm * GallopRise(P, D, Now) * (1.0 - ComeBack(P, D, Now - P.T0));
		if (Flown > 0.0) { Off += HerdLoopAt(G, RadiusCm, Along + P.LoopWay * Flown) - HerdLoopAt(G, RadiusCm, Along); }
	}
	if (P.bHold)
	{
		// Its path's own walk since the startle, held back while its walkers turn to flee, let go as its gallop comes on
		// (and what is left of it as it comes back), so it gallops off from where it stood.
		const double Let = (1.0 - GallopRise(P, D, Now)) * (1.0 - ComeBack(P, D, Now - P.T0));
		if (Let > 0.0) { Off += (HerdPathPos(G, RadiusCm, P.T0) - HerdPathPos(G, RadiusCm, Now)) * Let; }
	}
	return Off;
}

bool SlonkFauna::HerdFlightDry(const FFaunaGroup& G, const FFaunaKindDesc& D, const FPanic& P, double RadiusCm, double Now, const FVector& Avoid, FHerdWater& Water)
{
	if (!P.IsLive(Now)) { return true; }
	// Its frame a second apart from now to its fright's end (a gallop goes 11 m in one at most), read wherever it has gone
	// HerdFlightReadCm on from its last read and where its fright ends: dry about it, and as far on as its gallop would
	// coast there were it frightened again.
	const double End = P.T0 + P.LiveSec;
	auto At = [&](double T) { return HerdPathPos(G, RadiusCm, T) + HerdFrightOffset(G, D, P, RadiusCm, T) + Avoid; };
	FVector Prev = At(Now), LastRead = Prev;
	for (double T = Now + 1.0; ; T += 1.0)
	{
		const double Tc = FMath::Min(T, End);
		const FVector X = At(Tc);
		const FVector Vel = X - Prev;
		Prev = X;
		if (FVector::Dist(X, LastRead) >= HerdFlightReadCm || Tc >= End)
		{
			LastRead = X;
			if (!Water.DryAround(G, X, Vel, HerdFlightDryCm)) { return false; }
			if (Vel.Size() > 100.0 && !Water.DryAround(G, X + Vel * HerdCoastSec, Vel, HerdFlightDryCm)) { return false; }
		}
		if (Tc >= End) { break; }
	}
	return true;
}

SlonkFauna::FHerdWater::FHerdWater(const FPlanetTerrainParams& P)
	// Water, and any ground a herd never sets foot on (a dense wood, a cliff, steep ground, ice), barred alike: a flight or
	// a walk out of the crew's way is never taken through either.
	: IsWater([P](const FVector& Dir) { return SlonkTerrain::SampleStructure(P, Dir, 1000.0).bWater || !SlonkFauna::HerdCrosses(SlonkScatter::HabitatAt(P, Dir)); }), RadiusCm(P.RadiusCm)
{
}

bool SlonkFauna::FHerdWater::WetAt(const FFaunaGroup& G, const FVector& PosCm)
{
	if (!IsWater || RadiusCm <= 0.0) { return false; }
	// The lattice laid on its loop's tangent plane (the plane's own east and north, a point's place on it where the line
	// from the planet's middle through it meets the plane), its nearest point read.
	const FVector U = PosCm.GetSafeNormal();
	const double Dc = FVector::DotProduct(U, G.Centre);
	if (Dc < 0.5) { return false; }
	const int64 Ix = FMath::RoundToInt64(FVector::DotProduct(U, G.East) / Dc * RadiusCm / HerdWaterStepCm);
	const int64 Iy = FMath::RoundToInt64(FVector::DotProduct(U, G.North) / Dc * RadiusCm / HerdWaterStepCm);
	const uint64 Key = ((uint64)(uint32)(int32)Ix << 32) | (uint64)(uint32)(int32)Iy;
	if (const bool* Known = Wet.Find(Key)) { return *Known; }
	const bool bWet = IsWater((G.Centre + (G.East * (double)Ix + G.North * (double)Iy) * (HerdWaterStepCm / RadiusCm)).GetSafeNormal());
	Wet.Add(Key, bWet);
	return bWet;
}

bool SlonkFauna::FHerdWater::DryAround(const FFaunaGroup& G, const FVector& PosCm, const FVector& Way, double MarginCm)
{
	const FVector Up = PosCm.GetSafeNormal();
	FVector Fwd = Way - Up * FVector::DotProduct(Way, Up);
	if (WetAt(G, PosCm)) { return false; }
	const double Reach = G.HerdRadiusCm + MarginCm;
	if (Fwd.Size() < 1.0)
	{
		// Standing: all round it.
		FVector E, N;
		TangentAxes(Up, E, N);
		for (const FVector& A : { E, N, -E, -N }) { if (WetAt(G, PosCm + A * Reach)) { return false; } }
		return true;
	}
	const FVector Side = FVector::CrossProduct(Up, Fwd.GetSafeNormal());
	return !WetAt(G, PosCm + Side * Reach) && !WetAt(G, PosCm - Side * Reach);
}

SlonkFauna::FFaunaPose SlonkFauna::FlightAt(const FFaunaGroup& G, const FFaunaKindDesc& D, const FPanic& P, double RadiusCm, double Now, const FFaunaPose& Was, double Dt)
{
	const FFaunaPose Path = PathAt(G, RadiusCm, Now);
	if (!P.IsLive(Now)) { return Path; }
	if (G.Kind == EFaunaKind::Floater)
	{
		// A jelly turns no way as it flees: where its path and its flight put it, its loop's east and north laid level there.
		FFaunaPose Pose = Path;
		Pose.PosCm = Path.PosCm + PanicOffset(P, D, Now);
		Pose.VelCmS = FloaterVelocity(G, D, P, RadiusCm, Now);
		const FVector Up = Pose.PosCm.GetSafeNormal();
		Pose.Rot = FRotationMatrix::MakeFromXZ((G.East - Up * FVector::DotProduct(G.East, Up)).GetSafeNormal(), Up).ToQuat();
		return Pose;
	}
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
	// Where its last flight has it now, and (a galloping kind, a jelly) how fast it was going: it carries on from both as
	// it turns (a calm one from none).
	const FVector Was = PanicOffset(P, D, Now);
	const bool bCarriesPace = D.bGallop || D.Kind == EFaunaKind::Floater;
	const FVector WasVel = bCarriesPace && P.IsLive(Now) ? (Was - PanicOffset(P, D, Now - 0.05)) / 0.05 : FVector::ZeroVector;
	if (bCarriesPace) { P.CarryVel = WasVel; }
	P.Carry = Was;
	// A jelly sinks as it flees, the less the further a fright before has sunk it (level from half the sinking its loop
	// allows for): chased again and again, it is never driven down onto the ground.
	const double DownShare = D.Kind == EFaunaKind::Floater ? FMath::Clamp(1.0 + FVector::DotProduct(Was, Up) / (0.5 * Floater::SinkAllowCm), 0.0, 1.0) : 1.0;
	P.Dir = (Away.GetSafeNormal() + Up * (D.FleeUp * DownShare)).GetSafeNormal();
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

bool SlonkFauna::StartleByCommand(FPanic& P, const FFaunaKindDesc& D, const FVector& GroupPosCm, const FFaunaThreat& T, double Now)
{
	// A jelly asked again and again, from the same side, would be pushed off its loop past the ground read round it.
	if (D.Kind == EFaunaKind::Floater) { return StartleGroup(P, D, GroupPosCm, T, Now); }
	StartleNow(P, D, GroupPosCm, T, Now);
	return true;
}

bool SlonkFauna::StartleHerd(FPanic& P, FHerdState& S, const FFaunaGroup& G, const FFaunaKindDesc& D, double RadiusCm, const FFaunaThreat& T, double Now, bool bForce, FHerdWater* Water)
{
	if (!S.bStarted) { return false; }
	// Where its fright has it now (its hold of its path among it) and how fast that is taking it.
	const FVector Was = HerdFrightOffset(G, D, P, RadiusCm, Now);
	if (!bForce)
	{
		if (!Scares(D, S.FramePos, T, T.bCraft ? 0.0 : G.HerdRadiusCm)) { return false; }
		if (P.IsLive(Now) && Now - P.T0 < RestartleSec) { return false; }
		// A herd that found no way it could take from where its last flight carried it tries again only after a
		// re-startle's while, not every frame the threat stays near.
		if (Now < S.NextStartleSec) { return false; }
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
	// As it was, should no dry way be found from where an earlier fright has carried it.
	const FPanic Before = P;
	const FHerdState StateBefore = S;
	// Its alarm, its moment and the way away from the threat, as any group's; its gallop to come on as its walkers' strides
	// allow.
	StartleNow(P, D, S.FramePos, T, Now);
	P.RiseSec = HerdRiseSec(G, D);
	FVector Away = Level(P.Dir).GetSafeNormal();
	if (Away.IsNearlyZero()) { Away = Facing; }
	P.Carry = Was;
	P.LoopWay = 0;
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
		const double Rate = HerdTurnRateOf(G, true);
		if (S.Travel > 0.0)
		{
			Stand = (S.PivotSign > 0.0 ? 2.0 - S.Travel : S.Travel) / Rate;
			S.bTurnAround = true;
		}
		S.AvoidPace = 0.0;
		S.AvoidGoing = FVector::ZeroVector;
		P.FleeT0 = Now + Stand + FMath::Max(D.FleeTurnSec, 1.0 / Rate);
	}
	// Its fright as long as its way back asks: what an earlier one left it carrying (and the gallop it coasts on) comes back
	// with its own flight, at the same steady pace, over a walk back as much longer as the way is.
	const double BackCm = P.Carry.Size() + HerdCoastSec * P.CarryVel.Size() + D.FleeCm;
	P.LiveSec = D.FleeBackSec + (D.FrightSec - D.FleeBackSec) * FMath::Max(1.0, BackCm / FMath::Max(D.FleeCm, 1.0));
	// Its flight's way, and the side it loops round to come back (away from the way the threat goes).
	auto FleeOn = [&](const FVector& Way)
	{
		P.Dir = (Way + Up * D.FleeUp).GetSafeNormal();
		P.Side = FVector::CrossProduct(Up, Way).GetSafeNormal();
		if (FVector::DotProduct(Level(T.VelCmS), P.Side) > 0.0) { P.Side = -P.Side; }
	};
	FleeOn(Away);
	if (!Water) { return true; }
	// Kept dry: that way, else the ways turned from it by a twelfth, a sixth and a quarter of a turn, the nearer first and
	// toward the side it would loop round first, the first whose flight and walk back find no water.
	const FVector Toward = FVector::DotProduct(FVector::CrossProduct(Up, Away), P.Side) >= 0.0 ? FVector::CrossProduct(Up, Away) : -FVector::CrossProduct(Up, Away);
	for (const double Twelfths : { 0.0, 1.0, -1.0, 2.0, -2.0, 3.0, -3.0 })
	{
		const double A = Twelfths * UE_DOUBLE_PI / 6.0;
		FleeOn((Away * FMath::Cos(A) + Toward * FMath::Sin(A)).GetSafeNormal());
		if (HerdFlightDry(G, D, P, RadiusCm, Now, S.Avoid, *Water)) { return true; }
	}
	// No dry way off its loop: it flees along its own loop (whose ground keeps clear of the water), the way along it nearer
	// the way away.
	const FVector Along = Level(PathAt(G, RadiusCm, Now).Ahead).GetSafeNormal();
	const double Ahead = FVector::DotProduct(Along, Away) >= 0.0 ? 1.0 : -1.0;
	P.LoopWay = Ahead * G.Turn >= 0.0 ? 1 : -1;
	P.Dir = FVector::ZeroVector;
	P.Side = FVector::ZeroVector;
	// On its loop (nothing carried from an earlier fright) its ground keeps it dry; carried off it, its way along the loop
	// is read like any other, either way along it.
	if (P.Carry.IsNearlyZero() && P.CarryVel.IsNearlyZero()) { return true; }
	if (HerdFlightDry(G, D, P, RadiusCm, Now, S.Avoid, *Water)) { return true; }
	P.LoopWay = -P.LoopWay;
	if (HerdFlightDry(G, D, P, RadiusCm, Now, S.Avoid, *Water)) { return true; }
	// No dry way from where its last flight has carried it: it keeps the fright it has, whose way was read dry.
	P = Before;
	S = StateBefore;
	S.NextStartleSec = Now + RestartleSec;
	return false;
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

namespace
{
	/** The hash a four-winged flier's darts are drawn by, uint for uint as the material's. */
	float DartU(uint32 Cycle, uint32 Seed, uint32 K)
	{
		uint32 X = Cycle * 0x9E3779B1u + Seed * 0x85EBCA77u + K * 0xC2B2AE3Du + 0x165667B1u;
		X ^= X >> 16;
		X *= 0x7FEB352Du;
		X ^= X >> 15;
		X *= 0x846CA68Bu;
		X ^= X >> 16;
		return (float)(X >> 8) * (1.f / 16777216.f);
	}
}

float SlonkFauna::MembraneLinesAt(const FVector2f& UV, const FVector2f& UVPerPixel)
{
	// fauna_material.py's CODE, its alien fliers' branch, a membrane's lines.
	const float Avw = FMath::Max(UVPerPixel.Y, 1e-4f), Auw = FMath::Max(UVPerPixel.X, 1e-4f);
	const float Asw = FMath::Max(0.05f, 0.75f * Avw), Alw = FMath::Max(0.07f, 0.75f * Auw);
	const float Au = FracF(UV.X);
	const float Strut = (1.f - SmoothStepF(Asw - 0.5f * Avw, Asw + 0.5f * Avw, FMath::Abs(FracF(UV.Y + 0.5f) - 0.5f))) * FMath::Min(0.05f / FMath::Min(Asw, 0.5f), 1.f);
	const float Lead = (1.f - SmoothStepF(Alw - 0.5f * Auw, Alw + 0.5f * Auw, Au)) * FMath::Min(0.07f / FMath::Min(Alw, 1.f), 1.f);
	return FMath::Max(Strut, Lead);
}

float SlonkFauna::DartSeedOf(const FFaunaGroup& G)
{
	if (G.Kind != EFaunaKind::Flier || Wings::FormOf(G.Plan) != Wings::Four) { return 0.f; }
	return (float)((G.Seed * 2654435761u) >> 8) / 16777216.f;
}

FVector3f SlonkFauna::FlierDart(float T, const float* Gd)
{
	// The flock's own number, so every member darts together.
	const float Seed = Gd[GroupData::DartSeed];
	const float Dn = Wings::DartLo + FMath::FloorToFloat(FracF(Seed * 5.93f) * Wings::DartSpan);
	const float Du = T * (Dn / 3600.f) + FracF(Seed * 2.37f);
	const float Di = FMath::FloorToFloat(Du);
	const float Dt = SmoothStepF(1.f - Wings::DashShare, 1.f, Du - Di);
	const uint32 Sd = (uint32)(Seed * 65536.f);
	FVector3f Dp[2];
	for (int32 Dk = 0; Dk < 2; ++Dk)
	{
		const float Dc = Di + (float)Dk;
		const uint32 Dcu = (uint32)(Dc - Dn * FMath::FloorToFloat(Dc / Dn));
		for (int32 Dj = 0; Dj < 3; ++Dj) { Dp[Dk][Dj] = (DartU(Dcu, Sd, (uint32)Dj) * 2.f - 1.f) * Wings::DashCm[Dj]; }
	}
	return FMath::Lerp(Dp[0], Dp[1], Dt);
}

FVector3f SlonkFauna::FlierMotion(const FVector3f& LP, const FLinearColor& VC, float T, const float* C, const float* Gd, float DistCm, float PxPerRad, const FVector2f& UV, int32 WingForm)
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
	const float K = FMath::Clamp(2.f / FMath::Max(Px, 0.001f), 1.f, WingForm != Wings::Bird ? Wings::SpeckMax : 4.f);
	// Its plan's wings: a bird's as they always were.
	const bool bFour = WingForm == Wings::Four, bMemb = WingForm == Wings::Membrane;
	const float Nb = bMemb ? Wings::GlideLo + FMath::FloorToFloat(FracF(Seed * 7.31f) * Wings::GlideSpan) : 514.f + FMath::FloorToFloat(FracF(Seed * 7.31f) * 686.f);
	const float Cyc = FracF(T * (Nb / 3600.f) + Seed);
	const float Share = bMemb ? Wings::MembraneShare + 0.15f * FracF(Seed * 3.77f) : 0.45f + 0.35f * FracF(Seed * 3.77f);
	const float Burst = bFour ? 1.f : SmoothStepF(0.f, 0.08f, Cyc) * (1.f - SmoothStepF(Share, Share + 0.08f, Cyc));
	const float Urge = FMath::Clamp(Panic * 3.f, 0.f, 1.f) * SmoothStepF(0.f, 0.3f, OnsetAge);
	const float FlapOn = FMath::Max(Burst, Urge);
	const float Lag = bFour && UV.X > 1.5f ? Wings::HindLag : 0.f;
	const float BeatCalm = FMath::Sin(6.2831853f * FracF(T * Rate + Seed * 5.f - Lag));
	const float BeatFast = FMath::Sin(6.2831853f * FracF(T * Rate * 1.5f + Seed * 5.f - Lag));
	const float Beat = FMath::Lerp(BeatCalm, BeatFast, Urge);
	const float Swing = bFour ? Wings::FourSwing : (bMemb ? Wings::MembraneSwing : 0.9f);
	const float Raise = bMemb ? Wings::MembraneRaise : 0.12f;
	const float Ang = FMath::Lerp(Raise, Swing * Beat, FlapOn) / K;
	FVector3f P = LP;
	const float Va = VC.A;
	if (Va > 0.4f && Va < 0.6f)
	{
		const float S = VC.B > 0.5f ? 1.f : -1.f;
		const float D = FMath::Max(FMath::Abs(LP.Y) - 4.f, 0.f);
		const float A = Ang * (0.8f + 0.4f * VC.G);
		if (bMemb)
		{
			const float Wu = FMath::Clamp(UV.X, 0.f, 1.f);
			const float Wz = Wings::WaveCm * (D / 46.f) * (0.35f + 0.65f * Wu) * FMath::Lerp(Wings::GlideWave, 1.f, FlapOn)
				* FMath::Sin(6.2831853f * (FracF(T * Rate * Wings::WaveRate + Seed * 3.f) - 0.9f * D / 46.f - 0.6f * Wu));
			P.Y = S * (4.f + D * FMath::Cos(A) - Wz * FMath::Sin(A));
			P.Z = LP.Z + D * FMath::Sin(A) + Wz * FMath::Cos(A);
		}
		else
		{
			P.Y = S * (4.f + D * FMath::Cos(A));
			P.Z = LP.Z + D * FMath::Sin(A);
		}
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
	// A four-winged flier's darts, fading as the speck floor grows it.
	if (bFour)
	{
		// The flock's dart, as far in its frame for every member whatever its own size.
		const float Sc = Gd[GroupData::DartSpanM] > 0.f ? Gd[GroupData::DartSpanM] / FMath::Max(Size, 0.01f) : 1.f;
		P += FlierDart(T, Gd) * Sc / (K * K);
	}
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

	/**
	 * Where a herd's legs' walk stands as a turn begins, m: on a grid of 2^-10 m (about a millimetre), at or a hair past
	 * where they have walked to, so HerdData's slot 1 (a turn back to graze carried HerdData::PivotBack on) holds it
	 * exactly, and the turn's end, a whole PivotM on, as exactly where the next turn reads it back from. Rounded to the
	 * float nearest instead, a turn could read back as begun up to half a millimetre before its legs stood there: a foot
	 * put down just as it began would then read as put down during the turn rather than before it, and be drawn half a
	 * stance from where it stood.
	 */
	double TurnStartM(double LegsM)
	{
		constexpr double Grid = 1.0 / 1024.0;
		return FMath::CeilToDouble(LegsM / Grid) * Grid;
	}
}

void SlonkFauna::SampleHerdGround(const FFaunaGroup& G, double RadiusCm, const FVector& GroundPos, TFunctionRef<double(const FVector&)> HeightAt, FHerdGround& Out)
{
	FVector Up, East, North;
	HerdAxes(G, GroundPos, Up, East, North);
	const double Half = FMath::Max(G.GroundHalfCm, 100.0), Sp = Half * 0.5;
	FVector Ground[HerdData::GroundNodes];
	// Twice: its nodes laid on the plane tangent at its place, then on the plane that first read fits (so each is read
	// under about where the material puts it, and what was read before has no say), each about where the radial through
	// its place meets the plane found so far; the ground at each node straight down the radial from it.
	double SlopeE = 0.0, SlopeN = 0.0, Lift = 0.0;
	FVector Z = Up;
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const FVector Z0 = PlaneNormal(Up, East, North, SlopeE, SlopeN);
		const FVector X0 = (East - Z0 * FVector::DotProduct(East, Z0)).GetSafeNormal();
		const FVector Y0 = FVector::CrossProduct(Z0, X0);
		const FVector Mid = GroundPos + Up * Lift;
		double SumXZ = 0.0, SumXX = 0.0, SumYZ = 0.0, SumYY = 0.0, SumZ = 0.0;
		for (int32 M = 0; M < HerdData::GroundNodes; ++M)
		{
			const FIntPoint Ij = GroundNodeAt(M);
			const double X = (Ij.X - 2) * Sp, Y = (Ij.Y - 2) * Sp;
			const FVector Dir = (Mid + X0 * X + Y0 * Y).GetSafeNormal();
			Ground[M] = Dir * (RadiusCm + HeightAt(Dir));
			const double Zn = FVector::DotProduct(Ground[M] - Mid, Z0);
			SumXZ += X * Zn; SumXX += X * X; SumYZ += Y * Zn; SumYY += Y * Y; SumZ += Zn;
		}
		// The plane fitted to them: the nodes lie evenly either way of the middle, so its tilt along each axis and its height
		// there are the least squares' alone.
		const FVector N = (Z0 - X0 * (SumXZ / SumXX) - Y0 * (SumYZ / SumYY)).GetSafeNormal();
		const double Nu = FMath::Max(FVector::DotProduct(N, Up), 0.2);
		SlopeE = -FVector::DotProduct(N, East) / Nu;
		SlopeN = -FVector::DotProduct(N, North) / Nu;
		Z = PlaneNormal(Up, East, North, SlopeE, SlopeN);
		// How high it stands over the ground under the herd's place, along the radial there.
		const FVector OnPlane = Mid + Z0 * (SumZ / HerdData::GroundNodes);
		Lift = FVector::DotProduct(OnPlane - GroundPos, Z) / FMath::Max(FVector::DotProduct(Up, Z), 0.2);
	}
	Out.SlopeE = SlopeE;
	Out.SlopeN = SlopeN;
	Out.PlanePt = GroundPos + Up * Lift;
	for (int32 M = 0; M < HerdData::GroundNodes; ++M)
	{
		Out.Node[M] = (float)FMath::Clamp(FVector::DotProduct(Ground[M] - Out.PlanePt, Z), -HerdData::MaxGroundCm, HerdData::MaxGroundCm);
	}
	Out.HalfCm = Half;
	Out.bValid = true;
}

FVector SlonkFauna::HerdFramePos(const FFaunaGroup& G, double RadiusCm, const FVector& Dir, const FHerdGround& Ground)
{
	const FVector U = Dir.GetSafeNormal();
	if (!Ground.bValid) { return U * RadiusCm; }
	FVector Up, East, North;
	HerdAxes(G, U, Up, East, North);
	const FVector Z = PlaneNormal(Up, East, North, Ground.SlopeE, Ground.SlopeN);
	return U * (FVector::DotProduct(Ground.PlanePt, Z) / FMath::Max(FVector::DotProduct(U, Z), 0.2));
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

FVector SlonkFauna::FloaterVelocity(const FFaunaGroup& G, const FFaunaKindDesc& D, const FPanic& P, double RadiusCm, double Now)
{
	const FVector Path = PathAt(G, RadiusCm, Now).VelCmS;
	if (!P.IsLive(Now)) { return Path; }
	// Its flight's rate from a moment either way (from its startle at the earliest: before it, it was on another flight).
	constexpr double H = 0.05;
	const double A = FMath::Max(Now - H, P.T0), B = Now + H;
	return Path + (PanicOffset(P, D, B) - PanicOffset(P, D, A)) / (B - A);
}

double SlonkFauna::FloaterGroundAt(const FFaunaGroup& G, double Seconds)
{
	double T = FMath::Fmod(Seconds, ClockPeriodSec);
	if (T < 0.0) { T += ClockPeriodSec; }
	return LoopGround(G, G.Phase0 + G.Turn * UE_DOUBLE_TWO_PI * T / FMath::Max(G.PeriodSec, 1.0));
}

FVector SlonkFauna::FloaterShadowFall(const FVector& PosCm, double RadiusCm, double GroundCm, const FVector& SunLocal)
{
	const FVector Sun = SunLocal.GetSafeNormal();
	const double SunUp = FVector::DotProduct(Sun, PosCm.GetSafeNormal());
	const double Over = PosCm.Size() - RadiusCm - GroundCm;
	if (SunUp < 0.05 || Over <= 0.0) { return PosCm; }
	return PosCm - Sun * (Over / SunUp);
}

void SlonkFauna::FloaterGroupData(const FFaunaGroup& G, const FFaunaKindDesc& D, const FPanic& P, double RadiusCm, double Now, const FQuat& Rot, double ClockOffsetSec, float* Group)
{
	// Its fright as a flock's: when a startle squeezed it (slot 1), whether it is live, when it was last startled and when
	// its frightened pulse began to come in.
	FrightData(P, Now, Rot, ClockOffsetSec, Group);
	// The way it drifts through the air in its own frame, m/s, and how fast that changes, as of now on the material's clock,
	// which runs it on from there (the scatter's way, slot 4, unused). The change from a moment either way, but from its
	// latest startle at the earliest while that fright is live: before it, it was on another flight, which this one carries
	// on from (FloaterVelocity before it is its path's alone, so a difference across it would send a false change).
	constexpr double H = 0.1;
	const double Lo = P.IsLive(Now) ? FMath::Max(Now - H, P.T0) : Now - H;
	const FVector V0 = Rot.UnrotateVector(FloaterVelocity(G, D, P, RadiusCm, Now)) / 100.0;
	const FVector Vm = Rot.UnrotateVector(FloaterVelocity(G, D, P, RadiusCm, Lo)) / 100.0;
	const FVector Vp = Rot.UnrotateVector(FloaterVelocity(G, D, P, RadiusCm, Now + H)) / 100.0;
	const FVector Acc = (Vp - Vm) / (Now + H - Lo);
	Group[FloaterData::TrailX] = (float)V0.X;
	Group[FloaterData::TrailY] = (float)V0.Y;
	Group[GroupData::ScatterDirZ] = 0.f;
	Group[FloaterData::TrailRateX] = (float)Acc.X;
	Group[FloaterData::TrailRateY] = (float)Acc.Y;
	Group[FloaterData::ClockT0] = OnMaterialClock(Now, ClockOffsetSec);
}

namespace
{
	/** How far a jelly's bell has contracted in its pulse at a share of it (0 relaxed): quickly, then relaxing slowly. */
	float JellyContract(float X)
	{
		return SmoothStepF(0.f, 0.3f, X) * (1.f - SmoothStepF(0.35f, 1.f, X));
	}
	/** The light running down a jelly at night at a share of its pulse. */
	float JellyFlash(float X)
	{
		return SmoothStepF(0.f, 0.05f, X) * (1.f - SmoothStepF(0.08f, 0.3f, X));
	}
	/** A jelly's fright as its material reads it: how far its frightened pulse has come in, and how hard a startle squeezes
	 *  it now. */
	void JellyFright(float T, const float* Gd, float& OutUrge, float& OutSqueeze)
	{
		using namespace SlonkFauna;
		const float Jpa = FMath::Fmod(T - Gd[FloaterData::PanicT0] + 7201.f, 3600.f) - 1.f;
		const float Joa = FMath::Fmod(T - Gd[FloaterData::OnsetT0] + 7201.f, 3600.f) - 1.f;
		const float Jsa = FMath::Fmod(T - Gd[FloaterData::SqueezeT0] + 7201.f, 3600.f) - 1.f;
		const float Jlive = Gd[FloaterData::Startled] > 0.5f ? 1.f : 0.f;
		OutUrge = FMath::Clamp(Jlive * (1.f - SmoothStepF(20.f, 120.f, Jpa)) * 3.f, 0.f, 1.f) * SmoothStepF(0.f, 0.3f, Joa);
		OutSqueeze = Jlive * SmoothStepF(0.f, 0.4f, Jsa) * (1.f - SmoothStepF(3.f, 7.f, Jsa));
	}
	/** Its bell's ribs and ring canal at a point of it, as its pixel shader draws them. */
	void JellyLines(const FVector2f& UV, float Ribs, float& OutRib, float& OutRing)
	{
		const float Sym = FMath::Clamp(FMath::RoundToFloat(Ribs), 3.f, 16.f);
		const float V = FMath::Clamp(UV.Y, 0.f, 1.f);
		OutRib = 1.f - SmoothStepF(0.035f, 0.06f, FMath::Abs(FracF(UV.X * Sym + 0.5f) - 0.5f));
		OutRing = 1.f - SmoothStepF(0.012f, 0.025f, FMath::Abs(V - 0.93f));
	}
}

FVector3f SlonkFauna::FloaterMotion(const FVector3f& LP, const FLinearColor& VC, const FVector2f& UV, float T, const float* C, const float* Gd, float DistCm, float PxPerRad)
{
	// fauna_material.py's floater branch of WPO_CODE, line for line.
	const float Size = C[Custom::SizeM], Seed = C[Custom::Seed], Phase = C[Custom::Phase], Rate = C[Custom::RateHz];
	float Jurge = 0.f, Jsq = 0.f;
	JellyFright(T, Gd, Jurge, Jsq);
	const float Jtent = VC.R > 0.25f ? 1.f : 0.f;
	const float Jdown = Jtent > 0.5f ? 1.f + VC.G : VC.G;
	const float Jpc = FracF(T * Rate + Phase - 0.12f * Jdown);
	const float Jpf = FracF(T * Rate * 2.f + Phase - 0.12f * Jdown);
	const float Jc = FMath::Max(FMath::Lerp(0.16f * JellyContract(Jpc), 0.24f * JellyContract(Jpf), Jurge), 0.34f * Jsq);
	const float Jrc = FracF(T * Rate + Phase - 0.12f);
	const float Jrf = FracF(T * Rate * 2.f + Phase - 0.12f);
	const float Jr = FMath::Max(FMath::Lerp(0.16f * JellyContract(Jrc), 0.24f * JellyContract(Jrf), Jurge), 0.34f * Jsq);
	FVector3f Jp = LP;
	if (Jtent > 0.5f)
	{
		const float S = 1.f - Jr * (1.f - 0.5f * VC.G);
		Jp.X *= S;
		Jp.Y *= S;
		Jp.Z -= 20.f * Jr;
	}
	else
	{
		const float Jg2 = VC.G * VC.G;
		const float S = 1.f - Jc * Jg2;
		Jp.X *= S;
		Jp.Y *= S;
		Jp.Z -= 20.f * Jc * Jg2;
	}
	const float Jal = FMath::Max(UV.Y - 1.f, 0.f) * 100.f;
	const float Jamp = Jal * VC.G * Jtent;
	const float Jrl = FMath::Sqrt(LP.X * LP.X + LP.Y * LP.Y);
	const FVector2f Jout = Jrl > 0.001f ? FVector2f(LP.X, LP.Y) / Jrl : FVector2f(1.f, 0.f);
	const FVector2f Jacr(-Jout.Y, Jout.X);
	const float Jw1 = 540.f + FMath::FloorToFloat(FracF(Seed * 4.37f + VC.B * 7.13f) * 360.f);
	const float Jw2 = 420.f + FMath::FloorToFloat(FracF(Seed * 2.91f + VC.B * 5.77f) * 300.f);
	const float Js1 = FMath::Sin(6.2831853f * (FracF(T * (Jw1 / 3600.f) + VC.B * 3.7f + Seed) - 1.5f * VC.G));
	const float Js2 = FMath::Sin(6.2831853f * (FracF(T * (Jw2 / 3600.f) + VC.B * 1.9f + Seed * 3.f) - 1.1f * VC.G));
	const FVector2f Jwave = (Jacr * (0.07f * Js1) + Jout * (0.035f * Js2)) * Jamp;
	const float Jta = FMath::Clamp(FMath::Fmod(T - Gd[FloaterData::ClockT0] + 7201.f, 3600.f) - 1.f, -1.f, 1.f);
	const FVector2f Jvel = FVector2f(Gd[FloaterData::TrailX], Gd[FloaterData::TrailY]) + FVector2f(Gd[FloaterData::TrailRateX], Gd[FloaterData::TrailRateY]) * Jta;
	const float Jspd = Jvel.Size();
	const FVector2f Jbend = Jspd > 0.001f ? Jvel * (-0.4f * FMath::Min(Jspd / 6.f, 1.f) / Jspd) * Jamp : FVector2f(0.f, 0.f);
	const float Jlift = 0.6667f * FVector2f::DotProduct(Jbend, Jbend) / FMath::Max(Jal, 1.f);
	const FVector3f Jm(Jwave.X + Jbend.X, Jwave.Y + Jbend.Y, Jlift + 4.f * Jc);
	const float Jpx = FMath::Max(Size, 1.f) * 100.f * PxPerRad / FMath::Max(DistCm, 1.f);
	const float Jk = FMath::Clamp(4.f / FMath::Max(Jpx, 0.001f), 1.f, 2.f);
	return Jp * Jk + Jm * (2.f - Jk) - LP;
}

bool SlonkFauna::FloaterCover(const FVector2f& UV, const FLinearColor& VC, const float* C)
{
	// fauna_material.py's floater branch of CODE, its membrane, line for line: a pore in each cell of the bell's own
	// surface, none on its ribs, its ring canal or its crown.
	float Rib = 0.f, Ring = 0.f;
	JellyLines(UV, C[Custom::PerKind], Rib, Ring);
	const bool bBell = VC.R < 0.25f;
	const float V = FMath::Clamp(UV.Y, 0.f, 1.f);
	const float Vr = FMath::Min(V, 0.9999f);
	const float Row = FMath::FloorToFloat(Vr * 16.f);
	const float Nu = 16.f * FMath::Max(FMath::RoundToFloat(4.f * FMath::Sin((Row + 0.5f) / 16.f * 1.5707963f)), 1.f);
	const FVector2f Cl(FracF(UV.X) * Nu, Vr * 16.f);
	const FVector2f Cf(FMath::FloorToFloat(Cl.X), FMath::FloorToFloat(Cl.Y));
	uint32 H = ((uint32)Cf.X * 73856093u) ^ ((uint32)Cf.Y * 19349663u) ^ ((uint32)(C[Custom::Seed] * 65536.f) * 83492791u);
	H = (H ^ (H >> 13)) * 0x5bd1e995u;
	H ^= H >> 15;
	const FVector2f Ctr = FVector2f(0.5f, 0.5f) + (FVector2f((float)(H & 0xffu), (float)((H >> 8) & 0xffu)) / 255.f - FVector2f(0.5f, 0.5f)) * 0.2f;
	float Cover = V < 0.28f ? 1.f : FMath::Lerp(0.6f, 0.38f, FMath::Clamp((V - 0.28f) / 0.62f, 0.f, 1.f));
	Cover = FMath::Max(Cover, FMath::Max(Rib, Ring));
	const float Open = FMath::Sqrt(FMath::Clamp(1.f - Cover, 0.f, 1.f) / 3.14159265f);
	const bool bPore = bBell && (Cl - Cf - Ctr).Size() < Open;
	return !bPore;
}

float SlonkFauna::FloaterLight(const FVector2f& UV, const FLinearColor& VC, float T, const float* C, const float* Gd)
{
	// fauna_material.py's floater branch of CODE, its light, line for line.
	float Rib = 0.f, Ring = 0.f;
	JellyLines(UV, C[Custom::PerKind], Rib, Ring);
	const bool bBell = VC.R < 0.25f;
	const float V = FMath::Clamp(UV.Y, 0.f, 1.f);
	float Jurge = 0.f, Jsq = 0.f;
	JellyFright(T, Gd, Jurge, Jsq);
	const float Phase = C[Custom::Phase], Rate = C[Custom::RateHz];
	const float Jdown = bBell ? V : 1.f + VC.G;
	const float Jxc = FracF(T * Rate + Phase - 0.3f * Jdown);
	const float Jxf = FracF(T * Rate * 2.f + Phase - 0.3f * Jdown);
	const float Jwave = FMath::Lerp(JellyFlash(Jxc), JellyFlash(Jxf), Jurge);
	const float Jlines = bBell ? FMath::Max(Rib * (V >= 0.1f ? 1.f : 0.f), Ring) : 1.f;
	return Jlines * (0.2f + 1.8f * Jwave + 1.5f * Jsq);
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

double SlonkFauna::CrewRoomCm(const FFaunaGroup& G)
{
	if (G.DryCm >= HerdDryReadCm) { return 3.0e4; }
	return FMath::Clamp(G.DryCm - G.HerdRadiusCm - HerdCrewDryCm, 0.0, 3.0e4);
}

bool SlonkFauna::KeepFromCrew(FHerdState& S, const FFaunaGroup& G, const FFaunaKindDesc& D, const TArray<FVector>& CrewPosCm, double Dt, FHerdWater* Water)
{
	if (!S.bStarted || Dt <= 0.0) { return false; }
	const FVector Up = S.FramePos.GetSafeNormal();
	auto Level = [&Up](const FVector& V) { return V - Up * FVector::DotProduct(V, Up); };
	const double Keep = D.KeepFromCrewCm;
	const double Room = CrewRoomCm(G);
	// Whether the ground its edge would walk onto going a way, ahead and 40 degrees to either side, is dry.
	auto DryAhead = [&](const FVector& Way)
	{
		if (!Water) { return true; }
		const FVector Fwd = Level(Way).GetSafeNormal(), Side = FVector::CrossProduct(Up, Fwd);
		const double Reach = G.HerdRadiusCm + HerdCrewDryCm;
		for (const double A : { 0.0, 0.7, -0.7 })
		{
			if (Water->WetAt(G, S.FramePos + (Fwd * FMath::Cos(A) + Side * FMath::Sin(A)) * Reach)) { return false; }
		}
		return true;
	};
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
		const bool bGo = bOnTheMove && FVector::DotProduct(Way.GetSafeNormal(), Facing) > FMath::Cos(TurnAroundRad) && DryAhead(Way);
		S.AvoidPace = FMath::Clamp(S.AvoidPace + (bGo ? Dt : -2.0 * Dt), 0.0, 1.0);
		// It walks the way it wants once it may; easing off, on along the way it was walking, so it stops going forward
		// (its legs walking it down) and turns about only once it stands.
		if (bGo || S.AvoidPace <= 0.0) { S.AvoidGoing = Way; }
		FVector Go = S.AvoidGoing * (S.AvoidPace * Dt);
		// Going further off its path, it slows as it comes to the most it may go (at half a metre a second each second, so
		// it comes to a stand there rather than stopping dead).
		const double Out = FVector::DotProduct(Go, S.Avoid.GetSafeNormal());
		if (Out > 0.0 && Go.Size() > 0.0)
		{
			const double MostCmS = FMath::Sqrt(2.0 * 50.0 * FMath::Max(Room - S.Avoid.Size(), 0.0));
			Go *= FMath::Min(1.0, MostCmS * Dt / Go.Size());
		}
		S.Avoid = (S.Avoid + Go).GetClampedToMaxSize(Room);
	};
	if (!Push.IsNearlyZero())
	{
		// Off at a walk, at a trot for anyone close in; never further than 300 m off its path, nor than its loop's dry
		// ground leaves it room for.
		S.AvoidWay = Push.GetSafeNormal() * (Nearest < Keep * 0.6 ? 260.0 : 140.0);
		Pace(S.AvoidWay);
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

void SlonkFauna::StepHerd(FHerdState& S, const FFaunaGroup& G, double RadiusCm, double Now, double StepDt, const FVector& OffsetCm, bool bShaken, bool bMoving, bool bRefreshGround,
	TFunctionRef<double(const FVector&)> HeightAt, double ClockOffsetSec, float* OutData, const FVector* OffsetAheadCm)
{
	// The time this step spans: since its last step (the caller holds a long frame's step short, but its path has run on
	// the whole of it, so its speeds are read over that), else the step asked (its last step not known, or a second or
	// more ago: the clock jumped).
	const double Since = Now - S.LastStepSec;
	const double Dt = StepDt > 0.0 && Since > StepDt * 1.001 && Since <= 1.0 ? Since : StepDt;
	const FFaunaPose Pose = PathAt(G, RadiusCm, Now);
	// Its place on its path, off it by its fright and the crew: the drawn ground under it read again as asked (and the
	// first time), and its frame set where the radial there meets the plane fitted to that ground, which rises and falls
	// with the ground under the whole herd rather than with whatever bump is under its middle.
	const FVector Dir = (Pose.PosCm + OffsetCm).GetSafeNormal();
	if (bRefreshGround || !S.Ground.bValid) { SampleHerdGround(G, RadiusCm, Dir * (RadiusCm + HeightAt(Dir)), HeightAt, S.Ground); }
	const FVector Pos = HerdFramePos(G, RadiusCm, Dir, S.Ground);
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
	const double Rate = HerdTurnRateOf(G, bShaken);
	const double Travel1 = bStep ? FMath::Clamp(Travel0 + FMath::Clamp(Want - Travel0, -Rate * Dt, Rate * Dt), 0.0, 1.0) : Travel0;
	// Its way: set at once while it grazes (no walker faces it then), turned as it walks, and held while its walkers turn
	// to walk or back to graze (so each of them turns the same way all through its turn). As it walks it turns by how
	// sharply its way turned over the step (WalkKap, as the material ran it on) for how far it went ahead; how sharply it
	// turns from here is steered toward the way it goes (as sharply as that way is turning, and more to close what is
	// left of the gap over SteerM, or as sharply as it can still straighten out by the time it faces it if that is less),
	// no more than MaxWalkKap a metre nor a few radians a second, and
	// changes by no more than its herd's KapRate a second (Walker::MaxKapRate for the Earth's grazers): a planted hoof is
	// carried round by how sharply its body turns now.
	if (!S.bStarted)
	{
		// Come about on the move along its path alone, it faces the way its path goes now and turns as sharply as that way
		// turns; taken off it as it comes (its fright or the crew moving it), it faces the way it goes (Going).
		const FVector OffsetGoes = (OffsetAheadCm ? *OffsetAheadCm : OffsetCm) - OffsetCm;
		if (Travel0 >= 1.0 && Pose.VelCmS.Size() > 10.0 && OffsetGoes.Size() < 1.0)
		{
			Target = AngleOf(LiftOnPlane(Pose.VelCmS));
			const FVector Was = LiftOnPlane(PathAt(G, RadiusCm, Now - 0.5).VelCmS), Will = LiftOnPlane(PathAt(G, RadiusCm, Now + 0.5).VelCmS);
			S.WalkKap = FMath::Clamp(FMath::UnwindRadians(AngleOf(Will) - AngleOf(Was)) / FMath::Max(Pose.VelCmS.Size() / 100.0, 0.1), -Walker::MaxWalkKap, Walker::MaxWalkKap);
		}
		S.Heading = Target; S.TurnHeading = Target; S.LastTurnHeading = Target;
	}
	else if (Travel0 <= 0.0) { if (bTarget) { S.Heading += Off; } }
	else if (bStep && Travel0 >= 1.0)
	{
		// Turned on by how sharply its way turned over the step, as the material ran it on, on every step from a walk (a
		// turn back to graze beginning on it too: its walkers are turned from where they were drawn); steered on only while
		// it keeps walking.
		const double AheadM = Ahead / 100.0;
		S.Heading += S.WalkKap * AheadM;
		if (Travel1 >= 1.0)
		{
			const double V = FMath::Max(AheadM / Dt, 0.1);
			const double Left = bTarget ? FMath::UnwindRadians(Target - S.Heading) : 0.0;
			const double Along = bTarget && S.bTargetWas && AheadM > 0.005 ? FMath::Clamp(FMath::UnwindRadians(Target - S.TargetWas) / AheadM, -Walker::MaxWalkKap, Walker::MaxWalkKap) : 0.0;
			const double Cap = FMath::Min(Walker::MaxWalkKap, (bShaken ? 2.5 : 1.5) / V);
			const double KapRate = HerdKapRate(G);
			const double Close = FMath::Min(FMath::Abs(Left) / Walker::SteerM, FMath::Sqrt(2.0 * KapRate * FMath::Abs(Left) / V));
			const double Steer = FMath::Clamp(Along + FMath::Sign(Left) * Close, -Cap, Cap);
			const double Most = KapRate * Dt;
			S.WalkKap = FMath::Clamp(S.WalkKap + FMath::Clamp(Steer - S.WalkKap, -Most, Most), -Walker::MaxWalkKap, Walker::MaxWalkKap);
		}
	}
	S.TargetWas = Target;
	S.bTargetWas = bTarget && Travel0 >= 1.0;
	S.Heading = FMath::UnwindRadians(S.Heading);
	// A new turn begins: what came between it and the last (the other way), so a walker's feet still down from the last are
	// carried through the last, that and this one: to walk, the grazing clock the last stop left it at (it has grazed
	// since by its own steps); back to graze, how far its legs have walked since it last set off. Its way through each.
	const bool bNewTurn = Travel1 != Travel0 && (Travel0 <= 0.0 || Travel0 >= 1.0);
	// It begins where its legs have walked to after this step's walk ahead, on the grid its turns are kept on.
	const double NewStartM = bNewTurn ? TurnStartM(S.DistM + Ahead / 100.0) : 0.0;
	if (bNewTurn)
	{
		// A walk sets off straight (no foot is carried by how sharply it turns until its walkers have turned to it).
		if (Travel1 > Travel0) { S.SinceLastTurn = S.GrazeRef; S.WalkKap = 0.0; }
		else { S.SinceLastTurn = FMath::Clamp(NewStartM - (S.PivotStartM + Walker::PivotM), 0.0, 1000.0); }
		S.LastTurnHeading = S.TurnHeading;
		S.TurnHeading = S.Heading;
	}
	// Its legs walked on by how far it went ahead and by its walkers' steps as they turn (PivotM a whole turn), wrapped
	// to its loop (its last turn's start carried back with it); its grazing clock by the time it stood grazing (none on a
	// step that sets off). Partway through a turn its legs walk its steps alone: its walkers step round where they stand
	// (the material carries a foot put down then by the turn's steps, none of them ahead), and the turn stays begun where
	// it began, so no foot put down about then is read as put down the other side of it. Any way its frame still goes then
	// (the last of easing off as it turns about) its feet slide by, slower than it walks.
	const double LoopM = FMath::Max(G.LengthCm / 100.0, 1.0);
	const double Pivot = Walker::PivotM * FMath::Abs(Travel1 - Travel0);
	const double WalkedM = Travel0 > 0.0 && Travel0 < 1.0 ? 0.0 : Ahead / 100.0;
	S.DistM = bNewTurn ? NewStartM + Pivot : S.DistM + WalkedM + Pivot;
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
	S.GaitMS = bStep ? (bNewTurn ? 0.0 : WalkedM / Dt) + (bTurning ? Pivot / Dt : 0.0) : (Travel1 >= 1.0 ? S.SpeedMS : 0.0);
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
	S.LastStepSec = Now;
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

FVector3f SlonkFauna::WalkerMotion(const FVector3f& LP, const FVector3f& LPP, const FLinearColor& VC, float T, const float* C, const float* Gd, float DistCm, float PxPerRad,
	const Walker::FRig& Rig)
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
			Zf = Rig.LiftCm * FMath::Sin(3.14159265f * Q) * SmoothStepF(0.03f, 0.25f, W.SpeedMS);
		}
		P.X += Foot.X * VC.G;
		P.Y += Foot.Y * VC.G;
		P.Z += Zf * VC.G;
	}
	else if (Va > 0.3f && Va < 0.45f)
	{
		// The neck and head, turned about the neck's root (its plan's), the root itself held to the body, by its plan's share
		// of the Earth grazer's swing.
		const float A = Pitch * Rig.PitchScale * SmoothStepF(0.f, 0.3f, VC.G);
		const float Rx = P.X - Rig.NeckX, Rz = P.Z - Rig.NeckZ;
		const float Ca = FMath::Cos(A), Sa = FMath::Sin(A);
		P.X = Rig.NeckX + Rx * Ca - Rz * Sa;
		P.Z = Rig.NeckZ + Rx * Sa + Rz * Ca;
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

namespace
{
	/** The leaps' hash (fauna_material.py's leaper branch, uint for uint): a cycle's draw K for a fish's seed. */
	uint32 LeapHash(uint32 N, uint32 Seed, uint32 K)
	{
		uint32 X = N * 0x9E3779B1u + Seed * 0x85EBCA77u + K * 0xC2B2AE3Du + 0x27D4EB2Fu;
		X ^= X >> 16;
		X *= 0x7FEB352Du;
		X ^= X >> 15;
		X *= 0x846CA68Bu;
		X ^= X >> 16;
		return X;
	}
	/** Its top 24 bits as a unit value, exactly as a float holds them. */
	float LeapU(uint32 X) { return (float)(X >> 8) * (1.f / 16777216.f); }
	/** A fish's place in its group's order and the group's count, from its custom float (8 times the one plus the other). */
	void LeaperOrder(float PerKind, int32& OutIndex, int32& OutCount)
	{
		const float Idx = FMath::FloorToFloat((PerKind + 0.5f) / 8.f);
		OutIndex = (int32)Idx;
		OutCount = FMath::Max((int32)(PerKind - Idx * 8.f), 1);
	}
}

SlonkFauna::FLeap SlonkFauna::LeapOf(const float* Custom, int64 Cycle)
{
	// fauna_material.py's leaper branch: the cycle's place in the hour (its hash's), its draws, and the leap they make.
	FLeap L;
	L.Cycle = Cycle;
	const float Size = FMath::Max(Custom[Custom::SizeM], 0.1f);
	const float Cyc = FMath::Max(Custom[Custom::RateHz], 1.f);
	int32 Idx, Count;
	LeaperOrder(Custom[Custom::PerKind], Idx, Count);
	const int64 PerHour = FMath::Max<int64>(FMath::RoundToInt64(ClockPeriodSec / Cyc), 1);
	const int64 Wrapped = ((Cycle % PerHour) + PerHour) % PerHour;
	const uint32 SeedBits = (uint32)(Custom[Custom::Seed] * 65536.f);
	auto U = [&](uint32 K) { return LeapU(LeapHash((uint32)Wrapped, SeedBits, K)); };
	L.bLeaps = U(0) >= Leaper::SkipShare;
	const float J = Leaper::SlotLo + Leaper::SlotSpan * U(1);
	L.Yaw = (U(2) - 0.5f) * Leaper::YawSpan;
	const float Len = Size * 100.f;
	L.FromCm = FVector2D((2.f * U(3) - 1.f) * Leaper::Jitter * Len, (2.f * U(4) - 1.f) * Leaper::Jitter * Len);
	L.ApexCm = (Leaper::ApexLo + Leaper::ApexSpan * U(5)) * Len;
	L.RangeCm = (Leaper::RangeLo + Leaper::RangeSpan * U(6)) * Len;
	L.FlightSec = 2.0 * FMath::Sqrt(2.0 * L.ApexCm / Leaper::GravityCmS2);
	// How long it is drawn under the surface either side: until its middle is LeadDepth of its lengths down, at the speed it
	// crosses the surface.
	const double Rise = 4.0 * L.ApexCm / L.FlightSec;
	L.LeadSec = Leaper::LeadDepth * Len / Rise;
	L.T0 = ((double)Cycle + (double)Custom[Custom::Phase]) * Cyc + (double)(J * (Cyc / (float)Count));
	return L;
}

SlonkFauna::FLeap SlonkFauna::LeapAt(const float* Custom, double Seconds)
{
	const double Cyc = FMath::Max((double)Custom[Custom::RateHz], 1.0);
	return LeapOf(Custom, (int64)FMath::FloorToDouble(Seconds / Cyc - (double)Custom[Custom::Phase]));
}

SlonkFauna::FLeapPose SlonkFauna::LeapPoseAt(const FLeap& L, double H0, double H1, double T)
{
	FLeapPose P;
	const double Tf = FMath::Max(L.FlightSec, 1e-3);
	const double S = (T - L.T0) / Tf;
	H0 = FMath::Clamp(H0, -(double)Leaper::MaxSurfaceCm, (double)Leaper::MaxSurfaceCm);
	H1 = FMath::Clamp(H1, -(double)Leaper::MaxSurfaceCm, (double)Leaper::MaxSurfaceCm);
	// A parabola under gravity from the surface where it leaves to the surface where it dives back in, its apex ApexCm
	// over the line between them, run on either side of them under the water.
	const FVector2D Way(FMath::Cos(L.Yaw), FMath::Sin(L.Yaw));
	const FVector2D Xy = L.FromCm + Way * (L.RangeCm * S);
	P.PosCm = FVector(Xy.X, Xy.Y, FMath::Lerp(H0, H1, S) + 4.0 * L.ApexCm * S * (1.0 - S));
	P.Pitch = FMath::Atan2((H1 - H0) / Tf + 4.0 * L.ApexCm * (1.0 - 2.0 * S) / Tf, L.RangeCm / Tf);
	P.Share = S;
	return P;
}

bool SlonkFauna::LeapDived(const FPanic& Dive, double T0)
{
	if (Dive.T0 < -1.0e29) { return false; }
	const double Len = FMath::Clamp(Dive.T0 + Leaper::DiveSec - Dive.ScatterT0, 0.0, 3000.0);
	return T0 >= Dive.ScatterT0 && T0 < Dive.ScatterT0 + Len;
}

void SlonkFauna::LeapEventsBetween(const float* Custom, int32 Member, double FromSec, double ToSec, TArray<FLeapEvent>& Out)
{
	if (ToSec <= FromSec) { return; }
	const double Cyc = FMath::Max((double)Custom[Custom::RateHz], 1.0);
	const double Phase = (double)Custom[Custom::Phase];
	// Every cycle whose leap could leave or dive back in within the span (a leap is out of the water well within its cycle).
	const int64 First = (int64)FMath::FloorToDouble(FromSec / Cyc - Phase) - 1, Last = (int64)FMath::FloorToDouble(ToSec / Cyc - Phase);
	for (int64 N = First; N <= Last; ++N)
	{
		const FLeap L = LeapOf(Custom, N);
		if (!L.bLeaps) { continue; }
		for (const bool bEntry : { false, true })
		{
			const double At = bEntry ? L.T1() : L.T0;
			if (At > FromSec && At <= ToSec)
			{
				FLeapEvent E;
				E.Member = Member;
				E.bEntry = bEntry;
				E.TimeSec = At;
				E.Leap = L;
				Out.Add(E);
			}
		}
	}
}

FVector SlonkFauna::LeaperFramePoint(const FFaunaGroup& G, double RadiusCm, double WorldSec, const FVector& FrameCm)
{
	const FFaunaPose Pose = LeaperPathAt(G, RadiusCm, WorldSec);
	return Pose.PosCm + Pose.Rot.RotateVector(FrameCm);
}

FVector SlonkFauna::LeapCrossing(const FFaunaGroup& G, double RadiusCm, double SwellCm, double Fade, const FVector& HomeCm, const FLeap& L, bool bEntry, double* OutSurfaceCm)
{
	const double At = bEntry ? L.T1() : L.T0;
	const FVector2D Off = bEntry ? L.ToCm() : L.FromCm;
	const FVector Pt = LeaperFramePoint(G, RadiusCm, At, HomeCm + FVector(Off.X, Off.Y, 0.0));
	const double H = FMath::Clamp(SeaSurfaceCm(Pt, RadiusCm, At, SwellCm, Fade), -(double)Leaper::MaxSurfaceCm, (double)Leaper::MaxSurfaceCm);
	if (OutSurfaceCm) { *OutSurfaceCm = H; }
	return Pt.GetSafeNormal() * (RadiusCm + H);
}

FVector SlonkFauna::LeaperWatchDir(const FFaunaGroup& G, double RadiusCm, double WorldSec, double AltCm, double* OutSideCm)
{
	const FFaunaKindDesc& D = KindDesc(EFaunaKind::Leaper);
	const double SideCm = AltCm < D.ScareAltitudeCm * 1.2 ? 1.6 * D.ScareRadiusCm : FMath::Max(0.6 * AltCm, 3000.0);
	if (OutSideCm) { *OutSideCm = SideCm; }
	const FFaunaPose Here = LeaperPathAt(G, RadiusCm, WorldSec);
	// Out from the loop's middle through where the shoal is now, along the sea.
	const FVector Mid = G.Centre * RadiusCm;
	const FVector Up = G.Centre;
	FVector Out = Here.PosCm - Mid;
	Out = (Out - Up * FVector::DotProduct(Out, Up)).GetSafeNormal();
	if (Out.IsNearlyZero()) { Out = (Here.Rot.GetAxisY() - Up * FVector::DotProduct(Here.Rot.GetAxisY(), Up)).GetSafeNormal(); }
	// The loop is a circle on its plane of the projection, lifted onto the sphere: stretched there by up to the radial's
	// lean off the plane's axis, so on the sea it reaches no further from its middle than its radius over that.
	const double Reach = G.RadiusCm / FMath::Max(G.Centre.GetAbs().GetMax(), 0.5) * 1.02;
	return (Mid + Out * (Reach + SideCm)).GetSafeNormal();
}

bool SlonkFauna::StartleLeapers(FPanic& Dive, const FFaunaKindDesc& D, const FVector& GroupPosCm, const FFaunaThreat& T, double Now, bool bForce)
{
	const bool bWasLive = Dive.IsLive(Now);
	if (!bForce)
	{
		// A craft passing low over it, no one on foot (on a shore, or wading in), and not again within a re-startle's while.
		if (!T.bCraft || !Scares(D, GroupPosCm, T)) { return false; }
		if (bWasLive && Now - Dive.T0 < RestartleSec) { return false; }
	}
	// While its dive lasts a startle carries it on from where it began, adding to it only leaps not yet in sight (none
	// leaves the water from the window's end on, and none comes into sight on its way up until DiveLeadSec before it
	// leaves). Once the window has closed, or is too near closing for that, its fish leap again: a startle then waits until
	// every leap the dive kept under is past its time back in the water (moving the window off them then shows none of
	// them in the air), forced or not.
	const double End = Dive.T0 + Leaper::DiveSec;
	const bool bDiving = bWasLive && Now < End - Leaper::DiveLeadSec;
	if (bWasLive && !bDiving && Now < End + Leaper::HeldUnderSec) { return false; }
	// A new dive keeps under the leaps leaving from DiveLeadSec on, past those already on their way up. One kept up half an
	// hour runs from ten seconds back (every leap before that is long back in the water), so the material's hour never
	// wraps on it.
	double Began = bDiving ? Dive.ScatterT0 : Now + Leaper::DiveLeadSec;
	if (Now - Began > 1800.0) { Began = Now - 10.0; }
	Dive.T0 = Now;
	Dive.LiveSec = D.FrightSec;
	Dive.ScatterT0 = Began;
	Dive.FleeT0 = Now;
	Dive.Dir = FVector::ZeroVector;
	Dive.Carry = FVector::ZeroVector;
	Dive.CarryVel = FVector::ZeroVector;
	return true;
}

void SlonkFauna::LeaperDiveData(const FPanic& Dive, double Now, float* Group)
{
	const bool bLive = Dive.IsLive(Now);
	Group[LeaperData::DiveT0] = bLive ? OnMaterialClock(Dive.ScatterT0, 0.0) : 0.f;
	Group[LeaperData::Diving] = bLive ? 1.f : 0.f;
	Group[LeaperData::DiveLen] = bLive ? (float)FMath::Clamp(Dive.T0 + Leaper::DiveSec - Dive.ScatterT0, 0.0, 3000.0) : 0.f;
}

SlonkFauna::FLeap SlonkFauna::LeapToRead(const float* Custom, double WorldSec)
{
	const FLeap L = LeapAt(Custom, WorldSec);
	return WorldSec > L.T1() + L.LeadSec + 1.0 ? LeapOf(Custom, L.Cycle + 1) : L;
}

void SlonkFauna::LeaperGroupData(const FFaunaGroup& G, TConstArrayView<float> Custom, TConstArrayView<FVector> Homes, double RadiusCm, double SwellCm, double Fade, double WorldSec,
	const FPanic& Dive, FLeaperSurfaceCache& Cache, float* Group)
{
	LeaperDiveData(Dive, WorldSec, Group);
	const int32 N = FMath::Min3(Homes.Num(), Custom.Num() / Custom::Num, Leaper::MaxPerGroup);
	for (int32 i = 0; i < N; ++i)
	{
		const float* C = Custom.GetData() + i * Custom::Num;
		const FLeap L = LeapToRead(C, WorldSec);
		if (L.Cycle != Cache.Cycle[i])
		{
			Cache.Cycle[i] = L.Cycle;
			LeapCrossing(G, RadiusCm, SwellCm, 1.0, Homes[i], L, false, &Cache.Surface[i][0]);
			LeapCrossing(G, RadiusCm, SwellCm, 1.0, Homes[i], L, true, &Cache.Surface[i][1]);
		}
		for (int32 k = 0; k < 2; ++k)
		{
			Group[LeaperData::Surface0 + 2 * i + k] = (float)FMath::Clamp(Cache.Surface[i][k] * Fade, -(double)Leaper::MaxSurfaceCm, (double)Leaper::MaxSurfaceCm);
		}
	}
}

namespace
{
	/** The leaper branch's reading of a fish's leap at the material's time, in floats as the GPU runs it (both of the
	 *  material's shaders read it so). */
	struct FLeaperRead
	{
		float Size = 1.f, Tau = 0.f, Tau0 = 0.f, Tf = 1.f, Lead = 0.f, Yaw = 0.f, Fx = 0.f, Fy = 0.f, Apex = 0.f, Range = 0.f, HA = 0.f, HB = 0.f;
		bool bShown = false;
	};
	FLeaperRead ReadLeap(float T, const float* C, const float* Gd)
	{
		FLeaperRead R;
		const float S = FMath::Max(C[SlonkFauna::Custom::SizeM], 0.1f);
		const float Cyc = FMath::Max(C[SlonkFauna::Custom::RateHz], 1.f);
		const float Bearing = C[SlonkFauna::Custom::PerKind];
		const float Idx = FMath::FloorToFloat((Bearing + 0.5f) / 8.f);
		const float Cnt = FMath::Max(Bearing - Idx * 8.f, 1.f);
		const float U = T / Cyc - C[SlonkFauna::Custom::Phase];
		const float N = FMath::FloorToFloat(U);
		const float M = FMath::RoundToFloat(3600.f / Cyc);
		const float Nw = N - M * FMath::FloorToFloat(N / M);
		R.Tau = (U - N) * Cyc;
		const uint32 Sd = (uint32)(C[SlonkFauna::Custom::Seed] * 65536.f);
		const uint32 Nu = (uint32)Nw;
		auto Uk = [&](uint32 K) { return LeapU(LeapHash(Nu, Sd, K)); };
		const bool bLeaps = Uk(0) >= 0.25f;
		const float J = 0.15f + 0.45f * Uk(1);
		R.Tau0 = J * (Cyc / Cnt);
		R.Yaw = (Uk(2) - 0.5f) * 1.4f;
		const float Len = S * 100.f;
		R.Fx = (2.f * Uk(3) - 1.f) * 1.2f * Len;
		R.Fy = (2.f * Uk(4) - 1.f) * 1.2f * Len;
		R.Apex = (0.7f + 1.1f * Uk(5)) * Len;
		R.Range = (2.f + 2.5f * Uk(6)) * Len;
		R.Tf = 2.f * FMath::Sqrt(2.f * R.Apex / 981.f);
		R.Lead = 0.6f * Len / (4.f * R.Apex / R.Tf);
		// A dive keeps a leap leaving from when it began to its length past that in the water.
		const float T0m = T + (R.Tau0 - R.Tau);
		const float DiveAge = FMath::Fmod(T0m - Gd[SlonkFauna::LeaperData::DiveT0] + 7201.f, 3600.f) - 1.f;
		const bool bDived = Gd[SlonkFauna::LeaperData::Diving] > 0.5f && DiveAge >= 0.f && DiveAge < Gd[SlonkFauna::LeaperData::DiveLen];
		R.bShown = bLeaps && !bDived && R.Tau >= R.Tau0 - R.Lead && R.Tau <= R.Tau0 + R.Tf + R.Lead;
		const int32 I = FMath::Clamp((int32)Idx, 0, SlonkFauna::Leaper::MaxPerGroup - 1);
		R.HA = FMath::Clamp(Gd[SlonkFauna::LeaperData::Surface0 + 2 * I], -150.f, 150.f);
		R.HB = FMath::Clamp(Gd[SlonkFauna::LeaperData::Surface0 + 2 * I + 1], -150.f, 150.f);
		R.Size = S;
		return R;
	}
}

float SlonkFauna::GlowSpotAt(const FVector2f& UV, const FLinearColor& VC, const GlowOrgan::FRow& Row)
{
	// fauna_material.py's CODE, its glow organs' block, line for line.
	if (Row.Spots <= 0) { return 0.f; }
	const float Spots = (float)Row.Spots;
	const float Gw = FMath::Max(Row.U1 - Row.U0, 1e-3f);
	const float Gi = FMath::Clamp(FMath::FloorToFloat((UV.X - Row.U0) / Gw * Spots), 0.f, Spots - 1.f);
	const FVector2f Gd((UV.X - (Row.U0 + (Gi + 0.5f) * Gw / Spots)) / FMath::Max(Row.RU, 1e-4f), (UV.Y - Row.V) / FMath::Max(Row.RV, 1e-4f));
	const float Body = (VC.R < 0.2f && (VC.A < 0.1f || VC.A > 0.9f)) ? 1.f : 0.f;
	return Body * (1.f - SmoothStepF(0.75f, 1.f, Gd.Size()));
}

float SlonkFauna::GlowBreath(float T, const float* C)
{
	const float Seed = C[Custom::Seed];
	const float N = GlowOrgan::BreathLo + FMath::FloorToFloat(FracF(Seed * 3.3f) * GlowOrgan::BreathSpan);
	return GlowOrgan::Breath + (1.f - GlowOrgan::Breath) * (0.5f + 0.5f * FMath::Sin(6.2831853f * FracF(T * (N / 3600.f) + Seed * 2.1f)));
}

float SlonkFauna::GlowSpeckDim(EFaunaKind Kind, float SizeM, float DistCm, float PxPerRad, int32 WingForm, float ShareOfLeap)
{
	// fauna_material.py's CODE, each branch's light of its glow organs: over the square of how many times its size the
	// vertex shader's speck floor draws the animal there.
	const float D = FMath::Max(DistCm, 1.f);
	float K = 1.f;
	if (Kind == EFaunaKind::Herd) { K = FMath::Clamp(Walker::SpeckPx / FMath::Max(FMath::Max(SizeM, 0.1f) * 100.f * PxPerRad / D, 0.001f), 1.f, Walker::SpeckMax); }
	else if (Kind == EFaunaKind::Leaper)
	{
		// From its own size where it crosses the surface to its full growth at the top of its leap, as its body grows.
		const float Gr = FMath::Clamp(Leaper::SpeckPx / FMath::Max(FMath::Max(SizeM, 0.1f) * 100.f * PxPerRad / D, 0.001f), 1.f, Leaper::SpeckMax);
		K = 1.f + (Gr - 1.f) * FMath::Sin(3.14159265f * FMath::Clamp(ShareOfLeap, 0.f, 1.f));
	}
	else if (Kind == EFaunaKind::Flier) { K = FMath::Clamp(2.f / FMath::Max(FMath::Max(SizeM, 0.01f) * 100.f * PxPerRad / D, 0.001f), 1.f, WingForm != Wings::Bird ? Wings::SpeckMax : 4.f); }
	return 1.f / (K * K);
}

bool SlonkFauna::LeaperShown(float T, const float* Custom, const float* Group)
{
	return ReadLeap(T, Custom, Group).bShown;
}

FVector2f SlonkFauna::LeaperTurn(float T, const float* Custom, const float* Group)
{
	const FLeaperRead R = ReadLeap(T, Custom, Group);
	const float Sh = (R.Tau - R.Tau0) / R.Tf;
	const float Vh = R.Range / R.Tf;
	const float Vz = (R.HB - R.HA) / R.Tf + 4.f * R.Apex * (1.f - 2.f * Sh) / R.Tf;
	return FVector2f(FMath::Atan2(Vz, Vh), R.Yaw);
}

FVector3f SlonkFauna::LeaperMotion(const FVector3f& LP, const FLinearColor& VC, float T, const float* C, const float* Gd, float DistCm, float PxPerRad, int32 Form)
{
	// fauna_material.py's leaper branch, line for line.
	const FLeaperRead R = ReadLeap(T, C, Gd);
	const float S = R.Size;
	if (!R.bShown) { return FVector3f(0.f, 0.f, -(150.f / S + 80.f)); }
	const float Sh = (R.Tau - R.Tau0) / R.Tf;
	const float Base = FMath::Lerp(R.HA, R.HB, Sh);
	const float Px = R.Fx + FMath::Cos(R.Yaw) * R.Range * Sh;
	const float Py = R.Fy + FMath::Sin(R.Yaw) * R.Range * Sh;
	const float Pz = Base + 4.f * R.Apex * Sh * (1.f - Sh);
	const float Vh = R.Range / R.Tf;
	const float Vz = (R.HB - R.HA) / R.Tf + 4.f * R.Apex * (1.f - 2.f * Sh) / R.Tf;
	const float Pitch = FMath::Atan2(Vz, Vh);
	// Its tail flicking in the air, a whole number of beats a minute.
	const float Seed = C[SlonkFauna::Custom::Seed];
	const float Beat = FMath::RoundToFloat((6.f + 4.f * FracF(Seed * 3.7f)) * 60.f) / 60.f;
	const float Air = FMath::Clamp(4.f * Sh * (1.f - Sh), 0.f, 1.f);
	FVector3f P = LP;
	if (Form == FishForm::Eel)
	{
		// An eel's wave running back along all of it, less the wave at its middle (which stays on its arc).
		const float Swing = FMath::Lerp(FishForm::EelSwim, 1.f, Air) * FishForm::EelWaveCm;
		const float Ph = FracF(T * Beat * FishForm::EelWaveK + Seed * 5.f);
		const float Mid = FishForm::EelHead + (1.f - FishForm::EelHead) * 0.5f;
		P.Y += Swing * ((FishForm::EelHead + (1.f - FishForm::EelHead) * VC.G) * FMath::Sin(6.2831853f * (Ph - (50.f - LP.X) / FishForm::EelWaveLen))
			- Mid * FMath::Sin(6.2831853f * (Ph - 50.f / FishForm::EelWaveLen)));
	}
	else if (Form == FishForm::Spinefin && VC.R > 0.75f)
	{
		// A spinefin's fin, folded down in the water, spread as it leaves it, fluttering in the air.
		const float Fs = VC.B > 0.5f ? 1.f : -1.f;
		const float Fd = FMath::Max(FMath::Abs(LP.Y) - FishForm::FinRootCm, 0.f);
		const float Fa = -FishForm::FinFold * (1.f - SmoothStepF(0.f, FishForm::FinOpen, Air)) + FishForm::FinFlutter * Air * (0.5f + 0.5f * VC.G) * FMath::Sin(6.2831853f * FracF(T * Beat * 2.f + Seed * 3.f));
		P.Y = Fs * (FishForm::FinRootCm + Fd * FMath::Cos(Fa));
		P.Z = LP.Z + Fd * FMath::Sin(Fa);
	}
	else
	{
		P.Y += 12.f * VC.G * VC.G * Air * FMath::Sin(6.2831853f * FracF(T * Beat + Seed * 5.f));
	}
	// Pitched along its arc (nose up), turned the way it leaps.
	const float Cp = FMath::Cos(Pitch), Sp = FMath::Sin(Pitch);
	P = FVector3f(P.X * Cp - P.Z * Sp, P.Y, P.X * Sp + P.Z * Cp);
	const float Cy = FMath::Cos(R.Yaw), Sy = FMath::Sin(R.Yaw);
	P = FVector3f(P.X * Cy - P.Y * Sy, P.X * Sy + P.Y * Cy, P.Z);
	// The speck floor, from its own size at the surface to its full at the top of its leap.
	const float Pxs = S * 100.f * PxPerRad / FMath::Max(DistCm, 1.f);
	const float K = FMath::Clamp(2.f / FMath::Max(Pxs, 0.001f), 1.f, 2.f);
	P *= 1.f + (K - 1.f) * FMath::Sin(3.14159265f * FMath::Clamp(Sh, 0.f, 1.f));
	return P + FVector3f(Px, Py, Pz) / S - LP;
}

bool SlonkFauna::FFaunaField::SetWorld(const FFaunaWorld& W)
{
	if (bHasWorld && W == World) { return true; }
	// Only its seas changed (as fauna switched off and on takes the shoals out of the terrain's material and puts them
	// back): only the leapers live by them, so only their groups and cells go, and a scan reads theirs again.
	FFaunaWorld SameSeas = W;
	SameSeas.Sea = World.Sea;
	if (bHasWorld && SameSeas == World)
	{
		World = W;
		for (auto It = Active.CreateIterator(); It; ++It)
		{
			if (It.Value()->Kind != EFaunaKind::Leaper) { continue; }
			Members[(int32)EFaunaKind::Leaper] -= It.Value()->Members;
			Groups[(int32)EFaunaKind::Leaper] -= 1;
			It.RemoveCurrent();
		}
		Candidates.RemoveAll([](const FGroupRef& G) { return G->Kind == EFaunaKind::Leaper; });
		if (Cache.IsValid())
		{
			// The cache is shared with any scan in flight: a new one without the leapers' cells (a cell's kind is its id's top bits).
			TSharedPtr<FCellCache, ESPMode::ThreadSafe> Next = MakeShared<FCellCache, ESPMode::ThreadSafe>();
			Next->Reserve(Cache->Num());
			for (const TPair<uint64, FGroupRef>& C : *Cache)
			{
				if ((EFaunaKind)(C.Key >> 60) != EFaunaKind::Leaper) { Next->Add(C.Key, C.Value); }
			}
			Cache = Next;
		}
		bScanned = false;
		return true;
	}
	Clear(true);
	World = W;
	bHasWorld = true;
	return false;
}

uint32 SlonkFauna::FFaunaField::ScanKinds(double CameraAltitudeCm) const
{
	uint32 Kinds = 0u;
	for (int32 K = 0; K < NumKinds; ++K)
	{
		if (!World.Spawns((EFaunaKind)K)) { continue; }
		// Read from a kilometre over the height its groups can be kept at; one the last scan read stays read until the camera
		// is a kilometre higher still, so a camera flying about that height over rolling ground asks for no scan at each rise
		// and dip.
		const bool bWasRead = bScanned && (LastScanKinds & KindBit((EFaunaKind)K)) != 0u;
		const double Top = KindDesc((EFaunaKind)K).MaxAltitudeCm * SpawnHysteresis + ScanKindsMarginCm + (bWasRead ? ScanKindsBandCm : 0.0);
		if (CameraAltitudeCm <= Top) { Kinds |= KindBit((EFaunaKind)K); }
	}
	return Kinds;
}

double SlonkFauna::FFaunaField::ScanRadiusCm(uint32 Kinds) const
{
	double R = 0.0;
	for (int32 K = 0; K < NumKinds; ++K)
	{
		if (!World.Spawns((EFaunaKind)K) || (Kinds & KindBit((EFaunaKind)K)) == 0u) { continue; }
		const FFaunaKindDesc& D = KindDesc((EFaunaKind)K);
		R = FMath::Max(R, D.ReachCm * SpawnHysteresis + D.CellCm / 6.0 + D.CellCm);
	}
	return R;
}

bool SlonkFauna::FFaunaField::WantsScan(const FVector& GroundDir, double CameraAltitudeCm) const
{
	if (!bHasWorld || !World.SpawnsAny()) { return false; }
	if (!bScanned) { return true; }
	// Come up past a kind's height, or down into it: the kinds read change.
	const uint32 Kinds = ScanKinds(CameraAltitudeCm);
	if (Kinds != LastScanKinds) { return true; }
	double Step = TNumericLimits<double>::Max();
	for (int32 K = 0; K < NumKinds; ++K)
	{
		if (World.Spawns((EFaunaKind)K) && (Kinds & KindBit((EFaunaKind)K)) != 0u) { Step = FMath::Min(Step, KindDesc((EFaunaKind)K).CellCm / 6.0); }
	}
	return FVector::Dist(GroundDir.GetSafeNormal(), LastScanDir) * World.P.RadiusCm > Step;
}

TSharedRef<SlonkFauna::FFaunaField::FScan, ESPMode::ThreadSafe> SlonkFauna::FFaunaField::Scan(const FFaunaWorld& W, const FVector& GroundDir, double RadiusCm, TSharedPtr<const FCellCache, ESPMode::ThreadSafe> InCache,
	uint32 Kinds)
{
	const double Start = FPlatformTime::Seconds();
	TSharedRef<FScan, ESPMode::ThreadSafe> S = MakeShared<FScan, ESPMode::ThreadSafe>();
	S->CentreDir = GroundDir.GetSafeNormal();
	S->GroundCm = FMath::Max(FlightGroundCm(W.P, S->CentreDir), 0.0);
	// The kinds it reads: those asked for that live here.
	S->Kinds = 0u;
	TArray<FFaunaCell> Cells;
	for (int32 K = 0; K < NumKinds; ++K)
	{
		if (!W.Spawns((EFaunaKind)K) || (Kinds & KindBit((EFaunaKind)K)) == 0u) { continue; }
		S->Kinds |= KindBit((EFaunaKind)K);
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
	// The cells of the kinds it did not read (the camera too high for them) stay as the last scan of them left them, so
	// coming back down past their height reads only those new to them since (a cell's kind is its id's top bits). None of
	// their groups is a candidate: none could be spawned that high.
	if (Cache.IsValid())
	{
		for (const TPair<uint64, FGroupRef>& C : *Cache)
		{
			if ((S.Kinds & KindBit((EFaunaKind)(C.Key >> 60))) == 0u) { Next->Add(C.Key, C.Value); }
		}
	}
	Cache = Next;
	LastScanDir = S.CentreDir;
	LastScanKinds = S.Kinds;
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
	// The camera's height over the ground a group's kind is kept under: the ground under the scan's middle; for a jelly the
	// higher of that and its loop's highest ground, over which it drifts no higher than Floater::MaxCruiseCm and its bob,
	// so however high the ground under its loop stands it comes and goes past its fade as the camera climbs and comes down.
	auto AltOver = [&](const FFaunaGroup& G) { return G.Kind == EFaunaKind::Floater ? FMath::Min(Alt, CamLocal.Size() - R - LoopGroundTop(G)) : Alt; };
	// Gone past the hysteresis (or its kind gone, or the camera too high): let go.
	for (const TPair<uint64, FGroupRef>& A : Active)
	{
		const FFaunaKindDesc& D = KindDesc(A.Value->Kind);
		const double Dist = FVector::Dist(CamDir, A.Value->Centre) * R;
		if (!World.Spawns(A.Value->Kind) || Dist > D.ReachCm * SpawnHysteresis || AltOver(*A.Value) > D.MaxAltitudeCm * SpawnHysteresis) { OutLetGo.Add(A.Key); }
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
		if (Dist < D.ReachCm && AltOver(G) < D.MaxAltitudeCm) { Order.Add({ Dist, i }); }
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
