#include "BeltField.h"
#include "SlonkTiming.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "PlanetBody.h"
#include "RockMeshes.h"
#include "SlonkMaterials.h"
#include "StarBody.h"

ABeltField::ABeltField()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Root);
	RockMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(SlonkMaterials::AsteroidPath));
}

AActor* ABeltField::FindStar()
{
	if (Star)
	{
		return Star;
	}
	if (CachedStar.IsValid())
	{
		return CachedStar.Get();
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	// The nearest star: with several systems in the world the first one found may be light-years away.
	AStarBody* Nearest = nullptr;
	double BestD = TNumericLimits<double>::Max();
	for (TActorIterator<AStarBody> StarIt(World); StarIt; ++StarIt)
	{
		const double D = FVector::DistSquared(StarIt->GetActorLocation(), GetActorLocation());
		if (D < BestD) { BestD = D; Nearest = *StarIt; }
	}
	if (Nearest)
	{
		CachedStar = Nearest;
		return Nearest;
	}
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->ActorHasTag(APlanetBody::StarTag))
		{
			CachedStar = *It;
			return *It;
		}
	}
	return nullptr;
}

FVector ABeltField::GetStarLocation() const
{
	if (Star)
	{
		return Star->GetActorLocation();
	}
	return CachedStar.IsValid() ? CachedStar->GetActorLocation() : FVector::ZeroVector;
}

FVector ABeltField::NearestBeltPoint(const FVector& WorldLocation) const
{
	const FVector StarLoc = GetStarLocation();
	FVector Radial = WorldLocation - StarLoc;
	Radial.Z = 0.0;
	if (Radial.IsNearlyZero())
	{
		Radial = FVector::ForwardVector;
	}
	return StarLoc + Radial.GetSafeNormal() * BeltRadiusCm;
}

void ABeltField::BeginPlay()
{
	Super::BeginPlay();
	UMaterialInterface* Mat = SlonkMaterials::LoadOrFallback(RockMaterial, TEXT("BeltRock"), /*bLit*/ true);
	for (int32 V = 0; V < SlonkRocks::NumVariants; ++V)
	{
		UHierarchicalInstancedStaticMeshComponent* Layer = NewObject<UHierarchicalInstancedStaticMeshComponent>(this, *FString::Printf(TEXT("Rocks%d"), V));
		Layer->SetupAttachment(Root);
		Layer->SetMobility(EComponentMobility::Movable);
		Layer->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Layer->SetCollisionObjectType(ECC_WorldStatic);
		Layer->SetCollisionResponseToAllChannels(ECR_Block);
		Layer->SetGenerateOverlapEvents(false);
		Layer->CastShadow = false;
		Layer->bCastDynamicShadow = false;
		Layer->bAffectDistanceFieldLighting = false;
		Layer->bAffectDynamicIndirectLighting = false;
		Layer->SetStaticMesh(SlonkRocks::GetRockMesh(V));
		if (Mat)
		{
			Layer->SetMaterial(0, Mat);
		}
		Layer->RegisterComponent();
		RockLayers.Add(Layer);
	}
}

void ABeltField::ClearField()
{
	for (UHierarchicalInstancedStaticMeshComponent* Layer : RockLayers)
	{
		if (Layer && Layer->GetInstanceCount() > 0)
		{
			Layer->ClearInstances();
		}
	}
	RockCount = 0;
	bActive = false;
	CenterCell = FIntVector(TNumericLimits<int32>::Max());
}

void ABeltField::Rebuild(const FIntVector& Center, const FVector& StarLoc)
{
	// Park the actor at the centre cell's origin so instance offsets stay small floats.
	const FVector Origin = StarLoc + FVector(Center) * CellSizeCm;
	SetActorLocation(Origin);

	TArray<TArray<FTransform>> PerLayer;
	PerLayer.SetNum(RockLayers.Num());
	const int32 R = FMath::Clamp(CellRange, 1, 4);
	for (int32 Z = -R; Z <= R; ++Z)
	{
		for (int32 Y = -R; Y <= R; ++Y)
		{
			for (int32 X = -R; X <= R; ++X)
			{
				const FIntVector Key = Center + FIntVector(X, Y, Z);
				const FVector CellMin = FVector(Key) * CellSizeCm;
				const FVector CellMid = CellMin + FVector(CellSizeCm * 0.5);
				const double Radial = FMath::Sqrt(CellMid.X * CellMid.X + CellMid.Y * CellMid.Y);
				const double DR = (Radial - BeltRadiusCm) / FMath::Max(BeltHalfWidthCm, 1.0);
				const double DZ = CellMid.Z / FMath::Max(BeltHalfThicknessCm, 1.0);
				const double Weight = FMath::Exp(-(DR * DR + DZ * DZ));
				const uint32 Hash = HashCombine(HashCombine(GetTypeHash(Key.X), GetTypeHash(Key.Y)), HashCombine(GetTypeHash(Key.Z), GetTypeHash(Seed)));
				FRandomStream Rng(static_cast<int32>(Hash));
				const int32 Count = FMath::RoundToInt(RocksPerCell * Weight * Rng.FRandRange(0.6f, 1.4f));
				for (int32 i = 0; i < Count; ++i)
				{
					const FVector Local = CellMin - FVector(Center) * CellSizeCm + FVector(Rng.FRand(), Rng.FRand(), Rng.FRand()) * CellSizeCm;
					// Log-uniform radii, skewed toward small rocks.
					const float T = FMath::Pow(Rng.FRand(), 2.2f);
					const float Radius = MinRockRadiusCm * FMath::Pow(FMath::Max(MaxRockRadiusCm / MinRockRadiusCm, 1.f), T);
					const FRotator Rot(Rng.FRandRange(0.f, 360.f), Rng.FRandRange(0.f, 360.f), Rng.FRandRange(0.f, 360.f));
					const int32 Layer = Rng.RandRange(0, RockLayers.Num() - 1);
					PerLayer[Layer].Add(FTransform(Rot, Local, FVector(Radius / SlonkRocks::UnitRadius)));
				}
			}
		}
	}
	RockCount = 0;
	for (int32 L = 0; L < RockLayers.Num(); ++L)
	{
		if (RockLayers[L])
		{
			RockLayers[L]->ClearInstances();
			if (PerLayer[L].Num() > 0)
			{
				RockLayers[L]->AddInstances(PerLayer[L], false, false);
			}
			RockCount += PerLayer[L].Num();
		}
	}
	CenterCell = Center;
	bActive = true;
}

void ABeltField::Tick(float DeltaTime)
{
	FSlonkScopeTimer SlonkTimer_(TEXT("Tick BeltField"));
	Super::Tick(DeltaTime);
	UpdateTimer -= DeltaTime;
	if (UpdateTimer > 0.f)
	{
		return;
	}
	UpdateTimer = 0.25f;

	UWorld* World = GetWorld();
	AActor* StarActor = FindStar();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const APawn* Ship = PC ? PC->GetPawn() : nullptr;
	if (!StarActor || !Ship)
	{
		if (bActive) { ClearField(); }
		return;
	}
	const FVector StarLoc = StarActor->GetActorLocation();
	const FVector Rel = Ship->GetActorLocation() - StarLoc;
	const double Radial = FMath::Sqrt(Rel.X * Rel.X + Rel.Y * Rel.Y);
	const double Reach = CellSizeCm * (CellRange + 1);
	const bool bInBand = FMath::Abs(Radial - BeltRadiusCm) < BeltHalfWidthCm * 2.0 + Reach && FMath::Abs(Rel.Z) < BeltHalfThicknessCm * 2.0 + Reach;
	if (!bInBand)
	{
		if (bActive) { ClearField(); }
		// Sit on the belt's mid-circle nearest the ship so "jump to the belt" lands inside the field.
		SetActorLocation(NearestBeltPoint(Ship->GetActorLocation()));
		return;
	}
	const FIntVector Cell(FMath::FloorToInt(Rel.X / CellSizeCm), FMath::FloorToInt(Rel.Y / CellSizeCm), FMath::FloorToInt(Rel.Z / CellSizeCm));
	if (!bActive || Cell != CenterCell)
	{
		Rebuild(Cell, StarLoc);
	}
}
