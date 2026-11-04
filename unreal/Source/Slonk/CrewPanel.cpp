// Slonk - the crew panel.

#include "CrewPanel.h"
#include "LcarsTheme.h"
#include "LcarsPanels.h"
#include "SlonkUIRoot.h"
#include "StarshipPawn.h"
#include "ShipSystemsComponent.h"
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

UCrewRosterSubsystem* UCrewPanel::Roster() const
{
	const UWorld* W = GetWorld();
	return W && W->GetGameInstance() ? W->GetGameInstance()->GetSubsystem<UCrewRosterSubsystem>() : nullptr;
}

FLinearColor UCrewPanel::MoodColor(float Mood) const
{
	return Mood > 0.65f ? FLinearColor(0.55f, 0.9f, 0.6f) : Mood > 0.45f ? Lcars::Peach : Mood > 0.3f ? Lcars::Gold : FLinearColor(1.f, 0.5f, 0.4f);
}

FString UCrewPanel::RowText(const FCrewMember& M) const
{
	FString State = UCrewRosterSubsystem::ConditionName(M.Condition);
	if (M.Condition == ECrewCondition::Fit) { State = UCrewRosterSubsystem::ActivityName(M.Activity); }
	return FString::Printf(TEXT("%-26s %-12s %-22s %-6s mood %3d%%  %s%s"), *M.Styled().Left(26), *UCrewRosterSubsystem::DepartmentName(M.Department), *M.Posting.Left(22), *UCrewRosterSubsystem::WatchName(M.Watch), FMath::RoundToInt(M.Mood * 100.f), *State, M.Stress > 0.7f ? TEXT("  stressed") : TEXT(""));
}

void UCrewPanel::BuildContent()
{
	AddSidebarButton(TEXT("Close"), Lcars::Orange, [this]() { Close(); });
	FilterButton = AddSidebarButton(TEXT("Show: Staff"), Lcars::Tan, [this]()
	{
		// Trouble, senior staff, each department, everyone, round again.
		Filter = Filter >= (int32)ECrewDepartment::Count ? -2 : Filter + 1;
	});
	SortButton = AddSidebarButton(TEXT("Sort: Rank"), Lcars::Lilac, [this]() { Sort = (Sort + 1) % 3; });
	AddSidebarButton(TEXT("Commend"), Lcars::Gold, [this]() { if (UCrewRosterSubsystem* R = Roster()) { FString Reply; if (R->Commend(SelectedId, FString(), Reply)) { if (AStarshipPawn* S = GetShip()) { S->ScreenMessage(Reply, FColor::Cyan); } } } });
	AddSidebarButton(TEXT("Reprimand"), Lcars::Salmon, [this]() { if (UCrewRosterSubsystem* R = Roster()) { FString Reply; if (R->Reprimand(SelectedId, FString(), Reply)) { if (AStarshipPawn* S = GetShip()) { S->ScreenMessage(Reply, FColor::Orange); } } } });
	AddSidebarButton(TEXT("Promote"), Lcars::Peach, [this]() { if (UCrewRosterSubsystem* R = Roster()) { FString Reply; R->Promote(SelectedId, Reply); if (AStarshipPawn* S = GetShip()) { S->ScreenMessage(Reply, FColor::Cyan); } } });
	AddSidebarButton(TEXT("Leave: 3 days"), Lcars::Sky, [this]() { if (UCrewRosterSubsystem* R = Roster()) { FString Reply; R->GrantLeave(SelectedId, 3, Reply); if (AStarshipPawn* S = GetShip()) { S->ScreenMessage(Reply, FColor::Cyan); } } });
	AddSidebarButton(TEXT("Talk: Listen"), Lcars::Periwinkle, [this]() { if (UCrewRosterSubsystem* R = Roster()) { FString Reply; R->Talk(SelectedId, TEXT("listen"), Reply); if (AStarshipPawn* S = GetShip()) { S->ScreenMessage(Reply, FColor::Cyan, 8.f); } } });
	AddSidebarButton(TEXT("Talk: Encourage"), Lcars::Periwinkle, [this]() { if (UCrewRosterSubsystem* R = Roster()) { FString Reply; R->Talk(SelectedId, TEXT("encourage"), Reply); if (AStarshipPawn* S = GetShip()) { S->ScreenMessage(Reply, FColor::Cyan, 8.f); } } });
	AddSidebarButton(TEXT("Sickbay"), Lcars::Sky, [this]() { if (USlonkUIRoot* UI = GetUIRoot()) { Close(); UI->PushMenu<UMedicalPanel>(); } });
	AddSidebarButton(TEXT("Main Menu"), Lcars::Lavender, [this]() { if (USlonkUIRoot* UI = GetUIRoot()) { Close(); UI->PushMenu<UMainMenuPanel>(); } });

	AddLiveRow(Content, TEXT("Ship's day"), [this]()
	{
		UCrewRosterSubsystem* R = Roster(); if (!R) { return FString(TEXT("no roster")); }
		const float Mor = R->Morale();
		return FString::Printf(TEXT("stardate %s, %02d:00, %s watch   %d aboard, %d lost   morale %d%% %s   %d in trouble"), *R->StardateText(), R->GetHourOfDay(), *UCrewRosterSubsystem::WatchName(R->WatchOnDuty()).ToLower(), R->CountAboard(), R->GetLost().Num(), FMath::RoundToInt(Mor * 100.f), Mor > 0.7f ? TEXT("(high)") : Mor > 0.5f ? TEXT("(fair)") : Mor > 0.35f ? TEXT("(low)") : TEXT("(poor)"), R->InTrouble().Num());
	}, Lcars::Peach, 14.f);

	// Two columns: the list, the file.
	UHorizontalBox* Columns = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	USizeBox* Left = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass()); Left->SetWidthOverride(760.f);
	ListBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
	Left->AddChild(ListBox);
	if (UHorizontalBoxSlot* Sl = Columns->AddChildToHorizontalBox(Left)) { Sl->SetPadding(FMargin(0.f, 6.f, 14.f, 0.f)); Sl->SetVerticalAlignment(VAlign_Fill); }
	FileScroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
	FileBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	FileScroll->AddChild(FileBox);
	if (UHorizontalBoxSlot* Sl = Columns->AddChildToHorizontalBox(FileScroll)) { Sl->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); Sl->SetPadding(FMargin(0.f, 6.f, 0.f, 0.f)); }
	if (UVerticalBoxSlot* Sl = Content->AddChildToVerticalBox(Columns)) { Sl->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); }
}

void UCrewPanel::NativeOnActivated()
{
	Super::NativeOnActivated();
	LastRevision = -1; LastFilter = -99; LastFileKey.Reset();
}

void UCrewPanel::FillList()
{
	UCrewRosterSubsystem* R = Roster();
	if (!R || !ListBox) { return; }
	ListBox->ClearChildren();
	TArray<const FCrewMember*> Rows;
	if (Filter == -2) { for (int32 Id : R->InTrouble()) { if (const FCrewMember* M = R->Find(Id)) { Rows.Add(M); } } }
	else
	{
		for (const FCrewMember& M : R->GetRoster())
		{
			if (M.Condition == ECrewCondition::Transferred) { continue; }
			if (Filter == -1 && !(M.bHead || M.bSecond)) { continue; }
			if (Filter >= 0 && Filter < (int32)ECrewDepartment::Count && (int32)M.Department != Filter) { continue; }
			Rows.Add(&M);
		}
	}
	if (Filter != -2)
	{
		if (Sort == 0) { Rows.Sort([](const FCrewMember& A, const FCrewMember& B) { return A.Rank != B.Rank ? A.Rank > B.Rank : A.LastName + A.FirstName < B.LastName + B.FirstName; }); }
		else if (Sort == 1) { Rows.Sort([](const FCrewMember& A, const FCrewMember& B) { return A.Mood < B.Mood; }); }
		else { Rows.Sort([](const FCrewMember& A, const FCrewMember& B) { return (A.LastName.IsEmpty() ? A.FirstName : A.LastName) < (B.LastName.IsEmpty() ? B.FirstName : B.LastName); }); }
	}
	const FString Heading = Filter == -2 ? TEXT("In trouble") : Filter == -1 ? TEXT("Senior staff") : Filter < (int32)ECrewDepartment::Count ? UCrewRosterSubsystem::DepartmentName((ECrewDepartment)Filter) : TEXT("Everyone");
	const FString Short = Filter == -2 ? TEXT("Trouble") : Filter == -1 ? TEXT("Staff") : Filter < (int32)ECrewDepartment::Count ? UCrewRosterSubsystem::DepartmentName((ECrewDepartment)Filter) : TEXT("All");
	if (FilterButton) { if (UTextBlock* T = Cast<UTextBlock>(FilterButton->GetChildAt(0))) { T->SetText(FText::FromString((TEXT("Show: ") + Short).ToUpper())); } }
	if (SortButton) { if (UTextBlock* T = Cast<UTextBlock>(SortButton->GetChildAt(0))) { T->SetText(FText::FromString(Sort == 0 ? TEXT("SORT: RANK") : Sort == 1 ? TEXT("SORT: MOOD") : TEXT("SORT: NAME"))); } }
	UTextBlock* Head = Lcars::MakeText(WidgetTree, FString::Printf(TEXT("%s: %d"), *Heading, Rows.Num()), 14.f, Lcars::Peach, true, true);
	if (UScrollBoxSlot* Sl = Cast<UScrollBoxSlot>(ListBox->AddChild(Head))) { Sl->SetPadding(FMargin(4.f, 0.f, 0.f, 6.f)); }
	if (Rows.Num() == 0) { ListBox->AddChild(Lcars::MakeText(WidgetTree, TEXT("Nobody."), 13.f, Lcars::Dim, false, false)); }
	for (const FCrewMember* M : Rows)
	{
		UTextBlock* Label = nullptr;
		const FLinearColor Col = M->Condition == ECrewCondition::Dead ? Lcars::Red : M->Condition != ECrewCondition::Fit ? Lcars::Gold : (M->Id == SelectedId ? Lcars::Peach : Lcars::Darker(MoodColor(M->Mood), 0.45f));
		UButton* B = Lcars::MakeButton(WidgetTree, RowText(*M), Col, &Label);
		if (Label) { Label->SetFont(Lcars::Font(12.f, false, 0)); Label->SetColorAndOpacity(FSlateColor(M->Id == SelectedId ? Lcars::Black : FLinearColor::White)); Label->SetJustification(ETextJustify::Left); }
		const int32 Id = M->Id;
		Bind(B, [this, Id]() { SelectedId = Id; LastFileKey.Reset(); LastRevision = -1; }, M->FullName());
		if (UScrollBoxSlot* Sl = Cast<UScrollBoxSlot>(ListBox->AddChild(B))) { Sl->SetPadding(FMargin(0.f, 1.f, 0.f, 1.f)); }
	}
}

void UCrewPanel::FillFile()
{
	UCrewRosterSubsystem* R = Roster();
	if (!R || !FileBox) { return; }
	FileBox->ClearChildren();
	const FCrewMember* M = R->Find(SelectedId);
	if (!M)
	{
		AddLine(FileBox, TEXT("Select a crew member."), 14.f, Lcars::Dim, false, false);
		return;
	}
	auto Line = [&](const FString& T, float Size = 13.f, const FLinearColor& C = FLinearColor::White, float Pad = 0.f) { AddLine(FileBox, T, Size, C, false, false, Pad); };
	auto Heading = [&](const FString& T) { AddLine(FileBox, T, 13.f, Lcars::Peach, true, true, 10.f); };
	AddLine(FileBox, M->Styled(), 20.f, Lcars::Peach, true, false);
	Line(FString::Printf(TEXT("%s, %s department, %s, %s watch"), *UCrewRosterSubsystem::RankName(M->Rank), *UCrewRosterSubsystem::DepartmentName(M->Department).ToLower(), *M->Posting, *UCrewRosterSubsystem::WatchName(M->Watch).ToLower()), 13.f, Lcars::Peach);
	Line(FString::Printf(TEXT("%s, %d, of %s (%s). %s."), *M->Species, M->Age, *M->Home, *M->Pronoun, *M->Backstory), 13.f, Lcars::Dim);
	const FString State = M->Condition == ECrewCondition::Fit ? UCrewRosterSubsystem::ActivityName(M->Activity) : UCrewRosterSubsystem::ConditionName(M->Condition);
	Line(FString::Printf(TEXT("Now: %s, %s.   Mood %d%%   stress %d%%"), *State.ToLower(), *M->Where, FMath::RoundToInt(M->Mood * 100.f), FMath::RoundToInt(M->Stress * 100.f)), 13.f, MoodColor(M->Mood), 4.f);
	if (M->Condition == ECrewCondition::Injured || M->Condition == ECrewCondition::Critical) { Line(FString::Printf(TEXT("Under treatment, about %.0f hours to go."), M->ConditionHours), 13.f, Lcars::Gold); }
	if (M->bHead) { Line(FString::Printf(TEXT("Head of %s."), *UCrewRosterSubsystem::DepartmentName(M->Department).ToLower()), 13.f, Lcars::Gold); }
	else if (M->bSecond) { Line(FString::Printf(TEXT("Second in %s."), *UCrewRosterSubsystem::DepartmentName(M->Department).ToLower()), 13.f, Lcars::Gold); }

	Heading(TEXT("Skills"));
	{
		TArray<TPair<float, int32>> Top;
		for (int32 i = 0; i < M->Skills.Num(); ++i) { Top.Add({ M->Skills[i], i }); }
		Top.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B) { return A.Key > B.Key; });
		FString S;
		for (int32 i = 0; i < Top.Num() && i < 5; ++i) { S += FString::Printf(TEXT("%s%s %d"), i ? TEXT("   ") : TEXT(""), *UCrewRosterSubsystem::SkillName((ECrewSkill)Top[i].Value), FMath::RoundToInt(Top[i].Key)); }
		Line(S);
	}
	Heading(TEXT("Facets"));
	{
		FString S;
		for (int32 i = 0; i < M->Facets.Num(); ++i) { S += FString::Printf(TEXT("%s%s %d"), i ? TEXT("   ") : TEXT(""), *UCrewRosterSubsystem::FacetName((ECrewFacet)i), FMath::RoundToInt(M->Facets[i])); }
		Line(S);
	}
	if (M->Traits.Num() > 0)
	{
		Heading(TEXT("Traits"));
		for (FName T : M->Traits) { Line(UCrewRosterSubsystem::TraitText(T)); }
	}
	Heading(TEXT("Needs"));
	{
		FString S;
		for (int32 i = 0; i < M->Needs.Num(); ++i) { S += FString::Printf(TEXT("%s%s %d%%"), i ? TEXT("   ") : TEXT(""), *UCrewRosterSubsystem::NeedName((ECrewNeed)i), FMath::RoundToInt(M->Needs[i] * 100.f)); }
		Line(S);
		for (int32 i = 0; i < M->Needs.Num(); ++i) { if (M->Needs[i] < 0.25f) { Line(FString::Printf(TEXT("Wants for %s."), *UCrewRosterSubsystem::NeedName((ECrewNeed)i).ToLower()), 13.f, Lcars::Gold); } }
	}
	Heading(TEXT("Wants and fears"));
	for (const FCrewWant& W : M->Wants)
	{
		if (W.bFear) { Line(FString::Printf(TEXT("Fears %s."), *W.Text), 13.f, Lcars::Salmon); }
		else { Line(FString::Printf(TEXT("%s %s%s."), W.bGranted ? TEXT("Had") : TEXT("Wants"), *W.Text, W.bGranted ? TEXT("") : *FString::Printf(TEXT(" (since day %d)"), W.SinceDay)), 13.f, W.bGranted ? Lcars::Dim : FLinearColor::White); }
	}
	if (M->Ties.Num() > 0)
	{
		Heading(TEXT("Elsewhere"));
		for (const FCrewTie& T : M->Ties)
		{
			const int32 Since = R->GetDay() - T.LastContactDay;
			Line(FString::Printf(TEXT("%s%s, %s %s%s"), T.bFamily ? TEXT("") : TEXT("friend: "), *T.Name, *T.Relation, *T.Where(), Since < 60 ? *FString::Printf(TEXT("; last word %d day%s ago"), Since, Since == 1 ? TEXT("") : TEXT("s")) : TEXT("")), 13.f, T.bFamily ? FLinearColor::White : Lcars::Dim);
		}
	}
	if (M->Relationships.Num() > 0)
	{
		Heading(TEXT("Bonds"));
		TArray<FCrewRelationship> Rel = M->Relationships;
		Rel.Sort([](const FCrewRelationship& A, const FCrewRelationship& B) { return FMath::Abs(A.Affinity) > FMath::Abs(B.Affinity); });
		for (int32 i = 0; i < Rel.Num() && i < 6; ++i)
		{
			const FCrewMember* O = R->Find(Rel[i].Other);
			FString Name = O ? O->Styled() : TEXT("someone lost");
			if (!O) { for (const FCrewMember& L : R->GetLost()) { if (L.Id == Rel[i].Other) { Name = L.Styled() + TEXT(" (lost)"); } } }
			Line(FString::Printf(TEXT("%s, %s (%+.0f)"), *Name, *UCrewRosterSubsystem::RelationName(Rel[i].Kind), Rel[i].Affinity * 100.f), 13.f, Rel[i].Affinity < 0.f ? Lcars::Salmon : FLinearColor::White);
		}
	}
	if (M->Memories.Num() > 0)
	{
		Heading(TEXT("On the mind"));
		TArray<FCrewMemory> Mem = M->Memories;
		Mem.Sort([](const FCrewMemory& A, const FCrewMemory& B) { return FMath::Abs(A.Weight) > FMath::Abs(B.Weight); });
		for (int32 i = 0; i < Mem.Num() && i < 5; ++i) { Line(FString::Printf(TEXT("%s (%+.0f)%s"), *Mem[i].Text, Mem[i].Weight * 100.f, Mem[i].bPermanent ? TEXT(", for good") : TEXT("")), 13.f, Mem[i].Weight < 0.f ? Lcars::Salmon : FLinearColor::White); }
	}
	Heading(FString::Printf(TEXT("Service: %d missions, %d commendations, %d reprimands, %d wounds, joined %.1f"), M->Service.Missions, M->Service.Commendations, M->Service.Reprimands, M->Service.Wounds, M->Service.JoinedStardate));
	for (int32 i = M->History.Num() - 1; i >= 0 && i >= M->History.Num() - 14; --i)
	{
		const FCrewEvent& E = M->History[i];
		Line(FString::Printf(TEXT("%.1f  %s"), E.Stardate, *E.Text), 12.f, E.bMilestone ? Lcars::Peach : Lcars::Dim);
	}
}

void UCrewPanel::Refresh()
{
	UCrewRosterSubsystem* R = Roster();
	if (!R) { return; }
	if (Filter != LastFilter || Sort != LastSort || R->GetRevision() != LastRevision)
	{
		LastFilter = Filter; LastSort = Sort; LastRevision = R->GetRevision();
		FillList();
		LastFileKey.Reset();
	}
	const FCrewMember* M = R->Find(SelectedId);
	const FString Key = M ? FString::Printf(TEXT("%d/%d/%d/%d/%d"), M->Id, M->History.Num(), M->Memories.Num(), FMath::RoundToInt(M->Mood * 100.f), (int32)M->Condition) : TEXT("none");
	if (Key != LastFileKey) { LastFileKey = Key; FillFile(); }
}
