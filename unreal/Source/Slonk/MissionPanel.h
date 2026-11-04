// Slonk - the missions panel: the board (offers and what is under way), the inbox from Starfleet, the log of what
// was done; the selected mission's brief, its present situation and the options to take.

#pragma once

#include "CoreMinimal.h"
#include "LcarsPanel.h"
#include "Missions.h"
#include "MissionPanel.generated.h"

class UScrollBox;
class UVerticalBox;
class UTextBlock;

/** - : missions. Left the board, the inbox or the log; right the selected mission with its options as buttons. */
UCLASS()
class SLONK_API UMissionPanel : public ULcarsPanel
{
	GENERATED_BODY()

public:
	void Select(int32 Id) { SelectedId = Id; LastFileKey.Reset(); }

protected:
	virtual void BuildContent() override;
	virtual void Refresh() override;
	virtual void NativeOnActivated() override;
	virtual FString PanelTitle() const override { return TEXT("Missions"); }
	virtual FLinearColor FrameColor() const override { return Lcars::Gold; }
	virtual FVector2D PanelSize() const override { return FVector2D(1560.0, 900.0); }

private:
	UMissionSubsystem* Missions() const;
	void FillList();
	void FillFile();
	void Say(const FString& Text, bool bGood = true, float Seconds = 10.f) const;

	UPROPERTY() TObjectPtr<UScrollBox> ListBox;
	UPROPERTY() TObjectPtr<UScrollBox> FileScroll;
	UPROPERTY() TObjectPtr<UVerticalBox> FileBox;
	UPROPERTY() TObjectPtr<UButton> ShowButton;
	UPROPERTY() TArray<TObjectPtr<UButton>> RowButtons;
	UPROPERTY() TArray<TObjectPtr<UButton>> OptionButtons;
	/** 0: the board (open missions), 1: the inbox, 2: the log (closed missions). */
	int32 Show = 0;
	int32 SelectedId = -1;
	/** The inbox message selected while Show is the inbox. */
	int32 SelectedMessage = -1;
	int32 LastRevision = -1, LastShow = -1, LastSelected = -2, LastMessage = -2;
	FString LastFileKey;
};
