// Slonk - an instanced static mesh for the rocks and plants on a terrain chunk. The planet turns every frame and
// carries its chunks with it, and a moving instanced component works its bounds out afresh from every instance: a
// hundred thousand rocks were ten milliseconds of the game thread. This one boxes its instances once, in its own
// frame, when they are set, and moves the box. The engine's conservative bounds do the same but are only made when
// the instances reach the renderer, which a hidden chunk's pieces never do.

#pragma once

#include "CoreMinimal.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "ScatterInstancesComponent.generated.h"

UCLASS(ClassGroup = (Slonk))
class SLONK_API UScatterInstancesComponent : public UInstancedStaticMeshComponent
{
	GENERATED_BODY()

public:
	/** Replaces every instance (transforms relative to the component) and their custom data, and boxes them. */
	void SetPieces(const TArray<FTransform>& Transforms, TConstArrayView<float> CustomData);
	/** Removes every instance. */
	void ClearPieces();

	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;

private:
	FBox LocalBox = FBox(ForceInit);
};
