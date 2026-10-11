#include "FarBody.h"

#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "SlonkMaterials.h"

FVector IFarBody::PreRegistrationLocation(const AActor* Actor)
{
	if (!Actor)
	{
		return FVector::ZeroVector;
	}
	const USceneComponent* Root = Actor->GetRootComponent();
	if (!Root)
	{
		return Actor->GetActorLocation();
	}
	FVector Loc = Root->GetRelativeLocation();
	if (const USceneComponent* Parent = Root->GetAttachParent())
	{
		if (Parent->IsRegistered())
		{
			Loc += Parent->GetComponentLocation();
		}
	}
	return Loc;
}

bool IFarBody::IsLocationBeyondHorizon(const FVector& Loc)
{
	return Loc.Size() > SlonkMaterials::RenderHorizonCm;
}
