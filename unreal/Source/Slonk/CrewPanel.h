// Slonk - the crew panel: the roster by department, a personnel file for whoever is selected, the captain's actions.

#pragma once

#include "CoreMinimal.h"
#include "LcarsPanel.h"
#include "CrewRoster.h"
#include "CrewPanel.generated.h"

class UScrollBox;
class UVerticalBox;
class UTextBlock;

/** = : the crew. Rows by department (or those in trouble), a file for the selected person, the captain's hand in the sidebar. */
UCLASS()
class SLONK_API UCrewPanel : public ULcarsPanel
{
	GENERATED_BODY()

public:
	/** Open on a person (from another board). */
	void Select(int32 Id) { SelectedId = Id; LastFileKey.Reset(); }

protected:
	virtual void BuildContent() override;
	virtual void Refresh() override;
	virtual void NativeOnActivated() override;
	virtual FString PanelTitle() const override { return TEXT("Crew"); }
	virtual FLinearColor FrameColor() const override { return Lcars::Tan; }
	virtual FVector2D PanelSize() const override { return FVector2D(1560.0, 900.0); }

private:
	UCrewRosterSubsystem* Roster() const;
	void FillList();
	void FillFile();
	FString RowText(const FCrewMember& M) const;
	FLinearColor MoodColor(float Mood) const;

	UPROPERTY() TObjectPtr<UScrollBox> ListBox;
	UPROPERTY() TObjectPtr<UScrollBox> FileScroll;
	UPROPERTY() TObjectPtr<UVerticalBox> FileBox;
	UPROPERTY() TObjectPtr<UButton> FilterButton;
	UPROPERTY() TObjectPtr<UButton> SortButton;
	UPROPERTY() TArray<TObjectPtr<UButton>> RowButtons;
	/** The rows on screen, by crew id, so a revision or a click restyles in place instead of rebuilding 500 widgets. */
	TArray<int32> RowIds;
	TMap<int32, UButton*> RowById;
	TMap<int32, UTextBlock*> RowLabelById;
	int32 LastSelected = -1;
	int32 DayLineRevision = -1;
	FString DayLine;
	void StyleRow(const FCrewMember& M, bool bSelected);
	TArray<const FCrewMember*> GatherRows(UCrewRosterSubsystem* R) const;
	/** -2: in trouble, -1: senior staff, 0..: a department, Count: everyone. */
	int32 Filter = -1;
	int32 Sort = 0;
	int32 SelectedId = -1;
	int32 LastRevision = -1;
	int32 LastFilter = -99, LastSort = -1;
	FString LastFileKey;
};
