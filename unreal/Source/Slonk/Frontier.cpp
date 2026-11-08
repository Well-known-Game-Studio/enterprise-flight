// Slonk - the frontier: the settlements founded by the expansion missions, replayed into the world and grown.

#include "Frontier.h"
#include "Exploration.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "PlanetBody.h"
#include "StarSystem.h"
#include "SpaceTraffic.h"
#include "SpaceVessel.h"
#include "CrewRoster.h"

const TCHAR* UFrontierSubsystem::SlotName = TEXT("Frontier");

void UFrontierSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Load();
}

UFrontierSubsystem* UFrontierSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UFrontierSubsystem>() : nullptr;
}

void UFrontierSubsystem::InitForTest(UMissionSubsystem* InMissions, UExplorationSubsystem* InExploration)
{
	bNoDisk = true; MissionsOverride = InMissions; ExplorationOverride = InExploration;
	Settlements.Reset(); NextStarbaseNumber = 400;
}

FString UFrontierSubsystem::KindName(ESettlementKind K)
{
	static const TCHAR* N[] = { TEXT("Outpost"), TEXT("Colony"), TEXT("Starbase") };
	return (int32)K < (int32)ESettlementKind::Count ? N[(int32)K] : TEXT("-");
}

bool UFrontierSubsystem::ParseKind(const FString& Word, ESettlementKind& Out)
{
	const FString W = Word.ToLower();
	for (int32 i = 0; i < (int32)ESettlementKind::Count; ++i) { if (W.Len() >= 3 && KindName((ESettlementKind)i).ToLower().StartsWith(W)) { Out = (ESettlementKind)i; return true; } }
	return false;
}

FString UFrontierSubsystem::ColonyName(FRandomStream& R)
{
	static const TCHAR* First[] = { TEXT("New"), TEXT("Port"), TEXT("Fort"), TEXT("New"), TEXT("Haven") };
	TSet<FString> Used;
	FString Root = AStarSystem::GenerateWorldName(R, Used);
	Root.RemoveFromEnd(TEXT(" Prime"));
	const TCHAR* F = First[R.RandRange(0, UE_ARRAY_COUNT(First) - 1)];
	return FString(F) == TEXT("Haven") ? Root + TEXT(" Haven") : FString::Printf(TEXT("%s %s"), F, *Root);
}

float UFrontierSubsystem::LightsFor(double Population)
{
	// Eight hundred people are a glow; a hundred thousand light the night side like a world of their own.
	if (Population <= 0.0) { return 0.f; }
	return FMath::Clamp(1.f + 2.f * (float)FMath::LogX(10.0, FMath::Max(Population, 1.0) / 400.0), 0.6f, 6.f);
}

// ---- lifetime ---------------------------------------------------------------------------------

void UFrontierSubsystem::Load()
{
	Settlements.Reset(); NextStarbaseNumber = 400;
	if (UGameplayStatics::DoesSaveGameExist(SlotName, 0))
	{
		if (const USlonkFrontierSaveGame* S = Cast<USlonkFrontierSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, 0)))
		{
			Settlements = S->Settlements; NextStarbaseNumber = S->NextStarbaseNumber;
		}
	}
	UE_LOG(LogTemp, Log, TEXT("Frontier: %d settlements"), Settlements.Num());
	++Revision;
}

void UFrontierSubsystem::Save()
{
	++Revision;
	if (bNoDisk) { return; }
	USlonkFrontierSaveGame* S = Cast<USlonkFrontierSaveGame>(UGameplayStatics::CreateSaveGameObject(USlonkFrontierSaveGame::StaticClass()));
	if (!S) { return; }
	S->Settlements = Settlements; S->NextStarbaseNumber = NextStarbaseNumber;
	UGameplayStatics::SaveGameToSlot(S, SlotName, 0);
}

void UFrontierSubsystem::Reset()
{
	// The actors stay until the next session; the record is blank now.
	Settlements.Reset(); NextStarbaseNumber = 400;
	Save();
}

UMissionSubsystem* UFrontierSubsystem::Missions() const
{
	if (MissionsOverride.IsValid()) { return MissionsOverride.Get(); }
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UMissionSubsystem>() : nullptr;
}

UExplorationSubsystem* UFrontierSubsystem::Exploration() const
{
	if (ExplorationOverride.IsValid()) { return ExplorationOverride.Get(); }
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UExplorationSubsystem>() : nullptr;
}

void UFrontierSubsystem::Report(const FString& Text, const FLinearColor& Color, ECrewDepartment From)
{
	FCrewReport R; R.From = From; R.Text = Text; R.Color = Color;
	Reports.Add(R);
	UE_LOG(LogTemp, Log, TEXT("Frontier: %s"), *Text);
}

// ---- queries ----------------------------------------------------------------------------------

const FSettlement* UFrontierSubsystem::Find(const FString& SystemName, ESettlementKind Kind) const
{
	for (const FSettlement& S : Settlements) { if (S.Kind == Kind && S.System == SystemName) { return &S; } }
	return nullptr;
}

FSettlement* UFrontierSubsystem::FindMutable(const FString& SystemName, ESettlementKind Kind)
{
	for (FSettlement& S : Settlements) { if (S.Kind == Kind && S.System == SystemName) { return &S; } }
	return nullptr;
}

const FSettlement* UFrontierSubsystem::AtWorld(const FString& World) const
{
	for (const FSettlement& S : Settlements) { if (S.Kind == ESettlementKind::Colony && S.World == World) { return &S; } }
	return nullptr;
}

TArray<const FSettlement*> UFrontierSubsystem::InSystem(const FString& SystemName) const
{
	TArray<const FSettlement*> Out;
	for (const FSettlement& S : Settlements) { if (S.System == SystemName) { Out.Add(&S); } }
	return Out;
}

TArray<FString> UFrontierSubsystem::ColonyWorlds() const
{
	TArray<FString> Out;
	for (const FSettlement& S : Settlements) { if (S.Kind == ESettlementKind::Colony && S.Population > 0.0) { Out.Add(S.World); } }
	return Out;
}

double UFrontierSubsystem::WorksHoursLeft(const FString& SystemName) const
{
	const FSettlement* S = Find(SystemName, ESettlementKind::Starbase);
	return S && !S->bCommissioned ? S->WorksHoursLeft : 0.0;
}

// ---- the rules --------------------------------------------------------------------------------

bool UFrontierSubsystem::CanFound(ESettlementKind Kind, const FString& SystemName, FString& OutWorld, FString& OutWhy, bool bOrdered) const
{
	OutWorld.Empty();
	const UExplorationSubsystem* X = Exploration();
	const FCatalogueEntry* E = X ? X->Find(SystemName) : nullptr;
	if (!E) { OutWhy = FString::Printf(TEXT("%s is not in the catalogue; the Federation founds in new space"), *SystemName); return false; }
	if (E->State == EChartState::Unknown) { OutWhy = FString::Printf(TEXT("%s has not been charted"), *E->Name()); return false; }
	const FString Name = E->Name();
	if (Find(Name, Kind)) { OutWhy = FString::Printf(TEXT("%s already has %s"), *Name, Kind == ESettlementKind::Outpost ? TEXT("an outpost") : Kind == ESettlementKind::Colony ? TEXT("a colony") : TEXT("a starbase")); return false; }
	switch (Kind)
	{
	case ESettlementKind::Outpost:
		// Off the world a colony would take, never the pre-warp one (the Prime Directive: nothing of ours in their sky).
		for (const FString& W : E->HabitableWorlds) { if (!E->IsLit(W)) { OutWorld = W; break; } }
		if (OutWorld.IsEmpty()) { for (const FString& W : E->SolidWorlds) { if (!E->IsLit(W)) { OutWorld = W; break; } } }
		if (OutWorld.IsEmpty()) { OutWhy = FString::Printf(TEXT("%s has no solid world to put it off%s"), *Name, E->PreWarpWorld.IsEmpty() ? TEXT("") : TEXT(" that is not theirs")); return false; }
		return true;
	case ESettlementKind::Colony:
	{
		if (!Find(Name, ESettlementKind::Outpost) && !Find(Name, ESettlementKind::Starbase)) { OutWhy = FString::Printf(TEXT("%s has no outpost yet; the settlers need somewhere to land to"), *Name); return false; }
		for (const FString& W : E->HabitableWorlds) { if (!E->IsLit(W)) { OutWorld = W; break; } }
		if (OutWorld.IsEmpty()) { OutWhy = FString::Printf(TEXT("%s has no habitable world%s"), *Name, E->HabitableWorlds.Num() > 0 ? TEXT(" that is not already someone's") : TEXT("")); return false; }
		return true;
	}
	case ESettlementKind::Starbase:
	{
		if (!Find(Name, ESettlementKind::Outpost) && !Find(Name, ESettlementKind::Colony)) { OutWhy = FString::Printf(TEXT("%s has no outpost or colony to build from"), *Name); return false; }
		const UMissionSubsystem* M = Missions();
		if (!bOrdered && M && M->GetStanding() < StarbaseStandingNeeded) { OutWhy = FString::Printf(TEXT("Starfleet builds its bases where its best captains have been: regard %d, %d needed"), M->GetStanding(), StarbaseStandingNeeded); return false; }
		if (const FSettlement* O = Find(Name, ESettlementKind::Outpost)) { OutWorld = O->World; } else if (const FSettlement* C = Find(Name, ESettlementKind::Colony)) { OutWorld = C->World; }
		return true;
	}
	default: return false;
	}
}

const FCatalogueEntry* UFrontierSubsystem::PickSystem(ESettlementKind Kind, FRandomStream& R, FString& OutWorld, const TSet<FString>* ExcludeSystems) const
{
	const UExplorationSubsystem* X = Exploration();
	if (!X) { return nullptr; }
	TArray<const FCatalogueEntry*> Can;
	TArray<FString> Worlds;
	for (const FCatalogueEntry& E : X->GetCatalogue())
	{
		FString W, Why;
		if (ExcludeSystems && ExcludeSystems->Contains(E.Name())) { continue; }   // an order for it is out already
		if (CanFound(Kind, E.Designation, W, Why)) { Can.Add(&E); Worlds.Add(W); }
	}
	if (Can.Num() == 0) { return nullptr; }
	const int32 i = R.RandRange(0, Can.Num() - 1);
	OutWorld = Worlds[i];
	return Can[i];
}

// ---- founding ---------------------------------------------------------------------------------

const FSettlement* UFrontierSubsystem::Found(ESettlementKind Kind, const FString& SystemName, const FString& World, double People, bool bOrdered)
{
	FString Pick, Why;
	if (!CanFound(Kind, SystemName, Pick, Why, bOrdered)) { UE_LOG(LogTemp, Warning, TEXT("Frontier: cannot found %s at %s: %s"), *KindName(Kind), *SystemName, *Why); return nullptr; }
	const UExplorationSubsystem* X = Exploration();
	const FCatalogueEntry* E = X->Find(SystemName);
	FSettlement S;
	S.Kind = Kind; S.System = E->Name(); S.World = World.IsEmpty() ? Pick : World;
	S.FoundedStardate = Missions() ? Missions()->Stardate() : 0.0;
	S.Seed = GetTypeHash(S.System) ^ (int32)Kind * 7919 ^ Settlements.Num() * 31;
	FRandomStream R(S.Seed);
	switch (Kind)
	{
	case ESettlementKind::Outpost:
		S.Name = S.World + TEXT(" Outpost");
		Report(FString::Printf(TEXT("%s is raised, Captain: the Federation's first station in %s. The module's crew wave from the dome."), *S.Name, *S.System), FLinearColor(0.6f, 1.f, 0.6f));
		break;
	case ESettlementKind::Colony:
		S.Name = ColonyName(R); S.Population = People > 0.0 ? People : ColonyFirstPeople;
		Report(FString::Printf(TEXT("%s is founded on %s: %.0f people and a flag, and the lights on their first night."), *S.Name, *S.World, S.Population), FLinearColor(0.6f, 1.f, 0.6f));
		break;
	case ESettlementKind::Starbase:
		S.Name = FString::Printf(TEXT("Starbase %d"), NextStarbaseNumber++);
		S.WorksHoursLeft = StarbaseWorksHours; S.bCommissioned = false;
		Report(FString::Printf(TEXT("The keel of %s is laid in %s. Three days of ship's time to the commissioning, if nothing troubles the works."), *S.Name, *S.System), FLinearColor(0.6f, 1.f, 0.6f));
		break;
	default: return nullptr;
	}
	Settlements.Add(S);
	if (UWorld* W = GetWorld())
	{
		if (S.IsStation()) { SpawnStation(W, S); }
		else { LightWorld(W, S); }
	}
	if (UCrewRosterSubsystem* Ro = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCrewRosterSubsystem>() : nullptr)
	{
		FCrewExperience Xp; Xp.Tag = TEXT("Discovery"); Xp.Intensity = 0.5f; Xp.Place = S.System; Xp.Text = FString::Printf(TEXT("%s founded"), *S.Name);
		Ro->Experience(Xp);
	}
	Save();
	return &Settlements.Last();
}

bool UFrontierSubsystem::Commission(const FString& SystemName, FString& OutWhy)
{
	FSettlement* S = FindMutable(SystemName, ESettlementKind::Starbase);
	if (!S) { OutWhy = FString::Printf(TEXT("No starbase under way at %s"), *SystemName); return false; }
	if (S->bCommissioned) { OutWhy = FString::Printf(TEXT("%s is already commissioned"), *S->Name); return false; }
	if (S->WorksHoursLeft > 0.0) { OutWhy = FString::Printf(TEXT("The works on %s need another %.0f hours"), *S->Name, FMath::Max(S->WorksHoursLeft, 1.0)); return false; }
	S->bCommissioned = true;
	Report(FString::Printf(TEXT("%s is commissioned, Captain. Her patrol ships are out and the fleet has a home in %s."), *S->Name, *S->System), FLinearColor(0.6f, 1.f, 0.6f));
	if (UWorld* W = GetWorld())
	{
		// Her patrol ships: the station is in the sky already; two hulls about her.
		TActorIterator<ASpaceTraffic> It(W);
		ASpaceVessel* Station = nullptr;
		for (TActorIterator<ASpaceVessel> V(W); V; ++V) { if (V->GetDisplayName() == S->Name) { Station = *V; break; } }
		if (It && Station) { It->SpawnPatrols(Station->GetActorLocation(), Station->GetBoundingRadiusCm(), 2, S->Seed + 5); }
	}
	Save();
	return true;
}

void UFrontierSubsystem::Strike(const FString& World, bool bEvacuated, FString& OutText)
{
	for (FSettlement& S : Settlements)
	{
		if (S.Kind != ESettlementKind::Colony || S.World != World) { continue; }
		const double Before = S.Population;
		const double Dead = FMath::Min(Before, (Before < 5000.0 ? Before : Before * 0.7) * (bEvacuated ? 0.5 : 1.0));
		S.Population = FMath::Max(0.0, Before - Dead);
		S.Scar = bEvacuated ? 0.5f : 0.25f; S.RecoverPeople = Before;   // the lights that are left, dimmed, until the colony has grown back to what it was
		OutText = S.Population <= 0.0 ? FString::Printf(TEXT("%s is gone: %.0f people."), *S.Name, Dead) : FString::Printf(TEXT("%s: %.0f dead of %.0f%s."), *S.Name, Dead, Before, bEvacuated ? TEXT(", the evacuation having got the rest clear") : TEXT(""));
		Report(OutText, FLinearColor(1.f, 0.5f, 0.4f), ECrewDepartment::Medical);
		if (UWorld* W = GetWorld()) { LightWorld(W, S); }
		Save();
		return;
	}
	OutText = FString::Printf(TEXT("%s is struck; the world will carry the scar for an age."), *World);
}

void UFrontierSubsystem::RenameSystem(const FString& OldName, const FString& NewName)
{
	bool bAny = false;
	for (FSettlement& S : Settlements)
	{
		if (S.System != OldName) { continue; }
		S.System = NewName; bAny = true;
		if (S.World.StartsWith(OldName + TEXT(" "))) { S.World = NewName + S.World.RightChop(OldName.Len()); }
		if (S.Name.StartsWith(OldName + TEXT(" ")))
		{
			// The station in the sky carries the name too (the missions' targets follow through the mission system's own rename).
			const FString Was = S.Name;
			S.Name = NewName + S.Name.RightChop(OldName.Len());
			if (UWorld* W = GetWorld()) { for (TActorIterator<ASpaceVessel> V(W); V; ++V) { if (V->VesselName == Was) { V->VesselName = S.Name; } } }
			if (float* L = LightsApplied.Find(Was)) { LightsApplied.Add(S.Name, *L); LightsApplied.Remove(Was); }
		}
	}
	if (bAny) { Save(); }
}

// ---- the world --------------------------------------------------------------------------------

void UFrontierSubsystem::SpawnSettlements(UWorld* World)
{
	if (!World) { return; }
	for (const FSettlement& S : Settlements)
	{
		if (S.IsStation()) { SpawnStation(World, S); } else { LightWorld(World, S); }
	}
	if (Settlements.Num() > 0) { UE_LOG(LogTemp, Log, TEXT("Frontier: %d settlements in the world"), Settlements.Num()); }
}

void UFrontierSubsystem::SpawnStation(UWorld* World, const FSettlement& S)
{
	TActorIterator<ASpaceTraffic> It(World);
	if (!It) { return; }
	for (TActorIterator<ASpaceVessel> V(World); V; ++V) { if (V->GetDisplayName() == S.Name) { return; } }   // already in
	FVector Centre; double R = 0.0;
	APlanetBody* P = nullptr;
	for (TActorIterator<APlanetBody> B(World); B; ++B) { if (B->GetDisplayName() == S.World) { P = *B; break; } }
	if (P) { Centre = P->GetActorLocation(); R = P->GetRadiusCm() * 4.0; }
	else if (!AStarSystem::FindNamedLocation(World, S.System, Centre)) { return; }
	else { R = AStarSystem::AuCm * 0.5; }
	FRandomStream Rng(S.Seed);
	const double A = Rng.FRandRange(0.0, 2.0 * PI);
	const FVector Loc = Centre + FVector(FMath::Cos(A), FMath::Sin(A), 0.0) * R;
	const EStationStyle Style = S.Kind == ESettlementKind::Starbase ? EStationStyle::Spacedock : EStationStyle::K7;
	It->SpawnStation(S.Name, Style, Loc, S.Kind == ESettlementKind::Starbase && S.bCommissioned ? 2 : 0, S.Seed);
	if (AStarSystem* Sys = AStarSystem::Nearest(World, Loc)) { Sys->AdoptLoose(); }
}

void UFrontierSubsystem::LightWorld(UWorld* World, const FSettlement& S)
{
	const float L = LightsFor(S.Population) * S.Scar;
	LightsApplied.Add(S.Name, L);
	for (TActorIterator<APlanetBody> B(World); B; ++B)
	{
		if (B->GetDisplayName() != S.World) { continue; }
		B->SetNightLights(L);
		return;
	}
}

void UFrontierSubsystem::AdvanceSeconds(float RealSeconds, const FMissionShipState& Ship)
{
	Last = Ship;
	const UMissionSubsystem* M = Missions();
	if (!M) { return; }
	const double H = M->Hours();
	if (LastHours < 0.0 || H < LastHours) { LastHours = H; return; }
	const double Dt = H - LastHours;
	if (Dt < 0.05) { return; }
	LastHours = H;
	bool bChanged = false;
	for (FSettlement& S : Settlements)
	{
		if (S.Kind == ESettlementKind::Colony && S.Population > 0.0)
		{
			// A colony grows one in a hundred a day: a world in a few years.
			S.Population *= FMath::Pow(1.01, Dt / 24.0);
			// The lights follow the people once they have changed enough to see, against the lights last set (a step
			// at a time would never add up); a scar fades as the colony grows back past what it lost.
			if (S.Scar < 1.f && S.Population >= S.RecoverPeople) { S.Scar = 1.f; S.RecoverPeople = 0.0; Report(FString::Printf(TEXT("%s has grown back to what it was before the strike."), *S.Name), FLinearColor(0.6f, 1.f, 0.6f)); }
			const float* Applied = LightsApplied.Find(S.Name);
			if (!Applied || FMath::Abs(LightsFor(S.Population) * S.Scar - *Applied) > 0.2f) { if (UWorld* W = GetWorld()) { LightWorld(W, S); } }
			bChanged = true;
		}
		if (S.Kind == ESettlementKind::Starbase && !S.bCommissioned && S.WorksHoursLeft > 0.0)
		{
			S.WorksHoursLeft = FMath::Max(0.0, S.WorksHoursLeft - Dt);
			if (S.WorksHoursLeft <= 0.0) { Report(FString::Printf(TEXT("The works on %s are complete, Captain; she waits to be commissioned."), *S.Name), FLinearColor(0.6f, 1.f, 0.6f), ECrewDepartment::Engineering); }
			bChanged = true;
		}
	}
	if (bChanged) { SaveTimer -= Dt; if (SaveTimer <= 0.0) { SaveTimer = 6.0; Save(); } }
}
