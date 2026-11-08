// Slonk - automation tests for the ship's systems that live without a world: the crew roster, the missions and
// the fleet. They run headless (tools/run_tests.sh) or from the editor's Session Frontend, under "Slonk.".
// Each test makes its subsystems with InitForTest, which skips the save slots and never writes them.

#include "Misc/AutomationTest.h"
#include "../CrewRoster.h"
#include "../Missions.h"
#include "../Fleet.h"
#include "../Exploration.h"
#include "../Frontier.h"
#include "../Impactor.h"
#include "../GameClock.h"
#include "../WarpDriveComponent.h"
#include "../ShipInterior.h"
#include "../StarshipPawn.h"
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
		UExplorationSubsystem* Exploration = nullptr;
		UFrontierSubsystem* Frontier = nullptr;
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
			Exploration = NewObject<UExplorationSubsystem>(Outer);
			Frontier = NewObject<UFrontierSubsystem>(Outer);
			Roster->AddToRoot(); Missions->AddToRoot(); Fleet->AddToRoot(); Exploration->AddToRoot(); Frontier->AddToRoot();
			Roster->InitForTest(Seed);
			Fleet->InitForTest(Roster, Missions);
			Missions->InitForTest(Roster, Fleet, Exploration, Frontier);
			Exploration->InitForTest(Missions, Frontier);
			Frontier->InitForTest(Missions, Exploration);
		}
		~FSystems() { Roster->RemoveFromRoot(); Missions->RemoveFromRoot(); Fleet->RemoveFromRoot(); Exploration->RemoveFromRoot(); Frontier->RemoveFromRoot(); Outer->RemoveFromRoot(); }
		/** A charted catalogue system with three worlds, the second habitable; its designation. */
		FString ChartedWithWorlds(int32 Index = 0) const
		{
			const FString D = Exploration->GetCatalogue()[Index].Designation;
			Exploration->Chart(D, false);
			Exploration->SetWorldsForTest(D, { D + TEXT(" I"), D + TEXT(" II"), D + TEXT(" III") }, { D + TEXT(" II") });
			return D;
		}
		FMissionShipState Alongside(const FString& Station = TEXT("Starbase 11")) const
		{
			FMissionShipState S = Ship(); S.bAlongside = true; S.StationName = Station; S.Nearby.Add(Station); return S;
		}
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkClockScaleTest, "Slonk.Clock.Scale", SLONK_TEST_FLAGS)
bool FSlonkClockScaleTest::RunTest(const FString& Parameters)
{
	// The presets step both ways and stop at the ends.
	TestEqual(TEXT("up from 1"), UGameClockSubsystem::Step(1.f, 1), 10.f);
	TestEqual(TEXT("up from 60"), UGameClockSubsystem::Step(60.f, 1), 600.f);
	TestEqual(TEXT("top stays"), UGameClockSubsystem::Step(3600.f, 1), 3600.f);
	TestEqual(TEXT("down from 600"), UGameClockSubsystem::Step(600.f, -1), 60.f);
	TestEqual(TEXT("down from between"), UGameClockSubsystem::Step(100.f, -1), 60.f);
	TestEqual(TEXT("bottom stays"), UGameClockSubsystem::Step(1.f, -1), 1.f);
	// The ramp climbs through log space and comes down faster than it went up; it lands exactly.
	float S = 1.f; int32 Up = 0; while (S < 3600.f - 0.01f && Up < 1000) { S = UGameClockSubsystem::Ramp(S, 3600.f, 1.f / 60.f); ++Up; }
	TestTrue(TEXT("reaches the top within a few seconds"), Up > 30 && Up < 240);
	TestEqual(TEXT("lands on the target"), S, 3600.f);
	int32 Down = 0; while (S > 1.f + 0.01f && Down < 1000) { S = UGameClockSubsystem::Ramp(S, 1.f, 1.f / 60.f); ++Down; }
	TestTrue(TEXT("comes down faster"), Down < Up);
	TestEqual(TEXT("lands on x1"), S, 1.f);
	// The stock warp law: warp 1 is c, warp 9 about 1516 c, and four light-years at warp 7 is a little over two days.
	const double C = 2.99792458e10;
	TestTrue(TEXT("warp 1 is c"), FMath::IsNearlyEqual(UWarpDriveComponent::SpeedForFactor(1.0) / C, 1.0, 1e-6));
	TestTrue(TEXT("warp 9"), FMath::IsNearlyEqual(UWarpDriveComponent::SpeedForFactor(9.0) / C, 1516.4, 1.0));
	const double Hours = UWarpDriveComponent::HoursFor(4.0 * 9.4607e17, 7.0);
	TestTrue(*FString::Printf(TEXT("4 ly at warp 7 is about 53 h (%.1f)"), Hours), FMath::IsNearlyEqual(Hours, 53.4, 0.5));
	TestTrue(TEXT("transwarp climbs"), UWarpDriveComponent::SpeedForFactor(9.99) > UWarpDriveComponent::SpeedForFactor(9.9) * 900.0);
	// The fleet's cruise table.
	TestEqual(TEXT("Excelsior cruise"), UFleetSubsystem::CruiseFactor(TEXT("Excelsior")), 8.f);
	TestEqual(TEXT("Miranda cruise"), UFleetSubsystem::CruiseFactor(TEXT("Miranda")), 6.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFleetLegTest, "Slonk.Fleet.Leg", SLONK_TEST_FLAGS)
bool FSlonkFleetLegTest::RunTest(const FString& Parameters)
{
	// Without a world the places are unknown: a near leg is an hour, a far one a day; the record carries the leg
	// out and uses it again for the way home.
	FSystems S;
	S.Fleet->RegisterShip(TEXT("USS Hood"), TEXT("Excelsior"), TEXT("Starbase 11"), TEXT("Sol"));
	const FFleetShip* H = S.Fleet->Find(TEXT("USS Hood"));
	if (!TestNotNull(TEXT("registered"), H)) { return false; }
	TestEqual(TEXT("a near leg"), S.Fleet->LegHours(*H, TEXT("Sirius III"), false), 1.0);
	TestEqual(TEXT("a far leg"), S.Fleet->LegHours(*H, TEXT("Sirius"), true), 24.0);
	FString Reply; TestTrue(TEXT("sent out"), S.Fleet->DevMission(TEXT("USS Hood"), Reply));
	H = S.Fleet->Find(TEXT("USS Hood"));
	TestTrue(TEXT("under way"), H->Mission.Task == EFleetTask::UnderWay);
	TestEqual(TEXT("the leg is the countdown"), H->Mission.HoursLeft, H->Mission.TravelHours);
	TestTrue(TEXT("at least an hour"), H->Mission.TravelHours >= 1.f);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkExplorationCatalogueTest, "Slonk.Exploration.Catalogue", SLONK_TEST_FLAGS)
bool FSlonkExplorationCatalogueTest::RunTest(const FString& Parameters)
{
	// The same sky every game: a dozen stars, apart, in the band, each with its number and its own name.
	const TArray<FCatalogueEntry> A = UExplorationSubsystem::MakeCatalogue(UExplorationSubsystem::CatalogueSeed, UExplorationSubsystem::CatalogueCount);
	const TArray<FCatalogueEntry> B = UExplorationSubsystem::MakeCatalogue(UExplorationSubsystem::CatalogueSeed, UExplorationSubsystem::CatalogueCount);
	TestEqual(TEXT("a dozen"), A.Num(), UExplorationSubsystem::CatalogueCount);
	TSet<FString> Names;
	for (int32 i = 0; i < A.Num(); ++i)
	{
		const FCatalogueEntry& E = A[i];
		TestTrue(TEXT("a UFC number"), E.Designation.StartsWith(TEXT("UFC ")));
		TestFalse(TEXT("unique"), Names.Contains(E.Designation)); Names.Add(E.Designation);
		TestTrue(TEXT("in the band"), E.Spec.DistanceLy >= 6.f && E.Spec.DistanceLy <= 30.f);
		TestTrue(TEXT("star only, unpopulated"), E.Spec.bStarOnly && !E.Spec.bPopulate);
		TestFalse(TEXT("its own name"), E.ProperName.IsEmpty());
		TestEqual(TEXT("unknown"), (int32)E.State, (int32)EChartState::Unknown);
		TestEqual(TEXT("deterministic"), E.Designation, B[i].Designation);
		TestEqual(TEXT("deterministic name"), E.ProperName, B[i].ProperName);
		const FVector P = AStarSystem::DirectionFromSky(E.Spec.RightAscensionHours, E.Spec.DeclinationDeg) * (double)E.Spec.DistanceLy;
		for (int32 j = 0; j < i; ++j)
		{
			const FVector Q = AStarSystem::DirectionFromSky(A[j].Spec.RightAscensionHours, A[j].Spec.DeclinationDeg) * (double)A[j].Spec.DistanceLy;
			TestTrue(TEXT("apart"), FVector::Dist(P, Q) >= 1.4);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkExplorationChartTest, "Slonk.Exploration.Chart", SLONK_TEST_FLAGS)
bool FSlonkExplorationChartTest::RunTest(const FString& Parameters)
{
	FSystems S;
	const FCatalogueEntry& First = S.Exploration->GetCatalogue()[0];
	const FString D = First.Designation;
	const int32 Standing0 = S.Missions->GetStanding();
	// Arrival charts it: the first system charted always brings a first contact.
	TestTrue(TEXT("charted"), S.Exploration->Chart(D));
	TestFalse(TEXT("only once"), S.Exploration->Chart(D));
	TestEqual(TEXT("one charted"), S.Exploration->CountCharted(), 1);
	TestEqual(TEXT("state"), (int32)S.Exploration->Find(D)->State, (int32)EChartState::Charted);
	TestEqual(TEXT("regard for the chart"), S.Missions->GetStanding(), Standing0 + 10);
	const FMission* FC = nullptr; for (const FMission& M : S.Missions->GetMissions()) { if (M.Kind == EMissionKind::FirstContact) { FC = &M; } }
	if (TestNotNull(TEXT("a first contact"), FC)) { TestTrue(TEXT("active"), FC->State == EMissionState::Active); TestFalse(TEXT("a people"), FC->Group.IsNone()); }
	// Naming: only a charted star, never a taken name; the given name finds it.
	FString Reply;
	TestFalse(TEXT("not an unknown star"), S.Exploration->Name(S.Exploration->GetCatalogue()[1].Designation, TEXT("Nowhere"), Reply));
	TestTrue(TEXT("named"), S.Exploration->Name(D, TEXT("Vashti's Reach"), Reply));
	TestEqual(TEXT("found by name"), S.Exploration->Find(TEXT("Vashti's Reach")) ? S.Exploration->Find(TEXT("Vashti's Reach"))->Designation : FString(), D);
	TestFalse(TEXT("taken"), S.Exploration->Name(D, S.Exploration->GetCatalogue()[2].Designation, Reply));
	TestFalse(TEXT("not the scanner's nowhere"), S.Exploration->Name(D, TEXT("deep space"), Reply));
	// The first chart's contact is fixed when the chart is made, not when the roll comes.
	TestTrue(TEXT("guaranteed"), S.Exploration->Find(D)->bContactGuaranteed);
	TestTrue(TEXT("charted second"), S.Exploration->Chart(S.Exploration->GetCatalogue()[3].Designation, false));
	TestFalse(TEXT("not the second"), S.Exploration->Find(S.Exploration->GetCatalogue()[3].Designation)->bContactGuaranteed);
	// The survey completes the chart.
	S.Exploration->MarkSurveyed(TEXT("Vashti's Reach"));
	TestEqual(TEXT("surveyed"), (int32)S.Exploration->Find(D)->State, (int32)EChartState::Surveyed);
	// A people met is remembered, once.
	S.Exploration->RecordContact(TEXT("Vethrani"), TEXT("Vashti's Reach"), 1, TEXT("a treaty"));
	S.Exploration->RecordContact(TEXT("Vethrani"), TEXT("Vashti's Reach"), 0, TEXT("cooled"));
	TestEqual(TEXT("one record"), S.Exploration->GetContacts().Num(), 1);
	TestEqual(TEXT("the last word"), S.Exploration->ContactWith(TEXT("Vethrani"))->Relation, 0);
	TestEqual(TEXT("on the entry"), S.Exploration->Find(D)->Contact, FString(TEXT("Vethrani")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkMissionExploreTest, "Slonk.Missions.Explore", SLONK_TEST_FLAGS)
bool FSlonkMissionExploreTest::RunTest(const FString& Parameters)
{
	FSystems S;
	const int32 Id = S.Missions->DevSpawn(EMissionKind::Explore, EMissionSource::Starfleet);
	const FMission* M = S.Missions->Find(Id);
	if (!TestNotNull(TEXT("an order"), M)) { return false; }
	TestEqual(TEXT("a system"), (int32)M->TargetKind, (int32)EMissionTarget::System);
	const FCatalogueEntry* E = S.Exploration->Find(M->TargetName);
	if (!TestNotNull(TEXT("from the catalogue"), E)) { return false; }
	TestEqual(TEXT("unknown"), (int32)E->State, (int32)EChartState::Unknown);
	const FString Designation = E->Designation;
	FString Reply; FMissionOutcome Out;
	TestTrue(TEXT("accepted"), S.Missions->Accept(Id, Reply));
	TestFalse(TEXT("not there yet"), S.Missions->Act(Id, 0, Out, Reply));
	// Arrival: the ship's state names the system; the exploration tick charts it.
	const FMissionShipState At = S.Ship(M->TargetName);
	S.Missions->AdvanceSeconds(1.f, At);
	S.Exploration->AdvanceSeconds(3.f, At);
	TestEqual(TEXT("charted on arrival"), (int32)S.Exploration->Find(Designation)->State, (int32)EChartState::Charted);
	TestTrue(TEXT("in range"), S.Missions->IsAtTarget(*S.Missions->Find(Id)));
	// The survey option waits for the real survey; the chart is filed with the catalogue's own name.
	TestFalse(TEXT("survey not complete"), S.Missions->Act(Id, 0, Out, Reply));
	TestTrue(TEXT("says so"), Reply.Contains(TEXT("survey")));
	S.Exploration->MarkSurveyed(Designation);
	TestTrue(TEXT("survey carried"), S.Missions->Act(Id, 0, Out, Reply));
	TestTrue(TEXT("no check to fail"), Out.bSuccess);
	TestFalse(TEXT("one stage left"), Out.bMissionOver);
	TestTrue(TEXT("filed"), S.Missions->Act(Id, 0, Out, Reply));
	TestTrue(TEXT("over"), Out.bMissionOver);
	TestEqual(TEXT("complete"), (int32)S.Missions->Find(Id)->State, (int32)EMissionState::Completed);
	TestTrue(TEXT("science"), S.Missions->Find(Id)->ScienceEarned >= 300);
	const FCatalogueEntry* After = S.Exploration->Find(Designation);
	TestEqual(TEXT("named by the catalogue"), After->GivenName, After->ProperName);
	TestEqual(TEXT("the mission follows the name"), S.Missions->Find(Id)->TargetName, After->ProperName);
	// A reset of the catalogue takes the names back everywhere they went.
	const FString Proper = After->ProperName;
	S.Exploration->Reset();
	TestEqual(TEXT("nothing charted"), S.Exploration->CountCharted(), 0);
	TestNull(TEXT("the name is gone"), S.Exploration->Find(Proper));
	TestEqual(TEXT("the mission is back on the catalogue number"), S.Missions->Find(Id)->TargetName, Designation);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkMissionFirstContactTest, "Slonk.Missions.FirstContact", SLONK_TEST_FLAGS)
bool FSlonkMissionFirstContactTest::RunTest(const FString& Parameters)
{
	FSystems S;
	const int32 Id = S.Missions->RaiseEvent(EMissionKind::FirstContact);
	const FMission* M = S.Missions->Find(Id);
	if (!TestNotNull(TEXT("raised"), M)) { return false; }
	TestEqual(TEXT("active"), (int32)M->State, (int32)EMissionState::Active);
	TestEqual(TEXT("four stages"), M->Stages.Num(), 4);
	const FString People = M->Group.ToString();
	TestFalse(TEXT("a people"), People.IsEmpty());
	TestTrue(TEXT("named in the exchange"), M->Stages[1].Text.Contains(People));
	FString Reply; FMissionOutcome Out;
	// Her hull has to be in the neighbourhood to act; the test's state names it.
	TestFalse(TEXT("not in range"), S.Missions->Act(Id, 2, Out, Reply));
	S.Missions->AdvanceSeconds(1.f, S.Ship(TEXT("Sol"), { M->TargetName }));
	// Shields up: the hard start, then hold and wait; either the exchange opens or she leaves.
	TestTrue(TEXT("shields"), S.Missions->Act(Id, 2, Out, Reply));
	TestEqual(TEXT("a hard start"), S.Missions->Find(Id)->Stage, 2);
	TestTrue(TEXT("hold"), S.Missions->Act(Id, 1, Out, Reply));
	if (Out.bMissionOver)
	{
		TestEqual(TEXT("failed"), (int32)S.Missions->Find(Id)->State, (int32)EMissionState::Failed);
		const FContactRecord* C = S.Exploration->ContactWith(People);
		if (TestNotNull(TEXT("recorded"), C)) { TestEqual(TEXT("hostile"), C->Relation, -1); }
		return true;
	}
	TestEqual(TEXT("the exchange"), S.Missions->Find(Id)->Stage, 1);
	TestTrue(TEXT("linguists"), S.Missions->Act(Id, 0, Out, Reply));
	TestEqual(TEXT("terms"), S.Missions->Find(Id)->Stage, 3);
	TestTrue(TEXT("part in peace"), S.Missions->Act(Id, 2, Out, Reply));
	TestTrue(TEXT("over"), Out.bMissionOver);
	TestEqual(TEXT("complete"), (int32)S.Missions->Find(Id)->State, (int32)EMissionState::Completed);
	const FContactRecord* C = S.Exploration->ContactWith(People);
	if (TestNotNull(TEXT("recorded"), C)) { TestTrue(TEXT("not hostile"), C->Relation >= 0); TestEqual(TEXT("where"), C->System, S.Ship().SystemName); }
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkMissionBeaconTest, "Slonk.Missions.Beacon", SLONK_TEST_FLAGS)
bool FSlonkMissionBeaconTest::RunTest(const FString& Parameters)
{
	FSystems S;
	S.Missions->AdvanceSeconds(1.f, S.Ship());
	const int32 Id = S.Missions->RaiseEvent(EMissionKind::Beacon);
	const FMission* M = S.Missions->Find(Id);
	if (!TestNotNull(TEXT("raised"), M)) { return false; }
	TestEqual(TEXT("no target yet"), (int32)M->TargetKind, (int32)EMissionTarget::None);
	FString Reply; FMissionOutcome Out;
	TestTrue(TEXT("decode"), S.Missions->Act(Id, 0, Out, Reply));
	if (!Out.bSuccess) { TestTrue(TEXT("kept its secret"), Out.bMissionOver); return true; }
	// Decoded: it points at the nearest unknown star of the catalogue, and the source waits there.
	const FMission* After = S.Missions->Find(Id);
	TestEqual(TEXT("a system"), (int32)After->TargetKind, (int32)EMissionTarget::System);
	const FCatalogueEntry* E = S.Exploration->Find(After->TargetName);
	if (TestNotNull(TEXT("in the catalogue"), E)) { TestEqual(TEXT("unknown"), (int32)E->State, (int32)EChartState::Unknown); }
	TestEqual(TEXT("the source"), After->Stage, 1);
	TestFalse(TEXT("not there"), S.Missions->Act(Id, 0, Out, Reply));
	S.Missions->AdvanceSeconds(1.f, S.Ship(After->TargetName));
	TestTrue(TEXT("searched"), S.Missions->Act(Id, 0, Out, Reply));
	TestTrue(TEXT("over"), Out.bMissionOver);
	// With every star charted the decoded signal has nowhere to point: the beacon closes complete, its science kept.
	for (int32 Seed = 1; Seed < 40; ++Seed)
	{
		FSystems T(Seed);
		for (const FCatalogueEntry& Entry : T.Exploration->GetCatalogue()) { T.Exploration->Chart(Entry.Designation, false); }
		T.Missions->AdvanceSeconds(1.f, T.Ship());
		const int32 Id2 = T.Missions->RaiseEvent(EMissionKind::Beacon);
		const int32 Standing0 = T.Missions->GetStanding();
		if (!T.Missions->Act(Id2, 0, Out, Reply) || !Out.bSuccess) { continue; }
		const FMission* Done = T.Missions->Find(Id2);
		TestEqual(TEXT("closed complete"), (int32)Done->State, (int32)EMissionState::Completed);
		TestEqual(TEXT("its science kept"), Done->ScienceEarned, 100);
		TestTrue(TEXT("its regard paid"), T.Missions->GetStanding() > Standing0);
		TestTrue(TEXT("over at once"), Out.bMissionOver);
		return true;
	}
	AddError(TEXT("no seed decoded the beacon"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFrontierFoundTest, "Slonk.Frontier.Found", SLONK_TEST_FLAGS)
bool FSlonkFrontierFoundTest::RunTest(const FString& Parameters)
{
	FSystems S;
	const FString D = S.Exploration->GetCatalogue()[0].Designation;
	FString World, Why;
	TestFalse(TEXT("not before the chart"), S.Frontier->CanFound(ESettlementKind::Outpost, D, World, Why));
	S.ChartedWithWorlds(0);
	// The outpost first, off the habitable world; then the colony on it; the starbase when Starfleet thinks well of us.
	TestTrue(TEXT("an outpost may be founded"), S.Frontier->CanFound(ESettlementKind::Outpost, D, World, Why));
	TestEqual(TEXT("off the habitable world"), World, D + TEXT(" II"));
	TestFalse(TEXT("no colony without the outpost"), S.Frontier->CanFound(ESettlementKind::Colony, D, World, Why));
	const FSettlement* O = S.Frontier->Found(ESettlementKind::Outpost, D, World);
	if (!TestNotNull(TEXT("founded"), O)) { return false; }
	TestEqual(TEXT("named for its world"), O->Name, D + TEXT(" II Outpost"));
	TestFalse(TEXT("one outpost a system"), S.Frontier->CanFound(ESettlementKind::Outpost, D, World, Why));
	TestTrue(TEXT("a colony may follow"), S.Frontier->CanFound(ESettlementKind::Colony, D, World, Why));
	const FSettlement* C = S.Frontier->Found(ESettlementKind::Colony, D, World);
	if (!TestNotNull(TEXT("settled"), C)) { return false; }
	TestEqual(TEXT("eight hundred"), C->Population, UFrontierSubsystem::ColonyFirstPeople);
	TestTrue(TEXT("a colony world"), S.Frontier->ColonyWorlds().Contains(D + TEXT(" II")));
	TestFalse(TEXT("no starbase unregarded"), S.Frontier->CanFound(ESettlementKind::Starbase, D, World, Why));
	S.Missions->AddStanding(UFrontierSubsystem::StarbaseStandingNeeded, TEXT("test"));
	TestTrue(TEXT("a starbase now"), S.Frontier->CanFound(ESettlementKind::Starbase, D, World, Why));
	const FSettlement* B = S.Frontier->Found(ESettlementKind::Starbase, D, TEXT(""));
	if (!TestNotNull(TEXT("keel laid"), B)) { return false; }
	TestEqual(TEXT("the next number"), B->Name, FString(TEXT("Starbase 400")));
	TestFalse(TEXT("works to go"), B->bCommissioned);
	TestFalse(TEXT("not yet"), S.Frontier->Commission(D, Why));
	TestTrue(TEXT("says how long"), Why.Contains(TEXT("hours")));
	// Eighty hours of ship's time: the works done, the colony a little bigger.
	S.Frontier->AdvanceSeconds(1.f, S.Ship());
	FCrewShipState Clock; S.Roster->DevAdvanceHours(80, Clock);
	S.Frontier->AdvanceSeconds(1.f, S.Ship());
	TestEqual(TEXT("works done"), S.Frontier->WorksHoursLeft(D), 0.0);
	TestTrue(TEXT("commissioned"), S.Frontier->Commission(D, Why));
	TestTrue(TEXT("the colony grew"), S.Frontier->AtWorld(D + TEXT(" II"))->Population > UFrontierSubsystem::ColonyFirstPeople);
	// The system renamed: the settlements follow.
	FString Reply;
	TestTrue(TEXT("renamed"), S.Exploration->Name(D, TEXT("Vashti"), Reply));
	TestNotNull(TEXT("the colony by the new name"), S.Frontier->Find(TEXT("Vashti"), ESettlementKind::Colony));
	TestNotNull(TEXT("its world too"), S.Frontier->AtWorld(TEXT("Vashti II")));
	// Struck: a small colony is lost entire.
	FString Text;
	S.Frontier->Strike(TEXT("Vashti II"), false, Text);
	TestEqual(TEXT("gone"), S.Frontier->AtWorld(TEXT("Vashti II"))->Population, 0.0);
	TestEqual(TEXT("no colony worlds"), S.Frontier->ColonyWorlds().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkMissionOutpostTest, "Slonk.Missions.Outpost", SLONK_TEST_FLAGS)
bool FSlonkMissionOutpostTest::RunTest(const FString& Parameters)
{
	FSystems S;
	const FString D = S.ChartedWithWorlds(0);
	const int32 Id = S.Missions->DevSpawn(EMissionKind::Outpost, EMissionSource::Starfleet);
	const FMission* M = S.Missions->Find(Id);
	if (!TestNotNull(TEXT("an order"), M)) { return false; }
	TestEqual(TEXT("a system"), (int32)M->TargetKind, (int32)EMissionTarget::System);
	TestEqual(TEXT("the charted one"), M->TargetName, D);
	TestEqual(TEXT("one order a system"), S.Missions->DevSpawn(EMissionKind::Outpost, EMissionSource::Starfleet), -1);
	TestEqual(TEXT("nor by proposal"), S.Missions->DevSpawnAt(EMissionKind::Outpost, EMissionSource::Starfleet, D, FString()), -1);
	FString Reply; FMissionOutcome Out;
	TestTrue(TEXT("accepted"), S.Missions->Accept(Id, Reply));
	// The module is loaded alongside any starbase, the site chosen in the system, the outpost raised there.
	TestEqual(TEXT("the stage wants a starbase"), S.Missions->TargetNameOf(*M), FString(TEXT("a starbase")));
	TestFalse(TEXT("not alongside"), S.Missions->Act(Id, 0, Out, Reply));
	S.Missions->AdvanceSeconds(1.f, S.Alongside());
	TestTrue(TEXT("loaded"), S.Missions->Act(Id, 0, Out, Reply));
	TestEqual(TEXT("the site next"), S.Missions->Find(Id)->Stage, 1);
	TestFalse(TEXT("not in the system"), S.Missions->Act(Id, 0, Out, Reply));
	S.Missions->AdvanceSeconds(1.f, S.Ship(D));
	TestTrue(TEXT("site chosen"), S.Missions->Act(Id, 0, Out, Reply));
	TestEqual(TEXT("raising next"), S.Missions->Find(Id)->Stage, 2);
	TestFalse(TEXT("a site"), S.Missions->Find(Id)->Site.IsEmpty());
	for (int32 i = 0; i < 12 && !Out.bMissionOver; ++i) { TestTrue(TEXT("raising"), S.Missions->Act(Id, 0, Out, Reply)); }
	TestTrue(TEXT("over"), Out.bMissionOver);
	TestEqual(TEXT("complete"), (int32)S.Missions->Find(Id)->State, (int32)EMissionState::Completed);
	const FSettlement* O = S.Frontier->Find(D, ESettlementKind::Outpost);
	if (TestNotNull(TEXT("the outpost exists"), O)) { TestEqual(TEXT("at the site"), O->World, S.Missions->Find(Id)->Site); }
	// The starbase: the keel laid in the system, the commissioning refused until the works are done.
	S.Missions->AddStanding(UFrontierSubsystem::StarbaseStandingNeeded, TEXT("test"));
	const int32 Id2 = S.Missions->DevSpawn(EMissionKind::Starbase, EMissionSource::Starfleet);
	if (!TestTrue(TEXT("a starbase order"), Id2 > 0)) { return false; }
	TestTrue(TEXT("the works are in the deadline"), S.Missions->Find(Id2)->DeadlineStardate >= S.Missions->Stardate() + UFrontierSubsystem::StarbaseWorksHours / 24.0 + 2.0 - 0.01);
	TestTrue(TEXT("accepted"), S.Missions->Accept(Id2, Reply));
	S.Missions->AdvanceSeconds(1.f, S.Alongside());
	TestTrue(TEXT("corps embarked"), S.Missions->Act(Id2, 0, Out, Reply));
	S.Missions->AdvanceSeconds(1.f, S.Ship(D));
	TestTrue(TEXT("keel laid"), S.Missions->Act(Id2, 0, Out, Reply));
	TestNotNull(TEXT("the works begun"), S.Frontier->Find(D, ESettlementKind::Starbase));
	TestFalse(TEXT("commissioning refused"), S.Missions->Act(Id2, 1, Out, Reply));
	TestTrue(TEXT("says the hours"), Reply.Contains(TEXT("hours")));
	TestTrue(TEXT("the watch held"), S.Missions->Act(Id2, 0, Out, Reply));
	TestEqual(TEXT("still the works"), S.Missions->Find(Id2)->Stage, 2);
	S.Frontier->AdvanceSeconds(1.f, S.Ship(D));
	FCrewShipState Clock; S.Roster->DevAdvanceHours(80, Clock);
	S.Frontier->AdvanceSeconds(1.f, S.Ship(D));
	S.Missions->AdvanceSeconds(1.f, S.Ship(D));
	TestTrue(TEXT("commissioned"), S.Missions->Act(Id2, 1, Out, Reply));
	TestTrue(TEXT("no check to fail"), Out.bSuccess && Out.bMissionOver);
	TestTrue(TEXT("in commission"), S.Frontier->Find(D, ESettlementKind::Starbase)->bCommissioned);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkMissionImpactTest, "Slonk.Missions.Impact", SLONK_TEST_FLAGS)
bool FSlonkMissionImpactTest::RunTest(const FString& Parameters)
{
	FSystems S;
	const FString D = S.ChartedWithWorlds(0);
	const FString World = D + TEXT(" II");
	S.Frontier->Found(ESettlementKind::Outpost, D, World);
	S.Frontier->Found(ESettlementKind::Colony, D, World);
	S.Missions->AdvanceSeconds(1.f, S.Ship(D));
	// A rock for the colony world: tracked, then diverted at the rock.
	const int32 Id = S.Missions->RaiseEvent(EMissionKind::Impact);
	const FMission* M = S.Missions->Find(Id);
	if (!TestNotNull(TEXT("raised"), M)) { return false; }
	TestEqual(TEXT("the colony world"), M->TargetName, World);
	TestTrue(TEXT("a rock named"), M->Site.StartsWith(TEXT("Asteroid")) || M->Site.StartsWith(TEXT("Comet")));
	TestTrue(TEXT("hours to the landing"), M->ImpactHours >= 20.0 && M->ImpactHours <= 44.0);
	TestTrue(TEXT("the deadline is the landing"), FMath::IsNearlyEqual(M->DeadlineStardate, S.Missions->Stardate() + M->ImpactHours / 24.0, 0.01));
	FString Reply; FMissionOutcome Out;
	TestTrue(TEXT("the evacuation"), S.Missions->Act(Id, 1, Out, Reply));
	TestEqual(TEXT("warned if it carried"), S.Missions->Find(Id)->bEvacuated, Out.bSuccess);
	TestEqual(TEXT("divert next"), S.Missions->Find(Id)->Stage, 1);
	TestFalse(TEXT("the rock not in range"), S.Missions->Act(Id, 0, Out, Reply));
	S.Missions->AdvanceSeconds(1.f, S.Ship(D, { M->Site }));
	for (int32 i = 0; i < 12 && !Out.bMissionOver; ++i) { TestTrue(TEXT("tried"), S.Missions->Act(Id, i % 2, Out, Reply)); }
	TestTrue(TEXT("diverted"), Out.bMissionOver);
	TestEqual(TEXT("complete"), (int32)S.Missions->Find(Id)->State, (int32)EMissionState::Completed);
	TestEqual(TEXT("the colony untouched"), S.Frontier->AtWorld(World)->Population, UFrontierSubsystem::ColonyFirstPeople);
	// Another, left alone: the hour comes, the rock lands, the colony is lost and Starfleet's regard with it.
	const int32 Id2 = S.Missions->RaiseEvent(EMissionKind::Impact);
	if (!TestTrue(TEXT("raised again"), Id2 > 0)) { return false; }
	const int32 Standing0 = S.Missions->GetStanding();
	FCrewShipState Clock; S.Roster->DevAdvanceHours(60, Clock);
	S.Missions->AdvanceSeconds(1.f, S.Ship(D));
	TestEqual(TEXT("failed"), (int32)S.Missions->Find(Id2)->State, (int32)EMissionState::Failed);
	TestEqual(TEXT("the regard lost"), S.Missions->GetStanding(), Standing0 - 40);
	TestEqual(TEXT("the colony gone"), S.Frontier->AtWorld(World)->Population, 0.0);
	// A third, for another colony, abandoned by the captain: the rock is still real, and its landing is still the
	// ship's failure.
	const FString D2 = S.ChartedWithWorlds(1); const FString World2 = D2 + TEXT(" II");
	S.Frontier->Found(ESettlementKind::Outpost, D2, World2); S.Frontier->Found(ESettlementKind::Colony, D2, World2);
	S.Missions->AdvanceSeconds(1.f, S.Ship(D2));
	const int32 Id3 = S.Missions->RaiseEvent(EMissionKind::Impact);
	if (!TestTrue(TEXT("raised a third time"), Id3 > 0)) { return false; }
	const FString Rock3 = S.Missions->Find(Id3)->Site;
	TestTrue(TEXT("abandoned"), S.Missions->Abandon(Id3, Reply));
	TestFalse(TEXT("the rock is still out there"), S.Missions->Find(Id3)->bRockResolved);
	const int32 Standing1 = S.Missions->GetStanding();
	S.Missions->ImpactOutcome(Rock3, World2, EImpactOutcome::Struck);
	TestEqual(TEXT("the second colony gone"), S.Frontier->AtWorld(World2)->Population, 0.0);
	TestEqual(TEXT("failed after all"), (int32)S.Missions->Find(Id3)->State, (int32)EMissionState::Failed);
	TestTrue(TEXT("the rock done with"), S.Missions->Find(Id3)->bRockResolved);
	TestEqual(TEXT("the loss taken"), S.Missions->GetStanding(), Standing1 - 40);
	S.Missions->ImpactOutcome(Rock3, World2, EImpactOutcome::Struck);
	TestEqual(TEXT("once"), S.Missions->GetStanding(), Standing1 - 40);
	// A fourth, abandoned with its rock still out: the world is spoken for until the rock is done with.
	const FString D3 = S.ChartedWithWorlds(2); const FString World3 = D3 + TEXT(" II");
	S.Frontier->Found(ESettlementKind::Outpost, D3, World3); S.Frontier->Found(ESettlementKind::Colony, D3, World3);
	S.Missions->AdvanceSeconds(1.f, S.Ship(D3));
	const int32 Id4 = S.Missions->RaiseEvent(EMissionKind::Impact);
	if (!TestTrue(TEXT("raised a fourth time"), Id4 > 0)) { return false; }
	TestTrue(TEXT("abandoned too"), S.Missions->Abandon(Id4, Reply));
	TestEqual(TEXT("no second rock for a world under one"), S.Missions->RaiseEvent(EMissionKind::Impact), -1);
	// A world not ours struck: the scar is the frontier's to keep.
	S.Missions->ImpactOutcome(TEXT("Asteroid 0-Z"), D3 + TEXT(" III"), EImpactOutcome::Struck);
	TestEqual(TEXT("a quarter of its lights"), S.Frontier->WorldScar(D3 + TEXT(" III")), 0.25f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkMissionColonyTest, "Slonk.Missions.Colony", SLONK_TEST_FLAGS)
bool FSlonkMissionColonyTest::RunTest(const FString& Parameters)
{
	FSystems S;
	const FString D = S.ChartedWithWorlds(0);
	const FString World = D + TEXT(" II");
	S.Frontier->Found(ESettlementKind::Outpost, D, World);
	const int32 Id = S.Missions->DevSpawn(EMissionKind::Colony, EMissionSource::Starfleet);
	const FMission* M = S.Missions->Find(Id);
	if (!TestNotNull(TEXT("an order"), M)) { return false; }
	TestEqual(TEXT("the habitable world"), M->TargetName, World);
	TestEqual(TEXT("its system"), M->Site, D);
	TestEqual(TEXT("one order a system"), S.Missions->DevSpawn(EMissionKind::Colony, EMissionSource::Starfleet), -1);
	FString Reply; FMissionOutcome Out;
	TestTrue(TEXT("accepted"), S.Missions->Accept(Id, Reply));
	S.Missions->AdvanceSeconds(1.f, S.Alongside());
	TestTrue(TEXT("embarked"), S.Missions->Act(Id, 1, Out, Reply));   // the volunteers: six hundred, or five when it goes badly
	const double People = Out.bSuccess ? 600.0 : 500.0;
	TestFalse(TEXT("not at the world"), S.Missions->Act(Id, 0, Out, Reply));
	TestNull(TEXT("no colony yet"), S.Frontier->Find(D, ESettlementKind::Colony));
	S.Missions->AdvanceSeconds(1.f, S.Ship(D, { World }));
	TestTrue(TEXT("landed"), S.Missions->Act(Id, 0, Out, Reply));
	TestEqual(TEXT("the first weeks next"), S.Missions->Find(Id)->Stage, 2);
	// The colony is theirs from the landing: lit, counted, and a world the sky can threaten.
	if (!TestNotNull(TEXT("founded at the landing"), S.Frontier->Find(D, ESettlementKind::Colony))) { return false; }
	TestTrue(TEXT("a colony world"), S.Frontier->ColonyWorlds().Contains(World));
	TestEqual(TEXT("the people who embarked"), S.Frontier->Find(D, ESettlementKind::Colony)->Population, People);
	TestTrue(TEXT("the ship leaves them to it"), S.Missions->Act(Id, 1, Out, Reply));
	TestTrue(TEXT("over"), Out.bMissionOver);
	int32 Colonies = 0; for (const FSettlement* X : S.Frontier->InSystem(D)) { if (X->Kind == ESettlementKind::Colony) { ++Colonies; } }
	TestEqual(TEXT("one colony"), Colonies, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFrontierPrimeTest, "Slonk.Frontier.Prime", SLONK_TEST_FLAGS)
bool FSlonkFrontierPrimeTest::RunTest(const FString& Parameters)
{
	// Two lit worlds, both habitable: nothing of ours goes to either; the outpost takes the dead one.
	FSystems S;
	const FString D = S.Exploration->GetCatalogue()[0].Designation;
	S.Exploration->Chart(D, false);
	S.Exploration->SetWorldsForTest(D, { D + TEXT(" I"), D + TEXT(" II"), D + TEXT(" III") }, { D + TEXT(" II"), D + TEXT(" III") }, D + TEXT(" II"), { D + TEXT(" II"), D + TEXT(" III") });
	const FCatalogueEntry* E = S.Exploration->Find(D);
	TestTrue(TEXT("the second lit world is theirs too"), E && E->IsLit(D + TEXT(" III")));
	FString World, Why;
	TestTrue(TEXT("an outpost may be founded"), S.Frontier->CanFound(ESettlementKind::Outpost, D, World, Why));
	TestEqual(TEXT("off the dead world"), World, D + TEXT(" I"));
	TestNotNull(TEXT("founded"), S.Frontier->Found(ESettlementKind::Outpost, D, World));
	TestFalse(TEXT("no colony in someone's sky"), S.Frontier->CanFound(ESettlementKind::Colony, D, World, Why));
	TestTrue(TEXT("says whose"), Why.Contains(TEXT("someone's")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkFrontierScarTest, "Slonk.Frontier.Scar", SLONK_TEST_FLAGS)
bool FSlonkFrontierScarTest::RunTest(const FString& Parameters)
{
	// A warned strike on a big colony: a third die, the lights carry the scar until the people are back.
	FSystems S;
	const FString D = S.ChartedWithWorlds(0); const FString World = D + TEXT(" II");
	S.Frontier->Found(ESettlementKind::Outpost, D, World);
	const FSettlement* C = S.Frontier->Found(ESettlementKind::Colony, D, World, 10000.0);
	if (!TestNotNull(TEXT("settled"), C)) { return false; }
	TestEqual(TEXT("ten thousand"), C->Population, 10000.0);
	FString Text; S.Frontier->Strike(World, true, Text);
	C = S.Frontier->AtWorld(World);
	TestEqual(TEXT("sixty-five hundred left"), C->Population, 6500.0);
	TestEqual(TEXT("scarred by half"), C->Scar, 0.5f);
	// Forty-four days at one in a hundred: past ten thousand again, the scar gone.
	S.Frontier->AdvanceSeconds(1.f, S.Ship());
	FCrewShipState Clock; S.Roster->DevAdvanceHours(44 * 24, Clock);
	S.Frontier->AdvanceSeconds(1.f, S.Ship());
	C = S.Frontier->AtWorld(World);
	TestTrue(TEXT("grown back"), C->Population >= 10000.0);
	TestEqual(TEXT("the scar gone"), C->Scar, 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkImpactorTrackTest, "Slonk.Impactor.Track", SLONK_TEST_FLAGS)
bool FSlonkImpactorTrackTest::RunTest(const FString& Parameters)
{
	// A track past a point: the miss is the perpendicular, the time the run to it; behind the rock it is the distance now.
	const FVector Pos(0, 0, 0), Vel(1000, 0, 0);
	TestTrue(TEXT("the miss"), FMath::IsNearlyEqual(AImpactor::TrackMissDistance(Pos, Vel, FVector(1.0e6, 500, 0)), 500.0, 1.0e-6));
	TestTrue(TEXT("the time"), FMath::IsNearlyEqual(AImpactor::TrackSecondsToClosest(Pos, Vel, FVector(1.0e6, 500, 0)), 1000.0, 1.0e-6));
	TestTrue(TEXT("dead on"), AImpactor::TrackMissDistance(Pos, Vel, FVector(5.0e5, 0, 0)) < 1.0e-6);
	TestTrue(TEXT("behind"), FMath::IsNearlyEqual(AImpactor::TrackMissDistance(Pos, Vel, FVector(-1000, 0, 0)), 1000.0, 1.0e-6));
	TestEqual(TEXT("no time behind"), AImpactor::TrackSecondsToClosest(Pos, Vel, FVector(-1000, 0, 0)), 0.0);
	// A sideways push a day out clears a world: eighty metres a second over twenty hours is a radius and more.
	const FVector Far(-2.5e6 * 20 * 3600, 0, 0);
	TestTrue(TEXT("on course from far"), AImpactor::TrackMissDistance(Far, FVector(2.5e6, 0, 0), Pos) < 1.0);
	TestTrue(TEXT("pushed clear"), AImpactor::TrackMissDistance(Far, FVector(2.5e6, 9000, 0), Pos) > 6.0e8);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkInteriorRoomsTest, "Slonk.Interior.Rooms", SLONK_TEST_FLAGS)
bool FSlonkInteriorRoomsTest::RunTest(const FString& Parameters)
{
	// Every room is named and decked; the lift stops at the corridor for the rooms off it and nowhere else, and the
	// three off it share its deck.
	for (int32 i = 0; i < (int32)EShipRoom::Count; ++i)
	{
		const EShipRoom R = (EShipRoom)i;
		TestTrue(TEXT("named"), !UShipInteriorComponent::RoomName(R).IsEmpty());
		TestTrue(TEXT("decked"), !UShipInteriorComponent::DeckOf(R).IsEmpty());
		const EShipRoom Stop = UShipInteriorComponent::LiftStop(R);
		TestTrue(TEXT("a stop has its own lift"), UShipInteriorComponent::IsLiftStop(Stop));
		const bool bOff = R == EShipRoom::Sickbay || R == EShipRoom::Transporter || R == EShipRoom::RecDeck;
		TestEqual(TEXT("off the corridor or not"), (int32)Stop, (int32)(bOff ? EShipRoom::Corridor : R));
		if (bOff) { TestEqual(TEXT("on the corridor's deck"), UShipInteriorComponent::DeckOf(R), UShipInteriorComponent::DeckOf(EShipRoom::Corridor)); }
	}
	TestEqual(TEXT("the console's words"), (int32)AStarshipPawn::RoomFromWord(TEXT("torp")), (int32)EShipRoom::TorpedoBay);
	TestEqual(TEXT("the console's words"), (int32)AStarshipPawn::RoomFromWord(TEXT("Mess")), (int32)EShipRoom::Mess);
	TestEqual(TEXT("the console's words"), (int32)AStarshipPawn::RoomFromWord(TEXT("nowhere")), (int32)EShipRoom::Count);
	// The compartments the crew are placed in (and the hits find them in) are rooms of the interior.
	for (const TCHAR* Name : { TEXT("Officers' mess"), TEXT("Rec deck"), TEXT("Crew quarters"), TEXT("Officers' quarters"), TEXT("Torpedo bay"), TEXT("Cargo bays"), TEXT("Sickbay"), TEXT("Main engineering"), TEXT("Main bridge"), TEXT("Shuttle bay") })
	{
		bool bFound = false;
		for (int32 i = 0; i < (int32)EShipRoom::Count; ++i) { if (UShipInteriorComponent::RoomName((EShipRoom)i) == Name) { bFound = true; } }
		TestTrue(*FString::Printf(TEXT("%s is a room"), Name), bFound);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSlonkCrewPlacesTest, "Slonk.Crew.Places", SLONK_TEST_FLAGS)
bool FSlonkCrewPlacesTest::RunTest(const FString& Parameters)
{
	// Over a ship's day everyone off watch eats in the officers' mess or on the rec deck, sleeps in quarters and
	// takes their ease on the rec deck or in quarters; every place the routine names is a room of the interior.
	FSystems S;
	FCrewShipState Clock;
	TSet<FString> Places;
	int32 Meals = 0, OfficersMeals = 0, Rec = 0, Sleep = 0;
	for (int32 Hour = 0; Hour < 24; ++Hour)
	{
		S.Roster->DevAdvanceHours(1, Clock);
		for (const FCrewMember& M : S.Roster->GetRoster())
		{
			switch (M.Activity)
			{
			case ECrewActivity::Mess:
				TestTrue(TEXT("eats in a mess"), M.Where == TEXT("Officers' mess") || M.Where == TEXT("Rec deck"));
				++Meals; if (M.Where == TEXT("Officers' mess")) { ++OfficersMeals; }
				Places.Add(M.Where); break;
			case ECrewActivity::Sleeping:
				TestTrue(TEXT("sleeps in quarters"), M.Where.Contains(TEXT("quarters"))); ++Sleep; Places.Add(M.Where); break;
			case ECrewActivity::Recreation:
				if (M.Where == TEXT("Rec deck")) { ++Rec; } else { TestTrue(TEXT("or keeps to quarters"), M.Where.Contains(TEXT("quarters"))); }
				Places.Add(M.Where); break;
			default: break;
			}
		}
	}
	TestTrue(TEXT("meals taken"), Meals > 0);
	TestTrue(TEXT("officers in their mess"), OfficersMeals > 0 && OfficersMeals < Meals);
	TestTrue(TEXT("the rec deck used"), Rec > 0);
	TestTrue(TEXT("sleep in quarters"), Sleep > 0);
	for (const FString& P : Places)
	{
		bool bRoom = false;
		for (int32 i = 0; i < (int32)EShipRoom::Count; ++i) { if (UShipInteriorComponent::RoomName((EShipRoom)i) == P) { bRoom = true; } }
		TestTrue(*FString::Printf(TEXT("%s is a room of the interior"), *P), bRoom);
	}
	return true;
}

#endif
