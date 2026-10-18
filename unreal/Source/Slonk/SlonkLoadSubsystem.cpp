#include "SlonkLoadSubsystem.h"

#include "Engine/World.h"
#include "HAL/PlatformTime.h"

bool USlonkLoadSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void USlonkLoadSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	StartSeconds = FPlatformTime::Seconds();
}

USlonkLoadSubsystem* USlonkLoadSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<USlonkLoadSubsystem>() : nullptr;
}

void USlonkLoadSubsystem::Enqueue(UObject* Owner, const FString& Label, TFunction<void()> Work, float Weight)
{
	if (USlonkLoadSubsystem* Self = Get(Owner))
	{
		FSlonkLoadTask Task;
		Task.Label = Label;
		Task.Owner = Owner;
		Task.Work = MoveTemp(Work);
		Task.Weight = FMath::Max(Weight, 0.01f);
		Self->Add(MoveTemp(Task));
	}
	else if (Work)
	{
		Work();
	}
}

void USlonkLoadSubsystem::Add(FSlonkLoadTask&& Task)
{
	++TasksTotal;
	WeightTotal += Task.Weight;
	Queue.Add(MoveTemp(Task));
}

float USlonkLoadSubsystem::GetProgress() const
{
	const float P = WeightTotal > 0.f ? FMath::Clamp(WeightDone / WeightTotal, 0.f, 1.f) : 0.f;
	// The bar never steps back when a running task spawns more tasks.
	ShownProgress = bInitialLoadDone ? P : FMath::Max(ShownProgress, P);
	return ShownProgress;
}

float USlonkLoadSubsystem::GetElapsedSeconds() const
{
	return StartSeconds > 0.0 ? (float)(FPlatformTime::Seconds() - StartSeconds) : 0.f;
}

void USlonkLoadSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World || !World->HasBegunPlay())
	{
		return;
	}
	const double Budget = bInitialLoadDone ? BackgroundFrameBudget : LoadingFrameBudget;
	const double FrameStart = FPlatformTime::Seconds();
	int32 Ran = 0;
	while (Queue.Num() > 0)
	{
		// Always make progress, then stop once the budget is spent.
		if (Ran > 0 && FPlatformTime::Seconds() - FrameStart > Budget)
		{
			break;
		}
		FSlonkLoadTask Task = MoveTemp(Queue[0]);
		Queue.RemoveAt(0);
		CurrentLabel = Task.Label;
		if (Task.Owner.IsValid() && Task.Work)
		{
			const double T0 = FPlatformTime::Seconds();
			Task.Work();
			const double Took = FPlatformTime::Seconds() - T0;
			WorkSeconds += Took;
			if (Took > SlowestSeconds)
			{
				SlowestSeconds = Took;
				SlowestLabel = Task.Label;
			}
			if (Took > 0.25)
			{
				UE_LOG(LogTemp, Log, TEXT("Slonk load: '%s' took %.2f s"), *Task.Label, Took);
			}
		}
		++TasksDone;
		WeightDone += Task.Weight;
		RecentLabels.Add(Task.Label);
		if (RecentLabels.Num() > 6)
		{
			RecentLabels.RemoveAt(0);
		}
		++Ran;
	}
	if (!bInitialLoadDone && Queue.Num() == 0 && World->GetTimeSeconds() >= MinLoadSeconds)
	{
		bInitialLoadDone = true;
		CurrentLabel.Reset();
		UE_LOG(LogTemp, Log, TEXT("Slonk load: %d tasks, %.2f s of work over %.2f s; slowest '%s' %.2f s"), TasksDone, WorkSeconds, GetElapsedSeconds(), *SlowestLabel, SlowestSeconds);
	}
}
