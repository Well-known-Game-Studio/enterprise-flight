// Slonk - the master systems display: the ship in cutaway or in plan, her compartments coloured by their state, hits
// flashing where they landed, the shield facings round the hull, and a click on any compartment for its orders.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LcarsPanel.h"
#include "MsdPanel.generated.h"

class AStarshipPawn;
class UTexture2D;
struct FCompartment;

/** The two drawings: the side elevation (decks, the centre-line compartments) and the plan (shield facings, port and starboard). */
UENUM()
enum class EMsdView : uint8
{
	Elevation,
	Plan,
};

/** The drawing itself, painted in Slate: the hull's own silhouette rasterised from its mesh, then the compartment boxes. */
UCLASS()
class SLONK_API UMsdWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	virtual bool Initialize() override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;

	void SetShip(AStarshipPawn* InShip);
	void SetView(EMsdView InView);
	EMsdView GetView() const { return View; }
	int32 GetSelected() const { return Selected; }
	void SetSelected(int32 Index) { Selected = Index; }
	/** Select the compartment whose name contains Text (case-insensitive); for the console. */
	bool SelectByName(const FString& Text);
	TFunction<void(int32)> OnSelected;
	/** A shield facing (0 fore, 1 aft, 2 port, 3 starboard) clicked on the plan's shield ring. */
	TFunction<void(int32)> OnFacingClicked;

private:
	TWeakObjectPtr<AStarshipPawn> Ship;
	EMsdView View = EMsdView::Elevation;
	/** One raster per view: the hull seen from the side (X along, Z up) and from above (X along, port up). */
	struct FRaster
	{
		FSlateBrush Brush;
		int32 W = 0, H = 0;
		float Umin = -3600.f, Umax = 3600.f;		// the "up" axis span, padded
		/** The hull's lowest and highest "up" (cm) per raster column, for placing labels on the open side. */
		TArray<FVector2f> ColumnU;
	};
	UPROPERTY() TArray<TObjectPtr<UTexture2D>> Textures;
	FRaster Rasters[2];
	FSlateBrush BoxBrush;
	float LengthCm = 30500.f;
	/** The hull's widest half-breadth (cm), for the shield ring. */
	float HalfBeam = 7100.f;
	int32 Selected = INDEX_NONE;
	int32 Hovered = INDEX_NONE;
	int32 HoveredFacing = INDEX_NONE;
	// laid out each paint for the mouse
	mutable TArray<FSlateRect> Rects;
	mutable FVector2D Origin = FVector2D::ZeroVector;
	mutable double Scale = 1.0;
	mutable FVector4 RingPx = FVector4(0, 0, 0, 0);	// the shield ring on screen: centre x, y and semi-axes

	void BuildSilhouettes();
	void BuildRaster(EMsdView ForView);
	const FRaster& R() const { return Rasters[(int32)View]; }
	/** The compartment's "up" coordinate and half-extent in the current view. */
	float Up(const FVector& Local) const { return View == EMsdView::Plan ? -(float)Local.Y : (float)Local.Z; }
	float UpExtent(const FVector& Extent) const { return View == EMsdView::Plan ? (float)Extent.Y : (float)Extent.Z; }
	/** Whether a compartment is drawn in the current view: the centre-line ones in elevation, the off-axis ones in plan. */
	bool Shown(const FCompartment& C, int32 Index) const;
	/** Where the hull is at this X in the current view: its bottom and top in cm, or the whole span when nothing was rasterised. */
	FVector2f HullSpanAt(float LocalX) const;
	FVector2D Project(const FVector& Local) const { return FVector2D(Origin.X + (Local.X + LengthCm * 0.5) * Scale, Origin.Y + (R().Umax - Up(Local)) * Scale); }
	int32 HitTest(const FVector2D& Local) const;
	/** The facing under a point on the plan's shield ring, or INDEX_NONE. */
	int32 FacingAt(const FVector2D& Local) const;
};

/** ' : the display with its orders. */
UCLASS()
class SLONK_API UMsdPanel : public ULcarsPanel
{
	GENERATED_BODY()

public:
	UMsdWidget* GetDisplay() const { return Display; }

protected:
	virtual void BuildContent() override;
	virtual void Refresh() override;
	/** CommonUI pools and reuses the panel; point the display at whatever the player now commands. */
	virtual void NativeOnActivated() override;
	virtual FString PanelTitle() const override { return TEXT("Master Systems Display"); }
	virtual FLinearColor FrameColor() const override { return Lcars::Lavender; }
	virtual FVector2D PanelSize() const override { return FVector2D(1540.0, 880.0); }

private:
	UPROPERTY() TObjectPtr<UMsdWidget> Display;
	UPROPERTY() TArray<TObjectPtr<UButton>> ViewButtons;
	int32 LastViewShown = -1;
	const FCompartment* SelectedCompartment() const;
};
