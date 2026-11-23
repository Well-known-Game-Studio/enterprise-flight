#include "PlanetWeather.h"
#include "Async/ParallelFor.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"

namespace
{
	// Fractal Perlin, roughly -1..1 (the amplitude sum is divided out).
	float WeatherFbm(const FVector& P, int32 Octaves, float Lacunarity = 2.1f, float Gain = 0.5f)
	{
		float Sum = 0.f, Amp = 1.f, Norm = 0.f;
		FVector Q = P;
		for (int32 i = 0; i < Octaves; ++i)
		{
			Sum += Amp * FMath::PerlinNoise3D(Q);
			Norm += Amp;
			Amp *= Gain;
			Q = Q * Lacunarity + FVector(31.7f, 17.3f, 11.9f);
		}
		return Sum / Norm;
	}

	// Mean 0.5, standard deviation Sigma: whatever the noise flavour, the materials' coverage thresholds then mean
	// the same thing on every map.
	void Normalise(TArray<float>& F, float Sigma)
	{
		double Mean = 0.0, Sq = 0.0;
		for (float V : F) { Mean += V; Sq += double(V) * V; }
		Mean /= F.Num();
		const double Std = FMath::Sqrt(FMath::Max(Sq / F.Num() - Mean * Mean, 1e-12));
		const float K = Sigma / (float)Std;
		for (float& V : F) { V = FMath::Clamp(0.5f + (V - (float)Mean) * K, 0.f, 1.f); }
	}
}

void SlonkWeather::Generate(const FPlanetWeatherSpec& Spec, int32 W, int32 H, TArray<FColor>& Out)
{
	const int32 N = W * H;
	TArray<float> Masses, Cells, Type;
	Masses.SetNumUninitialized(N); Cells.SetNumUninitialized(N); Type.SetNumUninitialized(N);
	const FVector Seed = Spec.Seed;
	const float Scale = FMath::Max(Spec.Scale, 0.5f);
	const float Fronts = FMath::Clamp(Spec.Fronts, 0.f, 1.f);
	ParallelFor(H, [&](int32 Row)
	{
		const float Lat = (0.5f - (Row + 0.5f) / H) * PI;			// +90 at the top row
		const float CosLat = FMath::Cos(Lat), SinLat = FMath::Sin(Lat);
		for (int32 Col = 0; Col < W; ++Col)
		{
			const float Lon = ((Col + 0.5f) / W - 0.5f) * 2.f * PI;
			const FVector D(CosLat * FMath::Cos(Lon), CosLat * FMath::Sin(Lon), SinLat);
			const int32 I = Row * W + Col;
			if (Spec.Zonal > 0.f)
			{
				// Zones and belts: a sine of latitude at the band scale, its phase warped into festoons; eddies stretched
				// along the parallels; a few storm ovals from jittered points.
				const float Warp = WeatherFbm(D * Scale * 1.6f + Seed, 3) * 0.35f + WeatherFbm(D * Scale * 0.6f + Seed * 1.7f, 2) * 0.5f;
				const float NB = FMath::Max(Spec.BandScale * 0.45f, 4.f);
				const float Band = FMath::Sin(Lat * NB + Warp);
				const float Zone = FMath::SmoothStep(-0.35f, 0.35f, Band);
				const float Eddy = WeatherFbm(FVector(D.X * Scale * 3.f, D.Y * Scale * 3.f, D.Z * Scale * 12.f) + Seed * 2.3f, 4);
				float Storm = 0.f;
				{
					const FVector Q = D * 2.2f + Seed * 0.11f - FVector(0.5f, 0.5f, 0.5f);
					const FVector Fl(FMath::Floor(Q.X), FMath::Floor(Q.Y), FMath::Floor(Q.Z));
					for (int32 k = 0; k < 8; ++k)
					{
						const FVector C = Fl + FVector(k & 1, (k >> 1) & 1, (k >> 2) & 1);
						uint32 Hs = (uint32)(int32)C.X * 73856093u ^ (uint32)(int32)C.Y * 19349663u ^ (uint32)(int32)C.Z * 83492791u;
						Hs = (Hs ^ (Hs >> 13)) * 0x5bd1e995u; Hs ^= Hs >> 15;
						if ((Hs >> 29) > 1u) { continue; }					// a quarter of the cells hold a storm
						const FVector P0 = C + FVector((Hs & 0x3ffu) / 1023.f, ((Hs >> 10) & 0x3ffu) / 1023.f, ((Hs >> 20) & 0x3ffu) / 1023.f);
						FVector Dp = Q - P0; Dp.Z *= 2.5f;						// an oval, long along the parallel
						Storm = FMath::Max(Storm, 1.f - FMath::SmoothStep(0.f, 0.06f, (float)Dp.SizeSquared()));
					}
				}
				Masses[I] = (Zone - 0.5f) * 0.9f + Eddy * 0.5f + Storm * 0.7f;
				Cells[I] = WeatherFbm(FVector(D.X * Scale * 25.f, D.Y * Scale * 25.f, D.Z * Scale * 75.f) + Seed * 3.1f, 3, 2.2f, 0.55f);
				Type[I] = FMath::Lerp(Zone, 1.f, Storm);
				continue;
			}
			// Masses: domain-warped fractal noise on the sphere, as the shell's own noise was.
			const FVector P = D * Scale + Seed;
			const FVector Warp(WeatherFbm(P * 1.3f + FVector(1.7f, 9.2f, 0.4f), 2), WeatherFbm(P * 1.3f + FVector(8.3f, 2.8f, 5.1f), 2), WeatherFbm(P * 1.3f + FVector(0.9f, 4.4f, 6.1f), 2));
			const float M = WeatherFbm(P + Warp * 0.6f, 4);
			// Fronts: the same noise stretched ten to one along the parallels, so a threshold cuts east-west bands
			// and the cyclone-scale swirls the warp puts in them.
			// D.xy, not (cos, sin) of the longitude: with the latter the field kept a full circle of variation however
			// close to the pole, so the fronts narrowed into radial wedges there. With D.xy the stretch fades out at the pole.
			const FVector Q(D.X * Scale * 0.45f, D.Y * Scale * 0.45f, D.Z * Scale * 3.5f);
			const float F = WeatherFbm(Q + Seed * 0.7f + Warp * 0.35f + FVector(3.3f, 1.1f, 7.7f), 3);
			Masses[I] = M * (1.f - 0.55f * Fronts) + F * (0.9f * Fronts);
			// Cells: the kilometre clumps, with a little of the masses so they thin toward clear air.
			Cells[I] = WeatherFbm(D * Scale * Spec.CellScale + Seed * 3.1f, 3, 2.2f, 0.55f);
			// Type: broad regions of heaped cloud (0) and sheet cloud (1).
			Type[I] = WeatherFbm(D * Scale * 1.5f + Seed * 1.9f + FVector(5.5f, 2.2f, 8.8f), 2);
		}
	});
	Normalise(Masses, Spec.Zonal > 0.f ? 0.2f : 0.17f);
	Normalise(Cells, 0.17f);
	if (Spec.Zonal <= 0.f) { Normalise(Type, 0.2f); }
	Out.SetNumUninitialized(N);
	for (int32 I = 0; I < N; ++I)
	{
		Out[I] = FColor((uint8)FMath::RoundToInt(Masses[I] * 255.f), (uint8)FMath::RoundToInt(Cells[I] * 255.f), (uint8)FMath::RoundToInt(Type[I] * 255.f), 255);
	}
}

bool SlonkWeather::SampleAt(const TArray<FColor>& Pixels, int32 W, int32 H, const FVector& Dir, float& OutMasses, float& OutCells)
{
	if (W < 2 || H < 2 || Pixels.Num() != W * H) { return false; }
	const FVector D = Dir.GetSafeNormal();
	// The materials' WeatherUV: longitude round u, its seam at 180 degrees, and the colatitude down v; a texel's middle
	// at its half.
	const double U = FMath::Atan2(D.Y, D.X) / UE_DOUBLE_TWO_PI + 0.5;
	const double V = FMath::Acos(FMath::Clamp(D.Z, -1.0, 1.0)) / UE_DOUBLE_PI;
	const double X = U * W - 0.5, Y = FMath::Clamp(V * H - 0.5, 0.0, (double)(H - 1));
	const int32 X0 = FMath::FloorToInt32(X), Y0 = FMath::Min(FMath::FloorToInt32(Y), H - 2);
	const float Fx = (float)(X - X0), Fy = (float)(Y - Y0);
	// Wrapped round the seam, clamped at the poles (the texture's own addressing).
	const auto At = [&Pixels, W](int32 Xi, int32 Yi) -> const FColor& { return Pixels[Yi * W + ((Xi % W) + W) % W]; };
	const FColor& A = At(X0, Y0); const FColor& B = At(X0 + 1, Y0); const FColor& C = At(X0, Y0 + 1); const FColor& E = At(X0 + 1, Y0 + 1);
	OutMasses = FMath::Lerp(FMath::Lerp((float)A.R, (float)B.R, Fx), FMath::Lerp((float)C.R, (float)E.R, Fx), Fy) / 255.f;
	OutCells = FMath::Lerp(FMath::Lerp((float)A.G, (float)B.G, Fx), FMath::Lerp((float)C.G, (float)E.G, Fx), Fy) / 255.f;
	return true;
}

float SlonkWeather::CoverOf(float Masses, float Cells, float Coverage, float Deck, float WeatherIn)
{
	const float In = FMath::Clamp(WeatherIn, 0.f, 1.f);
	// The material's two smoothsteps about the style's coverage: how far inside a mass, and inside a cell.
	const float Clear = 1.f - Coverage;
	const float Mass = FMath::SmoothStep(Clear - 0.25f, Clear + 0.1f, Masses);
	const float Cell = FMath::SmoothStep(Clear - 0.1f, Clear + 0.1f, Cells);
	// Its Cloud_GlobalCoverage: the style's even bias without the map, a scattered sky's with it; then the map's push
	// (its WeatherAmp 1.4 and CellAmp 0.6).
	const float StyleBias = Coverage * 1.2f - 0.7f + Deck * 0.3f;
	const float Bias = FMath::Lerp(StyleBias, -0.25f + Deck * 0.3f, In) + ((Mass - 0.5f) * 1.4f + (Cell - 0.5f) * 0.6f) * In;
	return FMath::SmoothStep(-0.55f, 0.25f, Bias);
}

FVector SlonkWeather::SunThroughLayer(const FVector& PlaceFromCentre, const FVector& ToSun, double LayerRadiusCm, double& OutSunUp)
{
	const FVector Sun = ToSun.GetSafeNormal();
	OutSunUp = 1.0;
	if (Sun.IsNearlyZero()) { return PlaceFromCentre; }
	FVector Through = PlaceFromCentre;
	// The far root of |Place + t Sun| = LayerRadius, for a place under the shell.
	const double B = FVector::DotProduct(PlaceFromCentre, Sun), C = PlaceFromCentre.SizeSquared() - LayerRadiusCm * LayerRadiusCm;
	if (C < 0.0) { Through = PlaceFromCentre + Sun * (-B + FMath::Sqrt(B * B - C)); }
	OutSunUp = FVector::DotProduct(Through.GetSafeNormal(), Sun);
	return Through;
}

float SlonkWeather::DeckShade(float Cover)
{
	return FMath::SmoothStep(0.88f, 0.97f, Cover);
}

void SlonkWeather::SavePng(const TArray<FColor>& Pixels, int32 W, int32 H, const FString& Path)
{
	TArray64<uint8> Png;
	FImageUtils::PNGCompressImageArray(W, H, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);
	FFileHelper::SaveArrayToFile(Png, *Path);
}

UTexture2D* SlonkWeather::MakeTexture(const TArray<FColor>& Pixels, int32 W, int32 H, const FName& Name)
{
	check(IsInGameThread());
	if (Pixels.Num() != W * H || W < 2 || H < 2) { return nullptr; }
	UTexture2D* Tex = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8, Name);
	if (!Tex) { return nullptr; }
	Tex->SRGB = false;
	Tex->Filter = TF_Default;
	Tex->AddressX = TA_Wrap;
	Tex->AddressY = TA_Clamp;
	Tex->CompressionSettings = TC_VectorDisplacementmap;
	Tex->NeverStream = true;
#if WITH_EDITORONLY_DATA
	Tex->MipGenSettings = TMGS_NoMipmaps;
#endif
	FTexturePlatformData* PD = Tex->GetPlatformData();
	auto Fill = [](FTexture2DMipMap& Mip, const TArray<FColor>& Px)
	{
		Mip.BulkData.Lock(LOCK_READ_WRITE);
		void* Data = Mip.BulkData.Realloc(Px.Num() * sizeof(FColor));
		FMemory::Memcpy(Data, Px.GetData(), Px.Num() * sizeof(FColor));
		Mip.BulkData.Unlock();
	};
	Fill(PD->Mips[0], Pixels);
	// The mip chain by hand (a transient texture has none): the shell is a disc a few hundred pixels across from
	// far out, and an unmipped 2k map would shimmer on it.
	TArray<FColor> Prev = Pixels, Cur;
	int32 PW = W, PH = H;
	while (PW > 1 && PH > 1)
	{
		const int32 CW = PW / 2, CH = PH / 2;
		Cur.SetNumUninitialized(CW * CH);
		for (int32 Y = 0; Y < CH; ++Y)
		{
			for (int32 X = 0; X < CW; ++X)
			{
				const FColor& A = Prev[(2 * Y) * PW + 2 * X]; const FColor& B = Prev[(2 * Y) * PW + 2 * X + 1];
				const FColor& C = Prev[(2 * Y + 1) * PW + 2 * X]; const FColor& D = Prev[(2 * Y + 1) * PW + 2 * X + 1];
				Cur[Y * CW + X] = FColor((A.R + B.R + C.R + D.R + 2) / 4, (A.G + B.G + C.G + D.G + 2) / 4, (A.B + B.B + C.B + D.B + 2) / 4, 255);
			}
		}
		FTexture2DMipMap* Mip = new FTexture2DMipMap(CW, CH, 1);
		Fill(*Mip, Cur);
		PD->Mips.Add(Mip);
		Prev = MoveTemp(Cur);
		PW = CW; PH = CH;
	}
	Tex->UpdateResource();
	return Tex;
}
