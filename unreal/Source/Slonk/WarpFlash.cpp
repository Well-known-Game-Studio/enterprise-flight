#include "WarpFlash.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "SlonkMaterials.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	UStaticMeshComponent* MakeMesh(AActor* Owner, const TCHAR* Name, const TCHAR* MeshPath)
	{
		UStaticMeshComponent* C = Owner->CreateDefaultSubobject<UStaticMeshComponent>(Name);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetGenerateOverlapEvents(false);
		C->CastShadow = false;
		C->bCastDynamicShadow = false;
		C->SetMobility(EComponentMobility::Movable);
		C->SetTranslucentSortPriority(5);
		static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
		static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		if (FCString::Strcmp(MeshPath, TEXT("Cylinder")) == 0 && CylinderFinder.Succeeded()) { C->SetStaticMesh(CylinderFinder.Object); }
		if (FCString::Strcmp(MeshPath, TEXT("Sphere")) == 0 && SphereFinder.Succeeded()) { C->SetStaticMesh(SphereFinder.Object); }
		return C;
	}
}

AWarpStreakFlash::AWarpStreakFlash()
{
	PrimaryActorTick.bCanEverTick = true;
	Streak = MakeMesh(this, TEXT("Streak"), TEXT("Cylinder"));
	SetRootComponent(Streak);
}

AWarpStreakFlash* AWarpStreakFlash::Spawn(UWorld* World, const FVector& Origin, const FVector& Direction, float ShipLengthCm, bool bArrival, const FLinearColor& Color)
{
	if (!World)
	{
		return nullptr;
	}
	const FVector Dir = Direction.GetSafeNormal();
	const float Len = ShipLengthCm * 28.f;
	// The engine cylinder runs along +Z, 100 cm long and 100 cm across, centred on its origin.
	const FVector Centre = bArrival ? Origin - Dir * Len * 0.5f : Origin + Dir * Len * 0.5f;
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AWarpStreakFlash* F = World->SpawnActor<AWarpStreakFlash>(AWarpStreakFlash::StaticClass(), Centre, FRotationMatrix::MakeFromZ(Dir).Rotator(), P);
	if (!F)
	{
		return nullptr;
	}
	F->Length = Len;
	F->Radius = ShipLengthCm * 0.12f;
	F->Tint = Color;
	UMaterialInterface* Mat = SlonkMaterials::LoadOrFallback(TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(SlonkMaterials::TractorBeamPath)), TEXT("WarpStreak"));
	if (Mat)
	{
		F->MID = UMaterialInstanceDynamic::Create(Mat, F);
		F->MID->SetVectorParameterValue(TEXT("BeamColor"), Color);
		F->MID->SetScalarParameterValue(TEXT("Intensity"), 70.f);
		F->MID->SetScalarParameterValue(TEXT("BeamLength"), 3.f);
		F->Streak->SetMaterial(0, F->MID);
	}
	F->Tick(0.f);
	return F;
}

void AWarpStreakFlash::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	Age += DeltaTime;
	const float T = FMath::Clamp(Age / FMath::Max(Lifetime, 0.01f), 0.f, 1.f);
	const float Fade = (1.f - T) * (1.f - T);
	// Thin out faster than it fades so the streak ends as a hairline.
	const float Sx = Radius / 50.f * (0.15f + 0.85f * Fade);
	const float Sz = Length / 100.f;
	Streak->SetRelativeScale3D(FVector(Sx, Sx, Sz));
	if (MID) { MID->SetScalarParameterValue(TEXT("Intensity"), 70.f * Fade); }
	if (Age >= Lifetime)
	{
		Destroy();
	}
}
