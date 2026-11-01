// Slonk - main menu, controls and science log panels.

#include "LcarsPanels.h"
#include "SlonkUIRoot.h"
#include "StarChartPanel.h"
#include "StarshipPawn.h"
#include "StarshipHUD.h"
#include "StarshipMovementComponent.h"
#include "WarpDriveComponent.h"
#include "StarshipWeaponsComponent.h"
#include "ShipSystemsComponent.h"
#include "SpaceTarget.h"
#include "SpaceVessel.h"
#include "BorgCube.h"
#include "Components/ProgressBar.h"
#include "StarSystem.h"
#include "ScienceLog.h"
#include "ScienceScannerComponent.h"
#include "SlonkSettings.h"
#include "Blueprint/WidgetTree.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Spacer.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"

// ---------------------------------------------------------------------------------------------------------------------
// Main menu

void UMainMenuPanel::BuildContent()
{
	AddSidebarButton(TEXT("Resume"), Lcars::Orange, [this]() { Close(); });
	AddSidebarButton(TEXT("Star Chart"), Lcars::Periwinkle, [this]() { if (USlonkUIRoot* UI = GetUIRoot()) { Close(); UI->PushMenu<UStarChartPanel>(); } });
	AddSidebarButton(TEXT("Science Log"), Lcars::Gold, [this]() { if (USlonkUIRoot* UI = GetUIRoot()) { Close(); UI->PushMenu<UScienceLogPanel>(); } });
	AddSidebarButton(TEXT("Engineering"), Lcars::Tan, [this]() { if (USlonkUIRoot* UI = GetUIRoot()) { Close(); UI->PushMenu<UEngineeringPanel>(); } });
	AddSidebarButton(TEXT("Tactical"), Lcars::Red, [this]() { if (USlonkUIRoot* UI = GetUIRoot()) { Close(); UI->PushMenu<UTacticalPanel>(); } });
	AddSidebarButton(TEXT("Controls"), Lcars::Lavender, [this]() { if (USlonkUIRoot* UI = GetUIRoot()) { Close(); UI->PushMenu<UControlsPanel>(); } });
	AddSidebarButton(TEXT("Settings"), Lcars::Tan, [this]() { if (USlonkUIRoot* UI = GetUIRoot()) { Close(); UI->PushMenu<USettingsPanel>(); } });
	AddSidebarButton(TEXT("Quit"), Lcars::Salmon, [this]()
	{
		ULcarsPopup::Show(GetUIRoot(), TEXT("End Program"), TEXT("Leave the ship and quit to the desktop?"), { TEXT("Quit"), TEXT("Stay") }, [this](int32 Choice)
		{
			if (Choice == 0)
			{
				if (APlayerController* PC = GetOwningPlayer()) { UKismetSystemLibrary::QuitGame(this, PC, EQuitPreference::Quit, false); }
			}
		});
	});

	AStarshipPawn* Ship = GetShip();
	AddLine(Content, Ship ? Ship->GetCraftName() : TEXT("No vessel"), 30.f, Lcars::Peach, true, true);
	AddLine(Content, TEXT("Status report"), 13.f, Lcars::Lilac, true, true, 2.f);
	USpacer* Sp = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass()); Sp->SetSize(FVector2D(1.f, 16.f)); Content->AddChildToVerticalBox(Sp);

	AddLiveRow(Content, TEXT("Shields"), [this]() { AStarshipPawn* S = GetShip(); return S ? FString::Printf(TEXT("%d%%"), FMath::RoundToInt(S->GetShieldFraction() * 100.f)) : FString(); });
	AddLiveRow(Content, TEXT("Hull"), [this]() { AStarshipPawn* S = GetShip(); return S ? FString::Printf(TEXT("%d%%"), FMath::RoundToInt(S->GetHullFraction() * 100.f)) : FString(); });
	AddLiveRow(Content, TEXT("Speed"), [this]()
	{
		AStarshipPawn* S = GetShip(); AStarshipHUD* H = GetHUD();
		if (!S || !S->Movement || !H) { return FString(); }
		if (S->WarpDrive && S->WarpDrive->IsWarpEngaged()) { return FString::Printf(TEXT("Warp %.1f"), S->WarpDrive->WarpFactor); }
		return H->FormatSpeed(S->Movement->Velocity.Size());
	});
	AddLiveRow(Content, TEXT("Star system"), [this]()
	{
		AStarshipPawn* S = GetShip();
		return S ? UScienceScannerComponent::SystemNameAt(S->GetWorld(), S->GetActorLocation()) : FString();
	});
	AddLiveRow(Content, TEXT("Nav target"), [this]()
	{
		AStarshipHUD* H = GetHUD(); AStarshipPawn* S = GetShip();
		FNavObject O;
		if (!H || !S || !H->GetNavTargetInfo(O)) { return FString(TEXT("none")); }
		return FString::Printf(TEXT("%s, %s"), *O.Label, *H->FormatDistance(FMath::Max(FVector::Dist(O.Location, S->GetActorLocation()) - O.RadiusCm, 0.0)));
	});
	AddLiveRow(Content, TEXT("Weapons lock"), [this]()
	{
		AStarshipPawn* S = GetShip();
		const AActor* T = (S && S->Weapons) ? S->Weapons->LockedTarget.Get() : nullptr;
		return T ? T->GetActorNameOrLabel() : FString(TEXT("none"));
	});
	AddLiveRow(Content, TEXT("Science"), [this]()
	{
		const UWorld* W = GetWorld();
		const UScienceLogSubsystem* Log = (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UScienceLogSubsystem>() : nullptr;
		return Log ? FString::Printf(TEXT("%d points, %s"), Log->TotalPoints(), *Log->RatingTitle()) : FString();
	}, Lcars::Gold);

	USpacer* Fill = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass());
	if (UVerticalBoxSlot* S = Content->AddChildToVerticalBox(Fill)) { S->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); }
	AddLine(Content, TEXT("Escape resumes.  M  star chart    /  science log    H  controls"), 12.f, Lcars::Dim, false, false);
}

// ---------------------------------------------------------------------------------------------------------------------
// Controls

void UControlsPanel::BuildContent()
{
	AddSidebarButton(TEXT("Close"), Lcars::Orange, [this]() { Close(); });
	AddSidebarButton(TEXT("Main Menu"), Lcars::Lavender, [this]() { if (USlonkUIRoot* UI = GetUIRoot()) { Close(); UI->PushMenu<UMainMenuPanel>(); } });

	struct FRow { const TCHAR* Key; const TCHAR* What; };
	static const FRow Flight[] = {
		{ TEXT("Mouse / left stick"), TEXT("steer") },
		{ TEXT("A  D / bumpers"), TEXT("roll") },
		{ TEXT("Q  E"), TEXT("yaw") },
		{ TEXT("W  S / wheel"), TEXT("throttle") },
		{ TEXT("1  2  3  4"), TEXT("quarter to full impulse") },
		{ TEXT("X or 0"), TEXT("all stop") },
		{ TEXT("Z"), TEXT("reverse") },
		{ TEXT("PgUp  PgDn"), TEXT("vertical thrusters") },
		{ TEXT("Arrows"), TEXT("RCS thrusters fore/aft, port/starboard") },
		{ TEXT("J / Y button"), TEXT("engage or disengage warp") },
		{ TEXT("[  ]  or  -  +"), TEXT("warp factor") },
		{ TEXT("O"), TEXT("orbit hold round the nav target or nearest body") },
		{ TEXT("P"), TEXT("autopilot to the nav target") },
		{ TEXT("Home"), TEXT("jump to the nav target (dev)") },
		{ TEXT("C"), TEXT("shuttlecraft: launch, or dock within 500 m of the bay") },
		{ TEXT("F"), TEXT("airlock: step out onto the hull, or out of a landed shuttle") },
	};
	static const FRow OnFoot[] = {
		{ TEXT("Mouse"), TEXT("look; in free flight, pitch and yaw") },
		{ TEXT("W  S,  A  D"), TEXT("walk, sidestep; floating, the pack's fore/aft and sideways jets") },
		{ TEXT("Shift"), TEXT("run") },
		{ TEXT("Q  E"), TEXT("turn; in free flight, roll") },
		{ TEXT("Space"), TEXT("jump, as high as the world's gravity allows; on a hull, push off") },
		{ TEXT("PgUp  PgDn"), TEXT("pack: up and down") },
		{ TEXT("X"), TEXT("pack: kill relative motion") },
		{ TEXT("B"), TEXT("magnetic boots on/off: armed, they hold any hull they touch") },
		{ TEXT("F"), TEXT("board: from the hull, or within 10 m of the hatch") },
		{ TEXT("V"), TEXT("third person / helmet view") },
	};
	static const FRow Systems[] = {
		{ TEXT("LMB / RT"), TEXT("phasers (hold)") },
		{ TEXT("Space / G / A button"), TEXT("photon torpedo") },
		{ TEXT("T"), TEXT("cycle weapons target") },
		{ TEXT("Y"), TEXT("clear weapons target") },
		{ TEXT("B"), TEXT("tractor beam on the locked or nav target") },
		{ TEXT("Enter"), TEXT("scan the locked or nav target; landed, survey the ground") },
		{ TEXT("L"), TEXT("next nav target") },
		{ TEXT("K"), TEXT("clear nav target") },
		{ TEXT("I"), TEXT("nav filter: all, stars, worlds, stations, ships, nebulae") },
		{ TEXT("N"), TEXT("nav markers") },
		{ TEXT("M"), TEXT("star chart") },
		{ TEXT("/"), TEXT("science log") },
		{ TEXT(",  ."), TEXT("engineering, tactical boards") },
		{ TEXT("5  6  7"), TEXT("condition green, yellow alert, red alert") },
		{ TEXT("H"), TEXT("this list") },
		{ TEXT("Escape"), TEXT("main menu") },
		{ TEXT("V"), TEXT("camera") },
		{ TEXT("RMB hold"), TEXT("free look; with the wheel, zoom") },
		{ TEXT("R"), TEXT("reset camera and zoom") },
		{ TEXT("Tab"), TEXT("HUD on/off") },
		{ TEXT("U"), TEXT("warp visuals: classic / relativistic") },
		{ TEXT("`"), TEXT("console:  ship <command>  (ship help)") },
	};

	UHorizontalBox* Columns = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	if (UVerticalBoxSlot* S = Content->AddChildToVerticalBox(Columns)) { S->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); }
	auto Column = [&](const TCHAR* Heading, const FRow* Rows, int32 N)
	{
		UVerticalBox* Col = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		if (UHorizontalBoxSlot* S = Columns->AddChildToHorizontalBox(Col)) { S->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); S->SetPadding(FMargin(0.f, 0.f, 24.f, 0.f)); }
		AddLine(Col, Heading, 16.f, Lcars::Peach, true, true, 0.f);
		USpacer* Sp = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass()); Sp->SetSize(FVector2D(1.f, 8.f)); Col->AddChildToVerticalBox(Sp);
		for (int32 i = 0; i < N; ++i) { AddRow(Col, Rows[i].Key, Rows[i].What, Lcars::Dim, 13.f); }
	};
	Column(TEXT("Helm"), Flight, UE_ARRAY_COUNT(Flight));
	Column(TEXT("On foot"), OnFoot, UE_ARRAY_COUNT(OnFoot));
	Column(TEXT("Tactical, science, navigation"), Systems, UE_ARRAY_COUNT(Systems));
}

// ---------------------------------------------------------------------------------------------------------------------
// Settings

void USettingsPanel::Light(FChoiceRow& Row)
{
	for (int32 i = 0; i < Row.Buttons.Num(); ++i)
	{
		const FVector4 Radii(Lcars::PillR, Lcars::PillR, Lcars::PillR, Lcars::PillR);
		Lcars::SetButtonColor(Row.Buttons[i], i == Row.Current ? Lcars::Orange : Lcars::Lilac, &Radii);
	}
}

void USettingsPanel::AddChoiceRow(const FString& Label, const TArray<FString>& Choices, int32 Current, TFunction<int32(int32)> OnPick)
{
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	USizeBox* Min = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	Min->SetMinDesiredWidth(200.f);
	Min->AddChild(Lcars::MakeText(WidgetTree, Label, 15.f, Lcars::Peach));
	if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(Min)) { S->SetVerticalAlignment(VAlign_Center); S->SetPadding(FMargin(0.f, 0.f, 16.f, 0.f)); }
	const int32 RowIndex = Rows.AddDefaulted();
	Rows[RowIndex].Current = Current;
	for (int32 i = 0; i < Choices.Num(); ++i)
	{
		const FVector4 Radii(Lcars::PillR, Lcars::PillR, Lcars::PillR, Lcars::PillR);
		UButton* B = Lcars::MakeButton(WidgetTree, Choices[i], Lcars::Lilac, nullptr, &Radii);
		USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Box->SetHeightOverride(Lcars::ButtonH); Box->SetMinDesiredWidth(170.f);
		Box->AddChild(B);
		if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(Box)) { S->SetPadding(FMargin(0.f, 0.f, Lcars::Gap, 0.f)); }
		Rows[RowIndex].Buttons.Add(B);
		const int32 Pick = i;
		Bind(B, [this, RowIndex, Pick, OnPick]()
		{
			if (!Rows.IsValidIndex(RowIndex)) { return; }
			Rows[RowIndex].Current = OnPick ? OnPick(Pick) : Pick;
			Light(Rows[RowIndex]);
		}, Choices[i]);
	}
	Light(Rows[RowIndex]);
	if (UVerticalBoxSlot* S = Content->AddChildToVerticalBox(Row)) { S->SetPadding(FMargin(0.f, 4.f, 0.f, 8.f)); }
}

void USettingsPanel::BuildContent()
{
	AddSidebarButton(TEXT("Close"), Lcars::Orange, [this]() { Close(); });
	AddSidebarButton(TEXT("Main Menu"), Lcars::Lavender, [this]() { if (USlonkUIRoot* UI = GetUIRoot()) { Close(); UI->PushMenu<UMainMenuPanel>(); } });

	AddLine(Content, TEXT("Display"), 13.f, Lcars::Lilac, true, true, 0.f);
	USlonkSettingsSubsystem* Settings = GetGameInstance() ? GetGameInstance()->GetSubsystem<USlonkSettingsSubsystem>() : nullptr;
	AddChoiceRow(TEXT("Typeface"), { TEXT("Standard"), TEXT("Condensed") }, Settings && Settings->GetTypeface() == ELcarsTypeface::Condensed ? 1 : 0, [this, Settings](int32 Pick)
	{
		if (Settings) { Settings->SetTypeface(Pick == 1 ? ELcarsTypeface::Condensed : ELcarsTypeface::Standard); }
		// Panels take their face when built: rebuild this one in the new type.
		if (USlonkUIRoot* UI = GetUIRoot()) { UI->CloseAll(); UI->PushMenu<USettingsPanel>(); }
		return Pick;
	});
	AddLine(Content, TEXT("Standard is the engine's Roboto; Condensed is Antonio, the compressed face in the spirit of the real displays."), 12.f, Lcars::Dim, false, false, 0.f);

	AddLine(Content, TEXT("HUD"), 13.f, Lcars::Lilac, true, true, 14.f);
	AStarshipHUD* H = GetHUD();
	AddChoiceRow(TEXT("Nav markers"), { TEXT("On"), TEXT("Off") }, H && H->bShowNavMarkers ? 0 : 1, [this](int32 Pick) { if (AStarshipHUD* Hud = GetHUD()) { Hud->bShowNavMarkers = Pick == 0; } return Pick; });
	AddChoiceRow(TEXT("Minimap"), { TEXT("On"), TEXT("Off") }, H && H->bShowMinimap ? 0 : 1, [this](int32 Pick) { if (AStarshipHUD* Hud = GetHUD()) { Hud->bShowMinimap = Pick == 0; } return Pick; });
	AddChoiceRow(TEXT("Key hints"), { TEXT("On"), TEXT("Off") }, H && H->bShowHelp ? 0 : 1, [this](int32 Pick) { if (AStarshipHUD* Hud = GetHUD()) { Hud->bShowHelp = Pick == 0; } return Pick; });
	AStarshipPawn* Ship = GetShip();
	AddChoiceRow(TEXT("Warp view"), { TEXT("Classic"), TEXT("Relativistic") }, Ship && Ship->GetWarpVisualMode() == EWarpVisualMode::Relativistic ? 1 : 0, [this](int32 Pick) { if (AStarshipPawn* S = GetShip()) { S->SetWarpVisualMode(Pick == 1 ? EWarpVisualMode::Relativistic : EWarpVisualMode::Classic); } return Pick; });
	AddLine(Content, TEXT("The typeface is kept between sessions; the HUD switches last for this flight (N, M's sidebar and U also flip them)."), 12.f, Lcars::Dim, false, false, 6.f);
}

// ---------------------------------------------------------------------------------------------------------------------
// Science log

void UScienceLogPanel::BuildContent()
{
	AddSidebarButton(TEXT("Close"), Lcars::Orange, [this]() { Close(); });
	FilterButton = AddSidebarButton(TEXT("All Records"), Lcars::Gold, [this]()
	{
		bDiscoveriesOnly = !bDiscoveriesOnly;
		if (UTextBlock* T = FilterButton ? Cast<UTextBlock>(FilterButton->GetChildAt(0)) : nullptr) { T->SetText(FText::FromString(bDiscoveriesOnly ? TEXT("DISCOVERIES") : TEXT("ALL RECORDS"))); }
		FillRecords();
	});
	AddSidebarButton(TEXT("Star Chart"), Lcars::Periwinkle, [this]() { if (USlonkUIRoot* UI = GetUIRoot()) { Close(); UI->PushMenu<UStarChartPanel>(); } });
	AddSidebarButton(TEXT("Main Menu"), Lcars::Lavender, [this]() { if (USlonkUIRoot* UI = GetUIRoot()) { Close(); UI->PushMenu<UMainMenuPanel>(); } });
	AddSidebarButton(TEXT("Erase Log"), Lcars::Salmon, [this]()
	{
		ULcarsPopup::Show(GetUIRoot(), TEXT("Erase Science Log"), TEXT("Forget every scan on record? The save file goes too."), { TEXT("Erase"), TEXT("Keep") }, [this](int32 Choice)
		{
			if (Choice != 0) { return; }
			if (UScienceLogSubsystem* Log = GetGameInstance() ? GetGameInstance()->GetSubsystem<UScienceLogSubsystem>() : nullptr) { Log->Reset(); }
			FillRecords();
		});
	});

	const UScienceLogSubsystem* Log = GetGameInstance() ? GetGameInstance()->GetSubsystem<UScienceLogSubsystem>() : nullptr;
	AddLiveRow(Content, TEXT("Rating"), [this]()
	{
		const UScienceLogSubsystem* L = GetGameInstance() ? GetGameInstance()->GetSubsystem<UScienceLogSubsystem>() : nullptr;
		if (!L) { return FString(); }
		const int32 Next = L->NextRatingPoints();
		return Next > 0 ? FString::Printf(TEXT("%s, %d points  (%d to the next rating)"), *L->RatingTitle(), L->TotalPoints(), Next - L->TotalPoints())
						: FString::Printf(TEXT("%s, %d points"), *L->RatingTitle(), L->TotalPoints());
	}, Lcars::Gold, 16.f);
	AddLiveRow(Content, TEXT("Survey"), [this]()
	{
		AStarshipPawn* S = GetShip();
		const UScienceLogSubsystem* L = GetGameInstance() ? GetGameInstance()->GetSubsystem<UScienceLogSubsystem>() : nullptr;
		if (!S || !L) { return FString(); }
		FString System; TArray<FString> Keys;
		UScienceScannerComponent::SurveyKeys(S->GetWorld(), S->GetActorLocation(), System, Keys);
		if (Keys.Num() == 0) { return FString::Printf(TEXT("%s: no survey here"), *System); }
		TArray<FString> Missing;
		for (const FString& K : Keys) { if (!L->IsScanned(K)) { FString Sys, Label; K.Split(TEXT("/"), &Sys, &Label); Missing.Add(Label); } }
		return FString::Printf(TEXT("%s %d/%d%s"), *System, Keys.Num() - Missing.Num(), Keys.Num(), Missing.Num() ? *(TEXT(", still to scan: ") + FString::Join(Missing, TEXT(", "))) : TEXT(" complete"));
	}, Lcars::Peach, 14.f);
	USpacer* Sp = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass()); Sp->SetSize(FVector2D(1.f, 10.f)); Content->AddChildToVerticalBox(Sp);

	List = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("Records"));
	List->SetScrollBarVisibility(ESlateVisibility::Visible);
	if (UVerticalBoxSlot* S = Content->AddChildToVerticalBox(List)) { S->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); }
	FillRecords();
	(void)Log;
}

void UScienceLogPanel::FillRecords()
{
	if (!List) { return; }
	List->ClearChildren();
	const UScienceLogSubsystem* Log = GetGameInstance() ? GetGameInstance()->GetSubsystem<UScienceLogSubsystem>() : nullptr;
	if (!Log) { return; }
	const TArray<FScanRecord>& Recs = Log->GetRecords();
	int32 Shown = 0;
	for (int32 i = Recs.Num() - 1; i >= 0; --i)
	{
		const FScanRecord& R = Recs[i];
		if (bDiscoveriesOnly && R.Discovery.IsEmpty()) { continue; }
		++Shown;
		UVerticalBox* Entry = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		UHorizontalBox* Head = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		Entry->AddChildToVerticalBox(Head);
		FLinearColor KindColor = Lcars::Sky;
		switch (R.Kind)
		{
		case EScanKind::Star: KindColor = Lcars::Gold; break;
		case EScanKind::Planet: KindColor = Lcars::Sky; break;
		case EScanKind::Moon: KindColor = Lcars::Lilac; break;
		case EScanKind::Station: KindColor = Lcars::Peach; break;
		case EScanKind::Ship: KindColor = Lcars::Periwinkle; break;
		case EScanKind::Hostile: KindColor = Lcars::Red; break;
		case EScanKind::Belt: KindColor = Lcars::Tan; break;
		case EScanKind::Nebula: KindColor = Lcars::Lavender; break;
		case EScanKind::Surface: KindColor = Lcars::Orange; break;
		}
		USizeBox* KindBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		KindBox->SetWidthOverride(92.f);
		KindBox->AddChild(Lcars::MakeText(WidgetTree, UScienceLogSubsystem::KindName(R.Kind), 12.f, KindColor));
		if (UHorizontalBoxSlot* S = Head->AddChildToHorizontalBox(KindBox)) { S->SetVerticalAlignment(VAlign_Center); }
		if (UHorizontalBoxSlot* S = Head->AddChildToHorizontalBox(Lcars::MakeText(WidgetTree, R.Label, 16.f, FLinearColor::White, true, false))) { S->SetPadding(FMargin(0.f, 0.f, 10.f, 0.f)); S->SetVerticalAlignment(VAlign_Center); }
		if (UHorizontalBoxSlot* S = Head->AddChildToHorizontalBox(Lcars::MakeText(WidgetTree, R.System, 12.f, Lcars::Dim, false, false))) { S->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); S->SetVerticalAlignment(VAlign_Center); }
		if (UHorizontalBoxSlot* S = Head->AddChildToHorizontalBox(Lcars::MakeText(WidgetTree, FString::Printf(TEXT("+%d"), R.Total()), 16.f, Lcars::Orange))) { S->SetHorizontalAlignment(HAlign_Right); S->SetVerticalAlignment(VAlign_Center); }
		FString Detail = FString::Join(R.Facts, TEXT("  ·  "));
		if (!Detail.IsEmpty())
		{
			UTextBlock* D = Lcars::MakeText(WidgetTree, Detail, 12.f, Lcars::Dim, false, false);
			D->SetAutoWrapText(true);
			if (UVerticalBoxSlot* S = Entry->AddChildToVerticalBox(D)) { S->SetPadding(FMargin(92.f, 0.f, 0.f, 0.f)); }
		}
		if (!R.Discovery.IsEmpty())
		{
			UTextBlock* D = Lcars::MakeText(WidgetTree, TEXT("Discovery: ") + R.Discovery, 13.f, Lcars::Gold, true, false);
			if (UVerticalBoxSlot* S = Entry->AddChildToVerticalBox(D)) { S->SetPadding(FMargin(92.f, 0.f, 0.f, 0.f)); }
		}
		if (R.BonusNotes.Num() > 0)
		{
			UTextBlock* D = Lcars::MakeText(WidgetTree, FString::Join(R.BonusNotes, TEXT(", ")), 11.f, Lcars::Lilac, false, false);
			if (UVerticalBoxSlot* S = Entry->AddChildToVerticalBox(D)) { S->SetPadding(FMargin(92.f, 0.f, 0.f, 0.f)); }
		}
		if (UScrollBoxSlot* S = Cast<UScrollBoxSlot>(List->AddChild(Entry))) { S->SetPadding(FMargin(0.f, 4.f, 12.f, 8.f)); }
	}
	if (Shown == 0)
	{
		UTextBlock* T = Lcars::MakeText(WidgetTree, bDiscoveriesOnly ? TEXT("No discoveries on record yet.") : TEXT("Nothing catalogued yet. Pick a nav target (L), close to sensor range and press Enter."), 14.f, Lcars::Dim, false, false);
		List->AddChild(T);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Shared bits for the crew boards

namespace
{
	FLinearColor AlertColor(EAlertState A) { return A == EAlertState::Red ? Lcars::Red : A == EAlertState::Yellow ? Lcars::Gold : FLinearColor(0.45f, 0.85f, 0.5f); }
	FLinearColor IntegrityColor(float I) { return I > 0.66f ? FLinearColor(0.45f, 0.85f, 0.5f) : I > 0.33f ? Lcars::Gold : Lcars::Red; }
	FString KmText(double Cm)
	{
		const double Km = Cm / 1e5;
		if (Km >= 1e6) { return FString::Printf(TEXT("%.2f Mkm"), Km / 1e6); }
		if (Km >= 1000.0) { return FString::Printf(TEXT("%.0f km"), Km); }
		return FString::Printf(TEXT("%.1f km"), Km);
	}
	UProgressBar* MakeBar(UWidgetTree* Tree, const FLinearColor& Fill)
	{
		UProgressBar* Bar = Tree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass());
		FProgressBarStyle Style = Bar->GetWidgetStyle();
		Style.BackgroundImage = Lcars::Rounded(FLinearColor(0.12f, 0.12f, 0.16f), FVector4(4.f, 4.f, 4.f, 4.f));
		Style.FillImage = Lcars::Rounded(FLinearColor::White, FVector4(4.f, 4.f, 4.f, 4.f));
		Bar->SetWidgetStyle(Style);
		Bar->SetFillColorAndOpacity(Fill);
		Bar->SetPercent(1.f);
		return Bar;
	}
	void FillBridgeLog(ULcarsPanel* Panel, UWidgetTree* Tree, UScrollBox* Box, const UShipSystemsComponent* Sys, int32 MaxLines)
	{
		if (!Box || !Sys) { return; }
		Box->ClearChildren();
		const TArray<FBridgeLogEntry>& Log = Sys->GetLog();
		const int32 First = FMath::Max(0, Log.Num() - MaxLines);
		for (int32 i = Log.Num() - 1; i >= First; --i)
		{
			const FBridgeLogEntry& E = Log[i];
			UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
			const int32 M = (int32)E.Time / 60, S = (int32)E.Time % 60;
			USizeBox* TBox = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass()); TBox->SetWidthOverride(58.f);
			TBox->AddChild(Lcars::MakeText(Tree, FString::Printf(TEXT("%02d:%02d"), M, S), 12.f, Lcars::Dim, false, false));
			Row->AddChildToHorizontalBox(TBox);
			USizeBox* NBox = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass()); NBox->SetWidthOverride(190.f);
			NBox->AddChild(Lcars::MakeText(Tree, E.Speaker, 12.f, E.Color, true, false));
			Row->AddChildToHorizontalBox(NBox);
			UTextBlock* T = Lcars::MakeText(Tree, E.Text, 12.f, FLinearColor::White, false, false);
			T->SetAutoWrapText(true);
			if (UHorizontalBoxSlot* Sl = Row->AddChildToHorizontalBox(T)) { Sl->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); }
			if (UScrollBoxSlot* Sl = Cast<UScrollBoxSlot>(Box->AddChild(Row))) { Sl->SetPadding(FMargin(0.f, 2.f, 0.f, 2.f)); }
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Engineering

void UEngineeringPanel::BuildContent()
{
	AddSidebarButton(TEXT("Close"), Lcars::Orange, [this]() { Close(); });
	AddSidebarButton(TEXT("Balance Power"), Lcars::Tan, [this]() { if (AStarshipPawn* S = GetShip()) { if (S->Systems) { S->Systems->BalancePower(); } } });
	AutoRepairButton = AddSidebarButton(TEXT("Auto Repair"), Lcars::Peach, [this]() { if (AStarshipPawn* S = GetShip()) { if (S->Systems) { S->Systems->SetRepairPriority(EShipSystem::Count); } } });
	AddSidebarButton(TEXT("Status Report"), Lcars::Lilac, [this]() { if (AStarshipPawn* S = GetShip()) { if (S->Systems) { FString R; S->Systems->HandleOrder({ TEXT("order"), TEXT("report") }, R); } } });
	AddSidebarButton(TEXT("Tactical"), Lcars::Red, [this]() { if (USlonkUIRoot* UI = GetUIRoot()) { Close(); UI->PushMenu<UTacticalPanel>(); } });
	AddSidebarButton(TEXT("Main Menu"), Lcars::Lavender, [this]() { if (USlonkUIRoot* UI = GetUIRoot()) { Close(); UI->PushMenu<UMainMenuPanel>(); } });

	AStarshipPawn* Ship = GetShip();
	UShipSystemsComponent* Sys = Ship ? Ship->Systems.Get() : nullptr;
	const FOfficer* Chief = Sys ? Sys->OfficerAt(EStation::Engineering) : nullptr;
	AddRow(Content, TEXT("Chief Engineer"), Chief ? FString::Printf(TEXT("%s %s"), *Chief->Rank, *Chief->Name) : TEXT("post empty"), Lcars::Peach, 15.f);
	AddLiveRow(Content, TEXT("Condition"), [this]() { AStarshipPawn* S = GetShip(); return S && S->Systems ? UShipSystemsComponent::AlertName(S->Systems->GetAlert()) : FString(); }, Lcars::Gold, 15.f);
	AddLiveRow(Content, TEXT("Hull"), [this]() { AStarshipPawn* S = GetShip(); return S ? FString::Printf(TEXT("%d%%"), FMath::RoundToInt(S->GetHullFraction() * 100.f)) : FString(); }, Lcars::Peach, 15.f);

	// Column heads.
	{
		UHorizontalBox* Head = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		auto Cell = [&](const TCHAR* Text, float W) { USizeBox* B = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass()); B->SetWidthOverride(W); B->AddChild(Lcars::MakeText(WidgetTree, Text, 12.f, Lcars::Dim)); Head->AddChildToHorizontalBox(B); };
		Cell(TEXT("System"), 190.f); Cell(TEXT("Integrity"), 360.f); Cell(TEXT("Power"), 90.f); Cell(TEXT("Orders"), 300.f);
		if (UVerticalBoxSlot* S = Content->AddChildToVerticalBox(Head)) { S->SetPadding(FMargin(0.f, 10.f, 0.f, 4.f)); }
	}
	Rows.SetNum((int32)EShipSystem::Count);
	for (int32 i = 0; i < (int32)EShipSystem::Count; ++i)
	{
		const EShipSystem Sy = (EShipSystem)i;
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		USizeBox* NameBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass()); NameBox->SetWidthOverride(190.f);
		NameBox->AddChild(Lcars::MakeText(WidgetTree, UShipSystemsComponent::SystemName(Sy), 14.f, Lcars::Peach));
		if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(NameBox)) { S->SetVerticalAlignment(VAlign_Center); }
		// integrity: bar + number
		UHorizontalBox* IBox = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		USizeBox* BarBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass()); BarBox->SetWidthOverride(200.f); BarBox->SetHeightOverride(14.f);
		Rows[i].Bar = MakeBar(WidgetTree, IntegrityColor(1.f));
		BarBox->AddChild(Rows[i].Bar);
		if (UHorizontalBoxSlot* S = IBox->AddChildToHorizontalBox(BarBox)) { S->SetVerticalAlignment(VAlign_Center); S->SetPadding(FMargin(0.f, 0.f, 10.f, 0.f)); }
		UTextBlock* Pct = Lcars::MakeText(WidgetTree, TEXT("100%"), 14.f, FLinearColor::White, true, false);
		if (UHorizontalBoxSlot* S = IBox->AddChildToHorizontalBox(Pct)) { S->SetVerticalAlignment(VAlign_Center); }
		Live.Add({ Pct, [this, Sy]() { AStarshipPawn* S = GetShip(); if (!S || !S->Systems) { return FString(); } const FShipSystemState& St = S->Systems->GetSystem(Sy); return FString::Printf(TEXT("%d%%%s"), FMath::RoundToInt(St.Integrity * 100.f), St.bRepairing ? TEXT("  team on it") : TEXT("")); }, FString() });
		USizeBox* IWrap = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass()); IWrap->SetWidthOverride(360.f); IWrap->AddChild(IBox);
		if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(IWrap)) { S->SetVerticalAlignment(VAlign_Center); }
		// power
		UTextBlock* Pow = Lcars::MakeText(WidgetTree, TEXT("100%"), 14.f, FLinearColor::White, true, false);
		Live.Add({ Pow, [this, Sy]() { AStarshipPawn* S = GetShip(); return S && S->Systems ? FString::Printf(TEXT("%d%%"), FMath::RoundToInt(S->Systems->GetSystem(Sy).Power * 100.f)) : FString(); }, FString() });
		USizeBox* PWrap = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass()); PWrap->SetWidthOverride(90.f); PWrap->AddChild(Pow);
		if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(PWrap)) { S->SetVerticalAlignment(VAlign_Center); }
		// orders
		const FVector4 Radii(Lcars::PillR, Lcars::PillR, Lcars::PillR, Lcars::PillR);
		auto Small = [&](const FString& Label, const FLinearColor& Col, TFunction<void()> Fn) -> UButton*
		{
			UButton* B = Lcars::MakeButton(WidgetTree, Label, Col, nullptr, &Radii);
			USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass()); Box->SetHeightOverride(30.f); Box->SetMinDesiredWidth(130.f);
			Box->AddChild(B);
			if (UHorizontalBoxSlot* S = Row->AddChildToHorizontalBox(Box)) { S->SetPadding(FMargin(0.f, 0.f, Lcars::Gap, 0.f)); S->SetVerticalAlignment(VAlign_Center); }
			Bind(B, MoveTemp(Fn), Label);
			return B;
		};
		const FString Short = UShipSystemsComponent::SystemName(Sy);
		Rows[i].PowerButton = Small(FString::Printf(TEXT("Power to %s"), *Short), Lcars::Lilac, [this, Sy]() { if (AStarshipPawn* S = GetShip()) { if (S->Systems) { S->Systems->DivertPower(Sy); } } });
		Rows[i].RepairButton = Small(FString::Printf(TEXT("Repair %s"), *Short), Lcars::Lilac, [this, Sy]() { if (AStarshipPawn* S = GetShip()) { if (S->Systems) { S->Systems->SetRepairPriority(Sy); } } });
		if (UVerticalBoxSlot* S = Content->AddChildToVerticalBox(Row)) { S->SetPadding(FMargin(0.f, 3.f, 0.f, 3.f)); }
	}

	AddLine(Content, TEXT("Bridge log"), 14.f, Lcars::Peach, true, true, 14.f);
	LogBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
	if (UVerticalBoxSlot* S = Content->AddChildToVerticalBox(LogBox)) { S->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); }
}

void UEngineeringPanel::Refresh()
{
	AStarshipPawn* Ship = GetShip();
	UShipSystemsComponent* Sys = Ship ? Ship->Systems.Get() : nullptr;
	if (!Sys) { return; }
	const FVector4 Radii(Lcars::PillR, Lcars::PillR, Lcars::PillR, Lcars::PillR);
	const EShipSystem Priority = Sys->GetRepairPriority();
	for (int32 i = 0; i < Rows.Num(); ++i)
	{
		FSystemRow& R = Rows[i];
		const FShipSystemState& St = Sys->GetSystem((EShipSystem)i);
		if (R.Bar && FMath::Abs(St.Integrity - R.LastIntegrity) > 0.002f) { R.LastIntegrity = St.Integrity; R.Bar->SetPercent(St.Integrity); R.Bar->SetFillColorAndOpacity(IntegrityColor(St.Integrity)); }
		const bool bPow = St.Power > 1.01f, bRep = Priority == (EShipSystem)i;
		if (R.PowerButton && bPow != R.bLastPower) { R.bLastPower = bPow; Lcars::SetButtonColor(R.PowerButton, bPow ? Lcars::Orange : Lcars::Lilac, &Radii); }
		if (R.RepairButton && bRep != R.bLastRepair) { R.bLastRepair = bRep; Lcars::SetButtonColor(R.RepairButton, bRep ? Lcars::Orange : Lcars::Lilac, &Radii); }
	}
	if (AutoRepairButton) { Lcars::SetButtonColor(AutoRepairButton, Priority == EShipSystem::Count ? Lcars::Orange : Lcars::Peach, nullptr); }
	if (Sys->GetLog().Num() != LastLogCount) { LastLogCount = Sys->GetLog().Num(); FillBridgeLog(this, WidgetTree, LogBox, Sys, 12); }
}

// ---------------------------------------------------------------------------------------------------------------------
// Tactical

void UTacticalPanel::BuildContent()
{
	AddSidebarButton(TEXT("Close"), Lcars::Orange, [this]() { Close(); });
	AlertButtons.Add(AddSidebarButton(TEXT("Red Alert"), Lcars::Red, [this]() { if (AStarshipPawn* S = GetShip()) { if (S->Systems) { S->Systems->SetAlert(EAlertState::Red); } } }));
	AlertButtons.Add(AddSidebarButton(TEXT("Yellow Alert"), Lcars::Gold, [this]() { if (AStarshipPawn* S = GetShip()) { if (S->Systems) { S->Systems->SetAlert(EAlertState::Yellow); } } }));
	AlertButtons.Add(AddSidebarButton(TEXT("Condition Green"), FLinearColor(0.45f, 0.85f, 0.5f), [this]() { if (AStarshipPawn* S = GetShip()) { if (S->Systems) { S->Systems->SetAlert(EAlertState::Green); } } }));
	AddSidebarButton(TEXT("Next Target"), Lcars::Periwinkle, [this]()
	{
		AStarshipPawn* S = GetShip(); if (!S || !S->Weapons) { return; }
		S->Weapons->CycleTarget();
		if (S->Systems) { S->Systems->Report(EStation::Tactical, S->Weapons->LockedTarget ? FString::Printf(TEXT("Target locked: %s."), *(Cast<ASpaceVessel>(S->Weapons->LockedTarget) ? Cast<ASpaceVessel>(S->Weapons->LockedTarget)->GetDisplayName() : S->Weapons->LockedTarget->GetActorNameOrLabel())) : TEXT("No targets in range.")); }
	});
	AddSidebarButton(TEXT("Lock Nearest"), Lcars::Periwinkle, [this]() { AStarshipPawn* S = GetShip(); if (S && S->Weapons) { S->Weapons->LockNearest(); } });
	AddSidebarButton(TEXT("Clear Target"), Lcars::Lilac, [this]() { AStarshipPawn* S = GetShip(); if (S && S->Weapons) { S->Weapons->ClearTarget(); } });
	AddSidebarButton(TEXT("Fire Torpedo"), Lcars::Salmon, [this]()
	{
		AStarshipPawn* S = GetShip(); if (!S || !S->Weapons) { return; }
		const bool bFired = S->Weapons->FireTorpedo();
		if (S->Systems) { S->Systems->Report(EStation::Tactical, bFired ? TEXT("Torpedo away.") : TEXT("Tubes reloading.")); }
	});
	AddSidebarButton(TEXT("Engineering"), Lcars::Tan, [this]() { if (USlonkUIRoot* UI = GetUIRoot()) { Close(); UI->PushMenu<UEngineeringPanel>(); } });
	AddSidebarButton(TEXT("Main Menu"), Lcars::Lavender, [this]() { if (USlonkUIRoot* UI = GetUIRoot()) { Close(); UI->PushMenu<UMainMenuPanel>(); } });

	AStarshipPawn* Ship = GetShip();
	UShipSystemsComponent* Sys = Ship ? Ship->Systems.Get() : nullptr;
	const FOfficer* Tac = Sys ? Sys->OfficerAt(EStation::Tactical) : nullptr;
	AddRow(Content, TEXT("Tactical Officer"), Tac ? FString::Printf(TEXT("%s %s"), *Tac->Rank, *Tac->Name) : TEXT("post empty"), Lcars::Peach, 15.f);
	AddLiveRow(Content, TEXT("Condition"), [this]() { AStarshipPawn* S = GetShip(); return S && S->Systems ? UShipSystemsComponent::AlertName(S->Systems->GetAlert()) : FString(); }, Lcars::Gold, 15.f);
	AddLiveRow(Content, TEXT("Shields"), [this]()
	{
		AStarshipPawn* S = GetShip(); if (!S) { return FString(); }
		const FShipSystemState& St = S->Systems ? S->Systems->GetSystem(EShipSystem::Shields) : FShipSystemState();
		return FString::Printf(TEXT("%d%%  of a %.0f point grid   emitters %d%%   power %d%%"), FMath::RoundToInt(S->GetShieldFraction() * 100.f), S->MaxShieldStrength, FMath::RoundToInt(St.Integrity * 100.f), FMath::RoundToInt(St.Power * 100.f));
	});
	AddLiveRow(Content, TEXT("Hull"), [this]() { AStarshipPawn* S = GetShip(); return S ? FString::Printf(TEXT("%d%%"), FMath::RoundToInt(S->GetHullFraction() * 100.f)) : FString(); });
	AddLiveRow(Content, TEXT("Phasers"), [this]()
	{
		AStarshipPawn* S = GetShip(); if (!S || !S->Weapons) { return FString(); }
		const FShipSystemState& St = S->Systems ? S->Systems->GetSystem(EShipSystem::Phasers) : FShipSystemState();
		return FString::Printf(TEXT("charge %d%%   %.0f per second   banks %d%%%s"), FMath::RoundToInt(S->Weapons->GetPhaserEnergy() * 100.f), S->Weapons->PhaserDamagePerSecond, FMath::RoundToInt(St.Integrity * 100.f), S->Weapons->IsFiringPhasers() ? TEXT("   FIRING") : TEXT(""));
	});
	AddLiveRow(Content, TEXT("Torpedoes"), [this]()
	{
		AStarshipPawn* S = GetShip(); if (!S || !S->Weapons) { return FString(); }
		FString Tubes;
		for (int32 t = 0; t < S->Weapons->GetNumTubes(); ++t) { const float R = S->Weapons->GetTubeReadiness(t); Tubes += FString::Printf(TEXT("%stube %d %s"), t ? TEXT("   ") : TEXT(""), t + 1, R >= 1.f ? TEXT("ready") : *FString::Printf(TEXT("%d%%"), FMath::RoundToInt(R * 100.f))); }
		return FString::Printf(TEXT("%s   fired %d"), *Tubes, S->Weapons->GetTorpedoesFired());
	});
	AddLiveRow(Content, TEXT("Target"), [this]()
	{
		AStarshipPawn* S = GetShip(); if (!S || !S->Weapons) { return FString(); }
		AActor* T = S->Weapons->LockedTarget;
		if (!T) { return FString(TEXT("none locked")); }
		const ASpaceVessel* V = Cast<ASpaceVessel>(T);
		const ABorgCube* Borg = Cast<ABorgCube>(T);
		FString Name = V ? V->GetDisplayName() : T->GetActorNameOrLabel();
		FString Extra = V ? FString::Printf(TEXT("   hull %d%%"), FMath::RoundToInt(100.f * V->Health / FMath::Max(V->MaxHealth, 1.f))) : FString();
		if (Borg && Borg->IsHostile()) { Extra += TEXT("   HOSTILE"); }
		return FString::Printf(TEXT("%s   %s%s"), *Name, *KmText(FVector::Dist(T->GetActorLocation(), S->GetActorLocation())), *Extra);
	}, Lcars::Orange);

	AddLine(Content, TEXT("Contacts"), 14.f, Lcars::Peach, true, true, 14.f);
	Contacts = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
	if (UVerticalBoxSlot* S = Content->AddChildToVerticalBox(Contacts)) { S->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); }
	FillContacts();
}

void UTacticalPanel::FillContacts()
{
	AStarshipPawn* S = GetShip();
	if (!Contacts || !S) { return; }
	Contacts->ClearChildren();
	TArray<AActor*> Found;
	UGameplayStatics::GetAllActorsWithTag(S->GetWorld(), ASpaceTarget::TargetableTag, Found);
	struct FC { AActor* A; double D; };
	TArray<FC> List;
	const double Reach = S->Weapons ? S->Weapons->TargetScanRange * 20.0 : 3e9;		// 3,000 km
	for (AActor* A : Found)
	{
		if (!A || A == S) { continue; }
		const double D = FVector::Dist(A->GetActorLocation(), S->GetActorLocation());
		if (D <= Reach) { List.Add({ A, D }); }
	}
	List.Sort([](const FC& a, const FC& b) { return a.D < b.D; });
	for (int32 i = 0; i < FMath::Min(List.Num(), 14); ++i)
	{
		AActor* A = List[i].A;
		const ASpaceVessel* V = Cast<ASpaceVessel>(A);
		const ABorgCube* Borg = Cast<ABorgCube>(A);
		const bool bHostile = Borg && Borg->IsHostile();
		const bool bLocked = S->Weapons && S->Weapons->LockedTarget == A;
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		USizeBox* NBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass()); NBox->SetWidthOverride(320.f);
		NBox->AddChild(Lcars::MakeText(WidgetTree, (bLocked ? TEXT("> ") : TEXT("")) + (V ? V->GetDisplayName() : A->GetActorNameOrLabel()), 13.f, bHostile ? Lcars::Red : bLocked ? Lcars::Orange : FLinearColor::White, true, false));
		Row->AddChildToHorizontalBox(NBox);
		USizeBox* DBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass()); DBox->SetWidthOverride(120.f);
		DBox->AddChild(Lcars::MakeText(WidgetTree, KmText(List[i].D), 13.f, Lcars::Dim, false, false));
		Row->AddChildToHorizontalBox(DBox);
		// bearing: ahead / port / starboard / astern, from the ship's own axes
		const FVector L = S->GetActorTransform().InverseTransformPosition(A->GetActorLocation()).GetSafeNormal();
		const FString Bearing = FMath::Abs(L.X) > 0.7 ? (L.X > 0 ? TEXT("ahead") : TEXT("astern")) : (L.Y > 0 ? TEXT("starboard") : TEXT("port"));
		FString Kind = Borg ? TEXT("Borg") : Cast<ASpaceStation>(A) ? TEXT("station") : V ? TEXT("vessel") : TEXT("contact");
		if (bHostile) { Kind += TEXT("   HOSTILE"); }
		if (V) { Kind += FString::Printf(TEXT("   hull %d%%"), FMath::RoundToInt(100.f * V->Health / FMath::Max(V->MaxHealth, 1.f))); }
		Row->AddChildToHorizontalBox(Lcars::MakeText(WidgetTree, FString::Printf(TEXT("%s   %s"), *Bearing, *Kind), 13.f, Lcars::Dim, false, false));
		if (UScrollBoxSlot* Sl = Cast<UScrollBoxSlot>(Contacts->AddChild(Row))) { Sl->SetPadding(FMargin(0.f, 2.f, 0.f, 2.f)); }
	}
	if (List.Num() == 0) { Contacts->AddChild(Lcars::MakeText(WidgetTree, TEXT("no contacts within sensor reach"), 12.f, Lcars::Dim, false, false)); }
}

void UTacticalPanel::Refresh()
{
	AStarshipPawn* S = GetShip();
	UShipSystemsComponent* Sys = S ? S->Systems.Get() : nullptr;
	if (Sys && (int32)Sys->GetAlert() != LastAlert)
	{
		LastAlert = (int32)Sys->GetAlert();
		const FLinearColor Cols[3] = { Lcars::Red, Lcars::Gold, FLinearColor(0.45f, 0.85f, 0.5f) };
		const int32 Lit = Sys->GetAlert() == EAlertState::Red ? 0 : Sys->GetAlert() == EAlertState::Yellow ? 1 : 2;
		for (int32 i = 0; i < AlertButtons.Num(); ++i) { if (AlertButtons[i]) { Lcars::SetButtonColor(AlertButtons[i], i == Lit ? Lcars::Brighter(Cols[i], 0.25f) : Lcars::Darker(Cols[i], 0.45f), nullptr); } }
	}
	ContactsTimer -= GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.016f;
	if (ContactsTimer <= 0.f) { ContactsTimer = 0.5f; FillContacts(); }
}
