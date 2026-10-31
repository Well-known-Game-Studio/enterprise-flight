// Slonk - canvas HUD: speed, throttle, warp, weapons, target and controls help.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "StarshipHUD.generated.h"

UENUM(BlueprintType)
enum class ENavObjectKind : uint8
{
	Star,
	Planet,
	Moon,
	Station,
	Ship,
	Hostile,
	Belt,
	Nebula,
	Target
};

/** Which kinds the nav markers, the minimap and L cycling show (I cycles, "ship nav filter <kind>" sets). */
UENUM(BlueprintType)
enum class ENavFilter : uint8
{
	All,
	Stars,
	Worlds,
	Moons,
	Stations,
	Ships,
	Nebulae
};

/** One thing worth navigating to, gathered each frame. */
USTRUCT(BlueprintType)
struct FNavObject
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Nav") TObjectPtr<AActor> Actor;
	UPROPERTY(BlueprintReadOnly, Category = "Nav") FString Label;
	UPROPERTY(BlueprintReadOnly, Category = "Nav") ENavObjectKind Kind = ENavObjectKind::Planet;
	UPROPERTY(BlueprintReadOnly, Category = "Nav") FVector Location = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "Nav") double RadiusCm = 0.0;
};

UCLASS()
class SLONK_API AStarshipHUD : public AHUD
{
	GENERATED_BODY()

public:
	AStarshipHUD();

	virtual void DrawHUD() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD")
	bool bShowHelp = true;

	/** The nav objects the HUD knows about (bRefresh re-gathers now rather than on the quarter-second cadence). */
	const TArray<FNavObject>& GetNavObjects(bool bRefresh = false) { GatherNavObjects(bRefresh); return NavObjects; }
	FVector GetStarLocation() const { return StarLocation; }
	double GetBeltRadiusCm() const { return BeltRadiusCm; }
	FLinearColor KindColor(ENavObjectKind Kind) const;
	FString FormatSpeed(double CmPerSecond) const;
	FString FormatDistance(double Cm) const;

	/** Diamonds, names and distances for every body in the system (N). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Nav")
	bool bShowNavMarkers = true;

	/** 2.5D system overview in the lower-right corner (M). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Nav")
	bool bShowMinimap = true;

	/** Tilt of the minimap's orbital plane in degrees (0 = top-down, 90 = edge-on). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Nav")
	float MinimapTilt = 55.f;

	/** Minimap rotates with the ship's heading when true; north-up otherwise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Nav")
	bool bMinimapHeadingUp = false;

	/** Currently selected navigation target (L cycles). */
	UPROPERTY(BlueprintReadOnly, Category = "HUD|Nav")
	TWeakObjectPtr<AActor> NavTarget;

	UFUNCTION(BlueprintCallable, Category = "HUD|Nav") void ToggleNavMarkers() { bShowNavMarkers = !bShowNavMarkers; }
	UFUNCTION(BlueprintCallable, Category = "HUD|Nav") void ToggleMinimap() { bShowMinimap = !bShowMinimap; }
	UFUNCTION(BlueprintCallable, Category = "HUD|Nav") void CycleNavTarget();

	/** Kind filter for markers, minimap and cycling; the nav target and the system's sun stay shown regardless. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Nav")
	ENavFilter NavFilter = ENavFilter::All;
	UFUNCTION(BlueprintCallable, Category = "HUD|Nav") void CycleNavFilter();
	UFUNCTION(BlueprintCallable, Category = "HUD|Nav") bool SetNavFilterByName(const FString& Name);
	UFUNCTION(BlueprintCallable, Category = "HUD|Nav") FString NavFilterName() const;
	bool PassesNavFilter(ENavObjectKind Kind) const;
	/** Whether a nav object is listed and marked from here: stars and nebulae always, anything else only within
	 *  FarSystemCm (bodies of another star system project onto its star). */
	bool IsNavListed(const FNavObject& O, double Dist) const;
	static constexpr double FarSystemCm = 1.0e16;
	/** Select a specific nav object; false if it is not in the gathered list. */
	UFUNCTION(BlueprintCallable, Category = "HUD|Nav") bool SetNavTargetActor(AActor* Actor);
	/** Select by actor name or label substring. */
	UFUNCTION(BlueprintCallable, Category = "HUD|Nav") bool SetNavTargetByName(const FString& Name);
	UFUNCTION(BlueprintCallable, Category = "HUD|Nav") void ClearNavTarget() { NavTarget = nullptr; }

	/** The current nav target's gathered record, if any. */
	bool GetNavTargetInfo(FNavObject& Out);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD")
	FLinearColor Accent = FLinearColor(0.55f, 0.8f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD")
	FLinearColor Warning = FLinearColor(1.f, 0.6f, 0.2f);

private:
	void GatherNavObjects(bool bForce = false);
	void DrawNavMarkers(const AActor* Ship, float UIScale);
	/** The whole HUD for a crew member outside the craft. */
	void DrawPersonHUD(const class APersonPawn* Person, class UFont* Font, class UFont* SmallFont, float UIScale);
	void DrawMinimap(const AActor* Ship, float UIScale);
	/** The science rows of the left panel: points, rating, system survey, scan progress or the range hint. */
	void DrawScienceRows(const class AStarshipPawn* Ship, float& X, float& Y, float Line, float UIScale);
	/** Whether a nav object is in the science log already. */
	bool IsScanned(const FNavObject& O) const;
	/** A small tick mark: the object is catalogued. */
	void DrawTick(float X, float Y, float Size, const FLinearColor& Color);
	class UScienceLogSubsystem* ScienceLog() const;
	void DrawCircle(float CX, float CY, float R, const FLinearColor& Color, int32 Segments = 32, float Thickness = 1.f);
	void DrawEllipse(float CX, float CY, float RX, float RY, const FLinearColor& Color, int32 Segments = 48, float Thickness = 1.f);
	void DrawArc(float CX, float CY, float R, float FromDeg, float ToDeg, const FLinearColor& Color, int32 Segments = 12, float Thickness = 1.f);
	/** The nav target's colour this frame: white pulsing toward the accent, apart from the kind colours. */
	FLinearColor NavTargetColor() const;
	FVector2D MinimapProject(const FVector& Relative, double Scale, float CX, float CY, float YawDeg) const;

	TArray<FNavObject> NavObjects;
	FVector StarLocation = FVector::ZeroVector;
	double BeltRadiusCm = 0.0;
	float NavTime = 0.f;
	double LastNavGatherTime = -1.0;
	float SmoothedFrameMs = 0.f;
	float SmoothedGpuMs = 0.f;

	void DrawBar(float X, float Y, float W, float H, float Fraction, const FLinearColor& Color) const;
	void DrawReticle(float Scale);
	/** Full-screen Okudagram loading panel shown while the world-generation queue is still working. */
	void DrawLoadingScreen(const class USlonkLoadSubsystem& Load, UFont* Font, UFont* SmallFont, float UIScale);
};
