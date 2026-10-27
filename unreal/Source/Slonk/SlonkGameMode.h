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

	/** Keeps the ship near the origin and shifts the universe around it (real-scale systems). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Slonk|Scale")
	TObjectPtr<class UFloatingOriginComponent> FloatingOrigin;

	/** Convenience: the floating origin of the current world's game mode, if any. */
	static UFloatingOriginComponent* FindFloatingOrigin(const UWorld* World);

	/** Park the world's single Sky Atmosphere on the nearest planet with an atmosphere (true Rayleigh/Mie, works from inside). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slonk|Atmosphere")
	bool bUseSkyAtmosphere = true;

	/** Only planets closer than this many radii get the volumetric atmosphere; further ones keep the shell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slonk|Atmosphere")
	float SkyAtmosphereRadii = 14.f;

	/** Sky / aerial-perspective brightness multiplier. The sun is only tens of lux in this scene (not 100k), so the physically scaled sky needs a boost to read. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slonk|Atmosphere")
	float SkyLuminanceBoost = 1.5f;

	/** Sky light intensity while a planet's atmosphere is active (the capture is switched off in deep space). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slonk|Atmosphere")
	float SkyLightIntensityNearPlanet = 0.5f;

	/** Volumetric clouds on the atmosphere planet, replacing its cloud shell while the view is within VolumetricCloudRadii. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slonk|Atmosphere")
	bool bUseVolumetricClouds = true;

	/** Radii from the planet centre within which the volumetric clouds show and the shell hides (1.02 is 130 km up on an Earth; the ray-marched layer streaks from higher). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slonk|Atmosphere")
	float VolumetricCloudRadii = 1.02f;

	/** Cloud layer bottom and thickness, km (Earth: cumulus from 1.5 km, tops by 6). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slonk|Atmosphere")
	float CloudLayerBottomKm = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slonk|Atmosphere")
	float CloudLayerHeightKm = 5.f;

	/** How dark the clouds' shadows fall on the ground (0 none, 1 full). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slonk|Atmosphere")
	float CloudShadowStrength = 1.f;

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
	void SetSkyCaptureActive(bool bActive);
	void ApplySkyLightIntensity(float Intensity);
	bool bSkyCaptureActive = false;
	bool bSkyCaptureInitialised = false;
	void UpdateSkyAtmosphere();

	TWeakObjectPtr<AActor> CachedStar;
	float StarSearchTimer = 0.f;

	UPROPERTY() TObjectPtr<AActor> AtmosphereHolder;
	UPROPERTY() TObjectPtr<class USkyAtmosphereComponent> SkyAtmosphere;
	UPROPERTY() TObjectPtr<class UVolumetricCloudComponent> VolumetricCloud;
	UPROPERTY() TObjectPtr<class UMaterialInstanceDynamic> CloudMID;
	TWeakObjectPtr<class APlanetBody> CloudStyledPlanet;
	UPROPERTY() TObjectPtr<class APlanetBody> AtmospherePlanet;
	void SpawnTestTargets();
};
