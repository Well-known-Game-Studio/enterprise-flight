// Slonk - a star: granulated emissive sphere plus a corona shell. Tags itself "Star" so planets and the
// game mode light from it.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FarBody.h"
#include "StarBody.generated.h"

class UStaticMeshComponent;
class USphereComponent;
class UMaterialInstanceDynamic;
class UNiagaraComponent;
class UNiagaraSystem;

UCLASS()
class SLONK_API AStarBody : public AActor, public IFarBody
{
	GENERATED_BODY()

public:
	// IFarBody
	virtual void SetBeyondHorizon(bool bBeyond) override;
	virtual bool IsBeyondHorizon() const override { return bBeyondHorizon; }

	AStarBody();

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

	/** Camera-facing glow / ray sprite. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> Corona;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star")
	float RadiusKm = 3000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star")
	FString StarName = TEXT("Sol");

	/** Photosphere colour (also tints the system's sunlight). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star")
	FLinearColor StarColor = FLinearColor(1.0f, 0.86f, 0.62f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star")
	FLinearColor CoronaColor = FLinearColor(1.0f, 0.72f, 0.4f);

	/** Emissive multiplier of the photosphere. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star")
	float Intensity = 80.f;

	/** Minimum angular size of the glow sprite (fraction of the camera distance) so the star stays visible from across the system. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star")
	float GlowMinAngularSize = 0.035f;

	/** Corona shell extent as a fraction of the radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star", meta = (ClampMin = "0.1", ClampMax = "4"))
	float CoronaDepth = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star")
	float CoronaIntensity = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star")
	float CoronaFalloff = 3.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star")
	float LimbDarkening = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star")
	float GranuleScale = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star")
	float SpinDegPerSec = 0.05f;

	/** A young star (StarSystem's EStellarKind): its protoplanetary disk in its equator, out to DiskOuterAu, parted by
	 *  DiskGaps gaps, and twin jets from its poles JetLengthAu long. Hidden on any other star. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star|Young") bool bYoung = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star|Young") float DiskOuterAu = 4.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star|Young") int32 DiskGaps = 2;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star|Young") float JetLengthAu = 5.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components") TObjectPtr<UStaticMeshComponent> Disk;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components") TObjectPtr<UStaticMeshComponent> JetNorth;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components") TObjectPtr<UStaticMeshComponent> JetSouth;

	/** A planetary nebula about the star (a white dwarf, StarSystem's EStellarKind): the shells it shed as a dying
	 *  giant, ShellRadiusAu across from it, round or (ShellLobes) pinched at the waist into two lobes along its axis,
	 *  glowing ShellInner nearest it and ShellOuter at the edge (M_Nebula's shell mode). Hidden on any other star. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star|Shell") bool bShell = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star|Shell") float ShellRadiusAu = 100.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star|Shell", meta = (ClampMin = "0", ClampMax = "1")) float ShellLobes = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star|Shell") FLinearColor ShellInner = FLinearColor(0.3f, 0.95f, 0.85f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star|Shell") FLinearColor ShellOuter = FLinearColor(1.0f, 0.3f, 0.35f);
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components") TObjectPtr<UStaticMeshComponent> ShellVolume;

	/** A star of a close pair (StarSystem's EStellarKind::ClosePair). The donor fills its Roche lobe: Tide draws it
	 *  out toward its Partner into a teardrop pointed at the inner Lagrange point (M_Star's Tide, a fraction of its
	 *  radius). The accretor (bAccreting) draws the gas into a disk round itself, fed by a stream from that point
	 *  (M_AccretionDisk on the Disk plane), and goes round the donor at OrbitRadiusCm once in OrbitPeriodS of game
	 *  time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star|Pair") float Tide = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star|Pair") bool bAccreting = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star|Pair") TObjectPtr<AStarBody> Partner;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star|Pair") double OrbitRadiusCm = 0.0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star|Pair") float OrbitPeriodS = 20000.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star|Pair") float OrbitPhase = 0.f;

	/** A black hole (StarSystem's EStellarKind::BlackHole): no body and no glow of its own; its shadow, its accretion
	 *  disk and the bending of the light about it are traced through the space round it (M_BlackHole on the Lens
	 *  sphere, LensRadiusRs Schwarzschild radii across, placed for the camera as the other surroundings are).
	 *  RadiusKm is its Schwarzschild radius (2.95 km a sun's mass); the disk runs from the innermost stable orbit,
	 *  three of those, out to DiskOuterRs, DiskTempK near its inner edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star|Black hole") bool bBlackHole = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star|Black hole") float DiskOuterRs = 30.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star|Black hole") float DiskTempK = 9000.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star|Black hole") float LensRadiusRs = 60.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components") TObjectPtr<UStaticMeshComponent> LensVolume;
	/** Its mass in suns, from its Schwarzschild radius. */
	float GetMassSuns() const { return RadiusKm / 2.953f; }

	/** How spotted the star is: 0 a clean disc, 1 eight spot groups at once (the photosphere's Activity). Its spots,
	 *  and where and when they come, are drawn from its name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star", meta = (ClampMin = "0", ClampMax = "1"))
	float Activity = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star|Materials") TSoftObjectPtr<UMaterialInterface> StarMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star|Materials") TSoftObjectPtr<UMaterialInterface> CoronaMaterial;

	/**
	 * Beyond this camera distance (cm) the star's own glow sprite is hidden and the player's
	 * UDistantStarsComponent draws a correctly sized stand-in inside the ship's star sphere instead
	 * (the star sphere is opaque, so the real star is invisible from further out).
	 */
	static constexpr double FarSpriteDistanceCm = 2.5e11;

	UFUNCTION(BlueprintPure, Category = "Star")
	double GetRadiusCm() const { return FMath::Max(RadiusKm, 1.f) * 100000.0; }

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Star")
	void ApplyAppearance();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Star")
	void PCGPostSpawn() { ApplyAppearance(); }

	/** The plasma it throws off (NS_StarEjecta, tools/fx/star_ejecta.py): a steady wind from its surface, leaning to
	 *  the poles, and now and then an ejection from an active region. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Star|Materials") TSoftObjectPtr<UNiagaraSystem> EjectaSystem;

	/** An ejection now, from a latitude of the activity belts (dev: ship star cme). */
	UFUNCTION(BlueprintCallable, Category = "Star")
	void TriggerEjecta();
	/** The same, from the limb as seen from here (so it is seen leaving the disc's edge). */
	void TriggerEjectaSeenFrom(const FVector& ViewLocation);

private:
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> StarMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> CoronaMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> DiskMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> JetMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> ShellMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> AccretionMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> LensMID;
	void ApplySurroundings();
	/** Place the disk and jets, or the shell, for the camera: a copy shrunk toward the camera along the line of sight, the same angles,
	 *  so that its farthest point lies within ProxyCm: inside the ship's star sphere (5e11 cm, opaque, where the
	 *  sky is drawn) and the render horizon, both of which a disk of AUs would cross. As it is, when it fits. */
	void PlaceSurroundings(const FVector& ViewLoc);
	static constexpr double ProxyCm = 4.5e11;
	bool HasSurroundings() const { return bYoung || bShell || (bAccreting && Partner) || bBlackHole; }
	/** The plane's half-width round an accretor: out past the inner Lagrange point. */
	double AccretionHalfCm() const { return OrbitRadiusCm * 0.56; }
	UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> Ejecta;
	void TickEjecta(float DeltaTime);
	/** Seconds to the next ejection, and the seconds left of the one going. */
	float NextEjectaIn = 30.f;
	float EjectaLeft = 0.f;
	FRandomStream EjectaRng;
	int32 SettleTicks = 3;
	bool bBeyondHorizon = false;
	float MeshUnitRadius = 50.f;
};
