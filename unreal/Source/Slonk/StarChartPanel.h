// Slonk - the star chart: a full-screen LCARS map of the system with pan, zoom, tilt, selection and course setting.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LcarsPanel.h"
#include "StarshipHUD.h"
#include "StarChartPanel.generated.h"

/** What the chart is looking at: the neighbouring suns, the current system, or one world and its moons. */
UENUM()
enum class EChartScope : uint8
{
	Sector,
	System,
	Local
};

/**
 * The map itself: painted in Slate from the HUD's nav objects. Drag pans, the wheel zooms about the cursor, the
 * right button tilts and turns, a click selects a body (or another star system in the list), a double click sets
 * course. The ship, orbits, the belt, catalogued ticks and a scale bar are drawn like the minimap, larger.
 */
UCLASS()
class SLONK_API UStarChartWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual bool Initialize() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDoubleClick(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;

	/** Switch scope; Local picks its world from the selection, the course or the ship's neighbourhood. */
	void SetScope(EChartScope S);
	EChartScope GetScope() const { return Scope; }
	/** "Sol sector", "Sol system" or the focused world's name. */
	FString ScopeTitle() const;
	/** A one-line description of the scope for the footer. */
	FString ScopeHint() const;
	AActor* GetSelected() const { return Selected.Get(); }
	FString GetSelectedLabel() const { return SelectedLabel; }
	void SelectActor(AActor* Actor);
	void ResetView();
	/** Pan to the ship and zoom in on its neighbourhood. */
	void CentreOnShip();
	void CycleTilt();
	void ToggleHeadingUp() { bHeadingUp = !bHeadingUp; }
	bool IsHeadingUp() const { return bHeadingUp; }
	float GetTilt() const { return TiltDeg; }
	/** Called on a double click or Enter with the body (or far star) chosen. */
	TFunction<void(AActor*)> OnSetCourse;
	/** Called on any mouse press over the map (the panel closes its drop-down). */
	TFunction<void()> OnPressed;

private:
	struct FBody
	{
		TWeakObjectPtr<AActor> Actor;
		FString Label;
		ENavObjectKind Kind = ENavObjectKind::Planet;
		FVector Rel = FVector::ZeroVector;   // from the system's star
		double RadiusCm = 0.0;
		bool bScanned = false;
		FLinearColor Color = FLinearColor::White;
		FString Tag;                          // drawn after the label: a distance in Sector and Local
		FVector2D Screen = FVector2D::ZeroVector;
		FVector2D Base = FVector2D::ZeroVector;
	};
	struct FFarStar
	{
		TWeakObjectPtr<AActor> Actor;
		FString Label;
		double DistCm = 0.0;
		FSlateRect Hit;
	};
	FVector2D Project(const FVector& Rel) const;
	void Layout(const FVector2D& Size) const;
	/** Whether the body is drawn under the current nav filter (the star, course and selection always are). */
	bool IsShown(const FBody& B, const AStarshipHUD* H) const;
	int32 HitBody(const FVector2D& Local) const;
	int32 HitFarStar(const FVector2D& Local) const;
	FString DescribeBody(const FBody& B, TArray<FString>& OutLines) const;
	/** The world Local should centre on, from the selection, the course, then the ship. */
	AActor* ResolveLocalFocus(const TArray<FNavObject>& Nav, const FVector& ShipLoc) const;

	// Scope.
	EChartScope Scope = EChartScope::System;
	TWeakObjectPtr<AActor> LocalFocus;
	mutable FString FocusLabel;
	mutable FString LocalStarLabel;
	mutable double LocalRangeCm = 0.0;

	// View.
	bool bCentreOnShipPending = false;
	double Zoom = 1.0;
	FVector2D Pan = FVector2D::ZeroVector;
	float TiltDeg = 55.f;
	bool bHeadingUp = false;
	float YawOffsetDeg = 0.f;
	// Interaction.
	bool bLeftDown = false, bRightDown = false, bDragged = false;
	FVector2D LastMouse = FVector2D::ZeroVector;
	FVector2D Hover = FVector2D(-1.0, -1.0);
	TWeakObjectPtr<AActor> Selected;
	FString SelectedLabel;
	float Time = 0.f;
	// Scene, rebuilt every tick and laid out every paint.
	mutable TArray<FBody> Bodies;
	mutable TArray<FFarStar> FarStars;
	mutable FVector ShipRel = FVector::ZeroVector, ShipFwd = FVector::ForwardVector;
	mutable double MaxR = 1.0, Scale = 1.0, BeltRadiusCm = 0.0;
	mutable FVector2D Centre = FVector2D::ZeroVector, LaidOutSize = FVector2D::ZeroVector;
	mutable float ShipYawDeg = 0.f;
	FSlateBrush DotBrush;
	FSlateBrush BoxBrush;
};

/** M: the chart in an LCARS frame, with course, autopilot, filter and view buttons down the side. */
UCLASS()
class SLONK_API UStarChartPanel : public ULcarsPanel
{
	GENERATED_BODY()

protected:
	virtual void BuildContent() override;
	virtual void Refresh() override;
	virtual FString PanelTitle() const override { return TEXT("Stellar Cartography"); }
	virtual FLinearColor FrameColor() const override { return Lcars::Periwinkle; }
	virtual FVector2D PanelSize() const override { return FVector2D(1540.0, 880.0); }
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

private:
	void SetCourse(AActor* Actor);
	void ShowFilterMenu(bool bOpen);
	UPROPERTY() TObjectPtr<UStarChartWidget> Chart;
	UPROPERTY() TObjectPtr<UButton> FilterButton;
	UPROPERTY() TObjectPtr<UVerticalBox> FilterMenu;
	UPROPERTY() TObjectPtr<UBorder> FilterBacking;
	UPROPERTY() TArray<TObjectPtr<UButton>> FilterItems;
	int32 StyledFilter = -1;
	UPROPERTY() TObjectPtr<UButton> TiltButton;
	UPROPERTY() TObjectPtr<UButton> HeadingButton;
	UPROPERTY() TObjectPtr<UButton> MinimapButton;
	UPROPERTY() TObjectPtr<UTextBlock> Footer;
	UPROPERTY() TArray<TObjectPtr<UButton>> ScopeButtons;
	UPROPERTY() TObjectPtr<UTextBlock> ScopeTitle;
	int32 StyledScope = -1;
};
