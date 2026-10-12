// Slonk - a tumbling hull fragment thrown off a destroyed vessel.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HullDebris.generated.h"

class UStaticMeshComponent;

UCLASS()
class SLONK_API AHullDebris : public AActor
{
	GENERATED_BODY()

public:
	AHullDebris();
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Mesh;

	FVector Velocity = FVector::ZeroVector;
	FRotator Tumble = FRotator::ZeroRotator;
	float LifeSeconds = 30.f;

	/** Throw a cloud of fragments from a dying vessel. */
	static void Scatter(UWorld* World, const FVector& Origin, const FVector& BaseVelocity, float SizeCm, UMaterialInterface* Material, const FLinearColor& Color, int32 Count, int32 Seed);

private:
	float Age = 0.f;
};
