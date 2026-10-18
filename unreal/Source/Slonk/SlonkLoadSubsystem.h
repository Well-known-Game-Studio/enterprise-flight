// Slonk - the world-generation queue behind the loading screen. Heavy start-up work (procedural hulls, traffic
// spawning) is enqueued as small tasks and run a few per frame under a time budget, so the first seconds of a
// level show a progress bar that moves instead of a frozen frame. Later tasks keep using the queue, invisibly.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "SlonkLoadSubsystem.generated.h"

struct FSlonkLoadTask
{
	FString Label;
	TWeakObjectPtr<UObject> Owner;
	TFunction<void()> Work;
	/** Weight toward the progress fraction (a Borg cube is many ships' worth). */
	float Weight = 1.f;
};

UCLASS()
class SLONK_API USlonkLoadSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(USlonkLoadSubsystem, STATGROUP_Tickables); }
	virtual bool IsTickableInEditor() const override { return false; }

	/** Queue work to run on the game thread. If Owner dies first the task is dropped. Runs immediately when the
	 *  queue is not available (no world), so callers never need a fallback. */
	static void Enqueue(UObject* Owner, const FString& Label, TFunction<void()> Work, float Weight = 1.f);

	/** Convenience: the subsystem of the owner's world, if any. */
	static USlonkLoadSubsystem* Get(const UObject* WorldContext);

	/** True until the first drain of the queue after the level's first moments: the loading screen shows while this is true. */
	UFUNCTION(BlueprintPure, Category = "Slonk|Loading")
	bool IsInitialLoadDone() const { return bInitialLoadDone; }

	/** Fraction of queued weight completed so far (never goes backwards while loading). */
	UFUNCTION(BlueprintPure, Category = "Slonk|Loading")
	float GetProgress() const;

	UFUNCTION(BlueprintPure, Category = "Slonk|Loading")
	FString GetCurrentLabel() const { return CurrentLabel; }

	UFUNCTION(BlueprintPure, Category = "Slonk|Loading")
	int32 GetTasksDone() const { return TasksDone; }

	UFUNCTION(BlueprintPure, Category = "Slonk|Loading")
	int32 GetTasksTotal() const { return TasksTotal; }

	/** Seconds since the level began play. */
	UFUNCTION(BlueprintPure, Category = "Slonk|Loading")
	float GetElapsedSeconds() const;

	/** The last few finished task labels, newest last. */
	const TArray<FString>& GetRecentLabels() const { return RecentLabels; }

	/** Seconds of task work per frame while the loading screen is up, and afterwards. */
	UPROPERTY(EditAnywhere, Category = "Slonk|Loading") float LoadingFrameBudget = 0.05f;
	UPROPERTY(EditAnywhere, Category = "Slonk|Loading") float BackgroundFrameBudget = 0.008f;
	/** The loading phase cannot end before this many seconds of play, so late spawners get their tasks counted. */
	UPROPERTY(EditAnywhere, Category = "Slonk|Loading") float MinLoadSeconds = 0.75f;

private:
	void Add(FSlonkLoadTask&& Task);
	TArray<FSlonkLoadTask> Queue;
	int32 TasksDone = 0;
	int32 TasksTotal = 0;
	float WeightDone = 0.f;
	float WeightTotal = 0.f;
	mutable float ShownProgress = 0.f;
	FString CurrentLabel;
	TArray<FString> RecentLabels;
	bool bInitialLoadDone = false;
	double StartSeconds = 0.0;
	double WorkSeconds = 0.0;
	double SlowestSeconds = 0.0;
	FString SlowestLabel;
};
