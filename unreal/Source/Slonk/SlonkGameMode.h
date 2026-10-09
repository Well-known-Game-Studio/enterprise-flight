// Slonk - game mode for the starship sandbox.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "SlonkGameMode.generated.h"

/**
 * Game mode for flying the starship around. Sets the starship pawn / controller / HUD as defaults,
 * disables world-bounds kills (space is big), and optionally spawns a shell of target asteroids
 * so there is something to shoot at while testing.
 */
UCLASS()
class SLONK_API ASlonkGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ASlonkGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/**
	 * Keep the level's directional light pointing from the nearest star (actor tagged "Star") through the player.
	 * A single directional light cannot be right everywhere in a system, but it is right where the player is.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slonk|Lighting")
	bool bTrackStarLighting = true;

	/** Spawn a ring of target asteroids around the origin at BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slonk|Testing")
	bool bSpawnTestTargets = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slonk|Testing", meta = (EditCondition = "bSpawnTestTargets"))
	int32 NumTestTargets = 32;

	/** Minimum distance from the origin for test targets (cm). 20 km by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slonk|Testing", meta = (EditCondition = "bSpawnTestTargets"))
	double TestTargetMinDistance = 2000000.0;

	/** Maximum distance from the origin for test targets (cm). 80 km by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slonk|Testing", meta = (EditCondition = "bSpawnTestTargets"))
	double TestTargetMaxDistance = 8000000.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slonk|Testing", meta = (EditCondition = "bSpawnTestTargets"))
	int32 TestTargetSeed = 1701;

private:
	void DisableWorldBoundsChecks();
	void UpdateStarLighting();
	void SpawnTestTargets();
};
