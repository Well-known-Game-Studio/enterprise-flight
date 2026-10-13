// Slonk - the show-style streak of light a ship leaves when it jumps to warp (or arrives from it):
// a bright additive cylinder along the path that thins and fades out over a second.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WarpFlash.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;

UCLASS()
class SLONK_API AWarpStreakFlash : public AActor
{
	GENERATED_BODY()

public:
	AWarpStreakFlash();
	virtual void Tick(float DeltaTime) override;

	/**
	 * Spawn a streak. Origin is the end of the streak at the ship (departure point, or arrival point);
	 * Direction is the direction of travel; for an arrival the streak trails behind the origin.
	 */
	static AWarpStreakFlash* Spawn(UWorld* World, const FVector& Origin, const FVector& Direction, float ShipLengthCm, bool bArrival, const FLinearColor& Color);

	UPROPERTY(EditAnywhere, Category = "Warp Flash") float Lifetime = 1.1f;

private:
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Streak;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MID;
	float Age = 0.f;
	float Length = 1.f;
	float Radius = 1.f;
	FLinearColor Tint = FLinearColor::White;
};
