// Slonk - a crew member on foot: walks on worlds under their own gravity, flies a thruster pack in space and
// stands on the hulls with magnetic boots.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "PersonPawn.generated.h"

class UCapsuleComponent;
class UDynamicMeshComponent;
class USpringArmComponent;
class UCameraComponent;
class UMaterialInstanceDynamic;
class URcsThrusterComponent;
class APlanetBody;
class AStarshipPawn;

/** Whose motion the person shares. */
UENUM(BlueprintType)
enum class EPersonFrame : uint8
{
	/** Nothing near: inertial, the thruster pack is the only way about. */
	Free,
	/** Within a world's reach: carried round with its spin, pulled by its gravity, the ground is the floor. */
	Ground,
	/** On or beside a craft: positions are kept in the craft's frame so a parked ship is solid ground. */
	Hull
};

UENUM(BlueprintType)
enum class EPersonCamera : uint8
{
	Third,
	Helmet
};

/**
 * Kinematic, like the ships: the pawn integrates its own velocity and moves its root. Standing on a world it
 * walks on the analytic height field (the terrain meshes carry no collision); on a hull it traces the Boots
 * channel against the craft's complex collision and keeps its transform in the craft's frame between ticks;
 * floating it flies the pack with the ship's thruster controls. Gravity is the world's own, from its radius and
 * mean density, so a jump on a small moon goes a long way.
 */
UCLASS()
class SLONK_API APersonPawn : public APawn
{
	GENERATED_BODY()

public:
	APersonPawn();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/** Step out of a craft at Airlock (a standing transform: Z up along the surface). HullOf is the craft whose
	 *  hull the boots start on (null to start on the ground or in the open). */
	void BeginEVA(AStarshipPawn* FromCraft, const FTransform& Airlock, AStarshipPawn* HullOf);

	/** F: back aboard, when on the craft's hull or within BoardRangeCm of its airlock. */
	UFUNCTION(BlueprintCallable, Category = "Person") bool RequestBoard();
	UFUNCTION(BlueprintPure, Category = "Person") bool CanBoard() const;
	UFUNCTION(BlueprintPure, Category = "Person") double DistanceToAirlock() const;

	/** Console: `away <command>` (the same commands as DebugCommand). */
	UFUNCTION(Exec) void Away(const FString& Command);

	/** Remote-control hook, as the ship's: move <fwd> <right> <s> | vert <up> <s> | steer <pitch> <yaw> <roll> <s> |
	 *  look <yaw> <pitch> | jump | boots on|off | brake | camera third|helmet | sprint on|off | board */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug") FString DebugCommand;

	// ---- Per-frame input (consumed each tick) ----
	void SetMoveInput(float Forward, float Right) { MoveInput = FVector2D(FMath::Clamp(Forward, -1.f, 1.f), FMath::Clamp(Right, -1.f, 1.f)); }
	void SetVerticalInput(float Up) { VerticalInput = FMath::Clamp(Up, -1.f, 1.f); }
	/** Turn keys: yaw on the ground, roll in free flight. */
	void SetTurnInput(float Turn) { TurnInput = FMath::Clamp(Turn, -1.f, 1.f); }
	/** Mouse: turns the body and raises the eyes; in free flight it pitches and yaws the whole person. */
	void AddLookInput(float YawDegrees, float PitchDegrees);
	void SetSprint(bool bOn) { bSprint = bOn; }
	void SetFreeLook(bool bOn) { bFreeLook = bOn; }
	void Jump();
	void ToggleBoots();
	/** X: the pack kills relative motion until stopped, or until a jet is asked for. */
	void Brake() { bBraking = true; }
	void CycleCamera();
	void ResetCamera();
	void SetCameraMode(EPersonCamera Mode);
	void AdjustZoom(float Steps);
	EPersonCamera GetCameraMode() const { return CameraMode; }
	bool IsFreeLooking() const { return bFreeLook; }

	// ---- State (for the HUD) ----
	UPROPERTY(BlueprintReadOnly, Category = "Person") EPersonFrame Frame = EPersonFrame::Free;
	/** Feet on a surface: the ground, or a hull with the boots holding. */
	UPROPERTY(BlueprintReadOnly, Category = "Person") bool bGrounded = false;
	UPROPERTY(BlueprintReadOnly, Category = "Person") bool bBootsArmed = true;
	/** Velocity relative to the frame (cm/s). */
	UPROPERTY(BlueprintReadOnly, Category = "Person") FVector Velocity = FVector::ZeroVector;
	/** Height of the feet above the ground, cm; negative without a world near. */
	UPROPERTY(BlueprintReadOnly, Category = "Person") double AltitudeCm = -1.0;
	/** Local gravity, cm/s^2 (0 in the open). */
	UPROPERTY(BlueprintReadOnly, Category = "Person") double GravityCmS2 = 0.0;
	/** Peak height of the last jump, cm (for the HUD's sense of the gravity). */
	UPROPERTY(BlueprintReadOnly, Category = "Person") double LastJumpHeightCm = 0.0;
	/** The craft we came from, and the one the boots are on or near. */
	UPROPERTY(BlueprintReadOnly, Category = "Person") TWeakObjectPtr<AStarshipPawn> Craft;
	UPROPERTY(BlueprintReadOnly, Category = "Person") TWeakObjectPtr<AStarshipPawn> HullCraft;
	UPROPERTY(BlueprintReadOnly, Category = "Person") TWeakObjectPtr<APlanetBody> GroundPlanet;
	FString FrameDescription() const;

	// ---- Tunables ----
	UPROPERTY(EditAnywhere, Category = "Person|Walk") float WalkSpeedCm = 170.f;
	UPROPERTY(EditAnywhere, Category = "Person|Walk") float RunSpeedCm = 520.f;
	UPROPERTY(EditAnywhere, Category = "Person|Walk") float WalkAccelCm = 900.f;
	/** Take-off speed of a jump (cm/s): half a metre on Earth, three on a small moon. */
	UPROPERTY(EditAnywhere, Category = "Person|Walk") float JumpSpeedCm = 320.f;
	/** Steepest slope the legs will climb, degrees. */
	UPROPERTY(EditAnywhere, Category = "Person|Walk") float MaxSlopeDeg = 50.f;
	/** Thruster pack: acceleration and top speed relative to the frame (cm/s^2, cm/s). */
	UPROPERTY(EditAnywhere, Category = "Person|Pack") float JetAccelCm = 250.f;
	UPROPERTY(EditAnywhere, Category = "Person|Pack") float JetMaxSpeedCm = 2500.f;
	/** Attitude rates in free flight, deg/s, and how quickly they answer (1/s). */
	UPROPERTY(EditAnywhere, Category = "Person|Pack") float TurnRateDeg = 90.f;
	UPROPERTY(EditAnywhere, Category = "Person|Pack") float TurnResponse = 6.f;
	/** X: the pack kills relative motion at this rate (1/s). */
	UPROPERTY(EditAnywhere, Category = "Person|Pack") float BrakeRate = 3.f;
	/** Push-off speed when jumping free of a hull (cm/s). */
	UPROPERTY(EditAnywhere, Category = "Person|Boots") float PushOffSpeedCm = 150.f;
	/** How far the boots reach for a hull below the feet (cm), and how high a step they take. */
	UPROPERTY(EditAnywhere, Category = "Person|Boots") float BootReachCm = 110.f;
	UPROPERTY(EditAnywhere, Category = "Person|Boots") float StepUpCm = 50.f;
	/** Boarding reach from the airlock, cm. */
	UPROPERTY(EditAnywhere, Category = "Person") float BoardRangeCm = 1000.f;
	UPROPERTY(EditAnywhere, Category = "Person|Camera") float ThirdPersonDistanceCm = 380.f;
	UPROPERTY(EditAnywhere, Category = "Person|Camera") float ThirdPersonFOV = 90.f;
	UPROPERTY(EditAnywhere, Category = "Person|Camera") float HelmetFOV = 100.f;

	/** Half the capsule height: the root sits this far above the feet (cm). */
	static constexpr float HalfHeightCm = 90.f;
	static constexpr float EyeHeightCm = 74.f; // above the root

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components") TObjectPtr<UCapsuleComponent> Body;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components") TObjectPtr<UDynamicMeshComponent> Torso;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components") TObjectPtr<UDynamicMeshComponent> Visor;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components") TObjectPtr<UDynamicMeshComponent> LeftArm;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components") TObjectPtr<UDynamicMeshComponent> RightArm;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components") TObjectPtr<UDynamicMeshComponent> LeftLeg;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components") TObjectPtr<UDynamicMeshComponent> RightLeg;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components") TObjectPtr<USpringArmComponent> CameraArm;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components") TObjectPtr<UCameraComponent> ThirdCamera;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components") TObjectPtr<UCameraComponent> HelmetCamera;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components") TObjectPtr<URcsThrusterComponent> Jets;

private:
	void BuildFigure();
	void UpdateFigure(float DeltaTime);
	void UpdateCamera(float DeltaTime);
	void ProcessDebugCommand();
	void Message(const FString& Text, FColor Color, float Seconds = 6.f) const;

	void FindWorld();
	void CarryWithWorld(float DeltaTime);
	void TickWalking(float DeltaTime);
	void TickHull(float DeltaTime);
	void TickFloating(float DeltaTime);
	/** Integrate the pack and gravity for one tick of floating; returns the new location. */
	FVector IntegrateFloating(float DeltaTime, const FVector& Up, bool bFeetDown);
	/** Boots armed and a hull within reach: plant the feet on it. */
	bool TryStick(bool bAhead = true);
	bool TraceBoots(const FVector& Start, const FVector& End, FHitResult& Hit) const;
	/** Anything the boots channel blocks: a hull or a building. */
	bool TraceSolid(const FVector& Start, const FVector& End, FHitResult& Hit) const;
	void SetFrame(EPersonFrame NewFrame);
	void StandOn(const FVector& Feet, const FVector& Normal, bool bSnapUp);
	FVector FeetLocation() const;
	FVector CraftVelocity(const AStarshipPawn* C) const;
	double SurfaceRadiusAt(const FVector& WorldPos) const;

	FVector2D MoveInput = FVector2D::ZeroVector;
	float VerticalInput = 0.f;
	float TurnInput = 0.f;
	FVector2D LookDelta = FVector2D::ZeroVector;
	bool bSprint = false;
	bool bDebugSprint = false; // the harness's sprint, held until "sprint off"
	bool bFreeLook = false;
	bool bBraking = false;
	bool bJumpQueued = false;
	FVector2D FreeLookAngles = FVector2D::ZeroVector;
	float LookPitch = 0.f;
	float Zoom = 1.f;
	bool bFigureShown = true;
	EPersonCamera CameraMode = EPersonCamera::Third;
	FRotator TurnRates = FRotator::ZeroRotator;

	/** The person's transform in the hull craft's frame, kept between ticks while Frame == Hull. */
	FTransform HullLocal;
	float StickGrace = 0.f;
	float WalkPhase = 0.f;
	float Stride = 0.f;
	float Limber = 0.f;      // 0 standing, 1 floating (limbs loose)
	double JumpStartRadius = 0.0;
	FVector LastJetDemand = FVector::ZeroVector;
	FVector LastTorqueDemand = FVector::ZeroVector;

	TArray<TWeakObjectPtr<APlanetBody>> Planets;
	float PlanetScanTimer = 0.f;

	// Debug drives (seconds remaining).
	FVector2D DebugMove = FVector2D::ZeroVector; float DebugMoveTime = 0.f;
	float DebugVertical = 0.f; float DebugVerticalTime = 0.f;
	FVector DebugSteer = FVector::ZeroVector; float DebugSteerTime = 0.f;
	FVector2D DebugLook = FVector2D::ZeroVector; float DebugLookTime = 0.f;

	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> SuitMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> TrimMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> VisorMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> BootLightMID;
};
