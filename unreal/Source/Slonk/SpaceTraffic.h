// Slonk - populates the system with stations and patrolling ships at game start.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SpaceVessel.h"
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
	/** The home system's deep-space stations (K-7, Regula, Epsilon IX, Vanguard): out on their own beyond
	 *  MinDeepSpaceAu, beside a nebula where one lies that far out. Called once the nebulae exist. */
	void SpawnDeepSpaceStations();
	/** One ship by name at Loc, holding station there (no warp, no patrol): a mission's freighter, derelict or
	 *  stranger. A civilian hull is a Miranda in merchant colours. Returns it, or null. */
	/** A hull holding station for an encounter: a freighter, a Starfleet hull, or (bAlien) a stranger's: no class
	 *  of ours in her lines' colours, larger, lit in a colour nobody uses. */
	ASpaceVessel* SpawnNamedShip(const FString& Name, const FVector& Loc, bool bCivilian, bool bAlien = false);
	/** A station of this style at Loc with Ships patrol hulls about it (the frontier's outposts and starbases). */
	class ASpaceStation* SpawnStation(const FString& Name, EStationStyle Style, const FVector& Loc, int32 Ships, int32 SeedValue);
	/** Patrol hulls about a place (a starbase commissioned after its station was spawned). */
	void SpawnPatrols(const FVector& Loc, double SizeCm, int32 Ships, int32 SeedValue);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic") int32 Seed = 11;
	/** One station orbits each non-moon planet at this many radii. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic") float StationOrbitRadii = 4.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic") int32 ShipsPerStation = 2;
	/** Deep-space stations keep at least this far from their star. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic") float MinDeepSpaceAu = 7.f;
	/** Ships spawned around the player's start so there is traffic to look at straight away. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic") int32 ShipsNearStart = 3;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic") float StartSpreadCm = 4.0e6f;
	/** Distance (cm) from the player start to the Borg cube; 0 spawns none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic") float BorgDistanceCm = 6.0e7f;

	UPROPERTY(BlueprintReadOnly, Category = "Traffic") TArray<TObjectPtr<ASpaceVessel>> Spawned;

private:
	void Populate();
	void PopulateWith(const TArray<APlanetBody*>& Planets, FRandomStream& Rng, bool bNameStationsByPlanet);
	/** A station of the style StyleName at Loc with its patrol ships. */
	class ASpaceStation* SpawnStationAt(const FString& Name, const FString& StyleName, const FVector& Loc, FRandomStream& Rng);
	void SpawnPatrolShip(FRandomStream& Rng, const FVector& Loc, const FVector& Home, double Patrol);
	bool bDeepSpaceSpawned = false;
	int32 ShipIndex = 0;
	/** The next name from the table that no spawned ship carries (the fleet keys its records by name). */
	FString NextShipName();
	int32 StationIndex = 0;
	/** Ships and the Borg cube near the player's start; re-queues itself until the player pawn exists. */
	void SpawnStartShips();
	bool bPopulated = false;
	bool bStartShips = false;
	float StartTimer = 0.5f;
};
