// Slonk - floating origin. Keeps the player's ship near the world origin and shifts everything else,
// so a real-scale solar system (tens of AU) stays inside the precision the engine is happy with.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FloatingOriginComponent.generated.h"

UCLASS(ClassGroup = (Starship), meta = (BlueprintSpawnableComponent))
class SLONK_API UFloatingOriginComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFloatingOriginComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Rebase once the focus actor is farther than this from the world origin (cm). 1000 km by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floating Origin")
	double RebaseDistance = 1.0e8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Floating Origin")
	bool bEnabled = true;

	/** Universe-space position of the current world origin. Universe = World + this. */
	UFUNCTION(BlueprintPure, Category = "Floating Origin")
	FVector GetOriginUniversePosition() const { return OriginUniversePosition; }

	UFUNCTION(BlueprintPure, Category = "Floating Origin")
	FVector WorldToUniverse(const FVector& World) const { return World + OriginUniversePosition; }

	UFUNCTION(BlueprintPure, Category = "Floating Origin")
	FVector UniverseToWorld(const FVector& Universe) const { return Universe - OriginUniversePosition; }

	/** Shift the whole world by Offset (every top-level movable actor except the focus). */
	UFUNCTION(BlueprintCallable, Category = "Floating Origin")
	void ShiftWorld(const FVector& Offset);

	UFUNCTION(BlueprintPure, Category = "Floating Origin")
	int32 GetRebaseCount() const { return RebaseCount; }

private:
	AActor* GetFocusActor() const;

	FVector OriginUniversePosition = FVector::ZeroVector;
	int32 RebaseCount = 0;
};
