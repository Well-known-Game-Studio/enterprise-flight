// Slonk - the shuttlebay's clamshell door at the fantail, after the Enterprise-A's in The Final Frontier: the
// hull's own door, a half cone standing in the stern recess (8.3 m radius at its base ring under the deck, 2.8 m
// at its top), cut into eight gores that swing round its axis to nest against the outermost pair on each side,
// the innermost the furthest, leaving a tall slot that widens to most of the arch. The hull mesh keeps a hole where the door was (its stern material masks it), so the closed
// gores sit flush in it and the open ones show the bay inside.
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "BayDoors.generated.h"

class UStaticMeshComponent;

UCLASS(ClassGroup = (Slonk), meta = (BlueprintSpawnableComponent))
class SLONK_API UBayDoorsComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UBayDoorsComponent();

	/** Full travel, closed to open. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bay Doors") float OpenSeconds = 4.5f;
	/** Where the gore meshes SM_BayDoor1..8 live. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bay Doors") FString MeshFolder = TEXT("/Game/Slonk/Ship");
	/** The hull's painted stern material for the gores' outer skin (the unmasked one), used where a gore mesh has none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bay Doors") TSoftObjectPtr<UMaterialInterface> OuterMaterial;
	/** The hull's two-sided stern material for the stern wall SM_SternWall (closes the slot under the stern bulkhead beside the door), used where the mesh has none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bay Doors") TSoftObjectPtr<UMaterialInterface> WallMaterial;

	/** Loads the gores and hangs them on this component (whose origin is the door's axis on the deck). False without the meshes. */
	bool Build();
	void SetOpen(bool bOpen) { Target = bOpen ? 1.f : 0.f; }
	bool WantsOpen() const { return Target > 0.5f; }
	bool IsOpen() const { return Fraction >= 0.995f; }
	bool IsClosed() const { return Fraction <= 0.005f; }
	bool IsMoving() const { return !FMath::IsNearlyEqual(Fraction, Target, 0.004f); }
	float GetFraction() const { return Fraction; }
	void SetShown(bool bShown);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Gores;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Wall;
	float Fraction = 0.f;
	float Target = 0.f;
	void Apply();
};
