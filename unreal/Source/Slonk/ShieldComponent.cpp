#include "ShieldComponent.h"

#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "SlonkMaterials.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const TCHAR* ImpactParams[4] = { TEXT("ImpactA"), TEXT("ImpactB"), TEXT("ImpactC"), TEXT("ImpactD") };
	const TCHAR* TimeParams[4] = { TEXT("ImpactTimeA"), TEXT("ImpactTimeB"), TEXT("ImpactTimeC"), TEXT("ImpactTimeD") };
	const TCHAR* StrengthParams[4] = { TEXT("ImpactStrengthA"), TEXT("ImpactStrengthB"), TEXT("ImpactStrengthC"), TEXT("ImpactStrengthD") };
}

UShieldComponent::UShieldComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	CastShadow = false;
	bCastDynamicShadow = false;
	bAffectDistanceFieldLighting = false;
	SetMobility(EComponentMobility::Movable);
	SetTranslucentSortPriority(3);
	SetVisibility(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereFinder.Succeeded())
	{
		SetStaticMesh(SphereFinder.Object);
	}
	ShieldMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(SlonkMaterials::ShieldPath));
}

void UShieldComponent::BeginPlay()
{
	Super::BeginPlay();
	EnsureMaterial();
}

void UShieldComponent::EnsureMaterial()
{
	if (MID)
	{
		return;
	}
	if (UMaterialInterface* Mat = SlonkMaterials::LoadOrFallback(ShieldMaterial, TEXT("Shield")))
	{
		MID = UMaterialInstanceDynamic::Create(Mat, this);
		MID->SetVectorParameterValue(TEXT("ShieldColor"), ShieldColor);
		MID->SetScalarParameterValue(TEXT("Intensity"), Intensity);
		MID->SetScalarParameterValue(TEXT("ImpactDuration"), ImpactDuration);
		MID->SetScalarParameterValue(TEXT("ImpactRadius"), 50.f * ImpactRadiusFraction);
		for (int32 i = 0; i < 4; ++i)
		{
			MID->SetScalarParameterValue(TimeParams[i], -1000.f);
			MID->SetScalarParameterValue(StrengthParams[i], 0.f);
		}
		SetMaterial(0, MID);
	}
}

void UShieldComponent::SetShellExtent(const FVector& HalfExtentsCm)
{
	// The engine sphere is 50 cm in radius.
	SetRelativeScale3D(HalfExtentsCm / 50.0);
}

void UShieldComponent::RegisterImpact(const FVector& WorldPoint, float Strength)
{
	EnsureMaterial();
	UWorld* World = GetWorld();
	if (!MID || !World)
	{
		return;
	}
	// Project onto the shell: direction from the centre in mesh space, at the mesh radius.
	FVector Local = GetComponentTransform().InverseTransformPosition(WorldPoint);
	Local = Local.GetSafeNormal() * 50.0;
	const float Now = World->GetTimeSeconds();
	MID->SetVectorParameterValue(ImpactParams[NextSlot], FLinearColor(Local.X, Local.Y, Local.Z, 0.f));
	MID->SetScalarParameterValue(TimeParams[NextSlot], Now);
	MID->SetScalarParameterValue(StrengthParams[NextSlot], FMath::Clamp(Strength, 0.1f, 3.f));
	NextSlot = (NextSlot + 1) % 4;
	LastImpactTime = Now;
	if (!IsVisible())
	{
		SetVisibility(true);
	}
}

bool UShieldComponent::IsFlaring() const
{
	const UWorld* World = GetWorld();
	return World && World->GetTimeSeconds() - LastImpactTime < ImpactDuration;
}

void UShieldComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (IsVisible() && !IsFlaring())
	{
		SetVisibility(false);
	}
}
