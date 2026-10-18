#include "StarBody.h"
#include "SlonkTiming.h"

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

	StarMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Slonk/Materials/M_Star.M_Star")));
	CoronaMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Slonk/Materials/M_StarGlow.M_StarGlow")));

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
	}
	if (CoronaMID)
	{
		if (Corona->GetMaterial(0) != CoronaMID) { Corona->SetMaterial(0, CoronaMID); }
		CoronaMID->SetVectorParameterValue(TEXT("CoronaColor"), CoronaColor);
		CoronaMID->SetScalarParameterValue(TEXT("Intensity"), CoronaIntensity);
		CoronaMID->SetScalarParameterValue(TEXT("Falloff"), CoronaFalloff);
		// Where the photosphere disc ends inside the sprite (0..1 of the half-width).
		CoronaMID->SetScalarParameterValue(TEXT("DiscFraction"), 1.0f / (1.0f + CoronaDepth));
	}
}

void AStarBody::SetBeyondHorizon(bool bBeyond)
{
	bBeyondHorizon = bBeyond;
	SetActorHiddenInGame(bBeyond);
#if WITH_EDITOR
	if (GetWorld() && !GetWorld()->IsGameWorld())
	{
		SetIsTemporarilyHiddenInEditor(bBeyond);
	}
#endif
	if (Body) { Body->SetVisibility(!bBeyond); }
	if (Corona) { Corona->SetVisibility(!bBeyond); }
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
		AddActorLocalRotation(FRotator(0.f, SpinDegPerSec * DeltaTime, 0.f));
	}
	// Beyond the GPU precision horizon the player's distant-star sprite stands in for the mesh.
	SetBeyondHorizon(IsLocationBeyondHorizon(GetActorLocation()));

	// Billboard the glow toward the player's camera and keep it a minimum angular size.
	if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		FVector ViewLoc;
		FRotator ViewRot;
		PC->GetPlayerViewPoint(ViewLoc, ViewRot);
		const FVector ToCam = ViewLoc - GetActorLocation();
		const double Dist = ToCam.Size();
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
