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
	/** The ground detail as saves before GroundReach kept it. A save keeps only what differs from these defaults, so this
	 *  default must stay what those builds had (2, their default and their farthest): a save without it said 2. */
	UPROPERTY() uint8 GroundDetail = 2;
	/** The ground's rocks and plants drawn this many thinned levels beyond their own (slonk.Scatter.FarLevels), plus
	 *  one: 0, the default, never differs from a saved value, so every save keeps it and a later change of the settings'
	 *  default cannot move a player's choice. 0 in a save from before it. */
	UPROPERTY() uint8 GroundReach = 0;
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
	 *  farthest, 4 to the horizon. Each step about doubles the trees' reach, to 8-15 km on Horizon (the levels snap to
	 *  the world's chunk sizes, so it varies with its size). Each costs the GPU its drawing and the workers its building;
	 *  the trees past a couple of kilometres are a handful of triangles apiece (SlonkPlants' far crown). */
	int32 GetGroundDetail() const { return GroundDetail; }
	void SetGroundDetail(int32 Levels);
	/** What a player who has not chosen gets. */
	static constexpr int32 DefaultGroundDetail = 3;

private:
	void Save() const;
	ELcarsTypeface Typeface = ELcarsTypeface::Standard;
	int32 GroundDetail = DefaultGroundDetail;
	static const TCHAR* SlotName;
};
