#include "DistantStarsComponent.h"
#include "SlonkTiming.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "SlonkMaterials.h"
#include "StarBody.h"
#include "Nebula.h"
#include "PlanetBody.h"
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
	Nebulae.Reset();
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AStarBody> It(World); It; ++It)
		{
			Stars.Add(*It);
		}
		for (TActorIterator<ANebula> It(World); It; ++It)
		{
			Nebulae.Add(*It);
		}
		Planets.Reset();
		for (TActorIterator<APlanetBody> It(World); It; ++It)
		{
			Planets.Add(*It);
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
	FSlonkScopeTimer SlonkTimer_(TEXT("TickComponent DistantStarsComponent"));
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
	// Nebulae: a soft glow in the cloud's own colours, at its true angular size (never under the minimum), from
	// the moment it subtends NebulaMinAngularDeg until the volume takes over inside the render horizon. The
	// sprite fades out as the volume grows past a few pixels, so nothing pops.
	for (const TWeakObjectPtr<ANebula>& Weak : Nebulae)
	{
		const ANebula* Neb = Weak.Get();
		if (!Neb) { continue; }
		const FVector To = Neb->GetActorLocation() - Here;
		const double Dist = To.Size();
		const double R = Neb->GetRadiusCm();
		if (Dist < 1.0) { continue; }
		const double AngDeg = FMath::RadiansToDegrees(2.0 * FMath::Atan(R / Dist));
		// Full beyond the horizon (the volume cannot be drawn there); inside it the sprite fades as the volume
		// grows from a few pixels to a readable cloud.
		const float Fade = Dist >= SlonkMaterials::RenderHorizonCm ? 1.f : 1.f - FMath::SmoothStep(NebulaMinApparentDeg * 0.25f, NebulaMinApparentDeg, (float)AngDeg);
		if (AngDeg < NebulaMinAngularDeg || Fade <= 0.001f) { continue; }
		const FVector Dir = To / Dist;
		UStaticMeshComponent* Sprite = EnsureSprite(Used);
		UMaterialInstanceDynamic* Mid = SpriteMIDs[Used];
		++Used;
		const double Width = SpriteDistance * FMath::Tan(FMath::DegreesToRadians(FMath::Max(AngDeg, (double)NebulaMinApparentDeg)) * 0.5) * 2.0 * 1.6;
		Sprite->SetRelativeLocation(Dir * SpriteDistance);
		Sprite->SetWorldRotation(FRotationMatrix::MakeFromZ(-Dir).Rotator());
		Sprite->SetWorldScale3D(FVector(Width / 100.0));
		if (!Sprite->IsVisible()) { Sprite->SetVisibility(true); }
		if (Mid)
		{
			const FLinearColor C = (Neb->ColorA + Neb->ColorB) * 0.25f + Neb->EmissiveColor * 0.5f;
			Mid->SetScalarParameterValue(TEXT("DiscFraction"), 0.f);
			Mid->SetScalarParameterValue(TEXT("DiscFill"), 0.f);
			Mid->SetScalarParameterValue(TEXT("DiscIntensity"), 0.f);
			Mid->SetVectorParameterValue(TEXT("StarColor"), C);
			Mid->SetVectorParameterValue(TEXT("CoronaColor"), C);
			Mid->SetScalarParameterValue(TEXT("Falloff"), 2.f);
			Mid->SetScalarParameterValue(TEXT("Intensity"), Neb->Brightness * Neb->Emission * NebulaGlowScale * Fade);
		}
	}
	// Planets and moons beyond the horizon: a point of light, then a lit disc at the true angular size, in the
	// world's own colours, bright by how much of the lit half faces us. Inside the horizon the mesh is drawn
	// and the sprite fades out as it grows past a few pixels.
	const FVector SunHere = Stars.Num() > 0 && Stars[0].IsValid() ? Stars[0]->GetActorLocation() : FVector::ZeroVector;
	for (const TWeakObjectPtr<APlanetBody>& Weak : Planets)
	{
		const APlanetBody* Pl = Weak.Get();
		if (!Pl) { continue; }
		const FVector To = Pl->GetActorLocation() - Here;
		const double Dist = To.Size();
		if (Dist < 1.0) { continue; }
		const FVector Dir = To / Dist;
		const double AngDeg = FMath::RadiansToDegrees(2.0 * FMath::Atan(Pl->GetRadiusCm() / Dist));
		// Full beyond the horizon (nothing else is drawn there); inside it the sprite fades as the mesh grows
		// from a few pixels to a readable disc.
		const float Fade = Dist >= SlonkMaterials::RenderHorizonCm ? 1.f : 1.f - FMath::SmoothStep(PlanetMinApparentDeg, PlanetMinApparentDeg * 2.5f, (float)AngDeg);
		if (Fade <= 0.001f) { continue; }
		const bool bDisc = AngDeg >= PlanetDiscMinDeg;
		// The lit fraction facing us: the phase from the sun's and our bearings at the planet.
		const FVector ToSun = (SunHere - Pl->GetActorLocation()).GetSafeNormal();
		const double Lit = 0.5 + 0.5 * FVector::DotProduct(ToSun, -Dir);
		const FPlanetStyle& St = Pl->Style;
		FLinearColor C = FMath::Lerp(St.LandColor, St.OceanColor, FMath::Clamp(St.SeaLevel, 0.f, 1.f));
		if (St.bClouds) { C = FMath::Lerp(C, St.CloudColor, St.CloudCoverage * 0.5f); }
		if (St.bAtmosphere) { C = FMath::Lerp(C, St.AtmosphereColor, 0.15f); }
		UStaticMeshComponent* Sprite = EnsureSprite(Used);
		UMaterialInstanceDynamic* Mid = SpriteMIDs[Used];
		++Used;
		const double DiscWidth = SpriteDistance * FMath::Tan(FMath::DegreesToRadians(AngDeg) * 0.5) * 2.0;
		const double Width = FMath::Max(DiscWidth * 1.3, SpriteDistance * FMath::Tan(FMath::DegreesToRadians((double)PlanetMinApparentDeg) * 0.5) * 2.0);
		Sprite->SetRelativeLocation(Dir * SpriteDistance);
		Sprite->SetWorldRotation(FRotationMatrix::MakeFromZ(-Dir).Rotator());
		Sprite->SetWorldScale3D(FVector(Width / 100.0));
		if (!Sprite->IsVisible()) { Sprite->SetVisibility(true); }
		if (Mid)
		{
			Mid->SetScalarParameterValue(TEXT("DiscFraction"), bDisc ? (float)(DiscWidth / Width) : 0.f);
			Mid->SetScalarParameterValue(TEXT("DiscFill"), bDisc ? 1.f : 0.f);
			Mid->SetScalarParameterValue(TEXT("DiscIntensity"), PlanetDiscIntensity * (float)Lit * Fade);
			Mid->SetScalarParameterValue(TEXT("DiscLimbDarkening"), 0.6f);
			Mid->SetVectorParameterValue(TEXT("StarColor"), C);
			Mid->SetVectorParameterValue(TEXT("CoronaColor"), C);
			Mid->SetScalarParameterValue(TEXT("Falloff"), 3.f);
			Mid->SetScalarParameterValue(TEXT("Intensity"), (bDisc ? 0.15f * PlanetDiscIntensity : PlanetPointIntensity) * (float)FMath::Max(Lit, 0.15) * Fade);
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
