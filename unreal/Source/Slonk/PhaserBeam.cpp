#include "PhaserBeam.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "SlonkMaterials.h"
#include "UObject/ConstructorHelpers.h"

APhaserBeam::APhaserBeam()
{
	PrimaryActorTick.bCanEverTick = true;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));

	auto MakeMesh = [this](const TCHAR* Name, UStaticMesh* Mesh)
	{
		UStaticMeshComponent* C = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		C->SetupAttachment(RootComponent);
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->CastShadow = false;
		C->bCastDynamicShadow = false;
		if (Mesh)
		{
			C->SetStaticMesh(Mesh);
		}
		return C;
	};

	Beam = MakeMesh(TEXT("Beam"), CylinderFinder.Succeeded() ? CylinderFinder.Object : nullptr);
	MuzzleGlow = MakeMesh(TEXT("MuzzleGlow"), SphereFinder.Succeeded() ? SphereFinder.Object : nullptr);
	ImpactGlow = MakeMesh(TEXT("ImpactGlow"), SphereFinder.Succeeded() ? SphereFinder.Object : nullptr);

	ImpactLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("ImpactLight"));
	ImpactLight->SetupAttachment(RootComponent);
	ImpactLight->SetIntensityUnits(ELightUnits::Candelas);
	ImpactLight->SetIntensity(200000.f);
	ImpactLight->SetAttenuationRadius(15000.f);
	ImpactLight->SetLightColor(BeamColor);
	ImpactLight->CastShadows = false;

	BeamMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(SlonkMaterials::PhaserBeamPath));
}

void APhaserBeam::BeginPlay()
{
	Super::BeginPlay();
	if (UMaterialInterface* Mat = SlonkMaterials::LoadOrFallback(BeamMaterial, TEXT("PhaserBeam")))
	{
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Mat, this);
		Mid->SetVectorParameterValue(TEXT("BeamColor"), BeamColor);
		Beam->SetMaterial(0, Mid);
		MuzzleGlow->SetMaterial(0, Mid);
		ImpactGlow->SetMaterial(0, Mid);
	}
	ImpactLight->SetLightColor(BeamColor);
}

void APhaserBeam::SetBeam(const FVector& Start, const FVector& End, bool bHitSomething)
{
	CachedStart = Start;
	CachedEnd = End;
	bCachedHit = bHitSomething;

	const FVector Dir = (End - Start);
	const double Length = Dir.Size();
	if (Length < 1.0)
	{
		SetActorHiddenInGame(true);
		return;
	}
	SetActorHiddenInGame(false);

	// Keep the actor near the beam so LWC precision stays good along its length.
	SetActorLocation(Start);

	// The engine cylinder is 100 cm tall along its local Z, centred on its origin.
	const FRotator Rot = FRotationMatrix::MakeFromZ(Dir / Length).Rotator();
	const float Pulse = 1.f + 0.15f * FMath::Sin(Age * 55.f);
	Beam->SetWorldLocationAndRotation(Start + Dir * 0.5, Rot);
	Beam->SetWorldScale3D(FVector(BeamThickness * Pulse / 100.f, BeamThickness * Pulse / 100.f, Length / 100.0));

	const float MuzzleSize = BeamThickness * 2.2f * Pulse;
	MuzzleGlow->SetWorldLocation(Start);
	MuzzleGlow->SetWorldScale3D(FVector(MuzzleSize / 100.f));

	ImpactGlow->SetWorldLocation(End);
	ImpactGlow->SetVisibility(bHitSomething);
	ImpactGlow->SetWorldScale3D(FVector(BeamThickness * 4.f * Pulse / 100.f));
	ImpactLight->SetWorldLocation(End - Dir.GetSafeNormal() * BeamThickness * 3.0);
	ImpactLight->SetVisibility(bHitSomething);
}

void APhaserBeam::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	Age += DeltaTime;
}
