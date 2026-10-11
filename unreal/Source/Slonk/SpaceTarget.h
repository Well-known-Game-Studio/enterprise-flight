// Slonk - a destructible asteroid / target drone. Anything with the "Targetable" tag can be locked.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FarBody.h"
#include "SpaceTarget.generated.h"

class UStaticMeshComponent;

UCLASS()
class SLONK_API ASpaceTarget : public AActor, public IFarBody
{
	GENERATED_BODY()

public:
	// IFarBody
	virtual void SetBeyondHorizon(bool bBeyond) override;
	virtual bool IsBeyondHorizon() const override { return bBeyondHorizon; }

	ASpaceTarget();

	static const FName TargetableTag;

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void PreRegisterAllComponents() override;
	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	/** Hook for PCG "Spawn Actor" post-spawn functions: re-applies the radius after property overrides were copied in. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Target")
	void PCGPostSpawn();

	/** Pick a random size / tumble rate. Call before BeginPlay (e.g. right after spawning) or any time after. */
	void RandomizeAppearance(FRandomStream& Rng);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target")
	float MaxHealth = 100.f;

	UPROPERTY(BlueprintReadOnly, Category = "Target")
	float Health = 100.f;

	/** Radius of the body in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target")
	float Radius = 5000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target")
	FRotator TumbleRate = FRotator(2.f, 5.f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Target")
	TSoftObjectPtr<UMaterialInterface> BodyMaterial;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Body;

	UFUNCTION(BlueprintPure, Category = "Target")
	float GetHealthFraction() const { return MaxHealth > 0.f ? Health / MaxHealth : 0.f; }

private:
	void ApplyRadius();
	int32 SettleTicks = 3;
	float HorizonTimer = 0.f;
	bool bBeyondHorizon = false;
};
