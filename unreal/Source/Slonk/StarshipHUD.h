// Slonk - canvas HUD: speed, throttle, warp, weapons, target and controls help.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "StarshipHUD.generated.h"

UCLASS()
class SLONK_API AStarshipHUD : public AHUD
{
	GENERATED_BODY()

public:
	AStarshipHUD();

	virtual void DrawHUD() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD")
	bool bShowHelp = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD")
	FLinearColor Accent = FLinearColor(0.55f, 0.8f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD")
	FLinearColor Warning = FLinearColor(1.f, 0.6f, 0.2f);

private:
	void DrawBar(float X, float Y, float W, float H, float Fraction, const FLinearColor& Color) const;
	void DrawReticle(float Scale);
	FString FormatSpeed(double CmPerSecond) const;
	FString FormatDistance(double Cm) const;
};
