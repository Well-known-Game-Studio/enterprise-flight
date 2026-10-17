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
#include "ShieldComponent.h"
#include "TractorBeamComponent.h"
#include "RunningLightsComponent.h"
#include "RcsThrusterComponent.h"
#include "Rendering/PositionVertexBuffer.h"
#include "StaticMeshResources.h"
#include "Engine/DamageEvents.h"
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
#include "WarpViewComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

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

	Shield = CreateDefaultSubobject<UShieldComponent>(TEXT("Shield"));
	Shield->SetupAttachment(CollisionBox);
	TractorBeam = CreateDefaultSubobject<UTractorBeamComponent>(TEXT("TractorBeam"));
	RunningLights = CreateDefaultSubobject<URunningLightsComponent>(TEXT("RunningLights"));
	RunningLights->SetupAttachment(CollisionBox);
	Thrusters = CreateDefaultSubobject<URcsThrusterComponent>(TEXT("Thrusters"));
	Thrusters->SetupAttachment(CollisionBox);

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

	WarpView = CreateDefaultSubobject<UWarpViewComponent>(TEXT("WarpView"));
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
	HullWindowMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Slonk/Materials/M_HullWindows.M_HullWindows")));
	HullSkinMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Slonk/Materials/M_Constitution.M_Constitution")));

	// No hull point lights by default: the nacelle grilles, bussards, deflector and impulse deck glow through the
	// mesh's emissive maps (scaled by the warp drive below), which lights more honestly than point lights placed
	// inside the hull. LightSpecs can still be filled in the editor for a one-off.
	// Saucer (slots 1 and 2) and engineering hull (slot 3) plating: no emissive. Nacelles (slot 4): hot.
	// The glow maps carry the lit windows; this weight shows them without the plating reading as self-lit.
	SlotGlowWeights.Add(TEXT("Constitution_1"), 1.3f);
	SlotGlowWeights.Add(TEXT("Constitution_2"), 1.3f);
	SlotGlowWeights.Add(TEXT("Constitution_3"), 1.3f);
	SlotGlowWeights.Add(TEXT("Constitution_4"), 5.0f);
}

void AStarshipPawn::SetupRunningLights()
{
	if (!RunningLights)
	{
		return;
	}
	const float L = GetShipLengthCm();
	const float W = ShipWidthMeters * 100.f;
	const float H = ShipHeightMeters * 100.f;

	// Hull surface points come from the mesh itself (see ProbeHull), so the fittings sit on the skin whatever mesh is fitted.
	GatherHullVerts();
	auto Probe = [this](const FVector& Dir, const FBox& Region, const FVector& Fallback) -> FVector { return ProbeHull(Dir, Region, Fallback); };
	const FBox All(FVector(-L, -W, -H), FVector(L, W, H));
	const FBox Saucer(FVector(L * 0.1f, -W, -H), FVector(L, W, H));
	const FBox PortNacelle(FVector(-L, -W, H * 0.12f), FVector(L, -W * 0.14f, H));
	const FBox StarboardNacelle(FVector(-L, W * 0.14f, H * 0.12f), FVector(L, W, H));
	const FBox PortNacelleMid(FVector(-L * 0.32f, -W, H * 0.12f), FVector(-L * 0.16f, -W * 0.14f, H));
	const FBox StarboardNacelleMid(FVector(-L * 0.32f, W * 0.14f, H * 0.12f), FVector(-L * 0.16f, W, H));
	const FBox Secondary(FVector(-L * 0.45f, -W * 0.13f, -H), FVector(L * 0.05f, W * 0.13f, H * 0.05f));

	using RL = URunningLightsComponent;
	// Bulbs are small fittings, not spheres stuck on the hull: 37 cm on a 305 m ship. They still grow with distance so they read from afar.
	const float Bulb = L * 0.0012f;
	const float Strobe = 1.9f;
	const float Blink = 1.3f;
	TArray<FRunningLight> Lights;
	Lights.Add(FRunningLight(Probe(FVector(0, -1, 0), Saucer, FVector(L * 0.2f, -W * 0.5f, H * 0.1f)), RL::PortRed(), ERunningLightMode::Blink, Blink, 0.f, Bulb * 1.3f));
	Lights.Add(FRunningLight(Probe(FVector(0, 1, 0), Saucer, FVector(L * 0.2f, W * 0.5f, H * 0.1f)), RL::StarboardGreen(), ERunningLightMode::Blink, Blink, 0.f, Bulb * 1.3f));
	Lights.Add(FRunningLight(Probe(FVector(1, 0, 0), All, FVector(L * 0.5f, 0.f, H * 0.1f)), RL::StrobeWhite(), ERunningLightMode::Strobe, Strobe, 0.f, Bulb * 1.2f));
	Lights.Add(FRunningLight(Probe(FVector(0, 0, 1), Saucer, FVector(L * 0.2f, 0.f, H * 0.5f)), RL::StrobeWhite(), ERunningLightMode::Strobe, Strobe, 0.5f, Bulb * 1.2f));
	Lights.Add(FRunningLight(Probe(FVector(0, 0, -1), Saucer, FVector(L * 0.2f, 0.f, -H * 0.1f)), RL::StrobeWhite(), ERunningLightMode::Strobe, Strobe, 0.5f, Bulb * 1.2f));
	{
		const float Angles[4] = { 35.f, 70.f, 110.f, 145.f };
		for (int32 k = 0; k < 8; ++k)
		{
			const float A = FMath::DegreesToRadians(Angles[k % 4] * (k < 4 ? 1.f : -1.f));
			const FVector Dir(FMath::Cos(A), FMath::Sin(A), 0.f);
			Lights.Add(FRunningLight(Probe(Dir, Saucer, FVector(L * 0.2f, 0.f, H * 0.1f) + Dir * W * 0.5f), RL::FormationAmber(), ERunningLightMode::Blink, Blink * 2.f, 0.5f * (k % 2), Bulb * 0.6f, 0.45f));
		}
	}
	Lights.Add(FRunningLight(Probe(FVector(-1, 0, 0), PortNacelle, FVector(-L * 0.45f, -W * 0.26f, H * 0.37f)), RL::PortRed(), ERunningLightMode::Blink, Blink, 0.f, Bulb));
	Lights.Add(FRunningLight(Probe(FVector(-1, 0, 0), StarboardNacelle, FVector(-L * 0.45f, W * 0.26f, H * 0.37f)), RL::StarboardGreen(), ERunningLightMode::Blink, Blink, 0.f, Bulb));
	Lights.Add(FRunningLight(Probe(FVector(0, -1, 0), PortNacelleMid, FVector(-L * 0.25f, -W * 0.3f, H * 0.37f)), RL::StrobeWhite(), ERunningLightMode::Strobe, Strobe, 0.25f, Bulb * 0.9f));
	Lights.Add(FRunningLight(Probe(FVector(0, 1, 0), StarboardNacelleMid, FVector(-L * 0.25f, W * 0.3f, H * 0.37f)), RL::StrobeWhite(), ERunningLightMode::Strobe, Strobe, 0.25f, Bulb * 0.9f));
	Lights.Add(FRunningLight(Probe(FVector(0, 0, -1), Secondary, FVector(-L * 0.2f, 0.f, -H * 0.4f)), RL::StrobeWhite(), ERunningLightMode::Strobe, Strobe, 0.75f, Bulb));
	Lights.Add(FRunningLight(Probe(FVector(-1, 0, 0), Secondary, FVector(-L * 0.42f, 0.f, -H * 0.2f)), RL::FormationAmber(), ERunningLightMode::Steady, 1.f, 0.f, Bulb, 0.7f));

	// Bridge module: a small white lamp on each flank of its lower tier.
	const FBox BridgePort(FVector(7200.f, -1000.f, 3150.f), FVector(8900.f, -300.f, 3400.f));
	const FBox BridgeStbd(FVector(7200.f, 300.f, 3150.f), FVector(8900.f, 1000.f, 3400.f));
	Lights.Add(FRunningLight(Probe(FVector(0, -1, 0), BridgePort, FVector(8050.f, -600.f, 3250.f)), RL::StrobeWhite(), ERunningLightMode::Steady, 1.f, 0.f, Bulb * 0.7f, 0.6f));
	Lights.Add(FRunningLight(Probe(FVector(0, 1, 0), BridgeStbd, FVector(8050.f, 600.f, 3250.f)), RL::StrobeWhite(), ERunningLightMode::Steady, 1.f, 0.f, Bulb * 0.7f, 0.6f));

	RunningLights->MaxRadiusCm = L * 0.012f;
	RunningLights->MinAngularRadius = 0.002f;
	RunningLights->SetLights(Lights);

	// Floodlights (refit convention). Saucer: lamps near the middle of the top and underside, facing radially out
	// across the plating to the rim. Flanks: lamps at the stern of the engineering hull shining forward along the
	// name. Pylons: lamps at the pylon roots pointing up at the nacelles. Nacelles: lamps on top shining aft.
	TArray<FHullFloodlight> Floods;
	// Hull section bounds (pawn space): saucer X -637..15240, Y +-7190; engineering hull X -7316..5776, Y +-1649,
	// Z -3640..1716; nacelles+pylons X -15221..177, Y +-5333, Z -678..3551; impulse deck X 620..893, Z 1731..2128.
	const FVector SaucerCentre(8050.f, 0.f, 0.f);
	const float SaucerR = 7190.f;
	auto SurfaceAt = [&](const FVector& XY, bool bTop) -> FVector
	{
		// The hull skin straight above (or below) a point on the saucer plan, from the mesh: the highest (or
		// lowest) vertex in a full-height column there. The saucer's underside sits above z 500, so the column
		// must not be cut off at the nominal centre height or the underside fittings fall back into empty space.
		const FBox Column(FVector(XY.X - 250.f, XY.Y - 250.f, -H), FVector(XY.X + 250.f, XY.Y + 250.f, H));
		return Probe(FVector(0, 0, bTop ? 1.f : -1.f), Column, FVector(XY.X, XY.Y, bTop ? 1800.f : 600.f));
	};
	{
		// Three wide lamps on each face of the saucer, as on the refit: one forward and one out to each side. The
		// forward lamp has to light the whole registry, which spans most of the saucer's width ahead of the
		// superstructure, so it stands a little proud of the skin and throws a patch over half the radius wide.
		const float Angles[3] = { 0.f, 90.f, -90.f };
		for (float Deg : Angles)
		{
			const float A = FMath::DegreesToRadians(Deg);
			const FVector Dir(FMath::Cos(A), FMath::Sin(A), 0.f);
			const bool bForward = Deg == 0.f;
			for (int32 Side = 0; Side < 2; ++Side)
			{
				const bool bTop = Side == 0;
				const FVector LampSkin = SurfaceAt(SaucerCentre + Dir * SaucerR * (bForward ? 0.2f : 0.22f), bTop);
				const FVector Patch = SurfaceAt(SaucerCentre + Dir * SaucerR * (bForward ? 0.58f : 0.62f), bTop);
				const float Stand = bForward ? 320.f : 150.f;
				Floods.Add(FHullFloodlight(LampSkin + FVector(0.f, 0.f, bTop ? Stand : -Stand), Patch, SaucerR * (bForward ? 0.62f : 0.4f), bForward ? 30.f : 16.f));
			}
		}
	}
	for (int32 Side = -1; Side <= 1; Side += 2)
	{
		// Engineering hull flank: from the stern, forward along the side to the Starfleet pennant and insignia
		// painted toward the front of the hull.
		const FBox FlankAft(FVector(-6500.f, Side < 0 ? -2500.f : 0.f, -2600.f), FVector(-4500.f, Side < 0 ? 0.f : 2500.f, 800.f));
		const FBox FlankFore(FVector(1800.f, Side < 0 ? -2500.f : 0.f, -2600.f), FVector(3800.f, Side < 0 ? 0.f : 2500.f, 600.f));
		const FVector Stern = Probe(FVector(0, Side, 0), FlankAft, FVector(-5500.f, Side * 1500.f, -900.f));
		const FVector Fore = Probe(FVector(0, Side, 0), FlankFore, FVector(2800.f, Side * 1600.f, -1000.f));
		Floods.Add(FHullFloodlight(Stern + FVector(0.f, Side * 130.f, 0.f), Fore, 1100.f, 14.f));
		// Pylon root on the hull's top shoulder, pointing up the pylon at the nacelle's inboard underside.
		const FBox Shoulder(FVector(-6200.f, Side < 0 ? -1700.f : 300.f, 600.f), FVector(-4200.f, Side < 0 ? -300.f : 1700.f, 2200.f));
		const FVector Root = Probe(FVector(0, 0, 1), Shoulder, FVector(-5200.f, Side * 1000.f, 1600.f));
		const FBox NacelleBelly(FVector(-8500.f, Side < 0 ? -4500.f : 2800.f, 1200.f), FVector(-5500.f, Side < 0 ? -2800.f : 4500.f, 3000.f));
		const FVector Belly = Probe(FVector(0, 0, -1), NacelleBelly, FVector(-7000.f, Side * 3600.f, 2200.f));
		Floods.Add(FHullFloodlight(Root + FVector(0.f, 0.f, 120.f), Belly, 900.f, 14.f));
		// On the nacelle: a lamp on its top shoulder near the front, shining aft along the top where the registry is.
		const FBox NacelleTopFore(FVector(-5000.f, Side < 0 ? -4500.f : 2800.f, 2200.f), FVector(-3200.f, Side < 0 ? -2800.f : 4500.f, H));
		const FBox NacelleTopAft(FVector(-11500.f, Side < 0 ? -4500.f : 2800.f, 2200.f), FVector(-9000.f, Side < 0 ? -2800.f : 4500.f, H));
		const FVector TopFore = Probe(FVector(0, 0, 1), NacelleTopFore, FVector(-4000.f, Side * 3700.f, 3300.f));
		const FVector TopAft = Probe(FVector(0, 0, 1), NacelleTopAft, FVector(-10000.f, Side * 3700.f, 3300.f));
		Floods.Add(FHullFloodlight(TopFore + FVector(0.f, 0.f, 110.f), TopAft, 800.f, 12.f));
		// And one on the nacelle's outboard shoulder lighting the outboard flank.
		const FBox NacelleOutFore(FVector(-5000.f, Side < 0 ? -6000.f : 2800.f, 1500.f), FVector(-3200.f, Side < 0 ? -2800.f : 6000.f, H));
		const FBox NacelleOutAft(FVector(-11500.f, Side < 0 ? -6000.f : 2800.f, 1500.f), FVector(-9000.f, Side < 0 ? -2800.f : 6000.f, H));
		const FVector OutFore = Probe(FVector(0, Side, 0), NacelleOutFore, FVector(-4000.f, Side * 4300.f, 2700.f));
		const FVector OutAft = Probe(FVector(0, Side, 0), NacelleOutAft, FVector(-10000.f, Side * 4300.f, 2700.f));
		Floods.Add(FHullFloodlight(OutFore + FVector(0.f, Side * 110.f, 0.f), OutAft, 800.f, 12.f));
	}
	RunningLights->SetFloodlights(Floods);
}

void AStarshipPawn::GatherHullVerts()
{
	HullVerts.Reset();
	HullNormals.Reset();
	if (ShipMesh && ShipMesh->GetStaticMesh() && ShipMesh->GetStaticMesh()->GetRenderData() && ShipMesh->GetStaticMesh()->GetRenderData()->LODResources.Num() > 0)
	{
		const FStaticMeshLODResources& LOD = ShipMesh->GetStaticMesh()->GetRenderData()->LODResources[0];
		const FPositionVertexBuffer& PB = LOD.VertexBuffers.PositionVertexBuffer;
		const FStaticMeshVertexBuffer& VB = LOD.VertexBuffers.StaticMeshVertexBuffer;
		if (PB.GetVertexData() != nullptr && PB.GetNumVertices() > 0)
		{
			const FTransform MeshToPawn = ShipMesh->GetRelativeTransform();
			const bool bNormals = VB.GetTangentData() != nullptr && VB.GetNumVertices() == PB.GetNumVertices();
			HullVerts.Reserve(PB.GetNumVertices());
			if (bNormals)
			{
				HullNormals.Reserve(PB.GetNumVertices());
			}
			for (uint32 i = 0; i < PB.GetNumVertices(); ++i)
			{
				HullVerts.Add(MeshToPawn.TransformPosition(FVector(PB.VertexPosition(i))));
				if (bNormals)
				{
					const FVector4f N = VB.VertexTangentZ(i);
					HullNormals.Add(MeshToPawn.TransformVectorNoScale(FVector(N.X, N.Y, N.Z)).GetSafeNormal());
				}
			}
		}
	}
}

bool AStarshipPawn::SnapToHull(FVector& Pos, FVector& Dir, float Radius) const
{
	double BestD2 = (double)Radius * Radius;
	int32 Best = INDEX_NONE;
	for (int32 i = 0; i < HullVerts.Num(); ++i)
	{
		// Plating facing away from the exhaust is the wrong face of the hull (the underside when we want the top).
		if (HullNormals.IsValidIndex(i) && FVector::DotProduct(HullNormals[i], Dir) < -0.15)
		{
			continue;
		}
		const double D2 = FVector::DistSquared(HullVerts[i], Pos);
		if (D2 < BestD2)
		{
			BestD2 = D2;
			Best = i;
		}
	}
	if (Best == INDEX_NONE)
	{
		return false;
	}
	const FVector N = HullNormals.IsValidIndex(Best) ? HullNormals[Best] : Dir;
	// The nozzle's mouth sits just proud of the plating; the exhaust leaves at least ~30 degrees out of it.
	Pos = HullVerts[Best] + N * 25.f;
	Dir = (Dir + N * 0.6f).GetSafeNormal();
	return true;
}

FVector AStarshipPawn::ProbeHull(const FVector& Dir, const FBox& Region, const FVector& Fallback) const
{
	// The vertex furthest along Dir inside the region: the skin where a fitting would be bolted on. Falls back to
	// the given proportions of the hull box when the vertex data is not CPU-readable (cooked builds without CPU access).
	double Best = -1e30;
	FVector BestP = Fallback;
	for (const FVector& V : HullVerts)
	{
		if (!Region.IsInsideOrOn(V))
		{
			continue;
		}
		const double D = FVector::DotProduct(V, Dir);
		if (D > Best)
		{
			Best = D;
			BestP = V;
		}
	}
	return BestP + Dir * (GetShipLengthCm() * 0.001f);
}

void AStarshipPawn::SetupThrusters()
{
	if (!Thrusters)
	{
		return;
	}
	if (HullVerts.Num() == 0)
	{
		GatherHullVerts();
	}
	// Pawn-space hull sections (see SetupRunningLights): saucer centre (8050, 0), radius 7190, rim about z 250;
	// engineering hull x -7316..5776, y +-1649, z -3640..1716 with the deflector dish centred near (5750, 0, -1700);
	// nacelles x -15221..177, y +-5333, z -678..3551.
	const FVector SaucerCentre(8050.f, 0.f, 250.f);
	const float SaucerR = 7190.f;
	TArray<FRcsNozzle> Nozzles;

	// Saucer rim: four thruster packages, forward and aft of the beam on each side, as on the refit. Each has a
	// nozzle firing up from the rim's top face, one firing down from the bottom face, and a pair firing along the
	// rim tangent for yaw and fore-aft translation.
	{
		const float Angles[4] = { 41.f, -41.f, 139.f, -139.f };
		for (float Deg : Angles)
		{
			const float A = FMath::DegreesToRadians(Deg);
			const FVector Radial(FMath::Cos(A), FMath::Sin(A), 0.f);
			const FVector Tangent(-FMath::Sin(A), FMath::Cos(A), 0.f);
			const FVector OnRim = SaucerCentre + Radial * SaucerR * 0.985f;
			const FBox Column(OnRim - FVector(300.f, 300.f, 1500.f), OnRim + FVector(300.f, 300.f, 1500.f));
			const FVector Top = ProbeHull(FVector::UpVector, Column, OnRim + FVector(0.f, 0.f, 450.f));
			const FVector Bottom = ProbeHull(FVector::DownVector, Column, OnRim - FVector(0.f, 0.f, 450.f));
			// The rim band sits between the top and bottom faces just probed (about z 1440..1960 on this mesh, not
			// at the saucer's nominal centre height), so the tangential pair goes on its outer edge at mid-height.
			const float RimZ = 0.5f * (Top.Z + Bottom.Z);
			const FBox Edge(FVector(OnRim.X - 500.f, OnRim.Y - 500.f, RimZ - 400.f), FVector(OnRim.X + 500.f, OnRim.Y + 500.f, RimZ + 400.f));
			const FVector Rim = ProbeHull(Radial, Edge, FVector(SaucerCentre.X, SaucerCentre.Y, RimZ) + Radial * SaucerR);
			Nozzles.Add(FRcsNozzle(Top + Radial * 100.f, FVector::UpVector));
			Nozzles.Add(FRcsNozzle(Bottom + Radial * 100.f, FVector::DownVector));
			Nozzles.Add(FRcsNozzle(Rim + Tangent * 160.f, Tangent, 0.9f));
			Nozzles.Add(FRcsNozzle(Rim - Tangent * 160.f, -Tangent, 0.9f));
		}
	}
	// Deflector housing: four packages on the ring round the dish, each firing outward along its diagonal and
	// straight ahead (the forward-firing pair is what brakes the ship and backs it out of a dock). On this mesh
	// the dish face is at x ~3500..3800 (radius ~950 about z -1950) and the housing lip round it at x ~2700..3000.
	{
		const FVector Dish(2900.f, 0.f, -1900.f);
		for (int32 k = 0; k < 4; ++k)
		{
			const float A = FMath::DegreesToRadians(45.f + 90.f * k);
			const FVector Radial(0.f, FMath::Cos(A), FMath::Sin(A));
			const FVector Ring = Dish + Radial * 1350.f;
			const FBox Face(FVector(2300.f, Ring.Y - 400.f, Ring.Z - 400.f), FVector(3400.f, Ring.Y + 400.f, Ring.Z + 400.f));
			const FVector Front = ProbeHull(FVector::ForwardVector, Face, FVector(2900.f, Ring.Y, Ring.Z));
			Nozzles.Add(FRcsNozzle(Front + Radial * 120.f, Radial, 0.9f));
			Nozzles.Add(FRcsNozzle(Front + FVector(60.f, 0.f, 0.f), FVector::ForwardVector, 0.9f));
		}
	}
	// Nacelle aft caps: a package on the end of each nacelle firing aft (the main forward translation thrusters,
	// far from the centre so they also help pitch and yaw) and across.
	for (int32 Side = -1; Side <= 1; Side += 2)
	{
		const FBox Cap(FVector(-15600.f, Side < 0 ? -4600.f : 2800.f, 1200.f), FVector(-12500.f, Side < 0 ? -2800.f : 4600.f, 3400.f));
		const FVector Aft = ProbeHull(FVector::BackwardVector, Cap, FVector(-15100.f, Side * 3650.f, 2300.f));
		Nozzles.Add(FRcsNozzle(Aft, FVector::BackwardVector, 1.2f));
		Nozzles.Add(FRcsNozzle(Aft + FVector(400.f, Side * 700.f, 0.f), FVector(0.f, Side, 0.f), 0.9f));
		Nozzles.Add(FRcsNozzle(Aft + FVector(400.f, 0.f, 700.f), FVector::UpVector, 0.9f));
		Nozzles.Add(FRcsNozzle(Aft + FVector(400.f, 0.f, -700.f), FVector::DownVector, 0.9f));
	}
	// Every nozzle is then bedded on the nearest plating that faces its way, and its exhaust canted out of that
	// plating, so the plumes start on the skin rather than in the space beside it.
	int32 Snapped = 0;
	for (FRcsNozzle& N : Nozzles)
	{
		if (SnapToHull(N.Position, N.ExhaustDir, 900.f))
		{
			++Snapped;
		}
	}
	// The plumes are small next to the ship: scale with the hull so a different mesh keeps the proportion.
	Thrusters->PlumeLength = GetShipLengthCm() * 0.045f;
	Thrusters->PlumeWidth = GetShipLengthCm() * 0.009f;
	Thrusters->SetNozzles(Nozzles);
	UE_LOG(LogTemp, Log, TEXT("Starship thrusters: %d nozzles, %d bedded on the mesh (%d verts, %d normals)"), Nozzles.Num(), Snapped, HullVerts.Num(), HullNormals.Num());
}

void AStarshipPawn::SetupWeaponBanks()
{
	if (!Weapons)
	{
		return;
	}
	if (HullVerts.Num() == 0)
	{
		GatherHullVerts();
	}
	const float H = ShipHeightMeters * 100.f;
	const FVector SaucerCentre(8050.f, 0.f, 250.f);
	const float SaucerR = 7190.f;
	auto Skin = [&](const FVector& XY, bool bTop) -> FVector
	{
		// Full-height column: the saucer's underside is above z 500, so a column cut off at the nominal centre
		// height finds nothing there and the ventral banks would fall back into empty space.
		const FBox Column(FVector(XY.X - 250.f, XY.Y - 250.f, -H), FVector(XY.X + 250.f, XY.Y + 250.f, H));
		return ProbeHull(FVector(0, 0, bTop ? 1.f : -1.f), Column, FVector(XY.X, XY.Y, bTop ? 1800.f : 600.f));
	};
	auto AtAngle = [&](float Deg, float Frac) -> FVector
	{
		const float A = FMath::DegreesToRadians(Deg);
		return SaucerCentre + FVector(FMath::Cos(A), FMath::Sin(A), 0.f) * SaucerR * Frac;
	};
	// A saucer bank's field of fire is centred outward along its radial as much as away from the plating: the
	// emitters swivel, and what blocks them is the hull inboard, not the plane of the saucer. So a forward
	// target is taken by the forward banks top and bottom, one abeam by that side's quarter banks.
	auto Field = [](float Deg, bool bTop) -> FVector
	{
		const float A = FMath::DegreesToRadians(Deg);
		return FVector(FMath::Cos(A) * 0.85f, FMath::Sin(A) * 0.85f, bTop ? 0.5f : -0.5f).GetSafeNormal();
	};
	TArray<FPhaserBank> Banks;
	// Saucer, as on the refit: a bank on the centreline ahead of the superstructure and one on each forward
	// quarter, top and bottom; the underside also has a pair on the aft quarters, where the top has the impulse
	// deck instead.
	Banks.Add(FPhaserBank(TEXT("DorsalFwd"), Skin(AtAngle(0.f, 0.36f), true), Field(0.f, true)));
	Banks.Add(FPhaserBank(TEXT("DorsalPort"), Skin(AtAngle(-45.f, 0.80f), true), Field(-45.f, true)));
	Banks.Add(FPhaserBank(TEXT("DorsalStbd"), Skin(AtAngle(45.f, 0.80f), true), Field(45.f, true)));
	Banks.Add(FPhaserBank(TEXT("VentralFwd"), Skin(AtAngle(0.f, 0.36f), false), Field(0.f, false)));
	Banks.Add(FPhaserBank(TEXT("VentralPort"), Skin(AtAngle(-45.f, 0.80f), false), Field(-45.f, false)));
	Banks.Add(FPhaserBank(TEXT("VentralStbd"), Skin(AtAngle(45.f, 0.80f), false), Field(45.f, false)));
	Banks.Add(FPhaserBank(TEXT("VentralAftPort"), Skin(AtAngle(-135.f, 0.70f), false), Field(-135.f, false)));
	Banks.Add(FPhaserBank(TEXT("VentralAftStbd"), Skin(AtAngle(135.f, 0.70f), false), Field(135.f, false)));
	// Engineering hull: a bank on each stern flank beside the shuttlebay, covering astern.
	for (int32 Side = -1; Side <= 1; Side += 2)
	{
		const FBox Flank(FVector(-7600.f, Side < 0 ? -1700.f : 300.f, -1600.f), FVector(-6000.f, Side < 0 ? -300.f : 1700.f, 0.f));
		const FVector Normal = FVector(-0.75f, Side * 0.66f, 0.f).GetSafeNormal();
		const FVector P = ProbeHull(Normal, Flank, FVector(-7200.f, Side * 1100.f, -800.f));
		Banks.Add(FPhaserBank(Side < 0 ? TEXT("SternPort") : TEXT("SternStbd"), P, Normal));
	}
	// Bed every bank on the nearest plating that faces its way (the column probes can land on a vertex a few
	// metres off the skin where the mesh is sparse), then stand the emitter a little proud of it.
	for (FPhaserBank& B : Banks)
	{
		FVector Plating = B.Normal.Z > 0.3f ? FVector::UpVector : (B.Normal.Z < -0.3f ? FVector::DownVector : B.Normal);
		FVector P = B.Offset;
		if (SnapToHull(P, Plating, 900.f))
		{
			B.Offset = P + Plating * 30.f;
		}
	}
	Weapons->PhaserBanks = Banks;
	for (const FPhaserBank& B : Banks)
	{
		UE_LOG(LogTemp, Log, TEXT("Phaser bank %-14s pos %7.0f %7.0f %7.0f  field %5.2f %5.2f %5.2f"), *B.Name.ToString(), B.Offset.X, B.Offset.Y, B.Offset.Z, B.Normal.X, B.Normal.Y, B.Normal.Z);
	}
}

void AStarshipPawn::UpdateThrusters()
{
	if (!Thrusters || !Movement)
	{
		return;
	}
	// Attitude demand is the steering the flight model consumed this frame; at warp the field does the steering
	// and the thrusters stay quiet. Translation demand is the manoeuvring thrusters plus the vertical thrusters.
	const FVector Torque = Movement->bWarpActive ? FVector::ZeroVector : Movement->LastSteeringInput;
	FVector Force = Movement->LastThrusterInput;
	Force.Z = FMath::Clamp(Force.Z + Movement->LastVerticalInput, -1.f, 1.f);
	Thrusters->SetDemand(Torque, Force);
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
	if (Shield)
	{
		Shield->SetShellExtent(FVector(LengthCm * 0.6f, ShipWidthMeters * 100.f * 0.75f, ShipHeightMeters * 100.f * 1.3f));
	}
	if (TractorBeam)
	{
		// Deflector dish: forward on the secondary hull, below the centreline.
		TractorBeam->EmitterOffset = FVector(LengthCm * 0.22f, 0.f, -ShipHeightMeters * 100.f * 0.45f);
	}

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
		ApplyHullWindows(Scale, Rot, ShipMesh->GetRelativeLocation());
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
	// The warp view parameters must see this frame's camera: the spring arm applies its lag in its own tick
	// (post-physics) and the camera manager reads the camera components after every tick group, so push them
	// from a post-update tick that waits for the arm and cameras.
	if (WarpView)
	{
		WarpView->AddTickPrerequisiteComponent(ChaseArm);
		WarpView->AddTickPrerequisiteComponent(ChaseCamera);
		WarpView->AddTickPrerequisiteComponent(BridgeCamera);
		WarpView->AddTickPrerequisiteComponent(CinematicCamera);
	}
	ApplyHullDimensions();
	SetupHullMaterials();
	SetupHullLights();
	SetupRunningLights();
	SetupThrusters();
	SetupWeaponBanks();
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

void AStarshipPawn::UpdateWarpPostProcess()
{
	if (!WarpPPMID)
	{
		return;
	}
	const float Streak = LastStreak;
	const float Flash = LastFlash;
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

	// Camera frame, taken from the active camera component itself (this runs after the spring arm and the
	// cameras have ticked, and the camera manager will read the same transform for this frame). The classic
	// look wants the direction of travel as a screen point; the relativistic look works per pixel in view
	// space (x right, y up, z forward) and also needs the camera basis to get back to world space for the sky
	// it synthesises where the frame has no light to sample.
	UCameraComponent* Cam = GetActiveCamera();
	const FTransform CamXf = Cam ? Cam->GetComponentTransform() : GetActorTransform();
	const FVector CamLoc = CamXf.GetLocation();
	const FVector CamFwd = CamXf.GetUnitAxis(EAxis::X);
	const FVector CamRight = CamXf.GetUnitAxis(EAxis::Y);
	const FVector CamUp = CamXf.GetUnitAxis(EAxis::Z);
	auto ToView = [&](const FVector& W) { return FVector(W | CamRight, W | CamUp, W | CamFwd); };
	auto AsColor = [](const FVector& W) { return FLinearColor((float)W.X, (float)W.Y, (float)W.Z, 0.f); };

	// Classic: where the direction of travel lands on screen (viewport UV), projected with the camera's own
	// field of view. If it is behind the camera, use the anti-direction and flag it so the shader pushes outward
	// instead of pulling inward.
	float Aspect = 16.f / 9.f;
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		int32 SizeX = 0, SizeY = 0;
		PC->GetViewportSize(SizeX, SizeY);
		if (SizeX > 0 && SizeY > 0)
		{
			Aspect = (float)SizeX / (float)SizeY;
		}
	}
	const FVector TravelView = ToView(Travel);
	const float Behind = TravelView.Z >= 0.0 ? 0.f : 1.f;
	const FVector Dir = TravelView.Z >= 0.0 ? TravelView : -TravelView;
	const float TanHalfX = FMath::Tan(FMath::DegreesToRadians((Cam ? Cam->FieldOfView : 90.f) * 0.5f));
	const float TanHalfY = TanHalfX / FMath::Max(Aspect, 0.1f);
	const float Z = FMath::Max((float)Dir.Z, 0.05f);
	const float ForwardU = FMath::Clamp(0.5f + 0.5f * (float)Dir.X / Z / TanHalfX, -3.f, 4.f);
	const float ForwardV = FMath::Clamp(0.5f - 0.5f * (float)Dir.Y / Z / TanHalfY, -3.f, 4.f);

	// Effective beta for the relativistic look; see RelativisticMaxBeta.
	const double Speed = Movement ? Movement->GetSpeed() : 0.0;
	const double BetaV = Speed / 2.99792458e10;
	const float BetaEff = (float)FMath::Clamp(BetaV / (1.0 + BetaV), 0.0, (double)RelativisticMaxBeta);

	// The nearest star, so the shader can draw the sun where aberration puts it when its true bearing is outside
	// the frame and there is nothing to sample.
	FVector SunView(0.0, 0.0, 1.0);
	float SunAngle = 0.f, SunGlowAngle = 0.f;
	FLinearColor SunColor(0.f, 0.f, 0.f), SunGlowColor(0.f, 0.f, 0.f);
	{
		double Best = 1e300;
		const AStarBody* BestStar = nullptr;
		for (TActorIterator<AStarBody> It(GetWorld()); It; ++It)
		{
			const double D = FVector::DistSquared(It->GetActorLocation(), CamLoc);
			if (D < Best) { Best = D; BestStar = *It; }
		}
		if (BestStar)
		{
			const FVector To = BestStar->GetActorLocation() - CamLoc;
			const double Dist = To.Size();
			if (Dist > 1.0)
			{
				SunView = ToView(To / Dist);
				// Angular radius of the limb of a sphere is asin(R / d), not atan: they only agree far away.
				const double R = BestStar->GetRadiusCm();
				double Disc = FMath::Asin(FMath::Min(R / Dist, 1.0));
				double GlowR = Disc * 2.5;
				if (Dist > AStarBody::FarSpriteDistanceCm && DistantStars)
				{
					// Beyond the mesh hand-over the sun on screen is the distant-star sprite, which is enlarged for
					// legibility; draw the synthesised one the same size so it does not shrink when aberration
					// carries its true bearing out of the frame.
					const double Blend = FMath::Clamp((Dist - AStarBody::FarSpriteDistanceCm) / FMath::Max(DistantStars->ScaleFullDistance - AStarBody::FarSpriteDistanceCm, 1.0), 0.0, 1.0);
					Disc = R / Dist * FMath::Lerp(1.0, (double)DistantStars->ApparentSizeScale, Blend);
					GlowR = FMath::Max(Disc * (1.0 + DistantStars->GlowRadii), (double)DistantStars->MinAngularSize * 0.5);
				}
				SunAngle = (float)Disc;
				SunGlowAngle = (float)GlowR;
				SunColor = BestStar->StarColor * BestStar->Intensity;
				SunGlowColor = BestStar->CoronaColor * BestStar->CoronaIntensity * 8.f;
			}
		}
	}

	// Our own hull is drawn straight and must never be sampled as part of the universe. The hull lies within
	// half a ship length of the actor origin, so the exclusion follows the camera's distance from it rather
	// than a fixed depth, which the chase camera's zoom-out would otherwise exceed.
	{
		const float CamDist = (float)FVector::Dist(CamLoc, GetActorLocation());
		const float L = GetShipLengthCm();
		WarpPPMID->SetScalarParameterValue(TEXT("NearExcludeCm"), CamDist + 0.6f * L);
		WarpPPMID->SetScalarParameterValue(TEXT("FarIncludeCm"), CamDist + 1.1f * L);
	}
	WarpPPMID->SetScalarParameterValue(TEXT("Mode"), bRelativistic ? 1.f : 0.f);
	WarpPPMID->SetScalarParameterValue(TEXT("Strength"), Streak);
	WarpPPMID->SetScalarParameterValue(TEXT("Flash"), Flash);
	WarpPPMID->SetScalarParameterValue(TEXT("Behind"), Behind);
	WarpPPMID->SetScalarParameterValue(TEXT("Beta"), BetaEff);
	WarpPPMID->SetScalarParameterValue(TEXT("BlurStrength"), ClassicStreakStrength);
	WarpPPMID->SetScalarParameterValue(TEXT("ContractStrength"), RelativisticContraction);
	WarpPPMID->SetScalarParameterValue(TEXT("DopplerStrength"), RelativisticDoppler);
	WarpPPMID->SetScalarParameterValue(TEXT("SkyDepthCm"), (float)(StarSphereRadius * 0.5));
	WarpPPMID->SetScalarParameterValue(TEXT("SunAngle"), SunAngle);
	WarpPPMID->SetScalarParameterValue(TEXT("SunGlowAngle"), SunGlowAngle);
	WarpPPMID->SetVectorParameterValue(TEXT("SunColor"), SunColor);
	WarpPPMID->SetVectorParameterValue(TEXT("SunGlowColor"), SunGlowColor);
	WarpPPMID->SetVectorParameterValue(TEXT("ForwardUV"), FLinearColor(ForwardU, ForwardV, 0.f, 0.f));
	WarpPPMID->SetVectorParameterValue(TEXT("TravelView"), AsColor(TravelView));
	WarpPPMID->SetVectorParameterValue(TEXT("SunView"), AsColor(SunView));
	WarpPPMID->SetVectorParameterValue(TEXT("CamRight"), AsColor(CamRight));
	WarpPPMID->SetVectorParameterValue(TEXT("CamUp"), AsColor(CamUp));
	WarpPPMID->SetVectorParameterValue(TEXT("CamFwd"), AsColor(CamFwd));
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

void AStarshipPawn::ApplyHullWindows(float Scale, const FQuat& Rot, const FVector& RelLoc)
{
	if (!ShipMesh)
	{
		return;
	}
	if (!HullWindowsMID)
	{
		UMaterialInterface* Mat = HullWindowMaterial.LoadSynchronous();
		if (!Mat)
		{
			return;
		}
		HullWindowsMID = UMaterialInstanceDynamic::Create(Mat, this);
		ShipMesh->SetOverlayMaterial(HullWindowsMID);
	}
	auto V = [](const FVector& X) { return FLinearColor((float)X.X, (float)X.Y, (float)X.Z, 0.f); };
	// Where the saucer rim actually is: the mesh vertices on the outer 4% of the saucer radius give its height
	// and thickness, so the window band lands on the rim whatever the mesh's vertical layout.
	FVector SaucerCenter = WindowSaucerCenter;
	float SaucerThickness = WindowSaucerThickness;
	if (bAutoSaucerRim && ShipMesh->GetStaticMesh() && ShipMesh->GetStaticMesh()->GetRenderData() && ShipMesh->GetStaticMesh()->GetRenderData()->LODResources.Num() > 0)
	{
		const FPositionVertexBuffer& PB = ShipMesh->GetStaticMesh()->GetRenderData()->LODResources[0].VertexBuffers.PositionVertexBuffer;
		if (PB.GetVertexData() != nullptr && PB.GetNumVertices() > 0)
		{
			const FTransform MeshToPawn(Rot, RelLoc, FVector(Scale));
			double SumZ = 0.0, MinZ = 1e30, MaxZ = -1e30;
			int32 Count = 0;
			for (uint32 i = 0; i < PB.GetNumVertices(); ++i)
			{
				const FVector P = MeshToPawn.TransformPosition(FVector(PB.VertexPosition(i)));
				const double R = FVector2D(P.X - SaucerCenter.X, P.Y - SaucerCenter.Y).Size();
				if (R > WindowSaucerRadius * 0.96 && R < WindowSaucerRadius * 1.01 && FMath::Abs(P.Y) > WindowSaucerRadius * 0.3)
				{
					SumZ += P.Z; MinZ = FMath::Min(MinZ, P.Z); MaxZ = FMath::Max(MaxZ, P.Z); ++Count;
				}
			}
			if (Count >= 8)
			{
				SaucerCenter.Z = SumZ / Count;
				SaucerThickness = FMath::Max((float)(MaxZ - MinZ) * 0.5f, 300.f);
			}
			UE_LOG(LogTemp, Log, TEXT("Starship windows: saucer rim verts %d, z %.0f, thickness %.0f (mesh scale %.3f)"), Count, SaucerCenter.Z, SaucerThickness, Scale);
		}
	}
	// Mesh space -> pawn space, so the window zones can be stated in the same frame as the lights and floods.
	HullWindowsMID->SetScalarParameterValue(TEXT("MeshScale"), Scale);
	HullWindowsMID->SetVectorParameterValue(TEXT("MeshRelLoc"), V(RelLoc));
	HullWindowsMID->SetVectorParameterValue(TEXT("AxisX"), V(Rot.RotateVector(FVector::XAxisVector)));
	HullWindowsMID->SetVectorParameterValue(TEXT("AxisY"), V(Rot.RotateVector(FVector::YAxisVector)));
	HullWindowsMID->SetVectorParameterValue(TEXT("AxisZ"), V(Rot.RotateVector(FVector::ZAxisVector)));
	HullWindowsMID->SetVectorParameterValue(TEXT("WindowSaucerCenter"), V(SaucerCenter));
	HullWindowsMID->SetScalarParameterValue(TEXT("WindowSaucerRadius"), WindowSaucerRadius);
	HullWindowsMID->SetScalarParameterValue(TEXT("WindowSaucerThickness"), SaucerThickness);
	HullWindowsMID->SetScalarParameterValue(TEXT("WindowHullMaxY"), WindowHullMaxY);
	HullWindowsMID->SetScalarParameterValue(TEXT("WindowHullMaxZ"), WindowHullMaxZ);
	HullWindowsMID->SetScalarParameterValue(TEXT("WindowHullMinZ"), WindowHullMinZ);
	HullWindowsMID->SetScalarParameterValue(TEXT("WindowHullMinX"), WindowHullMinX);
	HullWindowsMID->SetScalarParameterValue(TEXT("WindowHullMaxX"), WindowHullMaxX);
	HullWindowsMID->SetVectorParameterValue(TEXT("WindowExcludeAMin"), V(WindowExcludeDeflectorMin));
	HullWindowsMID->SetVectorParameterValue(TEXT("WindowExcludeAMax"), V(WindowExcludeDeflectorMax));
	HullWindowsMID->SetVectorParameterValue(TEXT("WindowExcludeBMin"), V(WindowExcludeTorpedoMin));
	HullWindowsMID->SetVectorParameterValue(TEXT("WindowExcludeBMax"), V(WindowExcludeTorpedoMax));
	HullWindowsMID->SetVectorParameterValue(TEXT("PatchAMin"), V(DeflectorPatchMin)); HullWindowsMID->SetVectorParameterValue(TEXT("PatchAMax"), V(DeflectorPatchMax));
	HullWindowsMID->SetVectorParameterValue(TEXT("PatchADir"), FLinearColor(1.f, 0.f, 0.f, 0.f));
	HullWindowsMID->SetScalarParameterValue(TEXT("PatchAFacing"), DeflectorFacing);
	HullWindowsMID->SetScalarParameterValue(TEXT("BridgeRadius"), BridgeWindowRadius);
	HullWindowsMID->SetScalarParameterValue(TEXT("BridgeMinZ"), BridgeWindowMinZ);
	HullWindowsMID->SetScalarParameterValue(TEXT("BridgeMaxZ"), BridgeWindowMaxZ);
	HullWindowsMID->SetScalarParameterValue(TEXT("WindowPitch"), WindowPitch);
	HullWindowsMID->SetScalarParameterValue(TEXT("WindowDeck"), WindowDeck);
	HullWindowsMID->SetScalarParameterValue(TEXT("WindowGlow"), WindowGlow);
	HullWindowsMID->SetScalarParameterValue(TEXT("WindowOnFraction"), WindowOnFraction);
	HullWindowsMID->SetScalarParameterValue(TEXT("WindowPeriod"), WindowPeriod);
}


void AStarshipPawn::ApplyHullSkinParams()
{
	if (!ShipMesh)
	{
		return;
	}
	auto V = [](const FVector& X) { return FLinearColor((float)X.X, (float)X.Y, (float)X.Z, 0.f); };
	const float Scale = (float)ShipMesh->GetRelativeScale3D().X;
	const FQuat Rot = ShipMesh->GetRelativeRotation().Quaternion();
	const FVector RelLoc = ShipMesh->GetRelativeLocation();
	// The skin material keys lamps off the diffuse map; it needs the same mesh->pawn transform as the overlay for its boxes.
	for (int32 i = 0; i < HullMaterials.Num(); ++i)
	{
		UMaterialInstanceDynamic* Mid = HullMaterials[i];
		if (!Mid)
		{
			continue;
		}
		Mid->SetScalarParameterValue(TEXT("MeshScale"), Scale);
		Mid->SetVectorParameterValue(TEXT("MeshRelLoc"), V(RelLoc));
		Mid->SetVectorParameterValue(TEXT("AxisX"), V(Rot.RotateVector(FVector::XAxisVector)));
		Mid->SetVectorParameterValue(TEXT("AxisY"), V(Rot.RotateVector(FVector::YAxisVector)));
		Mid->SetVectorParameterValue(TEXT("AxisZ"), V(Rot.RotateVector(FVector::ZAxisVector)));
		if (i != ThrusterMaterialIndex)
		{
			Mid->SetScalarParameterValue(TEXT("BlueKeyGlow"), BlueKeyGlow);
			Mid->SetScalarParameterValue(TEXT("RedKeyGlow"), RedKeyGlow);
		}
		Mid->SetVectorParameterValue(TEXT("RedKeyAMin"), V(RedKeyTailMin)); Mid->SetVectorParameterValue(TEXT("RedKeyAMax"), V(RedKeyTailMax));
		Mid->SetVectorParameterValue(TEXT("RedKeyBMin"), V(TorpedoPatchMin)); Mid->SetVectorParameterValue(TEXT("RedKeyBMax"), V(TorpedoPatchMax));
		Mid->SetVectorParameterValue(TEXT("SlotBoxMin"), V(TorpedoPatchMin)); Mid->SetVectorParameterValue(TEXT("SlotBoxMax"), V(TorpedoPatchMax));
		Mid->SetVectorParameterValue(TEXT("SlotColor"), TorpedoBayGlowColor);
	}
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
	UMaterialInterface* Skin = HullSkinMaterial.LoadSynchronous();
	for (int32 i = 0; i < NumMaterials; ++i)
	{
		UMaterialInterface* Orig = ShipMesh->GetMaterial(i);
		UMaterialInstanceDynamic* Mid = nullptr;
		if (Skin && Orig)
		{
			// Our own skin material, fed the imported slot's textures.
			Mid = UMaterialInstanceDynamic::Create(Skin, this);
			static const TPair<FName, FName> TexParams[] = { { TEXT("DiffuseColorMap"), TEXT("Diffuse") }, { TEXT("EmissiveColorMap"), TEXT("Glow") }, { TEXT("NormalMap"), TEXT("Normal") } };
			for (const auto& Pair : TexParams)
			{
				UTexture* Tex = nullptr;
				if (Orig->GetTextureParameterValue(FHashedMaterialParameterInfo(Pair.Key), Tex) && Tex)
				{
					Mid->SetTextureParameterValue(Pair.Value, Tex);
				}
			}
			// Roughness (from the set's specular map) and baked occlusion, by slot name: the imported instances
			// never referenced them.
			const FString SlotName = ShipMesh->GetMaterialSlotNames().IsValidIndex(i) ? ShipMesh->GetMaterialSlotNames()[i].ToString() : FString();
			if (SlotName.StartsWith(TEXT("Constitution_")))
			{
				const FString Base = FString::Printf(TEXT("/Game/Enterprise/Fed_TMP_%s_"), *SlotName);
				if (UTexture* Rough = LoadObject<UTexture>(nullptr, *(Base + TEXT("ROUGH"))))
				{
					Mid->SetTextureParameterValue(TEXT("Rough"), Rough);
				}
				if (UTexture* AO = LoadObject<UTexture>(nullptr, *(Base + TEXT("AO"))))
				{
					Mid->SetTextureParameterValue(TEXT("AO"), AO);
				}
			}
			else
			{
				// The impulse grille has no such maps: plain roughness, no occlusion.
				Mid->SetScalarParameterValue(TEXT("RoughMapWeight"), 0.f);
				Mid->SetScalarParameterValue(TEXT("AOStrength"), 0.f);
			}
			ShipMesh->SetMaterial(i, Mid);
		}
		else
		{
			Mid = ShipMesh->CreateAndSetMaterialInstanceDynamic(i);
		}
		HullMaterials.Add(Mid);
	}
	ThrusterMaterialIndex = ShipMesh->GetMaterialIndex(ThrusterSlotName);
	if (HullMaterials.IsValidIndex(ThrusterMaterialIndex) && HullMaterials[ThrusterMaterialIndex])
	{
		// The grille's plating is red paint, not a lamp: no colour keying there, its glow map is the lamp.
		HullMaterials[ThrusterMaterialIndex]->SetScalarParameterValue(TEXT("BlueKeyGlow"), 0.f);
		HullMaterials[ThrusterMaterialIndex]->SetScalarParameterValue(TEXT("RedKeyGlow"), 0.f);
	}
	ApplyHullSkinParams();
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
	if (Shield)
	{
		const FVector Point = Hit.ImpactPoint.IsNearlyZero() ? GetActorLocation() + GetActorForwardVector() * GetShipLengthCm() * 0.5f : FVector(Hit.ImpactPoint);
		Shield->RegisterImpact(Point, 1.5f);
	}
	ShieldStrength = FMath::Max(ShieldStrength - 4.f, 0.f);
	ShieldRechargeTimer = ShieldRechargeDelay;
}

float AStarshipPawn::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	const float Applied = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (Applied <= 0.f)
	{
		return Applied;
	}
	FVector Point = GetActorLocation();
	if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
	{
		Point = static_cast<const FPointDamageEvent&>(DamageEvent).HitInfo.ImpactPoint;
	}
	else if (DamageEvent.IsOfType(FRadialDamageEvent::ClassID))
	{
		Point = static_cast<const FRadialDamageEvent&>(DamageEvent).Origin;
	}
	else if (DamageCauser)
	{
		Point = DamageCauser->GetActorLocation();
	}
	if (Shield && ShieldStrength > 0.f)
	{
		Shield->RegisterImpact(Point, FMath::Clamp(Applied / 10.f, 0.3f, 3.f));
	}
	const float Absorbed = FMath::Min(ShieldStrength, Applied);
	ShieldStrength -= Absorbed;
	HullIntegrity = FMath::Max(HullIntegrity - (Applied - Absorbed) * 0.5f, 1.f);
	ShieldRechargeTimer = ShieldRechargeDelay;
	AddCameraShake(FMath::Clamp(Applied / 40.f, 0.05f, 0.6f));
	return Applied;
}

void AStarshipPawn::ToggleTractorBeam()
{
	if (!TractorBeam)
	{
		return;
	}
	if (TractorBeam->IsBeamActive())
	{
		TractorBeam->Release();
		return;
	}
	AActor* Candidate = Weapons ? Weapons->LockedTarget.Get() : nullptr;
	if (!Candidate)
	{
		APlayerController* PC = Cast<APlayerController>(GetController());
		AStarshipHUD* H = PC ? Cast<AStarshipHUD>(PC->GetHUD()) : nullptr;
		FNavObject Info;
		if (H && H->GetNavTargetInfo(Info))
		{
			Candidate = Info.Actor;
		}
	}
	TractorBeam->Engage(Candidate);
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
	if (RunningLights) { RunningLights->SetVisibility(Mode != EStarshipCameraMode::Bridge); }
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

void AStarshipPawn::AdjustChaseZoom(float Steps)
{
	// Each wheel notch moves the chase distance by ~15%; the zoom sticks until R resets it.
	ChaseZoom = FMath::Clamp(ChaseZoom * FMath::Pow(0.85f, Steps), MinChaseZoom, MaxChaseZoom);
}

void AStarshipPawn::ResetCamera()
{
	FreeLookAngles = FVector2D::ZeroVector;
	ChaseZoom = 1.f;
	SetCameraMode(EStarshipCameraMode::Chase);
}

void AStarshipPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	ProcessDebugCommand();
	ShieldRechargeTimer -= DeltaTime;
	if (ShieldRechargeTimer <= 0.f && ShieldStrength < MaxShieldStrength)
	{
		ShieldStrength = FMath::Min(ShieldStrength + ShieldRechargePerSecond * DeltaTime, MaxShieldStrength);
	}
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
	if (DebugThrustTime > 0.f && Movement)
	{
		DebugThrustTime -= DeltaTime;
		Movement->SetThrusterInput(DebugThrust);
	}
	UpdateWarpEffects(DeltaTime);
	UpdateHullGlow(DeltaTime);
	UpdateThrusters();
	if (BridgeInterior && CameraMode == EStarshipCameraMode::Bridge)
	{
		const float WarpFrac = WarpDrive ? FMath::Clamp(WarpDrive->WarpFactor / 9.f, 0.f, 1.f) * (WarpDrive->GetState() == EWarpState::AtWarp ? 1.f : 0.3f) : 0.f;
		BridgeInterior->SetLiveReadouts(GetShieldFraction(), GetHullFraction(), Movement ? (float)Movement->Throttle : 0.f, WarpFrac, ShieldStrength < MaxShieldStrength * 0.25f);
	}
	UpdateCameras(DeltaTime);

	// Torpedo launch kick.
	if (Weapons)
	{
		const int32 Fired = Weapons->GetTorpedoesFired();
		if (Fired != LastTorpedoesFired)
		{
			AddCameraShake(0.25f);
		}
		LastTorpedoesFired = Fired;
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

	// Relativistic mode has no cartoon star streaks; the scene itself does the work. They must be fully off,
	// not merely faint: the streak cylinders write colour but no depth, so the relativistic view would take
	// them for the universe and draw a contracted copy of the whole tunnel in front of the ship.
	WarpStreaks->SetIntensity(WarpVisualMode == EWarpVisualMode::Relativistic ? 0.f : Streak);
	// The post-process parameters depend on the final camera transform, so WarpView pushes them after the
	// cameras have updated (see UpdateWarpPostProcess).
	LastStreak = Streak;
	LastFlash = Flash;
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
	const float ThrottleSigned = Movement ? (float)Movement->Throttle : 0.f;
	const float Throttle = FMath::Abs(ThrottleSigned);
	const bool bReverse = ThrottleSigned < -0.01f;
	// The impulse grille's own emissive follows the throttle either way; in reverse its glow map and plating
	// are recoloured blue, so the exhaust reads blue whether it is in shadow or in sunlight.
	const float ImpulseGlow = ImpulseGlowIdle + ImpulseGlowFull * Throttle;
	const float Reverse = bReverse ? FMath::Clamp(Throttle / 0.1f, 0.f, 1.f) : 0.f;
	if (HullMaterials.IsValidIndex(ThrusterMaterialIndex) && HullMaterials[ThrusterMaterialIndex])
	{
		UMaterialInstanceDynamic* Mid = HullMaterials[ThrusterMaterialIndex];
		Mid->SetScalarParameterValue(TEXT("GlowRecolor"), Reverse);
		Mid->SetVectorParameterValue(TEXT("GlowTint"), ReverseGlowColor);
		Mid->SetScalarParameterValue(TEXT("BaseRecolor"), Reverse);
		Mid->SetVectorParameterValue(TEXT("BaseTint"), ReverseBaseTint);
	}
	if (HullWindowsMID)
	{
		const float Ready = Weapons ? Weapons->GetTorpedoReadiness() : 1.f;
		HullWindowsMID->SetVectorParameterValue(TEXT("PatchAColor"), DeflectorGlowColor);
		HullWindowsMID->SetScalarParameterValue(TEXT("PatchAIntensity"), DeflectorGlow * (0.7f + 0.3f * Glow));
		for (UMaterialInstanceDynamic* Mid : HullMaterials)
		{
			if (Mid) { Mid->SetScalarParameterValue(TEXT("SlotGlow"), TorpedoBayGlow * (0.3f + 0.7f * FMath::Clamp(Ready, 0.f, 1.f))); }
		}
	}

	const TArray<FName> SlotNames = ShipMesh ? ShipMesh->GetMaterialSlotNames() : TArray<FName>();
	for (int32 i = 0; i < HullMaterials.Num(); ++i)
	{
		if (UMaterialInstanceDynamic* Mid = HullMaterials[i])
		{
			const float* SlotWeight = SlotNames.IsValidIndex(i) ? SlotGlowWeights.Find(SlotNames[i]) : nullptr;
			const float Slot = SlotWeight ? *SlotWeight : 1.f;
			const float Weight = (i == ThrusterMaterialIndex) ? BaseGlowWeight * ImpulseGlow : BaseGlowWeight * Slot * Glow;
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
		ChaseArm->TargetArmLength = ChaseArmBaseLength * ChaseZoom * (1.f + WarpArmLengthBoost * Streak);
		ChaseArm->SocketOffset = ChaseArmBaseSocketOffset * ChaseZoom + ShakeOffset * Amp * LengthCm * 0.01f;
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
	else if (Op == TEXT("bridge") && BridgeInterior)
	{
		const FString Which = ArgStr(1);
		BridgeInterior->SetStyle(Which == TEXT("original") || Which == TEXT("tos") ? EBridgeStyle::Original : (Which == TEXT("refit") || Which == TEXT("prims") ? EBridgeStyle::Refit : EBridgeStyle::RefitMesh));
		BridgeInterior->SetVisibility(CameraMode == EStarshipCameraMode::Bridge, true);
	}
	else if (Op == TEXT("rcs"))
	{
		if (ArgStr(1) == TEXT("all")) { if (Thrusters) { Thrusters->SetAllOn(Arg(2, 3.f)); } }
		else if (ArgStr(1) == TEXT("dump") && Thrusters)
		{
			int32 i = 0;
			for (const FRcsNozzle& N : Thrusters->GetNozzles())
			{
				UE_LOG(LogTemp, Log, TEXT("RCS nozzle %2d  pos %7.0f %7.0f %7.0f  exhaust %5.2f %5.2f %5.2f"), i++, N.Position.X, N.Position.Y, N.Position.Z, N.ExhaustDir.X, N.ExhaustDir.Y, N.ExhaustDir.Z);
			}
		}
		else { DebugThrust = FVector(Arg(1, 0.f), Arg(2, 0.f), Arg(3, 0.f)); DebugThrustTime = Arg(4, 2.f); }
	}
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
	else if (Op == TEXT("target") && Weapons) { if (ArgStr(1) == TEXT("clear")) { Weapons->ClearTarget(); } else if (ArgStr(1) == TEXT("nearest")) { Weapons->LockNearest(); } else { Weapons->CycleTarget(); } }
	else if (Op == TEXT("zoom")) { ChaseZoom = FMath::Clamp(Arg(1, 1.f), MinChaseZoom, MaxChaseZoom); }
	else if (Op == TEXT("meshdump")) { DumpHullMesh(); }
	else if (Op == TEXT("slotbox") && Args.Num() >= 7)
	{
		// Debug: move the torpedo launcher box (pawn space) and re-apply the skin parameters.
		TorpedoPatchMin = FVector(Arg(1, 0.f), Arg(2, 0.f), Arg(3, 0.f));
		TorpedoPatchMax = FVector(Arg(4, 0.f), Arg(5, 0.f), Arg(6, 0.f));
		ApplyHullSkinParams();
	}
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
	else if (Op == TEXT("tractor")) { ToggleTractorBeam(); }
	else if (Op == TEXT("shipwarp"))
	{
		// shipwarp [km] [far]: send the nearest (or farthest) patrolling ship to warp, to another station or, with a
		// distance, to a point that far ahead of this ship and two lengths to starboard (debug / demo).
		const bool bFar = ArgStr(2) == TEXT("far") || ArgStr(1) == TEXT("far");
		AProceduralStarship* Best = nullptr;
		double BestD = bFar ? -1.0 : 1e300;
		for (TActorIterator<AProceduralStarship> It(GetWorld()); It; ++It)
		{
			if (It->IsInWarp() || It->IsDying()) { continue; }
			const double D = FVector::DistSquared(It->GetActorLocation(), GetActorLocation());
			if (bFar ? D > BestD : D < BestD) { BestD = D; Best = *It; }
		}
		const float Km = Arg(1, 0.f);
		if (Best && Km > 0.f)
		{
			Best->WarpTo(GetActorLocation() + GetActorForwardVector() * Km * 1.0e5 + GetActorRightVector() * GetShipLengthCm() * 2.f);
		}
		else if (Best)
		{
			Best->WarpToRandomStation();
		}
	}
	else if (Op == TEXT("cscale")) { WarpVisualLightSpeedScale = FMath::Clamp(Arg(1, 1.f), 1e-7f, 1.f); }
	else if (Op == TEXT("hit"))
	{
		// Test: flare the shields as if struck from the left.
		FPointDamageEvent E(Arg(1, 15.f), FHitResult(), -GetActorRightVector(), nullptr);
		E.HitInfo.ImpactPoint = GetActorLocation() + GetActorRightVector() * ShipWidthMeters * 60.f;
		TakeDamage(Arg(1, 15.f), E, nullptr, nullptr);
	}
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

void AStarshipPawn::DumpHullMesh() const
{
	if (!ShipMesh || !ShipMesh->GetStaticMesh() || !ShipMesh->GetStaticMesh()->GetRenderData() || ShipMesh->GetStaticMesh()->GetRenderData()->LODResources.Num() == 0)
	{
		return;
	}
	const FStaticMeshLODResources& LOD = ShipMesh->GetStaticMesh()->GetRenderData()->LODResources[0];
	const FPositionVertexBuffer& PB = LOD.VertexBuffers.PositionVertexBuffer;
	if (PB.GetVertexData() == nullptr || PB.GetNumVertices() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("meshdump: vertex data not CPU accessible"));
		return;
	}
	TArray<int32> Section;
	Section.Init(-1, PB.GetNumVertices());
	const FRawStaticIndexBuffer& IB = LOD.IndexBuffer;
	if (IB.GetNumIndices() > 0 && IB.GetAllowCPUAccess())
	{
		for (const FStaticMeshSection& Sec : LOD.Sections)
		{
			for (uint32 i = Sec.FirstIndex; i < Sec.FirstIndex + Sec.NumTriangles * 3 && i < (uint32)IB.GetNumIndices(); ++i)
			{
				const uint32 Vi = IB.GetIndex(i);
				if (Section.IsValidIndex(Vi)) { Section[Vi] = Sec.MaterialIndex; }
			}
		}
	}
	const FTransform MeshToPawn = ShipMesh->GetRelativeTransform();
	FString Out;
	Out.Reserve(PB.GetNumVertices() * 28);
	const TArray<FName> Slots = ShipMesh->GetMaterialSlotNames();
	Out += TEXT("# slots:");
	for (int32 i = 0; i < Slots.Num(); ++i) { Out += FString::Printf(TEXT(" %d=%s"), i, *Slots[i].ToString()); }
	Out += TEXT("\n");
	for (uint32 i = 0; i < PB.GetNumVertices(); ++i)
	{
		const FVector P = MeshToPawn.TransformPosition(FVector(PB.VertexPosition(i)));
		Out += FString::Printf(TEXT("%.0f,%.0f,%.0f,%d\n"), P.X, P.Y, P.Z, Section[i]);
	}
	const FString Path = FPaths::ProjectSavedDir() / TEXT("meshdump.csv");
	FFileHelper::SaveStringToFile(Out, *Path);
	UE_LOG(LogTemp, Log, TEXT("meshdump: %u vertices -> %s"), PB.GetNumVertices(), *Path);
}
