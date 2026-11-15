// Slonk - settings subsystem.

#include "SlonkSettings.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"

const TCHAR* USlonkSettingsSubsystem::SlotName = TEXT("Settings");

namespace
{
	/** The ground detail goes to the terrain through its console variable (UPlanetTerrainComponent watches it). */
	void ApplyGroundDetail(int32 Levels)
	{
		if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(TEXT("slonk.Scatter.FarLevels"))) { V->Set(Levels, ECVF_SetByGameSetting); }
	}
}

void USlonkSettingsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (UGameplayStatics::DoesSaveGameExist(SlotName, 0))
	{
		if (const USlonkSettingsSaveGame* S = Cast<USlonkSettingsSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, 0)))
		{
			Typeface = S->Typeface == 1 ? ELcarsTypeface::Condensed : ELcarsTypeface::Standard;
			GroundDetail = FMath::Clamp((int32)S->GroundDetail, 0, 2);
		}
	}
	Lcars::SetTypeface(Typeface);
	ApplyGroundDetail(GroundDetail);
	UE_LOG(LogTemp, Log, TEXT("Settings: typeface %s, ground detail %d"), Lcars::TypefaceName(Typeface), GroundDetail);
}

void USlonkSettingsSubsystem::SetTypeface(ELcarsTypeface Face)
{
	Typeface = Face;
	Lcars::SetTypeface(Face);
	Save();
}

void USlonkSettingsSubsystem::SetGroundDetail(int32 Levels)
{
	GroundDetail = FMath::Clamp(Levels, 0, 2);
	ApplyGroundDetail(GroundDetail);
	Save();
}

void USlonkSettingsSubsystem::Save() const
{
	USlonkSettingsSaveGame* S = Cast<USlonkSettingsSaveGame>(UGameplayStatics::CreateSaveGameObject(USlonkSettingsSaveGame::StaticClass()));
	if (!S) { return; }
	S->Typeface = Typeface == ELcarsTypeface::Condensed ? 1 : 0;
	S->GroundDetail = (uint8)GroundDetail;
	UGameplayStatics::SaveGameToSlot(S, SlotName, 0);
}
