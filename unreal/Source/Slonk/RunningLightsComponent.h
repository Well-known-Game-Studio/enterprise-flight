// Slonk - navigation and formation lights: small emissive bulbs on a hull that blink, strobe or burn steadily.
// One instanced mesh per vessel; each instance carries its colour, period, phase and blink style as per-instance
// custom data so the material animates every bulb on its own. The bulbs grow with camera distance so a light
// never drops below a couple of pixels, which is what makes a far-off ship read as a ship.

#pragma once

#include "CoreMinimal.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "RunningLightsComponent.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;

UENUM(BlueprintType)
enum class ERunningLightMode : uint8
{
	/** Always on. */
	Steady,
	/** Slow on/off, about a third of the period lit. */
	Blink,
	/** Two short flashes per period, like an anti-collision strobe. */
	Strobe,
};

USTRUCT(BlueprintType)
struct FRunningLight
{
	GENERATED_BODY()

	/** Position in the owning component's local space (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector LocalPosition = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FLinearColor Color = FLinearColor::White;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) ERunningLightMode Mode = ERunningLightMode::Blink;
	/** Seconds per cycle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float Period = 2.f;
	/** 0..1 offset into the cycle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float Phase = 0.f;
	/** Physical bulb radius (cm) when the camera is close. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float RadiusCm = 60.f;
	/** Brightness multiplier on top of the component's Intensity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float Brightness = 1.f;

	FRunningLight() {}
	FRunningLight(const FVector& InPos, const FLinearColor& InColor, ERunningLightMode InMode, float InPeriod, float InPhase, float InRadius = 60.f, float InBrightness = 1.f)
		: LocalPosition(InPos), Color(InColor), Mode(InMode), Period(InPeriod), Phase(InPhase), RadiusCm(InRadius), Brightness(InBrightness) {}
};

/** A hull floodlight: a lamp on the skin aimed at a patch of hull (registry, name, pennant). */
USTRUCT(BlueprintType)
struct FHullFloodlight
{
	GENERATED_BODY()

	/** Lamp position in the owning component's local space (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector LampPosition = FVector::ZeroVector;
	/** Centre of the lit patch, local space (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector TargetPosition = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FLinearColor Color = FLinearColor(1.f, 0.96f, 0.88f);
	/** Illuminance at the patch centre (lux). The sun here is about 140 lux; a hull lamp is a small fraction of that. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float Lux = 14.f;
	/** Radius of the lit patch (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float PatchRadiusCm = 1500.f;

	FHullFloodlight() {}
	FHullFloodlight(const FVector& InLamp, const FVector& InTarget, float InPatch, float InLux = 14.f)
		: LampPosition(InLamp), TargetPosition(InTarget), Lux(InLux), PatchRadiusCm(InPatch) {}
};

class USpotLightComponent;

UCLASS(ClassGroup = (Starship), meta = (BlueprintSpawnableComponent))
class SLONK_API URunningLightsComponent : public UInstancedStaticMeshComponent
{
	GENERATED_BODY()

public:
	URunningLightsComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Replace every light. */
	UFUNCTION(BlueprintCallable, Category = "Running Lights")
	void SetLights(const TArray<FRunningLight>& InLights);

	UFUNCTION(BlueprintCallable, Category = "Running Lights")
	void ClearLights();

	/** Replace the hull floodlights (spot lights attached under this component, so visibility follows it). */
	UFUNCTION(BlueprintCallable, Category = "Running Lights")
	void SetFloodlights(const TArray<FHullFloodlight>& InFloods);

	/** Emissive strength of a fully lit bulb; high enough to bloom. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Running Lights")
	float Intensity = 120.f;

	/**
	 * Bulbs are scaled up with distance so their on-screen radius never falls below this (radians, roughly;
	 * one pixel at 1080p with a 90 degree field of view is about 0.0008, so 0.004 is a five pixel glow).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Running Lights")
	float MinAngularRadius = 0.0035f;

	/** Cap on the scaled bulb radius (cm) so far-away lights do not dwarf their hull. Set from the vessel size. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Running Lights")
	float MaxRadiusCm = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Running Lights")
	TSoftObjectPtr<UMaterialInterface> LightMaterial;

	/** Floodlights are switched off beyond this camera distance (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Running Lights")
	float FloodlightRangeCm = 3.0e6f;

	/** Convenience colours in the aviation / Starfleet convention. */
	static FLinearColor PortRed() { return FLinearColor(1.f, 0.08f, 0.04f); }
	static FLinearColor StarboardGreen() { return FLinearColor(0.1f, 1.f, 0.25f); }
	static FLinearColor StrobeWhite() { return FLinearColor(1.f, 0.97f, 0.9f); }
	static FLinearColor FormationAmber() { return FLinearColor(1.f, 0.72f, 0.35f); }

private:
	void EnsureMaterial();
	void ApplyScale(float Scale);

	UPROPERTY() TArray<FRunningLight> Lights;
	UPROPERTY() TArray<TObjectPtr<USpotLightComponent>> Floods;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MID;
	float LastScale = -1.f;
	float ScaleTimer = 0.f;
};
