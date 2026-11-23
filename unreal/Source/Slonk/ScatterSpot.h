// Slonk - what the scatter reads of a spot on a chunk's drawn ground, and the hashed draws it places things by: the
// helpers the rocks and plants are placed with (TerrainScatter.cpp), kept here for whatever else is placed on the
// chunks' ground or by their lattices. A cell's hash gives its draws, the chunk's fields interpolated on the drawn
// triangle give the ground a piece stands on, and the climate, the forest and the colour of the living ground there
// follow from those, every one a pure function of the planet and the chunk.

#pragma once

#include "CoreMinimal.h"
#include "PlanetTerrain.h"
#include "TerrainScatter.h"

namespace SlonkScatter
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

	/** What a point of the chunk stands on: the chunk's fields there, bilinear, and its place on the drawn mesh. */
	struct FSpot
	{
		double Slope = 0, Rock = 0, Wet = 0, Ice = 0, Canyon = 0, Wall = 0, Crater = 0, Table = 0, River = 0, HeightCm = 0, Stratum = 0;
		/** A strange world's own geology there (FPlanetTerrainSample::Geo): nothing grows on it. */
		double Geo = 0;
		/** The share of what would grow there that does: all of it off the geology (exactly 1 where there is none), none
		 *  where the ground is the geology's, as the material draws it (terrain_material.py's geo). */
		double Grows() const { const double T = FMath::Clamp((Geo - 0.1) / 0.4, 0.0, 1.0); return 1.0 - T * T * (3.0 - 2.0 * T); }
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
			S.Geo = Bi(SA.Geo, SB.Geo, SC.Geo, SD.Geo);
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
	inline FQuat StandingFrame(const FSpot& Sp, double ToGround, double TiltDeg, FDraw& D)
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

	/** The climate a plant grows in, from the world's snowline (SlonkTerrain::SnowlineCm), so the treeline is a
	 *  height for the latitude, as on an Earth, not a fraction of the world's relief. */
	struct FClimate
	{
		/** The height over the snowline there: 0 at the sea, 1 at the snowline. The treeline is about 0.75 of it. */
		double Alpine = 0.0;
		/** How cold it is for what grows, 0 in a warm lowland: the snowline's drop toward the poles, and the height. */
		double Cold = 0.0;
		/** On a tidally locked world (FPlanetTerrainParams::Climate Locked): the air's temperature there as a share of the
		 *  solvent's liquid range (SlonkTerrain::TemperatureN), which bakes the forest out past three quarters of it
		 *  and the grass past 0.95, and leaves a tundra under the trees' cold limit. A spinning world's plants read no
		 *  temperature of their own: they follow its snowline, which its warmth moves, and the ground's wet and ice,
		 *  which its heat dries and its cold freezes in the field (a warm desert's equator dries further), so a faintly
		 *  strange world grows nearly what its strangeness-0 twin does. */
		bool bClimate = false;
		double Heat = 0.0;
	};
	inline FClimate ClimateAt(const FPlanetTerrainParams& P, const FSpot& Sp)
	{
		FClimate C;
		// A world with no snow still has a treeline: as if its snowline stood at its relief.
		FPlanetTerrainParams Q = P;
		if (Q.SnowlineCm <= 0.0) { Q.SnowlineCm = FMath::Max(P.ReliefCm, 1.0); }
		const double Line = SlonkTerrain::SnowlineCm(Q, Sp.Up);
		C.Alpine = FMath::Clamp(FMath::Max(Sp.HeightCm, 0.0) / FMath::Max(Line, 1.0), 0.0, 1.5);
		C.Cold = FMath::Clamp(1.0 - Line / Q.SnowlineCm + 0.5 * C.Alpine, 0.0, 1.5);
		if (P.Climate == EClimateModel::Locked)
		{
			C.bClimate = true;
			C.Heat = SlonkTerrain::TemperatureN(P, Sp.Up, Sp.HeightCm);
		}
		return C;
	}

	/** Stands of forest with clearings between them, at the kilometre scale. */
	inline double ForestAt(const FPlanetTerrainParams& P, const FSpot& Sp)
	{
		return SmoothStep(-0.15, 0.25, SlonkTerrain::Fbm(Sp.Up * (P.RadiusCm / 120000.0) + P.Seed * 3.7, 3));
	}

	/** How likely a tree is to stand in its lattice cell at a spot, before the world's life and the density setting:
	 *  the forest's stands where it is wet and not steep or rocky, a line of trees along a canyon's river whatever
	 *  the forest does, none above the treeline or through lying snow. The trees placed and the ground's cover
	 *  (SlonkScatter::TreeCover) both read it. */
	inline double TreeDensity(const FSpot& Sp, const FClimate& Clim, double Forest)
	{
		const double SnowLying = SmoothStep(0.2, 0.6, Sp.Ice);
		const double D = (Forest * SmoothStep(0.3, 0.65, Sp.Wet) * (1.0 - SmoothStep(0.35, 0.75, Sp.Slope)) * (1.0 - 0.7 * Sp.Rocky) * 0.8
			+ Sp.Gorge * Sp.Wet * 0.35)
			* (1.0 - SmoothStep(0.65, 0.9, Clim.Alpine)) * (1.0 - SnowLying) * Sp.Grows();
		// A climate of its own: no tree where the air runs past three quarters of the liquid range, and none in the tundra
		// just above freezing, where only the grass holds on.
		return Clim.bClimate ? D * (1.0 - SmoothStep(0.45, 0.75, Clim.Heat)) * SmoothStep(0.0, 0.06, Clim.Heat) : D;
	}

	/** How likely a strange world's giant is to stand in its lattice cell at a spot, before the world's life and the
	 *  density setting: in loose groves some kilometres across, on the wet, level ground the forests take and out over
	 *  the meadows; none on rock to speak of, above the treeline or through lying snow, nor past the trees' own limits of
	 *  heat and cold on a world with a climate of its own. */
	inline double GiantDensity(const FPlanetTerrainParams& P, const FSpot& Sp, const FClimate& Clim)
	{
		const double Groves = SmoothStep(0.05, 0.45, SlonkTerrain::Fbm(Sp.Up * (P.RadiusCm / 900000.0) + P.Seed * 5.3 + FVector(2.0, 7.0, 4.0), 2));
		const double SnowLying = SmoothStep(0.2, 0.6, Sp.Ice);
		const double D = Groves * SmoothStep(0.2, 0.5, Sp.Wet) * Sp.Flat * (1.0 - 0.8 * Sp.Rocky) * (1.0 - SnowLying) * (1.0 - SmoothStep(0.6, 0.85, Clim.Alpine)) * 0.45 * Sp.Grows();
		return Clim.bClimate ? D * (1.0 - SmoothStep(0.45, 0.75, Clim.Heat)) * SmoothStep(0.0, 0.06, Clim.Heat) : D;
	}

	/** How parched a plant is, 0 lush to 1 dry: the per-instance dryness the foliage material yellows it by. */
	inline double DrynessAt(const FSpot& Sp) { return FMath::Clamp(1.0 - Sp.Wet * 1.3, 0.0, 1.0); }

	/** The colour of the living ground where a plant stands: the material's wet colour, browned toward its dry one. */
	inline FLinearColor FoliageAt(const FScatterPalette& Pal, double Dry, double Vary)
	{
		return FMath::Lerp(Pal.Land, Pal.Land2, (float)(Dry * 0.55)) * (float)Vary;
	}
}
