#include "StarSystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "StarBody.h"
#include "PlanetBody.h"
#include "BeltField.h"
#include "FarBody.h"
#include "SpaceTraffic.h"

AStarSystem::AStarSystem()
{
	PrimaryActorTick.bCanEverTick = false;
	Anchor = CreateDefaultSubobject<USceneComponent>(TEXT("Anchor"));
	Anchor->SetMobility(EComponentMobility::Movable);
	SetRootComponent(Anchor);
}

FVector AStarSystem::DirectionFromSky(float RightAscensionHours, float DeclinationDeg)
{
	const double Ra = FMath::DegreesToRadians((double)RightAscensionHours * 15.0);
	const double Dec = FMath::DegreesToRadians((double)DeclinationDeg);
	return FVector(FMath::Cos(Dec) * FMath::Cos(Ra), FMath::Cos(Dec) * FMath::Sin(Ra), FMath::Sin(Dec));
}

FVector AStarSystem::GetStarLocation() const
{
	return Star ? Star->GetActorLocation() : GetActorLocation();
}

AStarSystem* AStarSystem::Nearest(UWorld* World, const FVector& Location)
{
	AStarSystem* Best = nullptr;
	double BestD = TNumericLimits<double>::Max();
	for (TActorIterator<AStarSystem> It(World); It; ++It)
	{
		const double D = It->DistanceFrom(Location);
		if (D < BestD) { BestD = D; Best = *It; }
	}
	return Best;
}

void AStarSystem::Adopt(AActor* Body)
{
	if (!Body || Body == this || Body->GetAttachParentActor() == this) { return; }
	if (USceneComponent* Root = Body->GetRootComponent())
	{
		if (Root->Mobility != EComponentMobility::Movable) { Root->SetMobility(EComponentMobility::Movable); }
	}
	Body->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
}

int32 AStarSystem::AdoptLoose()
{
	UWorld* World = GetWorld();
	if (!World) { return 0; }
	const FVector Centre = GetStarLocation();
	int32 N = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (!IsValid(A) || A == this || A->GetAttachParentActor() != nullptr) { continue; }
		if (!Cast<IFarBody>(A) && !A->IsA<ABeltField>()) { continue; }
		if (FVector::DistSquared(A->GetActorLocation(), Centre) > RadiusCm * RadiusCm) { continue; }
		Adopt(A);
		++N;
	}
	return N;
}

AStarSystem* AStarSystem::CreateHome(UWorld* World, const FString& Name)
{
	if (!World) { return nullptr; }
	AStarBody* HomeStar = nullptr;
	double Best = TNumericLimits<double>::Max();
	for (TActorIterator<AStarBody> It(World); It; ++It)
	{
		const double D = It->GetActorLocation().SizeSquared();
		if (D < Best) { Best = D; HomeStar = *It; }
	}
	const FVector Loc = HomeStar ? HomeStar->GetActorLocation() : FVector::ZeroVector;
	FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AStarSystem* Sys = World->SpawnActor<AStarSystem>(AStarSystem::StaticClass(), Loc, FRotator::ZeroRotator, SP);
	if (!Sys) { return nullptr; }
	Sys->SystemName = Name;
	Sys->Star = HomeStar;
	if (HomeStar && HomeStar->StarName.IsEmpty()) { HomeStar->StarName = Name; }
	Sys->AdoptLoose();
	for (TActorIterator<APlanetBody> It(World); It; ++It)
	{
		if (It->GetAttachParentActor() == Sys) { Sys->Planets.Add(*It); }
	}
	UE_LOG(LogTemp, Log, TEXT("Star system %s (home): star %s, %d planets"), *Name, HomeStar ? *HomeStar->StarName : TEXT("none"), Sys->Planets.Num());
	return Sys;
}

void AStarSystem::ApplyStarClass(AStarBody* S, FRandomStream& Rng) const
{
	switch (StarClass)
	{
	case EStarClass::M:
		S->RadiusKm = Rng.FRandRange(120000.f, 250000.f);
		S->StarColor = FLinearColor(1.0f, 0.48f, 0.30f); S->CoronaColor = FLinearColor(1.0f, 0.40f, 0.22f); S->Intensity = 45.f;
		break;
	case EStarClass::K:
		S->RadiusKm = Rng.FRandRange(450000.f, 600000.f);
		S->StarColor = FLinearColor(1.0f, 0.72f, 0.45f); S->CoronaColor = FLinearColor(1.0f, 0.6f, 0.3f); S->Intensity = 65.f;
		break;
	case EStarClass::F:
		S->RadiusKm = Rng.FRandRange(850000.f, 1000000.f);
		S->StarColor = FLinearColor(1.0f, 0.95f, 0.85f); S->CoronaColor = FLinearColor(1.0f, 0.9f, 0.7f); S->Intensity = 95.f;
		break;
	case EStarClass::A:
		S->RadiusKm = Rng.FRandRange(1100000.f, 1400000.f);
		S->StarColor = FLinearColor(0.85f, 0.9f, 1.0f); S->CoronaColor = FLinearColor(0.7f, 0.8f, 1.0f); S->Intensity = 110.f;
		break;
	default:
		S->RadiusKm = Rng.FRandRange(650000.f, 750000.f);
		S->StarColor = FLinearColor(1.0f, 0.86f, 0.62f); S->CoronaColor = FLinearColor(1.0f, 0.72f, 0.4f); S->Intensity = 80.f;
		break;
	}
}

AStarSystem* AStarSystem::Generate(UWorld* World, const FStarSystemSpec& Spec, const FVector& HomeStarLocation)
{
	if (!World) { return nullptr; }
	const FVector Loc = HomeStarLocation + DirectionFromSky(Spec.RightAscensionHours, Spec.DeclinationDeg) * ((double)Spec.DistanceLy * LightYearCm);
	FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AStarSystem* Sys = World->SpawnActor<AStarSystem>(AStarSystem::StaticClass(), Loc, FRotator::ZeroRotator, SP);
	if (!Sys) { return nullptr; }
	Sys->SystemName = Spec.Name;
	Sys->Seed = Spec.Seed;
	Sys->StarClass = Spec.StarClass;
	FRandomStream Rng(Spec.Seed);

	// The star. Spawned deferred so the class is applied before its appearance pass.
	AStarBody* S = World->SpawnActorDeferred<AStarBody>(AStarBody::StaticClass(), FTransform(Loc), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!S) { return Sys; }
	S->StarName = Spec.Name;
	Sys->ApplyStarClass(S, Rng);
	S->FinishSpawning(FTransform(Loc));
	Sys->Star = S;
	Sys->Adopt(S);

	// Planets: a handful of orbits spaced geometrically outward from a few tenths of an AU, scaled by the star's
	// luminosity so the temperate band sits where a world can keep water. Kinds by temperature band.
	const double LumScale = Sys->StarClass == EStarClass::M ? 0.25 : Sys->StarClass == EStarClass::K ? 0.6 : Sys->StarClass == EStarClass::F ? 1.4 : Sys->StarClass == EStarClass::A ? 2.6 : 1.0;
	const int32 Count = Rng.RandRange(4, 7);
	double OrbitAu = LumScale * Rng.FRandRange(0.3, 0.45);
	static const TCHAR* Numerals[] = { TEXT("I"), TEXT("II"), TEXT("III"), TEXT("IV"), TEXT("V"), TEXT("VI"), TEXT("VII"), TEXT("VIII") };
	const double Tilt = FMath::DegreesToRadians(Rng.FRandRange(-4.0, 4.0));
	for (int32 i = 0; i < Count; ++i)
	{
		const double Band = OrbitAu / LumScale;       // 1 = Earth-equivalent insolation
		EPlanetArchetype Kind; float RadiusKm;
		const float T = Rng.FRand();
		if (Band < 0.6)
		{
			Kind = T < 0.3f ? EPlanetArchetype::Lava : T < 0.65f ? EPlanetArchetype::Venus : T < 0.85f ? EPlanetArchetype::Desert : EPlanetArchetype::Mars;
		}
		else if (Band < 1.8)
		{
			Kind = T < 0.35f ? EPlanetArchetype::Terrestrial : T < 0.6f ? EPlanetArchetype::Ocean : T < 0.8f ? EPlanetArchetype::Desert : EPlanetArchetype::Mars;
		}
		else
		{
			Kind = T < 0.45f ? EPlanetArchetype::GasGiant : T < 0.7f ? EPlanetArchetype::IceGiant : EPlanetArchetype::Ice;
		}
		switch (Kind)
		{
		case EPlanetArchetype::GasGiant: RadiusKm = Rng.FRandRange(40000.f, 70000.f); break;
		case EPlanetArchetype::IceGiant: RadiusKm = Rng.FRandRange(20000.f, 28000.f); break;
		case EPlanetArchetype::Lava: RadiusKm = Rng.FRandRange(4500.f, 7500.f); break;
		case EPlanetArchetype::Mars: RadiusKm = Rng.FRandRange(3000.f, 4500.f); break;
		case EPlanetArchetype::Ice: RadiusKm = Rng.FRandRange(3500.f, 6000.f); break;
		default: RadiusKm = Rng.FRandRange(5200.f, 7200.f); break;
		}
		const double A = Rng.FRandRange(0.0, 2.0 * PI);
		const double R = OrbitAu * AuCm;
		const FVector PLoc = Loc + FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, FMath::Sin(A + 1.0) * R * FMath::Sin(Tilt));
		APlanetBody* P = World->SpawnActorDeferred<APlanetBody>(APlanetBody::StaticClass(), FTransform(PLoc), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (P)
		{
			P->Archetype = Kind; P->RadiusKm = RadiusKm;
			P->PlanetName = FString::Printf(TEXT("%s %s"), *Spec.Name, Numerals[FMath::Min(i, 7)]);
			P->SeedLocation = PLoc; P->bSeedLocationSet = true;
			P->FinishSpawning(FTransform(PLoc));
			Sys->Adopt(P);
			Sys->Planets.Add(P);
		}
		OrbitAu *= Rng.FRandRange(1.55, 2.0);
	}

	// A belt in the first gap past the temperate band, two systems in three.
	if (Rng.FRand() < 0.67 && Sys->Planets.Num() >= 3)
	{
		double BeltAu = 0.0;
		for (int32 i = 1; i < Sys->Planets.Num(); ++i)
		{
			const double R0 = FVector::Dist(Sys->Planets[i - 1]->GetActorLocation(), Loc) / AuCm;
			const double R1 = FVector::Dist(Sys->Planets[i]->GetActorLocation(), Loc) / AuCm;
			if (R0 / LumScale > 1.2) { BeltAu = FMath::Sqrt(R0 * R1); break; }
		}
		if (BeltAu > 0.0)
		{
			ABeltField* Belt = World->SpawnActorDeferred<ABeltField>(ABeltField::StaticClass(), FTransform(Loc), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (Belt)
			{
				Belt->BeltRadiusCm = BeltAu * AuCm;
				Belt->Seed = Spec.Seed * 7 + 3;
				Belt->Star = S;
				Belt->FinishSpawning(FTransform(Loc));
				Sys->Adopt(Belt);
			}
		}
	}

	// A station with its patrol ships at each planet, from the level's traffic spawner.
	TActorIterator<ASpaceTraffic> TrafficIt(World);
	if (TrafficIt)
	{
		TrafficIt->PopulatePlanets(Sys->Planets, Spec.Seed * 13 + 5);
	}
	Sys->AdoptLoose();
	UE_LOG(LogTemp, Log, TEXT("Star system %s: class %d star, %d planets, %.2f ly at RA %.1fh Dec %.0f"), *Spec.Name, (int32)Spec.StarClass, Sys->Planets.Num(), Spec.DistanceLy, Spec.RightAscensionHours, Spec.DeclinationDeg);
	return Sys;
}
