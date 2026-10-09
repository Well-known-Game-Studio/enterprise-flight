#include "PlanetBody.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "SlonkMaterials.h"
#include "UObject/ConstructorHelpers.h"

APlanetBody::APlanetBody()
{
	PrimaryActorTick.bCanEverTick = true;

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	SetRootComponent(Body);
	Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Body->SetCollisionObjectType(ECC_WorldStatic);
	Body->SetCollisionResponseToAllChannels(ECR_Block);
	Body->SetMobility(EComponentMobility::Movable);
	Body->SetGenerateOverlapEvents(false);

	// Higher-poly unit sphere (radius 1 cm) so the silhouette stays round at 1000 km.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/ArtTools/RenderToTexture/Meshes/S_1_Unit_Sphere_HI.S_1_Unit_Sphere_HI"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> FallbackSphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereFinder.Succeeded())
	{
		Body->SetStaticMesh(SphereFinder.Object);
		MeshUnitRadius = 1.f;
	}
	else if (FallbackSphereFinder.Succeeded())
	{
		Body->SetStaticMesh(FallbackSphereFinder.Object);
		MeshUnitRadius = 50.f;
	}
	PlanetMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Slonk/Materials/M_Planet.M_Planet")));
}

void APlanetBody::PCGPostSpawn()
{
	ApplyAppearance();
}

double APlanetBody::GetRadiusCm() const
{
	return FMath::Max(RadiusKm, 1.f) * 100000.0;
}

void APlanetBody::ApplyAppearance()
{
	if (bRandomizeFromLocation)
	{
		const FVector L = GetActorLocation();
		const uint32 Hash = HashCombine(HashCombine(GetTypeHash(FMath::RoundToInt64(L.X / 1000.0)), GetTypeHash(FMath::RoundToInt64(L.Y / 1000.0))), GetTypeHash(FMath::RoundToInt64(L.Z / 1000.0)));
		FRandomStream Rng(static_cast<int32>(Hash));
		static const FLinearColor Palette[] = {
			FLinearColor(0.25f, 0.42f, 0.65f), // ocean blue
			FLinearColor(0.60f, 0.45f, 0.30f), // desert
			FLinearColor(0.75f, 0.60f, 0.40f), // gas giant tan
			FLinearColor(0.55f, 0.30f, 0.20f), // rust
			FLinearColor(0.70f, 0.72f, 0.75f), // ice
			FLinearColor(0.35f, 0.55f, 0.35f), // verdant
			FLinearColor(0.45f, 0.35f, 0.55f), // violet
			FLinearColor(0.80f, 0.70f, 0.50f), // pale gas
		};
		Color = Palette[Rng.RandRange(0, UE_ARRAY_COUNT(Palette) - 1)] * Rng.FRandRange(0.8f, 1.1f);
		Color.A = 1.f;
		SpinDegPerSec = Rng.FRandRange(0.1f, 0.8f) * (Rng.FRand() < 0.2f ? -1.f : 1.f);
	}

	// Body is the root: set its world scale so the planet is exactly RadiusKm whatever the spawner did to the actor scale.
	Body->SetWorldScale3D(FVector(GetRadiusCm() / MeshUnitRadius));

	if (!BodyMaterial)
	{
		if (UMaterialInterface* Mat = SlonkMaterials::LoadOrFallback(PlanetMaterial, TEXT("Planet"), /*bLit*/ true))
		{
			BodyMaterial = UMaterialInstanceDynamic::Create(Mat, this);
			Body->SetMaterial(0, BodyMaterial);
		}
	}
	if (BodyMaterial)
	{
		BodyMaterial->SetVectorParameterValue(TEXT("Color"), Color);
	}
}

void APlanetBody::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyAppearance();
}

void APlanetBody::BeginPlay()
{
	Super::BeginPlay();
	ApplyAppearance();
}

void APlanetBody::Tick(float DeltaTime)
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
