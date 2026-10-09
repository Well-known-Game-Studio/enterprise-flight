// Slonk - a planet / moon body. Spawned by hand or by the PCG solar-system graph.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PlanetBody.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;

UCLASS()
class SLONK_API APlanetBody : public AActor
{
	GENERATED_BODY()

public:
	APlanetBody();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Body;

	/** Radius in km. Drives the body's world scale directly (PCG feeds this through a property override). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	float RadiusKm = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	FLinearColor Color = FLinearColor(0.25f, 0.42f, 0.65f);

	/** Rotation about the local Z axis in degrees per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	float SpinDegPerSec = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	FString PlanetName;

	/** Pick Color (and spin) deterministically from the spawn location, so PCG-spawned planets vary without attribute plumbing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	bool bRandomizeFromLocation = true;

	/** Material with a "Color" vector parameter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	TSoftObjectPtr<UMaterialInterface> PlanetMaterial;

	UFUNCTION(BlueprintPure, Category = "Planet")
	double GetRadiusCm() const;

	/** Hook for PCG "Spawn Actor" post-spawn functions: re-applies size/colour after property overrides were copied in. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Planet")
	void PCGPostSpawn();

private:
	void ApplyAppearance();

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> BodyMaterial;

	/** Spawners (PCG) may overwrite properties right after spawn; re-apply for the first few ticks. */
	int32 SettleTicks = 3;
	float MeshUnitRadius = 50.f;
};
