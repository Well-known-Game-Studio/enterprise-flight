// Slonk - camera-attached "stars streaking past" tunnel used while at warp.

#pragma once

#include "CoreMinimal.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "WarpStreaksComponent.generated.h"

/**
 * A cylinder of thin, bright streaks that rush past the camera along its local -X axis.
 * Attach it to whichever camera is active. Intensity 0 hides it; 1 is full warp.
 * Uses an instanced static mesh so hundreds of streaks cost a single draw call and need no FX assets.
 */
UCLASS(ClassGroup = (Starship), meta = (BlueprintSpawnableComponent))
class SLONK_API UWarpStreaksComponent : public UInstancedStaticMeshComponent
{
	GENERATED_BODY()

public:
	UWarpStreaksComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Streaks")
	int32 NumStreaks = 700;

	/** Streaks live inside a tube of this radius around the camera's forward axis (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Streaks")
	float TunnelRadius = 6000.f;

	/** Keep a clear core so streaks never cut straight through the lens. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Streaks")
	float TunnelInnerRadius = 500.f;

	/** Streaks are recycled when they pass this far behind the camera, and spawn this far ahead (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Streaks")
	float TunnelHalfLength = 40000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Streaks")
	float StreakLength = 2500.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Streaks")
	float StreakThickness = 14.f;

	/** Scroll speed at full intensity (cm/s, in camera space). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Streaks")
	float ScrollSpeed = 90000.f;

	/** Material for the streaks. Falls back to an engine unlit material if unset / missing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Streaks")
	TSoftObjectPtr<UMaterialInterface> StreakMaterial;

	/** 0 = hidden, 1 = full warp. */
	UFUNCTION(BlueprintCallable, Category = "Starship|Warp")
	void SetIntensity(float NewIntensity);

	UFUNCTION(BlueprintPure, Category = "Starship|Warp")
	float GetIntensity() const { return Intensity; }

private:
	void BuildInstances();
	FTransform MakeStreakTransform(const FVector& Position, float LengthScale) const;
	void RandomizeRadial(FVector& Position) const;

	float Intensity = 0.f;
	TArray<FVector> Positions;
	TArray<float> LengthJitter;
	TArray<FTransform> Transforms;
	FRandomStream Rng;
};
