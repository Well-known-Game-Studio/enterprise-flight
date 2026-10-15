// Slonk - phasers, photon torpedoes and target lock for the starship.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "StarshipWeaponsComponent.generated.h"

class APhaserBeam;
class APhotonTorpedo;

/** A phaser bank: an emitter on the hull with the outward normal of the plating it sits on. */
USTRUCT(BlueprintType)
struct FPhaserBank
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName Name;
	/** Emitter position relative to the ship root (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector Offset = FVector::ZeroVector;
	/** Centre of the bank's field of fire: the hull normal where it is mounted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector Normal = FVector::UpVector;

	FPhaserBank() {}
	FPhaserBank(FName InName, const FVector& InOffset, const FVector& InNormal) : Name(InName), Offset(InOffset), Normal(InNormal.GetSafeNormal()) {}
};

UCLASS(ClassGroup = (Starship), meta = (BlueprintSpawnableComponent))
class SLONK_API UStarshipWeaponsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UStarshipWeaponsComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ---- Phasers ----

	/** Phaser banks round the hull. The ones with the squarest view of the aim point fire. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phasers")
	TArray<FPhaserBank> PhaserBanks;

	/** How many banks fire at once. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phasers")
	int32 MaxFiringBanks = 2;

	/** A bank's field of fire: the largest angle (deg) between its hull normal and the line to the aim point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phasers")
	float PhaserBankArc = 88.f;

	/** Indices into PhaserBanks of the banks that fired this tick. */
	UPROPERTY(BlueprintReadOnly, Category = "Phasers")
	TArray<int32> FiringBanks;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phasers")
	TSubclassOf<APhaserBeam> PhaserBeamClass;

	/** Max phaser range (cm). 30 km. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phasers")
	double PhaserRange = 3000000.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phasers")
	float PhaserDamagePerSecond = 30.f;

	/** Seconds of continuous fire from full charge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phasers")
	float PhaserFireSeconds = 6.f;

	/** Seconds to recharge from empty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phasers")
	float PhaserRechargeSeconds = 8.f;

	// ---- Torpedoes ----

	/** Torpedo tube positions relative to the ship root (cm). Tubes fire alternately. The refit has one launcher, with two tubes side by side, at the top of the neck facing forward. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Torpedoes")
	TArray<FVector> TorpedoTubeOffsets;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Torpedoes")
	TSubclassOf<APhotonTorpedo> TorpedoClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Torpedoes")
	float TorpedoCooldown = 0.9f;

	// ---- Targeting ----

	/** Targets are locked within this range (cm). 150 km. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targeting")
	double TargetScanRange = 15000000.0;

	UPROPERTY(BlueprintReadOnly, Category = "Targeting")
	TObjectPtr<AActor> LockedTarget;

	// ---- API ----

	UFUNCTION(BlueprintCallable, Category = "Starship|Weapons")
	void StartPhasers();

	UFUNCTION(BlueprintCallable, Category = "Starship|Weapons")
	void StopPhasers();

	UFUNCTION(BlueprintCallable, Category = "Starship|Weapons")
	bool FireTorpedo();

	/** Lock the next closest target in front of the ship (sorted by angle, then distance). */
	UFUNCTION(BlueprintCallable, Category = "Starship|Weapons")
	void CycleTarget();

	UFUNCTION(BlueprintCallable, Category = "Starship|Weapons")
	void ClearTarget();

	/** Lock the closest targetable actor whatever its bearing (the default cycle prefers what is ahead). */
	UFUNCTION(BlueprintCallable, Category = "Starship|Weapons")
	void LockNearest();

	UFUNCTION(BlueprintPure, Category = "Starship|Weapons")
	bool IsFiringPhasers() const { return bPhasersFiring && PhaserEnergy > 0.f; }

	UFUNCTION(BlueprintPure, Category = "Starship|Weapons")
	float GetPhaserEnergy() const { return PhaserEnergy; }

	/** 1 when a torpedo can be fired, counting up from 0 after each shot. */
	UFUNCTION(BlueprintPure, Category = "Starship|Weapons")
	float GetTorpedoReadiness() const { return TorpedoCooldown > 0.f ? FMath::Clamp(1.f - TorpedoTimer / TorpedoCooldown, 0.f, 1.f) : 1.f; }

	/** World-space point the weapons are aiming at (locked target, or the view direction out to range). */
	UFUNCTION(BlueprintPure, Category = "Starship|Weapons")
	FVector GetAimPoint() const;

private:
	void UpdatePhasers(float DeltaTime);
	void DestroyBeams();
	bool GetViewRay(FVector& OutOrigin, FVector& OutDirection) const;

	UPROPERTY()
	TArray<TObjectPtr<APhaserBeam>> ActiveBeams;

	bool bPhasersFiring = false;
	float PhaserEnergy = 1.f;
	float TorpedoTimer = 0.f;
	int32 NextTube = 0;
};
