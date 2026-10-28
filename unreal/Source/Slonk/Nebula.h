// Slonk - a nebula: a sphere of ray-marched gas the ship can fly into, lit by the star and glowing on its own.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FarBody.h"
#include "Nebula.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;

UCLASS()
class SLONK_API ANebula : public AActor, public IFarBody
{
	GENERATED_BODY()

public:
	ANebula();

	/** The sphere the gas is marched inside; two-sided so the march still runs with the camera in the cloud. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Volume;

	/** The cloud's glow as a light: fills the night sides of moons and ships in and near the cloud. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UPointLightComponent> Glow;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nebula")
	FString NebulaName;

	/** Radius of the cloud, km. A light-second across is 150,000 km: four seconds at full impulse. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nebula")
	float RadiusKm = 150000.f;

	/** Two gas colours the density noise blends between, and the glow of the gas itself. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nebula") FLinearColor ColorA = FLinearColor(0.95f, 0.35f, 0.55f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nebula") FLinearColor ColorB = FLinearColor(0.3f, 0.45f, 1.0f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nebula") FLinearColor EmissiveColor = FLinearColor(1.0f, 0.55f, 0.8f);
	/** Optical thickness of the cloud through its middle (1 is a few metres of visibility at the core, 0.2 a haze). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nebula") float Density = 0.6f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nebula") float Emission = 0.5f;
	/** Puts the gas on the scene's scale: the sun is tens of lux against a locked exposure, so a lit white surface sits near 40. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nebula") float Brightness = 20.f;
	/** Strength of the glow light at the cloud's centre (falls off linearly to twice the radius). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nebula") float GlowLight = 25.f;
	/** 0..1 how much the gas is drawn out into filaments and shells rather than lumps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nebula") float Filaments = 0.7f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nebula") float Seed = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Nebula|Materials") TSoftObjectPtr<UMaterialInterface> NebulaMaterial;

	double GetRadiusCm() const { return (double)RadiusKm * 1.0e5; }
	FString GetDisplayName() const { return NebulaName.IsEmpty() ? GetActorNameOrLabel() : NebulaName; }

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Nebula")
	void ApplyAppearance();

	/** Beyond the render horizon the volume is switched off; the ship's distant-stars component draws a glow
	 *  sprite in its place, so the cloud is in view at warp long before the volume can be. */
	virtual void SetBeyondHorizon(bool bBeyond) override;
	virtual bool IsBeyondHorizon() const override { return bBeyondHorizon; }

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

private:
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MID;
	bool bBeyondHorizon = false;
	float StarTimer = 0.f;
	void UpdateStar();
};
