#include "RunningLightsComponent.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "SlonkMaterials.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// The engine sphere is 50 cm in radius.
	constexpr float MeshRadiusCm = 50.f;
	constexpr int32 CustomFloats = 6; // phase, period, mode, r, g, b
}

URunningLightsComponent::URunningLightsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	CastShadow = false;
	bCastDynamicShadow = false;
	bAffectDistanceFieldLighting = false;
	bDisableCollision = true;
	SetMobility(EComponentMobility::Movable);
	SetTranslucentSortPriority(4);
	NumCustomDataFloats = CustomFloats;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereFinder.Succeeded())
	{
		SetStaticMesh(SphereFinder.Object);
	}
	LightMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(SlonkMaterials::RunningLightPath));
}

void URunningLightsComponent::BeginPlay()
{
	Super::BeginPlay();
	EnsureMaterial();
}

void URunningLightsComponent::EnsureMaterial()
{
	if (MID)
	{
		return;
	}
	if (UMaterialInterface* Mat = SlonkMaterials::LoadOrFallback(LightMaterial, TEXT("RunningLight")))
	{
		MID = UMaterialInstanceDynamic::Create(Mat, this);
		MID->SetScalarParameterValue(TEXT("Intensity"), Intensity);
		SetMaterial(0, MID);
	}
}

void URunningLightsComponent::ClearLights()
{
	Lights.Reset();
	ClearInstances();
	LastScale = -1.f;
}

void URunningLightsComponent::SetLights(const TArray<FRunningLight>& InLights)
{
	ClearInstances();
	Lights = InLights;
	NumCustomDataFloats = CustomFloats;
	for (const FRunningLight& L : Lights)
	{
		const int32 Index = AddInstance(FTransform(FRotator::ZeroRotator, L.LocalPosition, FVector(FMath::Max(L.RadiusCm, 1.f) / MeshRadiusCm)), false);
		if (Index < 0)
		{
			continue;
		}
		const FLinearColor C = L.Color * L.Brightness;
		SetCustomDataValue(Index, 0, L.Phase, false);
		SetCustomDataValue(Index, 1, FMath::Max(L.Period, 0.05f), false);
		SetCustomDataValue(Index, 2, (float)(uint8)L.Mode, false);
		SetCustomDataValue(Index, 3, C.R, false);
		SetCustomDataValue(Index, 4, C.G, false);
		SetCustomDataValue(Index, 5, C.B, false);
	}
	MarkRenderStateDirty();
	LastScale = -1.f;
	ScaleTimer = 0.f;
	EnsureMaterial();
}

void URunningLightsComponent::ApplyScale(float DistanceRadiusCm)
{
	// Scale relative to the largest bulb so small formation lights stay smaller than the main beacons.
	float RefRadius = 1.f;
	for (const FRunningLight& L : Lights)
	{
		RefRadius = FMath::Max(RefRadius, L.RadiusCm);
	}
	TArray<FTransform> Transforms;
	Transforms.Reserve(Lights.Num());
	for (const FRunningLight& L : Lights)
	{
		const float R = FMath::Max(L.RadiusCm, DistanceRadiusCm * (L.RadiusCm / RefRadius));
		Transforms.Add(FTransform(FRotator::ZeroRotator, L.LocalPosition, FVector(R / MeshRadiusCm)));
	}
	if (Transforms.Num() > 0)
	{
		BatchUpdateInstancesTransforms(0, Transforms, false, true, false);
	}
	LastScale = DistanceRadiusCm;
}

void URunningLightsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (Lights.Num() == 0 || !IsVisible())
	{
		return;
	}
	ScaleTimer -= DeltaTime;
	if (ScaleTimer > 0.f)
	{
		return;
	}
	ScaleTimer = 0.1f;
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC || !PC->PlayerCameraManager)
	{
		return;
	}
	const double Distance = FVector::Dist(PC->PlayerCameraManager->GetCameraLocation(), GetComponentLocation());
	const float Wanted = FMath::Clamp((float)(Distance * MinAngularRadius), 0.f, MaxRadiusCm);
	if (LastScale < 0.f || FMath::Abs(Wanted - LastScale) > FMath::Max(LastScale, 1.f) * 0.05f)
	{
		ApplyScale(Wanted);
	}
}
