#include "PlanetWeather.h"
#include "Async/ParallelFor.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"

namespace
{
	// Fractal Perlin, roughly -1..1 (the amplitude sum is divided out).
	float Fbm(const FVector& P, int32 Octaves, float Lacunarity = 2.1f, float Gain = 0.5f)
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
			// Masses: domain-warped fractal noise on the sphere, as the shell's own noise was.
			const FVector P = D * Scale + Seed;
			const FVector Warp(Fbm(P * 1.3f + FVector(1.7f, 9.2f, 0.4f), 2), Fbm(P * 1.3f + FVector(8.3f, 2.8f, 5.1f), 2), Fbm(P * 1.3f + FVector(0.9f, 4.4f, 6.1f), 2));
			const float M = Fbm(P + Warp * 0.6f, 4);
			// Fronts: the same noise stretched ten to one along the parallels, so a threshold cuts east-west bands
			// and the cyclone-scale swirls the warp puts in them.
			const FVector Q(FMath::Cos(Lon) * Scale * 0.45f, FMath::Sin(Lon) * Scale * 0.45f, SinLat * Scale * 3.5f);
			const float F = Fbm(Q + Seed * 0.7f + Warp * 0.35f + FVector(3.3f, 1.1f, 7.7f), 3);
			Masses[I] = M * (1.f - 0.55f * Fronts) + F * (0.9f * Fronts);
			// Cells: the kilometre clumps, with a little of the masses so they thin toward clear air.
			Cells[I] = Fbm(D * Scale * Spec.CellScale + Seed * 3.1f, 3, 2.2f, 0.55f);
			// Type: broad regions of heaped cloud (0) and sheet cloud (1).
			Type[I] = Fbm(D * Scale * 1.5f + Seed * 1.9f + FVector(5.5f, 2.2f, 8.8f), 2);
		}
	});
	Normalise(Masses, 0.17f);
	Normalise(Cells, 0.17f);
	Normalise(Type, 0.2f);
	Out.SetNumUninitialized(N);
	for (int32 I = 0; I < N; ++I)
	{
		Out[I] = FColor((uint8)FMath::RoundToInt(Masses[I] * 255.f), (uint8)FMath::RoundToInt(Cells[I] * 255.f), (uint8)FMath::RoundToInt(Type[I] * 255.f), 255);
	}
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
