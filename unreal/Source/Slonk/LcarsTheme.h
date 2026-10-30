// Slonk - the LCARS look for the menu layer: palette, metrics, brushes and small widget factories shared by every panel.

#pragma once

#include "CoreMinimal.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"
#include "Fonts/SlateFontInfo.h"

class UWidgetTree;
class UTextBlock;
class UBorder;
class UButton;
class UWidget;

/**
 * The TNG-era Okudagram palette and the pieces every panel is built from. Everything is code: rounded-box brushes
 * for the pills, bars and elbows, the engine's Roboto for type (uppercase, tracked out) until a condensed face is
 * brought in.
 */
namespace Lcars
{
	// Palette.
	const FLinearColor Orange(1.00f, 0.60f, 0.00f);
	const FLinearColor Peach(1.00f, 0.80f, 0.60f);
	const FLinearColor Lavender(0.80f, 0.60f, 0.80f);
	const FLinearColor Periwinkle(0.60f, 0.60f, 1.00f);
	const FLinearColor Salmon(0.80f, 0.40f, 0.40f);
	const FLinearColor Gold(1.00f, 0.80f, 0.40f);
	const FLinearColor Lilac(0.60f, 0.60f, 0.80f);
	const FLinearColor Tan(0.80f, 0.60f, 0.40f);
	const FLinearColor Sky(0.60f, 0.80f, 1.00f);
	const FLinearColor Red(0.80f, 0.27f, 0.27f);
	const FLinearColor Black(0.f, 0.f, 0.f);
	const FLinearColor Panel(0.02f, 0.02f, 0.03f);
	const FLinearColor Dim(1.f, 1.f, 1.f, 0.55f);

	// Metrics (Slate units; UMG scales them with the viewport DPI curve).
	constexpr float SidebarW = 150.f;
	constexpr float TopBarH = 34.f;
	constexpr float BottomBarH = 22.f;
	constexpr float Notch = 26.f;          // how far the elbows reach under/over the content corner
	constexpr float ElbowR = 56.f;         // outer radius of an elbow
	constexpr float InnerR = 22.f;         // concave inner radius, cut by the content box
	constexpr float Gap = 6.f;             // the black seam between blocks
	constexpr float ButtonH = 44.f;
	constexpr float PillR = 22.f;

	inline FSlateFontInfo Font(float Size, bool bBold = true, int32 Tracking = 60)
	{
		FSlateFontInfo F = FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"), Size);
		F.LetterSpacing = Tracking;
		return F;
	}

	/** A box with per-corner radii (TL, TR, BR, BL). */
	inline FSlateBrush Rounded(const FLinearColor& Color, const FVector4& Radii)
	{
		return FSlateRoundedBoxBrush(Color, Radii);
	}
	inline FSlateBrush Pill(const FLinearColor& Color, float R = PillR) { return Rounded(Color, FVector4(R, R, R, R)); }
	inline FSlateBrush Block(const FLinearColor& Color) { return Rounded(Color, FVector4(0.f, 0.f, 0.f, 0.f)); }

	inline FLinearColor Brighter(const FLinearColor& C, float K = 0.18f) { return FLinearColor(FMath::Min(C.R + K, 1.f), FMath::Min(C.G + K, 1.f), FMath::Min(C.B + K, 1.f), C.A); }
	inline FLinearColor Darker(const FLinearColor& C, float K = 0.25f) { return FLinearColor(C.R * (1.f - K), C.G * (1.f - K), C.B * (1.f - K), C.A); }

	/** Text in the LCARS manner: uppercase, tracked. */
	UTextBlock* MakeText(UWidgetTree* Tree, const FString& Text, float Size, const FLinearColor& Color, bool bBold = true, bool bUpper = true);
	/** A filled block or pill. */
	UBorder* MakeBlock(UWidgetTree* Tree, const FLinearColor& Color, const FVector4& Radii);
	/** A pill button with black text at its bottom right; OutLabel receives the text block for later changes. */
	UButton* MakeButton(UWidgetTree* Tree, const FString& Label, const FLinearColor& Color, UTextBlock** OutLabel = nullptr, const FVector4* Radii = nullptr);
	/** Recolour a button made by MakeButton. */
	void SetButtonColor(UButton* Button, const FLinearColor& Color, const FVector4* Radii = nullptr);
}
