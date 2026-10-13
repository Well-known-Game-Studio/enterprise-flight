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

/** Saucer, neck, engineering hull, pylons and two or four nacelles. Patrols around HomeLocation. */
UCLASS()
class SLONK_API AProceduralStarship : public ASpaceVessel
{
	GENERATED_BODY()

public:
	AProceduralStarship();
	virtual void Tick(float DeltaTime) override;

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

private:
	void PickWaypoint();
	FVector Waypoint = FVector::ZeroVector;
	bool bHasWaypoint = false;
	bool bHomeSet = false;
	FRandomStream NavRng;
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
