// Slonk - missions: Starfleet's orders over subspace, a starbase's postings, and what simply happens in space; each
// a few situations resolved by the captain's choice and the crew's checks, emitting the crew's experiences; the
// tracker, the inbox and the log. Design in docs/crew-design.md §4 and docs/missions-design.md. Saved to disk.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CrewRoster.h"
#include "Missions.generated.h"

UENUM(BlueprintType)
enum class EMissionKind : uint8 { Survey = 0, Rescue, Diplomacy, Delivery, Anomaly, Medical, Patrol, Contact, Derelict, Smuggler, Support, Assist, Explore, FirstContact, PreWarp, Beacon, Count UMETA(Hidden) };

UENUM(BlueprintType)
enum class EMissionSource : uint8 { Starfleet = 0, Starbase, Event, Fleet, Count UMETA(Hidden) };

UENUM(BlueprintType)
enum class EMissionState : uint8 { Offered = 0, Active, Completed, Failed, Abandoned, Expired, Count UMETA(Hidden) };

UENUM(BlueprintType)
enum class EMissionTarget : uint8 { None = 0, World, Station, Vessel, System, Count UMETA(Hidden) };

/** One way to take a situation: a check against the crew, and what follows either way. */
USTRUCT(BlueprintType)
struct FMissionOption
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FString Text;
	UPROPERTY(BlueprintReadOnly) ECrewSkill Skill = ECrewSkill::Command;
	UPROPERTY(BlueprintReadOnly) float Difficulty = 50.f;
	/** The department whose head and second lead the attempt (the party), with the rest picked by the skill. */
	UPROPERTY(BlueprintReadOnly) ECrewDepartment Party = ECrewDepartment::Command;
	/** The experience each outcome is for the crew (empty: none). */
	UPROPERTY(BlueprintReadOnly) FName SuccessTag;
	UPROPERTY(BlueprintReadOnly) FName FailTag;
	UPROPERTY(BlueprintReadOnly) FString SuccessText;
	UPROPERTY(BlueprintReadOnly) FString FailText;
	/** The next stage on each outcome; -1 ends the mission (success / failure by the flags). */
	UPROPERTY(BlueprintReadOnly) int32 NextOnSuccess = -1;
	UPROPERTY(BlueprintReadOnly) int32 NextOnFail = -1;
	UPROPERTY(BlueprintReadOnly) bool bFailEndsMission = false;
	/** Chance on a failure that someone of the party is hurt. */
	UPROPERTY(BlueprintReadOnly) float Risk = 0.f;
	/** Science points a success earns. */
	UPROPERTY(BlueprintReadOnly) int32 Science = 0;
	/** Refused until the science log's survey of the target system is complete; carried without a check then. */
	UPROPERTY(BlueprintReadOnly) bool bNeedsSurvey = false;
};

USTRUCT(BlueprintType)
struct FMissionStage
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FString Title;
	UPROPERTY(BlueprintReadOnly) FString Text;
	UPROPERTY(BlueprintReadOnly) TArray<FMissionOption> Options;
	/** The ship must be at the target to act on it. */
	UPROPERTY(BlueprintReadOnly) bool bAtTarget = true;
};

USTRUCT(BlueprintType)
struct FMission
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) int32 Id = -1;
	UPROPERTY(BlueprintReadOnly) EMissionKind Kind = EMissionKind::Survey;
	UPROPERTY(BlueprintReadOnly) EMissionSource Source = EMissionSource::Starfleet;
	UPROPERTY(BlueprintReadOnly) EMissionState State = EMissionState::Offered;
	UPROPERTY(BlueprintReadOnly) FString Title;
	UPROPERTY(BlueprintReadOnly) FString Brief;
	UPROPERTY(BlueprintReadOnly) FString From;
	UPROPERTY(BlueprintReadOnly) EMissionTarget TargetKind = EMissionTarget::None;
	UPROPERTY(BlueprintReadOnly) FString TargetName;
	/** The people or power concerned, if any. */
	UPROPERTY(BlueprintReadOnly) FName Group;
	UPROPERTY(BlueprintReadOnly) int32 Stage = 0;
	UPROPERTY(BlueprintReadOnly) TArray<FMissionStage> Stages;
	UPROPERTY(BlueprintReadOnly) double OfferedStardate = 0.0;
	UPROPERTY(BlueprintReadOnly) double DeadlineStardate = 0.0;
	UPROPERTY(BlueprintReadOnly) double ClosedStardate = 0.0;
	/** Starfleet's regard for a success (and the loss of a failure, half). */
	UPROPERTY(BlueprintReadOnly) int32 Standing = 10;
	/** The loss of a failure when it is not half (a breach of the Prime Directive); -1 for half. */
	UPROPERTY(BlueprintReadOnly) int32 FailLoss = -1;
	UPROPERTY(BlueprintReadOnly) int32 ScienceEarned = 0;
	/** The crew the captain named for the attempt; empty for the department's own. */
	UPROPERTY(BlueprintReadOnly) TArray<int32> Party;
	UPROPERTY(BlueprintReadOnly) TArray<FString> Log;
	/** The station that posted it, for a starbase's board. */
	UPROPERTY(BlueprintReadOnly) FString PostedAt;
	bool IsOpen() const { return State == EMissionState::Offered || State == EMissionState::Active; }
	const FMissionStage* Current() const { return Stages.IsValidIndex(Stage) ? &Stages[Stage] : nullptr; }
};

USTRUCT(BlueprintType)
struct FStarfleetMessage
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) double Stardate = 0.0;
	UPROPERTY(BlueprintReadOnly) FString From;
	UPROPERTY(BlueprintReadOnly) FString Subject;
	UPROPERTY(BlueprintReadOnly) FString Text;
	UPROPERTY(BlueprintReadOnly) int32 MissionId = -1;
	UPROPERTY(BlueprintReadOnly) bool bRead = false;
};

/** What the mission system needs to know of the ship each tick. */
struct FMissionShipState
{
	FVector Location = FVector::ZeroVector;
	FString SystemName;
	TArray<FString> Nearby;
	bool bAlongside = false;
	FString StationName;
	bool bAtWarp = false;
	bool bUnderFire = false;
};

/** What an act came to, for the console and the panel. */
USTRUCT(BlueprintType)
struct FMissionOutcome
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) bool bSuccess = false;
	UPROPERTY(BlueprintReadOnly) FString Text;
	UPROPERTY(BlueprintReadOnly) FString CheckSummary;
	UPROPERTY(BlueprintReadOnly) bool bMissionOver = false;
};

struct FFleetRequest;
class UFleetSubsystem;
class UExplorationSubsystem;

UCLASS()
class SLONK_API USlonkMissionSaveGame : public USaveGame
{
	GENERATED_BODY()
public:
	UPROPERTY() TArray<FMission> Missions;
	UPROPERTY() TArray<FStarfleetMessage> Inbox;
	UPROPERTY() int32 Standing = 0;
	UPROPERTY() int32 NextId = 1;
	UPROPERTY() double NextOrdersHours = 0.0;
	UPROPERTY() double NextEventHours = 0.0;
	UPROPERTY() int32 Completed = 0;
	UPROPERTY() int32 Failed = 0;
	/** The day each station last posted its board. */
	UPROPERTY() TMap<FString, int32> BoardDays;
};

/**
 * The missions: generated from Starfleet's orders (over subspace, at intervals), a starbase's board (on arrival),
 * and what happens in space (sporadic, on the clock while under way); tracked through their stages; resolved by
 * the captain's choice and the crew's checks; logged. One per game, saved to the "Missions" slot.
 */
UCLASS()
class SLONK_API UMissionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** Driven by the ship each tick; the clock is the crew roster's. */
	/** Seconds are the world's (scaled); RealSeconds the frame's, for the save delay (Seconds when left out). */
	void AdvanceSeconds(float Seconds, const FMissionShipState& Ship, float RealSeconds = -1.f);
	/** Days a ship at warp 7 needs from where we are to the mission's target (0 for one with no place to go). */
	double TravelDays(const FMission& M) const;
	/** The sector's own happenings (exploration): an event of this kind now, active at once; -1 when it cannot be made. */
	int32 RaiseEvent(EMissionKind Kind);
	/** Regard from outside the missions (a system charted). */
	void AddStanding(int32 Delta, const FString& Why);
	/** A place was named: open missions pointed at it follow. */
	void RenameTarget(const FString& OldName, const FString& NewName);
	double Stardate() const;

	// ---- the captain ----
	bool Accept(int32 Id, FString& OutReply);
	bool Abandon(int32 Id, FString& OutReply);
	/** Take an option of the current stage; the party is the mission's named one or the department's own. */
	bool Act(int32 Id, int32 Option, FMissionOutcome& Out, FString& OutReply);
	bool SetParty(int32 Id, const TArray<int32>& Ids, FString& OutReply);
	/** The ship is at the mission's target now (by the last state's neighbourhood). */
	bool IsAtTarget(const FMission& M) const;
	/** Dev: an order, a posting or an event of this kind, now. */
	/** Returns the new id; -1 when nothing could be made (no target in reach), -2 when the kind brings its own hull and so only happens as an event. */
	int32 DevSpawn(EMissionKind Kind, EMissionSource Source);
	/** The fleet ship's own work ended (done or failed): whatever of ours was on it closes. */
	void FleetWorkOver(int32 MissionId, bool bTheirSuccess);
	/** A fleet ship's request or offer is withdrawn (she managed, she is lost, she was recalled): closed, nothing owed. */
	void FleetWithdraw(int32 MissionId, const FString& Why);

	// ---- queries ----
	const TArray<FMission>& GetMissions() const { return Missions; }
	const TArray<FStarfleetMessage>& GetInbox() const { return Inbox; }
	const FMission* Find(int32 Id) const;
	TArray<const FMission*> Open() const;
	TArray<const FMission*> Active() const;
	int32 GetStanding() const { return Standing; }
	FString StandingTitle() const;
	int32 Unread() const;
	void MarkRead(int32 MessageIndex);
	int32 GetRevision() const { return Revision; }
	int32 GetCompleted() const { return Completed; }
	int32 GetFailed() const { return Failed; }
	/** Lines for the bridge log since the last call. */
	void DrainReports(TArray<FCrewReport>& Out) { Out = MoveTemp(Reports); Reports.Reset(); }
	/** The active mission to steer by, if any (the nearest deadline with a target). */
	const FMission* Objective() const;

	static FString KindName(EMissionKind K);
	static FString SourceName(EMissionSource S);
	static FString StateName(EMissionState S);
	static bool ParseKind(const FString& Word, EMissionKind& Out);

	UFUNCTION(BlueprintCallable, Category = "Missions") void Reset();
	/** Tests: wired to these systems directly, no save slot, a fixed seed. */
	void InitForTest(UCrewRosterSubsystem* InRoster, UFleetSubsystem* InFleet, UExplorationSubsystem* InExploration = nullptr);
	void Save();

private:
	void Load();
	UCrewRosterSubsystem* Roster() const;
	UFleetSubsystem* Fleet() const;
	UExplorationSubsystem* Exploration() const;
	/** What an option carried or failed does beyond the stage (a beacon decoded, a chart filed, a contact's record). */
	void AfterOption(FMission& M, const FMissionOption& O, bool bSuccess, bool bOver, bool bSucceeded);
	/** A fleet ship's request for support, or its work nearby, becomes a mission on the board. */
	void IssueFleetMission(const FFleetRequest& Q);
	void OfferAssists();
	class AStarshipPawn* CrewedShip() const;
	void Report(const FString& Text, const FLinearColor& Color = FLinearColor(0.75f, 0.9f, 1.f), ECrewDepartment From = ECrewDepartment::Command);
	double Hours() const;
	/** Any change: the panels refresh and a save follows in a couple of seconds. */
	void Dirty() { ++Revision; bSaveWanted = true; SaveDelaySeconds = 2.f; }
	// generation
	FMission MakeMission(EMissionKind Kind, EMissionSource Source, FRandomStream& R);
	bool PickTarget(FMission& M, FRandomStream& R);
	void IssueOrders();
	void PostBoard(const FString& StationName);
	void RollEvent();
	void SpawnEventVessel(FMission& M, const FString& Name);
	void Expire();
	FString CivilianName(FRandomStream& R) const;
	void Close(FMission& M, EMissionState State, const FString& Why);
	TArray<int32> PartyFor(const FMission& M, const FMissionOption& O) const;
	void Hurt(const FMission& M, const TArray<int32>& Party, float Risk);

	UPROPERTY() TArray<FMission> Missions;
	UPROPERTY() TArray<FStarfleetMessage> Inbox;
	int32 Standing = 0;
	int32 NextId = 1;
	int32 Completed = 0, Failed = 0;
	double NextOrdersHours = 0.0, NextEventHours = 0.0;
	int32 Revision = 0;
	FMissionShipState Last;
	FString BoardPostedAt;
	TMap<FString, int32> BoardDays;
	UPROPERTY() TArray<FCrewReport> Reports;
	FRandomStream Rng;
	/** The hull each open encounter spawned, by mission id; retired when the mission closes. Not saved. */
	TMap<int32, TWeakObjectPtr<class ASpaceVessel>> EventHulls;
	/** Hulls of closed encounters, oldest first; beyond a few the oldest is removed from the world. */
	TArray<TWeakObjectPtr<class ASpaceVessel>> RetiredHulls;
	void RetireHull(int32 MissionId);
	/** A name no open or closed mission has used. */
	FString UniqueName(FRandomStream& R, TFunction<FString()> Make) const;
	bool bSaveWanted = false;
	bool bNoDisk = false;
	TWeakObjectPtr<UCrewRosterSubsystem> RosterOverride;
	TWeakObjectPtr<UFleetSubsystem> FleetOverride;
	TWeakObjectPtr<UExplorationSubsystem> ExplorationOverride;
	float SaveDelaySeconds = 0.f;
	/** The clock's last reading, to notice it going back (a new complement starts it over). */
	double LastClockHours = -1.0;
	static const TCHAR* SlotName;
};
