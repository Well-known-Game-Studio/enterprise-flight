// Slonk - reaction control thrusters: small chemical nozzles in packages round the hull that fire for attitude
// changes and close-quarters translation. Asset-free: instanced engine cones with an unlit emissive material,
// each instance's brightness in its custom data.

#pragma once

#include "CoreMinimal.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "RcsThrusterComponent.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;

USTRUCT(BlueprintType)
struct FRcsNozzle
{
	GENERATED_BODY()

	/** Nozzle position in the owning component's local space (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector Position = FVector::ZeroVector;
	/** Unit direction the exhaust leaves in; the thrust is the opposite way. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector ExhaustDir = FVector::UpVector;
	/** Plume size multiplier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float Size = 1.f;

	FRcsNozzle() {}
	FRcsNozzle(const FVector& InPos, const FVector& InExhaust, float InSize = 1.f)
		: Position(InPos), ExhaustDir(InExhaust.GetSafeNormal()), Size(InSize) {}
};

UCLASS(ClassGroup = (Starship), meta = (BlueprintSpawnableComponent))
class SLONK_API URcsThrusterComponent : public UInstancedStaticMeshComponent
{
	GENERATED_BODY()

public:
	URcsThrusterComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Replace every nozzle. */
	UFUNCTION(BlueprintCallable, Category = "RCS")
	void SetNozzles(const TArray<FRcsNozzle>& InNozzles);

	/**
	 * Body-frame demand for this frame. Torque is (pitch, yaw, roll) in [-1, 1], the flight model's steering
	 * convention; Force is along (forward, right, up) in [-1, 1]. Every nozzle whose thrust helps fires in
	 * proportion to how much it helps, so a roll lights the saucer-rim nozzles on opposite faces, a yaw the
	 * tangential ones, and a sideways translation the ones that push that way.
	 */
	UFUNCTION(BlueprintCallable, Category = "RCS")
	void SetDemand(const FVector& TorquePitchYawRoll, const FVector& ForceFwdRightUp);

	/** Debug: hold every nozzle at full for this many seconds (0 cancels), to check the layout against the hull. */
	UFUNCTION(BlueprintCallable, Category = "RCS")
	void SetAllOn(float Seconds) { AllOnTime = Seconds; }

	UFUNCTION(BlueprintPure, Category = "RCS")
	const TArray<FRcsNozzle>& GetNozzles() const { return Nozzles; }

	/** Sum of the current activations (for sound or the HUD). */
	UFUNCTION(BlueprintPure, Category = "RCS")
	float GetTotalActivation() const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RCS")
	TSoftObjectPtr<UMaterialInterface> ThrusterMaterial;

	/** Plume length and base width at full activation (cm): a few metres on a 300 m hull. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RCS") float PlumeLength = 1100.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RCS") float PlumeWidth = 240.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RCS") FLinearColor PlumeColor = FLinearColor(0.85f, 0.92f, 1.f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RCS") float Intensity = 80.f;
	/** Activation rise and fall rates (1/s): a chemical thruster lights almost at once and cuts off quickly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RCS") float AttackRate = 30.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RCS") float DecayRate = 12.f;
	/** Below this demand a nozzle stays off, so tiny steering corrections do not spit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RCS") float Threshold = 0.08f;

private:
	void EnsureMaterial();
	void ApplyInstances();

	UPROPERTY() TArray<FRcsNozzle> Nozzles;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MID;
	TArray<float> Target;
	TArray<float> Current;
	float Clock = 0.f;
	float AllOnTime = 0.f;
	bool bDirty = true;
};
