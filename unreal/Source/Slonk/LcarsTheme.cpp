// Slonk - LCARS widget factories.

#include "LcarsTheme.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Styling/SlateTypes.h"
#include "Misc/Paths.h"

namespace Lcars
{
	static ELcarsTypeface CurrentTypeface = ELcarsTypeface::Standard;
	ELcarsTypeface GetTypeface() { return CurrentTypeface; }
	void SetTypeface(ELcarsTypeface Face) { CurrentTypeface = Face; }
	const TCHAR* TypefaceName(ELcarsTypeface Face) { return Face == ELcarsTypeface::Condensed ? TEXT("Condensed") : TEXT("Standard"); }

	FSlateFontInfo Font(float Size, bool bBold, int32 Tracking)
	{
		if (CurrentTypeface == ELcarsTypeface::Condensed)
		{
			// Slate loads a TTF by path (as the engine's own core style does); Antonio sits tight already, so a
			// third of the tracking and a touch more size to match Roboto's presence.
			static const FString Bold = FPaths::ProjectContentDir() / TEXT("UI/Fonts/Antonio-Bold.ttf");
			static const FString Regular = FPaths::ProjectContentDir() / TEXT("UI/Fonts/Antonio-Regular.ttf");
			FSlateFontInfo F(bBold ? Bold : Regular, Size * 1.1f);
			F.LetterSpacing = Tracking / 3;
			return F;
		}
		FSlateFontInfo F = FCoreStyle::GetDefaultFontStyle(bBold ? TEXT("Bold") : TEXT("Regular"), Size);
		F.LetterSpacing = Tracking;
		return F;
	}

	UTextBlock* MakeText(UWidgetTree* Tree, const FString& Text, float Size, const FLinearColor& Color, bool bBold, bool bUpper)
	{
		UTextBlock* T = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		T->SetText(FText::FromString(bUpper ? Text.ToUpper() : Text));
		T->SetFont(Font(Size, bBold, bUpper ? 60 : 20));
		T->SetColorAndOpacity(FSlateColor(Color));
		return T;
	}

	UBorder* MakeBlock(UWidgetTree* Tree, const FLinearColor& Color, const FVector4& Radii)
	{
		UBorder* B = Tree->ConstructWidget<UBorder>(UBorder::StaticClass());
		B->SetBrush(Rounded(Color, Radii));
		B->SetPadding(FMargin(0.f));
		return B;
	}

	static FButtonStyle ButtonStyleFor(const FLinearColor& Color, const FVector4& Radii)
	{
		FButtonStyle S;
		S.SetNormal(Rounded(Color, Radii));
		S.SetHovered(Rounded(Brighter(Color), Radii));
		S.SetPressed(Rounded(Darker(Color), Radii));
		S.SetDisabled(Rounded(FLinearColor(Color.R * 0.35f, Color.G * 0.35f, Color.B * 0.35f), Radii));
		S.SetNormalPadding(FMargin(0.f));
		S.SetPressedPadding(FMargin(0.f));
		return S;
	}

	UButton* MakeButton(UWidgetTree* Tree, const FString& Label, const FLinearColor& Color, UTextBlock** OutLabel, const FVector4* Radii)
	{
		const FVector4 R = Radii ? *Radii : FVector4(4.f, 4.f, 4.f, 4.f);
		UButton* B = Tree->ConstructWidget<UButton>(UButton::StaticClass());
		B->SetStyle(ButtonStyleFor(Color, R));
		UTextBlock* T = MakeText(Tree, Label, 14.f, Black);
		if (UButtonSlot* S = Cast<UButtonSlot>(B->AddChild(T)))
		{
			// LCARS puts the label in the block's bottom-right corner.
			S->SetHorizontalAlignment(HAlign_Right);
			S->SetVerticalAlignment(VAlign_Bottom);
			S->SetPadding(FMargin(8.f, 2.f, 10.f, 4.f));
		}
		if (OutLabel) { *OutLabel = T; }
		return B;
	}

	void SetButtonColor(UButton* Button, const FLinearColor& Color, const FVector4* Radii)
	{
		if (Button) { Button->SetStyle(ButtonStyleFor(Color, Radii ? *Radii : FVector4(4.f, 4.f, 4.f, 4.f))); }
	}
}
