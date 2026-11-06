// Slonk - the fleet: the other Starfleet ships as crewed ships with missions of their own, living on the ship's
// clock; their troubles become requests for support, their work something to lend a hand with when nearby; people
// transfer between them and us, and the crew's ties aboard them are real names. Design in docs/fleet-design.md.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CrewRoster.h"
#include "Missions.h"
#include "Fleet.generated.h"

class AProceduralStarship;

UENUM(BlueprintType)
enum class EFleetTask : uint8 { Idle = 0, UnderWay, OnStation, Returning, Count UMETA(Hidden) };

/** What has gone wrong aboard a ship on its mission, and which department of ours would answer it. */
UENUM(BlueprintType)
enum class EFleetTrouble : uint8 { None = 0, Engineering, Medical, Hostiles, Boarding, Count UMETA(Hidden) };

/** One of ours who knows this person, and how ("sister", "oldest friend", "old shipmate"). */
USTRUCT(BlueprintType)
struct FFleetTie
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) int32 CrewId = -1;
	UPROPERTY(BlueprintReadOnly) FString Relation;
};

/** Someone aboard another ship: an officer, or a person with ties to ours (family, friends, a transfer). */
USTRUCT(BlueprintType)
struct FFleetPerson
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FCrewMember Person;
	/** Everyone of ours who knows them: one record, however many know them. */
	UPROPERTY(BlueprintReadOnly) TArray<FFleetTie> Ties;
	bool TiedTo(int32 CrewId) const { for (const FFleetTie& T : Ties) { if (T.CrewId == CrewId) { return true; } } return false; }
	/** Where they came from, for the file ("formerly of the Enterprise", "served under Captain Vashti"). */
	UPROPERTY(BlueprintReadOnly) FString Origin;
};

USTRUCT(BlueprintType)
struct FFleetMission
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) EMissionKind Kind = EMissionKind::Patrol;
	UPROPERTY(BlueprintReadOnly) FString Title;
	UPROPERTY(BlueprintReadOnly) FString TargetName;
	/** The target lies in another star system: the ship goes out of sight for the run. */
	UPROPERTY(BlueprintReadOnly) bool bFar = false;
	UPROPERTY(BlueprintReadOnly) EFleetTask Task = EFleetTask::Idle;
	/** Ship's hours of travel or work left in the present task. */
	UPROPERTY(BlueprintReadOnly) float HoursLeft = 0.f;
	UPROPERTY(BlueprintReadOnly) float WorkHours = 6.f;
	UPROPERTY(BlueprintReadOnly) double StartedStardate = 0.0;
	UPROPERTY(BlueprintReadOnly) EFleetTrouble Trouble = EFleetTrouble::None;
	UPROPERTY(BlueprintReadOnly) FString TroubleText;
	UPROPERTY(BlueprintReadOnly) float TroubleHours = 0.f;
	/** The support mission raised for this trouble (our board), and whether a hand was lent to the work. */
	UPROPERTY(BlueprintReadOnly) int32 SupportMissionId = -1;
	UPROPERTY(BlueprintReadOnly) int32 AssistMissionId = -1;
	UPROPERTY(BlueprintReadOnly) bool bAssisted = false;
	/** A hand was offered this mission, whatever came of it: one offer per mission. */
	UPROPERTY(BlueprintReadOnly) bool bAssistOffered = false;
	/** We told her no help was coming (the support mission abandoned); she carries on alone and remembers it. */
	UPROPERTY(BlueprintReadOnly) bool bHelpRefused = false;
	bool IsActive() const { return Task != EFleetTask::Idle; }
};

USTRUCT(BlueprintType)
struct FFleetShip
{
	GENERATED_BODY()
	/** "USS Hood": the vessel's display name, the key everywhere. */
	UPROPERTY(BlueprintReadOnly) FString Name;
	UPROPERTY(BlueprintReadOnly) FString ShipClass;
	UPROPERTY(BlueprintReadOnly) int32 Seed = 0;
	UPROPERTY(BlueprintReadOnly) FCrewMember Captain;
	/** The heads of department. */
	UPROPERTY(BlueprintReadOnly) TArray<FFleetPerson> Officers;
	/** People with ties to ours, and transfers either way. */
	UPROPERTY(BlueprintReadOnly) TArray<FFleetPerson> Notables;
	UPROPERTY(BlueprintReadOnly) int32 Complement = 400;
	UPROPERTY(BlueprintReadOnly) int32 Lost = 0;
	UPROPERTY(BlueprintReadOnly) float Morale = 0.6f;
	/** Hull and systems, 0..1; low after a bad mission, repaired alongside. */
	UPROPERTY(BlueprintReadOnly) float Condition = 1.f;
	/** Their regard for us, -1..1: help given, requests ignored, people exchanged. */
	UPROPERTY(BlueprintReadOnly) float Relation = 0.f;
	UPROPERTY(BlueprintReadOnly) FString HomeStation;
	UPROPERTY(BlueprintReadOnly) FString SystemName;
	UPROPERTY(BlueprintReadOnly) FFleetMission Mission;
	UPROPERTY(BlueprintReadOnly) TArray<FString> Log;
	UPROPERTY(BlueprintReadOnly) int32 Done = 0;
	UPROPERTY(BlueprintReadOnly) int32 Failed = 0;
	UPROPERTY(BlueprintReadOnly) double NextMissionHours = 0.0;
	UPROPERTY(BlueprintReadOnly) bool bLost = false;
	/** The actor in the world, bound by name each scan; null while in another system or unspawned. */
	TWeakObjectPtr<AProceduralStarship> Actor;
	/** The hull has been put where the saved record says (away, at its target, or home) since this session's load. */
	bool bReconciled = false;
	FString TaskText() const;
	const FFleetPerson* Head(ECrewDepartment Dept) const;
	float Skill(ECrewSkill S) const;
};

/** A fleet ship asking something of us, for the mission system to put on the board. */
struct FFleetRequest
{
	FString ShipName;
	/** Support: trouble on their mission; Assist: their ordinary work, we are nearby. */
	bool bSupport = true;
	EFleetTrouble Trouble = EFleetTrouble::None;
	FString Text;
	FString MissionTitle;
	EMissionKind TheirKind = EMissionKind::Patrol;
	float HoursLeft = 24.f;
};

UCLASS()
class SLONK_API USlonkFleetSaveGame : public USaveGame
{
	GENERATED_BODY()
public:
	UPROPERTY() TArray<FFleetShip> Ships;
	UPROPERTY() int32 NextPersonId = 300000;
};

/**
 * The fleet registry: every USS in the galaxy gets a record the first time it is seen, with a captain and heads
 * of department made from the same pools as our crew, and from then on lives on the ship's clock: a mission every
 * few hours (travel, work, return; the actor warps to the target when it is in this system, goes out of sight when
 * it is not), trouble on the way that becomes a request for our support, work we can lend a hand with when we are
 * near. Our crew's ties aboard these ships become named people here; transfers move whole records between the
 * ships and us. Saved to the "Fleet" slot.
 */
UCLASS()
class SLONK_API UFleetSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** Driven by the mission system each tick with the same ship state. */
	void AdvanceSeconds(float Seconds, const FMissionShipState& Ship);

	// ---- queries ----
	const TArray<FFleetShip>& GetShips() const { return Ships; }
	const FFleetShip* Find(const FString& Name) const;
	/** Ships in hailing range (by the last ship state). */
	TArray<const FFleetShip*> Nearby() const;
	int32 GetRevision() const { return Revision; }
	void DrainRequests(TArray<FFleetRequest>& Out) { Out = MoveTemp(Requests); Requests.Reset(); }
	void DrainReports(TArray<FCrewReport>& Out) { Out = MoveTemp(Reports); Reports.Reset(); }
	/** The people aboard a ship tied to one of ours (notables and officers alike). */
	TArray<const FFleetPerson*> TiedTo(const FString& ShipName, int32 CrewId) const;

	// ---- the mission system's answers ----
	/** Our support mission came to an end: the trouble is over (success), or it is not (failure or expiry). */
	void SupportResolved(const FString& ShipName, bool bSuccess, bool bIgnored);
	/** The captain abandoned her call: she is told no help is coming and works at it alone, with the day she has left. */
	void SupportDeclined(const FString& ShipName);
	/** A hand lent to their ordinary work. */
	void AssistResolved(const FString& ShipName, bool bSuccess);
	/** The mission board was wiped (a reset): nothing of ours is on any ship's record any more. */
	void BoardCleared();

	/** The mission system raised one of ours for their trouble, or for their work. */
	void SetSupportMission(const FString& ShipName, int32 MissionId);
	void SetAssistMission(const FString& ShipName, int32 MissionId);
	/** A ship at work, no trouble, nothing of ours on it yet: a hand to lend when we are near. */
	bool WantsAssist(const FFleetShip& S) const { return !S.bLost && S.Mission.Task == EFleetTask::OnStation && S.Mission.Trouble == EFleetTrouble::None && !S.Mission.bAssistOffered; }

	// ---- people ----
	/** One of ours goes to them (fit, not a head, the ship in range). */
	bool TransferTo(const FString& ShipName, int32 CrewId, FString& OutReply);
	/** One of theirs comes to us (not an officer; they must think well enough of us, or the person has family here). */
	bool RequestPerson(const FString& ShipName, const FString& PersonName, FString& OutReply);
	/** A ship was destroyed: its people with it. */
	void NoteLost(const FString& ShipName, const FString& ByWhom);

	// ---- dev ----
	bool DevTrouble(const FString& ShipName, EFleetTrouble Kind, FString& OutReply);
	bool DevMission(const FString& ShipName, FString& OutReply);
	/** Dev: her actor warps to just off our bow. */
	bool DevSummon(const FString& ShipName, FString& OutReply);
	/** Dev: a mission in this system, at work now. */
	bool DevWork(const FString& ShipName, FString& OutReply);
	UFUNCTION(BlueprintCallable, Category = "Fleet") void Reset();
	/** Tests: wired to these systems directly, no save slot, a fixed seed. */
	void InitForTest(UCrewRosterSubsystem* InRoster, UMissionSubsystem* InMissions);
	/** A ship by name without a hull (another system, a test). Returns false when the name is taken. */
	bool RegisterShip(const FString& Name, const FString& ShipClass, const FString& Home, const FString& System);
	void Save();

	static FString TaskName(EFleetTask T);
	/** "sister of our Lt. X; oldest friend of our Ensign Y", for the files. */
	static FString TiesText(const FFleetPerson& P, const UCrewRosterSubsystem* R);
	static FString TroubleName(EFleetTrouble T);
	static bool ParseTrouble(const FString& Word, EFleetTrouble& Out);

private:
	void Load();
	UCrewRosterSubsystem* Roster() const;
	UMissionSubsystem* Missions() const;
	double Hours() const;
	double Stardate() const;
	void Dirty() { ++Revision; bSaveWanted = true; SaveDelaySeconds = 3.f; }
	void Report(const FString& Text, const FLinearColor& Color = FLinearColor(0.75f, 0.9f, 1.f), ECrewDepartment From = ECrewDepartment::Operations);
	/** Every USS in the world: a record for each new name, the actor bound for each known one. */
	void ScanShips();
	FFleetShip MakeShip(const FString& Name, const FString& ShipClass, const FString& Home, const FString& System);
	/** Our crew's ties aboard the fleet's ships become people aboard them: one pass over the roster, grouped by ship. */
	void BindTies();
	/** This ship's share of the ties (crew member, tie), one record per name aboard her. */
	void BindShipTies(FFleetShip& S, const TArray<TPair<const FCrewMember*, const FCrewTie*>>& Ties);
	/** One ship's hour: take a mission, travel, work, trouble, return. */
	void LiveHour(FFleetShip& S);
	void BeginMission(FFleetShip& S, bool bForceNear = false, bool bMoveActor = true);
	void EndMission(FFleetShip& S, bool bSuccess, const FString& Why);
	void Trouble(FFleetShip& S, EFleetTrouble Kind);
	/** Someone aboard dies of the trouble; our crew tied to them hear of it. */
	void Casualty(FFleetShip& S);
	/** She got through her trouble herself: the request withdrawn, nothing owed either way. */
	void SelfResolved(FFleetShip& S);
	/** Whatever of ours was on her board entries is withdrawn (a loss, a reset, a recall). */
	void DropBoardEntries(FFleetShip& S);
	/** The hull put where the record says, once after a load. */
	void Reconcile(FFleetShip& S);
	FFleetShip* FindMutable(const FString& Name);
	/** The actor goes to, or comes back from, its mission. */
	void SendActor(FFleetShip& S, bool bOut);
	FString Log(FFleetShip& S, const FString& Text);
	bool InRange(const FFleetShip& S) const;

	UPROPERTY() TArray<FFleetShip> Ships;
	int32 NextPersonId = 300000;
	int32 Revision = 0;
	FMissionShipState Last;
	TArray<FFleetRequest> Requests;
	UPROPERTY() TArray<FCrewReport> Reports;
	FRandomStream Rng;
	double LastHour = -1.0;
	/** While the hours are replayed, the hour being lived (Hours() and Stardate() read it); -1 otherwise. */
	double SimHours = -1.0;
	TSet<FString> LastNear;
	/** The roster revision the ties were last bound at: they change only when the roster does, or a ship is new. */
	int32 TiesRevision = -1;
	bool bBindWanted = false;
	float ScanTimer = 0.f;
	bool bSaveWanted = false;
	bool bNoDisk = false;
	TWeakObjectPtr<UCrewRosterSubsystem> RosterOverride;
	TWeakObjectPtr<UMissionSubsystem> MissionsOverride;
	float SaveDelaySeconds = 0.f;
	static const TCHAR* SlotName;
};
