// Slonk - kinematic 6DOF flight model for a capital starship (impulse + warp).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/MovementComponent.h"
#include "StarshipMovementComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnStarshipCollision, const FHitResult&, Hit);

/**
 * Kinematic flight model for a very large ship. Not physics-simulated: speeds at warp are far beyond
 * anything Chaos should see, so the component integrates its own velocity and angular rates and moves
 * the updated component directly. Sweeps for collision at impulse, teleports at warp.
 *
 * Steering input is in the range [-1, 1] per axis and is consumed every tick (set it every frame).
 */
UCLASS(ClassGroup = (Starship), meta = (BlueprintSpawnableComponent))
class SLONK_API UStarshipMovementComponent : public UMovementComponent
{
	GENERATED_BODY()

public:
	UStarshipMovementComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ---- Impulse drive -------------------------------------------------------------------------

	/** Speed at full (100%) impulse, cm/s. Canon full impulse is 0.25 c. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impulse")
	double FullImpulseSpeed = 7.4948e9;

	/** Linear acceleration at impulse, cm/s^2. 0 -> full impulse in about 8 s by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impulse")
	double ImpulseAcceleration = 9.4e8;

	/** Speed = FullImpulseSpeed * throttle^ThrottleExponent, so low throttle gives km/s precision near planets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impulse", meta = (ClampMin = "1", ClampMax = "5"))
	float ThrottleExponent = 3.f;

	/** Reverse thrust is limited to this fraction of full impulse. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impulse", meta = (ClampMin = "0", ClampMax = "1"))
	double ReverseThrottleLimit = 0.25;

	// ---- Attitude control ---------------------------------------------------------------------

	/** Max pitch rate, deg/s. A capital ship, but one that has to be fun to fly: a 180 takes ~6 s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attitude")
	float MaxPitchRate = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attitude")
	float MaxYawRate = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attitude")
	float MaxRollRate = 50.f;

	/** How quickly angular rates approach their target (1/s). Lower = heavier feel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attitude")
	float AngularResponse = 3.5f;

	/** Steering authority while at warp, as a fraction of impulse rates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attitude", meta = (ClampMin = "0", ClampMax = "1"))
	float WarpSteeringScale = 0.35f;

	// ---- Vertical thrust and orbit hold -------------------------------------------------------

	/** Max vertical (ship-up) thruster speed as a fraction of full impulse. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Impulse", meta = (ClampMin = "0", ClampMax = "1"))
	float VerticalThrustFraction = 0.02f;

	/** Vertical input in [-1, 1] as of the last tick (for the HUD). */
	UPROPERTY(BlueprintReadOnly, Category = "State")
	float LastVerticalInput = 0.f;

	/** Per-frame vertical thruster input, +1 = up along the hull. Consumed each tick. */
	UFUNCTION(BlueprintCallable, Category = "Starship|Movement")
	void SetVerticalInput(float Up) { VerticalInput = FMath::Clamp(Up, -1.f, 1.f); }

	// ---- Reaction control thrusters ----------------------------------------------------------

	/** Top speed from the manoeuvring thrusters alone (cm/s): docking speeds, 250 m/s by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Thrusters")
	double ThrusterMaxSpeed = 2.5e4;

	/** Body-frame thruster input (forward, right, up) in [-1, 1] as of the last tick (for FX / HUD). */
	UPROPERTY(BlueprintReadOnly, Category = "State")
	FVector LastThrusterInput = FVector::ZeroVector;

	/** Steering input (pitch, yaw, roll) the last tick consumed (for FX). */
	UPROPERTY(BlueprintReadOnly, Category = "State")
	FVector LastSteeringInput = FVector::ZeroVector;

	/** Per-frame thruster translation input along (forward, right, up), each in [-1, 1]. Consumed each tick. */
	UFUNCTION(BlueprintCallable, Category = "Starship|Movement")
	void SetThrusterInput(const FVector& ForwardRightUp)
	{
		ThrusterInput = FVector(FMath::Clamp(ForwardRightUp.X, -1.0, 1.0), FMath::Clamp(ForwardRightUp.Y, -1.0, 1.0), FMath::Clamp(ForwardRightUp.Z, -1.0, 1.0));
	}

	/** While holding orbit the ship circles OrbitBody at OrbitRadius; the throttle sets the tangential speed. */
	UPROPERTY(BlueprintReadOnly, Category = "Orbit")
	bool bOrbitHold = false;

	UPROPERTY(BlueprintReadOnly, Category = "Orbit")
	TObjectPtr<AActor> OrbitBody;

	UPROPERTY(BlueprintReadOnly, Category = "Orbit")
	FString OrbitBodyName;

	UPROPERTY(BlueprintReadOnly, Category = "Orbit")
	double OrbitRadius = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Orbit")
	double OrbitBodyRadius = 0.0;

	/** True when the held body is a vessel (ship / station): station-keeping on a moving target. */
	UPROPERTY(BlueprintReadOnly, Category = "Orbit")
	bool bFollowingVessel = false;

	/** Closest station-keeping range to a vessel, in multiples of its bounding radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit")
	float FollowMinRadii = 2.5f;

	/** Range the hold eases in to when it engages on a vessel, in bounding radii (so the ship is actually visible). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit")
	float FollowEngageRadii = 6.f;

	/** Range the hold is easing toward (cm); PgUp/PgDn move it. */
	UPROPERTY(BlueprintReadOnly, Category = "Orbit")
	double OrbitRadiusTarget = 0.0;

	/** While holding orbit and not steering, the nose turns to keep the body ahead (orbit plane level). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit")
	bool bOrbitFaceBody = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit")
	float OrbitFaceRate = 1.5f;

	/** Cap on the orbital speed (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Orbit")
	double OrbitMaxSpeed = 3.0e8;

	/** Start circling Body (radius BodyRadiusCm) at the current distance, prograde along the current heading. */
	UFUNCTION(BlueprintCallable, Category = "Starship|Movement")
	void SetOrbitHold(AActor* Body, double BodyRadiusCm, const FString& BodyName);

	UFUNCTION(BlueprintCallable, Category = "Starship|Movement")
	void ClearOrbitHold();
	double GetOrbitMinRadius() const;

	UFUNCTION(BlueprintPure, Category = "Starship|Movement")
	double GetOrbitSpeed() const
	{
		// Around a vessel the throttle circles at most a quarter turn per second, whatever the range.
		const double Cap = bFollowingVessel ? FMath::Max(OrbitRadius * 1.5, 1.0) : OrbitMaxSpeed;
		return FMath::Clamp(GetImpulseTargetSpeed(), -Cap, Cap);
	}

	/** Orbital period at the current throttle (s), 0 when stationary. */
	UFUNCTION(BlueprintPure, Category = "Starship|Movement")
	double GetOrbitPeriod() const { const double V = FMath::Abs(GetOrbitSpeed()); return V > 1.0 ? 2.0 * PI * OrbitRadius / V : 0.0; }

	// ---- Proximity governor -------------------------------------------------------------------

	/**
	 * Near a planet or star, closing speed is capped so the body fills the view over seconds instead of
	 * being crossed in a frame: cap = max(distance to surface / ApproachSeconds, MinApproachSpeed).
	 * At warp the cap slows the ship; once it falls below full impulse the warp drive drops out.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proximity")
	bool bProximityGovernor = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proximity", meta = (ClampMin = "0.5", ClampMax = "30"))
	float ApproachSeconds = 3.f;

	/** The governor only acts when the current heading would pass within this many radii of the body. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proximity", meta = (ClampMin = "1", ClampMax = "5"))
	float ThreatRadii = 1.3f;

	/** Floor for the closing-speed cap (cm/s), so the surface can still be reached. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Proximity")
	double MinApproachSpeed = 2.0e5;

	/** True while the governor is holding the ship below what the throttle or warp factor asks for. */
	UPROPERTY(BlueprintReadOnly, Category = "Proximity")
	bool bProximityLimited = false;

	/** Current closing-speed cap toward the nearest body (cm/s), or 0 when no body is near. */
	UPROPERTY(BlueprintReadOnly, Category = "Proximity")
	double ProximityCap = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Proximity")
	FString ProximityBodyName;

	/** Body currently governing the speed, if any. */
	UPROPERTY(BlueprintReadOnly, Category = "Proximity")
	TObjectPtr<AActor> ProximityBody;

	// ---- Runtime state ------------------------------------------------------------------------

	/** Current throttle in [-ReverseThrottleLimit, 1]. */
	UPROPERTY(BlueprintReadOnly, Category = "State")
	double Throttle = 0.0;

	/** Current body-frame angular velocity in deg/s (Pitch, Yaw, Roll). */
	UPROPERTY(BlueprintReadOnly, Category = "State")
	FRotator AngularVelocity = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "State")
	bool bWarpActive = false;

	/** Speed commanded by the warp drive while bWarpActive (cm/s). */
	UPROPERTY(BlueprintReadOnly, Category = "State")
	double WarpSpeed = 0.0;

	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnStarshipCollision OnCollision;

	// ---- API ----------------------------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Starship|Movement")
	void SetThrottle(double NewThrottle);

	UFUNCTION(BlueprintCallable, Category = "Starship|Movement")
	void AdjustThrottle(double Delta);

	/** Set the per-frame steering input, each axis in [-1, 1]. +Pitch = nose up, +Yaw = nose right, +Roll = roll right. */
	UFUNCTION(BlueprintCallable, Category = "Starship|Movement")
	void SetSteeringInput(float Pitch, float Yaw, float Roll);

	/** Adds to the per-frame steering input (accumulates until the next tick). */
	UFUNCTION(BlueprintCallable, Category = "Starship|Movement")
	void AddSteeringInput(float Pitch, float Yaw, float Roll);

	/** Called by the warp drive. While active, the ship moves at exactly WarpSpeed along its forward axis. */
	UFUNCTION(BlueprintCallable, Category = "Starship|Movement")
	void SetWarp(bool bActive, double InWarpSpeed);

	UFUNCTION(BlueprintPure, Category = "Starship|Movement")
	double GetSpeed() const { return Velocity.Size(); }

	/** Speed the impulse drive is currently trying to reach (cm/s), signed. */
	UFUNCTION(BlueprintPure, Category = "Starship|Movement")
	double GetImpulseTargetSpeed() const { return FMath::Sign(Throttle) * FMath::Pow(FMath::Abs(Throttle), (double)ThrottleExponent) * FullImpulseSpeed; }

	UFUNCTION(BlueprintPure, Category = "Starship|Movement")
	FVector GetSteeringInput() const { return SteeringInput; }

	/** Smoothed steering (what the ship is actually doing), each axis in [-1, 1]. Useful for FX / HUD. */
	UFUNCTION(BlueprintPure, Category = "Starship|Movement")
	FVector GetNormalizedAngularVelocity() const;

private:
	/** Evaluate every body against the heading; returns the unit direction to the governing body (zero if none threatens). */
	FVector UpdateProximity(const FVector& Here, const FVector& Dir);

	/** (Pitch, Yaw, Roll) in [-1, 1], consumed each tick. */
	FVector SteeringInput = FVector::ZeroVector;
	float VerticalInput = 0.f;
	FVector ThrusterInput = FVector::ZeroVector;
	FVector OrbitAxis = FVector::UpVector;
	float ProximityScanTimer = 0.f;
	struct FProximityCandidate { TWeakObjectPtr<AActor> Body; double Radius = 0.0; };
	TArray<FProximityCandidate> ProximityCandidates;
};
