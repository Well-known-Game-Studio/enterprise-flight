// Slonk - populates the system with stations and patrolling ships at game start.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SpaceTraffic.generated.h"

class ASpaceVessel;
class APlanetBody;

UCLASS()
class SLONK_API ASpaceTraffic : public AActor
{
	GENERATED_BODY()

public:
	ASpaceTraffic();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/** A station and its patrol ships at each of these planets (moons skipped), from its own seed: used by the
	 *  star-system generator for systems made after the level's own planets were populated. */
	void PopulatePlanets(const TArray<TObjectPtr<APlanetBody>>& Planets, int32 SeedValue);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic") int32 Seed = 11;
	/** One station orbits each non-moon planet at this many radii. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic") float StationOrbitRadii = 4.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic") int32 ShipsPerStation = 2;
	/** Ships spawned around the player's start so there is traffic to look at straight away. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic") int32 ShipsNearStart = 3;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic") float StartSpreadCm = 4.0e6f;
	/** Distance (cm) from the player start to the Borg cube; 0 spawns none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic") float BorgDistanceCm = 6.0e7f;

	UPROPERTY(BlueprintReadOnly, Category = "Traffic") TArray<TObjectPtr<ASpaceVessel>> Spawned;

private:
	void Populate();
	void PopulateWith(const TArray<APlanetBody*>& Planets, FRandomStream& Rng, bool bNameStationsByPlanet);
	int32 ShipIndex = 0;
	int32 StationIndex = 0;
	/** Ships and the Borg cube near the player's start; re-queues itself until the player pawn exists. */
	void SpawnStartShips();
	bool bPopulated = false;
	bool bStartShips = false;
	float StartTimer = 0.5f;
};
