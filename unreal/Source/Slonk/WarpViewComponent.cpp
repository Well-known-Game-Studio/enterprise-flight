#include "WarpViewComponent.h"
#include "StarshipPawn.h"

UWarpViewComponent::UWarpViewComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UWarpViewComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (AStarshipPawn* Pawn = Cast<AStarshipPawn>(GetOwner()))
	{
		Pawn->UpdateWarpPostProcess();
	}
}
