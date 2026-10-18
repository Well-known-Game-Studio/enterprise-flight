#include "SpaceTraffic.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "PlanetBody.h"
#include "SpaceVessel.h"
#include "BorgCube.h"
#include "SlonkLoadSubsystem.h"
#include "SlonkTiming.h"

namespace
{
	const TCHAR* ShipNames[] = { TEXT("Farragut"), TEXT("Lexington"), TEXT("Potemkin"), TEXT("Hood"), TEXT("Excalibur"), TEXT("Yorktown"), TEXT("Exeter"), TEXT("Constellation"), TEXT("Defiant"), TEXT("Intrepid"), TEXT("Kongo"), TEXT("Republic"), TEXT("Valiant"), TEXT("Endeavour"), TEXT("Saratoga"), TEXT("Reliant") };
	const TCHAR* StationNames[] = { TEXT("Starbase 11"), TEXT("Deep Space Station K-7"), TEXT("Starbase 6"), TEXT("Regula Station"), TEXT("Starbase 47"), TEXT("Epsilon IX"), TEXT("Starbase 24"), TEXT("Vanguard") };

	/** The class each name was in the films: Reliant and Saratoga were Mirandas, the TNG-era Hood, Potemkin,
	 *  Lexington and Farragut Excelsiors, the rest Constitutions. */
	EStarshipClass ClassForName(const TCHAR* Name)
	{
		const FString N(Name);
		if (N == TEXT("Reliant") || N == TEXT("Saratoga") || N == TEXT("Valiant")) { return EStarshipClass::Miranda; }
		if (N == TEXT("Hood") || N == TEXT("Potemkin") || N == TEXT("Lexington") || N == TEXT("Farragut")) { return EStarshipClass::Excelsior; }
		return EStarshipClass::Constitution;
	}

	/** Lengths in cm: Constitution refit 305 m, Miranda 243 m, Excelsior 467 m, with some spread. */
	float SizeForClass(EStarshipClass C, FRandomStream& Rng)
	{
		switch (C)
		{
		case EStarshipClass::Miranda: return 24300.f * Rng.FRandRange(0.92f, 1.08f);
		case EStarshipClass::Excelsior: return 46700.f * Rng.FRandRange(0.92f, 1.08f);
		default: return 30500.f * Rng.FRandRange(0.92f, 1.08f);
		}
	}

	EStationStyle StyleForName(const FString& Name)
	{
		if (Name.Contains(TEXT("K-7"))) { return EStationStyle::K7; }
		if (Name.Contains(TEXT("Regula")) || Name.Contains(TEXT("Epsilon"))) { return EStationStyle::Regula; }
		if (Name == TEXT("Starbase 6") || Name == TEXT("Starbase 24")) { return EStationStyle::Ring; }
		return EStationStyle::Spacedock;
	}
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
	USlonkLoadSubsystem::Enqueue(this, TEXT("Spawning traffic"), [this]() { SpawnStartShips(); }, 0.5f);
}

void ASpaceTraffic::Populate()
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || bPopulated)
	{
		return;
	}
	bPopulated = true;
	FSlonkScopeTimer Timer(TEXT("ASpaceTraffic::Populate"));
	FRandomStream Rng(Seed);
	int32 ShipIndex = 0, StationIndex = 0;
	auto SpawnShip = [&](const FVector& Loc, const FVector& Home, double Patrol)
	{
		// Deferred, so seed, size, colours and patrol data are in place when BeginPlay runs the one Rebuild.
		const FTransform Xf(FRotator(0.f, Rng.FRandRange(0.f, 360.f), 0.f), Loc);
		AProceduralStarship* Ship = World->SpawnActorDeferred<AProceduralStarship>(AProceduralStarship::StaticClass(), Xf, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Ship) { return; }
		Ship->Seed = Rng.RandRange(1, 1 << 20);
		const TCHAR* Name = ShipNames[ShipIndex++ % UE_ARRAY_COUNT(ShipNames)];
		Ship->ShipClass = ClassForName(Name);
		Ship->SizeCm = SizeForClass(Ship->ShipClass, Rng);
		Ship->VesselName = FString::Printf(TEXT("USS %s"), Name);
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
		Station->VesselName = StationNames[StationIndex++ % UE_ARRAY_COUNT(StationNames)];
		Station->Style = StyleForName(Station->VesselName);
		// Spacedock-type bases are the giants; the others are a kilometre or two across.
		Station->SizeCm = Station->Style == EStationStyle::Spacedock ? Rng.FRandRange(450000.f, 650000.f) : Rng.FRandRange(150000.f, 300000.f);
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
}

void ASpaceTraffic::SpawnStartShips()
{
	if (bStartShips)
	{
		return;
	}
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const APawn* Player = PC ? PC->GetPawn() : nullptr;
	if (!Player)
	{
		// The pawn is possessed a frame or two after BeginPlay; try again next frame.
		USlonkLoadSubsystem::Enqueue(this, TEXT("Spawning traffic"), [this]() { SpawnStartShips(); }, 0.01f);
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
		const TCHAR* Name = ShipNames[(i + 9) % UE_ARRAY_COUNT(ShipNames)];
		Ship->ShipClass = ClassForName(Name);
		Ship->SizeCm = SizeForClass(Ship->ShipClass, Rng);
		Ship->VesselName = FString::Printf(TEXT("USS %s"), Name);
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
