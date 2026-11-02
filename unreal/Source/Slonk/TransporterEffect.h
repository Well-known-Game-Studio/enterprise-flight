// Slonk - the transporter's shimmer, after the Enterprise-A's in The Undiscovered Country: the figure itself fills
// with blue sparkle inside a column of rising streaks and dissolves part by part; at the far end it forms again out
// of the same shimmer. The sparkle is a Niagara system (NS_Transporter, authored through the editor's Niagara
// toolset) riding a hidden static-mesh copy of the person's six dynamic-mesh parts, so its mesh emitter spawns on
// the body's own triangles; both emitters run in local space because the ship and the ground move metres a frame.
// Without the asset the engine-sphere sparkles below stand in, sampled from the same triangles on the CPU. A blue
// light and the suit swapped for a blue glow go with either.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TransporterEffect.generated.h"

class APersonPawn;
class UDynamicMeshComponent;
class UNiagaraSystem;
class UNiagaraComponent;
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
	/**
	 * The Niagara system (authored through the editor's Niagara toolset): a static-mesh emitter on the figure's own
	 * mesh and a cylinder of streaks, driven by User.Envelope, User.Direction and User.Color. When it is missing the
	 * sparkles below stand in, sampled from the same triangles.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transporter") TSoftObjectPtr<UNiagaraSystem> System;
	/** The Niagara sprites are additive: under the locked exposure (sunlit white is ~40) the colour goes in this many times over. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Transporter") float NiagaraBrightness = 80.f;
	UPROPERTY() TObjectPtr<UNiagaraComponent> Niagara;

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
