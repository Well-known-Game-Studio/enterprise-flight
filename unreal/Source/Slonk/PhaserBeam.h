// Slonk - a continuous phaser beam between an emitter and an impact point.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PhaserBeam.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;

UCLASS()
class SLONK_API APhaserBeam : public AActor
{
	GENERATED_BODY()

public:
	APhaserBeam();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/** Update the beam geometry. Call every frame while firing. */
	UFUNCTION(BlueprintCallable, Category = "Starship|Weapons")
	void SetBeam(const FVector& Start, const FVector& End, bool bHitSomething);

	/** Beam diameter in cm. A ~2.5 m beam reads well against a 300 m hull. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phaser")
	float BeamThickness = 250.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phaser")
	FLinearColor BeamColor = FLinearColor(1.f, 0.55f, 0.15f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Phaser")
	TSoftObjectPtr<UMaterialInterface> BeamMaterial;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Beam;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> MuzzleGlow;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> ImpactGlow;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UPointLightComponent> ImpactLight;

private:
	float Age = 0.f;
	FVector CachedStart = FVector::ZeroVector;
	FVector CachedEnd = FVector::ZeroVector;
	bool bCachedHit = false;
};
