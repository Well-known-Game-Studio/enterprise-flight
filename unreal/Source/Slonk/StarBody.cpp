#include "StarBody.h"

#include "Components/StaticMeshComponent.h"
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
	Body->SetCollisionResponseToAllChannels(ECR_Block);
	Body->SetMobility(EComponentMobility::Movable);
	Body->CastShadow = false;

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
	if (Mesh) { Corona->SetStaticMesh(Mesh); }

	StarMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Slonk/Materials/M_Star.M_Star")));
	CoronaMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Slonk/Materials/M_StarCorona.M_StarCorona")));

	Tags.AddUnique(APlanetBody::StarTag);
}

void AStarBody::ApplyAppearance()
{
	Tags.AddUnique(APlanetBody::StarTag);
	Body->SetWorldScale3D(FVector(GetRadiusCm() / MeshUnitRadius));
	Corona->SetRelativeScale3D(FVector(1.0 + CoronaDepth));

	if (!StarMID)
	{
		if (UMaterialInterface* Mat = SlonkMaterials::LoadOrFallback(StarMaterial, TEXT("Star")))
		{
			StarMID = UMaterialInstanceDynamic::Create(Mat, this);
		}
	}
	if (!CoronaMID)
	{
		if (UMaterialInterface* Mat = SlonkMaterials::LoadOrFallback(CoronaMaterial, TEXT("StarCorona")))
		{
			CoronaMID = UMaterialInstanceDynamic::Create(Mat, this);
		}
	}
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
		CoronaMID->SetScalarParameterValue(TEXT("CoronaDepth"), CoronaDepth);
		CoronaMID->SetScalarParameterValue(TEXT("Falloff"), CoronaFalloff);
	}
}

void AStarBody::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyAppearance();
}

void AStarBody::BeginPlay()
{
	Super::BeginPlay();
	ApplyAppearance();
}

void AStarBody::Tick(float DeltaTime)
{
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
}

#if WITH_EDITOR
void AStarBody::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);
	// Editor tools may set the actor transform after construction; keep the body sized by its radius.
	ApplyAppearance();
}
#endif
