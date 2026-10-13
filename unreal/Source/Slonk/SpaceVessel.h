// Slonk - procedurally built starships and space stations: a seed picks the proportions, the hull is generated with
// GeometryScript at BeginPlay, ships patrol around a home point, everything is a nav target and can be shot.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FarBody.h"
#include "RunningLightsComponent.h"
#include "SpaceVessel.generated.h"

class UDynamicMeshComponent;
class UDynamicMesh;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UShieldComponent;

UCLASS(Abstract)
class SLONK_API ASpaceVessel : public AActor, public IFarBody
{
	GENERATED_BODY()

public:
	ASpaceVessel();

	// IFarBody
	virtual void SetBeyondHorizon(bool bBeyond) override;
	virtual bool IsBeyondHorizon() const override { return bBeyondHorizon; }

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void PreRegisterAllComponents() override;
	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vessel")
	FString VesselName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vessel")
	int32 Seed = 0;

	/** Overall length (ships) or diameter (stations) in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vessel")
	float SizeCm = 30000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vessel")
	float MaxHealth = 400.f;

	UPROPERTY(BlueprintReadOnly, Category = "Vessel")
	float Health = 400.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vessel")
	FLinearColor HullColor = FLinearColor(0.62f, 0.64f, 0.68f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vessel")
	FLinearColor GlowColor = FLinearColor(0.35f, 0.6f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vessel|Materials") TSoftObjectPtr<UMaterialInterface> HullMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vessel|Materials") TSoftObjectPtr<UMaterialInterface> GlowMaterial;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components") TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components") TObjectPtr<UDynamicMeshComponent> Hull;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components") TObjectPtr<UDynamicMeshComponent> Glow;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components") TObjectPtr<UShieldComponent> Shield;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components") TObjectPtr<URunningLightsComponent> RunningLights;

	/** Held by a tractor beam: the AI stops moving itself. */
	UFUNCTION(BlueprintCallable, Category = "Vessel")
	void SetTractored(bool bHeld) { bTractored = bHeld; }

	UFUNCTION(BlueprintPure, Category = "Vessel")
	bool IsTractored() const { return bTractored; }

	UFUNCTION(BlueprintPure, Category = "Vessel")
	bool IsDying() const { return bDying; }

	/** Seconds of secondary explosions before the hull lets go. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vessel")
	float DeathSeconds = 1.6f;

	/** Velocity the wreck carries into its debris (set by moving subclasses). */
	FVector WreckVelocity = FVector::ZeroVector;

	UFUNCTION(BlueprintPure, Category = "Vessel")
	FString GetDisplayName() const { return VesselName.IsEmpty() ? GetActorNameOrLabel() : VesselName; }

	/** Radius of a sphere enclosing the vessel (cm). */
	UFUNCTION(BlueprintPure, Category = "Vessel")
	float GetBoundingRadiusCm() const { return SizeCm * 0.6f; }

	/** Build (or rebuild) the meshes from Seed. Game worlds only. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Vessel")
	void Rebuild();

protected:
	virtual void BuildMeshes(UDynamicMesh* HullMesh, UDynamicMesh* GlowMesh, FRandomStream& Rng) {}
	void ApplyMaterials();

	/** Filled by BuildMeshes (local positions); handed to RunningLights when the hull is done. */
	TArray<FRunningLight> PendingLights;
	TArray<FHullFloodlight> PendingFloods;

	/** Called at the end of Rebuild once the meshes and materials are in place. */
	virtual void OnMeshesBuilt() {}

	/** Where the vessel appears to be; the horizon test uses this (a warping ship's images are not at its true position). */
	virtual FVector GetVisualLocation() const { return GetActorLocation(); }

	/** Floating-origin helpers: positions kept across rebases live in universe space. */
	FVector ToUniverse(const FVector& World) const;
	FVector ToWorld(const FVector& Universe) const;

	/** Emissive strength of the glow parts (dish, bussards, nacelle grilles, windows). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vessel")
	float GlowIntensity = 150.f;

	/** Window zones for M_Hull, set by BuildMeshes (local space). Radius 0 = no saucer windows; MaxY 0 = no hull windows. */
	FVector WindowSaucerCenter = FVector::ZeroVector;
	float WindowSaucerRadius = 0.f;
	float WindowSaucerThickness = 1.f;
	float WindowHullMaxY = 0.f;
	float WindowHullMaxZ = 0.f;
	float WindowPitch = 600.f;
	float WindowDeck = 400.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vessel")
	float WindowGlow = 25.f;
	void ApplyWindowParams(UMaterialInstanceDynamic* MID) const;

	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> HullMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> GlowMID;
	bool bBuilt = false;
	bool bBeyondHorizon = false;
	bool bTractored = false;
	bool bDying = false;
	float DeathTimer = 0.f;
	float DeathFlashTimer = 0.f;
	FRandomStream DeathRng;
	void BeginDying();
	void TickDying(float DeltaTime);
	float HorizonTimer = 0.f;
};

UENUM(BlueprintType)
enum class EShipWarpPhase : uint8
{
	None,
	/** Spooling up: in the show-style view the hull stretches before it goes. */
	Spooling,
	/** Between the departure and arrival points at superluminal speed. */
	Travelling,
	/** Just dropped out: the show-style view relaxes the stretch. */
	Arriving,
	/** Back to patrolling, but light from the trip is still arriving: the relativistic view keeps drawing images. */
	Afterimages,
};

/** Saucer, neck, engineering hull, pylons and two or four nacelles. Patrols around HomeLocation and now and then warps to another station. */
UCLASS()
class SLONK_API AProceduralStarship : public ASpaceVessel
{
	GENERATED_BODY()

public:
	AProceduralStarship();
	virtual void Tick(float DeltaTime) override;
	virtual FVector GetVisualLocation() const override;

	/** Jump to warp towards a world-space destination; the ship patrols there afterwards. */
	UFUNCTION(BlueprintCallable, Category = "Ship|Warp")
	void WarpTo(const FVector& DestinationWorld);

	/** Pick another station in the system and warp there. Returns false when there is nowhere to go. */
	UFUNCTION(BlueprintCallable, Category = "Ship|Warp")
	bool WarpToRandomStation();

	UFUNCTION(BlueprintPure, Category = "Ship|Warp")
	bool IsInWarp() const { return WarpPhase == EShipWarpPhase::Spooling || WarpPhase == EShipWarpPhase::Travelling; }

	UFUNCTION(BlueprintPure, Category = "Ship|Warp")
	EShipWarpPhase GetWarpPhase() const { return WarpPhase; }

	/** Ships warp between stations on their own; off for a ship that should stay put. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Warp")
	bool bCanWarp = true;

	/** Seconds of patrol between warp trips (random in this range). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Warp")
	float WarpIntervalMin = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Warp")
	float WarpIntervalMax = 150.f;

	/** Trips are timed to take about this long whatever the distance (sets the warp factor). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Warp")
	float TargetTripSeconds = 22.f;

	/** Seconds the show-style stretch takes going in and coming out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship|Warp")
	float StretchSeconds = 0.6f;

	/** Centre of the patrol volume (world). Set by the spawner; defaults to the spawn point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship")
	FVector HomeLocation = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship")
	double PatrolRadiusCm = 3.0e7;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship")
	double CruiseSpeed = 2.0e6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship")
	double Acceleration = 4.0e5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ship")
	float TurnRate = 0.6f;

	UPROPERTY(BlueprintReadOnly, Category = "Ship")
	FVector Velocity = FVector::ZeroVector;

protected:
	virtual void BuildMeshes(UDynamicMesh* HullMesh, UDynamicMesh* GlowMesh, FRandomStream& Rng) override;
	virtual void OnMeshesBuilt() override;

private:
	void PickWaypoint();
	/** Patrol centre and current waypoint in universe space (they survive floating-origin rebases). */
	FVector HomeUniverse = FVector::ZeroVector;
	FVector WaypointUniverse = FVector::ZeroVector;
	bool bHasWaypoint = false;
	bool bHomeSet = false;
	FRandomStream NavRng;

	// ---- Warp ----
	/** One apparent image of the ship: where it is seen and how its light is shifted. */
	struct FShipImage
	{
		FVector Position = FVector::ZeroVector;
		float Doppler = 1.f;
		bool bVisible = false;
	};
	void TickWarp(float DeltaTime);
	void BeginTravel();
	void EndTravel();
	void FinishWarp();
	void UpdateWarpVisuals();
	bool IsRelativisticView() const;
	void ComputeImages(const FVector& Observer, double Now, FShipImage Out[4]) const;
	void ApplyImage(int32 Slot, const FShipImage& Image);
	void SetMainVisible(bool bVisible);
	void SetMainStretch(float Stretch, float GlowBoost);
	void ResetImages();

	UPROPERTY(VisibleAnywhere, Category = "Ship|Warp") EShipWarpPhase WarpPhase = EShipWarpPhase::None;
	UPROPERTY(VisibleAnywhere, Category = "Ship|Warp") float WarpTimer = 0.f;
	float PhaseTimer = 0.f;
	FVector WarpStartUniverse = FVector::ZeroVector;
	FVector WarpEndUniverse = FVector::ZeroVector;
	FVector WarpDirection = FVector::ForwardVector;
	double WarpSpeed = 0.0;
	double WarpDepartTime = 0.0;
	double WarpArriveTime = 0.0;
	double AfterimageDeadline = 0.0;
	bool bWarpVisualsDirty = false;

	/** Extra hull/glow copies for the relativistic view: a superluminal ship can be seen in up to three places at once. */
	UPROPERTY() TArray<TObjectPtr<UDynamicMeshComponent>> EchoHulls;
	UPROPERTY() TArray<TObjectPtr<UDynamicMeshComponent>> EchoGlows;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> EchoHullMIDs;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> EchoGlowMIDs;
};

/** Hub, ring(s), spokes and a cap; spins slowly. */
UCLASS()
class SLONK_API ASpaceStation : public ASpaceVessel
{
	GENERATED_BODY()

public:
	ASpaceStation();
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Station")
	float SpinDegPerSec = 0.4f;

protected:
	virtual void BuildMeshes(UDynamicMesh* HullMesh, UDynamicMesh* GlowMesh, FRandomStream& Rng) override;
};
