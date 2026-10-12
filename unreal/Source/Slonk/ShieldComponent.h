// Slonk - deflector shield bubble: invisible until something hits it, then a honeycomb flare spreads from the impact.

#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "ShieldComponent.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;

UCLASS(ClassGroup = (Starship), meta = (BlueprintSpawnableComponent))
class SLONK_API UShieldComponent : public UStaticMeshComponent
{
	GENERATED_BODY()

public:
	UShieldComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Seconds an impact flare stays visible. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield")
	float ImpactDuration = 1.4f;

	/** Flare radius as a fraction of the shell's mesh radius (50 cm mesh units). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield")
	float ImpactRadiusFraction = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield")
	FLinearColor ShieldColor = FLinearColor(0.35f, 0.65f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield")
	float Intensity = 45.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shield")
	TSoftObjectPtr<UMaterialInterface> ShieldMaterial;

	/** Flare the shield at a world point (projected onto the shell). Strength scales the brightness. */
	UFUNCTION(BlueprintCallable, Category = "Shield")
	void RegisterImpact(const FVector& WorldPoint, float Strength = 1.f);

	/** Size the ellipsoid shell: half extents in cm along the owner's local axes. */
	UFUNCTION(BlueprintCallable, Category = "Shield")
	void SetShellExtent(const FVector& HalfExtentsCm);

	UFUNCTION(BlueprintPure, Category = "Shield")
	bool IsFlaring() const;

private:
	void EnsureMaterial();

	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MID;
	int32 NextSlot = 0;
	float LastImpactTime = -1000.f;
};
