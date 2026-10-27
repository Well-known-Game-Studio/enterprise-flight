#include "Nebula.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SlonkMaterials.h"
#include "StarBody.h"
#include "UObject/ConstructorHelpers.h"

ANebula::ANebula()
{
	PrimaryActorTick.bCanEverTick = true;
	Volume = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Volume"));
	SetRootComponent(Volume);
	Volume->SetMobility(EComponentMobility::Movable);
	Volume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Volume->CastShadow = false;
	Volume->bCastDynamicShadow = false;
	Volume->bAffectDistanceFieldLighting = false;
	Volume->SetTranslucentSortPriority(-5); // behind the planets' translucent shells and the star glow
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/ArtTools/RenderToTexture/Meshes/S_1_Unit_Sphere_HI.S_1_Unit_Sphere_HI"));
	if (SphereFinder.Succeeded()) { Volume->SetStaticMesh(SphereFinder.Object); }
	NebulaMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Slonk/Materials/M_Nebula.M_Nebula")));
}

void ANebula::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyAppearance();
}

void ANebula::BeginPlay()
{
	Super::BeginPlay();
	ApplyAppearance();
	UpdateStar();
}

void ANebula::ApplyAppearance()
{
	// The unit sphere scaled to the radius; its bounds are the cloud, so it draws whenever any of it is in view.
	Volume->SetWorldScale3D(FVector(GetRadiusCm()));
	if (!MID)
	{
		if (UMaterialInterface* Mat = SlonkMaterials::LoadOrFallback(NebulaMaterial, TEXT("Nebula")))
		{
			MID = UMaterialInstanceDynamic::Create(Mat, this);
		}
	}
	if (MID)
	{
		if (Volume->GetMaterial(0) != MID) { Volume->SetMaterial(0, MID); }
		MID->SetScalarParameterValue(TEXT("RadiusCm"), (float)GetRadiusCm());
		MID->SetVectorParameterValue(TEXT("ColorA"), ColorA);
		MID->SetVectorParameterValue(TEXT("ColorB"), ColorB);
		MID->SetVectorParameterValue(TEXT("EmissiveColor"), EmissiveColor);
		MID->SetScalarParameterValue(TEXT("Density"), Density);
		MID->SetScalarParameterValue(TEXT("Emission"), Emission);
		MID->SetScalarParameterValue(TEXT("Brightness"), Brightness);
		MID->SetScalarParameterValue(TEXT("Filaments"), Filaments);
		MID->SetScalarParameterValue(TEXT("Seed"), Seed);
	}
}

void ANebula::UpdateStar()
{
	if (!MID) { return; }
	AStarBody* Best = nullptr;
	double BestD = TNumericLimits<double>::Max();
	for (TActorIterator<AStarBody> It(GetWorld()); It; ++It)
	{
		const double D = FVector::DistSquared(It->GetActorLocation(), GetActorLocation());
		if (D < BestD) { BestD = D; Best = *It; }
	}
	if (Best)
	{
		const FVector Dir = (Best->GetActorLocation() - GetActorLocation()).GetSafeNormal();
		MID->SetVectorParameterValue(TEXT("SunDirection"), FLinearColor(Dir.X, Dir.Y, Dir.Z, 0.f));
		MID->SetVectorParameterValue(TEXT("StarColor"), Best->StarColor);
	}
}

void ANebula::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	StarTimer -= DeltaTime;
	if (StarTimer <= 0.f)
	{
		StarTimer = 1.f;
		UpdateStar();
	}
}
