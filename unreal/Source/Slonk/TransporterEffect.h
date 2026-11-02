// Slonk - the transporter's shimmer: a column of swirling sparkles over the feet of whoever is beaming, gathering
// and brightening as they dematerialise, thinning as they stand again at the other end. Asset-free: engine spheres
// on the lit material, a point light, and the player's camera fading to the same pale blue-white.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TransporterEffect.generated.h"

class UInstancedStaticMeshComponent;
class UPointLightComponent;
class UMaterialInstanceDynamic;

UCLASS()
class SLONK_API ATransporterEffect : public AActor
{
	GENERATED_BODY()

public:
	ATransporterEffect();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/**
	 * A column over `Feet` (its up axis is the column's), dematerialising or materialising over `Seconds`, riding
	 * `AttachTo` (the ship's root on the pad, the world's on the ground) so it stays with what carries the person.
	 * `OnEnvelope` is called each tick with the shimmer's strength 0..1 (the pad's glow follows it) and once with 0
	 * at the end.
	 */
	static ATransporterEffect* Spawn(UWorld* World, const FTransform& Feet, bool bMaterialise, float Seconds, USceneComponent* AttachTo, TFunction<void(float)> OnEnvelope = nullptr);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transporter") float Duration = 1.7f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transporter") bool bMaterialise = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transporter") FLinearColor Color = FLinearColor(0.75f, 0.88f, 1.0f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transporter") int32 SparkleCount = 170;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transporter") float RadiusCm = 46.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transporter") float HeightCm = 205.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transporter") float LightCandela = 180.f;
	/** The running-light material's Intensity at full shimmer (an additive bulb; the ship's bulbs run at 120). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transporter") float Emissive = 260.f;
	/** Sparkles reach full size this far from the viewer's eye and shrink with the square of anything closer, so the
	 *  person inside the column sees drifting points rather than the nearest few bulbs filling the helmet. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transporter") float EyeClearanceCm = 130.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transporter") TSoftObjectPtr<UMaterialInterface> SparkleMaterial;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components") TObjectPtr<UInstancedStaticMeshComponent> Sparkles;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components") TObjectPtr<UPointLightComponent> Light;

	TFunction<void(float)> OnEnvelope;

private:
	struct FSparkle { float Angle, Radius, Height, Rise, Phase, Size; };
	TArray<FSparkle> Seeds;
	float Age = 0.f;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MID;
	float Envelope() const;
	void Layout();
};
