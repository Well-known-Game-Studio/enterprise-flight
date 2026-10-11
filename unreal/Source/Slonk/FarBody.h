// Slonk - bodies that may sit beyond the GPU's double-float precision horizon (planets, stars, belt rocks).

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "FarBody.generated.h"

UINTERFACE(MinimalAPI)
class UFarBody : public UInterface
{
	GENERATED_BODY()
};

/**
 * A primitive further than ~2^44 cm from the world origin cannot be expressed in the renderer's double-float
 * matrices and trips the engine's precision ensure the moment it has a scene proxy. Implementers switch their
 * primitives off (component visibility, not just the actor flag, so nothing exists even transiently during
 * world setup) when beyond SlonkMaterials::RenderHorizonCm and restore them when back within it.
 */
class IFarBody
{
	GENERATED_BODY()

public:
	virtual void SetBeyondHorizon(bool bBeyond) = 0;
	virtual bool IsBeyondHorizon() const = 0;

	/** Location to judge before registration: a loaded actor's world transform is not computed yet. */
	static FVector PreRegistrationLocation(const AActor* Actor);
	static bool IsLocationBeyondHorizon(const FVector& Loc);
};
