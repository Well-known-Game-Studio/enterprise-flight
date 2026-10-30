// Slonk - UI root.

#include "SlonkUIRoot.h"
#include "LcarsPanel.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Input/UIActionBindingHandle.h"
#include "CommonInputModeTypes.h"
#include "Engine/GameViewportClient.h"

USlonkUIRoot::USlonkUIRoot()
{
	bAutoActivate = true;
	SetIsFocusable(false);
}

bool USlonkUIRoot::Initialize()
{
	const bool bFirst = Super::Initialize();
	if (bFirst && WidgetTree && !WidgetTree->RootWidget && !HasAnyFlags(RF_ClassDefaultObject))
	{
		Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("Root"));
		WidgetTree->RootWidget = Root;
		MenuStack = WidgetTree->ConstructWidget<UCommonActivatableWidgetStack>(UCommonActivatableWidgetStack::StaticClass(), TEXT("MenuStack"));
		PopupStack = WidgetTree->ConstructWidget<UCommonActivatableWidgetStack>(UCommonActivatableWidgetStack::StaticClass(), TEXT("PopupStack"));
		for (UWidget* W : { (UWidget*)MenuStack, (UWidget*)PopupStack })
		{
			if (UOverlaySlot* S = Root->AddChildToOverlay(W)) { S->SetHorizontalAlignment(HAlign_Fill); S->SetVerticalAlignment(VAlign_Fill); }
		}
		SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
	return bFirst;
}

TOptional<FUIInputConfig> USlonkUIRoot::GetDesiredInputConfig() const
{
	return FUIInputConfig(ECommonInputMode::Game, EMouseCaptureMode::CapturePermanently_IncludingInitialMouseDown, true);
}

ULcarsPanel* USlonkUIRoot::PushMenuClass(TSubclassOf<ULcarsPanel> Class)
{
	return (MenuStack && Class) ? MenuStack->AddWidget<ULcarsPanel>(Class) : nullptr;
}

void USlonkUIRoot::TogglePanel(TSubclassOf<ULcarsPanel> Class)
{
	if (UCommonActivatableWidget* Top = TopMenu())
	{
		if (Class && Top->IsA(Class)) { Top->DeactivateWidget(); return; }
	}
	PushMenuClass(Class);
}

void USlonkUIRoot::CloseAll()
{
	if (PopupStack) { PopupStack->ClearWidgets(); }
	if (MenuStack) { MenuStack->ClearWidgets(); }
}

bool USlonkUIRoot::IsMenuOpen() const
{
	return (MenuStack && MenuStack->GetNumWidgets() > 0) || (PopupStack && PopupStack->GetNumWidgets() > 0);
}
