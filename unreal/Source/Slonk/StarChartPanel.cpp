// Slonk - star chart widget and panel.

#include "StarChartPanel.h"
#include "SlonkUIRoot.h"
#include "LcarsPanels.h"
#include "StarshipPawn.h"
#include "StarshipMovementComponent.h"
#include "StarSystem.h"
#include "ScienceLog.h"
#include "ScienceScannerComponent.h"
#include "PlanetBody.h"
#include "StarBody.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Rendering/DrawElements.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/GameInstance.h"

namespace
{
	constexpr double LocalSystemCm = 2000.0 * AStarSystem::AuCm;   // bodies farther than this from the star are other systems
	constexpr double LocalRadii = 400.0;                            // Local reaches this many radii from its world (the moon rule)
	// Round distances for the grid rings and the scale bar, in cm.
	const double ChartNiceSteps[] = {
		1.0e7, 2.0e7, 5.0e7, 1.0e8, 2.0e8, 5.0e8, 1.0e9, 2.0e9, 5.0e9, 1.0e10, 2.0e10, 5.0e10, 1.0e11, 2.0e11, 5.0e11,
		AStarSystem::AuCm * 0.05, AStarSystem::AuCm * 0.1, AStarSystem::AuCm * 0.25, AStarSystem::AuCm * 0.5, AStarSystem::AuCm,
		AStarSystem::AuCm * 2.0, AStarSystem::AuCm * 5.0, AStarSystem::AuCm * 10.0, AStarSystem::AuCm * 25.0, AStarSystem::AuCm * 50.0, AStarSystem::AuCm * 100.0,
		AStarSystem::LightYearCm * 0.5, AStarSystem::LightYearCm, AStarSystem::LightYearCm * 2.0, AStarSystem::LightYearCm * 5.0, AStarSystem::LightYearCm * 10.0, AStarSystem::LightYearCm * 20.0 };
	double NiceStepBelow(double Want)
	{
		double Pick = ChartNiceSteps[0];
		for (double N : ChartNiceSteps) { if (N <= Want) { Pick = N; } }
		return Pick;
	}
	const FLinearColor ChartGridColor(0.25f, 0.35f, 0.55f, 0.35f);
	const FLinearColor ChartOrbitColor(0.45f, 0.6f, 0.9f, 0.45f);
	const FLinearColor ChartBeltColor(0.8f, 0.6f, 0.4f, 0.6f);

	FString FormatLy(double Cm)
	{
		const double Ly = Cm / AStarSystem::LightYearCm;
		if (Ly >= 0.1) { return FString::Printf(TEXT("%.2f ly"), Ly); }
		const double Au = Cm / AStarSystem::AuCm;
		if (Au >= 0.01) { return FString::Printf(TEXT("%.2f AU"), Au); }
		return FString::Printf(TEXT("%.0f km"), Cm / 1.0e5);
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// Widget

bool UStarChartWidget::Initialize()
{
	const bool bFirst = Super::Initialize();
	if (bFirst && WidgetTree && !WidgetTree->RootWidget && !HasAnyFlags(RF_ClassDefaultObject))
	{
		UBorder* Bg = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ChartBg"));
		Bg->SetBrush(Lcars::Rounded(FLinearColor(0.f, 0.01f, 0.03f, 1.f), FVector4(6.f, 6.f, 6.f, 6.f)));
		Bg->SetPadding(FMargin(0.f));
		Bg->SetVisibility(ESlateVisibility::Visible);
		WidgetTree->RootWidget = Bg;
		SetVisibility(ESlateVisibility::Visible);
		SetIsFocusable(false);
		SetClipping(EWidgetClipping::ClipToBounds);   // a world drawn at true size can outgrow the map when zoomed in
		// The rounded-box shader never clamps its radius, so a fixed radius larger than half the box fills nothing;
		// HalfHeightRadius makes a square box a disc at any size.
		DotBrush = FSlateRoundedBoxBrush(FLinearColor::White, 4.f);
		DotBrush.OutlineSettings.RoundingType = ESlateBrushRoundingType::HalfHeightRadius;
		BoxBrush = Lcars::Rounded(FLinearColor(0.02f, 0.03f, 0.06f, 0.9f), FVector4(6.f, 6.f, 6.f, 6.f));
	}
	return bFirst;
}

void UStarChartWidget::ResetView()
{
	Zoom = 1.0;
	Pan = FVector2D::ZeroVector;
	TiltDeg = 55.f;
	YawOffsetDeg = 0.f;
}

void UStarChartWidget::CycleTilt()
{
	static const float Steps[] = { 0.f, 35.f, 55.f, 75.f };
	int32 Best = 0; float BestD = 1e9f;
	for (int32 i = 0; i < 4; ++i) { const float D = FMath::Abs(Steps[i] - TiltDeg); if (D < BestD) { BestD = D; Best = i; } }
	TiltDeg = Steps[(Best + 1) % 4];
}

void UStarChartWidget::SelectActor(AActor* Actor)
{
	Selected = Actor;
	SelectedLabel.Reset();
	for (const FBody& B : Bodies) { if (B.Actor.Get() == Actor) { SelectedLabel = B.Label; return; } }
	for (const FFarStar& F : FarStars) { if (F.Actor.Get() == Actor) { SelectedLabel = F.Label; return; } }
}

void UStarChartWidget::SetScope(EChartScope S)
{
	if (S == EChartScope::Local) { LocalFocus = nullptr; }   // resolved on the next tick, when the nav list is at hand
	Scope = S;
	Zoom = 1.0;
	Pan = FVector2D::ZeroVector;
}

FString UStarChartWidget::ScopeTitle() const
{
	switch (Scope)
	{
	case EChartScope::Sector: return LocalStarLabel + TEXT(" sector");
	case EChartScope::Local: return FocusLabel;
	default: return LocalStarLabel + TEXT(" system");
	}
}

FString UStarChartWidget::ScopeHint() const
{
	switch (Scope)
	{
	case EChartScope::Sector: return TEXT("The neighbouring suns about ") + LocalStarLabel + TEXT(", distances from the ship.  Click one and Set Course for the transwarp leg.");
	case EChartScope::Local: return FocusLabel + TEXT(" and its neighbourhood: moons, stations and traffic within 400 radii, distances from ") + FocusLabel + TEXT(".");
	default: return TEXT("Click a body or a system to select it.  Drag pans, wheel zooms, right drag tilts and turns.");
	}
}

AActor* UStarChartWidget::ResolveLocalFocus(const TArray<FNavObject>& Nav, const FVector& ShipLoc) const
{
	// The nearest planet to a point; within LocalRadii of it when bNear, else anywhere.
	auto NearestPlanet = [&](const FVector& To, bool bNear) -> AActor*
	{
		AActor* Best = nullptr; double BestD = TNumericLimits<double>::Max();
		for (const FNavObject& O : Nav)
		{
			if (!O.Actor || O.Kind != ENavObjectKind::Planet) { continue; }
			const double D = FVector::Dist(O.Location, To);
			if (bNear && D > LocalRadii * O.RadiusCm) { continue; }
			if (D < BestD) { BestD = D; Best = O.Actor; }
		}
		return Best;
	};
	auto FromActor = [&](AActor* A) -> AActor*
	{
		if (!A) { return nullptr; }
		if (APlanetBody* P = Cast<APlanetBody>(A))
		{
			if (!P->IsMoon()) { return P; }
			return AStarSystem::PrimaryOf(GetWorld(), P);
		}
		if (Cast<AStarBody>(A)) { return nullptr; }
		return NearestPlanet(A->GetActorLocation(), true);
	};
	APlayerController* PC = GetOwningPlayer();
	const AStarshipHUD* H = PC ? Cast<AStarshipHUD>(PC->GetHUD()) : nullptr;
	if (AActor* F = FromActor(Selected.Get())) { return F; }
	if (AActor* F = FromActor(H && H->NavTarget.IsValid() ? H->NavTarget.Get() : nullptr)) { return F; }
	if (AActor* F = NearestPlanet(ShipLoc, true)) { return F; }
	return NearestPlanet(ShipLoc, false);
}

void UStarChartWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	Time += InDeltaTime;
	APlayerController* PC = GetOwningPlayer();
	AStarshipHUD* H = PC ? Cast<AStarshipHUD>(PC->GetHUD()) : nullptr;
	APawn* Ship = PC ? PC->GetPawn() : nullptr;
	Bodies.Reset();
	FarStars.Reset();
	if (!H || !Ship) { return; }
	const UWorld* W = GetWorld();
	const UScienceLogSubsystem* Log = (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UScienceLogSubsystem>() : nullptr;
	const TArray<FNavObject>& Nav = H->GetNavObjects(true);
	const FVector Star = H->GetStarLocation();
	const FVector ShipLoc = Ship->GetActorLocation();
	for (const FNavObject& O : Nav) { if (O.Kind == ENavObjectKind::Star && O.Location.Equals(Star, 1.0)) { LocalStarLabel = O.Label; break; } }
	if (LocalStarLabel.IsEmpty()) { LocalStarLabel = TEXT("Sol"); }

	// Local needs a world to centre on; without one it falls back to the system.
	if (Scope == EChartScope::Local && !LocalFocus.IsValid()) { LocalFocus = ResolveLocalFocus(Nav, ShipLoc); }
	if (Scope == EChartScope::Local && !LocalFocus.IsValid()) { Scope = EChartScope::System; }
	FVector Origin = Star;
	double FocusR = 0.0;
	if (Scope == EChartScope::Local)
	{
		Origin = LocalFocus->GetActorLocation();
		for (const FNavObject& O : Nav) { if (O.Actor == LocalFocus.Get()) { FocusR = O.RadiusCm; FocusLabel = O.Label; break; } }
		if (FocusR <= 0.0) { FocusR = 6.0e8; }
		LocalRangeCm = LocalRadii * FocusR;
	}

	for (const FNavObject& O : Nav)
	{
		if (!O.Actor || O.Kind == ENavObjectKind::Belt) { continue; }
		const double FromStar = FVector::Dist(O.Location, Star);
		FBody B;
		switch (Scope)
		{
		case EChartScope::Sector:
			if (O.Kind != ENavObjectKind::Star) { continue; }
			B.Tag = FormatLy(FVector::Dist(O.Location, ShipLoc));
			break;
		case EChartScope::Local:
		{
			const double D = FVector::Dist(O.Location, Origin);
			const bool bFocus = O.Actor == LocalFocus.Get();
			if (!bFocus && (D > LocalRangeCm || O.Kind == ENavObjectKind::Star || O.Kind == ENavObjectKind::Nebula)) { continue; }
			if (!bFocus) { B.Tag = FormatLy(FMath::Max(D - O.RadiusCm - FocusR, 0.0)); }
			break;
		}
		default:
			if (FromStar > LocalSystemCm)
			{
				if (O.Kind == ENavObjectKind::Star)
				{
					FFarStar F; F.Actor = O.Actor; F.Label = O.Label; F.DistCm = FVector::Dist(O.Location, ShipLoc);
					FarStars.Add(F);
				}
				continue;
			}
			break;
		}
		B.Actor = O.Actor; B.Label = O.Label; B.Kind = O.Kind; B.Rel = O.Location - Origin; B.RadiusCm = O.RadiusCm;
		B.bScanned = Log && Log->IsScanned(UScienceScannerComponent::KeyFor(O.Actor, O.Label));
		B.Color = H->KindColor(O.Kind);
		if (const AStarBody* SB = Cast<AStarBody>(O.Actor)) { B.Color = Lcars::Brighter(SB->StarColor, 0.12f); B.Color.A = 1.f; }
		Bodies.Add(B);
	}
	FarStars.Sort([](const FFarStar& A, const FFarStar& B) { return A.DistCm < B.DistCm; });
	ShipRel = ShipLoc - Origin;
	ShipFwd = Ship->GetActorForwardVector();
	ShipYawDeg = Ship->GetActorRotation().Yaw;
	BeltRadiusCm = Scope == EChartScope::System ? H->GetBeltRadiusCm() : 0.0;
	// The default fit: Sector holds every sun; System the planets, the belt and the ship (nebulae sit far out and
	// would shrink everything); Local the world's moons and neighbours, never less than ten radii.
	switch (Scope)
	{
	case EChartScope::Sector:
		MaxR = AStarSystem::LightYearCm;
		for (const FBody& B : Bodies) { MaxR = FMath::Max(MaxR, FVector2D(B.Rel.X, B.Rel.Y).Size()); }
		break;
	case EChartScope::Local:
		MaxR = 10.0 * FocusR;
		for (const FBody& B : Bodies) { MaxR = FMath::Max(MaxR, FVector2D(B.Rel.X, B.Rel.Y).Size()); }
		if (ShipRel.Size() <= LocalRangeCm) { MaxR = FMath::Max(MaxR, FVector2D(ShipRel.X, ShipRel.Y).Size()); }
		break;
	default:
		MaxR = FMath::Max(FVector::Dist2D(ShipLoc, Star), BeltRadiusCm);
		for (const FBody& B : Bodies) { if (B.Kind != ENavObjectKind::Nebula) { MaxR = FMath::Max(MaxR, FVector2D(B.Rel.X, B.Rel.Y).Size()); } }
		break;
	}
	MaxR = FMath::Max(MaxR, 1.0);
	// A selection made before the first gather gets its label now.
	if (Selected.IsValid() && SelectedLabel.IsEmpty()) { SelectActor(Selected.Get()); }
	if (bCentreOnShipPending && LaidOutSize.X > 0.0)
	{
		bCentreOnShipPending = false;
		Zoom = FMath::Clamp(MaxR / FMath::Max(ShipRel.Size(), MaxR * 0.02) * 0.5, 1.0, 400.0);
		Scale = (FMath::Min(LaidOutSize.X, LaidOutSize.Y) * 0.42 / MaxR) * Zoom;
		Pan = FVector2D::ZeroVector;
		Pan = Centre - Project(ShipRel);
	}
}

void UStarChartWidget::CentreOnShip()
{
	bCentreOnShipPending = true;
}

FVector2D UStarChartWidget::Project(const FVector& Rel) const
{
	const float Yaw = (bHeadingUp ? ShipYawDeg : 0.f) + YawOffsetDeg;
	const FVector R = FRotator(0.f, -Yaw, 0.f).RotateVector(Rel);
	const float Tilt = FMath::DegreesToRadians(TiltDeg);
	const double X = R.Y * Scale;
	const double Y = -R.X * Scale * FMath::Cos(Tilt) - R.Z * Scale * FMath::Sin(Tilt);
	return Centre + Pan + FVector2D(X, Y);
}

void UStarChartWidget::Layout(const FVector2D& Size) const
{
	LaidOutSize = Size;
	Centre = Size * 0.5;
	Scale = (FMath::Min(Size.X, Size.Y) * 0.42 / MaxR) * Zoom;
	for (FBody& B : Bodies)
	{
		B.Screen = Project(B.Rel);
		B.Base = Project(FVector(B.Rel.X, B.Rel.Y, 0.0));
	}
}

bool UStarChartWidget::IsShown(const FBody& B, const AStarshipHUD* H) const
{
	// The star, the course and the selection always show; everything else obeys the HUD's nav filter.
	if (B.Kind == ENavObjectKind::Star || !H) { return true; }
	if (LocalFocus.IsValid() && B.Actor.Get() == LocalFocus.Get()) { return true; }
	if (H->NavTarget.IsValid() && B.Actor.Get() == H->NavTarget.Get()) { return true; }
	if (Selected.IsValid() && B.Actor.Get() == Selected.Get()) { return true; }
	return H->PassesNavFilter(B.Kind);
}

int32 UStarChartWidget::HitBody(const FVector2D& Local) const
{
	APlayerController* PC = GetOwningPlayer();
	const AStarshipHUD* H = PC ? Cast<AStarshipHUD>(PC->GetHUD()) : nullptr;
	int32 Best = -1; double BestD = 18.0 * 18.0;
	for (int32 i = 0; i < Bodies.Num(); ++i)
	{
		// Only what is drawn can be picked, so a hidden nebula never steals the click from the world under it.
		if (!IsShown(Bodies[i], H)) { continue; }
		const double D = FVector2D::DistSquared(Bodies[i].Screen, Local);
		if (D < BestD) { BestD = D; Best = i; }
	}
	return Best;
}

int32 UStarChartWidget::HitFarStar(const FVector2D& Local) const
{
	for (int32 i = 0; i < FarStars.Num(); ++i) { if (FarStars[i].Hit.ContainsPoint(Local)) { return i; } }
	return -1;
}

FString UStarChartWidget::DescribeBody(const FBody& B, TArray<FString>& OutLines) const
{
	APlayerController* PC = GetOwningPlayer();
	AStarshipHUD* H = PC ? Cast<AStarshipHUD>(PC->GetHUD()) : nullptr;
	const TCHAR* Kind = TEXT("");
	switch (B.Kind)
	{
	case ENavObjectKind::Star: Kind = TEXT("Star"); break;
	case ENavObjectKind::Planet: Kind = TEXT("Planet"); break;
	case ENavObjectKind::Moon: Kind = TEXT("Moon"); break;
	case ENavObjectKind::Station: Kind = TEXT("Station"); break;
	case ENavObjectKind::Ship: Kind = TEXT("Ship"); break;
	case ENavObjectKind::Hostile: Kind = TEXT("Hostile"); break;
	case ENavObjectKind::Nebula: Kind = TEXT("Nebula"); break;
	default: break;
	}
	const double Dist = FMath::Max((B.Rel - ShipRel).Size() - B.RadiusCm, 0.0);
	OutLines.Add(FString::Printf(TEXT("%s   %s from the ship"), Kind, H ? *H->FormatDistance(Dist) : TEXT("")));
	if (B.bScanned)
	{
		const UWorld* W = GetWorld();
		const UScienceLogSubsystem* Log = (W && W->GetGameInstance()) ? W->GetGameInstance()->GetSubsystem<UScienceLogSubsystem>() : nullptr;
		const FScanRecord* R = (Log && B.Actor.IsValid()) ? Log->Find(UScienceScannerComponent::KeyFor(B.Actor.Get(), B.Label)) : nullptr;
		if (R)
		{
			for (int32 i = 0; i < FMath::Min(R->Facts.Num(), 3); ++i) { OutLines.Add(R->Facts[i]); }
			if (!R->Discovery.IsEmpty()) { OutLines.Add(TEXT("Discovery: ") + R->Discovery); }
		}
		else { OutLines.Add(TEXT("Catalogued")); }
	}
	else
	{
		OutLines.Add(TEXT("Not yet scanned"));
	}
	return B.Label;
}

int32 UStarChartWidget::NativePaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& MyCullingRect, FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	LayerId = Super::NativePaint(Args, Geo, MyCullingRect, Out, LayerId, InWidgetStyle, bParentEnabled);
	const FVector2D Size = Geo.GetLocalSize();
	if (Size.X < 10.0 || Size.Y < 10.0) { return LayerId; }
	Layout(Size);
	const FPaintGeometry PG = Geo.ToPaintGeometry();
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const FSlateFontInfo Small = Lcars::Font(11.f, false, 20);
	const FSlateFontInfo Label = Lcars::Font(13.f, true, 30);
	const FSlateFontInfo Big = Lcars::Font(15.f, true, 40);
	auto Text = [&](const FString& S, const FVector2D& At, const FSlateFontInfo& F, const FLinearColor& C, int32 Layer, bool bRight = false)
	{
		FVector2D P = At;
		if (bRight) { P.X -= Measure->Measure(S, F).X; }
		FSlateDrawElement::MakeText(Out, Layer, Geo.ToPaintGeometry(FVector2f(2000.f, 40.f), FSlateLayoutTransform(FVector2f(P))), S, F, ESlateDrawEffect::None, C);
	};
	auto Lines = [&](TArray<FVector2f>&& Pts, const FLinearColor& C, float Thick, int32 Layer)
	{
		FSlateDrawElement::MakeLines(Out, Layer, PG, MoveTemp(Pts), ESlateDrawEffect::None, C, true, Thick);
	};
	auto Dot = [&](const FVector2D& P, float D, const FLinearColor& C, int32 Layer)
	{
		FSlateDrawElement::MakeBox(Out, Layer, Geo.ToPaintGeometry(FVector2f(D, D), FSlateLayoutTransform(FVector2f(P - FVector2D(D * 0.5, D * 0.5)))), &DotBrush, ESlateDrawEffect::None, C);
	};
	auto Ring = [&](const FVector2D& P, float R, const FLinearColor& C, float Thick, int32 Layer, int32 N = 40)
	{
		TArray<FVector2f> Pts; Pts.Reserve(N + 1);
		for (int32 i = 0; i <= N; ++i) { const float A = 2.f * PI * i / N; Pts.Add(FVector2f(P.X + R * FMath::Cos(A), P.Y + R * FMath::Sin(A))); }
		Lines(MoveTemp(Pts), C, Thick, Layer);
	};
	auto Orbit = [&](double R, const FLinearColor& C, float Thick, int32 Layer, bool bDashed)
	{
		const int32 N = 96;
		if (!bDashed)
		{
			TArray<FVector2f> Pts; Pts.Reserve(N + 1);
			for (int32 i = 0; i <= N; ++i) { const double A = 2.0 * PI * i / N; Pts.Add(FVector2f(Project(FVector(R * FMath::Cos(A), R * FMath::Sin(A), 0.0)))); }
			Lines(MoveTemp(Pts), C, Thick, Layer);
		}
		else
		{
			for (int32 i = 0; i < N; i += 2)
			{
				const double A0 = 2.0 * PI * i / N, A1 = 2.0 * PI * (i + 1) / N;
				Lines({ FVector2f(Project(FVector(R * FMath::Cos(A0), R * FMath::Sin(A0), 0.0))), FVector2f(Project(FVector(R * FMath::Cos(A1), R * FMath::Sin(A1), 0.0))) }, C, Thick, Layer);
			}
		}
	};
	const int32 L0 = LayerId + 1, L1 = LayerId + 2, L2 = LayerId + 3, L3 = LayerId + 4, L4 = LayerId + 5;

	// Grid: the plane as concentric rings at a round distance, labelled, and the cardinal axes.
	{
		const double Step = NiceStepBelow(MaxR / Zoom / 4.0);
		const int32 LabelEvery = FMath::Max(1, FMath::CeilToInt(72.0 / (Step * Scale)));   // labels need room along the axis
		int32 N = 0;
		for (double R = Step; R * Scale < FMath::Max(Size.X, Size.Y) * 0.8 && N < 24; R += Step, ++N)
		{
			Orbit(R, ChartGridColor, 1.f, L0, false);
			if ((N + 1) % LabelEvery == 0) { Text(FormatLy(R), Project(FVector(0.0, R, 0.0)) + FVector2D(4.0, -15.0), Small, FLinearColor(0.5f, 0.6f, 0.8f, 0.55f), L0); }
		}
		const double Axis = FMath::Max(Size.X, Size.Y) / Scale;
		Lines({ FVector2f(Project(FVector(-Axis, 0, 0))), FVector2f(Project(FVector(Axis, 0, 0))) }, ChartGridColor, 1.f, L0);
		Lines({ FVector2f(Project(FVector(0, -Axis, 0))), FVector2f(Project(FVector(0, Axis, 0))) }, ChartGridColor, 1.f, L0);
	}
	// Orbits: the planets' about the star, the moons' about the world; and the belt.
	if (Scope != EChartScope::Sector)
	{
		const ENavObjectKind Orbiting = Scope == EChartScope::Local ? ENavObjectKind::Moon : ENavObjectKind::Planet;
		TArray<double> Orbits;
		for (const FBody& B : Bodies)
		{
			if (B.Kind != Orbiting) { continue; }
			const double R = FVector2D(B.Rel.X, B.Rel.Y).Size();
			bool bDup = false;
			for (double E : Orbits) { if (FMath::Abs(E - R) < R * 0.03) { bDup = true; break; } }
			if (!bDup) { Orbits.Add(R); }
		}
		for (double R : Orbits) { Orbit(R, ChartOrbitColor, 1.2f, L1, false); }
		if (BeltRadiusCm > 0.0) { Orbit(BeltRadiusCm, ChartBeltColor, 2.f, L1, true); }
	}
	// Bodies.
	APlayerController* PC = GetOwningPlayer();
	AStarshipHUD* H = PC ? Cast<AStarshipHUD>(PC->GetHUD()) : nullptr;
	const int32 HoverI = Hover.X >= 0.0 ? HitBody(Hover) : -1;
	// Labels step down rather than print over one another (binaries, a ship beside its world).
	TArray<FSlateRect> Placed;
	auto PlaceLabel = [&](FVector2D At, const FVector2D& Sz) -> FVector2D
	{
		for (int32 k = 0; k < 8; ++k)
		{
			const FSlateRect R(At.X, At.Y, At.X + Sz.X, At.Y + Sz.Y);
			bool bHit = false;
			for (const FSlateRect& P : Placed) { if (FSlateRect::DoRectanglesIntersect(R, P)) { bHit = true; break; } }
			if (!bHit) { Placed.Add(R); return At; }
			At.Y += Sz.Y + 1.0;
		}
		Placed.Add(FSlateRect(At.X, At.Y, At.X + Sz.X, At.Y + Sz.Y));
		return At;
	};
	for (int32 i = 0; i < Bodies.Num(); ++i)
	{
		const FBody& B = Bodies[i];
		const bool bIsNav = H && H->NavTarget.IsValid() && B.Actor.Get() == H->NavTarget.Get();
		const bool bIsSel = Selected.IsValid() && B.Actor.Get() == Selected.Get();
		if (!IsShown(B, H)) { continue; }
		const FLinearColor C = B.Color;
		// A marker, or the body at true size once the zoom makes it bigger than one.
		const float Mark = B.Kind == ENavObjectKind::Star ? 12.f : B.Kind == ENavObjectKind::Planet ? 8.f : B.Kind == ENavObjectKind::Moon ? 4.f : 5.f;
		const float D = FMath::Clamp((float)(2.0 * B.RadiusCm * Scale), Mark, 4000.f);
		if (FMath::Abs(B.Screen.Y - B.Base.Y) > 1.0) { Lines({ FVector2f(B.Base), FVector2f(B.Screen) }, FLinearColor(C.R, C.G, C.B, 0.35f), 1.f, L1); }
		if (B.Kind == ENavObjectKind::Star) { Dot(B.Screen, D * 2.6f, FLinearColor(C.R, C.G, C.B, 0.18f), L2); }
		Dot(B.Screen, D, C, L2);
		if (bIsSel) { Ring(B.Screen, D * 0.5f + 9.f, Lcars::Orange, 2.f, L3); }
		if (bIsNav)
		{
			const float Pulse = 0.6f + 0.4f * FMath::Sin(Time * 5.f);
			Ring(B.Screen, D * 0.5f + 15.f, FLinearColor(1.f, 1.f, 1.f, Pulse), 1.5f, L3, 48);
		}
		if (i == HoverI) { Ring(B.Screen, D * 0.5f + 6.f, FLinearColor(1.f, 1.f, 1.f, 0.6f), 1.f, L3); }
		const bool bLabel = Scope != EChartScope::System || B.Kind == ENavObjectKind::Star || B.Kind == ENavObjectKind::Planet || B.Kind == ENavObjectKind::Nebula || bIsNav || bIsSel || i == HoverI || Zoom > 6.0 || (B.Kind == ENavObjectKind::Station && Zoom > 2.5);
		if (bLabel)
		{
			const FSlateFontInfo& F = B.Kind == ENavObjectKind::Star ? Big : Label;
			const FVector2D NameSz = Measure->Measure(B.Label, F);
			const double TagW = (B.bScanned ? 14.0 : 0.0) + (B.Tag.IsEmpty() ? 0.0 : Measure->Measure(B.Tag, Small).X + 6.0);
			const FVector2D At = PlaceLabel(B.Screen + FVector2D(D * 0.5 + 6.0, -8.0), FVector2D(NameSz.X + 6.0 + TagW, NameSz.Y));
			Text(B.Label, At, F, FLinearColor(C.R, C.G, C.B, 0.95f), L3);
			if (!At.Equals(B.Screen + FVector2D(D * 0.5 + 6.0, -8.0))) { Lines({ FVector2f(B.Screen + FVector2D(D * 0.5, 0.0)), FVector2f(At + FVector2D(-2.0, NameSz.Y * 0.6)) }, FLinearColor(C.R, C.G, C.B, 0.5f), 1.f, L3); }
			double X = At.X + NameSz.X + 6.0;
			if (B.bScanned)
			{
				// A tick after the name: catalogued.
				const FVector2D T(X, At.Y + 10.0);
				Lines({ FVector2f(T), FVector2f(T + FVector2D(3.0, 4.0)), FVector2f(T + FVector2D(9.0, -5.0)) }, Lcars::Gold, 1.5f, L3);
				X += 14.0;
			}
			if (!B.Tag.IsEmpty()) { Text(B.Tag, FVector2D(X, At.Y + 2.0), Small, Lcars::Dim, L3); }
		}
	}
	// The ship.
	{
		const FVector2D P = Project(ShipRel);
		const FVector2D Base = Project(FVector(ShipRel.X, ShipRel.Y, 0.0));
		const FVector2D Tip = Project(ShipRel + ShipFwd * (MaxR / Zoom * 0.06));
		const float Blink = 0.6f + 0.4f * FMath::Sin(Time * 8.f);
		Lines({ FVector2f(Base), FVector2f(P) }, FLinearColor(Lcars::Sky.R, Lcars::Sky.G, Lcars::Sky.B, 0.4f), 1.f, L1);
		Lines({ FVector2f(P), FVector2f(Tip) }, FLinearColor(Lcars::Sky.R, Lcars::Sky.G, Lcars::Sky.B, Blink), 2.f, L3);
		Dot(P, 7.f, FLinearColor(Lcars::Sky.R, Lcars::Sky.G, Lcars::Sky.B, Blink), L3);
		Text(TEXT("YOU"), PlaceLabel(P + FVector2D(8.0, 4.0), Measure->Measure(TEXT("YOU"), Small)), Small, Lcars::Sky, L3);
	}
	// Other systems, listed top right in System scope; each row is a hit target.
	if (FarStars.Num() > 0)
	{
		double Y = 10.0;
		Text(TEXT("OTHER SYSTEMS"), FVector2D(Size.X - 12.0, Y), Small, Lcars::Lilac, L4, true); Y += 18.0;
		for (FFarStar& F : FarStars)
		{
			const bool bSel = Selected.IsValid() && F.Actor.Get() == Selected.Get();
			const bool bNav = H && H->NavTarget.IsValid() && F.Actor.Get() == H->NavTarget.Get();
			const FString Row = FString::Printf(TEXT("%s   %s"), *F.Label.ToUpper(), *FormatLy(F.DistCm));
			const FVector2D Sz = Measure->Measure(Row, Label);
			const FVector2D At(Size.X - 12.0 - Sz.X, Y);
			F.Hit = FSlateRect(At.X - 6.0, At.Y - 2.0, Size.X - 6.0, At.Y + Sz.Y + 2.0);
			if (bSel || bNav)
			{
				FSlateDrawElement::MakeBox(Out, L3, Geo.ToPaintGeometry(FVector2f(F.Hit.GetSize()), FSlateLayoutTransform(FVector2f(F.Hit.GetTopLeft()))), &BoxBrush, ESlateDrawEffect::None, bNav ? FLinearColor(1.f, 1.f, 1.f, 0.18f) : FLinearColor(Lcars::Orange.R, Lcars::Orange.G, Lcars::Orange.B, 0.25f));
			}
			Text(Row, At, Label, bSel ? Lcars::Orange : Lcars::Gold, L4);
			Y += Sz.Y + 6.0;
		}
	}
	// Scale bar, bottom left.
	{
		const double Pick = NiceStepBelow(Size.X * 0.25 / Scale);
		const double Px = Pick * Scale;
		const FVector2D A(14.0, Size.Y - 18.0);
		Lines({ FVector2f(A), FVector2f(A + FVector2D(Px, 0.0)) }, Lcars::Peach, 2.f, L4);
		Lines({ FVector2f(A + FVector2D(0.0, -5.0)), FVector2f(A + FVector2D(0.0, 5.0)) }, Lcars::Peach, 2.f, L4);
		Lines({ FVector2f(A + FVector2D(Px, -5.0)), FVector2f(A + FVector2D(Px, 5.0)) }, Lcars::Peach, 2.f, L4);
		Text(FormatLy(Pick), A + FVector2D(Px + 8.0, -8.0), Small, Lcars::Peach, L4);
		Text(FString::Printf(TEXT("tilt %.0f   %s   zoom %.1fx"), TiltDeg, bHeadingUp ? TEXT("heading up") : TEXT("north up"), Zoom), FVector2D(14.0, Size.Y - 38.0), Small, Lcars::Dim, L4);
	}
	// Hover card.
	if (HoverI >= 0)
	{
		TArray<FString> InfoLines;
		const FString Title = DescribeBody(Bodies[HoverI], InfoLines);
		double Wd = Measure->Measure(Title, Big).X;
		for (const FString& S : InfoLines) { Wd = FMath::Max(Wd, (double)Measure->Measure(S, Small).X); }
		const FVector2D Box(Wd + 24.0, 30.0 + InfoLines.Num() * 16.0);
		FVector2D At = Hover + FVector2D(18.0, 12.0);
		if (At.X + Box.X > Size.X) { At.X = Hover.X - Box.X - 10.0; }
		if (At.Y + Box.Y > Size.Y) { At.Y = Size.Y - Box.Y - 4.0; }
		FSlateDrawElement::MakeBox(Out, L4, Geo.ToPaintGeometry(FVector2f(Box), FSlateLayoutTransform(FVector2f(At))), &BoxBrush, ESlateDrawEffect::None, FLinearColor::White);
		Text(Title, At + FVector2D(12.0, 6.0), Big, Lcars::Peach, L4 + 1);
		for (int32 i = 0; i < InfoLines.Num(); ++i) { Text(InfoLines[i], At + FVector2D(12.0, 28.0 + i * 16.0), Small, Lcars::Dim, L4 + 1); }
	}
	return L4 + 2;
}

FReply UStarChartWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& E)
{
	LastMouse = InGeometry.AbsoluteToLocal(E.GetScreenSpacePosition());
	bDragged = false;
	if (E.GetEffectingButton() == EKeys::LeftMouseButton) { bLeftDown = true; }
	else if (E.GetEffectingButton() == EKeys::RightMouseButton) { bRightDown = true; }
	else { return FReply::Unhandled(); }
	return FReply::Handled().CaptureMouse(TakeWidget());
}

FReply UStarChartWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& E)
{
	const FVector2D Local = InGeometry.AbsoluteToLocal(E.GetScreenSpacePosition());
	if (E.GetEffectingButton() == EKeys::LeftMouseButton && bLeftDown)
	{
		bLeftDown = false;
		if (!bDragged)
		{
			const int32 I = HitBody(Local);
			const int32 F = I < 0 ? HitFarStar(Local) : -1;
			if (I >= 0) { Selected = Bodies[I].Actor; SelectedLabel = Bodies[I].Label; }
			else if (F >= 0) { Selected = FarStars[F].Actor; SelectedLabel = FarStars[F].Label; }
			else { Selected = nullptr; SelectedLabel.Reset(); }
		}
		return FReply::Handled().ReleaseMouseCapture();
	}
	if (E.GetEffectingButton() == EKeys::RightMouseButton && bRightDown)
	{
		bRightDown = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

FReply UStarChartWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& E)
{
	const FVector2D Local = InGeometry.AbsoluteToLocal(E.GetScreenSpacePosition());
	Hover = Local;
	const FVector2D Delta = Local - LastMouse;
	if (bLeftDown)
	{
		if (Delta.Size() > 2.0) { bDragged = true; }
		Pan += Delta;
		LastMouse = Local;
		return FReply::Handled();
	}
	if (bRightDown)
	{
		bDragged = true;
		YawOffsetDeg += (float)Delta.X * 0.3f;
		TiltDeg = FMath::Clamp(TiltDeg + (float)Delta.Y * 0.3f, 0.f, 85.f);
		LastMouse = Local;
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply UStarChartWidget::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& E)
{
	const FVector2D Local = InGeometry.AbsoluteToLocal(E.GetScreenSpacePosition());
	const double Factor = FMath::Pow(1.2, (double)E.GetWheelDelta());
	const double NewZoom = FMath::Clamp(Zoom * Factor, 0.2, 400.0);
	const double K = NewZoom / Zoom;
	// Keep the point under the cursor where it is.
	Pan = (Local - Centre) - ((Local - Centre) - Pan) * K;
	Zoom = NewZoom;
	return FReply::Handled();
}

FReply UStarChartWidget::NativeOnMouseButtonDoubleClick(const FGeometry& InGeometry, const FPointerEvent& E)
{
	const FVector2D Local = InGeometry.AbsoluteToLocal(E.GetScreenSpacePosition());
	const int32 I = HitBody(Local);
	const int32 F = I < 0 ? HitFarStar(Local) : -1;
	AActor* A = I >= 0 ? Bodies[I].Actor.Get() : F >= 0 ? FarStars[F].Actor.Get() : nullptr;
	if (A)
	{
		SelectActor(A);
		if (OnSetCourse) { OnSetCourse(A); }
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void UStarChartWidget::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseLeave(InMouseEvent);
	Hover = FVector2D(-1.0, -1.0);
}

// ---------------------------------------------------------------------------------------------------------------------
// Panel

void UStarChartPanel::BuildContent()
{
	Chart = CreateWidget<UStarChartWidget>(GetOwningPlayer(), UStarChartWidget::StaticClass());
	Chart->OnSetCourse = [this](AActor* A) { SetCourse(A); };
	// Scope tabs across the top: Sector, System, Local, then the title of what is shown.
	{
		UHorizontalBox* Tabs = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		auto Tab = [&](const TCHAR* L, EChartScope S)
		{
			UButton* B = Lcars::MakeButton(WidgetTree, L, Lcars::Lilac);
			Bind(B, [this, S]() { if (Chart) { Chart->SetScope(S); } }, L);
			USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			Box->SetWidthOverride(118.f);
			Box->SetHeightOverride(30.f);
			Box->AddChild(B);
			if (UHorizontalBoxSlot* HS = Tabs->AddChildToHorizontalBox(Box)) { HS->SetPadding(FMargin(0.f, 0.f, Lcars::Gap, 0.f)); }
			ScopeButtons.Add(B);
		};
		Tab(TEXT("Sector"), EChartScope::Sector);
		Tab(TEXT("System"), EChartScope::System);
		Tab(TEXT("Local"), EChartScope::Local);
		ScopeTitle = Lcars::MakeText(WidgetTree, TEXT(""), 18.f, Lcars::Peach);
		if (UHorizontalBoxSlot* HS = Tabs->AddChildToHorizontalBox(ScopeTitle)) { HS->SetPadding(FMargin(18.f, 0.f, 0.f, 0.f)); HS->SetVerticalAlignment(VAlign_Center); }
		if (UVerticalBoxSlot* S = Content->AddChildToVerticalBox(Tabs)) { S->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f)); }
	}
	if (UVerticalBoxSlot* S = Content->AddChildToVerticalBox(Chart)) { S->SetSize(FSlateChildSize(ESlateSizeRule::Fill)); }
	Footer = AddLine(Content, TEXT(""), 13.f, Lcars::Peach, false, false, 8.f);

	AddSidebarButton(TEXT("Close"), Lcars::Orange, [this]() { Close(); });
	AddSidebarButton(TEXT("Set Course"), Lcars::Gold, [this]() { if (Chart) { SetCourse(Chart->GetSelected()); } });
	AddSidebarButton(TEXT("Autopilot"), Lcars::Peach, [this]()
	{
		if (Chart && Chart->GetSelected()) { SetCourse(Chart->GetSelected()); }
		if (AStarshipPawn* S = GetShip()) { S->Ship(TEXT("autopilot")); Close(); }
	});
	AddSidebarButton(TEXT("Clear Course"), Lcars::Tan, [this]() { if (AStarshipHUD* H = GetHUD()) { H->ClearNavTarget(); } });
	FilterButton = AddSidebarButton(TEXT("Filter: All"), Lcars::Lavender, [this]() { if (AStarshipHUD* H = GetHUD()) { H->CycleNavFilter(); } });
	TiltButton = AddSidebarButton(TEXT("Tilt 55"), Lcars::Periwinkle, [this]() { if (Chart) { Chart->CycleTilt(); } });
	HeadingButton = AddSidebarButton(TEXT("North Up"), Lcars::Lilac, [this]() { if (Chart) { Chart->ToggleHeadingUp(); } });
	AddSidebarButton(TEXT("Centre Ship"), Lcars::Sky, [this]() { if (Chart) { Chart->CentreOnShip(); } });
	AddSidebarButton(TEXT("Reset View"), Lcars::Tan, [this]() { if (Chart) { Chart->ResetView(); } });
	MinimapButton = AddSidebarButton(TEXT("Minimap On"), Lcars::Peach, [this]() { if (AStarshipHUD* H = GetHUD()) { H->ToggleMinimap(); } });
	AddSidebarButton(TEXT("Science Log"), Lcars::Gold, [this]() { if (USlonkUIRoot* UI = GetUIRoot()) { Close(); UI->PushMenu<UScienceLogPanel>(); } });
	AddSidebarButton(TEXT("Main Menu"), Lcars::Lavender, [this]() { if (USlonkUIRoot* UI = GetUIRoot()) { Close(); UI->PushMenu<UMainMenuPanel>(); } });

	// Start with the nav target selected.
	if (AStarshipHUD* H = GetHUD())
	{
		FNavObject O;
		if (H->GetNavTargetInfo(O) && Chart) { Chart->SelectActor(O.Actor); }
	}
}

void UStarChartPanel::SetCourse(AActor* Actor)
{
	AStarshipHUD* H = GetHUD();
	if (!H || !Actor) { return; }
	H->SetNavTargetActor(Actor);
}

void UStarChartPanel::Refresh()
{
	Super::Refresh();
	AStarshipHUD* H = GetHUD();
	auto Relabel = [](UButton* B, const FString& S) { if (UTextBlock* T = B ? Cast<UTextBlock>(B->GetChildAt(0)) : nullptr) { const FString U = S.ToUpper(); if (!T->GetText().ToString().Equals(U)) { T->SetText(FText::FromString(U)); } } };
	if (H) { Relabel(FilterButton, TEXT("Filter: ") + H->NavFilterName()); Relabel(MinimapButton, H->bShowMinimap ? TEXT("Minimap On") : TEXT("Minimap Off")); }
	if (Chart) { Relabel(TiltButton, FString::Printf(TEXT("Tilt %.0f"), Chart->GetTilt())); Relabel(HeadingButton, Chart->IsHeadingUp() ? TEXT("Heading Up") : TEXT("North Up")); }
	if (Chart)
	{
		const int32 S = (int32)Chart->GetScope();
		if (S != StyledScope)
		{
			StyledScope = S;
			for (int32 i = 0; i < ScopeButtons.Num(); ++i) { Lcars::SetButtonColor(ScopeButtons[i], i == S ? Lcars::Orange : Lcars::Lilac); }
		}
		if (ScopeTitle)
		{
			const FString T = Chart->ScopeTitle().ToUpper();
			if (!ScopeTitle->GetText().ToString().Equals(T)) { ScopeTitle->SetText(FText::FromString(T)); }
		}
	}
	if (Footer && Chart)
	{
		FString Line;
		if (AActor* Sel = Chart->GetSelected())
		{
			FNavObject O;
			const bool bIsNav = H && H->NavTarget.Get() == Sel;
			Line = FString::Printf(TEXT("Selected: %s%s     Set Course marks it for the HUD and autopilot; double click does the same.  Drag pans, wheel zooms, right drag tilts."), *Chart->GetSelectedLabel(), bIsNav ? TEXT("  (current course)") : TEXT(""));
		}
		else
		{
			Line = Chart->ScopeHint();
		}
		if (!Footer->GetText().ToString().Equals(Line)) { Footer->SetText(FText::FromString(Line)); }
	}
}

FReply UStarChartPanel::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Enter && Chart && Chart->GetSelected())
	{
		SetCourse(Chart->GetSelected());
		return FReply::Handled();
	}
	if (InKeyEvent.GetKey() == EKeys::M) { Close(); return FReply::Handled(); }
	if (Chart && InKeyEvent.GetKey() == EKeys::One) { Chart->SetScope(EChartScope::Sector); return FReply::Handled(); }
	if (Chart && InKeyEvent.GetKey() == EKeys::Two) { Chart->SetScope(EChartScope::System); return FReply::Handled(); }
	if (Chart && InKeyEvent.GetKey() == EKeys::Three) { Chart->SetScope(EChartScope::Local); return FReply::Handled(); }
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}
