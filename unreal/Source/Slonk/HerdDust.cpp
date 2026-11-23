#include "HerdDust.h"

#include "GameClock.h"
#include "HAL/IConsoleManager.h"
#include "TerrainScatter.h"

// The dust a herd raises as it trots and gallops over dry ground near the camera (NS_HerdDust). 0 raises none.
static TAutoConsoleVariable<int32> CVarFaunaDust(TEXT("slonk.Fauna.Dust"), 1,
	TEXT("The dust the herds raise as they trot and gallop over dry ground near the camera (1, the default; NS_HerdDust, none without it), or none (0)"));

bool SlonkFauna::DustEnabled() { return CVarFaunaDust.GetValueOnGameThread() != 0; }

// The herds' dust and the leapers' spray light themselves, so the engine's cloud shadow does not fall on them: under
// cloud they are dimmed by hand instead. 0 leaves them as bright as in the sun.
static TAutoConsoleVariable<int32> CVarFaunaCloudShade(TEXT("slonk.Fauna.CloudShade"), 1,
	TEXT("The herds' dust and the leaping fish's spray dimmed under cloud, as the ground they are over is by the clouds' shadow (1, the default), or as bright as in the sun (0)"));

bool SlonkFauna::CloudShadeEnabled() { return CVarFaunaCloudShade.GetValueOnGameThread() != 0; }

SlonkFauna::FHerdDust SlonkFauna::HerdDustFor(const FHerdDustInput& In)
{
	FHerdDust D;
	D.Radius = (float)FMath::Max(In.RadiusCm, 0.0);
	D.Size = (float)(DustPuffCmPerM * FMath::Max(In.SpanM, 0.1));
	// Under cloud, the sun's share of its light goes with the sun (the puff lights itself: no cloud's shadow falls on
	// it); its opacity stays. A clear sky's 1 leaves the colour exactly as given.
	const float Lit = (float)(1.0 - DustSunShare * FMath::Clamp(In.Shade, 0.0, 1.0));
	D.Color = FLinearColor(In.Color.R * Lit, In.Color.G * Lit, In.Color.B * Lit, In.Color.A);
	D.Drift = In.VelWorldCmS;
	D.Carry = In.CarryWorldCmS;
	// None too far off to make out, by night or on a dead world.
	const double Night = FMath::Clamp(In.Night, 0.0, 1.0);
	if (In.DistCm > DustReachCm || In.Life <= 0.0 || Night >= 1.0) { return D; }
	// Kicked up by the walkers' feet as they go faster than a walk: the faster, the more of them and the drier the ground
	// the more (lush grass still throws up some, two fifths of the driest's), fewer as the light goes.
	const double Above = FMath::Max(In.SpeedMS - DustWalkMS, 0.0);
	const double Dry = 0.4 + 0.6 * FMath::Clamp(1.0 - In.Wet, 0.0, 1.0);
	const double Rate = Above * FMath::Max(In.Members, 0) * DustPuffsPerWalker * Dry * FMath::Clamp(In.Life, 0.0, 1.0) * (1.0 - Night);
	D.Rate = (float)FMath::Min(Rate, DustMaxRate);
	return D;
}

FLinearColor SlonkFauna::HerdDustColor(const FScatterPalette& Pal, double Wet, double Night)
{
	// The bare dry ground, a little of its living cover where it is wetter, paled as dust in the air is; seen less as the
	// light goes.
	FLinearColor C = FMath::Lerp(Pal.Land2, Pal.Land, (float)FMath::Clamp(Wet * 0.4, 0.0, 0.4));
	C = FMath::Lerp(C, FLinearColor::White, 0.35f);
	C.A = (float)(0.6 * (1.0 - 0.7 * FMath::Clamp(Night, 0.0, 1.0)));
	return C;
}

FVector SlonkFauna::GroundCarryCmS(const FVector& PlanetUp, const FVector& PlanetCentre, double SpinDegPerSec, double ClockScale, bool bSpinHeld, const FVector& AtWorld,
	double FrameSec, double PuffStepCapSec)
{
	if (bSpinHeld || FMath::IsNearlyZero(SpinDegPerSec) || ClockScale <= 0.0) { return FVector::ZeroVector; }
	// The planet turns about its up by SpinDegPerSec of the game clock's seconds (a positive yaw turning its x toward its
	// y): the ground at a place goes round with it at omega cross r. Over a long frame it turns for no more than the clock's
	// longest frame, the puffs moving for no more than their emitter's longest tick: their velocity as far over the one as
	// the ground went over the other.
	const FVector Omega = PlanetUp.GetSafeNormal() * FMath::DegreesToRadians(SpinDegPerSec) * ClockScale;
	double Share = 1.0;
	if (FrameSec > 0.0)
	{
		const double Turned = FMath::Min(FrameSec, (double)UGameClockSubsystem::MaxFrameSeconds);
		const double Puffed = PuffStepCapSec > 0.0 ? FMath::Min(FrameSec, PuffStepCapSec) : FrameSec;
		Share = Turned / Puffed;
	}
	return FVector::CrossProduct(Omega, AtWorld - PlanetCentre) * Share;
}

bool SlonkFauna::StepDustGate(FDustGate& Gate, double SpeedMS, double Dt, double Now)
{
	// The speed smoothed over DustSmoothSec (the first frame taken as it is).
	if (!Gate.bPrimed) { Gate.SpeedMS = SpeedMS; Gate.bPrimed = true; }
	else if (Dt > 0.0) { Gate.SpeedMS += (SpeedMS - Gate.SpeedMS) * (1.0 - FMath::Exp(-Dt / DustSmoothSec)); }
	// On once it passes a brisk walk by a margin, off once it falls back to a little above the fastest walk (and not before
	// it has been on a while).
	if (!Gate.bOn && Gate.SpeedMS > DustWalkMS + DustOnMS) { Gate.bOn = true; Gate.OnAt = Now; }
	else if (Gate.bOn && Gate.SpeedMS <= DustOffMS && Now - Gate.OnAt >= DustMinOnSec) { Gate.bOn = false; }
	return Gate.bOn;
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
