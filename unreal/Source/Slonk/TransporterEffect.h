// Slonk - the transporter's shimmer, after the Enterprise-A's in The Undiscovered Country: the figure itself fills
// with blue sparkle, sampled from the body's own triangles, inside a column of rising streaks, and dissolves part by
// part; at the far end it forms again out of the same shimmer. Asset-free: engine spheres on the running lights'
// additive bulb material (round dots on the body, stretched streaks in the column), a blue light, the suit swapped
// for a blue glow while the shimmer holds it. The person is not a skeletal mesh but six dynamic-mesh parts, so the
// sampling here does what a Niagara mesh emitter would, reading the parts' triangles every tick.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TransporterEffect.generated.h"

class APersonPawn;
class UDynamicMeshComponent;
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
	 * The shimmer on `Body`, dematerialising or materialising over `Seconds`, following the body wherever it is
	 * carried. `OnEnvelope` is called each tick with the shimmer's strength 0..1 (the pad's glow follows it) and
	 * once with 0 at the end.
	 */
	static ATransporterEffect* Spawn(UWorld* World, APersonPawn* Body, bool bMaterialise, float Seconds, TFunction<void(float)> OnEnvelope = nullptr);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transporter") float Duration = 1.7f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transporter") bool bMaterialise = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transporter") FLinearColor Color = FLinearColor(0.28f, 0.56f, 1.0f);
	/** Sparkles on the body's surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transporter") int32 BodySparkles = 320;
	/** Streaks rising in the column round the body. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transporter") int32 Streaks = 110;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transporter") float RadiusCm = 40.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transporter") float HeightCm = 200.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transporter") float LightCandela = 140.f;
	/** The bulb material's Intensity at full shimmer: kept low enough that the blue stays blue instead of burning to white. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transporter") float Emissive = 60.f;
	/** Sparkles reach full size this far from the viewer's eye and shrink with the square of anything closer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transporter") float EyeClearanceCm = 130.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transporter") TSoftObjectPtr<UMaterialInterface> SparkleMaterial;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components") TObjectPtr<UInstancedStaticMeshComponent> Sparkles;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components") TObjectPtr<UPointLightComponent> Light;

	TFunction<void(float)> OnEnvelope;

private:
	/** A body sparkle: a point on one part's surface, drifting out along a direction, or a streak in the column. */
	struct FSparkle { int32 Part; FVector Local; FVector Drift; float Angle, Radius, Height, Rise, Phase, Size, Length; };
	TArray<FSparkle> Seeds;
	TWeakObjectPtr<APersonPawn> Body;
	TArray<TWeakObjectPtr<UDynamicMeshComponent>> Parts;
	float Age = 0.f;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MID;
	float Envelope() const;
	float Dissolve() const;
	void SampleBody(FRandomStream& R);
	void Layout();
	FTransform Feet() const;
};
