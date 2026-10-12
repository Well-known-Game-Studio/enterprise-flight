#include "TractorBeamComponent.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "SlonkMaterials.h"
#include "SpaceTarget.h"
#include "SpaceVessel.h"
#include "UObject/ConstructorHelpers.h"

UTractorBeamComponent::UTractorBeamComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// After the ship has moved this frame, so the held target follows without lag.
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	BeamMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(SlonkMaterials::TractorBeamPath));
}

void UTractorBeamComponent::BeginPlay()
{
	Super::BeginPlay();
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}
	UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	auto Make = [&](const TCHAR* Name, UStaticMesh* Mesh)
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Owner, Name);
		C->SetupAttachment(Owner->GetRootComponent());
		C->SetUsingAbsoluteRotation(true);
		C->SetUsingAbsoluteLocation(true);
		C->SetUsingAbsoluteScale(true);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetGenerateOverlapEvents(false);
		C->CastShadow = false;
		C->bCastDynamicShadow = false;
		C->SetMobility(EComponentMobility::Movable);
		C->SetTranslucentSortPriority(2);
		C->SetVisibility(false);
		if (Mesh) { C->SetStaticMesh(Mesh); }
		C->RegisterComponent();
		return C;
	};
	Beam = Make(TEXT("TractorBeamMesh"), Cylinder);
	EmitterGlow = Make(TEXT("TractorEmitterGlowMesh"), Sphere);
	if (UMaterialInterface* Mat = SlonkMaterials::LoadOrFallback(BeamMaterial, TEXT("TractorBeam")))
	{
		MID = UMaterialInstanceDynamic::Create(Mat, this);
		MID->SetVectorParameterValue(TEXT("BeamColor"), BeamColor);
		Beam->SetMaterial(0, MID);
		EmitterGlow->SetMaterial(0, MID);
	}
}

void UTractorBeamComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Release();
	Super::EndPlay(EndPlayReason);
}

double UTractorBeamComponent::TargetRadiusCm(const AActor* Actor)
{
	if (const ASpaceVessel* V = Cast<ASpaceVessel>(Actor))
	{
		return V->GetBoundingRadiusCm();
	}
	if (const ASpaceTarget* T = Cast<ASpaceTarget>(Actor))
	{
		return T->Radius;
	}
	return Actor ? Actor->GetComponentsBoundingBox().GetExtent().GetMax() : 0.0;
}

double UTractorBeamComponent::GetRangeCm() const
{
	return Target && GetOwner() ? FVector::Dist(Target->GetActorLocation(), GetOwner()->GetActorLocation()) : 0.0;
}

bool UTractorBeamComponent::Engage(AActor* NewTarget)
{
	AActor* Owner = GetOwner();
	if (!NewTarget || !Owner || NewTarget == Owner)
	{
		return false;
	}
	// Only things small enough to move: rocks, ships, stations.
	if (!NewTarget->IsA<ASpaceVessel>() && !NewTarget->IsA<ASpaceTarget>())
	{
		return false;
	}
	if (FVector::Dist(NewTarget->GetActorLocation(), Owner->GetActorLocation()) > MaxRangeCm)
	{
		return false;
	}
	Release();
	Target = NewTarget;
	if (ASpaceVessel* V = Cast<ASpaceVessel>(Target))
	{
		V->SetTractored(true);
	}
	Age = 0.f;
	if (Beam) { Beam->SetVisibility(true); }
	if (EmitterGlow) { EmitterGlow->SetVisibility(true); }
	return true;
}

void UTractorBeamComponent::Release()
{
	if (ASpaceVessel* V = Cast<ASpaceVessel>(Target))
	{
		V->SetTractored(false);
	}
	Target = nullptr;
	if (Beam) { Beam->SetVisibility(false); }
	if (EmitterGlow) { EmitterGlow->SetVisibility(false); }
}

void UTractorBeamComponent::UpdateBeamMesh(const FVector& Start, const FVector& End, double Radius)
{
	if (!Beam)
	{
		return;
	}
	const FVector D = End - Start;
	const double Len = D.Size();
	if (Len < 1.0)
	{
		return;
	}
	// Engine cylinder: 100 cm tall along Z, 50 cm radius, centred.
	Beam->SetWorldLocation((Start + End) * 0.5);
	Beam->SetWorldRotation(FRotationMatrix::MakeFromZ(D / Len).Rotator());
	Beam->SetWorldScale3D(FVector(Radius / 50.0, Radius / 50.0, Len / 100.0));
	if (EmitterGlow)
	{
		EmitterGlow->SetWorldLocation(Start);
		EmitterGlow->SetWorldScale3D(FVector(Radius * 1.6 / 50.0));
	}
	if (MID)
	{
		MID->SetScalarParameterValue(TEXT("BeamLength"), (float)(Len / 100000.0));
		MID->SetScalarParameterValue(TEXT("Age"), Age);
	}
}

void UTractorBeamComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AActor* Owner = GetOwner();
	if (!Target || !Owner)
	{
		return;
	}
	if (!IsValid(Target))
	{
		Release();
		return;
	}
	Age += DeltaTime;
	const FVector ShipLoc = Owner->GetActorLocation();
	const FVector Emitter = Owner->GetActorTransform().TransformPosition(EmitterOffset);
	FVector ToTarget = Target->GetActorLocation() - ShipLoc;
	const double Dist = ToTarget.Size();
	if (Dist > MaxRangeCm * 1.5 || Dist < 1.0)
	{
		Release();
		return;
	}
	const FVector Dir = ToTarget / Dist;
	const double TargetR = TargetRadiusCm(Target);
	const double OwnLength = Owner->GetComponentsBoundingBox().GetExtent().GetMax() * 2.0;
	const double Hold = OwnLength * HoldLengths + TargetR;
	// Reel in toward the hold distance, then keep station on the ship.
	const double NewDist = FMath::Max(Dist - PullSpeed * DeltaTime, Hold);
	const FVector NewLoc = ShipLoc + Dir * NewDist;
	Target->SetActorLocation(NewLoc, false, nullptr, ETeleportType::TeleportPhysics);
	// Beam from the dish to the near face of the target.
	const FVector End = NewLoc - Dir * TargetR * 0.7;
	UpdateBeamMesh(Emitter, End, FMath::Clamp(TargetR * 0.35, 150.0, 4000.0));
}
