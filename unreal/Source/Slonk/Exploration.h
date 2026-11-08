// Slonk - exploration: the sector catalogue of uncharted stars, charted by arrival, and the people met there.
// Design in docs/exploration-design.md. Saved to disk.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "StarSystem.h"
#include "Missions.h"
#include "Exploration.generated.h"

UENUM(BlueprintType)
enum class EChartState : uint8 { Unknown = 0, Charted, Surveyed, Count UMETA(Hidden) };

/** One star of the catalogue: a designation and a bearing until the ship goes there. */
USTRUCT(BlueprintType)
struct FCatalogueEntry
{
	GENERATED_BODY()
	/** "UFC 2461": the United Federation Catalogue number, kept after naming. */
	UPROPERTY(BlueprintReadOnly) FString Designation;
	/** The spec the system is generated from; its Name is the designation until the system is named. */
	UPROPERTY(BlueprintReadOnly) FStarSystemSpec Spec;
	/** The catalogue's own name for it, used when the chart is filed unnamed. */
	UPROPERTY(BlueprintReadOnly) FString ProperName;
	/** The name it was given (the captain's, or the catalogue's when filed); empty while unknown. */
	UPROPERTY(BlueprintReadOnly) FString GivenName;
	UPROPERTY(BlueprintReadOnly) EChartState State = EChartState::Unknown;
	UPROPERTY(BlueprintReadOnly) double ChartedStardate = 0.0;
	/** Worlds the sensors counted when it was revealed. */
	UPROPERTY(BlueprintReadOnly) int32 Worlds = 0;
	/** A world with city lights: a civilisation that has not left it. */
	UPROPERTY(BlueprintReadOnly) FString PreWarpWorld;
	/** The people met there, if any. */
	UPROPERTY(BlueprintReadOnly) FString Contact;
	/** What the sector rolled for it, raised once each. */
	UPROPERTY() bool bContactRolled = false;
	/** The first system charted always brings a first contact; fixed when the chart is made, whenever the roll comes. */
	UPROPERTY() bool bContactGuaranteed = false;
	UPROPERTY() bool bPreWarpRaised = false;
	UPROPERTY() bool bBeaconRaised = false;
	UPROPERTY() bool bHasBeacon = false;
	/** The name in use: the given one, else the designation. */
	FString Name() const { return GivenName.IsEmpty() ? Designation : GivenName; }
};

/** A people met in new space and how it went. */
USTRUCT(BlueprintType)
struct FContactRecord
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) FString Species;
	UPROPERTY(BlueprintReadOnly) FString System;
	UPROPERTY(BlueprintReadOnly) double Stardate = 0.0;
	/** 1 friendly, 0 wary, -1 hostile. */
	UPROPERTY(BlueprintReadOnly) int32 Relation = 0;
	UPROPERTY(BlueprintReadOnly) FString Note;
	FString RelationName() const { return Relation > 0 ? TEXT("friendly") : Relation < 0 ? TEXT("hostile") : TEXT("wary"); }
};

UCLASS()
class SLONK_API USlonkExplorationSaveGame : public USaveGame
{
	GENERATED_BODY()
public:
	UPROPERTY() TArray<FCatalogueEntry> Catalogue;
	UPROPERTY() TArray<FContactRecord> Contacts;
};

/**
 * The sector catalogue: a dozen uncharted stars within thirty light-years, the same in every game, each a star in
 * the sky until the ship comes within a light-year (its worlds are generated from its seed) and charted when the
 * ship arrives, mission or not. Charting rewards regard and science and rolls what the sector holds there (a first
 * contact, a pre-warp world, a beacon), which the mission system raises as events. The peoples met are recorded.
 * One per game, saved to the "Exploration" slot.
 */
UCLASS()
class SLONK_API UExplorationSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	static UExplorationSubsystem* Get(const UObject* WorldContext);

	/** The catalogue's stars into the world (the game mode, after the charted systems); charted ones revealed. */
	void SpawnStars(UWorld* World, const FVector& HomeStarLocation);
	/** Driven from the ship's tick after the missions: reveals within a light-year, charts on arrival. */
	void AdvanceSeconds(float RealSeconds, const FMissionShipState& Ship);

	/** Generate the system's worlds now (the approach does it; a mission or the console can ask). */
	bool Reveal(const FString& Designation);
	/** The system has an event still to raise (a contact unrolled, a lit world unraised, a beacon unraised). */
	static bool HasPendingEvents(const FCatalogueEntry& E) { return !E.bContactRolled || (!E.bPreWarpRaised && !E.PreWarpWorld.IsEmpty()) || (!E.bBeaconRaised && E.bHasBeacon); }
	/** The ship is there: charted, reported, rewarded, and (bRollEvents) the sector's events rolled; the tick rolls
	 *  them itself once the ship is out of warp, so a hull is spawned where she stops. False when already charted. */
	bool Chart(const FString& NameOrDesignation, bool bRollEvents = true);
	/** Name a charted system (the captain's, or the catalogue's own with an empty name). */
	bool Name(const FString& NameOrDesignation, const FString& NewName, FString& OutReply);
	/** The science log's survey of the system is complete. */
	void MarkSurveyed(const FString& NameOrDesignation);

	const TArray<FCatalogueEntry>& GetCatalogue() const { return Catalogue; }
	const FCatalogueEntry* Find(const FString& NameOrDesignation) const;
	/** The entry the ship is in by the last state, or null. */
	const FCatalogueEntry* Here() const { return Find(Last.SystemName); }
	int32 CountCharted() const;
	/** An unknown star for an order: one of the nearest few to the ship, or the nearest. */
	const FCatalogueEntry* PickUnknown(FRandomStream& R, bool bNearest) const;
	/** Where an entry's star is (the catalogue's bearing when there is no world). */
	FVector StarLocation(const FCatalogueEntry& E) const;

	/** A people nobody has met: a name from the generator, never one in the records. */
	FString NewSpeciesName(FRandomStream& R) const;
	void RecordContact(const FString& Species, const FString& System, int32 Relation, const FString& Note);
	const FContactRecord* ContactWith(const FString& Species) const;
	const TArray<FContactRecord>& GetContacts() const { return Contacts; }

	void DrainReports(TArray<FCrewReport>& Out) { Out = MoveTemp(Reports); Reports.Reset(); }
	int32 GetRevision() const { return Revision; }
	static FString StateName(EChartState S);

	UFUNCTION(BlueprintCallable, Category = "Exploration") void Reset();
	/** Tests: the catalogue from the seed, no save slot, no world. */
	void InitForTest(UMissionSubsystem* InMissions);
	/** The catalogue from a seed (pure): designations unique, bearings apart, distances 6..30 ly. */
	static TArray<FCatalogueEntry> MakeCatalogue(int32 Seed, int32 Count);
	static constexpr int32 CatalogueSeed = 2461;
	static constexpr int32 CatalogueCount = 12;
	static constexpr double RevealCm = 9.4607e17;   // a light-year

private:
	void Load();
	void Save();
	FCatalogueEntry* FindMutable(const FString& NameOrDesignation);
	/** A name no system in the sky or the catalogue carries, and not the scanner's "Deep space". */
	bool IsNameFree(const FCatalogueEntry& E, const FString& Want) const;
	AStarSystem* SystemOf(const FCatalogueEntry& E) const;
	/** The name in the sky, the lit world, the science log, the missions' targets and the contact records follow. */
	void ApplyRename(FCatalogueEntry& E, const FString& Old, const FString& New);
	UMissionSubsystem* Missions() const;
	void Report(const FString& Text, const FLinearColor& Color = FLinearColor(0.75f, 0.9f, 1.f), ECrewDepartment From = ECrewDepartment::Science);
	/** What the sector holds where the ship is, raised as events (one per visit). */
	void RollEvents(FCatalogueEntry& E);

	UPROPERTY() TArray<FCatalogueEntry> Catalogue;
	UPROPERTY() TArray<FContactRecord> Contacts;
	UPROPERTY() TArray<FCrewReport> Reports;
	TMap<FString, TWeakObjectPtr<AStarSystem>> Systems;
	FMissionShipState Last;
	float ScanTimer = 0.f;
	int32 Revision = 0;
	bool bNoDisk = false;
	TWeakObjectPtr<UMissionSubsystem> MissionsOverride;
	FRandomStream Rng;
	static const TCHAR* SlotName;
};
