// Slonk - the crew roster: every person aboard as a record with a history, needs, bonds and growth, the ship's
// clock they live by, and the captain's hand on them. Design in docs/crew-design.md. Saved to disk.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ShipSystemsComponent.h"
#include "CrewRoster.generated.h"

UENUM(BlueprintType)
enum class ECrewDepartment : uint8 { Command = 0, Helm, Operations, Engineering, Science, Medical, Tactical, Security, Services, Count UMETA(Hidden) };

UENUM(BlueprintType)
enum class ECrewRank : uint8 { Crewman = 0, AbleCrewman, PettyOfficer, Chief, Ensign, LieutenantJG, Lieutenant, LtCommander, Commander, Captain, Count UMETA(Hidden) };

UENUM(BlueprintType)
enum class ECrewSkill : uint8 { Command = 0, Helm, Operations, Engineering, Science, Medicine, Tactical, Security, Diplomacy, Trade, Count UMETA(Hidden) };

UENUM(BlueprintType)
enum class ECrewFacet : uint8 { Resolve = 0, Empathy, Curiosity, Discipline, Courage, Sociability, Ambition, Count UMETA(Hidden) };

UENUM(BlueprintType)
enum class ECrewNeed : uint8 { Rest = 0, Health, Safety, Belonging, Purpose, Recreation, Recognition, Count UMETA(Hidden) };

UENUM(BlueprintType)
enum class ECrewWatch : uint8 { Alpha = 0, Beta, Gamma, Count UMETA(Hidden) };

UENUM(BlueprintType)
enum class ECrewCondition : uint8 { Fit = 0, Injured, Critical, Dead, OnLeave, Away, Transferred, Count UMETA(Hidden) };

UENUM(BlueprintType)
enum class ECrewActivity : uint8 { OnWatch = 0, Sleeping, Mess, Recreation, Sickbay, Leave, Away, Count UMETA(Hidden) };

UENUM(BlueprintType)
enum class ECrewRelation : uint8 { Acquaintance = 0, Friend, CloseFriend, Rival, Mentor, Protege, Partner, Family, Count UMETA(Hidden) };

UENUM(BlueprintType)
enum class ECrewEventKind : uint8 { Joined = 0, Promoted, Demoted, Commended, Reprimanded, Wounded, Recovered, Died, Bereaved, Moment, Mission, Discovery, Leave, Letter, Transfer, Memorial, Check, Count UMETA(Hidden) };

UENUM(BlueprintType)
enum class ECrewTiePlace : uint8 { World = 0, Ship, Station, Count UMETA(Hidden) };

/** Someone elsewhere: family or a friend on a world, aboard another ship or at a station. Letters come from them,
 *  passing their place is a reunion, and the missions to come will put them in harm's way. */
USTRUCT(BlueprintType)
struct FCrewTie
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FString Name;
	/** "sister", "father", "academy roommate", "former captain"... */
	UPROPERTY(BlueprintReadOnly) FString Relation;
	UPROPERTY(BlueprintReadOnly) ECrewTiePlace Kind = ECrewTiePlace::World;
	/** The world, ship or station; empty until bound to the galaxy's own names. */
	UPROPERTY(BlueprintReadOnly) FString Place;
	UPROPERTY(BlueprintReadOnly) bool bFamily = false;
	UPROPERTY(BlueprintReadOnly) float Affinity = 0.5f;
	UPROPERTY(BlueprintReadOnly) int32 LastContactDay = -100;
	FString Where() const;
};

/** A disposition toward a people or a power: -1 hatred .. 1 affection, moved by what they live through. */
USTRUCT(BlueprintType)
struct FCrewBias
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FName Group;
	UPROPERTY(BlueprintReadOnly) float Disposition = 0.f;
	/** Why, in a few words, for the file. */
	UPROPERTY(BlueprintReadOnly) FString Reason;
};

/** A suspicion of someone aboard, with its reason; rumour carries it. */
USTRUCT(BlueprintType)
struct FCrewSuspicion
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) int32 Of = -1;
	UPROPERTY(BlueprintReadOnly) float Weight = 0.f;
	UPROPERTY(BlueprintReadOnly) FString Why;
	UPROPERTY(BlueprintReadOnly) int32 SinceDay = 0;
};

/** Something that happened, for everyone it reaches to live in their own terms: a mission's turn, an encounter,
 *  the captain's choice. Tags (dotted): Combat.EnemyDestroyed, Combat.ShipSpared, Combat.FriendlyLost,
 *  Contact.First, Contact.Friendly, Contact.Hostile, Rescue, Loss.Civilians, Parley.Held, Parley.Failed,
 *  Away.Danger, Away.Routine, Order.Refused, Promise.Kept, Promise.Broken, Captain.Fair, Captain.Unfair,
 *  Discovery, Shore. */
USTRUCT(BlueprintType)
struct FCrewExperience
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadWrite) FName Tag;
	/** 0..1: how big it was. */
	UPROPERTY(BlueprintReadWrite) float Intensity = 0.5f;
	/** Those who were there; empty means the whole ship hears of it (at less than being there). */
	UPROPERTY(BlueprintReadWrite) TArray<int32> Involved;
	/** The people or power it concerned (Klingon, Borg, Vulcan, Federation...), if any. */
	UPROPERTY(BlueprintReadWrite) FName Group;
	/** Where, by the galaxy's name, if anywhere: those with people there live it twice. */
	UPROPERTY(BlueprintReadWrite) FString Place;
	/** The plain description for the histories ("the Klingon cruiser destroyed off Cestus III"). */
	UPROPERTY(BlueprintReadWrite) FString Text;
	/** The captain decided it: trust moves with how each person saw the choice. */
	UPROPERTY(BlueprintReadWrite) bool bCaptainsChoice = false;
};

/** Words across a grudge not yet answered by the captain. */
USTRUCT(BlueprintType)
struct FCrewIncident
{
	GENERATED_BODY()
	UPROPERTY() int32 Offender = -1;
	UPROPERTY() int32 Victim = -1;
	UPROPERTY() int32 Day = 0;
};

/** A bond with another person. */
USTRUCT(BlueprintType)
struct FCrewRelationship
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) int32 Other = -1;
	/** -1 enemy .. 1 devoted. */
	UPROPERTY(BlueprintReadOnly) float Affinity = 0.f;
	UPROPERTY(BlueprintReadOnly) ECrewRelation Kind = ECrewRelation::Acquaintance;
};

/** A desire, or (bFear) a fear: a tag the simulation reads and a line for the file. */
USTRUCT(BlueprintType)
struct FCrewWant
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FName Tag;
	UPROPERTY(BlueprintReadOnly) FString Text;
	UPROPERTY(BlueprintReadOnly) bool bFear = false;
	/** Day it was taken up; a desire ignored for long is a pressure. */
	UPROPERTY(BlueprintReadOnly) int32 SinceDay = 0;
	UPROPERTY(BlueprintReadOnly) bool bGranted = false;
	/** The captain promised it, on this day. */
	UPROPERTY(BlueprintReadOnly) bool bPromised = false;
	UPROPERTY(BlueprintReadOnly) int32 PromisedDay = 0;
};

/** A moment that still weighs on the mood, decaying over days unless permanent. */
USTRUCT(BlueprintType)
struct FCrewMemory
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FString Text;
	UPROPERTY(BlueprintReadOnly) float Weight = 0.f;
	UPROPERTY(BlueprintReadOnly) double Stardate = 0.0;
	UPROPERTY(BlueprintReadOnly) bool bPermanent = false;
};

/** A line of the personal log. */
USTRUCT(BlueprintType)
struct FCrewEvent
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) double Stardate = 0.0;
	UPROPERTY(BlueprintReadOnly) ECrewEventKind Kind = ECrewEventKind::Moment;
	UPROPERTY(BlueprintReadOnly) FString Text;
	UPROPERTY(BlueprintReadOnly) TArray<int32> Others;
	UPROPERTY(BlueprintReadOnly) bool bMilestone = false;
};

USTRUCT(BlueprintType)
struct FCrewService
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) double JoinedStardate = 0.0;
	UPROPERTY(BlueprintReadOnly) int32 Missions = 0;
	UPROPERTY(BlueprintReadOnly) int32 Commendations = 0;
	UPROPERTY(BlueprintReadOnly) int32 Reprimands = 0;
	UPROPERTY(BlueprintReadOnly) int32 Wounds = 0;
	UPROPERTY(BlueprintReadOnly) int32 Promotions = 0;
	UPROPERTY(BlueprintReadOnly) int32 Engagements = 0;
};

/** One person aboard. */
USTRUCT(BlueprintType)
struct FCrewMember
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) int32 Id = -1;
	UPROPERTY(BlueprintReadOnly) FString FirstName;
	UPROPERTY(BlueprintReadOnly) FString LastName;
	UPROPERTY(BlueprintReadOnly) FString Species;
	/** "she", "he" or "they". */
	UPROPERTY(BlueprintReadOnly) FString Pronoun;
	UPROPERTY(BlueprintReadOnly) int32 Age = 30;
	UPROPERTY(BlueprintReadOnly) FString Home;
	UPROPERTY(BlueprintReadOnly) FString Backstory;
	UPROPERTY(BlueprintReadOnly) ECrewRank Rank = ECrewRank::Crewman;
	UPROPERTY(BlueprintReadOnly) ECrewDepartment Department = ECrewDepartment::Services;
	/** The compartment of the posting (a name from the ship's compartment map). */
	UPROPERTY(BlueprintReadOnly) FString Posting;
	UPROPERTY(BlueprintReadOnly) ECrewWatch Watch = ECrewWatch::Alpha;
	/** Head of the department: the officer who speaks for it on the bridge log. */
	UPROPERTY(BlueprintReadOnly) bool bHead = false;
	/** The department's second, who takes the station when the head is down. */
	UPROPERTY(BlueprintReadOnly) bool bSecond = false;
	/** Indexed by ECrewSkill, 0..100, and the experience toward the next point. */
	UPROPERTY(BlueprintReadOnly) TArray<float> Skills;
	UPROPERTY(BlueprintReadOnly) TArray<float> SkillXP;
	/** Indexed by ECrewFacet, 0..100. */
	UPROPERTY(BlueprintReadOnly) TArray<float> Facets;
	UPROPERTY(BlueprintReadOnly) TArray<FName> Traits;
	/** Indexed by ECrewNeed, 0..1. */
	UPROPERTY(BlueprintReadOnly) TArray<float> Needs;
	UPROPERTY(BlueprintReadOnly) float Mood = 0.6f;
	UPROPERTY(BlueprintReadOnly) float Stress = 0.f;
	UPROPERTY(BlueprintReadOnly) TArray<FCrewWant> Wants;
	UPROPERTY(BlueprintReadOnly) TArray<FCrewRelationship> Relationships;
	/** Family and friends elsewhere. */
	UPROPERTY(BlueprintReadOnly) TArray<FCrewTie> Ties;
	/** Opinion of the captain: -1 would see them relieved .. 1 would follow them anywhere. */
	UPROPERTY(BlueprintReadOnly) float Trust = 0.2f;
	UPROPERTY(BlueprintReadOnly) TArray<FCrewBias> Biases;
	UPROPERTY(BlueprintReadOnly) TArray<FCrewSuspicion> Suspicions;
	/** Trust and biases have been given to this record (older saves get theirs on load). */
	UPROPERTY(BlueprintReadOnly) bool bOpinionsSeeded = false;
	UPROPERTY(BlueprintReadOnly) ECrewCondition Condition = ECrewCondition::Fit;
	/** Ship's hours until a wound heals or a leave ends. */
	UPROPERTY(BlueprintReadOnly) float ConditionHours = 0.f;
	/** The station they were left at on leave; they come back aboard only alongside it. */
	UPROPERTY(BlueprintReadOnly) FString LeaveStation;
	UPROPERTY(BlueprintReadOnly) TArray<FCrewMemory> Memories;
	UPROPERTY(BlueprintReadOnly) TArray<FCrewEvent> History;
	UPROPERTY(BlueprintReadOnly) FCrewService Service;
	/** Where they are this hour and what they are doing (the placement table the rooms will read). */
	UPROPERTY(BlueprintReadOnly) FString Where;
	UPROPERTY(BlueprintReadOnly) ECrewActivity Activity = ECrewActivity::OnWatch;
	/** Day of the year they were born on, for the birthday in the mess. */
	UPROPERTY(BlueprintReadOnly) int32 Birthday = 0;

	FString FullName() const { return LastName.IsEmpty() ? FirstName : FirstName + TEXT(" ") + LastName; }
	/** "Lt. Cmdr. Rhys Dahl", or "Dr. Elena Vasquez" for the medical head. */
	FString Styled() const;
	bool IsAboard() const { return Condition != ECrewCondition::Dead && Condition != ECrewCondition::Transferred && Condition != ECrewCondition::Away && Condition != ECrewCondition::OnLeave; }
	bool IsFit() const { return Condition == ECrewCondition::Fit; }
	float Skill(ECrewSkill S) const { return Skills.IsValidIndex((int32)S) ? Skills[(int32)S] : 0.f; }
	float Facet(ECrewFacet F) const { return Facets.IsValidIndex((int32)F) ? Facets[(int32)F] : 50.f; }
	float Need(ECrewNeed N) const { return Needs.IsValidIndex((int32)N) ? Needs[(int32)N] : 0.5f; }
	/** Disposition toward a people or power (0 when none is held). */
	float Bias(FName Group) const;
	bool HasTrait(FName T) const { return Traits.Contains(T); }
	const FCrewRelationship* RelationWith(int32 Other) const;
};

/** What a check against the crew came to. */
USTRUCT(BlueprintType)
struct FCrewCheck
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) bool bSuccess = false;
	/** Effective skill minus difficulty: how comfortably, or by how much it was missed. */
	UPROPERTY(BlueprintReadOnly) float Margin = 0.f;
	UPROPERTY(BlueprintReadOnly) int32 BestId = -1;
	UPROPERTY(BlueprintReadOnly) int32 WorstId = -1;
	UPROPERTY(BlueprintReadOnly) FString Summary;
};

/** A line the roster wants read on the bridge log, by the department that says it. */
USTRUCT()
struct FCrewReport
{
	GENERATED_BODY()
	UPROPERTY() ECrewDepartment From = ECrewDepartment::Command;
	UPROPERTY() FString Text;
	UPROPERTY() FLinearColor Color = FLinearColor(0.75f, 0.9f, 1.f);
};

/** What the roster needs to know of the ship each tick. */
struct FCrewShipState
{
	EAlertState Alert = EAlertState::Green;
	bool bUnderFire = false;
	bool bAlongside = false;
	FString StationName;
	float MedicalSupplies = 1.f;
	int32 SickbayBeds = 24;
	/** Named things within hailing distance: the station alongside, vessels in range, the worlds close by. */
	TArray<FString> Nearby;
};

UCLASS()
class SLONK_API USlonkCrewSaveGame : public USaveGame
{
	GENERATED_BODY()
public:
	UPROPERTY() TArray<FCrewMember> Roster;
	UPROPERTY() TArray<FCrewMember> Lost;
	UPROPERTY() double Hours = 0.0;
	UPROPERTY() int32 Seed = 0;
	UPROPERTY() int32 NextId = 0;
	UPROPERTY() FString ShipName;
	UPROPERTY() int32 MourningDays = 0;
	UPROPERTY() double HourAccumulator = 0.0;
	UPROPERTY() TMap<int32, int32> PeerHours;
	UPROPERTY() TArray<int32> Targets;
	UPROPERTY() TArray<FCrewIncident> Incidents;
};

/**
 * The roster of everyone aboard, the ship's clock and the simulation of their days. One per game, saved to the
 * "Crew" slot after every ship's day and after anything that changes a record; the ship's systems component
 * feeds it the ship's state each tick and reads its heads of department for the bridge log.
 */
UCLASS()
class SLONK_API UCrewRosterSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	// ---- the clock ----
	/** A ship's hour passes in this many real seconds (60: a day aboard is 24 minutes of play). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crew") float SecondsPerShipHour = 60.f;
	UFUNCTION(BlueprintPure, Category = "Crew") double GetStardate() const { return StardateBase + Hours / 24.0; }
	UFUNCTION(BlueprintPure, Category = "Crew") int32 GetDay() const { return (int32)(Hours / 24.0); }
	UFUNCTION(BlueprintPure, Category = "Crew") int32 GetHourOfDay() const { return (int32)Hours % 24; }
	UFUNCTION(BlueprintPure, Category = "Crew") FString StardateText() const;
	/** The watch on duty now. */
	UFUNCTION(BlueprintPure, Category = "Crew") ECrewWatch WatchOnDuty() const { return (ECrewWatch)((GetHourOfDay() / 8) % 3); }
	double GetHours() const { return Hours; }

	// ---- driven by the ship ----
	/** Real seconds pass: the clock advances, and every full ship's hour the crew live an hour. */
	void AdvanceSeconds(float Seconds, const FCrewShipState& Ship);
	void NoteAlert(EAlertState NewAlert);
	/** A hit landed in this compartment: the next casualties come from the people in it. */
	void NoteHitCompartment(const FString& Compartment);
	/** The ship is losing air or the like: no compartment, and the dead are sickbay's patients first. */
	void NoteMedicalHazard();
	/** The medical model's counts changed: wound, kill and heal people by name to match (the dead and the newly
	 *  wounded from the compartment hit while the hit is fresh, else from sickbay's patients for the dead). Returns
	 *  the lines to report. */
	void SyncCasualties(float Injured, float Critical, float Dead, TArray<FString>& OutLines);
	/** Those the medical model should count: aboard and not ashore or away. */
	int32 CountMembers() const;
	/** A department head is hurt (lost: killed, and the second steps up) or back on duty. */
	void NoteHeadHurt(ECrewDepartment Dept, bool bLost, float Seconds);
	/** The ship's recovery timer has run: the head is fit again (true if they were under treatment). */
	bool HealHead(ECrewDepartment Dept);
	/** One person wounded or killed by name, away from the ship (a mission's party). The ship's counts must move
	 *  with it (UShipSystemsComponent::HurtCrewMember) or the next reconciliation undoes it. */
	bool WoundMember(int32 Id, bool bCritical, bool bKilled, const FString& Where);
	/** Engineers repaired something; the science watch finished a scan; something worth seeing passed. */
	void NoteRepair(const FString& SystemName, bool bUnderFire);
	void NoteScan(const FString& Label, int32 Points, bool bDiscovery);
	void NoteSight(FName Tag, const FString& Text);
	/** Alongside a station: recruits are offered, letters come, leave can be granted. Resupply done: the dead replaced. */
	void NoteStarbase(const FString& StationName);
	void NoteResupplyDone(const FString& StationName, TArray<FString>& OutLines);
	/** The galaxy's own names for the ties generated as placeholders: worlds, stations and ships. */
	void BindPlaces(const TArray<FString>& Worlds, const TArray<FString>& Stations, const TArray<FString>& Ships);
	bool HasUnboundTies() const;
	/** Something happened: everyone it reaches lives it in their own terms (see FCrewExperience). */
	void Experience(const FCrewExperience& E);
	/** A disposition moved by experience: toward a group, by this much, for this reason. */
	void MoveBias(FCrewMember& M, FName Group, float Delta, const FString& Reason);
	/** How the ship holds together: trust, bonds and the biases that divide it, 0..1. */
	UFUNCTION(BlueprintPure, Category = "Crew") float Cohesion() const;
	UFUNCTION(BlueprintPure, Category = "Crew") float MeanTrust() const;
	/** Lines for the bridge log since the last call. */
	void DrainReports(TArray<FCrewReport>& Out) { Out = MoveTemp(Reports); Reports.Reset(); }

	// ---- the captain's hand ----
	bool Promote(int32 Id, FString& OutReply);
	bool Demote(int32 Id, FString& OutReply);
	bool Commend(int32 Id, const FString& For, FString& OutReply);
	bool Reprimand(int32 Id, const FString& For, FString& OutReply);
	bool SetWatch(int32 Id, ECrewWatch Watch, FString& OutReply);
	bool SetPosting(int32 Id, ECrewDepartment Dept, const FString& Compartment, FString& OutReply);
	bool GrantLeave(int32 Id, int32 Days, FString& OutReply);
	/** A word with them: "listen", "encourage", "order" or "promise". */
	bool Talk(int32 Id, const FString& How, FString& OutReply);
	bool SignOn(int32 RecruitIndex, FString& OutReply);
	// ---- the fleet's people ----
	/** A person for another ship, from the same pools as ours: a given name and species when a tie names them. */
	FCrewMember MakeOutsider(FRandomStream& R, ECrewDepartment Dept, ECrewRank Rank, int32 Id, const FString& FullName = FString(), const FString& Species = FString());
	/** Someone joins from another ship (a transfer): a new id, a first history line, a place aboard. */
	bool Join(FCrewMember M, const FString& Why, FString& OutReply);
	/** One of ours goes to another ship: the record stays as Transferred, their close friends keep a tie to them there. */
	bool TransferOut(int32 Id, const FString& OtherShip, FCrewMember& OutPerson, FString& OutReply);
	/** News of a ship, for everyone with a tie aboard her. */
	void NoteShipNews(const FString& OtherShip, const FString& Text, float Weight);
	/** Someone a crew member is tied to was killed aboard another ship. */
	void TieLost(int32 CrewId, const FString& Name, const FString& OtherShip);
	/** Someone a crew member is tied to has just joined us: the tie becomes a bond aboard. */
	void TieComesAboard(int32 CrewId, const FString& Name, const FString& Relation);
	/** The person a tie named is no longer where the tie says (they came to us): the tie goes, no bond. */
	void TieRemoved(int32 CrewId, const FString& Name);
	bool Discharge(int32 Id, FString& OutReply);
	/** A day of mourning: grief settles faster, the lost are named. */
	FString Memorial();
	/** A check against some of the crew (an empty list: the department's head and second). A group named makes
	 *  the party's dispositions toward it count: a prejudiced negotiator does worse, a sympathetic one better. */
	FCrewCheck Check(ECrewSkill Skill, float Difficulty, const TArray<int32>& Ids, float CaptainBonus = 0.f, FName Group = NAME_None);

	// ---- queries ----
	const TArray<FCrewMember>& GetRoster() const { return Roster; }
	const TArray<FCrewMember>& GetLost() const { return Lost; }
	const TArray<FCrewMember>& GetRecruits() const { return Recruits; }
	FString GetRecruitStation() const { return RecruitStation; }
	const FCrewMember* Find(int32 Id) const;
	FCrewMember* FindMutable(int32 Id);
	const FCrewMember* Head(ECrewDepartment Dept) const;
	const FCrewMember* Second(ECrewDepartment Dept) const;
	/** Find by (part of) a name, the first match. */
	const FCrewMember* FindByName(const FString& Name) const;
	int32 CountAboard() const;
	int32 TargetFor(ECrewDepartment D) const;
	int32 CountCondition(ECrewCondition C) const;
	float Morale() const;
	/** Those with a need below a quarter, high stress or a wound, worst first. */
	TArray<int32> InTrouble() const;
	int32 GetRevision() const { return Revision; }
	FString GetShipName() const { return ShipName; }
	/** The ship registers at its start: its name, and the medical model's counts start from nothing. */
	void SetShipName(const FString& Name) { ShipName = Name; DeadBase = Lost.Num() - CountCondition(ECrewCondition::Dead); }

	// ---- names ----
	static FString DepartmentName(ECrewDepartment D);
	static FString RankName(ECrewRank R);
	static FString RankShort(ECrewRank R);
	static FString SkillName(ECrewSkill S);
	static FString FacetName(ECrewFacet F);
	static FString NeedName(ECrewNeed N);
	static FString WatchName(ECrewWatch W);
	static FString ConditionName(ECrewCondition C);
	static FString ActivityName(ECrewActivity A);
	static FString RelationName(ECrewRelation R);
	static FString TraitText(FName Trait);
	static bool ParseDepartment(const FString& Word, ECrewDepartment& Out);
	static bool ParseSkill(const FString& Word, ECrewSkill& Out);
	static ECrewSkill SkillOf(ECrewDepartment D);
	static ECrewDepartment DepartmentOf(EStation Station);
	static EStation StationOf(ECrewDepartment D);

	// ---- the whole ----
	/** A fresh complement from a seed (0: from the clock). */
	UFUNCTION(BlueprintCallable, Category = "Crew") void NewComplement(int32 Seed, int32 Complement = 502);
	/** Tests: a complement from a seed, no save slot read or written. */
	void InitForTest(int32 TestSeed);
	UFUNCTION(BlueprintCallable, Category = "Crew") void Reset();
	/** Dev: words across a grudge between these two, for the captain to answer. */
	bool DevIncident(int32 Offender, int32 Victim);
	/** Dev: live this many ship's hours at once. */
	void DevAdvanceHours(int32 HoursToLive, const FCrewShipState& Ship);
	void Save();
	/** Mark the roster changed; it is written a few seconds later, or at the day's end. */
	void RequestSave();

private:
	void Load();
	bool bSaveWanted = false;
	bool bNoDisk = false;
	float SaveDelaySeconds = 0.f;
	/** Per-department strengths this complement was generated with (recruits and replacements fill to them). */
	TArray<int32> Targets;
	/** Older saves: arrays to their lengths, values to their ranges, heads and seconds and ids made good. */
	void Validate();
	/** The next best in the department becomes its second if it has none. */
	void EnsureSecond(ECrewDepartment Dept);
	void LiveHour(const FCrewShipState& Ship);
	void LiveDay(const FCrewShipState& Ship);
	void Place(FCrewMember& M);
	void UpdateMood(FCrewMember& M);
	void Grow(FCrewMember& M, ECrewSkill S, float XP);
	void PersonalMoment(FCrewMember& M, const FCrewShipState& Ship);
	/** The hour's learning from the colleague a little ahead on the same watch and posting. */
	void LearnFromPeers();
	TMap<int32, int32> PeerHours;
	TSet<FString> UsedNames;
	void Remember(FCrewMember& M, const FString& Text, float Weight, bool bPermanent = false);
	void Record(FCrewMember& M, ECrewEventKind Kind, const FString& Text, bool bMilestone = false, const TArray<int32>& Others = {});
	void Bond(FCrewMember& A, FCrewMember& B, float Delta, ECrewRelation KindIfNew);
	void Grieve(const FCrewMember& Dead);
	void Wound(FCrewMember& M, bool bCritical, const FString& Where);
	void Kill(FCrewMember& M, const FString& Where);
	void Heal(FCrewMember& M);
	void PromoteToHead(ECrewDepartment Dept, const FString& Why);
	bool Report(ECrewDepartment From, const FString& Text, const FLinearColor& Color = FLinearColor(0.75f, 0.9f, 1.f), bool bAlways = false);
	int32 RequestsToday = 0;
	struct FCrewNamed { ECrewDepartment Dept; const TCHAR* First; const TCHAR* Last; const TCHAR* Species; const TCHAR* Pronoun; ECrewRank Rank; };
	FCrewMember MakePerson(FRandomStream& R, ECrewDepartment Dept, ECrewRank Rank, int32 Index, const FCrewNamed* Named = nullptr);
	void MakeNamed(FRandomStream& R, FCrewMember& M, const TCHAR* First, const TCHAR* Last, const TCHAR* Species, const TCHAR* Pronoun);
	void MakeRecruits(const FString& StationName);
	void MakeTies(FRandomStream& R, FCrewMember& M);
	void SeedOpinions(FRandomStream& R, FCrewMember& M);
	void Incident(FCrewMember& M, FCrewMember& O, bool bBias);
	int32 IncidentsToday = 0;
	void Reunion(FCrewMember& M, FCrewTie& T, bool bAshore);
	FString PostingFor(FRandomStream& R, ECrewDepartment Dept, int32 Index) const;
	void Dirty() { ++Revision; }
	TArray<int32> PresentIn(const FString& Compartment, bool bFitOnly) const;
	/** Fit people in the compartment hit (while the hit is fresh) and, after them, anyone fit aboard. */
	void Candidates(const FString& Compartment, TArray<int32>& OutLocal, TArray<int32>& OutElsewhere) const;
	void SetRelation(FCrewMember& From, int32 To, ECrewRelation Kind, float Affinity);
	bool bAlongside = false;
	FString AlongsideStation;

	UPROPERTY() TArray<FCrewMember> Roster;
	UPROPERTY() TArray<FCrewMember> Lost;
	UPROPERTY() TArray<FCrewMember> Recruits;
	UPROPERTY() TArray<FCrewReport> Reports;
	FString RecruitStation;
	int32 RecruitDay = -1;
	double Hours = 0.0;
	double HourAccumulator = 0.0;
	double StardateBase = 8305.0;
	int32 Seed = 0;
	int32 NextId = 0;
	int32 Revision = 0;
	FString ShipName;
	FString LastHitCompartment;
	double LastHitHours = -1000.0;
	TArray<TPair<FString, double>> RecentHitCompartments;
	EAlertState Alert = EAlertState::Green;
	double AlertSinceHours = 0.0;
	int32 LastSyncInjured = 0, LastSyncCritical = 0, LastSyncDead = 0;
	float ReportCooldownSeconds = 0.f;
	int32 MourningDays = 0;
	UPROPERTY() TArray<FCrewIncident> Incidents;
	int32 RiftsReported = -100;
	/** The lost before the medical model's current count began (a new session, a resupply). */
	int32 DeadBase = 0;
	FRandomStream Rng;
	static const TCHAR* SlotName;
};
