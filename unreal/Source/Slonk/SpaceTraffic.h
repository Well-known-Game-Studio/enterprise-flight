// Slonk - populates the system with stations and patrolling ships at game start.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SpaceTraffic.generated.h"

class ASpaceVessel;

UCLASS()
class SLONK_API ASpaceTraffic : public AActor
{
	GENERATED_BODY()

public:
	ASpaceTraffic();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic") int32 Seed = 11;
	/** One station orbits each non-moon planet at this many radii. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic") float StationOrbitRadii = 4.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic") int32 ShipsPerStation = 2;
	/** Ships spawned around the player's start so there is traffic to look at straight away. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic") int32 ShipsNearStart = 3;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic") float StartSpreadCm = 4.0e6f;

	UPROPERTY(BlueprintReadOnly, Category = "Traffic") TArray<TObjectPtr<ASpaceVessel>> Spawned;

private:
	void Populate();
	bool bPopulated = false;
	bool bStartShips = false;
	float StartTimer = 0.5f;
};
