#include "DistantStarsComponent.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "SlonkMaterials.h"
#include "StarBody.h"
#include "UObject/ConstructorHelpers.h"

UDistantStarsComponent::UDistantStarsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// After the floating origin has shifted the world and the ship has moved for this frame.
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	SetUsingAbsoluteRotation(true);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneFinder(TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneFinder.Succeeded())
	{
		PlaneMesh = PlaneFinder.Object;
	}
	GlowMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Slonk/Materials/M_StarGlow.M_StarGlow")));
}

void UDistantStarsComponent::Rescan()
{
	Stars.Reset();
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AStarBody> It(World); It; ++It)
		{
			Stars.Add(*It);
		}
	}
}

UStaticMeshComponent* UDistantStarsComponent::EnsureSprite(int32 Index)
{
	while (Sprites.Num() <= Index)
	{
		const int32 N = Sprites.Num();
		UStaticMeshComponent* Sprite = NewObject<UStaticMeshComponent>(GetOwner(), *FString::Printf(TEXT("DistantStar%d"), N));
		Sprite->SetupAttachment(this);
		Sprite->SetUsingAbsoluteRotation(true);
		Sprite->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Sprite->SetGenerateOverlapEvents(false);
		Sprite->CastShadow = false;
		Sprite->bCastDynamicShadow = false;
		Sprite->SetReceivesDecals(false);
		Sprite->bAffectDistanceFieldLighting = false;
		Sprite->bAffectDynamicIndirectLighting = false;
		Sprite->SetTranslucentSortPriority(1);
		if (PlaneMesh)
		{
			Sprite->SetStaticMesh(PlaneMesh);
		}
		UMaterialInstanceDynamic* Mid = nullptr;
		if (UMaterialInterface* Mat = SlonkMaterials::LoadOrFallback(GlowMaterial, TEXT("DistantStarGlow")))
		{
			Mid = UMaterialInstanceDynamic::Create(Mat, this);
			Sprite->SetMaterial(0, Mid);
		}
		Sprite->RegisterComponent();
		Sprites.Add(Sprite);
		SpriteMIDs.Add(Mid);
	}
	return Sprites[Index];
}

void UDistantStarsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	RescanTimer -= DeltaTime;
	if (RescanTimer <= 0.f)
	{
		RescanTimer = RescanInterval;
		Rescan();
	}

	// Keep the anchor on world axes so relative offsets below are world-space bearings.
	if (!GetComponentRotation().IsNearlyZero())
	{
		SetWorldRotation(FRotator::ZeroRotator);
	}

	const FVector Here = GetComponentLocation();
	int32 Used = 0;
	for (const TWeakObjectPtr<AStarBody>& Weak : Stars)
	{
		const AStarBody* Star = Weak.Get();
		if (!Star)
		{
			continue;
		}
		const FVector To = Star->GetActorLocation() - Here;
		const double Dist = To.Size();
		if (Dist <= AStarBody::FarSpriteDistanceCm || Dist < 1.0)
		{
			continue;
		}
		const FVector Dir = To / Dist;
		UStaticMeshComponent* Sprite = EnsureSprite(Used);
		UMaterialInstanceDynamic* Mid = SpriteMIDs[Used];
		++Used;

		// True angular diameter of the photosphere, projected onto the sprite distance.
		const double StarRadius = Star->GetRadiusCm();
		const double Blend = FMath::Clamp((Dist - AStarBody::FarSpriteDistanceCm) / FMath::Max(ScaleFullDistance - AStarBody::FarSpriteDistanceCm, 1.0), 0.0, 1.0);
		const double SizeScale = FMath::Lerp(1.0, (double)ApparentSizeScale, Blend);
		const double DiscWidth = 2.0 * StarRadius / Dist * SpriteDistance * SizeScale;
		const double Width = FMath::Max(DiscWidth * (1.0 + GlowRadii), SpriteDistance * (double)MinAngularSize);

		Sprite->SetRelativeLocation(Dir * SpriteDistance);
		// The engine plane faces +Z; point it back at the viewer.
		Sprite->SetWorldRotation(FRotationMatrix::MakeFromZ(-Dir).Rotator());
		Sprite->SetWorldScale3D(FVector(Width / 100.0));
		if (!Sprite->IsVisible())
		{
			Sprite->SetVisibility(true);
		}
		if (Mid)
		{
			Mid->SetScalarParameterValue(TEXT("DiscFraction"), (float)(DiscWidth / Width));
			Mid->SetScalarParameterValue(TEXT("DiscFill"), 1.f);
			Mid->SetScalarParameterValue(TEXT("DiscIntensity"), Star->Intensity);
			Mid->SetScalarParameterValue(TEXT("DiscLimbDarkening"), Star->LimbDarkening);
			Mid->SetVectorParameterValue(TEXT("StarColor"), Star->StarColor);
			Mid->SetVectorParameterValue(TEXT("CoronaColor"), Star->CoronaColor);
			Mid->SetScalarParameterValue(TEXT("Falloff"), Star->CoronaFalloff);
			Mid->SetScalarParameterValue(TEXT("Intensity"), Star->CoronaIntensity * GlowIntensityScale);
		}
	}
	for (int32 i = Used; i < Sprites.Num(); ++i)
	{
		if (Sprites[i] && Sprites[i]->IsVisible())
		{
			Sprites[i]->SetVisibility(false);
		}
	}
}
