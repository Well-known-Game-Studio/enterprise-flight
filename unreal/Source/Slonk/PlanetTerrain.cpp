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

	/** The gradient Grad() takes the dot product with. */
	FORCEINLINE FVector GradVec(uint32 H)
	{
		switch (H & 15)
		{
		case 0: case 12: return FVector(1.0, 1.0, 0.0);
		case 1: case 13: return FVector(-1.0, 1.0, 0.0);
		case 2: return FVector(1.0, -1.0, 0.0);
		case 3: return FVector(-1.0, -1.0, 0.0);
		case 4: return FVector(1.0, 0.0, 1.0);
		case 5: return FVector(-1.0, 0.0, 1.0);
		case 6: return FVector(1.0, 0.0, -1.0);
		case 7: return FVector(-1.0, 0.0, -1.0);
		case 8: return FVector(0.0, 1.0, 1.0);
		case 9: case 14: return FVector(0.0, -1.0, 1.0);
		case 10: return FVector(0.0, 1.0, -1.0);
		default: return FVector(0.0, -1.0, -1.0);
		}
	}
	FORCEINLINE double FadeD(double T) { return 30.0 * T * T * (T * (T - 2.0) + 1.0); }

	/** Perlin() with its analytic gradient (the same field: the corner gradients interpolated, plus the fade's slope
	 *  across each axis's corner differences), for the erosion the mountains take from their own slope. */
	double PerlinD(const FVector& Pt, FVector& OutGrad)
	{
		const double FX = FMath::Floor(Pt.X), FY = FMath::Floor(Pt.Y), FZ = FMath::Floor(Pt.Z);
		const int64 IX = (int64)FX, IY = (int64)FY, IZ = (int64)FZ;
		const double x = Pt.X - FX, y = Pt.Y - FY, z = Pt.Z - FZ;
		const double u = Fade(x), v = Fade(y), w = Fade(z);
		const double du = FadeD(x), dv = FadeD(y), dw = FadeD(z);
		const FVector ga = GradVec(Hash3(IX, IY, IZ)), gb = GradVec(Hash3(IX + 1, IY, IZ));
		const FVector gc = GradVec(Hash3(IX, IY + 1, IZ)), gd = GradVec(Hash3(IX + 1, IY + 1, IZ));
		const FVector ge = GradVec(Hash3(IX, IY, IZ + 1)), gf = GradVec(Hash3(IX + 1, IY, IZ + 1));
		const FVector gg = GradVec(Hash3(IX, IY + 1, IZ + 1)), gh = GradVec(Hash3(IX + 1, IY + 1, IZ + 1));
		const double A = FVector::DotProduct(ga, FVector(x, y, z)), B = FVector::DotProduct(gb, FVector(x - 1.0, y, z));
		const double C = FVector::DotProduct(gc, FVector(x, y - 1.0, z)), D = FVector::DotProduct(gd, FVector(x - 1.0, y - 1.0, z));
		const double E = FVector::DotProduct(ge, FVector(x, y, z - 1.0)), F = FVector::DotProduct(gf, FVector(x - 1.0, y, z - 1.0));
		const double G = FVector::DotProduct(gg, FVector(x, y - 1.0, z - 1.0)), H = FVector::DotProduct(gh, FVector(x - 1.0, y - 1.0, z - 1.0));
		const double k1 = B - A, k2 = C - A, k3 = E - A, k4 = A - B - C + D, k5 = A - C - E + G, k6 = A - B - E + F, k7 = -A + B + C - D + E - F - G + H;
		const double N = A + k1 * u + k2 * v + k3 * w + k4 * u * v + k5 * v * w + k6 * w * u + k7 * u * v * w;
		FVector Gr = ga + (gb - ga) * u + (gc - ga) * v + (ge - ga) * w + (ga - gb - gc + gd) * (u * v) + (ga - gc - ge + gg) * (v * w)
			+ (ga - gb - ge + gf) * (w * u) + (gb + gc + ge + gh - ga - gd - gf - gg) * (u * v * w);
		Gr.X += du * (k1 + k4 * v + k6 * w + k7 * v * w);
		Gr.Y += dv * (k2 + k4 * u + k5 * w + k7 * u * w);
		Gr.Z += dw * (k3 + k5 * v + k6 * u + k7 * u * v);
		OutGrad = Gr * 1.15;
		return N * 1.15;
	}

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

	/** Noise for ridges: Perlin is zero at every lattice point, so ridges folded from |Perlin| run along the lattice
	 *  diagonals. Adding a second Perlin at an incommensurate scale frees the zero set. Output about -1..1. */
	FORCEINLINE double RidgeBase(const FVector& Q)
	{
		return (Perlin(Q) + 0.6 * Perlin(FVector(Q.Y * 0.613 + 3.17, Q.Z * 0.587 - 7.9, Q.X * 0.641 + 1.3))) / 1.25;
	}

	/** Musgrave ridged multifractal, 0..1 with sharp ridges at 1. */
	double Ridged(const FVector& P, int32 Octaves, double Lacunarity = 2.1, double Gain = 0.5, double Sharp = 1.0)
	{
		double Sum = 0.0, Amp = 0.5, Weight = 1.0, Norm = 0.0;
		FVector Q = P;
		for (int32 i = 0; i < Octaves; ++i)
		{
			double S = 1.0 - FMath::Abs(RidgeBase(Q));
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

	/** How strongly the ranges' slope damps their finer ridges (ErodedRidged()): halved where the coarser ground
	 *  climbs at 1/sqrt(2.56), about 32 degrees, as the slope is estimated (it runs low by about a third: it leaves
	 *  out how the weights and the domain warp steepen it). Weaker left the flanks as rough as the crests; stronger
	 *  smooths the ranges into domes. */
	constexpr double MountainErode = 2.56;

	/** The average share of its amplitude a ridged octave adds after the first, (1 - |n|)^(1 + Sharp) under the
	 *  weight the octave before it leaves: what ErodedRidged() counts for the octaves a coarse level of detail leaves
	 *  out. Found by matching the coarse levels to the fine over the ranges of worlds of 600, 3,390 and 6,371 km:
	 *  at 0.6 the 20 km and 2 km levels stand within a tenth of a percent of the relief of the 200 m one, where
	 *  counting only the evaluated octaves left them 1 to 7% above it. */
	constexpr double MeanRidgeShare = 0.6;

	/** RidgeBase() with its gradient. */
	FORCEINLINE double RidgeBaseD(const FVector& Q, FVector& OutGrad)
	{
		FVector G1, G2;
		const double N1 = PerlinD(Q, G1);
		const double N2 = PerlinD(FVector(Q.Y * 0.613 + 3.17, Q.Z * 0.587 - 7.9, Q.X * 0.641 + 1.3), G2);
		// The second field's axes are the first's permuted and scaled: its gradient comes back through the same map.
		OutGrad = (G1 + FVector(G2.Z * 0.641, G2.X * 0.613, G2.Y * 0.587) * 0.6) / 1.25;
		return (N1 + 0.6 * N2) / 1.25;
	}

	/**
	 * A ridged multifractal that erodes. Each octave is damped by the slope the coarser octaves have already built
	 * across the ground, so the finer ridges fade on the steep flanks and gather on the crests and the gentler
	 * ground: sharp crest lines, smooth spurs running down from them, open valleys between, as frost and water leave a
	 * real range, where plain ridged noise is equally rough everywhere (after Quilez's eroded fbm). The slope is a sum
	 * of each octave's squared slope rather than of the gradients themselves: a ridge's gradient flips across its
	 * crest, and a summed vector would carry that jump into the damping as a crease line. Up is the radial direction,
	 * so only the slope across the ground counts. SlopeScale turns a slope in the noise's own units into rise over
	 * run on the ground (the height of the output's 1 over the length of the first octave's unit), so Erode means the
	 * same steepness on every world: an octave is halved where the ground under it climbs at 1/sqrt(Erode).
	 * Octaves is what this level of detail evaluates, MaxOctaves the whole stack: the octaves left out still count,
	 * at what one adds on average (MeanRidgeShare of its amplitude, under the weight carried down and the damping of
	 * a slope that goes on growing at the rate the evaluated octaves built it), so a coarse level stands where the
	 * fine ones average. Counting only the evaluated octaves' amplitudes, it stood higher: the fine octaves, damped
	 * on the flanks, add less than their share. 0..1, ridges at 1; Erode 0 is Ridged() over the whole stack.
	 */
	double ErodedRidged(const FVector& P, const FVector& Up, int32 Octaves, int32 MaxOctaves, double Lacunarity, double Gain, double Sharp, double SlopeScale, double Erode)
	{
		// The amplitudes' sum over the whole stack: the output's scale whatever this level of detail evaluates.
		double Norm = 0.0;
		for (int32 i = 0; i < MaxOctaves; ++i) { Norm += 0.5 * FMath::Pow(Gain, (double)i); }
		if (Norm <= 0.0) { return 0.0; }
		double Sum = 0.0, Amp = 0.5, Weight = 1.0, Freq = 1.0, SlopeSq = 0.0;
		FVector Q = P;
		const int32 Evaluated = FMath::Clamp(Octaves, 0, MaxOctaves);
		for (int32 i = 0; i < Evaluated; ++i)
		{
			FVector G;
			const double N = RidgeBaseD(Q, G);
			const double S0 = FMath::Max(1.0 - FMath::Abs(N), 0.0);
			const double SP = FMath::Pow(S0, Sharp);
			const double S = S0 * SP * Weight;
			Sum += S * Amp / (1.0 + Erode * SlopeSq);
			// This octave's slope on the ground, d/dQ (1 - |n|)^(1 + Sharp) as weighted and as it reaches the output
			// (over the amplitudes' sum), for the octaves after it.
			FVector D = G * ((1.0 + Sharp) * SP * Weight * Amp * Freq / Norm * SlopeScale);
			D -= Up * FVector::DotProduct(D, Up);
			SlopeSq += D.SizeSquared();
			Weight = FMath::Clamp(S * 2.0, 0.0, 1.0);
			Amp *= Gain;
			Freq *= Lacunarity;
			Q *= Lacunarity;
			Q += FVector(5.13, 31.7, 11.9);
		}
		const double SlopeSqPer = Evaluated > 0 ? SlopeSq / Evaluated : 0.0;
		for (int32 i = Evaluated; i < MaxOctaves; ++i)
		{
			const double S = MeanRidgeShare * Weight;
			Sum += S * Amp / (1.0 + Erode * SlopeSq);
			SlopeSq += SlopeSqPer;
			Weight = FMath::Clamp(S * 2.0, 0.0, 1.0);
			Amp *= Gain;
		}
		return FMath::Clamp(Sum / Norm, 0.0, 1.0);
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
	double SnowlineCm(const FPlanetTerrainParams& P, const FVector& Dir)
	{
		const double Cos2 = FMath::Max(1.0 - Dir.Z * Dir.Z / FMath::Max(Dir.SizeSquared(), 1e-12), 0.0);
		return P.SnowlineCm * FMath::Pow(Cos2, 0.75);
	}

	double SnowBandCm(double LineCm)
	{
		return FMath::Max(LineCm * 0.06, 2000.0);
	}

	double ReliefForRadius(double FractionAtEarthSize, double RadiusCm)
	{
		constexpr double EarthCm = 6.371e8;
		return FMath::Max(FractionAtEarthSize * EarthCm * FMath::Sqrt(FMath::Max(RadiusCm, 1.0) / EarthCm), 1.0);
	}

	double Noise3(const FVector& P) { return Perlin(P); }
	double Fbm(const FVector& P, int32 Octaves, double Lacunarity, double Gain) { return FbmN(P, Octaves, Lacunarity, Gain); }

	FVector CubeDir(int32 Face, double U, double V)
	{
		// Tangent warp: plain cube projection squeezes the corners of a face to half size.
		const double S = FMath::Tan(U * UE_DOUBLE_PI * 0.25);
		const double T = FMath::Tan(V * UE_DOUBLE_PI * 0.25);
		FVector D;
		switch (Face)
		{
		case 0: D = FVector(1.0, S, T); break;
		case 1: D = FVector(-1.0, -S, T); break;
		case 2: D = FVector(-S, 1.0, T); break;
		case 3: D = FVector(S, -1.0, T); break;
		case 4: D = FVector(-T, S, 1.0); break;
		default: D = FVector(T, S, -1.0); break;
		}
		return D.GetSafeNormal();
	}

	bool FaceUV(const FVector& Dir, int32& OutFace, double& OutU, double& OutV)
	{
		const FVector A = Dir.GetAbs();
		double S, T;
		if (A.X >= A.Y && A.X >= A.Z)
		{
			if (A.X <= 0.0) { return false; }
			OutFace = Dir.X > 0.0 ? 0 : 1;
			S = Dir.Y / Dir.X; T = Dir.X > 0.0 ? Dir.Z / Dir.X : -Dir.Z / Dir.X;
		}
		else if (A.Y >= A.Z)
		{
			OutFace = Dir.Y > 0.0 ? 2 : 3;
			S = -Dir.X / Dir.Y; T = Dir.Y > 0.0 ? Dir.Z / Dir.Y : -Dir.Z / Dir.Y;
		}
		else
		{
			OutFace = Dir.Z > 0.0 ? 4 : 5;
			T = -Dir.X / Dir.Z; S = Dir.Z > 0.0 ? Dir.Y / Dir.Z : -Dir.Y / Dir.Z;
		}
		OutU = FMath::Atan(S) * 4.0 / UE_DOUBLE_PI;
		OutV = FMath::Atan(T) * 4.0 / UE_DOUBLE_PI;
		// The mapping is checked rather than trusted: a sign slip here would put feet on the wrong chunk.
		return FVector::DotProduct(CubeDir(OutFace, OutU, OutV), Dir.GetSafeNormal()) > 0.999999;
	}

	FPlanetTerrainSample SampleStructure(const FPlanetTerrainParams& P, const FVector& Dir, double MinWavelengthCm)
	{
		FPlanetTerrainSample Out;
		// The structure stops at the refinement's first wavelength whatever the caller asks for.
		MinWavelengthCm = FMath::Max(MinWavelengthCm, RefineStartCm);
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
		// Mountains sets how much of the land the ranges cover, not how tall they stand (MountainHeight does that): a
		// range's core climbs toward the relief, its highest peaks the world's tallest ground at about seven tenths of
		// it, and its foothills fall away over the mask's edge. Below 0.2 the ranges fade out altogether.
		const double MtnField = FbmN(Q * 1.7 + FVector(31.0, 0.0, 0.0), 3, 2.0, 0.5) * 0.5 + 0.5;
		const double MtnLo = 0.62 - 0.25 * FMath::Clamp((double)P.Mountains, 0.0, 1.0);
		const double MtnMask = SmoothStep(MtnLo, MtnLo + 0.2, MtnField) * Land * SmoothStep(0.0, 0.2, (double)P.Mountains) * FMath::Clamp((double)P.MountainHeight, 0.0, 1.0);
		double Mtn = 0.0;
		if (MtnMask > 0.001)
		{
			// Ridges every ~50 km on an Earth (base wavelength Lambda0/40), fourteen octaves at gain 0.5 (at 0.6 the
			// fine octaves drowned the ranges' own shape in an even roughness; at 0.5 the crest lines join into a
			// network round basins and valleys). The range mask above sets where. Eroded: the finer ridges fade on
			// the steep flanks the coarser ones build, so the crests stay sharp, the spurs below them run smooth and
			// the valleys between open out.
			// The range's height per unit of the ridge noise over the length of its first octave's unit: its slope.
			const double SlopeScale = 0.65 * MtnMask * P.ReliefCm / (Lambda0 / 40.0);
			Mtn = ErodedRidged(QW * 40.0 + FVector(3.0, 71.0, 0.0), Dir, OctavesFor(Lambda0 / 40.0, MinWavelengthCm, 14, 2.1), 14, 2.1, 0.5, 0.9, SlopeScale, MountainErode);
			Mtn = FMath::Pow(Mtn, 1.3);
		}

		// ---- maria ----
		const double Maria = P.Maria > 0.f ? SmoothStep(0.55, 0.7, FbmN(Q * 1.3 + FVector(17.0, 0.0, 0.0), 3, 2.0, 0.5) * 0.5 + 0.5) * P.Maria : 0.0;

		// Height in units of the relief (1 would be the relief itself; the tallest peaks stand at about 0.7).
		double H = Base * 0.35 + MtnMask * Mtn * 0.65;
		H *= (1.0 - 0.88 * Maria);

		// ---- canyons: valley lines where a noise field crosses zero, cut into uplifted tablelands ----
		double Carve = 0.0, Wall = 0.0, River = 0.0, Banks = 0.0;
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
			// |n| < HalfWidth is inside; the noise climbs at roughly 1.5 per unit off its zero line.
			const double HalfWidth = P.CanyonWidthCm * 0.5 / CellCm * 1.5;
			// Rough the line with finer noise so the walls are not glass smooth.
			const double Wig = FbmN(QW * F * 9.0 + FVector(3.0, 5.0, 7.0), OctavesFor(CellCm / 9.0, MinWavelengthCm, 5), 2.0, 0.5);
			// Finer wiggle (about 8 km down to 500 m) so the cliff lines are scalloped and gullied, not ruled.
			const double Wig2 = FbmN(QW * F * 48.0 + FVector(11.0, 2.0, 9.0), OctavesFor(CellCm / 48.0, MinWavelengthCm, 5), 2.0, 0.55);
			// And finer again (2 km down), so the floor edge and the side canyons wander by hundreds of metres.
			const double Wig3 = FbmN(QW * F * 200.0 + FVector(5.0, 23.0, 1.0), OctavesFor(CellCm / 200.0, MinWavelengthCm, 4), 2.0, 0.5);
			const double V0 = FMath::Abs(Perlin(QW * F + FVector(57.0, 0.0, 0.0)) + 0.18 * HalfWidth * Wig + 0.3 * HalfWidth * Wig2 + 0.08 * HalfWidth * Wig3);
			// Fluting: noise from 200 m down to the cell moves the wall line in and out, so the cliffs stand in
			// buttresses and alcoves instead of ruled faces. It is keyed to the coarse position across the wall as
			// well as along it, so each stratum weathers to its own pattern.
			const double T0 = FMath::Clamp((V0 - HalfWidth * 0.65) / (HalfWidth * 0.35), 0.0, 1.0);
			const double F3 = CellCm / (RefineStartCm * 2.0);
			const double Flute = FbmN(QW * F * F3 + FVector(T0 * 0.8, 0.0, 13.0), OctavesFor(RefineStartCm * 2.0, MinWavelengthCm, 10), 2.0, 0.58);
			// About fifteen metres at the 400 m wavelength on an Earth-size canyon, a metre at 3 m.
			const double V = FMath::Abs(V0 + 0.0045 * HalfWidth * Flute * (0.3 + T0));
			// Main canyon: flat floor, then the wall climbs through the outer third of the half-width as a
			// staircase of benches and cliffs (the strata), each bench a little narrower than the last so the
			// profile opens upward like a real cut.
			// The wall profile is a stack of eight strata of hashed thickness; hard layers stand as cliffs,
			// soft ones slump into scree slopes, and a slow noise along the canyon shifts the whole stack so the
			// benches wander rather than run level for a hundred kilometres.
			const double Shift = 0.1 * FbmN(QW * F * 20.0 + FVector(29.0, 7.0, 3.0), 3, 2.0, 0.5);
			const double T = FMath::Clamp((V - HalfWidth * 0.65) / (HalfWidth * 0.35) + Shift, 0.0, 1.0);
			// Two slow noises along the canyon (about 3 km and 1.5 km on an Earth); each stratum takes its own
			// mix of them as a lateral shift, so the benches vary in width independently instead of running as
			// parallel ledges.
			const double Sh1 = FbmN(QW * F * 120.0 + FVector(2.0, 61.0, 17.0), OctavesFor(CellCm / 120.0, MinWavelengthCm, 3), 2.0, 0.5);
			const double Sh2 = FbmN(QW * F * 250.0 + FVector(43.0, 3.0, 29.0), OctavesFor(CellCm / 250.0, MinWavelengthCm, 3), 2.0, 0.5);
			auto StairAt = [&](double Tt, float& StratumOut) -> double
			{
				const int32 Layers = 8;
				double Thick[8], Total = 0.0;
				for (int32 L = 0; L < Layers; ++L) { Thick[L] = 0.5 + ((Hash3(L * 7 + 1, 13, (int64)(P.Seed.X * 10.0)) & 255) / 255.0) * 1.2; Total += Thick[L]; }
				double Acc = 0.0, Y = 0.0;
				for (int32 L = 0; L < Layers; ++L)
				{
					const double Th = Thick[L] / Total;
					const double Hard = 0.35 + ((Hash3(L * 3 + 2, 29, (int64)(P.Seed.Y * 10.0)) & 255) / 255.0) * 0.65;
					const double A = ((Hash3(L * 5 + 3, 41, 7) & 255) / 127.5 - 1.0), B = ((Hash3(L * 11 + 5, 53, 9) & 255) / 127.5 - 1.0);
					const double Tl = Tt + 0.06 * (A * Sh1 + B * Sh2);
					if (Tl >= Acc + Th) { Y += Th; Acc += Th; continue; }
					if (Tl > Acc)
					{
						const double Fr = (Tl - Acc) / Th;
						// Hard layer: bench then sheer cliff. Soft layer: an even slope.
						Y += Th * FMath::Lerp(Fr, FMath::Pow(Fr, 1.0 + 8.0 * P.Strata), Hard);
						StratumOut = (float)((L + Fr) / Layers);
					}
					else { StratumOut = (float)((double)L / Layers); }
					break;
				}
				return FMath::Clamp(Y, 0.0, 1.0);
			};
			float StratumMain = 0.f, StratumTrib = 0.f;
			const double Stair = StairAt(T, StratumMain);
			Out.Stratum = StratumMain;
			const double Main = 1.0 - (P.Strata > 0.f ? Stair * 0.85 + T * 0.15 : SmoothStep(0.0, 1.0, T));
			// Inner gorge: a narrow slot down the middle of the floor, a quarter of the depth again.
			const double Gorge = (1.0 - SmoothStep(HalfWidth * 0.08, HalfWidth * 0.2, V)) * 0.25;
			// Side canyons: a second, much denser line set, a third of the width and four fifths of the
			// depth, cut into the walls and the plateau beside the canyon. Each fades out two widths from the
			// main line, its floor climbing back through the strata, so it ends in an amphitheatre. Lines that
			// run along the canyon rather than into it are held back by the angle between the two fields'
			// gradients (taken by central differences along two tangents of the direction).
			double Trib = 0.0;
			{
				// Sixty times the trunk's line density: a side canyon every five kilometres or so on an Earth.
				const double Kt = 60.0;
				const double HWt = HalfWidth * 0.3 * Kt;
				const FVector Q1 = QW * F + FVector(57.0, 0.0, 0.0);
				const FVector Qt = QW * F * Kt + FVector(0.0, 91.0, 0.0);
				const FVector Ta = FVector::CrossProduct(FMath::Abs(Dir.Z) < 0.9 ? FVector::ZAxisVector : FVector::XAxisVector, Dir).GetSafeNormal();
				const FVector Tb = FVector::CrossProduct(Dir, Ta);
				// A step of a few hundredths of each field's own cell.
				const double E1 = 0.03, E2 = 0.03;
				const double G1a = Perlin(Q1 + Ta * E1) - Perlin(Q1 - Ta * E1), G1b = Perlin(Q1 + Tb * E1) - Perlin(Q1 - Tb * E1);
				const double G2a = Perlin(Qt + Ta * E2) - Perlin(Qt - Ta * E2), G2b = Perlin(Qt + Tb * E2) - Perlin(Qt - Tb * E2);
				const double CosAng = FMath::Abs(G1a * G2a + G1b * G2b) / (FMath::Sqrt(G1a * G1a + G1b * G1b) * FMath::Sqrt(G2a * G2a + G2b * G2b) + 1e-12);
				const double Across = 1.0 - SmoothStep(0.45, 0.85, CosAng);
				const double V2 = FMath::Abs(Perlin(Qt) + 0.5 * HWt * Wig2 + 0.3 * HWt * Wig3 + 0.0045 * HWt * Flute * 0.6);
				const double T2 = FMath::Clamp((V2 - HWt * 0.5) / (HWt * 0.5) + Shift, 0.0, 1.0);
				const double StairT = StairAt(T2, StratumTrib);
				const double Profile = 1.0 - (P.Strata > 0.f ? StairT * 0.85 + T2 * 0.15 : SmoothStep(0.0, 1.0, T2));
				// How far each side canyon reaches into the plateau varies along the trunk, so the heads do not
				// line up.
				const double ReachEnd = HalfWidth * (1.5 + 1.2 * (FbmN(QW * F * 30.0 + FVector(0.0, 0.0, 41.0), 2, 2.0, 0.5) * 0.5 + 0.5));
				const double Reach = 1.0 - SmoothStep(ReachEnd, ReachEnd + HalfWidth * 0.5, V0);
				Trib = Profile * 0.8 * Reach * Across;
				if (Trib > Main) { Out.Stratum = StratumTrib; }
			}
			// Depth varies along the canyon so it opens and narrows.
			const double Along = 0.75 + 0.25 * (FbmN(QW * 3.0 + FVector(19.0, 0.0, 0.0), 3, 2.0, 0.5) * 0.5 + 0.5);
			Carve = (FMath::Max(Main, Trib) + Gorge) * CanyonZone * Along;
			// The river: down the middle of the inner gorge with a meander of its own, 140 m wide on an Earth-size
			// canyon, with a strip of wetter ground along its banks. Dry worlds and airless ones have none.
			if (!P.bAirless && P.Moisture > 0.2f)
			{
				// Bends every couple of kilometres on an Earth-size gorge, swinging a few hundred metres.
				const double Meander = FbmN(QW * F * 150.0 + FVector(0.0, 0.0, 77.0), OctavesFor(CellCm / 150.0, MinWavelengthCm, 4), 2.0, 0.55);
				const double Vr = FMath::Abs(V + 0.07 * HalfWidth * Meander);
				const double GorgeFloor = 1.0 - SmoothStep(HalfWidth * 0.05, HalfWidth * 0.09, V);
				River = (1.0 - SmoothStep(HalfWidth * 0.02, HalfWidth * 0.03, Vr)) * GorgeFloor * CanyonZone;
				Banks = (1.0 - SmoothStep(HalfWidth * 0.03, HalfWidth * 0.08, Vr)) * GorgeFloor * CanyonZone;
			}
			// The wall band: everything between the rim and the floor.
			Wall = SmoothStep(0.02, 0.12, Carve) * (1.0 - SmoothStep(0.88, 0.98, Carve));
		}
		const double CanyonUnits = P.CanyonDepthCm / FMath::Max(P.ReliefCm, 1.0);
		// Never cut below sea level: keep a twentieth of the plateau height under the floor.
		const double Cut = FMath::Min(Carve * CanyonUnits, H * 0.95);
		H -= Cut;
		// River bed nine metres under the gorge floor; the water stands three metres under it.
		const double RiverBedCm = 900.0, RiverSurfaceCm = 300.0;
		H -= River * RiverBedCm / FMath::Max(P.ReliefCm, 1.0);

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
			// Six scales: on a moon the first is 400 km basins, the last 1 km bowls that only the near chunks carry.
			const double Scales[6] = { 1.5, 5.0, 15.0, 45.0, 135.0, 405.0 };
			const double Gates[6] = { 0.78, 0.62, 0.55, 0.5, 0.45, 0.42 };
			const double Amps[6] = { 0.5, 0.25, 0.12, 0.06, 0.03, 0.015 };
			double CraterH = 0.0;
			for (int32 i = 0; i < 6; ++i)
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

		// ---- lineae: the long cracks of an icy crust, a trough between a pair of ridges, stained inside ----
		double Linea = 0.0;
		if (P.Lineae > 0.f)
		{
			// Three line sets at 3, 8 and 20 cycles round the globe (1,500, 600 and 250 km apart on a Europa), bands
			// 10, 6 and 3 km wide with ridges along their edges and a trough down the middle, bent by the domain
			// warp and a finer wiggle so none runs straight. The band stays a small fraction of its noise cell, or
			// the lines merge into a stain over everything.
			// A zero set covers a strip about twice the band width per noise cell, so even narrow bands cover a lot;
			// a slow gate keeps whole regions clear, as Europa's lineae crowd some provinces and skip others.
			const double Scales[3] = { 3.0, 8.0, 20.0 };
			const double Bands[3] = { 6.0e5, 3.0e5, 1.5e5 };
			const double RidgeCm[3] = { 1.5e4, 8.0e3, 4.0e3 };
			const double Gate = SmoothStep(0.42, 0.58, FbmN(QW * 1.7 + FVector(3.0, 47.0, 5.0), 2, 2.0, 0.5) * 0.5 + 0.5);
			for (int32 i = 0; i < 3 && Gate > 0.001; ++i)
			{
				// The mask is computed at every wavelength (a line is long even if it is narrow, and the feature
				// search samples coarsely); only the ridges wait for a mesh fine enough to carry them.
				const double BandCm = Bands[i];
				const double CellCm = Lambda0 / Scales[i];
				// Half the band in noise units: the noise climbs about 1.5 per cell off its zero line.
				const double W = 0.5 * BandCm / CellCm * 1.5;
				// The noise is squashed along one axis per set (from the seed), so its zero set runs as long arcs
				// across the globe along that axis instead of closing into loops a cell across.
				const FVector Ax = FVector(FMath::Sin(P.Seed.X * 0.37 + i * 2.1), FMath::Cos(P.Seed.Y * 0.53 + i * 1.3), FMath::Sin(P.Seed.Z * 0.29 + i * 0.7)).GetSafeNormal();
				const FVector Qs = QW - Ax * (FVector::DotProduct(QW, Ax) * 0.8);
				const double Wig = FbmN(QW * Scales[i] * 7.0 + FVector(23.0 + i * 9.0, 4.0, 61.0), OctavesFor(CellCm / 7.0, MinWavelengthCm, 4), 2.0, 0.5);
				const double V = FMath::Abs(Perlin(Qs * Scales[i] + FVector(31.0 * i, 0.0, 17.0 + i)) + 0.3 * W * Wig);
				const double T = FMath::Clamp(V / W, 0.0, 1.0);
				const double Ridge = SmoothStep(0.45, 0.7, T) * (1.0 - SmoothStep(0.8, 1.0, T));
				const double Trough = 1.0 - SmoothStep(0.0, 0.45, T);
				if (MinWavelengthCm <= BandCm) { H += (Ridge * RidgeCm[i] - Trough * RidgeCm[i] * 0.35) / FMath::Max(P.ReliefCm, 1.0) * P.Lineae * Gate; }
				Linea = FMath::Max(Linea, (1.0 - SmoothStep(0.75, 1.0, T)) * Gate);
			}
		}

		// ---- hills: the last of the structure, from a few kilometres down to the refinement floor ----
		{
			const double DetailLambda = Lambda0 / 60.0;
			const int32 Oct = OctavesFor(DetailLambda, MinWavelengthCm, 8, 2.0);
			const double Rocky = FMath::Clamp(MtnMask * Mtn * 2.0 + Wall * 0.8 + Carve * 0.5 + (P.bAirless ? 0.3 : 0.0), 0.0, 1.0);
			// Rock keeps its roughness down the octaves, plains smooth out.
			const double D = FbmN(QW * 60.0 + FVector(0.0, 0.0, 41.0), Oct, 2.0, 0.5 + 0.12 * Rocky);
			// The gorge floor around the river stays level so the water does not climb the hills.
			H += D * (0.01 + 0.02 * Rocky) * (0.3 + 0.7 * Land) * (1.0 - Banks);
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
		Out.WaterCm = 0.0;
		Out.River = (float)River;
		if (River > 1.0 / 3.0 && HeightCm > 0.0)
		{
			// Bed below the water line: the surface sits a fixed distance under the undisturbed floor.
			Out.bWater = true;
			Out.WaterCm = HeightCm + River * RiverBedCm - RiverSurfaceCm;
		}

		// ---- biome ----
		const double Hr = FMath::Clamp(HeightCm / FMath::Max(P.ReliefCm, 1.0), 0.0, 1.0);
		const double WetField = FbmN(Q * 2.5 + FVector(43.0, 0.0, 0.0), 3, 2.0, 0.5) * 0.5 + 0.5;
		double Wet = FMath::Clamp(P.Moisture * 1.4 * (0.5 + WetField) - Hr * 0.6, 0.0, 1.0);
		Wet = FMath::Max(Wet, Banks * 0.8);
		if (P.bAirless) { Wet = 0.0; }
		const double Lat = FMath::Abs(Dir.Z);
		const double Polar = SmoothStep(P.IceCapStart - 0.06, P.IceCapStart + 0.04, Lat);
		// Snow above the snowline for the latitude, a few hundred metres of patchy cover below it to deep snow above.
		const double Line = SnowlineCm(P, Dir);
		const double Band = SnowBandCm(Line);
		const double Snow = P.bAirless || P.bLava || P.SnowlineCm <= 0.0 ? 0.0 : SmoothStep(Line - Band, Line + 2.0 * Band, HeightCm);
		Out.Ice = (float)FMath::Clamp(FMath::Max(Polar, Snow) * (P.bLava ? 0.0 : 1.0), 0.0, 1.0);
		Out.Rock = (float)FMath::Clamp(MtnMask * Mtn * 2.5 + Wall + CraterShade * 0.5, 0.0, 1.0);
		Out.Wet = (float)Wet;
		Out.Canyon = (float)FMath::Clamp(Carve, 0.0, 1.0);
		Out.Wall = (float)FMath::Clamp(Wall, 0.0, 1.0);
		Out.CutCm = (float)(Cut * P.ReliefCm);
		Out.Table = (float)FMath::Clamp(Tableland, 0.0, 1.0);
		Out.Crater = (float)FMath::Clamp(CraterShade, 0.0, 1.0);
		Out.Linea = (float)FMath::Clamp(Linea * P.Lineae, 0.0, 1.0);
		return Out;
	}

	void Refine(const FPlanetTerrainParams& P, const FVector& Dir, double Slope, double MinWavelengthCm, FPlanetTerrainSample& S)
	{
		S.Slope = (float)FMath::Min(Slope, 4.0);
		if (S.bWater || (MinWavelengthCm > 0.0 && MinWavelengthCm >= RefineStartCm)) { return; }
		const double R = P.RadiusCm;
		// Lattice coordinate with 200 m cells; the seed keeps it off the structure's lattices.
		const FVector Q = Dir * (R / RefineStartCm) + P.Seed * 7.3;
		// Three regimes of the coarse slope: cliff (over about 40 degrees), the slopes under it, the flats.
		const double Cliff = SmoothStep(0.7, 1.3, Slope);
		const double Talus = SmoothStep(0.18, 0.5, Slope) * (1.0 - Cliff);
		const double Flat = 1.0 - SmoothStep(0.12, 0.4, Slope);
		// Mountain and crater rock is rock on gentler ground too.
		const double Rocky = FMath::Max(Cliff, SmoothStep(0.45, 0.9, (double)S.Rock) * SmoothStep(0.3, 0.7, Slope));
		// Erosion smooths an atmosphere's flats; airless regolith stays lumpier.
		const double Air = P.bAirless ? 1.4 : 1.0;
		double Add = 0.0;
		// Fractured rock: ridged noise from 50 m down, sharp creases, about four metres at the top octave.
		if (Rocky > 0.002)
		{
			const int32 Oct = OctavesFor(RefineStartCm / 4.0, MinWavelengthCm, 12, 2.0);
			const double Rk = Ridged(Q * 4.0 + FVector(3.7, 1.1, 9.2), Oct, 2.0, 0.62, 0.8) - 0.45;
			Add += Rk * 450.0 * Rocky;
		}
		// Talus and colluvium: smooth lumps from 60 m down, with boulders where the cell can show them.
		if (Talus > 0.002)
		{
			const int32 Oct = OctavesFor(RefineStartCm / 3.3, MinWavelengthCm, 10, 2.0);
			Add += FbmN(Q * 3.3 + FVector(0.0, 5.5, 2.2), Oct, 2.0, 0.5) * 320.0 * Talus * Air;
			if (MinWavelengthCm > 0.0 && MinWavelengthCm < 800.0)
			{
				// Boulders: the crater field's feature points with an upturned profile, in two sizes, and only in
				// the patches a slow noise picks out so they lie in fans and trains rather than evenly.
				const double Field = SmoothStep(-0.05, 0.35, FbmN(Q * 2.0 + FVector(1.0, 9.0, 4.0), 2, 2.0, 0.5)) * Talus;
				if (Field > 0.01)
				{
					const FCraterHit Big = NearestCrater(Dir * (R / 2200.0) + P.Seed * 3.1, 0.12 * Field);
					if (Big.Dist < Big.Radius) { const double r = Big.Dist / Big.Radius; Add += FMath::Pow(FMath::Max(1.0 - r * r, 0.0), 0.6) * Big.Depth * 450.0 * Field; }
					if (MinWavelengthCm < 300.0)
					{
						const FCraterHit Small = NearestCrater(Dir * (R / 600.0) + P.Seed * 5.7, 0.3 * Field);
						if (Small.Dist < Small.Radius) { const double r = Small.Dist / Small.Radius; Add += FMath::Pow(FMath::Max(1.0 - r * r, 0.0), 0.6) * Small.Depth * 140.0 * Field; }
					}
				}
			}
		}
		// Soil, sand and regolith: gentle hummocks from 200 m down to the decimetre.
		if (Flat > 0.002)
		{
			const int32 Oct = OctavesFor(RefineStartCm, MinWavelengthCm, 11, 2.0);
			const double Dry = 1.0 - 0.5 * S.Wet;
			Add += FbmN(Q + FVector(7.7, 0.0, 4.4), Oct, 2.0, 0.45) * 150.0 * Flat * Dry * Air;
		}
		// Keep the coast where it is: the refinement fades out over the first few metres above the sea.
		if (!P.bAirless) { Add *= SmoothStep(0.0, 600.0, S.HeightCm); }
		S.HeightCm += Add;
		S.GroundCm += Add;
		S.Rock = (float)FMath::Clamp(FMath::Max((double)S.Rock, Rocky * 0.9 + Talus * 0.35), 0.0, 1.0);
	}

	FPlanetTerrainSample Sample(const FPlanetTerrainParams& P, const FVector& Dir, double MinWavelengthCm)
	{
		FPlanetTerrainSample S = SampleStructure(P, Dir, MinWavelengthCm);
		if (S.bWater || (MinWavelengthCm > 0.0 && MinWavelengthCm >= RefineStartCm)) { return S; }
		// Slope from structure samples SlopeStepCm apart along the tangent plane, the distance the chunks use.
		const double EpsCm = FMath::Max(MinWavelengthCm * 0.5, SlopeStepCm * 0.5);
		const FVector E = FVector::CrossProduct(FMath::Abs(Dir.Z) < 0.9 ? FVector::ZAxisVector : FVector::XAxisVector, Dir).GetSafeNormal();
		const FVector N = FVector::CrossProduct(Dir, E).GetSafeNormal();
		const double He0 = SampleStructure(P, (Dir - E * (EpsCm / P.RadiusCm)).GetSafeNormal(), MinWavelengthCm).HeightCm;
		const double He1 = SampleStructure(P, (Dir + E * (EpsCm / P.RadiusCm)).GetSafeNormal(), MinWavelengthCm).HeightCm;
		const double Hn0 = SampleStructure(P, (Dir - N * (EpsCm / P.RadiusCm)).GetSafeNormal(), MinWavelengthCm).HeightCm;
		const double Hn1 = SampleStructure(P, (Dir + N * (EpsCm / P.RadiusCm)).GetSafeNormal(), MinWavelengthCm).HeightCm;
		const double Gx = (He1 - He0) / (2.0 * EpsCm), Gy = (Hn1 - Hn0) / (2.0 * EpsCm);
		Refine(P, Dir, FMath::Sqrt(Gx * Gx + Gy * Gy), MinWavelengthCm, S);
		return S;
	}

	double SurfaceHeightCm(const FPlanetTerrainParams& P, const FVector& Dir)
	{
		const FPlanetTerrainSample S = Sample(P, Dir, 0.0);
		return FMath::Max(S.HeightCm, 0.0);
	}
}
