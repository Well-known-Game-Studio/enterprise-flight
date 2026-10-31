// Slonk - game mode for the starship sandbox.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "StarSystem.h"
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
	/** Sunlight falls off with distance from the star. The level's light intensity is taken as the sunlight of a
	 *  Sun-sized star at SunlightReferenceAU; from there it falls as (reference / distance)^SunlightFalloffExponent
	 *  (scaled by the star's radius squared; 2 is the physical inverse square, 1 keeps the outer giants well lit)
	 *  but never drops below SunlightFloorFraction while within SunlightFloorToAU, and the floor fades to black by
	 *  SunlightDarkAU, so the outer planets stay readable and the space between stars is dark. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slonk|Lighting")
	float SunlightReferenceAU = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slonk|Lighting")
	float SunlightFalloffExponent = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slonk|Lighting")
	float SunlightFloorFraction = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slonk|Lighting")
	float SunlightFloorToAU = 100.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slonk|Lighting")
	float SunlightDarkAU = 1000.f;
	float BaseSunlightLux = -1.f;

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

	/** Volumetric clouds on the atmosphere planet, replacing its cloud shell while the view is within VolumetricCloudRangeKm of the surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slonk|Atmosphere")
	bool bUseVolumetricClouds = true;

	/** Altitude (km) up to which the ray-marched layer is at full density; beyond it the layer thins as the cloud
	 *  shell comes in over the next VolumetricCloudFadeKm. In km, not radii: the renderer stops tracing the layer
	 *  from about 2,000 km out whatever the world's size, and the look holds to 1,500. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slonk|Atmosphere")
	float VolumetricCloudRangeKm = 900.f;

	/** Whether the sun's shadow maps shade the cloud layer (the terrain's and the ship's shadows on the clouds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slonk|Atmosphere")
	bool bShadowsOnClouds = true;

	/** Width of the hand-over band above VolumetricCloudRangeKm, km. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slonk|Atmosphere")
	float VolumetricCloudFadeKm = 600.f;

	/** Cloud layer bottom and thickness, km (Earth: cumulus from 1.5 km, tops by 6). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slonk|Atmosphere")
	float CloudLayerBottomKm = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slonk|Atmosphere")
	float CloudLayerHeightKm = 5.f;

	/** Debugging: cloud material parameters forced every tick, "Name=1.5;Other=0,1,0,0" (one value a scalar, four a vector). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slonk|Clouds")
	FString CloudParamOverrides;

	/** How dark the clouds' shadows fall on the ground (0 none, 1 full). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slonk|Atmosphere")
	float CloudShadowStrength = 1.f;

	/** The sun light's dynamic shadow distance is raised to at least this (cm). The level's light shipped with the
	 *  200 m default, and the virtual shadow maps drop any caster further than that from the camera, which is
	 *  where a chase camera keeps a 300 m starship: no ship shadow ever fell on a planet. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slonk|Lighting")
	float SunShadowDistanceCm = 5.0e6f;

	/** Place an icy moon with lineae and a hazy moon around every ringed giant on the first tick, named after it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slonk|Testing")
	bool bSpawnShowcaseMoons = true;
	bool bShowcaseMoonsSpawned = false;
	void SpawnShowcaseMoons();

	/** Neighbouring star systems generated at play start, each at its real-sky bearing and distance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slonk|Systems")
	bool bSpawnStarSystems = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slonk|Systems")
	FString HomeSystemName = TEXT("Sol");
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slonk|Systems")
	TArray<FStarSystemSpec> StarSystems;
	void SpawnStarSystems();
	/** A Jovian gas giant for the home system (the level's own giant is the ice giant Caerulea), at this orbit and bearing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slonk|Systems") bool bSpawnHomeGiant = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slonk|Systems") float HomeGiantOrbitAu = 9.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slonk|Systems") float HomeGiantBearingDeg = -60.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slonk|Systems") float HomeGiantRadiusKm = 69000.f;
	void SpawnHomeGiant(class AStarSystem* Home);
	bool bStarSystemsSpawned = false;
	float AdoptTimer = 0.f;

	/** Place a nebula out beyond each ringed giant on the first tick, named after it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slonk|Testing")
	bool bSpawnNebulae = true;
	void SpawnNebulae();

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
	float CloudViewSamples = 0.6f;
	TWeakObjectPtr<class UTexture2D> CloudWeatherSet;
	UPROPERTY() TObjectPtr<class APlanetBody> AtmospherePlanet;
	void SpawnTestTargets();
};
