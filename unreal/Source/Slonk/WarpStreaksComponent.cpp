#include "WarpStreaksComponent.h"

#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "SlonkMaterials.h"
#include "UObject/ConstructorHelpers.h"

UWarpStreaksComponent::UWarpStreaksComponent()
	: Rng(74656)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;

	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	CastShadow = false;
	bCastDynamicShadow = false;
	bReceivesDecals = false;
	SetCanEverAffectNavigation(false);
	SetVisibility(false);
	NumCustomDataFloats = 0;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderFinder.Succeeded())
	{
		SetStaticMesh(CylinderFinder.Object);
	}

	StreakMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(SlonkMaterials::WarpStreakPath));
}

void UWarpStreaksComponent::BeginPlay()
{
	Super::BeginPlay();

	UMaterialInterface* Material = SlonkMaterials::LoadOrFallback(StreakMaterial, TEXT("WarpStreaks"));
	if (Material)
	{
		SetMaterial(0, Material);
	}

	BuildInstances();
}

void UWarpStreaksComponent::RandomizeRadial(FVector& Position) const
{
	const float Angle = Rng.FRandRange(0.f, 2.f * PI);
	// Bias toward the outer tunnel so the center stays clear.
	const float R = FMath::Sqrt(Rng.FRandRange(0.f, 1.f)) * (TunnelRadius - TunnelInnerRadius) + TunnelInnerRadius;
	Position.Y = FMath::Cos(Angle) * R;
	Position.Z = FMath::Sin(Angle) * R;
}

FTransform UWarpStreaksComponent::MakeStreakTransform(const FVector& Position, float LengthScale) const
{
	// Engine cylinder is 100 cm tall along Z and 100 cm in diameter; pitch it 90 degrees to lie along X.
	const float Len = FMath::Max(StreakLength * LengthScale, 10.f);
	return FTransform(FRotator(90.f, 0.f, 0.f), Position, FVector(StreakThickness / 100.f, StreakThickness / 100.f, Len / 100.f));
}

void UWarpStreaksComponent::BuildInstances()
{
	ClearInstances();
	Positions.SetNum(NumStreaks);
	LengthJitter.SetNum(NumStreaks);
	Transforms.SetNum(NumStreaks);

	for (int32 i = 0; i < NumStreaks; ++i)
	{
		FVector P;
		P.X = Rng.FRandRange(-TunnelHalfLength, TunnelHalfLength);
		RandomizeRadial(P);
		Positions[i] = P;
		LengthJitter[i] = Rng.FRandRange(0.5f, 1.6f);
		Transforms[i] = MakeStreakTransform(P, 0.01f);
	}
	AddInstances(Transforms, /*bShouldReturnIndices*/ false, /*bWorldSpace*/ false);
}

void UWarpStreaksComponent::SetIntensity(float NewIntensity)
{
	Intensity = FMath::Clamp(NewIntensity, 0.f, 1.f);
	const bool bShouldBeVisible = Intensity > 0.01f;
	if (IsVisible() != bShouldBeVisible)
	{
		SetVisibility(bShouldBeVisible);
	}
}

void UWarpStreaksComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (Intensity <= 0.01f || Positions.Num() == 0)
	{
		return;
	}

	// Streaks get longer and faster as intensity rises; at the moment of the jump they are short dots.
	const float Speed = ScrollSpeed * (0.15f + 0.85f * Intensity);
	const float LengthScale = 0.1f + 1.4f * Intensity * Intensity;
	const float Step = Speed * FMath::Min(DeltaTime, 0.1f);

	for (int32 i = 0; i < Positions.Num(); ++i)
	{
		FVector& P = Positions[i];
		P.X -= Step;
		if (P.X < -TunnelHalfLength)
		{
			P.X += 2.f * TunnelHalfLength;
			RandomizeRadial(P);
		}
		Transforms[i] = MakeStreakTransform(P, LengthScale * LengthJitter[i]);
	}

	BatchUpdateInstancesTransforms(0, Transforms, /*bWorldSpace*/ false, /*bMarkRenderStateDirty*/ true, /*bTeleport*/ true);
}
