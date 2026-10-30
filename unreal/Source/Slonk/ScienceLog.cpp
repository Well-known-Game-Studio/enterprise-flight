// Slonk - the science log subsystem.

#include "ScienceLog.h"
#include "Kismet/GameplayStatics.h"

const TCHAR* UScienceLogSubsystem::SlotName = TEXT("ScienceLog");
const TCHAR* UScienceLogSubsystem::SurveyLabel = TEXT("System survey");

namespace
{
	struct FRating { int32 Points; const TCHAR* Title; };
	// Starfleet Science ratings, lowest first.
	const FRating Ratings[] = {
		{ 0, TEXT("Field Researcher") },
		{ 500, TEXT("Surveyor") },
		{ 1500, TEXT("Senior Surveyor") },
		{ 3500, TEXT("Science Officer") },
		{ 7000, TEXT("Chief Science Officer") },
		{ 12000, TEXT("Science Fellow") },
		{ 20000, TEXT("Daystrom Fellow") },
	};
}

void UScienceLogSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Load();
}

const TCHAR* UScienceLogSubsystem::KindName(EScanKind Kind)
{
	switch (Kind)
	{
	case EScanKind::Star: return TEXT("Star");
	case EScanKind::Planet: return TEXT("Planet");
	case EScanKind::Moon: return TEXT("Moon");
	case EScanKind::Station: return TEXT("Station");
	case EScanKind::Ship: return TEXT("Ship");
	case EScanKind::Hostile: return TEXT("Hostile");
	case EScanKind::Belt: return TEXT("Belt");
	case EScanKind::Nebula: return TEXT("Nebula");
	case EScanKind::Surface: return TEXT("Surface");
	}
	return TEXT("?");
}

int32 UScienceLogSubsystem::BasePoints(EScanKind Kind)
{
	switch (Kind)
	{
	case EScanKind::Star: return 150;
	case EScanKind::Planet: return 120;
	case EScanKind::Moon: return 80;
	case EScanKind::Station: return 50;
	case EScanKind::Ship: return 40;
	case EScanKind::Hostile: return 100;
	case EScanKind::Belt: return 90;
	case EScanKind::Nebula: return 200;
	case EScanKind::Surface: return 180;
	}
	return 0;
}

const FScanRecord* UScienceLogSubsystem::Find(const FString& Key) const
{
	return Records.FindByPredicate([&Key](const FScanRecord& R) { return R.Key == Key; });
}

int32 UScienceLogSubsystem::CountScanned(const TArray<FString>& Keys) const
{
	int32 N = 0;
	for (const FString& K : Keys) { if (IsScanned(K)) { ++N; } }
	return N;
}

int32 UScienceLogSubsystem::Add(FScanRecord& Record)
{
	if (const FScanRecord* Old = Find(Record.Key))
	{
		// A repeat scan refreshes the readings but earns nothing.
		Record.Points = 0; Record.Bonus = 0; Record.BonusNotes.Reset();
		Record.Discovery = Old->Discovery;
		return 0;
	}
	Record.Points = BasePoints(Record.Kind);
	const bool bFirstOfKind = !Records.ContainsByPredicate([&Record](const FScanRecord& R) { return R.Kind == Record.Kind && R.Label != SurveyLabel; });
	if (bFirstOfKind)
	{
		Record.Bonus += FirstOfKindBonus;
		Record.BonusNotes.Add(FString::Printf(TEXT("first %s scanned +%d"), KindName(Record.Kind), FirstOfKindBonus));
	}
	Record.When = FDateTime::UtcNow();
	Records.Add(Record);
	Recount();
	Save();
	return Record.Total();
}

int32 UScienceLogSubsystem::MarkSystemSurveyed(const FString& System)
{
	if (IsSystemSurveyed(System)) { return 0; }
	FScanRecord R;
	R.Key = MakeKey(System, SurveyLabel);
	R.System = System;
	R.Label = SurveyLabel;
	R.Kind = EScanKind::Star;
	R.Facts.Add(FString::Printf(TEXT("Every star and planet of %s catalogued"), *System));
	R.Bonus = SystemSurveyBonus;
	R.BonusNotes.Add(FString::Printf(TEXT("system survey complete +%d"), SystemSurveyBonus));
	R.When = FDateTime::UtcNow();
	Records.Add(R);
	Recount();
	Save();
	return SystemSurveyBonus;
}

FString UScienceLogSubsystem::RatingTitle() const
{
	const TCHAR* Title = Ratings[0].Title;
	for (const FRating& R : Ratings) { if (Total >= R.Points) { Title = R.Title; } }
	return Title;
}

int32 UScienceLogSubsystem::NextRatingPoints() const
{
	for (const FRating& R : Ratings) { if (Total < R.Points) { return R.Points; } }
	return 0;
}

void UScienceLogSubsystem::Reset()
{
	Records.Reset();
	Recount();
	UGameplayStatics::DeleteGameInSlot(SlotName, 0);
	UE_LOG(LogTemp, Log, TEXT("Science log reset"));
}

void UScienceLogSubsystem::Recount()
{
	Total = 0;
	for (const FScanRecord& R : Records) { Total += R.Total(); }
}

void UScienceLogSubsystem::Load()
{
	Records.Reset();
	if (UGameplayStatics::DoesSaveGameExist(SlotName, 0))
	{
		if (const USlonkScienceSaveGame* Save = Cast<USlonkScienceSaveGame>(UGameplayStatics::LoadGameFromSlot(SlotName, 0)))
		{
			Records = Save->Records;
		}
	}
	Recount();
	UE_LOG(LogTemp, Log, TEXT("Science log: %d records, %d points, %s"), Records.Num(), Total, *RatingTitle());
}

void UScienceLogSubsystem::Save() const
{
	USlonkScienceSaveGame* Save = Cast<USlonkScienceSaveGame>(UGameplayStatics::CreateSaveGameObject(USlonkScienceSaveGame::StaticClass()));
	if (!Save) { return; }
	Save->Records = Records;
	UGameplayStatics::SaveGameToSlot(Save, SlotName, 0);
}
