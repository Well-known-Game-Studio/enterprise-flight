// Slonk - the missions panel.

#include "MissionPanel.h"
#include "LcarsTheme.h"
#include "LcarsPanels.h"
#include "CrewPanel.h"
#include "SlonkUIRoot.h"
#include "StarshipPawn.h"
#include "StarshipHUD.h"
#include "CrewRoster.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Engine/GameInstance.h"

UFleetSubsystem* UMissionPanel::Fleet() const
{
	const UWorld* W = GetWorld();
	return W && W->GetGameInstance() ? W->GetGameInstance()->GetSubsystem<UFleetSubsystem>() : nullptr;
}

UMissionSubsystem* UMissionPanel::Missions() const
{
	const UWorld* W = GetWorld();
	return W && W->GetGameInstance() ? W->GetGameInstance()->GetSubsystem<UMissionSubsystem>() : nullptr;
}

void UMissionPanel::Say(const FString& Text, bool bGood, float Seconds) const
{
	if (AStarshipPawn* S = GetShip()) { S->ScreenMessage(Text, bGood ? FColor::Cyan : FColor::Orange, Seconds); }
}

void UMissionPanel::BuildContent()
{
	AddSidebarButton(TEXT("Close"), Lcars::Orange, [this]() { Close(); });
	ShowButton = AddSidebarButton(TEXT("Show: Board"), Lcars::Gold, [this]() { Show = (Show + 1) % 4; });
	AddSidebarButton(TEXT("Accept"), Lcars::Peach, [this]()
	{
		UMissionSubsystem* M = Missions(); if (!M) { return; }
		int32 Id = SelectedId;
		if (Show == 1 && M->GetInbox().IsValidIndex(SelectedMessage)) { Id = M->GetInbox()[SelectedMessage].MissionId; }
		FString Reply; const bool bOk = M->Accept(Id, Reply); Say(Reply, bOk); if (bOk) { SelectedId = Id; Show = 0; }
	});
	AddSidebarButton(TEXT("Abandon"), Lcars::Salmon, [this]()
	{
		UMissionSubsystem* M = Missions(); if (!M) { return; }
		const FMission* Sel = M->Find(SelectedId); if (!Sel) { return; }
		ULcarsPopup::Show(GetUIRoot(), TEXT("Abandon"), FString::Printf(TEXT("Abandon %s? Starfleet notes a mission dropped, and the crew will know."), *Sel->Title), { TEXT("Abandon"), TEXT("Keep it") }, [this](int32 Choice)
		{
			if (Choice != 0) { return; }
			if (UMissionSubsystem* M2 = Missions()) { FString Reply; const bool bOk = M2->Abandon(SelectedId, Reply); Say(Reply, bOk); }
		});
	});
	AddSidebarButton(TEXT("Set Course"), Lcars::Sky, [this]()
	{
		UMissionSubsystem* M = Missions(); const FMission* Sel = M ? M->Find(SelectedId) : nullptr;
		FString Dest;
		if (Show == 3) { Dest = SelectedShip; }
		else if (Sel && !Sel->TargetName.IsEmpty() && Sel->TargetKind != EMissionTarget::None) { Dest = Sel->TargetName; }
		if (Dest.IsEmpty()) { Say(TEXT("No destination for that one"), false); return; }
		if (AStarshipHUD* H = GetHUD()) { Say(H->SetNavTargetByName(Dest) ? FString::Printf(TEXT("Nav target: %s"), *Dest) : FString::Printf(TEXT("%s is not on the chart (out of the system, or away)"), *Dest), true); }
	});
	AddSidebarButton(TEXT("Crew"), Lcars::Tan, [this]() { if (USlonkUIRoot* UI = GetUIRoot()) { Close(); UI->PushMenu<UCrewPanel>(); } });
	AddSidebarButton(TEXT("Main Menu"), Lcars::Lavender, [this]() { if (USlonkUIRoot* UI = GetUIRoot()) { Close(); UI->PushMenu<UMainMenuPanel>(); } });

	AddLiveRow(Content, TEXT("Standing"), [this]()
	{
		UMissionSubsystem* M = Missions(); if (!M) { return FString(TEXT("no missions")); }
		UCrewRosterSubsystem* R = GetWorld() && GetWorld()->GetGameInstance() ? GetWorld()->GetGameInstance()->GetSubsystem<UCrewRosterSubsystem>() : nullptr;
		return FString::Printf(TEXT("stardate %s   Starfleet's regard %+d, %s   %d completed, %d failed   %d active, %d offered   %d unread"), R ? *R->StardateText() : TEXT("-"), M->GetStanding(), *M->StandingTitle(), M->GetCompleted(), M->GetFailed(), M->Active().Num(), M->Open().Num() - M->Active().Num(), M->Unread());
	}, Lcars::Peach, 14.f);

	UHorizontalBox* Columns = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	USizeBox* Left = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass()); Left->SetWidthOverride(700.f);
	ListBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
	Left->AddChild(ListBox);
	if (UHorizontalBoxSlot* Sl = Columns->AddChildToHorizontalBox(Left)) { Sl->SetPadding(FMargin(0.f, 6.f, 14.f, 0.f)); Sl->SetVerticalAlignment(VAlign_Fill); }
	FileScroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
	FileBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	FileScroll->AddChild(FileBox);
	if (UHorizontalBoxSlot* Sl = Columns->AddChildToHorizontalBox(FileScroll)) { Sl->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); Sl->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f)); }
	if (UVerticalBoxSlot* Sl = Content->AddChildToVerticalBox(Columns)) { Sl->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); }
}

void UMissionPanel::NativeOnActivated()
{
	Super::NativeOnActivated();
	LastRevision = -1; LastFileKey.Reset();
	// Open on the mission to steer by when nothing is selected.
	if (UMissionSubsystem* M = Missions()) { if (!M->Find(SelectedId)) { if (const FMission* O = M->Objective()) { SelectedId = O->Id; } else if (M->Open().Num() > 0) { SelectedId = M->Open()[0]->Id; } } }
}

void UMissionPanel::Refresh()
{
	UMissionSubsystem* M = Missions();
	if (!M) { return; }
	if (ShowButton) { if (UTextBlock* T = Cast<UTextBlock>(ShowButton->GetChildAt(0))) { T->SetText(FText::FromString(Show == 0 ? TEXT("SHOW: BOARD") : Show == 1 ? TEXT("SHOW: INBOX") : Show == 2 ? TEXT("SHOW: LOG") : TEXT("SHOW: FLEET"))); } }
	if (Show == 1 && Show != LastShow && M->GetInbox().IsValidIndex(SelectedMessage)) { SelectedId = M->GetInbox()[SelectedMessage].MissionId; }   // the pane and the sidebar act on the same mission
	const FMission* Sel = M->Find(SelectedId);
	const int32 Here = Sel ? (M->IsAtTarget(*Sel) ? 1 : 0) : -1;
	const int32 FleetRev = Fleet() ? Fleet()->GetRevision() : -1;
	if (M->GetRevision() != LastRevision || Show != LastShow || SelectedId != LastSelected || SelectedMessage != LastMessage || Here != LastHere || FleetRev != LastFleetRevision || SelectedShip != LastShip)
	{
		LastRevision = M->GetRevision(); LastShow = Show; LastSelected = SelectedId; LastMessage = SelectedMessage; LastHere = Here; LastFleetRevision = FleetRev; LastShip = SelectedShip;
		FillList(); FillFile();
	}
}

void UMissionPanel::FillList()
{
	UMissionSubsystem* M = Missions();
	for (UButton* B : RowButtons) { Unbind(B); }
	RowButtons.Reset(); ListBox->ClearChildren();
	if (!M) { return; }
	auto Head = [&](const FString& T) { UTextBlock* H = Lcars::MakeText(WidgetTree, T, 14.f, Lcars::Peach, true, true); if (UScrollBoxSlot* Sl = Cast<UScrollBoxSlot>(ListBox->AddChild(H))) { Sl->SetPadding(FMargin(4.f, 0.f, 0.f, 6.f)); } };
	auto Row = [&](const FString& Text, const FLinearColor& Col, bool bSelected, TFunction<void()> OnClick)
	{
		UTextBlock* Label = nullptr;
		UButton* B = Lcars::MakeButton(WidgetTree, Text, bSelected ? Lcars::Peach : Lcars::Darker(Col, 0.45f), &Label);
		if (Label) { Label->SetFont(Lcars::Font(12.f, false, 0)); Label->SetJustification(ETextJustify::Left); Label->SetColorAndOpacity(FSlateColor(bSelected ? Lcars::Black : FLinearColor::White)); }
		Bind(B, OnClick, Text);
		RowButtons.Add(B);
		if (UScrollBoxSlot* Sl = Cast<UScrollBoxSlot>(ListBox->AddChild(B))) { Sl->SetPadding(FMargin(0.f, 1.f, 0.f, 1.f)); }
	};
	if (Show == 3)
	{
		UFleetSubsystem* F = Fleet();
		TArray<const FFleetShip*> Ships; if (F) { for (const FFleetShip& S : F->GetShips()) { Ships.Add(&S); } }
		TSet<const FFleetShip*> Near; if (F) { Near.Append(F->Nearby()); }
		Ships.Sort([&](const FFleetShip& A, const FFleetShip& B) { const bool NA = Near.Contains(&A), NB = Near.Contains(&B); return NA != NB ? NA : A.Name < B.Name; });
		Head(FString::Printf(TEXT("The fleet: %d ships"), Ships.Num()));
		if (Ships.Num() == 0) { ListBox->AddChild(Lcars::MakeText(WidgetTree, TEXT("No other ships known yet."), 13.f, Lcars::Dim, false, false)); }
		for (const FFleetShip* S : Ships)
		{
			const bool bNear = Near.Contains(S);
			const FString T = FString::Printf(TEXT("%s  %s  %s%s  regard %+.0f"), *S->Name, *S->ShipClass, *S->TaskText(), bNear ? TEXT("  (in range)") : TEXT(""), S->Relation * 100.f);
			const FLinearColor Col = S->bLost ? Lcars::Salmon : S->Mission.Trouble != EFleetTrouble::None ? Lcars::Orange : bNear ? Lcars::Gold : S->Mission.IsActive() ? Lcars::Sky : Lcars::Lavender;
			const FString Name = S->Name;
			Row(T, Col, Name == SelectedShip, [this, Name]() { SelectedShip = Name; });
		}
		return;
	}
	if (Show == 1)
	{
		const TArray<FStarfleetMessage>& Inbox = M->GetInbox();
		Head(FString::Printf(TEXT("Inbox: %d, %d unread"), Inbox.Num(), M->Unread()));
		if (Inbox.Num() == 0) { ListBox->AddChild(Lcars::MakeText(WidgetTree, TEXT("Nothing from Starfleet yet."), 13.f, Lcars::Dim, false, false)); }
		for (int32 i = Inbox.Num() - 1; i >= 0; --i)
		{
			const FStarfleetMessage& Msg = Inbox[i];
			Row(FString::Printf(TEXT("%s %.1f  %s: %s"), Msg.bRead ? TEXT(" ") : TEXT("*"), Msg.Stardate, *Msg.From, *Msg.Subject), Msg.bRead ? Lcars::Lavender : Lcars::Sky, i == SelectedMessage, [this, i]() { SelectedMessage = i; if (UMissionSubsystem* M2 = Missions()) { M2->MarkRead(i); if (M2->GetInbox().IsValidIndex(i)) { SelectedId = M2->GetInbox()[i].MissionId; } } });
		}
		return;
	}
	TArray<const FMission*> Rows;
	for (const FMission& Ms : M->GetMissions()) { if (Ms.IsOpen() == (Show == 0)) { Rows.Add(&Ms); } }
	if (Show == 0) { Rows.Sort([](const FMission& A, const FMission& B) { return A.State != B.State ? A.State == EMissionState::Active : A.DeadlineStardate < B.DeadlineStardate; }); }
	else { Rows.Sort([](const FMission& A, const FMission& B) { return A.ClosedStardate > B.ClosedStardate; }); }
	Head(Show == 0 ? FString::Printf(TEXT("Mission board: %d"), Rows.Num()) : FString::Printf(TEXT("Mission log: %d"), Rows.Num()));
	if (Rows.Num() == 0) { ListBox->AddChild(Lcars::MakeText(WidgetTree, Show == 0 ? TEXT("Nothing on the board. Starfleet's orders come over subspace; a starbase posts its own; space brings the rest.") : TEXT("Nothing done yet."), 13.f, Lcars::Dim, false, false)); }
	for (const FMission* Ms : Rows)
	{
		const FString Where = Ms->TargetName.IsEmpty() ? TEXT("here") : Ms->TargetName;
		const FString T = Ms->IsOpen()
			? FString::Printf(TEXT("%s  %s  %s%s  due %.1f"), *UMissionSubsystem::StateName(Ms->State), *UMissionSubsystem::SourceName(Ms->Source), *Ms->Title, Ms->TargetKind == EMissionTarget::None || Ms->Title.Contains(Where) ? TEXT("") : *FString::Printf(TEXT(" @ %s"), *Where), Ms->DeadlineStardate)
			: FString::Printf(TEXT("%s  %s  %s  %.1f"), *UMissionSubsystem::StateName(Ms->State), *UMissionSubsystem::SourceName(Ms->Source), *Ms->Title, Ms->ClosedStardate);
		const FLinearColor Col = Ms->State == EMissionState::Active ? Lcars::Gold : Ms->State == EMissionState::Offered ? Lcars::Sky : Ms->State == EMissionState::Completed ? FLinearColor(0.55f, 0.9f, 0.6f) : Lcars::Salmon;
		const int32 Id = Ms->Id;
		Row(T, Col, Id == SelectedId, [this, Id]() { SelectedId = Id; });
	}
}

void UMissionPanel::FillFile()
{
	UMissionSubsystem* M = Missions();
	for (UButton* B : OptionButtons) { Unbind(B); }
	OptionButtons.Reset(); FileBox->ClearChildren();
	if (!M) { return; }
	auto Line = [&](const FString& T, float Size = 13.f, const FLinearColor& C = FLinearColor::White, float Pad = 0.f) { UTextBlock* B = AddLine(FileBox, T, Size, C, false, false, Pad); if (B) { B->SetAutoWrapText(true); } };
	auto Heading = [&](const FString& T) { AddLine(FileBox, T, 13.f, Lcars::Peach, true, true, 10.f); };
	if (Show == 3)
	{
		UFleetSubsystem* F = Fleet(); const FFleetShip* S = F ? F->Find(SelectedShip) : nullptr;
		if (!S) { Line(TEXT("Select a ship."), 14.f, Lcars::Dim); } else { FillFleetFile(*S); }
		return;
	}
	if (Show == 1 && M->GetInbox().IsValidIndex(SelectedMessage))
	{
		const FStarfleetMessage& Msg = M->GetInbox()[SelectedMessage];
		AddLine(FileBox, Msg.Subject, 20.f, Lcars::Peach, true, false);
		Line(FString::Printf(TEXT("From %s, stardate %.1f"), *Msg.From, Msg.Stardate), 13.f, Lcars::Dim);
		Line(Msg.Text, 14.f, FLinearColor::White, 8.f);
		if (const FMission* Ms = M->Find(Msg.MissionId)) { Line(FString::Printf(TEXT("Mission: %s, %s."), *Ms->Title, *UMissionSubsystem::StateName(Ms->State).ToLower()), 13.f, Lcars::Gold, 8.f); if (Ms->State == EMissionState::Offered) { Line(TEXT("Accept takes it on; the board shows it from then."), 13.f, Lcars::Dim); } }
		return;
	}
	const FMission* Ms = M->Find(SelectedId);
	if (!Ms) { Line(TEXT("Select a mission."), 14.f, Lcars::Dim); return; }
	AddLine(FileBox, Ms->Title, 20.f, Lcars::Peach, true, false);
	Line(FString::Printf(TEXT("%s, from %s; %s. %s%s"), *UMissionSubsystem::KindName(Ms->Kind), *Ms->From, *UMissionSubsystem::StateName(Ms->State).ToLower(), Ms->TargetName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT("At %s%s. "), *Ms->TargetName, M->IsAtTarget(*Ms) ? TEXT(" (in range)") : TEXT("")), Ms->IsOpen() ? *FString::Printf(TEXT("Due by stardate %.1f."), Ms->DeadlineStardate) : *FString::Printf(TEXT("Closed stardate %.1f."), Ms->ClosedStardate)), 13.f, Lcars::Dim);
	Line(Ms->Brief, 14.f, FLinearColor::White, 6.f);
	if (!Ms->Group.IsNone()) { Line(FString::Printf(TEXT("Concerns the %s. The crew's feelings about them weigh on every check, and what happens here will move them."), *Ms->Group.ToString()), 13.f, Lcars::Gold, 4.f); }
	if (Ms->State == EMissionState::Active)
	{
		if (const FMissionStage* St = Ms->Current())
		{
			Heading(St->Title);
			Line(St->Text, 14.f, FLinearColor::White);
			const bool bHere = !St->bAtTarget || M->IsAtTarget(*Ms);
			if (!bHere) { Line(FString::Printf(TEXT("Not yet in range of %s. Set course and close to within hailing distance."), *Ms->TargetName), 13.f, Lcars::Salmon, 4.f); }
			else { Line(TEXT("The captain's choice:"), 13.f, Lcars::Peach, 6.f); }
			for (int32 i = 0; i < St->Options.Num(); ++i)
			{
				const FMissionOption& O = St->Options[i];
				UTextBlock* Label = nullptr;
				UButton* B = Lcars::MakeButton(WidgetTree, FString::Printf(TEXT("%s   [%s %s %.0f%s%s]"), *O.Text, *UCrewRosterSubsystem::DepartmentName(O.Party), *UCrewRosterSubsystem::SkillName(O.Skill).ToLower(), O.Difficulty, O.Risk > 0.f ? TEXT(", risk") : TEXT(""), O.Science > 0 ? TEXT(", science") : TEXT("")), bHere ? Lcars::Gold : Lcars::Darker(Lcars::Gold, 0.6f), &Label);
				if (Label) { Label->SetFont(Lcars::Font(13.f, false, 0)); Label->SetJustification(ETextJustify::Left); Label->SetColorAndOpacity(FSlateColor(Lcars::Black)); }
				const int32 Id = Ms->Id;
				Bind(B, [this, Id, i]()
				{
					UMissionSubsystem* M2 = Missions(); if (!M2) { return; }
					FMissionOutcome Out; FString Reply;
					const bool bOk = M2->Act(Id, i, Out, Reply);
					if (!bOk) { Say(Reply, false); return; }
					Say(Out.CheckSummary, Out.bSuccess, 12.f);
					Say(Out.Text, Out.bSuccess, 14.f);
				}, O.Text);
				OptionButtons.Add(B);
				if (UVerticalBoxSlot* Sl = FileBox->AddChildToVerticalBox(B)) { Sl->SetPadding(FMargin(0.f, 3.f, 0.f, 3.f)); }
			}
		}
		if (Ms->Party.Num() > 0) { Line(FString::Printf(TEXT("Named party: %d (ship mission party %d <names> to change)."), Ms->Party.Num(), Ms->Id), 13.f, Lcars::Dim, 4.f); }
		else { Line(TEXT("Party: the option's department, its head and second with the best hands at the skill (ship mission party <id> <names> names your own)."), 13.f, Lcars::Dim, 4.f); }
	}
	else if (Ms->State == EMissionState::Offered) { Line(Ms->Source == EMissionSource::Starbase ? FString::Printf(TEXT("Posted at %s. Accept takes it on."), *Ms->PostedAt) : TEXT("Starfleet's orders. Accept takes them on; leaving them to lapse is noticed."), 13.f, Lcars::Gold, 8.f); }
	if (Ms->ScienceEarned > 0) { Line(FString::Printf(TEXT("Science earned: %d points."), Ms->ScienceEarned), 13.f, Lcars::Gold, 4.f); }
	if (Ms->Log.Num() > 0) { Heading(TEXT("Log")); for (const FString& L : Ms->Log) { Line(L, 13.f, L.StartsWith(TEXT("      ")) ? Lcars::Dim : FLinearColor::White); } }
}

void UMissionPanel::FillFleetFile(const FFleetShip& S)
{
	UFleetSubsystem* F = Fleet();
	UCrewRosterSubsystem* R = GetWorld() && GetWorld()->GetGameInstance() ? GetWorld()->GetGameInstance()->GetSubsystem<UCrewRosterSubsystem>() : nullptr;
	auto Line = [&](const FString& T, float Size = 13.f, const FLinearColor& C = FLinearColor::White, float Pad = 0.f) { UTextBlock* B = AddLine(FileBox, T, Size, C, false, false, Pad); if (B) { B->SetAutoWrapText(true); } };
	auto Heading = [&](const FString& T) { AddLine(FileBox, T, 13.f, Lcars::Peach, true, true, 10.f); };
	AddLine(FileBox, S.Name, 20.f, Lcars::Peach, true, false);
	Line(FString::Printf(TEXT("%s class, out of %s, %s. Captain %s commanding. %d aboard, %d lost; morale %d%%, condition %d%%. Their regard for us: %+.0f."), *S.ShipClass, *S.HomeStation, *S.SystemName, *S.Captain.FullName(), S.Complement, S.Lost, FMath::RoundToInt(S.Morale * 100.f), FMath::RoundToInt(S.Condition * 100.f), S.Relation * 100.f), 13.f, Lcars::Dim);
	if (S.bLost) { Line(TEXT("Lost with all hands."), 14.f, Lcars::Salmon, 6.f); }
	Heading(S.Mission.IsActive() ? TEXT("Mission") : TEXT("On station"));
	if (S.Mission.IsActive())
	{
		Line(FString::Printf(TEXT("%s: %s%s."), *S.Mission.Title, *S.TaskText(), S.Mission.bFar ? TEXT(" (out of the system)") : TEXT("")), 14.f, FLinearColor::White);
		if (S.Mission.Trouble != EFleetTrouble::None) { Line(FString::Printf(TEXT("In trouble for %.0f h: %s. %s"), S.Mission.TroubleHours, *S.Mission.TroubleText, S.Mission.SupportMissionId >= 0 ? TEXT("Her call for support is on the board.") : TEXT("")), 13.f, Lcars::Orange, 4.f); }
		else if (S.Mission.AssistMissionId >= 0) { Line(TEXT("A way to lend a hand is on the board."), 13.f, Lcars::Gold, 4.f); }
		else if (S.Mission.bAssisted) { Line(TEXT("We lent a hand."), 13.f, Lcars::Gold, 4.f); }
	}
	else { Line(FString::Printf(TEXT("Alongside %s; %d missions done, %d failed."), *S.HomeStation, S.Done, S.Failed), 13.f, FLinearColor::White); }
	Heading(TEXT("Senior staff"));
	Line(FString::Printf(TEXT("%s, commanding (%s)"), *S.Captain.Styled(), *S.Captain.Species), 13.f, Lcars::Peach);
	for (const FFleetPerson& P : S.Officers) { const FString Ties = UFleetSubsystem::TiesText(P, R); Line(FString::Printf(TEXT("%s, %s%s"), *P.Person.Styled(), *P.Person.Posting, Ties.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (%s)"), *Ties)), 13.f, FLinearColor::White); }
	if (S.Notables.Num() > 0)
	{
		Heading(TEXT("People we know aboard"));
		for (const FFleetPerson& P : S.Notables)
		{
			const FString Ties = UFleetSubsystem::TiesText(P, R);
			Line(FString::Printf(TEXT("%s, %s department; %s%s"), *P.Person.Styled(), *UCrewRosterSubsystem::DepartmentName(P.Person.Department).ToLower(), *P.Origin, Ties.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (%s)"), *Ties)), 13.f, FLinearColor::White);
		}
		Line(TEXT("ship fleet request <ship> <name> asks for one of them; ship fleet transfer <name> <ship> sends one of ours."), 12.f, Lcars::Dim, 4.f);
	}
	if (S.Log.Num() > 0) { Heading(TEXT("Log")); for (int32 i = FMath::Max(0, S.Log.Num() - 12); i < S.Log.Num(); ++i) { Line(S.Log[i], 13.f, Lcars::Dim); } }
}
