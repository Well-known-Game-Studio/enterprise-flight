#include "Nebula.h"
#include "Components/PointLightComponent.h"
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
	// Level with the rings and the star's corona (2), above the atmosphere shells and the distant-star sprites
	// (1): among equals distance sorts, and the cloud's origin is nearest whenever the camera is inside it, so
	// it composites last. At 0 the sprites and rings drew over the gas, which brightened every sky pixel except
	// where an opaque moon blocked them: a dark disc where the gas alone should have shown.
	Volume->SetTranslucentSortPriority(2);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/ArtTools/RenderToTexture/Meshes/S_1_Unit_Sphere_HI.S_1_Unit_Sphere_HI"));
	if (SphereFinder.Succeeded()) { Volume->SetStaticMesh(SphereFinder.Object); }
	Glow = CreateDefaultSubobject<UPointLightComponent>(TEXT("Glow"));
	Glow->SetupAttachment(Volume);
	Glow->SetUsingAbsoluteScale(true);
	Glow->SetMobility(EComponentMobility::Movable);
	Glow->SetCastShadows(false);
	Glow->bUseInverseSquaredFalloff = false;
	Glow->LightFalloffExponent = 2.f;
	Glow->bAffectsWorld = true;
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
	if (Glow)
	{
		// The glow's colour is the gas colours as the march mixes them, biased to the emissive.
		const FLinearColor C = (ColorA + ColorB) * 0.25f + EmissiveColor * 0.5f;
		Glow->SetLightColor(C);
		Glow->SetIntensity(GlowLight);
		Glow->SetAttenuationRadius((float)(GetRadiusCm() * 2.0));
		Glow->SetSourceRadius((float)(GetRadiusCm() * 0.5));
	}
}

void ANebula::SetBeyondHorizon(bool bBeyond)
{
	if (bBeyondHorizon == bBeyond) { return; }
	bBeyondHorizon = bBeyond;
	if (Volume) { Volume->SetVisibility(!bBeyond); }
	if (Glow) { Glow->SetVisibility(!bBeyond); }
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
