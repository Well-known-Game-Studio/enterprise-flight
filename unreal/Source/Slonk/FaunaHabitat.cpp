#include "FaunaHabitat.h"
#include "TerrainScatter.h"

bool SlonkFauna::ShoalAt(const FVector& PlanetLocalCm, double RadiusCm, float Density, double TimeSec, FShoal& Out)
{
	const FVector U = PlanetLocalCm.GetSafeNormal();
	if (Density <= 0.f || U.IsNearlyZero()) { return false; }
	// terrain_material.py's water block, number for number: the plane across the direction's largest axis, the point's
	// cell on it in the material's noise space, the cell's index wrapped to the period, its hash and its gate.
	const FVector Pn = SlonkScatter::ToNoiseSpace(PlanetLocalCm);
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
	const double Pu = (Fu + CtrU + LoopR * Cs) * Cell + (PlanetLocalCm[Iu] - Pn[Iu]);
	const double Pv = (Fv + CtrV + LoopR * Sn) * Cell + (PlanetLocalCm[Iv] - Pn[Iv]);
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
