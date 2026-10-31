// Slonk - a planet / moon body with procedural surface, clouds and a star-lit atmosphere shell.
// Spawned by hand or by the PCG solar-system graph.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FarBody.h"
#include "PlanetTerrain.h"
#include "Async/Future.h"
#include "PlanetBody.generated.h"

class UPlanetTerrainComponent;

class UStaticMeshComponent;
class USphereComponent;
class UDynamicMeshComponent;
class UMaterialInstanceDynamic;
class UTexture2D;
class UHierarchicalInstancedStaticMeshComponent;

UENUM(BlueprintType)
enum class EPlanetArchetype : uint8
{
	Auto        UMETA(DisplayName = "Auto (from location hash)"),
	Terrestrial,
	Ocean,
	Desert,
	Ice,
	Lava,
	GasGiant,
	Moon,
	/** Thick yellow cloud deck, no surface visible. */
	Venus,
	/** Red deserts, polar caps, thin pink sky, a little water. */
	Mars,
	/** Neptune-like: azure and violet bands, white streaks, a cold deep haze, and a light ring of comet ice. Never rolled; placed by hand. */
	IceGiant
};

UENUM(BlueprintType)
enum class EMoonKind : uint8
{
	Rocky,
	Icy,
	Lava
};

/** Everything that drives the planet look. Exposed so PCG / designers can override any field. */
USTRUCT(BlueprintType)
struct FPlanetStyle
{
	GENERATED_BODY()

	// Surface
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface") FLinearColor OceanColor = FLinearColor(0.05f, 0.17f, 0.35f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface") FLinearColor LandColor = FLinearColor(0.30f, 0.36f, 0.18f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface") FLinearColor LandColor2 = FLinearColor(0.55f, 0.45f, 0.28f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface") FLinearColor MountainColor = FLinearColor(0.55f, 0.52f, 0.50f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface") FLinearColor IceColor = FLinearColor(0.90f, 0.93f, 0.97f);
	/** 0 = all land, 1 = all ocean. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface", meta = (ClampMin = "0", ClampMax = "1")) float SeaLevel = 0.5f;
	/** Latitude (|z| of the unit normal) where polar ice begins; 2 disables caps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface") float IceCapStart = 0.82f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface") float NoiseScale = 3.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface") float OceanRoughness = 0.22f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface") float LandRoughness = 0.9f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface") FLinearColor ShallowColor = FLinearColor(0.1f, 0.35f, 0.5f);
	/** Strength of the procedural normal detail. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface") float NormalStrength = 0.25f;
	/** Impact craters (0 none .. 1 lunar). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface", meta = (ClampMin = "0", ClampMax = "1")) float CraterStrength = 0.f;
	/** Smooth dark basins between cratered highlands (0 none .. 1 lunar maria). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface", meta = (ClampMin = "0", ClampMax = "1")) float MariaStrength = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface") float DetailNormalScale = 5.f;
	/** Night-side city lights on land (0 = none). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface") float NightLightIntensity = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface") FLinearColor NightLightColor = FLinearColor(1.0f, 0.8f, 0.5f);
	/** Emissive strength of the "ocean" (molten rock on lava worlds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface") float LavaEmissive = 0.f;
	/** 1 = fully latitude-banded (gas giant), 0 = continents. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface", meta = (ClampMin = "0", ClampMax = "1")) float BandStrength = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface") float BandScale = 14.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface") float BandWarp = 0.6f;
	/** Vertex displacement of land above sea level as a fraction of the radius (heightmap relief). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface", meta = (ClampMin = "0", ClampMax = "0.2")) float ReliefFraction = 0.006f;
	/** Size scale of the crater fields (1 = lunar: basins up to a third of the radius); larger = smaller craters. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface") float CraterScale = 1.f;

	// Rings
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rings") bool bRings = false;
	/** Inner / outer edge of the ring system in planet radii. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rings") float RingInner = 1.5f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rings") float RingOuter = 2.4f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rings") FLinearColor RingColor = FLinearColor(0.85f, 0.78f, 0.65f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rings", meta = (ClampMin = "0", ClampMax = "1")) float RingOpacity = 0.9f;
	/** Offsets the ring band noise so no two ring systems match. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rings") float RingSeed = 0.f;
	/** Fine ringlet frequency multiplier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rings") float RingDetail = 1.f;

	// Clouds
	/** 0..1 lineae on an icy moon: long double-ridged cracks in the ice, stained brown (Europa). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface", meta = (ClampMin = "0", ClampMax = "1")) float Lineae = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds") bool bClouds = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (ClampMin = "0", ClampMax = "1")) float CloudCoverage = 0.45f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds") float CloudScale = 4.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds") FLinearColor CloudColor = FLinearColor::White;
	/** Cloud layer height as a fraction of the radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds") float CloudHeightFraction = 0.006f;
	/** Extra spin of the cloud layer, deg/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds") float CloudSpinDegPerSec = 0.5f;
	/** Internal evolution of the cloud shapes (noise units per second). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds") float CloudDrift = 0.03f;
	/** Volumetric layer: bottom above the surface and thickness, km (Earth: cumulus from 2 km, tops by 10). 0 thickness = no volumetric layer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds") float CloudBaseKm = 2.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds") float CloudThicknessKm = 8.f;
	/** Density of the volumetric layer relative to Earth cumulus (0.2 is Martian water ice, 2 is the Venus deck). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds") float CloudDensity = 1.f;
	/** 0 broken cumulus, 1 a continuous deck. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (ClampMin = "0", ClampMax = "1")) float CloudDeck = 0.f;

	// Atmosphere
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere") bool bAtmosphere = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere") FLinearColor AtmosphereColor = FLinearColor(0.35f, 0.6f, 1.0f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere") FLinearColor SunsetColor = FLinearColor(1.0f, 0.45f, 0.15f);
	/** Shell thickness as a fraction of the radius (how far the glow extends past the limb). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere", meta = (ClampMin = "0", ClampMax = "0.5")) float AtmosphereDepth = 0.08f;
	/** Falloff of the limb glow with height: higher = thinner, sharper. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere") float Falloff = 2.f;
	/** Shape of the haze over the disc: higher keeps haze near the limb. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere") float HazePower = 4.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere") float Intensity = 2.2f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere", meta = (ClampMin = "0", ClampMax = "1")) float Opacity = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere", meta = (ClampMin = "0", ClampMax = "1")) float Haze = 0.03f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere", meta = (ClampMin = "0", ClampMax = "1")) float SunsetStrength = 0.8f;
	/** Glow left on the night side (0 = pitch black). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere", meta = (ClampMin = "0", ClampMax = "1")) float NightFactor = 0.04f;
	/** Volumetric (Sky Atmosphere) layer: Rayleigh scale height in km. 0 = derive from AtmosphereDepth. Set per world by the archetype. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere") float AtmosphereScaleHeightKm = 0.f;
	/** Volumetric layer top in km. 0 = a few scale heights. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Atmosphere") float AtmosphereHeightKm = 0.f;
};

UCLASS()
class SLONK_API APlanetBody : public AActor, public IFarBody
{
	GENERATED_BODY()

public:
	// IFarBody
	virtual void SetBeyondHorizon(bool bBeyond) override;
	virtual bool IsBeyondHorizon() const override { return bBeyondHorizon; }

	APlanetBody();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
#if WITH_EDITOR
	virtual void PostEditMove(bool bFinished) override;
#endif
	virtual void Tick(float DeltaTime) override;
	virtual void PreRegisterAllComponents() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Body;

	/** Analytic collision at the body's radius (the render spheres carry no usable collision). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> CollisionSphere;

	/** Dense cube-sphere used when the camera is close (built on demand with GeometryScript). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UDynamicMeshComponent> DetailBody;

	/**
	 * In game the far sphere and the cloud shell are generated cube-spheres like the detail mesh (the engine's unit
	 * sphere asset gave the material different vertex positions, so continents changed at the hand-over). Body and
	 * Clouds keep the static meshes for the editor viewport and for collision.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UDynamicMeshComponent> FarBody;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UDynamicMeshComponent> CloudsMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Clouds;

	/**
	 * Quadtree terrain for worlds with relief (game only): six cube-sphere roots split toward the camera down to
	 * metre cells, built from the height field in PlanetTerrain.h. Replaces the far/detail spheres and the clipmap
	 * patches on those worlds; gas giants keep the shader-shaded sphere.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UPlanetTerrainComponent> Terrain;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Atmosphere;

	/** Flat annulus in the equatorial plane, built on demand when the style has rings. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UDynamicMeshComponent> Rings;

	/** Radius in km. Drives the body's world scale directly (PCG feeds this through a property override). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	float RadiusKm = 600.f;

	/** Rotation about the local Z axis in degrees per second. 0.0042 is a 24-hour day; the old stylised 0.3 (a
	 *  20-minute day, 30 km/s of ground speed on an Earth) is rescaled to a real day in BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	float SpinDegPerSec = 0.0042f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	FString PlanetName;

	/** Which preset to start from. Auto hashes the spawn location so PCG planets vary without attribute plumbing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	EPlanetArchetype Archetype = EPlanetArchetype::Auto;

	/** PCG can set this (0..1) to pick the archetype deterministically instead of hashing the location. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet", meta = (ClampMin = "0", ClampMax = "1"))
	float ArchetypeSelector = -1.f;

	/** For placed showcase moons: fix the moon kind and the haze instead of rolling them from the seed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet") bool bForceMoonKind = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet", meta = (EditCondition = "bForceMoonKind")) EMoonKind ForcedMoonKind = EMoonKind::Icy;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet") bool bForceHazy = false;
	/** Giants: the palette family (0 Jovian, 0.6 Saturnian, 0.95 Neptunian) instead of rolling it; negative rolls. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet") float PaletteFamily = -1.f;
	/** Give this world the showcase moons even without rings. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet") bool bShowcaseMoons = false;

	/** When true, Style is (re)filled from the archetype on construction; set false to hand-tune Style. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	bool bStyleFromArchetype = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	FPlanetStyle Style;

	/**
	 * World location the style and noise seed are hashed from. Captured the first time the appearance is applied
	 * (in the editor for placed/PCG planets) and never updated: the floating origin moves the actor at runtime,
	 * and a seed taken from the moved location would re-roll the continents mid-flight.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Planet")
	FVector SeedLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Planet")
	bool bSeedLocationSet = false;

	/** The archetype actually in use (after Auto resolution). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Planet")
	EPlanetArchetype ResolvedArchetype = EPlanetArchetype::Terrestrial;

	/** For Moon archetypes: which kind of moon the location hash produced. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Planet")
	EMoonKind ResolvedMoonKind = EMoonKind::Rocky;
	/** Moon with a thick haze layer (Titan-like). */
	bool bHazyMoon = false;

	UFUNCTION(BlueprintPure, Category = "Planet")
	bool IsMoon() const { return ResolvedArchetype == EPlanetArchetype::Moon; }
	/** Gas and ice giants: no ground; the painted sphere is a pressure level (the 10 bar deck). */
	UFUNCTION(BlueprintPure, Category = "Planet")
	bool IsGiant() const { return ResolvedArchetype == EPlanetArchetype::GasGiant || ResolvedArchetype == EPlanetArchetype::IceGiant; }
	bool HasSolidSurface() const { return !IsGiant(); }
	/** Atmospheric pressure in bar at an altitude above the deck of a giant (10 bar at the deck, a scale height per e-fold). */
	float PressureBarAt(double AltitudeCm) const;
	/** Keep the cloud shell drawn beyond this many km from the camera while the ray-marched layer covers the near disc. */
	void SetLayerReachKm(float ReachKm);

	/** Whether this world has settlements (lit at night, built by day). */
	bool HasCities() const;
	/** Reach (km) within which towns are built around the view; 0 switches the buildings off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Cities") float CityReachKm = 30.f;

	/** Display name: PlanetName if set, else the actor label. */
	UFUNCTION(BlueprintPure, Category = "Planet")
	FString GetDisplayName() const;

	/** Optional star to light the atmosphere from. If unset, the nearest actor tagged "Star" is used, else the directional light. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	TObjectPtr<AActor> Star;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Materials") TSoftObjectPtr<UMaterialInterface> SurfaceMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Materials") TSoftObjectPtr<UMaterialInterface> CloudMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Materials") TSoftObjectPtr<UMaterialInterface> BuildingMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Materials") TSoftObjectPtr<UMaterialInterface> AtmosphereMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Materials") TSoftObjectPtr<UMaterialInterface> RingMaterial;

	UFUNCTION(BlueprintPure, Category = "Planet")
	double GetRadiusCm() const;

	/** Mean density by archetype, g/cc (1.3 for a gas giant, 5.5 for a rocky world). */
	UFUNCTION(BlueprintPure, Category = "Planet")
	float GetMeanDensityGcc() const;

	/** Surface gravity from the radius and the mean density, cm/s^2 (Earth is 981). */
	UFUNCTION(BlueprintPure, Category = "Planet")
	double GetSurfaceGravityCmS2() const;

	// ---- Level of detail ----

	/** Use the quadtree terrain on worlds with relief (off: the old displaced spheres and clipmap patches). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|LOD")
	bool bUseTerrainChunks = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Materials") TSoftObjectPtr<UMaterialInterface> TerrainMaterial;

	UFUNCTION(BlueprintPure, Category = "Planet")
	bool HasTerrain() const { return bTerrainActive; }

	/** Distance from the centre to the ground (land or sea surface) under a world position, cm. The body radius without terrain. */
	UFUNCTION(BlueprintPure, Category = "Planet")
	double GetSurfaceRadiusCm(const FVector& WorldPos) const;

	/** The height field parameters this planet's terrain is built from. */
	FPlanetTerrainParams MakeTerrainParams() const;

	/** Switch to the dense mesh when the camera is closer than this many radii from the centre. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|LOD")
	float DetailMeshRadii = 10.f;

	/** Steps per cube face of the dense sphere (triangles = 12 * steps^2). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|LOD", meta = (ClampMin = "8", ClampMax = "200"))
	int32 DetailSphereSteps = 200;

	/**
	 * Close-range terrain: nested square grid patches (a clipmap) under the camera, each level four times the
	 * extent of the one inside it, so the vertex displacement is sampled finely where the player looks. The
	 * dense sphere keeps drawing underneath, lowered beneath the outermost patch so it never pokes through.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|LOD", meta = (ClampMin = "0", ClampMax = "4"))
	int32 PatchLevels = 3;

	/** Grid cells per side of each patch level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|LOD", meta = (ClampMin = "16", ClampMax = "400"))
	int32 PatchGridSteps = 160;

	/** Patches are active below this altitude (in radii). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|LOD")
	float PatchMaxAltitudeRadii = 0.6f;

	/** Half-extent of the innermost patch as a multiple of the altitude (gnomonic units; 1 ~ 45 degrees). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|LOD")
	float PatchExtentPerAltitude = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|LOD")
	float PatchMinHalfExtent = 0.003f;

	UFUNCTION(BlueprintPure, Category = "Planet")
	bool AreTerrainPatchesActive() const { return bPatchesActive; }

	// ---- Ring particles ----

	/** Individual ring rocks materialise in cells around the camera when it is this close (radii) to the ring plane. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Rings")
	float RingParticleMaxPlaneDistanceRadii = 0.25f;

	/** Edge of one ring particle cell (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Rings")
	double RingParticleCellCm = 1.5e7;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Rings", meta = (ClampMin = "1", ClampMax = "8"))
	int32 RingParticleCellRange = 6;

	/** How fast the ring rocks orbit, as a multiple of the planet's spin (1 = they turn with the planet, 0 = fixed). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Rings")
	float RingOrbitRateScale = 1.f;

	/** Candidate rocks per cell; each is kept with the ring's local density as probability. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Rings", meta = (ClampMin = "0", ClampMax = "300"))
	int32 RingParticlesPerCell = 260;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Rings")
	float RingParticleMinRadiusCm = 10000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Rings")
	float RingParticleMaxRadiusCm = 400000.f;

	/** Half-thickness of the particle sheet as a fraction of the planet radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Rings")
	float RingThicknessFraction = 0.0012f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Materials") TSoftObjectPtr<UMaterialInterface> RingRockMaterial;

	UFUNCTION(BlueprintPure, Category = "Planet")
	bool AreRingParticlesActive() const { return bRingParticlesActive; }

	/** Ring density (0..1) at fraction x across the ring's width; the same table the materials sample. */
	float RingDensityAt(float X) const;

	/** True while the world's Sky Atmosphere is parked on this planet (the shell atmosphere is hidden). */
	UFUNCTION(BlueprintCallable, Category = "Planet")
	void SetUsingSkyAtmosphere(bool bEnabled);
	/** How much of the cloud shell shows, 0..1: the game mode fades it out as the volumetric layer fades in. */
	void SetCloudShellFade(float Fade);
	/** 1 while this world is seen through another world's sky: its materials then add the sky's own in-scatter in
	 *  place of the renderer's aerial perspective, which stops 96 km out and left a moon a dark cut-out in the blue. */
	void SetSkyBehind(float K);

	UFUNCTION(BlueprintPure, Category = "Planet")
	bool IsUsingSkyAtmosphere() const { return bUsingSkyAtmosphere; }

	/** The world's weather map once generated (equirectangular over the cloud shell's frame: R masses, G cells, B type). */
	UTexture2D* GetWeatherMap() const { return WeatherMap; }
	/** 0..1: how far the weather map has faded in over the shell's own noise. */
	float GetWeatherStrength() const { return WeatherStrength; }
	/** The cloud shell's frame (the body's spin plus the shell's own drift): what the weather map is laid over. */
	FTransform GetCloudFrame() const;

	UFUNCTION(BlueprintPure, Category = "Planet")
	bool IsDetailMeshActive() const { return bDetailActive; }

	/** Hook for PCG "Spawn Actor" post-spawn functions: re-applies everything after property overrides were copied in. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Planet")
	void PCGPostSpawn();

	/** Re-apply radius, style and material parameters now. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Planet")
	void ApplyAppearance();

	/** Direction from this planet toward the star that lights it (unit vector). */
	UFUNCTION(BlueprintPure, Category = "Planet")
	FVector GetStarDirection() const;

	static const FName StarTag;

private:
	void ResolveStyleFromArchetype();
	FVector GetSeedLocation();
	void ApplyMaterials();
	void UpdateStarDirection();
	EPlanetArchetype PickArchetype(FRandomStream& Rng) const;

	void UpdateLOD();
	void EnsureTerrain();
	void ApplyTerrainMaterialParams();
	void BuildDetailMesh();
	void BuildFarMeshes();
	void UpdateShellVisibility();
	void UpdatePatches(const FVector& ViewLoc);
	void EnsurePatchComponents();
	void BuildPatchLevel(int32 Level, double HalfExtent);
	void SetPatchesActive(bool bActive);
	void ApplyPatchRegions();
	void BuildRings();
	void BuildRingProfile();
	void UpdateRingParticles(const FVector& ViewLoc);
	void ClearRingParticles();
	void UpdateSunDirectionParams();
	TArray<UMaterialInstanceDynamic*> SurfaceMIDs() const;

	UPROPERTY() TArray<TObjectPtr<UDynamicMeshComponent>> Patches;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> PatchMIDs;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> RingMID;
	UPROPERTY() TObjectPtr<UTexture2D> RingProfileTexture;
	/** Tilted with the body; turns at RingOrbitRateScale times the body's spin so the rocks visibly orbit. */
	UPROPERTY() TObjectPtr<USceneComponent> RingFrame;
	UPROPERTY() TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> RingRocks;
	float SpinAccumDeg = 0.f;
	TArray<float> RingProfile;
	TArray<float> RingTint;
	float RingProfileSeed = -1.f;
	float RingProfileDetail = -1.f;
	bool bRingParticlesActive = false;
	FIntVector RingParticleCell = FIntVector(TNumericLimits<int32>::Max());
	bool bPatchesActive = false;
	bool bRingsBuilt = false;
	float RingsBuiltInner = 0.f;
	float RingsBuiltOuter = 0.f;
	FVector PatchCenter = FVector::ZAxisVector;
	FVector PatchEast = FVector::XAxisVector;
	FVector PatchNorth = FVector::YAxisVector;
	double PatchHalfExtent0 = 0.0;
	TArray<double> PatchHalfExtents;

	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> SurfaceMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> DetailSurfaceMID;
	bool bUsingSkyAtmosphere = false;
	float CloudShellFade = 1.f;
	float LayerReachKm = 1.0e9f;
	bool bTerrainActive = false;
	float SkyBehind = 0.f;
	FPlanetTerrainParams TerrainParams;
	bool bDetailActive = false;
	bool bBeyondHorizon = false;
	bool bDetailBuilt = false;
	bool bFarBuilt = false;
	float LODTimer = 0.f;
	float CloudYawDeg = 0.f;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> CloudMID;
	UPROPERTY() TObjectPtr<UTexture2D> WeatherMap;
	UPROPERTY() TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Buildings;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> BuildingMID;
	struct FCityBuild { TSet<uint32> Towns; TArray<FTransform> Instances; int32 TownCount = 0; };
	TSharedPtr<FCityBuild, ESPMode::ThreadSafe> CityPending;
	TFuture<void> CityFuture;
	TSet<uint32> CityActiveTowns;
	FVector CityCentreLocal = FVector(1.0e30, 0.0, 0.0);
	float CityTimer = 0.f;
	void UpdateCities(const FVector& ViewLoc, float DeltaTime);
	void ClearCities();
	TSharedPtr<TArray<FColor>, ESPMode::ThreadSafe> WeatherPixels;
	TFuture<void> WeatherFuture;
	int32 WeatherW = 0;
	int32 WeatherH = 0;
	bool bWeatherRequested = false;
	float WeatherStrength = 0.f;
	void RequestWeatherMap(const FLinearColor& Seed);
	void PollWeatherMap(float DeltaTime);
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> AtmosphereMID;

	int32 SettleTicks = 3;
	float MeshUnitRadius = 50.f;
	FVector CachedStarDirection = FVector::ForwardVector;
	float StarSearchTimer = 0.f;
};
