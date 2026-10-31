// Slonk - the player's starship: hull, flight model, warp drive, weapons, cameras and effects.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "StarshipPawn.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class USpringArmComponent;
class UCameraComponent;
class UPointLightComponent;
class UPostProcessComponent;
class UMaterialInstanceDynamic;
class UStarshipMovementComponent;
class UWarpDriveComponent;
class UStarshipWeaponsComponent;
class UWarpStreaksComponent;
class UBridgeInteriorComponent;
class APersonPawn;
class UWarpViewComponent;
class UDistantStarsComponent;
class UShieldComponent;
class UTractorBeamComponent;
class URunningLightsComponent;
class URcsThrusterComponent;

UENUM(BlueprintType)
enum class EWarpVisualMode : uint8
{
	/** Star streaks plus radial streaking of everything you fly past. */
	Classic,
	/** Relativistic: aberration bunches the view forward, blue shift ahead, red shift behind, contraction along the travel axis. */
	Relativistic
};

UENUM(BlueprintType)
enum class EStarshipCameraMode : uint8
{
	Chase,
	Bridge,
	Cinematic,
	/** Fixed views in the ship's own frame, at the cinematic distance scaled by the chase zoom. */
	Front,
	Port,
	Starboard,
	Top,
	Bottom
};

/** A running / engine light on the hull, positioned relative to the ship root (cm). */
USTRUCT(BlueprintType)
struct FStarshipLightSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light")
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light")
	FVector Offset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light")
	FLinearColor Color = FLinearColor::White;

	/** Candelas. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light")
	float Intensity = 20000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light")
	float AttenuationRadius = 12000.f;

	/** Scaled by the warp glow multiplier (nacelles, deflector) ... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light")
	bool bWarpLight = true;

	/** ... or by the impulse throttle (impulse exhausts). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Light")
	bool bImpulseLight = false;
};

class AShuttlecraftPawn;
class UScienceScannerComponent;

UCLASS()
class SLONK_API AStarshipPawn : public APawn
{
	GENERATED_BODY()

public:
	AStarshipPawn();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	// ---- Autopilot to the HUD's nav target ----
	/** 0 off, 1 warp (holds the heading, sets the factor so the time to target stays AutopilotSeconds, drops out at
	 *  the standoff and eases in on impulse), 2 impulse (eases in to the standoff). Read by the capture scripts. */
	UPROPERTY(BlueprintReadOnly, Category = "Autopilot") int32 AutopilotState = 0;
	/** Distance to the nav target this frame, km (-1 without one). */
	UPROPERTY(BlueprintReadOnly, Category = "Autopilot") float NavDistanceKm = -1.f;
	void SetAutopilot(int32 Mode, double StandoffKm, float Seconds);
	/** P: autopilot to the nav target, a warp leg or an impulse approach by range; pressed again it disengages. */
	void ToggleAutopilot();
	/** In-game console entry (` opens it): "ship nav target Sirius", "ship autopilot warp", "ship help" lists them.
	 *  The rest of the line is the command. */
	UFUNCTION(Exec) void Ship(const FString& Command);

	// ---- Shuttlecraft ----
	/** The shuttle this ship has out (null while it is in the bay). */
	UPROPERTY(BlueprintReadOnly, Category = "Shuttle") TWeakObjectPtr<AShuttlecraftPawn> ActiveShuttle;
	/** The shuttlebay door in pawn space (cm): the fantail of the engineering hull. A shuttle leaves it astern. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shuttle") FVector ShuttleBayOffset = FVector(-7250.0, 0.0, -1600.0);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shuttle") TSubclassOf<AShuttlecraftPawn> ShuttleClass;
	/** The ship must be below this speed (cm/s) and out of warp to launch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shuttle") double ShuttleLaunchMaxSpeed = 5.0e4;
	/** C: on the starship, launch the shuttle and fly it; on the shuttle, dock when the bay is in range. */
	UFUNCTION(BlueprintCallable, Category = "Shuttle") virtual void ToggleShuttle();
	UFUNCTION(BlueprintCallable, Category = "Shuttle") bool LaunchShuttle();
	/** The shuttle has slid back into the bay: control returns here and the shuttle is removed. */
	void OnShuttleDocked(AShuttlecraftPawn* Shuttle);
	/** The bay door in world space, X pointing the way a shuttle leaves (astern). */
	FTransform GetShuttleBayTransform() const;
	virtual FString GetCraftName() const { return TEXT("U.S.S. Enterprise"); }
	virtual bool IsShuttle() const { return false; }

	// ---- On foot ----
	/** The crew member out of the craft (null while everyone is aboard). */
	UPROPERTY(BlueprintReadOnly, Category = "Person") TWeakObjectPtr<APersonPawn> ActivePerson;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Person") TSubclassOf<APersonPawn> PersonClass;
	/** F: open the airlock and step out onto the hull (or, from a landed shuttle, onto the ground). */
	UFUNCTION(BlueprintCallable, Category = "Person") bool StepOutside();
	/** Whether the craft can let someone out now, and why not. */
	virtual bool CanStepOutside(FString& Reason) const;
	/** Where a person appears: a standing transform (Z up off the surface). The starship's is a hatch on the
	 *  saucer top, found by tracing the Boots channel down onto the hull. */
	virtual FTransform GetAirlockTransform() const;
	/** Whether the person starts with the boots on this hull (the starship) or beside/below the craft (the shuttle). */
	virtual bool PersonStartsOnHull() const { return true; }
	/** The person is back through the airlock: control returns here and the person is removed. */
	void OnPersonBoarded(APersonPawn* Person);

	/** Brightness of the star sphere, 0..1: the game mode fades it into a daylit sky. */
	void SetStarFieldFade(float Fade);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Defence")
	TObjectPtr<UShieldComponent> Shield;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Defence")
	TObjectPtr<UTractorBeamComponent> TractorBeam;

	/** Blinking navigation and formation lights on the hull. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|FX")
	TObjectPtr<URunningLightsComponent> RunningLights;

	/** Reaction control thrusters: nozzle packages on the saucer rim, round the deflector and on the nacelle caps. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|FX")
	TObjectPtr<URcsThrusterComponent> Thrusters;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defence") float MaxShieldStrength = 100.f;
	UPROPERTY(BlueprintReadOnly, Category = "Defence") float ShieldStrength = 100.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defence") float ShieldRechargePerSecond = 5.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defence") float ShieldRechargeDelay = 4.f;
	UPROPERTY(BlueprintReadOnly, Category = "Defence") float HullIntegrity = 100.f;

	UFUNCTION(BlueprintPure, Category = "Starship|Defence") float GetShieldFraction() const { return MaxShieldStrength > 0.f ? ShieldStrength / MaxShieldStrength : 0.f; }
	UFUNCTION(BlueprintPure, Category = "Starship|Defence") float GetHullFraction() const { return HullIntegrity / 100.f; }

	/** Engage the tractor beam on the locked target (else the nav target), or release it. */
	UFUNCTION(BlueprintCallable, Category = "Starship|Defence") void ToggleTractorBeam();
	/** Start a scan of the locked target, else the nav target (the ground under a landed craft); cancels a running one. */
	UFUNCTION(BlueprintCallable, Category = "Starship|Science") void ToggleScan();
	/** A line in the on-screen message stack (newest on top). */
	void ScreenMessage(const FString& Text, FColor Color, float Seconds = 6.f) const;
	virtual UPawnMovementComponent* GetMovementComponent() const override;

	// ---- Components ----

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> CollisionBox;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> ShipMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStarshipMovementComponent> Movement;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UWarpDriveComponent> WarpDrive;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStarshipWeaponsComponent> Weapons;

	/** Science sensors: scans of the nav or locked target into the science log. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UScienceScannerComponent> Scanner;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Cameras")
	TObjectPtr<USpringArmComponent> ChaseArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Cameras")
	TObjectPtr<UCameraComponent> ChaseCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Cameras")
	TObjectPtr<UCameraComponent> BridgeCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Cameras")
	TObjectPtr<USceneComponent> OrbitPivot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Cameras")
	TObjectPtr<UCameraComponent> CinematicCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|FX")
	TObjectPtr<UWarpStreaksComponent> WarpStreaks;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|FX")
	TObjectPtr<UPostProcessComponent> PostProcess;

	/** Star field that follows the ship (fixed orientation) so distant systems are never hidden behind a level sky mesh. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|FX")
	TObjectPtr<UStaticMeshComponent> StarSphere;

	/** Daylight inside an atmosphere: the sky outshines the stars, so the game mode hides the star field. */
	void SetStarFieldHidden(bool bHide);

	/** Stand-in sprites for stars beyond the star sphere (the real sun from across the system). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|FX")
	TObjectPtr<UDistantStarsComponent> DistantStars;

	/** Procedural bridge set around the bridge camera; only visible in the bridge view. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Cameras")
	TObjectPtr<UBridgeInteriorComponent> BridgeInterior;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|FX")
	TArray<TObjectPtr<UPointLightComponent>> HullLights;

	// ---- Hull ----

	/** Real-world length of the ship. The mesh is scaled so its longest axis matches this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull")
	float ShipLengthMeters = 305.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull")
	float ShipWidthMeters = 142.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull")
	float ShipHeightMeters = 72.f;

	/** Rotation applied to the mesh so its bow points down +X and its dorsal side is +Z. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull")
	FRotator MeshRelativeRotation = FRotator::ZeroRotator;

	/** Extra offset applied to the mesh after centring its bounds on the root (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull")
	FVector MeshCenterOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull")
	bool bAutoScaleMesh = true;

	/** Scalar parameter on the hull materials that drives the emissive window / grille glow. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull")
	FName GlowParameterName = TEXT("EmissiveColorMapWeight");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull")
	float BaseGlowWeight = 1.0f;

	/**
	 * Per material slot multiplier on BaseGlowWeight. The hull plating is not emissive at all; the nacelle slot
	 * (bussards and grilles) runs hot. Slots not listed use 1.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull")
	TMap<FName, float> SlotGlowWeights;

	/** Material slot whose emissive is driven by impulse throttle (the impulse exhaust grille). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull")
	FName ThrusterSlotName = TEXT("Thruster");

	/**
	 * Overlay material that draws procedural lit windows on the hull (saucer rim and engineering hull flanks).
	 * The zones below are in pawn space; the mesh's own transform is passed to the material.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull|Windows")
	TSoftObjectPtr<UMaterialInterface> HullWindowMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull|Windows") FVector WindowSaucerCenter = FVector(8050.0, 0.0, 250.0);
	/** Measure the saucer rim's height and thickness from the mesh (overrides the Z and thickness below). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull|Windows") bool bAutoSaucerRim = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull|Windows") float WindowSaucerRadius = 7190.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull|Windows") float WindowSaucerThickness = 450.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull|Windows") float WindowHullMaxY = 1750.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull|Windows") float WindowHullMaxZ = 400.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull|Windows") float WindowHullMinZ = -1700.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull|Windows") float WindowHullMinX = -7000.f;
	/** Forward limit of the engineering-hull window zone: stops short of the deflector housing and dish. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull|Windows") float WindowHullMaxX = 2200.f;
	/** Keep-out boxes in pawn space: no windows on the deflector housing and dish, nor on the torpedo bay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull|Windows") FVector WindowExcludeDeflectorMin = FVector(2000.0, -1900.0, -3700.0);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull|Windows") FVector WindowExcludeDeflectorMax = FVector(6500.0, 1900.0, 900.0);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull|Windows") FVector WindowExcludeTorpedoMin = FVector(2600.0, -700.0, -700.0);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull|Windows") FVector WindowExcludeTorpedoMax = FVector(3500.0, 700.0, 200.0);
	/** Emissive patches drawn by the overlay (pawn space): deflector dish, torpedo bay, and the impulse exhaust when reversing. */
	/** The dish face: x ~3500..3800, radius ~950 about z -1950 (pawn space, from the mesh). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull|Glow") FVector DeflectorPatchMin = FVector(3300.0, -1100.0, -3000.0);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull|Glow") FVector DeflectorPatchMax = FVector(3950.0, 1100.0, -850.0);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull|Glow") FLinearColor DeflectorGlowColor = FLinearColor(0.45f, 0.8f, 1.0f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull|Glow") float DeflectorGlow = 12.f;
	/**
	 * The torpedo launcher: the small housing at the foot of the neck directly above the deflector dish, facing
	 * forward (pawn space x ~2850..3250, |y| < 350, z -450..-50, from the mesh and confirmed with `slotbox`). The
	 * dark tube mouths inside the box glow red. (An earlier box under the saucer centre had lit panes on the lower
	 * sensor dome instead.)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull|Glow") FVector TorpedoPatchMin = FVector(2850.0, -350.0, -450.0);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull|Glow") FVector TorpedoPatchMax = FVector(3250.0, 350.0, -50.0);
	/**
	 * The two tube doors on the launcher face, measured from the mesh: recessed panes mirrored about the centreline
	 * at |y| = Centre.Y, z = Centre.Z, lying in the plane x = Centre.X; Size = (half width, half height, tube mouth
	 * radius) in cm. The skin material lights the panes and the weapons component fires from the mouths.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull|Glow") FVector TorpedoTubeCentre = FVector(3069.0, 281.0, -323.0);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull|Glow") FVector TorpedoTubeSize = FVector(96.0, 39.0, 32.0);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull|Glow") FLinearColor TorpedoBayGlowColor = FLinearColor(1.0f, 0.12f, 0.05f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull|Glow") float TorpedoBayGlow = 30.f;
	/** Impulse grille emissive: idle, plus this much at full throttle (sunlit white plating is about 40 in these units). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull|Glow") float ImpulseGlowIdle = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull|Glow") float ImpulseGlowFull = 30.f;
	/** Patches only light faces turned toward their direction at least this much (cosine): keeps the torpedo glow off the neck and saucer underside. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull|Glow") float DeflectorFacing = 0.35f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull|Glow") float TorpedoFacing = 0.7f;
	/**
	 * The hull's own material: the imported diffuse / glow / normal maps, plus the saturated blue and red details
	 * of the diffuse map (dome, dish, blue window strips, red lamps at the shuttle bay) made emissive. The imported
	 * slot materials only provide the textures.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull|Glow")
	TSoftObjectPtr<UMaterialInterface> HullSkinMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull|Glow") float BlueKeyGlow = 12.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull|Glow") float RedKeyGlow = 10.f;
	/** Red in the diffuse map is only a lamp in these pawn-space boxes (the stern by the shuttle bay, the torpedo bay); elsewhere it is paint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull|Glow") FVector RedKeyTailMin = FVector(-8000.0, -2000.0, -4000.0);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull|Glow") FVector RedKeyTailMax = FVector(-4800.0, 2000.0, 1800.0);
	/** In reverse the impulse grille's glow and plating are recoloured from orange-red to these. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull|Glow") FLinearColor ReverseGlowColor = FLinearColor(0.4f, 0.7f, 1.0f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull|Glow") FLinearColor ReverseBaseTint = FLinearColor(0.4f, 0.55f, 0.9f);
	/** Bridge module windows: a ring of small panes round the bridge, radius and height band in pawn space (radius 0 = none). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull|Windows") float BridgeWindowRadius = 900.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull|Windows") float BridgeWindowMinZ = 3150.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull|Windows") float BridgeWindowMaxZ = 3330.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull|Windows") float WindowPitch = 700.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull|Windows") float WindowDeck = 460.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull|Windows") float WindowGlow = 22.f;
	/** Fraction of windows lit at any moment, and how long a window keeps its state before re-rolling (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull|Windows") float WindowOnFraction = 0.4f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull|Windows") float WindowPeriod = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hull")
	TArray<FStarshipLightSpec> LightSpecs;

	// ---- Cameras ----

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cameras")
	float ChaseDistanceFactor = 1.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cameras")
	float ChaseHeightFactor = 0.22f;

	/** Multiplier on the chase distance set with the mouse wheel while free-looking; R resets it to 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera", meta = (ClampMin = "0.1", ClampMax = "6.0"))
	float ChaseZoom = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") float MinChaseZoom = 0.3f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera") float MaxChaseZoom = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cameras")
	float ChaseFOV = 80.f;

	/** Bridge camera position relative to the ship root (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cameras")
	FVector BridgeCameraOffset = FVector(6500.0, 0.0, 2900.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cameras")
	float BridgeFOV = 92.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cameras")
	float CinematicDistanceFactor = 2.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cameras")
	float CinematicOrbitRate = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cameras")
	float CinematicFOV = 60.f;

	/** How fast free-look springs back to centre when released (1/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cameras")
	float FreeLookReturnRate = 4.f;

	// ---- Sky ----

	/** Radius of the ship-following star sphere (cm). 5 million km by default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky")
	double StarSphereRadius = 5.0e11;
	bool bStarFieldHidden = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sky")
	TSoftObjectPtr<UMaterialInterface> StarMaterial;

	// ---- Exposure ----

	/** Lock auto-exposure to this EV100 so a black sky never blows the hull out. Set <= -100 to leave exposure automatic. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Exposure")
	float LockedExposureEV = 5.5f;

	// ---- Debug ----

	/**
	 * Remote-control hook for automated testing: set this (e.g. through the editor's object tools while
	 * PIE runs) and the pawn executes it on the next tick, then clears it. Commands:
	 *   throttle <-0.25..1> | impulse <1-4> | stop | warp | warp on | warp off | warpfactor <f>
	 *   phasers on|off | torpedo | target | camera chase|bridge|cinematic | steer <pitch> <yaw> <roll> <seconds>
	 *   look <yaw> <pitch> <seconds> (free-look the active camera) | vert <up -1..1> <seconds> | orbit [on|off]
	 *   rcs <fwd> <right> <up> <seconds> (manoeuvring thrusters)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	FString DebugCommand;

	// ---- Warp FX ----

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp FX")
	EWarpVisualMode WarpVisualMode = EWarpVisualMode::Classic;

	/** Post-process material driving both warp looks (parameters: Mode, Strength, ForwardUV, Behind, ...). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp FX")
	TSoftObjectPtr<UMaterialInterface> WarpPostProcessMaterial;

	/** How hard the classic look streaks the scene toward the vanishing point (0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp FX")
	float ClassicStreakStrength = 0.6f;

	/** Relativistic aberration amount (0..1; 1 = the physical mapping for the effective beta). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp FX")
	float RelativisticContraction = 1.0f;

	/** Relativistic Doppler tint amount (0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp FX")
	float RelativisticDoppler = 0.85f;

	/**
	 * Above c there is no rest frame to transform into, so the relativistic look uses an effective speed
	 * beta = v / (v + c): 0.5 at warp 1, 0.79 at warp 1.5, 0.91 at warp 2, then on towards 1. This caps it so
	 * the sky does not collapse into a dot at high factors.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp FX", meta = (ClampMin = "0.0", ClampMax = "0.995"))
	float RelativisticMaxBeta = 0.93f;
	/** The beta clamp at full transwarp (warp 9.99): the forward crush tightens from warp 9.9's ring to a knot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Starship|Warp Look")
	float RelativisticTranswarpMaxBeta = 0.99f;
	/** How much stronger the classic view's colour fringe is at full transwarp (1 = double). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Starship|Warp Look")
	float TranswarpFringeBoost = 1.5f;
	/** 0..1 transwarp this frame (warp factor above the drive's TranswarpStart while at warp), for the effects. */
	float TranswarpFrac = 0.f;

	UFUNCTION(BlueprintCallable, Category = "Starship|Warp")
	void SetWarpVisualMode(EWarpVisualMode Mode) { WarpVisualMode = Mode; }

	UFUNCTION(BlueprintCallable, Category = "Starship|Warp")
	void ToggleWarpVisualMode() { WarpVisualMode = WarpVisualMode == EWarpVisualMode::Classic ? EWarpVisualMode::Relativistic : EWarpVisualMode::Classic; }

	UFUNCTION(BlueprintPure, Category = "Starship|Warp")
	EWarpVisualMode GetWarpVisualMode() const { return WarpVisualMode; }

	/**
	 * Scale on the speed of light used by the relativistic view of other ships' warp trips (1 = real). At the real
	 * value the retarded images of a nearby trip fly off at about c and are gone within a frame; around 1e-5 the
	 * splitting and time-reversed images can be watched. Other ships' warp speeds scale with it so the trips stay
	 * superluminal in the same ratio.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp")
	float WarpVisualLightSpeedScale = 1.f;

	UFUNCTION(BlueprintPure, Category = "Starship|Warp")
	float GetWarpVisualLightSpeedScale() const { return WarpVisualLightSpeedScale; }

	/** Ship position in universe space (world space plus the floating-origin offset). */
	UFUNCTION(BlueprintPure, Category = "Starship")
	FVector GetUniversePosition() const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp FX")
	float WarpFovBoost = 16.f;

	/** Extra chase-arm length at warp, as a fraction of the normal arm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp FX")
	float WarpArmLengthBoost = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp FX")
	float WarpChromaticAberration = 3.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp FX")
	float WarpFlashBloom = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp FX")
	FLinearColor WarpFlashTint = FLinearColor(0.75f, 0.85f, 1.3f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp FX")
	float WarpRumble = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Warp FX")
	float ShakeDecayRate = 3.f;

	// ---- API ----

	UFUNCTION(BlueprintCallable, Category = "Starship|Camera")
	virtual void SetCameraMode(EStarshipCameraMode Mode);

	UFUNCTION(BlueprintCallable, Category = "Starship|Camera")
	void CycleCameraMode();

	UFUNCTION(BlueprintPure, Category = "Starship|Camera")
	EStarshipCameraMode GetCameraMode() const { return CameraMode; }

	UFUNCTION(BlueprintCallable, Category = "Starship|Camera")
	void SetFreeLook(bool bEnabled) { bFreeLook = bEnabled; }

	UFUNCTION(BlueprintPure, Category = "Starship|Camera")
	bool IsFreeLooking() const { return bFreeLook; }

	/** Yaw/pitch in degrees to add to the free-look camera. */
	UFUNCTION(BlueprintCallable, Category = "Starship|Camera")
	void AddLookInput(float YawDegrees, float PitchDegrees);

	UFUNCTION(BlueprintCallable, Category = "Starship|Camera")
	void ResetCamera();

	/** Wheel zoom for the chase camera: positive steps bring it closer. */
	UFUNCTION(BlueprintCallable, Category = "Starship|Camera")
	void AdjustChaseZoom(float Steps);

	UFUNCTION(BlueprintPure, Category = "Starship|Camera")
	UCameraComponent* GetActiveCamera() const;

	/** Teleport the ship to a safe standoff from an actor (a few radii sunward), facing it, at rest. */
	UFUNCTION(BlueprintCallable, Category = "Starship")
	void JumpToActor(AActor* Target, double TargetRadiusCm, float StandoffRadii = 3.f);

	/** Standoff for jumps to a star, in star radii: close enough that the photosphere fills most of the view. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Starship")
	float StarJumpStandoffRadii = 1.5f;

	/** Jump to the HUD's nav target (cycling to the next body first if none is set). */
	UFUNCTION(BlueprintCallable, Category = "Starship")
	void JumpToNavTarget();

	/** Hold a circular orbit around the nav target (or the nearest planet/star when none is set); call again to release. */
	UFUNCTION(BlueprintCallable, Category = "Starship")
	void ToggleOrbitHold();

	UFUNCTION(BlueprintCallable, Category = "Starship")
	void SetOrbitHold(bool bEnable);

	/** Kick the camera (0..1 is a sensible range; stacks). */
	UFUNCTION(BlueprintCallable, Category = "Starship|Camera")
	void AddCameraShake(float Amplitude);

	UFUNCTION(BlueprintPure, Category = "Starship")
	float GetShipLengthCm() const { return ShipLengthMeters * 100.f; }

	/** True while a debug "steer" command is driving the ship (the controller leaves steering alone). */
	UFUNCTION(BlueprintPure, Category = "Starship")
	/** The controller leaves the steering alone while a debug steer or the autopilot owns it. */
	bool IsDebugSteering() const { return DebugSteerTime > 0.f || AutopilotState != 0; }

	UFUNCTION(BlueprintPure, Category = "Starship")
	bool IsDebugVertical() const { return DebugVerticalTime > 0.f; }

	UFUNCTION(BlueprintPure, Category = "Starship")
	bool IsDebugThrusting() const { return DebugThrustTime > 0.f; }

protected:
	UFUNCTION()
	void HandleCollision(const FHitResult& Hit);

	/** Fittings a different hull lays out its own way (the shuttle overrides these). */
	virtual void SetupRunningLights();
	/** Lay the reaction control nozzles out on the hull mesh. */
	virtual void SetupThrusters();
	/** Lay the phaser banks out on the hull mesh. */
	virtual void SetupWeaponBanks();
	/** First look at a console op; true when handled (the shuttle takes "dock" here). */
	virtual bool HandleCraftCommand(const FString& Op, const TArray<FString>& Args);

private:
	void ApplyHullDimensions();
	void SetupHullMaterials();
	void SetupHullLights();
	/** Feed this frame's steering and translation to the thruster FX. */
	void UpdateThrusters();
	/** Cache the hull's LOD0 vertices in pawn space for the probes below. */
	void GatherHullVerts();
	/** The hull vertex furthest along Dir inside Region (pawn space), a hair outside the skin; Fallback when the mesh is not CPU-readable. */
	FVector ProbeHull(const FVector& Dir, const FBox& Region, const FVector& Fallback) const;
	/**
	 * Move a fitting onto the nearest hull vertex within Radius whose plating faces roughly along Dir, and cant Dir
	 * out of that plating so a nozzle never fires into or along the skin. False (inputs untouched) when no vertex is near.
	 */
	bool SnapToHull(FVector& Pos, FVector& Dir, float Radius) const;
	void UpdateCameras(float DeltaTime);
	void UpdateWarpEffects(float DeltaTime);
	void UpdateHullGlow(float DeltaTime);
	void AttachStreaksToActiveCamera();
	void ProcessDebugCommand();
	void ApplyExposure();

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> HullMaterials;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> WarpPPMID;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> HullWindowsMID;
	void ApplyHullWindows(float Scale, const FQuat& Rot, const FVector& RelLoc);
	/** Mesh->pawn transform, key glows and boxes on the skin material instances (needs the instances, so after SetupHullMaterials). */
	void ApplyHullSkinParams();
	/** Debug: write the hull mesh's LOD0 vertices (pawn space, with material slot) to Saved/meshdump.csv. */
	void DumpHullMesh() const;

	float LastStreak = 0.f;
	float LastFlash = 0.f;
	/** Ticks after the cameras have settled (TG_PostUpdateWork) and calls UpdateWarpPostProcess. */
	UPROPERTY() TObjectPtr<UWarpViewComponent> WarpView;

public:
	/** Pushes the camera-dependent warp post-process parameters; runs from WarpView once the camera transforms are final for the frame. */
	void UpdateWarpPostProcess();

private:
	float ShieldRechargeTimer = 0.f;

	int32 ThrusterMaterialIndex = INDEX_NONE;
	EStarshipCameraMode CameraMode = EStarshipCameraMode::Chase;
	bool bFreeLook = false;
	FVector2D FreeLookAngles = FVector2D::ZeroVector; // (Yaw, Pitch)
	float ShakeAmplitude = 0.f;
	float ShakeTime = 0.f;
	float OrbitAngle = 0.f;
	float ChaseArmBaseLength = 0.f;
	FVector ChaseArmBaseSocketOffset = FVector::ZeroVector;
	int32 LastTorpedoesFired = 0;
	FVector DebugSteer = FVector::ZeroVector;
	float DebugSteerTime = 0.f;
	double AutopilotStandoffCm = 0.0;
	float AutopilotSeconds = 5.f;
	bool bAutopilotWarpEngaged = false;
	FVector2D AutopilotPrevError = FVector2D::ZeroVector;
	float AutopilotLogTimer = 0.f;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> StarSphereMID;
	float StarFieldFade = 1.f;
	void TickAutopilot(float DeltaTime);
	FVector2D DebugLook = FVector2D::ZeroVector;
	float DebugLookTime = 0.f;
	float DebugVertical = 0.f;
	float DebugVerticalTime = 0.f;
	FVector DebugThrust = FVector::ZeroVector;
	float DebugThrustTime = 0.f;
	TArray<FVector> HullVerts;
	TArray<FVector> HullNormals;
};
