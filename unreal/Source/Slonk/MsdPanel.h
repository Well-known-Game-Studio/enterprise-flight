// Slonk - the master systems display: the ship in cutaway, her compartments coloured by their state, hits flashing
// where they landed, the shield facings round the hull, and a click on any compartment for its orders.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LcarsPanel.h"
#include "MsdPanel.generated.h"

class AStarshipPawn;
class UTexture2D;
struct FCompartment;

/** The cutaway itself, painted in Slate: the hull's own silhouette rasterised from its mesh, then the compartment boxes. */
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
	int32 GetSelected() const { return Selected; }
	void SetSelected(int32 Index) { Selected = Index; }
	/** Select the compartment whose name contains Text (case-insensitive); for the console. */
	bool SelectByName(const FString& Text);
	TFunction<void(int32)> OnSelected;

private:
	TWeakObjectPtr<AStarshipPawn> Ship;
	UPROPERTY() TObjectPtr<UTexture2D> Silhouette;
	FSlateBrush SilBrush;
	FSlateBrush BoxBrush;
	float LengthCm = 30500.f;
	float Zmin = -3600.f, Zmax = 3600.f;
	int32 Selected = INDEX_NONE;
	int32 Hovered = INDEX_NONE;
	// laid out each paint for the mouse
	mutable TArray<FSlateRect> Rects;
	mutable FVector2D Origin = FVector2D::ZeroVector;
	mutable double Scale = 1.0;
	void BuildSilhouette();
	FVector2D Project(const FVector& Local) const { return FVector2D(Origin.X + (Local.X + LengthCm * 0.5) * Scale, Origin.Y + (Zmax - Local.Z) * Scale); }
	int32 HitTest(const FVector2D& Local) const;
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
	virtual FString PanelTitle() const override { return TEXT("Master Systems Display"); }
	virtual FLinearColor FrameColor() const override { return Lcars::Lavender; }
	virtual FVector2D PanelSize() const override { return FVector2D(1540.0, 880.0); }

private:
	UPROPERTY() TObjectPtr<UMsdWidget> Display;
	const FCompartment* SelectedCompartment() const;
};
