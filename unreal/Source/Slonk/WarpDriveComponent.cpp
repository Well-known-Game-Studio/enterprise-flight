#include "WarpDriveComponent.h"

#include "GameFramework/Actor.h"
#include "StarshipMovementComponent.h"

UWarpDriveComponent::UWarpDriveComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Tick before movement so the commanded speed is fresh when the ship integrates.
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UWarpDriveComponent::BeginPlay()
{
	Super::BeginPlay();
	ResolveMovement();
	WarpFactor = FMath::Clamp(WarpFactor, MinWarpFactor, MaxWarpFactor);
}

void UWarpDriveComponent::ResolveMovement()
{
	if (!Movement && GetOwner())
	{
		Movement = GetOwner()->FindComponentByClass<UStarshipMovementComponent>();
		if (Movement)
		{
			// Make sure we run before the movement component every frame.
			Movement->PrimaryComponentTick.AddPrerequisite(this, PrimaryComponentTick);
		}
	}
}

double UWarpDriveComponent::GetWarpSpeedForFactor(float Factor) const
{
	const double Base = Warp1Speed * FMath::Pow((double)FMath::Clamp(Factor, 0.f, TranswarpStart), (double)WarpSpeedExponent);
	if (Factor <= TranswarpStart)
	{
		return Base;
	}
	const double W = FMath::Min((double)Factor, 9.999);
	return Base * FMath::Pow((10.0 - (double)TranswarpStart) / (10.0 - W), (double)TranswarpExponent);
}

double UWarpDriveComponent::GetFactorForSpeed(double Speed) const
{
	const double AtStart = GetWarpSpeedForFactor(TranswarpStart);
	if (Speed <= AtStart)
	{
		return FMath::Pow(FMath::Max(Speed, 0.0) / Warp1Speed, 1.0 / (double)WarpSpeedExponent);
	}
	return 10.0 - (10.0 - (double)TranswarpStart) * FMath::Pow(AtStart / Speed, 1.0 / (double)TranswarpExponent);
}

float UWarpDriveComponent::GetChargeProgress() const
{
	switch (State)
	{
	case EWarpState::Charging: return ChargeDuration > 0.f ? FMath::Clamp(StateTime / ChargeDuration, 0.f, 1.f) : 1.f;
	case EWarpState::Jumping:
	case EWarpState::AtWarp: return 1.f;
	default: return 0.f;
	}
}

void UWarpDriveComponent::SetWarpFactor(float NewFactor)
{
	WarpFactor = FMath::Clamp(NewFactor, MinWarpFactor, MaxWarpFactor);
}

void UWarpDriveComponent::AdjustWarpFactor(float Delta)
{
	SetWarpFactor(WarpFactor + Delta);
}

void UWarpDriveComponent::EngageWarp()
{
	if (State == EWarpState::Idle || State == EWarpState::Dropping)
	{
		SetState(EWarpState::Charging);
	}
}

void UWarpDriveComponent::DisengageWarp()
{
	if (State == EWarpState::Charging)
	{
		SetState(EWarpState::Idle);
	}
	else if (State == EWarpState::Jumping || State == EWarpState::AtWarp)
	{
		SetState(EWarpState::Dropping);
	}
}

void UWarpDriveComponent::ToggleWarp()
{
	if (State == EWarpState::Idle || State == EWarpState::Dropping)
	{
		EngageWarp();
	}
	else
	{
		DisengageWarp();
	}
}

void UWarpDriveComponent::SetState(EWarpState NewState)
{
	if (NewState == State)
	{
		return;
	}

	ResolveMovement();
	const EWarpState Old = State;
	State = NewState;
	StateTime = 0.f;

	switch (State)
	{
	case EWarpState::Charging:
		break;

	case EWarpState::Jumping:
		EntrySpeed = Movement ? Movement->GetSpeed() : 0.0;
		CruiseSpeed = GetWarpSpeedForFactor(WarpFactor);
		CurrentSpeed = EntrySpeed;
		FlashIntensity = 1.f;
		if (Movement)
		{
			Movement->SetWarp(true, CurrentSpeed);
		}
		break;

	case EWarpState::AtWarp:
		CurrentSpeed = CruiseSpeed;
		break;

	case EWarpState::Dropping:
		EntrySpeed = CurrentSpeed;
		// Drop out at whatever the impulse throttle is asking for.
		CruiseSpeed = Movement ? FMath::Abs(Movement->GetImpulseTargetSpeed()) : 0.0;
		FlashIntensity = 1.f;
		break;

	case EWarpState::Idle:
		CurrentSpeed = 0.0;
		if (Movement)
		{
			// Hand the impulse drive a clean velocity: whatever residual warp speed the last Dropping tick left
			// would otherwise take minutes to bleed off at impulse acceleration.
			if (Old == EWarpState::Dropping && Movement->UpdatedComponent)
			{
				Movement->Velocity = Movement->UpdatedComponent->GetForwardVector() * FMath::Min(CruiseSpeed, Movement->FullImpulseSpeed);
			}
			Movement->SetWarp(false, 0.0);
		}
		break;
	}

	OnWarpStateChanged.Broadcast(Old, State);
}

void UWarpDriveComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	StateTime += DeltaTime;

	// Flash always decays quickly.
	FlashIntensity = FMath::Max(0.f, FlashIntensity - DeltaTime * 2.5f);

	float TargetGlow = 1.f;
	float TargetStreak = 0.f;

	switch (State)
	{
	case EWarpState::Idle:
		break;

	case EWarpState::Charging:
	{
		const float T = GetChargeProgress();
		// Glow ramps up with a little flicker right before the jump.
		TargetGlow = FMath::Lerp(1.f, WarpGlowMultiplier, T) + (T > 0.75f ? 0.6f * FMath::Sin(StateTime * 40.f) * (T - 0.75f) * 4.f : 0.f);
		if (StateTime >= ChargeDuration)
		{
			SetState(EWarpState::Jumping);
		}
		break;
	}

	case EWarpState::Jumping:
	{
		const float A = JumpDuration > 0.f ? FMath::Clamp(StateTime / JumpDuration, 0.f, 1.f) : 1.f;
		// Ease-in: the ship hangs, then is yanked to warp.
		const double Eased = FMath::Pow((double)A, 3.0);
		CurrentSpeed = FMath::Lerp(EntrySpeed, CruiseSpeed, Eased);
		TargetGlow = WarpGlowMultiplier * (1.f + 0.5f * (1.f - A));
		TargetStreak = A;
		if (Movement)
		{
			Movement->SetWarp(true, CurrentSpeed);
		}
		if (A >= 1.f)
		{
			SetState(EWarpState::AtWarp);
		}
		break;
	}

	case EWarpState::AtWarp:
	{
		// Follow warp factor changes smoothly while cruising.
		const double Desired = GetWarpSpeedForFactor(WarpFactor);
		const float Alpha = 1.f - FMath::Exp(-CruiseSpeedResponse * DeltaTime);
		CruiseSpeed = FMath::Lerp(CruiseSpeed, Desired, (double)Alpha);
		CurrentSpeed = CruiseSpeed;
		TargetGlow = WarpGlowMultiplier;
		TargetStreak = 1.f;
		if (Movement)
		{
			Movement->SetWarp(true, CurrentSpeed);
			// A body ahead has throttled the warp field below impulse speed: drop out rather than crawl at warp.
			if (Movement->bProximityLimited && Movement->ProximityCap < Movement->FullImpulseSpeed)
			{
				DisengageWarp();
			}
		}
		break;
	}

	case EWarpState::Dropping:
	{
		const float A = DropDuration > 0.f ? FMath::Clamp(StateTime / DropDuration, 0.f, 1.f) : 1.f;
		// Ease-out: most of the speed comes off immediately.
		const double Eased = 1.0 - FMath::Pow(1.0 - (double)A, 3.0);
		CurrentSpeed = FMath::Lerp(EntrySpeed, CruiseSpeed, Eased);
		TargetGlow = FMath::Lerp(WarpGlowMultiplier, 1.f, A);
		TargetStreak = 1.f - A;
		if (Movement)
		{
			Movement->SetWarp(true, CurrentSpeed);
		}
		if (A >= 1.f)
		{
			SetState(EWarpState::Idle);
		}
		break;
	}
	}

	const float GlowAlpha = 1.f - FMath::Exp(-6.f * DeltaTime);
	GlowMultiplier = FMath::Lerp(GlowMultiplier, TargetGlow, GlowAlpha);
	StreakIntensity = FMath::Clamp(TargetStreak, 0.f, 1.f);
}
