// Slonk - LCARS panel base and popup.

#include "LcarsPanel.h"
#include "SlonkUIRoot.h"
#include "StarshipPawn.h"
#include "PersonPawn.h"
#include "StarshipHUD.h"
#include "StarshipPlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Spacer.h"
#include "Input/UIActionBindingHandle.h"
#include "CommonInputModeTypes.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"

ULcarsPanel::ULcarsPanel()
{
	SetIsFocusable(true);
	bAutoActivate = true;
}

bool ULcarsPanel::Initialize()
{
	const bool bFirst = Super::Initialize();
	if (bFirst && WidgetTree && !WidgetTree->RootWidget && !HasAnyFlags(RF_ClassDefaultObject))
	{
		BuildFrame();
		BuildContent();
	}
	return bFirst;
}

TOptional<FUIInputConfig> ULcarsPanel::GetDesiredInputConfig() const
{
	// A menu: the cursor is free, the ship's controls stop until the panel closes.
	return FUIInputConfig(ECommonInputMode::Menu, EMouseCaptureMode::NoCapture, false);
}

UWidget* ULcarsPanel::NativeGetDesiredFocusTarget() const
{
	return const_cast<ULcarsPanel*>(this);
}

void ULcarsPanel::NativeOnActivated()
{
	Super::NativeOnActivated();
	SetFocus();
}

bool ULcarsPanel::IsCloseKey(const FKey& Key) const
{
	return Key == EKeys::Escape || Key == EKeys::Gamepad_FaceButton_Right || Key == EKeys::Gamepad_Special_Left;
}

FReply ULcarsPanel::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (IsCloseKey(InKeyEvent.GetKey()))
	{
		Close();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void ULcarsPanel::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!IsActivated()) { return; }
	for (FLiveText& L : Live)
	{
		if (!L.Text || !L.Fn) { continue; }
		const FString Now = L.Fn();
		if (Now != L.Last) { L.Last = Now; L.Text->SetText(FText::FromString(Now)); }
	}
	Refresh();
}

AStarshipPawn* ULcarsPanel::GetShip() const
{
	APawn* P = GetOwningPlayerPawn();
	if (AStarshipPawn* S = Cast<AStarshipPawn>(P)) { return S; }
	// On foot the boards still answer for the ship we came from.
	if (const APersonPawn* Person = Cast<APersonPawn>(P)) { return Person->Craft.Get(); }
	return nullptr;
}
AStarshipHUD* ULcarsPanel::GetHUD() const { APlayerController* PC = GetOwningPlayer(); return PC ? Cast<AStarshipHUD>(PC->GetHUD()) : nullptr; }
USlonkUIRoot* ULcarsPanel::GetUIRoot() const { AStarshipPlayerController* PC = Cast<AStarshipPlayerController>(GetOwningPlayer()); return PC ? PC->GetUIRoot() : nullptr; }

void ULcarsPanel::SetTitle(const FString& Title)
{
	if (TitleText) { TitleText->SetText(FText::FromString(Title.ToUpper())); }
}

void ULcarsPanel::Bind(UButton* Button, TFunction<void()> OnClick, const FString& Label)
{
	if (!Button) { return; }
	ULcarsButtonBinding* B = NewObject<ULcarsButtonBinding>(this);
	B->Fn = MoveTemp(OnClick);
	B->Label = Label;
	B->Button = Button;
	Bindings.Add(B);
	Button->OnClicked.AddDynamic(B, &ULcarsButtonBinding::Fire);
}

bool ULcarsPanel::ClickButton(const FString& Label)
{
	const FString Want = Label.TrimStartAndEnd();
	for (ULcarsButtonBinding* B : Bindings)
	{
		if (!B || !B->Button.IsValid()) { continue; }
		FString Shown;
		if (const UTextBlock* T = Cast<UTextBlock>(B->Button->GetChildAt(0))) { Shown = T->GetText().ToString(); }
		if (B->Label.Equals(Want, ESearchCase::IgnoreCase) || Shown.Equals(Want, ESearchCase::IgnoreCase)
			|| (!Want.IsEmpty() && (B->Label.StartsWith(Want, ESearchCase::IgnoreCase) || Shown.StartsWith(Want, ESearchCase::IgnoreCase))))
		{
			B->Button->OnClicked.Broadcast();
			return true;
		}
	}
	return false;
}

UButton* ULcarsPanel::AddSidebarButton(const FString& Label, const FLinearColor& Color, TFunction<void()> OnClick)
{
	if (!Sidebar) { return nullptr; }
	// The first block of the column takes the elbow's inner notch: it is drawn under it, so just a plain block.
	const FVector4 Radii(0.f, 0.f, 0.f, 0.f);
	UButton* Button = Lcars::MakeButton(WidgetTree, Label, Color, nullptr, &Radii);
	USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	Box->SetHeightOverride(Lcars::ButtonH);
	Box->AddChild(Button);
	// Insert before the filler so the filler stays last.
	const int32 Index = SidebarFiller ? Sidebar->GetChildIndex(SidebarFiller) : Sidebar->GetChildrenCount();
	Sidebar->InsertChildAt(Index, Box);
	if (UVerticalBoxSlot* S = Cast<UVerticalBoxSlot>(Box->Slot))
	{
		S->SetPadding(FMargin(0.f, 0.f, 0.f, Lcars::Gap));
		S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
	}
	Bind(Button, MoveTemp(OnClick), Label);
	++SidebarButtons;
	return Button;
}

UTextBlock* ULcarsPanel::AddLine(UVerticalBox* Into, const FString& Text, float Size, const FLinearColor& Color, bool bBold, bool bUpper, float PadTop)
{
	UTextBlock* T = Lcars::MakeText(WidgetTree, Text, Size, Color, bBold, bUpper);
	T->SetAutoWrapText(true);
	if (UVerticalBoxSlot* S = Into->AddChildToVerticalBox(T)) { S->SetPadding(FMargin(0.f, PadTop, 0.f, 2.f)); }
	return T;
}

UHorizontalBox* ULcarsPanel::AddRow(UVerticalBox* Into, const FString& Label, const FString& Value, const FLinearColor& ValueColor, float Size, UTextBlock** OutValue)
{
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	UTextBlock* L = Lcars::MakeText(WidgetTree, Label, Size, Lcars::Lilac, true, true);
	UTextBlock* V = Lcars::MakeText(WidgetTree, Value, Size, ValueColor, false, false);
	V->SetAutoWrapText(true);
	USizeBox* Min = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	Min->SetMinDesiredWidth(190.f);
	Min->AddChild(L);
	if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(Min)) { S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic)); S->SetPadding(FMargin(0.f, 0.f, 14.f, 0.f)); }
	if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(V)) { S->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); }
	if (UVerticalBoxSlot* S = Into->AddChildToVerticalBox(Row)) { S->SetPadding(FMargin(0.f, 1.f, 0.f, 3.f)); }
	if (OutValue) { *OutValue = V; }
	return Row;
}

void ULcarsPanel::AddLiveRow(UVerticalBox* Into, const FString& Label, TFunction<FString()> Value, const FLinearColor& ValueColor, float Size)
{
	FLiveText L;
	L.Fn = MoveTemp(Value);
	L.Last = L.Fn ? L.Fn() : FString();
	AddRow(Into, Label, L.Last, ValueColor, Size, &L.Text);
	Live.Add(MoveTemp(L));
}

void ULcarsPanel::BuildFrame()
{
	using namespace Lcars;
	const FVector2D Size = PanelSize();
	const float W = (float)Size.X, H = (float)Size.Y;
	const FLinearColor FrameCol = FrameColor();

	Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;

	// Dim the game behind the panel.
	UBorder* Backdrop = MakeBlock(WidgetTree, FLinearColor(0.f, 0.f, 0.f, 0.55f), FVector4(0.f, 0.f, 0.f, 0.f));
	if (UOverlaySlot* S = Root->AddChildToOverlay(Backdrop)) { S->SetHorizontalAlignment(HAlign_Fill); S->SetVerticalAlignment(VAlign_Fill); }

	this->Frame = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("Frame"));
	this->Frame->SetWidthOverride(W);
	this->Frame->SetHeightOverride(H);
	if (UOverlaySlot* S = Root->AddChildToOverlay(this->Frame)) { S->SetHorizontalAlignment(HAlign_Center); S->SetVerticalAlignment(VAlign_Center); }

	Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Canvas"));
	this->Frame->AddChild(Canvas);
	auto Place = [this](UWidget* Widget, float X, float Y, float SW, float SH, int32 Z) -> UCanvasPanelSlot*
	{
		UCanvasPanelSlot* S = Canvas->AddChildToCanvas(Widget);
		if (S) { S->SetAutoSize(false); S->SetPosition(FVector2D(X, Y)); S->SetSize(FVector2D(SW, SH)); S->SetZOrder(Z); }
		return S;
	};

	// Elbows: the top-left and bottom-left corners, reaching under the content box's rounded corners. A rounded box
	// clamps its radius to half its smaller side, so the block has to stand at least two radii tall for the outer
	// curve to sweep the whole corner and run on down into the column, as the real displays do.
	const float ElbowH = FMath::Max(TopBarH + Notch, ElbowR * 2.f);
	Place(MakeBlock(WidgetTree, FrameCol, FVector4(ElbowR, 0.f, 0.f, 0.f)), 0.f, 0.f, SidebarW + Notch, ElbowH, 0);
	Place(MakeBlock(WidgetTree, FrameCol, FVector4(0.f, 0.f, 0.f, ElbowR)), 0.f, H - ElbowH, SidebarW + Notch, ElbowH, 0);

	// Top bar: a short block, the title in the gap, a long block and a rounded end cap.
	{
		UHorizontalBox* Bar = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		const float BarX = SidebarW + Notch + Gap;
		Place(Bar, BarX, 0.f, W - BarX, TopBarH, 0);
		auto Seg = [&](UWidget* Wd, float Fixed, bool bFill)
		{
			if (UHorizontalBoxSlot* S = Bar->AddChildToHorizontalBox(Wd))
			{
				S->SetSize(FSlateChildSize(bFill ? ESlateSizeRule::Fill : ESlateSizeRule::Automatic));
				S->SetPadding(FMargin(0.f, 0.f, Gap, 0.f));
				S->SetVerticalAlignment(VAlign_Fill);
				if (!bFill && Fixed > 0.f)
				{
					if (USizeBox* B = Cast<USizeBox>(Wd)) { B->SetWidthOverride(Fixed); }
				}
			}
		};
		USizeBox* B1 = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass()); B1->AddChild(MakeBlock(WidgetTree, Peach, FVector4(0.f, 0.f, 0.f, 0.f))); Seg(B1, 120.f, false);
		TitleText = MakeText(WidgetTree, PanelTitle(), 20.f, FrameCol);
		USizeBox* TB = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		TB->AddChild(TitleText);
		if (UHorizontalBoxSlot* S = Bar->AddChildToHorizontalBox(TB)) { S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic)); S->SetPadding(FMargin(6.f, 0.f, 12.f, 0.f)); S->SetVerticalAlignment(VAlign_Center); }
		Seg(MakeBlock(WidgetTree, Lavender, FVector4(0.f, 0.f, 0.f, 0.f)), 0.f, true);
		USizeBox* B3 = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass()); B3->AddChild(MakeBlock(WidgetTree, FrameCol, FVector4(0.f, TopBarH * 0.5f, TopBarH * 0.5f, 0.f))); Seg(B3, 70.f, false);
		if (UHorizontalBoxSlot* S = Cast<UHorizontalBoxSlot>(B3->Slot)) { S->SetPadding(FMargin(0.f)); }
	}
	// Bottom bar.
	{
		UHorizontalBox* Bar = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		const float BarX = SidebarW + Notch + Gap;
		Place(Bar, BarX, H - BottomBarH, W - BarX, BottomBarH, 0);
		if (UHorizontalBoxSlot* S = Bar->AddChildToHorizontalBox(MakeBlock(WidgetTree, Tan, FVector4(0.f, 0.f, 0.f, 0.f)))) { S->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); S->SetPadding(FMargin(0.f, 0.f, Gap, 0.f)); S->SetVerticalAlignment(VAlign_Fill); }
		USizeBox* B2 = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass()); B2->SetWidthOverride(160.f); B2->AddChild(MakeBlock(WidgetTree, Periwinkle, FVector4(0.f, 0.f, 0.f, 0.f)));
		if (UHorizontalBoxSlot* S = Bar->AddChildToHorizontalBox(B2)) { S->SetPadding(FMargin(0.f, 0.f, Gap, 0.f)); S->SetVerticalAlignment(VAlign_Fill); }
		USizeBox* B3 = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass()); B3->SetWidthOverride(50.f); B3->AddChild(MakeBlock(WidgetTree, FrameCol, FVector4(0.f, BottomBarH * 0.5f, BottomBarH * 0.5f, 0.f)));
		if (UHorizontalBoxSlot* S = Bar->AddChildToHorizontalBox(B3)) { S->SetVerticalAlignment(VAlign_Fill); }
	}

	// Sidebar column between the elbows: buttons, then a filler block to the bottom elbow.
	Sidebar = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Sidebar"));
	const float SideY = ElbowH + Gap;
	Place(Sidebar, 0.f, SideY, SidebarW, H - SideY - ElbowH - Gap, 0);
	SidebarFiller = MakeBlock(WidgetTree, Lilac, FVector4(0.f, 0.f, 0.f, 0.f));
	if (UVerticalBoxSlot* S = Sidebar->AddChildToVerticalBox(SidebarFiller)) { S->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); }

	// Content box: near-black, its left corners rounded so the elbows show their inner curve round it.
	ContentBorder = MakeBlock(WidgetTree, Panel, FVector4(InnerR, 0.f, 0.f, InnerR));
	ContentBorder->SetPadding(FMargin(30.f, 22.f, 26.f, 18.f));
	Place(ContentBorder, SidebarW, TopBarH, W - SidebarW, H - TopBarH - BottomBarH, 1);
	Content = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Content"));
	ContentBorder->AddChild(Content);
}

// ---------------------------------------------------------------------------------------------------------------------
// Popup

ULcarsPopup* ULcarsPopup::Show(USlonkUIRoot* UI, const FString& InTitle, const FString& InMessage, const TArray<FString>& InButtons, TFunction<void(int32)> InOnChoice)
{
	if (!UI) { return nullptr; }
	return UI->PushPopup<ULcarsPopup>([&](ULcarsPopup& P)
	{
		P.Title = InTitle;
		P.Message = InMessage;
		P.Buttons = InButtons;
		P.OnChoice = MoveTemp(InOnChoice);
		P.Populate();
	});
}

void ULcarsPopup::Populate()
{
	if (!Content) { return; }
	Content->ClearChildren();
	SetTitle(Title);
	AddLine(Content, Message, 17.f, Lcars::Peach, false, false, 6.f);
	USpacer* Sp = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass());
	if (UVerticalBoxSlot* S = Content->AddChildToVerticalBox(Sp)) { S->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); }
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	if (UVerticalBoxSlot* S = Content->AddChildToVerticalBox(Row)) { S->SetHorizontalAlignment(HAlign_Right); }
	static const FLinearColor Colors[] = { Lcars::Orange, Lcars::Periwinkle, Lcars::Lavender, Lcars::Gold };
	for (int32 i = 0; i < Buttons.Num(); ++i)
	{
		const FVector4 Radii(Lcars::PillR, Lcars::PillR, Lcars::PillR, Lcars::PillR);
		UButton* B = Lcars::MakeButton(WidgetTree, Buttons[i], Colors[i % UE_ARRAY_COUNT(Colors)], nullptr, &Radii);
		USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Box->SetHeightOverride(Lcars::ButtonH); Box->SetMinDesiredWidth(150.f);
		Box->AddChild(B);
		if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(Box)) { S->SetPadding(FMargin(Lcars::Gap, 0.f, 0.f, 0.f)); }
		const int32 Index = i;
		Bind(B, [this, Index]() { TFunction<void(int32)> Cb = OnChoice; Close(); if (Cb) { Cb(Index); } }, Buttons[i]);
	}
}
