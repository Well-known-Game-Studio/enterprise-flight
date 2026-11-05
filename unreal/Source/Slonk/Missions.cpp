// Slonk - missions: generation from orders, postings and chance; the stages and their checks; the tracker, the
// inbox and the log.

#include "Missions.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "StarSystem.h"
#include "PlanetBody.h"
#include "SpaceVessel.h"
#include "SpaceTraffic.h"
#include "StarshipPawn.h"
#include "ShipSystemsComponent.h"
#include "ShuttlecraftPawn.h"
#include "PersonPawn.h"
#include "ScienceLog.h"
#include "Fleet.h"

const TCHAR* UMissionSubsystem::SlotName = TEXT("Missions");

namespace
{
	const TCHAR* CivilianShips[] = { TEXT("Calypso"), TEXT("Meridian"), TEXT("Brightwater"), TEXT("Alderaan Dawn"), TEXT("Kestrel"), TEXT("Halcyon"), TEXT("Tanager"), TEXT("Sable Coast"), TEXT("Wayfarer"), TEXT("Elan"), TEXT("Marigold"), TEXT("Tycho's Hope") };
	const TCHAR* OrionShips[] = { TEXT("Vashti"), TEXT("Kessa"), TEXT("Zhalan"), TEXT("Oreth"), TEXT("Tamuro"), TEXT("Belisk"), TEXT("Narava"), TEXT("Qel-Dorn") };
	const TCHAR* Admirals[] = { TEXT("Admiral Nogura"), TEXT("Commodore Mendez"), TEXT("Admiral Morrow"), TEXT("Commodore Probert"), TEXT("Admiral Cartwright") };
	const TCHAR* Powers[] = { TEXT("Klingon"), TEXT("Orion"), TEXT("Romulan"), TEXT("Tholian"), TEXT("Gorn") };
	const TCHAR* Peoples[] = { TEXT("Tellarite"), TEXT("Andorian"), TEXT("Vulcan"), TEXT("Human"), TEXT("Bolian"), TEXT("Caitian") };
	template <int N> const TCHAR* Pick(FRandomStream& R, const TCHAR* const (&T)[N]) { return T[R.RandRange(0, N - 1)]; }
	FMissionOption Opt(const TCHAR* Text, ECrewSkill Skill, float Diff, ECrewDepartment Party, const TCHAR* STag, const TCHAR* FTag, const TCHAR* SText, const TCHAR* FText, int32 NextS, int32 NextF, bool bFailEnds, float Risk, int32 Science = 0)
	{
		FMissionOption O; O.Text = Text; O.Skill = Skill; O.Difficulty = Diff; O.Party = Party; O.SuccessTag = STag; O.FailTag = FTag; O.SuccessText = SText; O.FailText = FText; O.NextOnSuccess = NextS; O.NextOnFail = NextF; O.bFailEndsMission = bFailEnds; O.Risk = Risk; O.Science = Science; return O;
	}
	FMissionStage Stage(const TCHAR* Title, const FString& Text, bool bAtTarget = true) { FMissionStage S; S.Title = Title; S.Text = Text; S.bAtTarget = bAtTarget; return S; }
}

FString UMissionSubsystem::KindName(EMissionKind K)
{
	static const TCHAR* N[] = { TEXT("Survey"), TEXT("Rescue"), TEXT("Diplomacy"), TEXT("Delivery"), TEXT("Anomaly"), TEXT("Medical"), TEXT("Patrol"), TEXT("Contact"), TEXT("Derelict"), TEXT("Smuggler"), TEXT("Support"), TEXT("Assist") };
	return (int32)K < (int32)EMissionKind::Count ? N[(int32)K] : TEXT("-");
}
FString UMissionSubsystem::SourceName(EMissionSource S) { static const TCHAR* N[] = { TEXT("Starfleet"), TEXT("Starbase"), TEXT("Event"), TEXT("Fleet") }; return (int32)S < 4 ? N[(int32)S] : TEXT("-"); }
FString UMissionSubsystem::StateName(EMissionState S) { static const TCHAR* N[] = { TEXT("Offered"), TEXT("Active"), TEXT("Completed"), TEXT("Failed"), TEXT("Abandoned"), TEXT("Expired") }; return (int32)S < 6 ? N[(int32)S] : TEXT("-"); }
bool UMissionSubsystem::ParseKind(const FString& Word, EMissionKind& Out)
{
	const FString W = Word.ToLower();
	for (int32 i = 0; i < (int32)EMissionKind::Count; ++i) { if (W.Len() >= 3 && KindName((EMissionKind)i).ToLower().StartsWith(W)) { Out = (EMissionKind)i; return true; } }
	return false;
}

// ---- lifetime ----

void UMissionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Load();
	Rng.Initialize((int32)(FDateTime::Now().GetTicks() % 1000003));
}

void UMissionSubsystem::Load()
{
	Missions.Reset(); Inbox.Reset();
	if (UGameplayStatics::DoesSaveGameExist(SlotName, 0))
	{
		if (const USlonkMissionSaveGame* S = Cast<USlonkMissionSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, 0)))
		{
			Missions = S->Missions; Inbox = S->Inbox; Standing = S->Standing; NextId = S->NextId; NextOrdersHours = S->NextOrdersHours; NextEventHours = S->NextEventHours; Completed = S->Completed; Failed = S->Failed; BoardDays = S->BoardDays;
			for (FMission& M : Missions)
			{
				bool bNeedsHull = false; for (int32 i = M.Stage; i < M.Stages.Num(); ++i) { bNeedsHull |= M.Stages[i].bAtTarget; }
				if (M.IsOpen() && M.Source == EMissionSource::Event && M.TargetKind == EMissionTarget::Vessel && bNeedsHull) { M.State = EMissionState::Expired; M.ClosedStardate = M.DeadlineStardate; M.Log.Add(TEXT("      Contact lost between sessions.")); bSaveWanted = true; SaveDelaySeconds = 2.f; }
			}
			UE_LOG(LogTemp, Log, TEXT("Missions: loaded %d (%d open), standing %d"), Missions.Num(), Open().Num(), Standing);
		}
	}
	++Revision;
}

void UMissionSubsystem::Save()
{
	if (bNoDisk) { bSaveWanted = false; return; }
	USlonkMissionSaveGame* S = Cast<USlonkMissionSaveGame>(UGameplayStatics::CreateSaveGameObject(USlonkMissionSaveGame::StaticClass()));
	if (!S) { return; }
	S->Missions = Missions; S->Inbox = Inbox; S->Standing = Standing; S->NextId = NextId; S->NextOrdersHours = NextOrdersHours; S->NextEventHours = NextEventHours; S->Completed = Completed; S->Failed = Failed; S->BoardDays = BoardDays;
	UGameplayStatics::SaveGameToSlot(S, SlotName, 0);
	bSaveWanted = false;
}

void UMissionSubsystem::Reset()
{
	if (!bNoDisk) { UGameplayStatics::DeleteGameInSlot(SlotName, 0); }
	// NextId runs on: the science log keeps mission/<id> keys.
	Missions.Reset(); Inbox.Reset(); Standing = 0; Completed = 0; Failed = 0; NextOrdersHours = 0.0; NextEventHours = 0.0; BoardPostedAt.Empty(); BoardDays.Empty();
	if (UFleetSubsystem* F = Fleet()) { F->BoardCleared(); }   // no ship's record points at a mission that is gone
	for (const TPair<int32, TWeakObjectPtr<ASpaceVessel>>& H : EventHulls) { if (H.Value.IsValid() && !H.Value->IsDying()) { H.Value->Destroy(); } }
	for (const TWeakObjectPtr<ASpaceVessel>& H : RetiredHulls) { if (H.IsValid() && !H->IsDying()) { H->Destroy(); } }
	EventHulls.Empty(); RetiredHulls.Empty();
	Dirty(); Save();
}

AStarshipPawn* UMissionSubsystem::CrewedShip() const
{
	// The ship whose people these are, whether the player sits in her chair, flies her shuttle or walks a deck.
	if (!GetWorld()) { return nullptr; }
	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	AStarshipPawn* Craft = Cast<AStarshipPawn>(PlayerPawn);
	if (!Craft) { if (const APersonPawn* P = Cast<APersonPawn>(PlayerPawn)) { Craft = P->Craft.Get(); } }
	if (const AShuttlecraftPawn* Sh = Cast<AShuttlecraftPawn>(Craft)) { Craft = Sh->Mothership.Get(); }
	if (Craft && Craft->Systems && Craft->Systems->GetRoster()) { return Craft; }
	for (TActorIterator<AStarshipPawn> It(GetWorld()); It; ++It) { if (!It->IsShuttle() && It->Systems && It->Systems->GetRoster()) { return *It; } }
	return nullptr;
}

UFleetSubsystem* UMissionSubsystem::Fleet() const { if (FleetOverride.IsValid()) { return FleetOverride.Get(); } return GetGameInstance() ? GetGameInstance()->GetSubsystem<UFleetSubsystem>() : nullptr; }

void UMissionSubsystem::InitForTest(UCrewRosterSubsystem* InRoster, UFleetSubsystem* InFleet)
{
	bNoDisk = true; RosterOverride = InRoster; FleetOverride = InFleet;
	Rng.Initialize(11);
}

UCrewRosterSubsystem* UMissionSubsystem::Roster() const
{
	if (RosterOverride.IsValid()) { return RosterOverride.Get(); }
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UCrewRosterSubsystem>() : nullptr;
}
double UMissionSubsystem::Stardate() const { const UCrewRosterSubsystem* R = Roster(); return R ? R->GetStardate() : 8305.0; }
double UMissionSubsystem::Hours() const { const UCrewRosterSubsystem* R = Roster(); return R ? R->GetHours() : 0.0; }

void UMissionSubsystem::Report(const FString& Text, const FLinearColor& Color, ECrewDepartment From)
{
	FCrewReport R; R.From = From; R.Text = Text; R.Color = Color;
	Reports.Add(R);
	UE_LOG(LogTemp, Log, TEXT("Missions: %s"), *Text);
}

FString UMissionSubsystem::StandingTitle() const
{
	if (Standing >= 200) { return TEXT("the fleet's finest"); }
	if (Standing >= 120) { return TEXT("highly regarded"); }
	if (Standing >= 60) { return TEXT("well regarded"); }
	if (Standing >= 20) { return TEXT("in good standing"); }
	if (Standing >= -10) { return TEXT("unremarked"); }
	if (Standing >= -40) { return TEXT("under a cloud"); }
	return TEXT("facing a board of inquiry");
}

int32 UMissionSubsystem::Unread() const { int32 N = 0; for (const FStarfleetMessage& M : Inbox) { if (!M.bRead) { ++N; } } return N; }
void UMissionSubsystem::MarkRead(int32 i) { if (Inbox.IsValidIndex(i) && !Inbox[i].bRead) { Inbox[i].bRead = true; Dirty(); } }

const FMission* UMissionSubsystem::Find(int32 Id) const { for (const FMission& M : Missions) { if (M.Id == Id) { return &M; } } return nullptr; }
TArray<const FMission*> UMissionSubsystem::Open() const { TArray<const FMission*> Out; for (const FMission& M : Missions) { if (M.IsOpen()) { Out.Add(&M); } } return Out; }
TArray<const FMission*> UMissionSubsystem::Active() const { TArray<const FMission*> Out; for (const FMission& M : Missions) { if (M.State == EMissionState::Active) { Out.Add(&M); } } return Out; }

const FMission* UMissionSubsystem::Objective() const
{
	const FMission* Best = nullptr;
	for (const FMission& M : Missions) { if (M.State == EMissionState::Active && (!Best || M.DeadlineStardate < Best->DeadlineStardate)) { Best = &M; } }
	return Best;
}

// ---- the ship's tick ----

void UMissionSubsystem::AdvanceSeconds(float Seconds, const FMissionShipState& Ship)
{
	Last = Ship;
	if (UFleetSubsystem* F = Fleet())
	{
		F->AdvanceSeconds(Seconds, Ship);
		TArray<FFleetRequest> Q; F->DrainRequests(Q);
		for (const FFleetRequest& R : Q) { int32 Calls = 0; for (const FMission& M : Missions) { if (M.IsOpen() && M.Kind == EMissionKind::Support) { ++Calls; } } if (Calls < 2) { IssueFleetMission(R); } }
		TArray<FCrewReport> Rep; F->DrainReports(Rep); Reports.Append(Rep);
		OfferAssists();
	}
	const double H = Hours();
	if (NextOrdersHours <= 0.0) { NextOrdersHours = H + Rng.FRandRange(2.0, 5.0); }
	if (NextEventHours <= 0.0) { NextEventHours = H + Rng.FRandRange(0.5, 1.5); }
	// Starfleet's orders come over subspace every several ship's hours while fewer than two of theirs are open.
	if (H >= NextOrdersHours)
	{
		NextOrdersHours = H + Rng.FRandRange(6.0, 14.0);
		int32 Theirs = 0; for (const FMission& M : Missions) { if (M.IsOpen() && M.Source == EMissionSource::Starfleet) { ++Theirs; } }
		if (Theirs < 2) { IssueOrders(); }
	}
	// A starbase's board, once per arrival.
	if (Ship.bAlongside && !Ship.StationName.IsEmpty() && Ship.StationName != BoardPostedAt) { BoardPostedAt = Ship.StationName; PostBoard(Ship.StationName); }
	else if (!Ship.bAlongside) { BoardPostedAt.Empty(); }
	// Space: something happens now and then while under way and not already dealing with one.
	if (H >= NextEventHours)
	{
		NextEventHours = H + Rng.FRandRange(1.0, 3.0);
		int32 Events = 0; for (const FMission& M : Missions) { if (M.IsOpen() && M.Source == EMissionSource::Event) { ++Events; } }
		if (!Ship.bAlongside && !Ship.bUnderFire && !Ship.bAtWarp && Events == 0) { RollEvent(); }
	}
	Expire();
	if (bSaveWanted) { SaveDelaySeconds -= Seconds; if (SaveDelaySeconds <= 0.f) { Save(); } }
}

void UMissionSubsystem::Expire()
{
	const double SD = Stardate();
	for (FMission& M : Missions)
	{
		if (!M.IsOpen() || M.DeadlineStardate <= 0.0 || SD <= M.DeadlineStardate) { continue; }
		if (M.State == EMissionState::Offered || M.Kind == EMissionKind::Assist)
		{
			Close(M, EMissionState::Expired, M.Kind == EMissionKind::Assist ? TEXT("their work went on without us") : TEXT("the offer lapsed"));
			if (M.Kind == EMissionKind::Assist) { if (UFleetSubsystem* F = Fleet()) { F->SetAssistMission(M.TargetName, -1); } }   // her file no longer points at it
			if (M.Source == EMissionSource::Starfleet) { Standing -= M.Standing / 4; Report(FString::Printf(TEXT("%s: Starfleet's orders went unanswered. Noted at Command."), *M.Title), FLinearColor(1.f, 0.75f, 0.5f), ECrewDepartment::Operations); }
		}
		else
		{
			Close(M, EMissionState::Failed, TEXT("the deadline passed"));
			Standing -= M.Standing / 2; ++Failed;
			if (M.Kind == EMissionKind::Support) { if (UFleetSubsystem* F = Fleet()) { F->SupportResolved(M.TargetName, false, true); } }
			Report(FString::Printf(TEXT("%s: the deadline has passed. Starfleet will want to know why."), *M.Title), FLinearColor(1.f, 0.6f, 0.5f));
			if (UCrewRosterSubsystem* R = Roster()) { FCrewExperience E; E.Tag = M.Kind == EMissionKind::Rescue || M.Kind == EMissionKind::Medical ? TEXT("Loss.Civilians") : TEXT("Order.Refused"); E.Intensity = 0.5f; E.Place = M.TargetName; E.Group = M.Group; E.Text = FString::Printf(TEXT("%s left undone"), *M.Title); E.bCaptainsChoice = true; R->Experience(E); }
		}
	}
}

void UMissionSubsystem::Close(FMission& M, EMissionState State, const FString& Why)
{
	M.State = State; M.ClosedStardate = Stardate();
	M.Log.Add(FString::Printf(TEXT("%.1f  %s: %s."), Stardate(), *StateName(State), *Why));
	if (M.Source == EMissionSource::Event && M.TargetKind == EMissionTarget::Vessel) { RetireHull(M.Id); }
	Dirty();
}

// ---- generation ----

FString UMissionSubsystem::CivilianName(FRandomStream& R) const
{
	return UniqueName(R, [&R]() { return FString::Printf(TEXT("S.S. %s"), Pick(R, CivilianShips)); });
}

bool UMissionSubsystem::PickTarget(FMission& M, FRandomStream& R)
{
	// The galaxy's own names: a world with a surface in another system for a survey or a plague, a station for a
	// delivery or a parley, a system for a patrol; never where the ship is.
	switch (M.Kind) { case EMissionKind::Survey: case EMissionKind::Medical: case EMissionKind::Delivery: case EMissionKind::Diplomacy: case EMissionKind::Patrol: break; default: return true; }   // the events bring their own target
	UWorld* W = GetWorld();
	if (!W) { return false; }
	TArray<AStarSystem*> Systems; for (TActorIterator<AStarSystem> It(W); It; ++It) { Systems.Add(*It); }
	TArray<ASpaceStation*> Stations; for (TActorIterator<ASpaceStation> It(W); It; ++It) { Stations.Add(*It); }
	auto World = [&]() -> bool
	{
		TArray<APlanetBody*> Worlds;
		for (AStarSystem* S : Systems) { if (S->SystemName == Last.SystemName && Systems.Num() > 1) { continue; } for (APlanetBody* P : S->Planets) { if (P && P->HasSolidSurface() && !P->IsMoon()) { Worlds.Add(P); } } }
		if (Worlds.Num() == 0) { return false; }
		M.TargetKind = EMissionTarget::World; M.TargetName = Worlds[R.RandRange(0, Worlds.Num() - 1)]->GetDisplayName(); return true;
	};
	auto Station = [&]() -> bool
	{
		TArray<ASpaceStation*> Others; for (ASpaceStation* S : Stations) { if (S->GetDisplayName() != Last.StationName) { Others.Add(S); } }
		if (Others.Num() == 0) { return false; }
		M.TargetKind = EMissionTarget::Station; M.TargetName = Others[R.RandRange(0, Others.Num() - 1)]->GetDisplayName(); return true;
	};
	auto System = [&]() -> bool
	{
		TArray<AStarSystem*> Others; for (AStarSystem* S : Systems) { if (S->SystemName != Last.SystemName) { Others.Add(S); } }
		if (Others.Num() == 0) { return false; }
		M.TargetKind = EMissionTarget::System; M.TargetName = Others[R.RandRange(0, Others.Num() - 1)]->SystemName; return true;
	};
	switch (M.Kind)
	{
	case EMissionKind::Survey: case EMissionKind::Medical: return World();
	case EMissionKind::Delivery: return R.FRand() < 0.6f ? Station() : World();
	case EMissionKind::Diplomacy: return R.FRand() < 0.5f ? Station() : World();
	case EMissionKind::Patrol: return System();
	default: return true;   // the events bring their own target
	}
}

FMission UMissionSubsystem::MakeMission(EMissionKind Kind, EMissionSource Source, FRandomStream& R)
{
	FMission M; M.Id = NextId++; M.Kind = Kind; M.Source = Source; M.OfferedStardate = Stardate();
	M.From = Source == EMissionSource::Starfleet ? Pick(R, Admirals) : Source == EMissionSource::Starbase ? FString::Printf(TEXT("%s operations"), *Last.StationName) : TEXT("the bridge");
	const double Days = R.FRandRange(4.0, 9.0);
	M.DeadlineStardate = Stardate() + Days;
	if (!PickTarget(M, R)) { M.Id = -1; return M; }   // nowhere to send it: the caller drops it
	const FString T = M.TargetName.IsEmpty() ? TEXT("here") : M.TargetName;
	using S = ECrewSkill; using D = ECrewDepartment;
	switch (Kind)
	{
	case EMissionKind::Survey:
	{
		M.Title = FString::Printf(TEXT("Survey %s"), *T); M.Standing = 10;
		M.Brief = FString::Printf(TEXT("Starfleet wants %s charted: a full sensor survey and, if the ground allows, a landing party's samples. Low risk, some science."), *T);
		FMissionStage S0 = Stage(TEXT("In orbit"), FString::Printf(TEXT("%s turns below. The science watch is ready; the question is how close to go."), *T));
		S0.Options.Add(Opt(TEXT("A full sensor sweep from orbit"), S::Science, 42.f, D::Science, TEXT("Discovery"), TEXT(""), TEXT("The sweep maps her end to end; the catalogue grows by a world."), TEXT("The sweep is patchy: the sensors fight the atmosphere and the map has holes."), 1, 1, false, 0.f, 120));
		S0.Options.Add(Opt(TEXT("Send a landing party for samples"), S::Science, 55.f, D::Science, TEXT("Away.Routine"), TEXT("Away.Danger"), TEXT("The party is down and back with cases full; the sweep comes with it."), TEXT("The ground is worse than it looked; the party comes back short of samples and shaken."), 1, 1, false, 0.3f, 200));
		FMissionStage S1 = Stage(TEXT("Findings"), TEXT("The data is in. Now it is a matter of what to make of it before Starfleet sees it."), false);
		S1.Options.Add(Opt(TEXT("Catalogue and file the report"), S::Science, 38.f, D::Science, TEXT("Discovery"), TEXT(""), TEXT("A clean report; Starfleet's cartographers are pleased."), TEXT("The report goes in late and thin."), -1, -1, false, 0.f, 60));
		M.Stages = { S0, S1 }; break;
	}
	case EMissionKind::Rescue:
	{
		const FString Ship = CivilianName(R);
		M.Title = FString::Printf(TEXT("Distress call: the %s"), *Ship); M.Standing = 15; M.TargetKind = EMissionTarget::Vessel; M.TargetName = Ship; M.Group = TEXT("Federation");
		M.Brief = FString::Printf(TEXT("A civilian freighter, the %s, is calling for help: a reactor fault, her crew in the forward sections with the air going. She is close."), *Ship);
		FMissionStage S0 = Stage(TEXT("Alongside"), TEXT("She is venting from the stern and her crew are on the wrong side of a bulkhead. There are minutes, not hours."));
		S0.Options.Add(Opt(TEXT("Beam them across through the interference"), S::Operations, 52.f, D::Operations, TEXT("Rescue"), TEXT("Loss.Civilians"), TEXT("Every one of them comes through the pattern buffers, coughing and alive."), TEXT("The transporter cannot hold a lock through the leak; by the time the lock holds, not all of them are breathing."), 1, 1, false, 0.1f));
		S0.Options.Add(Opt(TEXT("Send a damage-control party across"), S::Engineering, 56.f, D::Engineering, TEXT("Rescue"), TEXT("Away.Danger"), TEXT("The party cuts through, gets the air back and walks them out."), TEXT("The party gets them out, but not before the deck came apart around them."), 1, 1, false, 0.4f));
		S0.Options.Add(Opt(TEXT("Tractor her and tow her clear first"), S::Helm, 48.f, D::Helm, TEXT("Rescue"), TEXT("Loss.Civilians"), TEXT("Towed clear of her own leak, she holds together long enough."), TEXT("The tow pulls her apart; the stern goes with two of her crew."), 1, 1, false, 0.f));
		FMissionStage S1 = Stage(TEXT("Sickbay"), TEXT("Her crew are aboard, most of them hurt. Sickbay has them."), false);
		S1.Options.Add(Opt(TEXT("Treat them and set them down at the nearest port"), S::Medicine, 45.f, D::Medical, TEXT("Rescue"), TEXT("Loss.Civilians"), TEXT("They will all live. The captain of the freighter asks the ship's name."), TEXT("Two of them do not make it; the rest go ashore."), -1, -1, false, 0.f));
		M.Stages = { S0, S1 }; break;
	}
	case EMissionKind::Diplomacy:
	{
		M.Group = R.FRand() < 0.5f ? Pick(R, Powers) : Pick(R, Peoples);
		M.Title = FString::Printf(TEXT("Talks with the %ss at %s"), *M.Group.ToString(), *T); M.Standing = 20;
		M.Brief = FString::Printf(TEXT("A dispute at %s with a %s delegation has gone past what the local administrator can settle. Starfleet wants it settled without a shot. How you deal with them will be remembered on both sides."), *T, *M.Group.ToString());
		FMissionStage S0 = Stage(TEXT("Opening"), FString::Printf(TEXT("The %s delegation has come aboard. They are formal, wary, and watching who you bring to the table."), *M.Group.ToString()));
		S0.Options.Add(Opt(TEXT("Formal talks, by the book"), S::Diplomacy, 52.f, D::Command, TEXT("Parley.Held"), TEXT("Parley.Failed"), TEXT("The forms are observed and the room warms a degree."), TEXT("A form is missed and they take it as a slight."), 1, 1, false, 0.f));
		S0.Options.Add(Opt(TEXT("A show of the ship's strength first"), S::Tactical, 48.f, D::Tactical, TEXT("Contact.Hostile"), TEXT("Parley.Failed"), TEXT("They note the phaser banks and come to the table quieter."), TEXT("They note the phaser banks and walk out."), 1, -1, true, 0.f));
		S0.Options.Add(Opt(TEXT("Back channels through the crew"), S::Command, 50.f, D::Command, TEXT("Contact.Friendly"), TEXT("Parley.Failed"), TEXT("Someone aboard knows someone; the real talk happens in the mess."), TEXT("The back channel is noticed and resented."), 1, 1, false, 0.f));
		FMissionStage S1 = Stage(TEXT("Terms"), TEXT("Terms are on the table. The last day is the hard one."), false);
		S1.Options.Add(Opt(TEXT("Press for a settlement"), S::Diplomacy, 58.f, D::Command, TEXT("Parley.Held"), TEXT("Parley.Failed"), TEXT("Signed. Starfleet will hear of it from both sides, kindly."), TEXT("They sign nothing and leave with the dispute intact."), -1, -1, false, 0.f));
		M.Stages = { S0, S1 }; break;
	}
	case EMissionKind::Delivery:
	{
		M.Title = FString::Printf(TEXT("Supplies for %s"), *T); M.Standing = 8;
		M.Brief = FString::Printf(TEXT("%s needs medical supplies and spare parts the ship can carry. A milk run, unless something gets in the way."), *T);
		FMissionStage S0 = Stage(TEXT("Delivery"), FString::Printf(TEXT("%s is in range. The cargo is on the transporter pads."), *T));
		S0.Options.Add(Opt(TEXT("Beam the cargo down"), S::Operations, 35.f, D::Operations, TEXT("Shore"), TEXT(""), TEXT("Delivered, signed for, and thanks sent."), TEXT("A pad fault spoils a third of it; the rest is delivered with apologies."), -1, -1, false, 0.f));
		M.Stages = { S0 }; break;
	}
	case EMissionKind::Anomaly:
	{
		M.Title = TEXT("Sensor anomaly"); M.Standing = 6; M.TargetKind = EMissionTarget::None;
		M.Brief = TEXT("The science station reports something the sensors cannot classify: a region of space that returns readings it should not. It is close enough to look at.");
		FMissionStage S0 = Stage(TEXT("Readings"), TEXT("The readings are unlike anything in the catalogue. Closer, they get stranger."), false);
		S0.Options.Add(Opt(TEXT("Take the ship in for a close scan"), S::Science, 50.f, D::Science, TEXT("Discovery"), TEXT("Away.Danger"), TEXT("The sensors come back with something new under the sun: a paper's worth."), TEXT("The ship takes a buffeting and the sensors come back with noise."), -1, -1, false, 0.15f, 250));
		S0.Options.Add(Opt(TEXT("Launch a probe and keep clear"), S::Operations, 35.f, D::Science, TEXT("Discovery"), TEXT(""), TEXT("The probe returns clean data from a safe distance."), TEXT("The probe is lost; the data is thin."), -1, -1, false, 0.f, 100));
		S0.Options.Add(Opt(TEXT("Steer clear and log it"), S::Helm, 25.f, D::Helm, TEXT(""), TEXT(""), TEXT("Logged for someone braver."), TEXT("Logged."), -1, -1, false, 0.f, 20));
		M.Stages = { S0 }; break;
	}
	case EMissionKind::Medical:
	{
		M.Title = FString::Printf(TEXT("Outbreak on %s"), *T); M.Standing = 18;
		M.Brief = FString::Printf(TEXT("The colony on %s reports a fever moving through the settlements faster than its clinic can answer. The ship's sickbay is the nearest help."), *T);
		FMissionStage S0 = Stage(TEXT("The colony"), TEXT("The clinic is overrun. The doctors want to go down; the risk is the ship's."));
		S0.Options.Add(Opt(TEXT("Send the medical staff down"), S::Medicine, 55.f, D::Medical, TEXT("Rescue"), TEXT("Loss.Civilians"), TEXT("The fever breaks under the ship's doctors; the colony counts its lucky."), TEXT("The fever is ahead of them; the colony buries more than it should have."), 1, 1, false, 0.2f));
		S0.Options.Add(Opt(TEXT("Treat from orbit: supplies and protocols"), S::Medicine, 45.f, D::Medical, TEXT("Rescue"), TEXT("Loss.Civilians"), TEXT("The protocols hold it; slower, but nobody from the ship is exposed."), TEXT("From orbit it is not enough."), 1, 1, false, 0.f));
		FMissionStage S1 = Stage(TEXT("Containment"), TEXT("The fever has a source. Finding it is science's."), false);
		S1.Options.Add(Opt(TEXT("Trace the source"), S::Science, 50.f, D::Science, TEXT("Discovery"), TEXT(""), TEXT("A contaminated well; capped. The colony will not see it again."), TEXT("The source is not found; it will be back."), -1, -1, false, 0.f, 80));
		M.Stages = { S0, S1 }; break;
	}
	case EMissionKind::Patrol:
	{
		M.Group = Pick(R, Powers);
		M.Title = FString::Printf(TEXT("Patrol the %s system"), *T); M.Standing = 12;
		M.Brief = FString::Printf(TEXT("Starfleet has reports of %s activity near %s. Show the flag, sweep the system, and deal with what you find without starting a war."), *M.Group.ToString(), *T);
		FMissionStage S0 = Stage(TEXT("Sweep"), FString::Printf(TEXT("%s, swept once; the sensors have a contact at the edge of range that will not answer hails at once."), *T));
		S0.Options.Add(Opt(TEXT("Hail them and hold position"), S::Diplomacy, 46.f, D::Command, TEXT("Contact.Friendly"), TEXT("Contact.Hostile"), TEXT("They answer: a survey ship, lost and embarrassed. Courtesies are exchanged."), TEXT("They answer with a sensor lock and leave at speed."), -1, -1, false, 0.f));
		S0.Options.Add(Opt(TEXT("Close and warn them off"), S::Tactical, 50.f, D::Tactical, TEXT("Contact.Hostile"), TEXT("Contact.Hostile"), TEXT("They go, and Starfleet notes the border held."), TEXT("They go, after an exchange that cost paint on both sides."), -1, -1, false, 0.1f));
		M.Stages = { S0 }; break;
	}
	case EMissionKind::Contact:
	{
		M.Title = TEXT("An unknown vessel"); M.Standing = 25; M.TargetKind = EMissionTarget::Vessel; M.TargetName = FString::Printf(TEXT("Unknown contact %d"), M.Id); M.Group = TEXT("Unknown");
		M.Brief = TEXT("A ship of no known design holds station off the bow. It is not hailing, not fleeing, not firing. Nobody has seen its like.");
		FMissionStage S0 = Stage(TEXT("First contact"), TEXT("It waits. Whatever happens in the next hour will be in the histories."));
		S0.Options.Add(Opt(TEXT("Hail on all frequencies, every protocol"), S::Diplomacy, 60.f, D::Command, TEXT("Contact.First"), TEXT("Contact.Hostile"), TEXT("A reply: mathematics, then a greeting. First contact, and it holds."), TEXT("No reply they understand; it leaves, and the chance with it."), -1, -1, true, 0.f, 300));
		S0.Options.Add(Opt(TEXT("Observe and record before speaking"), S::Science, 50.f, D::Science, TEXT("Contact.First"), TEXT(""), TEXT("Enough is learned to speak well; the greeting lands."), TEXT("They tire of being watched and go."), -1, -1, true, 0.f, 200));
		M.Stages = { S0 }; break;
	}
	case EMissionKind::Derelict:
	{
		const FString Ship = CivilianName(R);
		M.Title = FString::Printf(TEXT("Derelict: the %s"), *Ship); M.Standing = 8; M.TargetKind = EMissionTarget::Vessel; M.TargetName = Ship;
		M.Brief = FString::Printf(TEXT("A hull without power drifts across the course: the %s, lost from the registers some years ago. Dark, cold, and worth a look."), *Ship);
		FMissionStage S0 = Stage(TEXT("Dark hull"), TEXT("Her airlocks will take a boarding party. What took her crew may still be aboard."));
		S0.Options.Add(Opt(TEXT("Board her with a security party"), S::Security, 50.f, D::Security, TEXT("Away.Routine"), TEXT("Away.Danger"), TEXT("Her logs and a hold of salvage come back with the party; her crew's fate with them."), TEXT("Something in the dark; the party comes back fewer and faster."), -1, -1, false, 0.45f, 120));
		S0.Options.Add(Opt(TEXT("Scan her from range and move on"), S::Science, 38.f, D::Science, TEXT("Discovery"), TEXT(""), TEXT("Her logs are read from here; the registers are updated."), TEXT("Too cold to read; the mystery stays."), -1, -1, false, 0.f, 60));
		M.Stages = { S0 }; break;
	}
	case EMissionKind::Smuggler:
	{
		M.Title = TEXT("An Orion freighter"); M.Standing = 8; M.TargetKind = EMissionTarget::Vessel; M.TargetName = UniqueName(R, [&R]() { return FString::Printf(TEXT("Orion freighter %s"), Pick(R, OrionShips)); }); M.Group = TEXT("Orion");
		M.Brief = TEXT("An Orion freighter crosses the course with her transponder dark and her cargo manifest a work of fiction. She is within the ship's right to inspect.");
		FMissionStage S0 = Stage(TEXT("Inspection"), TEXT("She heaves to, slowly, and her master invites the ship aboard with a smile that means nothing."));
		S0.Options.Add(Opt(TEXT("Board and inspect her holds"), S::Security, 52.f, D::Security, TEXT("Contact.Hostile"), TEXT("Away.Danger"), TEXT("Contraband, impounded; her master, charming to the last, is handed to the next starbase."), TEXT("The holds are clean and the party is kept waiting an hour for the pleasure of it."), -1, -1, false, 0.25f));
		S0.Options.Add(Opt(TEXT("Hail her master and talk"), S::Diplomacy, 45.f, D::Command, TEXT("Contact.Friendly"), TEXT("Contact.Hostile"), TEXT("He talks; nothing is found, and a name is noted for next time."), TEXT("He talks around every question and goes on his way."), -1, -1, false, 0.f));
		S0.Options.Add(Opt(TEXT("Let her go"), S::Command, 20.f, D::Command, TEXT("Combat.ShipSpared"), TEXT(""), TEXT("She goes. The security watch has opinions."), TEXT("She goes."), -1, -1, false, 0.f));
		M.Stages = { S0 }; break;
	}
	default: break;
	}
	return M;
}

void UMissionSubsystem::IssueOrders()
{
	FRandomStream R(Rng.RandRange(1, 1 << 20));
	static const EMissionKind Kinds[] = { EMissionKind::Survey, EMissionKind::Diplomacy, EMissionKind::Medical, EMissionKind::Patrol, EMissionKind::Survey, EMissionKind::Delivery };
	FMission M = MakeMission(Kinds[R.RandRange(0, UE_ARRAY_COUNT(Kinds) - 1)], EMissionSource::Starfleet, R);
	if (M.Id < 0) { return; }
	M.State = EMissionState::Offered;
	Missions.Add(M);
	FStarfleetMessage Msg; Msg.Stardate = Stardate(); Msg.From = M.From; Msg.Subject = M.Title; Msg.MissionId = M.Id;
	Msg.Text = FString::Printf(TEXT("To the %s. %s Report completion by stardate %.1f. %s, Starfleet Command."), *(Roster() ? Roster()->GetShipName() : FString(TEXT("Enterprise"))), *M.Brief, M.DeadlineStardate, *M.From);
	Inbox.Add(Msg);
	Report(FString::Printf(TEXT("Subspace message from Starfleet Command, Captain: %s. In your inbox."), *M.Title), FLinearColor(0.75f, 0.9f, 1.f), ECrewDepartment::Operations);
	Dirty();
}

void UMissionSubsystem::PostBoard(const FString& StationName)
{
	FRandomStream R(GetTypeHash(StationName) ^ (int32)(Hours() / 24.0));
	static const EMissionKind Kinds[] = { EMissionKind::Delivery, EMissionKind::Survey, EMissionKind::Diplomacy, EMissionKind::Medical };
	const int32 Day = (int32)(Hours() / 24.0);
	if (const int32* Posted = BoardDays.Find(StationName)) { if (*Posted == Day) { return; } }
	BoardDays.Add(StationName, Day);
	int32 Posted = 0;
	for (int32 i = 0; i < 3; ++i)
	{
		FMission M = MakeMission(Kinds[R.RandRange(0, UE_ARRAY_COUNT(Kinds) - 1)], EMissionSource::Starbase, R);
		if (M.Id < 0) { continue; }
		M.State = EMissionState::Offered; M.PostedAt = StationName; M.DeadlineStardate = Stardate() + R.FRandRange(3.0, 6.0);
		Missions.Add(M); ++Posted;
	}
	if (Posted > 0) { Report(FString::Printf(TEXT("%s has %d assignments posted for any ship that will take them; they are on the mission board."), *StationName, Posted), FLinearColor(0.75f, 0.9f, 1.f), ECrewDepartment::Operations); }
	Dirty();
}

void UMissionSubsystem::RollEvent()
{
	FRandomStream R(Rng.RandRange(1, 1 << 20));
	const float X = R.FRand();
	const EMissionKind Kind = X < 0.3f ? EMissionKind::Anomaly : X < 0.55f ? EMissionKind::Rescue : X < 0.75f ? EMissionKind::Derelict : X < 0.92f ? EMissionKind::Smuggler : EMissionKind::Contact;
	FMission M = MakeMission(Kind, EMissionSource::Event, R);
	M.State = EMissionState::Active; M.DeadlineStardate = Stardate() + 1.0;
	M.Log.Add(FString::Printf(TEXT("%.1f  %s"), Stardate(), *M.Brief));
	if (M.TargetKind == EMissionTarget::Vessel) { SpawnEventVessel(M, M.TargetName); }
	Missions.Add(M);
	Report(FString::Printf(TEXT("%s. It is on the mission board, Captain."), *M.Brief), FLinearColor(1.f, 0.85f, 0.5f), Kind == EMissionKind::Anomaly || Kind == EMissionKind::Contact ? ECrewDepartment::Science : ECrewDepartment::Operations);
	Dirty();
}

void UMissionSubsystem::SpawnEventVessel(FMission& M, const FString& Name)
{
	// A hull a few hundred kilometres off the bow, holding station, named for the mission.
	UWorld* W = GetWorld();
	if (!W) { return; }
	TActorIterator<ASpaceTraffic> It(W); ASpaceTraffic* Traffic = It ? *It : nullptr;
	if (!Traffic) { return; }
	const FVector Dir = FVector(Rng.FRandRange(0.6f, 1.f), Rng.FRandRange(-0.5f, 0.5f), Rng.FRandRange(-0.2f, 0.2f)).GetSafeNormal();
	const FVector Loc = Last.Location + Dir * Rng.FRandRange(0.8e7, 1.8e7);   // within the 300 km the ship counts as nearby
	if (ASpaceVessel* V = Traffic->SpawnNamedShip(Name, Loc, M.Kind != EMissionKind::Contact)) { EventHulls.Add(M.Id, V); }
}

void UMissionSubsystem::RetireHull(int32 MissionId)
{
	// A closed encounter's hull stays a while (the freighter limping, the derelict drifting); the oldest beyond
	// three leave the world, so the encounters do not pile up hulls for the rest of the session.
	TWeakObjectPtr<ASpaceVessel> V;
	if (EventHulls.RemoveAndCopyValue(MissionId, V) && V.IsValid()) { RetiredHulls.Add(V); }
	while (RetiredHulls.Num() > 3)
	{
		TWeakObjectPtr<ASpaceVessel> Old = RetiredHulls[0]; RetiredHulls.RemoveAt(0);
		if (Old.IsValid() && !Old->IsDying()) { Old->Destroy(); }
	}
}

FString UMissionSubsystem::UniqueName(FRandomStream& R, TFunction<FString()> Make) const
{
	FString N;
	for (int32 Try = 0; Try < 24; ++Try)
	{
		N = Make();
		bool bUsed = false; for (const FMission& M : Missions) { bUsed |= M.TargetName == N; }
		if (!bUsed) { return N; }
	}
	return FString::Printf(TEXT("%s (%d)"), *N, NextId);
}

void UMissionSubsystem::IssueFleetMission(const FFleetRequest& Q)
{
	using S = ECrewSkill; using D = ECrewDepartment;
	FMission M; M.Id = NextId++; M.Source = EMissionSource::Fleet; M.OfferedStardate = Stardate();
	M.TargetKind = EMissionTarget::Vessel; M.TargetName = Q.ShipName; M.Group = TEXT("Federation"); M.From = Q.ShipName;
	if (Q.bSupport)
	{
		// Still in that trouble? A long jump of the clock can queue a call her own people have since answered.
		const FFleetShip* FS = Fleet() ? Fleet()->Find(Q.ShipName) : nullptr;
		if (!FS || FS->bLost || FS->Mission.Task != EFleetTask::OnStation || FS->Mission.Trouble != Q.Trouble || FS->Mission.TroubleText != Q.Text) { return; }
		const float Left = FMath::Max(1.f, 24.f - FS->Mission.TroubleHours);
		M.Kind = EMissionKind::Support; M.Standing = 15; M.State = EMissionState::Active; M.DeadlineStardate = Stardate() + Left / 24.0;
		M.Title = FString::Printf(TEXT("Support the %s"), *Q.ShipName);
		M.Brief = FString::Printf(TEXT("The %s, on %s, signals that %s. She is asking for support from any ship that can reach her within the day."), *Q.ShipName, *Q.MissionTitle, *Q.Text);
		FMissionStage S0 = Stage(TEXT("Alongside"), FString::Printf(TEXT("The %s is close enough to help. %s."), *Q.ShipName, *Q.Text));
		switch (Q.Trouble)
		{
		case EFleetTrouble::Engineering:
			S0.Options.Add(Opt(TEXT("Send a damage-control party across"), S::Engineering, 55.f, D::Engineering, TEXT("Rescue"), TEXT("Away.Danger"), TEXT("Our engineers have her core stable and her lights back; her captain sends his thanks."), TEXT("The party cannot get at it in time and comes back burned."), -1, -1, true, 0.3f));
			S0.Options.Add(Opt(TEXT("Tractor her clear and feed her power"), S::Helm, 48.f, D::Helm, TEXT("Rescue"), TEXT(""), TEXT("Towed clear with a power lead across, she has time to put it right herself."), TEXT("The tow parts and the lead with it; she is no better off."), -1, -1, true, 0.f));
			break;
		case EFleetTrouble::Medical:
			S0.Options.Add(Opt(TEXT("Send the medical staff across"), S::Medicine, 55.f, D::Medical, TEXT("Rescue"), TEXT("Loss.Civilians"), TEXT("Our doctors turn her sickbay around; the worst of it is over by the next watch."), TEXT("It is beyond our people too; they come back having buried some of hers."), -1, -1, true, 0.2f));
			S0.Options.Add(Opt(TEXT("Supplies and protocols by transporter"), S::Medicine, 45.f, D::Medical, TEXT("Rescue"), TEXT("Loss.Civilians"), TEXT("With our stores and a protocol that works, her own surgeon gets ahead of it."), TEXT("Not enough, not in time."), -1, -1, true, 0.f));
			break;
		case EFleetTrouble::Hostiles:
			S0.Options.Add(Opt(TEXT("Close and drive them off"), S::Tactical, 55.f, D::Tactical, TEXT("Rescue"), TEXT("Combat.FriendlyLost"), TEXT("Two Starfleet ships are more than they bargained for; they break off and run."), TEXT("They do not run; the exchange costs both ships before they go."), -1, -1, true, 0.2f));
			S0.Options.Add(Opt(TEXT("Hail them and talk them down"), S::Diplomacy, 55.f, D::Command, TEXT("Parley.Held"), TEXT("Parley.Failed"), TEXT("Talked out of it, with a face-saving line about salvage law."), TEXT("They are not in a talking mood, and she takes another hit while we try."), -1, -1, true, 0.f));
			break;
		default:
			S0.Options.Add(Opt(TEXT("Beam a security party across"), S::Security, 55.f, D::Security, TEXT("Rescue"), TEXT("Away.Danger"), TEXT("Our people reach hers and bring everyone out together."), TEXT("The party is caught in the same trap and gets out by the skin of its teeth."), -1, -1, true, 0.4f));
			S0.Options.Add(Opt(TEXT("Lock on through the interference and beam her people out"), S::Operations, 52.f, D::Operations, TEXT("Rescue"), TEXT("Loss.Civilians"), TEXT("The transporter chief finds them where hers could not; every one comes through."), TEXT("The lock will not hold; not all of them make it."), -1, -1, true, 0.1f));
			break;
		}
		M.Stages = { S0 };
		M.Log.Add(FString::Printf(TEXT("%.1f  %s"), Stardate(), *M.Brief));
		if (UFleetSubsystem* F = Fleet()) { F->SetSupportMission(Q.ShipName, M.Id); }
	}
	else
	{
		M.Kind = EMissionKind::Assist; M.Standing = 6; M.State = EMissionState::Offered; M.DeadlineStardate = Stardate() + FMath::Max(1.f, Q.HoursLeft) / 24.0;
		M.Title = FString::Printf(TEXT("Lend a hand: the %s"), *Q.ShipName);
		M.Brief = FString::Printf(TEXT("The %s is at work nearby, %s. We are close enough to share it; her captain would not say no."), *Q.ShipName, *Q.MissionTitle);
		FMissionStage S0 = Stage(TEXT("Alongside"), FString::Printf(TEXT("The %s has her hands full with %s."), *Q.ShipName, *Q.MissionTitle));
		switch (Q.TheirKind)
		{
		case EMissionKind::Survey: S0.Options.Add(Opt(TEXT("Put the science watch on it with hers"), S::Science, 45.f, D::Science, TEXT("Contact.Friendly"), TEXT(""), TEXT("Two ships' sensors finish the chart in half the time."), TEXT("Our data does not match hers and costs her a day."), -1, -1, false, 0.f, 60)); break;
		case EMissionKind::Diplomacy: S0.Options.Add(Opt(TEXT("Sit in on the talks"), S::Diplomacy, 50.f, D::Command, TEXT("Contact.Friendly"), TEXT("Parley.Failed"), TEXT("A second captain at the table changes the room."), TEXT("Two captains at the table and the other side plays them off each other."), -1, -1, false, 0.f)); break;
		case EMissionKind::Medical: S0.Options.Add(Opt(TEXT("Send doctors across"), S::Medicine, 50.f, D::Medical, TEXT("Contact.Friendly"), TEXT(""), TEXT("Two sickbays' worth of hands; the colony is through the worst."), TEXT("Our doctors are a day behind hers and little use."), -1, -1, false, 0.1f)); break;
		case EMissionKind::Patrol: S0.Options.Add(Opt(TEXT("Sweep the system together"), S::Tactical, 45.f, D::Tactical, TEXT("Contact.Friendly"), TEXT(""), TEXT("Two ships sweep what one could not; the flag is well shown."), TEXT("We get in each other's way."), -1, -1, false, 0.f)); break;
		case EMissionKind::Rescue: S0.Options.Add(Opt(TEXT("Take a share of the survivors"), S::Operations, 48.f, D::Operations, TEXT("Rescue"), TEXT("Loss.Civilians"), TEXT("Her sickbay and ours between them have room for everyone."), TEXT("A transporter fault on our side loses time she did not have."), -1, -1, false, 0.1f)); break;
		default: S0.Options.Add(Opt(TEXT("Transfer stores across"), S::Operations, 40.f, D::Operations, TEXT("Contact.Friendly"), TEXT(""), TEXT("Our stores make up what she was short of."), TEXT("A pad fault spoils half of it."), -1, -1, false, 0.f)); break;
		}
		S0.Options.Add(Opt(TEXT("Hail and exchange officers for the day"), S::Command, 40.f, D::Command, TEXT("Contact.Friendly"), TEXT(""), TEXT("Officers dine across; the work goes on with two ships' worth of ideas."), TEXT("A pleasant evening, and nothing done."), -1, -1, false, 0.f));
		M.Stages = { S0 };
		if (UFleetSubsystem* F = Fleet()) { F->SetAssistMission(Q.ShipName, M.Id); }
	}
	Missions.Add(M);
	if (Q.bSupport) { Report(FString::Printf(TEXT("%s's call for support is on the mission board, Captain; she has a day."), *Q.ShipName), FLinearColor(1.f, 0.85f, 0.5f), ECrewDepartment::Operations); }
	else { Report(FString::Printf(TEXT("The %s is at work close by (%s); the board has a way to lend a hand."), *Q.ShipName, *Q.MissionTitle), FLinearColor(0.75f, 0.9f, 1.f), ECrewDepartment::Operations); }
	Dirty();
}

void UMissionSubsystem::OfferAssists()
{
	UFleetSubsystem* F = Fleet();
	if (!F) { return; }
	for (const FFleetShip* S : F->Nearby())
	{
		if (!F->WantsAssist(*S)) { continue; }
		FFleetRequest Q; Q.ShipName = S->Name; Q.bSupport = false; Q.MissionTitle = S->Mission.Title; Q.TheirKind = S->Mission.Kind; Q.HoursLeft = S->Mission.HoursLeft;
		IssueFleetMission(Q);
	}
}

void UMissionSubsystem::FleetWorkOver(int32 MissionId, bool bTheirSuccess)
{
	for (FMission& M : Missions)
	{
		if (M.Id != MissionId || !M.IsOpen()) { continue; }
		if (M.Kind == EMissionKind::Assist) { Close(M, EMissionState::Expired, bTheirSuccess ? TEXT("they finished without us") : TEXT("their work came to nothing before we lent a hand")); }
		else { Close(M, EMissionState::Failed, TEXT("the ship could not wait")); Standing -= M.Standing / 2; ++Failed; Report(FString::Printf(TEXT("%s: too late; she has given up waiting for us."), *M.Title), FLinearColor(1.f, 0.6f, 0.5f)); }
	}
}

void UMissionSubsystem::FleetWithdraw(int32 MissionId, const FString& Why)
{
	for (FMission& M : Missions)
	{
		if (M.Id != MissionId || !M.IsOpen()) { continue; }
		Close(M, EMissionState::Expired, Why);
		Report(FString::Printf(TEXT("%s: withdrawn; %s."), *M.Title, *Why), FLinearColor(0.75f, 0.9f, 1.f), ECrewDepartment::Operations);
	}
}

int32 UMissionSubsystem::DevSpawn(EMissionKind Kind, EMissionSource Source)
{
	FRandomStream R(Rng.RandRange(1, 1 << 20));
	if (Kind == EMissionKind::Support || Kind == EMissionKind::Assist) { return -2; }   // the fleet raises these (ship fleet trouble / mission)
	FMission M = MakeMission(Kind, Source, R);
	if (M.Id < 0) { return -1; }
	if (M.TargetKind == EMissionTarget::Vessel && Source != EMissionSource::Event) { return -2; }   // its hull only exists as an encounter
	if (Source == EMissionSource::Event)
	{
		M.State = EMissionState::Active; M.DeadlineStardate = Stardate() + 1.0;
		M.Log.Add(FString::Printf(TEXT("%.1f  %s"), Stardate(), *M.Brief));
		if (M.TargetKind == EMissionTarget::Vessel) { SpawnEventVessel(M, M.TargetName); }
	}
	else
	{
		M.State = EMissionState::Offered;
		if (Source == EMissionSource::Starfleet) { FStarfleetMessage Msg; Msg.Stardate = Stardate(); Msg.From = M.From; Msg.Subject = M.Title; Msg.MissionId = M.Id; Msg.Text = M.Brief; Inbox.Add(Msg); }
		else { M.PostedAt = Last.StationName.IsEmpty() ? TEXT("the board") : Last.StationName; }
	}
	Missions.Add(M);
	Dirty();
	return M.Id;
}

// ---- the captain ----

bool UMissionSubsystem::Accept(int32 Id, FString& OutReply)
{
	for (FMission& M : Missions)
	{
		if (M.Id != Id) { continue; }
		if (M.State != EMissionState::Offered) { OutReply = FString::Printf(TEXT("%s is %s"), *M.Title, *StateName(M.State).ToLower()); return false; }
		M.State = EMissionState::Active;
		M.Log.Add(FString::Printf(TEXT("%.1f  Accepted%s."), Stardate(), M.Source == EMissionSource::Starbase ? *(TEXT(" at ") + M.PostedAt) : TEXT("")));
		for (FStarfleetMessage& Msg : Inbox) { if (Msg.MissionId == Id) { Msg.bRead = true; } }
		OutReply = FString::Printf(TEXT("%s accepted%s"), *M.Title, M.TargetName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT("; make for %s"), *M.TargetName));
		Dirty();
		return true;
	}
	OutReply = TEXT("No such mission"); return false;
}

bool UMissionSubsystem::Abandon(int32 Id, FString& OutReply)
{
	for (FMission& M : Missions)
	{
		if (M.Id != Id) { continue; }
		if (!M.IsOpen()) { OutReply = FString::Printf(TEXT("%s is already %s"), *M.Title, *StateName(M.State).ToLower()); return false; }
		const bool bWasActive = M.State == EMissionState::Active;
		Close(M, EMissionState::Abandoned, TEXT("by the captain's decision"));
		if (bWasActive && M.Source != EMissionSource::Event) { Standing -= M.Standing / 2; ++Failed; }
		// She is told no help is coming and goes on alone with the day she has left; only its end counts as no help came.
		if (M.Kind == EMissionKind::Support) { if (UFleetSubsystem* F = Fleet()) { F->SupportDeclined(M.TargetName); } }
		if (M.Kind == EMissionKind::Assist) { if (UFleetSubsystem* F = Fleet()) { F->SetAssistMission(M.TargetName, -1); } }
		if (bWasActive) { if (UCrewRosterSubsystem* R = Roster()) { FCrewExperience E; E.Tag = M.Kind == EMissionKind::Rescue || M.Kind == EMissionKind::Medical ? TEXT("Loss.Civilians") : TEXT("Order.Refused"); E.Intensity = M.Kind == EMissionKind::Rescue ? 0.7f : 0.3f; E.Place = M.TargetName; E.Group = M.Group; E.Text = FString::Printf(TEXT("%s abandoned"), *M.Title); E.bCaptainsChoice = true; R->Experience(E); } }
		OutReply = FString::Printf(TEXT("%s abandoned"), *M.Title);
		return true;
	}
	OutReply = TEXT("No such mission"); return false;
}

bool UMissionSubsystem::SetParty(int32 Id, const TArray<int32>& Ids, FString& OutReply)
{
	for (FMission& M : Missions)
	{
		if (M.Id != Id) { continue; }
		M.Party = Ids; Dirty();
		OutReply = FString::Printf(TEXT("%d named for %s"), Ids.Num(), *M.Title); return true;
	}
	OutReply = TEXT("No such mission"); return false;
}

bool UMissionSubsystem::IsAtTarget(const FMission& M) const
{
	switch (M.TargetKind)
	{
	case EMissionTarget::None: return true;
	case EMissionTarget::System: return Last.SystemName == M.TargetName;
	default: for (const FString& N : Last.Nearby) { if (N == M.TargetName) { return true; } } return false;
	}
}

TArray<int32> UMissionSubsystem::PartyFor(const FMission& M, const FMissionOption& O) const
{
	// The captain's named party, else the option's department: its head and second and the two best at the skill.
	UCrewRosterSubsystem* R = Roster();
	if (!R) { return {}; }
	TArray<int32> Out;
	for (int32 Id : M.Party) { const FCrewMember* C = R->Find(Id); if (C && C->IsFit() && C->IsAboard()) { Out.AddUnique(Id); } }
	if (M.Party.Num() > 0) { return Out; }   // the captain named them: nobody else goes in their place, even if none can
	if (const FCrewMember* H = R->Head(O.Party)) { if (H->IsFit()) { Out.Add(H->Id); } }
	if (const FCrewMember* S = R->Second(O.Party)) { if (S->IsFit()) { Out.Add(S->Id); } }
	TArray<TPair<float, int32>> Best;
	for (const FCrewMember& C : R->GetRoster()) { if (C.IsFit() && C.IsAboard() && !Out.Contains(C.Id)) { Best.Add({ C.Skill(O.Skill), C.Id }); } }
	Best.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B) { return A.Key > B.Key; });
	for (int32 i = 0; i < Best.Num() && Out.Num() < 4; ++i) { Out.Add(Best[i].Value); }
	return Out;
}

void UMissionSubsystem::Hurt(const FMission& M, const TArray<int32>& Party, float Risk)
{
	// A failure with risk: someone of the party is wounded, now and then killed, through the ship's medical model.
	if (Party.Num() == 0 || Rng.FRand() >= Risk) { return; }
	UCrewRosterSubsystem* R = Roster();
	AStarshipPawn* Ship = CrewedShip();
	if (!R || !Ship || !Ship->Systems) { return; }
	const int32 Id = Party[Rng.RandRange(0, Party.Num() - 1)];
	const FCrewMember* C = R->Find(Id);
	if (!C) { return; }
	const bool bKilled = Risk > 0.3f && Rng.FRand() < 0.25f;
	const FString Where = M.TargetName.IsEmpty() ? M.Title : M.TargetName;
	if (!Ship->Systems->HurtCrewMember(Id, bKilled, Rng.FRand() < 0.35f, Where)) { return; }
	Report(FString::Printf(TEXT("%s %s on %s."), *C->Styled(), bKilled ? TEXT("was killed") : TEXT("is hurt"), *Where), FLinearColor(1.f, 0.5f, 0.4f), ECrewDepartment::Medical);
}

bool UMissionSubsystem::Act(int32 Id, int32 Option, FMissionOutcome& Out, FString& OutReply)
{
	FMission* MP = nullptr; for (FMission& M : Missions) { if (M.Id == Id) { MP = &M; } }
	if (!MP) { OutReply = TEXT("No such mission"); return false; }
	FMission& M = *MP;
	if (M.State != EMissionState::Active) { OutReply = FString::Printf(TEXT("%s is %s"), *M.Title, *StateName(M.State).ToLower()); return false; }
	const FMissionStage* St = M.Current();
	if (!St || !St->Options.IsValidIndex(Option)) { OutReply = TEXT("No such option"); return false; }
	if (St->bAtTarget && !IsAtTarget(M)) { OutReply = FString::Printf(TEXT("%s is not in range; make for %s first"), *M.TargetName, *M.TargetName); return false; }
	UCrewRosterSubsystem* R = Roster();
	if (!R) { OutReply = TEXT("No crew"); return false; }
	const FMissionOption O = St->Options[Option];
	const TArray<int32> Party = PartyFor(M, O);
	if (Party.Num() == 0) { OutReply = M.Party.Num() > 0 ? TEXT("None of the named party is fit and aboard; name another (ship mission party <id> <names>)") : TEXT("Nobody fit to send"); return false; }
	const FCrewCheck C = R->Check(O.Skill, O.Difficulty, Party, 0.f, M.Group);
	Out.bSuccess = C.bSuccess; Out.CheckSummary = C.Summary; Out.Text = C.bSuccess ? O.SuccessText : O.FailText;
	M.Log.Add(FString::Printf(TEXT("%.1f  %s: %s %s"), Stardate(), *St->Title, *O.Text, C.bSuccess ? TEXT("- carried.") : TEXT("- failed.")));
	M.Log.Add(FString::Printf(TEXT("      %s"), *Out.Text));
	// The crew live it.
	const FName Tag = C.bSuccess ? O.SuccessTag : O.FailTag;
	if (!Tag.IsNone())
	{
		FCrewExperience E; E.Tag = Tag; E.Intensity = C.bSuccess ? 0.6f : 0.7f; E.Involved = Party; E.Group = M.Group; E.Place = M.TargetName; E.bCaptainsChoice = true;
		E.Text = FString::Printf(TEXT("%s: %s"), *M.Title, *Out.Text);
		R->Experience(E);
	}
	if (!C.bSuccess) { Hurt(M, Party, O.Risk); }
	if (C.bSuccess && O.Science > 0) { M.ScienceEarned += O.Science; }
	// Onward, or the end.
	const int32 Next = C.bSuccess ? O.NextOnSuccess : O.NextOnFail;
	if (Next >= 0 && M.Stages.IsValidIndex(Next)) { M.Stage = Next; Out.bMissionOver = false; }
	else
	{
		const bool bSucceeded = C.bSuccess || !O.bFailEndsMission;
		Out.bMissionOver = true;
		if (bSucceeded)
		{
			Close(M, EMissionState::Completed, C.bSuccess ? TEXT("carried through") : TEXT("done, though not well"));
			Standing += C.bSuccess ? M.Standing : M.Standing / 3; ++Completed;
			if (M.ScienceEarned > 0) { if (UScienceLogSubsystem* L = GetGameInstance() ? GetGameInstance()->GetSubsystem<UScienceLogSubsystem>() : nullptr) { L->AddReward(FString::Printf(TEXT("mission/%d"), M.Id), M.TargetName.IsEmpty() ? FString(TEXT("Mission")) : M.TargetName, M.Title, M.ScienceEarned, Out.Text); } }
			Report(FString::Printf(TEXT("%s: complete. Starfleet's regard: %+d."), *M.Title, C.bSuccess ? M.Standing : M.Standing / 3), FLinearColor(0.6f, 1.f, 0.6f));
		}
		else
		{
			Close(M, EMissionState::Failed, Out.Text);
			Standing -= M.Standing / 2; ++Failed;
			Report(FString::Printf(TEXT("%s: failed. Starfleet's regard: %d."), *M.Title, -M.Standing / 2), FLinearColor(1.f, 0.6f, 0.5f));
		}
		// The fleet hears of it once our side is closed, so her own closing of the work finds nothing open.
		if (UFleetSubsystem* F = Fleet()) { if (M.Kind == EMissionKind::Support) { F->SupportResolved(M.TargetName, C.bSuccess, false); } else if (M.Kind == EMissionKind::Assist) { F->AssistResolved(M.TargetName, C.bSuccess); } }
	}
	OutReply = Out.Text;
	Dirty();
	return true;
}
