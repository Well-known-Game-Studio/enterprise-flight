// Slonk - the frontier: what the Federation founds in charted space (outposts, colonies, starbases) and keeps
// there. Design in docs/frontier-design.md. Saved to disk.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Missions.h"
#include "Frontier.generated.h"

class UExplorationSubsystem;
struct FCatalogueEntry;

UENUM(BlueprintType)
enum class ESettlementKind : uint8 { Outpost = 0, Colony, Starbase, Count UMETA(Hidden) };

/** One thing the Federation has founded in a system of the catalogue. */
USTRUCT(BlueprintType)
struct FSettlement
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) ESettlementKind Kind = ESettlementKind::Outpost;
	/** The system by the name in use (renamed with it). */
	UPROPERTY(BlueprintReadOnly) FString System;
	/** The world it is at or on (the station orbits it; the colony lives on it). Empty for a starbase off the star. */
	UPROPERTY(BlueprintReadOnly) FString World;
	/** The station's name, or the colony's. */
	UPROPERTY(BlueprintReadOnly) FString Name;
	UPROPERTY(BlueprintReadOnly) double FoundedStardate = 0.0;
	/** People, for a colony. */
	UPROPERTY(BlueprintReadOnly) double Population = 0.0;
	/** A starbase's works: hours of ship's time until it can be commissioned; 0 when built. */
	UPROPERTY(BlueprintReadOnly) double WorksHoursLeft = 0.0;
	UPROPERTY(BlueprintReadOnly) bool bCommissioned = true;
	/** A colony's lights after a strike: a quarter (half if it was warned) until it has grown back; 1 unmarked. */
	UPROPERTY() float Scar = 1.f;
	/** The people the colony had before the strike: the scar fades when it has grown back to them. */
	UPROPERTY() double RecoverPeople = 0.0;
	UPROPERTY() int32 Seed = 0;
	bool IsStation() const { return Kind != ESettlementKind::Colony; }
};

UCLASS()
class SLONK_API USlonkFrontierSaveGame : public USaveGame
{
	GENERATED_BODY()
public:
	UPROPERTY() TArray<FSettlement> Settlements;
	UPROPERTY() int32 NextStarbaseNumber = 400;
	/** Worlds not ours that were struck: the fraction of their lights left. */
	UPROPERTY() TMap<FString, float> WorldScars;
};

/**
 * The settlements: founded by the expansion missions, never on their own; replayed into the world at the start of a
 * session (the station spawned off its world, the colony world lit); growing a little every ship's day. The rules
 * of who may found what are here too (CanFound), so an order is only made where it can be carried.
 */
UCLASS()
class SLONK_API UFrontierSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	/** The session's end: growth and works not yet written are saved. */
	virtual void Deinitialize() override;
	static UFrontierSubsystem* Get(const UObject* WorldContext);

	/** The settlements into the world (the game mode, after the catalogue's stars are in). */
	void SpawnSettlements(UWorld* World);
	/** From the ship's tick: growth and the works, by the shared clock's hours. */
	void AdvanceSeconds(float RealSeconds, const FMissionShipState& Ship);

	/** Whether this kind may be founded in the system now, and the world it would take (empty for a starbase). */
	bool CanFound(ESettlementKind Kind, const FString& SystemName, FString& OutWorld, FString& OutWhy, bool bOrdered = false) const;
	/** A system where the kind may be founded: one of the nearest few by the last ship state, or null. */
	const FCatalogueEntry* PickSystem(ESettlementKind Kind, FRandomStream& R, FString& OutWorld, const TSet<FString>* ExcludeSystems = nullptr) const;
	/** Found it: the station spawned or the world lit, the record kept, the bridge told. Null when it may not be. */
	const FSettlement* Found(ESettlementKind Kind, const FString& SystemName, const FString& World, double People = 0.0, bool bOrdered = false);
	/** A starbase's works are done: commissioned, its patrol ships out. False with hours still to go. */
	bool Commission(const FString& SystemName, FString& OutWhy);
	/** A world was struck: the colony loses most of its people (all, if small); the evacuation halves the dead. */
	void Strike(const FString& World, bool bEvacuated, FString& OutText);
	/** A world not ours was struck: its lights dimmed by Factor now and at every session's start (struck again, dimmer). */
	void ScarWorld(const FString& World, float Factor);
	/** A system was renamed: its settlements and their worlds follow. */
	void RenameSystem(const FString& OldName, const FString& NewName);

	const TArray<FSettlement>& GetSettlements() const { return Settlements; }
	const FSettlement* Find(const FString& SystemName, ESettlementKind Kind) const;
	const FSettlement* AtWorld(const FString& World) const;
	TArray<const FSettlement*> InSystem(const FString& SystemName) const;
	/** The colony worlds: inhabited by us, and so worth defending. */
	TArray<FString> ColonyWorlds() const;
	double WorksHoursLeft(const FString& SystemName) const;
	/** The fraction of its lights a struck world of others has left (1 unmarked). */
	float WorldScar(const FString& World) const { const float* S = WorldScars.Find(World); return S ? *S : 1.f; }

	void DrainReports(TArray<FCrewReport>& Out) { Out = MoveTemp(Reports); Reports.Reset(); }
	int32 GetRevision() const { return Revision; }
	static FString KindName(ESettlementKind K);
	static bool ParseKind(const FString& Word, ESettlementKind& Out);
	/** A colony's name from the generator. */
	static FString ColonyName(FRandomStream& R);
	/** How bright a colony's lights are for its people. */
	static float LightsFor(double Population);
	static constexpr double StarbaseWorksHours = 72.0;
	static constexpr int32 StarbaseStandingNeeded = 60;
	static constexpr double ColonyFirstPeople = 800.0;

	UFUNCTION(BlueprintCallable, Category = "Frontier") void Reset();
	/** Tests: no save slot, no world. */
	void InitForTest(UMissionSubsystem* InMissions, UExplorationSubsystem* InExploration);

private:
	void Load();
	void Save();
	FSettlement* FindMutable(const FString& SystemName, ESettlementKind Kind);
	UMissionSubsystem* Missions() const;
	UExplorationSubsystem* Exploration() const;
	void Report(const FString& Text, const FLinearColor& Color = FLinearColor(0.75f, 0.9f, 1.f), ECrewDepartment From = ECrewDepartment::Operations);
	/** The station into the world (the kind's style, off its world or the star), its patrol ships if commissioned. */
	void SpawnStation(UWorld* World, const FSettlement& S);
	/** The colony world's lights for its people (and its scar), remembered so growth refreshes them when they change. */
	void LightWorld(UWorld* World, const FSettlement& S);
	TMap<FString, float> LightsApplied;
	UPROPERTY() TMap<FString, float> WorldScars;
	bool bUnsaved = false;

	UPROPERTY() TArray<FSettlement> Settlements;
	int32 NextStarbaseNumber = 400;
	UPROPERTY() TArray<FCrewReport> Reports;
	FMissionShipState Last;
	double LastHours = -1.0;
	/** Hours of ship's time between saves of the growth (the founding saves at once). */
	double SaveTimer = 6.0;
	int32 Revision = 0;
	bool bNoDisk = false;
	TWeakObjectPtr<UMissionSubsystem> MissionsOverride;
	TWeakObjectPtr<UExplorationSubsystem> ExplorationOverride;
	static const TCHAR* SlotName;
};
