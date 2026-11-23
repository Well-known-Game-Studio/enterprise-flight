#include "FishSplash.h"

#include "HAL/IConsoleManager.h"
#include "ScatterSpot.h"

// The splashes the leaping fish throw up where they leave the sea and dive back in near the camera (NS_FishSplash). 0
// throws none.
static TAutoConsoleVariable<int32> CVarFaunaSplash(TEXT("slonk.Fauna.Splash"), 1,
	TEXT("The splashes the leaping fish throw up where they leave the sea and dive back into it near the camera (1, the default; NS_FishSplash, none without it), or none (0)"));

bool SlonkFauna::SplashEnabled() { return CVarFaunaSplash.GetValueOnGameThread() != 0; }

float SlonkFauna::LeaperNight(double SunUp)
{
	return FMath::RoundToFloat((float)(1.0 - SlonkScatter::SmoothStep(-0.12, 0.08, SunUp)) * 100.f) / 100.f;
}

FLinearColor SlonkFauna::FoamColor(const FLinearColor& Shallow, const FLinearColor& Ocean)
{
	// The terrain's foam, a tenth of it the shallows' hue at its brightest (the deep's where the shallows are black).
	const FLinearColor Sea = Shallow.GetMax() > 1e-3f ? Shallow : Ocean;
	const float M = FMath::Max(Sea.GetMax(), 1e-3f);
	const FLinearColor Hue(Sea.R / M, Sea.G / M, Sea.B / M);
	FLinearColor C = FMath::Lerp(FLinearColor(0.88f, 0.9f, 0.92f), Hue * 0.9f, 0.1f);
	C.A = 0.85f;
	return C;
}

SlonkFauna::FFishSplash SlonkFauna::FishSplashFor(const FFishSplashInput& In)
{
	FFishSplash S;
	S.Size = (float)FMath::Max(In.SizeCm, 1.0);
	S.Entry = In.bEntry ? 1.f : 0.f;
	S.Night = (float)FMath::Clamp(In.Night, 0.0, 1.0);
	S.Color = FoamColor(In.Shallow, In.Ocean);
	S.Carry = In.CarryWorldCmS;
	return S;
}

UNiagaraComponent* SlonkFauna::FSplashPool::Acquire(double Now, TFunctionRef<UNiagaraComponent*()> Make)
{
	Tick(Now);
	UNiagaraComponent* C = nullptr;
	if (Resting.Num() > 0)
	{
		C = Resting[0];
		Resting.RemoveAt(0, EAllowShrinking::No);
	}
	else if (Made < MaxSplashComponents)
	{
		C = Make();
		if (C) { ++Made; }
	}
	if (!C)
	{
		++Dropped;
		return nullptr;
	}
	FLent L;
	L.Component = C;
	L.Since = Now;
	Live.Add(L);
	++Bursts;
	return C;
}

void SlonkFauna::FSplashPool::Tick(double Now)
{
	// The oldest first: a burst over by now rests, to be lent again.
	for (int32 i = 0; i < Live.Num();)
	{
		if (Now - Live[i].Since >= SplashLifeSec)
		{
			Resting.Add(Live[i].Component);
			Live.RemoveAt(i, EAllowShrinking::No);
		}
		else { ++i; }
	}
}
