#include "StarshipMovementComponent.h"

#include "Components/SceneComponent.h"

UStarshipMovementComponent::UStarshipMovementComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	bUpdateOnlyIfRendered = false;
	bAutoUpdateTickRegistration = true;
}

void UStarshipMovementComponent::SetThrottle(double NewThrottle)
{
	Throttle = FMath::Clamp(NewThrottle, -ReverseThrottleLimit, 1.0);
}

void UStarshipMovementComponent::AdjustThrottle(double Delta)
{
	SetThrottle(Throttle + Delta);
}

void UStarshipMovementComponent::SetSteeringInput(float Pitch, float Yaw, float Roll)
{
	SteeringInput = FVector(FMath::Clamp(Pitch, -1.f, 1.f), FMath::Clamp(Yaw, -1.f, 1.f), FMath::Clamp(Roll, -1.f, 1.f));
}

void UStarshipMovementComponent::AddSteeringInput(float Pitch, float Yaw, float Roll)
{
	SteeringInput.X = FMath::Clamp(SteeringInput.X + Pitch, -1.0, 1.0);
	SteeringInput.Y = FMath::Clamp(SteeringInput.Y + Yaw, -1.0, 1.0);
	SteeringInput.Z = FMath::Clamp(SteeringInput.Z + Roll, -1.0, 1.0);
}

void UStarshipMovementComponent::SetWarp(bool bActive, double InWarpSpeed)
{
	bWarpActive = bActive;
	WarpSpeed = FMath::Max(0.0, InWarpSpeed);
}

FVector UStarshipMovementComponent::GetNormalizedAngularVelocity() const
{
	return FVector(
		MaxPitchRate > 0.f ? AngularVelocity.Pitch / MaxPitchRate : 0.f,
		MaxYawRate > 0.f ? AngularVelocity.Yaw / MaxYawRate : 0.f,
		MaxRollRate > 0.f ? AngularVelocity.Roll / MaxRollRate : 0.f);
}

void UStarshipMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!UpdatedComponent || ShouldSkipUpdate(DeltaTime) || DeltaTime <= 0.f)
	{
		SteeringInput = FVector::ZeroVector;
		return;
	}

	// Clamp huge hitches so a frame spike never flings the ship across the system.
	DeltaTime = FMath::Min(DeltaTime, 0.1f);

	// ---- Attitude ----
	const float SteerScale = bWarpActive ? WarpSteeringScale : 1.f;
	const FRotator TargetRates(
		SteeringInput.X * MaxPitchRate * SteerScale,
		SteeringInput.Y * MaxYawRate * SteerScale,
		SteeringInput.Z * MaxRollRate * SteerScale);

	// Frame-rate independent exponential approach to the target rates.
	const float Alpha = 1.f - FMath::Exp(-AngularResponse * DeltaTime);
	AngularVelocity.Pitch = FMath::Lerp(AngularVelocity.Pitch, TargetRates.Pitch, Alpha);
	AngularVelocity.Yaw = FMath::Lerp(AngularVelocity.Yaw, TargetRates.Yaw, Alpha);
	AngularVelocity.Roll = FMath::Lerp(AngularVelocity.Roll, TargetRates.Roll, Alpha);

	const FQuat CurrentQuat = UpdatedComponent->GetComponentQuat();
	// Body-frame rotation: compose the small delta on the right so pitch/yaw/roll are relative to the hull.
	const FQuat DeltaQuat(FRotator(AngularVelocity.Pitch * DeltaTime, AngularVelocity.Yaw * DeltaTime, AngularVelocity.Roll * DeltaTime));
	const FQuat NewQuat = (CurrentQuat * DeltaQuat).GetNormalized();

	// ---- Linear ----
	const FVector Forward = NewQuat.GetForwardVector();
	if (bWarpActive)
	{
		// The warp drive owns the speed profile; we just follow it exactly.
		Velocity = Forward * WarpSpeed;
	}
	else
	{
		const FVector TargetVelocity = Forward * GetImpulseTargetSpeed();
		const FVector DeltaV = TargetVelocity - Velocity;
		Velocity += DeltaV.GetClampedToMaxSize(ImpulseAcceleration * DeltaTime);
	}

	const FVector Delta = Velocity * DeltaTime;

	if (bWarpActive)
	{
		// No sweeping at warp: nothing in a game world should stop a ship doing 10^10 cm/s.
		UpdatedComponent->MoveComponent(Delta, NewQuat, /*bSweep*/ false, nullptr, MOVECOMP_NoFlags, ETeleportType::TeleportPhysics);
	}
	else
	{
		FHitResult Hit;
		SafeMoveUpdatedComponent(Delta, NewQuat, /*bSweep*/ true, Hit);
		if (Hit.IsValidBlockingHit())
		{
			// Bounce a little off whatever we hit and kill the throttle.
			const FVector Reflected = FMath::GetReflectionVector(Velocity, Hit.Normal) * 0.1;
			Velocity = Reflected;
			Throttle = 0.0;
			OnCollision.Broadcast(Hit);
		}
	}

	UpdateComponentVelocity();
	SteeringInput = FVector::ZeroVector;
}
