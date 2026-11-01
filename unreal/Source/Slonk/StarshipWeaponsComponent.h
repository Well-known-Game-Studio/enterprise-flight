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
	/** Emitters of a multi-emitter bank (ship space, cm): each fires its own beam at the aim point, and the bank's
	 *  damage is shared between them. Empty means a single emitter at Offset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FVector> Emitters;

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

	/** Phaser banks round the hull. The ones with the squarest view of the aim point fire, every emitter of each. */
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

	/** Reload time of one tube (s). With two tubes a pair can go out back to back. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Torpedoes")
	float TorpedoCooldown = 2.5f;

	/** Casings in the magazine; the ship systems keep it and a starbase refills it. Ignored when unlimited. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Torpedoes")
	int32 TorpedoesCarried = 96;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Torpedoes")
	bool bUnlimitedTorpedoes = true;

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

	/** Readiness of the readiest tube: 1 when a torpedo can be fired, counting up from 0 as it reloads. */
	UFUNCTION(BlueprintPure, Category = "Starship|Weapons")
	float GetTorpedoReadiness() const;

	/** Readiness of one tube (0..1). Each tube reloads on its own, so both can fire back to back. */
	UFUNCTION(BlueprintPure, Category = "Starship|Weapons")
	float GetTubeReadiness(int32 Tube) const;

	UFUNCTION(BlueprintPure, Category = "Starship|Weapons")
	int32 GetNumTubes() const { return TorpedoTubeOffsets.Num(); }

	/** Count of torpedoes fired, for launch effects. */
	UFUNCTION(BlueprintPure, Category = "Starship|Weapons")
	int32 GetTorpedoesFired() const { return TorpedoesFired; }

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
	/** Seconds of reload left per tube. */
	TArray<float> TubeTimers;
	int32 NextTube = 0;
	int32 TorpedoesFired = 0;
};
