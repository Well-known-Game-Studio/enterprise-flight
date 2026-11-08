// Slonk - exploration: the catalogue of uncharted stars, revealed on the approach and charted on arrival; the
// sector's events; the peoples met.

#include "Exploration.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "StarBody.h"
#include "PlanetBody.h"
#include "ScienceLog.h"
#include "ScienceScannerComponent.h"
#include "CrewRoster.h"
#include "SlonkGameMode.h"
#include "Frontier.h"

const TCHAR* UExplorationSubsystem::SlotName = TEXT("Exploration");

void UExplorationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Rng.Initialize((int32)(FDateTime::Now().GetTicks() % 1000003));
	Load();
}

UExplorationSubsystem* UExplorationSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* W = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UExplorationSubsystem>() : nullptr;
}

void UExplorationSubsystem::InitForTest(UMissionSubsystem* InMissions, UFrontierSubsystem* InFrontier)
{
	bNoDisk = true; MissionsOverride = InMissions; FrontierOverride = InFrontier;
	Rng.Initialize(13);
	Catalogue = MakeCatalogue(CatalogueSeed, CatalogueCount);
	Contacts.Reset();
}

FString UExplorationSubsystem::StateName(EChartState S)
{
	static const TCHAR* N[] = { TEXT("Unknown"), TEXT("Charted"), TEXT("Surveyed") };
	return (int32)S < (int32)EChartState::Count ? N[(int32)S] : TEXT("-");
}

// ---- the catalogue ------------------------------------------------------------------------------

TArray<FCatalogueEntry> UExplorationSubsystem::MakeCatalogue(int32 Seed, int32 Count)
{
	// The three charted neighbours, so no catalogue star sits on top of one.
	struct FKnown { float RA, Dec, Ly; };
	static const FKnown Known[] = { { 14.66f, -60.83f, 4.37f }, { 17.96f, 4.69f, 5.96f }, { 6.75f, -16.72f, 8.6f } };
	FRandomStream R(Seed);
	TArray<FCatalogueEntry> Out;
	TSet<FString> UsedNames; TSet<int32> UsedNumbers;
	TArray<FVector> Placed;
	for (const FKnown& K : Known) { Placed.Add(AStarSystem::DirectionFromSky(K.RA, K.Dec) * (double)K.Ly); }
	for (int32 i = 0; i < Count; ++i)
	{
		FCatalogueEntry E;
		// A bearing and a distance apart from everything placed: at least a light-year and a half.
		for (int32 Attempt = 0; Attempt < 200; ++Attempt)
		{
			E.Spec.RightAscensionHours = R.FRandRange(0.f, 24.f);
			E.Spec.DeclinationDeg = FMath::RadiansToDegrees(FMath::Asin(R.FRandRange(-0.92f, 0.92f)));   // even over the sky
			E.Spec.DistanceLy = 6.f + 24.f * FMath::Pow(R.FRand(), 1.2f);
			const FVector P = AStarSystem::DirectionFromSky(E.Spec.RightAscensionHours, E.Spec.DeclinationDeg) * (double)E.Spec.DistanceLy;
			bool bClear = true; for (const FVector& Q : Placed) { if (FVector::Dist(P, Q) < 1.5) { bClear = false; break; } }
			if (bClear) { Placed.Add(P); break; }
		}
		// Red dwarfs most common, white stars rare; a companion one in four.
		const float C = R.FRand();
		E.Spec.StarClass = C < 0.4f ? EStarClass::M : C < 0.65f ? EStarClass::K : C < 0.85f ? EStarClass::G : C < 0.95f ? EStarClass::F : EStarClass::A;
		if (R.FRand() < 0.25f)
		{
			const float D = R.FRand();
			E.Spec.CompanionClass = D < 0.5f ? EStarClass::M : D < 0.8f ? EStarClass::K : EStarClass::WhiteDwarf;
			E.Spec.CompanionSeparationAU = R.FRandRange(15.f, 40.f);
		}
		int32 Number = 0; do { Number = R.RandRange(1000, 9999); } while (UsedNumbers.Contains(Number));
		UsedNumbers.Add(Number);
		E.Designation = FString::Printf(TEXT("UFC %d"), Number);
		E.Spec.Name = E.Designation;
		E.Spec.Seed = Number * 31 + i;
		E.Spec.bStarOnly = true; E.Spec.bPopulate = false;
		FString Proper = AStarSystem::GenerateWorldName(R, UsedNames);
		Proper.RemoveFromEnd(TEXT(" Prime"));
		E.ProperName = Proper;
		E.bHasBeacon = R.FRand() < 0.34f;
		Out.Add(E);
	}
	return Out;
}

void UExplorationSubsystem::Load()
{
	Catalogue.Reset(); Contacts.Reset();
	if (UGameplayStatics::DoesSaveGameExist(SlotName, 0))
	{
		if (const USlonkExplorationSaveGame* S = Cast<USlonkExplorationSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, 0)))
		{
			Catalogue = S->Catalogue; Contacts = S->Contacts;
		}
	}
	if (Catalogue.Num() != CatalogueCount) { Catalogue = MakeCatalogue(CatalogueSeed, CatalogueCount); }
	UE_LOG(LogTemp, Log, TEXT("Exploration: %d stars in the catalogue, %d charted, %d peoples met"), Catalogue.Num(), CountCharted(), Contacts.Num());
	++Revision;
}

void UExplorationSubsystem::Save()
{
	++Revision;
	if (bNoDisk) { return; }
	USlonkExplorationSaveGame* S = Cast<USlonkExplorationSaveGame>(UGameplayStatics::CreateSaveGameObject(USlonkExplorationSaveGame::StaticClass()));
	if (!S) { return; }
	S->Catalogue = Catalogue; S->Contacts = Contacts;
	UGameplayStatics::SaveGameToSlot(S, SlotName, 0);
}

void UExplorationSubsystem::Reset()
{
	// The names given go back to the catalogue numbers everywhere they went (the missions' targets, the science
	// log's records, the stars and worlds in the sky) before the catalogue is made anew; the stars lose their
	// worlds only with a new session.
	for (FCatalogueEntry& E : Catalogue) { if (!E.GivenName.IsEmpty()) { ApplyRename(E, E.GivenName, E.Designation); } }
	Catalogue = MakeCatalogue(CatalogueSeed, CatalogueCount); Contacts.Reset();
	for (FCatalogueEntry& E : Catalogue) { if (AStarSystem* S = SystemOf(E)) { S->bCharted = false; } }
	Save();
}

void UExplorationSubsystem::ApplyRename(FCatalogueEntry& E, const FString& OldName, const FString& New)
{
	const FString Old = OldName;   // a copy: Reset passes the entry's own given name, cleared on the next line
	if (Old.IsEmpty() || New.IsEmpty() || Old == New) { return; }
	E.GivenName = New == E.Designation ? FString() : New;
	if (AStarSystem* S = SystemOf(E)) { S->RenameSystem(New); }
	// The lit world the sensors noted carries the system's name too, and the lists of its worlds.
	auto Follow = [&](FString& W) { if (W.StartsWith(Old + TEXT(" "))) { W = New + W.RightChop(Old.Len()); } };
	Follow(E.PreWarpWorld);
	for (FString& W : E.SolidWorlds) { Follow(W); }
	for (FString& W : E.HabitableWorlds) { Follow(W); }
	if (UFrontierSubsystem* F = GetGameInstance() ? GetGameInstance()->GetSubsystem<UFrontierSubsystem>() : nullptr) { F->RenameSystem(Old, New); }
	if (FrontierOverride.IsValid()) { FrontierOverride->RenameSystem(Old, New); }
	if (UScienceLogSubsystem* L = GetGameInstance() ? GetGameInstance()->GetSubsystem<UScienceLogSubsystem>() : nullptr) { L->RenameSystem(Old, New); }
	if (UMissionSubsystem* M = Missions()) { M->RenameTarget(Old, New); }
	for (FContactRecord& C : Contacts) { if (C.System == Old) { C.System = New; } }
}

const FCatalogueEntry* UExplorationSubsystem::Find(const FString& NameOrDesignation) const
{
	for (const FCatalogueEntry& E : Catalogue) { if (E.Designation == NameOrDesignation || (!E.GivenName.IsEmpty() && E.GivenName == NameOrDesignation)) { return &E; } }
	return nullptr;
}

const FCatalogueEntry* UExplorationSubsystem::EntryForWorld(const FString& World) const
{
	for (const FCatalogueEntry& E : Catalogue)
	{
		if (E.SolidWorlds.Contains(World) || E.HabitableWorlds.Contains(World) || (!E.PreWarpWorld.IsEmpty() && E.PreWarpWorld == World)) { return &E; }
		if (E.State != EChartState::Unknown && World.StartsWith(E.Name() + TEXT(" "))) { return &E; }
	}
	return nullptr;
}

void UExplorationSubsystem::SetWorldsForTest(const FString& NameOrDesignation, const TArray<FString>& Solid, const TArray<FString>& Habitable, const FString& PreWarp)
{
	if (FCatalogueEntry* E = FindMutable(NameOrDesignation)) { E->SolidWorlds = Solid; E->HabitableWorlds = Habitable; E->PreWarpWorld = PreWarp; E->Worlds = Solid.Num(); }
}

FCatalogueEntry* UExplorationSubsystem::FindMutable(const FString& NameOrDesignation)
{
	for (FCatalogueEntry& E : Catalogue) { if (E.Designation == NameOrDesignation || (!E.GivenName.IsEmpty() && E.GivenName == NameOrDesignation)) { return &E; } }
	return nullptr;
}

int32 UExplorationSubsystem::CountCharted() const
{
	int32 N = 0; for (const FCatalogueEntry& E : Catalogue) { if (E.State != EChartState::Unknown) { ++N; } } return N;
}

FVector UExplorationSubsystem::StarLocation(const FCatalogueEntry& E) const
{
	if (const AStarSystem* S = SystemOf(E)) { return S->GetStarLocation(); }
	return AStarSystem::DirectionFromSky(E.Spec.RightAscensionHours, E.Spec.DeclinationDeg) * ((double)E.Spec.DistanceLy * AStarSystem::LightYearCm);
}

const FCatalogueEntry* UExplorationSubsystem::PickUnknown(FRandomStream& R, bool bNearest) const
{
	TArray<const FCatalogueEntry*> Unknown;
	for (const FCatalogueEntry& E : Catalogue) { if (E.State == EChartState::Unknown) { Unknown.Add(&E); } }
	if (Unknown.Num() == 0) { return nullptr; }
	const FVector Here = Last.Location;
	Unknown.Sort([&](const FCatalogueEntry& A, const FCatalogueEntry& B) { return FVector::DistSquared(StarLocation(A), Here) < FVector::DistSquared(StarLocation(B), Here); });
	if (bNearest) { return Unknown[0]; }
	return Unknown[R.RandRange(0, FMath::Min(Unknown.Num(), 4) - 1)];
}

AStarSystem* UExplorationSubsystem::SystemOf(const FCatalogueEntry& E) const
{
	if (const TWeakObjectPtr<AStarSystem>* P = Systems.Find(E.Designation)) { return P->Get(); }
	return nullptr;
}

UMissionSubsystem* UExplorationSubsystem::Missions() const
{
	if (MissionsOverride.IsValid()) { return MissionsOverride.Get(); }
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UMissionSubsystem>() : nullptr;
}

void UExplorationSubsystem::Report(const FString& Text, const FLinearColor& Color, ECrewDepartment From)
{
	FCrewReport R; R.From = From; R.Text = Text; R.Color = Color;
	Reports.Add(R);
	UE_LOG(LogTemp, Log, TEXT("Exploration: %s"), *Text);
}

// ---- the stars in the world ----------------------------------------------------------------------

void UExplorationSubsystem::SpawnStars(UWorld* World, const FVector& HomeStarLocation)
{
	if (!World) { return; }
	Systems.Reset();
	for (FCatalogueEntry& E : Catalogue)
	{
		FStarSystemSpec Spec = E.Spec;
		Spec.Name = E.Name();
		Spec.bStarOnly = true; Spec.bPopulate = false;
		AStarSystem* S = AStarSystem::Generate(World, Spec, HomeStarLocation);
		if (!S) { continue; }
		S->bCharted = E.State != EChartState::Unknown;
		Systems.Add(E.Designation, S);
	}
	// A charted system has its worlds from the start (the ship may be coming back to one), and the chart its lists of them.
	for (FCatalogueEntry& E : Catalogue) { if (E.State != EChartState::Unknown) { Reveal(E.Designation); } }
	UE_LOG(LogTemp, Log, TEXT("Exploration: %d catalogue stars in the sky"), Systems.Num());
}

bool UExplorationSubsystem::Reveal(const FString& Designation)
{
	FCatalogueEntry* E = FindMutable(Designation);
	AStarSystem* S = E ? SystemOf(*E) : nullptr;
	if (!E || !S) { return false; }
	if (!S->bRevealed)
	{
		S->Reveal();
		// Revealed after the start: the moons and the nebula the start passes would have given its giants.
		if (ASlonkGameMode* GM = S->GetWorld() ? Cast<ASlonkGameMode>(S->GetWorld()->GetAuthGameMode()) : nullptr) { GM->DressSystem(S); }
	}
	// What the sensors count, kept for the chart: the worlds, the lit one, and which have ground (the habitable
	// first, for whoever would settle there).
	int32 Worlds = 0; FString PreWarp;
	TArray<FString> Habitable, Solid, Other;
	for (const APlanetBody* P : S->Planets)
	{
		if (!P || P->IsMoon()) { continue; }
		++Worlds;
		if (P->Style.NightLightIntensity > 0.001f && PreWarp.IsEmpty()) { PreWarp = P->GetDisplayName(); }
		if (!P->HasSolidSurface()) { continue; }
		const EPlanetArchetype A = P->ResolvedArchetype;
		if (A == EPlanetArchetype::Terrestrial || A == EPlanetArchetype::Ocean) { Habitable.Add(P->GetDisplayName()); }
		else if (A == EPlanetArchetype::Desert || A == EPlanetArchetype::Mars) { Solid.Add(P->GetDisplayName()); }
		else { Other.Add(P->GetDisplayName()); }
	}
	E->Worlds = Worlds; E->PreWarpWorld = PreWarp;
	E->HabitableWorlds = Habitable;
	E->SolidWorlds = Habitable; E->SolidWorlds.Append(Solid); E->SolidWorlds.Append(Other);
	return true;
}

void UExplorationSubsystem::AdvanceSeconds(float RealSeconds, const FMissionShipState& Ship)
{
	Last = Ship;
	ScanTimer -= RealSeconds;
	if (ScanTimer > 0.f) { return; }
	ScanTimer = 2.f;
	for (FCatalogueEntry& E : Catalogue)
	{
		AStarSystem* S = SystemOf(E);
		if (!S) { continue; }
		if (!S->bRevealed && FVector::Dist(S->GetStarLocation(), Ship.Location) < RevealCm) { Reveal(E.Designation); }
	}
	// Arrival: the system the ship is in is one of the catalogue's. The chart is made on entry (sixteen hundred AU
	// out, still at warp); what the sector holds is raised once she has dropped out, so a hull is spawned where she
	// is and not where she was.
	if (FCatalogueEntry* E = FindMutable(Ship.SystemName))
	{
		if (E->State == EChartState::Unknown) { Chart(E->Designation, false); }
		// The science log's survey of the system, by whatever name it carries, completes the chart.
		if (E->State == EChartState::Charted)
		{
			const UScienceLogSubsystem* L = GetGameInstance() ? GetGameInstance()->GetSubsystem<UScienceLogSubsystem>() : nullptr;
			if (L && L->IsSystemSurveyed(E->Name())) { MarkSurveyed(E->Designation); }
		}
		if (!Ship.bAtWarp && HasPendingEvents(*E)) { RollEvents(*E); }
	}
}

bool UExplorationSubsystem::Chart(const FString& NameOrDesignation, bool bRollEvents)
{
	FCatalogueEntry* E = FindMutable(NameOrDesignation);
	if (!E || E->State != EChartState::Unknown) { return false; }
	Reveal(E->Designation);
	if (AStarSystem* S = SystemOf(*E)) { S->bCharted = true; }
	UMissionSubsystem* M = Missions();
	// The first chart of the game carries the first contact, however long the roll waits for her to drop out of warp.
	E->bContactGuaranteed = CountCharted() == 0;
	E->State = EChartState::Charted;
	E->ChartedStardate = M ? M->Stardate() : 0.0;
	const FString StarWord = E->Spec.StarClass == EStarClass::M ? TEXT("a red dwarf") : E->Spec.StarClass == EStarClass::K ? TEXT("an orange star") : E->Spec.StarClass == EStarClass::G ? TEXT("a yellow star") : E->Spec.StarClass == EStarClass::F ? TEXT("a yellow-white star") : TEXT("a white star");
	FString T = FString::Printf(TEXT("Entering %s, Captain: %s%s, %d world%s. No one has been here. It is ours to chart."), *E->Designation, *StarWord, E->Spec.CompanionClass != EStarClass::None ? TEXT(" with a companion") : TEXT(""), E->Worlds, E->Worlds == 1 ? TEXT("") : TEXT("s"));
	if (!E->PreWarpWorld.IsEmpty()) { T += FString::Printf(TEXT(" And lights on the night side of %s."), *E->PreWarpWorld); }
	Report(T, FLinearColor(0.7f, 1.f, 0.8f));
	// The chart's worth: regard, science, and the crew's own sense of it.
	if (M) { M->AddStanding(10, FString::Printf(TEXT("%s charted"), *E->Designation)); }
	if (UScienceLogSubsystem* L = GetGameInstance() ? GetGameInstance()->GetSubsystem<UScienceLogSubsystem>() : nullptr) { L->AddReward(FString::Printf(TEXT("chart/%s"), *E->Designation), E->Designation, TEXT("System charted"), 150, TEXT("First Starfleet vessel in the system")); }
	if (UCrewRosterSubsystem* R = GetGameInstance() ? GetGameInstance()->GetSubsystem<UCrewRosterSubsystem>() : nullptr)
	{
		FCrewExperience X; X.Tag = TEXT("Discovery"); X.Intensity = 0.4f; X.Place = E->Designation; X.Text = FString::Printf(TEXT("First into %s"), *E->Designation);
		R->Experience(X);
	}
	if (bRollEvents) { RollEvents(*E); }
	Save();
	return true;
}

void UExplorationSubsystem::RollEvents(FCatalogueEntry& E)
{
	UMissionSubsystem* M = Missions();
	if (!M) { return; }
	// One thing at a time: an encounter already open keeps the rest for the next visit.
	for (const FMission& X : M->GetMissions()) { if (X.IsOpen() && X.Source == EMissionSource::Event) { return; } }
	// A first contact: always in the first system charted, then one in two.
	if (!E.bContactRolled)
	{
		E.bContactRolled = true;
		if (E.bContactGuaranteed || Rng.FRand() < 0.5f)
		{
			if (M->RaiseEvent(EMissionKind::FirstContact) >= 0) { Save(); return; }
		}
	}
	if (!E.bPreWarpRaised && !E.PreWarpWorld.IsEmpty())
	{
		E.bPreWarpRaised = true;
		if (M->RaiseEvent(EMissionKind::PreWarp) >= 0) { Save(); return; }
	}
	if (!E.bBeaconRaised && E.bHasBeacon)
	{
		E.bBeaconRaised = true;
		if (M->RaiseEvent(EMissionKind::Beacon) >= 0) { Save(); return; }
	}
	// Something was rolled and nothing raised: the flags are still worth keeping.
	Save();
}

bool UExplorationSubsystem::Name(const FString& NameOrDesignation, const FString& NewName, FString& OutReply)
{
	FCatalogueEntry* E = FindMutable(NameOrDesignation);
	if (!E) { OutReply = FString::Printf(TEXT("%s is not in the catalogue"), *NameOrDesignation); return false; }
	if (E->State == EChartState::Unknown) { OutReply = FString::Printf(TEXT("%s has not been charted; nobody names a star they have not seen"), *E->Designation); return false; }
	FString Want = NewName.TrimStartAndEnd();
	if (Want.IsEmpty()) { Want = E->ProperName; }
	if (Want.Len() > 24) { OutReply = TEXT("A name of twenty-four letters at most"); return false; }
	if (!IsNameFree(*E, Want)) { OutReply = FString::Printf(TEXT("%s is taken"), *Want); return false; }
	ApplyRename(*E, E->Name(), Want);
	Report(FString::Printf(TEXT("%s is entered in the chart as %s."), *E->Designation, *Want), FLinearColor(0.7f, 1.f, 0.8f), ECrewDepartment::Operations);
	OutReply = FString::Printf(TEXT("%s is now %s"), *E->Designation, *Want);
	Save();
	return true;
}

bool UExplorationSubsystem::IsNameFree(const FCatalogueEntry& E, const FString& Want) const
{
	// Not the scanner's word for nowhere, not another entry's, not a system already in the sky by any name.
	if (Want.Equals(TEXT("Deep space"), ESearchCase::IgnoreCase)) { return false; }
	for (const FCatalogueEntry& O : Catalogue) { if (&O != &E && (O.Designation.Equals(Want, ESearchCase::IgnoreCase) || O.GivenName.Equals(Want, ESearchCase::IgnoreCase))) { return false; } }
	if (UWorld* W = GetWorld())
	{
		const AStarSystem* Mine = SystemOf(E);
		for (TActorIterator<AStarSystem> It(W); It; ++It)
		{
			if (*It == Mine) { continue; }
			if (It->SystemName.Equals(Want, ESearchCase::IgnoreCase)) { return false; }
			if (It->Star && It->Star->StarName.Equals(Want, ESearchCase::IgnoreCase)) { return false; }
		}
	}
	return true;
}

void UExplorationSubsystem::MarkSurveyed(const FString& NameOrDesignation)
{
	FCatalogueEntry* E = FindMutable(NameOrDesignation);
	if (!E || E->State == EChartState::Surveyed) { return; }
	if (E->State == EChartState::Unknown) { Chart(E->Designation); }
	E->State = EChartState::Surveyed;
	Report(FString::Printf(TEXT("The survey of %s is complete, Captain. The chart can be filed."), *E->Name()), FLinearColor(0.7f, 1.f, 0.8f));
	Save();
}

// ---- the peoples ---------------------------------------------------------------------------------

FString UExplorationSubsystem::NewSpeciesName(FRandomStream& R) const
{
	static const TCHAR* AfterVowel[] = { TEXT("n"), TEXT("ni"), TEXT("ri"), TEXT("th"), TEXT("ssi"), TEXT("x") };
	static const TCHAR* AfterConsonant[] = { TEXT("i"), TEXT("an"), TEXT("ari"), TEXT("ese"), TEXT("ar"), TEXT("ai") };
	TSet<FString> Used;
	for (const FContactRecord& C : Contacts) { Used.Add(C.Species); }
	for (int32 Attempt = 0; Attempt < 32; ++Attempt)
	{
		TSet<FString> Scratch;
		FString Root = AStarSystem::GenerateWorldName(R, Scratch);
		Root.RemoveFromEnd(TEXT(" Prime"));
		if (Root.Len() > 7) { Root = Root.Left(R.RandRange(4, 6)); }
		const TCHAR Tail = FChar::ToLower(Root[Root.Len() - 1]);
		const bool bVowel = Tail == 'a' || Tail == 'e' || Tail == 'i' || Tail == 'o' || Tail == 'u';
		const FString Name = Root + (bVowel ? AfterVowel[R.RandRange(0, UE_ARRAY_COUNT(AfterVowel) - 1)] : AfterConsonant[R.RandRange(0, UE_ARRAY_COUNT(AfterConsonant) - 1)]);
		if (!Used.Contains(Name)) { return Name; }
	}
	return FString::Printf(TEXT("People %d"), Contacts.Num() + 1);
}

void UExplorationSubsystem::RecordContact(const FString& Species, const FString& System, int32 Relation, const FString& Note)
{
	for (FContactRecord& C : Contacts) { if (C.Species == Species) { C.Relation = Relation; C.Note = Note; Save(); return; } }
	FContactRecord C; C.Species = Species; C.System = System; C.Relation = Relation; C.Note = Note;
	C.Stardate = Missions() ? Missions()->Stardate() : 0.0;
	Contacts.Add(C);
	if (FCatalogueEntry* E = FindMutable(System)) { E->Contact = Species; }
	Save();
}

const FContactRecord* UExplorationSubsystem::ContactWith(const FString& Species) const
{
	for (const FContactRecord& C : Contacts) { if (C.Species == Species) { return &C; } }
	return nullptr;
}
