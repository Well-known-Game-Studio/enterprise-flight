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
	Sys->NameUnnamedWorlds();
	UE_LOG(LogTemp, Log, TEXT("Star system %s (home): star %s, %d planets"), *Name, HomeStar ? *HomeStar->StarName : TEXT("none"), Sys->Planets.Num());
	return Sys;
}

void AStarSystem::ApplyStarClass(AStarBody* S, EStarClass Class, FRandomStream& Rng)
{
	switch (Class)
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
	case EStarClass::WhiteDwarf:
		// Earth-sized and blazing: a point of blue-white light with a tight corona.
		S->RadiusKm = Rng.FRandRange(5500.f, 7000.f);
		S->StarColor = FLinearColor(0.8f, 0.88f, 1.0f); S->CoronaColor = FLinearColor(0.6f, 0.75f, 1.0f); S->Intensity = 140.f;
		S->CoronaDepth = 1.2f;
		break;
	default:
		S->RadiusKm = Rng.FRandRange(650000.f, 750000.f);
		S->StarColor = FLinearColor(1.0f, 0.86f, 0.62f); S->CoronaColor = FLinearColor(1.0f, 0.72f, 0.4f); S->Intensity = 80.f;
		break;
	}
}

double AStarSystem::LuminosityScale(EStarClass Class)
{
	// Orbit scale of the temperate band relative to the Sun's: sqrt of the luminosity, roughly.
	switch (Class)
	{
	case EStarClass::M: return 0.25;
	case EStarClass::K: return 0.6;
	case EStarClass::F: return 1.4;
	case EStarClass::A: return 2.6;
	case EStarClass::WhiteDwarf: return 0.1;
	default: return 1.0;
	}
}

void AStarSystem::SpawnPlanets(UWorld* World, FRandomStream& Rng, const FVector& Centre, const FString& Prefix, EStarClass Class, int32 Count, double MaxAu, TArray<APlanetBody*>& Out)
{
	// Orbits spaced geometrically outward from a few tenths of an AU, scaled by the star's luminosity so the
	// temperate band sits where a world can keep water. Kinds by temperature band.
	const double LumScale = LuminosityScale(Class);
	double OrbitAu = LumScale * Rng.FRandRange(0.3, 0.45);
	const double Tilt = FMath::DegreesToRadians(Rng.FRandRange(-4.0, 4.0));
	for (int32 i = 0; i < Count && OrbitAu <= MaxAu; ++i)
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
		const FVector PLoc = Centre + FVector(FMath::Cos(A) * R, FMath::Sin(A) * R, FMath::Sin(A + 1.0) * R * FMath::Sin(Tilt));
		APlanetBody* P = World->SpawnActorDeferred<APlanetBody>(APlanetBody::StaticClass(), FTransform(PLoc), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (P)
		{
			P->Archetype = Kind; P->RadiusKm = RadiusKm;
			P->PlanetName = FString::Printf(TEXT("%s %s"), *Prefix, Numeral(i));
			P->SeedLocation = PLoc; P->bSeedLocationSet = true;
			P->FinishSpawning(FTransform(PLoc));
			Adopt(P);
			Planets.Add(P);
			Out.Add(P);
		}
		OrbitAu *= Rng.FRandRange(1.55, 2.0);
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
	ApplyStarClass(S, Spec.StarClass, Rng);
	S->FinishSpawning(FTransform(Loc));
	Sys->Star = S;
	Sys->Adopt(S);

	// A companion star, in the orbital plane at the given separation.
	const bool bBinary = Spec.CompanionClass != EStarClass::None && Spec.CompanionSeparationAU > 0.f;
	FVector CLoc = Loc;
	if (bBinary)
	{
		const double Ang = Rng.FRandRange(0.0, 2.0 * PI);
		CLoc = Loc + FVector(FMath::Cos(Ang), FMath::Sin(Ang), 0.0) * ((double)Spec.CompanionSeparationAU * AuCm);
		AStarBody* C = World->SpawnActorDeferred<AStarBody>(AStarBody::StaticClass(), FTransform(CLoc), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (C)
		{
			C->StarName = Spec.Name + TEXT(" B");
			ApplyStarClass(C, Spec.CompanionClass, Rng);
			C->FinishSpawning(FTransform(CLoc));
			Sys->Companion = C;
			Sys->Adopt(C);
		}
	}

	// Planets: the primary's inside a third of the separation (a binary's stable zone), then a few of the
	// companion's own.
	TArray<APlanetBody*> Primary, Secondary;
	const double MaxPrimaryAu = bBinary ? (double)Spec.CompanionSeparationAU * 0.3 : 1.0e9;
	Sys->SpawnPlanets(World, Rng, Loc, Spec.Name, Spec.StarClass, Rng.RandRange(4, 7), MaxPrimaryAu, Primary);
	if (bBinary && Sys->Companion)
	{
		Sys->SpawnPlanets(World, Rng, CLoc, Spec.Name + TEXT(" B"), Spec.CompanionClass, Rng.RandRange(1, 3), (double)Spec.CompanionSeparationAU * 0.3, Secondary);
	}

	// A belt in the first gap past the temperate band, two systems in three.
	if (Rng.FRand() < 0.67 && Primary.Num() >= 3)
	{
		double BeltAu = 0.0;
		const double LumScale = LuminosityScale(Spec.StarClass);
		for (int32 i = 1; i < Primary.Num(); ++i)
		{
			const double R0 = FVector::Dist(Primary[i - 1]->GetActorLocation(), Loc) / AuCm;
			const double R1 = FVector::Dist(Primary[i]->GetActorLocation(), Loc) / AuCm;
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
	UE_LOG(LogTemp, Log, TEXT("Star system %s: class %d star%s, %d planets (%d of the companion), %.2f ly at RA %.1fh Dec %.0f"), *Spec.Name, (int32)Spec.StarClass, bBinary ? TEXT(" with a companion") : TEXT(""), Sys->Planets.Num(), Secondary.Num(), Spec.DistanceLy, Spec.RightAscensionHours, Spec.DeclinationDeg);
	return Sys;
}

// ---------------------------------------------------------------------------------------------------------------------
// Names

const TCHAR* AStarSystem::Numeral(int32 Index)
{
	static const TCHAR* Numerals[] = { TEXT("I"), TEXT("II"), TEXT("III"), TEXT("IV"), TEXT("V"), TEXT("VI"), TEXT("VII"), TEXT("VIII"), TEXT("IX"), TEXT("X"), TEXT("XI"), TEXT("XII") };
	return Numerals[FMath::Clamp(Index, 0, (int32)UE_ARRAY_COUNT(Numerals) - 1)];
}

FString AStarSystem::GenerateWorldName(FRandomStream& Rng, TSet<FString>& Used)
{
	// Two or three syllables of the kind Starfleet charts are full of (Vulcan, Risa, Cestus, Talos, Ceti, Andoria),
	// with a Prime now and then.
	static const TCHAR* Onsets[] = { TEXT(""), TEXT("K"), TEXT("T"), TEXT("V"), TEXT("R"), TEXT("S"), TEXT("Th"), TEXT("D"), TEXT("M"), TEXT("N"), TEXT("B"), TEXT("Z"), TEXT("L"), TEXT("G"), TEXT("Sh"), TEXT("Tr"), TEXT("Kh"), TEXT("Ph"), TEXT("C") };
	static const TCHAR* Vowels[] = { TEXT("a"), TEXT("e"), TEXT("i"), TEXT("o"), TEXT("u"), TEXT("a"), TEXT("e"), TEXT("ai"), TEXT("ia"), TEXT("eo") };
	static const TCHAR* Plain[] = { TEXT("a"), TEXT("e"), TEXT("i"), TEXT("o"), TEXT("u") };
	// Syllable soup throws up the odd word nobody wants on a star chart.
	static const TCHAR* Banned[] = { TEXT("phal"), TEXT("fuk"), TEXT("fuc"), TEXT("cum"), TEXT("shit"), TEXT("ass"), TEXT("tit"), TEXT("coc"), TEXT("dic"), TEXT("pen"), TEXT("sex"), TEXT("vag"), TEXT("nig"), TEXT("kik"), TEXT("rap") };
	static const TCHAR* Mids[] = { TEXT("r"), TEXT("n"), TEXT("l"), TEXT("th"), TEXT("s"), TEXT("x"), TEXT("m"), TEXT("k"), TEXT("nd"), TEXT("rr"), TEXT("v"), TEXT("d"), TEXT("z"), TEXT("st"), TEXT("ll") };
	static const TCHAR* Ends[] = { TEXT("a"), TEXT("is"), TEXT("os"), TEXT("ar"), TEXT("on"), TEXT("ia"), TEXT("us"), TEXT("a"), TEXT("en"), TEXT("ir"), TEXT("or"), TEXT("ax"), TEXT("eth") };
	auto Pick = [&Rng](const TCHAR* const* Table, int32 N) { return Table[Rng.RandRange(0, N - 1)]; };
	for (int32 Attempt = 0; Attempt < 64; ++Attempt)
	{
		FString Name = Pick(Onsets, UE_ARRAY_COUNT(Onsets));
		Name += Pick(Vowels, UE_ARRAY_COUNT(Vowels));
		Name += Pick(Mids, UE_ARRAY_COUNT(Mids));
		if (Rng.FRand() < 0.45f)
		{
			Name += Pick(Plain, UE_ARRAY_COUNT(Plain));   // one digraph per name at most
			Name += Pick(Mids, UE_ARRAY_COUNT(Mids));
		}
		Name += Pick(Ends, UE_ARRAY_COUNT(Ends));
		if (Name.Len() < 4 || Name.Len() > 10) { continue; }
		bool bBanned = false;
		for (const TCHAR* B : Banned) { if (Name.ToLower().Contains(B)) { bBanned = true; break; } }
		if (bBanned) { continue; }
		Name[0] = FChar::ToUpper(Name[0]);
		if (Rng.FRand() < 0.15f) { Name += TEXT(" Prime"); }
		if (!Used.Contains(Name)) { Used.Add(Name); return Name; }
	}
	FString Fallback = FString::Printf(TEXT("World %d"), Used.Num() + 1);
	Used.Add(Fallback);
	return Fallback;
}

APlanetBody* AStarSystem::PrimaryOf(UWorld* World, const APlanetBody* Moon)
{
	APlanetBody* Primary = nullptr;
	double Best = TNumericLimits<double>::Max();
	for (TActorIterator<APlanetBody> It(World); It; ++It)
	{
		if (*It == Moon || It->IsMoon()) { continue; }
		const double D = FVector::Dist(It->GetActorLocation(), Moon->GetActorLocation());
		if (D < Best && D < 400.0 * It->GetRadiusCm()) { Best = D; Primary = *It; }
	}
	return Primary;
}

FString AStarSystem::MoonDesignation(UWorld* World, const APlanetBody* Primary)
{
	const FString Base = Primary->GetDisplayName() + TEXT(" ");
	TSet<FString> Taken;
	for (TActorIterator<APlanetBody> It(World); It; ++It)
	{
		if (It->IsMoon() && It->PlanetName.StartsWith(Base)) { Taken.Add(It->PlanetName); }
	}
	for (int32 i = 0; i < 12; ++i)
	{
		const FString Name = Base + Numeral(i);
		if (!Taken.Contains(Name)) { return Name; }
	}
	return Base + TEXT("XIII");
}

void AStarSystem::NameUnnamedWorlds()
{
	UWorld* World = GetWorld();
	if (!World) { return; }
	// Planets first, in orbit order, from a stream seeded by the system so the names hold between sessions
	// (the science log keys on them).
	TArray<APlanetBody*> Unnamed;
	TSet<FString> Used;
	for (APlanetBody* P : Planets)
	{
		if (!P || P->IsMoon()) { continue; }
		if (P->PlanetName.IsEmpty()) { Unnamed.Add(P); } else { Used.Add(P->PlanetName); }
	}
	const FVector Centre = GetStarLocation();
	Unnamed.Sort([&Centre](const APlanetBody& A, const APlanetBody& B) { return FVector::DistSquared(A.GetActorLocation(), Centre) < FVector::DistSquared(B.GetActorLocation(), Centre); });
	FRandomStream Rng((int32)(FCrc::StrCrc32(*SystemName) ^ (uint32)Seed));
	for (APlanetBody* P : Unnamed)
	{
		P->PlanetName = GenerateWorldName(Rng, Used);
		UE_LOG(LogTemp, Log, TEXT("Star system %s: %s is named %s"), *SystemName, *P->GetActorNameOrLabel(), *P->PlanetName);
	}
	// Then the moons, by distance from their primary.
	TArray<APlanetBody*> Moons;
	for (APlanetBody* P : Planets) { if (P && P->IsMoon() && P->PlanetName.IsEmpty()) { Moons.Add(P); } }
	Moons.Sort([](const APlanetBody& A, const APlanetBody& B)
	{
		const APlanetBody* PA = PrimaryOf(A.GetWorld(), &A); const APlanetBody* PB = PrimaryOf(B.GetWorld(), &B);
		const double DA = PA ? FVector::Dist(PA->GetActorLocation(), A.GetActorLocation()) : 0.0;
		const double DB = PB ? FVector::Dist(PB->GetActorLocation(), B.GetActorLocation()) : 0.0;
		return DA < DB;
	});
	for (APlanetBody* M : Moons)
	{
		if (const APlanetBody* Primary = PrimaryOf(World, M))
		{
			M->PlanetName = MoonDesignation(World, Primary);
			UE_LOG(LogTemp, Log, TEXT("Star system %s: %s is named %s"), *SystemName, *M->GetActorNameOrLabel(), *M->PlanetName);
		}
	}
}
