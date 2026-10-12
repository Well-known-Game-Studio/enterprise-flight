#include "StarshipMovementComponent.h"

#include "EngineUtils.h"
#include "PlanetBody.h"
#include "StarBody.h"

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
	bProximityLimited = false;
	if (bWarpActive)
	{
		// The warp drive owns the speed profile; we follow it exactly unless a body is in the way.
		Velocity = Forward * WarpSpeed;
	}
	else
	{
		const FVector TargetVelocity = Forward * GetImpulseTargetSpeed();
		const FVector DeltaV = TargetVelocity - Velocity;
		Velocity += DeltaV.GetClampedToMaxSize(ImpulseAcceleration * DeltaTime);
	}
	// Automatic braking: when the heading would carry us into a body, scale the whole velocity so the
	// closing rate never exceeds the cap (scaling keeps the heading; clamping one axis bled thrust sideways).
	{
		const FVector Dir = Velocity.GetSafeNormal();
		const FVector ToBody = UpdateProximity(UpdatedComponent->GetComponentLocation(), Dir);
		if (!ToBody.IsNearlyZero())
		{
			const double Closing = FVector::DotProduct(Velocity, ToBody);
			if (Closing > ProximityCap)
			{
				Velocity *= ProximityCap / Closing;
				bProximityLimited = true;
			}
		}
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

FVector UStarshipMovementComponent::UpdateProximity(const FVector& Here, const FVector& Dir)
{
	ProximityBody = nullptr;
	ProximityCap = 0.0;
	ProximityBodyName.Empty();
	if (!bProximityGovernor)
	{
		return FVector::ZeroVector;
	}
	ProximityScanTimer -= GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f;
	if (ProximityScanTimer <= 0.f)
	{
		ProximityScanTimer = 1.f;
		ProximityCandidates.Reset();
		if (UWorld* World = GetWorld())
		{
			for (TActorIterator<APlanetBody> It(World); It; ++It) { ProximityCandidates.Add({ *It, It->GetRadiusCm() }); }
			for (TActorIterator<AStarBody> It(World); It; ++It) { ProximityCandidates.Add({ *It, It->GetRadiusCm() }); }
		}
	}
	// Among the bodies our straight-line path would pass through (within ThreatRadii), keep the tightest cap.
	FVector BestDir = FVector::ZeroVector;
	double BestCap = TNumericLimits<double>::Max();
	AActor* BestBody = nullptr;
	for (const FProximityCandidate& C : ProximityCandidates)
	{
		AActor* Body = C.Body.Get();
		if (!Body)
		{
			continue;
		}
		const FVector To = Body->GetActorLocation() - Here;
		const double Dist = To.Size();
		if (Dist < 1.0)
		{
			continue;
		}
		const FVector ToDir = To / Dist;
		const double Dot = Dir.IsNearlyZero() ? 1.0 : FVector::DotProduct(Dir, ToDir);
		const double Surface = FMath::Max(Dist - C.Radius, 0.0);
		const bool bInside = Surface <= 0.0;
		if (!bInside)
		{
			if (Dot <= 0.0)
			{
				continue; // moving away
			}
			const double Periapsis = Dist * FMath::Sqrt(FMath::Max(1.0 - Dot * Dot, 0.0));
			if (Periapsis >= C.Radius * FMath::Max(ThreatRadii, 1.f))
			{
				continue; // the path misses it
			}
		}
		const double Cap = FMath::Max(Surface / FMath::Max(ApproachSeconds, 0.5f), MinApproachSpeed);
		if (Cap < BestCap)
		{
			BestCap = Cap;
			BestBody = Body;
			BestDir = ToDir;
		}
	}
	if (!BestBody)
	{
		return FVector::ZeroVector;
	}
	ProximityBody = BestBody;
	ProximityCap = BestCap;
	ProximityBodyName = BestBody->GetActorNameOrLabel();
	if (const APlanetBody* Planet = Cast<APlanetBody>(BestBody)) { if (!Planet->PlanetName.IsEmpty()) { ProximityBodyName = Planet->PlanetName; } }
	if (const AStarBody* Star = Cast<AStarBody>(BestBody)) { if (!Star->StarName.IsEmpty()) { ProximityBodyName = Star->StarName; } }
	return BestDir;
}
