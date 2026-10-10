// Slonk - a star: granulated emissive sphere plus a corona shell. Tags itself "Star" so planets and the
// game mode light from it.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "StarBody.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;

UCLASS()
class SLONK_API AStarBody : public AActor
{
	GENERATED_BODY()

public:
	AStarBody();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
#if WITH_EDITOR
	virtual void PostEditMove(bool bFinished) override;
#endif
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Body;

	/** Camera-facing glow / ray sprite. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Corona;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star")
	float RadiusKm = 3000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star")
	FString StarName = TEXT("Sol");

	/** Photosphere colour (also tints the system's sunlight). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star")
	FLinearColor StarColor = FLinearColor(1.0f, 0.86f, 0.62f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star")
	FLinearColor CoronaColor = FLinearColor(1.0f, 0.72f, 0.4f);

	/** Emissive multiplier of the photosphere. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star")
	float Intensity = 80.f;

	/** Minimum angular size of the glow sprite (fraction of the camera distance) so the star stays visible from across the system. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star")
	float GlowMinAngularSize = 0.035f;

	/** Corona shell extent as a fraction of the radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star", meta = (ClampMin = "0.1", ClampMax = "4"))
	float CoronaDepth = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star")
	float CoronaIntensity = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star")
	float CoronaFalloff = 3.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star")
	float LimbDarkening = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star")
	float GranuleScale = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star")
	float SpinDegPerSec = 0.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star|Materials") TSoftObjectPtr<UMaterialInterface> StarMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star|Materials") TSoftObjectPtr<UMaterialInterface> CoronaMaterial;

	UFUNCTION(BlueprintPure, Category = "Star")
	double GetRadiusCm() const { return FMath::Max(RadiusKm, 1.f) * 100000.0; }

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Star")
	void ApplyAppearance();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Star")
	void PCGPostSpawn() { ApplyAppearance(); }

private:
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> StarMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> CoronaMID;
	int32 SettleTicks = 3;
	float MeshUnitRadius = 50.f;
};
