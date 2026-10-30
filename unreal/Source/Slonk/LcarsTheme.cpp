// Slonk - LCARS widget factories.

#include "LcarsTheme.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Styling/SlateTypes.h"

namespace Lcars
{
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
