// Standalone probe of the planet height field: compiles unreal/Source/Slonk/PlanetTerrain.cpp against the stand-in
// CoreMinimal.h here and prints statistics and profiles, so the field can be tuned without an editor rebuild.
//   c++ -O2 -std=c++17 -I. probe.cpp ../../unreal/Source/Slonk/PlanetTerrain.cpp -o probe && ./probe
#include "CoreMinimal.h"
#include "PlanetTerrain.h"
#include <cstdio>
#include <vector>
#include <random>
#include <chrono>
#include <string>

// Hillshade of a square patch sampled like a terrain chunk (structure grid, slope by finite differences, refinement),
// written as a PGM beside the probe. Centre is a unit direction; CellCm the sample spacing; N the samples per side.
static void Patch(const FPlanetTerrainParams& P, const FVector& Centre, double CellCm, int N, const char* File, bool bRefine = true)
{
	FVector E = FVector::CrossProduct(FVector::ZAxisVector, Centre).GetSafeNormal();
	FVector Nn = FVector::CrossProduct(Centre, E).GetSafeNormal();
	const int W = N + 4;
	std::vector<double> H(W * W); std::vector<FPlanetTerrainSample> S(W * W); std::vector<FVector> D(W * W);
	for (int j = 0; j < W; ++j) for (int i = 0; i < W; ++i)
	{
		FVector d = (Centre + E * ((i - W / 2) * CellCm / P.RadiusCm) + Nn * ((j - W / 2) * CellCm / P.RadiusCm)).GetSafeNormal();
		D[j * W + i] = d; S[j * W + i] = SlonkTerrain::SampleStructure(P, d, CellCm * 2.0); H[j * W + i] = S[j * W + i].HeightCm;
	}
	double hmin = 1e30, hmax = -1e30, smax = 0;
	for (int j = 1; j < W - 1; ++j) for (int i = 1; i < W - 1; ++i)
	{
		int k = j * W + i;
		double gx = (H[k + 1] - H[k - 1]) / (2 * CellCm), gy = (H[k + W] - H[k - W]) / (2 * CellCm);
		if (bRefine) SlonkTerrain::Refine(P, D[k], std::sqrt(gx * gx + gy * gy), CellCm * 2.0, S[k]);
		smax = std::max(smax, std::sqrt(gx * gx + gy * gy));
	}
	std::vector<double> Hf(W * W);
	for (int k = 0; k < W * W; ++k) { Hf[k] = S[k].bWater ? S[k].WaterCm : S[k].HeightCm; hmin = std::min(hmin, Hf[k]); hmax = std::max(hmax, Hf[k]); }
	FILE* f = fopen(File, "wb"); fprintf(f, "P5\n%d %d\n255\n", N, N);
	int steep = 0;
	for (int j = 2; j < W - 2; ++j) for (int i = 2; i < W - 2; ++i)
	{
		int k = j * W + i;
		double gx = (Hf[k + 1] - Hf[k - 1]) / (2 * CellCm), gy = (Hf[k + W] - Hf[k - W]) / (2 * CellCm);
		if (std::sqrt(gx * gx + gy * gy) > 1.0) ++steep;
		// Light from the upper left, 40 degrees up.
		double nx = -gx, ny = -gy, nz = 1.0, nl = std::sqrt(nx * nx + ny * ny + nz * nz);
		double lx = -0.54, ly = -0.54, lz = 0.64;
		double sh = std::max(0.0, (nx * lx + ny * ly + nz * lz) / nl);
		unsigned char v = (unsigned char)std::min(255.0, 20 + 235 * sh);
		fputc(v, f);
	}
	fclose(f);
	printf("patch %s: %d x %d at %.2f m, height %.0f..%.0f m, structure slope max %.2f, %.1f%% of cells over 45 deg\n", File, N, N, CellCm / 100.0, hmin / 100.0, hmax / 100.0, smax, 100.0 * steep / (N * N));
}

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
		// Two stages: the random sweep finds tableland near the canyon lines, then a 1 km grid 40 km across the
		// best twenty candidates finds the deepest cut itself (a 7 km trunk hides from a random sweep).
		double bestCut = 0; FVector at;
		std::vector<std::pair<double, FVector>> cands;
		for (int i = 0; i < N * 3; ++i)
		{
			FVector d(U(rng), U(rng), U(rng)); if (d.Size() < 0.1) continue; d = d.GetSafeNormal();
			FPlanetTerrainSample S = SlonkTerrain::Sample(P, d, 2000.0 * 100.0);
			if (!S.bWater && S.Table > 0.3) cands.push_back({S.CutCm + S.Table * 1e4, d});
		}
		std::sort(cands.begin(), cands.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
		for (size_t c = 0; c < std::min<size_t>(20, cands.size()); ++c)
		{
			const FVector& g = cands[c].second;
			FVector e = FVector::CrossProduct(FVector::ZAxisVector, g).GetSafeNormal(), n = FVector::CrossProduct(g, e).GetSafeNormal();
			for (int i = -20; i <= 20; ++i) for (int j = -20; j <= 20; ++j)
			{
				FVector d = (g + e * (i * 1.0e5 / P.RadiusCm) + n * (j * 1.0e5 / P.RadiusCm)).GetSafeNormal();
				FPlanetTerrainSample S = SlonkTerrain::Sample(P, d, 2000.0 * 100.0);
				if (!S.bWater && S.CutCm > bestCut) { bestCut = S.CutCm; at = d; }
			}
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
	double kSteep = 0;
	// Wall profile: from the canyon point walk east until the canyon mask drops, then print 25 m steps across the wall.
	{
		double kEdge = 0;
		for (double k = 0; k < 20000; k += 100) { FVector d = (C + E * (k * 100.0 / P.RadiusCm)).GetSafeNormal(); if (SlonkTerrain::Sample(P, d, 0.0).Canyon < 0.5) { kEdge = k; break; } }
		printf("wall profile E of the canyon point, 25 m steps from %.0f m (floor edge) outward 4 km:\n", kEdge);
		double prev = 0, maxRise = 0; int iSteep = 0;
		for (int i = 0; i <= 160; ++i)
		{
			FVector d = (C + E * ((kEdge - 500.0 + i * 25.0) * 100.0 / P.RadiusCm)).GetSafeNormal();
			double h = SlonkTerrain::Sample(P, d, 0.0).HeightCm / 100.0;
			if (i && h - prev > maxRise) { maxRise = h - prev; iSteep = i; }
			prev = h;
			printf("%5.0f%s", h, (i % 20 == 19) ? "\n" : " ");
		}
		printf("\nsteepest 25 m step on the wall: %.0f m (%.0f deg)\n", maxRise, std::atan2(maxRise, 25.0) * 57.3);
		kSteep = kEdge - 500.0 + iSteep * 25.0;
	}
	// Timing of the two stages at a 50 cm wavelength.
	{
		auto t0 = std::chrono::steady_clock::now();
		double acc = 0; const int M = 4000;
		for (int i = 0; i < M; ++i) { FVector d = (C + E * ((i % 63) * 100.0 / P.RadiusCm) + Nn * ((i / 63) * 100.0 / P.RadiusCm)).GetSafeNormal(); acc += SlonkTerrain::SampleStructure(P, d, 50.0).HeightCm; }
		auto t1 = std::chrono::steady_clock::now();
		for (int i = 0; i < M; ++i) { FVector d = (C + E * ((i % 63) * 100.0 / P.RadiusCm) + Nn * ((i / 63) * 100.0 / P.RadiusCm)).GetSafeNormal(); FPlanetTerrainSample S = SlonkTerrain::SampleStructure(P, d, 50.0); SlonkTerrain::Refine(P, d, 0.9, 50.0, S); acc += S.HeightCm; }
		auto t2 = std::chrono::steady_clock::now();
		for (int i = 0; i < M; ++i) { FVector d = (C + E * ((i % 63) * 100.0 / P.RadiusCm) + Nn * ((i / 63) * 100.0 / P.RadiusCm)).GetSafeNormal(); acc += SlonkTerrain::SampleStructure(P, d, 2.0e5).HeightCm; }
		auto t3 = std::chrono::steady_clock::now();
		printf("per sample: structure %.2f us, structure+refine %.2f us, structure at 2 km %.2f us (%.0f)\n",
			std::chrono::duration<double, std::micro>(t1 - t0).count() / M, std::chrono::duration<double, std::micro>(t2 - t1).count() / M,
			std::chrono::duration<double, std::micro>(t3 - t2).count() / M, acc * 0.0);
	}
	// Hillshade patches: the wall (centred on the floor edge found above) at 1 m, and the floor/rim at 4 m.
	{
		double kEdge = 0;
		for (double k = 0; k < 20000; k += 100) { FVector d = (C + E * (k * 100.0 / P.RadiusCm)).GetSafeNormal(); if (SlonkTerrain::Sample(P, d, 2.0e4).Canyon < 0.5) { kEdge = k; break; } }
		std::string out = argc > 1 ? argv[1] : ".";
		FVector wall = (C + E * ((kEdge + 400.0) * 100.0 / P.RadiusCm)).GetSafeNormal();
		Patch(P, wall, 100.0, 512, (out + "/patch_wall_1m.pgm").c_str());
		Patch(P, wall, 100.0, 512, (out + "/patch_wall_1m_structure.pgm").c_str(), false);
		Patch(P, wall, 400.0, 512, (out + "/patch_wall_4m.pgm").c_str());
		Patch(P, C, 400.0, 512, (out + "/patch_floor_4m.pgm").c_str());
		// The canyon at the plateau scale: 20 km across at 40 m cells, structure only, for the side canyons.
		Patch(P, C, 4000.0, 512, (out + "/patch_canyon_40m.pgm").c_str(), false);
		Patch(P, C, 1600.0, 512, (out + "/patch_canyon_16m.pgm").c_str(), false);
		Patch(P, bestMtn, 100.0, 512, (out + "/patch_mtn_1m.pgm").c_str());
		// River: the first sample with the river mask high, then the hillshade across it at 4 m and 1 m.
		{
			// Rivers are 140 m wide: random sampling never lands on one. Comb tablelands for the deepest cut (the
			// inner gorge), then comb 50 m cells around it for the river.
			FVector river; bool found = false;
			std::vector<FVector> tables;
			for (int i = 0; i < N && tables.size() < 12; ++i)
			{
				FVector d(U(rng), U(rng), U(rng)); if (d.Size() < 0.1) continue; d = d.GetSafeNormal();
				FPlanetTerrainSample S = SlonkTerrain::SampleStructure(P, d, 2.0e5);
				if (!S.bWater && S.Table > 0.6f && std::fabs(d.Z) < 0.6) tables.push_back(d);
			}
			double bestCut = 0; FVector gorge;
			for (const FVector& t : tables)
			{
				FVector e = FVector::CrossProduct(FVector::ZAxisVector, t).GetSafeNormal(), n = FVector::CrossProduct(t, e).GetSafeNormal();
				for (int j = -20; j <= 20; ++j) for (int i = -20; i <= 20; ++i)
				{
					FVector d = (t + e * (i * 2.0e5 / P.RadiusCm) + n * (j * 2.0e5 / P.RadiusCm)).GetSafeNormal();
					FPlanetTerrainSample S = SlonkTerrain::SampleStructure(P, d, 1.0e5);
					if (S.CutCm > bestCut) { bestCut = S.CutCm; gorge = d; }
				}
			}
			if (bestCut > 0)
			{
				FVector e = FVector::CrossProduct(FVector::ZAxisVector, gorge).GetSafeNormal(), n = FVector::CrossProduct(gorge, e).GetSafeNormal();
				double bestR = 0;
				for (int j = -40; j <= 40; ++j) for (int i = -40; i <= 40; ++i)
				{
					FVector d = (gorge + e * (i * 5.0e3 / P.RadiusCm) + n * (j * 5.0e3 / P.RadiusCm)).GetSafeNormal();
					FPlanetTerrainSample S = SlonkTerrain::SampleStructure(P, d, 1.0e4);
					if (S.River > bestR) { bestR = S.River; river = d; }
				}
				printf("deepest gorge cut %.0f m; best river mask near it %.2f\n", bestCut / 100.0, bestR);
				found = bestR > 0.5;
			}
			if (found)
			{
				FPlanetTerrainSample S = SlonkTerrain::Sample(P, river, 0.0);
				printf("river at (%.4f %.4f %.4f): bed %.0f m, water %.0f m, river %.2f\n", river.X, river.Y, river.Z, S.HeightCm / 100.0, S.WaterCm / 100.0, S.River);
				Patch(P, river, 400.0, 512, (out + "/patch_river_4m.pgm").c_str());
				Patch(P, river, 100.0, 512, (out + "/patch_river_1m.pgm").c_str());
			}
			else printf("no river found\n");
		}
		FVector cliff = (C + E * (kSteep * 100.0 / P.RadiusCm)).GetSafeNormal();
		Patch(P, cliff, 50.0, 512, (out + "/patch_cliff_05m.pgm").c_str());
		Patch(P, cliff, 50.0, 512, (out + "/patch_cliff_05m_structure.pgm").c_str(), false);
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
