// Slonk - the player's starship: hull, flight model, warp drive, weapons, cameras and effects.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "StarshipPawn.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class USpringArmComponent;
class UCameraComponent;
class UPointLightComponent;
class UPostProcessComponent;
class UMaterialInstanceDynamic;
class UStarshipMovementComponent;
class UWarpDriveComponent;
class UStarshipWeaponsComponent;
class UWarpStreaksComponent;
class UBridgeInteriorComponent;
class UDistantStarsComponent;

UENUM(BlueprintType)
enum class EWarpVisualMode : uint8
{
	/** Star streaks plus radial streaking of everything you fly past. */
	Classic,
	/** Relativistic: aberration bunches the view forward, blue shift ahead, red shift behind, contraction along the travel axis. */
	Relativistic
};

UENUM(BlueprintType)
enum class EStarshipCameraMode : uint8
{
	Chase,
	Bridge,
	Cinematic
};

/** A running / engine light on the hull, positioned relative to the ship root (cm). */
USTRUCT(BlueprintType)
struct FStarshipLightSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light")
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light")
	FVector Offset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light")
	FLinearColor Color = FLinearColor::White;

	/** Candelas. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light")
	float Intensity = 20000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light")
	float AttenuationRadius = 12000.f;

	/** Scaled by the warp glow multiplier (nacelles, deflector) ... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light")
	bool bWarpLight = true;

	/** ... or by the impulse throttle (impulse exhausts). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light")
	bool bImpulseLight = false;
};

UCLASS()
class SLONK_API AStarshipPawn : public APawn
{
	GENERATED_BODY()

public:
	AStarshipPawn();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual UPawnMovementComponent* GetMovementComponent() const override;

	// ---- Components ----

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> CollisionBox;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> ShipMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStarshipMovementComponent> Movement;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UWarpDriveComponent> WarpDrive;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStarshipWeaponsComponent> Weapons;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Cameras")
	TObjectPtr<USpringArmComponent> ChaseArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Cameras")
	TObjectPtr<UCameraComponent> ChaseCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Cameras")
	TObjectPtr<UCameraComponent> BridgeCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Cameras")
	TObjectPtr<USceneComponent> OrbitPivot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Cameras")
	TObjectPtr<UCameraComponent> CinematicCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|FX")
	TObjectPtr<UWarpStreaksComponent> WarpStreaks;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|FX")
	TObjectPtr<UPostProcessComponent> PostProcess;

	/** Star field that follows the ship (fixed orientation) so distant systems are never hidden behind a level sky mesh. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|FX")
	TObjectPtr<UStaticMeshComponent> StarSphere;

	/** Stand-in sprites for stars beyond the star sphere (the real sun from across the system). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|FX")
	TObjectPtr<UDistantStarsComponent> DistantStars;

	/** Procedural bridge set around the bridge camera; only visible in the bridge view. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Cameras")
	TObjectPtr<UBridgeInteriorComponent> BridgeInterior;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|FX")
	TArray<TObjectPtr<UPointLightComponent>> HullLights;

	// ---- Hull ----

	/** Real-world length of the ship. The mesh is scaled so its longest axis matches this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull")
	float ShipLengthMeters = 305.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull")
	float ShipWidthMeters = 142.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull")
	float ShipHeightMeters = 72.f;

	/** Rotation applied to the mesh so its bow points down +X and its dorsal side is +Z. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull")
	FRotator MeshRelativeRotation = FRotator::ZeroRotator;

	/** Extra offset applied to the mesh after centring its bounds on the root (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull")
	FVector MeshCenterOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull")
	bool bAutoScaleMesh = true;

	/** Scalar parameter on the hull materials that drives the emissive window / grille glow. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull")
	FName GlowParameterName = TEXT("EmissiveColorMapWeight");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull")
	float BaseGlowWeight = 1.0f;

	/** Material slot whose emissive is driven by impulse throttle (the impulse exhaust grille). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull")
	FName ThrusterSlotName = TEXT("Thruster");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull")
	TArray<FStarshipLightSpec> LightSpecs;

	// ---- Cameras ----

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cameras")
	float ChaseDistanceFactor = 1.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cameras")
	float ChaseHeightFactor = 0.22f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cameras")
	float ChaseFOV = 80.f;

	/** Bridge camera position relative to the ship root (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cameras")
	FVector BridgeCameraOffset = FVector(6500.0, 0.0, 2900.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cameras")
	float BridgeFOV = 92.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cameras")
	float CinematicDistanceFactor = 2.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cameras")
	float CinematicOrbitRate = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cameras")
	float CinematicFOV = 60.f;

	/** How fast free-look springs back to centre when released (1/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cameras")
	float FreeLookReturnRate = 4.f;

	// ---- Sky ----

	/** Radius of the ship-following star sphere (cm). 5 million km by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky")
	double StarSphereRadius = 5.0e11;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky")
	TSoftObjectPtr<UMaterialInterface> StarMaterial;

	// ---- Exposure ----

	/** Lock auto-exposure to this EV100 so a black sky never blows the hull out. Set <= -100 to leave exposure automatic. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure")
	float LockedExposureEV = 5.5f;

	// ---- Debug ----

	/**
	 * Remote-control hook for automated testing: set this (e.g. through the editor's object tools while
	 * PIE runs) and the pawn executes it on the next tick, then clears it. Commands:
	 *   throttle <-0.25..1> | impulse <1-4> | stop | warp | warp on | warp off | warpfactor <f>
	 *   phasers on|off | torpedo | target | camera chase|bridge|cinematic | steer <pitch> <yaw> <roll> <seconds>
	 *   look <yaw> <pitch> <seconds> (free-look the active camera)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	FString DebugCommand;

	// ---- Warp FX ----

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp FX")
	EWarpVisualMode WarpVisualMode = EWarpVisualMode::Classic;

	/** Post-process material driving both warp looks (parameters: Mode, Strength, ForwardUV, Behind, ...). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp FX")
	TSoftObjectPtr<UMaterialInterface> WarpPostProcessMaterial;

	/** How hard the classic look streaks the scene toward the vanishing point (0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp FX")
	float ClassicStreakStrength = 0.6f;

	/** Relativistic aberration / contraction amount (0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp FX")
	float RelativisticContraction = 0.8f;

	/** Relativistic Doppler tint amount (0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp FX")
	float RelativisticDoppler = 0.85f;

	UFUNCTION(BlueprintCallable, Category = "Starship|Warp")
	void SetWarpVisualMode(EWarpVisualMode Mode) { WarpVisualMode = Mode; }

	UFUNCTION(BlueprintCallable, Category = "Starship|Warp")
	void ToggleWarpVisualMode() { WarpVisualMode = WarpVisualMode == EWarpVisualMode::Classic ? EWarpVisualMode::Relativistic : EWarpVisualMode::Classic; }

	UFUNCTION(BlueprintPure, Category = "Starship|Warp")
	EWarpVisualMode GetWarpVisualMode() const { return WarpVisualMode; }

	/** Ship position in universe space (world space plus the floating-origin offset). */
	UFUNCTION(BlueprintPure, Category = "Starship")
	FVector GetUniversePosition() const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp FX")
	float WarpFovBoost = 16.f;

	/** Extra chase-arm length at warp, as a fraction of the normal arm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp FX")
	float WarpArmLengthBoost = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp FX")
	float WarpChromaticAberration = 3.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp FX")
	float WarpFlashBloom = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp FX")
	FLinearColor WarpFlashTint = FLinearColor(0.75f, 0.85f, 1.3f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp FX")
	float WarpRumble = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp FX")
	float ShakeDecayRate = 3.f;

	// ---- API ----

	UFUNCTION(BlueprintCallable, Category = "Starship|Camera")
	void SetCameraMode(EStarshipCameraMode Mode);

	UFUNCTION(BlueprintCallable, Category = "Starship|Camera")
	void CycleCameraMode();

	UFUNCTION(BlueprintPure, Category = "Starship|Camera")
	EStarshipCameraMode GetCameraMode() const { return CameraMode; }

	UFUNCTION(BlueprintCallable, Category = "Starship|Camera")
	void SetFreeLook(bool bEnabled) { bFreeLook = bEnabled; }

	UFUNCTION(BlueprintPure, Category = "Starship|Camera")
	bool IsFreeLooking() const { return bFreeLook; }

	/** Yaw/pitch in degrees to add to the free-look camera. */
	UFUNCTION(BlueprintCallable, Category = "Starship|Camera")
	void AddLookInput(float YawDegrees, float PitchDegrees);

	UFUNCTION(BlueprintCallable, Category = "Starship|Camera")
	void ResetCamera();

	UFUNCTION(BlueprintPure, Category = "Starship|Camera")
	UCameraComponent* GetActiveCamera() const;

	/** Teleport the ship to a safe standoff from an actor (a few radii sunward), facing it, at rest. */
	UFUNCTION(BlueprintCallable, Category = "Starship")
	void JumpToActor(AActor* Target, double TargetRadiusCm, float StandoffRadii = 3.f);

	/** Jump to the HUD's nav target (cycling to the next body first if none is set). */
	UFUNCTION(BlueprintCallable, Category = "Starship")
	void JumpToNavTarget();

	/** Kick the camera (0..1 is a sensible range; stacks). */
	UFUNCTION(BlueprintCallable, Category = "Starship|Camera")
	void AddCameraShake(float Amplitude);

	UFUNCTION(BlueprintPure, Category = "Starship")
	float GetShipLengthCm() const { return ShipLengthMeters * 100.f; }

	/** True while a debug "steer" command is driving the ship (the controller leaves steering alone). */
	UFUNCTION(BlueprintPure, Category = "Starship")
	bool IsDebugSteering() const { return DebugSteerTime > 0.f; }

protected:
	UFUNCTION()
	void HandleCollision(const FHitResult& Hit);

private:
	void ApplyHullDimensions();
	void SetupHullMaterials();
	void SetupHullLights();
	void UpdateCameras(float DeltaTime);
	void UpdateWarpEffects(float DeltaTime);
	void UpdateHullGlow(float DeltaTime);
	void AttachStreaksToActiveCamera();
	void ProcessDebugCommand();
	void ApplyExposure();

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> HullMaterials;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> WarpPPMID;

	void UpdateWarpPostProcess(float Streak, float Flash);

	int32 ThrusterMaterialIndex = INDEX_NONE;
	EStarshipCameraMode CameraMode = EStarshipCameraMode::Chase;
	bool bFreeLook = false;
	FVector2D FreeLookAngles = FVector2D::ZeroVector; // (Yaw, Pitch)
	float ShakeAmplitude = 0.f;
	float ShakeTime = 0.f;
	float OrbitAngle = 0.f;
	float ChaseArmBaseLength = 0.f;
	FVector ChaseArmBaseSocketOffset = FVector::ZeroVector;
	float LastTorpedoReadiness = 1.f;
	FVector DebugSteer = FVector::ZeroVector;
	float DebugSteerTime = 0.f;
	FVector2D DebugLook = FVector2D::ZeroVector;
	float DebugLookTime = 0.f;
};
