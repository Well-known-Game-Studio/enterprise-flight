// Slonk - main menu, controls and science log panels.

#include "LcarsPanels.h"
#include "SlonkUIRoot.h"
#include "StarChartPanel.h"
#include "StarshipPawn.h"
#include "StarshipHUD.h"
#include "StarshipMovementComponent.h"
#include "WarpDriveComponent.h"
#include "StarshipWeaponsComponent.h"
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
