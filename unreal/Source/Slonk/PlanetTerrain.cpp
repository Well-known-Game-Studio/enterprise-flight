#include "PlanetTerrain.h"

namespace
{
	// ---- lattice gradient noise in doubles ----
	// Float noise breaks down long before the metre scale on a 6,000 km sphere (a unit direction times a frequency
	// of a million has ulps of a tenth), so the lattice is addressed with 64-bit integers and the fractions are
	// doubles. The hash is a plain integer mix; it only has to be uncorrelated, not fast to invert.
	FORCEINLINE uint64 Mix(uint64 X)
	{
		X ^= X >> 33; X *= 0xff51afd7ed558ccdULL; X ^= X >> 33; X *= 0xc4ceb9fe1a85ec53ULL; X ^= X >> 33;
		return X;
	}
	FORCEINLINE uint32 Hash3(int64 X, int64 Y, int64 Z)
	{
		return (uint32)Mix((uint64)X * 0x9E3779B97F4A7C15ULL ^ Mix((uint64)Y * 0xC2B2AE3D27D4EB4FULL ^ Mix((uint64)Z * 0x165667B19E3779F9ULL)));
	}
	FORCEINLINE double Grad(uint32 H, double X, double Y, double Z)
	{
		// Perlin's twelve edge gradients, picked by the low bits.
		switch (H & 15)
		{
		case 0: case 12: return X + Y;
		case 1: case 13: return -X + Y;
		case 2: return X - Y;
		case 3: return -X - Y;
		case 4: return X + Z;
		case 5: return -X + Z;
		case 6: return X - Z;
		case 7: return -X - Z;
		case 8: return Y + Z;
		case 9: case 14: return -Y + Z;
		case 10: return Y - Z;
		default: return -Y - Z;
		}
	}
	FORCEINLINE double Fade(double T) { return T * T * T * (T * (T * 6.0 - 15.0) + 10.0); }

	double Perlin(double X, double Y, double Z)
	{
		const double FX = FMath::Floor(X), FY = FMath::Floor(Y), FZ = FMath::Floor(Z);
		const int64 IX = (int64)FX, IY = (int64)FY, IZ = (int64)FZ;
		const double x = X - FX, y = Y - FY, z = Z - FZ;
		const double u = Fade(x), v = Fade(y), w = Fade(z);
		const double A = Grad(Hash3(IX, IY, IZ), x, y, z), B = Grad(Hash3(IX + 1, IY, IZ), x - 1, y, z);
		const double C = Grad(Hash3(IX, IY + 1, IZ), x, y - 1, z), D = Grad(Hash3(IX + 1, IY + 1, IZ), x - 1, y - 1, z);
		const double E = Grad(Hash3(IX, IY, IZ + 1), x, y, z - 1), F = Grad(Hash3(IX + 1, IY, IZ + 1), x - 1, y, z - 1);
		const double G = Grad(Hash3(IX, IY + 1, IZ + 1), x, y - 1, z - 1), H = Grad(Hash3(IX + 1, IY + 1, IZ + 1), x - 1, y - 1, z - 1);
		const double L0 = FMath::Lerp(FMath::Lerp(A, B, u), FMath::Lerp(C, D, u), v);
		const double L1 = FMath::Lerp(FMath::Lerp(E, F, u), FMath::Lerp(G, H, u), v);
		// Scaled so the output fills about -1..1.
		return FMath::Lerp(L0, L1, w) * 1.15;
	}
	FORCEINLINE double Perlin(const FVector& P) { return Perlin(P.X, P.Y, P.Z); }

	/** Fractal sum; Octaves already limited by the caller. Output roughly -1..1. */
	double FbmN(const FVector& P, int32 Octaves, double Lacunarity, double Gain)
	{
		double Sum = 0.0, Amp = 1.0, Norm = 0.0;
		FVector Q = P;
		for (int32 i = 0; i < Octaves; ++i)
		{
			Sum += Amp * Perlin(Q);
			Norm += Amp;
			Amp *= Gain;
			Q *= Lacunarity;
			// Decorrelate the octaves so the lattice lines never line up.
			Q += FVector(17.31, 9.17, 23.71);
		}
		return Norm > 0.0 ? Sum / Norm : 0.0;
	}

	/** Musgrave ridged multifractal, 0..1 with sharp ridges at 1. */
	double Ridged(const FVector& P, int32 Octaves, double Lacunarity = 2.1, double Gain = 0.5, double Sharp = 1.0)
	{
		double Sum = 0.0, Amp = 0.5, Weight = 1.0, Norm = 0.0;
		FVector Q = P;
		for (int32 i = 0; i < Octaves; ++i)
		{
			double S = 1.0 - FMath::Abs(Perlin(Q));
			S = FMath::Pow(FMath::Max(S, 0.0), 1.0 + Sharp);
			S *= Weight;
			Weight = FMath::Clamp(S * 2.0, 0.0, 1.0);
			Sum += S * Amp;
			Norm += Amp;
			Amp *= Gain;
			Q *= Lacunarity;
			Q += FVector(5.13, 31.7, 11.9);
		}
		return Norm > 0.0 ? FMath::Clamp(Sum / Norm, 0.0, 1.0) : 0.0;
	}

	FORCEINLINE double SmoothStep(double Lo, double Hi, double V)
	{
		const double T = FMath::Clamp((V - Lo) / (Hi - Lo), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}

	/** Octaves whose wavelength stays above MinWavelength, given the wavelength of the first one. */
	FORCEINLINE int32 OctavesFor(double FirstWavelengthCm, double MinWavelengthCm, int32 MaxOctaves, double Lacunarity = 2.0)
	{
		if (MinWavelengthCm <= 0.0 || FirstWavelengthCm <= MinWavelengthCm)
		{
			return MinWavelengthCm <= 0.0 ? MaxOctaves : 1;
		}
		const int32 N = 1 + (int32)FMath::CeilToDouble(FMath::Loge(FirstWavelengthCm / MinWavelengthCm) / FMath::Loge(Lacunarity));
		return FMath::Clamp(N, 1, MaxOctaves);
	}

	/** Worley-style crater field: the nearest feature point in the 27 cells around P, each with a hashed radius. */
	struct FCraterHit { double Dist = 1e9; double Radius = 0.0; double Depth = 0.0; };
	FCraterHit NearestCrater(const FVector& P, double Gate)
	{
		FCraterHit Best;
		const int64 IX = (int64)FMath::Floor(P.X), IY = (int64)FMath::Floor(P.Y), IZ = (int64)FMath::Floor(P.Z);
		for (int64 dz = -1; dz <= 1; ++dz)
		for (int64 dy = -1; dy <= 1; ++dy)
		for (int64 dx = -1; dx <= 1; ++dx)
		{
			const int64 CX = IX + dx, CY = IY + dy, CZ = IZ + dz;
			const uint32 H = Hash3(CX * 3 + 1, CY * 7 + 2, CZ * 11 + 3);
			// Only some cells hold a crater; the gate thins the field.
			if (((H >> 24) & 255) / 255.0 > Gate) { continue; }
			const FVector Centre((double)CX + ((H & 255) / 255.0), (double)CY + (((H >> 8) & 255) / 255.0), (double)CZ + (((H >> 16) & 255) / 255.0));
			const double D = (P - Centre).Size();
			const double R = 0.18 + 0.27 * (Hash3(CX, CY + 5, CZ) & 255) / 255.0;
			if (D - R < Best.Dist - Best.Radius)
			{
				Best.Dist = D; Best.Radius = R; Best.Depth = 0.6 + 0.4 * ((Hash3(CX + 9, CY, CZ) & 255) / 255.0);
			}
		}
		return Best;
	}
	/** Signed height of the crater profile at the hit: bowl (negative) with a raised rim, in units of the radius. */
	double CraterProfile(const FCraterHit& C)
	{
		if (C.Dist >= C.Radius * 1.6) { return 0.0; }
		const double r = C.Dist / C.Radius;
		if (r < 1.0)
		{
			// Parabolic bowl rising to the rim.
			return (r * r - 1.0) * 0.5 * C.Depth + 0.14 * SmoothStep(0.55, 1.0, r);
		}
		// Rim falling off outside, with ejecta tail.
		return 0.14 * (1.0 - SmoothStep(1.0, 1.6, r));
	}
}

namespace SlonkTerrain
{
	double Noise3(const FVector& P) { return Perlin(P); }
	double Fbm(const FVector& P, int32 Octaves, double Lacunarity, double Gain) { return FbmN(P, Octaves, Lacunarity, Gain); }

	FPlanetTerrainSample Sample(const FPlanetTerrainParams& P, const FVector& Dir, double MinWavelengthCm)
	{
		FPlanetTerrainSample Out;
		const double R = P.RadiusCm;
		const double CS = FMath::Max((double)P.ContinentScale, 0.1);
		// Wavelength of one noise cell of the base field.
		const double Lambda0 = R / CS;

		const FVector Q = Dir * CS + P.Seed;
		// Domain warp so the coastlines meander instead of looking like blobs.
		const FVector QW = Q + FVector(FbmN(Q * 0.45 + FVector(7.1, 0.0, 0.0), 3, 2.0, 0.5),
			FbmN(Q * 0.45 + FVector(0.0, 3.3, 0.0), 3, 2.0, 0.5),
			FbmN(Q * 0.45 + FVector(0.0, 0.0, 11.7), 3, 2.0, 0.5)) * 0.55;

		// ---- continents ----
		const double C = FbmN(QW, OctavesFor(Lambda0, MinWavelengthCm, 7, 2.2), 2.2, 0.5) * 0.5 + 0.5;
		const double Sea = P.bAirless ? -10.0 : (double)P.SeaLevel;
		const double Land = P.bAirless ? 1.0 : SmoothStep(Sea - 0.015, Sea + 0.015, C);
		// Base elevation of the land 0..1: plains at the coast, highlands inland.
		const double HN = P.bAirless ? FMath::Clamp(C, 0.0, 1.0) : FMath::Clamp((C - Sea) / FMath::Max(1.0 - Sea, 0.05), 0.0, 1.0);
		const double Base = FMath::Pow(HN, 1.3);

		// ---- mountain ranges ----
		const double MtnField = FbmN(Q * 1.7 + FVector(31.0, 0.0, 0.0), 3, 2.0, 0.5) * 0.5 + 0.5;
		const double MtnMask = SmoothStep(0.42, 0.68, MtnField) * Land * P.Mountains;
		double Mtn = 0.0;
		if (MtnMask > 0.001)
		{
			// Ridges every ~50 km on an Earth (base wavelength Lambda0/40), twelve octaves at gain 0.6: about a
			// kilometre of relief per 5 km, which is what a real range has. The range mask above sets where.
			Mtn = Ridged(QW * 40.0 + FVector(3.0, 71.0, 0.0), OctavesFor(Lambda0 / 40.0, MinWavelengthCm, 12, 2.1), 2.1, 0.6, 0.9);
			Mtn = FMath::Pow(Mtn, 1.3);
		}

		// ---- maria ----
		const double Maria = P.Maria > 0.f ? SmoothStep(0.55, 0.7, FbmN(Q * 1.3 + FVector(17.0, 0.0, 0.0), 3, 2.0, 0.5) * 0.5 + 0.5) * P.Maria : 0.0;

		// Height in units of the relief (0..1 at the tallest peak).
		double H = Base * 0.35 + MtnMask * Mtn * 0.65;
		H *= (1.0 - 0.88 * Maria);

		// ---- canyons: valley lines where a noise field crosses zero, cut into uplifted tablelands ----
		double Carve = 0.0, Wall = 0.0;
		const double TableField = FbmN(Q * 1.1 + FVector(0.0, 23.0, 0.0), 3, 2.0, 0.5) * 0.5 + 0.5;
		// Canyons sets how much of the land is tableland (threshold), never the depth of the cut.
		const double TableLo = 0.68 - 0.2 * FMath::Clamp((double)P.Canyons, 0.0, 1.0);
		const double Tableland = SmoothStep(TableLo, TableLo + 0.1, TableField) * SmoothStep(0.08, 0.25, HN) * (1.0 - SmoothStep(0.15, 0.6, MtnMask * Mtn * 3.0)) * Land;
		const double CanyonZone = P.Canyons > 0.f ? Tableland : 0.0;
		// The plateau rises a quarter of the relief (2.3 km on an Earth) so a canyon has something to cut into.
		H += Tableland * 0.25 * FMath::Max(P.Canyons, 0.3f);
		if (CanyonZone > 0.001)
		{
			const double F = 5.0; // one valley line per ~400 km on an Earth-size world
			const double CellCm = Lambda0 / F;
			// |n| < HalfWidth is inside; the noise climbs at about two per unit off its zero line, so double it.
			const double HalfWidth = P.CanyonWidthCm * 0.5 / CellCm * 2.0;
			// Rough the line with finer noise so the walls are not glass smooth.
			const double Wig = FbmN(QW * F * 9.0 + FVector(3.0, 5.0, 7.0), OctavesFor(CellCm / 9.0, MinWavelengthCm, 5), 2.0, 0.5);
			// Finer wiggle (about 8 km down to 500 m) so the cliff lines are scalloped and gullied, not ruled.
			const double Wig2 = FbmN(QW * F * 48.0 + FVector(11.0, 2.0, 9.0), OctavesFor(CellCm / 48.0, MinWavelengthCm, 5), 2.0, 0.55);
			const double V = FMath::Abs(Perlin(QW * F + FVector(57.0, 0.0, 0.0)) + 0.25 * HalfWidth * Wig + 0.12 * HalfWidth * Wig2);
			// Main canyon: flat floor, then the wall climbs through the outer third of the half-width as a
			// staircase of benches and cliffs (the strata), each bench a little narrower than the last so the
			// profile opens upward like a real cut.
			const double T = FMath::Clamp((V - HalfWidth * 0.65) / (HalfWidth * 0.35), 0.0, 1.0);
			const int32 Steps = 6;
			const double Tk = T * Steps;
			const double Bench = FMath::Floor(Tk);
			const double Cliff = FMath::Pow(Tk - Bench, 1.0 + 7.0 * P.Strata);
			const double Stair = (Bench + Cliff) / Steps;
			const double Main = 1.0 - (P.Strata > 0.f ? Stair * 0.85 + T * 0.15 : SmoothStep(0.0, 1.0, T));
			// Tributaries: a finer line set at a third of the width and half the depth.
			const double V2 = FMath::Abs(Perlin(QW * F * 2.6 + FVector(0.0, 91.0, 0.0)) + 0.2 * HalfWidth * Wig);
			const double Trib = (1.0 - SmoothStep(HalfWidth * 0.12, HalfWidth * 0.4, V2)) * 0.5;
			// Depth varies along the canyon so it opens and narrows.
			const double Along = 0.75 + 0.25 * (FbmN(QW * 3.0 + FVector(19.0, 0.0, 0.0), 3, 2.0, 0.5) * 0.5 + 0.5);
			Carve = FMath::Max(Main, Trib) * CanyonZone * Along;
			// The wall band: everything between the rim and the floor.
			Wall = SmoothStep(0.02, 0.12, Carve) * (1.0 - SmoothStep(0.88, 0.98, Carve));
		}
		const double CanyonUnits = P.CanyonDepthCm / FMath::Max(P.ReliefCm, 1.0);
		// Never cut below sea level: keep a twentieth of the plateau height under the floor.
		const double Cut = FMath::Min(Carve * CanyonUnits, H * 0.95);
		H -= Cut;

		// ---- strata: terraces on the canyon walls and steep rock ----
		if (P.Strata > 0.f && Wall > 0.01)
		{
			const double Step = CanyonUnits / 6.0;
			if (Step > 1e-9)
			{
				// Each stratum: a bench, then a cliff. Strata sets how sheer the cliff is.
				const double K = FMath::Floor(H / Step);
				const double Frac = H / Step - K;
				const double Shaped = FMath::Pow(Frac, 2.0 + 6.0 * P.Strata);
				const double Terraced = (K + Shaped) * Step;
				H = FMath::Lerp(H, Terraced, 0.85 * FMath::Min(Wall, 1.0));
			}
		}

		// ---- craters ----
		double CraterShade = 0.0;
		if (P.Craters > 0.f)
		{
			const FVector QC = Dir * P.CraterScale + P.Seed * 0.31;
			const double Scales[4] = { 1.5, 5.0, 15.0, 45.0 };
			const double Gates[4] = { 0.78, 0.62, 0.55, 0.5 };
			const double Amps[4] = { 0.5, 0.25, 0.12, 0.06 };
			double CraterH = 0.0;
			for (int32 i = 0; i < 4; ++i)
			{
				const double CellCm = R / (P.CraterScale * Scales[i]);
				if (MinWavelengthCm > 0.0 && CellCm * 0.3 < MinWavelengthCm) { break; }
				const FCraterHit Hit = NearestCrater(QC * Scales[i] + FVector(i * 7.3, i * 2.1, i * 5.7), Gates[i]);
				const double Prof = CraterProfile(Hit);
				CraterH += Prof * Amps[i] * (1.0 - 0.7 * Maria);
				CraterShade = FMath::Max(CraterShade, FMath::Abs(Prof) * 2.0);
			}
			H += CraterH * 0.35 * P.Craters;
		}

		// ---- detail: hills and rock down to the metre ----
		{
			const double DetailLambda = Lambda0 / 60.0;
			const int32 Oct = OctavesFor(DetailLambda, MinWavelengthCm, 15, 2.0);
			const double Rocky = FMath::Clamp(MtnMask * Mtn * 2.0 + Wall * 0.8 + Carve * 0.5 + (P.bAirless ? 0.3 : 0.0), 0.0, 1.0);
			// Rock keeps its roughness down the octaves (gain near 0.7), plains smooth out (0.5).
			const double D = FbmN(QW * 60.0 + FVector(0.0, 0.0, 41.0), Oct, 2.0, 0.6 + 0.14 * Rocky);
			H += D * (0.01 + 0.03 * Rocky) * (0.3 + 0.7 * Land);
		}

		// ---- ocean floor ----
		double HeightCm;
		if (!P.bAirless && C < Sea + 0.015)
		{
			const double Shelf = SmoothStep(Sea + 0.015, Sea - 0.06, C);
			const double Floor = -P.OceanDepthCm * (0.08 + 0.92 * Shelf);
			const double LandCm = H * P.ReliefCm;
			// Blend the coast so the beach runs smoothly into the shelf.
			HeightCm = FMath::Lerp(Floor, LandCm, Land);
		}
		else
		{
			HeightCm = H * P.ReliefCm;
		}
		Out.GroundCm = HeightCm;
		Out.bWater = !P.bAirless && HeightCm < 0.0;
		Out.HeightCm = HeightCm;

		// ---- biome ----
		const double Hr = FMath::Clamp(HeightCm / FMath::Max(P.ReliefCm, 1.0), 0.0, 1.0);
		const double WetField = FbmN(Q * 2.5 + FVector(43.0, 0.0, 0.0), 3, 2.0, 0.5) * 0.5 + 0.5;
		double Wet = FMath::Clamp(P.Moisture * 1.4 * (0.5 + WetField) - Hr * 0.6, 0.0, 1.0);
		if (P.bAirless) { Wet = 0.0; }
		const double Lat = FMath::Abs(Dir.Z);
		const double Polar = SmoothStep(P.IceCapStart - 0.06, P.IceCapStart + 0.04, Lat);
		const double Snow = P.bAirless || P.bLava ? 0.0 : SmoothStep(0.5, 0.72, Hr + 0.45 * Lat);
		Out.Ice = (float)FMath::Clamp(FMath::Max(Polar, Snow) * (P.bLava ? 0.0 : 1.0), 0.0, 1.0);
		Out.Rock = (float)FMath::Clamp(MtnMask * Mtn * 2.5 + Wall + CraterShade * 0.5, 0.0, 1.0);
		Out.Wet = (float)Wet;
		Out.Canyon = (float)FMath::Clamp(Carve, 0.0, 1.0);
		Out.Wall = (float)FMath::Clamp(Wall, 0.0, 1.0);
		Out.Crater = (float)FMath::Clamp(CraterShade, 0.0, 1.0);
		return Out;
	}

	double SurfaceHeightCm(const FPlanetTerrainParams& P, const FVector& Dir)
	{
		const FPlanetTerrainSample S = Sample(P, Dir, 0.0);
		return FMath::Max(S.HeightCm, 0.0);
	}
}
