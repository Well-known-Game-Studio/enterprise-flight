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
	/** The ground's rocks and plants drawn this many thinned levels beyond their own (slonk.Scatter.FarLevels). */
	UPROPERTY() uint8 GroundDetail = 3;
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

	/** How far the ground's rocks and plants reach: 0 near (each class on its own levels), 1 far, 2 farther, 3
	 *  farthest, 4 to the horizon (trees to about 1, 2.5, 5, 10 and 20 km). Each level costs the GPU its drawing and the
	 *  workers its building; the trees past a couple of kilometres are a handful of triangles apiece (SlonkPlants' far crown). */
	int32 GetGroundDetail() const { return GroundDetail; }
	void SetGroundDetail(int32 Levels);

private:
	void Save() const;
	ELcarsTypeface Typeface = ELcarsTypeface::Standard;
	int32 GroundDetail = 3;
	static const TCHAR* SlotName;
};
