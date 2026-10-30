#include "ShuttlecraftPawn.h"
#include "ScienceScannerComponent.h"
#include "Components/BoxComponent.h"
#include "Components/DynamicMeshComponent.h"
#include "GeometryScript/MeshNormalsFunctions.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MeshPrims.h"
#include "RcsThrusterComponent.h"
#include "RunningLightsComponent.h"
#include "ShieldComponent.h"
#include "SlonkMaterials.h"
#include "StarshipMovementComponent.h"
#include "StarshipWeaponsComponent.h"
#include "UDynamicMesh.h"
#include "WarpDriveComponent.h"
#include "BridgeInteriorComponent.h"

namespace
{
	UDynamicMeshComponent* MakeShuttlePart(AActor* Owner, const TCHAR* Name, USceneComponent* Parent)
	{
		UDynamicMeshComponent* C = Owner->CreateDefaultSubobject<UDynamicMeshComponent>(Name);
		C->SetupAttachment(Parent);
		C->SetMobility(EComponentMobility::Movable);
		C->SetGenerateOverlapEvents(false);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->CastShadow = true;
		C->bCastDynamicShadow = true;
		C->bAffectDistanceFieldLighting = false;
		return C;
	}

	void FinishPart(UDynamicMeshComponent* C, UDynamicMesh* Scratch)
	{
		if (!C || !Scratch) { return; }
		FGeometryScriptCalculateNormalsOptions NO;
		UGeometryScriptLibrary_MeshNormalsFunctions::RecomputeNormals(Scratch, NO);
		C->GetDynamicMesh()->SetMesh(MoveTemp(Scratch->GetMeshRef()));
		C->NotifyMeshUpdated();
	}

	UMaterialInstanceDynamic* GlowMID(UObject* Outer, const FLinearColor& Color, float Intensity)
	{
		UMaterialInterface* Mat = SlonkMaterials::LoadOrFallback(TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(SlonkMaterials::GlowPath)), TEXT("Glow"), false);
		if (!Mat) { return nullptr; }
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Mat, Outer);
		M->SetVectorParameterValue(TEXT("GlowColor"), Color);
		M->SetScalarParameterValue(TEXT("Intensity"), Intensity);
		return M;
	}
}

AShuttlecraftPawn::AShuttlecraftPawn()
{
	// A 9.6 m boat: the hull is built here, not loaded.
	ShipLengthMeters = 9.6f;
	ShipWidthMeters = 4.6f;
	ShipHeightMeters = 3.2f;
	bAutoScaleMesh = false;
	if (ShipMesh)
	{
		ShipMesh->SetStaticMesh(nullptr);
	}
	HullPart = MakeShuttlePart(this, TEXT("HullPart"), CollisionBox);
	GlowPart = MakeShuttlePart(this, TEXT("GlowPart"), CollisionBox);
	WarmPart = MakeShuttlePart(this, TEXT("WarmPart"), CollisionBox);
	GlassPart = MakeShuttlePart(this, TEXT("GlassPart"), CollisionBox);

	// Flight: a few kilometres a second flat out, agile, and quick to answer the throttle: eight g with a three
	// second response, so a tap shows at once and a full stop from top speed takes a dozen seconds, not four
	// minutes. The RCS moves it at docking pace; near the ground it climbs and sinks at a few metres a second.
	if (Movement)
	{
		Movement->FullImpulseSpeed = 5.0e5;
		Movement->ImpulseAcceleration = 8.0e3;
		Movement->SpeedResponseSeconds = 3.f;
		Movement->VerticalRateScale = 0.3f;
		Movement->ThrottleExponent = 2.f;
		Movement->ReverseThrottleLimit = 0.3;
		Movement->MaxPitchRate = 60.f;
		Movement->MaxYawRate = 60.f;
		Movement->MaxRollRate = 90.f;
		Movement->AngularResponse = 5.f;
		Movement->VerticalThrustFraction = 0.02f;
		Movement->ThrusterMaxSpeed = 3.0e3;
		Movement->GroundClearanceCm = 150.0;
		Movement->bCanLand = true;
		Movement->LandingSpeedCm = 600.0;
	}
	if (WarpDrive)
	{
		WarpDrive->bEnabled = false;
	}
	if (Scanner)
	{
		// Short-range sensors: worlds from two radii, vessels from 50 km, but quicker once there.
		Scanner->WorldRangeRadii = 2.f;
		Scanner->VesselRangeCm = 5.0e6;
		Scanner->SpeedFactor = 1.5f;
	}
	if (Weapons)
	{
		Weapons->MaxFiringBanks = 1;
		Weapons->PhaserDamagePerSecond = 6.f;
		Weapons->TorpedoTubeOffsets.Reset();
	}
	MaxShieldStrength = 25.f;
	ShieldStrength = 25.f;

	// Cameras: the chase view a little further out in hull lengths, the "bridge" view at the pilot's eyes.
	ChaseDistanceFactor = 2.2f;
	ChaseHeightFactor = 0.32f;
	BridgeCameraOffset = FVector(300.0, 0.0, 70.0);
	BridgeFOV = 95.f;
	CinematicDistanceFactor = 3.f;
	ShuttleBayOffset = FVector::ZeroVector;
}

void AShuttlecraftPawn::BeginPlay()
{
	// No bridge set inside a shuttle: drop the component before it builds itself.
	if (BridgeInterior)
	{
		BridgeInterior->DestroyComponent();
		BridgeInterior = nullptr;
	}
	Super::BeginPlay();
	BuildHull();
	SetCameraMode(GetCameraMode());
}

void AShuttlecraftPawn::BuildHull()
{
	using namespace SlonkMesh;
	UDynamicMesh* HM = NewObject<UDynamicMesh>(this);
	UDynamicMesh* GM = NewObject<UDynamicMesh>(this);
	UDynamicMesh* WM = NewObject<UDynamicMesh>(this);
	UDynamicMesh* XM = NewObject<UDynamicMesh>(this);

	// Hull (material 0), accents (material 1). Pawn space: nose +X, up +Z, origin mid-ship; cm.
	CurrentMaterialID() = 0;
	{
		// Fuselage: the side silhouette, extruded across the beam. Sloped windscreen forward, flat roof, a short
		// tumblehome at the stern.
		TArray<FVector2D> Side = { {-400.f, -95.f}, {280.f, -95.f}, {400.f, -30.f}, {400.f, 55.f}, {330.f, 120.f}, {-370.f, 120.f}, {-400.f, 80.f} };
		Extrude(HM, FVector::ZeroVector, FVector::ForwardVector, FVector::UpVector, Side, 260.f);
		Box(HM, FVector(-60.f, 0.f, 128.f), FVector(520.f, 160.f, 20.f));           // roof fairing
		Ellipsoid(HM, FVector(130.f, 0.f, 138.f), FVector(32.f, 32.f, 12.f));       // sensor dome
		for (float S : { -1.f, 1.f })
		{
			CapsuleX(HM, FVector(-350.f, S * 235.f, 5.f), 640.f, 42.f);            // nacelle
		}
	}
	CurrentMaterialID() = 1;
	for (float S : { -1.f, 1.f })
	{
		Box(HM, FVector(-40.f, S * 150.f, -45.f), FVector(700.f, 50.f, 110.f));    // side sponson
		Box(HM, FVector(-40.f, S * 195.f, 10.f), FVector(240.f, 50.f, 24.f));      // nacelle pylon
		Box(HM, FVector(-30.f, S * 95.f, -120.f), FVector(480.f, 28.f, 50.f));     // skid
	}
	Box(HM, FVector(330.f, 0.f, -120.f), FVector(70.f, 70.f, 50.f));              // nose pad
	Box(HM, FVector(-402.f, 0.f, 20.f), FVector(8.f, 200.f, 110.f));              // stern plate round the exhaust

	// Glow: the nacelle grilles (blue), the bussard caps and impulse exhaust (warm), the lit cockpit glass.
	CurrentMaterialID() = 0;
	for (float S : { -1.f, 1.f })
	{
		Box(GM, FVector(-110.f, S * (235.f - 40.f), 5.f), FVector(360.f, 6.f, 28.f));
		Box(GM, FVector(-110.f, S * (235.f + 40.f), 5.f), FVector(360.f, 6.f, 28.f));
		Ellipsoid(WM, FVector(300.f, S * 235.f, 5.f), FVector(40.f, 44.f, 44.f));
	}
	Box(WM, FVector(-404.f, 0.f, 30.f), FVector(6.f, 110.f, 36.f));
	// Windscreen: a pane lying in the sloped nose face, pushed a hair proud of it.
	Box(XM, FVector(366.5f, 0.f, 89.f), FVector(88.f, 190.f, 3.f), FRotator(-43.f, 0.f, 0.f));
	for (float S : { -1.f, 1.f })
	{
		Box(XM, FVector(170.f, S * 131.5f, 70.f), FVector(70.f, 3.f, 30.f));
		Box(XM, FVector(60.f, S * 131.5f, 70.f), FVector(70.f, 3.f, 30.f));
	}

	FinishPart(HullPart, HM);
	FinishPart(GlowPart, GM);
	FinishPart(WarmPart, WM);
	FinishPart(GlassPart, XM);

	if (UMaterialInterface* Hull = SlonkMaterials::LoadOrFallback(TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(SlonkMaterials::HullPath)), TEXT("Hull"), true))
	{
		auto Make = [&](const FLinearColor& C)
		{
			UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Hull, this);
			M->SetVectorParameterValue(TEXT("HullColor"), C);
			M->SetScalarParameterValue(TEXT("PanelScale"), 400.f / GetShipLengthCm());
			M->SetScalarParameterValue(TEXT("Roughness"), 0.55f);
			M->SetScalarParameterValue(TEXT("Metallic"), 0.1f);
			return M;
		};
		HullMID = Make(HullColor);
		AccentMID = Make(AccentColor);
		HullPart->ConfigureMaterialSet({ HullMID.Get(), AccentMID.Get() });
	}
	GrilleMID = GlowMID(this, GrilleColor, GrilleGlow);
	BussardMID = GlowMID(this, BussardColor, BussardGlow);
	GlassMID = GlowMID(this, GlassColor, GlassGlow);
	if (GrilleMID) { GlowPart->ConfigureMaterialSet({ GrilleMID.Get() }); }
	if (BussardMID) { WarmPart->ConfigureMaterialSet({ BussardMID.Get() }); }
	if (GlassMID) { GlassPart->ConfigureMaterialSet({ GlassMID.Get() }); }
}

void AShuttlecraftPawn::SetupRunningLights()
{
	if (!RunningLights) { return; }
	using RL = URunningLightsComponent;
	TArray<FRunningLight> Lights;
	Lights.Add(FRunningLight(FVector(150.f, -177.f, 10.f), RL::PortRed(), ERunningLightMode::Blink, 1.3f, 0.f, 6.f));
	Lights.Add(FRunningLight(FVector(150.f, 177.f, 10.f), RL::StarboardGreen(), ERunningLightMode::Blink, 1.3f, 0.f, 6.f));
	Lights.Add(FRunningLight(FVector(-60.f, 0.f, 140.f), RL::StrobeWhite(), ERunningLightMode::Blink, 1.9f, 0.5f, 6.f));
	Lights.Add(FRunningLight(FVector(-405.f, 0.f, 95.f), RL::StrobeWhite(), ERunningLightMode::Blink, 1.9f, 0.f, 5.f));
	RunningLights->SetLights(Lights);
}

void AShuttlecraftPawn::SetupThrusters()
{
	if (!Thrusters) { return; }
	// Four corner packages (out, up, down) plus fore and aft pairs, all on the sponsons and nose and stern plates.
	TArray<FRcsNozzle> Nozzles;
	for (float FX : { -330.f, 300.f })
	{
		for (float S : { -1.f, 1.f })
		{
			Nozzles.Add(FRcsNozzle(FVector(FX, S * 176.f, -20.f), FVector(0.f, S, 0.f), 0.8f));
			Nozzles.Add(FRcsNozzle(FVector(FX, S * 120.f, 121.f), FVector::UpVector, 0.7f));
			Nozzles.Add(FRcsNozzle(FVector(FX, S * 120.f, -101.f), FVector::DownVector, 0.7f));
		}
	}
	for (float S : { -1.f, 1.f })
	{
		Nozzles.Add(FRcsNozzle(FVector(401.f, S * 70.f, 10.f), FVector::ForwardVector, 0.7f));
		Nozzles.Add(FRcsNozzle(FVector(-407.f, S * 70.f, -30.f), FVector::BackwardVector, 0.7f));
	}
	Thrusters->PlumeLength = GetShipLengthCm() * 0.06f;
	Thrusters->PlumeWidth = GetShipLengthCm() * 0.025f;
	Thrusters->SetNozzles(Nozzles);
}

void AShuttlecraftPawn::SetupWeaponBanks()
{
	if (!Weapons) { return; }
	// One emitter under the nose, covering ahead.
	Weapons->PhaserBanks = { FPhaserBank(TEXT("Nose"), FVector(395.0, 0.0, -60.0), FVector(1.0, 0.0, -0.15)) };
	Weapons->TorpedoTubeOffsets.Reset();
}

void AShuttlecraftPawn::SetCameraMode(EStarshipCameraMode Mode)
{
	Super::SetCameraMode(Mode);
	// Out of the pilot's seat the hull is in the way of the view: hide it, as the starship hides its saucer.
	const bool bShow = Mode != EStarshipCameraMode::Bridge;
	for (UDynamicMeshComponent* P : { HullPart.Get(), GlowPart.Get(), WarmPart.Get(), GlassPart.Get() })
	{
		if (P) { P->SetVisibility(bShow); }
	}
	if (Thrusters) { Thrusters->SetVisibility(bShow, true); }
}

void AShuttlecraftPawn::BeginLaunch(AStarshipPawn* Ship)
{
	Mothership = Ship;
	SetPhase(EShuttlePhase::Launching);
}

void AShuttlecraftPawn::SetPhase(EShuttlePhase NewPhase)
{
	Phase = NewPhase;
	PhaseTime = 0.f;
	DockStep = 0;
	// The bay slides are kinematic: the flight model sits out until the shuttle is clear.
	if (Movement)
	{
		Movement->SetComponentTickEnabled(Phase == EShuttlePhase::Flying);
		if (Phase != EShuttlePhase::Flying)
		{
			Movement->SetThrottle(0.0);
			Movement->ClearOrbitHold();
			Movement->ClearTerrainFollow();
		}
	}
}

double AShuttlecraftPawn::DistanceToBay() const
{
	const AStarshipPawn* Ship = Mothership.Get();
	return Ship ? FVector::Dist(GetActorLocation(), Ship->GetShuttleBayTransform().GetLocation()) : -1.0;
}

void AShuttlecraftPawn::ToggleShuttle()
{
	RequestDock();
}

bool AShuttlecraftPawn::RequestDock()
{
	AStarshipPawn* Ship = Mothership.Get();
	if (!Ship)
	{
		ScreenMessage(TEXT("No ship to dock with"), FColor::Orange);
		return false;
	}
	if (Phase != EShuttlePhase::Flying)
	{
		return false;
	}
	const double Dist = DistanceToBay();
	if (Dist > DockRangeCm)
	{
		ScreenMessage(FString::Printf(TEXT("Dock: the bay is %.0f m away; close to within %.0f m astern of the %s"), Dist / 100.0, DockRangeCm / 100.0, *Ship->GetCraftName()), FColor::Orange);
		return false;
	}
	const double RelSpeed = Movement && Ship->Movement ? (Movement->Velocity - Ship->Movement->Velocity).Size() : 0.0;
	if (RelSpeed > DockMaxSpeedCm)
	{
		ScreenMessage(FString::Printf(TEXT("Dock: slow to under %.0f m/s relative to the ship (now %.0f m/s)"), DockMaxSpeedCm / 100.0, RelSpeed / 100.0), FColor::Orange);
		return false;
	}
	if (Movement && Movement->bLanded) { Movement->TakeOff(); }
	SetPhase(EShuttlePhase::Docking);
	ScreenMessage(TEXT("Docking: approach under the bay's control"), FColor::Cyan);
	return true;
}

bool AShuttlecraftPawn::HandleCraftCommand(const FString& Op, const TArray<FString>& Args)
{
	if (Op == TEXT("dock") || Op == TEXT("shuttle"))
	{
		RequestDock();
		return true;
	}
	return Super::HandleCraftCommand(Op, Args);
}

void AShuttlecraftPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	switch (Phase)
	{
	case EShuttlePhase::Launching: TickLaunch(DeltaTime); break;
	case EShuttlePhase::Docking: TickDock(DeltaTime); break;
	default: break;
	}
}

void AShuttlecraftPawn::TickLaunch(float DeltaTime)
{
	AStarshipPawn* Ship = Mothership.Get();
	if (!Ship)
	{
		SetPhase(EShuttlePhase::Flying);
		return;
	}
	PhaseTime += DeltaTime;
	// Out through the door and straight astern, picking up to bay speed over the first second.
	const float Run = BaySpeedCm * FMath::Max(PhaseTime - 0.5f * FMath::Min(PhaseTime, 1.f), 0.f);
	const FTransform Bay = Ship->GetShuttleBayTransform();
	SetActorLocationAndRotation(Bay.TransformPosition(FVector(Run, 0.f, 0.f)), Bay.GetRotation(), false, nullptr, ETeleportType::TeleportPhysics);
	if (Run >= BayRunCm)
	{
		SetPhase(EShuttlePhase::Flying);
		if (Movement)
		{
			Movement->Velocity = Bay.GetUnitAxis(EAxis::X) * BaySpeedCm + (Ship->Movement ? Ship->Movement->Velocity : FVector::ZeroVector);
		}
		ScreenMessage(TEXT("Shuttle clear of the bay. C docks again within 500 m of it; set down on a planet by easing onto the ground."), FColor::Cyan, 10.f);
	}
}

void AShuttlecraftPawn::TickDock(float DeltaTime)
{
	AStarshipPawn* Ship = Mothership.Get();
	if (!Ship)
	{
		SetPhase(EShuttlePhase::Flying);
		return;
	}
	PhaseTime += DeltaTime;
	const FTransform Bay = Ship->GetShuttleBayTransform();
	const FVector Out = Bay.GetUnitAxis(EAxis::X);                 // astern of the ship
	const FQuat Facing = Bay.GetRotation() * FQuat(FVector::UpVector, PI); // nose toward the door
	const FVector Here = GetActorLocation();
	FQuat Rot = FQuat::Slerp(GetActorQuat(), Facing, 1.f - FMath::Exp(-1.5f * DeltaTime));
	if (DockStep == 0)
	{
		// To the approach point astern of the door, fast when far, easing in.
		const FVector Goal = Bay.TransformPosition(FVector(BayRunCm, 0.f, 0.f));
		const FVector To = Goal - Here;
		const double Dist = To.Size();
		const double Speed = FMath::Clamp(Dist / 1.5, 300.0, 4000.0);
		const double Step = FMath::Min(Dist, Speed * DeltaTime);
		SetActorLocationAndRotation(Here + To.GetSafeNormal() * Step, Rot, false, nullptr, ETeleportType::TeleportPhysics);
		if (Dist - Step < 50.0 && FMath::RadiansToDegrees(Rot.AngularDistance(Facing)) < 3.f)
		{
			DockStep = 1;
		}
		return;
	}
	// Straight in along the bay axis at bay speed.
	const FVector Door = Bay.GetLocation();
	const FVector To = Door - Here;
	const double Dist = To.Size();
	const double Step = FMath::Min(Dist, (double)BaySpeedCm * DeltaTime);
	SetActorLocationAndRotation(Here + To.GetSafeNormal() * Step, Facing, false, nullptr, ETeleportType::TeleportPhysics);
	if (Dist - Step < 20.0)
	{
		Ship->OnShuttleDocked(this);
	}
}
