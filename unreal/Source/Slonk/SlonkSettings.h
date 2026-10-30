// Slonk - player settings that persist between sessions (the LCARS typeface for now), with a save slot behind them.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameFramework/SaveGame.h"
#include "LcarsTheme.h"
#include "SlonkSettings.generated.h"

UCLASS()
class SLONK_API USlonkSettingsSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY() uint8 Typeface = 0;
};

/** Loads the settings at start-up, applies them, and saves on every change. */
UCLASS()
class SLONK_API USlonkSettingsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	ELcarsTypeface GetTypeface() const { return Typeface; }
	void SetTypeface(ELcarsTypeface Face);

private:
	void Save() const;
	ELcarsTypeface Typeface = ELcarsTypeface::Standard;
	static const TCHAR* SlotName;
};
