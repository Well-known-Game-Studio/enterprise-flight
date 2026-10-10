// Slonk - a planet / moon body with procedural surface, clouds and a star-lit atmosphere shell.
// Spawned by hand or by the PCG solar-system graph.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PlanetBody.generated.h"

class UStaticMeshComponent;
class UDynamicMeshComponent;
class UMaterialInstanceDynamic;

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
	Moon
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface") float OceanRoughness = 0.45f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface") float LandRoughness = 0.9f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface") FLinearColor ShallowColor = FLinearColor(0.1f, 0.35f, 0.5f);
	/** Strength of the procedural normal detail. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface") float NormalStrength = 0.12f;
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface", meta = (ClampMin = "0", ClampMax = "0.2")) float ReliefFraction = 0.f;

	// Clouds
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds") bool bClouds = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds", meta = (ClampMin = "0", ClampMax = "1")) float CloudCoverage = 0.45f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds") float CloudScale = 4.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds") FLinearColor CloudColor = FLinearColor::White;
	/** Cloud layer height as a fraction of the radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds") float CloudHeightFraction = 0.012f;
	/** Extra spin of the cloud layer, deg/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Clouds") float CloudSpinDegPerSec = 0.15f;

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
};

UCLASS()
class SLONK_API APlanetBody : public AActor
{
	GENERATED_BODY()

public:
	APlanetBody();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
#if WITH_EDITOR
	virtual void PostEditMove(bool bFinished) override;
#endif
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Body;

	/** Dense cube-sphere used when the camera is close (built on demand with GeometryScript). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UDynamicMeshComponent> DetailBody;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Clouds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Atmosphere;

	/** Radius in km. Drives the body's world scale directly (PCG feeds this through a property override). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	float RadiusKm = 600.f;

	/** Rotation about the local Z axis in degrees per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	float SpinDegPerSec = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	FString PlanetName;

	/** Which preset to start from. Auto hashes the spawn location so PCG planets vary without attribute plumbing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	EPlanetArchetype Archetype = EPlanetArchetype::Auto;

	/** PCG can set this (0..1) to pick the archetype deterministically instead of hashing the location. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet", meta = (ClampMin = "0", ClampMax = "1"))
	float ArchetypeSelector = -1.f;

	/** When true, Style is (re)filled from the archetype on construction; set false to hand-tune Style. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	bool bStyleFromArchetype = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	FPlanetStyle Style;

	/** The archetype actually in use (after Auto resolution). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Planet")
	EPlanetArchetype ResolvedArchetype = EPlanetArchetype::Terrestrial;

	UFUNCTION(BlueprintPure, Category = "Planet")
	bool IsMoon() const { return ResolvedArchetype == EPlanetArchetype::Moon; }

	/** Display name: PlanetName if set, else the actor label. */
	UFUNCTION(BlueprintPure, Category = "Planet")
	FString GetDisplayName() const;

	/** Optional star to light the atmosphere from. If unset, the nearest actor tagged "Star" is used, else the directional light. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet")
	TObjectPtr<AActor> Star;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Materials") TSoftObjectPtr<UMaterialInterface> SurfaceMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Materials") TSoftObjectPtr<UMaterialInterface> CloudMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|Materials") TSoftObjectPtr<UMaterialInterface> AtmosphereMaterial;

	UFUNCTION(BlueprintPure, Category = "Planet")
	double GetRadiusCm() const;

	// ---- Level of detail ----

	/** Switch to the dense mesh when the camera is closer than this many radii from the centre. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|LOD")
	float DetailMeshRadii = 10.f;

	/** Steps per cube face of the dense sphere (triangles = 12 * steps^2). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Planet|LOD", meta = (ClampMin = "8", ClampMax = "200"))
	int32 DetailSphereSteps = 80;

	/** True while the world's Sky Atmosphere is parked on this planet (the shell atmosphere is hidden). */
	UFUNCTION(BlueprintCallable, Category = "Planet")
	void SetUsingSkyAtmosphere(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Planet")
	bool IsUsingSkyAtmosphere() const { return bUsingSkyAtmosphere; }

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
	void ApplyMaterials();
	void UpdateStarDirection();
	EPlanetArchetype PickArchetype(FRandomStream& Rng) const;

	void UpdateLOD();
	void BuildDetailMesh();

	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> SurfaceMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> DetailSurfaceMID;
	bool bUsingSkyAtmosphere = false;
	bool bDetailActive = false;
	bool bDetailBuilt = false;
	float LODTimer = 0.f;
	float CloudYawDeg = 0.f;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> CloudMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> AtmosphereMID;

	int32 SettleTicks = 3;
	float MeshUnitRadius = 50.f;
	FVector CachedStarDirection = FVector::ForwardVector;
	float StarSearchTimer = 0.f;
};
