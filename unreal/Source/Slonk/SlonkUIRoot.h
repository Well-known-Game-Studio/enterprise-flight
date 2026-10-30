// Slonk - the UI root: the always-active CommonUI layer that owns the menu and popup stacks.

#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "Widgets/CommonActivatableWidgetContainer.h"
#include "SlonkUIRoot.generated.h"

class UOverlay;
class ULcarsPanel;

/**
 * Added to the player's screen by the controller and activated for the whole session. Its own input config is the
 * game's (mouse captured, cursor hidden); any LCARS panel pushed onto a stack takes over with a menu config until it
 * closes, when CommonUI falls back to this.
 */
UCLASS()
class SLONK_API USlonkUIRoot : public UCommonActivatableWidget
{
	GENERATED_BODY()

public:
	USlonkUIRoot();
	virtual bool Initialize() override;
	virtual TOptional<FUIInputConfig> GetDesiredInputConfig() const override;

	template <typename T>
	T* PushMenu() { return MenuStack ? MenuStack->AddWidget<T>(T::StaticClass()) : nullptr; }
	template <typename T>
	T* PushPopup(TFunctionRef<void(T&)> Init) { return PopupStack ? PopupStack->AddWidget<T>(T::StaticClass(), Init) : nullptr; }
	ULcarsPanel* PushMenuClass(TSubclassOf<ULcarsPanel> Class);
	/** Open the panel, or close it if it is the one on top. */
	void TogglePanel(TSubclassOf<ULcarsPanel> Class);
	UFUNCTION(BlueprintCallable, Category = "UI") void CloseAll();
	UFUNCTION(BlueprintPure, Category = "UI") bool IsMenuOpen() const;
	UCommonActivatableWidget* TopMenu() const { return MenuStack ? MenuStack->GetActiveWidget() : nullptr; }
	UCommonActivatableWidget* TopPopup() const { return PopupStack ? PopupStack->GetActiveWidget() : nullptr; }

private:
	UPROPERTY() TObjectPtr<UOverlay> Root;
	UPROPERTY() TObjectPtr<UCommonActivatableWidgetStack> MenuStack;
	UPROPERTY() TObjectPtr<UCommonActivatableWidgetStack> PopupStack;
};
