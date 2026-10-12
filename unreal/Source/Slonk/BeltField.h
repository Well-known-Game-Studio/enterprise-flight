// Slonk - the asteroid belt as a local, deterministic rock field that materialises around the ship.
// At real scale the belt is far too sparse to ever see two rocks at once, so instead of placing every rock
// in the level, cells of space near the ship are filled from a hash of their coordinates whenever the ship is
// inside the belt's torus. Rocks are instanced with runtime-built meshes; the actor itself parks on the belt's
// mid-circle nearest the ship so it can be a nav target.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BeltField.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UMaterialInterface;

UCLASS()
class SLONK_API ABeltField : public AActor
{
	GENERATED_BODY()

public:
	ABeltField();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/** Mid-circle radius of the belt about the star (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Belt")
	double BeltRadiusCm = 4.04e13;

	/** Radial half-width of the torus (cm); rock density falls off as a gaussian of this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Belt")
	double BeltHalfWidthCm = 1.0e12;

	/** Vertical half-thickness (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Belt")
	double BeltHalfThicknessCm = 1.0e12;

	/** Edge of one field cell (cm). Cells within CellRange of the ship's cell are populated. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Belt")
	double CellSizeCm = 6.0e7;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Belt", meta = (ClampMin = "1", ClampMax = "4"))
	int32 CellRange = 2;

	/** Rocks in a cell at the belt's densest point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Belt", meta = (ClampMin = "0", ClampMax = "400"))
	int32 RocksPerCell = 45;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Belt")
	float MinRockRadiusCm = 2.0e4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Belt")
	float MaxRockRadiusCm = 5.0e5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Belt")
	int32 Seed = 7;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Belt")
	TSoftObjectPtr<UMaterialInterface> RockMaterial;

	/** The star the belt circles; the first AStarBody (or "Star"-tagged actor) if unset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Belt")
	TObjectPtr<AActor> Star;

	UFUNCTION(BlueprintPure, Category = "Belt")
	bool IsFieldActive() const { return bActive; }

	UFUNCTION(BlueprintPure, Category = "Belt")
	int32 GetRockCount() const { return RockCount; }

	/** Point on the belt's mid-circle nearest to a world location. */
	UFUNCTION(BlueprintPure, Category = "Belt")
	FVector NearestBeltPoint(const FVector& WorldLocation) const;

	FVector GetStarLocation() const;

private:
	void Rebuild(const FIntVector& Center, const FVector& StarLoc);
	void ClearField();
	AActor* FindStar();

	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> RockLayers;
	TWeakObjectPtr<AActor> CachedStar;
	FIntVector CenterCell = FIntVector(TNumericLimits<int32>::Max());
	bool bActive = false;
	int32 RockCount = 0;
	float UpdateTimer = 0.f;
	float StarTimer = 0.f;
};
