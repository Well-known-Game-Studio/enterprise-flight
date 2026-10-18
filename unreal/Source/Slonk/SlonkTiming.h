// Slonk - a scope timer that reports slow sections to the log, for finding what stalls the first frames.

#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformTime.h"

/** Logs "Slonk timing: <Name> <seconds>" when the scope took longer than Threshold seconds. */
struct FSlonkScopeTimer
{
	explicit FSlonkScopeTimer(const TCHAR* InName, double InThreshold = 0.25)
		: Name(InName), Threshold(InThreshold), Start(FPlatformTime::Seconds())
	{
	}
	~FSlonkScopeTimer()
	{
		const double Elapsed = FPlatformTime::Seconds() - Start;
		if (Elapsed > Threshold)
		{
			UE_LOG(LogTemp, Log, TEXT("Slonk timing: %s %.2f s"), Name, Elapsed);
		}
	}
	const TCHAR* Name;
	double Threshold;
	double Start;
};
