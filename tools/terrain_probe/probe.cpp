// Standalone probe of the planet height field: compiles unreal/Source/Slonk/PlanetTerrain.cpp against the stand-in
// CoreMinimal.h here and prints statistics and profiles, so the field can be tuned without an editor rebuild.
//   c++ -O2 -std=c++17 -I. probe.cpp ../../unreal/Source/Slonk/PlanetTerrain.cpp -o probe && ./probe
#include "CoreMinimal.h"
#include "PlanetTerrain.h"
#include <cstdio>
#include <vector>
#include <random>

static FPlanetTerrainParams Earthlike()
{
	FPlanetTerrainParams P;
	P.Seed = FVector(37.1, 61.7, 12.9);
	P.RadiusCm = 5.707e8;
	P.ReliefCm = 0.0016 * P.RadiusCm;
	P.OceanDepthCm = 0.0008 * P.RadiusCm;
	P.SeaLevel = 0.59f; P.ContinentScale = 3.f;
	P.Mountains = 0.6f; P.Canyons = 0.5f; P.Strata = 0.6f; P.Moisture = 0.55f;
	P.CanyonDepthCm = 1.6e5 * (P.RadiusCm / 6.371e8); P.CanyonWidthCm = 7.0e5 * (P.RadiusCm / 6.371e8);
	P.Craters = 0.1f;
	return P;
}

int main(int argc, char** argv)
{
	FPlanetTerrainParams P = Earthlike();
	std::mt19937_64 rng(7);
	std::uniform_real_distribution<double> U(-1.0, 1.0);
	const int N = 20000;
	FVector C;
	int land = 0, canyon = 0, table = 0; double hmax = 0, hsum = 0; FVector bestCanyon; double bestC = -1; FVector bestMtn; double bestH = -1;
	for (int i = 0; i < N; ++i)
	{
		FVector d(U(rng), U(rng), U(rng)); if (d.Size() < 0.1) continue; d = d.GetSafeNormal();
		FPlanetTerrainSample S = SlonkTerrain::Sample(P, d, 2000.0 * 100.0);
		if (!S.bWater) { ++land; hsum += S.HeightCm; hmax = std::max(hmax, S.HeightCm); if (S.HeightCm > bestH) { bestH = S.HeightCm; bestMtn = d; } }
		if (S.Canyon > 0.5) ++canyon;
		if (S.Canyon > bestC) { bestC = S.Canyon; bestCanyon = d; }
	}
	// Deepest cut on the globe and the canyon width there (rim to rim, where the cut exceeds a tenth of its depth).
	{
		double bestCut = 0; FVector at;
		for (int i = 0; i < N * 3; ++i)
		{
			FVector d(U(rng), U(rng), U(rng)); if (d.Size() < 0.1) continue; d = d.GetSafeNormal();
			FPlanetTerrainSample S = SlonkTerrain::Sample(P, d, 2000.0 * 100.0);
			if (!S.bWater && S.CutCm > bestCut) { bestCut = S.CutCm; at = d; }
		}
		FVector E2 = FVector::CrossProduct(FVector::ZAxisVector, at).GetSafeNormal();
		double wE = 0; for (int k = -200; k <= 200; ++k) { FVector d = (at + E2 * (k * 100.0 * 100.0 / P.RadiusCm)).GetSafeNormal(); if (SlonkTerrain::Sample(P, d, 0.0).CutCm > bestCut * 0.1) wE += 0.1; }
		FVector N2 = FVector::CrossProduct(at, E2).GetSafeNormal();
		double wN = 0; for (int k = -200; k <= 200; ++k) { FVector d = (at + N2 * (k * 100.0 * 100.0 / P.RadiusCm)).GetSafeNormal(); if (SlonkTerrain::Sample(P, d, 0.0).CutCm > bestCut * 0.1) wN += 0.1; }
		printf("deepest cut %.0f m; width of the cut there E-W %.1f km, N-S %.1f km (the smaller is the trunk)\n", bestCut / 100.0, wE, wN);
		C = at;
	}
	printf("land %.1f%%  mean land height %.0f m  max %.0f m  canyon-floor fraction %.2f%%  best canyon mask %.2f\n", 100.0 * land / N, hsum / std::max(land, 1) / 100.0, hmax / 100.0, 100.0 * canyon / N, bestC);
	// Profile across the best canyon point: walk east-west and north-south 25 km each way in 250 m steps.
	if (C.IsNearlyZero()) C = bestCanyon;
	FVector E = FVector::CrossProduct(FVector::ZAxisVector, C).GetSafeNormal();
	FVector Nn = FVector::CrossProduct(C, E).GetSafeNormal();
	for (int axis = 0; axis < 2; ++axis)
	{
		printf("profile %s through canyon point (m above sea, every 1 km from -25 km to +25 km):\n", axis ? "N-S" : "E-W");
		for (int k = -25; k <= 25; ++k)
		{
			FVector d = (C + (axis ? Nn : E) * (k * 1.0e5 / P.RadiusCm)).GetSafeNormal();
			FPlanetTerrainSample S = SlonkTerrain::Sample(P, d, 0.0);
			printf("%6.0f%s", S.HeightCm / 100.0, S.Canyon > 0.5 ? "*" : (S.Wall > 0.3 ? "|" : " "));
			if ((k + 25) % 10 == 9) printf("\n");
		}
		printf("\n");
	}
	// Fine profile: 100 m steps across 6 km at the canyon point, E-W.
	printf("fine E-W profile (100 m steps, -3..+3 km):\n");
	for (int k = -30; k <= 30; ++k)
	{
		FVector d = (C + E * (k * 1.0e4 / P.RadiusCm)).GetSafeNormal();
		FPlanetTerrainSample S = SlonkTerrain::Sample(P, d, 0.0);
		printf("%5.0f ", S.HeightCm / 100.0);
		if ((k + 30) % 15 == 14) printf("\n");
	}
	// Slope statistics on the fine profile: max rise per 100 m step.
	{
		double maxRise = 0, prev = 0;
		for (int k = -30; k <= 30; ++k)
		{
			FVector d = (C + E * (k * 1.0e4 / P.RadiusCm)).GetSafeNormal();
			double h = SlonkTerrain::Sample(P, d, 0.0).HeightCm / 100.0;
			if (k > -30) maxRise = std::max(maxRise, std::fabs(h - prev));
			prev = h;
		}
		printf("steepest 100 m step across the canyon: %.0f m (%.0f deg)\n", maxRise, std::atan2(maxRise, 100.0) * 57.3);
	}
	// Wall profile: from the canyon point walk east until the canyon mask drops, then print 25 m steps across the wall.
	{
		double kEdge = 0;
		for (double k = 0; k < 20000; k += 100) { FVector d = (C + E * (k * 100.0 / P.RadiusCm)).GetSafeNormal(); if (SlonkTerrain::Sample(P, d, 0.0).Canyon < 0.5) { kEdge = k; break; } }
		printf("wall profile E of the canyon point, 25 m steps from %.0f m (floor edge) outward 4 km:\n", kEdge);
		double prev = 0, maxRise = 0;
		for (int i = 0; i <= 160; ++i)
		{
			FVector d = (C + E * ((kEdge - 500.0 + i * 25.0) * 100.0 / P.RadiusCm)).GetSafeNormal();
			double h = SlonkTerrain::Sample(P, d, 0.0).HeightCm / 100.0;
			if (i) maxRise = std::max(maxRise, h - prev);
			prev = h;
			printf("%5.0f%s", h, (i % 20 == 19) ? "\n" : " ");
		}
		printf("\nsteepest 25 m step on the wall: %.0f m (%.0f deg)\n", maxRise, std::atan2(maxRise, 25.0) * 57.3);
	}
	printf("\nhighest point %.0f m; 100 m steps across it:\n", bestH / 100.0);
	E = FVector::CrossProduct(FVector::ZAxisVector, bestMtn).GetSafeNormal();
	for (int k = -20; k <= 20; ++k)
	{
		FVector d = (bestMtn + E * (k * 1.0e4 / P.RadiusCm)).GetSafeNormal();
		printf("%5.0f ", SlonkTerrain::Sample(P, d, 0.0).HeightCm / 100.0);
		if ((k + 20) % 15 == 14) printf("\n");
	}
	printf("\n");
	return 0;
}
