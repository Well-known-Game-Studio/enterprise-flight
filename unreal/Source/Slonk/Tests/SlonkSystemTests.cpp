// Slonk - automation tests for the ship's systems that live without a world: the crew roster, the missions and
// the fleet. They run headless (tools/run_tests.sh) or from the editor's Session Frontend, under "Slonk.".
// Each test makes its subsystems with InitForTest, which skips the save slots and never writes them.

#include "Misc/AutomationTest.h"
#include "../CrewRoster.h"
#include "../Missions.h"
#include "../Fleet.h"
#include "Engine/GameInstance.h"

#if WITH_DEV_AUTOMATION_TESTS

#define SLONK_TEST_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	/** A roster, a mission system and a fleet wired to each other, off the disk. */
	struct FSystems
	{
		UCrewRosterSubsystem* Roster = nullptr;
		UMissionSubsystem* Missions = nullptr;
		UFleetSubsystem* Fleet = nullptr;
		UGameInstance* Outer = nullptr;
		FSystems(int32 Seed = 7)
		{
			// The subsystems must live within a game instance; one that is never initialised has no collection of
			// its own, so they find each other through the test wiring instead.
			Outer = NewObject<UGameInstance>(GetTransientPackage());
			Outer->AddToRoot();
			Roster = NewObject<UCrewRosterSubsystem>(Outer);
			Missions = NewObject<UMissionSubsystem>(Outer);
			Fleet = NewObject<UFleetSubsystem>(Outer);
			Roster->AddToRoot(); Missions->AddToRoot(); Fleet->AddToRoot();
			Roster->InitForTest(Seed);
			Fleet->InitForTest(Roster, Missions);
			Missions->InitForTest(Roster, Fleet);
		}
		~FSystems() { Roster->RemoveFromRoot(); Missions->RemoveFromRoot(); Fleet->RemoveFromRoot(); Outer->RemoveFromRoot(); }
		FMissionShipState Ship(const FString& System = TEXT("Sol"), const TArray<FString>& Nearby = {}) const
		{
			FMissionShipState S; S.SystemName = System; S.Nearby = Nearby; return S;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkCrewComplementTest, "Slonk.Crew.Complement", SLONK_TEST_FLAGS)
bool FSlonkCrewComplementTest::RunTest(const FString& Parameters)
{
	FSystems S(3);
	TestEqual(TEXT("502 aboard"), S.Roster->CountAboard(), 502);
	TSet<int32> Ids;
	for (const FCrewMember& M : S.Roster->GetRoster()) { TestFalse(TEXT("ids unique"), Ids.Contains(M.Id)); Ids.Add(M.Id); TestTrue(TEXT("named"), !M.FirstName.IsEmpty()); }
	for (int32 d = 0; d < (int32)ECrewDepartment::Count; ++d) { TestNotNull(*FString::Printf(TEXT("head of %s"), *UCrewRosterSubsystem::DepartmentName((ECrewDepartment)d)), S.Roster->Head((ECrewDepartment)d)); }
	// The same seed gives the same people.
	FSystems T(3);
	TestEqual(TEXT("deterministic names"), T.Roster->GetRoster()[10].FullName(), S.Roster->GetRoster()[10].FullName());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkCrewCheckTest, "Slonk.Crew.Check", SLONK_TEST_FLAGS)
bool FSlonkCrewCheckTest::RunTest(const FString& Parameters)
{
	FSystems S;
	const FCrewCheck Easy = S.Roster->Check(ECrewSkill::Science, 0.f, {});
	TestTrue(TEXT("nothing to it"), Easy.bSuccess);
	TestTrue(TEXT("someone led"), Easy.BestId >= 0);
	TestFalse(TEXT("summary"), Easy.Summary.IsEmpty());
	const FCrewCheck Hard = S.Roster->Check(ECrewSkill::Science, 500.f, {});
	TestFalse(TEXT("beyond anyone"), Hard.bSuccess);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkCrewExperienceTest, "Slonk.Crew.Experience", SLONK_TEST_FLAGS)
bool FSlonkCrewExperienceTest::RunTest(const FString& Parameters)
{
	FSystems S;
	const float Before = S.Roster->MeanTrust();
	FCrewExperience E; E.Tag = TEXT("Combat.ShipSpared"); E.Intensity = 1.f; E.Group = TEXT("Klingon"); E.bCaptainsChoice = true; E.Text = TEXT("a cruiser spared");
	S.Roster->Experience(E);
	TestNotEqual(TEXT("trust moved"), S.Roster->MeanTrust(), Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkCrewTransferTest, "Slonk.Crew.Transfer", SLONK_TEST_FLAGS)
bool FSlonkCrewTransferTest::RunTest(const FString& Parameters)
{
	FSystems S;
	// Someone fit and not a head, with a friend aboard.
	const FCrewMember* Who = nullptr;
	for (const FCrewMember& M : S.Roster->GetRoster()) { if (!M.bHead && M.IsFit() && M.Relationships.Num() > 0 && M.Relationships[0].Affinity > 0.4f) { Who = &M; break; } }
	if (!TestNotNull(TEXT("a candidate"), Who)) { return false; }
	const int32 Id = Who->Id, Friend = Who->Relationships[0].Other;
	FCrewMember Gone; FString Reply;
	TestTrue(TEXT("transfers out"), S.Roster->TransferOut(Id, TEXT("USS Hood"), Gone, Reply));
	TestEqual(TEXT("marked transferred"), (int32)S.Roster->Find(Id)->Condition, (int32)ECrewCondition::Transferred);
	TestEqual(TEXT("one fewer aboard"), S.Roster->CountAboard(), 501);
	const FCrewMember* F = S.Roster->Find(Friend);
	bool bTie = false; for (const FCrewTie& T : F->Ties) { bTie |= T.Place == TEXT("USS Hood") && T.Name == Gone.FullName(); }
	TestTrue(TEXT("the friend keeps a tie aboard the Hood"), bTie);
	// And back aboard as a new record.
	TestTrue(TEXT("joins"), S.Roster->Join(Gone, TEXT("Back from the Hood."), Reply));
	TestEqual(TEXT("aboard again"), S.Roster->CountAboard(), 502);
	TestFalse(TEXT("a real posting"), S.Roster->GetRoster().Last().Posting.IsEmpty());
	TestNotEqual(TEXT("a new id"), S.Roster->GetRoster().Last().Id, Id);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkCrewTieMatchTest, "Slonk.Crew.TieMatch", SLONK_TEST_FLAGS)
bool FSlonkCrewTieMatchTest::RunTest(const FString& Parameters)
{
	// A tie is to a name aboard a ship: word of a namesake lost aboard another ship is nothing to them, and only
	// the right ship's loss is mourned and takes the tie.
	FSystems S;
	const FCrewMember* Who = nullptr;
	for (const FCrewMember& M : S.Roster->GetRoster()) { if (!M.bHead && M.IsFit() && M.Relationships.Num() > 0 && M.Relationships[0].Affinity > 0.4f) { Who = &M; break; } }
	if (!TestNotNull(TEXT("a candidate"), Who)) { return false; }
	const int32 Friend = Who->Relationships[0].Other;
	FCrewMember Gone; FString Reply;
	TestTrue(TEXT("transfers out"), S.Roster->TransferOut(Who->Id, TEXT("USS Hood"), Gone, Reply));
	const FCrewMember* F = S.Roster->Find(Friend);
	const int32 TiesBefore = F->Ties.Num(), EventsBefore = F->History.Num();
	TestTrue(TEXT("the tie is to the Hood"), UCrewRosterSubsystem::FindShipTie(*F, Gone.FullName(), TEXT("USS Hood")) != INDEX_NONE);
	S.Roster->TieLost(Friend, Gone.FullName(), TEXT("USS Lexington"));
	TestEqual(TEXT("a namesake elsewhere: no tie lost"), F->Ties.Num(), TiesBefore);
	TestEqual(TEXT("nothing to mourn"), F->History.Num(), EventsBefore);
	// Two ties to the same name aboard her (a sister who is also an old shipmate) go together, mourned once.
	{ FCrewTie T; T.Name = Gone.FullName(); T.Relation = TEXT("sister"); T.bFamily = true; T.Kind = ECrewTiePlace::Ship; T.Place = TEXT("USS Hood"); S.Roster->FindMutable(Friend)->Ties.Add(T); }
	S.Roster->TieLost(Friend, Gone.FullName(), TEXT("USS Hood"));
	TestEqual(TEXT("both ties gone"), F->Ties.Num(), TiesBefore - 1);
	TestTrue(TEXT("no tie to the Hood left"), UCrewRosterSubsystem::FindShipTie(*F, Gone.FullName(), TEXT("USS Hood")) == INDEX_NONE);
	int32 Bereaved = 0; for (const FCrewEvent& E : F->History) { Bereaved += E.Kind == ECrewEventKind::Bereaved; }
	TestEqual(TEXT("bereaved once"), Bereaved, 1);
	TestTrue(TEXT("as family"), F->History.Last().Kind == ECrewEventKind::Bereaved && F->History.Last().Text.Contains(TEXT("sister")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkMissionEventTest, "Slonk.Missions.Event", SLONK_TEST_FLAGS)
bool FSlonkMissionEventTest::RunTest(const FString& Parameters)
{
	FSystems S;
	const int32 Id = S.Missions->DevSpawn(EMissionKind::Anomaly, EMissionSource::Event);
	const FMission* M = S.Missions->Find(Id);
	if (!TestNotNull(TEXT("spawned"), M)) { return false; }
	TestEqual(TEXT("active at once"), (int32)M->State, (int32)EMissionState::Active);
	TestEqual(TEXT("one stage"), M->Stages.Num(), 1);
	TestEqual(TEXT("nothing to steer by but it"), S.Missions->Objective() ? S.Missions->Objective()->Id : -1, Id);
	FMissionOutcome Out; FString Reply;
	TestFalse(TEXT("no such option"), S.Missions->Act(Id, 9, Out, Reply));
	TestTrue(TEXT("the probe"), S.Missions->Act(Id, 1, Out, Reply));
	TestTrue(TEXT("over"), Out.bMissionOver);
	TestFalse(TEXT("still open"), S.Missions->Find(Id)->IsOpen());
	TestTrue(TEXT("logged"), S.Missions->Find(Id)->Log.Num() >= 3);
	TestNotEqual(TEXT("standing moved"), S.Missions->GetStanding(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkMissionDeadlineTest, "Slonk.Missions.Deadline", SLONK_TEST_FLAGS)
bool FSlonkMissionDeadlineTest::RunTest(const FString& Parameters)
{
	FSystems S;
	const int32 Id = S.Missions->DevSpawn(EMissionKind::Derelict, EMissionSource::Event);
	if (!TestTrue(TEXT("spawned"), Id > 0)) { return false; }
	FCrewShipState Clock;
	S.Roster->DevAdvanceHours(48, Clock);   // two ship's days; an event has one
	S.Missions->AdvanceSeconds(1.f, S.Ship());
	TestEqual(TEXT("failed by the deadline"), (int32)S.Missions->Find(Id)->State, (int32)EMissionState::Failed);
	TestEqual(TEXT("counted"), S.Missions->GetFailed(), 1);
	TestTrue(TEXT("standing fell"), S.Missions->GetStanding() < 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFleetSupportTest, "Slonk.Fleet.Support", SLONK_TEST_FLAGS)
bool FSlonkFleetSupportTest::RunTest(const FString& Parameters)
{
	FSystems S;
	S.Fleet->RegisterShip(TEXT("USS Hood"), TEXT("Excelsior"), TEXT("Starbase 11"), TEXT("Sol"));
	const FFleetShip* H = S.Fleet->Find(TEXT("USS Hood"));
	if (!TestNotNull(TEXT("registered"), H)) { return false; }
	TestEqual(TEXT("seven heads"), H->Officers.Num(), 7);
	TestFalse(TEXT("a captain"), H->Captain.FullName().IsEmpty());
	// Trouble at work in our system: a request, then a support mission, then our hand.
	S.Missions->AdvanceSeconds(1.f, S.Ship(TEXT("Sol"), { TEXT("USS Hood") }));   // the fleet learns where we are
	FString Reply;
	TestTrue(TEXT("trouble"), S.Fleet->DevTrouble(TEXT("USS Hood"), EFleetTrouble::Engineering, Reply));
	S.Missions->AdvanceSeconds(1.f, S.Ship(TEXT("Sol"), { TEXT("USS Hood") }));
	const FMission* Sup = nullptr;
	for (const FMission& M : S.Missions->GetMissions()) { if (M.Kind == EMissionKind::Support) { Sup = &M; } }
	if (!TestNotNull(TEXT("support on the board"), Sup)) { return false; }
	TestEqual(TEXT("targets the ship"), Sup->TargetName, FString(TEXT("USS Hood")));
	TestTrue(TEXT("in range"), S.Missions->IsAtTarget(*Sup));
	const float RegardBefore = H->Relation;
	FMissionOutcome Out;
	TestTrue(TEXT("acts"), S.Missions->Act(Sup->Id, 0, Out, Reply));
	TestTrue(TEXT("over"), Out.bMissionOver);
	if (Out.bSuccess)
	{
		TestEqual(TEXT("her trouble is over"), (int32)H->Mission.Trouble, (int32)EFleetTrouble::None);
		TestTrue(TEXT("regard up"), H->Relation > RegardBefore);
	}
	else
	{
		TestFalse(TEXT("her mission ended"), H->Mission.Task == EFleetTask::OnStation);
	}
	// A second trouble while the first call is closed does not leave stale ids behind.
	TestTrue(TEXT("no stale support id"), H->Mission.SupportMissionId < 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFleetTransferTest, "Slonk.Fleet.Transfer", SLONK_TEST_FLAGS)
bool FSlonkFleetTransferTest::RunTest(const FString& Parameters)
{
	FSystems S;
	S.Fleet->RegisterShip(TEXT("USS Hood"), TEXT("Excelsior"), TEXT("Starbase 11"), TEXT("Sol"));
	S.Missions->AdvanceSeconds(1.f, S.Ship(TEXT("Sol"), { TEXT("USS Hood") }));   // in range
	const FFleetShip* H = S.Fleet->Find(TEXT("USS Hood"));
	const int32 Complement = H->Complement;
	const FCrewMember* Who = nullptr;
	for (const FCrewMember& M : S.Roster->GetRoster()) { if (!M.bHead && M.IsFit()) { Who = &M; break; } }
	FString Reply;
	TestTrue(TEXT("one of ours goes"), S.Fleet->TransferTo(TEXT("USS Hood"), Who->Id, Reply));
	TestEqual(TEXT("she counts them"), H->Complement, Complement + 1);
	TestEqual(TEXT("she lists them"), H->Notables.Num(), 1);
	TestEqual(TEXT("we count one fewer"), S.Roster->CountAboard(), 501);
	// Her captain thinks little of us yet: no one comes the other way unless family is aboard.
	const FString Name = H->Notables[0].Person.FullName();
	const bool bFamily = H->Notables[0].Ties.Num() > 0;
	const bool bGot = S.Fleet->RequestPerson(TEXT("USS Hood"), Name, Reply);
	TestEqual(TEXT("a reunion or a refusal, by the ties"), bGot, bFamily || H->Relation >= 0.2f);
	if (bGot) { TestEqual(TEXT("aboard again"), S.Roster->CountAboard(), 502); TestEqual(TEXT("she counts one fewer"), H->Complement, Complement); }
	// Officers are never given up.
	TestFalse(TEXT("not her first officer"), S.Fleet->RequestPerson(TEXT("USS Hood"), H->Officers[0].Person.FullName(), Reply));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFleetTiesTest, "Slonk.Fleet.Ties", SLONK_TEST_FLAGS)
bool FSlonkFleetTiesTest::RunTest(const FString& Parameters)
{
	// The crew's ties aboard a ship become named people aboard her, each name once, under the name the tie gives
	// (whoever aboard us shares it); binding again, when the roster changes, adds nobody.
	FSystems S;
	TSet<FString> Pairs, Names;   // (who, whom) once: a person's two ties to the same name are one tie aboard her
	for (const FCrewMember& M : S.Roster->GetRoster()) { for (const FCrewTie& T : M.Ties) { if (T.Kind == ECrewTiePlace::Ship && T.Place == TEXT("USS Lexington")) { Pairs.Add(FString::Printf(TEXT("%d|%s"), M.Id, *T.Name)); Names.Add(T.Name); } } }
	const int32 Expected = Pairs.Num();
	if (!TestTrue(TEXT("the roster pins ties to the Lexington"), Expected > 10)) { return false; }
	S.Fleet->RegisterShip(TEXT("USS Lexington"), TEXT("Excelsior"), TEXT("Starbase 11"), TEXT("Sol"));
	const FFleetShip* L = S.Fleet->Find(TEXT("USS Lexington"));
	int32 Bound = 0; TSet<FString> Aboard;
	for (const FFleetPerson& P : L->Officers) { Bound += P.Ties.Num(); }
	for (const FFleetPerson& P : L->Notables) { Bound += P.Ties.Num(); TestFalse(*FString::Printf(TEXT("%s once"), *P.Person.FullName()), Aboard.Contains(P.Person.FullName())); Aboard.Add(P.Person.FullName()); TestTrue(*FString::Printf(TEXT("%s is a name a tie gave"), *P.Person.FullName()), Names.Contains(P.Person.FullName())); }
	TestEqual(TEXT("every tie bound once"), Bound, Expected);
	TestEqual(TEXT("one person per name"), L->Notables.Num(), Aboard.Num());
	// The roster changes (an experience bumps its revision); the fleet binds again and finds nothing new.
	const int32 Before = L->Notables.Num();
	FCrewExperience E; E.Tag = TEXT("Combat.ShipSpared"); E.Intensity = 0.5f; E.Group = TEXT("Klingon"); E.bCaptainsChoice = true; E.Text = TEXT("spared");
	S.Roster->Experience(E);
	S.Missions->AdvanceSeconds(1.f, S.Ship());
	S.Missions->AdvanceSeconds(1.f, S.Ship());
	TestEqual(TEXT("binding is idempotent"), L->Notables.Num(), Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFleetDeclinedTest, "Slonk.Fleet.Declined", SLONK_TEST_FLAGS)
bool FSlonkFleetDeclinedTest::RunTest(const FString& Parameters)
{
	// Abandoning her call tells her no help is coming: she carries on alone, thinks less of us, and her trouble
	// runs its own course; it is not failed on the spot.
	FSystems S;
	S.Fleet->RegisterShip(TEXT("USS Hood"), TEXT("Excelsior"), TEXT("Starbase 11"), TEXT("Sol"));
	const FFleetShip* H = S.Fleet->Find(TEXT("USS Hood"));
	S.Missions->AdvanceSeconds(1.f, S.Ship(TEXT("Sol"), { TEXT("USS Hood") }));
	FString Reply;
	TestTrue(TEXT("trouble"), S.Fleet->DevTrouble(TEXT("USS Hood"), EFleetTrouble::Medical, Reply));
	S.Missions->AdvanceSeconds(1.f, S.Ship(TEXT("Sol"), { TEXT("USS Hood") }));
	int32 SupId = -1; for (const FMission& M : S.Missions->GetMissions()) { if (M.Kind == EMissionKind::Support && M.IsOpen()) { SupId = M.Id; } }
	if (!TestTrue(TEXT("support on the board"), SupId >= 0)) { return false; }
	TestEqual(TEXT("her record points at it"), H->Mission.SupportMissionId, SupId);
	const float RegardBefore = H->Relation;
	TestTrue(TEXT("abandoned"), S.Missions->Abandon(SupId, Reply));
	TestEqual(TEXT("ours is abandoned"), (int32)S.Missions->Find(SupId)->State, (int32)EMissionState::Abandoned);
	TestTrue(TEXT("she is still in trouble"), H->Mission.Trouble != EFleetTrouble::None && H->Mission.Task == EFleetTask::OnStation);
	TestTrue(TEXT("told"), H->Mission.bHelpRefused);
	TestTrue(TEXT("no stale id"), H->Mission.SupportMissionId < 0);
	TestTrue(TEXT("regard fell"), H->Relation < RegardBefore);
	TestEqual(TEXT("one failure of ours"), S.Missions->GetFailed(), 1);
	// Her day runs out one way or the other: through it on her own, or beyond her.
	FCrewShipState Clock;
	S.Roster->DevAdvanceHours(30, Clock);
	S.Missions->AdvanceSeconds(1.f, S.Ship(TEXT("Sol"), { TEXT("USS Hood") }));
	TestEqual(TEXT("trouble over"), (int32)H->Mission.Trouble, (int32)EFleetTrouble::None);
	TestTrue(TEXT("the mission ended"), H->Done + H->Failed >= 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFleetAssistLapseTest, "Slonk.Fleet.AssistLapse", SLONK_TEST_FLAGS)
bool FSlonkFleetAssistLapseTest::RunTest(const FString& Parameters)
{
	// A ship at work in range: one offer to lend a hand, which lapses with her work and leaves no stale id; and
	// a wiped board leaves nothing of ours on any record.
	FSystems S;
	S.Fleet->RegisterShip(TEXT("USS Hood"), TEXT("Excelsior"), TEXT("Starbase 11"), TEXT("Sol"));
	const FFleetShip* H = S.Fleet->Find(TEXT("USS Hood"));
	FString Reply;
	TestTrue(TEXT("at work"), S.Fleet->DevWork(TEXT("USS Hood"), Reply));
	S.Missions->AdvanceSeconds(1.f, S.Ship(TEXT("Sol"), { TEXT("USS Hood") }));
	S.Missions->AdvanceSeconds(1.f, S.Ship(TEXT("Sol"), { TEXT("USS Hood") }));   // a second tick offers nothing more
	int32 Offers = 0, AssistId = -1; for (const FMission& M : S.Missions->GetMissions()) { if (M.Kind == EMissionKind::Assist) { ++Offers; AssistId = M.Id; } }
	TestEqual(TEXT("one offer"), Offers, 1);
	if (!TestTrue(TEXT("offered"), AssistId >= 0)) { return false; }
	TestEqual(TEXT("offered, not ordered"), (int32)S.Missions->Find(AssistId)->State, (int32)EMissionState::Offered);
	TestEqual(TEXT("her record points at it"), H->Mission.AssistMissionId, AssistId);
	TestTrue(TEXT("one offer per mission"), H->Mission.bAssistOffered);
	// Two days on, her work (and whatever followed it) is long over.
	FCrewShipState Clock;
	S.Roster->DevAdvanceHours(48, Clock);
	S.Missions->AdvanceSeconds(1.f, S.Ship(TEXT("Sol"), { TEXT("USS Hood") }));
	TestFalse(TEXT("the offer lapsed"), S.Missions->Find(AssistId)->IsOpen());
	TestNotEqual(TEXT("her record no longer points at it"), H->Mission.AssistMissionId, AssistId);
	TestEqual(TEXT("no standing lost"), S.Missions->GetFailed(), 0);
	// The board wiped: nothing of ours on her record.
	S.Missions->Reset();
	TestTrue(TEXT("cleared"), H->Mission.AssistMissionId < 0 && H->Mission.SupportMissionId < 0 && !H->Mission.bAssistOffered);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFleetClockResetTest, "Slonk.Fleet.ClockReset", SLONK_TEST_FLAGS)
bool FSlonkFleetClockResetTest::RunTest(const FString& Parameters)
{
	// A new complement starts the ship's clock over; the fleet keeps living on it instead of waiting for the old
	// reading to come round again, and the people its ties made go with the crew who knew them.
	FSystems S;
	S.Fleet->RegisterShip(TEXT("USS Lexington"), TEXT("Excelsior"), TEXT("Starbase 11"), TEXT("Sol"));
	const FFleetShip* L = S.Fleet->Find(TEXT("USS Lexington"));
	TestTrue(TEXT("people our crew know"), L->Notables.Num() > 0);
	FCrewShipState Clock;
	S.Roster->DevAdvanceHours(10, Clock);
	S.Missions->AdvanceSeconds(1.f, S.Ship());
	S.Roster->NewComplement(5);   // the clock back to zero, the ids used again by other people
	S.Missions->AdvanceSeconds(1.f, S.Ship());
	// Her file holds the new crew's people and only theirs: the old ties went with the crew who had them.
	TSet<FString> Theirs; for (const FCrewMember& M : S.Roster->GetRoster()) { for (const FCrewTie& T : M.Ties) { if (T.Kind == ECrewTiePlace::Ship && T.Place == TEXT("USS Lexington")) { Theirs.Add(T.Name); } } }
	for (const FFleetPerson& P : L->Notables) { TestTrue(*FString::Printf(TEXT("%s is known to the new crew"), *P.Person.FullName()), Theirs.Contains(P.Person.FullName())); for (const FFleetTie& T : P.Ties) { const FCrewMember* C = S.Roster->Find(T.CrewId); TestTrue(TEXT("each tie is a new crew member's own"), C && UCrewRosterSubsystem::FindShipTie(*C, P.Person.FullName(), TEXT("USS Lexington")) != INDEX_NONE); } }
	const int32 Lines = L->Log.Num();
	S.Roster->DevAdvanceHours(30, Clock);
	S.Missions->AdvanceSeconds(1.f, S.Ship());
	TestTrue(TEXT("she goes on living on the new clock"), L->Log.Num() > Lines);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFleetLossTest, "Slonk.Fleet.Loss", SLONK_TEST_FLAGS)
bool FSlonkFleetLossTest::RunTest(const FString& Parameters)
{
	FSystems S;
	S.Fleet->RegisterShip(TEXT("USS Hood"), TEXT("Excelsior"), TEXT("Starbase 11"), TEXT("Sol"));
	S.Fleet->NoteLost(TEXT("USS Hood"), TEXT("under fire"));
	const FFleetShip* H = S.Fleet->Find(TEXT("USS Hood"));
	TestTrue(TEXT("lost"), H->bLost);
	TestEqual(TEXT("nobody aboard"), H->Complement, 0);
	TestTrue(TEXT("all hands counted"), H->Lost > 0);
	TestFalse(TEXT("no mission"), H->Mission.IsActive());
	return true;
}

#endif
