// Slonk - the fleet: the other ships' crews and missions, their troubles and our hand in them.

#include "Fleet.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "SpaceVessel.h"
#include "StarSystem.h"
#include "PlanetBody.h"
#include "ScienceScannerComponent.h"

const TCHAR* UFleetSubsystem::SlotName = TEXT("Fleet");

namespace
{
	template <int N> const TCHAR* Pick(FRandomStream& R, const TCHAR* const (&T)[N]) { return T[R.RandRange(0, N - 1)]; }
	const TCHAR* EngineeringTroubles[] = { TEXT("a coolant leak has the warp core offline and the reactor on a timer"), TEXT("a plasma fire has taken the port nacelle and main power with it"), TEXT("the deflector has failed in a dust lane and the hull is taking a beating") };
	const TCHAR* MedicalTroubles[] = { TEXT("a fever is moving through the ship faster than sickbay can treat it"), TEXT("a radiation leak has forty in sickbay and the surgeon down with them"), TEXT("an away team has brought something back and the ward is full") };
	const TCHAR* HostileTroubles[] = { TEXT("Orion raiders have her boxed in and are not answering hails"), TEXT("a Klingon cruiser has her under its guns and is talking about salvage rights"), TEXT("two unidentified ships are circling her with weapons hot") };
	const TCHAR* BoardingTroubles[] = { TEXT("a landing party is pinned down on the surface and the transporter cannot find them"), TEXT("boarders are aboard and her security are holding the engineering deck by the door"), TEXT("her shuttle is down on a moon with its crew alive and its hull open") };
}

FString FFleetShip::TaskText() const
{
	switch (Mission.Task)
	{
	case EFleetTask::UnderWay: return Mission.HoursLeft < 1.f ? FString::Printf(TEXT("arriving at %s"), *Mission.TargetName) : FString::Printf(TEXT("under way to %s, %.0f h out"), *Mission.TargetName, Mission.HoursLeft);
	case EFleetTask::OnStation: return Mission.Trouble != EFleetTrouble::None ? FString::Printf(TEXT("in trouble at %s"), *Mission.TargetName) : Mission.HoursLeft < 1.f ? FString::Printf(TEXT("at %s, nearly done"), *Mission.TargetName) : FString::Printf(TEXT("at %s, %.0f h of work left"), *Mission.TargetName, Mission.HoursLeft);
	case EFleetTask::Returning: return FString::Printf(TEXT("returning to %s"), *HomeStation);
	default: return bLost ? TEXT("lost") : FString::Printf(TEXT("on station at %s"), *HomeStation);
	}
}

const FFleetPerson* FFleetShip::Head(ECrewDepartment Dept) const
{
	for (const FFleetPerson& P : Officers) { if (P.Person.Department == Dept) { return &P; } }
	return nullptr;
}

float FFleetShip::Skill(ECrewSkill S) const
{
	// The captain and the head whose skill it is: the ship's own capability, with its condition and morale.
	float Best = Captain.Skill(S);
	for (const FFleetPerson& P : Officers) { Best = FMath::Max(Best, P.Person.Skill(S)); }
	return Best * (0.6f + 0.4f * Condition) * (0.8f + 0.4f * Morale);
}

FString UFleetSubsystem::TiesText(const FFleetPerson& P, const UCrewRosterSubsystem* R)
{
	FString Out;
	for (const FFleetTie& T : P.Ties) { const FCrewMember* M = R ? R->Find(T.CrewId) : nullptr; if (M) { Out += FString::Printf(TEXT("%s%s of our %s"), Out.IsEmpty() ? TEXT("") : TEXT("; "), *T.Relation, *M->Styled()); } }
	return Out;
}

FString UFleetSubsystem::TaskName(EFleetTask T) { static const TCHAR* N[] = { TEXT("on station"), TEXT("under way"), TEXT("at work"), TEXT("returning") }; return (int32)T < 4 ? N[(int32)T] : TEXT("-"); }
FString UFleetSubsystem::TroubleName(EFleetTrouble T) { static const TCHAR* N[] = { TEXT("none"), TEXT("engineering"), TEXT("medical"), TEXT("hostiles"), TEXT("boarding") }; return (int32)T < 5 ? N[(int32)T] : TEXT("-"); }
bool UFleetSubsystem::ParseTrouble(const FString& Word, EFleetTrouble& Out)
{
	const FString W = Word.ToLower();
	for (int32 i = 1; i < (int32)EFleetTrouble::Count; ++i) { if (W.Len() >= 3 && TroubleName((EFleetTrouble)i).StartsWith(W)) { Out = (EFleetTrouble)i; return true; } }
	return false;
}

// ---- lifetime ----

void UFleetSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Load();
	Rng.Initialize((int32)(FDateTime::Now().GetTicks() % 1000003));
}

void UFleetSubsystem::Load()
{
	Ships.Reset();
	if (UGameplayStatics::DoesSaveGameExist(SlotName, 0))
	{
		if (const USlonkFleetSaveGame* S = Cast<USlonkFleetSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, 0)))
		{
			Ships = S->Ships; NextPersonId = S->NextPersonId;
			UE_LOG(LogTemp, Log, TEXT("Fleet: loaded %d ships"), Ships.Num());
		}
	}
	++Revision;
}

void UFleetSubsystem::Save()
{
	if (bNoDisk) { bSaveWanted = false; return; }
	USlonkFleetSaveGame* S = Cast<USlonkFleetSaveGame>(UGameplayStatics::CreateSaveGameObject(USlonkFleetSaveGame::StaticClass()));
	if (!S) { return; }
	S->Ships = Ships; S->NextPersonId = NextPersonId;
	UGameplayStatics::SaveGameToSlot(S, SlotName, 0);
	bSaveWanted = false;
}

void UFleetSubsystem::Reset()
{
	if (!bNoDisk) { UGameplayStatics::DeleteGameInSlot(SlotName, 0); }
	for (FFleetShip& S : Ships) { DropBoardEntries(S); if (S.Actor.IsValid()) { SendActor(S, false); } }
	Ships.Reset();
	Dirty(); Save();
}

UCrewRosterSubsystem* UFleetSubsystem::Roster() const { if (RosterOverride.IsValid()) { return RosterOverride.Get(); } return GetGameInstance() ? GetGameInstance()->GetSubsystem<UCrewRosterSubsystem>() : nullptr; }
UMissionSubsystem* UFleetSubsystem::Missions() const { if (MissionsOverride.IsValid()) { return MissionsOverride.Get(); } return GetGameInstance() ? GetGameInstance()->GetSubsystem<UMissionSubsystem>() : nullptr; }

void UFleetSubsystem::InitForTest(UCrewRosterSubsystem* InRoster, UMissionSubsystem* InMissions)
{
	bNoDisk = true; RosterOverride = InRoster; MissionsOverride = InMissions;
	Rng.Initialize(13);
}

bool UFleetSubsystem::RegisterShip(const FString& Name, const FString& ShipClass, const FString& Home, const FString& System)
{
	if (Find(Name)) { return false; }
	Ships.Add(MakeShip(Name, ShipClass, Home, System));
	Ships.Last().bReconciled = true;
	BindTies();
	Dirty();
	return true;
}
double UFleetSubsystem::Hours() const { if (SimHours >= 0.0) { return SimHours; } const UCrewRosterSubsystem* R = Roster(); return R ? R->GetHours() : 0.0; }
double UFleetSubsystem::Stardate() const { const UCrewRosterSubsystem* R = Roster(); if (!R) { return 8305.0; } return R->GetStardate() - (R->GetHours() - Hours()) / 24.0; }
FFleetShip* UFleetSubsystem::FindMutable(const FString& Name) { for (FFleetShip& S : Ships) { if (S.Name == Name) { return &S; } } return nullptr; }
const FFleetShip* UFleetSubsystem::Find(const FString& Name) const { for (const FFleetShip& S : Ships) { if (S.Name == Name) { return &S; } } return nullptr; }
bool UFleetSubsystem::InRange(const FFleetShip& S) const { return Last.Nearby.Contains(S.Name); }
TArray<const FFleetShip*> UFleetSubsystem::Nearby() const { TArray<const FFleetShip*> Out; for (const FFleetShip& S : Ships) { if (!S.bLost && InRange(S)) { Out.Add(&S); } } return Out; }

void UFleetSubsystem::Report(const FString& Text, const FLinearColor& Color, ECrewDepartment From)
{
	FCrewReport R; R.From = From; R.Text = Text; R.Color = Color; Reports.Add(R);
	UE_LOG(LogTemp, Log, TEXT("Fleet: %s"), *Text);
}

FString UFleetSubsystem::Log(FFleetShip& S, const FString& Text)
{
	const FString L = FString::Printf(TEXT("%.1f  %s"), Stardate(), *Text);
	S.Log.Add(L); if (S.Log.Num() > 40) { S.Log.RemoveAt(0); }
	return L;
}

TArray<const FFleetPerson*> UFleetSubsystem::TiedTo(const FString& ShipName, int32 CrewId) const
{
	TArray<const FFleetPerson*> Out;
	if (const FFleetShip* S = Find(ShipName))
	{
		for (const FFleetPerson& P : S->Officers) { if (P.TiedTo(CrewId)) { Out.Add(&P); } }
		for (const FFleetPerson& P : S->Notables) { if (P.TiedTo(CrewId)) { Out.Add(&P); } }
	}
	return Out;
}

// ---- the registry ----

void UFleetSubsystem::ScanShips()
{
	UWorld* W = GetWorld();
	if (!W) { return; }
	for (FFleetShip& S : Ships) { S.Actor = nullptr; }
	int32 New = 0;
	for (TActorIterator<AProceduralStarship> It(W); It; ++It)
	{
		const FString N = It->GetDisplayName();
		if (!N.StartsWith(TEXT("USS ")) || It->IsDying()) { continue; }
		FFleetShip* S = FindMutable(N);
		if (S && S->bLost)
		{
			// A hull bearing a lost ship's name: Starfleet has given the name again. The lost record keeps her
			// history under a marked name; the new ship starts fresh (every tie to the lost people is long dropped).
			FString Kept = N + TEXT(" (lost)"); for (int32 k = 2; Find(Kept); ++k) { Kept = FString::Printf(TEXT("%s (lost %d)"), *N, k); }
			Log(*S, FString::Printf(TEXT("A new ship bears the name %s."), *N));
			S->Name = Kept; S = nullptr;
		}
		if (!S)
		{
			// Her home is the nearest station; her system the sky she sails under.
			FString Home, System = UScienceScannerComponent::SystemNameAt(W, It->GetActorLocation());
			double Best = TNumericLimits<double>::Max();
			for (TActorIterator<ASpaceStation> St(W); St; ++St) { const double D = FVector::Dist(St->GetActorLocation(), It->GetActorLocation()); if (D < Best) { Best = D; Home = St->GetDisplayName(); } }
			static const TCHAR* ClassNames[] = { TEXT("Constitution"), TEXT("Constitution"), TEXT("Miranda"), TEXT("Excelsior") };
			Ships.Add(MakeShip(N, ClassNames[FMath::Clamp((int32)It->GetBuiltClass(), 0, 3)], Home, System));
			S = &Ships.Last(); S->bReconciled = true; ++New;   // a new record: her hull is where it is, at home
		}
		if (!S->Actor.IsValid()) { S->Actor = *It; }
		if (!S->bReconciled) { Reconcile(*S); }
	}
	if (New > 0) { UE_LOG(LogTemp, Log, TEXT("Fleet: %d ships registered (%d in all)"), New, Ships.Num()); bBindWanted = true; Dirty(); }
}

void UFleetSubsystem::Reconcile(FFleetShip& S)
{
	// A saved record meets its hull again: a ship on a far run is out of sight; one on a near mission goes where
	// the record says; the rest are home. (A lost record never binds a hull: the scan makes a new ship of the name.)
	AProceduralStarship* A = S.Actor.Get();
	if (!A) { return; }
	S.bReconciled = true;
	if (S.bLost) { S.Actor = nullptr; return; }
	if (!S.Mission.IsActive()) { if (A->IsAway()) { A->SetAway(false); } return; }
	if (S.Mission.bFar) { if (S.Mission.Task == EFleetTask::Returning && S.Mission.HoursLeft < 1.f) { SendActor(S, false); } else { A->SetAway(true); } return; }
	SendActor(S, S.Mission.Task != EFleetTask::Returning);
}

FFleetShip UFleetSubsystem::MakeShip(const FString& Name, const FString& ShipClass, const FString& Home, const FString& System)
{
	FFleetShip S; S.Name = Name; S.ShipClass = ShipClass; S.HomeStation = Home; S.SystemName = System;
	S.Seed = GetTypeHash(Name);
	FRandomStream R(S.Seed);
	S.Complement = ShipClass == TEXT("Miranda") ? R.RandRange(200, 260) : ShipClass == TEXT("Excelsior") ? R.RandRange(600, 760) : R.RandRange(380, 440);
	S.Morale = R.FRandRange(0.45f, 0.75f); S.Condition = R.FRandRange(0.8f, 1.f); S.Relation = R.FRandRange(-0.1f, 0.2f);
	S.NextMissionHours = Hours() + R.FRandRange(0.5, 6.0);
	if (UCrewRosterSubsystem* Ros = Roster())
	{
		S.Captain = Ros->MakeOutsider(R, ECrewDepartment::Command, ECrewRank::Captain, NextPersonId++);
		S.Captain.Posting = FString::Printf(TEXT("Commanding, %s"), *Name);
		static const ECrewDepartment Heads[] = { ECrewDepartment::Command, ECrewDepartment::Helm, ECrewDepartment::Engineering, ECrewDepartment::Science, ECrewDepartment::Medical, ECrewDepartment::Tactical, ECrewDepartment::Security };
		for (ECrewDepartment D : Heads)
		{
			FFleetPerson P; P.Person = Ros->MakeOutsider(R, D, D == ECrewDepartment::Command ? ECrewRank::Commander : ECrewRank::LtCommander, NextPersonId++);
			P.Person.bHead = true; P.Person.Posting = D == ECrewDepartment::Command ? TEXT("First officer") : FString::Printf(TEXT("Chief, %s"), *UCrewRosterSubsystem::DepartmentName(D).ToLower());
			P.Origin = FString::Printf(TEXT("serving under Captain %s"), *(S.Captain.LastName.IsEmpty() ? S.Captain.FirstName : S.Captain.LastName));
			S.Officers.Add(P);
		}
	}
	S.Log.Add(FString::Printf(TEXT("%.1f  %s, %s class, Captain %s commanding, out of %s."), Stardate(), *Name, *ShipClass, *S.Captain.FullName(), *Home));
	return S;
}

void UFleetSubsystem::BindTies()
{
	UCrewRosterSubsystem* Ros = Roster();
	if (!Ros) { return; }
	// The roster's ties by ship, in one pass; the dead and the transferred leave no one behind to know anybody.
	TMap<FString, TArray<TPair<const FCrewMember*, const FCrewTie*>>> ByShip;
	for (const FCrewMember& M : Ros->GetRoster())
	{
		if (M.Condition == ECrewCondition::Dead || M.Condition == ECrewCondition::Transferred) { continue; }
		for (const FCrewTie& T : M.Ties) { if (T.Kind == ECrewTiePlace::Ship && !T.Place.IsEmpty()) { ByShip.FindOrAdd(T.Place).Emplace(&M, &T); } }
	}
	static const TArray<TPair<const FCrewMember*, const FCrewTie*>> None;
	for (FFleetShip& S : Ships) { if (!S.bLost) { const TArray<TPair<const FCrewMember*, const FCrewTie*>>* L = ByShip.Find(S.Name); BindShipTies(S, L ? *L : None); } }
}

void UFleetSubsystem::BindShipTies(FFleetShip& S, const TArray<TPair<const FCrewMember*, const FCrewTie*>>& Ties)
{
	UCrewRosterSubsystem* Ros = Roster();
	if (!Ros) { return; }
	// Only the ties the roster still has: a knower dead, transferred or replaced (a new complement reuses ids)
	// takes their tie with them, and someone aboard her only because one of ours knew them goes when no one does.
	TSet<FString> Keys; for (const TPair<const FCrewMember*, const FCrewTie*>& Pair : Ties) { Keys.Add(FString::Printf(TEXT("%d|%s"), Pair.Key->Id, *Pair.Value->Name)); }
	int32 Pruned = 0;
	auto PruneTies = [&Keys, &Pruned](FFleetPerson& P) { const FString Name = P.Person.FullName(); Pruned += P.Ties.RemoveAll([&Keys, &Name](const FFleetTie& T) { return !Keys.Contains(FString::Printf(TEXT("%d|%s"), T.CrewId, *Name)); }); };
	for (FFleetPerson& P : S.Officers) { PruneTies(P); }
	for (FFleetPerson& P : S.Notables) { PruneTies(P); }
	const int32 Dropped = S.Notables.RemoveAll([](const FFleetPerson& P) { return P.Origin == TEXT("known to us") && P.Ties.Num() == 0; });
	bool bChanged = Pruned > 0 || Dropped > 0;
	if (Dropped > 0) { UE_LOG(LogTemp, Log, TEXT("Fleet: %s: %d people nobody aboard knows any more dropped from her file"), *S.Name, Dropped); }
	for (const TPair<const FCrewMember*, const FCrewTie*>& Pair : Ties)
	{
		const FCrewMember& M = *Pair.Key; const FCrewTie& T = *Pair.Value;
		// One record per name aboard her; a second of ours who knows them adds a tie, not a person.
		FFleetPerson* Have = nullptr;
		for (FFleetPerson& P : S.Officers) { if (P.Person.FullName() == T.Name) { Have = &P; } }
		for (FFleetPerson& P : S.Notables) { if (P.Person.FullName() == T.Name) { Have = &P; } }
		if (Have) { if (!Have->TiedTo(M.Id)) { FFleetTie Tie; Tie.CrewId = M.Id; Tie.Relation = T.Relation; Have->Ties.Add(Tie); bChanged = true; } continue; }
		FRandomStream R(GetTypeHash(T.Name) ^ S.Seed);
		FFleetPerson P;
		const ECrewDepartment D = (ECrewDepartment)R.RandRange(1, (int32)ECrewDepartment::Count - 1);
		const bool bSenior = T.Relation.Contains(TEXT("captain")) || T.Relation.Contains(TEXT("chief")) || T.Relation.Contains(TEXT("mentor"));
		P.Person = Ros->MakeOutsider(R, D, bSenior ? ECrewRank::Commander : (ECrewRank)R.RandRange((int32)ECrewRank::AbleCrewman, (int32)ECrewRank::Lieutenant), NextPersonId++, T.Name, M.Species);
		{ FFleetTie Tie; Tie.CrewId = M.Id; Tie.Relation = T.Relation; P.Ties.Add(Tie); }
		P.Origin = TEXT("known to us");
		S.Notables.Add(P); bChanged = true;
	}
	if (bChanged) { UE_LOG(LogTemp, Log, TEXT("Fleet: %s now carries %d people our crew know"), *S.Name, S.Notables.Num()); Dirty(); }
}

// ---- the ship's clock ----

void UFleetSubsystem::AdvanceSeconds(float Seconds, const FMissionShipState& Ship)
{
	Last = Ship;
	ScanTimer -= Seconds;
	if (ScanTimer <= 0.f) { ScanTimer = 5.f; ScanShips(); }
	// Ties from our crew become people aboard whenever either side is new: the roster's ties change only with
	// the roster (a new complement, a transfer, a join), the ships when one is first seen.
	{ const UCrewRosterSubsystem* Ros = Roster(); const int32 Rev = Ros ? Ros->GetRevision() : 0; if (bBindWanted || Rev != TiesRevision) { TiesRevision = Rev; bBindWanted = false; BindTies(); } }
	const double H = Hours();
	if (LastHour < 0.0) { LastHour = FMath::Floor(H); }
	// The shared clock went back (a new complement starts it over): every schedule keeps its distance to now.
	if (FMath::Floor(H) < LastHour) { const double Back = LastHour - FMath::Floor(H); for (FFleetShip& S : Ships) { S.NextMissionHours -= Back; } LastHour = FMath::Floor(H); }
	// Whole hours, however many the clock jumped (dev time), each lived at its own hour: departures, schedules
	// and log stamps read the hour being replayed, not the clock's final reading.
	int32 Steps = 0;
	while (FMath::Floor(H) > LastHour && Steps < 48) { LastHour += 1.0; ++Steps; SimHours = LastHour; for (FFleetShip& S : Ships) { if (!S.bLost) { LiveHour(S); } } }
	SimHours = -1.0;
	if (FMath::Floor(H) > LastHour) { LastHour = FMath::Floor(H); }
	if (Steps > 0) { Dirty(); }   // countdowns, repairs, trouble hours: the files and the save see them
	// Who is in range changed: the fleet view re-sorts, no save needed.
	TSet<FString> Near; for (const FFleetShip& S : Ships) { if (!S.bLost && InRange(S)) { Near.Add(S.Name); } }
	if (Near.Num() != LastNear.Num() || !Near.Includes(LastNear)) { LastNear = MoveTemp(Near); ++Revision; }
	if (bSaveWanted) { SaveDelaySeconds -= Seconds; if (SaveDelaySeconds <= 0.f) { Save(); } }
}

void UFleetSubsystem::LiveHour(FFleetShip& S)
{
	FFleetMission& M = S.Mission;
	switch (M.Task)
	{
	case EFleetTask::Idle:
		S.Condition = FMath::Min(1.f, S.Condition + 0.04f);   // alongside, the yard works on her
		S.Morale = FMath::Lerp(S.Morale, 0.6f, 0.05f);
		if (Hours() >= S.NextMissionHours) { BeginMission(S); }
		break;
	case EFleetTask::UnderWay:
		M.HoursLeft -= 1.f;
		if (M.HoursLeft <= 0.f) { M.Task = EFleetTask::OnStation; M.HoursLeft = M.WorkHours; Log(S, FString::Printf(TEXT("Arrived at %s; %s begins."), *M.TargetName, *M.Title)); Dirty(); }
		break;
	case EFleetTask::OnStation:
		if (M.Trouble != EFleetTrouble::None)
		{
			M.TroubleHours += 1.f;
			// They work at it themselves; now and then someone pays for it; past a day it is beyond them.
			const float Own = S.Skill(M.Trouble == EFleetTrouble::Engineering ? ECrewSkill::Engineering : M.Trouble == EFleetTrouble::Medical ? ECrewSkill::Medicine : M.Trouble == EFleetTrouble::Hostiles ? ECrewSkill::Tactical : ECrewSkill::Security);
			if (Rng.FRand() < 0.04f + Own / 2000.f) { SelfResolved(S); Log(S, TEXT("They got through it on their own.")); if (InRange(S)) { Report(FString::Printf(TEXT("%s reports she has her trouble in hand."), *S.Name)); } break; }
			if (Rng.FRand() < 0.25f) { Casualty(S); }
			if (M.TroubleHours >= 24.f) { SupportResolved(S.Name, false, M.SupportMissionId >= 0 || M.bHelpRefused); }
			break;
		}
		M.HoursLeft -= 1.f;
		if (M.Trouble == EFleetTrouble::None && Rng.FRand() < (M.Kind == EMissionKind::Rescue || M.Kind == EMissionKind::Patrol ? 0.03f : 0.015f) * (1.5f - S.Condition))
		{
			const float X = Rng.FRand();
			Trouble(S, M.Kind == EMissionKind::Patrol ? (X < 0.6f ? EFleetTrouble::Hostiles : EFleetTrouble::Engineering) : M.Kind == EMissionKind::Medical ? (X < 0.6f ? EFleetTrouble::Medical : EFleetTrouble::Boarding) : X < 0.35f ? EFleetTrouble::Engineering : X < 0.6f ? EFleetTrouble::Boarding : X < 0.8f ? EFleetTrouble::Medical : EFleetTrouble::Hostiles);
			break;
		}
		if (M.HoursLeft <= 0.f)
		{
			// The work done: did it come off? Their own hands, the ship's state, and whether we lent one.
			const ECrewSkill Sk = M.Kind == EMissionKind::Survey ? ECrewSkill::Science : M.Kind == EMissionKind::Diplomacy ? ECrewSkill::Diplomacy : M.Kind == EMissionKind::Medical ? ECrewSkill::Medicine : M.Kind == EMissionKind::Patrol ? ECrewSkill::Tactical : M.Kind == EMissionKind::Rescue ? ECrewSkill::Operations : ECrewSkill::Operations;
			const float Chance = FMath::Clamp(0.35f + S.Skill(Sk) / 160.f + (M.bAssisted ? 0.25f : 0.f), 0.2f, 0.95f);
			EndMission(S, Rng.FRand() < Chance, M.bAssisted ? TEXT("with a hand from us") : TEXT("on her own"));
		}
		break;
	case EFleetTask::Returning:
		M.HoursLeft -= 1.f;
		if (M.HoursLeft <= 0.f)
		{
			M.Task = EFleetTask::Idle; S.NextMissionHours = Hours() + Rng.FRandRange(3.0, 10.0);
			if (M.bFar) { SendActor(S, false); }   // back in sight at home; a local hull set out for home when the work ended
			if (InRange(S)) { Report(FString::Printf(TEXT("%s is back alongside %s."), *S.Name, *S.HomeStation)); }
			Dirty();
		}
		break;
	default: break;
	}
}

void UFleetSubsystem::BeginMission(FFleetShip& S, bool bForceNear, bool bMoveActor)
{
	UWorld* W = GetWorld();
	FFleetMission& M = S.Mission;
	M = FFleetMission();
	FRandomStream R(Rng.RandRange(1, 1 << 20));
	static const EMissionKind Kinds[] = { EMissionKind::Survey, EMissionKind::Delivery, EMissionKind::Diplomacy, EMissionKind::Medical, EMissionKind::Patrol, EMissionKind::Survey, EMissionKind::Rescue };
	M.Kind = Kinds[R.RandRange(0, UE_ARRAY_COUNT(Kinds) - 1)];
	// A place in her own system most of the time; one run in four goes to another star, out of sight.
	TArray<FString> Here, Far;
	if (W)
	{
		for (TActorIterator<AStarSystem> It(W); It; ++It)
		{
			const bool bOwn = It->SystemName == S.SystemName;
			if (!bOwn) { Far.Add(It->SystemName); continue; }
			for (APlanetBody* P : It->Planets) { if (P && !P->IsMoon()) { Here.Add(P->GetDisplayName()); } }
		}
		for (TActorIterator<ASpaceStation> It(W); It; ++It) { if (UScienceScannerComponent::SystemNameAt(W, It->GetActorLocation()) == S.SystemName && It->GetDisplayName() != S.HomeStation) { Here.Add(It->GetDisplayName()); } }
	}
	M.bFar = Far.Num() > 0 && (Here.Num() == 0 || (!bForceNear && R.FRand() < 0.25f));
	if (M.bFar) { M.TargetName = Far[R.RandRange(0, Far.Num() - 1)]; }
	else if (Here.Num() > 0) { M.TargetName = Here[R.RandRange(0, Here.Num() - 1)]; }
	else { M.TargetName = S.HomeStation; }   // no galaxy to pick from (a test): exercises off her own station
	const TCHAR* Work = M.Kind == EMissionKind::Survey ? TEXT("charting") : M.Kind == EMissionKind::Delivery ? TEXT("a supply run to") : M.Kind == EMissionKind::Diplomacy ? TEXT("talks at") : M.Kind == EMissionKind::Medical ? TEXT("a medical call at") : M.Kind == EMissionKind::Patrol ? TEXT("a patrol of") : TEXT("a distress call near");
	M.Title = FString::Printf(TEXT("%s %s"), Work, *M.TargetName);
	M.Task = EFleetTask::UnderWay; M.HoursLeft = M.bFar ? R.FRandRange(6.f, 14.f) : R.FRandRange(1.f, 3.f);
	M.WorkHours = R.FRandRange(3.f, 10.f); M.StartedStardate = Stardate();
	Log(S, FString::Printf(TEXT("Under way: %s."), *M.Title));
	if (bMoveActor) { SendActor(S, true); }
	if (InRange(S)) { Report(FString::Printf(TEXT("%s signals she is under way: %s."), *S.Name, *M.Title)); }
	// A ship near us at work is something to lend a hand with; the mission system puts it on the board when we come near.
	Dirty();
}

void UFleetSubsystem::EndMission(FFleetShip& S, bool bSuccess, const FString& Why)
{
	FFleetMission& M = S.Mission;
	if (bSuccess) { ++S.Done; S.Morale = FMath::Min(1.f, S.Morale + 0.08f); } else { ++S.Failed; S.Morale = FMath::Max(0.f, S.Morale - 0.1f); S.Condition = FMath::Max(0.2f, S.Condition - 0.15f); }
	Log(S, FString::Printf(TEXT("%s: %s, %s."), *M.Title, bSuccess ? TEXT("done") : TEXT("failed"), *Why));
	if (M.AssistMissionId >= 0) { if (UMissionSubsystem* Mi = Missions()) { Mi->FleetWorkOver(M.AssistMissionId, bSuccess); } }
	if (M.SupportMissionId >= 0) { if (UMissionSubsystem* Mi = Missions()) { Mi->FleetWorkOver(M.SupportMissionId, false); } }
	if (M.bAssisted) { if (UCrewRosterSubsystem* Ros = Roster()) { Ros->NoteShipNews(S.Name, FString::Printf(TEXT("%s finished %s%s"), *S.Name, *M.Title, bSuccess ? TEXT(", with our help") : TEXT(", badly")), bSuccess ? 0.12f : -0.08f); } }
	M.Task = EFleetTask::Returning; M.HoursLeft = M.bFar ? Rng.FRandRange(6.f, 14.f) : Rng.FRandRange(1.f, 3.f);
	M.Trouble = EFleetTrouble::None; M.SupportMissionId = -1; M.AssistMissionId = -1; M.bHelpRefused = false;
	if (!M.bFar) { SendActor(S, false); }   // her hull heads home now; the countdown is the trip. A far hull stays out of sight until it ends.
	if (InRange(S)) { Report(FString::Printf(TEXT("%s reports %s %s; she is heading home."), *S.Name, *M.Title, bSuccess ? TEXT("done") : TEXT("a failure"))); }
	Dirty();
}

void UFleetSubsystem::Trouble(FFleetShip& S, EFleetTrouble Kind)
{
	FFleetMission& M = S.Mission;
	FRandomStream R(Rng.RandRange(1, 1 << 20));
	M.Trouble = Kind; M.TroubleHours = 0.f;
	M.TroubleText = Kind == EFleetTrouble::Engineering ? Pick(R, EngineeringTroubles) : Kind == EFleetTrouble::Medical ? Pick(R, MedicalTroubles) : Kind == EFleetTrouble::Hostiles ? Pick(R, HostileTroubles) : Pick(R, BoardingTroubles);
	Log(S, FString::Printf(TEXT("Trouble: %s."), *M.TroubleText));
	// The request goes out on subspace to whoever is in range of it: us, when she is in our star system and not
	// out of sight on a far run; the mission system takes at most a couple at a time. Other ships' troubles are
	// theirs to live through; those with people aboard hear of it either way.
	const bool bHearable = !M.bFar && S.SystemName == Last.SystemName;
	if (bHearable)
	{
		FFleetRequest Q; Q.ShipName = S.Name; Q.bSupport = true; Q.Trouble = Kind; Q.Text = M.TroubleText; Q.MissionTitle = M.Title; Q.TheirKind = M.Kind; Q.HoursLeft = 24.f;
		Requests.Add(Q);
		Report(FString::Printf(TEXT("Subspace from %s, Captain %s: %s. She is asking for support at %s."), *S.Name, *S.Captain.FullName(), *M.TroubleText, *M.TargetName), FLinearColor(1.f, 0.75f, 0.5f));
	}
	if (UCrewRosterSubsystem* Ros = Roster()) { Ros->NoteShipNews(S.Name, FString::Printf(TEXT("%s is in trouble: %s"), *S.Name, *M.TroubleText), -0.15f); }
	Dirty();
}

void UFleetSubsystem::Casualty(FFleetShip& S)
{
	++S.Lost; S.Complement = FMath::Max(0, S.Complement - 1); S.Morale = FMath::Max(0.f, S.Morale - 0.03f);
	// Rarely someone with a name: a notable tied to one of ours, who hears of it as a loss.
	if (S.Notables.Num() > 0 && Rng.FRand() < 0.12f)
	{
		const int32 k = Rng.RandRange(0, S.Notables.Num() - 1);
		const FFleetPerson P = S.Notables[k]; S.Notables.RemoveAt(k);
		Log(S, FString::Printf(TEXT("%s was killed."), *P.Person.Styled()));
		if (UCrewRosterSubsystem* Ros = Roster()) { for (const FFleetTie& T : P.Ties) { Ros->TieLost(T.CrewId, P.Person.FullName(), S.Name); } }
	}
	Dirty();
}

void UFleetSubsystem::SendActor(FFleetShip& S, bool bOut)
{
	AProceduralStarship* A = S.Actor.Get();
	if (!A) { return; }
	if (bOut && S.Mission.bFar) { A->SetAway(true); return; }
	if (A->IsAway()) { A->SetAway(false); }   // back from a far run: at home, then on to wherever this is
	// A warp in progress (the traffic's own, or the last leg) gives way to this one; WarpTo will not interrupt one.
	if (A->IsInWarp()) { A->RecallTo(A->GetActorLocation(), A->PatrolRadiusCm); }
	// A place in this system: warp there (or home) and patrol about it.
	UWorld* W = GetWorld();
	const FString& Want = bOut ? S.Mission.TargetName : S.HomeStation;
	for (TActorIterator<ASpaceVessel> It(W); It; ++It) { if (It->GetDisplayName() == Want) { A->bCanWarp = false; A->HomeLocation = It->GetActorLocation(); A->PatrolRadiusCm = 2.0e7; A->WarpTo(It->GetActorLocation() + FVector(Rng.FRandRange(-1.f, 1.f), Rng.FRandRange(-1.f, 1.f), 0.2f).GetSafeNormal() * 1.5e7); return; } }
	for (TActorIterator<APlanetBody> It(W); It; ++It) { if (It->GetDisplayName() == Want) { const FVector Off = FVector(Rng.FRandRange(-1.f, 1.f), Rng.FRandRange(-1.f, 1.f), 0.3f).GetSafeNormal() * It->GetRadiusCm() * 3.0; A->bCanWarp = false; A->HomeLocation = It->GetActorLocation() + Off; A->PatrolRadiusCm = It->GetRadiusCm(); A->WarpTo(A->HomeLocation); return; } }
}

// ---- answers from the mission system ----

void UFleetSubsystem::SupportResolved(const FString& ShipName, bool bSuccess, bool bIgnored)
{
	FFleetShip* S = FindMutable(ShipName);
	if (!S || S->Mission.Trouble == EFleetTrouble::None) { return; }
	FFleetMission& M = S->Mission;
	UCrewRosterSubsystem* Ros = Roster();
	if (bSuccess)
	{
		S->Relation = FMath::Min(1.f, S->Relation + 0.3f); S->Morale = FMath::Min(1.f, S->Morale + 0.1f);
		M.Trouble = EFleetTrouble::None; M.SupportMissionId = -1; M.bAssisted = true;
		Log(*S, TEXT("The trouble is over; the work goes on."));
		if (Ros) { Ros->NoteShipNews(ShipName, FString::Printf(TEXT("%s is out of trouble, with our help"), *ShipName), 0.2f); }
	}
	else
	{
		// Beyond them: the mission is lost, the ship hurt, and they remember who did not come.
		S->Condition = FMath::Max(0.15f, S->Condition - 0.3f);
		for (int32 i = 0; i < 3; ++i) { Casualty(*S); }
		if (bIgnored) { S->Relation = FMath::Max(-1.f, S->Relation - 0.25f); Log(*S, TEXT("No help came.")); if (Ros) { Ros->NoteShipNews(ShipName, FString::Printf(TEXT("%s called for help and we did not go"), *ShipName), -0.2f); } }
		M.Trouble = EFleetTrouble::None;   // the board entry, if still open, closes through EndMission's FleetWorkOver
		Requests.RemoveAll([&ShipName](const FFleetRequest& Q) { return Q.bSupport && Q.ShipName == ShipName; });
		EndMission(*S, false, bIgnored ? TEXT("no help came") : TEXT("the help was not enough"));
	}
	Dirty();
}

void UFleetSubsystem::SupportDeclined(const FString& ShipName)
{
	FFleetShip* S = FindMutable(ShipName);
	if (!S) { return; }
	FFleetMission& M = S->Mission;
	M.SupportMissionId = -1;
	if (M.Trouble == EFleetTrouble::None) { Dirty(); return; }   // she managed in the meantime; nothing to tell her
	// She is told, and goes on alone with what is left of her day; her people remember who turned away.
	M.bHelpRefused = true;
	S->Relation = FMath::Max(-1.f, S->Relation - 0.15f);
	Log(*S, TEXT("Told no help is coming; they carry on alone."));
	if (UCrewRosterSubsystem* Ros = Roster()) { Ros->NoteShipNews(ShipName, FString::Printf(TEXT("%s asked us for help and we turned away"), *ShipName), -0.15f); }
	Report(FString::Printf(TEXT("%s acknowledges. Captain %s says she will manage without us."), *ShipName, *S->Captain.LastName), FLinearColor(1.f, 0.75f, 0.5f));
	Dirty();
}

void UFleetSubsystem::BoardCleared()
{
	for (FFleetShip& S : Ships) { S.Mission.SupportMissionId = -1; S.Mission.AssistMissionId = -1; S.Mission.bAssistOffered = false; }
	Requests.Reset();
	Dirty();
}

void UFleetSubsystem::SelfResolved(FFleetShip& S)
{
	FFleetMission& M = S.Mission;
	if (M.SupportMissionId >= 0) { if (UMissionSubsystem* Mi = Missions()) { Mi->FleetWithdraw(M.SupportMissionId, FString::Printf(TEXT("the %s has her trouble in hand"), *S.Name)); } }
	M.Trouble = EFleetTrouble::None; M.SupportMissionId = -1;
	Requests.RemoveAll([&S](const FFleetRequest& Q) { return Q.bSupport && Q.ShipName == S.Name; });
	if (UCrewRosterSubsystem* Ros = Roster()) { Ros->NoteShipNews(S.Name, FString::Printf(TEXT("%s has her trouble in hand"), *S.Name), 0.1f); }
	Dirty();
}

void UFleetSubsystem::DropBoardEntries(FFleetShip& S)
{
	UMissionSubsystem* Mi = Missions();
	if (Mi && S.Mission.SupportMissionId >= 0) { Mi->FleetWithdraw(S.Mission.SupportMissionId, FString::Printf(TEXT("the %s is no longer asking"), *S.Name)); }
	if (Mi && S.Mission.AssistMissionId >= 0) { Mi->FleetWithdraw(S.Mission.AssistMissionId, FString::Printf(TEXT("the %s's work is over"), *S.Name)); }
	S.Mission.SupportMissionId = -1; S.Mission.AssistMissionId = -1;
	Requests.RemoveAll([&S](const FFleetRequest& Q) { return Q.ShipName == S.Name; });
}

void UFleetSubsystem::AssistResolved(const FString& ShipName, bool bSuccess)
{
	FFleetShip* S = FindMutable(ShipName);
	if (!S || S->Mission.Task != EFleetTask::OnStation) { return; }
	S->Relation = FMath::Clamp(S->Relation + (bSuccess ? 0.15f : -0.05f), -1.f, 1.f);
	if (bSuccess) { S->Mission.bAssisted = true; S->Mission.HoursLeft = FMath::Max(1.f, S->Mission.HoursLeft * 0.5f); }
	S->Mission.AssistMissionId = -1;
	Log(*S, bSuccess ? FString::Printf(TEXT("A hand lent by the %s; the work goes faster."), *(Roster() ? Roster()->GetShipName() : FString(TEXT("Enterprise")))) : TEXT("A hand lent, and not much use."));
	Dirty();
}

void UFleetSubsystem::SetSupportMission(const FString& ShipName, int32 MissionId) { if (FFleetShip* S = FindMutable(ShipName)) { S->Mission.SupportMissionId = MissionId; Dirty(); } }
void UFleetSubsystem::SetAssistMission(const FString& ShipName, int32 MissionId) { if (FFleetShip* S = FindMutable(ShipName)) { S->Mission.AssistMissionId = MissionId; if (MissionId >= 0) { S->Mission.bAssistOffered = true; } Dirty(); } }

// ---- people ----

bool UFleetSubsystem::TransferTo(const FString& ShipName, int32 CrewId, FString& OutReply)
{
	FFleetShip* S = FindMutable(ShipName);
	UCrewRosterSubsystem* Ros = Roster();
	if (!S || S->bLost) { OutReply = TEXT("No such ship in the fleet"); return false; }
	if (!Ros) { OutReply = TEXT("No roster"); return false; }
	if (!InRange(*S)) { OutReply = FString::Printf(TEXT("%s is not in hailing range"), *ShipName); return false; }
	FCrewMember P;
	if (!Ros->TransferOut(CrewId, ShipName, P, OutReply)) { return false; }
	FFleetPerson F; F.Person = P; F.Person.Id = NextPersonId++; F.Person.bHead = false; F.Person.bSecond = false; F.Person.Condition = ECrewCondition::Fit;
	F.Origin = FString::Printf(TEXT("formerly of the %s"), *Ros->GetShipName());
	// Everyone aboard us who was close to them keeps a tie (the roster gave them one too).
	for (const FCrewRelationship& R : P.Relationships) { if (R.Affinity > 0.4f && Ros->Find(R.Other)) { FFleetTie Tie; Tie.CrewId = R.Other; Tie.Relation = TEXT("old shipmate"); F.Ties.Add(Tie); } }
	S->Notables.Add(F); ++S->Complement; S->Relation = FMath::Min(1.f, S->Relation + 0.05f);
	Log(*S, FString::Printf(TEXT("%s came aboard from the %s."), *P.Styled(), *Ros->GetShipName()));
	OutReply = FString::Printf(TEXT("%s transferred to the %s, with Captain %s's thanks"), *P.Styled(), *ShipName, *S->Captain.LastName);
	Dirty();
	return true;
}

bool UFleetSubsystem::RequestPerson(const FString& ShipName, const FString& PersonName, FString& OutReply)
{
	FFleetShip* S = FindMutable(ShipName);
	UCrewRosterSubsystem* Ros = Roster();
	if (!S || S->bLost) { OutReply = TEXT("No such ship in the fleet"); return false; }
	if (!Ros) { OutReply = TEXT("No roster"); return false; }
	if (!InRange(*S)) { OutReply = FString::Printf(TEXT("%s is not in hailing range"), *ShipName); return false; }
	const FString Want = PersonName.ToLower();
	int32 Found = -1;
	for (int32 i = 0; i < S->Notables.Num(); ++i) { const FString N = S->Notables[i].Person.FullName().ToLower(); if (N == Want || N.Contains(Want)) { Found = i; break; } }
	if (Found < 0)
	{
		for (const FFleetPerson& O : S->Officers) { if (O.Person.FullName().ToLower().Contains(Want)) { OutReply = FString::Printf(TEXT("Captain %s will not part with %s"), *S->Captain.LastName, *O.Person.Styled()); return false; } }
		OutReply = FString::Printf(TEXT("Nobody called %s aboard the %s that we know of"), *PersonName, *ShipName); return false;
	}
	const FFleetPerson P = S->Notables[Found];
	// Family or a friend aboard us now (a reunion), or a captain who thinks well of us; otherwise a polite no.
	bool bReunion = false; for (const FFleetTie& T : P.Ties) { const FCrewMember* C = Ros->Find(T.CrewId); bReunion |= C && C->IsAboard(); }
	if (!bReunion && S->Relation < 0.2f) { OutReply = FString::Printf(TEXT("Captain %s declines: the %s cannot spare %s (their regard for us is %+.0f)"), *S->Captain.LastName, *ShipName, *P.Person.FullName(), S->Relation * 100.f); return false; }
	S->Notables.RemoveAt(Found);
	FCrewMember M = P.Person;
	if (!Ros->Join(M, FString::Printf(TEXT("Transferred from the %s%s."), *ShipName, bReunion ? TEXT(", to serve with their own") : TEXT("")), OutReply)) { S->Notables.Insert(P, Found); return false; }
	for (const FFleetTie& T : P.Ties) { const FCrewMember* C = Ros->Find(T.CrewId); if (C && C->IsAboard()) { Ros->TieComesAboard(T.CrewId, M.FullName(), T.Relation, ShipName); } else { Ros->TieRemoved(T.CrewId, M.FullName(), ShipName); } }
	S->Complement = FMath::Max(0, S->Complement - 1);
	S->Relation = FMath::Max(-1.f, S->Relation - (bReunion ? 0.02f : 0.1f));
	Log(*S, FString::Printf(TEXT("%s transferred to the %s."), *P.Person.Styled(), *Ros->GetShipName()));
	Dirty();
	return true;
}

void UFleetSubsystem::NoteLost(const FString& ShipName, const FString& ByWhom)
{
	FFleetShip* S = FindMutable(ShipName);
	if (!S || S->bLost) { return; }
	DropBoardEntries(*S);
	S->bLost = true; S->Mission = FFleetMission(); S->Lost += S->Complement; S->Complement = 0;
	Log(*S, FString::Printf(TEXT("Lost with all hands%s."), ByWhom.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(", %s"), *ByWhom)));
	if (UCrewRosterSubsystem* Ros = Roster())
	{
		for (const FFleetPerson& P : S->Officers) { for (const FFleetTie& T : P.Ties) { Ros->TieLost(T.CrewId, P.Person.FullName(), ShipName); } }
		for (const FFleetPerson& P : S->Notables) { for (const FFleetTie& T : P.Ties) { Ros->TieLost(T.CrewId, P.Person.FullName(), ShipName); } }
	}
	Report(FString::Printf(TEXT("%s is lost with all hands, Captain %s with her."), *ShipName, *S->Captain.FullName()), FLinearColor(1.f, 0.5f, 0.4f));
	Dirty();
}

// ---- dev ----

bool UFleetSubsystem::DevTrouble(const FString& ShipName, EFleetTrouble Kind, FString& OutReply)
{
	FFleetShip* S = FindMutable(ShipName);
	if (!S) { OutReply = TEXT("No such ship"); return false; }
	if (S->Mission.Task != EFleetTask::OnStation) { DevWork(ShipName, OutReply); }
	if (S->Mission.Trouble != EFleetTrouble::None) { OutReply = FString::Printf(TEXT("%s is already in trouble"), *ShipName); return false; }
	Trouble(*S, Kind);
	OutReply = FString::Printf(TEXT("%s: %s"), *ShipName, *S->Mission.TroubleText);
	return true;
}

bool UFleetSubsystem::DevSummon(const FString& ShipName, FString& OutReply)
{
	FFleetShip* S = FindMutable(ShipName);
	if (!S) { OutReply = TEXT("No such ship"); return false; }
	AProceduralStarship* A = S->Actor.Get();
	if (!A) { OutReply = FString::Printf(TEXT("%s has no hull in this system"), *ShipName); return false; }
	if (A->IsAway()) { A->SetAway(false); }
	if (S->Mission.IsActive()) { DropBoardEntries(*S); S->Mission = FFleetMission(); S->NextMissionHours = Hours() + 3.0; Log(*S, TEXT("Recalled.")); }
	const FVector Loc = Last.Location + FVector(1.f, 0.3f, 0.05f).GetSafeNormal() * 1.0e7;
	A->bCanWarp = false;
	if (A->IsInWarp()) { A->RecallTo(Loc, 0.5e7); OutReply = FString::Printf(TEXT("%s is alongside (her warp cut short)"), *ShipName); }
	else { A->HomeLocation = Loc; A->PatrolRadiusCm = 0.5e7; A->WarpTo(Loc); OutReply = FString::Printf(TEXT("%s is coming alongside"), *ShipName); }
	return true;
}

bool UFleetSubsystem::DevWork(const FString& ShipName, FString& OutReply)
{
	FFleetShip* S = FindMutable(ShipName);
	if (!S) { OutReply = TEXT("No such ship"); return false; }
	if (S->Mission.IsActive()) { DropBoardEntries(*S); S->Mission = FFleetMission(); }
	BeginMission(*S, true, false);   // the record works; the hull stays where it is, for testing in range
	if (!S->Mission.IsActive()) { OutReply = TEXT("Nowhere in her system to work"); return false; }
	S->Mission.Task = EFleetTask::OnStation; S->Mission.HoursLeft = S->Mission.WorkHours;
	Log(*S, FString::Printf(TEXT("Arrived at %s; %s begins."), *S->Mission.TargetName, *S->Mission.Title));
	Dirty();
	OutReply = FString::Printf(TEXT("%s: %s"), *ShipName, *S->TaskText());
	return true;
}

bool UFleetSubsystem::DevMission(const FString& ShipName, FString& OutReply)
{
	FFleetShip* S = FindMutable(ShipName);
	if (!S) { OutReply = TEXT("No such ship"); return false; }
	if (S->Mission.IsActive()) { DropBoardEntries(*S); EndMission(*S, true, TEXT("cut short")); S->Mission.Task = EFleetTask::Idle; }
	BeginMission(*S);   // the new destination replaces whatever leg the hull was on
	OutReply = FString::Printf(TEXT("%s: %s"), *ShipName, *S->TaskText());
	return true;
}
