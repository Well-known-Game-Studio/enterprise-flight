// Slonk - the LCARS panel: an activatable CommonUI window with the elbowed Okudagram frame, a sidebar of buttons and
// a content area for the panel's own widgets.

#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "LcarsTheme.h"
#include "LcarsPanel.generated.h"

class UOverlay;
class USizeBox;
class UCanvasPanel;
class UVerticalBox;
class UHorizontalBox;
class UBorder;
class UButton;
class UTextBlock;
class AStarshipPawn;
class AStarshipHUD;
class USlonkUIRoot;

/** Binds a C++ callable to a UButton's dynamic OnClicked. */
UCLASS()
class ULcarsButtonBinding : public UObject
{
	GENERATED_BODY()

public:
	TFunction<void()> Fn;
	FString Label;
	TWeakObjectPtr<UButton> Button;
	UFUNCTION() void Fire() { if (Fn) { Fn(); } }
};

/**
 * Base of every menu, window and popup. Built entirely in code in Initialize: a dimmed backdrop, then a fixed-size
 * frame (PanelSize) with the top-left and bottom-left elbows, a title bar, a sidebar column of buttons and the black
 * content box whose rounded corner cuts the elbows' inner curve. Subclasses override BuildContent to fill Content
 * and AddSidebarButton for their actions. Escape, the B button or Start closes it; while any panel is active CommonUI
 * routes input to the UI with the cursor shown, and the ship's controls are suspended.
 */
UCLASS(Abstract)
class SLONK_API ULcarsPanel : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	ULcarsPanel();
	virtual bool Initialize() override;
	virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;
	virtual UWidget* NativeGetDesiredFocusTarget() const override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual void NativeOnActivated() override;

	UFUNCTION(BlueprintCallable, Category = "LCARS") void Close() { DeactivateWidget(); }
	void SetTitle(const FString& Title);
	/** A block in the sidebar; returns the button so its colour or label can change later. */
	UButton* AddSidebarButton(const FString& Label, const FLinearColor& Color, TFunction<void()> OnClick);
	/** Bind any UButton made elsewhere to a callable. */
	void Bind(UButton* Button, TFunction<void()> OnClick, const FString& Label = FString());
	/** Fire the button whose label (as bound, or as currently shown) matches, case-insensitively; for the console and scripts. */
	bool ClickButton(const FString& Label);
	UVerticalBox* GetContent() const { return Content; }

	AStarshipPawn* GetShip() const;
	AStarshipHUD* GetHUD() const;
	USlonkUIRoot* GetUIRoot() const;

protected:
	/** Fill Content (and the sidebar). Called once from Initialize after the frame exists. */
	virtual void BuildContent() {}
	/** Called every frame while active, for panels that show live data. */
	virtual void Refresh() {}
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FVector2D PanelSize() const { return FVector2D(1400.0, 820.0); }
	virtual FString PanelTitle() const { return TEXT("LCARS"); }
	virtual FLinearColor FrameColor() const { return Lcars::Orange; }
	/** Keys that close the panel (Escape and the gamepad's B / Start by default). */
	virtual bool IsCloseKey(const FKey& Key) const;

	/** A labelled row of text under Content: "LABEL   value". OutValue receives the value block. */
	UHorizontalBox* AddRow(UVerticalBox* Into, const FString& Label, const FString& Value, const FLinearColor& ValueColor = Lcars::Peach, float Size = 15.f, UTextBlock** OutValue = nullptr);
	/** A row whose value is re-read every frame while the panel is up. */
	void AddLiveRow(UVerticalBox* Into, const FString& Label, TFunction<FString()> Value, const FLinearColor& ValueColor = Lcars::Peach, float Size = 15.f);
	struct FLiveText { UTextBlock* Text = nullptr; TFunction<FString()> Fn; FString Last; };
	TArray<FLiveText> Live;
	UTextBlock* AddLine(UVerticalBox* Into, const FString& Text, float Size, const FLinearColor& Color, bool bBold = true, bool bUpper = false, float PadTop = 0.f);

	UPROPERTY() TObjectPtr<UOverlay> Root;
	UPROPERTY() TObjectPtr<USizeBox> Frame;
	UPROPERTY() TObjectPtr<UCanvasPanel> Canvas;
	UPROPERTY() TObjectPtr<UVerticalBox> Sidebar;
	UPROPERTY() TObjectPtr<UBorder> SidebarFiller;
	UPROPERTY() TObjectPtr<UBorder> ContentBorder;
	UPROPERTY() TObjectPtr<UVerticalBox> Content;
	UPROPERTY() TObjectPtr<UTextBlock> TitleText;
	UPROPERTY() TArray<TObjectPtr<ULcarsButtonBinding>> Bindings;
	int32 SidebarButtons = 0;

private:
	void BuildFrame();
};

/** A small LCARS dialog: a title, a message and a row of buttons; the callback gets the index pressed. */
UCLASS()
class SLONK_API ULcarsPopup : public ULcarsPanel
{
	GENERATED_BODY()

public:
	/** Push a popup onto the UI root's popup stack. */
	static ULcarsPopup* Show(USlonkUIRoot* UI, const FString& Title, const FString& Message, const TArray<FString>& Buttons, TFunction<void(int32)> OnChoice = nullptr);

	FString Title = TEXT("Confirm");
	FString Message;
	TArray<FString> Buttons;
	TFunction<void(int32)> OnChoice;
	void Populate();

protected:
	virtual FVector2D PanelSize() const override { return FVector2D(680.0, 300.0); }
	virtual FString PanelTitle() const override { return Title; }
	virtual FLinearColor FrameColor() const override { return Lcars::Salmon; }
};
