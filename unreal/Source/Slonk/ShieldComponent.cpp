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
	Strength = FMath::Clamp(Strength, 0.1f, 3.f);
	// A phaser beam reports a hit every frame. Restarting a slot each time kept its ring at age zero (invisible)
	// until the beam stopped, so a hit near a ring that is still young only tops up that ring; once the ring is
	// past half its life the slot restarts, and a held beam pulses instead of staying dark.
	int32 Slot = -1;
	const float Nearby = 50.f * ImpactRadiusFraction * 0.7f;
	for (int32 i = 0; i < 4; ++i)
	{
		const float Age = Now - SlotTime[i];
		if (Age < ImpactDuration && FVector::Dist(Local, SlotPoint[i]) < Nearby)
		{
			if (Age < ImpactDuration * 0.55f)
			{
				// Each frame of a held beam carries little damage; let the ring brighten while the beam stays on it.
				SlotStrength[i] = FMath::Min(FMath::Max(SlotStrength[i], Strength) + Strength * 0.35f, 2.5f);
				MID->SetScalarParameterValue(StrengthParams[i], SlotStrength[i]);
				LastImpactTime = Now;
				return;
			}
			Slot = i;
			break;
		}
	}
	if (Slot < 0)
	{
		Slot = NextSlot;
		NextSlot = (NextSlot + 1) % 4;
	}
	SlotPoint[Slot] = Local;
	SlotTime[Slot] = Now;
	SlotStrength[Slot] = Strength;
	MID->SetVectorParameterValue(ImpactParams[Slot], FLinearColor(Local.X, Local.Y, Local.Z, 0.f));
	MID->SetScalarParameterValue(TimeParams[Slot], Now);
	MID->SetScalarParameterValue(StrengthParams[Slot], Strength);
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
