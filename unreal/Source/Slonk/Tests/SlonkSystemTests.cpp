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
