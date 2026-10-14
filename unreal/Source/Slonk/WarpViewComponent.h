// Slonk - pushes the warp post-process parameters once the frame's camera transforms are final.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WarpViewComponent.generated.h"

/**
 * The warp post-process synthesises sky and sun from the camera basis, so the basis has to be the one this
 * frame renders with. The spring arm applies its lag in its own post-physics tick and the camera manager
 * reads the camera components after every tick group, so this component ticks in TG_PostUpdateWork, after
 * the arm and cameras (the pawn adds them as prerequisites), and calls the pawn's UpdateWarpPostProcess.
 */
UCLASS(ClassGroup = (Starship))
class SLONK_API UWarpViewComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWarpViewComponent();
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
};
