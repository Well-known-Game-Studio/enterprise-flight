// Slonk - asset-free explosion: an expanding emissive shell plus a light flash.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ExplosionEffect.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;

UCLASS()
class SLONK_API AExplosionEffect : public AActor
{
	GENERATED_BODY()

public:
	AExplosionEffect();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/** Peak shell radius in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosion")
	float MaxRadius = 4000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosion")
	float Duration = 0.9f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosion")
	float LightIntensity = 2.0e6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosion")
	FLinearColor Color = FLinearColor(1.f, 0.45f, 0.1f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Explosion")
	TSoftObjectPtr<UMaterialInterface> ShellMaterial;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Shell;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Core;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UPointLightComponent> Light;

	/** Spawn helper. */
	static AExplosionEffect* SpawnExplosion(UWorld* World, const FVector& Location, float Radius, float InDuration = 0.9f);

private:
	float Age = 0.f;
};
