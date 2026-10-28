// Slonk - draws every star that is too far away to render as a mesh as a correctly sized glow sprite
// just inside the ship's star sphere, so the sun never disappears from across the system.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "DistantStarsComponent.generated.h"

class AStarBody;
class ANebula;
class APlanetBody;
class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;

/**
 * Attach to the player ship (absolute rotation, so children sit on world axes). Each tick it finds
 * every AStarBody further away than AStarBody::FarSpriteDistanceCm and places a camera-facing
 * additive plane at SpriteDistance along the bearing to the star, scaled so the photosphere disc
 * subtends the true angular diameter (2R / D) with a glow around it that never shrinks below
 * MinAngularSize. The star itself hides its own glow past the hand-over distance.
 */
UCLASS(ClassGroup = (Starship), meta = (BlueprintSpawnableComponent))
class SLONK_API UDistantStarsComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UDistantStarsComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Distance (cm) at which the stand-in sprites are drawn. Must be inside the ship's star sphere. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distant Stars")
	double SpriteDistance = 4.0e11;

	/**
	 * Artistic enlargement of the distant disc: the real sun is only half a degree across at 1 AU, which reads
	 * as a dot. Blended in from 1x at the mesh hand-over distance to the full value at ScaleFullDistance.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distant Stars", meta = (ClampMin = "1", ClampMax = "10"))
	float ApparentSizeScale = 3.f;

	/** Distance (cm) at which ApparentSizeScale applies in full. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distant Stars")
	double ScaleFullDistance = 2.0e12;

	/** Minimum width of the glow sprite as a fraction of SpriteDistance (radians, roughly). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distant Stars")
	float MinAngularSize = 0.08f;

	/** How far the glow extends beyond the disc, in disc radii, when the disc is large enough to matter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distant Stars")
	float GlowRadii = 3.5f;

	/** Glow brightness multiplier on top of the star's CoronaIntensity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distant Stars")
	float GlowIntensityScale = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distant Stars")
	TSoftObjectPtr<UMaterialInterface> GlowMaterial;

	/** Seconds between rescans of the level for star actors. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distant Stars")
	float RescanInterval = 1.f;

	/** Nebulae beyond the render horizon get a glow sprite too, once they subtend at least this many degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distant Stars")
	float NebulaMinAngularDeg = 0.02f;

	/** The sprite never reads smaller than this many degrees, so a distant nebula is a smudge and not a dot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distant Stars")
	float NebulaMinApparentDeg = 3.f;

	/** Glow brightness of a nebula sprite relative to its Brightness. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distant Stars")
	float NebulaGlowScale = 6.f;

	/** Planets and moons beyond the horizon: a point of light this wide at least, a lit disc at the true angular
	 *  size once that passes PlanetDiscMinDeg. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distant Stars")
	float PlanetMinApparentDeg = 0.3f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distant Stars")
	float PlanetDiscMinDeg = 0.06f;
	/** Brightness of a planet disc and of its point of light: kept below the bloom threshold so a far planet
	 *  reads as a crisp point, not a halo (the sun's sprite sits near 60). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distant Stars")
	float PlanetDiscIntensity = 12.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distant Stars")
	float PlanetPointIntensity = 6.f;

private:
	void Rescan();
	UStaticMeshComponent* EnsureSprite(int32 Index);

	UPROPERTY() TObjectPtr<UStaticMesh> PlaneMesh;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Sprites;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> SpriteMIDs;
	TArray<TWeakObjectPtr<AStarBody>> Stars;
	TArray<TWeakObjectPtr<ANebula>> Nebulae;
	TArray<TWeakObjectPtr<APlanetBody>> Planets;
	float RescanTimer = 0.f;
};
