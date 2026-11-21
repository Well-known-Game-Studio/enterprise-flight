#include "HerdDust.h"

#include "HAL/IConsoleManager.h"
#include "TerrainScatter.h"

// The dust a herd raises as it trots and gallops over dry ground near the camera (NS_HerdDust). 0 raises none.
static TAutoConsoleVariable<int32> CVarFaunaDust(TEXT("slonk.Fauna.Dust"), 1,
	TEXT("The dust the herds raise as they trot and gallop over dry ground near the camera (1, the default; NS_HerdDust, none without it), or none (0)"));

bool SlonkFauna::DustEnabled() { return CVarFaunaDust.GetValueOnGameThread() != 0; }

SlonkFauna::FHerdDust SlonkFauna::HerdDustFor(const FHerdDustInput& In)
{
	FHerdDust D;
	D.Radius = (float)FMath::Max(In.RadiusCm, 0.0);
	D.Size = (float)(DustPuffCmPerM * FMath::Max(In.SpanM, 0.1));
	D.Color = In.Color;
	D.Drift = In.VelWorldCmS;
	// None too far off to make out, by night or on a dead world.
	const double Night = FMath::Clamp(In.Night, 0.0, 1.0);
	if (In.DistCm > DustReachCm || In.Life <= 0.0 || Night >= 1.0) { return D; }
	// Kicked up by the walkers' feet as they go faster than a walk: the faster, the more of them and the drier the ground
	// the more, fewer as the light goes.
	const double Above = FMath::Max(In.SpeedMS - DustWalkMS, 0.0);
	const double Rate = Above * FMath::Max(In.Members, 0) * DustPuffsPerWalker * FMath::Clamp(1.0 - In.Wet, 0.0, 1.0) * FMath::Clamp(In.Life, 0.0, 1.0) * (1.0 - Night);
	D.Rate = (float)FMath::Min(Rate, DustMaxRate);
	return D;
}

FLinearColor SlonkFauna::HerdDustColor(const FScatterPalette& Pal, double Wet, double Night)
{
	// The bare dry ground, a little of its living cover where it is wetter, paled as dust in the air is; seen less as the
	// light goes.
	FLinearColor C = FMath::Lerp(Pal.Land2, Pal.Land, (float)FMath::Clamp(Wet * 0.4, 0.0, 0.4));
	C = FMath::Lerp(C, FLinearColor::White, 0.35f);
	C.A = (float)(0.45 * (1.0 - 0.7 * FMath::Clamp(Night, 0.0, 1.0)));
	return C;
}

UNiagaraComponent* SlonkFauna::FDustPool::Find(uint64 Id) const
{
	UNiagaraComponent* const* C = Live.Find(Id);
	return C ? *C : nullptr;
}

UNiagaraComponent* SlonkFauna::FDustPool::Acquire(uint64 Id, double Now, TFunctionRef<UNiagaraComponent*()> Make)
{
	if (UNiagaraComponent* const* Own = Live.Find(Id)) { return *Own; }
	// The one that has rested longest (the first given back), once its last puffs have faded.
	UNiagaraComponent* C = nullptr;
	if (Resting.Num() > 0 && Now - Resting[0].Since >= DustPuffLifeSec)
	{
		C = Resting[0].Component;
		Resting.RemoveAt(0, EAllowShrinking::No);
	}
	if (!C)
	{
		C = Make();
		if (!C) { return nullptr; }
		++Made;
	}
	Live.Add(Id, C);
	return C;
}

UNiagaraComponent* SlonkFauna::FDustPool::Release(uint64 Id, double Now)
{
	UNiagaraComponent* C = nullptr;
	if (!Live.RemoveAndCopyValue(Id, C) || !C) { return nullptr; }
	FRest R;
	R.Component = C;
	R.Since = Now;
	Resting.Add(R);
	return C;
}
