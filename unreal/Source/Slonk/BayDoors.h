// Slonk - the shuttlebay's clamshell door at the fantail, after the Enterprise-A's in The Final Frontier: eight
// gores of the hull's own door surface (a surface of revolution about a vertical axis at the bay's aft end) that
// swing round the axis to stack beside the opening, the innermost the furthest, leaving a tall slot that widens
// to the whole arch. The hull mesh keeps a hole where the door was (its stern material masks it), so the closed
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
	/** How far the innermost gore swings; each gore outward swings one gore's width less, so all stack beyond the edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bay Doors") float SwingDeg = 62.f;
	/** Where the gore meshes SM_BayDoor1..8 live. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bay Doors") FString MeshFolder = TEXT("/Game/Slonk/Ship");
	/** The hull's painted stern material for the gores' outer skin (the unmasked one), used where a gore mesh has none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bay Doors") TSoftObjectPtr<UMaterialInterface> OuterMaterial;

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
	float Fraction = 0.f;
	float Target = 0.f;
	void Apply();
};
