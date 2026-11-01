#include "MsdPanel.h"
#include "LcarsPanels.h"
#include "PlanetWeather.h"
#include "ShipCompartments.h"
#include "ShipSystemsComponent.h"
#include "SlonkUIRoot.h"
#include "StarshipPawn.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"

// ---------------------------------------------------------------------------------------------------------------------
// The cutaway

bool UMsdWidget::Initialize()
{
	const bool bFirst = Super::Initialize();
	if (bFirst && WidgetTree && !WidgetTree->RootWidget && !HasAnyFlags(RF_ClassDefaultObject))
	{
		UBorder* Bg = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("MsdBg"));
		Bg->SetBrush(Lcars::Rounded(FLinearColor(0.f, 0.f, 0.01f, 1.f), FVector4(6.f, 6.f, 6.f, 6.f)));
		Bg->SetPadding(FMargin(0.f));
		Bg->SetVisibility(ESlateVisibility::Visible);
		WidgetTree->RootWidget = Bg;
		BoxBrush = Lcars::Rounded(FLinearColor::White, FVector4(3.f, 3.f, 3.f, 3.f));
		SetVisibility(ESlateVisibility::Visible);
	}
	return bFirst;
}

void UMsdWidget::SetShip(AStarshipPawn* InShip)
{
	Ship = InShip;
	if (InShip) { LengthCm = InShip->GetShipLengthCm(); }
	BuildSilhouette();
}

void UMsdWidget::BuildSilhouette()
{
	// Side elevation: X along, Z up. Triangles rasterised into a grid; vertices alone when the index buffer is not readable.
	AStarshipPawn* S = Ship.Get();
	if (!S) { return; }
	const TArray<FVector>& V = S->GetHullVerts();
	const TArray<uint32>& T = S->GetHullTris();
	if (V.Num() == 0) { return; }
	Zmin = 1e9f; Zmax = -1e9f;
	for (const FVector& P : V) { Zmin = FMath::Min(Zmin, (float)P.Z); Zmax = FMath::Max(Zmax, (float)P.Z); }
	const float Pad = (Zmax - Zmin) * 0.04f; Zmin -= Pad; Zmax += Pad;
	const int32 W = 768;
	const int32 H = FMath::Clamp((int32)(W * (Zmax - Zmin) / LengthCm), 64, 512);
	TArray<uint8> Mask; Mask.SetNumZeroed(W * H);
	auto ToGrid = [&](const FVector& P) { return FVector2D((P.X + LengthCm * 0.5f) / LengthCm * W, (Zmax - P.Z) / (Zmax - Zmin) * H); };
	if (T.Num() >= 3)
	{
		for (int32 i = 0; i + 2 < T.Num(); i += 3)
		{
			if (!V.IsValidIndex(T[i]) || !V.IsValidIndex(T[i + 1]) || !V.IsValidIndex(T[i + 2])) { continue; }
			const FVector2D A = ToGrid(V[T[i]]), B = ToGrid(V[T[i + 1]]), C = ToGrid(V[T[i + 2]]);
			const int32 X0 = FMath::Clamp(FMath::FloorToInt32(FMath::Min3(A.X, B.X, C.X)), 0, W - 1), X1 = FMath::Clamp(FMath::CeilToInt32(FMath::Max3(A.X, B.X, C.X)), 0, W - 1);
			const int32 Y0 = FMath::Clamp(FMath::FloorToInt32(FMath::Min3(A.Y, B.Y, C.Y)), 0, H - 1), Y1 = FMath::Clamp(FMath::CeilToInt32(FMath::Max3(A.Y, B.Y, C.Y)), 0, H - 1);
			const double Area = (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X);
			if (FMath::Abs(Area) < 1e-6)
			{
				// Edge-on: paint its vertices so thin things still show.
				for (const FVector2D& P : { A, B, C }) { const int32 x = FMath::Clamp((int32)P.X, 0, W - 1), y = FMath::Clamp((int32)P.Y, 0, H - 1); Mask[y * W + x] = 1; }
				continue;
			}
			for (int32 y = Y0; y <= Y1; ++y)
			{
				for (int32 x = X0; x <= X1; ++x)
				{
					const FVector2D P(x + 0.5, y + 0.5);
					const double W0 = ((B.X - P.X) * (C.Y - P.Y) - (B.Y - P.Y) * (C.X - P.X)) / Area;
					const double W1 = ((C.X - P.X) * (A.Y - P.Y) - (C.Y - P.Y) * (A.X - P.X)) / Area;
					const double W2 = 1.0 - W0 - W1;
					if (W0 >= -0.002 && W1 >= -0.002 && W2 >= -0.002) { Mask[y * W + x] = 1; }
				}
			}
		}
	}
	else
	{
		for (const FVector& P : V)
		{
			const FVector2D G = ToGrid(P);
			for (int32 dy = -1; dy <= 1; ++dy) for (int32 dx = -1; dx <= 1; ++dx)
			{
				const int32 x = (int32)G.X + dx, y = (int32)G.Y + dy;
				if (x >= 0 && x < W && y >= 0 && y < H) { Mask[y * W + x] = 1; }
			}
		}
	}
	// Pixels: a lavender fill with the deck lines through it and a bright outline, the MSD look.
	TArray<FColor> Px; Px.SetNum(W * H);
	const float DeckCm = 330.f;
	for (int32 y = 0; y < H; ++y)
	{
		const float Z = Zmax - (y + 0.5f) / H * (Zmax - Zmin);
		const bool bDeckLine = FMath::Fmod(Z - Zmin + 10000.f * DeckCm, DeckCm) < (Zmax - Zmin) / H * 1.2f;
		for (int32 x = 0; x < W; ++x)
		{
			const int32 i = y * W + x;
			if (!Mask[i]) { Px[i] = FColor(0, 0, 0, 0); continue; }
			const bool bEdge = x == 0 || y == 0 || x == W - 1 || y == H - 1 || !Mask[i - 1] || !Mask[i + 1] || !Mask[i - W] || !Mask[i + W];
			if (bEdge) { Px[i] = FColor(235, 200, 255, 255); }
			else if (bDeckLine) { Px[i] = FColor(60, 40, 90, 210); }
			else { Px[i] = FColor(120, 95, 165, 120); }
		}
	}
	Silhouette = SlonkWeather::MakeTexture(Px, W, H, TEXT("MsdSilhouette"));
	if (Silhouette)
	{
		SilBrush = FSlateBrush();
		SilBrush.SetResourceObject(Silhouette);
		SilBrush.ImageSize = FVector2D(W, H);
		SilBrush.DrawAs = ESlateBrushDrawType::Image;
	}
}

int32 UMsdWidget::HitTest(const FVector2D& P) const
{
	// Smallest box wins, so a bridge inside the quarters is still clickable.
	int32 Best = INDEX_NONE; double BestArea = TNumericLimits<double>::Max();
	for (int32 i = 0; i < Rects.Num(); ++i)
	{
		if (Rects[i].ContainsPoint(P)) { const double A = Rects[i].GetSize().X * Rects[i].GetSize().Y; if (A < BestArea) { BestArea = A; Best = i; } }
	}
	return Best;
}

bool UMsdWidget::SelectByName(const FString& Text)
{
	AStarshipPawn* S = Ship.Get();
	if (!S || !S->Systems) { return false; }
	const TArray<FCompartment>& K = S->Systems->GetCompartments();
	for (int32 i = 0; i < K.Num(); ++i) { if (K[i].Name.Contains(Text)) { Selected = i; if (OnSelected) { OnSelected(i); } return true; } }
	return false;
}

FReply UMsdWidget::NativeOnMouseButtonDown(const FGeometry& G, const FPointerEvent& E)
{
	const int32 Hit = HitTest(G.AbsoluteToLocal(E.GetScreenSpacePosition()));
	Selected = Hit;
	if (OnSelected) { OnSelected(Hit); }
	return FReply::Handled();
}

FReply UMsdWidget::NativeOnMouseMove(const FGeometry& G, const FPointerEvent& E)
{
	Hovered = HitTest(G.AbsoluteToLocal(E.GetScreenSpacePosition()));
	return FReply::Handled();
}

void UMsdWidget::NativeOnMouseLeave(const FPointerEvent& E) { Hovered = INDEX_NONE; Super::NativeOnMouseLeave(E); }

int32 UMsdWidget::NativePaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& MyCullingRect, FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	LayerId = Super::NativePaint(Args, Geo, MyCullingRect, Out, LayerId, InWidgetStyle, bParentEnabled);
	const FVector2D Size = Geo.GetLocalSize();
	AStarshipPawn* S = Ship.Get();
	if (Size.X < 10.0 || Size.Y < 10.0 || !S || !S->Systems) { return LayerId; }
	const TArray<FCompartment>& K = S->Systems->GetCompartments();
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FSlateFontInfo Small = Lcars::Font(11.f, false, 20);
	const FSlateFontInfo Label = Lcars::Font(12.f, true, 40);
	const FSlateFontInfo Big = Lcars::Font(15.f, true, 40);
	const FPaintGeometry PG = Geo.ToPaintGeometry();
	auto Text = [&](const FString& Str, const FVector2D& At, const FSlateFontInfo& F, const FLinearColor& C, int32 Layer, bool bRight = false, bool bCentre = false)
	{
		FVector2D P = At;
		const FVector2D M = Measure->Measure(Str, F);
		if (bRight) { P.X -= M.X; } else if (bCentre) { P.X -= M.X * 0.5; }
		FSlateDrawElement::MakeText(Out, Layer, Geo.ToPaintGeometry(FVector2f(2000.f, 40.f), FSlateLayoutTransform(FVector2f(P))), Str, F, ESlateDrawEffect::None, C);
	};
	auto Lines = [&](TArray<FVector2f>&& Pts, const FLinearColor& C, float Thick, int32 Layer) { FSlateDrawElement::MakeLines(Out, Layer, PG, MoveTemp(Pts), ESlateDrawEffect::None, C, true, Thick); };
	auto Box = [&](const FSlateRect& R, const FLinearColor& C, int32 Layer) { FSlateDrawElement::MakeBox(Out, Layer, Geo.ToPaintGeometry(FVector2f(R.GetSize()), FSlateLayoutTransform(FVector2f(R.GetTopLeft()))), &BoxBrush, ESlateDrawEffect::None, C); };
	auto Outline = [&](const FSlateRect& R, const FLinearColor& C, float Thick, int32 Layer)
	{
		Lines({ FVector2f(R.Left, R.Top), FVector2f(R.Right, R.Top), FVector2f(R.Right, R.Bottom), FVector2f(R.Left, R.Bottom), FVector2f(R.Left, R.Top) }, C, Thick, Layer);
	};

	// Layout: the hull fills the middle, with room above and below for the labels and the facings at the ends.
	const double MarginX = 70.0, MarginY = 130.0;
	const double Zspan = Zmax - Zmin;
	Scale = FMath::Min((Size.X - 2.0 * MarginX) / LengthCm, (Size.Y - 2.0 * MarginY) / Zspan);
	// The bow is drawn to the right: flip X so forward reads as a ship's profile does.
	const double ShipW = LengthCm * Scale, ShipH = Zspan * Scale;
	Origin = FVector2D((Size.X - ShipW) * 0.5, (Size.Y - ShipH) * 0.5);
	const int32 L0 = LayerId + 1, L1 = LayerId + 2, L2 = LayerId + 3, L3 = LayerId + 4, L4 = LayerId + 5;

	// The hull.
	if (Silhouette)
	{
		FSlateDrawElement::MakeBox(Out, L0, Geo.ToPaintGeometry(FVector2f(ShipW, ShipH), FSlateLayoutTransform(FVector2f(Origin))), &SilBrush, ESlateDrawEffect::None, FLinearColor::White);
	}
	// Compartments.
	Rects.SetNum(K.Num());
	struct FLabelSlot { int32 Index; FVector2D Anchor; bool bTop; };
	TArray<FLabelSlot> Slots;
	TArray<int32> Order; for (int32 i = 0; i < K.Num(); ++i) { Order.Add(i); }
	Order.Sort([&](int32 a, int32 b) { return K[a].Extent.X * K[a].Extent.Z > K[b].Extent.X * K[b].Extent.Z; });
	for (int32 i : Order)
	{
		const FCompartment& C = K[i];
		const FVector2D A = Project(FVector(C.Center.X - C.Extent.X, 0, C.Center.Z + C.Extent.Z));
		const FVector2D B = Project(FVector(C.Center.X + C.Extent.X, 0, C.Center.Z - C.Extent.Z));
		const FSlateRect R(A.X, A.Y, B.X, B.Y);
		Rects[i] = R;
		FLinearColor Col = Lcars::Lavender;
		float Integrity = 1.f;
		if (C.System != EShipSystem::Count)
		{
			Integrity = S->Systems->GetSystem(C.System).Integrity;
			Col = Integrity > 0.66f ? FLinearColor(0.45f, 0.85f, 0.5f) : Integrity > 0.33f ? Lcars::Gold : Lcars::Red;
		}
		const float Flash = FMath::Clamp(1.f - (Now - C.LastHitTime) / 1.5f, 0.f, 1.f);
		FLinearColor Fill = FLinearColor::LerpUsingHSV(Col, Lcars::Red, FMath::Clamp(C.Damage, 0.f, 1.f) * 0.7f);
		Fill.A = 0.22f + C.Damage * 0.45f + Flash * 0.4f;
		Box(R, Fill, L1);
		const bool bSel = i == Selected, bHov = i == Hovered;
		Outline(R, bSel ? Lcars::Orange : bHov ? FLinearColor::White : FLinearColor(Col.R, Col.G, Col.B, 0.9f), bSel ? 2.5f : 1.2f, L2);
		if (Flash > 0.f) { Outline(R, FLinearColor(1.f, 1.f, 1.f, Flash), 3.f, L3); }
		// Abbreviated state inside the box when it fits.
		if (R.GetSize().X > 48.0 && R.GetSize().Y > 16.0)
		{
			const FString In = C.System != EShipSystem::Count ? FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Integrity * 100.f)) : (C.Damage > 0.05f ? FString::Printf(TEXT("dmg %d%%"), FMath::RoundToInt(C.Damage * 100.f)) : FString());
			if (!In.IsEmpty()) { Text(In, FVector2D(R.GetCenter().X, R.GetCenter().Y - 7.0), Small, FLinearColor::White, L3, false, true); }
		}
		Slots.Add({ i, FVector2D(R.GetCenter().X, C.Center.Z > (Zmin + Zmax) * 0.5f ? R.Top : R.Bottom), C.Center.Z > (Zmin + Zmax) * 0.5f });
	}
	// Labels in rows above and below, with leader lines; a label that would overlap its neighbour drops a row.
	// Four passes: above and below, and in each the left half placed outward to the left, the right half outward to the
	// right, so the labels stay near their compartments and never run off the display.
	const double Mid = Origin.X + ShipW * 0.5;
	double LeftReach[4];		// how far right the left half's labels got, per row, so the right half starts beyond them
	for (int32 Pass = 0; Pass < 4; ++Pass)
	{
		const bool bTop = Pass < 2, bLeft = (Pass % 2) == 0;
		TArray<FLabelSlot> Group; for (const FLabelSlot& Sl : Slots) { if (Sl.bTop == bTop && (Sl.Anchor.X < Mid) == bLeft) { Group.Add(Sl); } }
		Group.Sort([bLeft](const FLabelSlot& a, const FLabelSlot& b) { return bLeft ? a.Anchor.X > b.Anchor.X : a.Anchor.X < b.Anchor.X; });
		double RowEdge[4]; for (int32 r = 0; r < 4; ++r) { RowEdge[r] = bLeft ? 1e9 : LeftReach[r]; }
		if (bLeft) { for (double& R : LeftReach) { R = -1e9; } }
		for (const FLabelSlot& Sl : Group)
		{
			const FCompartment& C = K[Sl.Index];
			const FString Str = C.Name.ToUpper();
			const double Wd = Measure->Measure(Str, Label).X;
			double X = FMath::Clamp(Sl.Anchor.X - Wd * 0.5, 8.0, Size.X - 8.0 - Wd);
			int32 Row = 0;
			auto Fits = [&](int32 R) { return bLeft ? X + Wd <= RowEdge[R] - 10.0 : X >= RowEdge[R] + 10.0; };
			while (Row < 3 && !Fits(Row)) { ++Row; }
			if (!Fits(Row)) { X = bLeft ? RowEdge[Row] - 10.0 - Wd : RowEdge[Row] + 10.0; X = FMath::Clamp(X, 8.0, Size.X - 8.0 - Wd); }
			RowEdge[Row] = bLeft ? X : X + Wd;
			if (bLeft) { LeftReach[Row] = FMath::Max(LeftReach[Row], X + Wd); }
			const double Y = bTop ? Origin.Y - 26.0 - Row * 20.0 : Origin.Y + ShipH + 12.0 + Row * 20.0;
			const bool bSel = Sl.Index == Selected;
			const FLinearColor LC = bSel ? Lcars::Orange : Lcars::Gold;
			Text(Str, FVector2D(X, Y), Label, LC, L4);
			const FVector2D Elbow(X + Wd * 0.5, bTop ? Y + 16.0 : Y - 2.0);
			Lines({ FVector2f(Sl.Anchor), FVector2f(Elbow) }, FLinearColor(LC.R, LC.G, LC.B, 0.55f), 1.f, L3);
		}
	}
	// Shield facings: bars off the bow and stern, and along the top (port) and bottom (starboard).
	{
		auto Bar = [&](const FVector2D& From, const FVector2D& To, int32 F, const FString& Name, const FVector2D& LabelAt, bool bRight)
		{
			const float Frac = S->ShieldFacingMax[F] > 0.f ? S->ShieldFacing[F] / S->ShieldFacingMax[F] : 0.f;
			const bool bRe = S->Systems->GetReinforcedFacing() == F;
			Lines({ FVector2f(From), FVector2f(To) }, FLinearColor(0.3f, 0.4f, 0.6f, 0.5f), 6.f, L1);
			Lines({ FVector2f(From), FVector2f(FMath::Lerp(From, To, (double)Frac)) }, bRe ? Lcars::Orange : Lcars::Sky, 6.f, L2);
			Text(FString::Printf(TEXT("%s %d%%"), *Name, FMath::RoundToInt(Frac * 100.f)), LabelAt, Small, bRe ? Lcars::Orange : Lcars::Sky, L4, bRight);
		};
		const double Bx0 = Origin.X - 36.0, Bx1 = Origin.X + ShipW + 36.0;
		Bar(FVector2D(Bx1, Origin.Y + ShipH * 0.75), FVector2D(Bx1, Origin.Y + ShipH * 0.25), 0, TEXT("FORE"), FVector2D(Bx1 - 20.0, Origin.Y + ShipH * 0.75 + 8.0), false);
		Bar(FVector2D(Bx0, Origin.Y + ShipH * 0.75), FVector2D(Bx0, Origin.Y + ShipH * 0.25), 1, TEXT("AFT"), FVector2D(Bx0 + 20.0, Origin.Y + ShipH * 0.75 + 8.0), true);
		Bar(FVector2D(Origin.X + ShipW * 0.3, Origin.Y - 100.0), FVector2D(Origin.X + ShipW * 0.7, Origin.Y - 100.0), 2, TEXT("PORT"), FVector2D(Origin.X + ShipW * 0.3, Origin.Y - 120.0), false);
		Bar(FVector2D(Origin.X + ShipW * 0.3, Origin.Y + ShipH + 90.0), FVector2D(Origin.X + ShipW * 0.7, Origin.Y + ShipH + 90.0), 3, TEXT("STARBOARD"), FVector2D(Origin.X + ShipW * 0.3, Origin.Y + ShipH + 96.0), false);
	}
	// Title line: the ship and her condition.
	Text(FString::Printf(TEXT("%s   HULL %d%%   %s"), *S->GetCraftName().ToUpper(), FMath::RoundToInt(S->GetHullFraction() * 100.f), *UShipSystemsComponent::AlertName(S->Systems->GetAlert())), FVector2D(14.0, 8.0), Big, Lcars::Peach, L4);
	return L4;
}

// ---------------------------------------------------------------------------------------------------------------------
// The panel

const FCompartment* UMsdPanel::SelectedCompartment() const
{
	AStarshipPawn* S = GetShip();
	if (!S || !S->Systems || !Display) { return nullptr; }
	const int32 i = Display->GetSelected();
	return S->Systems->GetCompartments().IsValidIndex(i) ? &S->Systems->GetCompartments()[i] : nullptr;
}

void UMsdPanel::BuildContent()
{
	AddSidebarButton(TEXT("Close"), Lcars::Orange, [this]() { Close(); });
	AddSidebarButton(TEXT("Repair Here"), Lcars::Gold, [this]()
	{
		AStarshipPawn* S = GetShip(); const FCompartment* C = SelectedCompartment();
		if (S && S->Systems && C) { if (C->System != EShipSystem::Count) { S->Systems->SetRepairPriority(C->System); } else { S->Systems->Report(EStation::Engineering, FString::Printf(TEXT("Nothing to repair in %s but the bulkheads; the team will patch them as they go."), *C->Name.ToLower())); } }
	});
	AddSidebarButton(TEXT("Power Here"), Lcars::Lilac, [this]()
	{
		AStarshipPawn* S = GetShip(); const FCompartment* C = SelectedCompartment();
		if (S && S->Systems && C && C->System != EShipSystem::Count) { S->Systems->DivertPower(C->System); }
	});
	AddSidebarButton(TEXT("Engineering"), Lcars::Tan, [this]() { if (USlonkUIRoot* UI = GetUIRoot()) { Close(); UI->PushMenu<UEngineeringPanel>(); } });
	AddSidebarButton(TEXT("Sickbay"), Lcars::Sky, [this]() { if (USlonkUIRoot* UI = GetUIRoot()) { Close(); UI->PushMenu<UMedicalPanel>(); } });
	AddSidebarButton(TEXT("Tactical"), Lcars::Red, [this]() { if (USlonkUIRoot* UI = GetUIRoot()) { Close(); UI->PushMenu<UTacticalPanel>(); } });
	AddSidebarButton(TEXT("Main Menu"), Lcars::Lavender, [this]() { if (USlonkUIRoot* UI = GetUIRoot()) { Close(); UI->PushMenu<UMainMenuPanel>(); } });

	Display = CreateWidget<UMsdWidget>(GetOwningPlayer(), UMsdWidget::StaticClass());
	Display->SetShip(GetShip());
	if (UVerticalBoxSlot* S = Content->AddChildToVerticalBox(Display)) { S->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); }
	AddLiveRow(Content, TEXT("Selected"), [this]()
	{
		AStarshipPawn* S = GetShip(); const FCompartment* C = SelectedCompartment();
		if (!S || !S->Systems) { return FString(); }
		if (!C) { return FString(TEXT("click a compartment")); }
		FString T = FString::Printf(TEXT("%s, deck %s"), *C->Name, *C->Deck);
		if (C->System != EShipSystem::Count)
		{
			const FShipSystemState& St = S->Systems->GetSystem(C->System);
			T += FString::Printf(TEXT("   %s %d%% at %d%% power%s"), *UShipSystemsComponent::SystemName(C->System).ToLower(), FMath::RoundToInt(St.Integrity * 100.f), FMath::RoundToInt(St.Power * 100.f), St.bRepairing ? TEXT(", team on it") : TEXT(""));
		}
		if (C->Damage > 0.02f) { T += FString::Printf(TEXT("   structural damage %d%%"), FMath::RoundToInt(C->Damage * 100.f)); }
		const int32 People = FMath::RoundToInt(C->CrewShare * S->Systems->GetCrew().Complement * S->Systems->CrewFactor());
		if (People > 0) { T += FString::Printf(TEXT("   about %d aboard"), People); }
		if (C->Station != EStation::Count) { if (const FOfficer* O = S->Systems->OfficerAt(C->Station)) { T += TEXT("   ") + O->Speaker(); } }
		return T;
	}, Lcars::Peach, 14.f);
}
