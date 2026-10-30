// Slonk - the LCARS panels: main menu, controls, science log.

#pragma once

#include "CoreMinimal.h"
#include "LcarsPanel.h"
#include "LcarsPanels.generated.h"

class UScrollBox;

/** Escape: ship status and the way to every other panel. */
UCLASS()
class SLONK_API UMainMenuPanel : public ULcarsPanel
{
	GENERATED_BODY()

protected:
	virtual void BuildContent() override;
	virtual FString PanelTitle() const override { return TEXT("Main Computer"); }
	virtual FVector2D PanelSize() const override { return FVector2D(1100.0, 640.0); }
};

/** H: every key and what it does, in two columns. */
UCLASS()
class SLONK_API UControlsPanel : public ULcarsPanel
{
	GENERATED_BODY()

protected:
	virtual void BuildContent() override;
	virtual FString PanelTitle() const override { return TEXT("Helm and Systems Controls"); }
	virtual FLinearColor FrameColor() const override { return Lcars::Periwinkle; }
};

/** /: the science catalogue. */
UCLASS()
class SLONK_API UScienceLogPanel : public ULcarsPanel
{
	GENERATED_BODY()

protected:
	virtual void BuildContent() override;
	virtual FString PanelTitle() const override { return TEXT("Science Log"); }
	virtual FLinearColor FrameColor() const override { return Lcars::Gold; }

private:
	void FillRecords();
	UPROPERTY() TObjectPtr<UScrollBox> List;
	bool bDiscoveriesOnly = false;
	UPROPERTY() TObjectPtr<UButton> FilterButton;
};
