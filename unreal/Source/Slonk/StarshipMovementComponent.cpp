#include "StarshipMovementComponent.h"

#include "EngineUtils.h"
#include "PlanetBody.h"
#include "PlanetTerrainComponent.h"
#include "StarBody.h"
#include "SpaceVessel.h"

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
	if (bActive)
	{
		ClearOrbitHold();
	}
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
		ThrusterInput = FVector::ZeroVector;
		return;
	}

	// Clamp huge hitches so a frame spike never flings the ship across the system.
	DeltaTime = FMath::Min(DeltaTime, 0.1f);

	if (bLanded)
	{
		HoldLanded(DeltaTime);
		if (bLanded)
		{
			return;
		}
	}

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
	const FVector Up = NewQuat.GetUpVector();
	const FVector Here = UpdatedComponent->GetComponentLocation();
	LastVerticalInput = VerticalInput;
	LastThrusterInput = ThrusterInput;
	LastSteeringInput = SteeringInput;
	bProximityLimited = false;

	// Orbit hold: station-keeping around a body. Attitude stays free; translation follows the circle.
	if (bOrbitHold && !bWarpActive)
	{
		if (!OrbitBody)
		{
			ClearOrbitHold();
		}
		else
		{
			const FVector Center = OrbitBody->GetActorLocation();
			const FVector R = Here - Center;
			const double Dist = R.Size();
			const FVector Radial = Dist > 1.0 ? R / Dist : FVector::UpVector;
			// Vertical thrust raises or lowers the orbit.
			const double MinRadius = GetOrbitMinRadius();
			// Range changes scale with the range itself, so a 5 km formation and a 50,000 km orbit both adjust sensibly.
			const double RangeRate = bFollowingVessel ? OrbitRadiusTarget * 0.5 : VerticalThrustFraction * FullImpulseSpeed;
			OrbitRadiusTarget = FMath::Max(OrbitRadiusTarget + VerticalInput * RangeRate * DeltaTime, MinRadius);
			// Ease toward the wanted range (a few seconds) instead of snapping.
			OrbitRadius = FMath::Max(FMath::Lerp(OrbitRadius, OrbitRadiusTarget, 1.0 - FMath::Exp(-1.2 * DeltaTime)), MinRadius);
			const double Speed = GetOrbitSpeed();
			const double Angle = Speed * DeltaTime / FMath::Max(OrbitRadius, 1.0);
			const FVector NewRadial = Radial.RotateAngleAxisRad(Angle, OrbitAxis).GetSafeNormal();
			const FVector NewPos = Center + NewRadial * OrbitRadius;
			Velocity = (NewPos - Here) / DeltaTime;
			FQuat OrbitQuat = NewQuat;
			if (bOrbitFaceBody && SteeringInput.IsNearlyZero(0.02))
			{
				// Nose on the body, orbit plane level: the planet sits ahead and slides past as we circle.
				const FQuat Want = FRotationMatrix::MakeFromXZ(-NewRadial, OrbitAxis).ToQuat();
				OrbitQuat = FQuat::Slerp(OrbitQuat, Want, 1.f - FMath::Exp(-OrbitFaceRate * DeltaTime)).GetNormalized();
				AngularVelocity = FRotator::ZeroRotator;
			}
			UpdatedComponent->MoveComponent(NewPos - Here, OrbitQuat, /*bSweep*/ false, nullptr, MOVECOMP_NoFlags, ETeleportType::TeleportPhysics);
			UpdateComponentVelocity();
			SteeringInput = FVector::ZeroVector;
			VerticalInput = 0.f;
			ThrusterInput = FVector::ZeroVector;
			return;
		}
	}

	if (bWarpActive)
	{
		// The warp drive owns the speed profile; we follow it exactly unless a body is in the way.
		Velocity = Forward * WarpSpeed;
	}
	else
	{
		// Manoeuvring thrusters add a small body-frame velocity on top of the impulse drive: enough to translate
		// alongside a station or back out of a dock, nothing like the drive itself.
		const FVector Right = NewQuat.GetRightVector();
		const FVector Thrusters = (Forward * ThrusterInput.X + Right * ThrusterInput.Y + Up * ThrusterInput.Z) * ThrusterMaxSpeed;
		// Vertical thrust: a fraction of full impulse in deep space (1500 km/s), but near a planet a climb rate that
		// grows with the square root of the height above ground: 60 m/s at 100 m, 190 at 1 km, 600 at 10 km. The
		// fixed fraction was 1500 km/s next to the ground, and only felt fine going down because the terrain
		// floor swallowed it at the clearance height.
		double VerticalSpeed = VerticalThrustFraction * FullImpulseSpeed;
		if (AltitudeAGLCm >= 0.0)
		{
			VerticalSpeed = FMath::Min(VerticalSpeed, GroundVerticalRate(AltitudeAGLCm));
		}
		const FVector TargetVelocity = Forward * GetImpulseTargetSpeed() + Up * (VerticalInput * VerticalSpeed) + Thrusters;
		const FVector DeltaV = TargetVelocity - Velocity;
		// Acceleration-limited, and with a response time also a fraction of the gap each tick: the shuttle at
		// 2 g took four minutes to stop from 5 km/s, and a tap of the throttle took seconds to show.
		double MaxDeltaV = ImpulseAcceleration * DeltaTime;
		if (SpeedResponseSeconds > 0.f)
		{
			MaxDeltaV = FMath::Max(MaxDeltaV, DeltaV.Size() * (1.0 - FMath::Exp(-DeltaTime / SpeedResponseSeconds)));
		}
		Velocity += DeltaV.GetClampedToMaxSize(MaxDeltaV);
	}
	// Automatic braking: when the heading would carry us into a body, scale the whole velocity so the
	// closing rate never exceeds the cap (scaling keeps the heading; clamping one axis bled thrust sideways).
	{
		const FVector Dir = Velocity.GetSafeNormal();
		const FVector ToBody = UpdateProximity(Here, Dir);
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

	ApplyTerrainFloor(DeltaTime);

	UpdateComponentVelocity();
	SteeringInput = FVector::ZeroVector;
	VerticalInput = 0.f;
	ThrusterInput = FVector::ZeroVector;
}

double UStarshipMovementComponent::GroundVerticalRate(double AltitudeCm) const
{
	// Climb rate near the ground grows with the square root of the height above it: 60 m/s at 100 m, 190 at 1 km,
	// 600 at 10 km for the starship; floored at its own clearance so touchdown height is not a crawl.
	const double Floor = FMath::Max(GroundClearanceCm, 300.0);
	return VerticalRateScale * 6000.0 * FMath::Sqrt(FMath::Max(AltitudeCm, Floor) / 10000.0);
}

void UStarshipMovementComponent::SetTerrainFollow(double AltitudeCm)
{
	bTerrainFollow = true;
	HoverAltitudeCm = FMath::Max(AltitudeCm, GroundClearanceCm);
	ClearOrbitHold();
}

void UStarshipMovementComponent::ApplyTerrainFloor(float DeltaTime)
{
	GroundBody = nullptr;
	AltitudeAGLCm = -1.0;
	GroundCarry = 0.f;
	if (!UpdatedComponent) { return; }
	TerrainScanTimer -= DeltaTime;
	if (TerrainScanTimer <= 0.f)
	{
		TerrainScanTimer = 1.f;
		TerrainBodies.Reset();
		if (UWorld* World = GetWorld())
		{
			for (TActorIterator<APlanetBody> It(World); It; ++It) { TerrainBodies.Add(*It); }
		}
	}
	const FVector Here = UpdatedComponent->GetComponentLocation();
	APlanetBody* Near = nullptr;
	double NearDist = TNumericLimits<double>::Max();
	for (const TWeakObjectPtr<APlanetBody>& W : TerrainBodies)
	{
		APlanetBody* P = W.Get();
		if (!P) { continue; }
		const double D = FVector::Dist(Here, P->GetActorLocation());
		if (D < P->GetRadiusCm() * 1.5 && D < NearDist) { Near = P; NearDist = D; }
	}
	if (!Near) { CarryBody = nullptr; return; }
	const FVector Centre = Near->GetActorLocation();
	const FVector Rv = Here - Centre;
	const double Dist = Rv.Size();
	if (Dist < 1.0) { return; }
	const FVector Radial = Rv / Dist;
	const double SurfR = Near->GetSurfaceRadiusCm(Here);
	GroundBody = Near;
	GroundHeightCm = SurfR - Near->GetRadiusCm();
	AltitudeAGLCm = Dist - SurfR;
	if (bWarpActive || bOrbitHold) { CarryBody = nullptr; return; }

	// Ride the ground: the planet turns under us (its spin is about its local Z), so near it turn with it, hull,
	// position and velocity together. The surface of an Earth-size world does close to half a kilometre a second;
	// nobody wants to match that by hand to set down, and a parked mothership should still be over the same
	// valley when the shuttle comes back.
	{
		const double Radii = Dist / FMath::Max(Near->GetRadiusCm(), 1.0);
		GroundCarry = bTerrainFollow ? 1.f : (float)FMath::Clamp((CarryZeroRadii - Radii) / FMath::Max(CarryZeroRadii - CarryFullRadii, 0.01f), 0.0, 1.0);
		// The world's actual turn since our last tick (not its rate times a clamped frame time, which fell behind
		// the ground on every long frame), scaled by the carry.
		const FQuat PlanetQ = Near->GetActorQuat();
		if (CarryBody.Get() != Near)
		{
			CarryBody = Near;
			CarryBodyQuat = PlanetQ;
		}
		else if (GroundCarry > 0.f)
		{
			FQuat Q = PlanetQ * CarryBodyQuat.Inverse();
			if (!Q.IsIdentity(1e-12))
			{
				if (GroundCarry < 1.f) { Q = FQuat::Slerp(FQuat::Identity, Q, GroundCarry); }
				UpdatedComponent->SetWorldLocationAndRotation(Centre + Q.RotateVector(Rv), Q * UpdatedComponent->GetComponentQuat(), false, nullptr, ETeleportType::TeleportPhysics);
				Velocity = Q.RotateVector(Velocity);
			}
		}
		CarryBodyQuat = PlanetQ;
	}

	double WantDist = -1.0;
	if (bTerrainFollow)
	{
		// Canyon autopilot: compare the depth of the cut 400 m ahead-left, ahead and ahead-right, and yaw the hull
		// (and the velocity with it) toward the deeper side, up to 20 degrees a second. The gorge bends a few
		// hundred metres per kilometre, which a straight run leaves within seconds.
		if (bCanyonFollow && Near->Terrain && Velocity.SizeSquared() > 1.0e6)
		{
			const FVector Up = Rv.GetSafeNormal();
			const FVector Fwd = (Velocity - Up * FVector::DotProduct(Velocity, Up)).GetSafeNormal();
			if (!Fwd.IsNearlyZero())
			{
				const FVector Right = FVector::CrossProduct(Up, Fwd).GetSafeNormal();
				const double W = 4.0e4;
				auto CutAt = [&](const FVector& Pw) { return (double)Near->Terrain->SampleAtWorld(Pw, 1.0e4).CutCm; };
				const double CutL = CutAt(Here + Fwd * W - Right * W), CutC = CutAt(Here + Fwd * W), CutR = CutAt(Here + Fwd * W + Right * W);
				const double Deepest = FMath::Max3(CutL, CutC, CutR);
				if (Deepest > 1.0)
				{
					const double Diff = FMath::Clamp((CutR - CutL) / Deepest * 2.0, -1.0, 1.0);
					const double Turn = Diff * FMath::DegreesToRadians(20.0) * DeltaTime;
					if (!FMath::IsNearlyZero(Turn))
					{
						const FQuat Q(Up, Turn);
						UpdatedComponent->SetWorldRotation(Q * UpdatedComponent->GetComponentQuat(), false, nullptr, ETeleportType::TeleportPhysics);
						Velocity = Q.RotateVector(Velocity);
					}
				}
			}
		}
		// Vertical thrust moves the hover height at the same square-root rate as free flight (the old 60% of the
		// height per second compounded: a climb doubled every second and a descent crawled).
		if (!FMath::IsNearlyZero(VerticalInput))
		{
			HoverAltitudeCm = FMath::Max(HoverAltitudeCm + VerticalInput * GroundVerticalRate(HoverAltitudeCm) * DeltaTime, GroundClearanceCm);
		}
		const double Target = SurfR + HoverAltitudeCm;
		// Ease onto the contour over about half a second: the ground is sampled at the ship, so a cliff ahead
		// becomes a climb as it passes under the hull rather than a wall.
		WantDist = FMath::Lerp(Dist, Target, 1.0 - FMath::Exp(-2.5 * DeltaTime));
	}
	else if (bCanLand && Near->HasTerrain() && Dist < SurfR + GroundClearanceCm + 100.0 && Velocity.Size() < LandingSpeedCm && VerticalInput <= 0.05f && Throttle < 0.02)
	{
		Land(Near, Centre, Radial, SurfR);
		return;
	}
	else if (Dist < SurfR + GroundClearanceCm)
	{
		WantDist = SurfR + GroundClearanceCm;
	}
	if (WantDist > 0.0)
	{
		const FVector RvNow = UpdatedComponent->GetComponentLocation() - Centre;
		const FVector RadialNow = RvNow.GetSafeNormal();
		const FVector NewPos = Centre + RadialNow * WantDist;
		UpdatedComponent->SetWorldLocation(NewPos, false, nullptr, ETeleportType::TeleportPhysics);
		// Keep the along-ground speed; drop whatever was carrying us into the ground.
		const double Inward = FVector::DotProduct(Velocity, Radial);
		if (bTerrainFollow || Inward < 0.0)
		{
			Velocity -= Radial * Inward;
		}
		AltitudeAGLCm = WantDist - SurfR;
	}
}

void UStarshipMovementComponent::SetOrbitHold(AActor* Body, double BodyRadiusCm, const FString& BodyName)
{
	if (!Body || !UpdatedComponent)
	{
		return;
	}
	const FVector Here = UpdatedComponent->GetComponentLocation();
	const FVector R = Here - Body->GetActorLocation();
	const double Dist = R.Size();
	if (Dist < 1.0)
	{
		return;
	}
	const FVector Radial = R / Dist;
	// Prograde along the current heading; if we are pointed straight at or away from the body, use the right vector.
	FVector Axis = FVector::CrossProduct(Radial, UpdatedComponent->GetForwardVector());
	if (Axis.SizeSquared() < 1e-4)
	{
		Axis = FVector::CrossProduct(Radial, UpdatedComponent->GetRightVector());
	}
	OrbitAxis = Axis.GetSafeNormal();
	OrbitBody = Body;
	OrbitBodyRadius = BodyRadiusCm;
	OrbitBodyName = BodyName;
	bFollowingVessel = Body->IsA<ASpaceVessel>();
	OrbitRadius = FMath::Max(Dist, GetOrbitMinRadius());
	// Planets: hold the current altitude. Vessels: close to a range where the ship fills some of the view.
	OrbitRadiusTarget = bFollowingVessel ? FMath::Clamp(OrbitRadius, GetOrbitMinRadius(), BodyRadiusCm * FMath::Max(FollowEngageRadii, FollowMinRadii)) : OrbitRadius;
	bOrbitHold = true;
}

double UStarshipMovementComponent::GetOrbitMinRadius() const
{
	return bFollowingVessel ? OrbitBodyRadius * FMath::Max(FollowMinRadii, 1.2f) : OrbitBodyRadius * 1.01 + 5.0e5;
}

void UStarshipMovementComponent::ClearOrbitHold()
{
	bOrbitHold = false;
	bFollowingVessel = false;
	OrbitBody = nullptr;
	OrbitBodyName.Empty();
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

void UStarshipMovementComponent::Land(APlanetBody* Body, const FVector& Centre, const FVector& Radial, double SurfR)
{
	if (!Body || !UpdatedComponent) { return; }
	// The slope under the skids, from the ground height a few metres out along two tangents.
	const FVector Up = Radial;
	FVector T1 = FVector::CrossProduct(Up, UpdatedComponent->GetForwardVector()).GetSafeNormal();
	if (T1.IsNearlyZero()) { T1 = FVector::CrossProduct(Up, FVector::RightVector).GetSafeNormal(); }
	const FVector T2 = FVector::CrossProduct(T1, Up).GetSafeNormal();
	const double D = 400.0;
	auto H = [&](const FVector& T) { return Body->GetSurfaceRadiusCm(Centre + Up * SurfR + T * D); };
	const double S1 = (H(T1) - H(-T1)) / (2.0 * D);
	const double S2 = (H(T2) - H(-T2)) / (2.0 * D);
	const FVector Normal = (Up - T1 * S1 - T2 * S2).GetSafeNormal();
	// Keep the heading; stand the hull on the slope.
	FVector Fwd = UpdatedComponent->GetForwardVector();
	Fwd = (Fwd - Normal * FVector::DotProduct(Fwd, Normal)).GetSafeNormal();
	if (Fwd.IsNearlyZero()) { Fwd = T2; }
	const FQuat Rot = FRotationMatrix::MakeFromXZ(Fwd, Normal).ToQuat();
	const FVector Pos = Centre + Up * (SurfR + GroundClearanceCm);
	UpdatedComponent->SetWorldLocationAndRotation(Pos, Rot, false, nullptr, ETeleportType::TeleportPhysics);
	const FTransform PlanetT(Body->GetActorQuat(), Centre);
	LandedLocal = FTransform(Rot, Pos).GetRelativeTransform(PlanetT);
	if (LandedBody.Get() != Body)
	{
		if (AActor* Old = LandedBody.Get()) { PrimaryComponentTick.RemovePrerequisite(Old, Old->PrimaryActorTick); }
		// Hold to this frame's planet transform, not last frame's: the hull would otherwise sit a frame's worth of
		// the ground's speed (metres) behind whoever stands beside it.
		PrimaryComponentTick.AddPrerequisite(Body, Body->PrimaryActorTick);
	}
	LandedBody = Body;
	bLanded = true;
	Velocity = FVector::ZeroVector;
	AngularVelocity = FRotator::ZeroRotator;
	Throttle = 0.0;
	ClearTerrainFollow();
	GroundBody = Body;
	AltitudeAGLCm = GroundClearanceCm;
	GroundHeightCm = SurfR - Body->GetRadiusCm();
	UE_LOG(LogTemp, Log, TEXT("Landed on %s, ground %.0f m above sea, slope %.1f deg"), *Body->GetDisplayName(), GroundHeightCm / 100.0, FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(Normal, Up))));
}

void UStarshipMovementComponent::HoldLanded(float DeltaTime)
{
	APlanetBody* Body = LandedBody.Get();
	if (!Body || !UpdatedComponent)
	{
		bLanded = false;
		return;
	}
	// Throttle or lift asked for: let go of the ground.
	if (VerticalInput > 0.1f || Throttle > 0.02 || ThrusterInput.Z > 0.1 || ThrusterInput.X > 0.1)
	{
		TakeOff();
		return;
	}
	// The planet turns under us: sit where we landed in its frame.
	const FTransform PlanetT(Body->GetActorQuat(), Body->GetActorLocation());
	const FTransform W = LandedLocal * PlanetT;
	UpdatedComponent->SetWorldLocationAndRotation(W.GetLocation(), W.GetRotation(), false, nullptr, ETeleportType::TeleportPhysics);
	Velocity = FVector::ZeroVector;
	AngularVelocity = FRotator::ZeroRotator;
	LastSteeringInput = FVector::ZeroVector;
	LastThrusterInput = FVector::ZeroVector;
	LastVerticalInput = VerticalInput;
	SteeringInput = FVector::ZeroVector;
	ThrusterInput = FVector::ZeroVector;
	VerticalInput = 0.f;
	GroundBody = Body;
	CarryBody = Body;
	CarryBodyQuat = Body->GetActorQuat(); // in step for the take-off
	AltitudeAGLCm = GroundClearanceCm;
	const FVector Here = W.GetLocation();
	GroundHeightCm = Body->GetSurfaceRadiusCm(Here) - Body->GetRadiusCm();
}

void UStarshipMovementComponent::TakeOff()
{
	if (!bLanded) { return; }
	bLanded = false;
	if (UpdatedComponent && LandedBody.IsValid())
	{
		// A push off the skids, so the floor does not catch us again before the thrusters take over.
		const FVector Up = (UpdatedComponent->GetComponentLocation() - LandedBody->GetActorLocation()).GetSafeNormal();
		Velocity = Up * FMath::Max(LandingSpeedCm, 300.0);
	}
	if (AActor* Old = LandedBody.Get()) { PrimaryComponentTick.RemovePrerequisite(Old, Old->PrimaryActorTick); }
	LandedBody = nullptr;
}
