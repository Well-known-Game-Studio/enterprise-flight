#include "SpaceTraffic.h"
#include "StarSystem.h"
#include "StarBody.h"
#include "Nebula.h"

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
	// Starbases orbit the home planets; the deep-space stations sit out beyond the planets, beside the nebulae.
	const TCHAR* StationNames[] = { TEXT("Starbase 11"), TEXT("Starbase 6"), TEXT("Starbase 47"), TEXT("Starbase 24"), TEXT("Starbase 12"), TEXT("Starbase 74"), TEXT("Starbase 375"), TEXT("Starbase 234") };
	const TCHAR* DeepSpaceNames[] = { TEXT("Deep Space Station K-7"), TEXT("Regula Station"), TEXT("Epsilon IX"), TEXT("Vanguard") };

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
	TArray<APlanetBody*> Planets;
	for (TActorIterator<APlanetBody> It(World); It; ++It)
	{
		Planets.Add(*It);
	}
	PopulateWith(Planets, Rng, false);
}

void ASpaceTraffic::PopulatePlanets(const TArray<TObjectPtr<APlanetBody>>& Planets, int32 SeedValue)
{
	FRandomStream Rng(SeedValue);
	TArray<APlanetBody*> List;
	for (const TObjectPtr<APlanetBody>& P : Planets)
	{
		if (P) { List.Add(P); }
	}
	PopulateWith(List, Rng, true);
}

void ASpaceTraffic::SpawnPatrolShip(FRandomStream& Rng, const FVector& Loc, const FVector& Home, double Patrol)
{
	UWorld* World = GetWorld();
	if (!World) { return; }
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
}

ASpaceStation* ASpaceTraffic::SpawnStationAt(const FString& Name, const FString& StyleName, const FVector& Loc, FRandomStream& Rng)
{
	UWorld* World = GetWorld();
	if (!World) { return nullptr; }
	const FTransform StationXf(FRotator(Rng.FRandRange(-15.f, 15.f), Rng.FRandRange(0.f, 360.f), 0.f), Loc);
	ASpaceStation* Station = World->SpawnActorDeferred<ASpaceStation>(ASpaceStation::StaticClass(), StationXf, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Station) { return nullptr; }
	Station->Seed = Rng.RandRange(1, 1 << 20);
	Station->VesselName = Name;
	Station->Style = StyleForName(StyleName);
	// Spacedock-type bases are the giants; the others are a kilometre or two across.
	Station->SizeCm = Station->Style == EStationStyle::Spacedock ? Rng.FRandRange(450000.f, 650000.f) : Rng.FRandRange(150000.f, 300000.f);
	Station->FinishSpawning(StationXf);
	Spawned.Add(Station);
	for (int32 i = 0; i < ShipsPerStation; ++i)
	{
		SpawnPatrolShip(Rng, Loc + Rng.GetUnitVector() * Station->SizeCm * Rng.FRandRange(3.f, 8.f), Loc, Station->SizeCm * 10.0);
	}
	return Station;
}

void ASpaceTraffic::PopulateWith(const TArray<APlanetBody*>& Planets, FRandomStream& Rng, bool bNameStationsByPlanet)
{
	UWorld* World = GetWorld();
	if (!World) { return; }
	for (APlanetBody* Planet : Planets)
	{
		if (!Planet || Planet->IsMoon())
		{
			continue;
		}
		const double R = Planet->GetRadiusCm();
		const float A = Rng.FRandRange(0.f, 2.f * PI);
		const FVector Loc = Planet->GetActorLocation() + FVector(FMath::Cos(A), FMath::Sin(A), 0.f) * R * StationOrbitRadii;
		// Generated systems name their stations after the planet ("Sirius III Station"): the stock starbase names
		// are the home system's, and a nav target by name must be unique across systems.
		const FString Stock = StationNames[StationIndex++ % UE_ARRAY_COUNT(StationNames)];
		SpawnStationAt(bNameStationsByPlanet ? Planet->GetDisplayName() + TEXT(" Station") : Stock, Stock, Loc, Rng);
	}
}

void ASpaceTraffic::SpawnDeepSpaceStations()
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || bDeepSpaceSpawned) { return; }
	bDeepSpaceSpawned = true;
	FRandomStream Rng(Seed * 7 + 3);
	// The home star is the sun nearest this spawner.
	FVector Star = FVector::ZeroVector;
	double BestD = TNumericLimits<double>::Max();
	for (TActorIterator<AStarBody> It(World); It; ++It)
	{
		const double D = FVector::DistSquared(It->GetActorLocation(), GetActorLocation());
		if (D < BestD) { BestD = D; Star = It->GetActorLocation(); }
	}
	// This system's nebulae that lie far enough out, nearest first; other systems' are light-years away.
	const double MinR = MinDeepSpaceAu * AStarSystem::AuCm;
	TArray<ANebula*> Nebs;
	for (TActorIterator<ANebula> It(World); It; ++It)
	{
		const double D = FVector::Dist(It->GetActorLocation(), Star);
		if (D >= MinR && D < 500.0 * AStarSystem::AuCm) { Nebs.Add(*It); }
	}
	Nebs.Sort([&](const ANebula& A, const ANebula& B) { return FVector::DistSquared(A.GetActorLocation(), Star) < FVector::DistSquared(B.GetActorLocation(), Star); });
	const int32 N = UE_ARRAY_COUNT(DeepSpaceNames);
	for (int32 i = 0; i < N; ++i)
	{
		FVector Loc;
		if (i < Nebs.Num())
		{
			// Just outside the cloud on its far side from the star, so the glow is the backdrop from the inner system.
			const ANebula* Neb = Nebs[i];
			const FVector Away = (Neb->GetActorLocation() - Star).GetSafeNormal();
			const FVector Side = FVector::CrossProduct(Away, FVector::UpVector).GetSafeNormal();
			Loc = Neb->GetActorLocation() + Away * (Neb->GetRadiusCm() * 1.6) + Side * (Neb->GetRadiusCm() * Rng.FRandRange(-0.5f, 0.5f));
		}
		else
		{
			// On its own, spread about the star between eight and twelve AU, a little off the plane.
			const double Ang = 2.0 * PI * i / N + Rng.FRandRange(-0.4f, 0.4f);
			const double R = AStarSystem::AuCm * Rng.FRandRange(8.f, 12.f);
			Loc = Star + FVector(FMath::Cos(Ang) * R, FMath::Sin(Ang) * R, AStarSystem::AuCm * Rng.FRandRange(-0.3f, 0.3f));
		}
		if (ASpaceStation* S = SpawnStationAt(DeepSpaceNames[i], DeepSpaceNames[i], Loc, Rng))
		{
			UE_LOG(LogTemp, Log, TEXT("Deep-space station %s at %.1f AU%s"), *S->VesselName, FVector::Dist(Loc, Star) / AStarSystem::AuCm, i < Nebs.Num() ? *(TEXT(" beside ") + Nebs[i]->GetDisplayName()) : TEXT(""));
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
