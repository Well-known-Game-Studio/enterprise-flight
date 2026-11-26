#include "StarBody.h"
#include "SlonkTiming.h"
#include "GameClock.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "PlanetBody.h"
#include "SlonkMaterials.h"
#include "UObject/ConstructorHelpers.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"

AStarBody::AStarBody()
{
	PrimaryActorTick.bCanEverTick = true;

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	SetRootComponent(Body);
	Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Body->SetCollisionObjectType(ECC_WorldStatic);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	CollisionSphere = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionSphere"));
	CollisionSphere->SetupAttachment(Body);
	CollisionSphere->SetUsingAbsoluteScale(true);
	CollisionSphere->SetMobility(EComponentMobility::Movable);
	CollisionSphere->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CollisionSphere->SetCollisionObjectType(ECC_WorldStatic);
	CollisionSphere->SetCollisionResponseToAllChannels(ECR_Block);
	CollisionSphere->SetGenerateOverlapEvents(false);
	CollisionSphere->SetHiddenInGame(true);
	Body->SetMobility(EComponentMobility::Movable);
	Body->CastShadow = false;
	Body->bCastDynamicShadow = false;
	Body->bAffectDistanceFieldLighting = false;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/ArtTools/RenderToTexture/Meshes/S_1_Unit_Sphere_HI.S_1_Unit_Sphere_HI"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> FallbackSphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UStaticMesh* Mesh = nullptr;
	if (SphereFinder.Succeeded()) { Mesh = SphereFinder.Object; MeshUnitRadius = 1.f; }
	else if (FallbackSphereFinder.Succeeded()) { Mesh = FallbackSphereFinder.Object; MeshUnitRadius = 50.f; }
	if (Mesh) { Body->SetStaticMesh(Mesh); }

	Corona = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Corona"));
	Corona->SetupAttachment(Body);
	Corona->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Corona->SetMobility(EComponentMobility::Movable);
	Corona->CastShadow = false;
	Corona->SetTranslucentSortPriority(2);
	Corona->SetUsingAbsoluteRotation(true);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneFinder(TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneFinder.Succeeded()) { Corona->SetStaticMesh(PlaneFinder.Object); }

	// A young star's disk (an engine plane in its equator) and its jets (engine cones from its centre out along its
	// axis), in absolute scale; hidden unless it is young (ApplyYoung).
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeFinder(TEXT("/Engine/BasicShapes/Cone.Cone"));
	auto Part = [this](const TCHAR* Name, UStaticMesh* Mesh)
	{
		UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		C->SetupAttachment(Body);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetMobility(EComponentMobility::Movable);
		C->SetUsingAbsoluteScale(true);
		C->SetUsingAbsoluteLocation(true);
		C->SetUsingAbsoluteRotation(true);
		C->CastShadow = false;
		C->SetVisibility(false);
		if (Mesh) { C->SetStaticMesh(Mesh); }
		return C;
	};
	Disk = Part(TEXT("Disk"), PlaneFinder.Succeeded() ? PlaneFinder.Object : nullptr);
	JetNorth = Part(TEXT("JetNorth"), ConeFinder.Succeeded() ? ConeFinder.Object : nullptr);
	JetSouth = Part(TEXT("JetSouth"), ConeFinder.Succeeded() ? ConeFinder.Object : nullptr);

	StarMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Slonk/Materials/M_Star.M_Star")));
	CoronaMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Slonk/Materials/M_StarGlow.M_StarGlow")));
	EjectaSystem = TSoftObjectPtr<UNiagaraSystem>(FSoftObjectPath(TEXT("/Game/Slonk/FX/NS_StarEjecta.NS_StarEjecta")));

	Tags.AddUnique(APlanetBody::StarTag);
}

void AStarBody::ApplyAppearance()
{
	Tags.AddUnique(APlanetBody::StarTag);
	// Older saved instances may still reference the retired corona shell material.
	if (CoronaMaterial.ToString().Contains(TEXT("M_StarCorona")))
	{
		CoronaMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Slonk/Materials/M_StarGlow.M_StarGlow")));
		CoronaMID = nullptr;
	}
	Body->SetWorldScale3D(FVector(GetRadiusCm() / MeshUnitRadius));
	if (CollisionSphere) { CollisionSphere->SetSphereRadius((float)GetRadiusCm(), false); }
	// The plane is 100 cm wide in local units; the glow spans (1 + CoronaDepth) radii each side of the centre.
	Corona->SetWorldScale3D(FVector(GetRadiusCm() * 2.0 * (1.0 + CoronaDepth) / 100.0));

	// MIDs are serialized with the actor, so a saved instance can carry one made from a retired parent material.
	auto EnsureMID = [this](TObjectPtr<UMaterialInstanceDynamic>& Slot, const TSoftObjectPtr<UMaterialInterface>& Source, const TCHAR* DebugName)
	{
		UMaterialInterface* Mat = SlonkMaterials::LoadOrFallback(Source, DebugName);
		if (Mat && (!Slot || Slot->Parent != Mat))
		{
			Slot = UMaterialInstanceDynamic::Create(Mat, this);
		}
	};
	EnsureMID(StarMID, StarMaterial, TEXT("Star"));
	EnsureMID(CoronaMID, CoronaMaterial, TEXT("StarGlow"));
	if (StarMID)
	{
		if (Body->GetMaterial(0) != StarMID) { Body->SetMaterial(0, StarMID); }
		StarMID->SetVectorParameterValue(TEXT("StarColor"), StarColor);
		StarMID->SetScalarParameterValue(TEXT("Intensity"), Intensity);
		StarMID->SetScalarParameterValue(TEXT("LimbDarkening"), LimbDarkening);
		StarMID->SetScalarParameterValue(TEXT("GranuleScale"), GranuleScale);
		StarMID->SetScalarParameterValue(TEXT("Activity"), Activity);
		StarMID->SetScalarParameterValue(TEXT("Seed"), (float)(GetTypeHash(StarName) % 997u));
	}
	if (CoronaMID)
	{
		if (Corona->GetMaterial(0) != CoronaMID) { Corona->SetMaterial(0, CoronaMID); }
		CoronaMID->SetVectorParameterValue(TEXT("CoronaColor"), CoronaColor);
		CoronaMID->SetScalarParameterValue(TEXT("Intensity"), CoronaIntensity);
		CoronaMID->SetScalarParameterValue(TEXT("Falloff"), CoronaFalloff);
		// Where the photosphere disc ends inside the sprite (0..1 of the half-width).
		CoronaMID->SetScalarParameterValue(TEXT("DiscFraction"), 1.0f / (1.0f + CoronaDepth));
		// For the near look (the chromosphere, prominences and streamers): the star's size, its spots' activity and
		// its own seed, as the photosphere has them.
		CoronaMID->SetScalarParameterValue(TEXT("StarRadiusCm"), (float)GetRadiusCm());
		CoronaMID->SetScalarParameterValue(TEXT("Activity"), Activity);
		CoronaMID->SetScalarParameterValue(TEXT("Seed"), (float)(GetTypeHash(StarName) % 997u));
	}
	ApplyYoung();
}

void AStarBody::ApplyYoung()
{
	if (!Disk || !JetNorth || !JetSouth) { return; }
	// (Seen from any distance: beyond the star's own horizon they are drawn shrunk toward the camera, PlaceYoung.)
	Disk->SetVisibility(bYoung);
	JetNorth->SetVisibility(bYoung);
	JetSouth->SetVisibility(bYoung);
	if (!bYoung) { return; }
	constexpr double AuCm = 1.495978707e13;
	const double Outer = FMath::Max((double)DiskOuterAu, 0.2) * AuCm;
	auto Mat = [this](TObjectPtr<UMaterialInstanceDynamic>& Slot, const TCHAR* Path, const TCHAR* Debug)
	{
		if (!Slot)
		{
			if (UMaterialInterface* M = SlonkMaterials::LoadOrFallback(TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(Path)), Debug)) { Slot = UMaterialInstanceDynamic::Create(M, this); }
		}
		return Slot.Get();
	};
	if (UMaterialInstanceDynamic* D = Mat(DiskMID, TEXT("/Game/Slonk/Materials/M_StarDisk.M_StarDisk"), TEXT("StarDisk")))
	{
		Disk->SetMaterial(0, D);
		// The inner edge a few tenths of an AU out, the star's own colour on the dust, and its gaps by its name.
		D->SetScalarParameterValue(TEXT("InnerFrac"), (float)FMath::Clamp((0.05 * AuCm + GetRadiusCm() * 8.0) / Outer, 0.01, 0.3));
		D->SetScalarParameterValue(TEXT("GapCount"), (float)DiskGaps);
		D->SetScalarParameterValue(TEXT("Seed"), (float)(GetTypeHash(StarName) % 997u));
		D->SetVectorParameterValue(TEXT("StarColor"), StarColor);
	}
	if (UMaterialInstanceDynamic* J = Mat(JetMID, TEXT("/Game/Slonk/Materials/M_StarJet.M_StarJet"), TEXT("StarJet")))
	{
		JetNorth->SetMaterial(0, J);
		JetSouth->SetMaterial(0, J);
	}
}

void AStarBody::PlaceYoung(const FVector& ViewLoc)
{
	if (!bYoung || !Disk || !JetNorth || !JetSouth) { return; }
	constexpr double AuCm = 1.495978707e13;
	const FVector Centre = GetActorLocation();
	const double Dist = FVector::Dist(Centre, ViewLoc);
	// Everything at k of its size and k of the way from the camera (the same angles), k such that the farthest
	// reach of the disk or a jet is within the proxy distance.
	const double Reach = Dist + FMath::Max((double)DiskOuterAu, (double)JetLengthAu) * AuCm;
	const double K = FMath::Min(1.0, YoungProxyCm / FMath::Max(Reach, 1.0));
	const FVector At = ViewLoc + (Centre - ViewLoc) * K;
	const FQuat Q = GetActorQuat();
	const FVector Up = Q.GetUpVector();
	const double Outer = FMath::Max((double)DiskOuterAu, 0.2) * AuCm;
	// The plane is 100 cm across and faces +Z, the star's axis: laid in its equator.
	Disk->SetWorldLocationAndRotation(At, Q);
	Disk->SetWorldScale3D(FVector(Outer * 2.0 * K / 100.0));
	// The cone is 100 cm tall, its point at +50 z and its base, 50 cm across, at -50: each jet's point at the centre.
	const double L = FMath::Max((double)JetLengthAu, 0.2) * AuCm;
	const double W = L * 0.035;
	JetNorth->SetWorldLocationAndRotation(At + Up * (L * 0.5 * K), Q * FQuat(FVector::RightVector, PI));
	JetNorth->SetWorldScale3D(FVector(W * K / 100.0, W * K / 100.0, L * K / 100.0));
	JetSouth->SetWorldLocationAndRotation(At - Up * (L * 0.5 * K), Q);
	JetSouth->SetWorldScale3D(FVector(W * K / 100.0, W * K / 100.0, L * K / 100.0));
}

void AStarBody::SetBeyondHorizon(bool bBeyond)
{
	bBeyondHorizon = bBeyond;
	SetActorHiddenInGame(bBeyond && !bYoung);   // (a young star's disk and jets are drawn from any distance: only its body and glow hide)
#if WITH_EDITOR
	if (GetWorld() && !GetWorld()->IsGameWorld())
	{
		SetIsTemporarilyHiddenInEditor(bBeyond);
	}
#endif
	if (Body) { Body->SetVisibility(!bBeyond); }
	if (Corona) { Corona->SetVisibility(!bBeyond); }
	// (A young star's disk and jets stay: they are placed for the camera, PlaceYoung.)
	if (bYoung && Disk && JetNorth && JetSouth) { Disk->SetVisibility(true); JetNorth->SetVisibility(true); JetSouth->SetVisibility(true); }
}

void AStarBody::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyAppearance();
}

void AStarBody::PreRegisterAllComponents()
{
	Super::PreRegisterAllComponents();
	// Hide before any scene proxy can exist (see IFarBody).
	SetBeyondHorizon(IsLocationBeyondHorizon(PreRegistrationLocation(this)));
}

void AStarBody::BeginPlay()
{
	Super::BeginPlay();
	ApplyAppearance();
	// Its ejecta: one component at the centre, in cm about it (absolute scale: the body's is the radius), turning
	// with the star. Game worlds only.
	if (GetWorld() && GetWorld()->IsGameWorld())
	{
		if (UNiagaraSystem* Sys = EjectaSystem.LoadSynchronous())
		{
			Ejecta = NewObject<UNiagaraComponent>(this, TEXT("Ejecta"), RF_Transient);
			Ejecta->SetupAttachment(Body);
			Ejecta->SetUsingAbsoluteScale(true);
			Ejecta->SetWorldScale3D(FVector::OneVector);
			Ejecta->SetAsset(Sys);
			Ejecta->SetCastShadow(false);
			Ejecta->SetAutoActivate(true);
			Ejecta->RegisterComponent();
			const float R = (float)GetRadiusCm();
			Ejecta->SetVariableFloat(TEXT("Radius"), R);
			Ejecta->SetVariableFloat(TEXT("WindRate"), 6.f + 14.f * Activity);
			Ejecta->SetVariableLinearColor(TEXT("WindColor"), FLinearColor(CoronaColor.R * 0.9f, CoronaColor.G * 0.9f, CoronaColor.B * 0.9f, 0.22f));
			Ejecta->SetVariableLinearColor(TEXT("EjectaColor"), FLinearColor(3.2f, 2.7f, 2.2f, 0.3f));
			Ejecta->SetVariableFloat(TEXT("EjectaRate"), 0.f);
		}
		EjectaRng.Initialize((int32)(GetTypeHash(StarName) ^ 0x5bd1e995u));
		NextEjectaIn = EjectaRng.FRandRange(20.f, 90.f);
	}
}

void AStarBody::TriggerEjecta()
{
	if (!Ejecta) { return; }
	// From an activity belt, either side of the equator, anywhere round: a point on the surface in the star's frame.
	const float Lat = (EjectaRng.FRand() < 0.5f ? -1.f : 1.f) * EjectaRng.FRandRange(0.1f, 0.55f);
	const float Lon = EjectaRng.FRandRange(0.f, 2.f * PI);
	const FVector Dir(FMath::Cos(Lat) * FMath::Cos(Lon), FMath::Cos(Lat) * FMath::Sin(Lon), FMath::Sin(Lat));
	Ejecta->SetVariableVec3(TEXT("EjectaPos"), Dir * GetRadiusCm());
	Ejecta->SetVariableFloat(TEXT("EjectaRate"), 500.f);
	EjectaLeft = 1.6f;
	UE_LOG(LogTemp, Log, TEXT("Star %s: an ejection at latitude %.0f"), *StarName, FMath::RadiansToDegrees(Lat));
}

void AStarBody::TriggerEjectaSeenFrom(const FVector& ViewLocation)
{
	if (!Ejecta) { return; }
	// A point on the limb as seen from there: square to the line of sight, at a random angle round it, in the star's frame.
	const FVector Los = (GetActorLocation() - ViewLocation).GetSafeNormal();
	FVector A = FVector::CrossProduct(Los, GetActorUpVector()).GetSafeNormal();
	if (A.IsNearlyZero()) { A = FVector::CrossProduct(Los, FVector::ForwardVector).GetSafeNormal(); }
	const FVector B = FVector::CrossProduct(Los, A);
	const float Ang = EjectaRng.FRandRange(0.f, 2.f * PI);
	const FVector World = A * FMath::Cos(Ang) + B * FMath::Sin(Ang);
	const FVector Local = Body->GetComponentQuat().UnrotateVector(World);
	Ejecta->SetVariableVec3(TEXT("EjectaPos"), Local * GetRadiusCm());
	Ejecta->SetVariableFloat(TEXT("EjectaRate"), 500.f);
	EjectaLeft = 1.6f;
}

void AStarBody::TickEjecta(float DeltaTime)
{
	if (!Ejecta) { return; }
	if (EjectaLeft > 0.f)
	{
		EjectaLeft -= DeltaTime;
		if (EjectaLeft <= 0.f) { Ejecta->SetVariableFloat(TEXT("EjectaRate"), 0.f); }
	}
	// The more active the star, the more often: every two to six minutes at the Sun's middling activity.
	NextEjectaIn -= DeltaTime;
	if (NextEjectaIn <= 0.f)
	{
		NextEjectaIn = EjectaRng.FRandRange(120.f, 360.f) / FMath::Max(Activity / 0.6f, 0.1f);
		if (Activity > 0.05f) { TriggerEjecta(); }
	}
}

void AStarBody::Tick(float DeltaTime)
{
	FSlonkScopeTimer SlonkTimer_(TEXT("Tick StarBody"));
	Super::Tick(DeltaTime);
	if (SettleTicks > 0)
	{
		--SettleTicks;
		ApplyAppearance();
	}
	if (!FMath::IsNearlyZero(SpinDegPerSec))
	{
		AddActorLocalRotation(FRotator(0.f, SpinDegPerSec * UGameClockSubsystem::Sim(this, DeltaTime), 0.f));
	}
	// Beyond the GPU precision horizon the player's distant-star sprite stands in for the mesh.
	SetBeyondHorizon(IsLocationBeyondHorizon(GetActorLocation()));
	TickEjecta(DeltaTime);
	if (Ejecta && Ejecta->IsVisible() == bBeyondHorizon) { Ejecta->SetVisibility(!bBeyondHorizon); }

	// Billboard the glow toward the player's camera and keep it a minimum angular size.
	if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		FVector ViewLoc;
		FRotator ViewRot;
		PC->GetPlayerViewPoint(ViewLoc, ViewRot);
		const FVector ToCam = ViewLoc - GetActorLocation();
		const double Dist = ToCam.Size();
		PlaceYoung(ViewLoc);
		// Past the hand-over distance the distant-star sprite on the player takes over.
		const bool bNear = Dist < FarSpriteDistanceCm;
		if (Corona->IsVisible() != (bNear && !bBeyondHorizon))
		{
			Corona->SetVisibility(bNear && !bBeyondHorizon);
		}
		if (Dist > 1.0 && bNear)
		{
			// The engine plane faces +Z; point that at the camera.
			Corona->SetWorldRotation(FRotationMatrix::MakeFromZ(ToCam / Dist).Rotator());
			const double Natural = GetRadiusCm() * 2.0 * (1.0 + CoronaDepth);
			const double Minimum = Dist * GlowMinAngularSize;
			const double Width = FMath::Max(Natural, Minimum);
			Corona->SetWorldScale3D(FVector(Width / 100.0));
			if (CoronaMID)
			{
				// Keep the photosphere cut-out matched to the real disc, and brighten the glow with distance
				// so the star reads as a star from across the system instead of a dim dot.
				CoronaMID->SetScalarParameterValue(TEXT("DiscFraction"), (float)(GetRadiusCm() * 2.0 / Width));
				const float Radii = (float)(Dist / GetRadiusCm());
				CoronaMID->SetScalarParameterValue(TEXT("Intensity"), CoronaIntensity * FMath::Clamp(Radii / 8.f, 1.f, 40.f));
				// Close in, the corona's structure; from across the system, the halo and rays an eye sees. Crossed between
				// twenty and sixty radii. And the star's north, so what stands on the limb stays put on the star.
				CoronaMID->SetScalarParameterValue(TEXT("Near"), 1.f - FMath::SmoothStep(20.f, 60.f, Radii));
				CoronaMID->SetScalarParameterValue(TEXT("CamRadii"), Radii);
				const FVector Up = GetActorUpVector();
				CoronaMID->SetVectorParameterValue(TEXT("StarUp"), FLinearColor((float)Up.X, (float)Up.Y, (float)Up.Z, 0.f));
			}
		}
	}
}

#if WITH_EDITOR
void AStarBody::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);
	// Editor tools may set the actor transform after construction; keep the body sized by its radius.
	ApplyAppearance();
}
#endif
