// Slonk - tractor beam: a blue graviton beam from the deflector that pulls the locked target in and holds it.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TractorBeamComponent.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;

UCLASS(ClassGroup = (Starship), meta = (BlueprintSpawnableComponent))
class SLONK_API UTractorBeamComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTractorBeamComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Targets further than this (cm) cannot be grabbed; the beam drops at 1.5x. 60 km. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tractor")
	double MaxRangeCm = 6.0e6;

	/** How fast the target is reeled in (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tractor")
	double PullSpeed = 3.0e5;

	/** Hold distance from the ship centre, in own ship lengths, plus the target's radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tractor")
	float HoldLengths = 1.2f;

	/** Beam origin relative to the owner (cm): the deflector dish. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tractor")
	FVector EmitterOffset = FVector(6000.0, 0.0, -1500.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tractor")
	FLinearColor BeamColor = FLinearColor(0.4f, 0.75f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tractor")
	TSoftObjectPtr<UMaterialInterface> BeamMaterial;

	UPROPERTY(BlueprintReadOnly, Category = "Tractor")
	TObjectPtr<AActor> Target;

	UFUNCTION(BlueprintPure, Category = "Tractor")
	bool IsBeamActive() const { return Target != nullptr; }

	UFUNCTION(BlueprintCallable, Category = "Tractor")
	bool Engage(AActor* NewTarget);

	UFUNCTION(BlueprintCallable, Category = "Tractor")
	void Release();

	UFUNCTION(BlueprintPure, Category = "Tractor")
	double GetRangeCm() const;

	/** Bounding radius (cm) of anything we might grab. */
	static double TargetRadiusCm(const AActor* Actor);

private:
	void UpdateBeamMesh(const FVector& Start, const FVector& End, double Radius);

	UPROPERTY() TObjectPtr<UStaticMeshComponent> Beam;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> EmitterGlow;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> MID;
	float Age = 0.f;
};
