#include "HullDebris.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

AHullDebris::AHullDebris()
{
	PrimaryActorTick.bCanEverTick = true;
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->CastShadow = false;
	Mesh->bCastDynamicShadow = false;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded())
	{
		Mesh->SetStaticMesh(CubeFinder.Object);
	}
}

void AHullDebris::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	Age += DeltaTime;
	AddActorWorldOffset(Velocity * DeltaTime);
	AddActorLocalRotation(Tumble * DeltaTime);
	if (Age > LifeSeconds)
	{
		Destroy();
	}
}

void AHullDebris::Scatter(UWorld* World, const FVector& Origin, const FVector& BaseVelocity, float SizeCm, UMaterialInterface* Material, const FLinearColor& Color, int32 Count, int32 Seed)
{
	if (!World)
	{
		return;
	}
	FRandomStream Rng(Seed);
	for (int32 i = 0; i < Count; ++i)
	{
		const FVector Dir = Rng.GetUnitVector();
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AHullDebris* D = World->SpawnActor<AHullDebris>(AHullDebris::StaticClass(), Origin + Dir * SizeCm * Rng.FRandRange(0.05f, 0.35f), FRotator(Rng.FRandRange(0.f, 360.f), Rng.FRandRange(0.f, 360.f), 0.f), P);
		if (!D)
		{
			continue;
		}
		// Engine cube is 100 cm; fragments are flat-ish plates of varying size.
		const FVector Dims(SizeCm * Rng.FRandRange(0.04f, 0.16f), SizeCm * Rng.FRandRange(0.03f, 0.12f), SizeCm * Rng.FRandRange(0.008f, 0.03f));
		D->Mesh->SetRelativeScale3D(Dims / 100.f);
		D->Velocity = BaseVelocity + Dir * SizeCm * Rng.FRandRange(0.15f, 0.6f);
		D->Tumble = FRotator(Rng.FRandRange(-90.f, 90.f), Rng.FRandRange(-90.f, 90.f), Rng.FRandRange(-90.f, 90.f));
		D->LifeSeconds = Rng.FRandRange(20.f, 40.f);
		if (Material)
		{
			UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Material, D);
			Mid->SetVectorParameterValue(TEXT("HullColor"), Color * 0.6f);
			Mid->SetScalarParameterValue(TEXT("PanelScale"), 400.f / FMath::Max(SizeCm, 1.f));
			D->Mesh->SetMaterial(0, Mid);
		}
	}
}
