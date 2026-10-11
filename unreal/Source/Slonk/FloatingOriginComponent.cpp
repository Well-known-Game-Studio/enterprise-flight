#include "FloatingOriginComponent.h"

#include "Engine/Brush.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "FarBody.h"

UFloatingOriginComponent::UFloatingOriginComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// After the ship has moved (TG_PrePhysics) but before the pawn's camera update (TG_PostPhysics) and the
	// player camera manager (which runs after TG_PostUpdateWork): rebasing later leaves the camera POV a frame behind.
	PrimaryComponentTick.TickGroup = TG_DuringPhysics;
}

AActor* UFloatingOriginComponent::GetFocusActor() const
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	return PC ? PC->GetPawn() : nullptr;
}

void UFloatingOriginComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!bEnabled)
	{
		return;
	}
	AActor* Focus = GetFocusActor();
	if (!Focus)
	{
		return;
	}
	const FVector FocusLoc = Focus->GetActorLocation();
	if (FocusLoc.SizeSquared() > RebaseDistance * RebaseDistance)
	{
		ShiftWorld(-FocusLoc);
	}
}

void UFloatingOriginComponent::ShiftWorld(const FVector& Offset)
{
	UWorld* World = GetWorld();
	if (!World || Offset.IsNearlyZero())
	{
		return;
	}
	ABrush* DefaultBrush = World->GetDefaultBrush();

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor) || Actor == DefaultBrush || Actor->IsA<AWorldSettings>() || Actor->IsA<APlayerStart>() || Actor->IsA<ADirectionalLight>() || Actor->IsA<ASkyLight>())
		{
			continue;
		}
		// Children follow their parent.
		if (Actor->GetAttachParentActor() != nullptr)
		{
			continue;
		}
		USceneComponent* Root = Actor->GetRootComponent();
		if (!Root)
		{
			continue;
		}
		if (Root->Mobility != EComponentMobility::Movable)
		{
			Root->SetMobility(EComponentMobility::Movable);
		}
		// A body that crosses the GPU precision horizon must be switched off *before* its transform update is
		// queued, or the renderer still builds a matrix for it this frame and trips the precision ensure.
		TArray<AActor*> Bodies;
		if (Cast<IFarBody>(Actor)) { Bodies.Add(Actor); }
		TArray<AActor*> Children;
		Actor->GetAttachedActors(Children, true, true);
		for (AActor* Child : Children)
		{
			if (Child && Cast<IFarBody>(Child)) { Bodies.Add(Child); }
		}
		for (AActor* Body : Bodies)
		{
			IFarBody* Far = Cast<IFarBody>(Body);
			if (!Far->IsBeyondHorizon() && IFarBody::IsLocationBeyondHorizon(Body->GetActorLocation() + Offset))
			{
				Far->SetBeyondHorizon(true);
			}
		}
		Actor->SetActorLocation(Root->GetComponentLocation() + Offset, false, nullptr, ETeleportType::TeleportPhysics);
		for (AActor* Body : Bodies)
		{
			IFarBody* Far = Cast<IFarBody>(Body);
			const bool bBeyond = IFarBody::IsLocationBeyondHorizon(Body->GetActorLocation());
			if (Far->IsBeyondHorizon() != bBeyond)
			{
				Far->SetBeyondHorizon(bBeyond);
			}
		}
	}

	OriginUniversePosition -= Offset;
	++RebaseCount;
}
