// Slonk - photon torpedo projectile.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PhotonTorpedo.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UPointLightComponent;
class UProjectileMovementComponent;
class UNiagaraComponent;
class UNiagaraSystem;

UCLASS()
class SLONK_API APhotonTorpedo : public AActor
{
	GENERATED_BODY()

public:
	APhotonTorpedo();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/**
	 * Launch along the tube's direction. The torpedo runs straight for StraightRunSeconds, then turns toward the
	 * homing target, or failing that toward AimDirection (a heading, so it survives a floating-origin shift).
	 */
	UFUNCTION(BlueprintCallable, Category = "Starship|Weapons")
	void Launch(const FVector& Direction, double InheritedSpeed, AActor* HomingTarget, const FVector& AimDirection);

	/** Seconds the torpedo flies straight out of the tube before it starts to turn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Torpedo")
	float StraightRunSeconds = 0.6f;

	/** How fast it can turn once it is seeking (deg/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Torpedo")
	float TurnRateDegPerSecond = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Torpedo")
	double Speed = 60000.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Torpedo")
	float LifeSeconds = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Torpedo")
	float Damage = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Torpedo")
	float DamageRadius = 6000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Torpedo")
	float ExplosionRadius = 5000.f;


	/** Visual radius of the glowing casing (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Torpedo")
	float VisualRadius = 220.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Torpedo")
	FLinearColor GlowColor = FLinearColor(1.f, 0.25f, 0.05f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Torpedo")
	TSoftObjectPtr<UMaterialInterface> GlowMaterial;

	/** Optional Niagara trail; nothing is spawned if unset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Torpedo")
	TSoftObjectPtr<UNiagaraSystem> TrailSystem;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> Collision;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Casing;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Halo;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UPointLightComponent> Light;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UProjectileMovementComponent> Movement;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UNiagaraComponent> Trail;

protected:
	UFUNCTION()
	void OnHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	void Detonate(const FVector& Location);

private:
	float Age = 0.f;
	bool bDetonated = false;
	TWeakObjectPtr<AActor> SeekTarget;
	FVector SeekDirection = FVector::ZeroVector;
	bool bSeeking = false;
};
