#include "StarshipPawn.h"

#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "StarshipHUD.h"
#include "PlanetBody.h"
#include "StarBody.h"
#include "SpaceVessel.h"
#include "EngineUtils.h"
#include "SlonkGameMode.h"
#include "FloatingOriginComponent.h"
#include "Engine/BlendableInterface.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/SpringArmComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "StarshipMovementComponent.h"
#include "StarshipWeaponsComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "WarpDriveComponent.h"
#include "WarpStreaksComponent.h"
#include "BridgeInteriorComponent.h"
#include "DistantStarsComponent.h"

AStarshipPawn::AStarshipPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics; // after movement, before cameras render
	SpawnCollisionHandlingMethod = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;
	BaseEyeHeight = 0.f;

	CollisionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("CollisionBox"));
	SetRootComponent(CollisionBox);
	CollisionBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CollisionBox->SetCollisionObjectType(ECC_Pawn);
	CollisionBox->SetCollisionResponseToAllChannels(ECR_Block);
	CollisionBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	CollisionBox->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	CollisionBox->SetGenerateOverlapEvents(false);
	CollisionBox->SetCanEverAffectNavigation(false);

	ShipMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ShipMesh"));
	ShipMesh->SetupAttachment(CollisionBox);
	ShipMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ShipMesh->SetGenerateOverlapEvents(false);
	ShipMesh->SetMobility(EComponentMobility::Movable);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> HullFinder(TEXT("/Game/Enterprise/1701-a.1701-a"));
	if (HullFinder.Succeeded())
	{
		ShipMesh->SetStaticMesh(HullFinder.Object);
	}

	Movement = CreateDefaultSubobject<UStarshipMovementComponent>(TEXT("Movement"));
	Movement->SetUpdatedComponent(CollisionBox);

	WarpDrive = CreateDefaultSubobject<UWarpDriveComponent>(TEXT("WarpDrive"));
	Weapons = CreateDefaultSubobject<UStarshipWeaponsComponent>(TEXT("Weapons"));

	// Chase camera on a spring arm. Location lag is deliberately off: at impulse speeds the lag
	// distance would already be kilometres; rotation lag gives the weighty feel instead.
	ChaseArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("ChaseArm"));
	ChaseArm->SetupAttachment(CollisionBox);
	ChaseArm->bDoCollisionTest = false;
	ChaseArm->bUsePawnControlRotation = false;
	ChaseArm->bInheritPitch = true;
	ChaseArm->bInheritYaw = true;
	ChaseArm->bInheritRoll = true;
	ChaseArm->bEnableCameraLag = false;
	ChaseArm->bEnableCameraRotationLag = true;
	ChaseArm->CameraRotationLagSpeed = 4.f;
	ChaseArm->CameraLagMaxTimeStep = 1.f / 30.f;

	ChaseCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ChaseCamera"));
	ChaseCamera->SetupAttachment(ChaseArm, USpringArmComponent::SocketName);
	ChaseCamera->bUsePawnControlRotation = false;
	ChaseCamera->SetFieldOfView(ChaseFOV);

	BridgeCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("BridgeCamera"));
	BridgeCamera->SetupAttachment(CollisionBox);
	BridgeCamera->bUsePawnControlRotation = false;
	BridgeCamera->SetFieldOfView(BridgeFOV);
	BridgeCamera->SetActive(false);

	BridgeInterior = CreateDefaultSubobject<UBridgeInteriorComponent>(TEXT("BridgeInterior"));
	BridgeInterior->SetupAttachment(CollisionBox);
	BridgeInterior->SetVisibility(false, true);

	DistantStars = CreateDefaultSubobject<UDistantStarsComponent>(TEXT("DistantStars"));
	DistantStars->SetupAttachment(CollisionBox);

	OrbitPivot = CreateDefaultSubobject<USceneComponent>(TEXT("OrbitPivot"));
	OrbitPivot->SetupAttachment(CollisionBox);
	OrbitPivot->SetUsingAbsoluteRotation(true); // orbit in world space so the ship's own roll doesn't swing the shot

	CinematicCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("CinematicCamera"));
	CinematicCamera->SetupAttachment(OrbitPivot);
	CinematicCamera->bUsePawnControlRotation = false;
	CinematicCamera->SetFieldOfView(CinematicFOV);
	CinematicCamera->SetActive(false);

	WarpStreaks = CreateDefaultSubobject<UWarpStreaksComponent>(TEXT("WarpStreaks"));
	WarpStreaks->SetupAttachment(ChaseCamera);

	PostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("PostProcess"));
	PostProcess->SetupAttachment(CollisionBox);
	PostProcess->bUnbound = true;
	PostProcess->BlendWeight = 1.f;
	PostProcess->Priority = 10.f;

	StarSphere = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StarSphere"));
	StarSphere->SetupAttachment(CollisionBox);
	StarSphere->SetUsingAbsoluteRotation(true);
	StarSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	StarSphere->SetGenerateOverlapEvents(false);
	StarSphere->CastShadow = false;
	StarSphere->bCastDynamicShadow = false;
	StarSphere->SetReceivesDecals(false);
	StarSphere->bAffectDistanceFieldLighting = false;
	StarSphere->bAffectDynamicIndirectLighting = false;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SkyFinder(TEXT("/Engine/EngineSky/SM_SkySphere.SM_SkySphere"));
	if (SkyFinder.Succeeded())
	{
		StarSphere->SetStaticMesh(SkyFinder.Object);
	}
	StarMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Slonk/Materials/M_Stars.M_Stars")));
	WarpPostProcessMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Slonk/Materials/M_PP_Warp.M_PP_Warp")));

	// Default hull lights for the Constitution refit, relative to the hull centre. Tune in the editor.
	auto AddLight = [this](const TCHAR* Name, FVector Offset, FLinearColor Color, float Intensity, float Radius, bool bWarp, bool bImpulse)
	{
		FStarshipLightSpec Spec;
		Spec.Name = Name;
		Spec.Offset = Offset;
		Spec.Color = Color;
		Spec.Intensity = Intensity;
		Spec.AttenuationRadius = Radius;
		Spec.bWarpLight = bWarp;
		Spec.bImpulseLight = bImpulse;
		LightSpecs.Add(Spec);
	};
	const FLinearColor WarpBlue(0.35f, 0.6f, 1.f);
	const FLinearColor BussardRed(1.f, 0.25f, 0.08f);
	const FLinearColor ImpulseOrange(1.f, 0.45f, 0.15f);
	AddLight(TEXT("NacelleGrillePort"), FVector(-7500.0, -3700.0, 2700.0), WarpBlue, 60000.f, 16000.f, true, false);
	AddLight(TEXT("NacelleGrilleStarboard"), FVector(-7500.0, 3700.0, 2700.0), WarpBlue, 60000.f, 16000.f, true, false);
	AddLight(TEXT("BussardPort"), FVector(-2600.0, -3700.0, 2700.0), BussardRed, 40000.f, 9000.f, true, false);
	AddLight(TEXT("BussardStarboard"), FVector(-2600.0, 3700.0, 2700.0), BussardRed, 40000.f, 9000.f, true, false);
	AddLight(TEXT("Deflector"), FVector(1500.0, 0.0, -1700.0), WarpBlue, 50000.f, 12000.f, true, false);
	AddLight(TEXT("ImpulseEngines"), FVector(-800.0, 0.0, 1500.0), ImpulseOrange, 30000.f, 10000.f, false, true);
}

UPawnMovementComponent* AStarshipPawn::GetMovementComponent() const
{
	// Our movement component is a UMovementComponent, not a pawn movement component.
	return nullptr;
}

void AStarshipPawn::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyHullDimensions();
}

void AStarshipPawn::ApplyHullDimensions()
{
	const float LengthCm = GetShipLengthCm();
	CollisionBox->SetBoxExtent(FVector(LengthCm * 0.5f, ShipWidthMeters * 50.f, ShipHeightMeters * 50.f));

	if (ShipMesh && ShipMesh->GetStaticMesh())
	{
		const FBoxSphereBounds Bounds = ShipMesh->GetStaticMesh()->GetBounds();
		float Scale = 1.f;
		if (bAutoScaleMesh)
		{
			const float LongestExtent = Bounds.BoxExtent.GetMax();
			if (LongestExtent > KINDA_SMALL_NUMBER)
			{
				Scale = LengthCm / (2.f * LongestExtent);
			}
		}
		const FQuat Rot = MeshRelativeRotation.Quaternion();
		ShipMesh->SetRelativeScale3D(FVector(Scale));
		ShipMesh->SetRelativeRotation(Rot);
		ShipMesh->SetRelativeLocation(-Rot.RotateVector(Bounds.Origin) * Scale + MeshCenterOffset);
	}

	// Cameras scale with the hull so the framing is the same whatever the real length is.
	// SM_SkySphere is 4096 cm in radius.
	StarSphere->SetRelativeScale3D(FVector(StarSphereRadius / 4096.0));
	StarSphere->SetWorldRotation(FRotator::ZeroRotator);

	ChaseArmBaseLength = LengthCm * ChaseDistanceFactor;
	ChaseArmBaseSocketOffset = FVector(0.0, 0.0, LengthCm * ChaseHeightFactor);
	ChaseArm->TargetArmLength = ChaseArmBaseLength;
	ChaseArm->SocketOffset = ChaseArmBaseSocketOffset;
	ChaseCamera->SetFieldOfView(ChaseFOV);

	BridgeCamera->SetRelativeLocation(BridgeCameraOffset);
	BridgeCamera->SetFieldOfView(BridgeFOV);
	// The set's origin is the deck under the captain's chair; the camera sits at the seated eye point.
	BridgeInterior->SetRelativeLocation(BridgeCameraOffset - BridgeInterior->EyeOffset);

	const float OrbitDistance = LengthCm * CinematicDistanceFactor;
	const FVector OrbitOffset(-OrbitDistance, 0.0, OrbitDistance * 0.18);
	CinematicCamera->SetRelativeLocation(OrbitOffset);
	CinematicCamera->SetRelativeRotation((-OrbitOffset).Rotation());
	CinematicCamera->SetFieldOfView(CinematicFOV);
}

void AStarshipPawn::BeginPlay()
{
	Super::BeginPlay();
	ApplyHullDimensions();
	SetupHullMaterials();
	SetupHullLights();
	SetCameraMode(CameraMode);

	if (Movement)
	{
		Movement->OnCollision.AddDynamic(this, &AStarshipPawn::HandleCollision);
	}

	// Post-process overrides we animate.
	FPostProcessSettings& S = PostProcess->Settings;
	S.bOverride_SceneFringeIntensity = true;
	S.bOverride_BloomIntensity = true;
	S.bOverride_ColorGain = true;
	S.bOverride_VignetteIntensity = true;
	S.bOverride_MotionBlurAmount = true;
	S.SceneFringeIntensity = 0.f;
	S.BloomIntensity = 0.675f;
	S.ColorGain = FVector4(1, 1, 1, 1);
	S.VignetteIntensity = 0.4f;
	S.MotionBlurAmount = 0.3f;
	ApplyExposure();

	if (UMaterialInterface* PPMat = WarpPostProcessMaterial.LoadSynchronous())
	{
		WarpPPMID = UMaterialInstanceDynamic::Create(PPMat, this);
		S.WeightedBlendables.Array.Add(FWeightedBlendable(0.f, WarpPPMID));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Starship: warp post-process material %s not found; warp visuals limited to streaks."), *WarpPostProcessMaterial.ToString());
	}
}

FVector AStarshipPawn::GetUniversePosition() const
{
	if (const UFloatingOriginComponent* Origin = ASlonkGameMode::FindFloatingOrigin(GetWorld()))
	{
		return Origin->WorldToUniverse(GetActorLocation());
	}
	return GetActorLocation();
}

void AStarshipPawn::UpdateWarpPostProcess(float Streak, float Flash)
{
	if (!WarpPPMID)
	{
		return;
	}
	FPostProcessSettings& S = PostProcess->Settings;
	for (FWeightedBlendable& B : S.WeightedBlendables.Array)
	{
		if (B.Object == WarpPPMID)
		{
			B.Weight = FMath::Clamp(Streak, 0.f, 1.f);
		}
	}
	if (Streak <= 0.001f)
	{
		return;
	}

	const bool bRelativistic = WarpVisualMode == EWarpVisualMode::Relativistic;
	const FVector Travel = (Movement && Movement->Velocity.SizeSquared() > 1.0) ? Movement->Velocity.GetSafeNormal() : GetActorForwardVector();

	// Where the direction of travel lands on screen (viewport UV). If it is behind the camera, use the
	// anti-direction and flag it so the shader pushes outward instead of pulling inward.
	float ForwardU = 0.5f, ForwardV = 0.5f, Behind = 0.f;
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		FVector CamLoc;
		FRotator CamRot;
		PC->GetPlayerViewPoint(CamLoc, CamRot);
		const float Facing = FVector::DotProduct(CamRot.Vector(), Travel);
		const FVector Dir = Facing >= 0.f ? Travel : -Travel;
		Behind = Facing >= 0.f ? 0.f : 1.f;
		FVector2D Screen;
		int32 SizeX = 0, SizeY = 0;
		PC->GetViewportSize(SizeX, SizeY);
		if (SizeX > 0 && SizeY > 0 && PC->ProjectWorldLocationToScreen(CamLoc + Dir * 1.0e9, Screen, true))
		{
			ForwardU = Screen.X / SizeX;
			ForwardV = Screen.Y / SizeY;
		}
		else
		{
			// Exactly sideways: pick the edge the travel direction points to.
			const FVector Right = CamRot.RotateVector(FVector::RightVector);
			const FVector Up = CamRot.RotateVector(FVector::UpVector);
			ForwardU = 0.5f + 0.5f * FVector::DotProduct(Right, Dir) * 3.f;
			ForwardV = 0.5f - 0.5f * FVector::DotProduct(Up, Dir) * 3.f;
		}
	}

	const double Speed = Movement ? Movement->GetSpeed() : 0.0;
	const float Beta = (float)FMath::Clamp(Speed / 2.99792458e10, 0.0, 1.0);

	WarpPPMID->SetScalarParameterValue(TEXT("Mode"), bRelativistic ? 1.f : 0.f);
	WarpPPMID->SetScalarParameterValue(TEXT("Strength"), Streak);
	WarpPPMID->SetScalarParameterValue(TEXT("Flash"), Flash);
	WarpPPMID->SetScalarParameterValue(TEXT("Behind"), Behind);
	WarpPPMID->SetScalarParameterValue(TEXT("Beta"), FMath::Max(Beta, Streak * 0.9f));
	WarpPPMID->SetScalarParameterValue(TEXT("BlurStrength"), ClassicStreakStrength);
	WarpPPMID->SetScalarParameterValue(TEXT("ContractStrength"), RelativisticContraction);
	WarpPPMID->SetScalarParameterValue(TEXT("DopplerStrength"), RelativisticDoppler);
	WarpPPMID->SetVectorParameterValue(TEXT("ForwardUV"), FLinearColor(ForwardU, ForwardV, 0.f, 0.f));
}

void AStarshipPawn::ApplyExposure()
{
	FPostProcessSettings& S = PostProcess->Settings;
	const bool bLock = LockedExposureEV > -100.f;
	S.bOverride_AutoExposureMinBrightness = bLock;
	S.bOverride_AutoExposureMaxBrightness = bLock;
	S.AutoExposureMinBrightness = LockedExposureEV;
	S.AutoExposureMaxBrightness = LockedExposureEV;
}

void AStarshipPawn::SetupHullMaterials()
{
	if (UMaterialInterface* Stars = StarMaterial.LoadSynchronous())
	{
		StarSphere->SetMaterial(0, Stars);
	}
	else
	{
		StarSphere->SetVisibility(false);
	}
	HullMaterials.Reset();
	ThrusterMaterialIndex = INDEX_NONE;
	if (!ShipMesh || !ShipMesh->GetStaticMesh())
	{
		return;
	}
	const int32 NumMaterials = ShipMesh->GetNumMaterials();
	for (int32 i = 0; i < NumMaterials; ++i)
	{
		UMaterialInstanceDynamic* Mid = ShipMesh->CreateAndSetMaterialInstanceDynamic(i);
		HullMaterials.Add(Mid);
	}
	ThrusterMaterialIndex = ShipMesh->GetMaterialIndex(ThrusterSlotName);
}

void AStarshipPawn::SetupHullLights()
{
	for (UPointLightComponent* L : HullLights)
	{
		if (L)
		{
			L->DestroyComponent();
		}
	}
	HullLights.Reset();

	for (const FStarshipLightSpec& Spec : LightSpecs)
	{
		UPointLightComponent* Light = NewObject<UPointLightComponent>(this, Spec.Name.IsNone() ? NAME_None : Spec.Name);
		if (!Light)
		{
			continue;
		}
		Light->SetupAttachment(CollisionBox);
		Light->SetRelativeLocation(Spec.Offset);
		Light->SetIntensityUnits(ELightUnits::Candelas);
		Light->SetIntensity(Spec.Intensity);
		Light->SetAttenuationRadius(Spec.AttenuationRadius);
		Light->SetLightColor(Spec.Color);
		Light->CastShadows = false;
		Light->SetMobility(EComponentMobility::Movable);
		Light->RegisterComponent();
		HullLights.Add(Light);
	}
}

void AStarshipPawn::HandleCollision(const FHitResult& Hit)
{
	AddCameraShake(0.8f);
}

void AStarshipPawn::AddCameraShake(float Amplitude)
{
	ShakeAmplitude = FMath::Clamp(ShakeAmplitude + Amplitude, 0.f, 2.f);
}

UCameraComponent* AStarshipPawn::GetActiveCamera() const
{
	switch (CameraMode)
	{
	case EStarshipCameraMode::Bridge: return BridgeCamera;
	case EStarshipCameraMode::Cinematic: return CinematicCamera;
	default: return ChaseCamera;
	}
}

void AStarshipPawn::SetCameraMode(EStarshipCameraMode Mode)
{
	CameraMode = Mode;
	ChaseCamera->SetActive(Mode == EStarshipCameraMode::Chase);
	BridgeCamera->SetActive(Mode == EStarshipCameraMode::Bridge);
	CinematicCamera->SetActive(Mode == EStarshipCameraMode::Cinematic);
	// Hide the hull from the bridge so the saucer doesn't fill the lens.
	ShipMesh->SetOwnerNoSee(false);
	ShipMesh->SetVisibility(Mode != EStarshipCameraMode::Bridge);
	BridgeInterior->SetVisibility(Mode == EStarshipCameraMode::Bridge, true);
	AttachStreaksToActiveCamera();
}

void AStarshipPawn::CycleCameraMode()
{
	switch (CameraMode)
	{
	case EStarshipCameraMode::Chase: SetCameraMode(EStarshipCameraMode::Bridge); break;
	case EStarshipCameraMode::Bridge: SetCameraMode(EStarshipCameraMode::Cinematic); break;
	default: SetCameraMode(EStarshipCameraMode::Chase); break;
	}
}

void AStarshipPawn::AttachStreaksToActiveCamera()
{
	if (UCameraComponent* Cam = GetActiveCamera())
	{
		WarpStreaks->AttachToComponent(Cam, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		WarpStreaks->SetRelativeLocation(FVector::ZeroVector);
	}
}

void AStarshipPawn::AddLookInput(float YawDegrees, float PitchDegrees)
{
	FreeLookAngles.X = FMath::Clamp(FreeLookAngles.X + YawDegrees, -170.f, 170.f);
	FreeLookAngles.Y = FMath::Clamp(FreeLookAngles.Y + PitchDegrees, -80.f, 80.f);
}

void AStarshipPawn::ResetCamera()
{
	FreeLookAngles = FVector2D::ZeroVector;
	SetCameraMode(EStarshipCameraMode::Chase);
}

void AStarshipPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	ProcessDebugCommand();
	ApplyExposure();
	if (DebugSteerTime > 0.f && Movement)
	{
		DebugSteerTime -= DeltaTime;
		// Movement ticks before this (pre-physics); set the input it will consume next frame.
		Movement->SetSteeringInput(DebugSteer.X, DebugSteer.Y, DebugSteer.Z);
	}
	if (DebugLookTime > 0.f)
	{
		DebugLookTime -= DeltaTime;
		FreeLookAngles = DebugLook;
	}
	if (DebugVerticalTime > 0.f && Movement)
	{
		DebugVerticalTime -= DeltaTime;
		Movement->SetVerticalInput(DebugVertical);
	}
	UpdateWarpEffects(DeltaTime);
	UpdateHullGlow(DeltaTime);
	UpdateCameras(DeltaTime);

	// Torpedo launch kick.
	if (Weapons)
	{
		const float Ready = Weapons->GetTorpedoReadiness();
		if (Ready < LastTorpedoReadiness - 0.5f)
		{
			AddCameraShake(0.25f);
		}
		LastTorpedoReadiness = Ready;
	}
}

void AStarshipPawn::UpdateWarpEffects(float DeltaTime)
{
	if (!WarpDrive)
	{
		return;
	}
	const float Streak = WarpDrive->GetStreakIntensity();
	const float Flash = WarpDrive->GetFlashIntensity();

	// Relativistic mode has no cartoon star streaks; the scene itself does the work.
	WarpStreaks->SetIntensity(WarpVisualMode == EWarpVisualMode::Relativistic ? Streak * 0.15f : Streak);
	UpdateWarpPostProcess(Streak, Flash);
	// Streaks fly past along the real direction of travel, whatever the camera is doing.
	const FVector Travel = (Movement && Movement->Velocity.SizeSquared() > 1.0) ? Movement->Velocity.GetSafeNormal() : GetActorForwardVector();
	WarpStreaks->SetTravelDirection(Travel);

	FPostProcessSettings& S = PostProcess->Settings;
	S.SceneFringeIntensity = WarpChromaticAberration * FMath::Max(Flash, Streak * 0.5f);
	S.BloomIntensity = 0.675f + WarpFlashBloom * Flash + 0.8f * Streak;
	const float Gain = 1.f + 1.5f * Flash * Flash;
	S.ColorGain = FVector4(
		FMath::Lerp(1.f, WarpFlashTint.R * Gain, Flash),
		FMath::Lerp(1.f, WarpFlashTint.G * Gain, Flash),
		FMath::Lerp(1.f, WarpFlashTint.B * Gain, Flash),
		1.f);
	S.VignetteIntensity = 0.4f + 0.45f * Streak;
	// The bridge set rides with the ship, which the floating origin teleports every frame at warp; the
	// renderer reads that as motion and smears the interior, so leave motion blur to the exterior views.
	S.MotionBlurAmount = (CameraMode == EStarshipCameraMode::Bridge) ? 0.f : 0.3f + 0.5f * Streak;

	// Continuous rumble at warp and on the jump.
	if (Flash > 0.5f)
	{
		ShakeAmplitude = FMath::Max(ShakeAmplitude, 0.6f);
	}
}

void AStarshipPawn::UpdateHullGlow(float DeltaTime)
{
	const float Glow = WarpDrive ? WarpDrive->GetGlowMultiplier() : 1.f;
	const float Throttle = Movement ? FMath::Abs((float)Movement->Throttle) : 0.f;
	const float ImpulseGlow = 0.35f + 3.f * Throttle;

	for (int32 i = 0; i < HullMaterials.Num(); ++i)
	{
		if (UMaterialInstanceDynamic* Mid = HullMaterials[i])
		{
			const float Weight = (i == ThrusterMaterialIndex) ? BaseGlowWeight * ImpulseGlow : BaseGlowWeight * Glow;
			Mid->SetScalarParameterValue(GlowParameterName, Weight);
		}
	}

	for (int32 i = 0; i < HullLights.Num() && i < LightSpecs.Num(); ++i)
	{
		UPointLightComponent* Light = HullLights[i];
		if (!Light)
		{
			continue;
		}
		const FStarshipLightSpec& Spec = LightSpecs[i];
		float Mult = 1.f;
		if (Spec.bWarpLight)
		{
			Mult *= Glow;
		}
		if (Spec.bImpulseLight)
		{
			Mult *= ImpulseGlow;
		}
		Light->SetIntensity(Spec.Intensity * Mult);
	}
}

void AStarshipPawn::UpdateCameras(float DeltaTime)
{
	// Recompute from properties every frame so the camera can be tuned live.
	ChaseArmBaseLength = GetShipLengthCm() * ChaseDistanceFactor;
	ChaseArmBaseSocketOffset = FVector(0.0, 0.0, GetShipLengthCm() * ChaseHeightFactor);

	const float Streak = WarpDrive ? WarpDrive->GetStreakIntensity() : 0.f;
	const float Flash = WarpDrive ? WarpDrive->GetFlashIntensity() : 0.f;
	const float ThrottleAbs = Movement ? FMath::Abs((float)Movement->Throttle) : 0.f;

	// Free look: hold to orbit, release to spring back.
	if (!bFreeLook && DebugLookTime <= 0.f)
	{
		const float A = 1.f - FMath::Exp(-FreeLookReturnRate * DeltaTime);
		FreeLookAngles = FMath::Lerp(FreeLookAngles, FVector2D::ZeroVector, A);
	}

	// Camera shake: decaying impulse plus rumble from warp / full impulse.
	ShakeTime += DeltaTime;
	ShakeAmplitude = FMath::Max(0.f, ShakeAmplitude - ShakeDecayRate * ShakeAmplitude * DeltaTime);
	const float Rumble = WarpRumble * Streak + 0.05f * ThrottleAbs * ThrottleAbs;
	const float Amp = ShakeAmplitude + Rumble;
	const float LengthCm = GetShipLengthCm();
	const FVector ShakeOffset(
		FMath::PerlinNoise1D(ShakeTime * 11.f + 3.1f),
		FMath::PerlinNoise1D(ShakeTime * 13.f + 7.7f),
		FMath::PerlinNoise1D(ShakeTime * 17.f + 1.3f));
	const float ShakeRoll = FMath::PerlinNoise1D(ShakeTime * 9.f + 5.5f) * Amp * 1.5f;

	switch (CameraMode)
	{
	case EStarshipCameraMode::Chase:
	{
		ChaseArm->SetRelativeRotation(FRotator(FreeLookAngles.Y, FreeLookAngles.X, 0.f));
		ChaseArm->TargetArmLength = ChaseArmBaseLength * (1.f + WarpArmLengthBoost * Streak);
		ChaseArm->SocketOffset = ChaseArmBaseSocketOffset + ShakeOffset * Amp * LengthCm * 0.01f;
		ChaseCamera->SetRelativeRotation(FRotator(0.f, 0.f, ShakeRoll));
		ChaseCamera->SetFieldOfView(ChaseFOV + WarpFovBoost * Streak + 8.f * Flash);
		break;
	}
	case EStarshipCameraMode::Bridge:
	{
		BridgeCamera->SetRelativeLocation(BridgeCameraOffset + ShakeOffset * Amp * 40.f);
		BridgeCamera->SetRelativeRotation(FRotator(FreeLookAngles.Y, FreeLookAngles.X, ShakeRoll * 0.5f));
		BridgeCamera->SetFieldOfView(BridgeFOV + WarpFovBoost * Streak + 8.f * Flash);
		break;
	}
	case EStarshipCameraMode::Cinematic:
	{
		OrbitAngle += CinematicOrbitRate * DeltaTime;
		const float Pitch = 8.f * FMath::Sin(OrbitAngle * 0.02f);
		OrbitPivot->SetWorldRotation(FRotator(Pitch, OrbitAngle, 0.f));
		const float OrbitDistance = LengthCm * CinematicDistanceFactor * (1.f + 0.4f * Streak);
		const FVector OrbitOffset(-OrbitDistance, 0.0, OrbitDistance * 0.18);
		CinematicCamera->SetRelativeLocation(OrbitOffset + ShakeOffset * Amp * LengthCm * 0.005f);
		CinematicCamera->SetRelativeRotation((-OrbitOffset).Rotation() + FRotator(0.f, 0.f, ShakeRoll * 0.3f));
		CinematicCamera->SetFieldOfView(CinematicFOV + 6.f * Flash);
		break;
	}
	}
}

void AStarshipPawn::ProcessDebugCommand()
{
	if (DebugCommand.IsEmpty())
	{
		return;
	}
	TArray<FString> Args;
	DebugCommand.TrimStartAndEnd().ParseIntoArrayWS(Args);
	const FString Cmd = DebugCommand;
	DebugCommand.Empty();
	if (Args.Num() == 0)
	{
		return;
	}
	const FString Op = Args[0].ToLower();
	auto Arg = [&Args](int32 Index, float Default) { return Args.IsValidIndex(Index) ? FCString::Atof(*Args[Index]) : Default; };
	auto ArgStr = [&Args](int32 Index) { return Args.IsValidIndex(Index) ? Args[Index].ToLower() : FString(); };

	UE_LOG(LogTemp, Log, TEXT("Starship debug command: %s"), *Cmd);

	if (Op == TEXT("throttle") && Movement) { Movement->SetThrottle(Arg(1, 0.f)); }
	else if (Op == TEXT("impulse") && Movement) { Movement->SetThrottle(FMath::Clamp(Arg(1, 4.f), 1.f, 4.f) * 0.25f); }
	else if (Op == TEXT("stop") && Movement) { Movement->SetThrottle(0.0); if (WarpDrive) { WarpDrive->DisengageWarp(); } }
	else if (Op == TEXT("warp") && WarpDrive)
	{
		const FString Mode = ArgStr(1);
		if (Mode == TEXT("on")) { WarpDrive->EngageWarp(); }
		else if (Mode == TEXT("off")) { WarpDrive->DisengageWarp(); }
		else { WarpDrive->ToggleWarp(); }
	}
	else if (Op == TEXT("warpfactor") && WarpDrive) { WarpDrive->SetWarpFactor(Arg(1, 2.f)); }
	else if (Op == TEXT("phasers") && Weapons) { if (ArgStr(1) == TEXT("off")) { Weapons->StopPhasers(); } else { Weapons->StartPhasers(); } }
	else if (Op == TEXT("torpedo") && Weapons) { Weapons->FireTorpedo(); }
	else if (Op == TEXT("target") && Weapons) { if (ArgStr(1) == TEXT("clear")) { Weapons->ClearTarget(); } else { Weapons->CycleTarget(); } }
	else if (Op == TEXT("camera"))
	{
		const FString Mode = ArgStr(1);
		if (Mode == TEXT("bridge")) { SetCameraMode(EStarshipCameraMode::Bridge); }
		else if (Mode == TEXT("cinematic")) { SetCameraMode(EStarshipCameraMode::Cinematic); }
		else if (Mode == TEXT("chase")) { SetCameraMode(EStarshipCameraMode::Chase); }
		else { CycleCameraMode(); }
	}
	else if (Op == TEXT("steer"))
	{
		DebugSteer = FVector(Arg(1, 0.f), Arg(2, 0.f), Arg(3, 0.f));
		DebugSteerTime = Arg(4, 1.f);
	}
	else if (Op == TEXT("look"))
	{
		DebugLook = FVector2D(Arg(1, 0.f), Arg(2, 0.f));
		DebugLookTime = Arg(3, 2.f);
	}
	else if (Op == TEXT("vert"))
	{
		DebugVertical = Arg(1, 0.f);
		DebugVerticalTime = Arg(2, 1.f);
	}
	else if (Op == TEXT("orbit"))
	{
		const FString Mode = ArgStr(1);
		if (Mode == TEXT("on")) { SetOrbitHold(true); }
		else if (Mode == TEXT("off")) { SetOrbitHold(false); }
		else { ToggleOrbitHold(); }
	}
	else if (Op == TEXT("shake")) { AddCameraShake(Arg(1, 1.f)); }
	else if (Op == TEXT("jump")) { JumpToNavTarget(); }
	else if (Op == TEXT("warpvis"))
	{
		const FString Mode = ArgStr(1);
		if (Mode == TEXT("classic")) { SetWarpVisualMode(EWarpVisualMode::Classic); }
		else if (Mode == TEXT("real") || Mode == TEXT("relativistic")) { SetWarpVisualMode(EWarpVisualMode::Relativistic); }
		else { ToggleWarpVisualMode(); }
	}
	else if (Op == TEXT("exec"))
	{
		// Everything after "exec" is a console command (e.g. "exec stat unit").
		FString Rest = Cmd.TrimStartAndEnd();
		Rest.RemoveFromStart(TEXT("exec"), ESearchCase::IgnoreCase);
		if (APlayerController* PC = Cast<APlayerController>(GetController()))
		{
			PC->ConsoleCommand(Rest.TrimStartAndEnd());
		}
	}
	else if (Op == TEXT("nav"))
	{
		if (APlayerController* PC = Cast<APlayerController>(GetController()))
		{
			if (AStarshipHUD* H = Cast<AStarshipHUD>(PC->GetHUD()))
			{
				const FString What = ArgStr(1);
				if (What == TEXT("next")) { H->CycleNavTarget(); }
				else if (What == TEXT("clear")) { H->ClearNavTarget(); }
				else if (What == TEXT("markers")) { H->ToggleNavMarkers(); }
				else if (What == TEXT("map")) { H->ToggleMinimap(); }
				else if (What == TEXT("target")) { H->SetNavTargetByName(ArgStr(2)); }
			}
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Starship debug command not understood: %s"), *Cmd);
	}
}

void AStarshipPawn::JumpToActor(AActor* Target, double TargetRadiusCm, float StandoffRadii)
{
	if (!Target)
	{
		return;
	}
	const FVector TargetLoc = Target->GetActorLocation();
	// Park 90 degrees from the star in the orbital plane, so the view runs straight down the
	// terminator: half lit, half dark. Fall back to "from where we are".
	FVector Approach = FVector::ZeroVector;
	if (const APlanetBody* Planet = Cast<APlanetBody>(Target))
	{
		const FVector ToStar = Planet->GetStarDirection();
		if (!ToStar.IsNearlyZero())
		{
			// Orbits lie in the world XY plane; rotate the star bearing a quarter turn about Z.
			FVector Side = FVector::CrossProduct(FVector::UpVector, ToStar);
			if (Side.IsNearlyZero())
			{
				Side = FVector::CrossProduct(ToStar, FVector::ForwardVector);
			}
			Approach = Side.GetSafeNormal();
		}
	}
	if (Approach.IsNearlyZero())
	{
		Approach = (GetActorLocation() - TargetLoc).GetSafeNormal();
	}
	if (Approach.IsNearlyZero())
	{
		Approach = FVector::ForwardVector;
	}
	if (Target->IsA<AStarBody>())
	{
		StandoffRadii = StarJumpStandoffRadii;
	}
	double Standoff = FMath::Max(TargetRadiusCm * StandoffRadii, GetShipLengthCm() * 20.0);
	if (Target->IsA<ASpaceVessel>())
	{
		// Ships and stations are small: arrive close enough to see them.
		Standoff = FMath::Max(TargetRadiusCm * 5.0, GetShipLengthCm() * 3.0);
	}
	const FVector NewLoc = TargetLoc + Approach * Standoff;
	const FRotator NewRot = (-Approach).Rotation();

	if (WarpDrive && WarpDrive->IsWarpEngaged())
	{
		WarpDrive->DisengageWarp();
	}
	if (Movement)
	{
		Movement->SetThrottle(0.0);
		Movement->Velocity = FVector::ZeroVector;
		Movement->AngularVelocity = FRotator::ZeroRotator;
		Movement->SetWarp(false, 0.0);
	}
	SetActorLocationAndRotation(NewLoc, NewRot, false, nullptr, ETeleportType::TeleportPhysics);
	// Rebase right away: the floating origin ticks during physics, which may already have run this frame.
	if (UFloatingOriginComponent* Origin = ASlonkGameMode::FindFloatingOrigin(GetWorld()))
	{
		Origin->ShiftWorld(-GetActorLocation());
	}
	FreeLookAngles = FVector2D::ZeroVector;
	AddCameraShake(0.4f);
}

void AStarshipPawn::JumpToNavTarget()
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	AStarshipHUD* H = PC ? Cast<AStarshipHUD>(PC->GetHUD()) : nullptr;
	if (!H)
	{
		return;
	}
	FNavObject Info;
	if (!H->GetNavTargetInfo(Info))
	{
		H->CycleNavTarget();
		if (!H->GetNavTargetInfo(Info))
		{
			return;
		}
	}
	JumpToActor(Info.Actor, Info.RadiusCm);
}

void AStarshipPawn::ToggleOrbitHold()
{
	SetOrbitHold(!(Movement && Movement->bOrbitHold));
}

void AStarshipPawn::SetOrbitHold(bool bEnable)
{
	if (!Movement)
	{
		return;
	}
	if (!bEnable)
	{
		if (AActor* Old = Movement->OrbitBody)
		{
			RemoveTickPrerequisiteActor(Old);
			Movement->RemoveTickPrerequisiteActor(Old);
		}
		Movement->ClearOrbitHold();
		return;
	}
	AActor* Body = nullptr;
	double Radius = 0.0;
	FString Name;
	// Prefer the nav target, then whatever body is nearest.
	APlayerController* PC = Cast<APlayerController>(GetController());
	AStarshipHUD* H = PC ? Cast<AStarshipHUD>(PC->GetHUD()) : nullptr;
	FNavObject Info;
	if (H && H->GetNavTargetInfo(Info) && Info.Actor && Info.RadiusCm > 0.0)
	{
		Body = Info.Actor;
		Radius = Info.RadiusCm;
		Name = Info.Label;
	}
	if (!Body)
	{
		double BestSurface = TNumericLimits<double>::Max();
		auto Consider = [&](AActor* Candidate, double R, const FString& Label)
		{
			const double Surface = FVector::Dist(Candidate->GetActorLocation(), GetActorLocation()) - R;
			if (Surface < BestSurface)
			{
				BestSurface = Surface;
				Body = Candidate;
				Radius = R;
				Name = Label;
			}
		};
		for (TActorIterator<APlanetBody> It(GetWorld()); It; ++It) { Consider(*It, It->GetRadiusCm(), It->GetDisplayName()); }
		for (TActorIterator<AStarBody> It(GetWorld()); It; ++It) { Consider(*It, It->GetRadiusCm(), It->StarName); }
	}
	if (Body)
	{
		if (WarpDrive && WarpDrive->IsWarpEngaged())
		{
			WarpDrive->DisengageWarp();
		}
		Movement->SetOrbitHold(Body, Radius, Name);
		// A moving target must have moved for this frame before we station-keep on it, or we trail a frame behind.
		AddTickPrerequisiteActor(Body);
		Movement->AddTickPrerequisiteActor(Body);
	}
}
