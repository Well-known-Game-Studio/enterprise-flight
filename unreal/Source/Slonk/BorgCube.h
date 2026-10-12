// Slonk - a procedural Borg cube: a greebled block with green power conduits that hunts the player when
// they stray too close, cutting with a beam and launching torpedoes.

#pragma once

#include "CoreMinimal.h"
#include "SpaceVessel.h"
#include "BorgCube.generated.h"

class APhaserBeam;

UCLASS()
class SLONK_API ABorgCube : public ASpaceVessel
{
	GENERATED_BODY()

public:
	ABorgCube();
	virtual void Tick(float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Range (cm) at which the cube turns hostile. 200 km. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Borg")
	double AggroRangeCm = 2.0e7;

	/** Stops closing at this range (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Borg")
	double HoldRangeCm = 3.0e6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Borg")
	double ApproachSpeed = 1.2e6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Borg")
	float TurnRate = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Borg")
	float BeamDamagePerSecond = 7.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Borg")
	float BeamBurstSeconds = 2.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Borg")
	float BeamPauseSeconds = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Borg")
	double BeamRangeCm = 8.0e6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Borg")
	float TorpedoInterval = 11.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Borg")
	FLinearColor WeaponColor = FLinearColor(0.3f, 1.0f, 0.35f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Borg|Materials")
	TSoftObjectPtr<UMaterialInterface> BeamMaterial;

	UFUNCTION(BlueprintPure, Category = "Borg")
	bool IsHostile() const { return bHostile; }

protected:
	virtual void BuildMeshes(UDynamicMesh* HullMesh, UDynamicMesh* GlowMesh, FRandomStream& Rng) override;

private:
	void StopBeam();

	UPROPERTY() TObjectPtr<APhaserBeam> CuttingBeam;
	bool bHostile = false;
	float BeamTimer = 0.f;
	bool bBeamOn = false;
	float TorpedoTimer = 3.f;
	FVector Velocity = FVector::ZeroVector;
};
