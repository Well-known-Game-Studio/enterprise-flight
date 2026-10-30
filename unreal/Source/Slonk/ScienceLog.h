// Slonk - the science log: every completed scan, the points it earned, and the rating they add up to. Saved to disk.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameFramework/SaveGame.h"
#include "ScienceLog.generated.h"

/** What kind of thing a scan record describes. Surface is a landed survey of a world, separate from its orbital scan. */
UENUM(BlueprintType)
enum class EScanKind : uint8
{
	Star,
	Planet,
	Moon,
	Station,
	Ship,
	Hostile,
	Belt,
	Nebula,
	Surface
};

/** One completed scan. Key is "<system>/<label>" (plus "/surface" for a landed survey), stable across sessions
 *  because every generated name comes from the system seed. */
USTRUCT(BlueprintType)
struct FScanRecord
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Science") FString Key;
	UPROPERTY(BlueprintReadOnly, Category = "Science") FString System;
	UPROPERTY(BlueprintReadOnly, Category = "Science") FString Label;
	UPROPERTY(BlueprintReadOnly, Category = "Science") EScanKind Kind = EScanKind::Planet;
	/** Sensor readings, one line each. */
	UPROPERTY(BlueprintReadOnly, Category = "Science") TArray<FString> Facts;
	/** A notable find, if the scan turned one up. */
	UPROPERTY(BlueprintReadOnly, Category = "Science") FString Discovery;
	/** Points for the scan itself. */
	UPROPERTY(BlueprintReadOnly, Category = "Science") int32 Points = 0;
	/** Extra points: a discovery, the first of its kind, completing a system survey. */
	UPROPERTY(BlueprintReadOnly, Category = "Science") int32 Bonus = 0;
	/** Why the bonus was awarded, for the log. */
	UPROPERTY(BlueprintReadOnly, Category = "Science") TArray<FString> BonusNotes;
	UPROPERTY(BlueprintReadOnly, Category = "Science") FDateTime When;

	int32 Total() const { return Points + Bonus; }
};

/** The on-disk form: the records, nothing else. */
UCLASS()
class SLONK_API USlonkScienceSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY() TArray<FScanRecord> Records;
};

/**
 * Holds the science log for the game instance, loads it at start-up and saves after every record. Points add up
 * to a Starfleet Science rating (Field Researcher up to Daystrom Fellow); a system counts as surveyed when its
 * star(s) and every planet have been scanned.
 */
UCLASS()
class SLONK_API UScienceLogSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** Add a completed scan: applies the first-of-kind bonus, stores and saves. Returns the record's total points. */
	int32 Add(FScanRecord& Record);
	bool IsScanned(const FString& Key) const { return Find(Key) != nullptr; }
	const FScanRecord* Find(const FString& Key) const;
	const TArray<FScanRecord>& GetRecords() const { return Records; }

	UFUNCTION(BlueprintPure, Category = "Science") int32 TotalPoints() const { return Total; }
	UFUNCTION(BlueprintPure, Category = "Science") FString RatingTitle() const;
	/** Points needed for the next rating; 0 at the top. */
	UFUNCTION(BlueprintPure, Category = "Science") int32 NextRatingPoints() const;
	/** How many of the given keys are scanned (for the system survey count). */
	int32 CountScanned(const TArray<FString>& Keys) const;
	/** Whether the system-survey bonus has already been paid for a system (it is stored as a record of its own). */
	bool IsSystemSurveyed(const FString& System) const { return IsScanned(MakeKey(System, SurveyLabel)); }
	/** Pay the system-survey bonus: a record of its own so the log shows it. Returns the bonus. */
	int32 MarkSystemSurveyed(const FString& System);

	/** Forget everything, on disk too (testing). */
	UFUNCTION(BlueprintCallable, Category = "Science") void Reset();

	static FString MakeKey(const FString& System, const FString& Label) { return System + TEXT("/") + Label; }
	static const TCHAR* KindName(EScanKind Kind);
	/** Points a plain scan of this kind is worth. */
	static int32 BasePoints(EScanKind Kind);
	static constexpr int32 FirstOfKindBonus = 100;
	static constexpr int32 SystemSurveyBonus = 500;
	static const TCHAR* SurveyLabel;

private:
	void Load();
	void Save() const;
	void Recount();

	UPROPERTY() TArray<FScanRecord> Records;
	int32 Total = 0;
	static const TCHAR* SlotName;
};
