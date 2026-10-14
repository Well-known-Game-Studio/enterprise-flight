#include "SpaceTraffic.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "PlanetBody.h"
#include "SpaceVessel.h"
#include "BorgCube.h"

namespace
{
	const TCHAR* ShipNames[] = { TEXT("Farragut"), TEXT("Lexington"), TEXT("Potemkin"), TEXT("Hood"), TEXT("Excalibur"), TEXT("Yorktown"), TEXT("Exeter"), TEXT("Constellation"), TEXT("Defiant"), TEXT("Intrepid"), TEXT("Kongo"), TEXT("Republic"), TEXT("Valiant"), TEXT("Endeavour"), TEXT("Saratoga"), TEXT("Reliant") };
	const TCHAR* StationNames[] = { TEXT("Starbase 11"), TEXT("Deep Space Station K-7"), TEXT("Starbase 6"), TEXT("Regula Station"), TEXT("Starbase 47"), TEXT("Epsilon IX"), TEXT("Starbase 24"), TEXT("Vanguard") };
}

ASpaceTraffic::ASpaceTraffic()
{
	PrimaryActorTick.bCanEverTick = true;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Root);
}

void ASpaceTraffic::BeginPlay()
{
	Super::BeginPlay();
	Populate();
}

void ASpaceTraffic::Populate()
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || bPopulated)
	{
		return;
	}
	bPopulated = true;
	FRandomStream Rng(Seed);
	int32 ShipIndex = 0, StationIndex = 0;
	auto SpawnShip = [&](const FVector& Loc, const FVector& Home, double Patrol)
	{
		// Deferred, so seed, size, colours and patrol data are in place when BeginPlay runs the one Rebuild.
		const FTransform Xf(FRotator(0.f, Rng.FRandRange(0.f, 360.f), 0.f), Loc);
		AProceduralStarship* Ship = World->SpawnActorDeferred<AProceduralStarship>(AProceduralStarship::StaticClass(), Xf, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Ship) { return; }
		Ship->Seed = Rng.RandRange(1, 1 << 20);
		Ship->SizeCm = Rng.FRandRange(22000.f, 45000.f);
		Ship->VesselName = FString::Printf(TEXT("USS %s"), ShipNames[ShipIndex++ % UE_ARRAY_COUNT(ShipNames)]);
		const float Tint = Rng.FRandRange(0.55f, 0.72f);
		Ship->HullColor = FLinearColor(Tint, Tint * Rng.FRandRange(0.98f, 1.04f), Tint * Rng.FRandRange(1.0f, 1.1f));
		Ship->GlowColor = Rng.FRand() < 0.75f ? FLinearColor(0.35f, 0.6f, 1.0f) : FLinearColor(1.0f, 0.35f, 0.15f);
		Ship->HomeLocation = Home;
		Ship->PatrolRadiusCm = Patrol;
		Ship->CruiseSpeed = Rng.FRandRange(1.5e6, 3.5e6);
		Ship->FinishSpawning(Xf);
		Spawned.Add(Ship);
	};
	for (TActorIterator<APlanetBody> It(World); It; ++It)
	{
		if (It->IsMoon())
		{
			continue;
		}
		const double R = It->GetRadiusCm();
		const float A = Rng.FRandRange(0.f, 2.f * PI);
		const FVector Loc = It->GetActorLocation() + FVector(FMath::Cos(A), FMath::Sin(A), 0.f) * R * StationOrbitRadii;
		const FTransform StationXf(FRotator(Rng.FRandRange(-15.f, 15.f), Rng.FRandRange(0.f, 360.f), 0.f), Loc);
		ASpaceStation* Station = World->SpawnActorDeferred<ASpaceStation>(ASpaceStation::StaticClass(), StationXf, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Station) { continue; }
		Station->Seed = Rng.RandRange(1, 1 << 20);
		Station->SizeCm = Rng.FRandRange(250000.f, 600000.f);
		Station->VesselName = StationNames[StationIndex++ % UE_ARRAY_COUNT(StationNames)];
		Station->FinishSpawning(StationXf);
		Spawned.Add(Station);
		for (int32 i = 0; i < ShipsPerStation; ++i)
		{
			SpawnShip(Loc + Rng.GetUnitVector() * Station->SizeCm * Rng.FRandRange(3.f, 8.f), Loc, Station->SizeCm * 10.0);
		}
	}
}

void ASpaceTraffic::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bStartShips)
	{
		return;
	}
	StartTimer -= DeltaTime;
	if (StartTimer > 0.f)
	{
		return;
	}
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const APawn* Player = PC ? PC->GetPawn() : nullptr;
	if (!Player)
	{
		return;
	}
	bStartShips = true;
	FRandomStream Rng(Seed * 7 + 3);
	const FVector Base = Player->GetActorLocation();
	for (int32 i = 0; i < ShipsNearStart; ++i)
	{
		const FVector Loc = Base + Rng.GetUnitVector() * StartSpreadCm * Rng.FRandRange(0.3f, 1.f);
		const FTransform Xf(FRotator(0.f, Rng.FRandRange(0.f, 360.f), 0.f), Loc);
		AProceduralStarship* Ship = World->SpawnActorDeferred<AProceduralStarship>(AProceduralStarship::StaticClass(), Xf, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Ship) { continue; }
		Ship->Seed = Rng.RandRange(1, 1 << 20);
		Ship->SizeCm = Rng.FRandRange(22000.f, 45000.f);
		Ship->VesselName = FString::Printf(TEXT("USS %s"), ShipNames[(i + 9) % UE_ARRAY_COUNT(ShipNames)]);
		Ship->GlowColor = FLinearColor(0.35f, 0.6f, 1.0f);
		Ship->HomeLocation = Base;
		Ship->PatrolRadiusCm = StartSpreadCm * 1.5;
		Ship->FinishSpawning(Xf);
		Spawned.Add(Ship);
	}
	if (BorgDistanceCm > 0.f)
	{
		const float A = Rng.FRandRange(0.f, 2.f * PI);
		const FVector Loc = Base + FVector(FMath::Cos(A), FMath::Sin(A), 0.05f) * BorgDistanceCm;
		const FTransform CubeXf(FRotator(0.f, Rng.FRandRange(0.f, 360.f), 0.f), Loc);
		if (ABorgCube* Cube = World->SpawnActorDeferred<ABorgCube>(ABorgCube::StaticClass(), CubeXf, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
		{
			Cube->Seed = Rng.RandRange(1, 1 << 20);
			Cube->FinishSpawning(CubeXf);
			Spawned.Add(Cube);
		}
	}
}
