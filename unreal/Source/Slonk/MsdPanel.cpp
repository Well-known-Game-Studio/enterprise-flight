#include "MsdPanel.h"
#include "LcarsPanels.h"
#include "PlanetWeather.h"
#include "ShipCompartments.h"
#include "ShipSystemsComponent.h"
#include "SlonkUIRoot.h"
#include "StarshipPawn.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"

// ---------------------------------------------------------------------------------------------------------------------
// The drawing

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
	// Rebinding to the same ship keeps the selection and the rasterised hull; a new ship starts clean.
	if (Ship.Get() == InShip && (InShip == nullptr || Textures.Num() > 0)) { return; }
	Ship = InShip;
	Selected = INDEX_NONE; Hovered = INDEX_NONE; HoveredFacing = INDEX_NONE;
	Rects.Reset();
	Textures.Reset();
	for (FRaster& Ra : Rasters) { Ra = FRaster(); }
	if (InShip) { LengthCm = InShip->GetShipLengthCm(); }
	BuildSilhouettes();
}

void UMsdWidget::SetView(EMsdView InView)
{
	View = InView;
	Hovered = INDEX_NONE; HoveredFacing = INDEX_NONE;
	Rects.Reset();
}

void UMsdWidget::BuildSilhouettes()
{
	AStarshipPawn* S = Ship.Get();
	if (!S || S->GetHullVerts().Num() == 0) { return; }
	HalfBeam = 1.f;
	for (const FVector& P : S->GetHullVerts()) { HalfBeam = FMath::Max(HalfBeam, (float)FMath::Abs(P.Y)); }
	BuildRaster(EMsdView::Elevation);
	BuildRaster(EMsdView::Plan);
}

void UMsdWidget::BuildRaster(EMsdView ForView)
{
	// Side elevation: X along, Z up. Plan: X along, port up. Triangles rasterised into a grid; vertices alone when the
	// index buffer is not readable.
	AStarshipPawn* S = Ship.Get();
	if (!S) { return; }
	const TArray<FVector>& V = S->GetHullVerts();
	const TArray<uint32>& T = S->GetHullTris();
	if (V.Num() == 0) { return; }
	const bool bPlan = ForView == EMsdView::Plan;
	FRaster& Ra = Rasters[(int32)ForView];
	auto UpOf = [bPlan](const FVector& P) { return bPlan ? -(float)P.Y : (float)P.Z; };
	float Umin = 1e9f, Umax = -1e9f;
	for (const FVector& P : V) { Umin = FMath::Min(Umin, UpOf(P)); Umax = FMath::Max(Umax, UpOf(P)); }
	if (bPlan) { const float M = FMath::Max(FMath::Abs(Umin), FMath::Abs(Umax)); Umin = -M; Umax = M; }	// symmetric about the centre line
	const float Pad = (Umax - Umin) * 0.04f; Umin -= Pad; Umax += Pad;
	const int32 W = 768;
	const int32 H = FMath::Clamp((int32)(W * (Umax - Umin) / LengthCm), 64, 512);
	TArray<uint8> Mask; Mask.SetNumZeroed(W * H);
	auto ToGrid = [&](const FVector& P) { return FVector2D((P.X + LengthCm * 0.5f) / LengthCm * W, (Umax - UpOf(P)) / (Umax - Umin) * H); };
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
	// The hull's span per column, for the labels.
	Ra.ColumnU.Init(FVector2f(Umin, Umax), W);
	for (int32 x = 0; x < W; ++x)
	{
		int32 Top = -1, Bottom = -1;
		for (int32 y = 0; y < H; ++y) { if (Mask[y * W + x]) { if (Top < 0) { Top = y; } Bottom = y; } }
		if (Top >= 0) { Ra.ColumnU[x] = FVector2f(Umax - (Bottom + 1.f) / H * (Umax - Umin), Umax - (float)Top / H * (Umax - Umin)); }
	}
	// Pixels: a lavender fill with the deck lines (elevation) or frame stations (plan) through it and a bright outline.
	TArray<FColor> Px; Px.SetNum(W * H);
	const float DeckCm = 330.f, FrameCm = 1500.f;
	for (int32 y = 0; y < H; ++y)
	{
		const float U = Umax - (y + 0.5f) / H * (Umax - Umin);
		const bool bDeckLine = !bPlan && FMath::Fmod(U - Umin + 10000.f * DeckCm, DeckCm) < (Umax - Umin) / H * 1.2f;
		for (int32 x = 0; x < W; ++x)
		{
			const int32 i = y * W + x;
			if (!Mask[i]) { Px[i] = FColor(0, 0, 0, 0); continue; }
			const float Xcm = (x + 0.5f) / W * LengthCm;
			const bool bFrameLine = bPlan && FMath::Fmod(Xcm, FrameCm) < LengthCm / W * 1.2f;
			const bool bEdge = x == 0 || y == 0 || x == W - 1 || y == H - 1 || !Mask[i - 1] || !Mask[i + 1] || !Mask[i - W] || !Mask[i + W];
			if (bEdge) { Px[i] = FColor(235, 200, 255, 255); }
			else if (bDeckLine || bFrameLine) { Px[i] = FColor(60, 40, 90, 210); }
			else { Px[i] = FColor(120, 95, 165, 120); }
		}
	}
	UTexture2D* Tex = SlonkWeather::MakeTexture(Px, W, H, bPlan ? TEXT("MsdPlan") : TEXT("MsdSilhouette"));
	if (Tex)
	{
		Textures.Add(Tex);
		Ra.Brush = FSlateBrush();
		Ra.Brush.SetResourceObject(Tex);
		Ra.Brush.ImageSize = FVector2D(W, H);
		Ra.Brush.DrawAs = ESlateBrushDrawType::Image;
	}
	Ra.W = W; Ra.H = H; Ra.Umin = Umin; Ra.Umax = Umax;
}

bool UMsdWidget::Shown(const FCompartment& C, int32 Index) const
{
	// The plan is the tactical drawing: the shield ring, whatever lies off the centre line (the nacelles) and the fighting
	// systems, phasers, torpedoes, shields and impulse, which read better against the facings. The elevation keeps the
	// decks: quarters, sickbay, engineering and the rest. Whatever is selected shows in both, so the choice carries across.
	const bool bTactical = FMath::Abs(C.Center.Y) > 1.f || C.System == EShipSystem::Phasers || C.System == EShipSystem::Torpedoes || C.System == EShipSystem::Shields || C.System == EShipSystem::Impulse;
	return Index == Selected || (View == EMsdView::Plan ? bTactical : !bTactical);
}

FVector2f UMsdWidget::HullSpanAt(float LocalX) const
{
	const FRaster& Ra = R();
	if (Ra.ColumnU.Num() == 0) { return FVector2f(Ra.Umin, Ra.Umax); }
	const int32 x = FMath::Clamp((int32)((LocalX + LengthCm * 0.5f) / LengthCm * Ra.ColumnU.Num()), 0, Ra.ColumnU.Num() - 1);
	return Ra.ColumnU[x];
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

int32 UMsdWidget::FacingAt(const FVector2D& P) const
{
	AStarshipPawn* S = Ship.Get();
	if (View != EMsdView::Plan || !S || RingPx.Z <= 0.0 || RingPx.W <= 0.0) { return INDEX_NONE; }
	const double Nx = (P.X - RingPx.X) / RingPx.Z, Ny = (P.Y - RingPx.Y) / RingPx.W;
	const double Rr = FMath::Sqrt(Nx * Nx + Ny * Ny);
	if (FMath::Abs(Rr - 1.0) * FMath::Min(RingPx.Z, RingPx.W) > 16.0) { return INDEX_NONE; }
	// Screen back to the ship's frame: along the length, and across with port up on the screen.
	return S->ShieldFacingOfLocal(FVector((P.X - RingPx.X) / Scale, (P.Y - RingPx.Y) / Scale, 0.0));
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
	// The shield ring comes first: where an arc crosses a nacelle (the aft arc does, on the refit) the arc is what the
	// click means; the compartments are everywhere else.
	const FVector2D P = G.AbsoluteToLocal(E.GetScreenSpacePosition());
	const int32 F = FacingAt(P);
	if (F != INDEX_NONE) { if (OnFacingClicked) { OnFacingClicked(F); } return FReply::Handled(); }
	const int32 Hit = HitTest(P);
	Selected = Hit;
	if (OnSelected) { OnSelected(Hit); }
	return FReply::Handled();
}

FReply UMsdWidget::NativeOnMouseMove(const FGeometry& G, const FPointerEvent& E)
{
	const FVector2D P = G.AbsoluteToLocal(E.GetScreenSpacePosition());
	HoveredFacing = FacingAt(P);
	Hovered = HoveredFacing == INDEX_NONE ? HitTest(P) : INDEX_NONE;
	return FReply::Handled();
}

void UMsdWidget::NativeOnMouseLeave(const FPointerEvent& E) { Hovered = INDEX_NONE; HoveredFacing = INDEX_NONE; Super::NativeOnMouseLeave(E); }

int32 UMsdWidget::NativePaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& MyCullingRect, FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	LayerId = Super::NativePaint(Args, Geo, MyCullingRect, Out, LayerId, InWidgetStyle, bParentEnabled);
	const FVector2D Size = Geo.GetLocalSize();
	AStarshipPawn* S = Ship.Get();
	if (Size.X < 10.0 || Size.Y < 10.0 || !S || !S->Systems) { return LayerId; }
	const bool bPlan = View == EMsdView::Plan;
	const FRaster& Ra = R();
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
	auto Box = [&](const FSlateRect& Rc, const FLinearColor& C, int32 Layer) { FSlateDrawElement::MakeBox(Out, Layer, Geo.ToPaintGeometry(FVector2f(Rc.GetSize()), FSlateLayoutTransform(FVector2f(Rc.GetTopLeft()))), &BoxBrush, ESlateDrawEffect::None, C); };
	auto Outline = [&](const FSlateRect& Rc, const FLinearColor& C, float Thick, int32 Layer)
	{
		Lines({ FVector2f(Rc.Left, Rc.Top), FVector2f(Rc.Right, Rc.Top), FVector2f(Rc.Right, Rc.Bottom), FVector2f(Rc.Left, Rc.Bottom), FVector2f(Rc.Left, Rc.Top) }, C, Thick, Layer);
	};

	// Layout: the hull fills the middle. The elevation keeps room above and below for the labels; the plan keeps room all
	// round for the shield ring and its readouts.
	const double Uspan = Ra.Umax - Ra.Umin;
	const double RingA = LengthCm * 0.5 * 1.08, RingB = HalfBeam * 1.4;		// the shield ring's semi-axes, cm
	if (bPlan) { Scale = FMath::Min((Size.X - 2.0 * 96.0) / (2.0 * RingA), (Size.Y - 2.0 * 46.0) / (2.0 * RingB)); }
	else { Scale = FMath::Min((Size.X - 2.0 * 70.0) / LengthCm, (Size.Y - 2.0 * 130.0) / Uspan); }
	// The bow is drawn to the right, as a ship's profile reads; in plan that puts port at the top.
	const double ShipW = LengthCm * Scale, ShipH = Uspan * Scale;
	Origin = FVector2D((Size.X - ShipW) * 0.5, (Size.Y - ShipH) * 0.5);
	const int32 L0 = LayerId + 1, L1 = LayerId + 2, L2 = LayerId + 3, L3 = LayerId + 4, L4 = LayerId + 5;

	// The hull.
	if (Ra.W > 0)
	{
		FSlateDrawElement::MakeBox(Out, L0, Geo.ToPaintGeometry(FVector2f(ShipW, ShipH), FSlateLayoutTransform(FVector2f(Origin))), &Ra.Brush, ESlateDrawEffect::None, FLinearColor::White);
	}
	// Compartments.
	Rects.SetNum(K.Num());
	struct FLabelSlot { int32 Index; FVector2D Anchor; bool bTop; double ClearY; };	// ClearY < 0: rest against the hull's outline
	TArray<FLabelSlot> Slots;
	TArray<int32> Order;
	for (int32 i = 0; i < K.Num(); ++i)
	{
		const FCompartment& C = K[i];
		if (!Shown(C, i)) { Rects[i] = FSlateRect(-10.0, -10.0, -10.0, -10.0); continue; }
		Order.Add(i);
		const FVector2D A = Project(C.Center - C.Extent), B = Project(C.Center + C.Extent);
		Rects[i] = FSlateRect(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y), FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y));
	}
	Order.Sort([&](int32 a, int32 b) { return Rects[a].GetArea() > Rects[b].GetArea(); });
	for (int32 i : Order)
	{
		const FCompartment& C = K[i];
		const FSlateRect Rc = Rects[i];
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
		Box(Rc, Fill, L1);
		const bool bSel = i == Selected, bHov = i == Hovered;
		Outline(Rc, bSel ? Lcars::Orange : bHov ? FLinearColor::White : FLinearColor(Col.R, Col.G, Col.B, 0.9f), bSel ? 2.5f : 1.2f, L2);
		if (Flash > 0.f) { Outline(Rc, FLinearColor(1.f, 1.f, 1.f, Flash), 3.f, L3); }
		// Abbreviated state inside the box when it fits.
		if (Rc.GetSize().X > 48.0 && Rc.GetSize().Y > 16.0)
		{
			const FString In = C.System != EShipSystem::Count ? FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Integrity * 100.f)) : (C.Damage > 0.05f ? FString::Printf(TEXT("dmg %d%%"), FMath::RoundToInt(C.Damage * 100.f)) : FString());
			// A box with a smaller one drawn inside it (the torpedo bay over the shield generators, in plan) keeps its
			// figure in a corner so the two do not print on top of each other.
			bool bNested = false;
			for (int32 j : Order) { if (j != i && Rects[j].GetArea() < Rc.GetArea() && Rc.ContainsPoint(Rects[j].GetCenter())) { bNested = true; break; } }
			if (!In.IsEmpty()) { if (bNested) { Text(In, FVector2D(Rc.Left + 4.0, Rc.Top + 1.0), Small, FLinearColor::White, L3); } else { Text(In, FVector2D(Rc.GetCenter().X, Rc.GetCenter().Y - 7.0), Small, FLinearColor::White, L3, false, true); } }
		}
		// The label goes to the side of the hull the compartment is nearer, measured where the hull actually is at that
		// length: the bridge and the upper decks read above the saucer, the lower saucer decks beneath it, where the
		// space under the saucer's forward half is otherwise empty. In plan, port things read above and starboard below.
		const FVector2f Span = HullSpanAt(C.Center.X);
		const float U = Up(C.Center);
		if (bPlan && FMath::Abs(U) <= 1.f)
		{
			// A centre-line compartment in plan sits deep inside the saucer: its label rests against the box itself,
			// alternately above and below, so a stacked pair (the torpedo bay over the shield generators) reads apart.
			const bool bTop = (Slots.Num() % 2) == 0;
			Slots.Add({ i, FVector2D(Rc.GetCenter().X, bTop ? Rc.Top : Rc.Bottom), bTop, bTop ? Rc.Top : Rc.Bottom });
			continue;
		}
		const bool bTop = bPlan ? U > 0.f : (UpExtent(C.Extent) * 2.f >= (Span.Y - Span.X) * 0.8f || U > Span.X + (Span.Y - Span.X) * 0.55f);
		Slots.Add({ i, FVector2D(Rc.GetCenter().X, bTop ? Rc.Top : Rc.Bottom), bTop, -1.0 });
	}
	// Labels sit just clear of the hull, above or below, with leader lines. Each label's resting height comes from the
	// hull's own outline across the label's width (so one under the saucer hugs the saucer while one under the secondary
	// hull sits beneath that), snapped to a 20 px lattice; a label that would overlap a neighbour on its line either
	// slides a little along it or moves a line further out, whichever disturbs it less. Four passes: above and below,
	// each with the left half placed outward to the left and the right half outward to the right.
	const double Mid = Origin.X + ShipW * 0.5;
	const double Pitch = 20.0;
	const double TopLimit = bPlan ? 30.0 : Origin.Y - 106.0, BottomLimit = bPlan ? Size.Y - 36.0 : Origin.Y + ShipH + 92.0;
	auto HullEdgeY = [&](double X0, double X1, bool bTop)
	{
		// Screen Y of the hull's outline over [X0, X1]: the highest point for labels above, the lowest for those below.
		float U = bTop ? Ra.Umin : Ra.Umax;
		const double Step = FMath::Max(8.0, (X1 - X0) / 12.0);
		for (double X = X0; X <= X1 + 0.5; X += Step)
		{
			const FVector2f Sp = HullSpanAt((float)((X - Origin.X) / Scale - LengthCm * 0.5));
			U = bTop ? FMath::Max(U, Sp.Y) : FMath::Min(U, Sp.X);
		}
		return Origin.Y + (Ra.Umax - U) * Scale;
	};
	TMap<int32, double> LeftReach;		// per line: how far right the left half's labels got, so the right half starts beyond
	for (int32 Pass = 0; Pass < 4; ++Pass)
	{
		const bool bTop = Pass < 2, bLeft = (Pass % 2) == 0;
		TArray<FLabelSlot> Group; for (const FLabelSlot& Sl : Slots) { if (Sl.bTop == bTop && (Sl.Anchor.X < Mid) == bLeft) { Group.Add(Sl); } }
		Group.Sort([bLeft](const FLabelSlot& a, const FLabelSlot& b) { return bLeft ? a.Anchor.X > b.Anchor.X : a.Anchor.X < b.Anchor.X; });
		TMap<int32, double> Edge;			// per line: the edge the next label must stay clear of
		if (bLeft) { LeftReach.Reset(); }
		for (const FLabelSlot& Sl : Group)
		{
			const FCompartment& C = K[Sl.Index];
			const FString Str = C.Name.ToUpper();
			const double Wd = Measure->Measure(Str, Label).X;
			const double Want = FMath::Clamp(Sl.Anchor.X - Wd * 0.5, 8.0, Size.X - 8.0 - Wd);
			// The resting line: just clear of the hull under or over the label's width.
			const double Clear = Sl.ClearY >= 0.0 ? Sl.ClearY : HullEdgeY(Want, Want + Wd, bTop);
			const double Y0 = bTop ? FMath::Max(TopLimit, FMath::FloorToDouble((Clear - 26.0) / Pitch) * Pitch) : FMath::Min(BottomLimit, FMath::CeilToDouble((Clear + 12.0) / Pitch) * Pitch);
			auto EdgeOf = [&](int32 Line) { if (const double* E = Edge.Find(Line)) { return *E; } if (!bLeft) { if (const double* Rr = LeftReach.Find(Line)) { return *Rr; } } return bLeft ? 1e9 : -1e9; };
			// Try the resting line and up to three further out; the cheapest of a slide along the line or a move outward wins.
			double BestX = Want, BestY = Y0, BestCost = 1e18;
			for (int32 Away = 0; Away < 4; ++Away)
			{
				const double Y = bTop ? Y0 - Away * Pitch : Y0 + Away * Pitch;
				if (Y < TopLimit - 0.5 || Y > BottomLimit + 0.5) { break; }
				const int32 Line = FMath::RoundToInt(Y / Pitch);
				const double E = EdgeOf(Line);
				double X = bLeft ? FMath::Min(Want, E - 10.0 - Wd) : FMath::Max(Want, E + 10.0);
				X = FMath::Clamp(X, 8.0, Size.X - 8.0 - Wd);
				const bool bFits = bLeft ? X + Wd <= E - 10.0 : X >= E + 10.0;
				const double Cost = FMath::Abs(X - Want) + Away * 40.0 + (bFits ? 0.0 : 1e6);
				if (Cost < BestCost) { BestCost = Cost; BestX = X; BestY = Y; }
			}
			const int32 Line = FMath::RoundToInt(BestY / Pitch);
			Edge.Add(Line, bLeft ? BestX : BestX + Wd);
			if (bLeft) { double& Rr = LeftReach.FindOrAdd(Line, -1e9); Rr = FMath::Max(Rr, BestX + Wd); }
			const bool bSel = Sl.Index == Selected;
			const FLinearColor LC = bSel ? Lcars::Orange : Lcars::Gold;
			Text(Str, FVector2D(BestX, BestY), Label, LC, L4);
			const FVector2D Elbow(BestX + Wd * 0.5, bTop ? BestY + 16.0 : BestY - 2.0);
			Lines({ FVector2f(Sl.Anchor), FVector2f(Elbow) }, FLinearColor(LC.R, LC.G, LC.B, 0.55f), 1.f, L3);
		}
	}
	// Recent hits flash where they landed, in either view: a ring that opens and fades, pale blue where the shield held
	// and warm where it reached the hull. A struck facing's arc lights for a moment too.
	float FacingFlash[4] = { 0.f, 0.f, 0.f, 0.f };
	for (const FShipHitMark& Hm : S->Systems->GetRecentHits())
	{
		const float Life = Hm.bThrough ? 1.5f : 0.9f;
		const float Age = Now - Hm.Time;
		if (Age < 0.f || Age > Life) { continue; }
		if (Hm.Facing >= 0 && Hm.Facing < 4) { FacingFlash[Hm.Facing] = FMath::Max(FacingFlash[Hm.Facing], 1.f - Age / 0.9f); }
		const FVector2D Cn = Project(Hm.Point);
		const double Rad = 6.0 + Age * (Hm.bThrough ? 16.0 : 24.0);
		TArray<FVector2f> Ring; for (int32 k = 0; k <= 24; ++k) { const double A = k / 24.0 * 2.0 * PI; Ring.Add(FVector2f(Cn.X + Rad * FMath::Cos(A), Cn.Y + Rad * FMath::Sin(A))); }
		const FLinearColor RC = Hm.bThrough ? FLinearColor(1.f, 0.9f, 0.7f) : Lcars::Sky;
		Lines(MoveTemp(Ring), FLinearColor(RC.R, RC.G, RC.B, 1.f - Age / Life), Hm.bThrough ? 2.f : 1.5f, L3);
	}
	// Shield facings. In plan they are a ring round the hull, each facing an arc filled from its middle outward by
	// its strength, clickable to reinforce; the arcs split where the hull's own fore/aft/port/starboard rule splits.
	RingPx = FVector4(0, 0, 0, 0);
	if (bPlan)
	{
		const double Acm = RingA, Bcm = RingB;
		const FVector2D Cn(Origin.X + ShipW * 0.5, Origin.Y + ShipH * 0.5);
		const double Apx = Acm * Scale, Bpx = Bcm * Scale;
		RingPx = FVector4(Cn.X, Cn.Y, Apx, Bpx);
		// Walk the ring from the fore/port boundary so each facing's arc is one run.
		const double T0 = FMath::Atan2(Acm * S->ShipWidthMeters * 50.0, Bcm * LengthCm * 0.5);
		const int32 N = 240;
		TArray<TArray<FVector2f>> Runs; TArray<int32> RunFacing;
		for (int32 k = 0; k <= N; ++k)
		{
			const double A = T0 + k / (double)N * 2.0 * PI;
			const double AlongCm = Acm * FMath::Cos(A), UpCm = Bcm * FMath::Sin(A);	// up = port
			const int32 F = S->ShieldFacingOfLocal(FVector(AlongCm, -UpCm, 0.0));
			const FVector2f P(Cn.X + AlongCm * Scale, Cn.Y - UpCm * Scale);
			if (Runs.Num() == 0 || RunFacing.Last() != F) { Runs.AddDefaulted(); RunFacing.Add(F); }
			Runs.Last().Add(P);
		}
		// The walk starts on a boundary, but a sample can fall just inside the previous facing: fold a stub into its run.
		if (Runs.Num() > 4 && RunFacing[0] == RunFacing.Last()) { Runs.Last().Append(Runs[0]); Runs.RemoveAt(0); RunFacing.RemoveAt(0); }
		static const TCHAR* Names[4] = { TEXT("FORE"), TEXT("AFT"), TEXT("PORT"), TEXT("STARBOARD") };
		for (int32 r = 0; r < Runs.Num(); ++r)
		{
			const int32 F = RunFacing[r];
			if (F < 0 || F > 3 || Runs[r].Num() < 3) { continue; }
			const float Frac = S->ShieldFacingMax[F] > 0.f ? FMath::Clamp(S->ShieldFacing[F] / S->ShieldFacingMax[F], 0.f, 1.f) : 0.f;
			const bool bRe = S->Systems->GetReinforcedFacing() == F, bHov = HoveredFacing == F;
			const float Fl = FacingFlash[F];
			const FLinearColor Track = FMath::Lerp(FLinearColor(0.3f, 0.4f, 0.6f, bHov ? 0.9f : 0.5f), FLinearColor(1.f, 1.f, 1.f, 1.f), Fl);
			const FLinearColor Fill = FMath::Lerp(bRe ? Lcars::Orange : Frac > 0.33f ? Lcars::Sky : Lcars::Red, FLinearColor::White, Fl * 0.7f);
			TArray<FVector2f> Whole = Runs[r];
			Lines(MoveTemp(Whole), Track, bHov || Fl > 0.f ? 8.f : 6.f, L1);
			const int32 M = Runs[r].Num(), Half = FMath::RoundToInt(Frac * (M - 1) * 0.5f);
			const int32 C0 = FMath::Max(0, M / 2 - Half), C1 = FMath::Min(M - 1, M / 2 + Half);
			if (C1 > C0) { TArray<FVector2f> Part; for (int32 k = C0; k <= C1; ++k) { Part.Add(Runs[r][k]); } Lines(MoveTemp(Part), Fill, 6.f, L2); }
			// The readout just outside the arc's middle.
			const FVector2f Pm = Runs[r][M / 2];
			const FVector2D Dir = (FVector2D(Pm) - Cn).GetSafeNormal();
			const FString Str = FString::Printf(TEXT("%s %d%%"), Names[F], FMath::RoundToInt(Frac * 100.f));
			const FLinearColor TC = bRe ? Lcars::Orange : Lcars::Sky;
			if (FMath::Abs(Dir.X) > FMath::Abs(Dir.Y)) { Text(Str, FVector2D(Pm.X + Dir.X * 14.0, Pm.Y - 8.0), Small, TC, L4, Dir.X < 0.0); }
			else { Text(Str, FVector2D(Pm.X, Pm.Y + (Dir.Y < 0.0 ? -24.0 : 8.0)), Small, TC, L4, false, true); }
		}
	}
	// Title line: the ship and her condition.
	Text(FString::Printf(TEXT("%s   HULL %d%%   SHIELDS %d%%   %s   %s"), *S->GetCraftName().ToUpper(), FMath::RoundToInt(S->GetHullFraction() * 100.f), FMath::RoundToInt(S->MaxShieldStrength > 0.f ? S->ShieldStrength / S->MaxShieldStrength * 100.f : 0.f), *UShipSystemsComponent::AlertName(S->Systems->GetAlert()), bPlan ? TEXT("PLAN") : TEXT("ELEVATION")), FVector2D(14.0, 8.0), Big, Lcars::Peach, L4);
	if (bPlan) { Text(TEXT("click a facing to reinforce it"), FVector2D(Size.X - 14.0, 10.0), Small, Lcars::Lilac, L4, true); }
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

void UMsdPanel::NativeOnActivated()
{
	Super::NativeOnActivated();
	if (Display) { Display->SetShip(GetShip()); }
}

void UMsdPanel::Refresh()
{
	// The view buttons light the one showing.
	const int32 V = Display ? (int32)Display->GetView() : -1;
	if (V == LastViewShown) { return; }
	LastViewShown = V;
	const FVector4 Radii(0.f, 0.f, 0.f, 0.f);
	for (int32 i = 0; i < ViewButtons.Num(); ++i) { if (ViewButtons[i]) { Lcars::SetButtonColor(ViewButtons[i], i == V ? Lcars::Orange : Lcars::Lavender, &Radii); } }
}

void UMsdPanel::BuildContent()
{
	AddSidebarButton(TEXT("Close"), Lcars::Orange, [this]() { Close(); });
	ViewButtons.Reset();
	ViewButtons.Add(AddSidebarButton(TEXT("Elevation"), Lcars::Lavender, [this]() { if (Display) { Display->SetView(EMsdView::Elevation); } }));
	ViewButtons.Add(AddSidebarButton(TEXT("Plan"), Lcars::Lavender, [this]() { if (Display) { Display->SetView(EMsdView::Plan); } }));
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
	// A click on the plan's shield ring reinforces that facing, or evens the grid if it already is.
	Display->OnFacingClicked = [this](int32 F) { if (AStarshipPawn* S = GetShip()) { if (S->Systems) { S->Systems->ReinforceShields(S->Systems->GetReinforcedFacing() == F ? -1 : F); } } };
	if (UVerticalBoxSlot* S = Content->AddChildToVerticalBox(Display)) { S->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); }
	AddLiveRow(Content, TEXT("Selected"), [this]()
	{
		AStarshipPawn* S = GetShip(); const FCompartment* C = SelectedCompartment();
		if (!S || !S->Systems) { return FString(); }
		if (!C) { return FString(TEXT("click a compartment")); }
		FString T = C->Deck == TEXT("-") ? C->Name : FString::Printf(TEXT("%s, deck %s"), *C->Name, *C->Deck);
		if (C->System != EShipSystem::Count)
		{
			const FShipSystemState& St = S->Systems->GetSystem(C->System);
			T += FString::Printf(TEXT("   %s %d%% at %d%% power%s"), *UShipSystemsComponent::SystemName(C->System).ToLower(), FMath::RoundToInt(St.Integrity * 100.f), FMath::RoundToInt(St.Power * 100.f), St.bRepairing ? TEXT(", team on it") : TEXT(""));
		}
		if (C->Damage > 0.02f) { T += FString::Printf(TEXT("   structural damage %d%%"), FMath::RoundToInt(C->Damage * 100.f)); }
		const int32 People = FMath::RoundToInt(C->CrewShare * S->Systems->GetCrew().Fit);
		if (People > 0) { T += FString::Printf(TEXT("   about %d aboard"), People); }
		if (C->Station != EStation::Count) { if (const FOfficer* O = S->Systems->OfficerAt(C->Station)) { T += TEXT("   ") + O->Speaker(); } }
		return T;
	}, Lcars::Peach, 14.f);
}
