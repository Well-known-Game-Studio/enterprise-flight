// Slonk - settings subsystem.

#include "SlonkSettings.h"
#include "Kismet/GameplayStatics.h"

const TCHAR* USlonkSettingsSubsystem::SlotName = TEXT("Settings");

void USlonkSettingsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (UGameplayStatics::DoesSaveGameExist(SlotName, 0))
	{
		if (const USlonkSettingsSaveGame* S = Cast<USlonkSettingsSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, 0)))
		{
			Typeface = S->Typeface == 1 ? ELcarsTypeface::Condensed : ELcarsTypeface::Standard;
		}
	}
	Lcars::SetTypeface(Typeface);
	UE_LOG(LogTemp, Log, TEXT("Settings: typeface %s"), Lcars::TypefaceName(Typeface));
}

void USlonkSettingsSubsystem::SetTypeface(ELcarsTypeface Face)
{
	Typeface = Face;
	Lcars::SetTypeface(Face);
	Save();
}

void USlonkSettingsSubsystem::Save() const
{
	USlonkSettingsSaveGame* S = Cast<USlonkSettingsSaveGame>(UGameplayStatics::CreateSaveGameObject(USlonkSettingsSaveGame::StaticClass()));
	if (!S) { return; }
	S->Typeface = Typeface == ELcarsTypeface::Condensed ? 1 : 0;
	UGameplayStatics::SaveGameToSlot(S, SlotName, 0);
}
