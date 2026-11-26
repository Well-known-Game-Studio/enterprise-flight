// Slonk - missions: generation from orders, postings and chance; the stages and their checks; the tracker, the
// inbox and the log.

#include "Missions.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "WarpDriveComponent.h"
#include "StarSystem.h"
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
#include "Exploration.h"
#include "Frontier.h"
#include "Impactor.h"
#include "Leviathan.h"

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
	// ---- a fold in time: the lock on the old road -------------------------------------------------------------
	// Its particulars come from the mission's secret: which of the three answers the stone takes (0 its call as
	// given, 1 its call turned about, 2 silence for its length), which of the wrong ones the dead hull gave, which
	// the stone's faces rule out (the other wrong one, so the two together leave the right one), how the call
	// runs, and how many days a ship that rides the closing is thrown.
	int32 LockAnswer(const FMission& M) { return M.Secret % 3; }
	int32 LockHullTried(const FMission& M) { return (LockAnswer(M) + 1 + (M.Secret / 3) % 2) % 3; }
	int32 LockCarvedOut(const FMission& M) { return 3 - LockAnswer(M) - LockHullTried(M); }
	int32 LockDaysThrown(const FMission& M) { return 5 + (M.Secret / 6) % 7; }
	const TCHAR* LockCall(const FMission& M)
	{
		static const TCHAR* Calls[] = { TEXT("long, short, short, long, short"), TEXT("short, short, long, short, long"), TEXT("long, long, short, short, long"), TEXT("short, long, long, short, short") };
		return Calls[(M.Secret / 42) % UE_ARRAY_COUNT(Calls)];
	}
	FName LockRefusedMark(int32 Answer) { static const TCHAR* Marks[] = { TEXT("Refused.Given"), TEXT("Refused.Turned"), TEXT("Refused.Silence") }; return FName(Marks[Answer % 3]); }
	const TCHAR* LockAnswerText(int32 Answer)
	{
		static const TCHAR* Texts[] = { TEXT("Answer: its call as given"), TEXT("Answer: its call turned about"), TEXT("Answer: silence for its length") };
		return Texts[Answer % 3];
	}
	const TCHAR* LockRefusedLine(int32 Answer)
	{
		static const TCHAR* Lines[] = { TEXT("Refused by the stone: its call given back as given."), TEXT("Refused by the stone: its call turned about."), TEXT("Refused by the stone: silence for its length.") };
		return Lines[Answer % 3];
	}
	const TCHAR* LockHullClue(int32 Tried)
	{
		static const TCHAR* Clues[] = {
			TEXT("Her transmitter still runs on a trickle of power, giving the stone's call back to it as given, as it has for longer than the Federation is old. It did not serve her people."),
			TEXT("Her transmitter still runs on a trickle of power, giving the stone's call back to it turned about, as it has for longer than the Federation is old. It did not serve her people."),
			TEXT("Her last order stands in her log, and was kept to the end: show no light, send nothing, keep silence through the stone's call, engines ready. It did not serve her people.") };
		return Clues[Tried % 3];
	}
	const TCHAR* LockCarvedClue(int32 Out)
	{
		static const TCHAR* Clues[] = {
			TEXT("Ship after ship is cut into the stone beside the gate's own mark, and no ship's mark repeats the gate's: to its makers a call given back unchanged was no answer."),
			TEXT("Ship after ship is cut into the stone beside the gate's own mark, and every one faces as the gate's faces: nothing on the stone is turned about. To its makers a thing reversed was a thing refused."),
			TEXT("Ship after ship is cut into the stone beside the gate's own mark, and every one is cut deep, a thing said: none of them passed by keeping still.") };
		return Clues[Out % 3];
	}
	/** The watch of a loop in time, as it reads on this pass: the same every time, then what the passes have left
	 *  (one line a thing known, with what each way out would cost where that can be seen), then what this pass has
	 *  made ready. */
	FString LoopWatchText(const FMission& M)
	{
		auto Has = [&M](const TCHAR* Mark) { return M.Marks.Contains(FName(Mark)); };
		static const TCHAR* Times[] = { TEXT("second"), TEXT("third"), TEXT("fourth"), TEXT("fifth"), TEXT("sixth"), TEXT("seventh"), TEXT("eighth"), TEXT("ninth"), TEXT("tenth"), TEXT("eleventh"), TEXT("twelfth") };
		FString T;
		if (M.Pass == 0)
		{
			T = Has(TEXT("Met")) ? TEXT("A fold in space lies across the course, closing on the ship, and Science knows it at sight: another lock on the old road.")
				: TEXT("A fold in space lies across the course, and it is closing on the ship. Nothing in the catalogue describes it.");
		}
		else
		{
			T = FString::Printf(TEXT("The forenoon watch, for the %s time, though nobody aboard would say so: the same reports in the same words, and the fold closing as before."),
				M.Pass - 1 < UE_ARRAY_COUNT(Times) ? Times[M.Pass - 1] : *FString::Printf(TEXT("%dth"), M.Pass + 1));
			T += Has(TEXT("Echoes")) ? TEXT(" What the last watches learned is in the crew's hands before it is in their heads.") : TEXT(" Some of the crew have the look of people who have heard all of it before.");
		}
		TArray<FString> Known;
		if (Has(TEXT("Curve")))
		{
			Known.Add(TEXT("The fold is a closed curve, and has the ship by her drive's own field: that is what it will tear when it closes. A cold core would give it nothing to hold; and a cold core is half a day in the relighting."));
			if (!Has(TEXT("Anchor"))) { Known.Add(TEXT("The whole of it hangs from a hard point at the seam, which nobody has yet looked at. Break that and the fold would let go all at once: a hard blow to a ship not rigged for it.")); }
		}
		if (Has(TEXT("Hailed"))) { Known.Add(TEXT("A hail comes back before it is sent. No help is coming in.")); }
		if (Has(TEXT("Echoes"))) { Known.Add(TEXT("What a watch learns is carried through the closing, and what it does is undone. A ship might be carried through the same way, and come out wherever the stars had got to by then.")); }
		if (Has(TEXT("Anchor")))
		{
			Known.Add(FString::Printf(TEXT("The stone at the seam is a lock on an old road, of the make of the rings and the standing stones. It calls in five beats (%s) and wants an answer; a wrong one it refuses, and closes the fold hard on the ship that gave it. It could be broken: the fold would let go all at once, a hard blow to a ship not rigged for it; and nothing else of its makers' has ever been found working."), LockCall(M)));
			if (Has(TEXT("Wary"))) { Known.Add(TEXT("Its call is short and hard: word of the stone that was broken seems to have gone down the road.")); }
		}
		if (Has(TEXT("Hull"))) { Known.Add(FString(TEXT("The dead hull. ")) + LockHullClue(LockHullTried(M))); }
		if (Has(TEXT("Faces"))) { Known.Add(FString(TEXT("The stone's faces. ")) + LockCarvedClue(LockCarvedOut(M))); }
		for (int32 k = 0; k < 3; ++k) { if (M.Marks.Contains(LockRefusedMark(k))) { Known.Add(LockRefusedLine(k)); } }
		if (M.Readied.Contains(TEXT("Braced"))) { Known.Add(TEXT("This watch: the forward decks are cleared, and she is rigged for a blow.")); }
		for (const FString& K : Known) { T += TEXT("\n- ") + K; }
		return T;
	}
	// ---- a leviathan alongside ----------------------------------------------------------------------------------
	// A living thing nine kilometres long, cased in rock and wreck, lays itself along the beam and closes on the ship
	// with every hour's work done under it. It wants one thing of her, of four, another each time (the mission's
	// secret): 0 plate for its case, 1 quiet (her drive lies across its hearing of the road), 2 lee from the star's
	// flare, 3 hands to clean what it cannot reach. Three things can be found out (0 its call, 1 its track, 2 what a
	// boat sees), and each strikes out one want it has not, a different one each: the three together leave the one
	// it has, two leave an even pair.
	/** A mission whose acts are paid for in hours it has only so many of (FMission::PassHours, HoursLeft). */
	bool Budgeted(const FMission& M) { return M.PassHours > 0.0; }
	int32 LevWant(const FMission& M) { return M.Secret % 4; }
	int32 LevRuledOut(const FMission& M, int32 Source)
	{
		static const int32 LevPerm[6][3] = { { 0, 1, 2 }, { 0, 2, 1 }, { 1, 0, 2 }, { 1, 2, 0 }, { 2, 0, 1 }, { 2, 1, 0 } };
		return (LevWant(M) + 1 + LevPerm[(M.Secret / 4) % 6][Source % 3]) % 4;
	}
	/** What a finding says: one observation against one reading, silent on the other three. */
	const TCHAR* LevClue(int32 Source, int32 Out)
	{
		static const TCHAR* Clues[3][4] = {
			{ TEXT("It sounds the ship from stem to stern and lets every echo of her plating go by unanswered."),
			  TEXT("The drive's field was run up and down its whole range while we listened. Its call did not change by a beat."),
			  TEXT("When the star flared across every band, its call neither faltered nor fell quiet. The star does not trouble it."),
			  TEXT("Listening close, its plates ride quiet on it: no grind of grit under them when it swims. Its hide is clean.") },
			{ TEXT("Its track passes a hulk by a thousand kilometres: sixty thousand tons of plate adrift. It did not turn its head."),
			  TEXT("It turned for the ship from farther off than her drive can be heard. Something told it she was here; not her engines."),
			  TEXT("It has lain four days in the open glare of this star, and through the last flare it did not alter course."),
			  TEXT("Twice in four days it has rolled through a dust lane, as its kind scour themselves. What tending it needs, it does.") },
			{ TEXT("The boat trailed a ton of scrap plate on a line past its head, twice. It let the plate go by."),
			  TEXT("The boat ran her drive up hard under its head, shut it down, and ran it up again. It did not turn to her drive or from it."),
			  TEXT("The boat laid the lee of her shields over its head a quarter of an hour. It moved out from under, into the glare."),
			  TEXT("The boat put two hands in suits onto its hide. It shut its plates under their boots, and held them shut until they were off.") } };
		return Clues[Source % 3][Out % 4];
	}
	const TCHAR* LevSourceMark(int32 Source) { static const TCHAR* Marks[] = { TEXT("Call"), TEXT("Track"), TEXT("Boat") }; return Marks[Source % 3]; }
	FName LevGiftMark(int32 Gift) { static const TCHAR* Marks[] = { TEXT("Not.Plate"), TEXT("Not.Quiet"), TEXT("Not.Lee"), TEXT("Not.Hands") }; return FName(Marks[Gift % 4]); }
	/** The hours of work a meeting has before it is against the hull: seven, an hour fewer for a ship that broke a
	 *  lock on its road, two for one that burned one of its kind, two for each time this one was left astern; never
	 *  fewer than the dearest gift takes. */
	double LevHours(const FMission& M, int32 TimesLeft)
	{
		return FMath::Max(3.0, 7.0 - (M.Marks.Contains(TEXT("Wary")) ? 1.0 : 0.0) - (M.Marks.Contains(TEXT("Shut")) ? 2.0 : 0.0) - 2.0 * TimesLeft);
	}
	/** Starfleet's regard for a leviathan given what it wanted: the less for each thing given that it did not want,
	 *  for a shot fired into it, and for each time it was left astern to follow. */
	int32 LevGivenRegard(const FMission& M)
	{
		int32 Misread = M.Marks.Contains(TEXT("Fired")) ? 1 : 0;
		for (int32 k = 0; k < 4; ++k) { Misread += M.Marks.Contains(LevGiftMark(k)) ? 1 : 0; }
		return FMath::Max(5, 45 - 12 * Misread - 6 * M.Pass);
	}
	/** The board of a leviathan alongside: how near it is, what each officer makes of it and what each reading would
	 *  cost to act on, what every other way of parting from it costs, then one line a thing known. */
	FString LeviathanText(const FMission& M)
	{
		auto Has = [&M](const TCHAR* Mark) { return M.Marks.Contains(FName(Mark)); };
		const int32 Left = FMath::RoundToInt(FMath::Max(M.HoursLeft, 0.0));
		FString T;
		TArray<FString> Lines;
		if (Left > 0)
		{
			T = FString::Printf(TEXT("It lies %.0f kilometres off the beam, and every hour's work done under it brings it on: %s, and it is against the hull. It has done her no harm. It wants something of her, and four officers read it four ways."),
				ALeviathan::StationCm(M.HoursLeft) / 1.0e5, Left == 1 ? TEXT("one hour's work more") : *FString::Printf(TEXT("%d hours' work"), Left));
			static const TCHAR* Readings[] = {
				TEXT("The operations officer: plate. It wears wreck and wants more. All the lockers' stock and a strake of her own skin: gone, whether it wanted them or not."),
				TEXT("The chief engineer: quiet. Her drive lies across its hearing of the road. A core shut down cold is twelve hours relighting, wanted or not, and a cold ship cannot go to warp."),
				TEXT("The helmsman: lee. The star is working up to a flare. It would lie in her shadow a day and a night, and every other errand a day the nearer."),
				TEXT("The doctor: hands. It is fouled under its plates where it cannot reach. Parties on its hide for a watch, and it can roll on them.") };
			static const TCHAR* Refused[] = { TEXT("Given, and not wanted: plate."), TEXT("Given, and not wanted: quiet."), TEXT("Given, and not wanted: lee."), TEXT("Given, and not wanted: hands.") };
			for (int32 k = 0; k < 4; ++k) { Lines.Add(M.Marks.Contains(LevGiftMark(k)) ? Refused[k] : Readings[k]); }
			Lines.Add(TEXT("The wrong thing given costs an hour more, and is held against her when the right one is found."));
		}
		else
		{
			T = TEXT("It lies along the hull, feeling over her plating. There is no time left for work: stand fast, fire, or run if she can.");
		}
		// What each other way of parting costs, said before it is taken (and none of it telling what it wants).
		Lines.Add(TEXT("Standing fast, it takes what it came for in its own way: a hard blow, people hurt, and if it is plate or quiet it wants, a strake of her skin or the core put out cold."));
		Lines.Add(Has(TEXT("Fired")) ? TEXT("Her guns would turn it. It strikes as it goes; Starfleet will not thank her, and its kind remember.")
			: TEXT("Her guns would turn it. The first shot brings it on and is held against her; it strikes as it goes; Starfleet will not thank her, and its kind remember."));
		Lines.Add(M.Pass >= 2 ? FString(TEXT("Warp, with a lit core, or going down into a world's air, leaves it astern. It has followed her twice, and will not a third time: what it wanted would go unsaid."))
			: FString::Printf(TEXT("Warp, with a lit core, or going down into a world's air, leaves it astern with what has been learned. It will follow%s, and the leaving is held against her when the right thing is given."),
				LevHours(M, M.Pass + 1) < M.PassHours - 0.5 ? TEXT(", with less patience") : TEXT("")));
		if (M.Pass > 0) { Lines.Add(TEXT("It is the same one: the same stone in its case. It has followed her, and comes on faster than when it first came.")); }
		if (Has(TEXT("Road")) && !Has(TEXT("Hailed"))) { Lines.Add(TEXT("The stone in its case is of the road's make, and her mark is on its fellows. It may know the beats.")); }
		if (Has(TEXT("Wary"))) { Lines.Add(TEXT("The stone in its case calls short and hard: word of the broken lock has gone down the road. It comes on the faster.")); }
		if (Has(TEXT("Shut"))) { Lines.Add(TEXT("Every plate on it is shut. Its kind have been burned by a ship before, and it comes on fast.")); }
		if (Has(TEXT("Filed"))) { Lines.Add(TEXT("Communications has the last one's calls on file: this one's was read as it came up.")); }
		static const TCHAR* Sources[] = { TEXT("Its call. "), TEXT("Its track. "), TEXT("The boat. ") };
		for (int32 s = 0; s < 3; ++s) { if (Has(LevSourceMark(s))) { Lines.Add(FString(Sources[s]) + LevClue(s, LevRuledOut(M, s))); } }
		if (Has(TEXT("Fired"))) { Lines.Add(TEXT("It has been fired on, and holds it against her.")); }
		if (Has(TEXT("Hailed"))) { Lines.Add(TEXT("It knew the road's beats, and fell back for them, the once.")); }
		for (const FString& L : Lines) { T += TEXT("\n- ") + L; }
		return T;
	}
	/** A stage at its own place: a Station with no name is any station the ship is alongside. */
	FMissionStage StageAt(const TCHAR* Title, const FString& Text, EMissionTarget Kind, const FString& Name) { FMissionStage S = Stage(Title, Text, true); S.bOwnTarget = true; S.TargetKind = Kind; S.TargetName = Name; return S; }
}

FString UMissionSubsystem::KindName(EMissionKind K)
{
	static const TCHAR* N[] = { TEXT("Survey"), TEXT("Rescue"), TEXT("Diplomacy"), TEXT("Delivery"), TEXT("Anomaly"), TEXT("Medical"), TEXT("Patrol"), TEXT("Contact"), TEXT("Derelict"), TEXT("Smuggler"), TEXT("Support"), TEXT("Assist"), TEXT("Explore"), TEXT("FirstContact"), TEXT("PreWarp"), TEXT("Beacon"), TEXT("Outpost"), TEXT("Colony"), TEXT("Starbase"), TEXT("Impact"), TEXT("TimeLoop"), TEXT("Leviathan") };
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
			Missions = S->Missions; Inbox = S->Inbox; Legacy = S->Legacy; DriveColdUntilHours = S->DriveColdUntilHours; Standing = S->Standing; NextId = S->NextId; NextOrdersHours = S->NextOrdersHours; NextEventHours = S->NextEventHours; Completed = S->Completed; Failed = S->Failed; BoardDays = S->BoardDays;
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
	S->Missions = Missions; S->Inbox = Inbox; S->Legacy = Legacy; S->DriveColdUntilHours = DriveColdUntilHours; S->Standing = Standing; S->NextId = NextId; S->NextOrdersHours = NextOrdersHours; S->NextEventHours = NextEventHours; S->Completed = Completed; S->Failed = Failed; S->BoardDays = BoardDays;
	UGameplayStatics::SaveGameToSlot(S, SlotName, 0);
	bSaveWanted = false;
}

void UMissionSubsystem::Reset()
{
	if (!bNoDisk) { UGameplayStatics::DeleteGameInSlot(SlotName, 0); }
	// NextId runs on: the science log keeps mission/<id> keys.
	Missions.Reset(); Inbox.Reset(); Legacy.Reset(); DriveColdUntilHours = 0.0; Standing = 0; Completed = 0; Failed = 0; NextOrdersHours = 0.0; NextEventHours = 0.0; BoardPostedAt.Empty(); BoardDays.Empty();
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

void UMissionSubsystem::InitForTest(UCrewRosterSubsystem* InRoster, UFleetSubsystem* InFleet, UExplorationSubsystem* InExploration, UFrontierSubsystem* InFrontier)
{
	bNoDisk = true; RosterOverride = InRoster; FleetOverride = InFleet; ExplorationOverride = InExploration; FrontierOverride = InFrontier;
	Rng.Initialize(11);
}

UCrewRosterSubsystem* UMissionSubsystem::Roster() const
{
	if (RosterOverride.IsValid()) { return RosterOverride.Get(); }
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UCrewRosterSubsystem>() : nullptr;
}
double UMissionSubsystem::TravelDays(const FMission& M) const
{
	if (M.TargetKind == EMissionTarget::None || M.TargetKind == EMissionTarget::Vessel) { return 0.0; }
	FVector To;
	if (!AStarSystem::FindNamedLocation(GetWorld(), M.TargetName, To)) { return M.TargetKind == EMissionTarget::System ? 2.0 : 0.0; }
	return UWarpDriveComponent::HoursFor(FVector::Dist(Last.Location, To), 7.0) / 24.0;
}

double UMissionSubsystem::Stardate() const { const UCrewRosterSubsystem* R = Roster(); return R ? R->GetStardate() : 8305.0; }
double UMissionSubsystem::Hours() const { const UCrewRosterSubsystem* R = Roster(); return R ? R->GetHours() : 0.0; }
UExplorationSubsystem* UMissionSubsystem::Exploration() const
{
	if (ExplorationOverride.IsValid()) { return ExplorationOverride.Get(); }
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UExplorationSubsystem>() : nullptr;
}
UFrontierSubsystem* UMissionSubsystem::Frontier() const
{
	if (FrontierOverride.IsValid()) { return FrontierOverride.Get(); }
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UFrontierSubsystem>() : nullptr;
}

void UMissionSubsystem::AddStanding(int32 Delta, const FString& Why)
{
	Standing += Delta;
	Report(FString::Printf(TEXT("%s. Starfleet's regard: %+d."), *Why, Delta), FLinearColor(0.6f, 1.f, 0.6f), ECrewDepartment::Operations);
	Dirty();
}

void UMissionSubsystem::RenameTarget(const FString& OldName, const FString& NewName)
{
	bool bAny = false;
	// Open or closed: the log reads by the name the chart carries now. The system's worlds carry its name with a
	// numeral ("UFC 8752 II"), so a mission at one of them follows the same way the actors do.
	for (FMission& M : Missions)
	{
		const bool bSystem = M.TargetName == OldName;
		const bool bWorld = (M.TargetKind == EMissionTarget::World || M.TargetKind == EMissionTarget::System || M.TargetKind == EMissionTarget::Station) && M.TargetName.StartsWith(OldName + TEXT(" "));   // a station named for its world too
		if (!bSystem && !bWorld) { continue; }
		const FString Was = M.TargetName;
		M.TargetName = bSystem ? NewName : NewName + M.TargetName.RightChop(OldName.Len());
		M.Title = M.Title.Replace(*Was, *M.TargetName); bAny = true;
		M.Log.Add(FString::Printf(TEXT("      %s is now %s."), *Was, *M.TargetName));
	}
	// The stages' own places and the site chosen follow the same way.
	auto Follow = [&](FString& N) { if (N == OldName) { N = NewName; bAny = true; } else if (N.StartsWith(OldName + TEXT(" "))) { N = NewName + N.RightChop(OldName.Len()); bAny = true; } };
	for (FMission& M : Missions) { Follow(M.Site); for (FMissionStage& St : M.Stages) { if (St.bOwnTarget) { Follow(St.TargetName); } } }
	if (bAny) { Dirty(); }
}

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

void UMissionSubsystem::AdvanceSeconds(float Seconds, const FMissionShipState& Ship, float RealSeconds)
{
	if (RealSeconds < 0.f) { RealSeconds = Seconds; }
	// (The first look after a load is not the drive being engaged.)
	const bool bFirstLook = LastClockHours < 0.0;
	const bool bWasAtWarp = bFirstLook ? Ship.bAtWarp : Last.bAtWarp;
	Last = Ship;
	if (UFleetSubsystem* F = Fleet())
	{
		F->AdvanceSeconds(Seconds, Ship, RealSeconds);
		TArray<FFleetRequest> Q; F->DrainRequests(Q);
		// (No call reaches a ship held in a loop in time, any more than Starfleet's orders do: she could not answer it.)
		const bool bHeld = IsHeldInTime();
		for (const FFleetRequest& R : Q) { int32 Calls = 0; for (const FMission& M : Missions) { if (M.IsOpen() && M.Kind == EMissionKind::Support) { ++Calls; } } if (Calls < 2 && !bHeld) { IssueFleetMission(R); } }
		TArray<FCrewReport> Rep; F->DrainReports(Rep); Reports.Append(Rep);
		if (!bHeld) { OfferAssists(); }
	}
	const double H = Hours();
	// The shared clock went back: the schedules keep their distance to now.
	if (LastClockHours >= 0.0 && H < LastClockHours)
	{
		const double Back = LastClockHours - H; NextOrdersHours -= Back; NextEventHours -= Back;
		DriveColdUntilHours -= Back;
	}
	LastClockHours = H;
	// (A clock begun again between sessions does not lengthen the relighting of a cold core.)
	DriveColdUntilHours = FMath::Min(DriveColdUntilHours, H + 12.0);
	if (NextOrdersHours <= 0.0) { NextOrdersHours = H + Rng.FRandRange(2.0, 5.0); }
	if (NextEventHours <= 0.0) { NextEventHours = H + Rng.FRandRange(0.5, 1.5); }
	// Starfleet's orders come over subspace every several ship's hours while fewer than two of theirs are open.
	if (H >= NextOrdersHours)
	{
		NextOrdersHours = H + Rng.FRandRange(6.0, 14.0);
		int32 Theirs = 0; for (const FMission& M : Missions) { if (M.IsOpen() && M.Source == EMissionSource::Starfleet) { ++Theirs; } }
		// (No subspace reaches a ship held in a loop in time: the orders wait for her.)
		if (Theirs < 2 && !IsHeldInTime()) { IssueOrders(); }
	}
	// A starbase's board, once per arrival.
	if (Ship.bAlongside && !Ship.StationName.IsEmpty() && Ship.StationName != BoardPostedAt && !IsHeldInTime()) { BoardPostedAt = Ship.StationName; PostBoard(Ship.StationName); }
	else if (!Ship.bAlongside) { BoardPostedAt.Empty(); }
	// Space: something happens now and then while under way and not already dealing with one.
	if (H >= NextEventHours)
	{
		NextEventHours = H + Rng.FRandRange(1.0, 3.0);
		int32 Events = 0; for (const FMission& M : Missions) { if (M.IsOpen() && M.Source == EMissionSource::Event) { ++Events; } }
		if (!Ship.bAlongside && !Ship.bUnderFire && !Ship.bAtWarp && Events == 0) { RollEvent(); }
	}
	// A cold core does not answer the helm (the ship lets the drive go): Engineering says how long yet.
	if (Ship.bAtWarp && !bWasAtWarp && IsDriveCold()) { Report(FString::Printf(TEXT("The core is cold, Captain: %.0f hours yet before she will go to warp."), FMath::Max(DriveColdHoursLeft(), 1.0)), FLinearColor(1.f, 0.75f, 0.5f), ECrewDepartment::Engineering); }
	// A loop in time comes round the moment the ship runs for it at warp. (Its hours are spent by what is done in
	// them, in Act, and never by the clock; the date the board shows is the one they would run out on.)
	for (FMission& M : Missions)
	{
		if (M.State != EMissionState::Active || M.Kind != EMissionKind::TimeLoop) { continue; }
		if (Ship.bAtWarp && !bWasAtWarp) { ComeRound(M, TEXT("The ship runs for clear space at warp, and every heading bends back into the fold.")); }
		if (M.State == EMissionState::Active) { M.DeadlineStardate = Stardate() + M.HoursLeft / 24.0; }
	}
	// A leviathan alongside is left astern by a ship that goes to warp (a cold core does not go), or down into a
	// world's air, or to ground. Otherwise it waits on the captain: its hours are spent by what is done under it,
	// and its body is put back in the world where a load, or a world not yet ready, left it without one.
	for (FMission& M : Missions)
	{
		if (M.State != EMissionState::Active || M.Kind != EMissionKind::Leviathan) { continue; }
		if (!bFirstLook && ((Ship.bAtWarp && !IsDriveCold()) || !Ship.bClearSpace)) { M.Exit = TEXT("Slipped"); LeaveLeviathan(M); continue; }
		M.DeadlineStardate = Stardate() + M.HoursLeft / 24.0;
		if (GetWorld() && !ALeviathan::FindByMission(GetWorld(), M.Id)) { SpawnLeviathan(M); }
	}
	Expire();
	if (bSaveWanted) { SaveDelaySeconds -= RealSeconds; if (SaveDelaySeconds <= 0.f) { Save(); } }
}

bool UMissionSubsystem::IsHeldInTime() const
{
	for (const FMission& M : Missions) { if (M.State == EMissionState::Active && M.Kind == EMissionKind::TimeLoop) { return true; } }
	return false;
}

bool UMissionSubsystem::IsCompanyAlongside() const
{
	for (const FMission& M : Missions) { if (M.State == EMissionState::Active && M.Kind == EMissionKind::Leviathan) { return true; } }
	return false;
}

double UMissionSubsystem::PassHoursLeft(const FMission& M) const
{
	return M.State == EMissionState::Active && Budgeted(M) ? FMath::Max(M.HoursLeft, 0.0) : 0.0;
}

void UMissionSubsystem::BeginPass(FMission& M)
{
	// The pass has its hours again: the deadline the board and the HUD show is the pass's own.
	M.HoursLeft = M.PassHours;
	M.DeadlineStardate = Stardate() + M.PassHours / 24.0;
}

bool UMissionSubsystem::HasHoursFor(const FMission& M, const FMissionOption& O) const
{
	return O.Hours <= 0.f || !Budgeted(M) || PassHoursLeft(M) + 1.0e-3 >= O.Hours;
}

void UMissionSubsystem::ComeRound(FMission& M, const FString& How)
{
	// What the pass learned it keeps; what it made ready is undone.
	M.Readied.Reset();
	++M.Pass; M.Stage = 0;
	if (!How.IsEmpty()) { M.Log.Add(FString::Printf(TEXT("%.1f  %s"), Stardate(), *How)); }
	// Twelve passes and no way found: nobody aboard is keeping a watch any longer, and a ship nobody is working is
	// drift to the lock. Out, with nothing to show for it but the science, and the core gone cold of itself. (No crew
	// is held for good, whatever its checks.)
	if (M.Pass >= 12)
	{
		M.Exit = TEXT("Worn"); M.Standing = 0;
		DriveColdUntilHours = Hours() + 12.0;
		CompleteOutside(M, TEXT("let go, worn out"), TEXT("After twelve passes of the one watch nobody aboard is keeping it. The core gutters untended, and the fold closes on a ship too still to hold: drift, to the lock, and drift goes free."));
		if (UCrewRosterSubsystem* R = Roster()) { FCrewExperience E; E.Tag = TEXT("Time.Freed"); E.Intensity = 0.3f; E.Text = TEXT("Out of the fold in time, with nothing done to get out"); R->Experience(E); }
		return;
	}
	M.Log.Add(FString::Printf(TEXT("%.1f  The fold closes on the ship. Then it is the forenoon watch again: pass %d."), Stardate(), M.Pass + 1));
	BeginPass(M);
	if (M.Stages.Num() > 0) { M.Stages[0].Text = LoopWatchText(M); }
	// The same report in the same words, every pass; and what the passes have left, after it.
	Report(FString::Printf(TEXT("%s It is on the mission board, Captain."), *M.Brief), FLinearColor(1.f, 0.85f, 0.5f), ECrewDepartment::Science);
	if (!M.Marks.Contains(TEXT("Echoes"))) { Report(TEXT("Captain, sickbay has had three of the crew in this watch, each sure of what would be said before it was said."), FLinearColor(0.75f, 0.9f, 1.f), ECrewDepartment::Medical); }
	// Nobody remembers, and everybody is the wearier for it.
	if (UCrewRosterSubsystem* R = Roster()) { FCrewExperience E; E.Tag = TEXT("Time.Loop"); E.Intensity = 0.3f; E.Text = TEXT("A watch that felt lived before"); R->Experience(E); }
	Dirty();
}

void UMissionSubsystem::LeaveFold(FMission& M, const FMissionOption& O)
{
	// Out of the fold: what follows is the way out's own.
	UCrewRosterSubsystem* R = Roster();
	auto Felt = [R](const TCHAR* Tag, float Intensity, const FString& Text) { if (R) { FCrewExperience E; E.Tag = Tag; E.Intensity = Intensity; E.Text = Text; R->Experience(E); } };
	const FLinearColor Good(0.6f, 1.f, 0.6f), Grave(1.f, 0.75f, 0.5f);
	const int32 Passes = M.Pass + 1;
	if (M.Exit == FName(TEXT("Answered")))
	{
		// The road knows the ship: Science will know the next lock at sight.
		Legacy.AddUnique(TEXT("Lock.Answered"));
		Report(FString::Printf(TEXT("The lock stands open astern, Captain, and our mark is on the stone with the others. %d passes of the one watch, by what it left in the log."), Passes), Good, ECrewDepartment::Science);
		Felt(TEXT("Time.Freed"), 0.6f, TEXT("Through the lock on the old road, by its own leave"));
	}
	else if (M.Exit == FName(TEXT("Cold")))
	{
		// Dead metal goes free, and is twelve hours in the relighting: no warp until then.
		DriveColdUntilHours = Hours() + 12.0;
		Report(FString::Printf(TEXT("We are through, Captain, and dark: %d passes of the one watch behind us. A cold core is twelve hours in the lighting; she has impulse, and nothing more, until then."), Passes), Grave, ECrewDepartment::Engineering);
		Felt(TEXT("Time.Freed"), 0.5f, TEXT("Out of the fold in time: the watch ended at last"));
	}
	else if (M.Exit == FName(TEXT("Broken")))
	{
		// The fold lets go all at once: she takes it on her shields and her frame, the less for being rigged for it;
		// and the road remembers.
		Legacy.AddUnique(TEXT("Lock.Broken"));
		const bool bBraced = M.Readied.Contains(TEXT("Braced"));
		// (The ship's own model of a blow with no place to it: something breaks, and people are hurt.)
		if (AStarshipPawn* Ship = CrewedShip()) { if (Ship->Systems) { const float Blow = bBraced ? 25.f : 70.f; Ship->Systems->OnHit(Blow, Blow); } }
		Report(M.Marks.Contains(TEXT("Anchor")) ? TEXT("The stone is dust, Captain. Whatever road it kept has no gate here now; and what else of its makers' was listening, we cannot say.")
			: TEXT("There is worked stone in the dust, Captain: carved, every piece of it, and older than anything in the catalogue. Whatever that was, somebody made it; and it was working until we came."), Grave, ECrewDepartment::Science);
		Felt(TEXT("Relic.Broken"), 0.6f, TEXT("The only working thing of a vanished people's, and we broke it to get by"));
		Felt(TEXT("Time.Freed"), 0.3f, TEXT("Out of the fold in time"));
	}
	else if (M.Exit == FName(TEXT("Thrown")))
	{
		// Carried through the closing: no hour of the ship's own spent, and the stars days on.
		const int32 Days = LockDaysThrown(M);
		if (R) { R->SkipDays(Days); }
		M.ClosedStardate = Stardate();   // (closed where she came out: the next fold keeps its ten days from there)
		Legacy.AddUnique(TEXT("Lock.Ridden"));
		Report(FString::Printf(TEXT("We are clear of it, Captain. By the pulsars it is stardate %.1f: %d days gone between one breath and the next, and everything we were due for with them."), Stardate(), Days), Grave, ECrewDepartment::Science);
		Felt(TEXT("Time.Lost"), 0.6f, FString::Printf(TEXT("%d days gone that nobody aboard lived"), Days));
		Save();   // (with the roster's own save of the days: the two files do not disagree about them)
	}
	Dirty();
}

void UMissionSubsystem::SpawnLeviathan(FMission& M)
{
	// Its body, on the beam of the ship whose people these are (none in a test, or before the world has her).
	UWorld* W = GetWorld();
	AStarshipPawn* Ship = W ? CrewedShip() : nullptr;
	if (!Ship || M.Site.IsEmpty()) { return; }
	ALeviathan::Spawn(W, Ship, M.Site, M.Id, M.HoursLeft);
}

void UMissionSubsystem::LeviathanOutcome(int32 MissionId, ELeviathanOutcome Outcome)
{
	FMission* MP = nullptr; for (FMission& Each : Missions) { if (Each.Id == MissionId && Each.Kind == EMissionKind::Leviathan && Each.State == EMissionState::Active) { MP = &Each; } }
	if (!MP) { return; }
	FMission& M = *MP;
	if (Outcome == ELeviathanOutcome::Struck)
	{
		// The first shot into it, the once (a body put back after a load has been fired on already): it does not
		// turn, it comes on; and it is held against her as a thing misread is.
		if (M.Marks.Contains(TEXT("Fired"))) { return; }
		M.Marks.Add(TEXT("Fired"));
		M.HoursLeft = FMath::Max(M.HoursLeft - 1.0, 0.0);
		M.Log.Add(FString::Printf(TEXT("%.1f  The ship fired on it. It did not turn: it came on."), Stardate()));
		if (UCrewRosterSubsystem* R = Roster()) { FCrewExperience E; E.Tag = TEXT("Beast.Harmed"); E.Intensity = 0.4f; E.Text = TEXT("The ship fired on a living thing that had done her no harm"); E.bCaptainsChoice = true; R->Experience(E); }
		Report(TEXT("It felt that, Captain. It has not turned: it has come on."), FLinearColor(1.f, 0.75f, 0.5f), ECrewDepartment::Tactical);
		if (M.Stages.Num() > 0) { M.Stages[0].Text = LeviathanText(M); }
		if (ALeviathan* Body = ALeviathan::FindByMission(GetWorld(), M.Id)) { Body->Ripple(ALeviathan::Amber()); }
		Dirty();
		return;
	}
	// Turned by her guns: no regard for it, and what it left adrift is the science (earned before the closing, which
	// pays it).
	M.Exit = TEXT("Driven"); M.Standing = 0; M.ScienceEarned += 120;
	CompleteOutside(M, TEXT("driven off"), TEXT("Plate boils off its flank. It flares white end to end, lashes once along the hull as it turns, and goes. It has left pieces of its case adrift."));
	LeaveLeviathan(M);
}

void UMissionSubsystem::LeviathanAfter(FMission& M, const FMissionOption& O, bool bSuccess)
{
	// What an act on the board costs beyond its hours, and what the creature shows for it.
	UCrewRosterSubsystem* R = Roster();
	ALeviathan* Body = ALeviathan::FindByMission(GetWorld(), M.Id);
	int32 Gift = -1; for (int32 k = 0; k < 4; ++k) { if (O.NotIfMark == LevGiftMark(k)) { Gift = k; } }
	if (Gift >= 0)
	{
		// The wrong thing given costs an hour more: of the work left, and of the stars' time.
		if (O.bWrong) { if (R) { R->LiveHours(1); } M.HoursLeft = FMath::Max(M.HoursLeft - 1.0, 0.0); }
		// What was given is gone, whether it was wanted or not (a check that failed gave nothing): the lockers' stock
		// and a strake of hull; or the core, twelve hours in the relighting from the hour it went cold.
		if (bSuccess || O.bWrong)
		{
			if (Gift == 0) { if (AStarshipPawn* Ship = CrewedShip()) { if (Ship->Systems) { Ship->Systems->Stores.SpareParts = 0.f; } Ship->HullIntegrity = FMath::Max(Ship->HullIntegrity - 35.f, 1.f); } }
			if (Gift == 1) { DriveColdUntilHours = Hours() + 12.0; }
		}
		if (O.bWrong && Body) { Body->Ripple(ALeviathan::Amber()); }
	}
	else if (bSuccess && !O.GivesMark.IsNone())
	{
		// A finding; or the road's beats, for which it gives her room: three hours for the one they took.
		if (O.GivesMark == FName(TEXT("Hailed"))) { M.HoursLeft += 3.0; }
		if (Body) { Body->Ripple(ALeviathan::Pale()); }
	}
	if (M.IsOpen() && M.Stages.Num() > 0) { M.Stages[0].Text = LeviathanText(M); M.DeadlineStardate = Stardate() + M.HoursLeft / 24.0; }
}

void UMissionSubsystem::LeaveLeviathan(FMission& M)
{
	// Parted from: what follows is the way's own.
	UCrewRosterSubsystem* R = Roster();
	auto Felt = [R](const TCHAR* Tag, float Intensity, const FString& Text, bool bChoice) { if (R) { FCrewExperience E; E.Tag = Tag; E.Intensity = Intensity; E.Text = Text; E.bCaptainsChoice = bChoice; R->Experience(E); } };
	const FLinearColor Good(0.6f, 1.f, 0.6f), Grave(1.f, 0.75f, 0.5f);
	AStarshipPawn* Ship = GetWorld() ? CrewedShip() : nullptr;
	ALeviathan* Body = ALeviathan::FindByMission(GetWorld(), M.Id);
	const int32 Want = LevWant(M);
	if (M.Exit == FName(TEXT("Given")))
	{
		// The lee is a day and a night in her shadow, the tending a watch on its hide: lived at once, and every other
		// errand so much the nearer. Its kind know her for it; and what one of them was burned for is unsaid.
		if (R && Want == 2) { R->LiveHours(22); } else if (R && Want == 3) { R->LiveHours(6); }
		Legacy.AddUnique(TEXT("Leviathan.Given")); Legacy.Remove(TEXT("Leviathan.Blooded"));
		Report(TEXT("It has what it came for, Captain, and is gone down its road."), Good, ECrewDepartment::Science);
		if (Body) { Body->Depart(true); }
	}
	else if (M.Exit == FName(TEXT("Taken")))
	{
		// It takes what it came for in its own way: a hard blow by the ship's own model of one (something breaks, and
		// people are hurt); and plate it takes from her skin, quiet by leaning on her until the core is out.
		if (Ship && Ship->Systems) { Ship->Systems->OnHit(60.f, 60.f); }
		if (Ship && Want == 0) { Ship->HullIntegrity = FMath::Max(Ship->HullIntegrity - 35.f, 1.f); }
		if (Want == 1) { DriveColdUntilHours = Hours() + 12.0; }
		Felt(TEXT("Beast.Struck"), 0.7f, TEXT("A leviathan took what it came for from the ship, and nobody stopped it"), false);
		Report(Want == 1 ? TEXT("It has taken what it came for and gone, Captain, and the core has gone with it: twelve hours to relight. Damage and casualties to follow.")
			: TEXT("It has taken what it came for and gone, Captain. Damage and casualties to follow."), Grave, ECrewDepartment::Engineering);
		if (Body) { Body->Lash(); Body->Depart(false); }
	}
	else if (M.Exit == FName(TEXT("Driven")))
	{
		// It strikes as it goes, the lighter blow; the crew carry what was done to it, and its kind remember.
		if (Ship && Ship->Systems) { Ship->Systems->OnHit(35.f, 35.f); }
		Legacy.AddUnique(TEXT("Leviathan.Blooded")); Legacy.Remove(TEXT("Leviathan.Given"));
		Felt(TEXT("Beast.Harmed"), 0.9f, TEXT("A living thing that had done the ship no harm, burned until it turned"), true);
		Report(TEXT("It struck us as it went, Captain. The pieces are aboard for Science."), Grave, ECrewDepartment::Tactical);
		if (Body) { Body->Lash(); Body->Depart(false); }
	}
	else if (M.Exit == FName(TEXT("Slipped")))
	{
		// Left astern: nothing settled, what was learned kept, and the same creature the next of its kind to come. The
		// third time it does not follow (so there is an end to leaving it). No regard either way, and not a mission
		// completed: the board has it as the captain's leaving.
		const bool bLast = M.Pass >= 2;
		const FString Text = bLast ? TEXT("She draws away, and this time it does not follow: it hangs astern a while, then turns back down its road with what it wanted of her unsaid.")
			: TEXT("She draws away, and it falls astern still swimming after her. What it wanted, it wants yet.");
		M.Standing = 0;
		M.Log.Add(FString::Printf(TEXT("      %s"), *Text));
		Close(M, EMissionState::Abandoned, TEXT("left astern"));
		PayScience(M, Text);
		if (!bLast) { Legacy.AddUnique(TEXT("Leviathan.Astern")); }
		Report(bLast ? TEXT("It has turned back to its road, Captain. We shall not see that one again.") : TEXT("It has not turned back to its road, Captain. It is still coming."), FLinearColor(0.75f, 0.9f, 1.f), ECrewDepartment::Helm);
		// (Its body finds the mission closed, and goes of itself.)
	}
	// (Closed where the hours lived on the way out left her: the next of its kind keeps its days from there.)
	M.ClosedStardate = Stardate();
	Dirty();
}

void UMissionSubsystem::Expire()
{
	const double SD = Stardate();
	for (FMission& M : Missions)
	{
		// (A loop in time the ship is in does not run out: it comes round. Nor does a leviathan alongside: it waits.)
		if (!M.IsOpen() || M.DeadlineStardate <= 0.0 || SD <= M.DeadlineStardate || (Budgeted(M) && M.State == EMissionState::Active)) { continue; }
		if (M.State == EMissionState::Offered || M.Kind == EMissionKind::Assist)
		{
			Close(M, EMissionState::Expired, M.Kind == EMissionKind::Assist ? TEXT("their work went on without us") : TEXT("the offer lapsed"));
			if (M.Kind == EMissionKind::Assist) { if (UFleetSubsystem* F = Fleet()) { F->SetAssistMission(M.TargetName, -1); } }   // her file no longer points at it
			if (M.Source == EMissionSource::Starfleet) { Standing -= M.Standing / 4; Report(FString::Printf(TEXT("%s: Starfleet's orders went unanswered. Noted at Command."), *M.Title), FLinearColor(1.f, 0.75f, 0.5f), ECrewDepartment::Operations); }
		}
		else if (M.Kind == EMissionKind::Impact)
		{
			// The hour came. Where the rock is real it says itself how it ends (held in the beam, or slowed, it may be
			// late): the deadline follows it. Without one, it lands.
			if (const AImpactor* Rock = AImpactor::FindByName(GetWorld(), M.Site)) { M.DeadlineStardate = SD + FMath::Max(Rock->HoursToClosest(), 1.0) / 24.0; Dirty(); }
			else { ImpactOutcome(M.Site, M.TargetName, EImpactOutcome::Struck); }
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

void UMissionSubsystem::PayScience(const FMission& M, const FString& Note)
{
	if (M.ScienceEarned <= 0) { return; }
	if (UScienceLogSubsystem* L = GetGameInstance() ? GetGameInstance()->GetSubsystem<UScienceLogSubsystem>() : nullptr)
	{
		L->AddReward(FString::Printf(TEXT("mission/%d"), M.Id), M.TargetName.IsEmpty() ? FString(TEXT("Mission")) : M.TargetName, M.Title, M.ScienceEarned, Note);
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
	// A place named from outside (a proposal, a dev rock): taken as given, the kind's own rules aside.
	const bool bExpansion = M.Kind == EMissionKind::Outpost || M.Kind == EMissionKind::Colony || M.Kind == EMissionKind::Starbase;
	if (!ForcedTarget.IsEmpty())
	{
		if (bExpansion && HasOpenExpansion(M.Kind, M.Kind == EMissionKind::Colony ? ForcedSite : ForcedTarget)) { return false; }   // one order a system
		M.TargetName = ForcedTarget; M.Site = ForcedSite;
		M.TargetKind = M.Kind == EMissionKind::Colony || M.Kind == EMissionKind::Impact ? EMissionTarget::World : EMissionTarget::System;
		return true;
	}
	// The galaxy's own names: a world with a surface in another system for a survey or a plague, a station for a
	// delivery or a parley, a system for a patrol; never where the ship is.
	switch (M.Kind)
	{
	case EMissionKind::Survey: case EMissionKind::Medical: case EMissionKind::Delivery: case EMissionKind::Diplomacy: case EMissionKind::Patrol: break;
	case EMissionKind::Explore:
	{
		// An unknown star of the catalogue, one of the nearest few.
		const UExplorationSubsystem* X = Exploration();
		const FCatalogueEntry* E = X ? X->PickUnknown(R, false) : nullptr;
		if (!E) { return false; }
		M.TargetKind = EMissionTarget::System; M.TargetName = E->Name(); return true;
	}
	case EMissionKind::FirstContact:
	{
		UExplorationSubsystem* X = Exploration();
		if (!X) { return false; }
		M.TargetKind = EMissionTarget::Vessel; M.TargetName = FString::Printf(TEXT("Unknown vessel %d"), M.Id); M.Group = FName(*X->NewSpeciesName(R)); return true;
	}
	case EMissionKind::PreWarp:
	{
		// The lit world of the system the ship is in.
		const UExplorationSubsystem* X = Exploration();
		const FCatalogueEntry* E = X ? X->Here() : nullptr;
		if (!E || E->PreWarpWorld.IsEmpty()) { return false; }
		M.TargetKind = EMissionTarget::World; M.TargetName = E->PreWarpWorld; M.Group = FName(*FString::Printf(TEXT("%s people"), *E->PreWarpWorld)); return true;
	}
	case EMissionKind::Beacon: M.TargetKind = EMissionTarget::None; return true;   // the signal gives the target when decoded
	case EMissionKind::TimeLoop: case EMissionKind::Leviathan: M.TargetKind = EMissionTarget::None; return true;   // it is where the ship is
	case EMissionKind::Outpost: case EMissionKind::Colony: case EMissionKind::Starbase:
	{
		// Somewhere the Federation may found it (the frontier's rules); a colony's order names the world.
		const UFrontierSubsystem* F = Frontier();
		const ESettlementKind SK = M.Kind == EMissionKind::Outpost ? ESettlementKind::Outpost : M.Kind == EMissionKind::Colony ? ESettlementKind::Colony : ESettlementKind::Starbase;
		FString World;
		TSet<FString> Busy; for (const FMission& O : Missions) { if (O.IsOpen() && O.Kind == M.Kind) { Busy.Add(O.Kind == EMissionKind::Colony ? O.Site : O.TargetName); } }   // one order of a kind a system
		const FCatalogueEntry* E = F ? F->PickSystem(SK, R, World, &Busy) : nullptr;
		if (!E) { return false; }
		if (M.Kind == EMissionKind::Colony) { M.TargetKind = EMissionTarget::World; M.TargetName = World; M.Site = E->Name(); }
		else { M.TargetKind = EMissionTarget::System; M.TargetName = E->Name(); }
		return true;
	}
	case EMissionKind::Impact: return PickInhabitedWorld(M, R, M.Source == EMissionSource::Event);
	default: return true;   // the events bring their own target
	}
	UWorld* W = GetWorld();
	if (!W) { return false; }
	TArray<AStarSystem*> Systems; for (TActorIterator<AStarSystem> It(W); It; ++It) { if (It->bCharted) { Systems.Add(*It); } }   // nowhere nobody has been
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
	if (!PickTarget(M, R)) { M.Id = -1; return M; }   // nowhere to send it: the caller drops it
	// Reachable at any pace: the leg at warp 7, then two to four days to do the thing.
	M.DeadlineStardate = Stardate() + TravelDays(M) + R.FRandRange(2.0, 4.0);
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
	case EMissionKind::Explore:
	{
		M.Title = FString::Printf(TEXT("Chart %s"), *T); M.Standing = 20;
		M.Brief = FString::Printf(TEXT("Starfleet wants %s charted: nobody has been there. The leg is a long one and the chart is the ship's to make: the star, its worlds, and whatever waits there."), *T);
		FMissionStage S0 = Stage(TEXT("In the system"), TEXT("The star fills the forward screen and the sensors count its worlds. Starfleet wants a chart, and a chart means the survey."));
		FMissionOption Sv = Opt(TEXT("Complete the survey: the star and every world scanned"), S::Science, 0.f, D::Science, TEXT("Discovery"), TEXT(""), TEXT("Every body in the log; the chart is the best Starfleet will see this year."), TEXT(""), 1, 1, false, 0.f, 300);
		Sv.bNeedsSurvey = true; S0.Options.Add(Sv);
		S0.Options.Add(Opt(TEXT("Send the shuttle down to the most promising world"), S::Science, 55.f, D::Science, TEXT("Away.Routine"), TEXT("Away.Danger"), TEXT("Samples, images, a flag on a ridge; the chart is thin but the world is known."), TEXT("The landing is rough and the party comes back with less than they went for."), 1, 1, false, 0.3f, 180));
		FMissionStage S1 = Stage(TEXT("The chart"), TEXT("The data is in. It wants a name and a signature."), false);
		S1.Options.Add(Opt(TEXT("Name it and file the chart"), S::Command, 25.f, D::Command, TEXT("Discovery"), TEXT(""), TEXT("Filed. The name goes on the charts of every ship in the fleet."), TEXT("Filed, after a fashion; the cartographers will tidy it."), -1, -1, false, 0.f, 60));
		M.Stages = { S0, S1 }; break;
	}
	case EMissionKind::FirstContact:
	{
		const FString P = M.Group.ToString();
		M.Title = TEXT("First contact"); M.Standing = 40;
		M.Brief = TEXT("A vessel of no known design has come out of the dark and holds station off the bow, her lights slow and strange. Nobody has met these people. Whatever is said in the next hour will be the first of it.");
		FMissionStage S0 = Stage(TEXT("Approach"), TEXT("She waits. Her hull is nothing in the registers; her drive signature is nothing in the books."));
		S0.Options.Add(Opt(TEXT("Hail on all frequencies, every protocol"), S::Diplomacy, 50.f, D::Command, TEXT("Contact.First"), TEXT("Contact.Hostile"), TEXT("A reply, in mathematics first, then in something like a greeting."), TEXT("Silence, then a burst that scrambles the forward sensors for a minute: a warning, or fear."), 1, 1, false, 0.f));
		S0.Options.Add(Opt(TEXT("Observe and record before speaking"), S::Science, 45.f, D::Science, TEXT("Contact.First"), TEXT(""), TEXT("An hour of patient listening gives the linguists a start; when the ship speaks, it is heard."), TEXT("She grows restless under the sensors; the opening is narrower."), 1, 1, false, 0.f, 100));
		S0.Options.Add(Opt(TEXT("Shields up and hold position"), S::Security, 30.f, D::Security, TEXT("Contact.Hostile"), TEXT("Contact.Hostile"), TEXT("The shields come up; she sees it and her own lights change. The security watch approves and nobody else does."), TEXT("The shields are slow, and she sees that too."), 2, 2, false, 0.f));
		FMissionStage S1 = Stage(TEXT("The exchange"), FString::Printf(TEXT("Words, or something like them, pass. They call themselves the %s. What they want is still the question."), *P));
		S1.Options.Add(Opt(TEXT("Let the linguists work through the night"), S::Science, 50.f, D::Science, TEXT("Contact.First"), TEXT(""), TEXT("By morning there is a grammar and a first conversation: they are travellers, curious, and wary of ships that arm themselves."), TEXT("The language will not open; gestures carry the rest, and gestures can be misread."), 3, 3, false, 0.f, 150));
		S1.Options.Add(Opt(TEXT("Offer the ship's own records: who we are, where we come from"), S::Diplomacy, 50.f, D::Command, TEXT("Contact.Friendly"), TEXT("Contact.Hostile"), TEXT("They answer in kind: a history, their worlds, a star chart none of ours shows."), TEXT("The offer reads as a demand; the exchange cools."), 3, 3, false, 0.f, 100));
		S1.Options.Add(Opt(TEXT("Invite a delegation aboard"), S::Diplomacy, 60.f, D::Command, TEXT("Contact.Friendly"), TEXT("Away.Danger"), TEXT("Three of them in the transporter room, taller than anyone expected; the mess hall has never been so quiet, or so warm afterwards."), TEXT("Something in the air or the light does them harm and their ship takes them back fast, and angry."), 3, 3, false, 0.2f, 200));
		FMissionStage S2 = Stage(TEXT("A hard start"), TEXT("Her weapons, if that is what they are, are live. Words will have to carry more than they should."));
		S2.Options.Add(Opt(TEXT("Lower the shields and hail"), S::Diplomacy, 60.f, D::Command, TEXT("Contact.First"), TEXT("Contact.Hostile"), TEXT("A long silence, then a reply; the start was bad and they say so, but they stay."), TEXT("She fires once across the bow and goes. The histories will not be kind."), 1, -1, true, 0.1f));
		S2.Options.Add(Opt(TEXT("Hold, and wait for her to speak"), S::Command, 45.f, D::Command, TEXT("Contact.First"), TEXT("Contact.Hostile"), TEXT("She speaks first, a plain carrier wave and a tone, and the linguists have their start."), TEXT("She leaves. The chance goes with her."), 1, -1, true, 0.f));
		FMissionStage S3 = Stage(TEXT("Terms"), FString::Printf(TEXT("The %s are ready to talk about what comes next."), *P));
		S3.Options.Add(Opt(TEXT("Offer a treaty of friendship between their people and the Federation"), S::Diplomacy, 55.f, D::Command, TEXT("Contact.Friendly"), TEXT("Contact.Hostile"), TEXT("Accepted, in their way: a gift of their charts and a promise of their next ship at our starbase. First contact, and it holds."), TEXT("They are not ready for treaties; they go, politely, with the door open a crack."), -1, -1, false, 0.f, 400));
		S3.Options.Add(Opt(TEXT("Trade: medical supplies for their charts"), S::Operations, 45.f, D::Operations, TEXT("Contact.Friendly"), TEXT(""), TEXT("Their charts for a hold of supplies; the science officer will be a week reading them."), TEXT("The trade falls through over what a unit of anything is worth."), -1, -1, false, 0.f, 300));
		S3.Options.Add(Opt(TEXT("Part in peace, and leave the rest for the diplomats"), S::Command, 25.f, D::Command, TEXT("Contact.First"), TEXT(""), TEXT("A last exchange of signals and she turns away. The diplomats will have their day."), TEXT("She goes without a word; a start, all the same."), -1, -1, false, 0.f, 150));
		M.Stages = { S0, S1, S2, S3 }; break;
	}
	case EMissionKind::PreWarp:
	{
		M.Title = FString::Printf(TEXT("A civilisation on %s"), *T); M.Standing = 12; M.FailLoss = 30;
		M.Brief = FString::Printf(TEXT("%s has a people of its own: their towns, their roads, a people who have not left their world. The Prime Directive applies. Nothing the ship does here can be undone."), *T);
		FMissionStage S0 = Stage(TEXT("In orbit"), FString::Printf(TEXT("%s turns below, its lights coming on along the terminator. Their sky is clear of anything that might see the ship, so far."), *T));
		S0.Options.Add(Opt(TEXT("Observe from high orbit, sensors passive"), S::Science, 40.f, D::Science, TEXT("Discovery"), TEXT(""), TEXT("Days of patient listening: their broadcasts, their cities, their arguments. A people, recorded."), TEXT("The sensors catch little through their weather; the record is thin."), -1, -1, false, 0.f, 150));
		S0.Options.Add(Opt(TEXT("A survey party in disguise, on the ground"), S::Science, 60.f, D::Science, TEXT("Away.Routine"), TEXT("Away.Danger"), TEXT("The party walks their streets for a day and comes back with more than any orbit could give."), TEXT("A face seen too closely; the party comes back in a hurry and leaves a story behind."), -1, -1, false, 0.35f, 250));
		S0.Options.Add(Opt(TEXT("Make contact: they are ready"), S::Diplomacy, 70.f, D::Command, TEXT("Contact.First"), TEXT("Contact.Hostile"), TEXT("They were ready. A landing in daylight, a crowd, and a world that will never be the same, for the better."), TEXT("They were not ready. A world changed by a ship in the sky, and the Directive broken."), -1, -1, true, 0.f, 200));
		M.Stages = { S0 }; break;
	}
	case EMissionKind::Beacon:
	{
		M.Title = TEXT("An ancient beacon"); M.Standing = 15;
		M.Brief = TEXT("Something older than any people known is transmitting from the edge of the system: a pattern, patient, repeating, and pointed somewhere.");
		FMissionStage S0 = Stage(TEXT("The signal"), TEXT("The pattern is mathematics, then coordinates, then something nobody can read."), false);
		S0.Options.Add(Opt(TEXT("Decode the coordinates"), S::Science, 50.f, D::Science, TEXT("Discovery"), TEXT(""), TEXT("It points at a star in the catalogue nobody has visited."), TEXT("The coordinates will not resolve; the signal keeps its secret."), 1, -1, false, 0.f, 100));
		S0.Options.Add(Opt(TEXT("Record it and let Starfleet's cryptographers have it"), S::Operations, 30.f, D::Science, TEXT(""), TEXT(""), TEXT("Recorded; a copy goes home by subspace."), TEXT("Recorded, badly."), -1, -1, false, 0.f, 40));
		FMissionStage S1 = Stage(TEXT("The source"), TEXT("The signal is louder here. Somewhere in this system is what sent it."));
		S1.Options.Add(Opt(TEXT("Search the system for the source"), S::Science, 55.f, D::Science, TEXT("Discovery"), TEXT("Away.Danger"), TEXT("On a dead moon, a structure older than the mountains, still transmitting: a find for the histories."), TEXT("Weeks of searching turn up nothing but the signal; it goes on without them."), -1, -1, false, 0.15f, 500));
		M.Stages = { S0, S1 }; break;
	}
	case EMissionKind::Outpost:
	{
		M.Title = FString::Printf(TEXT("An outpost at %s"), *T); M.Standing = 15;
		M.Brief = FString::Printf(TEXT("Starfleet wants the Federation's flag in %s: an outpost module, a crew of thirty, and a station off one of its worlds. The module waits in a starbase's yard."), *T);
		FMissionStage S0 = StageAt(TEXT("The module"), TEXT("The outpost module is a hundred metres of prefabricated station in the yard, and thirty people with their lives in crates."), EMissionTarget::Station, TEXT(""));
		S0.Options.Add(Opt(TEXT("Take on the module and its crew"), S::Operations, 35.f, D::Operations, TEXT(""), TEXT(""), TEXT("Loaded, lashed and logged; the yard waves them off."), TEXT("Loaded, after a day of arguments about the manifest."), 1, 1, false, 0.f));
		S0.Options.Add(Opt(TEXT("Load it fast and go"), S::Engineering, 45.f, D::Engineering, TEXT(""), TEXT("Away.Danger"), TEXT("Six hours, not a day; the chief says never again."), TEXT("A crate goes adrift in the bay and a hand with it."), 1, 1, false, 0.1f));
		FMissionStage S1 = StageAt(TEXT("The site"), FString::Printf(TEXT("%s's worlds turn below the sensors. The outpost will be off one of them for a century; the choice is the ship's."), *T), EMissionTarget::System, T);
		S1.Options.Add(Opt(TEXT("Survey the worlds for a site"), S::Science, 45.f, D::Science, TEXT("Discovery"), TEXT(""), TEXT("The science watch picks a world with water and a quiet sky."), TEXT("The survey is thin; the first solid world will have to do."), 2, 2, false, 0.f, 80));
		S1.Options.Add(Opt(TEXT("Put it off the first solid ground"), S::Engineering, 35.f, D::Engineering, TEXT(""), TEXT(""), TEXT("Done; it is not elegant."), TEXT("Done, in a worse orbit than meant."), 2, 2, false, 0.f));
		FMissionStage S2 = StageAt(TEXT("Raising the outpost"), TEXT("The module is in orbit off the world; now it becomes a station."), EMissionTarget::System, T);
		S2.Options.Add(Opt(TEXT("Land the module and raise the outpost"), S::Engineering, 50.f, D::Engineering, TEXT("Away.Routine"), TEXT("Away.Danger"), TEXT("The dome is up and the lights are on; the crew's first signal home is a photograph."), TEXT("A strut fails on the way in; a day lost and someone hurt."), -1, 2, false, 0.15f, 60));
		M.Stages = { S0, S1, S2 }; break;
	}
	case EMissionKind::Colony:
	{
		M.Title = FString::Printf(TEXT("Settle %s"), *T); M.Standing = 25;
		M.Brief = FString::Printf(TEXT("Eight hundred settlers have asked Starfleet for %s, and Starfleet has asked the ship: carry them from the starbase where they wait, land them, and see them through their first weeks."), *T);
		FMissionStage S0 = StageAt(TEXT("Embarkation"), TEXT("Eight hundred people and their seed stock, their animals and their arguments, on a starbase's concourse."), EMissionTarget::Station, TEXT(""));
		S0.Options.Add(Opt(TEXT("Embark the settlers and their seed stock"), S::Medicine, 40.f, D::Medical, TEXT(""), TEXT(""), TEXT("Every one of them examined, inoculated and berthed; the ship is a town for a week."), TEXT("Berthed, the examinations half done; sickbay will catch up on the way."), 1, 1, false, 0.f));
		S0.Options.Add(Opt(TEXT("Take the volunteers and the stores there are"), S::Operations, 35.f, D::Operations, TEXT(""), TEXT(""), TEXT("Six hundred of the keenest and what will fit."), TEXT("Five hundred, and short of everything."), 1, 1, false, 0.f));
		FMissionStage S1 = Stage(TEXT("The landing"), FString::Printf(TEXT("%s below, green and brown and blue; the settlers at the viewports have gone quiet."), *T));
		S1.Options.Add(Opt(TEXT("Shuttle them down a hundred at a time"), S::Engineering, 50.f, D::Engineering, TEXT("Away.Routine"), TEXT("Away.Danger"), TEXT("Eight flights, two days, nobody hurt; the last shuttle brings the chief's tools."), TEXT("The sixth flight lands hard; there are injuries, and the settlers help carry."), 2, 2, false, 0.2f));
		S1.Options.Add(Opt(TEXT("Beam them down and the cargo after"), S::Operations, 45.f, D::Operations, TEXT(""), TEXT(""), TEXT("Twenty at a time all day; the transporter chief sleeps for a week."), TEXT("A pattern buffer fault; one group is a long hour in the buffer, and shaken."), 2, 2, false, 0.f));
		FMissionStage S2 = Stage(TEXT("The first weeks"), TEXT("Tents, then prefabs, then the first crop. The ship can stay a while."));
		S2.Options.Add(Opt(TEXT("See them through the first weeks"), S::Medicine, 45.f, D::Medical, TEXT("Away.Routine"), TEXT(""), TEXT("A fever is caught early, a well is sited, a child is born aboard; they will be all right."), TEXT("A fever runs through the camp before it is caught; they will be all right, mostly."), -1, -1, false, 0.f, 100));
		S2.Options.Add(Opt(TEXT("Leave them to it and report"), S::Command, 30.f, D::Command, TEXT(""), TEXT(""), TEXT("They wave the ship off. The chief says they will manage."), TEXT("They watch the ship go."), -1, -1, false, 0.f));
		M.Stages = { S0, S1, S2 }; break;
	}
	case EMissionKind::Starbase:
	{
		M.Title = FString::Printf(TEXT("A starbase for %s"), *T); M.Standing = 40;
		M.DeadlineStardate += UFrontierSubsystem::StarbaseWorksHours / 24.0;   // the works themselves, on top of the leg and the margin
		M.Brief = FString::Printf(TEXT("Starfleet will build a starbase in %s: the construction corps and the first hull sections wait at a starbase, the works take days, and the ship holds the ground."), *T);
		FMissionStage S0 = StageAt(TEXT("The corps"), TEXT("Four hundred of the construction corps and the first hull sections: a starbase in kit form."), EMissionTarget::Station, TEXT(""));
		S0.Options.Add(Opt(TEXT("Embark the construction corps and the hull sections"), S::Engineering, 45.f, D::Engineering, TEXT(""), TEXT(""), TEXT("Embarked; the bay has never been so full."), TEXT("Embarked, over two days instead of one."), 1, 1, false, 0.f));
		FMissionStage S1 = StageAt(TEXT("The keel"), FString::Printf(TEXT("%s, and the place the station will be. The corps are ready."), *T), EMissionTarget::System, T);
		S1.Options.Add(Opt(TEXT("Lay the keel"), S::Engineering, 50.f, D::Engineering, TEXT("Discovery"), TEXT(""), TEXT("The keel is laid, the first section is lit, the works begin."), TEXT("The first section is set wrong and reset; a day lost, then the works begin."), 2, 2, false, 0.f));
		FMissionStage S2 = StageAt(TEXT("The works"), TEXT("The station grows a section a day. The ship holds station while it does."), EMissionTarget::System, T);
		S2.Options.Add(Opt(TEXT("Hold station and guard the works"), S::Tactical, 40.f, D::Security, TEXT(""), TEXT(""), TEXT("A quiet watch; the corps wave."), TEXT("A raider's probe, driven off; the corps lose a day."), 2, 2, false, 0.f));
		FMissionOption Cm = Opt(TEXT("Commission the starbase"), S::Command, 0.f, D::Command, TEXT("Discovery"), TEXT(""), TEXT("Her number is read, her flag is up and her patrol ships are out: a home in new space."), TEXT(""), -1, -1, false, 0.f, 100);
		Cm.bNeedsWorks = true; S2.Options.Add(Cm);
		M.Stages = { S0, S1, S2 }; break;
	}
	case EMissionKind::Impact:
	{
		// The rock: named for the stardate, one in three a comet; the hours to its landing budget the deadline (an
		// order's leg counted in).
		const bool bComet = R.FRand() < 0.34f;
		M.Site = FString::Printf(TEXT("%s %d-%c"), bComet ? TEXT("Comet") : TEXT("Asteroid"), (int32)Stardate(), TCHAR('A' + (M.Id % 26)));
		M.ImpactHours = (Source == EMissionSource::Event ? 0.0 : TravelDays(M) * 24.0) + R.FRandRange(20.0, 44.0);
		M.DeadlineStardate = Stardate() + M.ImpactHours / 24.0;
		M.Title = FString::Printf(TEXT("%s: %s"), *M.Site, *T); M.Standing = 35; M.FailLoss = 40;
		M.Brief = FString::Printf(TEXT("%s on a closing course with %s: %s a kilometre or more across, and in %.0f hours it lands. %s has people on it."), *M.Site, *T, bComet ? TEXT("dirty ice") : TEXT("rock"), M.ImpactHours, *T);
		FMissionStage S0 = Stage(TEXT("The track"), TEXT("The sensors have it; the rest is what to do before it arrives."), false);
		S0.Options.Add(Opt(TEXT("Refine its orbit"), S::Science, 40.f, D::Science, TEXT("Discovery"), TEXT(""), TEXT("The track is sure now: the hour, the place, the size of it."), TEXT("The track is rough; the hour is a guess."), 1, 1, false, 0.f, 80));
		S0.Options.Add(Opt(TEXT("Warn the world and begin the evacuation"), S::Operations, 45.f, D::Operations, TEXT(""), TEXT(""), TEXT("The word goes down and the roads fill; if it lands, fewer die."), TEXT("The word goes down and is not believed; some leave."), 1, 1, false, 0.f));
		FMissionStage S1 = StageAt(TEXT("Divert it"), TEXT("The rock turns slowly in the lights, a mountain out of place. There is time for one thing at a time."), EMissionTarget::Vessel, M.Site);
		S1.Options.Add(Opt(TEXT("Tractor it off its course"), S::Engineering, 50.f, D::Engineering, TEXT(""), TEXT(""), TEXT("The beam holds for an hour and the course bends by a hair: enough."), TEXT("The beam loses its grip; the course is what it was."), -1, 1, false, 0.f));
		S1.Options.Add(Opt(TEXT("Break it up with torpedoes"), S::Tactical, 45.f, D::Security, TEXT(""), TEXT(""), TEXT("A spread, placed; the rock comes apart and the pieces go wide."), TEXT("The spread scars it and it rolls on."), -1, 1, false, 0.f));
		S1.Options.Add(Opt(TEXT("A party to plant charges on it"), S::Engineering, 60.f, D::Engineering, TEXT("Away.Routine"), TEXT("Away.Danger"), TEXT("The charges are set in a seam and the rock splits clean."), TEXT("The seam gives early; the party is got off, not all of it whole."), -1, 1, false, 0.3f, 120));
		M.Stages = { S0, S1 }; break;
	}
	case EMissionKind::TimeLoop:
	{
		// A fold in time: a lock left on an old road by the people who raised the rings and the standing stones. It
		// holds a ship with a live drive in one closed watch, which comes round until she is got out of it; and there
		// are four ways out, each with its own price (LeaveFold): answer the stone, go through it cold, break it, or
		// ride the closing and be thrown days on. One stage; an option costs hours of the watch; what it teaches is
		// kept from pass to pass (Marks), what it makes ready is not (Readied). The stone takes one answer of three,
		// another in every fold (Secret): the dead hull and the stone's faces each rule one out, and a wrong answer
		// costs the pass, an hour of every pass after, and a blow to the ship. The twelfth pass is the last (ComeRound).
		M.Title = TEXT("A fold in time"); M.Standing = 40; M.TargetKind = EMissionTarget::None; M.PassHours = 5.0;
		M.Secret = R.RandRange(0, (1 << 20) - 1);
		M.Brief = TEXT("The chronometers have fallen out with the stars: every clock aboard agrees with every other, and none with the pulsars. Science reports a fold in space across the course, closing on the ship. Five hours, at the rate it is going.");
		// What the ship's earlier folds left: one answered, and Science knows the next at sight; one broken, and the
		// next stone is the harder to satisfy.
		const bool bWary = Remembers(TEXT("Lock.Broken"));
		if (Remembers(TEXT("Lock.Answered"))) { M.Marks.Add(TEXT("Met")); M.Marks.Add(TEXT("Curve")); M.Marks.Add(TEXT("Anchor")); }
		if (bWary) { M.Marks.Add(TEXT("Wary")); }
		FMissionOption Scan = Opt(TEXT("Close in and scan the fold"), S::Science, 55.f, D::Science, TEXT("Discovery"), TEXT(""), TEXT("It is a closed curve: space and time bent round to meet themselves, and the ship inside. It has her by the drive's own field, and when it closes the field is what it will tear. There is other metal in the fold, cold and old; and at the seam a hard point the whole of it hangs from, like a net from a float."), TEXT("The sensors return the ship's own echo and nothing else."), 0, 0, false, 0.f, 150);
		Scan.Hours = 2.f; Scan.GivesMark = TEXT("Curve");
		FMissionOption Hail = Opt(TEXT("Hail Starfleet"), S::Operations, 35.f, D::Operations, TEXT(""), TEXT(""), TEXT("The hail comes back to the ship an hour before it was sent, in the communications officer's own voice."), TEXT("No answer, on any band."), 0, 0, false, 0.f, 30);
		Hail.Hours = 1.f; Hail.GivesMark = TEXT("Hailed");
		FMissionOption Doctor = Opt(TEXT("Ask the doctor what ails them"), S::Medicine, 55.f, D::Medical, TEXT(""), TEXT(""), TEXT("It is not memory, the doctor says. It is an echo: what a watch learns is carried into the next, in the hands more than the head. And if the fold carries that through its closing, it might carry a ship."), TEXT("Fatigue, the doctor says, and prescribes sleep."), 0, 0, false, 0.f, 60);
		Doctor.Hours = 1.f; Doctor.MinPass = 1; Doctor.GivesMark = TEXT("Echoes");
		FMissionOption Probe = Opt(TEXT("Send a probe to the hard point"), S::Science, 70.f, D::Science, TEXT("Discovery"), TEXT(""), TEXT(""), TEXT("The probe's track bends away from the seam, and it is lost."), 0, 0, false, 0.f, 120);
		Probe.SuccessText = FString::Printf(TEXT("A stone the size of a shuttle, worked on every face and older than anything in the catalogue: the same hands raised the rings and the standing stones. The fold hangs from it. It is a lock on an old road, and it is calling, five beats over and over: %s. It wants an answer."), LockCall(M));
		Probe.Hours = 3.f; Probe.NeedsMark = TEXT("Curve"); Probe.GivesMark = TEXT("Anchor");
		FMissionOption Hull = Opt(TEXT("Board the dead hull"), S::Science, 65.f, D::Science, TEXT("Away.Routine"), TEXT("Away.Danger"), TEXT(""), TEXT("Every hatch is sealed from within, and the party's air runs short before one is cut. They are brought back with nothing."), 0, 0, false, 0.2f, 80);
		Hull.SuccessText = FString::Printf(TEXT("Another people's ship, of no make in the catalogue, held here as we are and long dead in it. %s"), LockHullClue(LockHullTried(M)));
		Hull.Hours = 2.f; Hull.NeedsMark = TEXT("Curve"); Hull.GivesMark = TEXT("Hull");
		FMissionOption Faces = Opt(TEXT("Read the stone's faces"), S::Science, 75.f, D::Science, TEXT("Discovery"), TEXT(""), LockCarvedClue(LockCarvedOut(M)), TEXT("The marks will not resolve at this range before the hours given to them are gone."), 0, 0, false, 0.f, 100);
		Faces.Hours = 2.f; Faces.NeedsMark = TEXT("Anchor"); Faces.GivesMark = TEXT("Faces");
		FMissionOption Brace = Opt(TEXT("Clear the forward decks and rig for a blow"), S::Security, 35.f, D::Security, TEXT(""), TEXT(""), TEXT("The forward sections are cleared and every hand is at a brace. It is to do again if the watch comes round."), TEXT("The drill goes badly: sections report late, and not all of them."), 0, 0, false, 0.f);
		Brace.Hours = 1.f; Brace.NeedsMark = TEXT("Curve"); Brace.MakesReady = TEXT("Braced");
		FMissionOption Cold = Opt(TEXT("Shut the warp core down cold"), S::Engineering, 65.f, D::Engineering, TEXT(""), TEXT(""), TEXT("With the core cold there is no field for the fold to grip. It closes on dead metal and goes through her like weather: to the lock she is drift, and drift goes free."), TEXT("The core will not go cold in the hours given to it."), -1, 0, false, 0.f, 60);
		Cold.Hours = 4.f; Cold.NeedsMark = TEXT("Curve"); Cold.Exit = TEXT("Cold"); Cold.ExitStanding = 35;
		// (Breaking it needs nothing known but where it is: the quickest way out, and the one taken blind.)
		FMissionOption Break = Opt(TEXT("Torpedo the hard point"), S::Tactical, 75.f, D::Security, TEXT(""), TEXT(""), TEXT("The hard point breaks. The fold lets go all at once and throws the ship clear like a shot from a sling; astern there is dust, and nothing holding anything."), TEXT("The spread bursts short: the fold bends its track away from the seam."), -1, 0, false, 0.2f);
		Break.Hours = 1.f; Break.NeedsMark = TEXT("Curve"); Break.Exit = TEXT("Broken"); Break.ExitStanding = 20; Break.SaferWhenReady = TEXT("Braced");
		FMissionOption Ride = Opt(TEXT("Ride the closing through"), S::Helm, 85.f, D::Helm, TEXT("Discovery"), TEXT(""), TEXT("She holds the fold's own pitch as it closes, and is carried through the closing as the echoes are: out, clear, and no hour of her own spent. The stars' hours are another matter."), TEXT("She slips off the pitch, and the closing takes her as it did before."), -1, 0, false, 0.25f, 150);
		Ride.Hours = 1.f; Ride.NeedsMark = TEXT("Curve"); Ride.NeedsMark2 = TEXT("Echoes"); Ride.Exit = TEXT("Thrown"); Ride.ExitStanding = 30; Ride.bFailEndsPass = true; Ride.SaferWhenReady = TEXT("Braced");
		FMissionOption Run = Opt(TEXT("Come about and run for clear space"), S::Helm, 40.f, D::Helm, TEXT(""), TEXT(""), TEXT("The helm steers a straight line and the stars draw it into a circle: every heading bends back to the same place. Then the fold closes."), TEXT("She runs, and the fold runs with her. Then it closes."), 0, 0, false, 0.f);
		// (An order, not an attempt: there is no check in it, and nothing to be got by repeating it.)
		Run.bSure = true; Run.bSuccessEndsPass = true; Run.bFailEndsPass = true;
		FMissionStage S0 = Stage(TEXT("The forenoon watch"), FString(), false);
		S0.Options = { Scan, Hail, Doctor, Probe, Hull, Faces };
		// The three answers: one is the stone's; the others are refused whoever sends them, and are known for it after.
		for (int32 k = 0; k < 3; ++k)
		{
			FMissionOption Answer = Opt(LockAnswerText(k), S::Operations, bWary ? 95.f : 80.f, D::Operations, TEXT("Discovery"), TEXT(""), TEXT("The stone takes the answer. The fold opens along its seam like a lock gate, and the road beyond it is clear."), TEXT("The answer goes out ragged, out of time with the call. The stone waits: it has not refused."), -1, 0, false, 0.f, 250);
			Answer.Hours = 1.f; Answer.NeedsMark = TEXT("Anchor"); Answer.NotIfMark = LockRefusedMark(k); Answer.Exit = TEXT("Answered"); Answer.ExitStanding = 50;
			Answer.WrongBlow = 25.f; Answer.SaferWhenReady = TEXT("Braced");
			if (k != LockAnswer(M))
			{
				// (Everything the board shows of it is as the right one's: only the stone knows the difference.)
				Answer.bWrong = true; Answer.FailText = TEXT("The stone goes dark: refused. The fold closes at once, and harder than before.");
				Answer.FailGivesMark = LockRefusedMark(k); Answer.bFailEndsPass = true; Answer.FailShortensPass = 1.f;
			}
			S0.Options.Add(Answer);
		}
		S0.Options.Append({ Brace, Cold, Break, Ride, Run });
		M.Stages = { S0 };
		M.Stages[0].Text = LoopWatchText(M);
		break;
	}
	case EMissionKind::Leviathan:
	{
		// A leviathan alongside (the helpers above): one stage, and so many hours of work before it is against the
		// hull. Three findings each strike out one reading of what it wants; four gifts, one of them wanted, each
		// with a price of its own that is paid whether it was wanted or not; and four ways of parting from it
		// (LeaveLeviathan): given what it wants, left to take it, driven off by the ship's own guns, or left astern
		// at warp, to follow. Nothing here waits on the clock: the captain's order ends it, whenever it is given.
		M.Title = TEXT("A leviathan alongside"); M.Standing = 0; M.TargetKind = EMissionTarget::None;
		// One left astern is the same creature come back: the same want, what was learned of it, and less patience.
		const FMission* Was = nullptr;
		// (Only as the thing that happens, alongside at once: an offer of one, which only the console makes, is a
		// stranger, and leaves the one that follows still to come.)
		if (Source == EMissionSource::Event && Remembers(TEXT("Leviathan.Astern")))
		{
			for (const FMission& Old : Missions) { if (Old.Kind == EMissionKind::Leviathan && Old.Exit == FName(TEXT("Slipped"))) { Was = &Old; } }
			Legacy.Remove(TEXT("Leviathan.Astern"));
		}
		if (Was)
		{
			M.Secret = Was->Secret; M.Site = Was->Site; M.Marks = Was->Marks; M.Pass = Was->Pass + 1;
			M.Brief = TEXT("It is back. The leviathan that was left astern has come up with the ship again and laid itself along the starboard beam as before: the same one, by the stone in its case. It is edging in.");
		}
		else
		{
			M.Secret = R.RandRange(0, (1 << 20) - 1);
			M.Site = FString::Printf(TEXT("Leviathan %d-%c"), (int32)Stardate(), TCHAR('A' + (M.Id % 26)));
			M.Brief = TEXT("Something has come up from astern and laid itself along the starboard beam: a living thing nine kilometres long, cased in rock and wreck, pale lights in the seams of its case and one worked stone set among the wreck. It keeps the ship's course and speed to the metre, and it is edging in. It has not said what it wants.");
			// What the ship's earlier meetings left: on the old road (a lock answered, and it may know the beats; one
			// broken, and it comes on the faster, unless its kind have had better of her since), and with its kind (one
			// given what it wanted, and this one's call is read as it comes up; one burned, and this one comes on fast).
			if (Remembers(TEXT("Lock.Answered"))) { M.Marks.Add(TEXT("Road")); }
			if (Remembers(TEXT("Lock.Broken")) && !Remembers(TEXT("Leviathan.Given"))) { M.Marks.Add(TEXT("Wary")); }
			if (Remembers(TEXT("Leviathan.Blooded"))) { M.Marks.Add(TEXT("Shut")); }
			if (Remembers(TEXT("Leviathan.Given"))) { M.Marks.Add(TEXT("Call")); M.Marks.Add(TEXT("Filed")); }
		}
		M.PassHours = LevHours(M, M.Pass);
		M.HoursLeft = M.PassHours;
		FMissionStage S0 = Stage(TEXT("Alongside"), FString(), false);
		// What can be found out: each its hours, each one reading struck out. (Surer checks than the gifts': a crew
		// that carries a gift only three times in four must still do better by finding out than by guessing.)
		FMissionOption Listen = Opt(TEXT("Listen to it across the bands"), S::Operations, 55.f, D::Operations, TEXT("Discovery"), TEXT(""), LevClue(0, LevRuledOut(M, 0)), TEXT("It calls on a dozen bands at once; the hour goes in sorting them."), 0, 0, false, 0.f, 60);
		Listen.Hours = 1.f; Listen.GivesMark = LevSourceMark(0);
		FMissionOption Plot = Opt(TEXT("Plot its course back"), S::Science, 60.f, D::Science, TEXT("Discovery"), TEXT(""), LevClue(1, LevRuledOut(M, 1)), TEXT("The plot is too thin to carry its track past yesterday."), 0, 0, false, 0.f, 80);
		Plot.Hours = 2.f; Plot.GivesMark = LevSourceMark(1);
		FMissionOption Boat = Opt(TEXT("Send a boat down its length"), S::Helm, 65.f, D::Helm, TEXT("Away.Routine"), TEXT("Away.Danger"), LevClue(2, LevRuledOut(M, 2)), TEXT("Its wash throws the boat off twice before she can lie alongside; she comes back with nothing seen."), 0, 0, false, 0.2f, 100);
		Boat.Hours = 2.f; Boat.GivesMark = LevSourceMark(2);
		// (An order: a ship the road knows shows it the road's own beats, and it gives her room, the once.)
		FMissionOption Beats = Opt(TEXT("Show it the road's five beats"), S::Operations, 0.f, D::Operations, TEXT(""), TEXT(""), TEXT("Five beats on the running lights, as the stone gave them. It checks, as its kind check at every lock on the road, and falls back."), TEXT(""), 0, 0, false, 0.f);
		Beats.bSure = true; Beats.Hours = 1.f; Beats.NeedsMark = TEXT("Road"); Beats.GivesMark = TEXT("Hailed");
		S0.Options = { Listen, Plot, Boat, Beats };
		// The four gifts. One is what it wants; the others are done as ordered, and are not (and everything the board
		// shows of a gift is the same whichever it is: only the creature knows the difference).
		struct FLevGift { const TCHAR* Text; S Skill; float Difficulty; D Party; float Hours; float Risk; const TCHAR* FailTag; const TCHAR* Wanted; const TCHAR* NotWanted; const TCHAR* Failed; };
		static const FLevGift Gifts[4] = {
			{ TEXT("Give it plate: the lockers' stock and a strake of hull"), S::Operations, 70.f, D::Operations, 1.f, 0.f, TEXT(""),
				TEXT("It takes the plate as a hand takes a coin, sets it into its case, and turns away down its road."),
				TEXT("The plate drifts down its whole length untouched. It comes on."),
				TEXT("The stock fouls in the lock and is got back aboard.") },
			{ TEXT("Shut the warp core down cold"), S::Engineering, 70.f, D::Engineering, 3.f, 0.f, TEXT(""),
				TEXT("As the field dies its head lifts off the ship, as if a door had shut on a noise. It listens; it finds its road; it goes."),
				TEXT("The core is cold, and it has not slowed. It comes on, and now there is no going to warp."),
				TEXT("The core will not go cold in the hours given to it.") },
			{ TEXT("Lay her lee over it and hold"), S::Helm, 70.f, D::Helm, 2.f, 0.f, TEXT(""),
				TEXT("She spreads her shields head to the star, and it settles into their shadow like a ship into a berth. A day and a night; then it goes."),
				TEXT("Two hours it lies in her shadow, then moves out into the glare. It comes on."),
				TEXT("She cannot be held head to the star so close aboard it.") },
			{ TEXT("Send parties across to tend it"), S::Medicine, 75.f, D::Medical, 2.f, 0.35f, TEXT("Away.Danger"),
				TEXT("It lifts its plates to them down its whole length; under them is a century of grit it could not reach. They work it clean in a watch, and it goes."),
				TEXT("They find nothing under its plates to do, and it rolls to be rid of them. It comes on."),
				TEXT("A plate comes down early; the parties are got off in a hurry.") } };
		for (int32 k = 0; k < 4; ++k)
		{
			const FLevGift& G = Gifts[k];
			const bool bWanted = k == LevWant(M);
			FMissionOption Gift = Opt(G.Text, G.Skill, G.Difficulty, G.Party, TEXT("Beast.Tended"), G.FailTag, G.Wanted, bWanted ? G.Failed : G.NotWanted, -1, 0, false, G.Risk, 200);
			Gift.Hours = G.Hours; Gift.NotIfMark = LevGiftMark(k); Gift.Exit = TEXT("Given"); Gift.ExitStanding = 45;
			if (!bWanted) { Gift.bWrong = true; Gift.FailGivesMark = LevGiftMark(k); }
			S0.Options.Add(Gift);
		}
		// Standing fast: always there, no hours, no check. What it does says what it wanted.
		static const TCHAR* Takes[4] = {
			TEXT("It lays its head along the hull and takes a strake of her plating, lifted like bark, with whatever was behind it. Then it goes."),
			TEXT("It leans on the nacelles until the field dies and the core with it, lifts its head as if listening, and goes."),
			TEXT("It shoulders in under her shields to lie in their shadow, hard against her, and every frame in her feels it. Then it goes."),
			TEXT("It rubs its fouled flank down the length of her, plate grinding on plate. Then it goes.") };
		FMissionOption Stand = Opt(TEXT("Stand fast and let it come"), S::Command, 0.f, D::Command, TEXT(""), TEXT(""), Takes[LevWant(M)], TEXT(""), -1, -1, false, 0.f, 150);
		Stand.bSure = true; Stand.Exit = TEXT("Taken"); Stand.ExitStanding = 10;
		S0.Options.Add(Stand);
		M.Stages = { S0 };
		M.Stages[0].Text = LeviathanText(M);
		break;
	}
	default: break;
	}
	return M;
}

int32 UMissionSubsystem::RaiseEvent(EMissionKind Kind)
{
	FRandomStream R(Rng.RandRange(1, 1 << 20));
	FMission M = MakeMission(Kind, EMissionSource::Event, R);
	if (M.Id < 0) { return -1; }
	M.State = EMissionState::Active; if (Kind != EMissionKind::Impact) { M.DeadlineStardate = Stardate() + (Kind == EMissionKind::Beacon ? 10.0 : 2.0); }
	M.Log.Add(FString::Printf(TEXT("%.1f  %s"), Stardate(), *M.Brief));
	Place(M);
	Missions.Add(M);
	Report(FString::Printf(TEXT("%s It is on the mission board, Captain."), *M.Brief), FLinearColor(1.f, 0.85f, 0.5f), ECrewDepartment::Science);
	Dirty();
	return M.Id;
}

void UMissionSubsystem::AfterOption(FMission& M, const FMissionOption& O, bool bSuccess, bool bOver, bool bSucceeded)
{
	UExplorationSubsystem* X = Exploration();
	switch (M.Kind)
	{
	case EMissionKind::Beacon:
		// Decoded: the signal names the nearest unknown star; with none left it has said all it can.
		if (M.Stage == 1 && M.TargetKind == EMissionTarget::None && bSuccess && !bOver)
		{
			FRandomStream R(M.Id);
			const FCatalogueEntry* E = X ? X->PickUnknown(R, true) : nullptr;
			if (E)
			{
				M.TargetKind = EMissionTarget::System; M.TargetName = E->Name();
				// The leg is budgeted like any order's: the deadline grows to cover it, never shrinks.
				M.DeadlineStardate = FMath::Max(M.DeadlineStardate, Stardate() + TravelDays(M) + 3.0);
				M.Log.Add(FString::Printf(TEXT("      The coordinates resolve to %s, %.1f light-years out."), *E->Name(), E->Spec.DistanceLy));
				Report(FString::Printf(TEXT("The beacon points at %s, Captain: %.1f light-years, uncharted."), *E->Name(), E->Spec.DistanceLy), FLinearColor(0.7f, 1.f, 0.8f), ECrewDepartment::Science);
			}
			else
			{
				M.Log.Add(TEXT("      The coordinates point beyond the catalogue; there is nowhere to follow them yet."));
				Close(M, EMissionState::Completed, TEXT("the signal read, its source beyond reach")); Standing += M.Standing / 2; ++Completed;
				PayScience(M, TEXT("The signal read; its source beyond the catalogue."));
				Report(FString::Printf(TEXT("%s: complete. Starfleet's regard: %+d."), *M.Title, M.Standing / 2), FLinearColor(0.6f, 1.f, 0.6f));
			}
		}
		break;
	case EMissionKind::Explore:
		// Filed: a system still unnamed takes the catalogue's own name.
		if (bOver && bSucceeded && X) { if (const FCatalogueEntry* E = X->Find(M.TargetName)) { if (E->GivenName.IsEmpty()) { FString Reply; X->Name(E->Designation, TEXT(""), Reply); } } }
		break;
	case EMissionKind::FirstContact:
		if (bOver && X)
		{
			const int32 Relation = !bSucceeded ? -1 : bSuccess ? 1 : 0;
			X->RecordContact(M.Group.ToString(), Last.SystemName, Relation, O.Text);
			if (!bSucceeded) { Report(FString::Printf(TEXT("The %s are recorded as hostile. The next ship to meet them will know."), *M.Group.ToString()), FLinearColor(1.f, 0.6f, 0.5f), ECrewDepartment::Science); }
			else { Report(FString::Printf(TEXT("The %s are in the record: first contact at %s, %s."), *M.Group.ToString(), *Last.SystemName, bSuccess ? TEXT("friendly") : TEXT("wary")), FLinearColor(0.7f, 1.f, 0.8f), ECrewDepartment::Science); }
		}
		break;
	case EMissionKind::PreWarp:
		if (bOver && X && O.SuccessTag == TEXT("Contact.First")) { X->RecordContact(M.Group.ToString(), Last.SystemName, bSuccess ? 1 : -1, O.Text); }
		break;
	case EMissionKind::Outpost:
	{
		UFrontierSubsystem* F = Frontier();
		// The site: the survey's pick (the habitable first), else the first solid ground; then the outpost itself.
		if (M.Stage == 2 && M.Site.IsEmpty() && X)
		{
			if (const FCatalogueEntry* E = X->Find(M.TargetName))
			{
				TArray<FString> Free; for (const FString& W : E->SolidWorlds) { if (!(E->IsLit(W) || APlanetBody::IsNamedWorldInhabited(GetWorld(), W))) { Free.Add(W); } }   // never in someone's sky
				if (Free.Num() > 0) { M.Site = bSuccess ? Free[0] : Free.Last(); }
			}
		}
		if (bOver && bSucceeded && F) { F->Found(ESettlementKind::Outpost, M.TargetName, M.Site); }
		break;
	}
	case EMissionKind::Colony:
	{
		// The settlers are down once the landing is behind them: the colony is theirs from then (lit, counted, worth
		// defending), whatever the first weeks bring.
		UFrontierSubsystem* F = Frontier();
		// The embarkation: the volunteers are six hundred, five hundred when it goes badly; the full expedition eight.
		if (M.Stage == 1 && !bOver && O.Skill == ECrewSkill::Operations) { M.People = bSuccess ? 600.0 : 500.0; }
		if (F && ((M.Stage == 2 && !bOver) || (bOver && bSucceeded)) && !F->Find(M.Site, ESettlementKind::Colony)) { F->Found(ESettlementKind::Colony, M.Site, M.TargetName, M.People); }
		break;
	}
	case EMissionKind::Starbase:
	{
		UFrontierSubsystem* F = Frontier();
		if (!F) { break; }
		if (M.Stage == 2 && !bOver && !F->Find(M.TargetName, ESettlementKind::Starbase))
		{
			// The keel: the order was given when Starfleet's regard allowed it, and it holds (bOrdered). Should the
			// founding fail all the same, the stage is held and the bridge told, rather than works that do not exist.
			if (!F->Found(ESettlementKind::Starbase, M.TargetName, TEXT(""), 0.0, true))
			{
				FString W2, Why; F->CanFound(ESettlementKind::Starbase, M.TargetName, W2, Why, true);
				M.Stage = 1; M.Log.Add(FString::Printf(TEXT("      The keel is not laid: %s."), *Why));
				Report(FString::Printf(TEXT("%s: the keel cannot be laid. %s."), *M.Title, *Why), FLinearColor(1.f, 0.75f, 0.5f), ECrewDepartment::Engineering);
			}
		}
		if (bOver && bSucceeded) { FString Why; F->Commission(M.TargetName, Why); }
		break;
	}
	case EMissionKind::TimeLoop:
		if (bOver && bSucceeded) { LeaveFold(M, O); }
		break;
	case EMissionKind::Leviathan:
		LeviathanAfter(M, O, bSuccess);
		if (bOver && bSucceeded) { LeaveLeviathan(M); }
		break;
	case EMissionKind::Impact:
	{
		if (M.Stage == 1 && !bOver && bSuccess && O.Skill == ECrewSkill::Operations) { M.bEvacuated = true; }
		if (bOver && bSucceeded)
		{
			// The crew did it: the rock goes wide or comes apart in the world.
			if (AImpactor* Rock = AImpactor::FindByName(GetWorld(), M.Site)) { if (O.Skill == ECrewSkill::Engineering && O.Risk <= 0.f) { Rock->DevDeflect(); } else { Rock->Shatter(); } }
			else { M.bRockResolved = true; }   // no actor to say so (a past session's rock): the crew's word is the end of it
		}
		break;
	}
	default: break;
	}
}

void UMissionSubsystem::IssueOrders()
{
	FRandomStream R(Rng.RandRange(1, 1 << 20));
	static const EMissionKind Kinds[] = { EMissionKind::Survey, EMissionKind::Diplomacy, EMissionKind::Medical, EMissionKind::Patrol, EMissionKind::Survey, EMissionKind::Delivery, EMissionKind::Explore, EMissionKind::Explore, EMissionKind::Explore, EMissionKind::Outpost, EMissionKind::Colony, EMissionKind::Starbase, EMissionKind::Impact };
	FMission M = MakeMission(Kinds[R.RandRange(0, UE_ARRAY_COUNT(Kinds) - 1)], EMissionSource::Starfleet, R);
	if (M.Id < 0) { M = MakeMission(EMissionKind::Survey, EMissionSource::Starfleet, R); }   // nothing left to chart or found, nowhere in danger
	if (M.Id < 0) { return; }
	M.State = EMissionState::Offered;
	Place(M);
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
	static const EMissionKind Kinds[] = { EMissionKind::Delivery, EMissionKind::Survey, EMissionKind::Diplomacy, EMissionKind::Medical, EMissionKind::Explore, EMissionKind::Outpost, EMissionKind::Colony };
	const int32 Day = (int32)(Hours() / 24.0);
	if (const int32* Posted = BoardDays.Find(StationName)) { if (*Posted == Day) { return; } }
	BoardDays.Add(StationName, Day);
	int32 Posted = 0;
	for (int32 i = 0; i < 3; ++i)
	{
		FMission M = MakeMission(Kinds[R.RandRange(0, UE_ARRAY_COUNT(Kinds) - 1)], EMissionSource::Starbase, R);
		if (M.Id < 0) { continue; }
		M.State = EMissionState::Offered; M.PostedAt = StationName; M.DeadlineStardate = Stardate() + TravelDays(M) + R.FRandRange(1.5, 3.0);
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
	// Where there is a world with people on it, one time in six the sky throws something at it.
	FMission M; M.Id = -1;
	// Once in a long while the ship runs into a fold in time: one event in sixteen, and none within ten days of the
	// last. (A draw of its own, off the event's seed, so every other event falls as it did before there were any.)
	FRandomStream Odd((int32)HashCombine((uint32)R.GetInitialSeed(), 0x7f4a7c15u));
	const float OddDraw = Odd.FRand();
	bool bLoopLately = false;
	for (const FMission& Was : Missions) { bLoopLately |= Was.Kind == EMissionKind::TimeLoop && (Was.IsOpen() || Was.ClosedStardate > Stardate() - 10.0); }
	// And once in as long a while something of the old road's comes alongside: the next sixteenth of the same draw,
	// none within ten days of the last, and only in open space with a core that could take her away from it. One that
	// was left astern is the next of its kind, once two days are out, whatever the draw (a fold on its own sixteenth
	// still comes first).
	const bool bFollowed = Remembers(TEXT("Leviathan.Astern"));
	bool bLeviathanLately = false;
	for (const FMission& Was : Missions) { bLeviathanLately |= Was.Kind == EMissionKind::Leviathan && (Was.IsOpen() || Was.ClosedStardate > Stardate() - (bFollowed ? 2.0 : 10.0)); }
	const bool bLeviathan = !bLeviathanLately && Last.bClearSpace && !IsDriveCold() && (bFollowed || (OddDraw >= 0.0625f && OddDraw < 0.125f));
	if (OddDraw < 0.0625f && !bLoopLately) { M = MakeMission(EMissionKind::TimeLoop, EMissionSource::Event, R); }
	else if (bLeviathan) { M = MakeMission(EMissionKind::Leviathan, EMissionSource::Event, R); }
	else if (R.FRand() < 0.17f) { M = MakeMission(EMissionKind::Impact, EMissionSource::Event, R); }
	if (M.Id < 0) { M = MakeMission(Kind, EMissionSource::Event, R); }
	M.State = EMissionState::Active; if (M.Kind != EMissionKind::Impact) { M.DeadlineStardate = Stardate() + 1.0; }
	M.Log.Add(FString::Printf(TEXT("%.1f  %s"), Stardate(), *M.Brief));
	Place(M);
	Missions.Add(M);
	Report(FString::Printf(TEXT("%s It is on the mission board, Captain."), *M.Brief), FLinearColor(1.f, 0.85f, 0.5f),
		M.Kind == EMissionKind::Anomaly || M.Kind == EMissionKind::Contact || M.Kind == EMissionKind::Impact || M.Kind == EMissionKind::TimeLoop || M.Kind == EMissionKind::Leviathan ? ECrewDepartment::Science : ECrewDepartment::Operations);
	Dirty();
}

void UMissionSubsystem::Place(FMission& M)
{
	if (M.TargetKind == EMissionTarget::Vessel && M.Source == EMissionSource::Event) { SpawnEventVessel(M, M.TargetName); }
	if (M.Kind == EMissionKind::Impact) { SpawnImpactor(M, M.ImpactHours); }
	if (M.Kind == EMissionKind::TimeLoop && M.State == EMissionState::Active) { BeginPass(M); }
	if (M.Kind == EMissionKind::Leviathan && M.State == EMissionState::Active) { BeginPass(M); SpawnLeviathan(M); bHelmStop = true; }
}

bool UMissionSubsystem::HasOpenExpansion(EMissionKind Kind, const FString& System) const
{
	for (const FMission& M : Missions) { if (M.IsOpen() && M.Kind == Kind && (M.Kind == EMissionKind::Colony ? M.Site : M.TargetName) == System) { return true; } }
	return false;
}

void UMissionSubsystem::RestoreImpactors()
{
	// The rock of an open impact mission, where it would be by now on the course its seed gives: the hours left to
	// the deadline. One whose hour has passed lands at the next tick (Expire finds no rock).
	UWorld* W = GetWorld();
	if (!W) { return; }
	int32 N = 0;
	TArray<TPair<FString, FString>> Landed;
	for (FMission& M : Missions)
	{
		// Open or closed: a rock abandoned, or whose offer lapsed, is still out there until it is done with.
		if (M.Kind != EMissionKind::Impact || M.bRockResolved || M.Site.IsEmpty() || AImpactor::FindByName(W, M.Site)) { continue; }
		const double HoursLeft = (M.DeadlineStardate - Stardate()) * 24.0;
		if (HoursLeft <= 0.0) { Landed.Emplace(M.Site, M.TargetName); continue; }   // its hour passed between sessions
		SpawnImpactor(M, HoursLeft); ++N;
	}
	for (const TPair<FString, FString>& L : Landed) { ImpactOutcome(L.Key, L.Value, EImpactOutcome::Struck); }
	if (N > 0) { UE_LOG(LogTemp, Log, TEXT("Missions: %d rocks back on their courses"), N); }
}

bool UMissionSubsystem::PickInhabitedWorld(FMission& M, FRandomStream& R, bool bHere)
{
	// A world with people: one lit at night in a charted system, or a colony of ours; never one already under a rock.
	TSet<FString> Taken; for (const FMission& O : Missions) { if (O.Kind == EMissionKind::Impact && !O.bRockResolved) { Taken.Add(O.TargetName); } }   // until the rock is done with, open mission or not
	TArray<FString> Worlds;
	if (UWorld* W = GetWorld())
	{
		for (TActorIterator<APlanetBody> It(W); It; ++It)
		{
			if (It->IsMoon() || !It->IsInhabited() || Taken.Contains(It->GetDisplayName())) { continue; }
			const AStarSystem* Sys = AStarSystem::Nearest(W, It->GetActorLocation());
			if (!Sys || !Sys->bCharted || (Sys->SystemName == Last.SystemName) != bHere) { continue; }
			Worlds.AddUnique(It->GetDisplayName());
		}
	}
	if (const UFrontierSubsystem* F = Frontier())
	{
		const UExplorationSubsystem* X = Exploration();
		for (const FString& C : F->ColonyWorlds())
		{
			const FCatalogueEntry* E = X ? X->EntryForWorld(C) : nullptr;
			if (Taken.Contains(C) || (E && (E->Name() == Last.SystemName) != bHere)) { continue; }
			Worlds.AddUnique(C);
		}
	}
	if (Worlds.Num() == 0) { return false; }
	M.TargetKind = EMissionTarget::World; M.TargetName = Worlds[R.RandRange(0, Worlds.Num() - 1)];
	return true;
}

void UMissionSubsystem::SpawnImpactor(FMission& M, double HoursToImpact)
{
	UWorld* W = GetWorld();
	if (!W || M.Site.IsEmpty()) { return; }
	APlanetBody* Target = nullptr;
	for (TActorIterator<APlanetBody> It(W); It; ++It) { if (It->GetDisplayName() == M.TargetName) { Target = *It; break; } }
	if (!Target) { return; }
	if (AImpactor* Rock = AImpactor::Spawn(W, Target, M.Site, HoursToImpact, M.Site.StartsWith(TEXT("Comet")), M.Id * 131 + 7)) { Rock->MissionId = M.Id; }
}

void UMissionSubsystem::CompleteOutside(FMission& M, const FString& Why, const FString& Text)
{
	M.Log.Add(FString::Printf(TEXT("      %s"), *Text));
	Close(M, EMissionState::Completed, Why);
	Standing += M.Standing; ++Completed;
	PayScience(M, Text);
	Report(FString::Printf(TEXT("%s: complete. Starfleet's regard: %+d."), *M.Title, M.Standing), FLinearColor(0.6f, 1.f, 0.6f));
}

void UMissionSubsystem::ImpactOutcome(const FString& Rock, const FString& World, EImpactOutcome Outcome)
{
	// The mission for the rock: the open one, else the closed one (abandoned by the captain; or completed as averted
	// before the rock was bent back): a landing counts against it either way.
	FMission* MP = nullptr; for (FMission& M : Missions) { if (M.IsOpen() && M.Kind == EMissionKind::Impact && M.Site == Rock) { MP = &M; } }
	if (!MP) { for (FMission& M : Missions) { if (M.Kind == EMissionKind::Impact && M.Site == Rock) { MP = &M; } } }
	if (MP) { MP->bRockResolved = true; }
	if (Outcome != EImpactOutcome::Struck)
	{
		if (MP && MP->IsOpen()) { CompleteOutside(*MP, Outcome == EImpactOutcome::Averted ? TEXT("the rock goes wide") : TEXT("the rock broken up"), Outcome == EImpactOutcome::Averted ? FString::Printf(TEXT("%s will miss %s. The world never knew."), *Rock, *World) : FString::Printf(TEXT("%s is in pieces, and the pieces go wide of %s."), *Rock, *World)); }
		Dirty();
		return;
	}
	// It lands. The world is marked, the colony counts its dead, Starfleet's regard falls, and the crew carry it.
	if (MP && MP->State == EMissionState::Failed) { return; }   // it has landed once already
	const bool bEvacuated = MP && MP->bEvacuated;
	FString Text;
	UFrontierSubsystem* F = Frontier();
	const bool bColony = F && F->ColonyWorlds().Contains(World);
	if (F) { F->Strike(World, bEvacuated, Text); }   // a colony's lights are its own: dimmed by its scar
	if (!bColony && F) { F->ScarWorld(World, bEvacuated ? 0.5f : 0.25f); }   // a world of theirs: the scar kept and put back next session
	Report(FString::Printf(TEXT("%s has struck %s. %s"), *Rock, *World, *Text), FLinearColor(1.f, 0.4f, 0.3f), ECrewDepartment::Science);
	if (UCrewRosterSubsystem* R = Roster())
	{
		FCrewExperience E; E.Tag = TEXT("Loss.Civilians"); E.Intensity = bEvacuated ? 0.6f : 0.9f; E.Place = World; E.Text = FString::Printf(TEXT("%s struck %s"), *Rock, *World);
		R->Experience(E);
	}
	if (MP)
	{
		MP->Log.Add(FString::Printf(TEXT("      %s"), *Text));
		Close(*MP, EMissionState::Failed, MP->IsOpen() ? TEXT("the rock landed") : TEXT("the rock landed after all"));
		const int32 Loss = MP->FailLoss >= 0 ? MP->FailLoss : MP->Standing / 2;
		Standing -= Loss; ++Failed;
		Report(FString::Printf(TEXT("%s: failed. Starfleet's regard: %d."), *MP->Title, -Loss), FLinearColor(1.f, 0.6f, 0.5f));
	}
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
	const bool bAlien = M.Kind == EMissionKind::Contact || M.Kind == EMissionKind::FirstContact;
	if (ASpaceVessel* V = Traffic->SpawnNamedShip(Name, Loc, !bAlien, bAlien)) { EventHulls.Add(M.Id, V); }
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

int32 UMissionSubsystem::DevSpawn(EMissionKind Kind, EMissionSource Source) { return DevSpawnAt(Kind, Source, FString(), FString()); }

int32 UMissionSubsystem::DevSpawnAt(EMissionKind Kind, EMissionSource Source, const FString& Target, const FString& Site)
{
	FRandomStream R(Rng.RandRange(1, 1 << 20));
	if (Kind == EMissionKind::Support || Kind == EMissionKind::Assist) { return -2; }   // the fleet raises these (ship fleet trouble / mission)
	ForcedTarget = Target; ForcedSite = Site;
	FMission M = MakeMission(Kind, Source, R);
	ForcedTarget.Empty(); ForcedSite.Empty();
	if (M.Id < 0) { return -1; }
	if (M.TargetKind == EMissionTarget::Vessel && Source != EMissionSource::Event) { return -2; }   // its hull only exists as an encounter
	if (Source == EMissionSource::Event)
	{
		M.State = EMissionState::Active; if (Kind != EMissionKind::Impact) { M.DeadlineStardate = Stardate() + 1.0; }
		M.Log.Add(FString::Printf(TEXT("%.1f  %s"), Stardate(), *M.Brief));
	}
	else
	{
		M.State = EMissionState::Offered;
		if (Source == EMissionSource::Starfleet) { FStarfleetMessage Msg; Msg.Stardate = Stardate(); Msg.From = M.From; Msg.Subject = M.Title; Msg.MissionId = M.Id; Msg.Text = M.Brief; Inbox.Add(Msg); }
		else { M.PostedAt = Last.StationName.IsEmpty() ? TEXT("the board") : Last.StationName; }
	}
	Place(M);
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
		if (M.Kind == EMissionKind::TimeLoop) { BeginPass(M); }
		if (M.Kind == EMissionKind::Leviathan) { BeginPass(M); SpawnLeviathan(M); bHelmStop = true; }
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
		if (M.Kind == EMissionKind::TimeLoop && M.State == EMissionState::Active) { OutReply = TEXT("There is nowhere to leave it for: the fold is all round the ship"); return false; }
		if (M.Kind == EMissionKind::Leviathan && M.State == EMissionState::Active) { OutReply = IsDriveCold() ? TEXT("It is alongside, and the core is cold: there is no leaving it at warp until she is lit") : TEXT("It is alongside: take her to warp, or down into a world's air, and it is left astern"); return false; }
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

EMissionTarget UMissionSubsystem::TargetKindOf(const FMission& M) const
{
	const FMissionStage* St = M.Current();
	return St && St->bOwnTarget ? St->TargetKind : M.TargetKind;
}

FString UMissionSubsystem::TargetNameOf(const FMission& M) const
{
	const FMissionStage* St = M.Current();
	if (St && St->bOwnTarget) { return St->TargetKind == EMissionTarget::Station && St->TargetName.IsEmpty() ? TEXT("a starbase") : St->TargetName; }
	return M.TargetName;
}

bool UMissionSubsystem::IsAtTarget(const FMission& M) const
{
	const FMissionStage* St = M.Current();
	const bool bOwn = St && St->bOwnTarget;
	const EMissionTarget K = bOwn ? St->TargetKind : M.TargetKind;
	const FString& N = bOwn ? St->TargetName : M.TargetName;
	switch (K)
	{
	case EMissionTarget::None: return true;
	case EMissionTarget::System: return Last.SystemName == N;
	case EMissionTarget::Station: if (N.IsEmpty()) { return Last.bAlongside; } break;
	default: break;
	}
	for (const FString& Near : Last.Nearby) { if (Near == N) { return true; } }
	return false;
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
	// (Where there is no place named, the mission's own name; a leviathan by its own, as the HUD has it.)
	const FString Where = !M.TargetName.IsEmpty() ? M.TargetName : M.Kind == EMissionKind::Leviathan && !M.Site.IsEmpty() ? M.Site : M.Title;
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
	const bool bLeviathan = M.Kind == EMissionKind::Leviathan;
	if (!M.Offers(O))
	{
		OutReply = M.Learned(O) ? (bLeviathan ? TEXT("That is known already: it is on the board") : TEXT("That is known already: it is in the watch's report"))
			: M.Closed(O) ? (bLeviathan ? TEXT("That was given, and not wanted") : TEXT("That way is closed: it was tried, and refused"))
			: M.Done(O) ? TEXT("That is done already, this watch") : TEXT("Nobody aboard knows to try that yet");
		return false;
	}
	if (!HasHoursFor(M, O))
	{
		OutReply = bLeviathan ? FString::Printf(TEXT("There is not the time: it wants %s, and it is against the hull %s"), O.Hours < 1.5f ? TEXT("an hour's work") : *FString::Printf(TEXT("%.0f hours' work"), O.Hours),
				PassHoursLeft(M) < 0.5 ? TEXT("now") : *FString::Printf(TEXT("in %.0f"), PassHoursLeft(M)))
			: FString::Printf(TEXT("There is not the time left in this watch: it wants %.0f hours, and the fold closes in %.1f"), O.Hours, PassHoursLeft(M));
		return false;
	}
	if (O.bNeedsSurvey)
	{
		const UScienceLogSubsystem* L = GetGameInstance() ? GetGameInstance()->GetSubsystem<UScienceLogSubsystem>() : nullptr;
		const UExplorationSubsystem* X = Exploration();
		const FCatalogueEntry* E = X ? X->Find(M.TargetName) : nullptr;
		const bool bSurveyed = (E && E->State == EChartState::Surveyed) || (L && L->IsSystemSurveyed(M.TargetName));
		if (!bSurveyed) { OutReply = FString::Printf(TEXT("The survey of %s is not complete: scan the star and every world (Enter on each)"), *M.TargetName); return false; }
	}
	const TArray<int32> Party = PartyFor(M, O);
	if (Party.Num() == 0) { OutReply = M.Party.Num() > 0 ? TEXT("None of the named party is fit and aboard; name another (ship mission party <id> <names>)") : TEXT("Nobody fit to send"); return false; }
	if (O.bNeedsWorks)
	{
		const UFrontierSubsystem* F = Frontier();
		const double Left = F ? F->WorksHoursLeft(M.TargetName) : 0.0;
		if (!F || !F->Find(M.TargetName, ESettlementKind::Starbase)) { OutReply = TEXT("No keel has been laid"); return false; }
		if (Left > 0.0) { OutReply = FString::Printf(TEXT("The works need another %.0f hours of ship's time"), FMath::Max(Left, 1.0)); return false; }
	}
	// The work already done is not rolled for: a survey on file, a station built, carry without a check.
	FCrewCheck C;
	if (O.bNeedsSurvey || O.bNeedsWorks) { C.bSuccess = true; C.Margin = 100.f; C.BestId = Party[0]; C.Summary = O.bNeedsSurvey ? TEXT("the survey on file") : TEXT("the works complete"); }
	// (A wrong guess is wrong whoever makes it; and what is only an order given is carried as given.)
	else if (O.bWrong) { C.bSuccess = false; C.Margin = -100.f; C.BestId = Party[0]; C.Summary = bLeviathan ? TEXT("Done as ordered. It was not what it wanted") : TEXT("Sent true, and in time. It was not the answer"); }
	else if (O.bSure) { C.bSuccess = true; C.Margin = 100.f; C.BestId = Party[0]; C.Summary = TEXT("Done as ordered"); }
	else { C = R->Check(O.Skill, O.Difficulty, Party, 0.f, M.Group); }
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
	if (!C.bSuccess) { Hurt(M, Party, O.Risk * (!O.SaferWhenReady.IsNone() && M.Readied.Contains(O.SaferWhenReady) ? 0.33f : 1.f)); }
	// The hours it took are lived, carried or not: the stardate moves, and a pass has that much less of itself.
	if (O.Hours > 0.f) { R->LiveHours(FMath::RoundToInt(O.Hours)); M.HoursLeft = FMath::Max(M.HoursLeft - O.Hours, 0.0); }
	// A wrong guess can be a blow to the ship, the less for her being rigged for one (her own model of a blow with
	// no place to it: something breaks, and people are hurt).
	if (!C.bSuccess && O.bWrong && O.WrongBlow > 0.f)
	{
		if (AStarshipPawn* Struck = CrewedShip()) { if (Struck->Systems) { const float Blow = O.WrongBlow * (!O.SaferWhenReady.IsNone() && M.Readied.Contains(O.SaferWhenReady) ? 0.33f : 1.f); Struck->Systems->OnHit(Blow, Blow); } }
	}
	// What the mission learns it keeps (and is not offered again, so its science is earned the once); what it makes
	// ready lasts the pass; and a failure can teach too, or cost every pass after it.
	if (C.bSuccess && O.Science > 0) { M.ScienceEarned += O.Science; }
	if (C.bSuccess && !O.GivesMark.IsNone()) { M.Marks.AddUnique(O.GivesMark); }
	if (C.bSuccess && !O.MakesReady.IsNone()) { M.Readied.AddUnique(O.MakesReady); }
	if (!C.bSuccess && !O.FailGivesMark.IsNone()) { M.Marks.AddUnique(O.FailGivesMark); }
	if (!C.bSuccess && O.FailShortensPass > 0.f) { M.PassHours = FMath::Max(3.0, M.PassHours - O.FailShortensPass); }
	// A loop in time: the outcome may end the pass, and so do its hours, spent, unless this was the way out; then
	// the watch begins again.
	if (M.Kind == EMissionKind::TimeLoop)
	{
		const bool bWayOut = (C.bSuccess ? O.NextOnSuccess : O.NextOnFail) < 0;
		const bool bEndsPass = C.bSuccess ? O.bSuccessEndsPass : O.bFailEndsPass;
		if (bEndsPass || (!bWayOut && PassHoursLeft(M) <= 1.0e-3))
		{
			ComeRound(M, bEndsPass ? FString() : FString(TEXT("The watch's hours are spent.")));
			Out.bMissionOver = !M.IsOpen(); OutReply = Out.Text;   // (the twelfth pass is the last: ComeRound)
			Dirty();
			return true;
		}
		if (M.Stages.Num() > 0) { M.Stages[0].Text = LoopWatchText(M); }
	}
	// Onward, or the end.
	const int32 Next = C.bSuccess ? O.NextOnSuccess : O.NextOnFail;
	if (Next >= 0 && M.Stages.IsValidIndex(Next)) { M.Stage = Next; Out.bMissionOver = false; AfterOption(M, O, C.bSuccess, false, true); Out.bMissionOver = !M.IsOpen(); }
	else
	{
		const bool bSucceeded = C.bSuccess || !O.bFailEndsMission;
		Out.bMissionOver = true;
		if (bSucceeded)
		{
			// The way out that was taken, and Starfleet's regard for it; a loop left in few passes is the better
			// seamanship.
			M.Exit = O.Exit;
			if (O.ExitStanding > 0) { M.Standing = O.ExitStanding; }
			if (M.Kind == EMissionKind::TimeLoop) { M.Standing = FMath::Max(10, M.Standing - 3 * M.Pass); }
			// A leviathan given what it wanted: the less for every misreading on the way to it.
			if (bLeviathan && M.Exit == FName(TEXT("Given"))) { M.Standing = LevGivenRegard(M); }
			Close(M, EMissionState::Completed, C.bSuccess ? TEXT("carried through") : TEXT("done, though not well"));
			Standing += C.bSuccess ? M.Standing : M.Standing / 3; ++Completed;
			PayScience(M, Out.Text);
			Report(FString::Printf(TEXT("%s: complete. Starfleet's regard: %+d."), *M.Title, C.bSuccess ? M.Standing : M.Standing / 3), FLinearColor(0.6f, 1.f, 0.6f));
		}
		else
		{
			const int32 Loss = M.FailLoss >= 0 ? M.FailLoss : M.Standing / 2;
			Close(M, EMissionState::Failed, Out.Text);
			Standing -= Loss; ++Failed;
			Report(FString::Printf(TEXT("%s: failed. Starfleet's regard: %d."), *M.Title, -Loss), FLinearColor(1.f, 0.6f, 0.5f));
		}
		AfterOption(M, O, C.bSuccess, true, bSucceeded);
		// The fleet hears of it once our side is closed, so her own closing of the work finds nothing open.
		if (UFleetSubsystem* F = Fleet()) { if (M.Kind == EMissionKind::Support) { F->SupportResolved(M.TargetName, C.bSuccess, false); } else if (M.Kind == EMissionKind::Assist) { F->AssistResolved(M.TargetName, C.bSuccess); } }
	}
	OutReply = Out.Text;
	Dirty();
	return true;
}
